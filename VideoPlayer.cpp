#include "VideoPlayer.h"

#include <thread>

bool Videoplayer::m_isinited = false;

bool Videoplayer::initPlayer()
{
    if (!m_isinited) {
        av_register_all();          // ��ʼ��FFMPEG  �����������������ʹ�ñ������ͽ�����
        avformat_network_init();    // ֧�ִ������ļ�

        LogDebug("init player success");
        m_isinited = true;
    }

    return true;
}

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

    m_audioDecodeBufSize = 0;
    m_audioDecodeBufIndex = 0;
    m_audioDecodeBuf = new uint8_t[(MAX_AUDIO_FRAME_SIZE * 3) / 2];

    m_audioDeviceId = 0;
    m_audioClock = 0;
}

bool Videoplayer::startPlayer(const std::string& filepath)
{
    LogInfo("start player");
    if (filepath.empty()) {
        LogError("no video file");
        return false;
    }
    if (!m_isinited) {
        LogError("player is not inited");
        return false;
    }

    m_filepath = filepath;
    init();

    // �����̶߳�ȡ�ļ�
    std::thread([&](Videoplayer* p) {
        p->readFileThread();
    }, this).detach();

    return true;
}

void Videoplayer::play()
{
    m_isPause = false;
}

void Videoplayer::pause()
{
    m_playState = VideoPlayer_Pausing;
    m_isPause = true;
}

void Videoplayer::stop()
{
    LogInfo("stop player");
    m_isQuit = true;

    usleep(10*1000);
    clearResource();

    m_playState = VideoPlayer_Stoped;
}

void Videoplayer::clearResource()
{
    LogInfo("clear resource");
    clearVideoQueue();

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
    LogDebug("================== start to read file: %s ==================", m_filepath.c_str());
    m_isReadThreadFinished = false;
    int videoStreamId = -1;
    int audioStreamId = -1;

    m_avformatCtx = avformat_alloc_context();
    if (m_avformatCtx == nullptr) {
        LogError("avformat alloc context failed");
        return;
    }

    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "rtsp_transport", "tcp", 0); // ����tcp or udp��Ĭ��һ������tcp�ٳ���udp
    av_dict_set(&opts, "stimeout", "60000000", 0);  // ���ó�ʱ3��

    // 1.����Ƶ�ļ�
    if (avformat_open_input(&m_avformatCtx, m_filepath.c_str(), nullptr, &opts) != 0) {
        LogError("can't open the file: %s", m_filepath.c_str());
        goto end;
    }

    // 2.������Ƶ��
    if (avformat_find_stream_info(m_avformatCtx, nullptr) < 0) {
        LogError("Could't find stream infomation");
        goto end;
    }

    // ѭ��������Ƶ�а���������Ϣ
    for (int i = 0; i < m_avformatCtx->nb_streams; i++) {
        if (m_avformatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStreamId = i;
        }
        if (m_avformatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO  && audioStreamId < 0) {
            audioStreamId = i;
        }
    }

    LogDebug("get videoStreamId: %d, audioStreamId: %d", videoStreamId, audioStreamId);

    if (!openVideoDecoder(videoStreamId)) {
        goto end;
    }
    m_playState = VideoPlayer_Playing;
    openAudioDecoder(audioStreamId);

    LogDebug("++++++ video dump info +++++++");
    av_dump_format(m_avformatCtx, 0, m_filepath.c_str(), 0); // �����Ƶ��Ϣ

    readFrame(videoStreamId, audioStreamId);

end:
    m_isReadThreadFinished = true;
    LogInfo("================== read file thread over ==================");
}

bool Videoplayer::openVideoDecoder(const int streamId)
{
    if (streamId < 0) {
        return false;
    }
    // 3.������Ƶ������
    m_videoCodecCtx = m_avformatCtx->streams[streamId]->codec;  // �����Ƶ���Ľ�����������
    if (m_videoCodecCtx == nullptr) {
        LogError("find the avcodec contex failed");
        return false;
    }

    AVCodec *videoDecoder = avcodec_find_decoder(m_videoCodecCtx->codec_id);
    if (videoDecoder == nullptr) {
        LogError("find the decoder failed");
        return false;
    }

    // 4.����Ƶ������
    if (avcodec_open2(m_videoCodecCtx, videoDecoder, nullptr) < 0) {
        LogError("open video codec failed");
        return false;
    }

    m_videoStream = m_avformatCtx->streams[streamId];
    double videoDuration = m_videoStream->duration * av_q2d(m_videoStream->time_base);
    LogInfo("video duration: %f", videoDuration);

    // ����һ���߳�ר������������Ƶ
    std::thread([&](Videoplayer* p) {
        p->decodeVideoThread();
    }, this).detach();

    return true;
}

