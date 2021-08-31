#include "VideoPlayer.h"

constexpr int VIDEO_DELAY_THRESHOLD = 10;

// ��Ƶ�������߳�
void Videoplayer::decodeVideoThread()
{
    LogInfo("================== %s start ==================", __FUNCTION__);
    usleep(1000);   // �����������
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
        // 2.���������뵽������
        if (avcodec_send_packet(m_videoCodecCtx, packet) != 0) {
           LogError("input AVPacket to decoder failed!\n");
           av_packet_unref(packet);
           continue;
        }

        // 3.����
        decodeFrame(packet);
        av_packet_unref(packet);
    }

    LogInfo("================== decode over ==================");
    m_isVideoDecodeFinished = true;

    usleep(500*1000);   // ����Ƶ������
    if (!m_isQuit) {
        m_isQuit = true;    // ����Ƶ�߳�ֹͣ
    }
    stop();

    LogInfo("%s finished", __FUNCTION__);
    return;
}

void Videoplayer::decodeFrame(AVPacket *packet)
{
    AVFrame *videoFrame = av_frame_alloc(); // �����ԭʼ����
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

        // �������ݸ�Qt������Ⱦ
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
    double videoPts = 0; // ��ǰ��Ƶ��pts��ʱ����ʾ��
    double audioPts = 0; // ��Ƶpts

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
            // ����Ƶ����ͬ�����ⲿʱ��
            int64_t curtime = av_gettime();
            LogDebug("curtime: %ld, starttime: %ld", curtime, m_videoStartTime);
            audioPts = (curtime - m_videoStartTime) / 1000000.0;   // ��
        }

        LogDebug("video pts: %f, audio pts: %f", videoPts, audioPts);
        if (videoPts <= audioPts) {
            break;
        }

        int delayTime = videoPts * 1000 - audioPts * 1000;   // ����
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

    // ��Ҫ��rgbFrame->data����ÿռ䣬����sws_scale����dst pointer����
    av_image_alloc(rgbFrame->data, rgbFrame->linesize, videoWidth, videoHeight,
        AV_PIX_FMT_RGB32, 1);

    // ������������ת����RGB32
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
