# GStreamer 官方教程 playback-tutorial-4 的完整示例

原文地址: <https://gstreamer.freedesktop.org/documentation/tutorials/playback/progressive-streaming.html?gi-language=c>

这段代码使用 GStreamer 的 `playbin` 元件播放一个网络视频，同时：

1. 启用 *progressive download* 功能（像播放器一样一边下载一边播放）。
2. 监听各种 GStreamer 消息（ERROR、EOS、BUFFERING 等）。
3. 在终端绘制一个动态的缓存进度条（78 字符宽度）。
4. 显示临时文件下载路径（如果使用 progressive download）。
5. 对 live 流（如 RTSP）自动处理时钟丢失和无缓冲行为。
这段代码是基于 **GStreamer** 的 **网络视频渐进式下载播放器**，核心功能是：播放网络视频（支持渐进式下载缓存），并实时展示缓冲进度和播放进度的可视化界面。它重点解决了网络视频播放的核心问题——**缓冲管理**（避免卡顿）和 **用户体验优化**（进度可视化），以下是详细解析：


### 一、核心功能概述

1. **网络视频播放**：通过 `playbin` 播放指定网络视频（`sintel_trailer-480p.webm`），`playbin` 内置解封装、解码、渲染全流程。
2. **渐进式下载（缓存）**：启用 `GST_PLAY_FLAG_DOWNLOAD` 标志，让 `playbin` 下载视频到临时文件，支持边下载边播放（类似视频网站的“预加载”）。
3. **缓冲状态管理**：监听 `BUFFERING` 消息，缓冲进度 < 100% 时暂停播放，缓冲完成后自动恢复，避免播放卡顿。
4. **可视化进度条**：每秒刷新终端界面，用字符图形展示：
   - 已缓冲的视频片段（`-` 表示）；
   - 当前播放位置（`>` 表示，缓冲完成时）或缓冲中标记（`X` 表示，缓冲未完成时）；
   - 实时缓冲百分比（如 `Buffering: 75%`）。
5. **错误与异常处理**：处理播放错误、流结束（EOS）、时钟丢失（`CLOCK_LOST`）等情况，保证程序稳健运行。


### 二、关键结构体与宏定义
#### 1. 宏定义（进度条长度）

```c
#define GRAPH_LENGTH 78  /* 终端进度条的字符长度（78个字符，适配大多数终端宽度） */
```

#### 2. `CustomData` 结构体（全局状态存储）

```c
typedef struct _CustomData {
    gboolean is_live;        /* 是否为直播流（直播流不处理缓冲） */
    GstElement *pipeline;    /* 核心管道（playbin） */
    GMainLoop *loop;         /* GLib 主循环（驱动事件） */
    gint buffering_level;    /* 当前缓冲进度（0~100%） */
} CustomData;
```

#### 3. `GstPlayFlags` 枚举（扩展 `playbin` 功能）

```c
typedef enum {
    GST_PLAY_FLAG_DOWNLOAD = (1 << 7)  /* 启用渐进式下载（缓存）功能 */
} GstPlayFlags;
```

- 注：`GST_PLAY_FLAG_DOWNLOAD` 是 `playbin` 的扩展标志，用于开启“下载到临时文件”功能，默认关闭。临时文件路径可通过 `temp-location` 属性获取（示例中通过 `deep-notify::temp-location` 信号打印）。


### 三、核心流程与关键函数解析
#### 1. 主函数（`main`）：初始化与管道启动

主函数是程序入口，负责初始化、配置 `playbin`、注册回调，流程如下：

##### （1）初始化与数据清零

```c
gst_init(&argc, &argv);  /* 初始化 GStreamer 库 */
memset(&data, 0, sizeof(data));  /* 初始化 CustomData 结构体 */
data.buffering_level = 100;  /* 初始缓冲进度设为 100%（默认无缓冲需求） */
```

##### （2）创建管道与配置下载功能

