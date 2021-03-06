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

#include "sourcetreeitem.h"

#include <QDebug>
#include <QTreeView>

#include "collection.h"
#include "playlist.h"
#include "tomahawk/tomahawkapp.h"
#include "sourcesmodel.h"

using namespace Tomahawk;


static inline QList< playlist_ptr > dynListToPlaylists( const QList< Tomahawk::dynplaylist_ptr >& list )
{
    QList< playlist_ptr > newptrs;
    foreach( const dynplaylist_ptr& pl, list ) {
        newptrs << pl.staticCast<Playlist>();
    }
    return newptrs;
}


SourceTreeItem::SourceTreeItem( const source_ptr& source, QObject* parent )
    : QObject( parent )
    , m_source( source )
{
    QString name;
    if( source.isNull() )
        name = tr( "Super Collection" );
    else 
    {
        if( TomahawkApp::instance()->scrubFriendlyName() && source->friendlyName().contains( '@' ) )
            name = source->friendlyName().left( source->friendlyName().indexOf( '@' ) );
        else
            name = source->friendlyName();
    }
    
    QStandardItem* item = new QStandardItem( name );
    item->setIcon( QIcon( RESPATH "images/user-avatar.png" ) );
    item->setEditable( false );
    item->setData( SourcesModel::CollectionSource, Type );
    item->setData( (qlonglong)this, SourceItemPointer );
    m_columns << item;

    if ( !source.isNull() )
    {
        onPlaylistsAdded( source->collection()->playlists() );
        onDynamicPlaylistsAdded( source->collection()->dynamicPlaylists() );

        connect( source->collection().data(), SIGNAL( playlistsAdded( QList<Tomahawk::playlist_ptr> ) ),
                                                SLOT( onPlaylistsAdded( QList<Tomahawk::playlist_ptr> ) ) );
        connect( source->collection().data(), SIGNAL( playlistsDeleted( QList<Tomahawk::playlist_ptr> ) ),
                                                SLOT( onPlaylistsDeleted( QList<Tomahawk::playlist_ptr> ) ) );

        connect( source->collection().data(), SIGNAL( dynamicPlaylistsAdded( QList<Tomahawk::dynplaylist_ptr> ) ),
                                                SLOT( onDynamicPlaylistsAdded( QList<Tomahawk::dynplaylist_ptr> ) ) );
        connect( source->collection().data(), SIGNAL( dynamicPlaylistsDeleted( QList<Tomahawk::dynplaylist_ptr> ) ),
                                                SLOT( onDynamicPlaylistsDeleted( QList<Tomahawk::dynplaylist_ptr> ) ) );
    }

/*    m_widget = new SourceTreeItemWidget( source );
    connect( m_widget, SIGNAL( clicked() ), SLOT( onClicked() ) );*/
}


SourceTreeItem::~SourceTreeItem()
{
    qDebug() << Q_FUNC_INFO;
}


void
SourceTreeItem::onClicked()
{
    emit clicked( m_columns.at( 0 )->index() );
}


void
SourceTreeItem::onOnline()
{
    m_widget->onOnline();
}


void
SourceTreeItem::onOffline()
{
    m_widget->onOffline();
}


void
SourceTreeItem::onPlaylistsAdded( const QList<playlist_ptr>& playlists )
{
    // const-ness is important for getting the right pointer!
    foreach( const playlist_ptr& p, playlists )
    { 
        m_playlists.append( p );
        qlonglong ptr = reinterpret_cast<qlonglong>( &m_playlists.last() );

        connect( p.data(), SIGNAL( revisionLoaded( Tomahawk::PlaylistRevision ) ),
                             SLOT( onPlaylistLoaded( Tomahawk::PlaylistRevision ) ), Qt::QueuedConnection );
        connect( p.data(), SIGNAL( changed() ),
                             SLOT( onPlaylistChanged() ), Qt::QueuedConnection );

        qDebug() << "Playlist added:" << p->title() << p->creator() << p->info() << ptr;

        playlistAddedInternal( ptr, p, false );
    }
}


void
SourceTreeItem::onPlaylistsDeleted( const QList<playlist_ptr>& playlists )
{
    // const-ness is important for getting the right pointer!
    foreach( const playlist_ptr& p, playlists )
    {
        qlonglong ptr = qlonglong( p.data() );
        qDebug() << "Playlist removed:" << p->title() << p->creator() << p->info() << ptr;

        QStandardItem* item = m_columns.at( 0 );
        int rows = item->rowCount();
        for ( int i = rows - 1; i >= 0; i-- )
        {
            QStandardItem* pi = item->child( i );
            qlonglong piptr = pi->data( PlaylistPointer ).toLongLong();
            playlist_ptr* pl = reinterpret_cast<playlist_ptr*>(piptr);
            SourcesModel::SourceType type = static_cast<SourcesModel::SourceType>( pi->data( Type ).toInt() );

            if ( type == SourcesModel::PlaylistSource && ptr == qlonglong( pl->data() ) )
            {
                item->removeRow( i );
                m_playlists.removeAll( p );
                break;
            }
        }
    }
}


void
SourceTreeItem::onPlaylistLoaded( Tomahawk::PlaylistRevision revision )
{
    qlonglong ptr = reinterpret_cast<qlonglong>( sender() );

    QStandardItem* item = m_columns.at( 0 );
    int rows = item->rowCount();
    for ( int i = 0; i < rows; i++ )
    {
        QStandardItem* pi = item->child( i );
        qlonglong piptr = pi->data( PlaylistPointer ).toLongLong();
        playlist_ptr* pl = reinterpret_cast<playlist_ptr*>(piptr);
        SourcesModel::SourceType type = static_cast<SourcesModel::SourceType>( pi->data( Type ).toInt() );

        if ( type == SourcesModel::PlaylistSource && ptr == qlonglong( pl->data() ) )
        {
            pi->setEnabled( true );
            m_current_revisions.insert( pl->data()->guid(), revision.revisionguid );
            break;
        }
    }
}


