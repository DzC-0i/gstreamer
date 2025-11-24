# GStreamer 官方教程 playback-tutorial-3 的完整示例

原文地址: <https://gstreamer.freedesktop.org/documentation/tutorials/playback/short-cutting-the-pipeline.html?gi-language=c>

这段代码是基于 **GStreamer** 的 **实时音频生成与播放** 示例，核心功能是：通过 `appsrc` 元素动态生成音频波形数据（无需外部音频文件），并通过 `playbin` 完成解码和播放。它展示了 GStreamer 中 `appsrc`（应用层数据源）的核心用法——允许应用程序手动推送自定义数据到 GStreamer 管道，适用于实时音频/视频生成、自定义数据源（如网络流、内存数据）等场景。

### 一、核心功能概述

1. **动态音频生成**：通过数学公式生成“迷幻波形”音频（16位整型、44.1kHz采样率、单声道）。
2. **`appsrc` 集成 `playbin`**：`playbin` 是 GStreamer 的“全能播放器”，这里通过 `uri=appsrc://` 让 `playbin` 自动创建 `appsrc` 作为数据源，避免手动构建复杂管道。
3. **按需数据推送**：`appsrc` 会根据自身缓存状态发送 `need-data`/`enough-data` 信号，程序通过 idle  handler 按需推送音频数据，避免数据溢出或不足。
4. **错误处理与主循环**：通过总线（Bus）监听错误消息，用 GLib 主循环驱动事件（数据生成、信号处理）。


### 二、关键结构体与宏定义
#### 1. 宏定义（音频参数）

```c
#define CHUNK_SIZE 1024   /* 每次推送的字节数（1024字节 = 512个16位采样点） */
#define SAMPLE_RATE 44100 /* 采样率（每秒44100个采样点，标准音频采样率） */
```

- 注：音频格式为 `GST_AUDIO_FORMAT_S16`（16位有符号整型），每个采样点占 2 字节，因此 `CHUNK_SIZE=1024` 对应 `1024/2=512` 个采样点。

#### 2. `CustomData` 结构体（全局状态存储）

```c
typedef struct _CustomData {
    GstElement *pipeline;  /* 核心管道（playbin） */
    GstElement *app_source;/* 应用层数据源（appsrc，由 playbin 自动创建） */

    guint64 num_samples;   /* 已生成的总采样点数（用于计算缓冲区时间戳） */
    gfloat a, b, c, d;     /* 波形生成的数学参数 */

    guint sourceid;        /* idle handler 的 ID（用于控制数据推送开关） */
    GMainLoop *main_loop;  /* GLib 主循环（驱动事件） */
} CustomData;
```


### 三、核心流程与关键函数解析

整个程序的核心逻辑是：`playbin` 创建 `appsrc` → 配置 `appsrc` 音频格式 → 按需推送生成的音频数据 → 播放。

#### 1. 主函数（`main`）：初始化与管道启动

主函数是程序入口，负责初始化、创建管道、注册回调，流程如下：

##### （1）初始化与数据清零

```c
memset(&data, 0, sizeof(data));  /* 初始化 CustomData 结构体（所有字段置0） */
data.b = 1; /* 波形生成的初始参数（控制波形形状） */
data.d = 1;
gst_init(&argc, &argv); /* 初始化 GStreamer 库 */
```

##### （2）创建 `playbin` 管道（关键：`appsrc://` URI）

```c
// 创建 playbin，URI 设为 "appsrc://" → 告诉 playbin 用 appsrc 作为数据源
data.pipeline = gst_parse_launch("playbin uri=appsrc://", NULL);
// 注册 "source-setup" 信号回调：playbin 创建 appsrc 后，调用 source_setup 配置 appsrc
g_signal_connect(data.pipeline, "source-setup", G_CALLBACK(source_setup), &data);
```
- 关键：`playbin` 支持多种 URI 协议（如 `file://` 本地文件、`https://` 网络流），`appsrc://` 是特殊协议，用于让 `playbin` 自动创建 `appsrc` 元素作为输入源，简化管道构建。

##### （3）总线监听与错误处理

