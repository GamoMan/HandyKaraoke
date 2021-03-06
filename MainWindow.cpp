#include "MainWindow.h"
#include "ui_MainWindow.h"

#include "SettingsDialog.h"
#include "Dialogs/AboutDialog.h"
#include "Midi/MidiFile.h"

#include <QTime>
#include <QMenu>
#include <QCloseEvent>
#include <QMessageBox>
#include <QDebug>
#include <QDir>
#include <QDirIterator>


MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    qApp->installEventFilter(this);

    lyrWidget = new LyricsWidget(this);
    updateDetail = new Detail(this);

    ui->setupUi(this);

    settings = new QSettings();
    db = new SongDatabase();

    timer1 = new QTimer(this);
    timer2 = new QTimer(this);
    timer2->setSingleShot(true);

    positionTimer = new QTimer();
    positionTimer->setInterval(30);

    detailTimer = new QTimer(this);
    detailTimer->setSingleShot(true);

    songDetailTimer = new QTimer(this);
    songDetailTimer->setSingleShot(true);

    player = new MidiPlayer();

    locale = QLocale(QLocale::English, QLocale::UnitedStates);

    { // Channel Mixer

        ui->chMix->setPlayer(player);

        bool s = settings->value("ChMixShow", false).toBool();

        if (s) {
            ui->chMix->show();
            ui->expandChMix->show();
        }
        else {
            ui->chMix->hide();
            ui->expandChMix->hide();
        }

        connect(ui->chMix, SIGNAL(buttonCloseClicked()), this, SLOT(showHideChMix()));
    }

    { // UI window size
        int sTime = settings->value("SearchTimeout", 5).toInt();
        int pTime = settings->value("PlaylistTimeout", 5).toInt();
        setSearchTimeout(sTime);
        setPlaylistTimeout(pTime);

        bool removeList  = settings->value("RemoveFromPlaylist", true).toBool();
        bool aPlayNext   = settings->value("AutoPlayNext", true).toBool();
        remove_playlist = removeList;
        auto_playnext = aPlayNext;

        int bg = settings->value("BackgroundType", 0).toInt();
        if (bg == 0) {
            QString color = settings->value("BackgroundColor", "#525252").toString();
            setBackgroundColor(color);
        } else {
            QString img = settings->value("BackgroundImage", "").toString();
            if (QFile::exists(img)) {
                setBackgroundImage(img);
            } else {
                QString color = settings->value("BackgroundColor", "#525252").toString();
                setBackgroundColor(color);
            }
        }

        int w     = settings->value("WindowWidth", this->minimumWidth()).toInt();
        int h     = settings->value("WindowHeight", this->minimumHeight()).toInt();
        bool max  = settings->value("WindowMaximized", false).toBool();
        bool full = settings->value("WindowFullScreen", false).toBool();

        this->resize(w, h);
        if (max) {
            this->showMaximized();
        } else {
            if (full) {
                this->showFullScreen();
            }
        }
    }


    { // Player
        int oPort   = settings->value("MidiOut", 0).toInt();
        int vl      = settings->value("MidiVolume", 50).toInt();
        bool lDrum  = settings->value("MidiLockDrum", false).toBool();
        bool lSnare = settings->value("MidiLockSnare", false).toBool();
        bool lBass  = settings->value("MidiLockBass", false).toBool();

        player->setMidiOut(oPort);
        player->setVolume(vl);

        if (lDrum) {
            int ldNum = settings->value("MidiLockDrumNumber", 0).toInt();
            player->setLockDrum(true, ldNum);
        }
        if (lSnare) {
            int lsNum = settings->value("MidiLockSnareNumber", 38).toInt();
            player->setLockSnare(true, lsNum);
        }
        if (lBass) {
            int lbNum = settings->value("MidiLockBassNumber", 32).toInt();
            player->setLockBass(true, lbNum);
        }

        connect(player, SIGNAL(finished()), this, SLOT(onPlayerThreadFinished()));
        connect(player, SIGNAL(bpmChanged(int)), ui->rhmWidget, SLOT(setBpm(int)));
    }


    { // Synth
        MidiSynthesizer *synth = player->midiSynthesizer();
        // Synth soundfont
        std::vector<std::string> sfs;
        QStringList sfList = settings->value("SynthSoundfonts", QStringList()).toStringList();

        // Synth soundfont volume
        QList<int> sfvl;
        int idx=0;
        settings->beginReadArray("SynthSoundfontsVolume");
        for (const QString &s : sfList) {
            sfs.push_back(s.toStdString());

            settings->setArrayIndex(idx);
            sfvl.append(settings->value("SoundfontVolume", 100).toInt());
            idx++;
        }
        settings->endArray();

        synth->setSoundFonts(sfs);
        for (int i=0; i<sfvl.size(); i++) {
            synth->setSoundfontVolume(i, sfvl.at(i) / 100.0f);
        }
        // -----------

        // Synth Map soundfont
        std::vector<int> sfMap = synth->getMapSoundfontIndex();
        settings->beginReadArray("SynthSoundfontsMap");
        for (int i=0; i<129; i++) {
            settings->setArrayIndex(i);
            sfMap[i] = settings->value("mapTo", 0).toInt();
        }
        settings->endArray();

        synth->setMapSoundfontIndex(sfMap);


        // Synth EQ
        Equalizer24BandFX *eq = synth->equalizer24BandFX();
        std::map<EQFrequency24Range, float> eqgain = eq->gain();

        bool eqon = settings->value("SynthFX24EQOn", false).toBool();
        if (eqon)
            eq->on();

        int gi =0;
        settings->beginReadArray("SynthFX24EQGain");
        for (const auto& g : eqgain) {
            settings->setArrayIndex(gi);
            float gain = settings->value("gain", 0.0f).toFloat();
            eq->setGain(g.first, gain);
            gi++;
        }
        settings->endArray();


        // Synth reverb
        ReverbFX *reverb = synth->reverbFX();

        bool rvOn   = settings->value("SynthFXReverbOn", false).toBool();
        int rvGain  = settings->value("SynthFXReverbInGain", 0).toInt();
        int rvMix   = settings->value("SynthFXReverbMix", 0).toInt();
        int rvTime  = settings->value("SynthFXReverbTime", 1000).toInt();
        float rvHF  = settings->value("SynthFXReverbHF", 0.001).toFloat();

        if (rvOn)
            reverb->on();

        reverb->setInGain((float)rvGain);
        reverb->setReverbMix((float)rvMix);
        reverb->setReverbTime((float)rvTime);
        reverb->setHighFreqRTRatio(rvHF);


        // Synth chorus
        ChorusFX *chorus = synth->chorusFX();

        bool cOn = settings->value("SynthFXChorusOn", false).toBool();

        int cWf  = settings->value("SynthFXChorusWaveform", 1).toInt();
        int cPh  = settings->value("SynthFXChorusPhase", 3).toInt();

        int cWet = settings->value("SynthFXChorusWetDryMix", 50).toInt();
        int cDep = settings->value("SynthFXChorusDepth", 10).toInt();
        int cFb  = settings->value("SynthFXChorusFeedback", 25).toInt();
        int cFq  = settings->value("SynthFXChorusFrequency", 1).toInt();
        int cDl  = settings->value("SynthFXChorusDelay", 16).toInt();

        WaveformType lWaveform = static_cast<WaveformType>(cWf);
        PhaseType lPhase = static_cast<PhaseType>(cPh);

        if (cOn)
            chorus->on();

        chorus->setWaveform(lWaveform);
        chorus->setPhase(lPhase);
        chorus->setWetDryMix((float)cWet);
        chorus->setDepth((float)cDep);
        chorus->setFeedback((float)cFb);
        chorus->setFrequency((float)cFq);
        chorus->setDelay((float)cDl);


        // Create Synth effect dialog
        eq24Dlg = new Equalizer24BandDialog(this, eq);
        eq24Dlg->setWindowTitle("อีควอไลเซอร์ : Equalizer");
        eq24Dlg->adjustSize();
        eq24Dlg->setFixedSize(eq24Dlg->size());

        reverbDlg = new ReverbDialog(this, reverb);
        reverbDlg->setWindowTitle("เอฟเฟ็กต์เสียงก้อง : Reverb");
        reverbDlg->adjustSize();
        reverbDlg->setFixedSize(reverbDlg->size());

        chorusDlg = new ChorusDialog(this, chorus);
        chorusDlg->setWindowTitle("เอฟเฟ็กต์เสียงประสาน : Chorus");
        chorusDlg->adjustSize();
        chorusDlg->setFixedSize(chorusDlg->size());

        // Create synth mixer
        synthMix = new SynthMixerDialog(this, this);
    }


    { // Lyrics
        QString family  = settings->value("LyricsFamily", font().family()).toString();
        int size        = settings->value("LyricsSize", 40).toInt();
        int weight      = settings->value("LyricsWeight", 75).toInt();
        bool italic     = settings->value("LyricsItalic", false).toBool();
        bool strikeOut  = settings->value("LyricsStrikeOut", false).toBool();
        bool underline  = settings->value("LyricsUnderline", false).toBool();

        QString tColor  = settings->value("LyricsTextColor", "#f3f378").toString();
        QString tbColor = settings->value("LyricsTextBorderColor", "#000000").toString();
        int     tbWidth = settings->value("LyricsTextBorderWidth", 2).toInt();

        QString cColor  = settings->value("LyricsCurColor", "#ff0000").toString();
        QString cbColor = settings->value("LyricsCurBorderColor", "#ffffff").toString();
        int     cbWidth = settings->value("LyricsCurBorderWidth", 3).toInt();

        int     line1Y  = settings->value("LyricsLine1Y", 320).toInt();
        int     line2Y  = settings->value("LyricsLine2Y", 170).toInt();
        int     aTime   = settings->value("LyricsAnimationTime", 250).toInt();

        QFont f;
        f.setFamily(family);
        f.setPointSize(size);
        f.setWeight(weight);
        f.setItalic(italic);
        f.setStrikeOut(strikeOut);
        f.setUnderline(underline);

        lyrWidget->setTextFont(f);
        lyrWidget->setTextColor(QColor(tColor));
        lyrWidget->setTextBorderColor(QColor(tbColor));
        lyrWidget->setTextBorderWidth(tbWidth);

        lyrWidget->setCurColor(QColor(cColor));
        lyrWidget->setCurBorderColor(QColor(cbColor));
        lyrWidget->setCurBorderWidth(cbWidth);

        lyrWidget->setLine1Y(line1Y);
        lyrWidget->setLine2Y(line2Y);
        lyrWidget->setAnimationTime(aTime);
    }


    { // Init UI
        setWindowIcon(QIcon(":/Icons/App/icon.svg"));

        updateDetail->hide();
        updateDetail->resize(250, 60);
        updateDetail->setText("กำลังปรับปรุงฐานข้อมูล");

        ui->detail->hide();
        ui->frameSearch->hide();
        ui->framePlaylist->hide();
        ui->songDetail->hide();
        ui->sliderVolume->setValue(player->volume());

        connect(db, SIGNAL(updateStarted()), updateDetail, SLOT(show()));
        connect(db, SIGNAL(updateFinished()), updateDetail, SLOT(hide()));
        connect(db, SIGNAL(updatePositionChanged(int)), this, SLOT(onDbUpdateChanged(int)));

        connect(timer2, SIGNAL(timeout()), ui->frameSearch, SLOT(hide()));
        connect(timer2, SIGNAL(timeout()), ui->framePlaylist, SLOT(hide()));
        connect(timer1, SIGNAL(timeout()), this, SLOT(showCurrentTime()));
        connect(positionTimer, SIGNAL(timeout()), this, SLOT(onPositiomTimerTimeOut()));

        connect(detailTimer, SIGNAL(timeout()), this, SLOT(onDetailTimerTimeout()));
        connect(songDetailTimer, SIGNAL(timeout()), ui->songDetail, SLOT(hide()));

        connect(ui->btnPause, SIGNAL(clicked()), this, SLOT(pause()));
        connect(ui->btnStop, SIGNAL(clicked()), this, SLOT(stop()));
        connect(ui->btnNext, SIGNAL(clicked()), this, SLOT(playNext()));
        connect(ui->btnPrevious, SIGNAL(clicked()), this, SLOT(playPrevious()));
        connect(ui->sliderPosition, SIGNAL(sliderPressed()), this, SLOT(onSliderPositionPressed()));
        connect(ui->sliderPosition, SIGNAL(sliderReleased()), this, SLOT(onSliderPositionReleased()));
        connect(ui->sliderVolume, SIGNAL(valueChanged(int)), this, SLOT(onSliderVolumeValueChanged(int)));

        timer1->start(1000);
    }


    // Menu
    connect(this, SIGNAL(customContextMenuRequested(const QPoint&)), this, SLOT(showContextMenu(const QPoint&)));

}

