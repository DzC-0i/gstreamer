#include <gst/gst.h>

int main(int argc, char *argv[]) {
    GstElement *pipeline, *source, *sink, *filter, *convert;
    GstBus *bus;
    GstMessage *msg;
    GstStateChangeReturn ret;

    /* 初始化 GStreamer */
    gst_init(&argc, &argv);

    /* 创建元素 */
    source = gst_element_factory_make("videotestsrc", "source");
    sink = gst_element_factory_make("autovideosink", "sink");
    filter = gst_element_factory_make("vertigotv", "filter");
    convert = gst_element_factory_make("videoconvert", "convert"); /* 用于格式转换 */

    /* 检查所有元素是否成功创建 */
    if (!pipeline || !source || !sink || !filter || !convert) {
        g_printerr("有元素无法创建，请检查插件安装。\n");
        return -1;
    }

    /* 创建空管道 */
    pipeline = gst_pipeline_new("test-pipeline");

    /* 构建管道 */
    gst_bin_add_many(GST_BIN(pipeline), source, filter, convert, sink, NULL);

    /* 链接元素：source -> filter -> convert -> sink */
    if (gst_element_link_many(source, filter, convert, sink, NULL) != TRUE) {
        g_printerr("元素无法链接。通常是由于格式不兼容。\n");
        gst_object_unref(pipeline);
        return -1;
    }

    /* 设置播放状态 */
    ret = gst_element_set_state(pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        g_printerr("无法启动管道。\n");
        gst_object_unref(pipeline);
        return -1;
    }

    /* 等待播放结束或出错 */
    bus = gst_element_get_bus(pipeline);
    msg = gst_bus_timed_pop_filtered(bus, GST_CLOCK_TIME_NONE, GST_MESSAGE_ERROR | GST_MESSAGE_EOS);

    /* 解析消息 */
    if (msg != NULL) {
        GError *error;
        gchar *debug_info;

        switch (GST_MESSAGE_TYPE(msg)) {
            case GST_MESSAGE_ERROR:
                gst_message_parse_error(msg, &error, &debug_info);
                g_printerr("播放过程中出错: %s\n", error->message);
                g_printerr("调试信息: %s\n", debug_info ? debug_info : "无");
                g_clear_error(&error);
                g_free(debug_info);
                break;
            case GST_MESSAGE_EOS:
                g_print("播放结束。\n");
                break;
            default:
                g_printerr("收到意外消息。\n");
                break;
        }
        gst_message_unref(msg);
    }

    /* 清理资源 */
    gst_object_unref(bus);
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
    return 0;
}

// 编译命令：
// gcc playback-tutorial-7_video.c -o playback-tutorial-7_video `pkg-config --cflags --libs gstreamer-1.0`