```c
// 1. 创建 playbin 管道，指定网络视频 URI
pipeline = gst_parse_launch("playbin uri=https://gstreamer.freedesktop.org/data/media/sintel_trailer-480p.webm", NULL);
// 2. 获取管道总线（用于消息监听）
bus = gst_element_get_bus(pipeline);

// 3. 启用渐进式下载功能（关键：设置 GST_PLAY_FLAG_DOWNLOAD 标志）
g_object_get(pipeline, "flags", &flags, NULL);  /* 读取当前 flags */
flags |= GST_PLAY_FLAG_DOWNLOAD;                /* 新增下载标志 */
g_object_set(pipeline, "flags", flags, NULL);   /* 应用配置 */

// （可选）限制下载缓存大小（如 4MB），默认无限制
/* g_object_set (pipeline, "ring-buffer-max-size", (guint64)4000000, NULL); */
```

通过设置 `GST_PLAY_FLAG_DOWNLOAD` 这个标志， `playbin` 指示其内部队列（实际上是一个 queue2 元素）存储所有下载的数据。

##### （3）启动播放与判断流类型

```c
ret = gst_element_set_state(pipeline, GST_STATE_PLAYING);  /* 启动播放 */
if (ret == GST_STATE_CHANGE_FAILURE) {  /* 播放失败（如网络错误、解码失败） */
    g_printerr("Unable to set the pipeline to the playing state.\n");
    gst_object_unref(pipeline);
    return -1;
} else if (ret == GST_STATE_CHANGE_NO_PREROLL) {  /* 无预滚动 → 直播流（live stream） */
    data.is_live = TRUE;  /* 直播流不处理缓冲（无需缓存，实时播放） */
}
```

- 关键：`GST_STATE_CHANGE_NO_PREROLL` 表示流是“直播流”（如实时摄像头、网络直播），这类流无法预加载，因此不需要处理缓冲逻辑。

##### （4）注册回调与启动主循环

```c
// 1. 创建主循环
main_loop = g_main_loop_new(NULL, FALSE);
data.loop = main_loop;
data.pipeline = pipeline;

// 2. 总线监听：注册消息回调（处理错误、EOS、缓冲等）
gst_bus_add_signal_watch(bus);
g_signal_connect(bus, "message", G_CALLBACK(cb_message), &data);

// 3. 监听临时文件路径：打印下载的临时文件位置
g_signal_connect(pipeline, "deep-notify::temp-location", G_CALLBACK(got_location), NULL);

// 4. 注册 UI 刷新回调：每秒调用一次 refresh_ui，更新进度条
g_timeout_add_seconds(1, (GSourceFunc)refresh_ui, &data);

// 5. 启动主循环（阻塞，直到 quit）
g_main_loop_run(main_loop);
```

`deep-notify` 信号是由 `GstObject` 元素（如 `playbin` ）在它们的任何子元素属性发生变化时发出的。在这种情况下，我们想知道何时 `temp-location` 属性发生变化，这表明 `queue2` 已经决定在哪里存储下载的数据。

##### （5）资源释放

```c
g_main_loop_unref(main_loop);
gst_object_unref(bus);
gst_element_set_state(pipeline, GST_STATE_NULL);  /* 停止管道，释放资源 */
gst_object_unref(pipeline);
```

#### 2. `got_location`：获取临时下载文件路径

当 `playbin` 创建临时下载文件时，会触发 `deep-notify::temp-location` 信号，调用此函数打印临时文件路径：

```c
static void got_location(GstObject *gstobject, GstObject *prop_object, GParamSpec *prop, gpointer data) {
    gchar *location;
    g_object_get(G_OBJECT(prop_object), "temp-location", &location, NULL);  /* 读取临时文件路径 */
    g_print("Temporary file: %s\n", location);  /* 打印路径（如 /tmp/gst-playbin-xxx.webm） */
    g_free(location);  /* 释放字符串 */

    /* 取消注释以下行，可在程序退出后保留临时文件（默认自动删除） */
    /* g_object_set (G_OBJECT (prop_object), "temp-remove", FALSE, NULL); */
}
```

- 作用：查看 `playbin` 下载视频的临时存储位置，默认程序退出后会自动删除临时文件。

#### 3. `cb_message`：处理管道消息（核心回调）

管道的所有消息（错误、EOS、缓冲、时钟丢失等）都会触发此函数，核心逻辑如下：

