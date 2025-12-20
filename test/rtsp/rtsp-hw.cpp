#include <gst/gst.h>
#include <gst/rtsp/rtsp.h>
#include <gst/sdp/gstsdpmessage.h>
// #include <string>
#include <iostream>
#include <signal.h>
#include <mutex>

GMainLoop *loop;

void handle_signal(int signum)
{
    g_print("\nReceive (Ctrl+C), waiting to exit, cleanning...\n");
    g_main_loop_quit(loop);
}

/* 外加的一个宏定义, 用于在低版本也能够通过编译情况*/
#ifndef GST_STATE_GET_NAME
#define gst_state_get_name(state)                                                                \
    ((state) == GST_STATE_VOID_PENDING ? "VOID_PENDING" : (state) == GST_STATE_NULL  ? "NULL"    \
                                                      : (state) == GST_STATE_READY   ? "READY"   \
                                                      : (state) == GST_STATE_PAUSED  ? "PAUSED"  \
                                                      : (state) == GST_STATE_PLAYING ? "PLAYING" \
                                                                                     : "UNKNOWN")
#endif

class Gstreamer_HW
{
public:
    Gstreamer_HW(const std::string &rtsp_url_, gboolean use_tcp_);
    ~Gstreamer_HW();

private:
    typedef struct _CustomData
    {
        GstElement *pipeline = nullptr;

        GstElement *src = nullptr;

        GstElement *depay = nullptr;
        GstElement *parse = nullptr;
        GstElement *dec = nullptr;

        GstElement *sink = nullptr;

        gboolean video_linked = FALSE; // 表示是否已经链接视频流

    } CustomData;

    std::string rtsp_url;
    gboolean use_tcp;

    CustomData data;

    // 静态变量，用于确保 gst_init 只调用一次
    static std::once_flag gstreamer_initialized;

    // 静态函数，负责初始化 GStreamer
    static void initialize_gstreamer();

    gboolean create_pipeline();

    void destroy_pipeline();

    // 解析 SDP 消息
    static void on_sdp_received(GstElement *element, GstSDPMessage *sdp, gpointer user_data);
    // 筛选流 - 仅允许视频流
    static gboolean on_select_stream(GstElement *element, guint stream_index, GstCaps *caps, gpointer user_data);
    // 绑定视频 Pad - 识别编码并链接解码器
    static void on_pad_added(GstElement *element, GstPad *pad, gpointer user_data);

    // 管道消息回调
    static void error_cb(GstBus *bus, GstMessage *msg, CustomData *data);
    static void eos_cb(GstBus *bus, GstMessage *msg, CustomData *data);
    static void state_changed_cb(GstBus *bus, GstMessage *msg, CustomData *data);
};

/* ---------------------------------------------------------
 * main()
 * rtspsrc -> depay -> (parse) -> dec -> sink
 * g++ ./rtsp-hw.cpp -o ./rtsp-hw `pkg-config --cflags --libs gstreamer-1.0 gstreamer-rtsp-1.0`
 * --------------------------------------------------------- */
int main(int argc, char *argv[])
{
    // 注册 SIGINT 信号（Ctrl+C）处理函数
    signal(SIGINT, handle_signal);

    if (argc < 2)
    {
        g_print("Usage: %s rtsp://xxx.xxx.xxx.xxx [--tcp]\n", argv[0]);
        return 0;
    }

    std::string url = argv[1];
    // bool tcp = false;

    // if (argc >= 3 && std::string(argv[2]) == "--tcp")
    //     tcp = true;

    gboolean tcp = (argc >= 3 && std::string(argv[2]) == "--tcp");

    g_print("Link to %s, use tcp %s\n", url.c_str(), tcp ? "true" : "false");

    Gstreamer_HW gst_rtsp_play(url, tcp);

    // GStreamer 的 Bus 信号依赖 GLib 的主循环（GMainLoop）分发事件，如果没有启动主循环，信号永远不会触发
    loop = g_main_loop_new(NULL, FALSE);

    g_main_loop_run(loop); // 阻塞运行，直到调用 g_main_loop_quit

    // 退出时清理
    g_main_loop_unref(loop);

    g_print("Exit\n");

    return 0;
}

// 静态成员变量初始化
std::once_flag Gstreamer_HW::gstreamer_initialized;

void Gstreamer_HW::initialize_gstreamer()
{
    gst_init(nullptr, nullptr); // 初始化 GStreamer
    std::cout << "GStreamer initialized.\n";
}