MainWindow::~MainWindow()
{
    stop();

    { // Write synth FX settings
        // Synth EQ
        Equalizer24BandFX *eq = player->midiSynthesizer()->equalizer24BandFX();
        std::map<EQFrequency24Range, float> eqgain = eq->gain();

        settings->setValue("SynthFXEQOn", eq->isOn());

        int gi =0;
        settings->beginWriteArray("SynthFX24EQGain");
        for (const auto& g : eqgain) {
            settings->setArrayIndex(gi);
            settings->setValue("gain", g.second);
            gi++;
        }
        settings->endArray();

        // Synth reverb
        ReverbFX *reverb = player->midiSynthesizer()->reverbFX();
        settings->setValue("SynthFXReverbOn", reverb->isOn());
        settings->setValue("SynthFXReverbInGain", (int)reverb->inGain());
        settings->setValue("SynthFXReverbMix", (int)reverb->reverbMix());
        settings->setValue("SynthFXReverbTime", (int)reverb->reverbTime());
        settings->setValue("SynthFXReverbHF", reverb->highFreqRTRatio());


        // Synth chorus
        ChorusFX *chorus = player->midiSynthesizer()->chorusFX();
        settings->setValue("SynthFXChorusOn", chorus->isOn());

        int cWf  = static_cast<int>(chorus->waveform());
        int cPh  = static_cast<int>(chorus->phase());
        settings->setValue("SynthFXChorusWaveform", cWf);
        settings->setValue("SynthFXChorusPhase", cPh);

        settings->setValue("SynthFXChorusWetDryMix", (int)chorus->wetDryMix());
        settings->setValue("SynthFXChorusDepth", (int)chorus->depth());
        settings->setValue("SynthFXChorusFeedback", (int)chorus->feedback());
        settings->setValue("SynthFXChorusFrequency", (int)chorus->frequency());
        settings->setValue("SynthFXChorusDelay", (int)chorus->delay());
    }

    delete synthMix;
    // Delete Synth effect dialog
    delete eq24Dlg;
    delete reverbDlg;
    delete chorusDlg;


    settings->setValue("MidiVolume", ui->sliderVolume->value());
    if (this->isFullScreen()) {
        settings->setValue("WindowFullScreen", true);
        settings->setValue("WindowMaximized", false);
    } else if (this->isMaximized()) {
        settings->setValue("WindowFullScreen", false);
        settings->setValue("WindowMaximized", true);
    } else {
        settings->setValue("WindowFullScreen", false);
        settings->setValue("WindowMaximized", false);
        settings->setValue("WindowWidth", this->width());
        settings->setValue("WindowHeight", this->height());
    }

    foreach (Song *s, playlist) {
        delete s;
    }

    delete player;

    delete songDetailTimer;
    delete detailTimer;

    delete positionTimer;
    delete timer2;
    delete timer1;

    delete db;
    delete settings;
    delete ui;

    delete updateDetail;
    delete lyrWidget;
}

