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

    // ��ȡ�ļ�
    void readFileThread();
    void readFrame(const int videoStreamId, const int audioStreamId);

    // ��Ƶ�������
    bool putVideoPacket(const AVPacket &pkt);
    bool getVideoPacket(AVPacket& packet);
    void clearVideoQueue();

    // ��Ƶ�������
    bool putAudioPacket(const AVPacket &pkt);
    bool getAudioPacket(AVPacket& packet);
    void clearAudioQueue();

    // ��Ƶ����
    bool openVideoDecoder(const int streamId);
    void decodeVideoThread();
    void decodeFrame(AVPacket *packet);
    bool convert2rgb(const int videoWidth, const int videoHeight, AVFrame *videoFrame, AVFrame *rgbFrame);
    bool AvSynchronize(AVPacket *packet, AVFrame *videoFrame);

    // ��Ƶ����
    bool openSdlAudio();
    void closeSdlAudio();
    bool openAudioDecoder(const int streamId);
    static void sdlAudioCallBackFunc(void *userdata, Uint8 *stream, int len);
    void sdlAudioCallBack(Uint8 *stream, int len);
    int decodeAudioFrame(uint8_t *decodeBuf);
    void pauseAudio(bool pauseOn);
    int convert2pcm(AVFrame* audioFrame, uint8_t *decodeBuf);
    int convert2pcm2(AVFrame* audioFrame, uint8_t *decodeBuf);

    // ��Ⱦ
    void RenderVideo(const uint8_t *videoBuffer, const int width, const int height);

private:
    static bool m_isinited;
    std::string m_filepath;

    VideoPlayerCallBack *m_videoPlayerCallBack;

    VideoPlayerState m_playState;
    bool m_isPause;
    bool m_isQuit;

    // �߳�״̬��־
    bool m_isReadThreadFinished;
    bool m_isVideoDecodeFinished;
    bool m_isAudioDecodeFinished;

    // ��Ƶ֡����
    std::mutex m_videoMutex;
    std::condition_variable m_videoCondvar;
    std::list<AVPacket> m_videoPacktList;

    // ��Ƶ֡����
    std::mutex m_audioMutex;
    std::condition_variable m_audioCondvar;
    std::list<AVPacket> m_audioPacktList;

    AVFormatContext *m_avformatCtx;

    // ��Ƶ���
    AVStream *m_videoStream; // ��Ƶ��
    AVCodecContext *m_videoCodecCtx;    // ��Ƶ�������
    int64_t m_videoStartTime; //��ʼ������Ƶ��ʱ��

    // ��Ƶ���
    AVStream *m_audioStream; // ��Ƶ��
    AVCodecContext *m_audioCodecCtx;    // ��Ƶ�������
    SDL_AudioDeviceID m_audioDeviceId;
    double m_audioCurPts;
    int m_audioDecodeBufSize;
    int m_audioDecodeBufIndex;
    uint8_t* m_audioDecodeBuf;
    SwrContext *m_audioSwrCtx;
};

char* getStateString(const VideoPlayerState state);

#endif // VIDEOPLAYER_H