```c
static void cb_message(GstBus *bus, GstMessage *msg, CustomData *data) {
    switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_ERROR:  /* 播放错误（如网络中断、解码失败） */
        {
            GError *err;
            gchar *debug;
            gst_message_parse_error(msg, &err, &debug);
            g_print("Error: %s\n", err->message);
            g_error_free(err);
            g_free(debug);
            // 停止管道，退出主循环
            gst_element_set_state(data->pipeline, GST_STATE_READY);
            g_main_loop_quit(data->loop);
            break;
        }
        case GST_MESSAGE_EOS:  /* 流结束（视频播放完成） */
            gst_element_set_state(data->pipeline, GST_STATE_READY);
            g_main_loop_quit(data->loop);
            break;
        case GST_MESSAGE_BUFFERING:  /* 缓冲状态更新 */
            // 直播流不处理缓冲（无需缓存）
            if (data->is_live) break;

            // 解析缓冲进度（0~100%）
            gst_message_parse_buffering(msg, &data->buffering_level);

            // 缓冲 < 100% → 暂停播放（避免卡顿）；缓冲完成 → 恢复播放
            if (data->buffering_level < 100)
                gst_element_set_state(data->pipeline, GST_STATE_PAUSED);
            else
                gst_element_set_state(data->pipeline, GST_STATE_PLAYING);
            break;
        case GST_MESSAGE_CLOCK_LOST:  /* 时钟丢失（如设备切换导致时钟异常） */
            // 重新设置管道状态，恢复时钟同步
            gst_element_set_state(data->pipeline, GST_STATE_PAUSED);
            gst_element_set_state(data->pipeline, GST_STATE_PLAYING);
            break;
        default:  /* 其他未处理消息（如状态变更、标签等） */
            break;
    }
}
```

- 核心逻辑：通过 `BUFFERING` 消息动态调整播放状态，确保缓冲足够后再播放，避免“边缓冲边卡顿”。

#### 4. `refresh_ui`：刷新终端进度条（可视化核心）

每秒被 `g_timeout_add_seconds` 调用一次，生成字符型进度条，展示缓冲状态和播放位置：

```c
static gboolean refresh_ui(CustomData *data) {
    GstQuery *query;  /* 用于查询管道状态（缓冲范围、播放位置、时长） */
    gboolean result;

    // 1. 创建“缓冲查询”：查询已缓冲的视频范围（按百分比）
    query = gst_query_new_buffering(GST_FORMAT_PERCENT);
    result = gst_element_query(data->pipeline, query);  /* 向管道发送查询 */
    if (result) {  /* 查询成功 */
        gint n_ranges, range, i;
        gchar graph[GRAPH_LENGTH + 1];  /* 进度条字符数组 */
        gint64 position = 0, duration = 0;  /* 播放位置、视频总时长（纳秒） */

        // 2. 初始化进度条：全部填充空格
        memset(graph, ' ', GRAPH_LENGTH);
        graph[GRAPH_LENGTH] = '\0';  /* 字符串结束符 */

        // 3. 解析已缓冲的范围（可能有多个不连续的缓冲片段）
        n_ranges = gst_query_get_n_buffering_ranges(query);  /* 缓冲片段数量 */
        for (range = 0; range < n_ranges; range++) {
            gint64 start, stop;  /* 缓冲片段的起始/结束百分比（0~100） */
            gst_query_parse_nth_buffering_range(query, range, &start, &stop);
            // 将百分比映射到进度条的字符索引（0~GRAPH_LENGTH-1）
            start = start * GRAPH_LENGTH / 100;
            stop = stop * GRAPH_LENGTH / 100;
            // 用 '-' 标记已缓冲的片段
            for (i = (gint)start; i < stop; i++)
                graph[i] = '-';
        }

        // 4. 解析播放位置和总时长，标记当前播放点
        if (gst_element_query_position(data->pipeline, GST_FORMAT_TIME, &position) &&  /* 查询播放位置 */
            GST_CLOCK_TIME_IS_VALID(position) &&  /* 位置有效 */
            gst_element_query_duration(data->pipeline, GST_FORMAT_TIME, &duration) &&  /* 查询总时长 */
            GST_CLOCK_TIME_IS_VALID(duration)) {  /* 时长有效 */
            // 将播放位置映射到进度条的字符索引
            i = (gint)(GRAPH_LENGTH * (double)position / (double)(duration + 1));
            // 缓冲未完成 → 标记为 'X'；缓冲完成 → 标记为 '>'
            graph[i] = data->buffering_level < 100 ? 'X' : '>';
        }

        // 5. 打印进度条和缓冲信息
        g_print("[%s]", graph);  /* 打印进度条（如 [-------          >              ]） */
        if (data->buffering_level < 100) {
            g_print(" Buffering: %3d%%", data->buffering_level);  /* 缓冲中，显示百分比 */
        } else {
            g_print("                ");  /* 缓冲完成，清空百分比区域（保持界面整洁） */
        }
        g_print("\r");  /* 回车不换行，实现进度条刷新（覆盖上一次输出） */
    }

    gst_query_unref(query);  /* 释放查询对象 */
    return TRUE;  /* 返回 TRUE，继续每秒刷新 */
}
```