Gstreamer_HW::Gstreamer_HW(const std::string &rtsp_url_, gboolean use_tcp_)
{
    // 确保 gst_init 只调用一次
    std::call_once(gstreamer_initialized, &Gstreamer_HW::initialize_gstreamer);

    rtsp_url = rtsp_url_;
    use_tcp = use_tcp_;

    memset(&data, 0, sizeof(data));

    data.video_linked = FALSE;

    create_pipeline();
}

Gstreamer_HW::~Gstreamer_HW()
{
    destroy_pipeline();
}

void Gstreamer_HW::on_sdp_received(GstElement *element, GstSDPMessage *sdp, gpointer user_data)
{
    CustomData *ctx = (CustomData *)user_data;
    guint i, j;
    guint media_count, time_count;

    g_print("\n===== Parsing SDP message, all stream information =====\n");

    // // Print basic SDP information
    // g_print("Session Name: %s\n", sdp->origin.username ? sdp->origin.username : "N/A");
    // g_print("Session ID: %s\n", sdp->origin.sess_id ? sdp->origin.sess_id : "N/A");
    // g_print("Session Version: %s\n", sdp->origin.sess_version ? sdp->origin.sess_version : "N/A");

    // 获取并打印 origin 信息
    const GstSDPOrigin *origin = gst_sdp_message_get_origin(sdp); // 修正为指针引用
    g_print("Session Origin: %s\n", origin->username ? origin->username : "N/A");

    // 获取时间描述的数量
    time_count = gst_sdp_message_times_len(sdp);
    g_print("Total time descriptions: %u\n", time_count);

    // 解析每个时间描述的信息
    for (i = 0; i < time_count; i++)
    {
        const GstSDPTime *time = gst_sdp_message_get_time(sdp, i);
        g_print("Time description %u: Start time: %u, Stop time: %u\n", i + 1, time->start, time->stop);
    }

    // 获取媒体流数量
    media_count = gst_sdp_message_medias_len(sdp);
    g_print("Total media streams: %u\n", media_count);

    // 解析每个媒体流的信息
    for (i = 0; i < media_count; i++)
    {
        const GstSDPMedia *media = gst_sdp_message_get_media(sdp, i);

        // 打印媒体流的基本信息
        const gchar *media_type = gst_sdp_media_get_media(media);
        guint port = gst_sdp_media_get_port(media);
        const gchar *protocol = gst_sdp_media_get_proto(media);

        g_print("Media %u: %s, Port: %u, Protocol: %s\n", i + 1, media_type, port, protocol);

        // 获取媒体流的属性
        guint attr_count = gst_sdp_media_attributes_len(media); // 使用正确的函数
        for (j = 0; j < attr_count; j++)
        {
            const GstSDPAttribute *attr = gst_sdp_media_get_attribute(media, j); // 获取属性名
            g_print("  Attribute: %s = %s\n", attr->key, attr->value);
        }
    }

    g_print("=== SDP parsing completed ===\n\n");
}

gboolean Gstreamer_HW::on_select_stream(GstElement *element, guint stream_index, GstCaps *caps, gpointer user_data)
{
    CustomData *ctx = (CustomData *)user_data;
    gchar *caps_str = gst_caps_to_string(caps);

    g_print("\n===== Select stream: Index %d, Caps=%s =====\n", stream_index, caps_str);

    // Check if the Caps is for video stream
    const gchar *media = gst_caps_get_structure(caps, 0) ? gst_structure_get_string(gst_caps_get_structure(caps, 0), "media") : NULL;

    if (media && g_str_equal(media, "video"))
    {
        g_print("  -> Selected video stream (Index %u)\n", stream_index);
        g_free(caps_str);
        return TRUE; // Select video
    }

    g_print("  -> Skipping non-video stream (Index %u)\n", stream_index);
    g_free(caps_str);
    return FALSE; // Skip other streams (audio/data)
}

