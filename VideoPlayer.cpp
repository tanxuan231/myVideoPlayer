#include "VideoPlayer.h"

#include <thread>

Videoplayer::Videoplayer() : m_videoPlayerCallBack(nullptr),
    m_avformatCtx(nullptr),
    m_videoCodecCtx(nullptr),
    m_videoStream(nullptr),
    m_audioCodecCtx(nullptr),
    m_audioStream(nullptr),
    m_audioSwrCtx(nullptr)
{
    init();
}

Videoplayer::~Videoplayer()
{
    stop();
    avformat_network_deinit();
}

VideoPlayerState Videoplayer::getState()
{
    return m_playState;
}

void Videoplayer::init()
{
    m_playState = VideoPlayer_Null;
    m_isPause = false;
    m_isQuit = false;
    m_isReadThreadFinished = false;
    m_isVideoDecodeFinished = false;
    m_lastVframePts = 0;
    m_lastVframeDelay = 0;
    m_vframeClock = 0;

    m_audioDecodeBufSize = 0;
    m_audioDecodeBufIndex = 0;
    m_audioDecodeBuf = new uint8_t[(MAX_AUDIO_FRAME_SIZE * 3) / 2];

    m_audioDeviceId = 0;
    m_audioCurPts = 0;

    avformat_network_init();    // ֧�ִ������ļ�
}

bool Videoplayer::startPlayer(const std::string& filepath)
{
    LogInfo("start player");
    if (filepath.empty()) {
        LogError("no video file");
        return false;
    }

    init();
    m_filepath = filepath;

    // �����̶߳�ȡ�ļ�
    m_readFileThread = std::thread([&](Videoplayer* p) {
        p->readFileThread();
    }, this);

    return true;
}

void Videoplayer::play()
{
    LogInfo("to start player");
    pauseAudio(false);
    if (m_playState == VideoPlayer_Pausing) {
        m_playState = VideoPlayer_Playing;
    } else {
        LogWarn("play state error: %s", getStateString(m_playState));
    }
    m_isPause = false;
}

void Videoplayer::pause()
{
    LogInfo("to pause player");
    pauseAudio(true);
    if (m_playState == VideoPlayer_Playing) {
        m_playState = VideoPlayer_Pausing;
    } else {
        LogWarn("play state error: %d", getStateString(m_playState));
    }
    m_isPause = true;
}

void Videoplayer::stop()
{
    LogInfo("to stop player");
    m_isQuit = true;

    if (m_readFileThread.joinable()) {
        LogInfo("to join read file thread");
        m_readFileThread.join();
        LogInfo("to join read file thread over");
    }

    if (m_decodeVideoThread.joinable()) {
        LogInfo("to join decode video thread");
        m_decodeVideoThread.join();
        LogInfo("to join decode video thread over");
    }

    m_playState = VideoPlayer_Stoped;

    clearResource();
    LogInfo("to stop player over");
}

void Videoplayer::clearResource()
{
    LogInfo("clear resource");
    closeSdlAudio();
    clearVideoQueue();
    clearAudioQueue();

    if (m_audioSwrCtx != nullptr) {
        swr_free(&m_audioSwrCtx);
        m_audioSwrCtx = nullptr;
    }

    if (m_videoCodecCtx != nullptr) {
        avcodec_close(m_videoCodecCtx);
        m_videoCodecCtx = nullptr;
    }

    if (m_avformatCtx != nullptr) {
        avformat_close_input(&m_avformatCtx);
        avformat_free_context(m_avformatCtx);
        m_avformatCtx = nullptr;
    }
}

