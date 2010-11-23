#include "albumview.h"

#include <QDebug>
#include <QHeaderView>
#include <QKeyEvent>
#include <QPainter>
#include <QScrollBar>

#include "tomahawk/tomahawkapp.h"
#include "audioengine.h"
#include "tomahawksettings.h"

#include "albummodel.h"
#include "albumproxymodel.h"
#include "playlistmanager.h"

using namespace Tomahawk;


AlbumView::AlbumView( QWidget* parent )
    : QListView( parent )
    , m_model( 0 )
    , m_proxyModel( 0 )
//    , m_delegate( 0 )
{
    setDragEnabled( true );
    setDropIndicatorShown( false );
    setDragDropOverwriteMode( false );
    setUniformItemSizes( true );
    setSpacing( 8 );

    setResizeMode( Adjust );
    setViewMode( IconMode );
    setIconSize( QSize( 64, 64 ) );

    setProxyModel( new AlbumProxyModel( this ) );

    connect( this, SIGNAL( doubleClicked( QModelIndex ) ), SLOT( onItemActivated( QModelIndex ) ) );
}


AlbumView::~AlbumView()
{
    qDebug() << Q_FUNC_INFO;
}


void
AlbumView::setProxyModel( AlbumProxyModel* model )
{
    m_proxyModel = model;
//    m_delegate = new PlaylistItemDelegate( this, m_proxyModel );
//    setItemDelegate( m_delegate );

    QListView::setModel( m_proxyModel );
}


void
AlbumView::setModel( AlbumModel* model )
{
    m_model = model;

    if ( m_proxyModel )
        m_proxyModel->setSourceModel( model );

    connect( m_proxyModel, SIGNAL( filterChanged( QString ) ), SLOT( onFilterChanged( QString ) ) );

    setAcceptDrops( false );
}


void
AlbumView::onItemActivated( const QModelIndex& index )
{
    AlbumItem* item = m_model->itemFromIndex( m_proxyModel->mapToSource( index ) );
    if ( item )
    {
//        qDebug() << "Result activated:" << item->album()->tracks().first()->toString() << item->album()->tracks().first()->results().first()->url();
//        APP->audioEngine()->playItem( item->album().data(), item->album()->tracks().first()->results().first() );

        APP->playlistManager()->show( item->album() );
    }
}


void
AlbumView::dragEnterEvent( QDragEnterEvent* event )
{
    qDebug() << Q_FUNC_INFO;
    QListView::dragEnterEvent( event );
}


void
AlbumView::dragMoveEvent( QDragMoveEvent* event )
{
    QListView::dragMoveEvent( event );
}


void
AlbumView::dropEvent( QDropEvent* event )
{
    QListView::dropEvent( event );
}


void
AlbumView::paintEvent( QPaintEvent* event )
{
    QListView::paintEvent( event );
}


void
AlbumView::onFilterChanged( const QString& )
{
    if ( selectedIndexes().count() )
        scrollTo( selectedIndexes().at( 0 ), QAbstractItemView::PositionAtCenter );
}


void
AlbumView::startDrag( Qt::DropActions supportedActions )
{
}


// Inspired from dolphin's draganddrophelper.cpp
QPixmap
AlbumView::createDragPixmap( int itemCount ) const
{
    return QPixmap();
}