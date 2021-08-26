#ifndef VIDEOPLAYER_H
#define VIDEOPLAYER_H

#include <string>
#include <list>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <unistd.h>
#include "Log.h"

extern "C" {
    #include <libavcodec/avcodec.h>
    #include <libavformat/avformat.h>
    #include <libavutil/time.h>
    #include <libavutil/pixfmt.h>
    #include <libavutil/display.h>
    #include <libavutil/avstring.h>
    #include <libavutil/opt.h>
    #include <libswscale/swscale.h>
    #include <libswresample/swresample.h>
    #include <libavutil/imgutils.h>
    #include <libavfilter/avfilter.h>
    #include <libavfilter/buffersink.h>
    #include <libavfilter/buffersrc.h>
}
#include "types.h"
#include "VideoPlayerCallBack.h"

#define MAX_AUDIO_SIZE (50 * 20)
#define MAX_VIDEO_SIZE (25 * 20)
#define FLUSH_DATA "FLUSH"


class Videoplayer
{
public:
    Videoplayer();
    ~Videoplayer();

    static bool initPlayer();

    void setVideoPlayerCallBack(VideoPlayerCallBack *pointer) { m_videoPlayerCallBack = pointer; }

    bool startPlayer(const std::string& filepath);

    void play();
    void pause();
    void stop();
private:
    // 读取文件
    void readFileThread();
    void readFrame(const int videoStreamId, const int audioStreamId);

    bool putVideoPacket(const AVPacket &pkt);
    bool getVideoPacket(AVPacket& packet);
    void clearVideoQuene();

    // 解码
    void decodeVideoThread();
    void decodeFrame(AVCodecContext *pCodecCtx, AVFrame *pFrame, AVPacket *packet);

    // 渲染
    void RenderVideo(const uint8_t *videoBuffer, const int width, const int height);

private:
    static bool m_isinited;
    std::string m_filepath;

    VideoPlayerCallBack *m_videoPlayerCallBack;

    VideoPlayerState m_playState;
    bool m_isPause;
    bool m_isQuit;

    bool m_isReadThreadFinished;
    bool m_isVideoThreadFinished;

    // 视频相关
    AVFormatContext *m_avformatCtx;
    AVCodecContext *m_avcodecCtx;
    AVCodec *m_avCodec;
    AVStream *m_videoStream; // 视频流

    // 视频帧队列
    //Cond *mConditon_Video;
    std::mutex m_videoMutex;
    std::condition_variable m_videoCondvar;
    std::list<AVPacket> m_videoPacktList;

    uint64_t mVideoStartTime; //开始播放视频的时间
    uint64_t mPauseStartTime; //暂停开始的时间
};

#endif // VIDEOPLAYER_H
