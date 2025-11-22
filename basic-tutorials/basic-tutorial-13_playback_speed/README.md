# GStreamer 官方教程 basic-tutorial-13 的完整示例

程序原文地址: <https://gstreamer.freedesktop.org/documentation/tutorials/basic/playback-speed.html?gi-language=c>

这是一个基于 `playbin` 的 **可交互视频播放器**，核心功能是播放网络视频流，同时支持通过键盘快捷键控制播放状态——包括暂停/继续、调节播放速度、切换播放方向（正放/倒放）、单帧步进等，是在基础播放器上扩展了用户交互能力的进阶示例。

## 一、核心组件与整体流程
### 1.1 核心组件说明

| 组件/结构                | 功能描述                                                                 |
|--------------------------|--------------------------------------------------------------------------|
| `playbin`                | 核心播放组件：封装源、解复用、解码、渲染全流程，支持通过属性（如 `video-sink`）获取内部元素。 |
| `CustomData`             | 自定义数据结构：存储管道、视频渲染器、主循环，以及播放状态（`playing`）、播放速率（`rate`）等核心状态。 |
| `GMainLoop`              | GLib 主循环：阻塞等待事件（键盘输入、播放消息），驱动异步交互。 |
| `GIOChannel`             | IO 通道：监听标准输入（键盘），将键盘事件转换为 GLib 事件，触发回调处理。 |
| `GstEvent`（`seek`/`step`） | GStreamer 事件：`seek` 事件用于调节播放速率、切换方向；`step` 事件用于单帧步进。 |
| `video-sink`             | 视频渲染元素：`playbin` 内部的视频输出组件（如 `autovideosink`），通过 `g_object_get` 获取，用于发送控制事件。 |

### 1.2 整体工作流程

```mathematica
初始化 GStreamer → 构建 playbin 管道 → 监听键盘输入 → 启动播放 → 主循环阻塞 → 键盘事件触发回调 → 执行对应控制（暂停/调速/倒放/单帧）→ 退出主循环 → 释放资源
```

## 二、代码逐段解析
### 2.1 头文件与数据结构定义

```c
#include <string.h>
#include <stdio.h>
#include <gst/gst.h>

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

// 自定义数据结构：存储播放器状态和核心资源
typedef struct _CustomData
{
  GstElement *pipeline;    // playbin 管道实例
  GstElement *video_sink;  // 视频渲染元素（从 playbin 中获取）
  GMainLoop *loop;         // GLib 主循环

  gboolean playing;        // 播放状态标记（TRUE=播放，FALSE=暂停）
  gdouble rate;            // 播放速率（1.0=正常，>1=加速，<0=倒放，0.5=减速）
} CustomData;
```

- 关键字段：
  - `playing`：记录当前播放状态，用于切换暂停/继续；
  - `rate`：播放速率（支持正负值，正为正放、负为倒放，绝对值表示速度倍数）；
  - `video_sink`：从 `playbin` 中获取的视频渲染元素，用于发送 `seek`（调速/倒放）和 `step`（单帧）事件。

### 2.2 播放速率控制函数 `send_seek_event`

核心功能：发送 `GST_EVENT_SEEK` 事件到 `video_sink`，实现播放速率调节和播放方向切换（正放→倒放/倒放→正放）。

