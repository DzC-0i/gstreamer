# GStreamer 官方教程 basic-tutorial-5 的完整示例

程序原文地址: <https://gstreamer.freedesktop.org/documentation/tutorials/basic/toolkit-integration.html?gi-language=c#goal>

将学习：
- 如何指示 GStreamer 将视频输出到特定窗口（而不是创建自己的窗口）。
- 如何使用 GStreamer 中的信息持续刷新 GUI。
- 如何从 GStreamer 的多个线程更新 GUI，这是一个在大多数 GUI 工具包中禁止的操作。
- 一种机制，让你**只订阅感兴趣的消息**，而不是被通知所有消息。

这个示例展示了如何使用 GStreamer 与 GTK 结合，构建一个简单的 带 GUI 的媒体播放器。
它具备以下功能：
- 播放 / 暂停 / 停止 控制
- 播放进度条（拖动支持跳转）
- 视频嵌入 GTK 窗口
- 自动解析媒体流（视频 / 音频 / 字幕）并显示信息

## 🧩 代码结构拆解
### 🧩 一、主要结构体

```C
typedef struct _CustomData {
  GstElement *playbin;           // 播放管道（包含完整的音视频解码逻辑）
  GtkWidget *sink_widget;        // 视频输出窗口（GTK 小部件）
  GtkWidget *slider;             // 进度条
  GtkWidget *streams_list;       // 显示流信息的文本区
  gulong slider_update_signal_id;// 滑块信号 ID（防止死循环）
  GstState state;                // 当前播放状态
  gint64 duration;               // 媒体总时长（纳秒）
} CustomData;
```

playbin 是 GStreamer 提供的高级封装元素，它内部包含了解复用器、解码器、音频/视频 sink 等，适合快速实现播放器。

### 二、播放控制回调函数

```C
static void play_cb(...)  { gst_element_set_state(data->playbin, GST_STATE_PLAYING); }
static void pause_cb(...) { gst_element_set_state(data->playbin, GST_STATE_PAUSED); }
static void stop_cb(...)  { gst_element_set_state(data->playbin, GST_STATE_READY);  }
```

这些函数是按钮回调, 通过 `gst_element_set_state()` 设置 GStreamer 管道的状态：
- 点击 **Play** → pipeline 进入播放状态；
- 点击 **Pause** → 暂停；
- 点击 **Stop** → 停止（重置到 READY 状态）。

### 🪟 三、GTK 界面创建 (create_ui)

```C
static void create_ui (CustomData *data) {
  // 创建主窗口
  GtkWidget *main_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  g_signal_connect(main_window, "delete-event", G_CALLBACK(delete_event_cb), data);

  // 创建播放、暂停、停止按钮并绑定回调
  play_button = gtk_button_new_from_icon_name ("media-playback-start", GTK_ICON_SIZE_SMALL_TOOLBAR);
  g_signal_connect (G_OBJECT (play_button), "clicked", G_CALLBACK (play_cb), data);

  pause_button = gtk_button_new_from_icon_name ("media-playback-pause", GTK_ICON_SIZE_SMALL_TOOLBAR);
  g_signal_connect (G_OBJECT (pause_button), "clicked", G_CALLBACK (pause_cb), data);

  stop_button = gtk_button_new_from_icon_name ("media-playback-stop", GTK_ICON_SIZE_SMALL_TOOLBAR);
  g_signal_connect (G_OBJECT (stop_button), "clicked", G_CALLBACK (stop_cb), data);

  // 创建滑块（播放进度条）
  data->slider = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
  gtk_scale_set_draw_value (GTK_SCALE (data->slider), 0);
  g_signal_connect(data->slider, "value-changed", G_CALLBACK(slider_cb), data);

  // 流信息显示窗口
  data->streams_list = gtk_text_view_new();
  gtk_text_view_set_editable(GTK_TEXT_VIEW(data->streams_list), FALSE);

  // 组件UI布局
  controls = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_box_pack_start (GTK_BOX (controls), play_button, FALSE, FALSE, 2);
  gtk_box_pack_start (GTK_BOX (controls), pause_button, FALSE, FALSE, 2);
  gtk_box_pack_start (GTK_BOX (controls), stop_button, FALSE, FALSE, 2);
  gtk_box_pack_start (GTK_BOX (controls), data->slider, TRUE, TRUE, 2);

  // 创建视频显示区和流信息区
  main_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_box_pack_start(main_hbox, data->sink_widget, TRUE, TRUE, 0);
  gtk_box_pack_start(main_hbox, data->streams_list, FALSE, FALSE, 2);

  main_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  gtk_box_pack_start (GTK_BOX (main_box), main_hbox, TRUE, TRUE, 0);
  gtk_box_pack_start (GTK_BOX (main_box), controls, FALSE, FALSE, 0);

  // 整体布局
  gtk_container_add (GTK_CONTAINER (main_window), main_box);
  gtk_window_set_default_size (GTK_WINDOW (main_window), 640, 480);

  gtk_widget_show_all(main_window);
}
```

