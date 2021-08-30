#include "VideoPlayer.h"

// ��Ƶ�������߳�
void Videoplayer::decodeVideoThread()
{
    LogDebug("================== %s start ==================", __FUNCTION__);
    usleep(1000);   // �����������
    m_isVideoDecodeFinished = false;

    // ������Ƶ���
    AVCodecContext *pCodecCtx = m_videoStream->codec; // ��Ƶ������

    while(1) {
        if (m_isQuit) {
            LogDebug("is quited");
            break;
        }

        if (m_isPause == true) {            
            usleep(10*1000);
            continue;
        }

        // 1.�Ӷ����л�ȡpackage
        AVPacket tmpPkt;
        if (!getVideoPacket(tmpPkt)) {
            if (m_isReadThreadFinished) {
                LogInfo("decode no packet in queue, break it");
                break;
            } else {
                usleep(10*1000); // ����ֻ����ʱû�����ݶ���
                continue;
            }
        }

        AVPacket *packet = &tmpPkt;
        // �յ�������� ˵���ո�ִ�й���ת ������Ҫ�ѽ����������� ���һ��
        if (packet->data != NULL && strcmp((char*)packet->data, FLUSH_DATA) == 0) {
            LogWarn("flush buffers");
            avcodec_flush_buffers(m_videoStream->codec);
            av_packet_unref(packet);
            continue;
        }

        // 2.���������뵽������
        if (avcodec_send_packet(pCodecCtx, packet) != 0) {
           LogError("input AVPacket to decoder failed!\n");
           av_packet_unref(packet);
           continue;
        }

        // 3.����
        decodeFrame(pCodecCtx, packet);
        av_packet_unref(packet);
    }

    LogDebug("================== decode over ==================");

    if (!m_isQuit) {
        m_isQuit = true;
    }

    m_isVideoDecodeFinished = true;

    LogDebug("%s finished", __FUNCTION__);
    return;
}

void Videoplayer::decodeFrame(AVCodecContext *pCodecCtx, AVPacket *packet)
{
    AVFrame *videoFrame = av_frame_alloc();;
    AVFrame *rgbFrame = nullptr;
    struct SwsContext *imgConvertCtx = nullptr;
    double videoPts = 0; // ��ǰ��Ƶ��pts��ʱ����ʾ��
    double audioPts = 0; // ��Ƶpts
    int videoWidth  = 0;
    int videoHeight =  0;

    while (0 == avcodec_receive_frame(pCodecCtx, videoFrame)) {
        LogDebug("video start: %ld, pts: %ld, %ld, %ld, dts: %ld, %ld",
                 m_videoStartTime, packet->pts, videoFrame->pts, videoFrame->pkt_pts, packet->dts, videoFrame->pkt_dts);

        if (packet->dts == AV_NOPTS_VALUE && videoFrame->opaque && *(uint64_t*) videoFrame->opaque != AV_NOPTS_VALUE) {
            videoPts = *(uint64_t *) videoFrame->opaque;
        } else if (packet->dts != AV_NOPTS_VALUE) {
            videoPts = packet->dts;
        } else {
            videoPts = 0;
        }

        //double timestamp = av_frame_get_best_effort_timestamp(videoFrame) * av_q2d(m_videoStream->time_base);
        videoPts *= av_q2d(m_videoStream->time_base);

        // ����Ƶͬ��
        while (true) {
            if (m_isQuit) {
                break;
            }
            if (m_audioStream != nullptr) {
                if (m_isReadThreadFinished && m_audioPacktList.empty()) {
                    // ����Ƶ������ͬ��
                    break;
                }
                audioPts = m_audioClock;
            } else {
                // ����Ƶ����ͬ�����ⲿʱ��
                audioPts = (av_gettime() - m_videoStartTime) / 1000000.0;   // ��
                m_audioClock = audioPts;
            }

            LogDebug("video pts: %f, audio pts: %f", videoPts, audioPts);
            if (videoPts <= audioPts) {
                break;
            }

            int delayTime = (videoPts - audioPts) * 1000;   // ����
            delayTime = delayTime > 5 ? 5 : delayTime;
            usleep(delayTime * 1000);
        }


        //LogDebug("width: %d | %d, height: %d | %d", videoFrame->width, videoWidth, videoFrame->height, videoHeight);
        if (videoFrame->width != videoWidth || videoFrame->height != videoHeight) {
            videoWidth  = videoFrame->width;
            videoHeight = videoFrame->height;
        }

        if (rgbFrame != nullptr) {
            av_free(rgbFrame);
        }

        if (imgConvertCtx != nullptr) {
            sws_freeContext(imgConvertCtx);
        }

        rgbFrame = av_frame_alloc();
        if (!rgbFrame) {
            LogError("frame alloc YUV failed");
            av_packet_unref(packet);
            break;
        }

        // ��Ҫ��rgbFrame->data����ÿռ䣬����sws_scale����dst pointer����
        av_image_alloc(rgbFrame->data,
            rgbFrame->linesize,
            videoWidth, videoHeight,
            AV_PIX_FMT_RGB32,
            1);

        // ������������ת����RGB32
        imgConvertCtx = sws_getContext(videoWidth, videoHeight,
                (AVPixelFormat)videoFrame->format, videoWidth, videoHeight,
                AV_PIX_FMT_RGB32, SWS_BICUBIC, NULL, NULL, NULL);

        sws_scale(imgConvertCtx,
                (const uint8_t* const*)videoFrame->data,
                videoFrame->linesize, 0, videoHeight, rgbFrame->data,
                rgbFrame->linesize);

        // �������ݸ�Qt������Ⱦ
        RenderVideo(rgbFrame->data[0], videoWidth, videoHeight);
    }

    if (videoFrame != nullptr) {
        av_free(videoFrame);
    }

    if (rgbFrame != nullptr) {
        av_free(rgbFrame);
    }

    if (imgConvertCtx != nullptr) {
        sws_freeContext(imgConvertCtx);
    }
}
