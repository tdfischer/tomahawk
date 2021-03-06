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
#include <QVariant>
#include <utils/tomahawkutils.h>
#include <gloox/capabilities.h>
#include <QMessageBox>
#include <qjson/parser.h>
#include <tomahawksettings.h>

#define TOMAHAWK_CAP_NODE_NAME QString::fromAscii("http://tomahawk-player.org/")

Jabber_p::Jabber_p( const QString& jid, const QString& password, const QString& server, const int port )
    : QObject()
    , m_server()
{
    qDebug() << Q_FUNC_INFO;
    qsrand( QTime( 0, 0, 0 ).secsTo( QTime::currentTime() ) );

    m_presences[gloox::Presence::Available] = "available";
    m_presences[gloox::Presence::Chat] = "chat";
    m_presences[gloox::Presence::Away] = "away";
    m_presences[gloox::Presence::DND] = "dnd";
    m_presences[gloox::Presence::XA] = "xa";
    m_presences[gloox::Presence::Unavailable] = "unavailable";
    m_presences[gloox::Presence::Probe] = "probe";
    m_presences[gloox::Presence::Error] = "error";
    m_presences[gloox::Presence::Invalid] = "invalid";

    m_jid = gloox::JID( jid.toStdString() );

    if( m_jid.resource().find( "tomahawk" ) == std::string::npos )
    {
        qDebug() << "!!! Setting your resource to 'tomahawk' prior to logging in to jabber";
        m_jid.setResource( QString( "tomahawk%1" ).arg( qrand() ).toStdString() );
    }

    qDebug() << "Our JID set to:" << m_jid.full().c_str();

    m_client = QSharedPointer<gloox::Client>( new gloox::Client( m_jid, password.toStdString(), port ) );
    const gloox::Capabilities *caps = m_client->presence().capabilities();
    if( caps )
    {
        const_cast<gloox::Capabilities*>(caps)->setNode(TOMAHAWK_CAP_NODE_NAME.toStdString());
    }
    m_server = server;
    
    qDebug() << "proxy type is " << TomahawkUtils::proxy()->type();
    
    setProxy( TomahawkUtils::proxy() );
}


Jabber_p::~Jabber_p()
{
   // qDebug() << Q_FUNC_INFO;
    if ( !m_client.isNull() )
    {
       // m_client->disco()->removeDiscoHandler( this );
        m_client->rosterManager()->removeRosterListener();
        m_client->removeConnectionListener( this );
    }
}

void
Jabber_p::resolveHostSRV()
{
    qDebug() << Q_FUNC_INFO;
    if( m_server.isEmpty() )
    {
        qDebug() << "No server found!";
        return;
    }
    if( TomahawkUtils::proxy()->type() == QNetworkProxy::Socks5Proxy ||
        ( TomahawkUtils::proxy()->type() == QNetworkProxy::DefaultProxy &&
          QNetworkProxy::applicationProxy().type() == QNetworkProxy::Socks5Proxy ) )
    {
        if( TomahawkSettings::instance()->jabberServer().isEmpty() )
        {
           qDebug() << "Right now, you must explicitly set your jabber server if you are using a proxy, due to a bug in the DNS lookup library";
           m_server = QString();
        }
        QMetaObject::invokeMethod( this, "go", Qt::QueuedConnection );
        return;
    }
            
            
    TomahawkUtils::DNSResolver *resolver = TomahawkUtils::dnsResolver();
    connect( resolver, SIGNAL(result(QString &)), SLOT(resolveResult(QString &)) );
    qDebug() << "Resolving SRV record of " << m_server;
    resolver->resolve( m_server, "SRV" );
}

