#include "SongDatabase.h"

#include <Midi/MidiFile.h>

#include <QDebug>
#include <QDir>
#include <QSettings>
#include <QDirIterator>
#include <QFile>
#include <QSqlQuery>
#include <bass.h>
#include <bassmidi.h>

SongDatabase::SongDatabase(QObject *parent) : QObject(parent)
{
    bool validDB=false;
    QString sql="";

    thread = new QThread();
    song = new Song();
    searchType = ByName;
    dCount = 0;
    this->moveToThread(thread);
    connect(thread, SIGNAL(started()), this, SLOT(update()));
    connect(this, SIGNAL(updateFinished()), thread, SLOT(quit()));

    if (isDBPath()) {
       validDB = true;
    }
    QString path = QDir::toNativeSeparators(QDir::currentPath() + "/Data/Database.db3");
    db = QSqlDatabase::addDatabase("QSQLITE");
    db.setDatabaseName(path);
    if (db.open()) {
        if (validDB == false){
            sql = "CREATE TABLE IF NOT EXISTS songs ("
                  "id TEXT PRIMARY KEY,name TEXT,artist TEXT,keyname TEXT,tempo INTEGER,songtype TEXT,lyrics TEXT,path TEXT); ";
            QSqlQuery query;
            query.exec(sql);
            query.finish();
            query.clear();

            query.exec("CREATE INDEX id_idx ON songs(id); ");
            query.exec("CREATE INDEX name_idx ON songs(name); ");
            query.exec("CREATE INDEX artist_idx ON songs(artist); ");
//            query.exec("CREATE INDEX lyrics_idx ON songs(substr(lyrics,1,200)); ");
//            query.exec("CREATE INDEX compound_idx ON songs(id,name,artist,substr(lyrics,1,200)); ");
            query.finish();
            query.clear();
            qDebug() << "SongDatabase: create DB";
        }
        db.close();
    }
    if (db.open()) {
        qDebug() << "SongDatabase: opened";
        QSqlQuery q;
        q.exec("SELECT Count(*) FROM songs");
        if (q.next()) {
            dCount = q.value(0).toInt();
        }
        q.finish(); q.clear();
    } else {
        qDebug() << "SongDatabase: can't open";
    }
}

SongDatabase::~SongDatabase()
{
    if (db.isOpen()) {
        db.close();
        qDebug() << "SongDatabase: closed";
    }
    delete song;
    delete thread;
}

bool SongDatabase::isDBPath()
{
    QDir dir;
    QString DBpath = QDir::toNativeSeparators(QDir::currentPath() + "/Data");

    if (!dir.exists(DBpath)) {
        dir.mkpath(DBpath);
    }

    QString DBpathFile = QDir::toNativeSeparators(QDir::currentPath() + "/Data/Database.db3");

    if (dir.exists(DBpathFile)) {
        return true;
    } else {
        return false;
    }
}

bool SongDatabase::isNCNPath(QString path)
{
    QDir dir;
    QString cur = QDir::toNativeSeparators(path + "/Cursor");
    QString lyr = QDir::toNativeSeparators(path + "/Lyrics");
    QString mid = QDir::toNativeSeparators(path + "/Song");

    if (dir.exists(cur) && dir.exists(lyr) && dir.exists(mid)) {
        return true;
    } else {
        return false;
    }
}

