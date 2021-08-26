#include "VideoPlayer.h"

#include <thread>

bool Videoplayer::m_isinited = false;

bool Videoplayer::initPlayer()
{
    if (!m_isinited) {
        av_register_all();          // 初始化FFMPEG  调用了这个才能正常使用编码器和解码器
        avformat_network_init();    // 支持打开网络文件

        LogDebug("init player success");
        m_isinited = true;
    }

    return true;
}

Videoplayer::Videoplayer() : m_videoPlayerCallBack(nullptr),
    m_playState(VideoPlayer_Null),
    m_isPause(false),
    m_isQuit(false),
    m_isReadThreadFinished(false),
    m_isVideoThreadFinished(false),
    m_avformatCtx(nullptr),
    m_avcodecCtx(nullptr),
    m_avCodec(nullptr),
    m_videoStream(nullptr)
{

}

Videoplayer::~Videoplayer()
{

}

bool Videoplayer::startPlayer(const std::string& filepath)
{
    if (!m_isinited) {
        LogError("player is not inited");
        return false;
    }

    m_filepath = filepath;

    // 启动线程读取文件
    std::thread([&](Videoplayer* p) {
        p->readFileThread();
    }, this).detach();

    return true;
}

// 子线程
void Videoplayer::readFileThread()
{
    LogDebug("================== start to read file: %s ==================", m_filepath.c_str());
    m_isReadThreadFinished = false;
    int videoStreamId = -1;
    int audioStreamId = -1;

    m_avformatCtx = avformat_alloc_context();

    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "rtsp_transport", "tcp", 0); // 设置tcp or udp，默认一般优先tcp再尝试udp
    av_dict_set(&opts, "stimeout", "60000000", 0);  // 设置超时3秒

    // 1.打开视频文件
    if (avformat_open_input(&m_avformatCtx, m_filepath.c_str(), nullptr, &opts) != 0)
    {
        LogError("can't open the file: %s", m_filepath.c_str());
        goto end;
    }

    // 2.查找视频流
    if (avformat_find_stream_info(m_avformatCtx, nullptr) < 0)
    {
        LogError("Could't find stream infomation");
        goto end;
    }

    // 循环查找视频中包含的流信息
    for (int i = 0; i < m_avformatCtx->nb_streams; i++)
    {
        if (m_avformatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            videoStreamId = i;
        }
        if (m_avformatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO  && audioStreamId < 0)
        {
            audioStreamId = i;
        }
    }

    LogDebug("get videoStreamId: %d, audioStreamId: %d", videoStreamId, audioStreamId);
    if (videoStreamId < 0) {
        goto end;
    }

    // 3.查找视频解码器
    m_avcodecCtx = m_avformatCtx->streams[videoStreamId]->codec;
    if (m_avcodecCtx == nullptr) {
        LogError("find the avcodec contex failed");
        goto end;
    }
    m_avCodec = avcodec_find_decoder(m_avcodecCtx->codec_id);
    if (m_avCodec == nullptr) {
        LogError("find the decoder failed");
        goto end;
    }

    // 4.打开视频解码器
    if (avcodec_open2(m_avcodecCtx, m_avCodec, NULL) < 0) {
        LogError("open video codec failed");
        goto end;
    }

    m_videoStream = m_avformatCtx->streams[videoStreamId];

    // 创建一个线程专门用来解码视频
    LogDebug("start to create a video decoder thread");
    std::thread([&](Videoplayer* p) {
        p->decodeVideoThread();
    }, this).detach();

    LogDebug("start to dump the video info:");
    av_dump_format(m_avformatCtx, 0, m_filepath.c_str(), 0); // 输出视频信息
    m_playState = VideoPlayer_Playing;

    mVideoStartTime = av_gettime();
    LogDebug("mVideoStartTime: %ld", mVideoStartTime);

    readFrame(videoStreamId, audioStreamId);

end:
    LogInfo("read file thread end");
    m_isReadThreadFinished = true;

    clearVideoQuene();

    if (m_avcodecCtx != nullptr) {
        avcodec_close(m_avcodecCtx);
        m_avcodecCtx = nullptr;
    }

    avformat_close_input(&m_avformatCtx);
    avformat_free_context(m_avformatCtx);

    LogInfo("================== read file thread over ==================");
}

