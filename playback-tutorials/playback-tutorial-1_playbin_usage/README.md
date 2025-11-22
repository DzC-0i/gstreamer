# GStreamer 官方教程 playback-tutorial-1 的完整示例

原文地址: <https://gstreamer.freedesktop.org/documentation/tutorials/playback/playbin-usage.html?gi-language=c>

这段代码是 **GStreamer 多媒体播放的进阶示例**，核心功能是：使用 `playbin`（全能播放组件）播放网络视频（多音轨 WebM 文件），支持解析音视频/字幕流信息、切换音频轨道，同时通过 GLib 主循环处理消息和键盘输入。以下分模块详细解释：

在看代码前，先明确 3 个关键组件：
1. **`playbin`**：GStreamer 内置的「全能播放器」，无需手动拼接 `filesrc`/`decodebin`/`sink` 等元素，自动处理「文件读取→解复用→解码→渲染」全流程；
2. **GLib Main Loop**：GLib 库的主事件循环，用于监听 GStreamer 消息（如播放状态、错误）和键盘输入；
3. **GStreamer 消息总线（Bus）**：GStreamer 组件间通信的通道，程序通过监听总线消息获取播放状态（如开始、结束、错误）。

## 二、代码结构与逐段解释
### 1. 头文件与数据结构定义

```c
#include <gst/gst.h>
#include <stdio.h>

/* 自定义数据结构：存储所有需要跨函数共享的信息 */
typedef struct _CustomData
{
    GstElement *playbin; /* 核心播放组件 playbin */

    gint n_video; /* 嵌入的视频流数量 */
    gint n_audio; /* 嵌入的音频流数量（多音轨） */
    gint n_text;  /* 嵌入的字幕流数量 */

    gint current_video; /* 当前播放的视频流索引 */
    gint current_audio; /* 当前播放的音频流索引 */
    gint current_text;  /* 当前播放的字幕流索引 */

    GMainLoop *main_loop; /* GLib 主循环（驱动程序运行） */
} CustomData;

/* playbin 功能标志：控制启用哪些输出（视频/音频/字幕） */
typedef enum
{
    GST_PLAY_FLAG_VIDEO = (1 << 0), /* 启用视频输出 */
    GST_PLAY_FLAG_AUDIO = (1 << 1), /* 启用音频输出 */
    GST_PLAY_FLAG_TEXT = (1 << 2)   /* 启用字幕输出 */
} GstPlayFlags;

/* 函数声明：消息处理、键盘输入处理（跨函数调用需提前声明） */
static gboolean handle_message(GstBus *bus, GstMessage *msg, CustomData *data);
static gboolean handle_keyboard(GIOChannel *source, GIOCondition cond, CustomData *data);
```

- **`CustomData` 结构体**：统一管理所有全局状态（播放组件、流信息、主循环），避免使用全局变量，方便函数间传递数据；
- **`GstPlayFlags` 枚举**：用位运算控制 `playbin` 的功能（比如只启用音视频，禁用字幕）。


### 2. 主函数（程序入口）

主函数是程序的「控制中心」，负责初始化、组件创建、参数配置、启动播放和主循环。