void SongDatabase::update()
{
    if (!db.isOpen()) {
        qDebug() << "SongDatabase: Database is't opened";
        return;
    }
    int count = 0;
    QSettings st;

    // Count NCN
    QString ncnPath = st.value("NCNPath", QDir::toNativeSeparators(QDir::currentPath() + "/Songs/NCN")).toString();
    QDir dir(QDir::toNativeSeparators(ncnPath + "/Lyrics"));
    QDirIterator it(dir.path() ,QStringList() << "*lyr" ,QDir::Files, QDirIterator::Subdirectories);

    while (it.hasNext()) {
        it.next();
        count++;
    }

    upCount = count;
    emit updateStarted();
    emit updateCountChanged(count);

    upTing = true;
    QSqlQuery q;

    q.exec("DROP INDEX id_idx; ");
    q.exec("DROP INDEX name_idx; ");
    q.exec("DROP INDEX artist_idx; ");
    q.exec("DROP INDEX lyrics_idx; ");
    q.exec("DROP INDEX compound_idx; ");

    q.exec("PRAGMA cache_size=500;");
    q.exec("PRAGMA page_size=4096;");
    q.exec("PRAGMA journal_mode = MEMORY");
    q.exec("PRAGMA temp_store = MEMORY;");
    q.exec("PRAGMA synchronous=OFF;");

    db.transaction();
    q.exec("DELETE FROM songs");
    q.exec("vacuum");
    db.commit();

    db.transaction();
    // Update NCN
    int i = 0;
    int erCount = 0;
    QDirIterator it2(dir.path() ,QStringList() << "*.lyr" ,QDir::Files, QDirIterator::Subdirectories);
    while (it2.hasNext()) {

        it2.next();

        QString id = it2.fileName();
        id = id.replace(id.length() - 4, 4, "");

        bool result = insertNCN(ncnPath, id, it2.filePath(), it2.fileName());
        if (!result)
            erCount ++;

        i++;
        emit updatePositionChanged(i);
    }

    db.commit();

    q.exec("CREATE INDEX id_idx ON songs(id); ");
    q.exec("CREATE INDEX name_idx ON songs(name); ");
    q.exec("CREATE INDEX artist_idx ON songs(artist); ");
//    q.exec("CREATE INDEX lyrics_idx ON songs(substr(lyrics,1,200); ");
//    q.exec("CREATE INDEX compound_idx ON songs(id,name,artist,substr(lyrics,1,200)); ");

//    q.exec("PRAGMA journal_mode = DEFAULT");
//    q.exec("PRAGMA temp_store = DEFAULT;");
//    q.exec("PRAGMA synchronous=NORMAL;");
    q.finish();
    q.clear();

    dCount = i - erCount;

    upTing = false;

    emit updateFinished();
}

bool SongDatabase::insertNCN(const QString &ncnPath, const QString &songId, const QString &lyrFilePath, const QString &fileName)
{
    QString id = songId;

    // Read .MID file
    QString midPath = lyrFilePath;
            midPath = midPath.replace("Lyrics", "Song");
            midPath = midPath.replace(fileName, "");

    int bpm = 0;
    QString path = "";
    if (_SkipMIDI == false) {
        QDirIterator mit(midPath ,QStringList() << id +".mid" ,QDir::Files);
        if (mit.hasNext()) {
            mit.next();

            MidiFile *midi = new MidiFile();
            if (!midi->read(mit.filePath().toStdString(), true)) {
                delete midi;
                return false;
            }
            emit updateSongNameChanged(mit.fileName());
            path = mit.filePath().replace(ncnPath, "");
            bpm = midi->bpm();
            delete midi;
        } else {
            return false;
        }
    } else {
        path=midPath+id+".mid";
        path=path.replace(ncnPath, "");
        // Read .LYR file
        emit updateSongNameChanged(id+".lyr");
    }
    QFile file(lyrFilePath);
    if (!file.open(QFile::ReadOnly)) {
        return false;
    }
    QTextStream textStream(&file);
    textStream.setCodec("TIS-620");

    QString name = textStream.readLine();
    QString artist = textStream.readLine();
    QString key = textStream.readLine();
    QString type = "NCN";
    textStream.readLine();
    QString lyr = textStream.readAll();
    file.close();


    // Insert to database
    QSqlQuery query;
//    QString sql;
    query.prepare("INSERT INTO songs VALUES "
                  "(?, ?, ?, ?, ?, ?, ?, ?);");
    query.bindValue(0, id);
    query.bindValue(1, name);
    query.bindValue(2, artist);
    query.bindValue(3, key);
    query.bindValue(4, bpm);
    query.bindValue(5, type);
    query.bindValue(6, lyr);
    query.bindValue(7, path);

    query.exec();
    query.finish();
    query.clear();

    return true;
}

Song* SongDatabase::nextType(const QString &s)
{
    Song *sg = song;
    switch (searchType) {
    case ById:
        searchType = ByName;
        sg = search(s);
        break;
    case ByName:
        searchType = ByArtist;
        sg = search(s);
        break;
    case ByArtist:
        searchType = ById;
        sg = search(s);
        break;
    }
    return sg;
}

void setSong(Song *s, QSqlQuery *qry) {
    s->setId(qry->value(0).toString());
    s->setName(qry->value(1).toString());
    s->setArtist(qry->value(2).toString());
    s->setKey(qry->value(3).toString());
    s->setTempo(qry->value(4).toInt());
    s->setSongType(qry->value(5).toString());
    s->setLyrics(qry->value(6).toString());
    s->setPath(qry->value(7).toString());
}