void Gstreamer_HW::on_pad_added(GstElement *element, GstPad *pad, gpointer user_data)
{
    CustomData *ctx = (CustomData *)user_data;

    // 如果视频流已经链接过，直接跳过处理
    if (ctx->video_linked)
    {
        g_print("\nVideo stream already linked, skipping.\n");
        return;
    }

    GstCaps *caps = gst_pad_get_current_caps(pad);

    if (!caps)
        caps = gst_pad_get_allowed_caps(pad);
    if (!caps)
    {
        g_print("\nUnable to get Pad Caps, ignoring\n");
        return;
    }

    gchar *caps_str = gst_caps_to_string(caps);
    g_print("\n===== New video Pad: Caps=%s =====\n", caps_str);

    // Get the structure from Caps
    GstStructure *structure = gst_caps_get_structure(caps, 0);

    gboolean is_vp = FALSE;

    // Check for video encoding type
    const gchar *encoding_type = gst_structure_get_string(structure, "encoding-name");
    if (encoding_type != NULL)
    {
        if (g_str_has_prefix(encoding_type, "H264") || g_str_has_prefix(encoding_type, "AVC"))
        {
            ctx->depay = gst_element_factory_make("rtph264depay", "h264-depay");
            ctx->parse = gst_element_factory_make("h264parse", "h264-parse");
            g_print("  -> Identified H.264 encoding\n");
        }
        else if (g_str_has_prefix(encoding_type, "H265") || g_str_has_prefix(encoding_type, "HEVC"))
        {
            ctx->depay = gst_element_factory_make("rtph265depay", "h265-depay");
            ctx->parse = gst_element_factory_make("h265parse", "h265-parse");
            g_print("  -> Identified H.265 encoding\n");
        }
        else if (g_str_has_prefix(encoding_type, "VP8"))
        {
            ctx->depay = gst_element_factory_make("rtpvp8depay", "vp8-depay");
            is_vp = TRUE;
            g_print("  -> Identified VP8 encoding\n");
        }
        else if (g_str_has_prefix(encoding_type, "VP9"))
        {
            ctx->depay = gst_element_factory_make("rtpvp9depay", "vp9-depay");
            is_vp = TRUE;
            g_print("  -> Identified VP9 encoding\n");
        }
        else
        {
            g_print("  -> Unsupported video encoding, ignoring\n");
            g_free(caps_str);
            gst_caps_unref(caps);
            return;
        }
    }

    gst_caps_unref(caps);
    g_free(caps_str);

    ctx->dec = gst_element_factory_make("mppvideodec", "mppdec");

    // 当需要使用 OpenCV 或者 AI 推理的时候不建议开启这两项
    // 启用 ARM 帧缓冲压缩, 只在 直连显示 / Mali GPU pipeline 时打开
    if (g_object_class_find_property(G_OBJECT_GET_CLASS(ctx->dec), "arm-afbc"))
        g_object_set(ctx->dec, "arm-afbc", TRUE, NULL);
    // 启用 DMA zero-copy, 只在 硬件渲染链路（kmssink / waylandsink / glimagesink）时打开
    if (g_object_class_find_property(G_OBJECT_GET_CLASS(ctx->dec), "dma-feature"))
        g_object_set(ctx->dec, "dma-feature", TRUE, NULL);

    // GstElement *conv = gst_element_factory_make("videoconvert", "conv");
    ctx->sink = gst_element_factory_make("autovideosink", "video-sink"); // 自动视频输出

    if (!ctx->depay || !ctx->dec || !ctx->sink || (!is_vp && !ctx->parse))
    {
        g_printerr("\nFailed to create pipeline elements\n");
        return;
    }

    // // 对于非 VP8/VP9，必须有 parse
    // if (!is_vp && !ctx->parse) {
    //     g_printerr("Failed: parser is required for H.264/H.265 but missing\n");
    //     return;
    // }

    if (is_vp)
    {
        // Add all created elements to the pipeline
        gst_bin_add_many(GST_BIN(ctx->pipeline), ctx->depay, ctx->dec, ctx->sink, NULL);

        gst_element_link_many(ctx->depay, ctx->dec, ctx->sink, NULL);

        // Ensure all added elements synchronize with the parent's state
        if (gst_element_sync_state_with_parent(ctx->depay) != GST_STATE_CHANGE_SUCCESS ||
            gst_element_sync_state_with_parent(ctx->dec) != GST_STATE_CHANGE_SUCCESS ||
            gst_element_sync_state_with_parent(ctx->sink) != GST_STATE_CHANGE_SUCCESS)
        {
            g_printerr("\nFailed to synchronize element states!\n");
            return;
        }
    }
    else
    {
        // Add all created elements to the pipeline
        gst_bin_add_many(GST_BIN(ctx->pipeline), ctx->depay, ctx->parse, ctx->dec, ctx->sink, NULL);

        gst_element_link_many(ctx->depay, ctx->parse, ctx->dec, ctx->sink, NULL);

        // Ensure all added elements synchronize with the parent's state
        if (gst_element_sync_state_with_parent(ctx->depay) != GST_STATE_CHANGE_SUCCESS ||
            gst_element_sync_state_with_parent(ctx->parse) != GST_STATE_CHANGE_SUCCESS ||
            gst_element_sync_state_with_parent(ctx->dec) != GST_STATE_CHANGE_SUCCESS ||
            gst_element_sync_state_with_parent(ctx->sink) != GST_STATE_CHANGE_SUCCESS)
        {
            g_printerr("\nFailed to synchronize element states!\n");
            return;
        }
    }

    /* Now link dynamic pad */
    GstPad *sinkpad = gst_element_get_static_pad(ctx->depay, "sink");
    GstPadLinkReturn ret = gst_pad_link(pad, sinkpad);
    if (ret != GST_PAD_LINK_OK)
    {
        g_printerr("\nFailed to link pad: %d\n", ret);
        gst_object_unref(sinkpad);
        return;
    }

    gst_object_unref(sinkpad);

    g_print("\nVideo stream successfully linked to the pipeline\n");

    ctx->video_linked = TRUE;
}