```c
int main(int argc, char *argv[])
{
    CustomData data; // 实例化自定义数据结构
    GstBus *bus;     // 消息总线（监听 GStreamer 消息）
    GstStateChangeReturn ret; // 状态变更返回值（判断是否启动成功）
    gint flags;      // playbin 功能标志
    GIOChannel *io_stdin; // 标准输入通道（监听键盘输入）

    /* 1. 初始化 GStreamer：必须是第一个 GStreamer 函数，解析命令行参数 */
    gst_init(&argc, &argv);

    /* 2. 创建 playbin 组件：参数1=组件类型，参数2=组件名称（调试用） */
    data.playbin = gst_element_factory_make("playbin", "playbin");
    if (!data.playbin) // 检查组件创建是否成功（失败可能是缺少插件）
    {
        g_printerr("Not all elements could be created.\n");
        return -1;
    }

    /* 3. 设置播放 URI：支持网络地址（http/https）或本地文件（file:///路径） */
    g_object_set(data.playbin, "uri", "https://gstreamer.freedesktop.org/data/media/sintel_cropped_multilingual.webm", NULL);

    /* 4. 配置 playbin 功能：启用音视频，禁用字幕 */
    g_object_get(data.playbin, "flags", &flags, NULL); // 读取当前 flags
    flags |= GST_PLAY_FLAG_VIDEO | GST_PLAY_FLAG_AUDIO; // 启用音视频（位或运算）
    flags &= ~GST_PLAY_FLAG_TEXT; // 禁用字幕（位与非运算）
    g_object_set(data.playbin, "flags", flags, NULL); // 写入配置

    /* 5. 设置网络连接速度（单位：kbps）：影响 playbin 的缓冲策略（比如低速网络多缓冲） */
    g_object_set(data.playbin, "connection-speed", 56, NULL);

    /* 6. 配置消息总线：监听 playbin 的消息（错误、播放结束、状态变更等） */
    bus = gst_element_get_bus(data.playbin); // 获取 playbin 的消息总线
    // 给总线添加「消息监听器」：收到消息时调用 handle_message 函数
    gst_bus_add_watch(bus, (GstBusFunc)handle_message, &data);

    /* 7. 配置键盘输入监听：监听标准输入（stdin），支持用户按键交互 */
#ifdef G_OS_WIN32 // Windows 系统特殊处理
    io_stdin = g_io_channel_win32_new_fd(fileno(stdin));
#else // Linux/macOS 系统
    io_stdin = g_io_channel_unix_new(fileno(stdin));
#endif
    // 给标准输入添加「监听器」：有输入时调用 handle_keyboard 函数
    g_io_add_watch(io_stdin, G_IO_IN, (GIOFunc)handle_keyboard, &data);

    /* 8. 启动播放：将 playbin 状态设置为 GST_STATE_PLAYING */
    ret = gst_element_set_state(data.playbin, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) // 检查启动是否成功（比如 URI 无效、插件缺失）
    {
        g_printerr("Unable to set the pipeline to the playing state.\n");
        gst_object_unref(data.playbin); // 释放资源
        return -1;
    }

    /* 9. 启动 GLib 主循环：程序进入「阻塞状态」，等待消息/输入触发 */
    data.main_loop = g_main_loop_new(NULL, FALSE); // 创建主循环
    g_main_loop_run(data.main_loop); // 运行主循环（直到调用 g_main_loop_quit 才退出）

    /* 10. 程序退出：释放所有资源（避免内存泄漏） */
    g_main_loop_unref(data.main_loop); // 释放主循环
    g_io_channel_unref(io_stdin);     // 释放标准输入通道
    gst_object_unref(bus);            // 释放消息总线
    gst_element_set_state(data.playbin, GST_STATE_NULL); // 停止播放，释放流资源
    gst_object_unref(data.playbin);   // 释放 playbin 组件
    return 0;
}
```

主函数的核心流程：**初始化 → 创建组件 → 配置参数 → 启动播放 → 进入主循环 → 退出释放资源**。

### 3. 流信息解析函数 `analyze_streams`

当播放状态切换到「PLAYING」时，自动调用该函数，解析并打印音视频/字幕流的元信息（如编码格式、语言、比特率）。

