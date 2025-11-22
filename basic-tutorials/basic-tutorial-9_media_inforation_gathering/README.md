# GStreamer 媒体文件信息探测工具代码解析

程序原文地址: <https://gstreamer.freedesktop.org/documentation/tutorials/basic/media-information-gathering.html?gi-language=c>

这是一个基于 GStreamer 的 **媒体文件信息探测工具**，核心功能是通过 `GstDiscoverer` 组件异步探测指定媒体 URI（本地文件或网络流）的详细信息，包括：媒体有效性、时长、标签（标题/作者等）、可seek性、流拓扑结构（音频/视频/字幕流）、编码格式等。代码采用 GLib 主循环实现异步事件驱动，是 GStreamer 媒体信息探测的典型应用。

这段程序做了以下几件事情：
1. 演示了 GstDiscoverer 的完整用法（创建、启动、异步任务、回调处理）；
2. 提供了媒体元数据（标签、流信息、时长）的标准化解析方式；
3. 采用 GLib 主循环实现异步事件驱动，符合 GStreamer 编程规范；
4. 代码结构清晰（辅助函数 + 回调函数 + 主函数），可扩展性强。

## 一、核心组件与整体流程
### 1.1 核心组件说明

| 组件/结构                | 功能描述                                                                 |
|--------------------------|--------------------------------------------------------------------------|
| `GstDiscoverer`          | GStreamer 核心探测组件：异步分析媒体文件，获取格式、流信息、标签等元数据。 |
| `CustomData`             | 自定义数据结构：存储 `GstDiscoverer` 实例和 GLib 主循环，用于回调函数共享数据。 |
| `GMainLoop`              | GLib 主循环：阻塞等待探测回调信号（探测完成、全部任务结束），驱动异步流程。 |
| `GstDiscovererInfo`      | 探测结果容器：存储单个 URI 的完整探测信息（时长、标签、流信息等）。        |
| `GstDiscovererStreamInfo`| 流信息容器：存储单个媒体流（音频/视频/字幕）的详细信息（编码格式、Caps 等）。 |
| `GstTagList`             | 标签列表：存储媒体文件的元数据标签（如标题、作者、日期、比特率等）。        |

### 1.2 整体工作流程

```mathematica
初始化 → 创建 GstDiscoverer → 连接回调信号 → 启动探测 → 异步添加 URI 任务 → 主循环阻塞 → 探测回调处理结果 → 全部完成后退出主循环 → 释放资源
```

## 二、代码逐段解析
### 2.1 头文件与数据结构定义

```c
#include <string.h>
#include <gst/gst.h>
#include <gst/pbutils/pbutils.h>  // 提供编解码器描述等工具函数

/* 自定义数据结构：传递给回调函数的共享数据 */
typedef struct _CustomData {
  GstDiscoverer *discoverer;  // 媒体探测实例
  GMainLoop *loop;             // GLib 主循环
} CustomData;
```

- 关键头文件 `gst/pbutils/pbutils.h`：提供 `gst_pb_utils_get_codec_description()` 等工具函数，用于将编码格式（Caps）转换为人类可读的描述（如 "VP8 视频编码"）。

### 2.2 标签打印辅助函数 `print_tag_foreach`

用于遍历 `GstTagList`（标签列表），将每个标签以「标签昵称: 值」的格式打印（支持字符串和非字符串类型标签）：

```c
static void print_tag_foreach (const GstTagList *tags, const gchar *tag, gpointer user_data) {
  GValue val = { 0, };  // 存储标签值的通用容器
  gchar *str;
  gint depth = GPOINTER_TO_INT (user_data);  // 缩进深度（美化输出）

  // 从标签列表中复制当前标签的值到 GValue
  gst_tag_list_copy_value (&val, tags, tag);

  // 序列化标签值为字符串（支持字符串/非字符串类型）
  if (G_VALUE_HOLDS_STRING (&val))
    str = g_value_dup_string (&val);  // 字符串类型直接复制
  else
    str = gst_value_serialize (&val);  // 非字符串类型（如数字、时间）序列化

  // 打印标签（缩进 depth 级，标签昵称为人类可读名称）
  g_print ("%*s%s: %s\n", 2 * depth, " ", gst_tag_get_nick (tag), str);
  
  // 释放资源
  g_free (str);
  g_value_unset (&val);
}
```