void Gstreamer_HW::error_cb(GstBus *bus, GstMessage *msg, CustomData *data)
{
    GError *err;
    gchar *debug_info;

    /* Print error details on the screen */
    gst_message_parse_error(msg, &err, &debug_info);
    g_printerr("Error received from element %s: %s\n",
               GST_OBJECT_NAME(msg->src), err->message);
    g_printerr("Debugging information: %s\n",
               debug_info ? debug_info : "none");
    g_clear_error(&err);
    g_free(debug_info);

    /* Set the pipeline to READY (which stops playback) */
    gst_element_set_state(data->pipeline, GST_STATE_NULL);

    g_print("Press CTRL + C to exit\n");
}

void Gstreamer_HW::eos_cb(GstBus *bus, GstMessage *msg, CustomData *data)
{
    g_print("End-Of-Stream reached.\n");
    gst_element_set_state(data->pipeline, GST_STATE_NULL);

    g_print("Press CTRL + C to exit\n");
}

void Gstreamer_HW::state_changed_cb(GstBus *bus, GstMessage *msg, CustomData *data)
{
    GstState old_state, new_state, pending_state;
    gst_message_parse_state_changed(msg, &old_state, &new_state, &pending_state);

    // 打印多个消息
    // if (GST_MESSAGE_SRC(msg) == GST_OBJECT(data->pipeline))
    // {
    //     g_print("Pipeline state changed from %s to %s\n",
    //             gst_state_get_name(old_state), gst_state_get_name(new_state));
    // }

    const gchar *src_name = GST_OBJECT_NAME(msg->src);
    const gchar *pipeline_name = GST_OBJECT_NAME(data->pipeline);

    // // 打印消息来源（是Pipeline本身，还是子元素如rtspsrc）
    // g_print("[State Change] Source: %s | Old: %s | New: %s | Pending: %s\n",
    //         src_name,
    //         gst_element_state_get_name(old_state),
    //         gst_element_state_get_name(new_state),
    //         gst_element_state_get_name(pending_state));

    // 只关注Pipeline自身的“最终状态变更”（过滤子元素和过渡消息）
    if (g_strcmp0(src_name, pipeline_name) == 0 && old_state != new_state)
    {
        g_print("Pipeline state changed from %s to %s\n",
                gst_element_state_get_name(old_state),
                gst_element_state_get_name(new_state));
    }
}

