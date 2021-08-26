#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "Log.h"
#include <QString>
#include <QPainter>
#include <QDir>
#include <QFileDialog>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_image(nullptr)
    , m_ispause(false)
{
    ui->setupUi(this);
    connect(this, &MainWindow::DisplayVideoSignal, this, &MainWindow::DisplayVideoSlot);
    m_videoplayer.setVideoPlayerCallBack(this);
    m_videoplayer.initPlayer();
}

MainWindow::~MainWindow()
{
    disconnect(this, &MainWindow::DisplayVideoSignal, this, &MainWindow::DisplayVideoSlot);
    delete ui;
}

void MainWindow::onDisplayVideo(VideoFramePtr videoFrame)
{

}

void MainWindow::onDisplayVideo(const uint8_t *yuv420Buffer, const int width, const int height)
{
    LogDebug("%s start", __FUNCTION__);
    QImage *videoImage = new QImage(yuv420Buffer, width, height, QImage::Format_RGB32);
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

    LogDebug("paint event, w: %d, h: %d", this->width(), this->height());
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

// 选择文件
void MainWindow::on_selectFilePushBtn_clicked()
{
    QString dlgTitle = "select a video file";
    QString fileName = QFileDialog::getOpenFileName(this, dlgTitle, QDir::currentPath());
    m_videoFilepath = fileName.toStdString();
    LogInfo("get fileName: %s", fileName.toLatin1().data());
}

// 播放
void MainWindow::on_playPushBtn_clicked()
{
    LogInfo("play button clicked");
    m_videoFilepath = "/Users/xuan.tan/big_buck_bunny_720p_30mb.mp4";
    m_videoplayer.startPlayer(m_videoFilepath);
}

// 暂停/继续
void MainWindow::on_pausePushBtn_clicked()
{
    LogInfo("pause/continue button clicked");
    if (m_ispause) {
        m_videoplayer.play();
    } else {
        m_videoplayer.pause();
    }
    m_ispause = !m_ispause;
}
