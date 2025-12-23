#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <gst/rtsp/rtsp.h>

#include <opencv2/opencv.hpp>

#include <iostream>
#include <thread>
#include <atomic>
#include <signal.h>

struct PushContext
{
    GstElement *pipeline;
    GstElement *appsrc;
    GMainLoop *loop;
    cv::VideoCapture *cap;
};

static std::atomic<bool> running{true};
static std::thread worker;

/* ---------------- 信号处理（安全） ---------------- */
void handle_signal(int)
{
    g_print("\r\nSignal received, exiting...\n");
    running.store(false);
}

/* ---------------- Bus 回调 ---------------- */
static void bus_error_cb(GstBus *, GstMessage *msg, gpointer data)
{
    GError *err = nullptr;
    gchar *dbg = nullptr;

    gst_message_parse_error(msg, &err, &dbg);
    g_printerr("ERROR: %s\n", err->message);
    g_printerr("DEBUG: %s\n", dbg ? dbg : "none");

    g_clear_error(&err);
    g_free(dbg);

    auto *ctx = static_cast<PushContext *>(data);
    g_main_loop_quit(ctx->loop);
}

static void bus_eos_cb(GstBus *, GstMessage *, gpointer data)
{
    auto *ctx = static_cast<PushContext *>(data);
    g_print("EOS received\n");
    g_main_loop_quit(ctx->loop);
}

static void state_changed_cb(GstBus *bus, GstMessage *msg, gpointer data)
{
    auto *ctx = static_cast<PushContext *>(data);

    GstState old_state, new_state, pending_state;
    gst_message_parse_state_changed(msg, &old_state, &new_state, &pending_state); // 打印多个消息
    // if (GST_MESSAGE_SRC(msg) == GST_OBJECT(data->pipeline))
    // {
    // g_print("Pipeline state changed from %s to %s\n",
    // gst_state_get_name(old_state), gst_state_get_name(new_state));
    // }
    const gchar *src_name = GST_OBJECT_NAME(msg->src);
    const gchar *pipeline_name = GST_OBJECT_NAME(ctx->pipeline);
    // // 打印消息来源（是Pipeline本身，还是子元素如rtspsrc）
    // g_print("[State Change] Source: %s | Old: %s | New: %s | Pending: %s\n",
    // src_name,
    // gst_element_state_get_name(old_state),
    // gst_element_state_get_name(new_state),
    // gst_element_state_get_name(pending_state));

    // 只关注Pipeline自身的“最终状态变更”（过滤子元素和过渡消息）
    if (g_strcmp0(src_name, pipeline_name) == 0 && old_state != new_state)
    {
        g_print("Pipeline state changed from %s to %s\n",
                gst_element_state_get_name(old_state),
                gst_element_state_get_name(new_state));
    }
}

