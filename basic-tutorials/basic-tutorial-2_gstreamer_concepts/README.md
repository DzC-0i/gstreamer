# GStreamer 官方教程 basic-tutorial-2 的完整示例

程序原文地址: <https://gstreamer.freedesktop.org/documentation/tutorials/basic/concepts.html?gi-language=c>

播放一个动态的彩条测试视频

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

### 2️⃣ 主逻辑函数定义

```C
int tutorial_main (int argc, char *argv[])
```

主要逻辑都写在 `tutorial_main()` 中。
真正的 `main()` 在最后面（为兼容 macOS 的 GUI 事件循环）。

### 3️⃣ 初始化 GStreamer

```C
gst_init(&argc, &argv);
```

初始化 GStreamer 库（必须的）。
负责：
- 注册插件
- 初始化线程系统
- 解析命令行参数（如 `--gst-debug`）

### 4️⃣ 创建元素（Element）

```C
source = gst_element_factory_make("videotestsrc", "source");
sink   = gst_element_factory_make("autovideosink", "sink");
```

创建两个 GStreamer 元素：

| 元素名             | 类型     | 功能                             |
| --------------- | ------ | ------------------------------ |
| `videotestsrc`  | Source | 生成彩条测试视频                       |
| `autovideosink` | Sink   | 自动选择一个视频输出设备（X11、Wayland、DRM等） |

`gst_element_factory_make(factory_name, instance_name)`

从插件工厂中创建一个元素实例。

### 5️⃣ 创建管线（Pipeline）

```C
pipeline = gst_pipeline_new("test-pipeline");
```

创建一个空的管线容器（Pipeline）。

GStreamer 的数据流总是从 source → filter → sink 经过 pipeline。

### 6️⃣ 检查创建是否成功

```C
if (!pipeline || !source || !sink) {
  g_printerr("Not all elements could be created.\n");
  return -1;
}
```

如果插件没安装或出错，这里能检测出来。

### 7️⃣ 把元素添加进管线

```C
gst_bin_add_many(GST_BIN(pipeline), source, sink, NULL);
```

GStreamer 的管线是一个特殊的 `Bin`（容器）。

这里把 source 和 sink 添加进去。

### 8️⃣ 链接元素（建立数据通路）

```C
if (gst_element_link(source, sink) != TRUE) {
  g_printerr("Elements could not be linked.\n");
  gst_object_unref(pipeline);
  return -1;
}
```

连接 source → sink。

就像命令行里 `videotestsrc ! autovideosink`。

如果格式不匹配（caps negotiation 失败），会返回 FALSE。

### 9️⃣ 配置源属性

```C
g_object_set(source, "pattern", 0, NULL);
```

修改 videotestsrc 的属性：

| 属性        | 含义     | 值                |
| --------- | ------ | ---------------- |
| `pattern` | 彩条图案样式 | 0 = SMPTE 彩条（默认） |


其它可用 pattern 值：
- 0 = smpte
- 1 = snow
- 2 = black
- 3 = white
- 4 = red
- 18 = ball
可在命令行查看：

```bash
gst-inspect-1.0 videotestsrc
```

### 🔟 设置播放状态

```C
ret = gst_element_set_state(pipeline, GST_STATE_PLAYING);
```

让整个管线开始播放。
内部会自动：
- 打开设备
- 创建线程
- 开始数据流动

返回值 `ret` 会告诉你是否成功：
- `GST_STATE_CHANGE_SUCCESS`
- `GST_STATE_CHANGE_FAILURE`
- `GST_STATE_CHANGE_ASYNC`

把管线状态改为 **PLAYING（播放中）**

GStreamer 管线状态有四个：

| 状态        | 说明    |
| --------- | ----- |
| `NULL`    | 未初始化  |
| `READY`   | 资源已分配 |
| `PAUSED`  | 暂停播放  |
| `PLAYING` | 正在播放  |

### 11 等待消息（错误或结束）

```C
bus = gst_element_get_bus(pipeline);
msg = gst_bus_timed_pop_filtered(bus, GST_CLOCK_TIME_NONE,
          GST_MESSAGE_ERROR | GST_MESSAGE_EOS);
```

GStreamer 使用 **Bus（消息总线）** 来通知应用层：
- 错误 (GST_MESSAGE_ERROR)
- 结束 (GST_MESSAGE_EOS)
- 状态变化
- 警告等等

这行代码会 **阻塞等待**，直到出现错误或播放结束。

### 12 解析消息

```C
if (msg != NULL) {
  GError *err;
  gchar *debug_info;

  switch (GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_ERROR:
      gst_message_parse_error(msg, &err, &debug_info);
      g_printerr("Error from %s: %s\n", GST_OBJECT_NAME(msg->src), err->message);
      g_printerr("Debug info: %s\n", debug_info ? debug_info : "none");
      g_clear_error(&err);
      g_free(debug_info);
      break;

    case GST_MESSAGE_EOS:
      g_print("End-Of-Stream reached.\n");
      break;

    default:
      g_printerr("Unexpected message received.\n");
      break;
  }
}
```

这部分就是对消息的分析打印：
- 如果是错误 → 打印详细信息
- 如果是结束 → 打印结束提示
- 否则打印意外消息（理论上不会到这里）

### 13 释放资源

```C
gst_object_unref(bus);
gst_element_set_state(pipeline, GST_STATE_NULL);
gst_object_unref(pipeline);
```

把状态恢复为 `NULL` 并释放对象。

否则 GStreamer 会保留线程和句柄。

### 14 main() 函数封装

```C
int main (int argc, char *argv[]) {
#if defined(__APPLE__) && TARGET_OS_MAC && !TARGET_OS_IPHONE
  return gst_macos_main ((GstMainFunc) tutorial_main, argc, argv, NULL);
#else
  return tutorial_main (argc, argv);
#endif
}
```

和前一个例子一样，macOS 特殊处理。

Linux / Windows 下直接运行 `tutorial_main()`。

## 🧠 程序执行流程图

```mathematica
main()
 └── tutorial_main()
      ├── gst_init()
      ├── source = videotestsrc
      ├── sink = autovideosink
      ├── pipeline = gst_pipeline_new()
      ├── gst_bin_add_many(source, sink)
      ├── gst_element_link(source, sink)
      ├── g_object_set(source, "pattern", 0)
      ├── gst_element_set_state(PLAYING)
      ├── bus = gst_element_get_bus()
      ├── msg = gst_bus_timed_pop_filtered(... ERROR | EOS)
      ├── 打印消息 / 错误
      ├── gst_element_set_state(NULL)
      └── 释放资源
```

## 🚀 程序运行效果

```bash
# 编译
gcc basic-tutorial-2.c -o basic-tutorial-2 `pkg-config --cflags --libs gstreamer-1.0`

# 运行
./basic-tutorial-2
```

运行后程序会：
- 打开一个视频窗口（自动选择 sink）
- 播放动态彩条测试视频
- 当你按 Ctrl+C 或窗口关闭后退出

## 注意事项

如果在开发板上运行，`autovideosink` 可能失败

可以改为：(其他更改参数参考 [basic-tutorial-1](../basic-tutorial-1/README.md#注意事项 "注意事项"))

```C
sink = gst_element_factory_make("kmssink", "sink");
```

或
```C
sink = gst_element_factory_make("fbdevsink", "sink");
```
