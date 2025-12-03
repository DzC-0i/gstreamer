#include <gst/gst.h>
#include <glib.h>
#include <string>
#include <vector>
#include <map>
#include <cmath>

class MultiStreamPlayer {
private:
    struct StreamInfo {
        GstElement* src;
        GstElement* convert;
        GstElement* scale;
        GstElement* queue;
        std::string uri;
    };

    GstElement* pipeline_;
    GstElement* compositor_;
    std::vector<StreamInfo> streams_;
    GMainLoop* main_loop_;
    gint stream_count_;
    gint grid_cols_;
    gint grid_rows_;

public:
    MultiStreamPlayer() : pipeline_(nullptr), compositor_(nullptr),
                         main_loop_(nullptr), stream_count_(0), grid_cols_(2), grid_rows_(2) {}

    ~MultiStreamPlayer() {
        cleanup();
    }

    // 设置网格布局
    void setGridLayout(int cols, int rows) {
        grid_cols_ = cols;
        grid_rows_ = rows;
    }

    // 添加视频流
    bool addStream(const std::string& uri) {
        if (stream_count_ >= grid_cols_ * grid_rows_) {
            g_printerr("Cannot add more streams. Grid layout is full (%dx%d = %d streams)\n",
                      grid_cols_, grid_rows_, grid_cols_ * grid_rows_);
            return false;
        }

        StreamInfo stream_info;
        stream_info.uri = uri;

        // 创建流元素
        stream_info.src = gst_element_factory_make("uridecodebin", nullptr);
        stream_info.convert = gst_element_factory_make("videoconvert", nullptr);
        stream_info.scale = gst_element_factory_make("videoscale", nullptr);
        stream_info.queue = gst_element_factory_make("queue", nullptr);

        if (!stream_info.src || !stream_info.convert || !stream_info.scale || !stream_info.queue) {
            g_printerr("Failed to create elements for stream: %s\n", uri.c_str());
            return false;
        }

        // 设置URI
        g_object_set(G_OBJECT(stream_info.src), "uri", uri.c_str(), nullptr);

        streams_.push_back(stream_info);
        stream_count_++;

        g_print("Successfully added stream: %s\n", uri.c_str());
        return true;
    }

    // 创建管道和布局
    bool buildPipeline() {
        if (streams_.empty()) {
            g_printerr("No streams to play\n");
            return false;
        }

        // 创建主管道
        pipeline_ = gst_pipeline_new("multi-stream-pipeline");
        if (!pipeline_) {
            g_printerr("Failed to create pipeline\n");
            return false;
        }

        // 创建合成器
        compositor_ = gst_element_factory_make("compositor", "compositor");
        if (!compositor_) {
            g_printerr("Failed to create compositor\n");
            return false;
        }

        // 创建视频sink
        GstElement* sink = gst_element_factory_make("autovideosink", "sink");
        if (!sink) {
            g_printerr("Failed to create video sink\n");
            return false;
        }

        // 将元素添加到管道
        gst_bin_add_many(GST_BIN(pipeline_), compositor_, sink, nullptr);

        // 计算每个视频的尺寸和位置
        int grid_width = 1920;  // 输出总宽度
        int grid_height = 1080; // 输出总高度
        int cell_width = grid_width / grid_cols_;
        int cell_height = grid_height / grid_rows_;

        g_print("Grid layout: %dx%d, Cell size: %dx%d\n",
                grid_cols_, grid_rows_, cell_width, cell_height);

        for (int i = 0; i < stream_count_; i++) {
            auto& stream = streams_[i];

            // 计算在网格中的位置
            int row = i / grid_cols_;
            int col = i % grid_cols_;
            int x = col * cell_width;
            int y = row * cell_height;

            g_print("Stream %d position: (%d, %d) size: %dx%d\n",
                   i, x, y, cell_width, cell_height);

            // 将流元素添加到管道
            gst_bin_add_many(GST_BIN(pipeline_),
                           stream.src, stream.queue, stream.scale, stream.convert, nullptr);

            // 连接视频处理链
            if (!gst_element_link_many(stream.queue, stream.scale, stream.convert, nullptr)) {
                g_printerr("Failed to link stream elements for stream %d\n", i);
                return false;
            }

            // 连接到合成器
            GstPad* convert_pad = gst_element_get_static_pad(stream.convert, "src");
            GstPad* compositor_sink_pad = gst_element_get_request_pad(compositor_, "sink_%u");

            if (!compositor_sink_pad) {
                g_printerr("Failed to get compositor sink pad for stream %d\n", i);
                gst_object_unref(convert_pad);
                return false;
            }

            // 设置合成器属性
            g_object_set(compositor_sink_pad,
                        "xpos", x,
                        "ypos", y,
                        "width", cell_width,
                        "height", cell_height,
                        nullptr);

            // 连接pad
            if (gst_pad_link(convert_pad, compositor_sink_pad) != GST_PAD_LINK_OK) {
                g_printerr("Failed to link stream %d to compositor\n", i);
                gst_object_unref(convert_pad);
                gst_object_unref(compositor_sink_pad);
                return false;
            }

            gst_object_unref(convert_pad);
            gst_object_unref(compositor_sink_pad);

            // 设置uridecodebin的信号处理
            g_signal_connect(stream.src, "pad-added", G_CALLBACK(onPadAdded), &stream);
        }

        // 连接合成器到sink
        if (!gst_element_link(compositor_, sink)) {
            g_printerr("Failed to link compositor to sink\n");
            return false;
        }

        return true;
    }

