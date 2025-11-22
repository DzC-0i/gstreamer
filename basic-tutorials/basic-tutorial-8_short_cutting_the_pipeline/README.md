# GStreamer 多分支音频处理管道代码解析

程序原文地址: <https://gstreamer.freedesktop.org/documentation/tutorials/basic/short-cutting-the-pipeline.html?gi-language=c>

这是一个 **GStreamer 高级示例代码**，核心功能是：通过 `appsrc`（应用程序数据源）自定义生成音频数据，经 `tee` 元素分流为 **三路数据流**（音频播放、音频可视化、应用程序接收），完整演示了「自定义数据输入」「动态 Pad 分流」「多分支协同处理」等进阶用法，同时结合了 GLib 主循环实现异步事件驱动。

## 一、核心架构与数据流
### 1.1 管道整体架构

```mathematica
appsrc（自定义音频源）→ tee（分流器）→ 分支1（音频播放）→ audio_queue → audio_convert1 → audio_resample → autoaudiosink（扬声器）
                          ↓
                        分支2（视频可视化）→ video_queue → audio_convert2 → wavescope（波形转换）→ videoconvert → autovideosink（窗口）
                          ↓
                        分支3（应用接收）→ app_queue → appsink（应用程序获取数据）
```

### 1.2 关键元素功能说明
| 元素名          | 功能描述                                                                 |
|-----------------|--------------------------------------------------------------------------|
| `appsrc`        | 应用程序数据源：由用户代码生成音频数据并推送到管道（核心自定义输入组件）。 |
| `tee`           | 分流器：将一路输入数据复制为三路输出，需动态请求输出 Pad。                 |
| `queue`         | 数据队列：为每个分支提供独立缓存，避免不同分支处理速度不匹配导致阻塞。     |
| `audioconvert`  | 音频格式转换：统一音频采样格式/声道数，确保各分支元素兼容性（两个实例分别服务不同分支）。 |
| `audioresample` | 音频重采样：适配音频设备支持的采样率（如 44100Hz ↔ 48000Hz）。           |
| `autoaudiosink` | 自动音频输出：自动选择系统默认音频设备（扬声器/耳机）。                  |
| `wavescope`     | 音频可视化：将音频数据转换为波形视频（示波器风格）。                     |
| `videoconvert`  | 视频格式转换：适配视频设备的颜色空间（如 RGB ↔ YUV）。                   |
| `autovideosink` | 自动视频输出：弹出窗口显示可视化波形。                                    |
| `appsink`       | 应用程序数据接收端：将管道中的音频数据回调给应用程序（此处仅打印占位符）。 |
| `GMainLoop`     | GLib 主循环：处理异步事件（如 `appsrc` 的数据请求、错误消息）。            |

### 1.3 自定义数据结构 `CustomData`

用于存储管道所有组件和状态，方便在回调函数中共享访问：

```c
typedef struct _CustomData
{
    // 管道和所有元素
    GstElement *pipeline, *app_source, *tee, *audio_queue, *audio_convert1, *audio_resample, *audio_sink;
    GstElement *video_queue, *audio_convert2, *visual, *video_convert, *video_sink;
    GstElement *app_queue, *app_sink;

    guint64 num_samples; /* 已生成的音频样本数（用于时间戳计算） */
    gfloat a, b, c, d;   /* 波形生成的参数（用于自定义音频波形） */
    guint sourceid;      /* GLib 空闲处理器 ID（控制数据推送） */
    GMainLoop *main_loop;/* GLib 主循环（事件驱动核心） */
} CustomData;
```

## 二、核心流程与代码解析
### 2.1 初始化与元素创建
#### 步骤1：全局初始化

```c
// 初始化自定义数据结构（清零+波形参数初始化）
memset(&data, 0, sizeof(data));
data.b = 1; /* 波形生成初始参数 */
data.d = 1;

// 初始化 GStreamer
gst_init(&argc, &argv);
```

#### 步骤2：创建所有元素

通过 `gst_element_factory_make` 创建管道所需的 14 个元素（包括 3 个队列、2 个音频格式转换器、3 个输出组件等），并创建空管道：

```c
data.app_source = gst_element_factory_make("appsrc", "audio_source");
data.tee = gst_element_factory_make("tee", "tee");
// ... 省略其他元素创建 ...
data.pipeline = gst_pipeline_new("test-pipeline");
```

#### 步骤3：元素有效性检查

确保所有元素创建成功（避免空指针导致崩溃）：

```c
if (!data.pipeline || !data.app_source || !data.tee || ...) {
    g_printerr("Not all elements could be created.\n");
    return -1;
}
```

### 2.2 元素配置（关键自定义部分）
#### 配置1：`wavescope` 可视化参数

