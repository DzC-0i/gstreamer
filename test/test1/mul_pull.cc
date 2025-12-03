#include <gst/gst.h>
#include <glib.h>
#include <string>
#include <vector>
#include <map>

class MultiStreamPlayer {
private:
    struct StreamInfo {
        GstElement* pipeline;
        GstElement* video_sink;
        std::string uri;
        guint bus_watch_id;
    };

    std::vector<StreamInfo> streams_;
    GMainLoop* main_loop_;
    gint stream_count_;

public:
    MultiStreamPlayer() : main_loop_(nullptr), stream_count_(0) {}

    ~MultiStreamPlayer() {
        cleanup();
    }

    // 添加视频流
    bool addStream(const std::string& uri, const std::string& window_id = "") {
        StreamInfo stream_info;
        stream_info.uri = uri;

        // 创建管道
        std::string pipeline_str;
        if (window_id.empty()) {
            // 自动分配窗口
            // pipeline_str = "uridecodebin uri=" + uri + " ! videoconvert ! videoscale ! queue ! autovideosink sync=false";            // 这边不同步
            pipeline_str = "uridecodebin uri=" + uri + " ! videoconvert ! videoscale ! queue ! autovideosink sync=true";                // 默认是 true 不加也行
        } else {
            // 指定窗口ID（适用于X11）
            // pipeline_str = "uridecodebin uri=" + uri + " ! videoconvert ! videoscale ! queue ! xvimagesink window-id=" + window_id + " sync=false";
            pipeline_str = "uridecodebin uri=" + uri + " ! videoconvert ! videoscale ! queue ! xvimagesink window-id=" + window_id + " sync=true";
        }

        g_print("Creating pipeline: %s\n", pipeline_str.c_str());

        GError* error = nullptr;
        stream_info.pipeline = gst_parse_launch(pipeline_str.c_str(), &error);

        if (!stream_info.pipeline || error) {
            g_printerr("Failed to create pipeline for %s: %s\n", uri.c_str(), error ? error->message : "Unknown error");
            if (error) g_error_free(error);
            return false;
        }

        // 获取video sink用于后续控制
        stream_info.video_sink = gst_bin_get_by_name(GST_BIN(stream_info.pipeline), "sink");
        if (!stream_info.video_sink) {
            // 尝试获取autovideosink或xvimagesink
            GstElement* sink = nullptr;
            g_object_get(stream_info.pipeline, "video-sink", &sink, NULL);
            if (sink) {
                stream_info.video_sink = sink;
            }
        }

        // 设置总线监视
        GstBus* bus = gst_element_get_bus(stream_info.pipeline);
        stream_info.bus_watch_id = gst_bus_add_watch(bus, busWatchHandler, this);
        gst_object_unref(bus);

        streams_.push_back(stream_info);
        stream_count_++;

        g_print("Successfully added stream: %s\n", uri.c_str());
        return true;
    }

    // 启动所有流
    bool startAll() {
        if (streams_.empty()) {
            g_printerr("No streams to play\n");
            return false;
        }

        // 启动所有管道
        for (auto& stream : streams_) {
            GstStateChangeReturn ret = gst_element_set_state(stream.pipeline, GST_STATE_PLAYING);
            if (ret == GST_STATE_CHANGE_FAILURE) {
                g_printerr("Failed to start pipeline for: %s\n", stream.uri.c_str());
                return false;
            }
        }

        // 创建主循环
        main_loop_ = g_main_loop_new(nullptr, FALSE);

        g_print("Starting %d video streams...\n", stream_count_);
        g_print("Press Ctrl+C to stop\n");

        // 运行主循环
        g_main_loop_run(main_loop_);

        return true;
    }

    // 停止特定流
    void stopStream(int index) {
        if (index >= 0 && index < streams_.size()) {
            gst_element_set_state(streams_[index].pipeline, GST_STATE_NULL);
        }
    }

    // 暂停特定流
    void pauseStream(int index) {
        if (index >= 0 && index < streams_.size()) {
            gst_element_set_state(streams_[index].pipeline, GST_STATE_PAUSED);
        }
    }

    // 恢复播放特定流
    void playStream(int index) {
        if (index >= 0 && index < streams_.size()) {
            gst_element_set_state(streams_[index].pipeline, GST_STATE_PLAYING);
        }
    }

private:
    // 总线消息处理
    static gboolean busWatchHandler(GstBus* bus, GstMessage* msg, gpointer user_data) {
        MultiStreamPlayer* player = static_cast<MultiStreamPlayer*>(user_data);

        switch (GST_MESSAGE_TYPE(msg)) {
            case GST_MESSAGE_EOS: {
                GstElement* pipeline = GST_ELEMENT(GST_MESSAGE_SRC(msg));
                for (const auto& stream : player->streams_) {
                    if (stream.pipeline == pipeline) {
                        g_print("End of stream: %s\n", stream.uri.c_str());
                        break;
                    }
                }
                break;
            }

            case GST_MESSAGE_ERROR: {
                GError* error;
                gchar* debug_info;
                GstElement* pipeline = GST_ELEMENT(GST_MESSAGE_SRC(msg));

                gst_message_parse_error(msg, &error, &debug_info);

                for (const auto& stream : player->streams_) {
                    if (stream.pipeline == pipeline) {
                        g_printerr("Error in stream %s: %s\n", stream.uri.c_str(), error->message);
                        if (debug_info) {
                            g_printerr("Debug info: %s\n", debug_info);
                        }
                        break;
                    }
                }

                g_error_free(error);
                g_free(debug_info);

                // 出错时退出主循环
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
                if (GST_MESSAGE_SRC(msg) == GST_OBJECT(player->streams_[0].pipeline)) {
                    GstState old_state, new_state, pending_state;
                    gst_message_parse_state_changed(msg, &old_state, &new_state, &pending_state);

                    if (GST_ELEMENT(GST_MESSAGE_SRC(msg)) == player->streams_[0].pipeline) {
                        g_print("Pipeline state changed from %s to %s\n",
                               gst_element_state_get_name(old_state),
                               gst_element_state_get_name(new_state));
                    }
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
        // 停止所有流
        for (auto& stream : streams_) {
            if (stream.pipeline) {
                gst_element_set_state(stream.pipeline, GST_STATE_NULL);

                if (stream.bus_watch_id) {
                    GstBus* bus = gst_element_get_bus(stream.pipeline);
                    gst_bus_remove_watch(bus);
                    gst_object_unref(bus);
                }

                if (stream.video_sink) {
                    gst_object_unref(stream.video_sink);
                }

                gst_object_unref(stream.pipeline);
            }
        }
        streams_.clear();

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

    // 添加多个视频流
    // 这里使用测试流，你可以替换为实际的RTSP、HTTP等流媒体地址

    // 测试流1:  Big Buck Bunny 测试视频
    player.addStream("https://gstreamer.freedesktop.org/data/media/sintel_trailer-480p.webm");

    // 测试流2: Sintel 测试视频
    player.addStream("https://gstreamer.freedesktop.org/data/media/sintel_trailer-480p.webm");

    // 测试流3: 另一个测试视频
    player.addStream("https://gstreamer.freedesktop.org/data/media/sintel_trailer-480p.webm");

    // 实际应用中可以使用RTSP流:
    // player.addStream("rtsp://your-camera-ip:554/stream1");
    // player.addStream("rtsp://your-camera-ip:554/stream2");

    // 启动所有流
    player.startAll();

    return 0;
}

// g++ mul_pull.cc -o mul_pull `pkg-config --cflags --libs gstreamer-1.0`
