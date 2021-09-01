#include "VideoPlayer.h"

// SDL回调函数
void Videoplayer::sdlAudioCallBackFunc(void *userdata, Uint8 *stream, int len)
{
    Videoplayer *player = (Videoplayer*)userdata;
    player->sdlAudioCallBack(stream, len);
}

void Videoplayer::sdlAudioCallBack(Uint8 *stream, int len)
{
    LogDebug("========== %s start ============", __FUNCTION__);
    int copyLen, decodeDataSize;
    memset(stream, 0, len);
    LogDebug("len: %d, decode buf size: %d, index: %d", len, m_audioDecodeBufSize, m_audioDecodeBufIndex);

    if (m_audioDecodeBuf == nullptr) {
        return;
    }

    // 向设备发送长度为len的解码后的数据
    while (len > 0) {
        if (m_audioDecodeBufIndex >= m_audioDecodeBufSize) {
            // 缓冲区中无数据, 从音频队列中解码数据
            decodeDataSize = decodeAudioFrame(m_audioDecodeBuf);

            if (decodeDataSize <= 0) {
                // 没有解码数据，默认播放静音
                m_audioDecodeBufSize = 1024;
                memset(m_audioDecodeBuf, 0, m_audioDecodeBufSize);
            } else {
                m_audioDecodeBufSize = decodeDataSize;
            }
            m_audioDecodeBufIndex = 0;
        }

        copyLen = m_audioDecodeBufSize - m_audioDecodeBufIndex;    // 缓冲区中剩下的数据长度
        if (copyLen > len) {
            copyLen = len; // 一次最多len长度的数据
        }

        memcpy(stream, (uint8_t *)m_audioDecodeBuf + m_audioDecodeBufIndex, copyLen);
        SDL_MixAudio(stream, m_audioDecodeBuf + m_audioDecodeBufIndex, len, SDL_MIX_MAXVOLUME);

        len -= copyLen;
        stream += copyLen;
        m_audioDecodeBufIndex += copyLen;
    }
}

int Videoplayer::decodeAudioFrame(uint8_t *decodeBuf)
{
    AVFrame *audioFrame = av_frame_alloc();
    if (!audioFrame) {
        return -1;
    }

    int decodeSize = 0;
    AVPacket *packet = nullptr;

    do {
        if (m_isQuit) {
            LogDebug("is quit");
            break;
        }

        if (m_isPause) {
            LogDebug("is pause");
            break;
        }

        AVPacket tmpPkt;
        if (!getAudioPacket(tmpPkt)) {
            break;
        }

        packet = &tmpPkt;
        if (packet->pts != AV_NOPTS_VALUE) {
            m_audioCurPts = av_q2d(m_audioStream->time_base) * packet->pts;
        } else {
            LogWarn("there no audio pts value");
        }

        LogDebug("audio pts: %f", m_audioCurPts);

        int ret = avcodec_send_packet(m_audioCodecCtx, packet);
        if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
            LogError("codec send packet failed, ret: %d", ret);
            break;
        }

        ret = avcodec_receive_frame(m_audioCodecCtx, audioFrame);
        if (ret < 0 && ret != AVERROR_EOF) {
            LogError("codec receive frame failed, ret: %d", ret);
            break;
        }
        //LogDebug("audioFrame best timestamp: %f", audioFrame->best_effort_timestamp*av_q2d(m_audioStream->time_base));

        // 设置通道数或channel_layout
        if (audioFrame->channels > 0 && audioFrame->channel_layout == 0) {
            audioFrame->channel_layout = av_get_default_channel_layout(audioFrame->channels);
        } else if (audioFrame->channels == 0 && audioFrame->channel_layout > 0) {
            audioFrame->channels = av_get_channel_layout_nb_channels(audioFrame->channel_layout);
        }

        decodeSize = convert2pcm(audioFrame, decodeBuf);
    } while(false);

    if (packet != nullptr) {
        av_packet_unref(packet);
    }
    av_frame_free(&audioFrame);

    //LogDebug("get decode size: %d", decodeSize);
    return decodeSize;
}