```c
GstBus *bus = gst_element_get_bus(data.pipeline);
gst_bus_add_signal_watch(bus);  /* 让总线对收到的消息触发信号 */
// 注册错误消息回调：收到错误时调用 error_cb 打印信息并退出
g_signal_connect(G_OBJECT(bus), "message::error", (GCallback)error_cb, &data);
gst_object_unref(bus);
```

##### （4）启动播放与主循环

```c
gst_element_set_state(data.pipeline, GST_STATE_PLAYING);  /* 启动管道 */
data.main_loop = g_main_loop_new(NULL, FALSE);  /* 创建主循环 */
g_main_loop_run(data.main_loop);  /* 启动主循环（阻塞，直到 quit） */
```

##### （5）资源释放

```c
gst_element_set_state(data.pipeline, GST_STATE_NULL);  /* 停止管道，释放资源 */
gst_object_unref(data.pipeline);  /* 释放 playbin */
```

#### 2. `source_setup`：配置 `appsrc`（关键回调）

当 `playbin` 成功创建 `appsrc` 后，会触发 `source-setup` 信号，调用此函数配置 `appsrc` 的核心参数：
```c
static void source_setup(GstElement *pipeline, GstElement *source, CustomData *data) {
    GstAudioInfo info;  /* 音频格式信息结构体 */
    GstCaps *audio_caps;/* 音频功能描述（格式、采样率、声道数） */

    data->app_source = source;  /* 保存 appsrc 指针到全局数据 */

    // 1. 配置音频格式：16位有符号整型（S16）、44.1kHz采样率、单声道（1声道）
    gst_audio_info_set_format(&info, GST_AUDIO_FORMAT_S16, SAMPLE_RATE, 1, NULL);
    // 2. 将音频格式转换为 GstCaps（GStreamer 中用于描述数据格式的结构体）
    audio_caps = gst_audio_info_to_caps(&info);
    // 3. 配置 appsrc：设置数据格式（caps）、时间戳格式（GST_FORMAT_TIME）
    g_object_set(source, "caps", audio_caps, "format", GST_FORMAT_TIME, NULL);
    // 4. 注册 appsrc 的关键信号：
    g_signal_connect(source, "need-data", G_CALLBACK(start_feed), data);  /* 需要数据时触发 */
    g_signal_connect(source, "enough-data", G_CALLBACK(stop_feed), data);/* 数据足够时触发 */
    // 5. 释放 caps（不再需要）
    gst_caps_unref(audio_caps);
}
```

- 核心作用：`appsrc` 是“应用层控制的数据源”，必须明确告诉它要推送的数据格式（如音频的采样率、位深、声道数），否则 `playbin` 无法解码播放。

#### 3. `start_feed` / `stop_feed`：控制数据推送

这两个函数是 `appsrc` 信号的回调，用于开启/关闭数据推送：

##### （1）`start_feed`：开启数据推送

当 `appsrc` 的内部缓存不足时，会发送 `need-data` 信号，调用此函数：

```c
static void start_feed(GstElement *source, guint size, CustomData *data) {
    if (data->sourceid == 0) {  /* 避免重复添加 idle handler */
        g_print("Start feeding\n");
        // 添加 idle handler：主循环空闲时调用 push_data 推送数据
        data->sourceid = g_idle_add((GSourceFunc)push_data, data);
    }
}
```

- 关键：`g_idle_add` 是 GLib 函数，用于在主循环“空闲时”反复调用 `push_data`（直到被移除），确保数据持续推送且不阻塞主循环。

##### （2）`stop_feed`：停止数据推送

当 `appsrc` 的内部缓存足够时，会发送 `enough-data` 信号，调用此函数：

```c
static void stop_feed(GstElement *source, CustomData *data) {
    if (data->sourceid != 0) {  /* 存在 idle handler 时才移除 */
        g_print("Stop feeding\n");
        g_source_remove(data->sourceid);  /* 移除 idle handler，停止推送 */
        data->sourceid = 0;
    }
}
```

- 核心作用：避免数据推送过快导致缓存溢出，实现“按需推送”，平衡 CPU 占用和播放流畅度。

