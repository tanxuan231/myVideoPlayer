#ifndef VIDEOPLAYERCALLBACK_H
#define VIDEOPLAYERCALLBACK_H

#include <stdint.h>
#include "types.h"
#include "VideoFrame.h"

class VideoPlayerCallBack
{
public:
    // �����У��������ʧ��
    //VideoPlayerCallBack();
    //~VideoPlayerCallBack();

    // ���ļ�ʧ��
    //virtual void onOpenVideoFileFailed(const int &code = 0) = 0;

    // ��sdlʧ�ܵ�ʱ��ص��˺���
    //virtual void onOpenSdlFailed(const int &code) = 0;

    // ��ȡ����Ƶʱ����ʱ����ô˺���
    //virtual void onTotalTimeChanged(const int64_t &uSec) = 0;

    // ������״̬�ı��ʱ��ص��˺���
    //virtual void onPlayerStateChanged(const VideoPlayerState &state, const bool &hasVideo, const bool &hasAudio) = 0;

    // ������Ƶ���˺�����������ʱ�����������Ӱ�첥�ŵ������ԡ�
    virtual void onDisplayVideo(VideoFramePtr videoFrame) = 0;

    virtual void onDisplayVideo(const uint8_t *yuv420Buffer, const int width, const int height) = 0;
};

#endif // VIDEOPLAYERCALLBACK_H
