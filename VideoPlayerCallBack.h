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

    virtual void onVideoPlayFailed(const int &errorCode = 0) = 0;

    virtual void onDisplayVideo(const uint8_t *yuv420Buffer, const int width, const int height) = 0;
};

#endif // VIDEOPLAYERCALLBACK_H
