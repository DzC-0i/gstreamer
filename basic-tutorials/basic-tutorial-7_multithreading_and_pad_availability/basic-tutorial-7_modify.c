#include <gst/gst.h>

int main(int argc, char *argv[])
{
    GstElement *pipeline, *audio_source, *tee, *audio_queue, *audio_convert, *audio_resample, *audio_sink;
    GstElement *video_queue, *visual, *video_convert, *video_sink;
    GstBus *bus;
    GstMessage *msg;
    GstPad *tee_audio_pad, *tee_video_pad;
    GstPad *queue_audio_pad, *queue_video_pad;

    GstPadTemplate *pad_template = NULL; // 初始化Pad模板指针为 NULL

    /* Initialize GStreamer */
    gst_init(&argc, &argv);

    /* Create the elements */
    audio_source = gst_element_factory_make("audiotestsrc", "audio_source");
    tee = gst_element_factory_make("tee", "tee");
    audio_queue = gst_element_factory_make("queue", "audio_queue");
    audio_convert = gst_element_factory_make("audioconvert", "audio_convert");
    audio_resample = gst_element_factory_make("audioresample", "audio_resample");
    audio_sink = gst_element_factory_make("autoaudiosink", "audio_sink");
    video_queue = gst_element_factory_make("queue", "video_queue");
    visual = gst_element_factory_make("wavescope", "visual");
    video_convert = gst_element_factory_make("videoconvert", "csp");
    video_sink = gst_element_factory_make("autovideosink", "video_sink");

    /* Create the empty pipeline */
    pipeline = gst_pipeline_new("test-pipeline");

    if (!pipeline || !audio_source || !tee || !audio_queue || !audio_convert || !audio_resample || !audio_sink ||
        !video_queue || !visual || !video_convert || !video_sink)
    {
        g_printerr("Not all elements could be created.\n");
        return -1;
    }

    /* Configure elements */
    g_object_set(audio_source, "freq", 215.0f, NULL);
    g_object_set(visual, "shader", 0, "style", 1, NULL);

    /* Link all elements that can be automatically linked because they have "Always" pads */
    gst_bin_add_many(GST_BIN(pipeline), audio_source, tee, audio_queue, audio_convert, audio_resample, audio_sink,
                     video_queue, visual, video_convert, video_sink, NULL);
    if (gst_element_link_many(audio_source, tee, NULL) != TRUE ||
        gst_element_link_many(audio_queue, audio_convert, audio_resample, audio_sink, NULL) != TRUE ||
        gst_element_link_many(video_queue, visual, video_convert, video_sink, NULL) != TRUE)
    {
        g_printerr("Elements could not be linked.\n");
        gst_object_unref(pipeline);
        return -1;
    }

    /* Manually link the Tee, which has "Request" pads */
    // 使用兼容的 API 函数
    // 1. 获取 tee 元素的静态 Pad 模板（名称为 "src_%u"）
    pad_template = gst_element_get_pad_template(tee, "src_%u");
    if (!pad_template)
    {
        g_printerr("Failed to get tee pad template 'src_%u'\n", "src_%u");
        gst_object_unref(pipeline);
        return -1;
    }

    // 2. 用模板请求音频分支的 Pad
    tee_audio_pad = gst_element_request_pad(tee, pad_template, NULL, NULL);
    if (!tee_audio_pad)
    {
        g_printerr("Failed to request audio pad from tee\n");
        gst_object_unref(pad_template); // 释放Pad模板
        gst_object_unref(pipeline);
        return -1;
    }
    g_print("Obtained request pad %s for audio branch.\n", gst_pad_get_name(tee_audio_pad));

    // 3. 用同一个模板请求视频分支的 Pad（tee 会自动生成新的编号，如 src_0、src_1）
    tee_video_pad = gst_element_request_pad(tee, pad_template, NULL, NULL);
    if (!tee_video_pad)
    {
        g_printerr("Failed to request video pad from tee\n");
        gst_element_release_request_pad(tee, tee_audio_pad); // 释放已请求的音频Pad
        gst_object_unref(tee_audio_pad);
        gst_object_unref(pad_template); // 释放Pad模板
        gst_object_unref(pipeline);
        return -1;
    }
    g_print("Obtained request pad %s for video branch.\n", gst_pad_get_name(tee_video_pad));

    queue_audio_pad = gst_element_get_static_pad(audio_queue, "sink");
    if (!queue_audio_pad)
    {
        g_printerr("Failed to get sink pad of audio_queue\n");
        goto cleanup; // 跳转到统一清理逻辑
    }

    queue_video_pad = gst_element_get_static_pad(video_queue, "sink");
    if (!queue_video_pad)
    {
        g_printerr("Failed to get sink pad of video_queue\n");
        goto cleanup; // 跳转到统一清理逻辑
    }

    if (gst_pad_link(tee_audio_pad, queue_audio_pad) != GST_PAD_LINK_OK ||
        gst_pad_link(tee_video_pad, queue_video_pad) != GST_PAD_LINK_OK)
    {
        g_printerr("Tee could not be linked.\n");
        goto cleanup; // 优化4：链接失败时，通过统一清理逻辑释放所有资源
    }

    gst_object_unref(queue_audio_pad);
    queue_audio_pad = NULL; // 置 NULL，避免重复释放
    gst_object_unref(queue_video_pad);
    queue_video_pad = NULL; // 置 NULL，避免重复释放

    // Pad 链接成功后，释放 PadTemplate（已无需使用）
    gst_object_unref(pad_template);
    pad_template = NULL; // 置 NULL，避免重复释放

    /* Start playing the pipeline */
    gst_element_set_state(pipeline, GST_STATE_PLAYING);

    /* Wait until error or EOS */
    bus = gst_element_get_bus(pipeline);
    msg = gst_bus_timed_pop_filtered(bus, GST_CLOCK_TIME_NONE, GST_MESSAGE_ERROR | GST_MESSAGE_EOS);

    /* Free resources */
    if (msg != NULL)
        gst_message_unref(msg);
    gst_object_unref(bus);
    gst_element_set_state(pipeline, GST_STATE_NULL);

cleanup:
    // 释放队列 Pad（若未释放）
    if (queue_audio_pad)
        gst_object_unref(queue_audio_pad);
    if (queue_video_pad)
        gst_object_unref(queue_video_pad);

    // 释放 tee 的动态 Pad（若已请求）
    if (tee_audio_pad)
    {
        gst_element_release_request_pad(tee, tee_audio_pad);
        gst_object_unref(tee_audio_pad);
    }
    if (tee_video_pad)
    {
        gst_element_release_request_pad(tee, tee_video_pad);
        gst_object_unref(tee_video_pad);
    }

    // 释放 PadTemplate（若已获取）
    if (pad_template)
        gst_object_unref(pad_template);

    gst_object_unref(pipeline);
    return 0;
}

// gcc basic-tutorial-7_modify.c -o basic-tutorial-7_modify `pkg-config --cflags --libs gstreamer-1.0`