/* ---------------- 推帧线程 ---------------- */
void push_thread(PushContext *ctx)
{
    cv::Mat frame_bgr;
    cv::Mat frame_yuv;

    int i = 0;

    GstClockTime target_frame_duration = gst_util_uint64_scale_int(1, GST_SECOND, 25); // 25fps

    using steady = std::chrono::steady_clock;

    while (running.load())
    {
        if (!ctx->cap->read(frame_bgr))
        {
            g_print("File EOS\n");
            gst_app_src_end_of_stream(GST_APP_SRC(ctx->appsrc));
            break;
        }

        cv::cvtColor(frame_bgr, frame_yuv, cv::COLOR_BGR2YUV_I420);

        const guint size = frame_yuv.total() * frame_yuv.elemSize();
        GstBuffer *buffer = gst_buffer_new_allocate(nullptr, size, nullptr);

        // === 单调系统时间 ===
        static const auto t0 = steady::now();
        static steady::time_point last_tp = t0;

        static GstClockTime last_pts = 0;

        auto now = steady::now();
        GstClockTime pts = std::chrono::duration_cast<std::chrono::nanoseconds>(now - t0).count();

        GstClockTime frame_duration = std::chrono::duration_cast<std::chrono::nanoseconds>(now - last_tp).count();

        if (pts <= last_pts)
            pts = last_pts + 1;

        last_pts = pts;
        last_tp  = now;

        GST_BUFFER_PTS(buffer) = pts;
        GST_BUFFER_DTS(buffer) = pts;

        if (pts < target_frame_duration / 2)
        {
            g_print("\r\n12 \t %d \n", i++);
            GST_BUFFER_DURATION(buffer) = target_frame_duration;
        }
        else{
            GST_BUFFER_DURATION(buffer) = frame_duration;
        }

        // === 填充数据 ===
        GstMapInfo map;
        gst_buffer_map(buffer, &map, GST_MAP_WRITE);
        memcpy(map.data, frame_yuv.data, size);
        gst_buffer_unmap(buffer, &map);

        GstFlowReturn ret =
            gst_app_src_push_buffer(GST_APP_SRC(ctx->appsrc), buffer);

        if (ret != GST_FLOW_OK)
        {
            gst_buffer_unref(buffer);
            g_printerr("push_buffer failed: %d\n", ret);
            break;
        }

        g_usleep(40000); // 25fps , 实际上比25fps要慢一些，测试使用就不调了
    }

    ctx->cap->release();
}

/* ---------------------------------------------------------
 * main()
 * opencv -> appsrc -> videoconvert -> mpph265enc -> queue -> h265parse -> rtspclientsink (rtsp://127.0.0.1:8554/live)
 * opencv -> appsrc -> videoconvert -> mpph265enc -> queue -> h265parse -> rtph265pay -> udpsink (host=127.0.0.1 port=1234)
 * g++ ./push-rtsp.cpp -o ./push-rtsp `pkg-config --cflags --libs gstreamer-1.0 gstreamer-rtsp-1.0 gstreamer-app-1.0 opencv4`
 * 查看 pad 、回调、参数等等 可以通过 `gst-inspect-1.0 + [管道插件](如: mpph265enc 、 rtspclientsink)` 查看情况
 * ---------------------------------------------------------
 * */