void MainWindow::setBackgroundColor(QString colorName)
{
    bgType = 0;
    this->setStyleSheet("#MainWindow {background-color: " + colorName + ";}");
}

void MainWindow::setBackgroundImage(QString img)
{
    if (QFile::exists(img)) {
        this->setStyleSheet("");
        bgType = 1;
        bgImg = img;
        QPixmap bg(img);
        bg = bg.scaled(this->size(), Qt::IgnoreAspectRatio);
        QPalette palette;
        palette.setBrush(QPalette::Background, bg); //set the pic to the background
        this->setPalette(palette); //show the background pic
    }
}

QList<long> readCurFile(QFile *curFile, uint32_t resolution) {
    QList<long> curs;
    if (!curFile->isOpen()) {
        curFile->open(QIODevice::ReadOnly);
    }
    QDataStream in(curFile);
    while (!in.atEnd()) {
        quint8 b1 = 0;
        quint8 b2 = 0;
        in >> b1;
        in >> b2;
        long cs = (b1 + (b2 << 8)) * resolution / 24;
        curs.append(cs);
    }
    return curs;
}

void MainWindow::play(int index)
{
    stop();
    if (index == -1 && playingSong.id() != "") {
        lyrWidget->reset();
        player->start();
        lyrWidget->show();
        positionTimer->start();
        player->midiSynthesizer()->setFX(player->midiSynthesizer()->getFX());
        return;
    }

    QString ncnPath = settings->value("NCNPath").toString();

    Song *s = playlist[index];
    QString p = QDir::toNativeSeparators(ncnPath + s->path());
    playingSong = *s;
    playingIndex = index;

    if (remove_playlist) {
        delete s; // delete playlist in "index"
        playlist.removeAt(index);
        ui->playlist->takeItem(index);
        playingIndex = -1;
    }


    QString curPath = "";
    {// Find Cursor file and path
        curPath = playingSong.path().replace("Song", "Cursor");
        curPath = QDir::toNativeSeparators(ncnPath + curPath);
        curPath = curPath.replace(playingSong.id(), "");
        curPath = curPath.replace(curPath.length() - 4, 4, "");
        QDirIterator ct(curPath ,QStringList() << playingSong.id() +".cur" ,QDir::Files);
        if (ct.hasNext()) {
            ct.next();
            curPath = ct.filePath();
        }
    }

    player->load(p.toStdString(), true);

    QFile f(curPath);
    lyrWidget->setLyrics(playingSong.lyrics(), readCurFile(&f, player->midiFile()->resorution()));
    onPlayerDurationTickChanged(player->durationTick());
    onPlayerDurationMSChanged(player->durationMs());

    // RHM
    ui->rhmWidget->setBeat(player->beatInBar(), player->beatCount());

    // SongDetail
    ui->songDetail->setDetail(&playingSong);
    ui->songDetail->adjustSize();
    ui->songDetail->show();
    songDetailTimer->start(5000);

    player->start();
    lyrWidget->show();
    positionTimer->start();
}

