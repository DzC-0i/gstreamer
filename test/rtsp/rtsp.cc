#include <gst/gst.h>
#include <gst/rtsp/gstrtsp.h>
#include <string>
#include <iostream>

/* 外加的一个宏定义, 用于在低版本也能够通过编译情况*/
#ifndef GST_STATE_GET_NAME
#define gst_state_get_name(state)                                                                \
    ((state) == GST_STATE_VOID_PENDING ? "VOID_PENDING" : (state) == GST_STATE_NULL  ? "NULL"    \
                                                      : (state) == GST_STATE_READY   ? "READY"   \
                                                      : (state) == GST_STATE_PAUSED  ? "PAUSED"  \
                                                      : (state) == GST_STATE_PLAYING ? "PLAYING" \
                                                                                     : "UNKNOWN")
#endif

/*
 * RTSP Player (Single Stream)
 * - 仅拉视频（忽略音频）
 * - 自动识别 H264 / H265
 * - 自动重连（指数退避）
 * - 支持 TCP / UDP
 */

class RTSPPlayer
{
public:
    RTSPPlayer(const std::string &uri, bool use_tcp);
    ~RTSPPlayer();

    void start();

private:
    std::string uri_;
    bool use_tcp_;

    GstElement *pipeline_ = nullptr;
    GstElement *src_ = nullptr;

    guint reconnect_timer_ = 0;
    int retry_count_ = 0;

    GMainLoop *loop_ = nullptr;

    GstElement *depay_ = nullptr;
    GstElement *parse_ = nullptr;
    GstElement *dec_ = nullptr;
    GstElement *conv_ = nullptr;
    GstElement *sink_ = nullptr;

    GstElement *create_pipeline();
    void destroy_pipeline();

    void schedule_reconnect();
    static gboolean reconnect_callback(gpointer data);

    static void pad_added_cb(GstElement *src, GstPad *pad, gpointer user_data);
    static gboolean bus_callback(GstBus *bus, GstMessage *msg, gpointer user_data);
};

/* ---------------------------------------------------------
 * Constructor
 * --------------------------------------------------------- */
RTSPPlayer::RTSPPlayer(const std::string &uri, bool use_tcp)
    : uri_(uri), use_tcp_(use_tcp)
{
    gst_init(nullptr, nullptr);
}

/* ---------------------------------------------------------
 * Destructor
 * --------------------------------------------------------- */
RTSPPlayer::~RTSPPlayer()
{
    destroy_pipeline();
}

/* 创建 Pipeline */
GstElement *RTSPPlayer::create_pipeline()
{
    // rtspsrc → depay → parse → decode → convert → autovideosink

    pipeline_ = gst_pipeline_new("rtsp-pipeline");
    src_ = gst_element_factory_make("rtspsrc", "rtspsrc");

    if (!pipeline_ || !src_)
    {
        g_printerr("Failed to create basic GStreamer elements.\n");
        return nullptr;
    }

    /* 配置 RTSP 源 */
    g_object_set(src_, "location", uri_.c_str(), NULL);
    g_object_set(src_, "latency", 200, NULL);
    // g_object_set(src_, "media-types", GST_RTSP_MEDIA_TYPE_VIDEO, NULL); // 只拉取视频流忽略音频   没有这个参数

    // 设置 2 秒超时（单位：微秒）2秒没数据就报错
    g_object_set(src_, "timeout", (guint64)2 * 1000 * 1000, NULL);

    if (use_tcp_)
    {
        // RTSP over TCP
        // g_object_set(src_, "protocols", 4, NULL); // GST_RTSP_LOWER_TRANS_TCP
        g_object_set(src_, "protocols", GST_RTSP_LOWER_TRANS_TCP, NULL); // GST_RTSP_LOWER_TRANS_TCP

        // TCP keepalive 时间3秒
        g_object_set(src_, "tcp-timeout", (guint64)3 * 1000 * 1000, NULL);

        g_print("RTSP use TCP\n");
    }
    else
    {
        // RTSP over UDP
        // g_object_set(src_, "protocols", 1, NULL);
        g_object_set(src_, "protocols", GST_RTSP_LOWER_TRANS_UDP, NULL); // GST_RTSP_LOWER_TRANS_UDP  这边没有改动过，默认是UDP的

        g_print("RTSP use UDP\n");
    }

    /* 动态 Pad 处理 */
    g_signal_connect(src_, "pad-added", G_CALLBACK(pad_added_cb), this);

    /* 将 rtspsrc 添加进 pipeline */
    gst_bin_add(GST_BIN(pipeline_), src_);

    /* 监听 Bus 消息 */
    GstBus *bus = gst_element_get_bus(pipeline_);
    gst_bus_add_watch(bus, bus_callback, this);
    gst_object_unref(bus);

    return pipeline_;
}

/* ---------------------------------------------------------
 * rtspsrc dynamic pad added (identify H264 / H265)
 * --------------------------------------------------------- */
