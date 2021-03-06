/* === This file is part of Tomahawk Player - <http://tomahawk-player.org> ===
 * 
 *   Copyright 2010-2011, Christian Muehlhaeuser <muesli@tomahawk-player.org>
 *
 *   Tomahawk is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   Tomahawk is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with Tomahawk. If not, see <http://www.gnu.org/licenses/>.
 */

#include "jabber_p.h"

#include <QDebug>
#include <QTime>
#include <QTimer>
#include <QString>
#include <QRegExp>
#include <QThread>
#include <utils/tomahawkutils.h>

#include <jreen/abstractroster.h>
#include <jreen/capabilities.h>


//remove
#include <QMessageBox>
#include <jreen/connection.h>

using namespace std;


#define TOMAHAWK_CAP_NODE_NAME QLatin1String("http://tomahawk-player.org/")

Jabber_p::Jabber_p( const QString& jid, const QString& password, const QString& server, const int port )
    : QObject()
    , m_server()
{
    qDebug() << Q_FUNC_INFO;
    //qsrand( QTime( 0, 0, 0 ).secsTo( QTime::currentTime() ) );
    qsrand(QDateTime::currentDateTime().toTime_t());

    m_presences[jreen::Presence::Available] = "available";
    m_presences[jreen::Presence::Chat] = "chat";
    m_presences[jreen::Presence::Away] = "away";
    m_presences[jreen::Presence::DND] = "dnd";
    m_presences[jreen::Presence::XA] = "xa";
    m_presences[jreen::Presence::Unavailable] = "unavailable";
    m_presences[jreen::Presence::Probe] = "probe";
    m_presences[jreen::Presence::Error] = "error";
    m_presences[jreen::Presence::Invalid] = "invalid";

    m_jid = jreen::JID( jid );

    m_client = new jreen::Client( jid, password );
    m_client->setResource( QString( "tomahawk%1" ).arg( "DOMME" ) );

    jreen::Capabilities::Ptr caps = m_client->presence().findExtension<jreen::Capabilities>();
    caps->setNode(TOMAHAWK_CAP_NODE_NAME);

    qDebug() << "Our JID set to:" << m_client->jid().full();
    qDebug() << "Our Server set to:" << m_client->server();
    qDebug() << "Our Port set to" << m_client->port();

    connect(m_client->connection(), SIGNAL(error(SocketError)), SLOT(onError(SocketError)));
    connect(m_client, SIGNAL(serverFeaturesReceived(QSet<QString>)), SLOT(onConnect()));
    connect(m_client, SIGNAL(disconnected(jreen::Client::DisconnectReason)), SLOT(onDisconnect(jreen::Client::DisconnectReason)));
    connect(m_client, SIGNAL(destroyed(QObject*)), this, SLOT(onDestroy()));
    connect(m_client, SIGNAL(newMessage(jreen::Message)), SLOT(onNewMessage(jreen::Message)));
    connect(m_client, SIGNAL(newPresence(jreen::Presence)), SLOT(onNewPresence(jreen::Presence)));

    qDebug() << "Connecting to the XMPP server...";
    m_client->connectToServer();
}


Jabber_p::~Jabber_p()
{
    delete m_client;
}

void
Jabber_p::setProxy( QNetworkProxy* proxy )
{
    qDebug() << Q_FUNC_INFO << "NOT IMPLEMENTED";
}

void
Jabber_p::disconnect()
{
    if ( m_client )
    {
        m_client->disconnect();
    }
}


void
Jabber_p::sendMsg( const QString& to, const QString& msg )
{
    qDebug() << Q_FUNC_INFO;
    if ( QThread::currentThread() != thread() )
    {
        qDebug() << Q_FUNC_INFO << "invoking in correct thread, not"
                 << QThread::currentThread();

        QMetaObject::invokeMethod( this, "sendMsg",
                                   Qt::QueuedConnection,
                                   Q_ARG( const QString, to ),
                                   Q_ARG( const QString, msg )
                                 );
        return;
    }

    if ( !m_client ) {
        return;
    }

    qDebug() << Q_FUNC_INFO << to << msg;
    jreen::Message m( jreen::Message::Chat, jreen::JID(to), msg);

    m_client->send( m ); // assuming this is threadsafe
}


void
Jabber_p::broadcastMsg( const QString &msg )
{
    qDebug() << Q_FUNC_INFO;
    if ( QThread::currentThread() != thread() )
    {
        QMetaObject::invokeMethod( this, "broadcastMsg",
                                   Qt::QueuedConnection,
                                   Q_ARG(const QString, msg)
                                 );
        return;
    }

    if ( !m_client )
        return;

    foreach( const QString& jidstr, m_peers.keys() )
    {
        qDebug() << "Broadcasting to" << jidstr <<"...";
        jreen::Message m(jreen::Message::Chat, jreen::JID(jidstr), msg, "");
        m_client->send( m );
    }
}


void
Jabber_p::addContact( const QString& jid, const QString& msg )
{
    if ( QThread::currentThread() != thread() )
    {
        QMetaObject::invokeMethod( this, "addContact",
                                   Qt::QueuedConnection,
                                   Q_ARG(const QString, jid),
                                   Q_ARG(const QString, msg)
                                 );
        return;
    }

    // Add contact to the Tomahawk group on the roster
    m_roster->add( jid, jid, QStringList() << "Tomahawk" );

    return;
}