void MainWindow::pause()
{
    positionTimer->stop();
    lyrWidget->stopAnimation();
    player->stop();
}

void MainWindow::resume()
{
    player->start();
    positionTimer->start();
}

void MainWindow::stop()
{
    positionTimer->stop();
    player->stop(true);

    ui->sliderPosition->setValue(0);
    lyrWidget->hide();
    lyrWidget->reset();
    ui->sliderPosition->setValue(0);
    onPlayerPositionMSChanged(0);

    ui->rhmWidget->reset();
}

void MainWindow::playNext()
{
    if (playlist.count() > 0 && playingIndex < playlist.count()-1) {
        play(playingIndex+1);
    } else {
        stop();
    }
}

void MainWindow::playPrevious()
{
    if (playingIndex > 0 && playlist.count() > 0) {
        play(playingIndex - 1);
    }
}

void MainWindow::mouseMoveEvent(QMouseEvent *event)
{

}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    if (bgType == 1) {
        setBackgroundImage(bgImg);
        QMainWindow::resizeEvent(event);
    }
    lyrWidget->resize(ui->centralWidget->size());
    updateDetail->move(width() - 260, 70);
    emit resized(event->size());
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    QMessageBox::StandardButton resBtn = QMessageBox::question(
                                            this, "ออกจากโปรแกรม",
                                            "ท่านต้องการออกจากโปรแกรม?",
                                            QMessageBox::Yes|QMessageBox::No);
    if (resBtn != QMessageBox::Yes) {
        event->ignore();
    } else {
        event->accept();
    }
}