void
Jabber_p::setProxy( QNetworkProxy* proxy )
{
    qDebug() << Q_FUNC_INFO;

    if( m_client.isNull() || !proxy )
    {
        qDebug() << "No client or no proxy";
        return;
    }

    QNetworkProxy appProx = QNetworkProxy::applicationProxy();
    QNetworkProxy* p = proxy->type() == QNetworkProxy::DefaultProxy ? &appProx : proxy;

    if( p->type() == QNetworkProxy::NoProxy )
    {
        qDebug() << "Setting proxy to none";
        m_client->setConnectionImpl( new gloox::ConnectionTCPClient( m_client.data(), m_client->logInstance(), m_client->server(), m_client->port() ) );
    }
    else if( proxy->type() == QNetworkProxy::Socks5Proxy )
    {
        qDebug() << "Setting proxy to SOCKS5";
        qDebug() << "proxy host = " << proxy->hostName();
        qDebug() << "proxy port = " << proxy->port();
        m_client->setConnectionImpl( new gloox::ConnectionSOCKS5Proxy( m_client.data(),
                                     new gloox::ConnectionTCPClient( m_client->logInstance(), proxy->hostName().toStdString(), proxy->port() ),
                                     m_client->logInstance(), m_client->server(), m_client->port() ) );
    }
    else
    {
        qDebug() << "Proxy type unknown";
    }
}

void
Jabber_p::resolveResult( QString& result )
{
    if ( result != "NONE" )
        m_server = result;
    qDebug() << "Final host name for XMPP server set to " << m_server;
    QMetaObject::invokeMethod( this, "go", Qt::QueuedConnection );
}

void
Jabber_p::go()
{
    qDebug() << Q_FUNC_INFO;
    if( !m_server.isEmpty() )
        m_client->setServer( m_server.toStdString() );
    else
    {
        qDebug() << "No server found!";
        return;
    }

    m_client->registerPresenceHandler( this );
    m_client->registerConnectionListener( this );
    m_client->rosterManager()->registerRosterListener( this, false ); // false means async
    m_client->logInstance().registerLogHandler( gloox::LogLevelWarning, gloox::LogAreaAll, this );
    m_client->registerMessageHandler( this );

    /*
    m_client->disco()->registerDiscoHandler( this );
    m_client->disco()->setVersion( "gloox_tomahawk", GLOOX_VERSION, "xplatform" );
    m_client->disco()->setIdentity( "client", "bot" );
    m_client->disco()->addFeature( "tomahawk:player" );
    */
    
    m_client->setPresence( gloox::Presence::XA, -127, "Tomahawk available" );

    // m_client->connect();
    // return;

    // Handle proxy
    
    qDebug() << "Connecting to the XMPP server...";
    if( m_client->connect( false ) )
    {
        int sock = static_cast<gloox::ConnectionTCPClient*>( m_client->connectionImpl() )->socket();
        m_notifier.reset( new QSocketNotifier( sock, QSocketNotifier::Read ) );
        connect( m_notifier.data(), SIGNAL( activated(int) ), SLOT( doJabberRecv() ));
    }
    else
        qDebug() << "Could not connect to the XMPP server!";
}


void
Jabber_p::doJabberRecv()
{
    if ( m_client.isNull() )
        return;

    gloox::ConnectionError ce = m_client->recv( 100 );
    if ( ce != gloox::ConnNoError )
    {
        qDebug() << "Jabber_p::Recv failed, disconnected";
    }
}


void
Jabber_p::disconnect()
{
    if ( !m_client.isNull() )
    {
        m_client->disconnect();
    }
}


void
Jabber_p::sendMsg( const QString& to, const QString& msg )
{
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

    if ( m_client.isNull() )
        return;

    qDebug() << Q_FUNC_INFO << to << msg;
    gloox::Message m( gloox::Message::Chat, gloox::JID(to.toStdString()), msg.toStdString(), "" );

    m_client->send( m ); // assuming this is threadsafe
}


