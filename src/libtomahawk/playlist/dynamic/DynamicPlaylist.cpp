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

#include "DynamicPlaylist.h"

#include "sourcelist.h"
#include "GeneratorFactory.h"
#include "database/database.h"
#include "database/databasecommand.h"
#include "database/databasecommand_createdynamicplaylist.h"
#include "database/databasecommand_setdynamicplaylistrevision.h"
#include "database/databasecommand_loaddynamicplaylist.h"
#include "database/databasecommand_deletedynamicplaylist.h"

using namespace Tomahawk;

DynamicPlaylist::DynamicPlaylist(const Tomahawk::source_ptr& author, const QString& type )
    : Playlist(author)
{
    qDebug() << Q_FUNC_INFO << "JSON";
    m_generator = geninterface_ptr( GeneratorFactory::create( type ) );
}


DynamicPlaylist::~DynamicPlaylist()
{

}

// Called by loadAllPlaylists command
DynamicPlaylist::DynamicPlaylist ( const Tomahawk::source_ptr& src, 
                                   const QString& currentrevision,
                                   const QString& title, 
                                   const QString& info, 
                                   const QString& creator, 
                                   const QString& type, 
                                   GeneratorMode mode,
                                   bool shared, 
                                   int lastmod, 
                                   const QString& guid )
    : Playlist( src, currentrevision, title, info, creator, shared, lastmod, guid )
{
    qDebug() << "Creating Dynamic Playlist 1";
    // TODO instantiate generator
    m_generator = geninterface_ptr( GeneratorFactory::create( type ) );
    m_generator->setMode( mode );
}


// called when a new playlist is created (no currentrevision, new guid)
DynamicPlaylist::DynamicPlaylist ( const Tomahawk::source_ptr& author, 
                                   const QString& guid, 
                                   const QString& title, 
                                   const QString& info, 
                                   const QString& creator, 
                                   const QString& type, 
                                   bool shared )
    : Playlist ( author, guid, title, info, creator, shared )
{
    qDebug() << "Creating Dynamic Playlist 2";
    m_generator = geninterface_ptr( GeneratorFactory::create( type ) );
}

geninterface_ptr 
DynamicPlaylist::generator() const
{
    return m_generator;
}

int 
DynamicPlaylist::mode() const
{
    return m_generator->mode();
}

void 
DynamicPlaylist::setGenerator(const Tomahawk::geninterface_ptr& gen_ptr)
{
    m_generator = gen_ptr;
}

QString 
DynamicPlaylist::type() const
{
    return m_generator->type();
}

void 
DynamicPlaylist::setMode( int mode )
{
    m_generator->setMode( (GeneratorMode)mode );
}



dynplaylist_ptr 
DynamicPlaylist::create( const Tomahawk::source_ptr& author, 
                                         const QString& guid, 
                                         const QString& title, 
                                         const QString& info, 
                                         const QString& creator, 
                                         bool shared )
{
    // default generator
    QString type = "";
    dynplaylist_ptr dynplaylist = dynplaylist_ptr( new DynamicPlaylist( author, guid, title, info, creator, type, shared ) );
    
    DatabaseCommand_CreateDynamicPlaylist* cmd = new DatabaseCommand_CreateDynamicPlaylist( author, dynplaylist );
    connect( cmd, SIGNAL(finished()), dynplaylist.data(), SIGNAL(created()) );
    Database::instance()->enqueue( QSharedPointer<DatabaseCommand>(cmd) );
    dynplaylist->reportCreated( dynplaylist );
    return dynplaylist;
    
}

void 
DynamicPlaylist::createNewRevision( const QString& newUuid )
{
    if( mode() == Static )
    {
        createNewRevision( newUuid.isEmpty() ? uuid() : newUuid, currentrevision(), type(), generator()->controls(), entries() );
    } else if( mode() == OnDemand )
    {
        createNewRevision( newUuid.isEmpty() ? uuid() : newUuid, currentrevision(), type(), generator()->controls());
    }
}


// create a new revision that will be a static playlist, as it has entries
void 
DynamicPlaylist::createNewRevision( const QString& newrev, 
                                          const QString& oldrev, 
                                          const QString& type, 
                                          const QList< dyncontrol_ptr>& controls, 
                                          const QList< plentry_ptr >& entries )
{
    // get the newly added tracks
    QList< plentry_ptr > added = newEntries( entries );
    
    QStringList orderedguids;
    for( int i = 0; i < entries.size(); ++i )
        orderedguids << entries.at(i)->guid();
    
    // no conflict resolution or partial updating for controls. all or nothing baby
        
    // source making the change (local user in this case)
    source_ptr author = SourceList::instance()->getLocal();
    // command writes new rev to DB and calls setRevision, which emits our signal
    DatabaseCommand_SetDynamicPlaylistRevision* cmd =
    new DatabaseCommand_SetDynamicPlaylistRevision( author,
                                                     guid(),
                                                     newrev,
                                                     oldrev,
                                                     orderedguids,
                                                     added,
                                                     entries,
                                                     type,
                                                     Static,
                                                    controls );
    Database::instance()->enqueue( QSharedPointer<DatabaseCommand>( cmd ) );
}