bool MainWindow::eventFilter(QObject *object, QEvent *ev)
{
 if (ev->type() == QEvent::KeyPress)
  {
       QKeyEvent *event = static_cast<QKeyEvent *>(ev);

       if (event->modifiers() & Qt::ControlModifier) {
           switch (event->key()) {
           case Qt::Key_Up:
               ui->sliderVolume->setValue(ui->sliderVolume->value() + 5);
               break;
           case Qt::Key_Down:
               ui->sliderVolume->setValue(ui->sliderVolume->value() - 5);
               break;
           default:
               break;
           }
           return true;
       }

       switch (event->key()) {
       case Qt::Key_F5: {
           if (player->isPlayerPaused())
               resume();
           else
               play(-1);
           break;
       }
       case Qt::Key_F6: {
           pause();
           break;
       }
       case Qt::Key_F7: {
           stop();
           break;
       }
       case Qt::Key_F8: {
           playPrevious();
           break;
       }
       case Qt::Key_F9: {
           playNext();
           break;
       }
       case Qt::Key_Insert: {
           player->setTranspose(player->transpose()+1);
           int trp = player->transpose();
           QString t;
           if (trp > 0) t = "+" + QString::number(trp);
           else t = QString::number(trp);
           ui->detail->setDetail("คีย์เพลง ", t);
           ui->detail->show();
           detailTimer->start(3000);
           if (this->width() < 1150 && ui->chMix->isVisible()) {
               ui->lcdTime->hide();
           }
           break;
       }
       case Qt::Key_Delete: {
           player->setTranspose(player->transpose()-1);
           int trp = player->transpose();
           QString t;
           if (trp > 0) t = "+" + QString::number(trp);
           else t = QString::number(trp);
           ui->detail->setDetail("คีย์เพลง ", t);
           ui->detail->show();
           detailTimer->start(3000);
           if (this->width() < 1150 && ui->chMix->isVisible()) {
               ui->lcdTime->hide();
           }
           break;
       }
       case Qt::Key_PageUp:
           if (ui->playlist->isVisible()) {
               int i = ui->playlist->currentRow();
               if (i == 0)
                   break;

               playlist.swap(i, i-1);
               QListWidgetItem *item = ui->playlist->takeItem(i);
               ui->playlist->insertItem(i-1, item);
               ui->playlist->setCurrentRow(i-1);
               ui->playlist->show();
               timer2->start(playlist_timeout);
           } else {
                float tmp = player->currentBpm();
                float sc = player->GetCurrentTempoScale()-10/tmp;
                player->SetCurrentTempoScale(sc);
           }
           break;
       case Qt::Key_PageDown:
           if (ui->playlist->isVisible()) {
               int i = ui->playlist->currentRow();
               if (i >= ui->playlist->count() -1)
                   break;

               playlist.swap(i, i+1);
               QListWidgetItem *item = ui->playlist->takeItem(i);
               ui->playlist->insertItem(i+1, item);
               ui->playlist->setCurrentRow(i+1);
               ui->playlist->show();
               timer2->start(playlist_timeout);
           } else {
               float tmp = player->currentBpm();
               float sc = player->GetCurrentTempoScale()+10/tmp;
               player->SetCurrentTempoScale(sc);
           }
           break;
       case Qt::Key_Home:
           if (playingSong.id() != "") {
               play(-1);
           }
           break;
       case Qt::Key_End:
           if (auto_playnext) {
               playNext();
           } else {
               stop();
           }
           break;
       case Qt::Key_Up:
           if (ui->framePlaylist->isVisible()) {
               if (ui->playlist->currentRow() > 0 )
                   ui->playlist->setCurrentRow( ui->playlist->currentRow() - 1 );
               timer2->start(playlist_timeout);
           } else {
               ui->frameSearch->hide();
               if (ui->playlist->currentRow() == -1)
                   ui->playlist->setCurrentRow(0);
               ui->framePlaylist->show();
               timer2->start(playlist_timeout);
           }
           break;
       case Qt::Key_Down:
           if (ui->framePlaylist->isVisible()) {
               if (ui->playlist->currentRow() < ui->playlist->count() - 1 )
                   ui->playlist->setCurrentRow( ui->playlist->currentRow() + 1 );
               timer2->start(playlist_timeout);
           } else {
               ui->frameSearch->hide();
               if (playingIndex == -1)
                   ui->playlist->setCurrentRow(0);
               else
                   ui->playlist->setCurrentRow(playingIndex);
               ui->framePlaylist->show();
               timer2->start(playlist_timeout);
           }
           break;
       case Qt::Key_Right:
           if (ui->frameSearch->isVisible()) {
               setFrameSearch( db->searchNext() );
               timer2->start(search_timeout);
           } else {
               ui->framePlaylist->hide();
               if (ui->lbId->text() == "")
                   setFrameSearch( db->search("") );
               ui->lbSearch->setText("_");
               ui->frameSearch->show();
               timer2->start(search_timeout);
           }
           break;
       case Qt::Key_Left:
           if (ui->frameSearch->isVisible()) {
               setFrameSearch( db->searchPrevious() );
               timer2->start(search_timeout);
           } else {
               ui->framePlaylist->hide();
               if (ui->lbId->text() == "")
                   setFrameSearch( db->search("") );
               ui->lbSearch->setText("_");
               ui->frameSearch->show();
               timer2->start(search_timeout);
           }
           break;
       case Qt::Key_Tab:
           if (ui->frameSearch->isVisible()) {
               QString s = ui->lbSearch->text();
               s = s.replace(s.length() - 1, 1, "");
               setFrameSearch( db->nextType(s) );
               timer2->start(search_timeout);
           }
           break;
       case Qt::Key_Backspace:
           if (ui->frameSearch->isVisible()) {
               if (ui->lbSearch->text() == "_") {
                   timer2->start(search_timeout);
                   break;
               }
               QString s = ui->lbSearch->text();
               s = s.replace(s.length() - 2, 2, "");
               setFrameSearch( db->search(s) );
               ui->lbSearch->setText(s + "_");
               timer2->start(search_timeout);
           }
           break;
       case Qt::Key_Enter:
       case Qt::Key_Return:
           if (ui->frameSearch->isVisible()) {
               Song *s = db->currentSong();
               ui->playlist->addItem( " " + s->id() + " " + s->name() +
                                      " - " + s->artist());
               QSize size;
               size.setHeight(41);
               ui->playlist->item(ui->playlist->count() - 1)->setSizeHint(size);
               ui->frameSearch->hide();

               // Test
               Song *sToAdd = new Song();
               *sToAdd = *s;
               playlist.append(sToAdd);

               if (auto_playnext && playlist.count() == 1 && player->isPlayerStopped()) {
                   play(0);
               }
           }
           if (ui->framePlaylist->isVisible()) {
               ui->framePlaylist->hide();
               play(ui->playlist->currentRow());
           }
           break;
       case Qt::Key_Space:
           if (ui->frameSearch->isHidden()) {
               ui->framePlaylist->hide();
               ui->songDetail->show();
               songDetailTimer->start(5000);
           }
           break;
       case Qt::Key_Escape:
           if (ui->frameSearch->isVisible()) {
               QString s = "_";
               ui->lbSearch->setText(s);
               setFrameSearch( db->search(s) );
               timer2->start(search_timeout);
           }
           break;
       default:
           if (ui->frameSearch->isVisible()) {
               QString s = ui->lbSearch->text();
               s = s.replace(s.length() - 1, 1, "");
               ui->lbSearch->setText(s + event->text() + "_");
               setFrameSearch( db->search(s + event->text()) );
               timer2->start(search_timeout);
           } else {
               db->setSearchType(SongDatabase::ByName);
               ui->lbSearch->setText(event->text() + "_");
               setFrameSearch( db->search(event->text() ));
               ui->framePlaylist->hide();
               ui->frameSearch->show();
               timer2->start(search_timeout);
           }
           break;
       }
      return true; //Here the signal was processed and is not going to be handled by QtTabWidget
  }

 return false;
}