void Videoplayer::readFrame(const int videoStreamId, const int audioStreamId)
{
    LogDebug("start to read frame, videoStreamId: %d, audioStreamId: %d", videoStreamId, audioStreamId);
    while (1) {
        if (m_isQuit) {
            LogInfo("is quit, break it");
            break;
        }

        // 这里做了个限制  当队列里面的数据超过某个大小的时候 就暂停读取  防止一下子就把视频读完了，导致的空间分配不足
        // 这个值可以稍微写大一些
        //if (mAudioPacktList.size() > MAX_AUDIO_SIZE || mVideoPacktList.size() > MAX_VIDEO_SIZE)
        if (m_videoPacktList.size() > MAX_VIDEO_SIZE) {
            LogWarn("video pack list size %u > %u", m_videoPacktList.size(), MAX_VIDEO_SIZE);
            usleep(10*1000);
            continue;
        }

        if (m_isPause) {
            LogDebug("is pausing...");
            usleep(10*1000);
            continue;
        }

        AVPacket packet;
        // 读取码流中的音频若干帧或者视频一帧
        int ret = av_read_frame(m_avformatCtx, &packet);
        if (ret < 0) {
            //LogWarn("read frame failed, ret: %d | %d", ret, AVERROR_EOF);
            static bool firstEnd = true;
            if (firstEnd && ret == AVERROR_EOF) {
                firstEnd = false;

                LogWarn("set packet to null");
                packet.data = NULL;
                packet.size = 0;
            } else {
                if (m_isVideoThreadFinished || m_isQuit) {
                    LogWarn("read frame failed, but is quit, break it");
                    break; // 解码线程也执行完了 可以退出了
                }
                usleep(10*1000);
                continue;
            }
        }

        //LogDebug("get packet stream index: %d", packet.stream_index);
        if (packet.stream_index == videoStreamId) {
            //LogDebug("video stream info");
            // 这里我们将数据存入队列 因此不调用 av_free_packet 释放
            putVideoPacket(packet);
            usleep(1000);    // 稍微休息下
        }
        else if( packet.stream_index == audioStreamId) {
            //LogDebug("audio stream info");
        }
        else
        {
            // Free the packet that was allocated by av_read_frame
            LogWarn("other stream info");
            av_packet_unref(&packet);
        }
    }

    // 等待播放完毕
    LogInfo("read frame over, m_isVideoThreadFinished: %d", m_isVideoThreadFinished);
    while (!m_isVideoThreadFinished)
    {
        usleep(100*1000);
    }
    LogInfo("read frame over");
}

bool Videoplayer::putVideoPacket(const AVPacket &pkt)
{
    if (av_dup_packet((AVPacket*)&pkt) < 0)
    //if (av_packet_ref())
    {
        LogError("dup packet failed");
        return false;
    }

    // 条件变量控制同步
    std::unique_lock<std::mutex> lock(m_videoMutex);
    m_videoPacktList.push_back(pkt);
    LogDebug("IN videoPacktList size: %d", m_videoPacktList.size());
    m_videoCondvar.notify_one();

    return true;
}

bool Videoplayer::getVideoPacket(AVPacket& packet)
{
    std::unique_lock<std::mutex> lock(m_videoMutex);
    if (m_videoPacktList.size() <= 0) {
        //LogDebug("video package list is empty");
        return false;
    }

    LogDebug("OUT videoPacktList size: %d", m_videoPacktList.size());
    packet = m_videoPacktList.front();
    m_videoPacktList.pop_front();

    return true;
}

void Videoplayer::clearVideoQuene()
{
    std::unique_lock<std::mutex> lock(m_videoMutex);
    for (AVPacket pkt : m_videoPacktList)
    {
        //av_free_packet(&pkt);
        av_packet_unref(&pkt);
    }

    m_videoPacktList.clear();
}

void Videoplayer::RenderVideo(const uint8_t *videoBuffer, const int width, const int height)
{
    if (m_videoPlayerCallBack != nullptr) {
        m_videoPlayerCallBack->onDisplayVideo(videoBuffer, width, height);
        return;

        VideoFramePtr videoFrame = std::make_shared<VideoFrame>();

        videoFrame->initBuffer(width, height);
        videoFrame->setYUVbuf(videoBuffer);
        m_videoPlayerCallBack->onDisplayVideo(videoFrame);
    }
}
