#include "VideoPlayer.h"

double Videoplayer::getAudioClock()
{
    int hw_buf_size = m_audioDecodeBufSize - m_audioDecodeBufIndex;
    int bytes_per_sec = m_audioCodecCtx->sample_rate * m_audioCodecCtx->channels * 2;
    double pts = m_audioClock - static_cast<double>(hw_buf_size) / bytes_per_sec;

    return pts;
}

// SDL�ص�����
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
    LogDebug("decode buf size: %d, index: %d", m_audioDecodeBufSize, m_audioDecodeBufIndex);

    // ���豸���ͳ���Ϊlen������
    while (len > 0) {
        if (m_audioDecodeBufIndex >= m_audioDecodeBufSize) {
            // ��������������, ����Ƶ�����н�������
            decodeDataSize = decodeAudioFrame(m_audioDecodeBuf);

            if (decodeDataSize <= 0) {
                // û�н������ݣ�Ĭ�ϲ��ž���
                m_audioDecodeBufSize = 1024;
                memset(m_audioDecodeBuf, 0, m_audioDecodeBufSize);
            } else {
                m_audioDecodeBufSize = decodeDataSize;
            }
            m_audioDecodeBufIndex = 0;
        }

        copyLen = m_audioDecodeBufSize - m_audioDecodeBufIndex;    // ��������ʣ�µ����ݳ���
        if (copyLen > len) {
            copyLen = len; // һ�����len���ȵ�����
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

        //�յ�������� ˵���ո�ִ�й���ת ������Ҫ�ѽ����������� ���һ��
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

        ret = avcodec_receive_frame(m_audioCodecCtx, audioFrame);
        if (ret < 0 && ret != AVERROR_EOF) {
            LogError("codec receive frame failed, ret: %d", ret);
            break;
        }

        int index = av_get_channel_layout_channel_index(av_get_default_channel_layout(4), AV_CH_FRONT_CENTER);

        // ����ͨ������channel_layout
        if (audioFrame->channels > 0 && audioFrame->channel_layout == 0) {
            audioFrame->channel_layout = av_get_default_channel_layout(audioFrame->channels);
        } else if (audioFrame->channels == 0 && audioFrame->channel_layout > 0) {
            audioFrame->channels = av_get_channel_layout_nb_channels(audioFrame->channel_layout);
        }

        AVSampleFormat dst_format = AV_SAMPLE_FMT_S16;//av_get_packed_sample_fmt((AVSampleFormat)audioFrame->format);
        Uint64 dst_layout = av_get_default_channel_layout(audioFrame->channels);
        // ����ת������
        swr_ctx = swr_alloc_set_opts(nullptr, dst_layout, dst_format, audioFrame->sample_rate,
            audioFrame->channel_layout, (AVSampleFormat)audioFrame->format, audioFrame->sample_rate, 0, nullptr);
        if (!swr_ctx) {
            LogError("swr alloc set options failed");
            break;
        }

        if (swr_init(swr_ctx) < 0) {
            LogError("swr init failed");
            break;
        }

        // ����ת�����sample���� a * b / c
        int dst_nb_samples = av_rescale_rnd(swr_get_delay(swr_ctx, audioFrame->sample_rate) + audioFrame->nb_samples,
                                            audioFrame->sample_rate, audioFrame->sample_rate, AVRounding(1));
        // ת��������ֵΪת�����sample����
        int nb = swr_convert(swr_ctx, &decodeBuf, dst_nb_samples, (const uint8_t**)audioFrame->data, audioFrame->nb_samples);
        decodeSize = audioFrame->channels * nb * av_get_bytes_per_sample(dst_format);

        // ÿ������Ƶ���ŵ��ֽ��� sample_rate * channels * sample_format(һ��sampleռ�õ��ֽ���)
        m_audioClock += static_cast<double>(decodeSize) / (2 * m_audioStream->codec->channels * m_audioStream->codec->sample_rate);

        break;
    }

    if (packet != nullptr) {
        av_packet_unref(packet);
    }
    av_frame_free(&audioFrame);

    if (swr_ctx != nullptr) {
        swr_free(&swr_ctx);
        swr_ctx = nullptr;
    }

    LogDebug("get decode size: %d", decodeSize);
    return decodeSize;
}

int Videoplayer::decodeAudioFrame2()
{
/*
    AVFrame *audioFrame = av_frame_alloc();
    if (!audioFrame) {
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

        //�յ�������� ˵���ո�ִ�й���ת ������Ҫ�ѽ����������� ���һ��
        if (strcmp((char*)packet->data, FLUSH_DATA) == 0) {
            avcodec_flush_buffers(m_audioStream->codec);
            av_packet_unref(packet);
            continue;
        }

        //����AVPacket->AVFrame
        int got_frame = 0;
        int size = avcodec_decode_audio4(m_audioCodecCtx, audioFrame, &got_frame, packet);
        av_packet_unref(packet);

        if (got_frame)
        {
            // �µ�FFmepg3��audio������ƽ�棨planar����ʽ����SDL������Ƶ�ǲ�֧��ƽ���ʽ��
            // ��Ҫת��������SDL
            // ����ת�����sample����
            int nb_samples = av_rescale_rnd(swr_get_delay(m_audioSwrCtx, m_audioCodecCtx->sample_rate) + audioFrame->nb_samples,
                    m_audioCodecCtx->sample_rate, m_audioCodecCtx->sample_rate, AVRounding(1));

            if (m_auidoFrameSample != nullptr)
            {
                if (m_auidoFrameSample->nb_samples != nb_samples)
                {
                    av_frame_free(&m_auidoFrameSample);
                    m_auidoFrameSample = nullptr;
                }
            }

            ///����һ֡����ܻ�ȡ�������ʵ���Ϣ����˽���ʼ���ŵ�����
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
                                   (const uint8_t**)audioFrame->data,
                                   audioFrame->nb_samples);


            int resampled_data_size = av_samples_get_buffer_size(NULL, audio_tgt_channels, m_auidoFrameSample->nb_samples, out_sample_fmt, 1);

            audioBufferSize = resampled_data_size;
            break;
        }
    }

    return audioBufferSize;
*/
    return 0;
}
