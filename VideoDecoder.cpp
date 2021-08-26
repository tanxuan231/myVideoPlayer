#include "VideoPlayer.h"

// 视频解码子线程
void Videoplayer::decodeVideoThread()
{
    LogDebug("================== %s start ==================", __FUNCTION__);
    usleep(1000);   // 等数据入队列

    m_isVideoThreadFinished = false;

    int videoWidth  = 0;
    int videoHeight =  0;

    double video_pts = 0; // 当前视频的pts，时间显示戳
    double audio_pts = 0; // 音频pts

    // 解码视频相关
    AVFrame *pFrame = nullptr;
    AVFrame *rgbFrame = nullptr;
    uint8_t *outBuffer = nullptr; // 解码后的数据
    struct SwsContext *imgConvertCtx = nullptr;  // 用于解码后的视频格式转换

    AVCodecContext *pCodecCtx = m_videoStream->codec; // 视频解码器
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

        // 1.从队列中获取package
/*
        AVPacket pkt1;
        {
            std::unique_lock<std::mutex> lock(m_videoMutex);
            if (m_videoPacktList.size() <= 0) {
                //LogDebug("video package list is empty");
                //if (mIsReadFinished) {
                    // 队列里面没有数据了且读取完毕了
                    //break;
                //} else {
                    usleep(10*1000); // 队列只是暂时没有数据而已
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
                // 队列里面没有数据了且读取完毕了
                //break;
            //} else {
                usleep(10*1000); // 队列只是暂时没有数据而已
                continue;
            //}
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
            //int yuvSize = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, videoWidth, videoHeight, 1);  // 按1字节进行内存对齐,得到的内存大小最接近实际大小
            int yuvSize = av_image_get_buffer_size(AV_PIX_FMT_RGB32, videoWidth, videoHeight, 1);  // 按1字节进行内存对齐,得到的内存大小最接近实际大小
            //int yuvSize = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height, 0);  //按0字节进行内存对齐，得到的内存大小是0
            //int yuvSize = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height, 4);   //按4字节进行内存对齐，得到的内存大小稍微大一些

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

            // 由于解码后的数据不一定都是yuv420p，因此需要将解码后的数据统一转换成YUV420P
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