void
Jabber_p::broadcastMsg( const QString &msg )
{
    if ( QThread::currentThread() != thread() )
    {
        QMetaObject::invokeMethod( this, "broadcastMsg",
                                   Qt::QueuedConnection,
                                   Q_ARG(const QString, msg)
                                 );
        return;
    }

    if ( m_client.isNull() )
        return;

    std::string msg_s = msg.toStdString();
    foreach( const QString& jidstr, m_peers.keys() )
    {
        gloox::Message m(gloox::Message::Chat, gloox::JID(jidstr.toStdString()), msg_s, "");
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

    handleSubscription(gloox::JID(jid.toStdString()), msg.toStdString());
    return;
}

/// GLOOX IMPL STUFF FOLLOWS

void
Jabber_p::onConnect()
{
    qDebug() << "Connected to the XMPP server";
    // update jid resource, servers like gtalk use resource binding and may
    // have changed our requested /resource
    if ( m_client->resource() != m_jid.resource() )
    {
        m_jid.setResource( m_client->resource() );
        QString jidstr( m_jid.full().c_str() );
        emit jidChanged( jidstr );
    }

    qDebug() << "Connected as:" << m_jid.full().c_str();
    emit connected();
}


void
Jabber_p::onDisconnect( gloox::ConnectionError e )
{
    qDebug() << "Jabber Disconnected";
    QString error;
    bool triggeredDisconnect = false;

    switch( e )
    {
    case gloox::AuthErrorUndefined:
        error = " No error occurred, or error condition is unknown";
        break;

    case gloox::SaslAborted:
        error = "The receiving entity acknowledges an &lt;abort/&gt; element sent "
                "by the initiating entity; sent in reply to the &lt;abort/&gt; element.";
        break;

    case gloox::SaslIncorrectEncoding:
        error = "The data provided by the initiating entity could not be processed "
                "because the [BASE64] encoding is incorrect (e.g., because the encoding "
                "does not adhere to the definition in Section 3 of [BASE64]); sent in "
                "reply to a &lt;response/&gt; element or an &lt;auth/&gt; element with "
                "initial response data.";
        break;

    case gloox::SaslInvalidAuthzid:
        error = "The authzid provided by the initiating entity is invalid, either "
                "because it is incorrectly formatted or because the initiating entity "
                "does not have permissions to authorize that ID; sent in reply to a "
                "&lt;response/&gt; element or an &lt;auth/&gt; element with initial "
                "response data.";
        break;

    case gloox::SaslInvalidMechanism:
        error = "The initiating entity did not provide a mechanism or requested a "
                "mechanism that is not supported by the receiving entity; sent in reply "
                "to an &lt;auth/&gt; element.";
        break;

    case gloox::SaslMalformedRequest:
        error = "The request is malformed (e.g., the &lt;auth/&gt; element includes "
                "an initial response but the mechanism does not allow that); sent in "
                "reply to an &lt;abort/&gt;, &lt;auth/&gt;, &lt;challenge/&gt;, or "
                "&lt;response/&gt; element.";
        break;

    case gloox::SaslMechanismTooWeak:
        error = "The mechanism requested by the initiating entity is weaker than "
                "server policy permits for that initiating entity; sent in reply to a "
                "&lt;response/&gt; element or an &lt;auth/&gt; element with initial "
                "response data.";
        break;

    case gloox::SaslNotAuthorized:
        error = "The authentication failed because the initiating entity did not "
                "provide valid credentials (this includes but is not limited to the "
                "case of an unknown username); sent in reply to a &lt;response/&gt; "
                "element or an &lt;auth/&gt; element with initial response data. ";
        break;

    case gloox::SaslTemporaryAuthFailure:
        error = "The authentication failed because of a temporary error condition "
                "within the receiving entity; sent in reply to an &lt;auth/&gt; element "
                "or &lt;response/&gt; element.";
        break;

    case gloox::NonSaslConflict:
        error = "XEP-0078: Resource Conflict";
        break;

    case gloox::NonSaslNotAcceptable:
        error = "XEP-0078: Required Information Not Provided";
        break;

    case gloox::NonSaslNotAuthorized:
        error = "XEP-0078: Incorrect Credentials";
        break;

    case gloox::ConnAuthenticationFailed:
        error = "Authentication failed";
        break;

    case gloox::ConnNoSupportedAuth:
        error = "No supported auth mechanism";
        break;

    default :
        error = "UNKNOWN ERROR";
        triggeredDisconnect = true;
    }

    qDebug() << "Connection error msg:" << error;

    // Assume that an unknown error is due to a disconnect triggered by the user
    if( !triggeredDisconnect )
        emit authError( e, error ); // trigger reconnect
    emit disconnected();


}


bool
Jabber_p::onTLSConnect( const gloox::CertInfo& info )
{
    qDebug()    << Q_FUNC_INFO
                << "Status" << info.status
                << "issuer" << info.issuer.c_str()
                << "peer"   << info.server.c_str()
                << "proto"  << info.protocol.c_str()
                << "mac"    << info.mac.c_str()
                << "cipher" << info.cipher.c_str()
                << "compression" << info.compression.c_str()
                << "from"   << std::ctime( (const time_t*)&info.date_from )
                << "to"     << std::ctime( (const time_t*)&info.date_to )
                ;

    //onConnect();
    return true;
}


void
Jabber_p::handleMessage( const gloox::Message& m, gloox::MessageSession * /*session*/ )
{
    QString from = QString::fromStdString( m.from().full() );
    QString msg = QString::fromStdString( m.body() );

    if ( !msg.length() )
        return;

    qDebug() << "Jabber_p::handleMessage" << from << msg;

    //printf( "from: %s, type: %d, subject: %s, message: %s, thread id: %s\n",
    //        msg.from().full().c_str(), msg.subtype(),
    //        msg.subject().c_str(), msg.body().c_str(), msg.thread().c_str() );

    //sendMsg( from, QString("You said %1").arg(msg) );

    QJson::Parser parser;
    bool ok;
    QVariant v = parser.parse( msg.toAscii(), &ok );
    if ( !ok  || v.type() != QVariant::Map )
    {
        sendMsg( from, QString( "I'm sorry -- I'm just an automatic presence used by Tomahawk Player (http://gettomahawk.com). If you are getting this message, the person you are trying to reach is probably not signed on, so please try again later!" ) );
        return;
    }
    
    emit msgReceived( from, msg );
}


void
Jabber_p::handleLog( gloox::LogLevel level, gloox::LogArea area, const std::string& message )
{
    qDebug() << Q_FUNC_INFO
             << "level:" << level
             << "area:" << area
             << "msg:" << message.c_str();
}


/// ROSTER STUFF
// {{{
void
Jabber_p::onResourceBindError( gloox::ResourceBindError error )
{
    qDebug() << Q_FUNC_INFO;
}


void
Jabber_p::onSessionCreateError( gloox::SessionCreateError error )
{
    qDebug() << Q_FUNC_INFO;
}


void
Jabber_p::handleItemSubscribed( const gloox::JID& jid )
{
    qDebug() << Q_FUNC_INFO << jid.full().c_str();
}


void
Jabber_p::handleItemAdded( const gloox::JID& jid )
{
    qDebug() << Q_FUNC_INFO << jid.full().c_str();
}


void
Jabber_p::handleItemUnsubscribed( const gloox::JID& jid )
{
    qDebug() << Q_FUNC_INFO << jid.full().c_str();
}


void
Jabber_p::handleItemRemoved( const gloox::JID& jid )
{
    qDebug() << Q_FUNC_INFO << jid.full().c_str();
}


void
Jabber_p::handleItemUpdated( const gloox::JID& jid )
{
    qDebug() << Q_FUNC_INFO << jid.full().c_str();
}
// }}}


void
Jabber_p::handleRoster( const gloox::Roster& roster )
{
//    qDebug() << Q_FUNC_INFO;

gloox::Roster::const_iterator it = roster.begin();
    for ( ; it != roster.end(); ++it )
    {
        if ( (*it).second->subscription() != gloox::S10nBoth ) continue;
        qDebug() << (*it).second->jid().c_str() << (*it).second->name().c_str();
        //printf("JID: %s\n", (*it).second->jid().c_str());
    }

    // mark ourselves as "extended away" lowest priority:
    // there is no "invisible" in the spec. XA is the lowest?
    //m_client->setPresence( Presence::Available, 1, "Tomahawk App, not human" );
}


void
Jabber_p::handleRosterError( const gloox::IQ& /*iq*/ )
{
    qDebug() << Q_FUNC_INFO;
}


void
Jabber_p::handlePresence( const gloox::Presence& presence )
{
    gloox::JID jid = presence.from();
    QString fulljid( jid.full().c_str() );

    qDebug() << "* handleRosterPresence" << fulljid << presence.subtype();

    if( jid == m_jid )
        return;

    // ignore anyone not running tomahawk:
    // convert to QString to get proper regex support
    QString node;
    const gloox::Capabilities *caps = presence.findExtension<gloox::Capabilities>( gloox::ExtCaps );
    if( caps )
        QString node = QString::fromAscii( caps->node().c_str() );

    if( !QString::fromAscii( jid.resource().c_str() ).startsWith( QLatin1String( "tomahawk" ) )
        && !( node == TOMAHAWK_CAP_NODE_NAME ) )
    {
        //qDebug() << "not considering resource of" << res;
        // Disco them to check if they are tomahawk-capable

        //qDebug() << "No tomahawk resource, DISCOing..." << jid.full().c_str();
        //m_client->disco()->getDiscoInfo( jid, "", this, 0 );
        return;
    }

    //qDebug() << "handling presence for resource of" << res;

    //qDebug() << Q_FUNC_INFO << "jid:" << QString::fromStdString(item.jid())
    //        << " resource:" << QString::fromStdString(resource)
    //        << " presencetype" << presence;

    // "going offline" event
    if ( !presenceMeansOnline( presence.subtype() ) &&
         ( !m_peers.contains( fulljid ) ||
           presenceMeansOnline( m_peers.value( fulljid ) ) ) )
    {
        m_peers[ fulljid ] = presence.subtype();
        qDebug() << "* Peer goes offline:" << fulljid;
        emit peerOffline( fulljid );
        return;
    }

    // "coming online" event
    if ( presenceMeansOnline( presence.subtype() ) &&
         ( !m_peers.contains( fulljid ) ||
           !presenceMeansOnline( m_peers.value( fulljid ) ) ) )
    {
        m_peers[ fulljid ] = presence.subtype();
        qDebug() << "* Peer goes online:" << fulljid;
        emit peerOnline( fulljid );
        return;
    }

    //qDebug() << "Updating presence data for" << fulljid;
    m_peers[ fulljid ] = presence.subtype();
}


bool
Jabber_p::handleSubscription( const gloox::JID& jid, const std::string& /*msg*/ )
{
    qDebug() << Q_FUNC_INFO << jid.bare().c_str();

    gloox::StringList groups;
    groups.push_back( "Tomahawk" );
    m_client->rosterManager()->subscribe( jid, "", groups, "" );
    return true;
}


bool
Jabber_p::handleSubscriptionRequest( const gloox::JID& jid, const std::string& /*msg*/ )
{
    qDebug() << Q_FUNC_INFO << jid.bare().c_str();

    // check if the requester is already on the roster
    gloox::RosterItem *item = m_client->rosterManager()->getRosterItem(jid);
    if(item) qDebug() << Q_FUNC_INFO << jid.bare().c_str() << "subscription status:" << static_cast<int>( item->subscription() );
    if(item &&
        (
            item->subscription() == gloox::S10nNoneOut ||    // Contact and user are not subscribed to each other, and user has sent contact a subscription request but contact has not replied yet.
            item->subscription() == gloox::S10nTo ||         // User is subscribed to contact (one-way).
            item->subscription() == gloox::S10nToIn          // User is subscribed to contact, and contact has sent user a subscription request but user has not replied yet.
        )
    )
    {
        qDebug() << Q_FUNC_INFO << jid.bare().c_str() << "already on the roster so we assume ack'ing subscription request is okay...";

        // ack the request
        m_client->rosterManager()->ackSubscriptionRequest( jid, true );

        // return anything, the result is ignored
        return false;
    }

    // we don't have to check for an already open check box because gloox doesnt call this method until a former request was ack'ed
    qDebug() << Q_FUNC_INFO << jid.bare().c_str() << "open subscription request box";

    // preparing the confirm box for the user
    QMessageBox *confirmBox = new QMessageBox(
                                QMessageBox::Question,
                                tr( "Authorize User" ),
                                QString( tr( "Do you want to grant <b>%1</b> access to your Collection?" ) ).arg( QLatin1String( jid.bare().c_str() ) ),
                                QMessageBox::Yes | QMessageBox::No,
                                0
                              );

    // add confirmBox to m_subscriptionConfirmBoxes
    m_subscriptionConfirmBoxes.insert( jid, confirmBox );

    // display the box and wait for the answer
    confirmBox->open( this, SLOT( onSubscriptionRequestConfirmed( int ) ) );

    return false;
}


void
Jabber_p::onSubscriptionRequestConfirmed( int result )
{
    qDebug() << Q_FUNC_INFO << result;

    QList< QMessageBox* > confirmBoxes = m_subscriptionConfirmBoxes.values();
    gloox::JID jid;

    foreach( QMessageBox* currentBox, confirmBoxes )
    {
        if( currentBox == sender() )
        {
            jid = m_subscriptionConfirmBoxes.key( currentBox );
        }
    }

    qDebug() << Q_FUNC_INFO << "box confirmed for" << jid.bare().c_str();

    // we got an answer, deleting the box
    m_subscriptionConfirmBoxes.remove( jid );
    sender()->deleteLater();

    QMessageBox::StandardButton allowSubscription = static_cast<QMessageBox::StandardButton>( result );

    if ( allowSubscription == QMessageBox::Yes )
    {
        qDebug() << Q_FUNC_INFO << jid.bare().c_str() << "accepted by user, adding to roster";
        gloox::StringList groups;
        groups.push_back( "Tomahawk" );
        m_client->rosterManager()->subscribe( jid, "", groups, "" );
    }
    else
    {
        qDebug() << Q_FUNC_INFO << jid.bare().c_str() << "declined by user";
    }

    m_client->rosterManager()->ackSubscriptionRequest( jid, allowSubscription == QMessageBox::Yes );
}


bool
Jabber_p::handleUnsubscriptionRequest( const gloox::JID& jid, const std::string& /*msg*/ )
{
    qDebug() << Q_FUNC_INFO << jid.bare().c_str();
    return true;
}


void
Jabber_p::handleNonrosterPresence( const gloox::Presence& presence )
{
    qDebug() << Q_FUNC_INFO << presence.from().full().c_str();
}
/// END ROSTER STUFF


void
Jabber_p::handleVCard( const gloox::JID& jid, const gloox::VCard* vcard )
{
    qDebug() << "VCARD RECEIVED!" << jid.bare().c_str();
}


void
Jabber_p::handleVCardResult( gloox::VCardHandler::VCardContext context, const gloox::JID& jid, gloox::StanzaError se )
{
    qDebug() << "VCARD RESULT RECEIVED!" << jid.bare().c_str();
}


/// DISCO STUFF
void
Jabber_p::handleDiscoInfo( const gloox::JID& from, const gloox::Disco::Info& info, int context )
{
    QString jidstr( from.full().c_str() );
    //qDebug() << "DISCOinfo" << jidstr;
    if ( info.hasFeature("tomahawk:player") )
    {
        qDebug() << "Peer online and DISCOed ok:" << jidstr;
        m_peers.insert( jidstr, gloox::Presence::XA );
        emit peerOnline( jidstr );
    }
    else
    {
        //qDebug() << "Peer DISCO has no tomahawk:" << jidstr;
    }
}


void
Jabber_p::handleDiscoItems( const gloox::JID& /*iq*/, const gloox::Disco::Items&, int /*context*/ )
{
    qDebug() << Q_FUNC_INFO;
}


void
Jabber_p::handleDiscoError( const gloox::JID& j, const gloox::Error* e, int /*context*/ )
{
    qDebug() << Q_FUNC_INFO << j.full().c_str() << e->text().c_str() << e->type();
}
/// END DISCO STUFF


bool
Jabber_p::presenceMeansOnline( gloox::Presence::PresenceType p )
{
    switch(p)
    {
        case gloox::Presence::Invalid:
        case gloox::Presence::Unavailable:
        case gloox::Presence::Error:
            return false;
            break;
        default:
            return true;
    }
}
