#include "VideoPlayer.h"

// ��Ƶ�������߳�
void Videoplayer::decodeVideoThread()
{
    LogDebug("================== %s start ==================", __FUNCTION__);
    usleep(1000);   // �����������

    m_isVideoThreadFinished = false;

    int videoWidth  = 0;
    int videoHeight =  0;

    double video_pts = 0; // ��ǰ��Ƶ��pts��ʱ����ʾ��
    double audio_pts = 0; // ��Ƶpts

    // ������Ƶ���
    AVFrame *pFrame = nullptr;
    AVFrame *rgbFrame = nullptr;
    uint8_t *outBuffer = nullptr; // ����������
    struct SwsContext *imgConvertCtx = nullptr;  // ���ڽ�������Ƶ��ʽת��

    AVCodecContext *pCodecCtx = m_videoStream->codec; // ��Ƶ������
    pFrame = av_frame_alloc();

    while(1) {
        if (m_isQuit) {
            LogDebug("is quited");
            clearVideoQuene();
            break;
        }

        if (m_isPause == true) {
            LogDebug("is pause");
            usleep(10*1000);
            continue;
        }

        // 1.�Ӷ����л�ȡpackage
/*
        AVPacket pkt1;
        {
            std::unique_lock<std::mutex> lock(m_videoMutex);
            if (m_videoPacktList.size() <= 0) {
                //LogDebug("video package list is empty");
                //if (mIsReadFinished) {
                    // ��������û���������Ҷ�ȡ�����
                    //break;
                //} else {
                    usleep(10*1000); // ����ֻ����ʱû�����ݶ���
                    continue;
                //}
            } else {
                //LogDebug("get a video package");
            }

            LogDebug("OUT videoPacktList size: %d", m_videoPacktList.size());
            pkt1 = m_videoPacktList.front();
            m_videoPacktList.pop_front();
        }
*/
        AVPacket pkt1;
        if (!getVideoPacket(pkt1)) {
            //if (mIsReadFinished) {
                // ��������û���������Ҷ�ȡ�����
                //break;
            //} else {
                usleep(10*1000); // ����ֻ����ʱû�����ݶ���
                continue;
            //}
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
        while (0 == avcodec_receive_frame(pCodecCtx, pFrame)) {
            if (packet->dts == AV_NOPTS_VALUE && pFrame->opaque && *(uint64_t*) pFrame->opaque != AV_NOPTS_VALUE) {
                video_pts = *(uint64_t *) pFrame->opaque;
            } else if (packet->dts != AV_NOPTS_VALUE) {
                video_pts = packet->dts;
            } else {
                video_pts = 0;
            }

            video_pts *= av_q2d(m_videoStream->time_base);
            //video_clock = video_pts;

            LogDebug("width: %d | %d, height: %d | %d", pFrame->width, videoWidth, pFrame->height, videoHeight);
            if (pFrame->width != videoWidth || pFrame->height != videoHeight) {
                videoWidth  = pFrame->width;
                videoHeight = pFrame->height;
            }
            if (rgbFrame != nullptr) {
                av_free(rgbFrame);
            }

            if (outBuffer != nullptr) {
                av_free(outBuffer);
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
/*
            LogDebug("start to av_image_get_buffer_size");
            //int yuvSize = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, videoWidth, videoHeight, 1);  // ��1�ֽڽ����ڴ����,�õ����ڴ��С��ӽ�ʵ�ʴ�С
            int yuvSize = av_image_get_buffer_size(AV_PIX_FMT_RGB32, videoWidth, videoHeight, 1);  // ��1�ֽڽ����ڴ����,�õ����ڴ��С��ӽ�ʵ�ʴ�С
            //int yuvSize = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height, 0);  //��0�ֽڽ����ڴ���룬�õ����ڴ��С��0
            //int yuvSize = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height, 4);   //��4�ֽڽ����ڴ���룬�õ����ڴ��С��΢��һЩ

            unsigned int numBytes = static_cast<unsigned int>(yuvSize);
            LogDebug("yuvsize: %d, numBytes: %u", yuvSize, numBytes);
            outBuffer = static_cast<uint8_t *>(av_malloc(numBytes * sizeof(uint8_t)));
            if (!outBuffer) {
                //av_image_fill_arrays(rgbFrame->data, rgbFrame->linesize, outBuffer, AV_PIX_FMT_RGBA, videoWidth, videoHeight, 1);
            }
*/
            av_image_alloc(rgbFrame->data,
                rgbFrame->linesize,
                videoWidth, videoHeight,
                AV_PIX_FMT_RGB32,
                1);

            // ���ڽ��������ݲ�һ������yuv420p�������Ҫ������������ͳһת����YUV420P
            //LogDebug("src image format: %d", pFrame->format);
            imgConvertCtx = sws_getContext(videoWidth, videoHeight,
                    (AVPixelFormat)pFrame->format, videoWidth, videoHeight,
                    AV_PIX_FMT_RGB32, SWS_BICUBIC, NULL, NULL, NULL);

            LogDebug("start to sws_scale");
            sws_scale(imgConvertCtx,
                    (const uint8_t* const*)pFrame->data,
                    pFrame->linesize, 0, videoHeight, rgbFrame->data,
                    rgbFrame->linesize);

            RenderVideo(rgbFrame->data[0], videoWidth, videoHeight);
        }

        av_packet_unref(packet);
    }
    LogDebug("================== decode over ==================");

    av_free(pFrame);

    if (rgbFrame != nullptr)
    {
        av_free(rgbFrame);
    }

    if (outBuffer != nullptr)
    {
        av_free(outBuffer);
    }

    if (imgConvertCtx != nullptr)
    {
        sws_freeContext(imgConvertCtx);
    }

    if (!m_isQuit)
    {
        m_isQuit = true;
    }

    m_isVideoThreadFinished = true;

    LogDebug("%s finished", __FUNCTION__);

    return;
}