- 关键函数：
  - `gst_tag_get_nick(tag)`：将标签名（如 `GST_TAG_TITLE`）转换为人类可读昵称（如 "title"）；
  - `gst_value_serialize(&val)`：将任意 GValue 类型（如整数、时间）序列化为字符串，确保兼容性。

### 2.3 流信息打印函数 `print_stream_info`

打印单个媒体流（音频/视频/字幕）的基本信息：流类型、编码格式描述、流关联标签：

```c
static void print_stream_info (GstDiscovererStreamInfo *info, gint depth) {
  gchar *desc = NULL;
  GstCaps *caps;  // 流的能力描述（编码格式、分辨率等）
  const GstTagList *tags;  // 流专属标签

  // 获取流的 Caps（核心：描述流的编码格式、参数）
  caps = gst_discoverer_stream_info_get_caps (info);
  if (caps) {
    if (gst_caps_is_fixed (caps))  // 固定 Caps（包含具体参数，如 1920x1080）
      desc = gst_pb_utils_get_codec_description (caps);  // 编解码器描述（如 "VP8 视频"）
    else  // 非固定 Caps（仅描述类型，无具体参数）
      desc = gst_caps_to_string (caps);  // 直接转换为字符串（如 "video/x-vp8"）
    gst_caps_unref (caps);  // 释放 Caps
  }

  // 打印流类型和编码描述（缩进美化）
  g_print ("%*s%s: %s\n", 2 * depth, " ", 
           gst_discoverer_stream_info_get_stream_type_nick (info),  // 流类型昵称（如 "video"）
           (desc ? desc : ""));  // 编码描述（无则为空）

  // 释放编码描述字符串
  if (desc) g_free (desc);

  // 打印流专属标签（如音频流的比特率、视频流的分辨率）
  tags = gst_discoverer_stream_info_get_tags (info);
  if (tags) {
    g_print ("%*sTags:\n", 2 * (depth + 1), " ");
    gst_tag_list_foreach (tags, print_tag_foreach, GINT_TO_POINTER (depth + 2));
  }
}
```

### 2.4 流拓扑递归打印函数 `print_topology`

递归打印媒体文件的完整流拓扑结构（处理容器格式中的多流嵌套，如 MKV 包含音频+视频+字幕流）：

```c
static void print_topology (GstDiscovererStreamInfo *info, gint depth) {
  GstDiscovererStreamInfo *next;

  if (!info) return;

  // 打印当前流信息
  print_stream_info (info, depth);

  // 处理「顺序流」（如同一类型的多个流，如双音轨）
  next = gst_discoverer_stream_info_get_next (info);
  if (next) {
    print_topology (next, depth + 1);  // 递归打印下一个流（缩进+1）
    gst_discoverer_stream_info_unref (next);  // 释放流信息
  } 
  // 处理「容器流」（如 MKV、MP4 等包含多个子流的格式）
  else if (GST_IS_DISCOVERER_CONTAINER_INFO (info)) {
    GList *tmp, *streams;

    // 获取容器内的所有子流
    streams = gst_discoverer_container_info_get_streams (GST_DISCOVERER_CONTAINER_INFO (info));
    for (tmp = streams; tmp; tmp = tmp->next) {
      GstDiscovererStreamInfo *tmpinf = (GstDiscovererStreamInfo *) tmp->data;
      print_topology (tmpinf, depth + 1);  // 递归打印子流（缩进+1）
    }
    gst_discoverer_stream_info_list_free (streams);  // 释放流列表
  }
}
```

- 核心逻辑：处理两种流关系——「顺序流」（同一层级的多个流）和「容器流」（嵌套子流），确保完整打印所有流信息。

### 2.5 探测结果回调函数 `on_discovered_cb`

当 `GstDiscoverer` 完成单个 URI 的探测后触发，处理探测结果（成功/失败）并打印信息：