```c
static void analyze_streams(CustomData *data)
{
    gint i;
    GstTagList *tags; // 存储流的元信息（标签列表）
    gchar *str;       // 临时字符串（存储标签值）
    guint rate;       // 临时变量（存储比特率）

    /* 1. 读取流的数量：从 playbin 中获取视频/音频/字幕流的个数 */
    g_object_get(data->playbin, "n-video", &data->n_video, NULL);
    g_object_get(data->playbin, "n-audio", &data->n_audio, NULL);
    g_object_get(data->playbin, "n-text", &data->n_text, NULL);

    /* 2. 打印流的数量汇总 */
    g_print("%d video stream(s), %d audio stream(s), %d text stream(s)\n",
            data->n_video, data->n_audio, data->n_text);

    /* 3. 解析并打印每个视频流的信息 */
    g_print("\n");
    for (i = 0; i < data->n_video; i++)
    {
        tags = NULL;
        // 发送信号给 playbin，获取第 i 个视频流的标签（元信息）
        g_signal_emit_by_name(data->playbin, "get-video-tags", i, &tags);
        if (tags) // 如果有标签信息
        {
            g_print("video stream %d:\n", i);
            // 从标签中获取「视频编码格式」并打印
            gst_tag_list_get_string(tags, GST_TAG_VIDEO_CODEC, &str);
            g_print("  codec: %s\n", str ? str : "unknown");
            g_free(str); // 释放字符串（GLib 函数返回的字符串需手动释放）
            gst_tag_list_free(tags); // 释放标签列表
        }
    }

    /* 4. 解析并打印每个音频流的信息（编码、语言、比特率） */
    g_print("\n");
    for (i = 0; i < data->n_audio; i++)
    {
        tags = NULL;
        g_signal_emit_by_name(data->playbin, "get-audio-tags", i, &tags);
        if (tags)
        {
            g_print("audio stream %d:\n", i);
            if (gst_tag_list_get_string(tags, GST_TAG_AUDIO_CODEC, &str))
            {
                g_print("  codec: %s\n", str);
                g_free(str);
            }
            if (gst_tag_list_get_string(tags, GST_TAG_LANGUAGE_CODE, &str))
            {
                g_print("  language: %s\n", str); // 比如 "en"（英语）、"fr"（法语）
                g_free(str);
            }
            if (gst_tag_list_get_uint(tags, GST_TAG_BITRATE, &rate))
            {
                g_print("  bitrate: %d\n", rate); // 比特率（单位：bps）
            }
            gst_tag_list_free(tags);
        }
    }

    /* 5. 解析并打印每个字幕流的信息（语言） */
    g_print("\n");
    for (i = 0; i < data->n_text; i++)
    {
        tags = NULL;
        g_signal_emit_by_name(data->playbin, "get-text-tags", i, &tags);
        if (tags)
        {
            g_print("subtitle stream %d:\n", i);
            if (gst_tag_list_get_string(tags, GST_TAG_LANGUAGE_CODE, &str))
            {
                g_print("  language: %s\n", str);
                g_free(str);
            }
            gst_tag_list_free(tags);
        }
    }

    /* 6. 获取并打印当前正在播放的流索引 */
    g_object_get(data->playbin, "current-video", &data->current_video, NULL);
    g_object_get(data->playbin, "current-audio", &data->current_audio, NULL);
    g_object_get(data->playbin, "current-text", &data->current_text, NULL);

    g_print("\n");
    g_print("Currently playing video stream %d, audio stream %d and text stream %d\n",
            data->current_video, data->current_audio, data->current_text);
    g_print("Type any number and hit ENTER to select a different audio stream\n");
}
```

核心逻辑：通过 `g_object_get` 获取流数量，通过 `g_signal_emit_by_name` 触发 `playbin` 的「获取标签」信号，解析并打印元信息。


### 4. 消息处理函数 `handle_message`

监听 GStreamer 消息总线的所有消息，根据消息类型（错误、播放结束、状态变更）做对应处理。