void MainWindow::showCurrentTime()
{
    QTime time = QTime::currentTime();
    QString currentTime = locale.toString(time, "hh:mm:ss");
    ui->lcdTime->display(currentTime);
}

void MainWindow::setFrameSearch(Song *s)
{
    ui->lbId->setText(s->id());
    ui->lbName->setText(" " + s->name());
    ui->lbBtw->setText("-");
    ui->lbArtist->setText(s->artist());
    if (s->key() != "")
        ui->lbTempoKey->setText(" (" + QString::number(s->tempo()) + "-" + s->key() + ")");
    else
        ui->lbTempoKey->setText(" (" + QString::number(s->tempo()) + ")");
    ui->lbType->setText("[" + s->songType() + "]");
    ui->lbLyrics->setText(s->lyrics().replace("\r\n", " "));
}

void MainWindow::showContextMenu(const QPoint &pos)
{
    QMenu menu(tr("Context menu"), this);

    QAction actionSettings("ตั้งค่า", this);
    QAction actionShowHideChMix("ช่องสัญญาณมิกเซอร์ (แสดง/ซ่อน)", this);
    QAction actionShowSynthMixDlg("Handy Synth Mixer", this);
    QAction actionShowEqDlg("อีควอไลเซอร์", this);
    QAction actionShowReverbDlg("เอฟเฟ็กต์เสียงก้อง", this);
    QAction actionShowChorusDlg("เอฟเฟ็กต์เสียงประสาน", this);
    //QAction actionMinimize("ยุบหน้าจอ", this);
    QAction actionFullScreen("เต็มหน้าจอ (ย่อ/ขยาย)", this);
    QAction actionAbout("เกี่ยวกับ", this);
    QAction actionExit("ออกจากโปรแกรม", this);

    connect(&actionSettings, SIGNAL(triggered()), this, SLOT(showSettingsDialog()));
    connect(&actionShowHideChMix, SIGNAL(triggered()), this, SLOT(showHideChMix()));
    connect(&actionShowSynthMixDlg, SIGNAL(triggered()), synthMix, SLOT(show()));
    connect(&actionShowEqDlg, SIGNAL(triggered()), eq24Dlg, SLOT(show()));
    connect(&actionShowReverbDlg, SIGNAL(triggered()), reverbDlg, SLOT(show()));
    connect(&actionShowChorusDlg, SIGNAL(triggered()), chorusDlg, SLOT(show()));
    //connect(&actionMinimize, SIGNAL(triggered()), this, SLOT(minimizeWindow()));
    connect(&actionFullScreen, SIGNAL(triggered()), this, SLOT(showFullScreenOrNormal()));
    connect(&actionAbout, SIGNAL(triggered()), this, SLOT(showAboutDialog()));
    connect(&actionExit, SIGNAL(triggered()), this, SLOT(close()));

    menu.addAction(&actionSettings);
    menu.addSeparator();
    menu.addAction(&actionShowHideChMix);
    menu.addSeparator();
    menu.addAction(&actionShowSynthMixDlg);
    menu.addAction(&actionShowEqDlg);
    menu.addAction(&actionShowReverbDlg);
    menu.addAction(&actionShowChorusDlg);
    menu.addSeparator();
    //menu.addAction(&actionMinimize);
    menu.addAction(&actionFullScreen);
    menu.addSeparator();
    menu.addAction(&actionAbout);
    menu.addAction(&actionExit);

    menu.exec(mapToGlobal(pos));
}