bool Videoplayer::openSdlAudio()
{
    // ��SDL�������ò��ŵĸ�ʽΪ:AUDIO_S16LSB ˫������44100hz
    // ����ʹ��ffmpeg��������Ƶ����Ҫ�ز����ɺ����һ���ĸ�ʽ�����򲥷Ż�������
    SDL_AudioSpec desiredSpec, obtainedSpec;
    desiredSpec.channels = 2;   // ˫����
    desiredSpec.freq = 44100;
    desiredSpec.format = AUDIO_S16SYS;
    desiredSpec.samples = FFMAX(512, 2 << av_log2(desiredSpec.freq / 30));
    desiredSpec.silence = 0;
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

    LogInfo("audio device id: %d", m_audioDeviceId);
    if (m_audioDeviceId <= 0) {
        //mIsAudioThreadFinished = true;
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

bool Videoplayer::openAudioDecoder(const int streamId)
{
    if (streamId < 0) {
        return false;
    }

    if (SDL_Init(SDL_INIT_AUDIO) != 0) {
        LogError("sdl init for audio failed");
        return false;
    }

    // 3.������Ƶ������
    m_audioCodecCtx = m_avformatCtx->streams[streamId]->codec;  // �����Ƶ���Ľ�����������
    if (m_audioCodecCtx == nullptr) {
        LogError("find the avcodec contex failed");
        return false;
    }

    AVCodec *audioDecoder = avcodec_find_decoder(m_audioCodecCtx->codec_id);
    if (audioDecoder == nullptr) {
        LogError("find the decoder failed");
        return false;
    }

    // 4.����Ƶ������
    if (avcodec_open2(m_audioCodecCtx, audioDecoder, nullptr) < 0) {
        LogError("open video codec failed");
        return false;
    }

    // ��ѹ�����ŵ�����֡�Ķ���
    AVFrame *inFrame = av_frame_alloc();

    // ����Ĳ�����ʽ
    AVSampleFormat in_sample_fmt = m_audioCodecCtx->sample_fmt;
    // ����Ĳ�����ʽ 16bit PCM
    AVSampleFormat out_sample_fmt = AV_SAMPLE_FMT_S16;
    // ����Ĳ�����
    int in_sample_rate = m_audioCodecCtx->sample_rate;
    // ����Ĳ�����
    int out_sample_rate = m_audioCodecCtx->sample_rate;
    // �������������
    uint64_t in_ch_layout = m_audioCodecCtx->channel_layout;
    // �������������
    uint64_t out_ch_layout = AV_CH_LAYOUT_STEREO;
    int outChannelCount = 0;

    do {
        // ����swrcontext�����ļ�
    /*
        m_audioSwrCtx = swr_alloc();
        if (m_audioSwrCtx == nullptr) {
            LogError("swr alloc failed");
            goto end;
        }
    */
        // ��Swrcontext����ռ䣬���ù�������
        m_audioSwrCtx = swr_alloc_set_opts(m_audioSwrCtx, out_ch_layout, out_sample_fmt, out_sample_rate,
                                           in_ch_layout, in_sample_fmt, in_sample_rate, 0, nullptr);
        if (m_audioSwrCtx == nullptr) {
            LogError("swr alloc set opts failed");
            break;
        }
        if (swr_init(m_audioSwrCtx) < 0) {
            LogError("swr init failed");
            break;
        }

        // ��ȡ��������
        outChannelCount = av_get_channel_layout_nb_channels(out_ch_layout);
        LogInfo("audio out channel count: %d", outChannelCount);
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

void Videoplayer::readFrame(const int videoStreamId, const int audioStreamId)
{
    LogDebug("start to read frame, videoStreamId: %d, audioStreamId: %d", videoStreamId, audioStreamId);
    m_videoStartTime = av_gettime();
    LogDebug("videoStartTime: %ld", m_videoStartTime);

    while (1) {
        if (m_isQuit) {
            LogInfo("is quit, break it");
            break;
        }

        if (m_isPause) {
            usleep(10*1000);
            continue;
        }

        // ��΢��Ϣ�� TODO:֡�ʿ���
        if (m_audioPacktList.size() > MAX_AUDIO_SIZE || m_videoPacktList.size() > MAX_VIDEO_SIZE) {
            LogDebug("audio/video pack list size %u > %u | %u > %u",
                     m_audioPacktList.size(), MAX_AUDIO_SIZE, m_videoPacktList.size(), MAX_VIDEO_SIZE);
            usleep(10*1000);
            continue;
        }

        AVPacket packet;
        // ��ȡ�����е���Ƶ����֡������Ƶһ֡
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

        LogDebug("get packet stream index: %d", packet.stream_index);
        if (packet.stream_index == videoStreamId) {
            putVideoPacket(packet);
        } else if( packet.stream_index == audioStreamId) {
            putAudioPacket(packet);
        } else {
            LogWarn("other stream info");
            av_packet_unref(&packet);
        }
    }

    LogInfo("read frame over");
}

bool Videoplayer::putVideoPacket(const AVPacket &pkt)
{
    if (av_dup_packet((AVPacket*)&pkt) < 0) {
        LogError("dup packet failed");
        return false;
    }

    // ������������ͬ��
    std::unique_lock<std::mutex> lock(m_videoMutex);
    m_videoPacktList.push_back(pkt);
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
    if (av_dup_packet((AVPacket*)&pkt) < 0) {
        LogError("dup packet failed");
        return false;
    }

    // ������������ͬ��
    std::unique_lock<std::mutex> lock(m_audioMutex);
    m_audioPacktList.push_back(pkt);
    LogDebug("IN audioPacktList size: %d", m_audioPacktList.size());
    m_audioCondvar.notify_one();

    return true;
}

bool Videoplayer::getAudioPacket(AVPacket& packet)
{
    std::unique_lock<std::mutex> lock(m_audioMutex);
    if (m_audioPacktList.size() <= 0) {
        LogDebug("audioPacktList is empty");
        return false;
    }

    LogDebug("OUT audioPacktList size: %d", m_audioPacktList.size());
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

void Videoplayer::RenderVideo(const uint8_t *videoBuffer, const int width, const int height)
{
    if (m_videoPlayerCallBack != nullptr) {
        m_videoPlayerCallBack->onDisplayVideo(videoBuffer, width, height);
        return;
    }
}
