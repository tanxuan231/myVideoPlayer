#include "VideoPlayer.h"

// ��Ƶ�������߳�
void Videoplayer::decodeVideoThread()
{
    LogDebug("================== %s start ==================", __FUNCTION__);
    usleep(1000);   // �����������
    m_isVideoDecodeFinished = false;

    // ������Ƶ���
    AVCodecContext *pCodecCtx = m_videoStream->codec; // ��Ƶ������
    AVFrame *pFrame = av_frame_alloc();;

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
        AVPacket pkt1;
        if (!getVideoPacket(pkt1)) {
            if (m_isReadThreadFinished) {
                LogInfo("decode no packet in queue, break it");
                break;
            } else {
                usleep(10*1000); // ����ֻ����ʱû�����ݶ���
                continue;
            }
        }

        AVPacket *packet = &pkt1;
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
        decodeFrame(pCodecCtx, pFrame, packet);
        av_packet_unref(packet);
    }

    LogDebug("================== decode over ==================");
    if (pFrame != nullptr) {
        av_free(pFrame);
    }

    if (!m_isQuit) {
        m_isQuit = true;
    }

    m_isVideoDecodeFinished = true;

    LogDebug("%s finished", __FUNCTION__);

    return;
}

void Videoplayer::decodeFrame(AVCodecContext *pCodecCtx, AVFrame *pFrame, AVPacket *packet)
{
    AVFrame *rgbFrame = nullptr;
    struct SwsContext *imgConvertCtx = nullptr;
    double video_pts = 0; // ��ǰ��Ƶ��pts��ʱ����ʾ��
    double audio_pts = 0; // ��Ƶpts
    int videoWidth  = 0;
    int videoHeight =  0;

    while (0 == avcodec_receive_frame(pCodecCtx, pFrame)) {
        if (packet->dts == AV_NOPTS_VALUE && pFrame->opaque && *(uint64_t*) pFrame->opaque != AV_NOPTS_VALUE) {
            video_pts = *(uint64_t *) pFrame->opaque;
        } else if (packet->dts != AV_NOPTS_VALUE) {
            video_pts = packet->dts;
        } else {
            video_pts = 0;
        }

        video_pts *= av_q2d(m_videoStream->time_base);

        LogDebug("width: %d | %d, height: %d | %d", pFrame->width, videoWidth, pFrame->height, videoHeight);
        if (pFrame->width != videoWidth || pFrame->height != videoHeight) {
            videoWidth  = pFrame->width;
            videoHeight = pFrame->height;
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
                (AVPixelFormat)pFrame->format, videoWidth, videoHeight,
                AV_PIX_FMT_RGB32, SWS_BICUBIC, NULL, NULL, NULL);

        sws_scale(imgConvertCtx,
                (const uint8_t* const*)pFrame->data,
                pFrame->linesize, 0, videoHeight, rgbFrame->data,
                rgbFrame->linesize);

        // �������ݸ�Qt������Ⱦ
        RenderVideo(rgbFrame->data[0], videoWidth, videoHeight);
    }

    if (rgbFrame != nullptr) {
        av_free(rgbFrame);
    }

    if (imgConvertCtx != nullptr) {
        sws_freeContext(imgConvertCtx);
    }
}