```c
static void send_seek_event (CustomData * data)
{
  gint64 position;  // 当前播放位置（单位：纳秒，GST_FORMAT_TIME 格式）
  GstEvent *seek_event;  // seek 事件实例

  /* 1. 获取当前播放位置（必须，seek 事件需要基于当前位置计算新的播放范围） */
  if (!gst_element_query_position (data->pipeline, GST_FORMAT_TIME, &position)) {
    g_printerr ("Unable to retrieve current position.\n");
    return;
  }

  /* 2. 创建 seek 事件（根据播放速率正负，设置不同的播放范围） */
  if (data->rate > 0) {  // 正速率：正放，播放范围从「当前位置」到「流结束」
    seek_event = gst_event_new_seek (
      data->rate,                // 目标播放速率
      GST_FORMAT_TIME,           // 时间格式（以纳秒为单位）
      GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE,  // 标志：刷新缓冲区+精准定位
      GST_SEEK_TYPE_SET, position,  // 开始位置：当前位置
      GST_SEEK_TYPE_END, 0          // 结束位置：流结束（0 表示默认结束点）
    );
  } else {  // 负速率：倒放，播放范围从「流开始」到「当前位置」
    seek_event = gst_event_new_seek (
      data->rate,                // 目标播放速率（负数表示倒放）
      GST_FORMAT_TIME,           // 时间格式
      GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE,  // 标志
      GST_SEEK_TYPE_SET, 0,        // 开始位置：流开始
      GST_SEEK_TYPE_SET, position  // 结束位置：当前位置
    );
  }

  /* 3. 若未获取 video_sink，从 playbin 中获取（playbin 内置 video-sink 属性） */
  if (data->video_sink == NULL) {
    g_object_get (data->pipeline, "video-sink", &data->video_sink, NULL);
  }

  /* 4. 发送 seek 事件到 video_sink，触发速率/方向变更 */
  gst_element_send_event (data->video_sink, seek_event);

  /* 打印当前速率 */
  g_print ("Current rate: %g\n", data->rate);
}
```

- 关键细节：
  - `GST_SEEK_FLAG_FLUSH`：发送事件前清空管道缓冲区，避免旧数据影响新速率播放；
  - `GST_SEEK_FLAG_ACCURATE`：要求精准定位（尤其倒放和变速时，避免跳帧）；
  - 倒放逻辑：通过设置播放范围为「开始→当前位置」+ 负速率，实现从当前位置倒退回开始。

### 2.3 键盘事件处理函数 `handle_keyboard`

监听键盘输入，根据按键执行对应控制逻辑（暂停/继续、调速、倒放、单帧步进、退出）：

```c
static gboolean handle_keyboard (GIOChannel * source, GIOCondition cond, CustomData * data)
{
  gchar *str = NULL;  // 存储读取的键盘输入

  /* 读取键盘输入（一行文字，取第一个字符作为命令） */
  if (g_io_channel_read_line (source, &str, NULL, NULL, NULL) != G_IO_STATUS_NORMAL) {
    return TRUE;  // 读取失败仍返回 TRUE，保持监听
  }

  /* 转换第一个字符为小写（支持大小写兼容，如 'P' 和 'p' 都触发暂停/继续） */
  switch (g_ascii_tolower (str[0])) {
    // 1. 暂停/继续切换（按键 'P' 或 'p'）
    case 'p':
      data->playing = !data->playing;  // 切换播放状态标记
      // 设置管道状态：PLAYING 或 PAUSED
      gst_element_set_state (data->pipeline, data->playing ? GST_STATE_PLAYING : GST_STATE_PAUSED);
      g_print ("Setting state to %s\n", data->playing ? "PLAYING" : "PAUSE");
      break;

    // 2. 调节播放速度（按键 'S' 加速，'s' 减速）
    case 's':
      if (g_ascii_isupper (str[0])) {  // 大写 'S'：速率 ×2（加速）
        data->rate *= 2.0;
      } else {  // 小写 's'：速率 ÷2（减速）
        data->rate /= 2.0;
      }
      send_seek_event (data);  // 发送 seek 事件生效速率
      break;

    // 3. 切换播放方向（按键 'D' 或 'd'）
    case 'd':
      data->rate *= -1.0;  // 速率取反（正→负=倒放，负→正=正放）
      send_seek_event (data);  // 发送 seek 事件生效方向
      break;

    // 4. 单帧步进（按键 'N' 或 'n'，暂停时效果更明显）
    case 'n':
      // 若未获取 video_sink，从 playbin 中获取
      if (data->video_sink == NULL) {
        g_object_get (data->pipeline, "video-sink", &data->video_sink, NULL);
      }

      // 发送 step 事件：单帧步进（按当前速率方向）
      gst_element_send_event (data->video_sink,
        gst_event_new_step (
          GST_FORMAT_BUFFERS,  // 步进单位：缓冲区（对应单帧）
          1,                   // 步进数量：1 帧
          ABS (data->rate),    // 步进速率：当前速率的绝对值（保持速度比例）
          TRUE,                // 步进后暂停（方便查看单帧）
          FALSE                // 不刷新缓冲区
        )
      );
      g_print ("Stepping one frame\n");
      break;

    // 5. 退出程序（按键 'Q' 或 'q'）
    case 'q':
      g_main_loop_quit (data->loop);  // 退出主循环
      break;

    // 其他按键：忽略
    default:
      break;
  }

  g_free (str);  // 释放输入字符串内存
  return TRUE;   // 返回 TRUE 保持键盘监听
}
```