- 进度条示例（缓冲中）：`[-------X---------------------------] Buffering:  25%`
- 进度条示例（缓冲完成）：`[------------------------>----------]                `
- 核心作用：通过字符图形让用户直观看到“已缓冲部分”和“当前播放位置”，提升用户体验。


### 四、关键 GStreamer 概念总结
#### 1. `playbin` 渐进式下载（`GST_PLAY_FLAG_DOWNLOAD`）

- 功能：开启后，`playbin` 会将网络视频下载到本地临时文件，边下载边播放，支持断点续传（网络中断后恢复可继续下载）。
- 相关属性：
  - `temp-location`：临时文件路径（默认在系统临时目录，如 `/tmp`）；
  - `temp-remove`：是否自动删除临时文件（默认 `TRUE`，程序退出后删除）；
  - `ring-buffer-max-size`：最大缓存大小（限制下载文件的最大体积）。

#### 2. 缓冲消息（`GST_MESSAGE_BUFFERING`）

- 触发时机：`playbin` 下载视频数据时，会定期发送此消息，携带当前缓冲进度（0~100%）。
- 处理逻辑：缓冲进度 < 100% 时暂停播放，避免因数据不足导致卡顿；缓冲完成后恢复播放，平衡“预加载”和“实时播放”。

#### 3. 管道查询（`GstQuery`）

- 作用：应用程序通过 `GstQuery` 向管道查询状态（如缓冲范围、播放位置、时长、支持的格式等），是“双向通信”的关键。
- 本示例用到的查询：
  - `gst_query_new_buffering`：查询已缓冲的视频范围；
  - `gst_element_query_position`：查询当前播放位置；
  - `gst_element_query_duration`：查询视频总时长。

#### 4. GLib 定时回调（`g_timeout_add_seconds`）

- 功能：每隔指定时间（本示例 1 秒）调用一次回调函数（`refresh_ui`），用于更新 UI 或执行周期性任务。
- 注意：回调函数返回 `TRUE` 会持续触发，返回 `FALSE` 会停止触发。


### 五、编译与运行说明
#### 1. 编译命令

```bash
gcc playback-tutorial-4.c -o playback-tutorial-4 `pkg-config --cflags --libs gstreamer-1.0`
```

#### 2. 运行效果

- 执行程序后，终端会先打印临时下载文件路径（如 `Temporary file: /tmp/gst-playbin-abc123.webm`）；
- 终端实时刷新进度条，缓冲时显示 `Buffering: XX%`，缓冲完成后显示当前播放位置（`>`）；
- 视频窗口弹出，缓冲完成后开始播放；
- 播放完成或按 `Ctrl+C` 终止程序，终端换行并退出。


### 六、常见问题排查
#### 1. 无法下载视频（无临时文件路径）

- 检查网络是否正常：确保能访问 `https://gstreamer.freedesktop.org/data/media/sintel_trailer-480p.webm`；
- 检查是否安装网络插件：`gst-inspect-1.0 souphttpsrc`（若提示“No such element or plugin”，需安装 `gstreamer1.0-plugins-good`）。

#### 2. 进度条不更新

- 检查 `g_timeout_add_seconds` 是否注册成功：确保回调函数 `refresh_ui` 返回 `TRUE`；
- 检查管道查询是否成功：若 `gst_element_query` 返回 `FALSE`，可能是视频未加载完成（等待几秒再试）。

#### 3. 播放卡顿（缓冲频繁暂停）

- 限制缓存大小：取消注释 `g_object_set (pipeline, "ring-buffer-max-size", (guint64)4000000, NULL);`，减少缓存占用；
- 降低视频分辨率：更换低分辨率视频 URI（如 360P 视频），减少网络带宽压力。
