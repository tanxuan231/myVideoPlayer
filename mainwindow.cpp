#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "Log.h"
#include <QPainter>


MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_image(nullptr)
{
    ui->setupUi(this);
    connect(this, &MainWindow::DisplayVideoSignal, this, &MainWindow::DisplayVideoSlot);
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
    painter.drawRect(0, 0, this->width(), this->height()); //�Ȼ��ɺ�ɫ

    if (m_image == nullptr) {
        return;
    }
    if (m_image->size().width() <= 0) {
        return;
    }

    LogDebug("paint event, w: %d, h: %d", this->width(), this->height());
    // ��ͼ�񰴱������ųɺʹ���һ����С
    QImage img = m_image->scaled(this->size(), Qt::KeepAspectRatio);

    int x = this->width() - img.width();
    int y = this->height() - img.height();
    x /= 2;
    y /= 2;
    painter.drawImage(QPoint(x,y), img); //����ͼ��

    delete m_image;
    m_image = nullptr;
}