Song *SongDatabase::search(const QString &s)
{
    QString sql = "";
    switch (searchType) {
    case ById:
        sql = "SELECT * FROM songs WHERE id LIKE ? "
              "ORDER BY id, name, artist LIMIT 1";
        break;
    case ByName:
        sql = "SELECT * FROM songs WHERE name LIKE ? "
              "ORDER BY name, artist, id LIMIT 1";
        break;
    case ByArtist:
        sql = "SELECT * FROM songs WHERE artist LIKE ? "
              "ORDER BY artist, name, id LIMIT 1";
        break;
    }

    QSqlQuery query;
    query.prepare(sql);
    query.bindValue(0, s+"%");
    if (query.exec()) {
        if (query.next()) {
            setSong(song, &query);
        }
    }
    query.finish();
    query.clear();

    return song;
}

Song *SongDatabase::searchNext()
{
    QSqlQuery q;
    QString sql = "";
    switch (searchType) {
    case ById:
        sql = "SELECT * FROM songs WHERE id >= ? "
                      "ORDER BY id, name, artist LIMIT 500";
        q.prepare(sql);
        q.bindValue(0, song->id());
        if (q.exec()) {
            while (q.next()) {
                if (q.value("id").toString() == song->id()) {
                    if (q.value("name").toString() <= song->name()) {
                        if (q.value("name").toString() < song->name()) {
                            continue;
                        } else { // ==
                            if (q.value("artist").toString() <= song->artist())
                                continue;
                        }
                    }
                }
                setSong(song, &q);
                break;
            }
        }
        break;
    case ByName:
        sql = "SELECT * FROM songs WHERE name >= ? "
                      "ORDER BY name, artist, id LIMIT 500";
        q.prepare(sql);
        q.bindValue(0, song->name());
        if (q.exec()) {
            while (q.next()) {
                if (q.value("name").toString() == song->name()) {
                    if (q.value("artist").toString() <= song->artist()) {
                        if (q.value("artist").toString() < song->artist()) {
                            continue;
                        } else { // ==
                            if (q.value("id").toString() <= song->id())
                                continue;
                        }
                    }
                }
                setSong(song, &q);
                break;
            }
        }
        break;
    case ByArtist:
        sql = "SELECT * FROM songs WHERE artist >= ? "
                      "ORDER BY artist, name, id LIMIT 500";
        q.prepare(sql);
        q.bindValue(0, song->artist());
        if (q.exec()) {
            while (q.next()) {
                if (q.value("artist").toString() == song->artist()) {
                    if (q.value("name").toString() <= song->name()) {
                        if (q.value("name").toString() < song->name()) {
                            continue;
                        } else { // ==
                            if (q.value("id").toString() <= song->id())
                                continue;
                        }
                    }
                }
                setSong(song, &q);
                break;
            }
        }
        break;
    }

    q.finish();
    q.clear();

    return song;
}

Song *SongDatabase::searchPrevious()
{
    QSqlQuery q;
    QString sql = "";
    switch (searchType) {
    case ById:
        sql = "SELECT * FROM songs WHERE id <= ? "
                      "ORDER BY id DESC, name DESC, artist DESC LIMIT 500";
        q.prepare(sql);
        q.bindValue(0, song->id());
        if (q.exec()) {
            while (q.next()) {
                if (q.value("id").toString() == song->id()) {
                    if (q.value("name").toString() >= song->name()) {
                        if (q.value("name").toString() > song->name()) {
                            continue;
                        } else { // ==
                            if (q.value("artist").toString() >= song->artist())
                                continue;
                        }
                    }
                }
                setSong(song, &q);
                break;
            }
        }
        break;
    case ByName:
        sql = "SELECT * FROM songs WHERE name <= ? "
                      "ORDER BY name DESC, artist DESC, id DESC LIMIT 500";
        q.prepare(sql);
        q.bindValue(0, song->name());
        if (q.exec()) {
            while (q.next()) {
                if (q.value("name").toString() == song->name()) {
                    if (q.value("artist").toString() >= song->artist()) {
                        if (q.value("artist").toString() > song->artist()) {
                            continue;
                        } else { // ==
                            if (q.value("id").toString() >= song->id())
                                continue;
                        }
                    }
                }
                setSong(song, &q);
                break;
            }
        }
        break;
    case ByArtist:
        sql = "SELECT * FROM songs WHERE artist <= ? "
                      "ORDER BY artist DESC, name DESC, id DESC LIMIT 500";
        q.prepare(sql);
        q.bindValue(0, song->artist());
        if (q.exec()) {
            while (q.next()) {
                if (q.value("artist").toString() == song->artist()) {
                    if (q.value("name").toString() >= song->name()) {
                        if (q.value("name").toString() > song->name()) {
                            continue;
                        } else { // ==
                            if (q.value("id").toString() >= song->id())
                                continue;
                        }
                    }
                }
                setSong(song, &q);
                break;
            }
        }
        break;
    }

    q.finish();
    q.clear();

    return song;
}