```c
static void on_discovered_cb (GstDiscoverer *discoverer, GstDiscovererInfo *info, GError *err, CustomData *data) {
  GstDiscovererResult result;
  const gchar *uri;
  const GstTagList *tags;
  GstDiscovererStreamInfo *sinfo;

  // 获取探测的 URI 和结果状态
  uri = gst_discoverer_info_get_uri (info);
  result = gst_discoverer_info_get_result (info);

  // 根据探测结果状态打印提示
  switch (result) {
    case GST_DISCOVERER_URI_INVALID:
      g_print ("Invalid URI '%s'\n", uri); break;  // URI 格式无效
    case GST_DISCOVERER_ERROR:
      g_print ("Discoverer error: %s\n", err->message); break;  // 探测错误
    case GST_DISCOVERER_TIMEOUT:
      g_print ("Timeout\n"); break;  // 探测超时（默认 5 秒）
    case GST_DISCOVERER_BUSY:
      g_print ("Busy\n"); break;  // 探测器忙碌
    case GST_DISCOVERER_MISSING_PLUGINS:{  // 缺少必要插件（如编码格式不支持）
      const GstStructure *s = gst_discoverer_info_get_misc (info);
      gchar *str = gst_structure_to_string (s);
      g_print ("Missing plugins: %s\n", str);
      g_free (str);
      break;
    }
    case GST_DISCOVERER_OK:
      g_print ("Discovered '%s'\n", uri); break;  // 探测成功
  }

  // 探测失败则直接返回（不打印详细信息）
  if (result != GST_DISCOVERER_OK) {
    g_printerr ("This URI cannot be played\n");
    return;
  }

  // 探测成功：打印详细信息
  // 1. 媒体时长（GST_TIME_FORMAT 格式化时间为 "时:分:秒.毫秒"）
  g_print ("\nDuration: %" GST_TIME_FORMAT "\n", GST_TIME_ARGS (gst_discoverer_info_get_duration (info)));

  // 2. 媒体全局标签（如标题、作者、版权等）
  tags = gst_discoverer_info_get_tags (info);
  if (tags) {
    g_print ("Tags:\n");
    gst_tag_list_foreach (tags, print_tag_foreach, GINT_TO_POINTER (1));  // 缩进 1 级
  }

  // 3. 可seek性（是否支持快进/后退）
  g_print ("Seekable: %s\n", (gst_discoverer_info_get_seekable (info) ? "yes" : "no"));

  // 4. 流拓扑结构（所有音频/视频/字幕流信息）
  g_print ("\n");
  sinfo = gst_discoverer_info_get_stream_info (info);  // 获取根流信息
  if (sinfo) {
    g_print ("Stream information:\n");
    print_topology (sinfo, 1);  // 递归打印流拓扑（缩进 1 级）
    gst_discoverer_stream_info_unref (sinfo);  // 释放流信息
  }

  g_print ("\n");
}
```

### 2.6 探测完成回调函数 `on_finished_cb`

当 `GstDiscoverer` 处理完所有添加的 URI 任务后触发，退出主循环：

```c
static void on_finished_cb (GstDiscoverer *discoverer, CustomData *data) {
  g_print ("Finished discovering\n");
  g_main_loop_quit (data->loop);  // 退出主循环，程序准备收尾
}
```

### 2.7 主函数 `main`（程序入口）

初始化组件、启动探测、阻塞主循环、释放资源：

```c
int main (int argc, char **argv) {
  CustomData data;
  GError *err = NULL;
  // 默认探测 URI（网络视频流：Sintel 预告片），支持命令行传入自定义 URI
  gchar *uri = "https://gstreamer.freedesktop.org/data/media/sintel_trailer-480p.webm";

  // 命令行传入 URI 则覆盖默认值（如 ./discoverer file:///home/user/video.mp4）
  if (argc > 1) {
    uri = argv[1];
  }

  // 初始化自定义数据结构（清零）
  memset (&data, 0, sizeof (data));

  // 初始化 GStreamer
  gst_init (&argc, &argv);

  g_print ("Discovering '%s'\n", uri);

  // 1. 创建 GstDiscoverer 实例（超时时间 5 秒）
  data.discoverer = gst_discoverer_new (5 * GST_SECOND, &err);
  if (!data.discoverer) {
    g_print ("Error creating discoverer instance: %s\n", err->message);
    g_clear_error (&err);
    return -1;
  }

  // 2. 连接回调信号：
  // - "discovered"：单个 URI 探测完成触发
  // - "finished"：所有 URI 探测完成触发
  g_signal_connect (data.discoverer, "discovered", G_CALLBACK (on_discovered_cb), &data);
  g_signal_connect (data.discoverer, "finished", G_CALLBACK (on_finished_cb), &data);

  // 3. 启动探测器（此时未开始探测，仅初始化内部线程）
  gst_discoverer_start (data.discoverer);

  // 4. 异步添加探测任务（非阻塞，探测在后台线程执行）
  if (!gst_discoverer_discover_uri_async (data.discoverer, uri)) {
    g_print ("Failed to start discovering URI '%s'\n", uri);
    g_object_unref (data.discoverer);
    return -1;
  }

  // 5. 创建并启动 GLib 主循环（阻塞，等待回调信号）
  data.loop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (data.loop);

  // 6. 探测完成：停止探测器
  gst_discoverer_stop (data.discoverer);

  // 7. 释放所有资源
  g_object_unref (data.discoverer);  // 释放探测器
  g_main_loop_unref (data.loop);     // 释放主循环

  return 0;
}
```