// ��ȡ�ļ����߳�
void Videoplayer::readFileThread()
{
    LogInfo("================== start to read file: %s ==================", m_filepath.c_str());
    m_isReadThreadFinished = false;

    m_avformatCtx = avformat_alloc_context();
    if (m_avformatCtx == nullptr) {
        LogError("avformat alloc context failed");
        return;
    }

    do {
        AVDictionary* opts = nullptr;
        av_dict_set(&opts, "rtsp_transport", "tcp", 0); // ����tcp or udp��Ĭ��һ������tcp�ٳ���udp
        av_dict_set(&opts, "stimeout", "60000000", 0);  // ���ó�ʱ3��

        // 1.����Ƶ�ļ�
        if (avformat_open_input(&m_avformatCtx, m_filepath.c_str(), nullptr, &opts) != 0) {
            LogError("can't open the file: %s", m_filepath.c_str());
            break;
        }

        // 2.��ȡ����Ϣ
        if (avformat_find_stream_info(m_avformatCtx, nullptr) < 0) {
            LogError("Could't find stream infomation");
            break;
        }
        LogDebug("++++++ video dump info +++++++");
        av_dump_format(m_avformatCtx, 0, m_filepath.c_str(), 0);

        // 3.��������Ƶ��ID
        int videoStreamId = -1;
        int audioStreamId = -1;
        for (int i = 0; i < m_avformatCtx->nb_streams; i++) {
            if (m_avformatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                videoStreamId = i;
            }
            if (m_avformatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO  && audioStreamId < 0) {
                audioStreamId = i;
            }
        }

        LogDebug("get videoStreamId: %d, audioStreamId: %d", videoStreamId, audioStreamId);

        if (!openVideoDecoder(videoStreamId)) {
            break;
        }
        m_playState = VideoPlayer_Playing;
        openAudioDecoder(audioStreamId);

        readFrame(videoStreamId, audioStreamId);
    } while(false);

    m_isReadThreadFinished = true;
    LogInfo("================== read file thread over ==================");
}


void Videoplayer::readFrame(const int videoStreamId, const int audioStreamId)
{
    LogDebug("start to read frame, videoStreamId: %d, audioStreamId: %d", videoStreamId, audioStreamId);
    m_videoStartTime = av_gettime();
    m_vframeClock = static_cast<double>(av_gettime()) / 1000000.0;
    //LogDebug("videoStartTime: %ld", m_videoStartTime);

    while (1) {
        if (m_isQuit) {
            LogInfo("is quit, break it");
            break;
        }

        if (m_isPause) {
            usleep(10*1000);
            continue;
        }

        // ���ƶ�ȡ�ٶ�
        if (m_audioPacktList.size() > MAX_AUDIO_SIZE || m_videoPacktList.size() > MAX_VIDEO_SIZE) {
            LogDebug("audio/video pack list size %u > %u | %u > %u",
                     m_audioPacktList.size(), MAX_AUDIO_SIZE, m_videoPacktList.size(), MAX_VIDEO_SIZE);
            usleep(10*1000);
            continue;
        }

        AVPacket packet;
        // 6.��ȡ�����е���Ƶ����֡������Ƶһ֡
        int ret = av_read_frame(m_avformatCtx, &packet);
        if (ret < 0) {
            //LogWarn("read frame failed, ret: %d | %d", ret, AVERROR_EOF);
            static bool firstEnd = true;
            if (firstEnd && ret == AVERROR_EOF) {
                firstEnd = false;

                LogWarn("set packet to null");
                packet.data = NULL;
                packet.size = 0;
            } else if (ret == AVERROR_EOF) {
                LogInfo("read file eof");
                break;
            } else {
                if (m_isVideoDecodeFinished || m_isQuit) {
                    LogWarn("read frame failed, but video decode finished or is quit, break it");
                    break; // �����߳�Ҳִ������ �����˳���
                }
                usleep(10*1000);
                continue;
            }
        }

        if (packet.stream_index == videoStreamId) {
            putVideoPacket(packet);
        } else if( packet.stream_index == audioStreamId) {
            putAudioPacket(packet);
        } else {
            LogWarn("other stream info");
            av_packet_unref(&packet);
        }
    }

    LogInfo("========== read frame over ===========");
}

bool Videoplayer::openVideoDecoder(const int streamId)
{
    if (streamId < 0) {
        LogError("open video decoder failed for error stream id: %d", streamId);
        return false;
    }

    m_videoCodecCtx = m_avformatCtx->streams[streamId]->codec;
    if (m_videoCodecCtx == nullptr) {
        LogError("find the avcodec contex failed");
        return false;
    }

    // 4.�ҵ�������
    AVCodec *videoDecoder = avcodec_find_decoder(m_videoCodecCtx->codec_id);
    if (videoDecoder == nullptr) {
        LogError("find the decoder failed");
        return false;
    }

    // 5.����Ƶ������
    if (avcodec_open2(m_videoCodecCtx, videoDecoder, nullptr) < 0) {
        LogError("open video codec failed");
        return false;
    }

    m_videoStream = m_avformatCtx->streams[streamId];
    double videoDuration = m_videoStream->duration * av_q2d(m_videoStream->time_base);
    LogInfo("open vidoe decoder success. video duration: %f", videoDuration);

    // 7.����
    m_decodeVideoThread = std::thread([&](Videoplayer* p) {
        p->decodeVideoThread();
    }, this);
    //LogInfo("decode video thread id: %s", m_decodeVideoThread);

    return true;
}

