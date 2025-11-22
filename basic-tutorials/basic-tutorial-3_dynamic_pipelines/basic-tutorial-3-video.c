#include <gst/gst.h>

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
    GstElement *convert;
    GstElement *sink;
} CustomData;

/* pad-added 回调函数 */
static void pad_added_handler(GstElement *src, GstPad *new_pad, CustomData *data)
{
    GstPad *sink_pad = gst_element_get_static_pad(data->convert, "sink");
    GstPadLinkReturn ret;
    GstCaps *new_pad_caps = NULL;
    GstStructure *new_pad_struct = NULL;
    const gchar *new_pad_type = NULL;

    g_print("Received new pad '%s' from '%s'\n",
            GST_PAD_NAME(new_pad), GST_ELEMENT_NAME(src));

    if (gst_pad_is_linked(sink_pad))
    {
        g_print("We are already linked. Ignoring.\n");
        goto exit;
    }

    /* 获取 pad 的媒体类型 */
    new_pad_caps = gst_pad_get_current_caps(new_pad);
    new_pad_struct = gst_caps_get_structure(new_pad_caps, 0);
    new_pad_type = gst_structure_get_name(new_pad_struct);

    /* 只接受 video/x-raw 类型 */
    if (!g_str_has_prefix(new_pad_type, "video/x-raw"))
    {
        g_print("It has type '%s' which is not raw video. Ignoring.\n", new_pad_type);
        goto exit;
    }

    /* 链接 */
    ret = gst_pad_link(new_pad, sink_pad);
    if (GST_PAD_LINK_FAILED(ret))
    {
        g_print("Type is '%s' but link failed.\n", new_pad_type);
    }
    else
    {
        g_print("Link succeeded (type '%s').\n", new_pad_type);
    }

exit:
    if (new_pad_caps != NULL)
        gst_caps_unref(new_pad_caps);
    gst_object_unref(sink_pad);
}

int main(int argc, char *argv[])
{
    CustomData data;
    GstBus *bus;
    GstMessage *msg;
    GstStateChangeReturn ret;
    gboolean terminate = FALSE;

    gst_init(&argc, &argv);

    /* 创建元素 */
    data.source = gst_element_factory_make("uridecodebin", "source");
    data.convert = gst_element_factory_make("videoconvert", "convert");
    data.sink = gst_element_factory_make("autovideosink", "sink");
    data.pipeline = gst_pipeline_new("video-pipeline");

    if (!data.pipeline || !data.source || !data.convert || !data.sink)
    {
        g_printerr("Not all elements could be created.\n");
        return -1;
    }

    /* 添加到 pipeline */
    gst_bin_add_many(GST_BIN(data.pipeline), data.source, data.convert, data.sink, NULL);
    if (!gst_element_link(data.convert, data.sink))
    {
        g_printerr("Elements could not be linked.\n");
        gst_object_unref(data.pipeline);
        return -1;
    }

    /* 设置播放 URL */
    g_object_set(data.source, "uri",
                 "https://gstreamer.freedesktop.org/data/media/sintel_trailer-480p.webm", NULL);

    /* 绑定 pad-added 信号 */
    g_signal_connect(data.source, "pad-added", G_CALLBACK(pad_added_handler), &data);

    /* 设置播放状态 */
    ret = gst_element_set_state(data.pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE)
    {
        g_printerr("Unable to set the pipeline to the playing state.\n");
        gst_object_unref(data.pipeline);
        return -1;
    }

    /* 监听总线消息 */
    bus = gst_element_get_bus(data.pipeline);
    do
    {
        msg = gst_bus_timed_pop_filtered(bus, GST_CLOCK_TIME_NONE,
                                         GST_MESSAGE_STATE_CHANGED | GST_MESSAGE_ERROR | GST_MESSAGE_EOS);

        if (msg != NULL)
        {
            GError *err;
            gchar *debug_info;

            switch (GST_MESSAGE_TYPE(msg))
            {
            case GST_MESSAGE_ERROR:
                gst_message_parse_error(msg, &err, &debug_info);
                g_printerr("Error received from element %s: %s\n",
                           GST_OBJECT_NAME(msg->src), err->message);
                g_printerr("Debugging information: %s\n",
                           debug_info ? debug_info : "none");
                g_clear_error(&err);
                g_free(debug_info);
                terminate = TRUE;
                break;
            case GST_MESSAGE_EOS:
                g_print("End-Of-Stream reached.\n");
                terminate = TRUE;
                break;
            case GST_MESSAGE_STATE_CHANGED:
                if (GST_MESSAGE_SRC(msg) == GST_OBJECT(data.pipeline))
                {
                    GstState old_state, new_state, pending_state;
                    gst_message_parse_state_changed(msg, &old_state, &new_state, &pending_state);
                    g_print("Pipeline state changed from %s to %s:\n",
                            gst_state_get_name(old_state),
                            gst_state_get_name(new_state));
                }
                break;
            default:
                g_printerr("Unexpected message received.\n");
                break;
            }
            gst_message_unref(msg);
        }
    } while (!terminate);

    gst_object_unref(bus);
    gst_element_set_state(data.pipeline, GST_STATE_NULL);
    gst_object_unref(data.pipeline);
    return 0;
}

// gcc basic-tutorial-3-video.c -o basic-tutorial-3-video `pkg-config --cflags --libs gstreamer-1.0`
