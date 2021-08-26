#include "mainwindow.h"

#include <QApplication>
#include "VideoPlayer.h"
#include "Log.h"

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    MainWindow w;

    Videoplayer player;
    player.initPlayer();
    player.setVideoPlayerCallBack(&w);

    w.show();

    //player.startPlayer("/Users/xuan.tan/big_buck_bunny_720p_30mb.mp4");
    player.startPlayer("/Users/xuan.tan/big_buck_bunny_720p_1mb.mp4");

    return a.exec();
}