void RTSPPlayer::pad_added_cb(GstElement *src, GstPad *pad, gpointer user_data)
{
    RTSPPlayer *self = reinterpret_cast<RTSPPlayer *>(user_data);

    GstCaps *caps = gst_pad_get_current_caps(pad);
    if (!caps) return;

    GstStructure *s = gst_caps_get_structure(caps, 0);
    const gchar *name = gst_structure_get_name(s);

    g_print("New dynamic pad: %s\n", name);

    if (!g_str_has_prefix(name, "application/x-rtp"))
    {
        g_print("Ignore non-RTP pad: %s\n", name);
        gst_caps_unref(caps);
        return;
    }

    const gchar *encoding = nullptr;
    if (gst_structure_has_field(s, "encoding-name"))
        encoding = gst_structure_get_string(s, "encoding-name");

    if (!encoding)
    {
        g_print("No encoding-name found — ignoring pad.\n");
        gst_caps_unref(caps);
        return;
    }

    /* Create RTP chain */
    if (g_strcmp0(encoding, "H264") == 0)
    {
        g_print("[Detected] H264 stream\n");
        self->depay_ = gst_element_factory_make("rtph264depay", nullptr);
        self->parse_ = gst_element_factory_make("h264parse", nullptr);

        self->dec_ = gst_element_factory_make("avdec_h264", nullptr);
        if (!self->dec_)
            self->dec_ = gst_element_factory_make("v4l2h264dec", nullptr);
    }
    else if (g_strcmp0(encoding, "H265") == 0 || g_strcmp0(encoding, "HEVC") == 0)
    {
        g_print("[Detected] H265 stream\n");
        self->depay_ = gst_element_factory_make("rtph265depay", nullptr);
        self->parse_ = gst_element_factory_make("h265parse", nullptr);

        self->dec_ = gst_element_factory_make("avdec_h265", nullptr);
        if (!self->dec_)
            self->dec_ = gst_element_factory_make("v4l2h265dec", nullptr);
    }
    else
    {
        g_print("Unknown encoding: %s\n", encoding);
        gst_caps_unref(caps);
        return;
    }

    gst_caps_unref(caps);  // ← 统一释放

    /* Create remaining elements */
    self->conv_ = gst_element_factory_make("videoconvert", nullptr);
    self->sink_ = gst_element_factory_make("autovideosink", nullptr);

    if (!self->depay_ || !self->parse_ || !self->dec_ || !self->conv_ || !self->sink_)
    {
        g_printerr("Failed to create some pipeline elements.\n");
        return;
    }

    gst_bin_add_many(
        GST_BIN(self->pipeline_),
        self->depay_, self->parse_, self->dec_,
        self->conv_, self->sink_, NULL
    );

    gst_element_sync_state_with_parent(self->depay_);
    gst_element_sync_state_with_parent(self->parse_);
    gst_element_sync_state_with_parent(self->dec_);
    gst_element_sync_state_with_parent(self->conv_);
    gst_element_sync_state_with_parent(self->sink_);

    gst_element_link_many(
        self->depay_, self->parse_, self->dec_, self->conv_, self->sink_, NULL);

    /* Now link dynamic pad */
    GstPad *sinkpad = gst_element_get_static_pad(self->depay_, "sink");
    GstPadLinkReturn ret = gst_pad_link(pad, sinkpad);
    if (ret != GST_PAD_LINK_OK)
        g_printerr("Failed to link pad: %d\n", ret);

    gst_object_unref(sinkpad);

    if (self->retry_count_ != 0)
    {
        self->retry_count_ = 0;
        g_print("RTP data received. Reset retry count.\n");
    }
}

/* ---------------------------------------------------------
 * Bus callback (error handling)
 * --------------------------------------------------------- */
