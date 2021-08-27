QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++11

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    AudioDecoder.cpp \
    VideoDecoder.cpp \
    VideoPlayer.cpp \
    main.cpp \
    mainwindow.cpp

HEADERS += \
    Log.h \
    VideoPlayer.h \
    VideoPlayerCallBack.h \
    mainwindow.h \
    types.h


INCLUDEPATH += /usr/local/Cellar/ffmpeg/4.4_2/include
LIBS += -L/usr/local/Cellar/ffmpeg/4.4_2/lib -lavcodec -lavdevice -lavfilter -lavformat -lavutil -lpostproc -lswresample -lswscale

INCLUDEPATH += /usr/local/Cellar/sdl2/2.0.16/include/SDL2
LIBS += -L/usr/local/Cellar/sdl2/2.0.16/lib -lSDL2

FORMS += \
    mainwindow.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