int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        std::cout << "Usage: " << argv[0]
                  << " ./test.mp4 rtsp://127.0.0.1:8554/live\n";
        return -1;
    }

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    std::string input = argv[1];
    std::string rtsp = argv[2];

    gst_init(nullptr, nullptr);

    cv::VideoCapture cap(input);
    if (!cap.isOpened())
    {
        std::cerr << "Open video failed\n";
        return -1;
    }

    double fps = cap.get(cv::CAP_PROP_FPS);               // 视频帧率
    int width = cap.get(cv::CAP_PROP_FRAME_WIDTH);        // 帧宽度
    int height = cap.get(cv::CAP_PROP_FRAME_HEIGHT);      // 帧高度
    int total_frames = cap.get(cv::CAP_PROP_FRAME_COUNT); // 总帧数

    g_print("Video info:\r\n");
    g_print(" FPS: %.2f\r\n", fps);
    g_print(" Resolution: %d x %d\r\n", width, height);
    g_print(" Total frames: %d\r\n", total_frames);

    /* ---------- GStreamer pipeline ---------- */
    GstElement *pipeline = gst_pipeline_new("rk3588-pipeline");
    GstElement *appsrc = gst_element_factory_make("appsrc", "src");
    GstElement *enc = gst_element_factory_make("mpph265enc", "enc");
    GstElement *parse = gst_element_factory_make("h265parse", "parse");
    GstElement *queue = gst_element_factory_make("queue", "queue");
    GstElement *sink = gst_element_factory_make("rtspclientsink", "sink");

    if (!pipeline || !appsrc || !enc || !parse || !queue || !sink)
    {
        g_printerr("Create element failed\n");
        return -1;
    }

    /* ---------- appsrc ---------- */
    g_object_set(appsrc,
                 "is-live", TRUE,
                 "format", GST_FORMAT_TIME,
                 "do-timestamp", FALSE, // 使用外部时间戳，使流更加稳定
                 "block", TRUE,
                 "max-bytes", 4 * 1024 * 1024, // 4MB 缓冲区
                 "emit-signals", FALSE,        // 你是主动 push，不需要信号
                 "stream-type", GST_APP_STREAM_TYPE_STREAM,
                 NULL);

    GstCaps *caps = gst_caps_new_simple(
        "video/x-raw",
        "format", G_TYPE_STRING, "I420",
        "width", G_TYPE_INT, width,
        "height", G_TYPE_INT, height,
        "framerate", GST_TYPE_FRACTION, (int)fps, 1,
        NULL);

    g_object_set(appsrc, "caps", caps, NULL);
    gst_caps_unref(caps);

    /* ---------- mpph265enc（RK3588 推荐） ---------- */
    g_object_set(enc,
                 "rc-mode", 1, // CBR
                 "bps", 4000000,  // >4Mbps(1080p)
                 "bps-max", 4500000,
                 "bps-min", 3500000,

                 "gop", (int)fps * 2, // 2 秒一个 IDR（RTSP 友好）
                 "header-mode", 1,    // SPS/PPS 每个 IDR

                 "qp-init", 28,
                 "qp-min", 22,
                 "qp-max", 38,

                 "qos", TRUE,
                 "max-reenc", 1,   // 最多重编码 1 次

                 NULL);

    /* ---------- h265parse ---------- */
    g_object_set(parse,
                 "config-interval", -1,  // 每个 IDR 注入 VPS/SPS/PPS
                 NULL);

    /* ---------- queue ---------- */
    g_object_set(queue,
                 "max-size-time", 500 * GST_MSECOND,
                 "max-size-buffers", 0,
                 "max-size-bytes", 0,
                 "leaky",  2, // downstream（更适合推流）
                 "silent", TRUE,
                 NULL);
    // g_object_set(queue,
    //              "max-size-buffers", 5, // 最多 5 帧
    //              "max-size-time", 0,
    //              "max-size-bytes", 0,
    //              "leaky", 2, // downstream（更适合推流）
    //              "silent", TRUE,
    //              NULL);

    /* ---------- rtsp sink ---------- */
    g_object_set(sink,
                 "location", rtsp.c_str(),
                 "protocols", GST_RTSP_LOWER_TRANS_TCP,
                 "latency", 300,              // 200~500ms
                 "tcp-timeout", 5 * 1000000,     // 5s
                 "retry", 5,
                 "do-rtsp-keep-alive", TRUE,
                 NULL);

    gst_bin_add_many(GST_BIN(pipeline),
                     appsrc, enc, parse, queue, sink, NULL);

    gst_element_link_many(
        appsrc, enc, parse, queue, sink, NULL);

    /* ---------- Bus ---------- */
    GMainLoop *loop = g_main_loop_new(nullptr, FALSE);
    GstBus *bus = gst_element_get_bus(pipeline);
    gst_bus_add_signal_watch(bus);

    PushContext ctx;
    ctx.pipeline = pipeline;
    ctx.appsrc = appsrc;
    ctx.loop = loop;
    ctx.cap = &cap;

    g_signal_connect(bus, "message::error",
                     G_CALLBACK(bus_error_cb), &ctx);
    g_signal_connect(bus, "message::eos",
                     G_CALLBACK(bus_eos_cb), &ctx);

    g_signal_connect(bus, "message::state-changed",
                     G_CALLBACK(state_changed_cb), &ctx);

    gst_object_unref(bus);

    /* ---------- Start ---------- */
    gst_element_set_state(pipeline, GST_STATE_PLAYING);

    worker = std::thread(push_thread, &ctx);

    g_main_loop_run(loop);

    /* ---------- Cleanup ---------- */
    running.store(false);
    if (worker.joinable())
        worker.join();

    g_print("Waiting...\n");
    g_usleep(4000000);

    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
    g_main_loop_unref(loop);

    g_print("Exit clean\n");
    return 0;
}