gboolean RTSPPlayer::bus_callback(GstBus *bus, GstMessage *msg, gpointer user_data)
{
    RTSPPlayer *self = reinterpret_cast<RTSPPlayer *>(user_data);

    switch (GST_MESSAGE_TYPE(msg))
    {
    case GST_MESSAGE_ERROR:
        GError *err;
        gchar *debug_info;
        gst_message_parse_error(msg, &err, &debug_info);
        g_printerr("Error received from element %s: %s\n",
                    GST_OBJECT_NAME(msg->src), err->message);
        g_printerr("Debugging information: %s\n",
                    debug_info ? debug_info : "none");
        g_clear_error(&err);
        g_free(debug_info);

        // start reconnect timer
        self->schedule_reconnect();

        break;
    case GST_MESSAGE_EOS:
        g_print("End-Of-Stream reached.\n");

        // start reconnect timer
        self->schedule_reconnect();

        break;
    case GST_MESSAGE_STATE_CHANGED:
        /* We are only interested in state-changed messages from the pipeline */
        if (GST_MESSAGE_SRC(msg) == GST_OBJECT(self->pipeline_))
        {
            GstState old_state, new_state, pending_state;
            gst_message_parse_state_changed(msg, &old_state, &new_state,
                                            &pending_state);
            g_print("Pipeline state changed from %s to %s:\n",
                    gst_state_get_name(old_state), gst_state_get_name(new_state));
        }

        break;
    case GST_MESSAGE_BUFFERING:
    {
        /* 仅对拉流场景有用 — 当 buffering < 100 时可暂停 pipeline */
        gint percent = 0;
        gst_message_parse_buffering(msg, &percent);
        g_print("Buffering: %d%%\n", percent);
        if (percent < 100)
        {
            gst_element_set_state(self->pipeline_, GST_STATE_PAUSED);
        }
        else
        {
            gst_element_set_state(self->pipeline_, GST_STATE_PLAYING);
        }
        break;
    }
    case GST_MESSAGE_WARNING:
    {
        GError *err = nullptr;
        gchar *debug = nullptr;
        gst_message_parse_warning(msg, &err, &debug);
        g_printerr("Warning from %s: %s\n", GST_OBJECT_NAME(msg->src), err ? err->message : "unknown");
        if (debug) g_printerr("Debug info: %s\n", debug);
        g_clear_error(&err);
        g_free(debug);
        break;
    }
    case GST_MESSAGE_INFO:
    {
        GError *info = nullptr;
        gchar *debug = nullptr;
        gst_message_parse_info(msg, &info, &debug);
        g_print("Info from %s: %s\n", GST_OBJECT_NAME(msg->src), info ? info->message : "none");
        if (debug) g_print("Debug info: %s\n", debug);
        g_clear_error(&info);
        g_free(debug);
        break;
    }
    default:
        // /* We should not reach here */
        // g_printerr("Unexpected message received.\n");
        // break;
         /* 其它许多正常的消息（TAG, QOS, STREAM_STATUS, ELEMENT 等）都不会影响播放，
           生产环境下通常不打印它们，除非你正在调试特定问题。*/
        break;
    }

    return TRUE;
}

/* ---------------------------------------------------------
 * Destroy pipeline
 * --------------------------------------------------------- */
void RTSPPlayer::destroy_pipeline()
{
    if (!pipeline_)
        return;

    gst_element_set_state(pipeline_, GST_STATE_NULL);

    gst_object_unref(pipeline_);
    pipeline_ = nullptr;
}

/* ---------------------------------------------------------
 * Schedule auto reconnection
 * --------------------------------------------------------- */
void RTSPPlayer::schedule_reconnect()
{
    if (reconnect_timer_)
        return;

    destroy_pipeline();

    int delay = (1 << retry_count_) * 1000;
    if (retry_count_ < 5) retry_count_++;    // 控制延时，最多加5次

    // std::cout << "Connection lost. Reconnecting in " << delay << " ms ..." << std::endl;
    g_print("Connection lost. Reconnecting in %d ms ...\n", delay);

    reconnect_timer_ = g_timeout_add(delay, reconnect_callback, this);
}

/* ---------------------------------------------------------
 * Reconnection callback
 * --------------------------------------------------------- */
gboolean RTSPPlayer::reconnect_callback(gpointer data)
{
    RTSPPlayer *self = reinterpret_cast<RTSPPlayer *>(data);
    self->reconnect_timer_ = 0;

    // std::cout << "Reconnecting now..." << std::endl;
    g_print("Reconnecting now...\n");

    self->pipeline_ = self->create_pipeline();
    gst_element_set_state(self->pipeline_, GST_STATE_PLAYING);

    return FALSE; // once only
}

/* ---------------------------------------------------------
 * Start playing
 * --------------------------------------------------------- */
void RTSPPlayer::start()
{
    loop_ = g_main_loop_new(NULL, FALSE);

    pipeline_ = create_pipeline();
    gst_element_set_state(pipeline_, GST_STATE_PLAYING);

    // std::cout << "Start playing RTSP: " << uri_ << std::endl;
    g_print("Start playing RTSP: %s\n", uri_.c_str());

    g_main_loop_run(loop_);
}

/* ---------------------------------------------------------
 * main()
 * --------------------------------------------------------- */
int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        // printf("Usage: %s rtsp://xxx.xxx.xxx.xxx [--tcp]\n", argv[0]);
        g_print("Usage: %s rtsp://xxx.xxx.xxx.xxx [--tcp]\n", argv[0]);
        return 0;
    }

    std::string uri = argv[1];
    // bool tcp = false;

    // if (argc >= 3 && std::string(argv[2]) == "--tcp")
    //     tcp = true;

    bool tcp = (argc >= 3 && std::string(argv[2]) == "--tcp");

    RTSPPlayer player(uri, tcp);
    player.start();

    return 0;
}

// g++ rtsp.cc -o rtsp `pkg-config --cflags --libs gstreamer-1.0 gstreamer-rtsp-1.0`
