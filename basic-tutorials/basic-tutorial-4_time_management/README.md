# GStreamer 官方教程 basic-tutorial-4 的完整示例

程序原文地址: <https://gstreamer.freedesktop.org/documentation/tutorials/basic/time-management.html?gi-language=c>

使用 playbin 元件播放媒体、跟踪播放进度、查询时长、执行 seek（跳转）、以及处理播放状态与错误信息。

## 🧩 代码结构拆解
### 🧩 一、结构定义

```C
typedef struct _CustomData {
  GstElement *playbin;          /* 播放器元素（包含完整解码与输出） */
  gboolean playing;             /* 当前是否在播放 */
  gboolean terminate;           /* 是否需要退出循环 */
  gboolean seek_enabled;        /* 是否支持 seek（跳转） */
  gboolean seek_done;           /* 是否已经执行过 seek */
  gint64 duration;              /* 媒体总时长（纳秒） */
} CustomData;
```

`playbin` 是一个「一体化」的 GStreamer 元素，它内部自动构建完整的音视频解码管线。只需提供一个 URI，它会自动选择解码器和输出设备。

### 🧩 二、初始化与构建

```C
gst_init(&argc, &argv);
data.playbin = gst_element_factory_make("playbin", "playbin");
```

`gst_init` 初始化 GStreamer 库，`gst_element_factory_make` 创建 `playbin` 元素。  
这部分准备了播放所需的对象。

### 🧩 三、设置播放源

```C
g_object_set(data.playbin, "uri",
  "https://gstreamer.freedesktop.org/data/media/sintel_trailer-480p.webm", NULL);
```

这一步设置 playbin 的媒体 URI。可以是：
- 本地文件：`file:///home/user/video.mp4`
- 网络流：`https://...` 或 `rtsp://...`

### 🧩 四、启动播放

```C
ret = gst_element_set_state(data.playbin, GST_STATE_PLAYING);
```

将 `playbin` 设置为 **PLAYING** 状态，它会自动完成：
- 下载或打开文件
- 解码音视频流
- 输出到音频设备和视频窗口

### 🧩 五、消息总线循环（核心逻辑）

```C
bus = gst_element_get_bus(data.playbin);
do {
  msg = gst_bus_timed_pop_filtered(bus, 100 * GST_MSECOND,
        GST_MESSAGE_STATE_CHANGED | GST_MESSAGE_ERROR | GST_MESSAGE_EOS |
        GST_MESSAGE_DURATION);
  ...
} while (!data.terminate);
```

这是主循环。
`playbin` 会通过 **Bus（消息总线）** 发出事件，程序通过 `gst_bus_timed_pop_filtered` 监听：
- `GST_MESSAGE_ERROR`：出错
- `GST_MESSAGE_EOS`：播放结束
- `GST_MESSAGE_STATE_CHANGED`：状态切换（例如从 PAUSED → PLAYING）
- `GST_MESSAGE_DURATION`：总时长更新

所需超时时间必须作为 `GstClockTime` 指定，因此以纳秒为单位。表示不同时间单位的数字，应该乘以 `GST_SECOND` 或 `GST_MSECOND` 这样的宏。

### 🧩 六、进度显示 + Seek 逻辑

当 msg == NULL（超时）时，程序每 100ms 执行一次进度查询：

```C
gst_element_query_position(data.playbin, GST_FORMAT_TIME, &current);
gst_element_query_duration(data.playbin, GST_FORMAT_TIME, &data.duration);

g_print("Position %" GST_TIME_FORMAT " / %" GST_TIME_FORMAT "\r",
        GST_TIME_ARGS(current), GST_TIME_ARGS(data.duration));
```

查询当前播放时间与总时长，并在终端动态打印。

> 注意使用 GST_TIME_FORMAT 和 GST_TIME_ARGS 宏来提供用户友好的 GStreamer 时间表示。

`GST_FORMAT_TIME` 表示我们正在以时间单位指定目标。其他查找格式使用不同的单位。

`GstSeekFlags` ，最常见的形式：

`GST_SEEK_FLAG_FLUSH` : 在进行定位操作之前，此选项会丢弃管道中当前的所有数据。在管道重新填充且新数据开始显示时可能会短暂暂停，但会显著提高应用程序的“响应性”。如果未提供此标志，可能会在一段时间内显示“过时”数据，直到新位置出现在管道的末尾。