```c
g_object_set(data.visual, "shader", 0, "style", 0, NULL);
```

- `shader=0`：使用默认渲染器；
- `style=0`：波形显示为「填充模式」（`style=1` 为线条模式）。

#### 配置2：`appsrc` 自定义音频源（核心）

`appsrc` 是应用程序向管道推送数据的入口，需配置数据格式、回调函数：

```c
// 1. 定义音频格式：16位有符号整数（S16）、44100Hz 采样率、单声道
GstAudioInfo info;
GstCaps *audio_caps;
gst_audio_info_set_format(&info, GST_AUDIO_FORMAT_S16, SAMPLE_RATE, 1, NULL);
audio_caps = gst_audio_info_to_caps(&info);

// 2. 配置 appsrc：指定输出格式、时间戳模式
g_object_set(data.app_source, "caps", audio_caps, "format", GST_FORMAT_TIME, NULL);

// 3. 连接信号回调：数据请求/停止请求
g_signal_connect(data.app_source, "need-data", G_CALLBACK(start_feed), &data);
g_signal_connect(data.app_source, "enough-data", G_CALLBACK(stop_feed), &data);
```

- `need-data` 信号：当 `appsrc` 缓冲区为空时触发，通知应用程序推送数据；
- `enough-data` 信号：当 `appsrc` 缓冲区已满时触发，通知应用程序停止推送。

#### 配置3：`appsink` 应用数据接收

```c
// 1. 启用信号发射（接收新数据时触发回调）
// 2. 指定接收的数据格式（与 appsrc 输出格式一致）
g_object_set(data.app_sink, "emit-signals", TRUE, "caps", audio_caps, NULL);

// 3. 连接新数据回调：当 appsink 收到数据时调用 new_sample
g_signal_connect(data.app_sink, "new-sample", G_CALLBACK(new_sample), &data);

// 释放 Caps 资源（已配置给元素，无需保留）
gst_caps_unref(audio_caps);
```

### 2.3 元素添加与静态链接

将所有元素添加到管道，并链接「静态 Pad 元素」（Pad 为 `Always` 类型，始终存在）：

```c
// 批量添加元素到管道（GST_BIN 是 pipeline 的父类，需强制转换）
gst_bin_add_many(GST_BIN(data.pipeline), data.app_source, data.tee, ..., NULL);

// 分分支自动链接（仅支持静态 Pad 元素）
if (gst_element_link_many(data.app_source, data.tee, NULL) != TRUE ||  // 源 → tee（输入 Pad 静态）
    gst_element_link_many(data.audio_queue, audio_convert1, audio_resample, audio_sink, NULL) != TRUE ||  // 音频分支
    gst_element_link_many(data.video_queue, audio_convert2, visual, video_convert, video_sink, NULL) != TRUE ||  // 视频分支
    gst_element_link_many(data.app_queue, data.app_sink, NULL) != TRUE) {  // 应用分支
    g_printerr("Elements could not be linked.\n");
    gst_object_unref(data.pipeline);
    return -1;
}
```

- 无法自动链接 `tee` 的输出 Pad：因为 `tee` 的输出 Pad 是 `Request` 类型（按需创建），需手动请求并链接。

### 2.4 动态 Pad 请求与 `tee` 链接（核心难点）

`tee` 需为三个分支分别请求输出 Pad，再手动链接到对应队列的输入 Pad：

```c
/* 手动链接 tee 的动态输出 Pad */
// 1. 为三个分支分别请求 tee 的输出 Pad（模板名 "src_%u"，%u 自动编号）
tee_audio_pad = gst_element_request_pad_simple(data.tee, "src_%u");
tee_video_pad = gst_element_request_pad_simple(data.tee, "src_%u");
tee_app_pad = gst_element_request_pad_simple(data.tee, "src_%u");

// 2. 获取三个队列的静态输入 Pad（名称均为 "sink"）
queue_audio_pad = gst_element_get_static_pad(data.audio_queue, "sink");
queue_video_pad = gst_element_get_static_pad(data.video_queue, "sink");
queue_app_pad = gst_element_get_static_pad(data.app_queue, "sink");

// 3. 手动链接 tee 输出 Pad ↔ 队列输入 Pad
if (gst_pad_link(tee_audio_pad, queue_audio_pad) != GST_PAD_LINK_OK ||
    gst_pad_link(tee_video_pad, queue_video_pad) != GST_PAD_LINK_OK ||
    gst_pad_link(tee_app_pad, queue_app_pad) != GST_PAD_LINK_OK) {
    g_printerr("Tee could not be linked\n");
    gst_object_unref(data.pipeline);
    return -1;
}

// 4. 释放队列 Pad 引用（链接成功后无需单独持有）
gst_object_unref(queue_audio_pad);
gst_object_unref(queue_video_pad);
gst_object_unref(queue_app_pad);
```