GTK 的 `sink_widget` 是从 `gtkglsink` 里取出的一个视频渲染控件，GStreamer 会把视频画面画到这个 widget 里。

### ⏳ 四、刷新界面 (`refresh_ui`)

```C
static gboolean refresh_ui (CustomData *data) {
  gst_element_query_position(...); // 当前播放位置
  gst_element_query_duration(...); // 媒体总时长
  gtk_range_set_value(data->slider, current / GST_SECOND); // 更新滑块位置
}
```

这函数每秒执行一次（通过 g_timeout_add_seconds 注册），动态刷新播放进度。

⚙️ 它在播放中会：
- 查询当前播放时间；
- 更新滑块；
- 避免滑块被更新时触发 seek 循环。

### 🏷 五、Tag 处理（元信息）

**主函数注册信息回调**

```C
g_signal_connect (G_OBJECT (data.playbin), "video-tags-changed", (GCallback) tags_cb, &data);
g_signal_connect (G_OBJECT (data.playbin), "audio-tags-changed", (GCallback) tags_cb, &data);
g_signal_connect (G_OBJECT (data.playbin), "text-tags-changed", (GCallback) tags_cb, &data);
```

**具体执行函数**

```C
static void tags_cb(GstElement *playbin, gint stream, CustomData *data) {
  gst_element_post_message(playbin,
    gst_message_new_application(GST_OBJECT(playbin),
      gst_structure_new_empty("tags-changed")));
}
```

当 GStreamer 发现音视频流中的元数据（如编码器、语言、比特率）时，会调用这个函数。
它通过 application message 通知主线程去更新 UI。

### ⚙️ 六、Bus 消息回调

Bus 是 GStreamer 的事件系统（错误、状态变化、EOS 等）。

```C
bus = gst_element_get_bus (data.playbin);
gst_bus_add_signal_watch (bus);
g_signal_connect (G_OBJECT (bus), "message::error", (GCallback)error_cb, &data);
g_signal_connect (G_OBJECT (bus), "message::eos", (GCallback)eos_cb, &data);
g_signal_connect (G_OBJECT (bus), "message::state-changed", (GCallback)state_changed_cb, &data);
g_signal_connect (G_OBJECT (bus), "message::application", (GCallback)application_cb, &data);
gst_object_unref (bus);
```

**错误回调**

```C
static void error_cb(GstBus *bus, GstMessage *msg, CustomData *data) {
  gst_message_parse_error(msg, &err, &debug_info);
  g_printerr("Error: %s\n", err->message);
  gst_element_set_state(data->playbin, GST_STATE_READY);
}
```

**EOS（播放完毕）**

```C
static void eos_cb(GstBus *bus, GstMessage *msg, CustomData *data) {
  g_print("End-Of-Stream reached.\n");
  gst_element_set_state(data->playbin, GST_STATE_READY);
}
```

**状态变化**

