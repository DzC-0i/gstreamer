#include <gst/gst.h>
#include <gst/gstdebugutils.h> // ✅ 导出 DOT 文件所需头文件

/* 外加的一个宏定义, 用于在低版本也能够通过编译情况*/
#ifndef GST_STATE_GET_NAME
#define gst_state_get_name(state)                                                                \
    ((state) == GST_STATE_VOID_PENDING ? "VOID_PENDING" : (state) == GST_STATE_NULL  ? "NULL"    \
                                                      : (state) == GST_STATE_READY   ? "READY"   \
                                                      : (state) == GST_STATE_PAUSED  ? "PAUSED"  \
                                                      : (state) == GST_STATE_PLAYING ? "PLAYING" \
                                                                                     : "UNKNOWN")
#endif

typedef struct _CustomData
{
    GstElement *pipeline;
    GstElement *source;
    GstElement *audio_convert;
    GstElement *audio_resample;
    GstElement *audio_sink;
    GstElement *video_convert;
    GstElement *video_sink;
} CustomData;

/* 当 uridecodebin 动态创建 pad 时回调 */
static void pad_added_handler(GstElement *src, GstPad *new_pad, CustomData *data)
{
    GstCaps *caps = gst_pad_query_caps(new_pad, NULL);
    const gchar *name = gst_structure_get_name(gst_caps_get_structure(caps, 0));
    g_print("Received new pad '%s' from '%s' with type '%s'\n",
            GST_PAD_NAME(new_pad), GST_ELEMENT_NAME(src), name);

    if (g_str_has_prefix(name, "audio/x-raw"))
    {
        GstPad *sink_pad = gst_element_get_static_pad(data->audio_convert, "sink");
        if (!gst_pad_is_linked(sink_pad))
        {
            if (gst_pad_link(new_pad, sink_pad) == GST_PAD_LINK_OK)
                g_print("Linked audio pad successfully\n");
        }
        gst_object_unref(sink_pad);
    }
    else if (g_str_has_prefix(name, "video/x-raw"))
    {
        GstPad *sink_pad = gst_element_get_static_pad(data->video_convert, "sink");
        if (!gst_pad_is_linked(sink_pad))
        {
            if (gst_pad_link(new_pad, sink_pad) == GST_PAD_LINK_OK)
                g_print("Linked video pad successfully\n");
        }
        gst_object_unref(sink_pad);
    }

    gst_caps_unref(caps);
}

int main(int argc, char *argv[])
{
    CustomData data;
    GstBus *bus;
    GstMessage *msg;
    gboolean terminate = FALSE;

    gst_init(&argc, &argv);

    /* 创建元素 */
    data.source = gst_element_factory_make("uridecodebin", "source");
    data.audio_convert = gst_element_factory_make("audioconvert", "audioconvert");
    data.audio_resample = gst_element_factory_make("audioresample", "audioresample");
    data.audio_sink = gst_element_factory_make("autoaudiosink", "audio_sink");
    data.video_convert = gst_element_factory_make("videoconvert", "videoconvert");
    data.video_sink = gst_element_factory_make("autovideosink", "video_sink");

    data.pipeline = gst_pipeline_new("media-pipeline");

    if (!data.pipeline || !data.source || !data.audio_convert || !data.audio_resample ||
        !data.audio_sink || !data.video_convert || !data.video_sink)
    {
        g_printerr("Failed to create elements\n");
        return -1;
    }

    /* 构建管道 */
    gst_bin_add_many(GST_BIN(data.pipeline),
                     data.source,
                     data.audio_convert, data.audio_resample, data.audio_sink,
                     data.video_convert, data.video_sink,
                     NULL);

    if (!gst_element_link_many(data.audio_convert, data.audio_resample, data.audio_sink, NULL))
    {
        g_printerr("Failed to link audio chain\n");
        return -1;
    }

    if (!gst_element_link_many(data.video_convert, data.video_sink, NULL))
    {
        g_printerr("Failed to link video chain\n");
        return -1;
    }

    /* 设置播放 URI */
    g_object_set(data.source,
                 "uri", "https://gstreamer.freedesktop.org/data/media/sintel_trailer-480p.webm",
                 NULL);

    g_signal_connect(data.source, "pad-added", G_CALLBACK(pad_added_handler), &data);

    /* 启动播放 */
    gst_element_set_state(data.pipeline, GST_STATE_PLAYING);

    /* ✅ 导出 DOT 文件（关键部分） */
    GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(data.pipeline),
                              GST_DEBUG_GRAPH_SHOW_ALL,
                              "pipeline_PLAYING");

    g_print("Use command to export file to running place: export GST_DEBUG_DUMP_DOT_DIR=. \n");
    g_print("DOT file exported: pipeline_PLAYING.dot\n");
    g_print("Use command: dot -Tpng pipeline_PLAYING.dot -o pipeline.png\n");

    /* 等待播放结束或错误 */
    bus = gst_element_get_bus(data.pipeline);
    while (!terminate)
    {
        msg = gst_bus_timed_pop_filtered(bus, GST_CLOCK_TIME_NONE,
                                         GST_MESSAGE_ERROR | GST_MESSAGE_EOS);

        if (msg != NULL)
        {
            GError *err;
            gchar *debug_info;

            switch (GST_MESSAGE_TYPE(msg))
            {
            case GST_MESSAGE_ERROR:
                gst_message_parse_error(msg, &err, &debug_info);
                g_printerr("Error: %s\n", err->message);
                g_clear_error(&err);
                g_free(debug_info);
                terminate = TRUE;
                break;
            case GST_MESSAGE_EOS:
                g_print("End of stream\n");
                terminate = TRUE;
                break;
            default:
                break;
            }
            gst_message_unref(msg);
        }
    }

    /* 清理资源 */
    gst_element_set_state(data.pipeline, GST_STATE_NULL);
    gst_object_unref(data.pipeline);
    gst_object_unref(bus);
    return 0;
}

// gcc basic-tutorial-3-audio_video.c -o basic-tutorial-3-audio_video `pkg-config --cflags --libs gstreamer-1.0`