- 关键细节：
  - 键盘输入通过 `GIOChannel` 读取，支持跨平台（Windows/Unix）；
  - 单帧步进（`step` 事件）：步进后自动暂停（`TRUE` 参数），适合逐帧查看画面；
  - 速率调节：加速/减速按 2 倍系数切换（如 1.0→2.0→4.0 或 1.0→0.5→0.25）。

### 2.4 主函数 `tutorial_main` 与平台适配 `main`
#### 2.4.1 `tutorial_main`：核心逻辑实现

初始化组件、构建管道、监听键盘、启动播放、释放资源：

```c
int tutorial_main (int argc, char *argv[])
{
  CustomData data;
  GstStateChangeReturn ret;
  GIOChannel *io_stdin;  // 监听键盘输入的 IO 通道

  /* 1. 初始化 GStreamer */
  gst_init (&argc, &argv);

  /* 2. 初始化自定义数据结构（清零） */
  memset (&data, 0, sizeof (data));

  /* 3. 打印使用说明（告知用户快捷键功能） */
  g_print ("USAGE: Choose one of the following options, then press enter:\n"
      " 'P' to toggle between PAUSE and PLAY\n"
      " 'S' to increase playback speed, 's' to decrease playback speed\n"
      " 'D' to toggle playback direction\n"
      " 'N' to move to next frame (in the current direction, better in PAUSE)\n"
      " 'Q' to quit\n");

  /* 4. 构建管道：playbin + 网络视频 URI */
  data.pipeline = gst_parse_launch (
    "playbin uri=https://gstreamer.freedesktop.org/data/media/sintel_trailer-480p.webm",
    NULL
  );

  /* 5. 创建 IO 通道，监听标准输入（键盘） */
#ifdef G_OS_WIN32
  // Windows 平台：创建 Windows 风格的 IO 通道
  io_stdin = g_io_channel_win32_new_fd (fileno (stdin));
#else
  // Unix/Linux/macOS 平台：创建 Unix 风格的 IO 通道
  io_stdin = g_io_channel_unix_new (fileno (stdin));
#endif
  // 添加 IO 监听：当标准输入有数据（键盘按键）时，触发 handle_keyboard 回调
  g_io_add_watch (io_stdin, G_IO_IN, (GIOFunc) handle_keyboard, &data);

  /* 6. 启动播放：设置管道为 PLAYING 状态 */
  ret = gst_element_set_state (data.pipeline, GST_STATE_PLAYING);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    g_printerr ("Unable to set the pipeline to the playing state.\n");
    gst_object_unref (data.pipeline);
    return -1;
  }
  // 初始化播放状态和速率（默认：播放中，正常速率 1.0）
  data.playing = TRUE;
  data.rate = 1.0;

  /* 7. 创建并启动主循环（阻塞，等待事件） */
  data.loop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (data.loop);

  /* 8. 释放所有资源 */
  g_main_loop_unref (data.loop);         // 释放主循环
  g_io_channel_unref (io_stdin);         // 释放 IO 通道
  gst_element_set_state (data.pipeline, GST_STATE_NULL);  // 停止管道
  if (data.video_sink != NULL)
    gst_object_unref (data.video_sink);  // 释放 video_sink
  gst_object_unref (data.pipeline);      // 释放管道
  return 0;
}
```

#### 2.4.2 `main`：平台适配入口

处理 macOS 平台的特殊启动逻辑，确保跨平台兼容性：

```c
int main (int argc, char *argv[])
{
#if defined(__APPLE__) && TARGET_OS_MAC && !TARGET_OS_IPHONE
  // macOS 平台：使用 gst_macos_main 启动（适配 macOS 沙箱和 UI 线程）
  return gst_macos_main ((GstMainFunc) tutorial_main, argc, argv, NULL);
#else
  // 其他平台（Windows/Linux）：直接运行核心逻辑
  return tutorial_main (argc, argv);
#endif
}
```