```c
static gboolean handle_message(GstBus *bus, GstMessage *msg, CustomData *data)
{
    GError *err;
    gchar *debug_info;

    /* 切换消息类型 */
    switch (GST_MESSAGE_TYPE(msg))
    {
    case GST_MESSAGE_ERROR: // 错误消息（比如网络错误、解码失败）
        // 解析错误信息和调试信息
        gst_message_parse_error(msg, &err, &debug_info);
        g_printerr("Error received from element %s: %s\n", GST_OBJECT_NAME(msg->src), err->message);
        g_printerr("Debugging information: %s\n", debug_info ? debug_info : "none");
        // 释放错误资源
        g_clear_error(&err);
        g_free(debug_info);
        g_main_loop_quit(data->main_loop); // 退出主循环（程序终止）
        break;

    case GST_MESSAGE_EOS: // 播放结束消息（End-Of-Stream）
        g_print("End-Of-Stream reached.\n");
        g_main_loop_quit(data->main_loop); // 退出主循环
        break;

    case GST_MESSAGE_STATE_CHANGED: // 状态变更消息（比如从 STOPPED → PLAYING）
    {
        GstState old_state, new_state, pending_state;
        // 解析状态变更信息（旧状态、新状态、待处理状态）
        gst_message_parse_state_changed(msg, &old_state, &new_state, &pending_state);
        // 只处理 playbin 自身的状态变更（忽略子组件的状态变更）
        if (GST_MESSAGE_SRC(msg) == GST_OBJECT(data->playbin))
        {
            if (new_state == GST_STATE_PLAYING)
            {
                /* 当 playbin 进入 PLAYING 状态时，解析流信息 */
                analyze_streams(data);
            }
        }
    }
    break;
    }

    /* 返回 TRUE：继续监听后续消息；返回 FALSE：停止监听 */
    return TRUE;
}
```

核心作用：**错误处理、播放结束处理、状态变更触发流解析**，是程序的「消息中枢」。


### 5. 键盘输入处理函数 `handle_keyboard`

监听用户的键盘输入，支持输入音频流索引并切换音频轨道（比如输入 `1` 切换到第 1 个音频流）。

```c
static gboolean handle_keyboard(GIOChannel *source, GIOCondition cond, CustomData *data)
{
    gchar *str = NULL; // 存储用户输入的字符串

    /* 读取用户输入的一行文本（直到按下 ENTER） */
    if (g_io_channel_read_line(source, &str, NULL, NULL, NULL) == G_IO_STATUS_NORMAL)
    {
        // 将输入的字符串转换为整数（音频流索引）
        int index = g_ascii_strtoull(str, NULL, 0);
        // 检查索引是否合法（0 ≤ index < 音频流总数）
        if (index < 0 || index >= data->n_audio)
        {
            g_printerr("Index out of bounds\n");
        }
        else
        {
            /* 合法索引：设置当前播放的音频流 */
            g_print("Setting current audio stream to %d\n", index);
            g_object_set(data->playbin, "current-audio", index, NULL);
        }
    }
    g_free(str); // 释放输入字符串
    return TRUE; // 继续监听后续输入
}
```

核心逻辑：读取用户输入 → 转换为索引 → 验证合法性 → 调用 `g_object_set` 切换音频流。


## 三、程序运行流程总结

1. 启动程序 → 初始化 GStreamer → 创建 `playbin` 并配置 URI；
2. 启动播放 → `playbin` 状态切换为 `PLAYING`；
3. 消息总线收到「状态变更消息」→ 调用 `analyze_streams` 打印流信息；
4. 程序进入主循环，阻塞等待：
   - 收到「错误消息」→ 打印错误并退出；
   - 收到「播放结束消息」→ 打印提示并退出；
   - 收到「键盘输入」→ 切换音频流；
5. 退出程序 → 释放所有资源。

## 四、核心亮点与用途

1. **简化播放逻辑**：用 `playbin` 替代手动拼接管道，适合快速实现音视频播放；
2. **多音轨支持**：解析并切换音频流，适合多语言视频（比如电影的中英文字幕/音频）；
3. **交互性**：支持键盘输入，提升用户体验；
4. **消息驱动**：通过消息总线处理状态和错误，符合 GStreamer 的设计理念。


## 五、运行说明

1. 依赖：需要安装 GStreamer 核心库、网络插件（支持 https）、WebM 解码插件；
2. 运行效果：
   - 启动后自动播放网络视频；
   - 打印音视频/字幕流信息（比如 1 个视频流、3 个音频流）；
   - 输入音频流索引（如 `2`）并按回车，切换到对应音频轨道；
3. 退出：播放结束或按 `Ctrl+C` 退出。
