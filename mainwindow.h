#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QImage>
#include <QDragEnterEvent>
#include <QDropEvent>
#include "VideoPlayerCallBack.h"
#include "VideoPlayer.h"

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
    void onSetVideoWinRect(const int width, const int height);
    void onVideoPlayFailed(const int &errorCode = 0);
    void onDisplayVideo(const uint8_t *buffer, const int width, const int height);

    void dragEnterEvent(QDragEnterEvent *event);
    void dropEvent(QDropEvent *event);

private:
    void resizeEvent(QResizeEvent *event);
    void paintEvent(QPaintEvent *event);
    void playVideo(std::string fileName);

signals:
    void DisplayVideoSignal(QImage* image);
    void setVideoWinRectSignal(const int _width, const int _height);

private slots:
    void DisplayVideoSlot(QImage* image);
    void setVideoWinRectSlot(const int _width, const int _height);

    void on_selectFilePushBtn_clicked();

    void on_playPushBtn_clicked();

    void on_pausePushBtn_clicked();

    void on_rtspPushBtn_clicked();

private:
    Ui::MainWindow *ui;
    QImage* m_image;

    std::string m_videoFilepath;
    std::string m_lastvVideoFilepath;
    Videoplayer m_videoplayer;
};
#endif // MAINWINDOW_H