gboolean Gstreamer_HW::create_pipeline()
{
    data.pipeline = gst_pipeline_new("rtsp-pipeline");
    data.src = gst_element_factory_make("rtspsrc", "rtspsrc_url");

    if (!data.pipeline || !data.src)
    {
        g_printerr("Failed to create basic GStreamer elements.\n");

        return FALSE;
    }

    /* 配置 RTSP 源 */
    /**
     *  Supported URI protocols:
            rtsp
            rtspu
            rtspt
            rtsph
            rtsp-sdp
            rtsps
            rtspsu
            rtspst
            rtspsh

     */
    g_object_set(data.src, "location", rtsp_url.c_str(), NULL); // 设置源地址
    /**
     * Control the buffering algorithm in use
                        flags: 可读, 可写
                        Enum "GstRTSPSrcBufferMode" Default: 3, "auto"
                           (0): none             - Only use RTP timestamps
                           (1): slave            - Slave receiver to sender clock
                           (2): buffer           - Do low/high watermark buffering
                           (3): auto             - Choose mode depending on stream live
                           (4): synced           - Synchronized sender and receiver clocks
     */
    g_object_set(data.src, "buffer-mode", 3, NULL);        // 自动调整缓冲区大小
    g_object_set(data.src, "drop-on-latency", TRUE, NULL); // 缓冲区满时丢弃旧数据
    g_object_set(data.src, "latency", 200, NULL);          // RTSP 接收缓冲区

    if (use_tcp)
    {
        /**
         * Allowed lower transport protocols
                        flags: 可读, 可写
                        Flags "GstRTSPLowerTrans" Default: 0x00000007, "tcp+udp-mcast+udp"
                           (0x00000000): unknown          - GST_RTSP_LOWER_TRANS_UNKNOWN
                           (0x00000001): udp              - GST_RTSP_LOWER_TRANS_UDP
                           (0x00000002): udp-mcast        - GST_RTSP_LOWER_TRANS_UDP_MCAST
                           (0x00000004): tcp              - GST_RTSP_LOWER_TRANS_TCP
                           (0x00000010): http             - GST_RTSP_LOWER_TRANS_HTTP
                           (0x00000020): tls              - GST_RTSP_LOWER_TRANS_TLS
         */
        g_object_set(data.src, "protocols", GST_RTSP_LOWER_TRANS_TCP, NULL); // 使用 TCP 传输
        /**
         * Fail after timeout microseconds on TCP connections (0 = disabled)
                        flags: 可读, 可写
                        Unsigned Integer64. Range: 0 - 18446744073709551615 Default: 20000000
         */
        g_object_set(data.src, "tcp-timeout", (guint64)4 * 1000 * 1000, NULL); // TCP 超时 4秒

        g_print("RTSP use TCP\n");
    }
    else
    {
        g_object_set(data.src, "protocols", GST_RTSP_LOWER_TRANS_UDP, NULL); // 使用 UDP 传输

        g_print("RTSP use UDP\n");
    }

    // 将 src 添加进 pipeline
    gst_bin_add(GST_BIN(data.pipeline), data.src);

    // 注册回调
    g_signal_connect(data.src, "on-sdp", G_CALLBACK(on_sdp_received), &data);
    g_signal_connect(data.src, "select-stream", G_CALLBACK(on_select_stream), &data);
    g_signal_connect(data.src, "pad-added", G_CALLBACK(on_pad_added), &data);

    // GStreamer 的 Bus 信号依赖 GLib 的主循环（GMainLoop）分发事件
    GstBus *bus = gst_element_get_bus(data.pipeline);
    gst_bus_add_signal_watch(bus);
    g_signal_connect(G_OBJECT(bus), "message::error", G_CALLBACK(error_cb), &data);
    g_signal_connect(G_OBJECT(bus), "message::eos", G_CALLBACK(eos_cb), &data);
    g_signal_connect(G_OBJECT(bus), "message::state-changed", G_CALLBACK(state_changed_cb), &data);
    // g_signal_connect(G_OBJECT(bus), "message::application", (GCallback)application_cb, &data);
    gst_object_unref(bus);

    /* Start playing the pipeline */
    gst_element_set_state(data.pipeline, GST_STATE_PLAYING);

    return TRUE;
}

void Gstreamer_HW::destroy_pipeline()
{
    g_print("Destroying pipeline...\n");

    // Check if pipeline is null first
    if (data.pipeline)
    {
        GstStateChangeReturn ret = gst_element_set_state(data.pipeline, GST_STATE_NULL);

        if (ret == GST_STATE_CHANGE_FAILURE) {
            g_printerr("Failed to set pipeline to NULL state.\n");
        } else {
            g_print("Pipeline state changed to NULL successfully.\n");
        }

        gst_object_unref(data.pipeline); // Unref pipeline
        g_print("Pipeline unref'd and set to NULL state.\n");
        data.pipeline = nullptr;
    }
    else
    {
        g_print("Pipeline is already null.\n");
    }

    // Nullify elements to prevent future use
    data.src = nullptr;
    data.depay = nullptr;
    data.parse = nullptr;
    data.dec = nullptr;
    data.sink = nullptr;

    data.video_linked = FALSE;

    g_print("Pipeline destroyed successfully.\n");
}
