#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QImage>
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
    void onDisplayVideo(const uint8_t *buffer, const int width, const int height);

private:
    void paintEvent(QPaintEvent *event);
    void DisplayVideoSlot(QImage* image);

signals:
    void DisplayVideoSignal(QImage* image);

private slots:
    void on_selectFilePushBtn_clicked();

    void on_playPushBtn_clicked();

    void on_pausePushBtn_clicked();

private:
    Ui::MainWindow *ui;
    QImage* m_image;

    bool m_ispause;
    std::string m_videoFilepath;
    std::string m_lastvVideoFilepath;
    Videoplayer m_videoplayer;
};
#endif // MAINWINDOW_H
