#include "VideoPlayer.h"

// 视频解码子线程
void Videoplayer::decodeVideoThread()
{
    LogDebug("================== %s start ==================", __FUNCTION__);
    usleep(1000);   // 等数据入队列
    m_isVideoDecodeFinished = false;

    // 解码视频相关
    AVCodecContext *pCodecCtx = m_videoStream->codec; // 视频解码器
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

        // 1.从队列中获取package
        AVPacket pkt1;
        if (!getVideoPacket(pkt1)) {
            if (m_isReadThreadFinished) {
                LogInfo("decode no packet in queue, break it");
                break;
            } else {
                usleep(10*1000); // 队列只是暂时没有数据而已
                continue;
            }
        }

        AVPacket *packet = &pkt1;
        // 收到这个数据 说明刚刚执行过跳转 现在需要把解码器的数据 清除一下
        if (packet->data != NULL && strcmp((char*)packet->data, FLUSH_DATA) == 0) {
            LogWarn("flush buffers");
            avcodec_flush_buffers(m_videoStream->codec);
            av_packet_unref(packet);
            continue;
        }

        // 2.将数据送入到解码器
        if (avcodec_send_packet(pCodecCtx, packet) != 0) {
           LogError("input AVPacket to decoder failed!\n");
           av_packet_unref(packet);
           continue;
        }

        // 3.解码
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
    double video_pts = 0; // 当前视频的pts，时间显示戳
    double audio_pts = 0; // 音频pts
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

        // 需要给rgbFrame->data分配好空间，避免sws_scale出现dst pointer错误
        av_image_alloc(rgbFrame->data,
            rgbFrame->linesize,
            videoWidth, videoHeight,
            AV_PIX_FMT_RGB32,
            1);

        // 将解码后的数据转换成RGB32
        imgConvertCtx = sws_getContext(videoWidth, videoHeight,
                (AVPixelFormat)pFrame->format, videoWidth, videoHeight,
                AV_PIX_FMT_RGB32, SWS_BICUBIC, NULL, NULL, NULL);

        sws_scale(imgConvertCtx,
                (const uint8_t* const*)pFrame->data,
                pFrame->linesize, 0, videoHeight, rgbFrame->data,
                rgbFrame->linesize);

        // 发送数据给Qt进行渲染
        RenderVideo(rgbFrame->data[0], videoWidth, videoHeight);
    }

    if (rgbFrame != nullptr) {
        av_free(rgbFrame);
    }

    if (imgConvertCtx != nullptr) {
        sws_freeContext(imgConvertCtx);
    }
}
