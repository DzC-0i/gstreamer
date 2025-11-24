#include <gst/gst.h>
#include <gst/video/video.h>
#include <string.h>
#include <math.h>

#define WIDTH 640
#define HEIGHT 480
#define FRAMERATE 30
#define CHUNK_SIZE (WIDTH * HEIGHT * 4)  /* RGBA format, 4 bytes per pixel */

/* Structure to contain all our information */
typedef struct _CustomData {
    GstElement *pipeline;
    GstElement *app_source;
    GstElement *video_convert;
    GstElement *video_sink;

    guint64 num_frames;      /* Number of frames generated so far */
    gint pattern_type;       /* Pattern type for animation */
    guint sourceid;          /* To control the GSource */

    GMainLoop *main_loop;    /* GLib's Main Loop */
} CustomData;

/* Generate a simple animated pattern */
static void generate_frame(guint8 *data, guint64 frame_count, gint pattern_type) {
    int x, y;
    guint8 r, g, b;

    for (y = 0; y < HEIGHT; y++) {
        for (x = 0; x < WIDTH; x++) {
            int offset = (y * WIDTH + x) * 4;

            switch (pattern_type) {
                case 0:  /* Moving color bars */
                    r = (x + frame_count) % 256;
                    g = (y + frame_count * 2) % 256;
                    b = (x + y + frame_count * 3) % 256;
                    break;

                case 1:  /* Circular pattern */
                    {
                        int center_x = WIDTH / 2;
                        int center_y = HEIGHT / 2;
                        int dx = x - center_x;
                        int dy = y - center_y;
                        float dist = sqrt(dx * dx + dy * dy);
                        r = (sin(dist * 0.1 + frame_count * 0.1) * 127 + 128);
                        g = (sin(dist * 0.1 + frame_count * 0.2) * 127 + 128);
                        b = (sin(dist * 0.1 + frame_count * 0.3) * 127 + 128);
                    }
                    break;

                case 2:  /* Plasma effect */
                    r = 128 + 127 * sin(x * 0.1 + frame_count * 0.1);
                    g = 128 + 127 * sin(y * 0.1 + frame_count * 0.2);
                    b = 128 + 127 * sin((x + y) * 0.1 + frame_count * 0.3);
                    break;

                default:  /* Solid color with moving line */
                    r = (x + frame_count) % 256;
                    g = (y) % 256;
                    b = 128;
                    break;
            }

            data[offset] = r;     /* Red */
            data[offset + 1] = g; /* Green */
            data[offset + 2] = b; /* Blue */
            data[offset + 3] = 255; /* Alpha (fully opaque) */
        }
    }
}

/* This method is called by the idle GSource to push video frames */
static gboolean push_data(CustomData *data) {
    GstBuffer *buffer;
    GstFlowReturn ret;
    GstMapInfo map;
    guint8 *raw_data;

    /* Create a new buffer */
    buffer = gst_buffer_new_and_alloc(CHUNK_SIZE);

    /* Set timestamp and duration */
    GST_BUFFER_TIMESTAMP(buffer) = gst_util_uint64_scale(data->num_frames, GST_SECOND, FRAMERATE);
    GST_BUFFER_DURATION(buffer) = gst_util_uint64_scale(1, GST_SECOND, FRAMERATE);

    /* Generate video frame */
    gst_buffer_map(buffer, &map, GST_MAP_WRITE);
    raw_data = (guint8 *)map.data;

    generate_frame(raw_data, data->num_frames, data->pattern_type);

    gst_buffer_unmap(buffer, &map);
    data->num_frames++;

    /* Change pattern every 100 frames for variety */
    if (data->num_frames % 100 == 0) {
        data->pattern_type = (data->pattern_type + 1) % 3;
    }

    /* Push the buffer */
    g_signal_emit_by_name(data->app_source, "push-buffer", buffer, &ret);
    gst_buffer_unref(buffer);

    if (ret != GST_FLOW_OK) {
        g_print("Push buffer returned %d, stopping\n", ret);
        return FALSE;
    }

    return TRUE;
}

/* This signal callback triggers when appsrc needs data */
static void start_feed(GstElement *source, guint size, CustomData *data) {
    if (data->sourceid == 0) {
        g_print("Start feeding video data\n");
        data->sourceid = g_idle_add((GSourceFunc)push_data, data);
    }
}

