# GStreamer 官方教程 basic-tutorial-3 的完整示例

程序原文地址: <https://gstreamer.freedesktop.org/documentation/tutorials/basic/dynamic-pipelines.html?gi-language=c>

如何在 C 语言中动态链接解码器输出的 pad（管道连接点），从而实现播放任意 URI（如网络媒体文件）的完整流程。

这个程序：
- 使用 GStreamer 播放一个在线视频（sintel_trailer-480p.webm）
- 展示如何在运行时动态地连接管道中的元件（uridecodebin 会根据媒体内容自动产生 pad）
- 监听 GStreamer 消息总线（Bus），输出状态、错误等信息

## 📦 模块结构概览
### 数据结构定义

```C
typedef struct _CustomData {
  GstElement *pipeline;
  GstElement *source;
  GstElement *convert;
  GstElement *resample;
  GstElement *sink;
} CustomData;

```

这个结构体保存了整个 pipeline 的核心元素句柄，方便在回调函数中访问。

## 🧩 代码结构拆解
### 1️⃣ 初始化 GStreamer

```C
gst_init(&argc, &argv);
```

初始化 GStreamer 库（必须在使用前调用）

### 2️⃣ 创建 GStreamer 元件

```C
data.source = gst_element_factory_make("uridecodebin", "source");
data.convert = gst_element_factory_make("audioconvert", "convert");
data.resample = gst_element_factory_make("audioresample", "resample");
data.sink = gst_element_factory_make("autoaudiosink", "sink");
```

| 元件              | 功能                             |
| --------------- | ------------------------------ |
| `uridecodebin`  | 自动从 URI 解码媒体流（视频或音频），输出相应的 pad |
| `audioconvert`  | 转换音频格式（float/int、通道数等）         |
| `audioresample` | 改变采样率                          |
| `autoaudiosink` | 自动选择系统可用的音频输出设备                |

### 3️⃣ 创建 pipeline 并添加元素

```C
data.pipeline = gst_pipeline_new("test-pipeline");
gst_bin_add_many(GST_BIN(data.pipeline), data.source, data.convert,
                 data.resample, data.sink, NULL);
```

`pipeline` 是 GStreamer 的核心容器，内部是一个 “bin”，可放多个元素。

### 4️⃣ 链接静态部分（非动态部分）

```C
gst_element_link_many(data.convert, data.resample, data.sink, NULL);
```

由于 `uridecodebin` 的输出 pad 是运行时动态生成的，所以这里只能先链接后面的部分。

### 5️⃣ 设置播放源 URI

```C
g_object_set(data.source, "uri",
    "https://gstreamer.freedesktop.org/data/media/sintel_trailer-480p.webm", NULL);
```

`uridecodebin` 会根据 URI 自动探测文件类型并加载相应的解码器。

### 6️⃣ 动态 pad 连接信号

```C
g_signal_connect(data.source, "pad-added", G_CALLBACK(pad_added_handler), &data);
```

当 `uridecodebin` 发现新的流（比如音频流）时，会触发 `pad-added` 信号。

程序在回调函数中决定是否将该 pad 链接到管道的下游（convert）。

### 7️⃣ 设置 pipeline 为播放状态

```C
ret = gst_element_set_state(data.pipeline, GST_STATE_PLAYING);
```

启动整个播放管道。

GStreamer 会自动开始拉取数据、解码、输出音频。

### 8️⃣ 监听消息总线 (Bus)

```C
bus = gst_element_get_bus(data.pipeline);
do {
  msg = gst_bus_timed_pop_filtered(bus, GST_CLOCK_TIME_NONE,
       GST_MESSAGE_STATE_CHANGED | GST_MESSAGE_ERROR | GST_MESSAGE_EOS);
```

Bus 用来传递 pipeline 的异步消息，如：
- 状态变化 (STATE_CHANGED)
- 出错 (ERROR)
- 流结束 (EOS)

### 9️⃣ 消息处理

