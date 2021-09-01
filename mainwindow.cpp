#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "Log.h"
#include <QString>
#include <QPainter>
#include <QDir>
#include <QList>
#include <QMimeData>
#include <QUrl>
#include <QFileDialog>
#include <QLineEdit>
#include <QInputDialog>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_image(nullptr)
{
    ui->setupUi(this);
    setAcceptDrops(true);   // 接受拖拽
    connect(this, &MainWindow::DisplayVideoSignal, this, &MainWindow::DisplayVideoSlot);
    m_videoplayer.setVideoPlayerCallBack(this);

    //playVideo("/Users/xuan.tan/video/big_buck_bunny_720p_1mb.mp4");
    //playVideo("/Users/xuan.tan/video/big_buck_bunny_720p_30mb.mp4");
    //playVideo("/Users/xuan.tan/video/woshiyanshuojia4_1.mp4");
    //playVideo("/Users/xuan.tan/video/videoplayback_noauido.mp4");

    //playVideo("rtsp://wowzaec2demo.streamlock.net/vod/mp4:BigBuckBunny_115k.mov");
}

MainWindow::~MainWindow()
{
    disconnect(this, &MainWindow::DisplayVideoSignal, this, &MainWindow::DisplayVideoSlot);
    delete ui;
}

void MainWindow::onVideoPlayFailed(const int &errorCode)
{

}

void MainWindow::onDisplayVideo(const uint8_t *buffer, const int width, const int height)
{
    QImage *videoImage = new QImage(buffer, width, height, QImage::Format_RGB32);
    emit DisplayVideoSignal(videoImage);
}

void MainWindow::DisplayVideoSlot(QImage* image)
{
    if (image == nullptr) {
        return;
    }
    m_image = image;
    update();
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    ui->horizontalLayoutWidget->setGeometry((this->width() - ui->horizontalLayoutWidget->width())/2,
                                            this->height() - ui->horizontalLayoutWidget->height() - 20,
                                            ui->horizontalLayoutWidget->width(),
                                            ui->horizontalLayoutWidget->height());
}

void MainWindow::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.setBrush(Qt::black);
    painter.drawRect(0, 0, this->width(), this->height());

    if (m_image == nullptr) {
        return;
    }
    if (m_image->size().width() <= 0) {
        return;
    }

    //LogDebug("paint event, w: %d, h: %d", this->width(), this->height());
    // 将图像按比例缩放成和窗口一样大小
    QImage img = m_image->scaled(this->size(), Qt::KeepAspectRatio);

    int x = this->width() - img.width();
    int y = this->height() - img.height();
    x /= 2;
    y /= 2;
    painter.drawImage(QPoint(x,y), img); //画出图像

    delete m_image;
    m_image = nullptr;
}

// 拖拽进入事件
void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    event->acceptProposedAction();  // 向用户暗示当前窗口可接受拖拽动作
}

// 拖拽释放鼠标事件
void MainWindow::dropEvent(QDropEvent *event)
{
    QList<QUrl> urls = event->mimeData()->urls();
    if (urls.isEmpty()) {
        return;
    }

    QString fileName = urls.first().toLocalFile();
    if (fileName.isEmpty()) {
        return;
    }
    LogInfo("drag drop event, file: %s", fileName.toLatin1().data());
    playVideo(fileName.toStdString());
}

// 选择文件
void MainWindow::on_selectFilePushBtn_clicked()
{
    QString dlgTitle = "select a video file";
    QString fileName = QFileDialog::getOpenFileName(this, dlgTitle, QDir::currentPath());
    if (fileName.isEmpty()) {
        return;
    }

    LogInfo("select file push button, file: %s", fileName.toLatin1().data());
    m_videoFilepath = fileName.toStdString();    
    playVideo(m_videoFilepath);
}

void MainWindow::playVideo(std::string fileName)
{
    VideoPlayerState playState = m_videoplayer.getState();
    LogInfo("play video, playState: %s, last: %s, cur: %s",
            getStateString(playState), m_lastvVideoFilepath.c_str(), fileName.c_str());
    if (m_lastvVideoFilepath == fileName &&
            (playState == VideoPlayer_Playing ||
             playState == VideoPlayer_Pausing)) {
        return;
    }

    if ((!m_lastvVideoFilepath.empty() && m_lastvVideoFilepath != fileName) ||
            (playState == VideoPlayer_Null)) {
        m_videoplayer.stop();
    }
    m_videoplayer.startPlayer(fileName);
    m_lastvVideoFilepath = fileName;
}

// 播放
void MainWindow::on_playPushBtn_clicked()
{
    VideoPlayerState playState = m_videoplayer.getState();
    LogInfo("play/stop button clicked, playState: %s", getStateString(playState));
    if (playState != VideoPlayer_Playing && playState != VideoPlayer_Pausing) {
        playVideo(m_videoFilepath);
    } else {
        m_videoplayer.stop();
    }
}

// 暂停/继续
void MainWindow::on_pausePushBtn_clicked()
{
    VideoPlayerState playState = m_videoplayer.getState();
    LogInfo("pause/continue button clicked, playState: %s", getStateString(playState));
    if (playState != VideoPlayer_Playing && playState != VideoPlayer_Pausing) {
        return;
    }
    if (playState == VideoPlayer_Pausing) {
        m_videoplayer.play();
    } else {
        m_videoplayer.pause();
    }
}

// 拉流地址
void MainWindow::on_rtspPushBtn_clicked()
{
    QString dlgTitle = "";
    QString txtLabel = "please input rtmp stream";
    QString defaultInput = "rtsp://wowzaec2demo.streamlock.net/vod/mp4:BigBuckBunny_115k.mov";
    QLineEdit::EchoMode echoMode = QLineEdit::Normal;
    bool ok = false;

    QString text = QInputDialog::getText(this, dlgTitle, txtLabel, echoMode, defaultInput, &ok);
    if (ok && !text.isEmpty()) {
        LogInfo("get rtsp stream: %s", text.toLatin1().data());
        m_videoFilepath = text.toStdString();
        playVideo(m_videoFilepath);
    }
}
