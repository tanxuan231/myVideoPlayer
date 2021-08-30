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

extern "C" {
    #include <SDL.h>
    #include <SDL_audio.h>
    #include <SDL_types.h>
    #include <SDL_name.h>
    #include <SDL_main.h>
    #include <SDL_config.h>
}

#include "types.h"
#include "VideoPlayerCallBack.h"

static const int MAX_AUDIO_FRAME_SIZE = 192000;
#define MAX_AUDIO_SIZE (50 * 20)
#define MAX_VIDEO_SIZE (25 * 20)

class Videoplayer
{
public:
    Videoplayer();
    ~Videoplayer();

    static bool initPlayer();

    VideoPlayerState getState();

    void setVideoPlayerCallBack(VideoPlayerCallBack *pointer) { m_videoPlayerCallBack = pointer; }

    bool startPlayer(const std::string& filepath);

    void play();
    void pause();
    void stop();
    void clearResource();

private:
    void init();

    // 读取文件
    void readFileThread();
    void readFrame(const int videoStreamId, const int audioStreamId);

    // 视频队列相关
    bool putVideoPacket(const AVPacket &pkt);
    bool getVideoPacket(AVPacket& packet);
    void clearVideoQueue();

    // 音频队列相关
    bool putAudioPacket(const AVPacket &pkt);
    bool getAudioPacket(AVPacket& packet);
    void clearAudioQueue();

    // 视频解码
    bool openVideoDecoder(const int streamId);
    void decodeVideoThread();
    void decodeFrame(AVCodecContext *pCodecCtx, AVPacket *packet);

    // 音频解码
    bool openSdlAudio();
    void closeSdlAudio();
    bool openAudioDecoder(const int streamId);
    static void sdlAudioCallBackFunc(void *userdata, Uint8 *stream, int len);
    void sdlAudioCallBack(Uint8 *stream, int len);
    int decodeAudioFrame(uint8_t *decodeBuf);
    int decodeAudioFrame2();
    double getAudioClock();

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
    bool m_isVideoDecodeFinished;
    bool m_isAudioDecodeFinished;

    // 视频相关
    AVFormatContext *m_avformatCtx;
    AVCodecContext *m_videoCodecCtx;
    AVStream *m_videoStream; // 视频流

    // 视频帧队列
    std::mutex m_videoMutex;
    std::condition_variable m_videoCondvar;
    std::list<AVPacket> m_videoPacktList;

    // 音频相关
    AVCodecContext *m_audioCodecCtx;
    AVStream *m_audioStream; // 音频流
    SDL_AudioDeviceID m_audioDeviceId;
    double m_audioClock;
    int m_audioDecodeBufSize;
    int m_audioDecodeBufIndex;
    uint8_t* m_audioDecodeBuf;
    AVFrame *m_auidoFrameSample;
    SwrContext *m_audioSwrCtx;

    // 音频帧队列
    std::mutex m_audioMutex;
    std::condition_variable m_audioCondvar;
    std::list<AVPacket> m_audioPacktList;

    // 帧率控制
    int64_t m_videoStartTime; //开始播放视频的时间
    int64_t mPauseStartTime; //暂停开始的时间
};

#endif // VIDEOPLAYER_H
