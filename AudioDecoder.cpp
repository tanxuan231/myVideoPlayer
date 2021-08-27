#include "VideoPlayer.h"

// SDL回调函数
void Videoplayer::sdlAudioCallBackFunc(void *userdata, Uint8 *stream, int len)
{
    Videoplayer *player = (Videoplayer*)userdata;
    player->sdlAudioCallBack(stream, len);
}

static const int MAX_AUDIO_FRAME_SIZE = 192000;

void Videoplayer::sdlAudioCallBack(Uint8 *stream, int len)
{
    LogDebug("========== %s start ============", __FUNCTION__);
    int copyLen, decodeDataSize;
    static uint8_t decodeBuf[(MAX_AUDIO_FRAME_SIZE * 3) / 2] = {0};
    static unsigned int decodeBufSize = 0;
    static unsigned int decodeBufIndex = 0;

    memset(stream, 0, len);
    //decodeBufSize = 0;
    //decodeBufIndex = 0;
    LogDebug("decode buf size: %d, index: %d", decodeBufSize, decodeBufIndex);

    // 向设备发送长度为len的数据
    while (len > 0) {
        if (decodeBufIndex >= decodeBufSize) {
            // 缓冲区中无数据, 从音频队列中解码数据
            decodeDataSize = decodeAudioFrame(decodeBuf);

            if (decodeDataSize <= 0) {
                // 没有解码数据，默认播放静音
                decodeBufSize = 1024;
                memset(decodeBuf, 0, decodeBufSize);
            } else {
                decodeBufSize = decodeDataSize;
            }
            decodeBufIndex = 0;
        }

        copyLen = decodeBufSize - decodeBufIndex;    // 缓冲区中剩下的数据长度
        if (copyLen > len) {
            copyLen = len; // 一次最多len长度的数据
        }

        memcpy(stream, (uint8_t *)decodeBuf + decodeBufIndex, copyLen);
        SDL_MixAudio(stream, decodeBuf + decodeBufIndex, len, SDL_MIX_MAXVOLUME);

        len -= copyLen;
        stream += copyLen;
        decodeBufIndex += copyLen;
    }
}

int Videoplayer::decodeAudioFrame(uint8_t *decodeBuf)
{
    AVFrame *decodeFrame = av_frame_alloc();
    if (!decodeFrame) {
        return -1;
    }

    int decodeSize = 0;
    SwrContext *swr_ctx = nullptr;
    AVPacket *packet = nullptr;

    while(1) {
        if (m_isQuit) {
            LogInfo("is quit");
            //m_isAudioDecodeFinished = true;
            //clearAudioQueue();
            //break;
        }

        if (m_isPause == true) {
            LogInfo("is pause");
            break;
        }

        AVPacket tmpPkt;
        if (!getAudioPacket(tmpPkt)) {
            //usleep(1000);
            break;
        }

        packet = &tmpPkt;
        if (packet->pts != AV_NOPTS_VALUE) {
            LogDebug("there no pts value");
            m_audioClock = av_q2d(m_audioStream->time_base) * packet->pts;
        }

        //收到这个数据 说明刚刚执行过跳转 现在需要把解码器的数据 清除一下
        if (strcmp((char*)packet->data, FLUSH_DATA) == 0) {
            avcodec_flush_buffers(m_audioStream->codec);
            av_packet_unref(packet);
            continue;
        }

        int ret = avcodec_send_packet(m_audioCodecCtx, packet);
        if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
            LogError("codec send packet failed, ret: %d", ret);
            break;
        }

        ret = avcodec_receive_frame(m_audioCodecCtx, decodeFrame);
        if (ret < 0 && ret != AVERROR_EOF) {
            LogError("codec receive frame failed, ret: %d", ret);
            break;
        }

        int index = av_get_channel_layout_channel_index(av_get_default_channel_layout(4), AV_CH_FRONT_CENTER);

        // 设置通道数或channel_layout
        if (decodeFrame->channels > 0 && decodeFrame->channel_layout == 0) {
            decodeFrame->channel_layout = av_get_default_channel_layout(decodeFrame->channels);
        } else if (decodeFrame->channels == 0 && decodeFrame->channel_layout > 0) {
            decodeFrame->channels = av_get_channel_layout_nb_channels(decodeFrame->channel_layout);
        }

        AVSampleFormat dst_format = AV_SAMPLE_FMT_S16;//av_get_packed_sample_fmt((AVSampleFormat)decodeFrame->format);
        Uint64 dst_layout = av_get_default_channel_layout(decodeFrame->channels);
        // 设置转换参数
        swr_ctx = swr_alloc_set_opts(nullptr, dst_layout, dst_format, decodeFrame->sample_rate,
            decodeFrame->channel_layout, (AVSampleFormat)decodeFrame->format, decodeFrame->sample_rate, 0, nullptr);
        if (!swr_ctx) {
            LogError("swr alloc set options failed");
            break;
        }

        if (swr_init(swr_ctx) < 0) {
            LogError("swr init failed");
            break;
        }

        // 计算转换后的sample个数 a * b / c
        int dst_nb_samples = av_rescale_rnd(swr_get_delay(swr_ctx, decodeFrame->sample_rate) + decodeFrame->nb_samples,
                                            decodeFrame->sample_rate, decodeFrame->sample_rate, AVRounding(1));
        // 转换，返回值为转换后的sample个数
        int nb = swr_convert(swr_ctx, &decodeBuf, dst_nb_samples, (const uint8_t**)decodeFrame->data, decodeFrame->nb_samples);
        decodeSize = decodeFrame->channels * nb * av_get_bytes_per_sample(dst_format);

        break;
    }

    if (packet != nullptr) {
        av_packet_unref(packet);
    }
    av_frame_free(&decodeFrame);

    if (decodeBuf != nullptr) {
        swr_free(&swr_ctx);
        swr_ctx = nullptr;
    }

    LogDebug("get decode size: %d", decodeSize);
    return decodeSize;
}

