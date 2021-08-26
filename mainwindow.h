#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QImage>
#include "VideoPlayerCallBack.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow, public VideoPlayerCallBack
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    void onDisplayVideo(VideoFramePtr videoFrame);
    void onDisplayVideo(const uint8_t *yuv420Buffer, const int width, const int height);

private:
    void paintEvent(QPaintEvent *event);
    void DisplayVideoSlot(QImage* image);

signals:
    void DisplayVideoSignal(QImage* image);

private:
    Ui::MainWindow *ui;
    QImage* m_image;
};
#endif // MAINWINDOW_H