void
Jabber_p::onConnect()
{
    qDebug() << Q_FUNC_INFO;

    // update jid resource, servers like gtalk use resource binding and may
    // have changed our requested /resource
    if ( m_client->jid().resource() != m_jid.resource() )
    {
        m_jid.setResource( m_client->jid().resource() );
        QString jidstr( m_jid.full() );
        emit jidChanged( jidstr );
    }

    emit connected();
    qDebug() << "Connected as:" << m_jid.full();

    m_client->setPresence(jreen::Presence::Available, "Tomahawk-JREEN available", 1);
    m_client->disco()->setSoftwareVersion( "Tomahawk JREEN", "0.0.0.0", "Foobar" );
    m_client->setPingInterval(60000);
    m_roster = new jreen::SimpleRoster( m_client );
    m_roster->load();

    // join MUC with bare jid as nickname
    //TODO: make the room a list of rooms and make that configurable
    QString bare(m_jid.bare());
    m_room = new jreen::MUCRoom(m_client, jreen::JID(QString("tomahawk@conference.qutim.org/").append(bare.replace("@", "-"))));
    m_room->setHistorySeconds(0);
    m_room->join();

    // treat muc participiants like contacts
    connect(m_room, SIGNAL(messageReceived(jreen::Message, bool)), this, SLOT(onNewMessage(jreen::Message)));
    connect(m_room, SIGNAL(presenceReceived(jreen::Presence,const jreen::MUCRoom::Participant*)), this, SLOT(onNewPresence(jreen::Presence)));
}


void
Jabber_p::onDisconnect( jreen::Client::DisconnectReason reason )
{
    QString error;
    bool reconnect = false;
    int reconnectInSeconds = 0;

    switch( reason )
    {
        case jreen::Client::User:
            error = "User Interaction";
            break;
        case jreen::Client::HostUnknown:
            error = "Host is unknown";
            break;
        case jreen::Client::ItemNotFound:
            error = "Item not found";
            break;
        case jreen::Client::AuthorizationError:
            error = "Authorization Error";
            break;
        case jreen::Client::RemoteStreamError:
            error = "Remote Stream Error";
            reconnect = true;
            break;
        case jreen::Client::RemoteConnectionFailed:
            error = "Remote Connection failed";
            break;
        case jreen::Client::InternalServerError:
            error = "Internal Server Error";
            reconnect = true;
            break;
        case jreen::Client::SystemShutdown:
            error = "System shutdown";
            reconnect = true;
            reconnectInSeconds = 60;
            break;
        case jreen::Client::Conflict:
            error = "Conflict";
            break;

        case jreen::Client::Unknown:
            error = "Unknown";
            break;

        default:
            qDebug() << "Not all Client::DisconnectReasons checked";
            Q_ASSERT(false);
            break;
    }

    qDebug() << "Disconnected from server:" << error;

    if(reconnect)
        QTimer::singleShot(reconnectInSeconds*1000, m_client, SLOT(connectToServer()));

    emit disconnected();
}

void
Jabber_p::onNewMessage( const jreen::Message& m )
{
    QString from = m.from().full();
    QString msg = m.body();

    if ( msg.isEmpty() )
        return;

    qDebug() << Q_FUNC_INFO << m.from().full() << ":" << m.body();
    emit msgReceived( from, msg );
}


void Jabber_p::onNewPresence( const jreen::Presence& presence)
{

    jreen::JID jid = presence.from();
    QString fulljid( jid.full() );

    qDebug() << Q_FUNC_INFO << "handle presence" << fulljid << presence.subtype();

    if( jid == m_jid )
        return;

    if ( presence.error() ) {
        qDebug() << Q_FUNC_INFO << "presence error: no tomahawk";
        return;
    }

    // ignore anyone not running tomahawk:
    jreen::Capabilities::Ptr caps = presence.findExtension<jreen::Capabilities>();
    if ( caps && (caps->node() == TOMAHAWK_CAP_NODE_NAME ))
    {
        qDebug() << Q_FUNC_INFO << presence.from().full() << "tomahawk detected by caps";
    }
    // this is a hack actually as long as gloox based libsip_jabber is around
    // remove this as soon as everyone is using jreen
    else if( presence.from().resource().startsWith( QLatin1String("tomahawk") ) )
    {
        qDebug() << Q_FUNC_INFO << presence.from().full() << "tomahawk detected by resource";
    }
    else if( caps && caps->node() != TOMAHAWK_CAP_NODE_NAME )
    {
        qDebug() << Q_FUNC_INFO << presence.from().full() << "*no tomahawk* detected by caps!" << caps->node() << presence.from().resource();
        return;
    }
    else if( !caps )
    {
        qDebug() << Q_FUNC_INFO << "no tomahawk detected by resource and !caps";
        return;
    }

    qDebug() << Q_FUNC_INFO << fulljid << " is a tomahawk resource.";

    // "going offline" event
    if ( !presenceMeansOnline( presence.subtype() ) &&
         ( !m_peers.contains( fulljid ) ||
           presenceMeansOnline( m_peers.value( fulljid ) )
         )
       )
    {
        m_peers[ fulljid ] = presence.subtype();
        qDebug() << Q_FUNC_INFO << "* Peer goes offline:" << fulljid;
        emit peerOffline( fulljid );
        return;
    }

    // "coming online" event
    if( presenceMeansOnline( presence.subtype() ) &&
        ( !m_peers.contains( fulljid ) ||
          !presenceMeansOnline( m_peers.value( fulljid ) )
        )
       )
    {
        m_peers[ fulljid ] = presence.subtype();
        qDebug() << Q_FUNC_INFO << "* Peer goes online:" << fulljid;
        emit peerOnline( fulljid );
        return;
    }

    //qDebug() << "Updating presence data for" << fulljid;
    m_peers[ fulljid ] = presence.subtype();

}

bool
Jabber_p::presenceMeansOnline( jreen::Presence::Type p )
{
    switch(p)
    {
        case jreen::Presence::Invalid:
        case jreen::Presence::Unavailable:
        case jreen::Presence::Error:
            return false;
            break;
        default:
            return true;
    }
}