// create a new revision that will be an ondemand playlist, as it has no entries
void 
DynamicPlaylist::createNewRevision( const QString& newrev, 
                                          const QString& oldrev, 
                                          const QString& type, 
                                          const QList< dyncontrol_ptr>& controls )
{
    // can skip the entry stuff. just overwrite with new info
    source_ptr author = SourceList::instance()->getLocal();
    // command writes new rev to DB and calls setRevision, which emits our signal
    DatabaseCommand_SetDynamicPlaylistRevision* cmd =
    new DatabaseCommand_SetDynamicPlaylistRevision( author,
                                                    guid(),
                                                    newrev,
                                                    oldrev,
                                                    type,
                                                    OnDemand,
                                                    controls );
    Database::instance()->enqueue( QSharedPointer<DatabaseCommand>( cmd ) );
}

void 
DynamicPlaylist::loadRevision( const QString& rev )
{
    qDebug() << Q_FUNC_INFO << "Loading with:" << ( rev.isEmpty() ? currentrevision() : rev );
    
    DatabaseCommand_LoadDynamicPlaylist* cmd = new DatabaseCommand_LoadDynamicPlaylist( rev.isEmpty() ? currentrevision() : rev );
    
    if( m_generator->mode() == OnDemand ) {
        connect( cmd, SIGNAL( done( QString,
                                    bool,
                                    QString,
                                    QList< QVariantMap >,
                                    bool ) ),
                 SLOT( setRevision( QString,
                                    bool,
                                    QString,
                                    QList< QVariantMap >,
                                    bool) ) );
    } else if( m_generator->mode() == Static ) {
        connect( cmd, SIGNAL( done( QString,
                                    QList< QString >,
                                    QList< QString >,
                                    QString,
                                    QList< QVariantMap >,
                                    bool,
                                    QMap< QString, Tomahawk::plentry_ptr >,
                                    bool ) ),
                 SLOT( setRevision( QString,
                                    QList< QString >,
                                    QList< QString >,
                                    QString,
                                    QList< QVariantMap >,
                                    bool,
                                    QMap< QString, Tomahawk::plentry_ptr >,
                                    bool ) ) );        
        
    }
	Database::instance()->enqueue( QSharedPointer<DatabaseCommand>( cmd ) );
}

bool 
DynamicPlaylist::remove( const Tomahawk::dynplaylist_ptr& playlist )
{
    DatabaseCommand_DeletePlaylist* cmd = new DatabaseCommand_DeleteDynamicPlaylist( playlist->author(), playlist->guid() );
    Database::instance()->enqueue( QSharedPointer<DatabaseCommand>(cmd) );
        
    return false;
}

void 
DynamicPlaylist::reportCreated( const Tomahawk::dynplaylist_ptr& self )
{
    qDebug() << Q_FUNC_INFO;
    Q_ASSERT( self.data() == this );
    Q_ASSERT( !author().isNull() );
    Q_ASSERT( !author()->collection().isNull() );
    // will emit Collection::playlistCreated(...)
    qDebug() << "Creating dynplaylist belonging to:" << author().data() << author().isNull();
    qDebug() << "REPORTING DYNAMIC PLAYLIST CREATED:" << this << author()->friendlyName();
    author()->collection()->addDynamicPlaylist( self );    
}

void 
DynamicPlaylist::reportDeleted( const Tomahawk::dynplaylist_ptr& self )
{
    qDebug() << Q_FUNC_INFO;
    Q_ASSERT( self.data() == this );
    // will emit Collection::playlistDeleted(...)
    author()->collection()->deleteDynamicPlaylist( self ); 
}

void DynamicPlaylist::addEntries(const QList< query_ptr >& queries, const QString& oldrev)
{
    Q_ASSERT( m_generator->mode() == Static );
    
    QList<plentry_ptr> el = addEntriesInternal( queries );
    
    QString newrev = uuid();
    createNewRevision( newrev, oldrev, m_generator->type(), m_generator->controls(), el );
}

void DynamicPlaylist::addEntry(const Tomahawk::query_ptr& query, const QString& oldrev)
{
    QList<query_ptr> queries;
    queries << query;
    
    addEntries( queries, oldrev );
}