## 三、编译与运行
### 3.1 编译命令

需安装 GStreamer 核心库和跨平台 IO 相关依赖，编译命令：

```bash
gcc -o gst-interactive-player gst-interactive-player.c `pkg-config --cflags --libs gstreamer-1.0 glib-2.0`
```

### 3.2 运行与交互示例

```bash
./gst-interactive-player
```

运行后终端会打印使用说明，按以下快捷键交互：
| 快捷键 | 功能描述                                  | 示例效果                          |
|--------|-------------------------------------------|-----------------------------------|
| `P/p`  | 切换暂停/继续                              | 播放中→暂停，暂停中→播放          |
| `S`    | 加速播放（速率 ×2）                        | 1.0 → 2.0 → 4.0 → 8.0...          |
| `s`    | 减速播放（速率 ÷2）                        | 1.0 → 0.5 → 0.25 → 0.125...       |
| `D/d`  | 切换播放方向（正放↔倒放）                  | 1.0 → -1.0（倒放）→ 1.0（正放）   |
| `N/n`  | 单帧步进（步进后自动暂停）                 | 暂停时逐帧查看画面，支持倒放方向步进 |
| `Q/q`  | 退出程序                                  | 停止播放并关闭窗口                |

### 3.3 自定义播放 URI

支持命令行传入自定义视频 URI（本地文件或网络流）：

```bash
# 播放本地 MP4 文件
./gst-interactive-player file:///home/user/Videos/test.mp4
```

## 四、核心技术亮点
### 1. 键盘交互的跨平台实现

通过 `GIOChannel` 封装标准输入监听，适配 Windows/Unix/macOS 平台，无需针对不同系统编写单独的键盘处理逻辑。

### 2. `seek` 事件的灵活运用

通过 `gst_event_new_seek` 实现「速率调节+方向切换」，核心是通过「播放范围+速率符号」控制倒放，通过「速率绝对值」控制播放速度。

### 3. `step` 事件的单帧控制

支持逐帧查看画面（尤其适合视频编辑、画面分析场景），步进后自动暂停，提升用户体验。

### 4. `playbin` 的属性访问

通过 `g_object_get (data->pipeline, "video-sink", &data->video_sink, NULL)` 获取 `playbin` 内部的视频渲染元素，实现对底层组件的精细控制，同时保留 `playbin` 的封装优势。

## 五、常见问题与解决方案
### 1. 键盘输入无响应？

- 原因：终端未聚焦（需确保终端是当前活动窗口），或 IO 通道创建失败；
- 解决方案：运行后点击终端窗口聚焦，若仍无响应，检查 GStreamer 和 GLib 依赖是否完整。

### 2. 倒放时画面卡顿或无声音？

- 原因：部分编码格式（如 H.264）对倒放支持有限，或缺少对应的解码插件；
- 解决方案：尝试使用 WebM（VP8/VP9 编码）或 OGG 格式视频，安装 `gstreamer1.0-plugins-good` 插件。

### 3. 单帧步进无效果？

- 原因：管道处于播放状态（`PLAYING`），步进后立即继续播放，视觉上无法感知；
- 解决方案：先按 `P` 暂停，再按 `N` 单帧步进，效果更明显。

### 4.  macOS 平台无法启动？

- 原因：未适配 macOS 沙箱机制，或缺少 `gst_macos_main` 相关依赖；
- 解决方案：确保安装了完整的 macOS 版 GStreamer（包含 `gstreamer1.0-macosx` 组件）。

## 六、扩展方向

1. **音量控制**：通过 `g_object_set (data->pipeline, "volume", 0.5, NULL)` 调节音量（0.0~1.0）；
2. **进度条显示**：定期查询 `gst_element_query_position` 获取当前播放位置，结合 `gst_element_query_duration` 获取总时长，打印进度条；
3. **快进/后退**：通过 `seek` 事件直接跳转到指定时间（如 `position + 10*GST_SECOND` 快进 10 秒）；
4. **播放列表**：支持多个 URI 切换，记录每个视频的播放状态和速率；
5. **图形界面（GUI）**：结合 GTK/Qt 替换键盘控制，实现按钮、滑块等可视化控件。