void MainWindow::showSettingsDialog()
{
    SettingsDialog d(this, this);
    d.setModal(true);
    d.setMinimumSize(550, 400);
    d.exec();
}

void MainWindow::showHideChMix()
{
    if (ui->chMix->isVisible()) {
        ui->chMix->hide();
        ui->expandChMix->hide();
        settings->setValue("ChMixShow", false);
    }
    else {
        ui->chMix->show();
        ui->expandChMix->show();
        settings->setValue("ChMixShow", true);
    }
}

void MainWindow::minimizeWindow()
{
    setWindowState(Qt::WindowMinimized);
}

void MainWindow::showFullScreenOrNormal()
{
    if (this->isFullScreen()) {
        this->showNormal();
    } else {
        this->showFullScreen();
    }
}

void MainWindow::showAboutDialog()
{
    AboutDialog d(this);
    d.setModal(true);
    d.exec();
}

void MainWindow::onPositiomTimerTimeOut()
{
    int tick = player->positionTick();
    ui->sliderPosition->setValue(tick);
    lyrWidget->setPositionCursor(tick+5);

    onPlayerPositionMSChanged(player->positionMs());

    ui->rhmWidget->setCurrentBeat( player->currentBeat() );

    onPlayerPositionMSChanged(player->positionMs());
}