void DynamicPlaylist::setRevision( const QString& rev, 
                                   const QList< QString >& neworderedguids, 
                                   const QList< QString >& oldorderedguids, 
                                   const QString& type, 
                                   const QList< dyncontrol_ptr >& controls, 
                                   bool is_newest_rev, 
                                   const QMap< QString, plentry_ptr >& addedmap, 
                                   bool applied)
{
    // we're probably being called by a database worker thread
    if( QThread::currentThread() != thread() )
    {
        QMetaObject::invokeMethod( this,
                                   "setRevision",
                                   Qt::BlockingQueuedConnection,
                                   Q_ARG( QString,  rev ),
                                   Q_ARG( QList<QString> , neworderedguids ),
                                   Q_ARG( QList<QString> , oldorderedguids ),
                                   Q_ARG( QString , type ),
                                   QGenericArgument( "QList< Tomahawk::dyncontrol_ptr > " , (const void*)&controls ),
                                   Q_ARG( bool, is_newest_rev ),
                                   QGenericArgument( "QMap< QString,Tomahawk::plentry_ptr > " , (const void*)&addedmap ),
                                   Q_ARG( bool, applied ) );
        return;
    }
    if( m_generator->type() != type ) { // new generator needed
        m_generator = GeneratorFactory::create( type );
    }
    
    m_generator->setControls( controls );
    m_generator->setMode( Static );
    
    DynamicPlaylistRevision dpr = setNewRevision( rev, neworderedguids, oldorderedguids, is_newest_rev, addedmap );
    dpr.applied = applied;
    dpr.controls = controls;
    dpr.type = type;
    dpr.mode = Static;
    
    if( applied ) {
        setCurrentrevision( rev );
    }
    //     qDebug() << "EMITTING REVISION LOADED 1!";
     emit dynamicRevisionLoaded( dpr );    
}


void 
DynamicPlaylist::setRevision( const QString& rev, 
                                    const QList< QString >& neworderedguids, 
                                    const QList< QString >& oldorderedguids, 
                                    const QString& type, 
                                    const QList< QVariantMap>& controlsV, 
                                    bool is_newest_rev, 
                                    const QMap< QString, Tomahawk::plentry_ptr >& addedmap, 
                                    bool applied )
{
    if( QThread::currentThread() != thread() )
    {
        QMetaObject::invokeMethod( this,
                                    "setRevision",
                                    Qt::BlockingQueuedConnection,
                                    Q_ARG( QString,  rev ),
                                    Q_ARG( QList<QString> , neworderedguids ),
                                    Q_ARG( QList<QString> , oldorderedguids ),
                                    Q_ARG( QString , type ),
                                    QGenericArgument( "QList< QVariantMap > " , (const void*)&controlsV ),
                                    Q_ARG( bool, is_newest_rev ),
                                    QGenericArgument( "QMap< QString,Tomahawk::plentry_ptr > " , (const void*)&addedmap ),
                                    Q_ARG( bool, applied ) );
        return;
    }
    
    QList<dyncontrol_ptr> controls = variantsToControl( controlsV );
    setRevision( rev, neworderedguids, oldorderedguids, type, controls, is_newest_rev, addedmap, applied );
    
}

void DynamicPlaylist::setRevision( const QString& rev, 
                                   bool is_newest_rev, 
                                   const QString& type, 
                                   const QList< dyncontrol_ptr >& controls, 
                                   bool applied )
{
    if( QThread::currentThread() != thread() )
    {
        QMetaObject::invokeMethod( this,
                                   "setRevision",
                                   Qt::BlockingQueuedConnection,
                                   Q_ARG( QString, rev ),
                                   Q_ARG( bool, is_newest_rev ),
                                   Q_ARG( QString, type ),
                                   QGenericArgument( "QList< Tomahawk::dyncontrol_ptr >" , (const void*)&controls ),
                                   Q_ARG( bool, applied ) );
        return;
    }
    if( m_generator->type() != type ) { // new generator needed
        m_generator = geninterface_ptr( GeneratorFactory::create( type ) );
    }
    
    m_generator->setControls( controls );
    m_generator->setMode( OnDemand );
    
    DynamicPlaylistRevision dpr;
    dpr.oldrevisionguid = currentrevision();
    dpr.revisionguid = rev;
    dpr.controls = controls;
    dpr.type = type;
    dpr.mode = OnDemand;
    
    if( applied ) {
        setCurrentrevision( rev );
    }
    //     qDebug() << "EMITTING REVISION LOADED 2!";
    emit dynamicRevisionLoaded( dpr ); 
}


void 
DynamicPlaylist::setRevision( const QString& rev, 
                                    bool is_newest_rev, 
                                    const QString& type, 
                                    const QList< QVariantMap >& controlsV, 
                                    bool applied )
{
    if( QThread::currentThread() != thread() )
    {
        QMetaObject::invokeMethod( this,
                                   "setRevision",
                                   Qt::BlockingQueuedConnection,
                                   Q_ARG( QString, rev ),
                                   Q_ARG( bool, is_newest_rev ),
                                   Q_ARG( QString, type ),
                                   QGenericArgument( "QList< QVariantMap >" , (const void*)&controlsV ),
                                   Q_ARG( bool, applied ) );
        return;
    } 
    
    QList<dyncontrol_ptr> controls = variantsToControl( controlsV );
    setRevision( rev, is_newest_rev, type, controls, applied );
}

QList< dyncontrol_ptr > DynamicPlaylist::variantsToControl( const QList< QVariantMap >& controlsV )
{
    QList<dyncontrol_ptr> realControls;
    foreach( QVariantMap controlV, controlsV ) {
        dyncontrol_ptr control = GeneratorFactory::createControl( controlV.value( "type" ).toString(), controlV.value( "selectedType" ).toString() );
        qDebug() << "CReating control with data:" << controlV;
        QJson::QObjectHelper::qvariant2qobject( controlV, control.data() );
        realControls << control;
    }
    return realControls;
}