#### 4. `push_data`：生成音频数据并推送（核心逻辑）

这是 idle handler 的回调函数，每次被调用时生成一段音频数据并推送到 `appsrc`：

```c
static gboolean push_data(CustomData *data) {
    GstBuffer *buffer;    /* GStreamer 数据缓冲区（存储音频数据） */
    GstFlowReturn ret;    /* 推送结果（成功/失败） */
    int i;
    GstMapInfo map;       /* 用于映射缓冲区数据到内存（方便写入） */
    gint16 *raw;          /* 指向音频原始数据（16位整型） */
    gint num_samples = CHUNK_SIZE / 2;  /* 本次推送的采样点数（1024字节 / 2字节/采样点 = 512） */
    gfloat freq;          /* 波形频率（动态变化，实现“迷幻”效果） */

    // 1. 创建一个大小为 CHUNK_SIZE 的空缓冲区
    buffer = gst_buffer_new_and_alloc(CHUNK_SIZE);

    // 2. 设置缓冲区的时间戳和时长（关键：GStreamer 依赖时间戳同步播放）
    GST_BUFFER_TIMESTAMP(buffer) = gst_util_uint64_scale(data->num_samples, GST_SECOND, SAMPLE_RATE);
    GST_BUFFER_DURATION(buffer) = gst_util_uint64_scale(num_samples, GST_SECOND, SAMPLE_RATE);
    // 解释：
    // - 时间戳 = 总采样点数 * 1秒 / 采样率 → 对应音频在时间轴上的位置
    // - 时长 = 本次采样点数 * 1秒 / 采样率 → 缓冲区数据的播放时长

    // 3. 映射缓冲区到内存（可写模式），获取原始数据指针
    gst_buffer_map(buffer, &map, GST_MAP_WRITE);
    raw = (gint16 *)map.data;  /* map.data 是缓冲区的原始字节数据，转为 16位整型指针 */

    // 4. 生成“迷幻波形”音频数据（核心数学逻辑）
    data->c += data->d;
    data->d -= data->c / 1000;
    freq = 1100 + 1000 * data->d;  /* 动态变化的频率（1100Hz ~ 2100Hz 左右） */
    for (i = 0; i < num_samples; i++) {
        data->a += data->b;
        data->b -= data->a / freq;  /* 基于频率的反馈公式，生成非线性波形 */
        raw[i] = (gint16)(500 * data->a);  /* 缩放幅度（避免溢出 16位整型范围） */
    }

    // 5. 解除缓冲区映射（必须调用，否则内存泄漏）
    gst_buffer_unmap(buffer, &map);
    // 6. 更新总采样点数
    data->num_samples += num_samples;

    // 7. 推送缓冲区到 appsrc（通过信号发射）
    g_signal_emit_by_name(data->app_source, "push-buffer", buffer, &ret);

    // 8. 释放缓冲区（推送后应用层不再需要）
    gst_buffer_unref(buffer);

    // 9. 检查推送结果：失败则停止推送
    if (ret != GST_FLOW_OK) {
        return FALSE;
    }

    return TRUE;  /* 返回 TRUE 表示继续调用 idle handler（持续推送） */
}
```

- 波形生成逻辑：通过简单的数学反馈公式（`a += b; b -= a/freq`）生成非线性波形，配合动态变化的频率（`freq`），实现“迷幻”音效（类似正弦波的变体，但更复杂）。
- 关键注意：16位整型的取值范围是 `-32768 ~ 32767`，因此代码中用 `500 * data->a` 缩放幅度，避免数据溢出导致的失真。

#### 5. `error_cb`：错误处理

当管道发生错误（如解码失败、设备不可用）时，总线会发送 `message::error` 信号，调用此函数：

```c
static void error_cb(GstBus *bus, GstMessage *msg, CustomData *data) {
    GError *err;
    gchar *debug_info;
    // 解析错误信息
    gst_message_parse_error(msg, &err, &debug_info);
    g_printerr("Error from %s: %s\n", GST_OBJECT_NAME(msg->src), err->message);
    g_printerr("Debug info: %s\n", debug_info ? debug_info : "none");
    // 释放错误资源
    g_clear_error(&err);
    g_free(debug_info);
    // 退出主循环（程序终止）
    g_main_loop_quit(data->main_loop);
}
```