/* This callback triggers when appsrc has enough data */
static void stop_feed(GstElement *source, CustomData *data) {
    if (data->sourceid != 0) {
        g_print("Stop feeding video data\n");
        g_source_remove(data->sourceid);
        data->sourceid = 0;
    }
}

/* Error callback */
static void error_cb(GstBus *bus, GstMessage *msg, CustomData *data) {
    GError *err;
    gchar *debug_info;

    gst_message_parse_error(msg, &err, &debug_info);
    g_printerr("Error received from element %s: %s\n", GST_OBJECT_NAME(msg->src), err->message);
    g_printerr("Debugging information: %s\n", debug_info ? debug_info : "none");
    g_clear_error(&err);
    g_free(debug_info);

    g_main_loop_quit(data->main_loop);
}

/* This function is called when playbin has created the appsrc element */
static void source_setup(GstElement *pipeline, GstElement *source, CustomData *data) {
    GstCaps *video_caps;

    g_print("Video source has been created. Configuring.\n");
    data->app_source = source;

    /* Configure video caps for appsrc */
    video_caps = gst_caps_new_simple("video/x-raw",
        "format", G_TYPE_STRING, "RGBA",
        "width", G_TYPE_INT, WIDTH,
        "height", G_TYPE_INT, HEIGHT,
        "framerate", GST_TYPE_FRACTION, FRAMERATE, 1,
        NULL);

    g_object_set(source,
        "caps", video_caps,
        "format", GST_FORMAT_TIME,
        "block", TRUE,
        NULL);

    g_signal_connect(source, "need-data", G_CALLBACK(start_feed), data);
    g_signal_connect(source, "enough-data", G_CALLBACK(stop_feed), data);
    gst_caps_unref(video_caps);
}

/* EOS callback */
static void eos_cb(GstBus *bus, GstMessage *msg, CustomData *data) {
    g_print("End-of-stream reached\n");
    g_main_loop_quit(data->main_loop);
}

int main(int argc, char *argv[]) {
    CustomData data;
    GstBus *bus;

    /* Initialize custom data structure */
    memset(&data, 0, sizeof(data));
    data.pattern_type = 0;

    /* Initialize GStreamer */
    gst_init(&argc, &argv);

    /* Create a custom pipeline for video */
    data.pipeline = gst_parse_launch(
        "appsrc name=video_source ! "
        "videoconvert ! "
        "autovideosink sync=false",  // sync=false for real-time generation
        NULL);

    /* Get the appsrc element */
    data.app_source = gst_bin_get_by_name(GST_BIN(data.pipeline), "video_source");

    /* Configure appsrc */
    GstCaps *video_caps = gst_caps_new_simple("video/x-raw",
        "format", G_TYPE_STRING, "RGBA",
        "width", G_TYPE_INT, WIDTH,
        "height", G_TYPE_INT, HEIGHT,
        "framerate", GST_TYPE_FRACTION, FRAMERATE, 1,
        NULL);

    g_object_set(data.app_source,
        "caps", video_caps,
        "format", GST_FORMAT_TIME,
        "block", TRUE,
        NULL);

    g_signal_connect(data.app_source, "need-data", G_CALLBACK(start_feed), &data);
    g_signal_connect(data.app_source, "enough-data", G_CALLBACK(stop_feed), &data);
    gst_caps_unref(video_caps);

    /* Set up bus monitoring */
    bus = gst_element_get_bus(data.pipeline);
    gst_bus_add_signal_watch(bus);
    g_signal_connect(G_OBJECT(bus), "message::error", (GCallback)error_cb, &data);
    g_signal_connect(G_OBJECT(bus), "message::eos", (GCallback)eos_cb, &data);
    gst_object_unref(bus);

    /* Start playing */
    gst_element_set_state(data.pipeline, GST_STATE_PLAYING);

    /* Create and run main loop */
    data.main_loop = g_main_loop_new(NULL, FALSE);
    g_print("Video generation started. Press Ctrl+C to stop.\n");
    g_main_loop_run(data.main_loop);

    /* Cleanup */
    gst_element_set_state(data.pipeline, GST_STATE_NULL);
    if (data.app_source) {
        gst_object_unref(data.app_source);
    }
    gst_object_unref(data.pipeline);
    g_main_loop_unref(data.main_loop);

    return 0;
}

//gcc playback-tutorial-3_video.c -o playback-tutorial-3_video -lm `pkg-config --cflags --libs gstreamer-1.0`
