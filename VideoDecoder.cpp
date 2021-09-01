#include "VideoPlayer.h"

static constexpr int VIDEO_DELAY_THRESHOLD = 10;
static constexpr int VIDEO_SKIP_THRESHOLD = 30;
static constexpr double SYNC_THRESHOLD = 0.01;
static constexpr double NOSYNC_THRESHOLD = 10.0;

// ��Ƶ�������߳�
void Videoplayer::decodeVideoThread()
{
    LogInfo("================== decode video thread start ==================");
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

    m_isVideoDecodeFinished = true;

    usleep(100*1000);   // ����Ƶ������
    if (!m_isQuit) {
        LogInfo("set isquit to true");
        m_isQuit = true;    // ����Ƶ�߳�ֹͣ
    }
    closeSdlAudio();

    LogInfo("================== decode video thread over ==================");
    return;
}

void Videoplayer::decodeFrame(AVPacket *packet)
{
    AVFrame *videoFrame = av_frame_alloc(); // �����ԭʼ����
    AVFrame *rgbFrame = nullptr;
    int videoWidth  = 0;
    int videoHeight =  0;
    bool skipVideoFrame = false;

    while (0 == avcodec_receive_frame(m_videoCodecCtx, videoFrame)) {
        AvSynchronize(packet, videoFrame, skipVideoFrame);

        if (skipVideoFrame) {
            //LogWarn("video is too slow, skip it");
            //break;
        }

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

bool Videoplayer::AvSynchronize(AVPacket *packet, AVFrame *videoFrame, bool &skipVideoFrame)
{
    double videoPts = 0; // ��ǰ��Ƶ��pts��ʱ����ʾ��
    double audioPts = 0; // ��Ƶpts
    skipVideoFrame = false;

    LogDebug("video start: %ld, pts: %ld, %ld, %ld, dts: %ld, %ld",
             m_videoStartTime, packet->pts, videoFrame->pts, videoFrame->pkt_pts, packet->dts, videoFrame->pkt_dts);

    //LogDebug("videoFrame best timestamp: %f", videoFrame->best_effort_timestamp*av_q2d(m_videoStream->time_base));

    if (packet->pts == AV_NOPTS_VALUE && videoFrame->opaque && *(uint64_t*) videoFrame->opaque != AV_NOPTS_VALUE) {
        videoPts = *(uint64_t *) videoFrame->opaque;
    } else if (packet->pts != AV_NOPTS_VALUE) {
        videoPts = packet->pts;
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
            //audioPts = getAudioClock();
        } else {
            // ����Ƶ����ͬ�����ⲿʱ��
            int64_t curtime = av_gettime();
            LogDebug("curtime: %ld, starttime: %ld", curtime, m_videoStartTime);
            audioPts = (curtime - m_videoStartTime) / 1000000.0;   // ��
        }

        int delayTime = videoPts * 1000 - audioPts * 1000;   // ����
        LogDebug("video pts: %f, audio pts: %f, delay time: %d ms", videoPts, audioPts, delayTime);

        if (delayTime <= 0) {
            if (abs(delayTime) > VIDEO_SKIP_THRESHOLD) {
                skipVideoFrame = true;
            }
            break;
        }

        delayTime = delayTime > VIDEO_DELAY_THRESHOLD ? VIDEO_DELAY_THRESHOLD : delayTime;
        LogDebug("sleep %d ms", delayTime);
        usleep(delayTime * 1000);
    }
}


bool Videoplayer::AvSynchronize2(AVPacket *packet, AVFrame *videoFrame, bool &skipVideoFrame)
{
    double videoPts = 0; // ��ǰ��Ƶ��pts��ʱ����ʾ��
    double audioPts = 0; // ��Ƶpts
    skipVideoFrame = false;

    LogDebug("video start: %ld, pts: %ld, %ld, %ld, dts: %ld, %ld",
             m_videoStartTime, packet->pts, videoFrame->pts, videoFrame->pkt_pts, packet->dts, videoFrame->pkt_dts);

    //LogDebug("videoFrame best timestamp: %f", videoFrame->best_effort_timestamp*av_q2d(m_videoStream->time_base));

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
            //audioPts = m_audioCurPts;
            audioPts = getAudioClock();
        } else {
            // ����Ƶ����ͬ�����ⲿʱ��
            int64_t curtime = av_gettime();
            LogDebug("curtime: %ld, starttime: %ld", curtime, m_videoStartTime);
            audioPts = (curtime - m_videoStartTime) / 1000000.0;   // ��
        }

        double delay = videoPts - m_lastVframePts;
        if (delay <= 0 || delay >= 1.0) {
            delay = m_lastVframeDelay;
        }
        m_lastVframePts = videoPts;
        m_lastVframeDelay = delay;

        double diff = videoPts - audioPts;  // diff < 0 => video slow
        double threshold = (delay > SYNC_THRESHOLD) ? delay : SYNC_THRESHOLD;

        if (fabs(diff) < NOSYNC_THRESHOLD) {
            if (diff <= -threshold) {
                delay = 0;
            } else {
                delay *= 2;
            }
        }

        m_vframeClock += delay;

        double actualDelay = m_vframeClock - static_cast<double>(av_gettime()/1000000.0);   // s
        if (actualDelay <= 0.010) {
            actualDelay = 0.010;
        }

        int sleepTime = static_cast<int>(actualDelay * 1000 + 0.5); // ms
        LogDebug("sleep time : %d", sleepTime);
        usleep(sleepTime * 1000);
        break;
    }
}

bool Videoplayer::convert2rgb(const int videoWidth, const int videoHeight, AVFrame *videoFrame, AVFrame *rgbFrame)
{
    struct SwsContext *imgConvertCtx = nullptr;

    // ��Ҫ��rgbFrame->data����ÿռ䣬����sws_scale����dst pointer����
    av_image_alloc(rgbFrame->data, rgbFrame->linesize, videoWidth, videoHeight,
        AV_PIX_FMT_RGB32, 1);

    // ������������ת����RGB32
    imgConvertCtx = sws_getContext(videoWidth, videoHeight, (AVPixelFormat)videoFrame->format,
                                   videoWidth, videoHeight, AV_PIX_FMT_RGB32,
                                   SWS_BICUBIC, NULL, NULL, NULL);

    sws_scale(imgConvertCtx,
              (const uint8_t* const*)videoFrame->data, videoFrame->linesize, 0, videoHeight,
              rgbFrame->data, rgbFrame->linesize);

    if (imgConvertCtx != nullptr) {
        sws_freeContext(imgConvertCtx);
    }

    return true;
}