- `gst_element_request_pad_simple`：简化版动态 Pad 请求函数（GStreamer 1.16+ 支持），`src_%u` 是 `tee` 的输出 Pad 模板名（`%u` 自动生成编号，如 `src_0`、`src_1`、`src_2`）；
- `gst_pad_link`：必须满足「SRC Pad → SINK Pad」方向（`tee` 的 Pad 是 SRC 类型，队列的 Pad 是 SINK 类型）。

### 2.5 回调函数解析（事件驱动核心）
#### 回调1：`push_data` —— 生成并推送音频数据

由 GLib 空闲处理器调用（`g_idle_add`），向 `appsrc` 推送自定义音频数据：

```c
static gboolean push_data(CustomData *data)
{
    GstBuffer *buffer;
    GstMapInfo map;
    gint16 *raw;
    gint num_samples = CHUNK_SIZE / 2; /* 每个样本 16 位（2字节），所以样本数 = 字节数/2 */

    // 1. 创建缓冲区（大小为 CHUNK_SIZE=1024 字节）
    buffer = gst_buffer_new_and_alloc(CHUNK_SIZE);

    // 2. 设置缓冲区时间戳和时长（确保音视频同步）
    GST_BUFFER_TIMESTAMP(buffer) = gst_util_uint64_scale(data->num_samples, GST_SECOND, SAMPLE_RATE);
    GST_BUFFER_DURATION(buffer) = gst_util_uint64_scale(num_samples, GST_SECOND, SAMPLE_RATE);

    // 3. 生成自定义音频波形（迷幻正弦波变体）
    gst_buffer_map(buffer, &map, GST_MAP_WRITE);  // 映射缓冲区到可写内存
    raw = (gint16 *)map.data;  // 缓冲区数据指针（16位整数格式）
    data->c += data->d;
    data->d -= data->c / 1000;
    gfloat freq = 1100 + 1000 * data->d;  // 动态频率（产生变化的音调）
    for (int i = 0; i < num_samples; i++) {
        data->a += data->b;
        data->b -= data->a / freq;
        raw[i] = (gint16)(500 * data->a);  // 音频样本值（控制音量）
    }
    gst_buffer_unmap(buffer, &map);  // 解除映射
    data->num_samples += num_samples;  // 更新已生成样本数

    // 4. 推送缓冲区到 appsrc
    GstFlowReturn ret;
    g_signal_emit_by_name(data->app_source, "push-buffer", buffer, &ret);

    // 5. 释放缓冲区（推送后应用程序不再持有）
    gst_buffer_unref(buffer);

    // 6. 若推送失败，停止数据生成
    return (ret == GST_FLOW_OK) ? TRUE : FALSE;
}
```

- 核心逻辑：生成动态变化的音频波形（频率随参数 `d` 变化），确保音频不是单调的正弦波；
- 时间戳设置：通过 `gst_util_uint64_scale` 将样本数转换为 GStreamer 时间戳（单位：纳秒），确保各分支数据同步。

#### 回调2：`start_feed` —— 启动数据推送

当 `appsrc` 触发 `need-data` 信号时调用，添加 GLib 空闲处理器（`push_data`）到主循环：

```c
static void start_feed(GstElement *source, guint size, CustomData *data)
{
    if (data->sourceid == 0) {
        g_print("Start feeding\n");
        // 添加空闲处理器：主循环空闲时调用 push_data 推送数据
        data->sourceid = g_idle_add((GSourceFunc)push_data, data);
    }
}
```

#### 回调3：`stop_feed` —— 停止数据推送

当 `appsrc` 触发 `enough-data` 信号时调用，移除空闲处理器：

```c
static void stop_feed(GstElement *source, CustomData *data)
{
    if (data->sourceid != 0) {
        g_print("Stop feeding\n");
        g_source_remove(data->sourceid);  // 移除空闲处理器
        data->sourceid = 0;
    }
}
```

#### 回调4：`new_sample` —— 应用接收数据

当 `appsink` 收到数据时触发，此处仅打印 `*` 表示数据接收成功：

```c
static GstFlowReturn new_sample(GstElement *sink, CustomData *data)
{
    GstSample *sample;
    // 从 appsink 拉取样本（包含缓冲区和 Caps）
    g_signal_emit_by_name(sink, "pull-sample", &sample);
    if (sample) {
        g_print("*");  // 打印占位符，表示成功接收数据
        gst_sample_unref(sample);  // 释放样本资源
        return GST_FLOW_OK;
    }
    return GST_FLOW_FLUSHING;
}
```

#### 回调5：`error_cb` —— 错误处理