### 四、关键 GStreamer 概念总结
#### 1. `appsrc` 核心特性

- `appsrc` 是“应用层数据源”，允许应用程序手动生成/推送数据到 GStreamer 管道，适用于：
  - 实时音频/视频生成（如本示例的波形生成）；
  - 自定义数据源（如网络流、内存中的音频数据、硬件采集数据）；
  - 需精确控制数据推送时机的场景。
- 必须配置的核心参数：
  - `caps`：数据格式描述（如音频的采样率、位深、声道数）；
  - `format`：时间戳格式（通常设为 `GST_FORMAT_TIME`，即时间戳以纳秒为单位）。
- 关键信号：
  - `need-data`：缓存不足，需要推送数据；
  - `enough-data`：缓存足够，停止推送；
  - `push-buffer`：应用程序推送数据的信号。

#### 2. `playbin` 与 `appsrc://` 协议

- `playbin` 是 GStreamer 的“高级封装元素”，内置了解封装、解码、渲染等完整流程，无需手动拼接管道（如 `src → decoder → sink`）。
- `appsrc://` 是 `playbin` 支持的特殊 URI，用于让 `playbin` 自动创建 `appsrc` 作为输入源，简化“应用层生成数据 + 播放”的场景。

#### 3. 缓冲区（`GstBuffer`）与时间戳

- `GstBuffer` 是 GStreamer 中存储媒体数据的核心结构体，包含：
  - 原始数据（如音频字节）；
  - 时间戳（`GST_BUFFER_TIMESTAMP`）：数据在时间轴上的位置（用于同步播放）；
  - 时长（`GST_BUFFER_DURATION`）：数据的播放时长。
- 时间戳计算：`gst_util_uint64_scale` 是 GStreamer 工具函数，用于将“采样点数”转换为“时间（纳秒）”，公式为：`时间 = 采样点数 * 1秒 / 采样率`。

#### 4. GLib 主循环与 idle handler

- `GMainLoop`：GLib 的主循环，是 GStreamer 应用的事件驱动核心，负责处理信号、回调、IO 事件等。
- `g_idle_add`：添加一个“空闲时执行”的 handler，主循环没有其他事件时会反复调用该 handler，适用于：
  - 持续生成数据（如本示例的音频推送）；
  - 非阻塞的后台任务。


### 五、编译与运行说明
#### 1. 编译命令

```bash
gcc playback-tutorial-3.c -o playback-tutorial-3 `pkg-config --cflags --libs gstreamer-1.0 gstreamer-audio-1.0`
```

- 依赖库说明：
  - `gstreamer-1.0`：核心库；
  - `gstreamer-audio-1.0`：音频相关结构体（如 `GstAudioInfo`）；
  - `glib-2.0`：GLib 主循环。

#### 2. 运行效果

- 执行程序后，会听到持续的“迷幻”波形音效（类似电子音）；
- 终端会打印 `Start feeding`（开始推送数据），若 `appsrc` 缓存足够，会打印 `Stop feeding`（暂停推送），之后根据缓存状态反复切换；
- 按 `Ctrl+C` 可终止程序。


### 六、常见问题排查
#### 1. 没有声音

- 检查是否安装了音频输出插件（如 `autoaudiosink`）：`gst-inspect-1.0 autoaudiosink`；
- 验证音频格式配置：确保 `GST_AUDIO_FORMAT_S16`、`SAMPLE_RATE=44100`、单声道配置正确；
- 检查系统音量是否正常。

#### 2. 程序崩溃或报错

- 检查 `appsrc` 的 `caps` 是否正确：若格式不匹配，`playbin` 会解码失败；
- 确保 `gst_buffer_map` 后调用 `gst_buffer_unmap`：否则会导致内存泄漏或崩溃；
- 验证 `push_data` 中 `raw[i]` 的取值范围：避免 16位整型溢出（如 `500 * data->a` 不要超过 `32767`）。