`GST_SEEK_FLAG_KEY_UNIT` : 对于大多数编码视频流，无法定位到任意位置，只能定位到称为关键帧的特定帧。当使用此标志时，定位操作实际上会移动到最近的关键帧并立即开始产生数据。如果未使用此标志，管道将内部移动到最近的关键帧（它没有其他选择），但数据不会显示，直到它到达请求的位置。这种最后的选择更准确，但可能需要更长时间。

`GST_SEEK_FLAG_ACCURATE` : 一些媒体片段没有提供足够的索引信息，这意味着随机定位会耗费时间。在这些情况下，GStreamer 通常会估计要定位的位置，并且通常工作得很好。如果这个精度对你的情况不够（你看到定位没有到达你要求的确切时间），那么请提供这个标志。请注意，计算定位位置可能会花费更长的时间（在某些文件上非常长）。

最后，我们提供要查找的位置。由于我们请求了 `GST_FORMAT_TIME` ，值必须是纳秒，因此我们简化为以秒表示时间，然后乘以 `GST_SECOND` 宏来对应值 。

### 🧩 七、执行 Seek（跳转）

```C
if (data.seek_enabled && !data.seek_done && current > 10 * GST_SECOND) {
  g_print("\nReached 10s, performing seek...\n");
  gst_element_seek_simple(data.playbin, GST_FORMAT_TIME,
      GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT, 30 * GST_SECOND);
  data.seek_done = TRUE;
}
```

意思是：

> 当播放到第 10 秒时，如果支持跳转，就跳到第 30 秒。

`GST_SEEK_FLAG_FLUSH` 表示清空旧的缓冲，`GST_SEEK_FLAG_KEY_UNIT` 表示从关键帧开始。

### 🧩 八、处理消息函数 handle_message

**错误处理**

```C
case GST_MESSAGE_ERROR:
  gst_message_parse_error(msg, &err, &debug_info);
  g_printerr("Error received from element %s: %s\n", GST_OBJECT_NAME(msg->src), err->message);
  ...
  data->terminate = TRUE;
  break;
```

→ 解析错误信息并退出。

**播放结束**

```C
case GST_MESSAGE_EOS:
  g_print("\nEnd-Of-Stream reached.\n");
  data->terminate = TRUE;
  break;
```

→ 文件播放到尾部。

**时长变化**

```C
case GST_MESSAGE_DURATION:
  data->duration = GST_CLOCK_TIME_NONE;
  break;
```

→ 媒体时长改变，需要重新查询。

**状态变化**

```C
case GST_MESSAGE_STATE_CHANGED:
  gst_message_parse_state_changed(msg, &old_state, &new_state, &pending_state);
  g_print("Pipeline state changed from %s to %s:\n",
          gst_state_get_name(old_state), gst_state_get_name(new_state));
```

当 pipeline 状态变化时打印如： Pipeline state changed from PAUSED to PLAYING:

当进入 **`PLAYING`** 时，进一步查询是否支持 seek：

```C
GstQuery *query = gst_query_new_seeking(GST_FORMAT_TIME);
if (gst_element_query(data->playbin, query)) {
  gst_query_parse_seeking(query, NULL, &data->seek_enabled, &start, &end);
}
```

### 🧩 九、资源释放

```C
gst_object_unref(bus);
gst_element_set_state(data.playbin, GST_STATE_NULL);
gst_object_unref(data.playbin);
```

## 🧠 程序执行总流程图

```mathematica
初始化 GStreamer
     ↓
创建 playbin 元素
     ↓
设置播放 URI
     ↓
设置状态为 PLAYING
     ↓
循环读取消息
 ├── 错误 → 退出
 ├── 结束 → 退出
 ├── 状态变化 → 打印状态、检测 seek
 ├── 无消息 → 查询播放进度、在 10s 时 seek
     ↓
释放资源退出

```

## 🚀 程序运行效果

```bash
# 编译
gcc basic-tutorial-4.c -o basic-tutorial-4 `pkg-config --cflags --libs gstreamer-1.0`

# 运行
./basic-tutorial-4
```

运行10秒后跳转到30秒附近