int Videoplayer::convert2pcm(AVFrame* audioFrame, uint8_t *decodeBuf)
{
    if (m_audioSwrCtx == nullptr) {
        LogError("audio swr context is null");
        return -1;
    }

    int decodeSize = 0;
    do {
        // 计算转换后的sample个数 a * b / c
        int dstSamplesNum = av_rescale_rnd(swr_get_delay(m_audioSwrCtx, audioFrame->sample_rate) + audioFrame->nb_samples,
                                            audioFrame->sample_rate, audioFrame->sample_rate, AVRounding(1));
        // 转换，返回值为转换后的sample个数
        int nb = swr_convert(m_audioSwrCtx, &decodeBuf, dstSamplesNum, (const uint8_t**)audioFrame->data, audioFrame->nb_samples);
        decodeSize = audioFrame->channels * nb * av_get_bytes_per_sample(AV_SAMPLE_FMT_S16);

        // 每秒钟音频播放的字节数 sample_rate * channels * sample_format(一个sample占用的字节数)
        //m_audioCurPts += static_cast<double>(decodeSize) / (2 * m_audioStream->codec->channels * m_audioStream->codec->sample_rate);
    } while (false);

    return decodeSize;
}

double Videoplayer::getAudioClock()
{
    int usedBufSize = m_audioDecodeBufSize - m_audioDecodeBufIndex;
    int bytesPerSec = m_audioStream->codec->sample_rate * m_audioCodecCtx->channels * 2;
    double pts = m_audioCurPts - static_cast<double>(usedBufSize) / bytesPerSec;

    return pts;
}

int Videoplayer::convert2pcm2(AVFrame* audioFrame, uint8_t *decodeBuf)
{
    SwrContext *swrCtx = nullptr;
    Uint64 dstLayout = av_get_default_channel_layout(audioFrame->channels);
    AVSampleFormat dstFormat = AV_SAMPLE_FMT_S16;

    // 设置转换参数
    LogDebug("swr_alloc_set_opts: outChannelLayout: %lu, outSampleRate: %d", dstLayout, audioFrame->sample_rate);
    LogDebug("swr_alloc_set_opts: inChannelLayout: %lu, inSampleFmt: %d, inSampleRate: %d",
             audioFrame->channel_layout, audioFrame->format, audioFrame->sample_rate);
    swrCtx = swr_alloc_set_opts(nullptr, dstLayout, dstFormat, audioFrame->sample_rate,
                                audioFrame->channel_layout, (AVSampleFormat)audioFrame->format, audioFrame->sample_rate,
                                0, nullptr);
    if (!swrCtx) {
        LogError("swr alloc set options failed");
        return -1;
    }

    int decodeSize = 0;
    do {
        if (swr_init(swrCtx) < 0) {
            LogError("swr init failed");
            break;
        }

        // 计算转换后的sample个数 a * b / c
        int dstSamplesNum = av_rescale_rnd(swr_get_delay(swrCtx, audioFrame->sample_rate) + audioFrame->nb_samples,
                                            audioFrame->sample_rate, audioFrame->sample_rate, AVRounding(1));
        // 转换，返回值为转换后的sample个数
        int nb = swr_convert(swrCtx, &decodeBuf, dstSamplesNum, (const uint8_t**)audioFrame->data, audioFrame->nb_samples);
        decodeSize = audioFrame->channels * nb * av_get_bytes_per_sample(dstFormat);

        // 每秒钟音频播放的字节数 sample_rate * channels * sample_format(一个sample占用的字节数)
        //m_audioClock += static_cast<double>(decodeSize) / (2 * m_audioStream->codec->channels * m_audioStream->codec->sample_rate);
    } while (false);

    if (swrCtx != nullptr) {
        swr_free(&swrCtx);
        swrCtx = nullptr;
    }

    return decodeSize;
}