void MainWindow::onPlayerDurationMSChanged(qint64 d)
{
    QDateTime t = QDateTime::fromMSecsSinceEpoch(d);
    ui->lbDuration->setText( locale.toString(t, "mm:ss") );
}

void MainWindow::onPlayerPositionMSChanged(qint64 p)
{
    QDateTime t = QDateTime::fromMSecsSinceEpoch(p);
    ui->lbPosition->setText( locale.toString(t, "mm:ss") );
}

void MainWindow::onPlayerDurationTickChanged(int d)
{
    ui->sliderPosition->setMaximum(d);
}

void MainWindow::onSliderPositionPressed()
{
    playAfterSeek = player->isPlayerPlaying();
    pause();
}

void MainWindow::onSliderPositionReleased()
{
    if (player->isPlayerStopped()) {
        ui->sliderPosition->setValue(0);
        return;
    }

    player->setPositionTick(ui->sliderPosition->value());
    lyrWidget->setSeekPositionCursor(ui->sliderPosition->value());
    onPlayerPositionMSChanged(player->positionMs());

    // Seek beat
    ui->rhmWidget->setSeekBeat(player->currentBeat());

    if (playAfterSeek) resume();
}

void MainWindow::on_btnVolumeMute_clicked()
{
    if (ui->sliderVolume->isEnabled()) {
        ui->sliderVolume->setEnabled(false);
        ui->btnVolumeMute->setIcon(QIcon(":/Icons/volume-adjustment-mute.png"));
        player->setVolume(0);
    } else {
        ui->sliderVolume->setEnabled(true);
        ui->btnVolumeMute->setIcon(QIcon(":/Icons/volume-up-interface-symbol.png"));
        player->setVolume(ui->sliderVolume->value());
    }
}

void MainWindow::on_btnPlay_clicked()
{
    if (player->isPlayerPaused()) {
        resume();
    } else {
        if (playingSong.id() != "")
            play(-1);
    }
}


void MainWindow::onSliderVolumeValueChanged(int value)
{
    player->setVolume(value);

    ui->detail->setDetail("ระดับเสียง", QString::number(value));
    ui->detail->show();
    detailTimer->start(3000);
    if (this->width() < 1150 && ui->chMix->isVisible()) {
        ui->lcdTime->hide();
    }
}

void MainWindow::onPlayerThreadFinished()
{
    if (player->isPlayerFinished()) {
        playNext();
    }
}

void MainWindow::onDbUpdateChanged(int v)
{
    int p = (100 * v / db->updateCount()) - 1;
    if (p < 0) p = 0;
    updateDetail->setValue(QString::number(p) + "%");
}

void MainWindow::onDetailTimerTimeout()
{
     ui->detail->hide();
     ui->lcdTime->show();
}