    // 启动所有流
    bool startAll() {
        if (!buildPipeline()) {
            g_printerr("Failed to build pipeline\n");
            return false;
        }

        // 设置管道状态为播放
        GstStateChangeReturn ret = gst_element_set_state(pipeline_, GST_STATE_PLAYING);
        if (ret == GST_STATE_CHANGE_FAILURE) {
            g_printerr("Failed to start pipeline\n");
            return false;
        }

        // 设置总线监视
        GstBus* bus = gst_element_get_bus(pipeline_);
        gst_bus_add_watch(bus, busWatchHandler, this);
        gst_object_unref(bus);

        // 创建主循环
        main_loop_ = g_main_loop_new(nullptr, FALSE);

        g_print("Starting %d video streams in %dx%d grid...\n",
               stream_count_, grid_cols_, grid_rows_);
        g_print("Press Ctrl+C to stop\n");

        // 运行主循环
        g_main_loop_run(main_loop_);

        return true;
    }

private:
    // 处理uridecodebin的pad-added信号
    static void onPadAdded(GstElement* src, GstPad* new_pad, gpointer user_data) {
        StreamInfo* stream = static_cast<StreamInfo*>(user_data);

        GstCaps* new_pad_caps = gst_pad_get_current_caps(new_pad);
        GstStructure* new_pad_struct = gst_caps_get_structure(new_pad_caps, 0);
        const gchar* new_pad_type = gst_structure_get_name(new_pad_struct);

        g_print("New pad type: %s\n", new_pad_type);

        if (g_str_has_prefix(new_pad_type, "video/x-raw")) {
            // 视频pad，连接到queue
            GstPad* sink_pad = gst_element_get_static_pad(stream->queue, "sink");

            if (gst_pad_is_linked(sink_pad)) {
                g_print("Queue pad is already linked. Ignoring.\n");
                gst_object_unref(sink_pad);
                gst_caps_unref(new_pad_caps);
                return;
            }

            GstPadLinkReturn ret = gst_pad_link(new_pad, sink_pad);
            if (GST_PAD_LINK_FAILED(ret)) {
                g_printerr("Failed to link pads\n");
            } else {
                g_print("Successfully linked video pad\n");
            }

            gst_object_unref(sink_pad);
        }

        gst_caps_unref(new_pad_caps);
    }

    // 总线消息处理
    static gboolean busWatchHandler(GstBus* bus, GstMessage* msg, gpointer user_data) {
        MultiStreamPlayer* player = static_cast<MultiStreamPlayer*>(user_data);

        switch (GST_MESSAGE_TYPE(msg)) {
            case GST_MESSAGE_EOS: {
                g_print("End of stream reached\n");
                g_main_loop_quit(player->main_loop_);
                break;
            }

            case GST_MESSAGE_ERROR: {
                GError* error;
                gchar* debug_info;
                gst_message_parse_error(msg, &error, &debug_info);
                g_printerr("Error: %s\n", error->message);
                if (debug_info) {
                    g_printerr("Debug info: %s\n", debug_info);
                }
                g_error_free(error);
                g_free(debug_info);
                g_main_loop_quit(player->main_loop_);
                break;
            }

            case GST_MESSAGE_WARNING: {
                GError* warning;
                gchar* debug_info;
                gst_message_parse_warning(msg, &warning, &debug_info);
                g_printerr("Warning: %s\n", warning->message);
                if (debug_info) {
                    g_printerr("Debug info: %s\n", debug_info);
                }
                g_error_free(warning);
                g_free(debug_info);
                break;
            }

            case GST_MESSAGE_STATE_CHANGED: {
                if (GST_MESSAGE_SRC(msg) == GST_OBJECT(player->pipeline_)) {
                    GstState old_state, new_state, pending_state;
                    gst_message_parse_state_changed(msg, &old_state, &new_state, &pending_state);
                    g_print("Pipeline state changed from %s to %s\n",
                           gst_element_state_get_name(old_state),
                           gst_element_state_get_name(new_state));
                }
                break;
            }

            default:
                break;
        }

        return TRUE;
    }

    // 清理资源
    void cleanup() {
        if (pipeline_) {
            gst_element_set_state(pipeline_, GST_STATE_NULL);
            gst_object_unref(pipeline_);
            pipeline_ = nullptr;
        }

        if (main_loop_) {
            g_main_loop_unref(main_loop_);
            main_loop_ = nullptr;
        }
    }
};

// 使用示例
int main(int argc, char* argv[]) {
    // 初始化GStreamer
    gst_init(&argc, &argv);

    // 创建多流播放器
    MultiStreamPlayer player;

    // 设置网格布局 (列数, 行数)
    player.setGridLayout(2, 2);  // 2x2网格，最多4个视频

    // 添加多个视频流
    // player.addStream("https://gstreamer.freedesktop.org/data/media/sintel_trailer-480p.webm");
    // player.addStream("https://gstreamer.freedesktop.org/data/media/sintel_trailer-480p.webm");
    // player.addStream("https://gstreamer.freedesktop.org/data/media/sintel_trailer-480p.webm");
    // player.addStream("https://gstreamer.freedesktop.org/data/media/sintel_trailer-480p.webm");

    // 对于实际RTSP流:
    player.addStream("rtsp://admin:kemande1206@192.168.13.8");
    player.addStream("rtsp://admin:kemande1206@192.168.13.9");
    player.addStream("rtsp://admin:kemande1206@192.168.13.120");
    player.addStream("rtsp://admin:kemande1206@192.168.13.183");

    // 启动所有流（在同一个窗口中显示）
    player.startAll();

    return 0;
}

// g++ mul_pull_place.cc -o mul_pull_place `pkg-config --cflags --libs gstreamer-1.0`