```C
static void state_changed_cb(GstBus *bus, GstMessage *msg, CustomData *data) {
  gst_message_parse_state_changed(msg, &old_state, &new_state, &pending_state);
  if (GST_MESSAGE_SRC(msg) == GST_OBJECT(data->playbin)) {
    data->state = new_state;
    g_print("State set to %s\n", gst_element_state_get_name(new_state));
  }
}
```

**应用层消息（Tag 更新）**

```C
static void application_cb(GstBus *bus, GstMessage *msg, CustomData *data) {
  if (strcmp(gst_structure_get_name(gst_message_get_structure(msg)), "tags-changed") == 0)
    analyze_streams(data);
}
```

### 📊 七、分析流信息 (**analyze_streams**)

```C
g_signal_emit_by_name(data->playbin, "get-video-tags", i, &tags);
gst_tag_list_get_string(tags, GST_TAG_VIDEO_CODEC, &str);
```

并把信息写入 `GtkTextView`（右边信息框）中。

### 🚀 八、主函数 main()

```C
int main(...) {
  gtk_init(...);             // 初始化 GTK
  gst_init(...);             // 初始化 GStreamer
  playbin = gst_element_factory_make("playbin", NULL);
  videosink = gst_element_factory_make("glsinkbin", NULL);
  gtkglsink = gst_element_factory_make("gtkglsink", NULL);

  // 尝试使用 gtkglsink，如果失败则回退到 gtksink
  if (gtkglsink) {
    g_object_set(videosink, "sink", gtkglsink, NULL);
    g_object_get(gtkglsink, "widget", &data.sink_widget, NULL);
  } else {
    videosink = gst_element_factory_make("gtksink", NULL);
    g_object_get(videosink, "widget", &data.sink_widget, NULL);
  }

  // 播放 URL
  g_object_set(playbin, "uri", "https://gstreamer.freedesktop.org/data/media/sintel_trailer-480p.webm", NULL);
  g_object_set(playbin, "video-sink", videosink, NULL);

  create_ui(&data);                      // 创建界面
  gst_bus_add_signal_watch(bus);         // 监听消息
  gst_element_set_state(playbin, GST_STATE_PLAYING); // 开始播放
  g_timeout_add_seconds(1, refresh_ui, &data);       // 定时刷新界面
  gtk_main();                            // 进入 GTK 主循环
}
```

## 🧠 程序执行总结和流程图

| 模块                        | 职责                  |
| ------------------------- | ------------------- |
| **GStreamer (`playbin`)** | 管理解码、播放、音视频输出。      |
| **GTK (`gtkglsink`)**     | 提供可嵌入的视频显示控件。       |
| **Bus 消息系统**              | 异步传递错误、状态、tag、结束事件。 |
| **UI 回调**                 | 用户交互（播放、暂停、拖动滑块）。   |
| **刷新定时器**                 | 每秒更新播放进度条。          |


```mathematica
         ┌──────────────────────────┐
         │        GTK UI            │
         │  ┌──────────────┐        │
         │  │ Video Widget │◄─────┐ │
         │  └──────────────┘      │ │
         │  [Play][Pause][Stop]   │ │
         │  ─────────────── Slider│ │
         │  Stream Info TextView  │ │
         └─────────▲──────────────┘ │
                   │ Bus Messages    │
                   ▼
         ┌──────────────────────────┐
         │       GStreamer          │
         │        Playbin           │
         │   (auto decode & play)   │
         └──────────────────────────┘

```

## 🚀 程序运行效果

```bash
# 编译
gcc basic-tutorial-5.c -o basic-tutorial-5 `pkg-config --cflags --libs gtk+-3.0 gstreamer-1.0`

# 运行
./basic-tutorial-5
```

出现一个UI界面并播放

## 注意事项

需要安装依赖库 `gtk+-3.0` 

```bash
sudo apt update
sudo apt install libgtk-3-dev
```

由于没有延迟管理（缓冲），所以在慢速连接上，可能在几秒后停止，或者出现播放失败的情况