int Videoplayer::decodeAudioFrame2()
{
/*
    AVFrame *decodeFrame = av_frame_alloc();
    if (!decodeFrame) {
        return 0;
    }

    int audioBufferSize = 0;

    while(1) {
        if (m_isQuit) {
            mIsAudioThreadFinished = true;
            clearAudioQuene();
            break;
        }

        if (m_isPause == true) {
            break;
        }

        AVPacket tmpPkt;
        if (!getAudioPacket(tmpPkt)) {
            usleep(1000);
            continue;
        }

        AVPacket *packet = &tmpPkt;
        if (packet->pts != AV_NOPTS_VALUE) {
            LogWarn("there no pts value");
            m_audioClock = av_q2d(m_audioStream->time_base) * packet->pts;
        }

        //收到这个数据 说明刚刚执行过跳转 现在需要把解码器的数据 清除一下
        if (strcmp((char*)packet->data, FLUSH_DATA) == 0) {
            avcodec_flush_buffers(m_audioStream->codec);
            av_packet_unref(packet);
            continue;
        }

        //解码AVPacket->AVFrame
        int got_frame = 0;
        int size = avcodec_decode_audio4(m_audioCodecCtx, decodeFrame, &got_frame, packet);
        av_packet_unref(packet);

        if (got_frame)
        {
            // 新的FFmepg3中audio增加了平面（planar）格式，而SDL播放音频是不支持平面格式的
            // 需要转换后送入SDL
            // 计算转换后的sample个数
            int nb_samples = av_rescale_rnd(swr_get_delay(m_audioSwrCtx, m_audioCodecCtx->sample_rate) + decodeFrame->nb_samples,
                    m_audioCodecCtx->sample_rate, m_audioCodecCtx->sample_rate, AVRounding(1));

            if (m_auidoFrameSample != nullptr)
            {
                if (m_auidoFrameSample->nb_samples != nb_samples)
                {
                    av_frame_free(&m_auidoFrameSample);
                    m_auidoFrameSample = nullptr;
                }
            }

            ///解码一帧后才能获取到采样率等信息，因此将初始化放到这里
            if (m_auidoFrameSample == nullptr)
            {
                m_auidoFrameSample = av_frame_alloc();

                m_auidoFrameSample->format = out_sample_fmt;
                m_auidoFrameSample->channel_layout = out_ch_layout;
                m_auidoFrameSample->sample_rate = out_sample_rate;
                m_auidoFrameSample->nb_samples = nb_samples;

                int ret = av_samples_fill_arrays(m_auidoFrameSample->data, m_auidoFrameSample->linesize, audio_buff, audio_tgt_channels, m_auidoFrameSample->nb_samples, out_sample_fmt, 0);
//                int ret = av_frame_get_buffer(m_auidoFrameSample, 0);
                if (ret < 0)
                {
                    fprintf(stderr, "Error allocating an audio buffer\n");
//                        exit(1);
                }
            }

            int len2 = swr_convert(m_audioSwrCtx,
                                   m_auidoFrameSample->data,
                                   m_auidoFrameSample->nb_samples,
                                   (const uint8_t**)decodeFrame->data,
                                   decodeFrame->nb_samples);


            int resampled_data_size = av_samples_get_buffer_size(NULL, audio_tgt_channels, m_auidoFrameSample->nb_samples, out_sample_fmt, 1);

            audioBufferSize = resampled_data_size;
            break;
        }
    }

    return audioBufferSize;
*/
    return 0;
}
