#include "VideoPlayer.h"

constexpr int VIDEO_DELAY_THRESHOLD = 10;

// 视频解码子线程
void Videoplayer::decodeVideoThread()
{
    LogInfo("================== %s start ==================", __FUNCTION__);
    usleep(1000);   // 等数据入队列
    m_isVideoDecodeFinished = false;

    while(1) {
        if (m_isQuit) {
            LogDebug("is quited");
            break;
        }

        if (m_isPause) {
            usleep(10*1000);
            continue;
        }

        // 1.从队列中获取package
        AVPacket tmpPkt;
        if (!getVideoPacket(tmpPkt)) {
            if (m_isReadThreadFinished) {
                LogInfo("decode no packet in queue, break it");
                break;
            } else {
                usleep(10*1000); // 队列只是暂时没有数据而已
                continue;
            }
        }

        AVPacket *packet = &tmpPkt;
        // 2.将数据送入到解码器
        if (avcodec_send_packet(m_videoCodecCtx, packet) != 0) {
           LogError("input AVPacket to decoder failed!\n");
           av_packet_unref(packet);
           continue;
        }

        // 3.解码
        decodeFrame(packet);
        av_packet_unref(packet);
    }

    LogInfo("================== decode over ==================");
    m_isVideoDecodeFinished = true;

    usleep(500*1000);   // 让音频播放完
    if (!m_isQuit) {
        m_isQuit = true;    // 让音频线程停止
    }
    stop();

    LogInfo("%s finished", __FUNCTION__);
    return;
}

void Videoplayer::decodeFrame(AVPacket *packet)
{
    AVFrame *videoFrame = av_frame_alloc(); // 解码后原始数据
    AVFrame *rgbFrame = nullptr;
    int videoWidth  = 0;
    int videoHeight =  0;

    while (0 == avcodec_receive_frame(m_videoCodecCtx, videoFrame)) {
        AvSynchronize(packet, videoFrame);

        //LogDebug("width: %d | %d, height: %d | %d", videoFrame->width, videoWidth, videoFrame->height, videoHeight);
        if (videoFrame->width != videoWidth || videoFrame->height != videoHeight) {
            videoWidth  = videoFrame->width;
            videoHeight = videoFrame->height;
        }

        rgbFrame = av_frame_alloc();
        if (!rgbFrame) {
            LogError("frame alloc rgb frame failed");
            break;
        }

        // yuv => rgb
        convert2rgb(videoWidth, videoHeight, videoFrame, rgbFrame);

        // 发送数据给Qt进行渲染
        RenderVideo(rgbFrame->data[0], videoWidth, videoHeight);

        if (rgbFrame != nullptr) {
            av_free(rgbFrame);
            rgbFrame = nullptr;
        }
    }

    if (videoFrame != nullptr) {
        av_free(videoFrame);
    }
}

bool Videoplayer::AvSynchronize(AVPacket *packet, AVFrame *videoFrame)
{
    double videoPts = 0; // 当前视频的pts，时间显示戳
    double audioPts = 0; // 音频pts

    LogDebug("video start: %ld, pts: %ld, %ld, %ld, dts: %ld, %ld",
             m_videoStartTime, packet->pts, videoFrame->pts, videoFrame->pkt_pts, packet->dts, videoFrame->pkt_dts);

    if (packet->dts == AV_NOPTS_VALUE && videoFrame->opaque && *(uint64_t*) videoFrame->opaque != AV_NOPTS_VALUE) {
        videoPts = *(uint64_t *) videoFrame->opaque;
    } else if (packet->dts != AV_NOPTS_VALUE) {
        videoPts = packet->dts;
    } else {
        videoPts = 0;
    }

    videoPts *= av_q2d(m_videoStream->time_base);
    LogDebug("video pts2: %f", videoPts);

    while (true) {
        if (m_isQuit) {
            break;
        }

        if (m_isPause) {
            break;
        }

        if (m_audioStream != nullptr) {
            if (m_isReadThreadFinished && m_audioPacktList.empty()) {
                break;
            }
            audioPts = m_audioCurPts;
        } else {
            // 无音频，则同步到外部时钟
            int64_t curtime = av_gettime();
            LogDebug("curtime: %ld, starttime: %ld", curtime, m_videoStartTime);
            audioPts = (curtime - m_videoStartTime) / 1000000.0;   // 秒
        }

        LogDebug("video pts: %f, audio pts: %f", videoPts, audioPts);
        if (videoPts <= audioPts) {
            break;
        }

        int delayTime = videoPts * 1000 - audioPts * 1000;   // 毫秒
        LogDebug("delay time: %d ms", delayTime);

        if (delayTime <= 0) {
            break;
        }

        delayTime = delayTime > VIDEO_DELAY_THRESHOLD ? VIDEO_DELAY_THRESHOLD : delayTime;
        LogDebug("sleep %d ms", delayTime);
        usleep(delayTime * 1000);
    }
}

bool Videoplayer::convert2rgb(const int videoWidth, const int videoHeight, AVFrame *videoFrame, AVFrame *rgbFrame)
{
    struct SwsContext *imgConvertCtx = nullptr;

    // 需要给rgbFrame->data分配好空间，避免sws_scale出现dst pointer错误
    av_image_alloc(rgbFrame->data, rgbFrame->linesize, videoWidth, videoHeight,
        AV_PIX_FMT_RGB32, 1);

    // 将解码后的数据转换成RGB32
    imgConvertCtx = sws_getContext(videoWidth, videoHeight,
            (AVPixelFormat)videoFrame->format, videoWidth, videoHeight,
            AV_PIX_FMT_RGB32, SWS_BICUBIC, NULL, NULL, NULL);

    sws_scale(imgConvertCtx,
            (const uint8_t* const*)videoFrame->data,
            videoFrame->linesize, 0, videoHeight, rgbFrame->data,
            rgbFrame->linesize);

    if (imgConvertCtx != nullptr) {
        sws_freeContext(imgConvertCtx);
    }

    return true;
}
