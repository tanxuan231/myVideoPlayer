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

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_image(nullptr)
    , m_ispause(false)
{
    ui->setupUi(this);
    setAcceptDrops(true);   // 接受拖拽
    connect(this, &MainWindow::DisplayVideoSignal, this, &MainWindow::DisplayVideoSlot);
    m_videoplayer.setVideoPlayerCallBack(this);
    m_videoplayer.initPlayer();

    playVideo("/Users/xuan.tan/big_buck_bunny_720p_1mb.mp4");
    //playVideo("/Users/xuan.tan/big_buck_bunny_720p_30mb.mp4");
}

MainWindow::~MainWindow()
{
    disconnect(this, &MainWindow::DisplayVideoSignal, this, &MainWindow::DisplayVideoSlot);
    delete ui;
}

void MainWindow::onDisplayVideo(const uint8_t *buffer, const int width, const int height)
{
    //LogDebug("%s start", __FUNCTION__);
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

void MainWindow::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.setBrush(Qt::black);
    painter.drawRect(0, 0, this->width(), this->height()); //先画成黑色

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

    m_videoFilepath = fileName.toStdString();    
    playVideo(m_videoFilepath);
}

void MainWindow::playVideo(std::string fileName)
{
    VideoPlayerState playState = m_videoplayer.getState();
    LogInfo("play button clicked, playState: %d, last: %s, cur: %s",
            playState, m_lastvVideoFilepath.c_str(), fileName.c_str());
    if (m_lastvVideoFilepath == fileName &&
            (playState == VideoPlayer_Playing ||
             playState == VideoPlayer_Pausing)) {
        return;
    }

    if (!m_lastvVideoFilepath.empty() && m_lastvVideoFilepath != fileName) {
        m_videoplayer.stop();
    }
    m_videoplayer.startPlayer(fileName);
    m_lastvVideoFilepath = fileName;
}

// 播放
void MainWindow::on_playPushBtn_clicked()
{
    playVideo(m_videoFilepath);
}

// 暂停/继续
void MainWindow::on_pausePushBtn_clicked()
{
    VideoPlayerState playState = m_videoplayer.getState();
    LogInfo("pause/continue button clicked, playState: %d", playState);
    if (playState != VideoPlayer_Playing || playState != VideoPlayer_Pausing) {
        return;
    }
    if (m_ispause) {
        m_videoplayer.play();
    } else {
        m_videoplayer.pause();
    }
    m_ispause = !m_ispause;
}