void
SourceTreeItem::onPlaylistChanged()
{
    qlonglong ptr = reinterpret_cast<qlonglong>( sender() );

    QStandardItem* item = m_columns.at( 0 );
    int rows = item->rowCount();
    for ( int i = 0; i < rows; i++ )
    {
        QStandardItem* pi = item->child( i );
        SourcesModel::SourceType type = static_cast<SourcesModel::SourceType>( pi->data( Type ).toInt() );

        if ( type == SourcesModel::PlaylistSource )
        {
            qlonglong piptr = pi->data( PlaylistPointer ).toLongLong();
            playlist_ptr* pl = reinterpret_cast<playlist_ptr*>(piptr);

            if ( ptr == qlonglong( pl->data() ) )
            {
                pi->setText( pl->data()->title() );
                break;
            }
        }
        if ( type == SourcesModel::DynamicPlaylistSource )
        {
            qlonglong piptr = pi->data( DynamicPlaylistPointer ).toLongLong();
            dynplaylist_ptr* pl = reinterpret_cast<dynplaylist_ptr*>(piptr);

            if ( ptr == qlonglong( pl->data() ) )
            {
                pi->setText( pl->data()->title() );
                break;
            }
        }
    }
}


void
SourceTreeItem::onDynamicPlaylistsAdded( const QList< dynplaylist_ptr >& playlists )
{
    // const-ness is important for getting the right pointer!
    foreach( const dynplaylist_ptr& p, playlists )
    {
        m_dynplaylists.append( p );
        qlonglong ptr = reinterpret_cast<qlonglong>( &m_dynplaylists.last() );

        connect( p.data(), SIGNAL( dynamicRevisionLoaded( Tomahawk::DynamicPlaylistRevision) ),
                             SLOT( onDynamicPlaylistLoaded( Tomahawk::DynamicPlaylistRevision ) ), Qt::QueuedConnection );
        connect( p.data(), SIGNAL( changed() ),
                             SLOT( onPlaylistChanged() ), Qt::QueuedConnection );

//         qDebug() << "Dynamic Playlist added:" << p->title() << p->creator() << p->info() << p->currentrevision() << ptr;

        playlistAddedInternal( ptr, p, true );
    }
}


void
SourceTreeItem::onDynamicPlaylistsDeleted( const QList< dynplaylist_ptr >& playlists )
{
    // const-ness is important for getting the right pointer!
    foreach( const dynplaylist_ptr& p, playlists )
    {
        qlonglong ptr = qlonglong( p.data() );
//         qDebug() << "dynamic playlist removed:" << p->title() << p->creator() << p->info() << ptr;

        QStandardItem* item = m_columns.at( 0 );
        int rows = item->rowCount();
        for ( int i = rows - 1; i >= 0; i-- )
        {
            QStandardItem* pi = item->child( i );
            qlonglong piptr = pi->data( DynamicPlaylistPointer ).toLongLong();
            dynplaylist_ptr* pl = reinterpret_cast<dynplaylist_ptr*>(piptr);
            SourcesModel::SourceType type = static_cast<SourcesModel::SourceType>( pi->data( Type ).toInt() );

            //qDebug() << "Deleting dynamic playlist:" << pl->isNull();
            if ( type == SourcesModel::DynamicPlaylistSource && ptr == qlonglong( pl->data() ) )
            {
                item->removeRow( i );
                m_dynplaylists.removeAll( p );
                break;
            }
        }
    }
}


void
SourceTreeItem::onDynamicPlaylistLoaded( DynamicPlaylistRevision revision )
{
    qlonglong ptr = reinterpret_cast<qlonglong>( sender() );

    QStandardItem* item = m_columns.at( 0 );
    int rows = item->rowCount();
    for ( int i = 0; i < rows; i++ )
    {
        QStandardItem* pi = item->child( i );
        qlonglong piptr = pi->data( DynamicPlaylistPointer ).toLongLong();
        playlist_ptr* pl = reinterpret_cast<playlist_ptr*>(piptr);
        SourcesModel::SourceType type = static_cast<SourcesModel::SourceType>( pi->data( Type ).toInt() );

        if ( type == SourcesModel::DynamicPlaylistSource && ptr == qlonglong( pl->data() ) )
        {
            pi->setEnabled( true );
            m_current_dynamic_revisions.insert( pl->data()->guid(), revision.revisionguid );
            break;
        }
    }
}


void SourceTreeItem::playlistAddedInternal( qlonglong ptr, const Tomahawk::playlist_ptr& p, bool dynamic )
{
    QStandardItem* subitem = new QStandardItem( p->title() );
    subitem->setIcon( QIcon( RESPATH "images/playlist-icon.png" ) );
    subitem->setEditable( false );
    subitem->setEnabled( false );
    subitem->setData( ptr, dynamic ? DynamicPlaylistPointer : PlaylistPointer );
    subitem->setData( dynamic ? SourcesModel::DynamicPlaylistSource : SourcesModel::PlaylistSource, Type );
    subitem->setData( (qlonglong)this, SourceItemPointer );

    m_columns.at( 0 )->appendRow( subitem );
//    Q_ASSERT( qobject_cast<QTreeView*>((parent()->parent()) ) );
//    qobject_cast<QTreeView*>((parent()->parent()))->expandAll();

    p->loadRevision();
}