当管道发生错误时（如元素链接失败、设备不可用），打印错误信息并退出主循环：

```c
static void error_cb(GstBus *bus, GstMessage *msg, CustomData *data)
{
    GError *err;
    gchar *debug_info;
    gst_message_parse_error(msg, &err, &debug_info);
    g_printerr("Error from %s: %s\n", GST_OBJECT_NAME(msg->src), err->message);
    g_printerr("Debug: %s\n", debug_info ? debug_info : "none");
    g_clear_error(&err);
    g_free(debug_info);
    g_main_loop_quit(data->main_loop);  // 退出主循环
}
```

### 2.6 管道启动与主循环运行

```c
// 1. 配置消息总线：监听错误消息
GstBus *bus = gst_element_get_bus(data.pipeline);
gst_bus_add_signal_watch(bus);
g_signal_connect(G_OBJECT(bus), "message::error", (GCallback)error_cb, &data);
gst_object_unref(bus);

// 2. 启动管道（设置为 PLAYING 状态）
gst_element_set_state(data.pipeline, GST_STATE_PLAYING);

// 3. 启动 GLib 主循环（阻塞，直到收到退出信号）
data.main_loop = g_main_loop_new(NULL, FALSE);
g_main_loop_run(data.main_loop);
```

- `g_main_loop_run`：阻塞当前线程，处理所有异步事件（`need-data`、`new-sample`、错误消息等），直到 `g_main_loop_quit` 被调用。

### 2.7 资源释放（避免内存泄漏）

```c
// 1. 释放 tee 的动态 Pad
gst_element_release_request_pad(data.tee, tee_audio_pad);
gst_element_release_request_pad(data.tee, tee_video_pad);
gst_element_release_request_pad(data.tee, tee_app_pad);
gst_object_unref(tee_audio_pad);
gst_object_unref(tee_video_pad);
gst_object_unref(tee_app_pad);

// 2. 停止管道并释放资源
gst_element_set_state(data.pipeline, GST_STATE_NULL);
gst_object_unref(data.pipeline);
```
- 动态 Pad 必须先通过 `gst_element_release_request_pad` 归还给 `tee`，再解引用；
- 管道设置为 `GST_STATE_NULL` 会停止所有元素，释放内部缓冲区和线程资源。


## 三、编译与运行

需安装 GStreamer 核心库、音频/视频插件，编译时指定依赖：

```bash
# 编译
gcc basic-tutorial-8.c -o basic-tutorial-8 `pkg-config --cflags --libs gstreamer-1.0 gstreamer-audio-1.0`

# 运行
./basic-tutorial-8
```

运行后会出现以下效果：
1. 终端输出：`Start feeding` + 持续打印 `*`（表示 `appsink` 成功接收数据）；
2. 扬声器播放动态变化的音频（迷幻波形，频率随时间变化）；
3. 弹出视频窗口，显示音频的填充式波形可视化（随音频实时变化）。

## 四、核心难点与关键技术
### 4.1 `appsrc` 自定义数据推送机制

- `appsrc` 是「拉模式」工作：管道需要数据时触发 `need-data` 信号，应用程序被动推送数据；
- 时间戳设置是关键：必须为每个缓冲区设置 `GST_BUFFER_TIMESTAMP` 和 `GST_BUFFER_DURATION`，否则会导致音视频同步问题或元素处理失败。

### 4.2 `tee` 动态 Pad 管理

- `tee` 的输出 Pad 是 `Request` 类型，需为每个分支单独请求，且请求后必须释放（`release_request_pad`），否则会内存泄漏；
- 多个分支通过 `queue` 隔离：每个分支有独立的缓存和线程，避免一个分支阻塞影响其他分支。

### 4.3 GLib 主循环的作用

- 主循环是事件驱动的核心：所有回调函数（`push_data`、`new_sample`、`error_cb`）都在主循环中执行；
- 空闲处理器（`g_idle_add`）：仅在主循环无其他事件时调用 `push_data`，避免数据推送占用过多 CPU。

### 4.4 格式一致性保证

- `appsrc`、`appsink`、`audioconvert` 等元素的 Caps 必须一致（16位、44100Hz、单声道），否则会导致链接失败或数据解析错误；
- 使用 `GstAudioInfo` 简化音频格式配置：避免手动构造 Caps 字符串，减少错误。

## 五、注意

1. **编译错误：`gst_element_request_pad_simple` 未定义**：
   - 原因：GStreamer 版本 < 1.16，或缺少 `gobject/gobject.h` 头文件；
   - 解决方案：升级 GStreamer 或替换为兼容 API `gst_element_request_pad`。 (注意: 在使用之前先需要通过 `gst_element_get_pad_template` 获取到Pad 模板)