bool Videoplayer::openAudioDecoder(const int streamId)
{
    if (streamId < 0) {
        return false;
    }

    if (SDL_Init(SDL_INIT_AUDIO) != 0) {
        LogError("sdl init for audio failed");
        return false;
    }

    m_audioCodecCtx = m_avformatCtx->streams[streamId]->codec;  // �����Ƶ���Ľ�����������
    if (m_audioCodecCtx == nullptr) {
        LogError("find the avcodec contex failed");
        return false;
    }

    // 4.�ҵ�������
    AVCodec *audioDecoder = avcodec_find_decoder(m_audioCodecCtx->codec_id);
    if (audioDecoder == nullptr) {
        LogError("find the decoder failed");
        return false;
    }

    // 5.����Ƶ������
    if (avcodec_open2(m_audioCodecCtx, audioDecoder, nullptr) < 0) {
        LogError("open video codec failed");
        return false;
    }

    do {
        if (!initAudioSwsCtx()) {
            break;
        }

        m_audioStream = m_avformatCtx->streams[streamId];
        double audioDuration = m_audioStream->duration * av_q2d(m_audioStream->time_base);
        LogInfo("audio duration: %f", audioDuration);

        if (!openSdlAudio()) {
            LogError("open sdl audio failed");
            break;
        } else {
            LogInfo("open sdl audio success");
            m_isAudioDecodeFinished = false;
            return true;
        }
    } while(false);

    if (m_audioSwrCtx != nullptr) {
        swr_free(&m_audioSwrCtx);
        m_audioSwrCtx = nullptr;
    }

    return false;
}

bool Videoplayer::initAudioSwsCtx()
{
    // ����Ĳ�����ʽ
    AVSampleFormat inSampleFmt = m_audioCodecCtx->sample_fmt;
    // ����Ĳ�����ʽ 16bit PCM
    AVSampleFormat outSampleFmt = AV_SAMPLE_FMT_S16;
    // ����Ĳ�����
    int inSampleRate = m_audioCodecCtx->sample_rate;
    // ����Ĳ�����
    int outSampleRate = m_audioCodecCtx->sample_rate;
    // �������������
    int64_t inChannelLayout = m_audioCodecCtx->channel_layout;
    // �������������
    int64_t outChannelLayout = av_get_default_channel_layout(m_audioCodecCtx->channels);

    // ��Swrcontext����ռ䣬����ת������
    LogDebug("swr_alloc_set_opts: outChannelLayout: %lu, outSampleRate: %d", outChannelLayout, outSampleRate);
    LogDebug("swr_alloc_set_opts: inChannelLayout: %lu, inSampleFmt: %d, inSampleRate: %d", inChannelLayout, inSampleFmt, inSampleRate);
    m_audioSwrCtx = swr_alloc_set_opts(nullptr, outChannelLayout, outSampleFmt, outSampleRate,
                                       inChannelLayout, inSampleFmt, inSampleRate, 0, nullptr);
    if (m_audioSwrCtx == nullptr) {
        LogError("swr alloc set opts failed");
        return false;
    }

    if (swr_init(m_audioSwrCtx) < 0) {
        LogError("swr init failed");
        return false;
    }

    return true;
}

bool Videoplayer::openSdlAudio()
{
    // ��SDL�������ò��ŵĸ�ʽΪ:AUDIO_S16LSB ˫������44100hz
    // ����ʹ��ffmpeg��������Ƶ����Ҫ�ز����ɺ����һ���ĸ�ʽ�����򲥷Ż�������
    SDL_AudioSpec desiredSpec, obtainedSpec;
    desiredSpec.channels = m_audioCodecCtx->channels;   // ������
    desiredSpec.freq = m_audioCodecCtx->sample_rate;
    desiredSpec.format = AUDIO_S16SYS;
    Uint16 samples = FFMAX(512, 2 << av_log2(desiredSpec.freq / 30));
    desiredSpec.samples = samples;
    desiredSpec.silence = 0;    // ���þ�����ֵ
    desiredSpec.callback = sdlAudioCallBackFunc;  // �ص�����
    desiredSpec.userdata = this;                  // ��������ص��������������

    // ����Ƶ�豸
    int num = SDL_GetNumAudioDevices(0);
    for (int i = 0; i < num; i++) {
        m_audioDeviceId = SDL_OpenAudioDevice(SDL_GetAudioDeviceName(i, 0), false, &desiredSpec, &obtainedSpec, 0);
        if (m_audioDeviceId > 0) {
            break;
        }
    }

    LogInfo("desired samples: %u, channels: %d, freq: %d", desiredSpec.samples, desiredSpec.channels, desiredSpec.freq);
    LogInfo("obtained samples: %u, channels: %d, freq: %d", obtainedSpec.samples, obtainedSpec.channels, obtainedSpec.freq);
    LogInfo("audio device id: %d", m_audioDeviceId);
    if (m_audioDeviceId <= 0) {
        return false;
    }

    SDL_LockAudioDevice(m_audioDeviceId);
    SDL_PauseAudioDevice(m_audioDeviceId, 0);
    SDL_UnlockAudioDevice(m_audioDeviceId);

    return true;
}