```C
if (msg != NULL) {
    GError *err;
    gchar *debug_info;

    switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_ERROR:
        gst_message_parse_error (msg, &err, &debug_info);
        g_printerr ("Error received from element %s: %s\n",
            GST_OBJECT_NAME (msg->src), err->message);
        g_printerr ("Debugging information: %s\n",
            debug_info ? debug_info : "none");
        g_clear_error (&err);
        g_free (debug_info);
        terminate = TRUE;
        break;
    case GST_MESSAGE_EOS:
        g_print ("End-Of-Stream reached.\n");
        terminate = TRUE;
        break;
    case GST_MESSAGE_STATE_CHANGED:
        /* We are only interested in state-changed messages from the pipeline */
        if (GST_MESSAGE_SRC (msg) == GST_OBJECT (data.pipeline)) {
        GstState old_state, new_state, pending_state;
        gst_message_parse_state_changed (msg, &old_state, &new_state,
            &pending_state);
        g_print ("Pipeline state changed from %s to %s:\n",
            gst_state_get_name (old_state), gst_state_get_name (new_state));
        }
        break;
    default:
        /* We should not reach here */
        g_printerr ("Unexpected message received.\n");
        break;
    }
    gst_message_unref (msg);
}
```

当 `pipeline` 状态改变时，打印当前状态（例如：`READY` -> `PLAYING`）

### 🔟 清理资源

```C
gst_element_set_state(data.pipeline, GST_STATE_NULL);
gst_object_unref(data.pipeline);
gst_object_unref(bus);
```

释放所有 GStreamer 资源。

### 🧠 pad_added_handler 回调函数

核心逻辑：**动态链接 `uridecodebin` 输出的 pad**

```C
pad_added_handler (GstElement * src, GstPad * new_pad, CustomData * data)
```

**步骤解析：**
1. 获取 convert 的 sink pad
2. 判断是否已链接
3. 检查 pad 类型是否为 audio/x-raw
4. 尝试连接 new_pad → convert:sink
5. 释放引用

```C
if (!g_str_has_prefix(new_pad_type, "audio/x-raw")) {
    g_print("It has type '%s' which is not raw audio. Ignoring.\n", new_pad_type);
    return;
}
gst_pad_link(new_pad, sink_pad);
```

### main() 的特殊处理

```C
#if defined(__APPLE__) && TARGET_OS_MAC && !TARGET_OS_IPHONE
  return gst_macos_main ((GstMainFunc) tutorial_main, argc, argv, NULL);
#else
  return tutorial_main (argc, argv);
#endif
```

## 🧠 程序执行总流程图

```mathematica
   URI
    ↓
 uridecodebin
    ↓ (pad-added)
 audioconvert → audioresample → autoaudiosink
```

动态 pad 链接是这里的重点：只有当 uridecodebin 确定媒体类型（音频或视频）时才会创建输出 pad，然后程序动态链接下游元件。

## 🚀 程序运行效果

```bash
# 编译
gcc basic-tutorial-3.c -o basic-tutorial-3 `pkg-config --cflags --libs gstreamer-1.0`

# 运行
./basic-tutorial-3
```

```bash
# 运行结果提示
$ ./basic-tutorial-3 
xcb_connection_has_error() returned true
xcb_connection_has_error() returned true
Pipeline state changed from NULL to READY:
Received new pad 'src_0' from 'source':
It has type 'video/x-raw' which is not raw audio. Ignoring.
Received new pad 'src_1' from 'source':
Link succeeded (type 'audio/x-raw').
Pipeline state changed from READY to PAUSED:
Pipeline state changed from PAUSED to PLAYING:
End-Of-Stream reached.
```

## 注意事项

程序开头多加了一段, 在低版本没有对应API的时候用于通过编译(另外添加了只播放视频的版本、音频和视频一起播放的版本)

生成dot文件需要在终端先执行`export GST_DEBUG_DUMP_DOT_DIR=.`确定文件生成位置(只有一个文件有加dot输出情况)

用 dot 命令将 DOT 文件转换为 PNG/PDF（Graphviz 工具）：

```bash
# 转换为 PNG
dot -Tpng -o pipeline.png pipeline.dot

# 转换为 PDF
dot -Tpdf -o pipeline.pdf pipeline.dot
```

```C
#ifndef GST_STATE_GET_NAME
#define gst_state_get_name(state) \
  ((state) == GST_STATE_VOID_PENDING ? "VOID_PENDING" : \
  (state) == GST_STATE_NULL ? "NULL" : \
  (state) == GST_STATE_READY ? "READY" : \
  (state) == GST_STATE_PAUSED ? "PAUSED" : \
  (state) == GST_STATE_PLAYING ? "PLAYING" : "UNKNOWN")
#endif
```

这样可以保证 在所有版本都能编译运行。
