# GStreamer 官方教程 basic-tutorial-1 的完整示例

程序原文地址: <https://gstreamer.freedesktop.org/documentation/tutorials/basic/hello-world.html?gi-language=c>

这段程序做了下面几件事：

1. 初始化 GStreamer 环境
2. 创建一个播放管线（playbin，自动播放所有类型媒体）
3. 播放来自网络的 WebM 视频
4. 等待播放结束或出错
5. 清理释放资源

## 🧩 代码结构拆解
### 1️⃣ 头文件

```C
#include <gst/gst.h>
#ifdef __APPLE__
#include <TargetConditionals.h>
#endif
```

- `gst/gst.h` 是 GStreamer 的主头文件，包含所有核心类型和函数。
- `TargetConditionals.h` 第二个头文件只在 macOS 下使用，用于区分 macOS 和 iOS（iPhone）。

### 2️⃣ 主函数定义

```C
int tutorial_main (int argc, char *argv[])
```

- 主程序逻辑被放在 tutorial_main() 中。
- GStreamer 官方示例这么写是为了在 macOS 上做特殊处理（后面会看到）。

### 3️⃣ 初始化 GStreamer

```C
gst_init (&argc, &argv);
```

这一步必须调用，
它会：

- 初始化 GStreamer 库
- 解析命令行参数（比如 --gst-debug）
- 注册所有已安装的插件（source、decoder、sink 等）

### 4️⃣ 创建播放管线

```C
pipeline = gst_parse_launch(
  "playbin uri=https://gstreamer.freedesktop.org/data/media/sintel_trailer-480p.webm",
  NULL
);
```

这里使用了 playbin 元件：

- playbin 是一个 自动播放器（高级封装）
- 它能自动：
    - 下载/读取媒体文件
    - 解码音视频
    - 自动选择合适的音视频输出设备（sink）

`gst_parse_launch()` 的作用是：

> 从字符串创建完整的 GStreamer 管线。

这里的等价命令行是：

```bash
gst-launch-1.0 playbin uri=https://gstreamer.freedesktop.org/data/media/sintel_trailer-480p.webm
```

### 5️⃣ 设置播放状态

```C
gst_element_set_state(pipeline, GST_STATE_PLAYING);
```

把管线状态改为 **PLAYING（播放中）**

GStreamer 管线状态有四个：

| 状态        | 说明    |
| --------- | ----- |
| `NULL`    | 未初始化  |
| `READY`   | 资源已分配 |
| `PAUSED`  | 暂停播放  |
| `PLAYING` | 正在播放  |

### 6️⃣ 等待消息（错误或结束）

```C
bus = gst_element_get_bus(pipeline);
msg = gst_bus_timed_pop_filtered(
  bus,
  GST_CLOCK_TIME_NONE,
  GST_MESSAGE_ERROR | GST_MESSAGE_EOS
);
```

📦 **Bus（总线）** 是 GStreamer 的消息系统，用于传递事件（错误、结束、状态变化等）。

这段代码的意思是：

- 从管线中拿到 `bus`
- 阻塞等待直到：
    - 播放结束 ( `EOS: End Of Stream` )
    - 出错 ( `ERROR` )

`GST_CLOCK_TIME_NONE` 表示无限等待。

### 7️⃣ 错误处理

```C
if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ERROR) {
  g_printerr("An error occurred! Re-run with the GST_DEBUG=*:WARN ...\n");
}
```

这里简单打印错误提示。
在更完整的版本中，你可以通过 `gst_message_parse_error()` 获取具体错误信息。

### 8️⃣ 清理释放资源

```C
gst_message_unref(msg);
gst_object_unref(bus);
gst_element_set_state(pipeline, GST_STATE_NULL);
gst_object_unref(pipeline);
```

依次：
- 释放消息对象
- 释放总线
- 把管线状态重置为 NULL（停止）
- 释放管线本身

### 9️⃣ main() 的封装

```C
int main (int argc, char *argv[]) {
#if defined(__APPLE__) && TARGET_OS_MAC && !TARGET_OS_IPHONE
  return gst_macos_main((GstMainFunc)tutorial_main, argc, argv, NULL);
#else
  return tutorial_main(argc, argv);
#endif
}
```

这部分是为了 兼容 macOS 平台。

在 macOS 上，如果你想打开窗口播放视频，
必须在主线程启动 Cocoa GUI 事件循环，
所以 GStreamer 提供了一个 `gst_macos_main()` 封装。

在 Linux / Windows 下不需要特殊处理，直接运行 tutorial_main()。

## 🧠 总结执行流程图

```mathematica
main()
 └── tutorial_main()
      ├── gst_init()
      ├── gst_parse_launch("playbin uri=...")
      ├── gst_element_set_state(PLAYING)
      ├── bus = gst_element_get_bus()
      ├── msg = gst_bus_timed_pop_filtered(... ERROR | EOS)
      ├── if ERROR → print message
      ├── 释放资源
      └── return
```

## 🚀 程序运行效果

如果你在有图形界面的系统上运行：

```bash
# 编译
gcc basic-tutorial-1.c -o basic-tutorial-1 `pkg-config --cflags --libs gstreamer-1.0`

# 运行
./basic-tutorial-1
```

你会看到：
- 自动下载并播放《Sintel》预告片
- 视频窗口自动弹出
- 播放完后自动退出

## 注意事项

如果在开发板上运行，可能失败，可改为使用 `fbdevsink` 输出到 Framebuffer。

```C
  /* Build the pipeline */
  pipeline =
      gst_parse_launch
      ("playbin uri=https://gstreamer.freedesktop.org/data/media/sintel_trailer-480p.webm video-sink=fbdevsink",
      NULL);
```

这段是经过修改的

| 环境           | 输出方式                     | 命令示例                           |
| ------------ | ------------------------ | ------------------------------ |
| GUI 桌面（X11）  | autovideosink            | `videotestsrc ! autovideosink` |
| 无桌面 / DRM 驱动 | kmssink                  | `videotestsrc ! kmssink`       |
| Framebuffer  | fbdevsink                | `videotestsrc ! fbdevsink`     |
| 无显示测试        | fakesink                 | `videotestsrc ! fakesink`      |
| SSH + X11 转发 | autovideosink + `ssh -X` | `./basic-tutorial-1`           |