void Videoplayer::closeSdlAudio()
{
    if (m_audioDeviceId > 0) {
        SDL_LockAudioDevice(m_audioDeviceId);
        SDL_PauseAudioDevice(m_audioDeviceId, 1);
        SDL_UnlockAudioDevice(m_audioDeviceId);

        SDL_CloseAudioDevice(m_audioDeviceId);
    }

    m_audioDeviceId = 0;
}

bool Videoplayer::putVideoPacket(const AVPacket &pkt)
{
    AVPacket dstPkt;
    if (av_packet_ref(&dstPkt, &pkt) < 0) {
        LogError("ref packet failed");
        return false;
    }

    // ������������ͬ��
    std::unique_lock<std::mutex> lock(m_videoMutex);
    m_videoPacktList.push_back(dstPkt);
    //LogDebug("IN videoPacktList size: %d", m_videoPacktList.size());
    m_videoCondvar.notify_one();

    return true;
}

bool Videoplayer::getVideoPacket(AVPacket& packet)
{
    std::unique_lock<std::mutex> lock(m_videoMutex);
    if (m_videoPacktList.size() <= 0) {
        //LogDebug("videoPacktList is empty");
        return false;
    }

    //LogDebug("OUT videoPacktList size: %d", m_videoPacktList.size());
    packet = m_videoPacktList.front();
    m_videoPacktList.pop_front();

    return true;
}

void Videoplayer::clearVideoQueue()
{
    LogInfo("to clear video queue");
    std::unique_lock<std::mutex> lock(m_videoMutex);
    for (AVPacket pkt : m_videoPacktList) {
        av_packet_unref(&pkt);
    }
    m_videoPacktList.clear();
}

bool Videoplayer::putAudioPacket(const AVPacket &pkt)
{
    AVPacket dstPkt;
    if (av_packet_ref(&dstPkt, &pkt) < 0) {
        LogError("ref packet failed");
        return false;
    }

    // ������������ͬ��
    std::unique_lock<std::mutex> lock(m_audioMutex);
    m_audioPacktList.push_back(dstPkt);
    //LogDebug("IN audioPacktList size: %d", m_audioPacktList.size());
    m_audioCondvar.notify_one();

    return true;
}

bool Videoplayer::getAudioPacket(AVPacket& packet)
{
    std::unique_lock<std::mutex> lock(m_audioMutex);
    if (m_audioPacktList.size() <= 0) {
        //LogDebug("audioPacktList is empty");
        return false;
    }

    //LogDebug("OUT audioPacktList size: %d", m_audioPacktList.size());
    packet = m_audioPacktList.front();
    m_audioPacktList.pop_front();

    return true;
}

void Videoplayer::clearAudioQueue()
{
    LogInfo("to clear audio queue");
    std::unique_lock<std::mutex> lock(m_audioMutex);
    for (AVPacket pkt : m_audioPacktList) {
        av_packet_unref(&pkt);
    }
    m_audioPacktList.clear();
}

void Videoplayer::pauseAudio(bool pauseOn)
{
    if (m_audioDeviceId > 0) {
        SDL_LockAudioDevice(m_audioDeviceId);
        SDL_PauseAudioDevice(m_audioDeviceId, pauseOn ? 1 : 0);
        SDL_UnlockAudioDevice(m_audioDeviceId);
    }
}

void Videoplayer::RenderVideo(const uint8_t *videoBuffer, const int width, const int height)
{
    if (m_videoPlayerCallBack != nullptr) {
        m_videoPlayerCallBack->onDisplayVideo(videoBuffer, width, height);
        return;
    }
}

char* getStateString(const VideoPlayerState state)
{
    switch (state) {
        case VideoPlayer_Playing:
            return "player playing";
        case VideoPlayer_Pausing:
            return "player pausing";
        case VideoPlayer_Stoped:
            return "player stoped";
        default:
            return "player not inited";
    }
}