## 三、编译与运行

需安装 GStreamer 核心库、pbutils 工具库（提供编解码器描述），编译命令：

- 依赖说明：
  - `gstreamer-pbutils-1.0`：提供 `gst_pb_utils_get_codec_description()` 等工具函数；
  - `glib-2.0`：提供 GLib 主循环相关 API。

```bash
# 编译
gcc basic-tutorial-9.c -o basic-tutorial-9 `pkg-config --cflags --libs gstreamer-1.0 gstreamer-pbutils-1.0`
```

#### 示例 1：探测默认网络流

```bash
./basic-tutorial-9
```

输出示例（关键信息）：

```bash
Discovering 'https://gstreamer.freedesktop.org/data/media/sintel_trailer-480p.webm'
Discovered 'https://gstreamer.freedesktop.org/data/media/sintel_trailer-480p.webm'

Duration: 0:00:52.250000000
Tags:
  datetime: 2012-04-11T16:08:01Z
  container format: Matroska
  video codec: On2 VP8
  language code: en
  title: Audio
  application name: ffmpeg2theora-0.24
  encoder: Xiph.Org libVorbis I 20090709
  encoder version: 0
  audio codec: Vorbis
  nominal bitrate: 80000
  bitrate: 80000
Seekable: yes

Stream information:
  container: WebM
    audio: Vorbis
      Tags:
        datetime: 2012-04-11T16:08:01Z
        container format: Matroska
        language code: en
        title: Audio
        application name: ffmpeg2theora-0.24
        encoder: Xiph.Org libVorbis I 20090709
        encoder version: 0
        audio codec: Vorbis
        nominal bitrate: 80000
        bitrate: 80000
    video: VP8
      Tags:
        datetime: 2012-04-11T16:08:01Z
        container format: Matroska
        video codec: On2 VP8

Finished discovering
```

#### 示例 2：探测本地文件

```bash
./basic-tutorial-9 file:///home/user/Videos/test.mp4
```

## 四、核心技术亮点
### 1. 异步探测机制
`GstDiscoverer` 采用后台线程执行探测，主线程通过 GLib 主循环阻塞等待回调，避免探测耗时（如网络流加载）阻塞主线程，提升程序响应性。

### 2. 完整的错误处理
覆盖 URI 无效、探测超时、缺少插件、探测错误等多种异常场景，错误信息清晰，便于调试。

### 3. 流拓扑递归解析
支持容器格式（如 MKV、MP4、MOV）的嵌套流解析，确保打印所有音频、视频、字幕流的详细信息。

### 4. 标签序列化兼容
支持任意类型标签（字符串、数字、时间、分辨率等）的序列化打印，兼容性强。

### 5. 人类可读的格式描述
通过 `gst_pb_utils_get_codec_description()` 将底层 Caps（如 `video/x-vp8`）转换为人类可读的描述（如 "VP8 video"），提升输出可读性。

## 五、扩展方向
1. **批量探测**：支持传入多个 URI（命令行参数或文件列表），批量探测并输出汇总结果；
2. **结果导出**：将探测结果导出为 JSON/XML 文件，便于后续处理；
3. **自定义超时**：允许通过命令行参数设置探测超时时间（默认 5 秒）；
4. **过滤流信息**：支持仅打印音频/视频流信息，忽略其他流（如字幕）；
5. **本地文件扫描**：递归扫描目录下所有媒体文件，批量探测并生成文件清单。
