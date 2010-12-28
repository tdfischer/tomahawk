#ifndef DATABASECOLLECTION_H
#define DATABASECOLLECTION_H

#include "collection.h"
#include "typedefs.h"

#include "dllmacro.h"

class DLLEXPORT DatabaseCollection : public Tomahawk::Collection
{
Q_OBJECT

public:
    explicit DatabaseCollection( const Tomahawk::source_ptr& source, QObject* parent = 0 );
    ~DatabaseCollection()
    {
        qDebug() << Q_FUNC_INFO;
    }

    virtual void loadTracks();
    virtual void loadPlaylists();

    virtual QList< Tomahawk::playlist_ptr > playlists();
    virtual QList< Tomahawk::query_ptr > tracks();

public slots:
    virtual void addTracks( const QList<QVariant> &newitems );
    virtual void removeTracks( const QList<QVariant> &olditems );
};

#endif // DATABASECOLLECTION_H