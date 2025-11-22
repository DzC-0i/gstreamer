# GStreamer 实用元素

原文地址: <https://gstreamer.freedesktop.org/documentation/tutorials/basic/handy-elements.html?gi-language=c>

只是翻译了一下

本教程列出了值得了解的实用 GStreamer 元素。它们从强大的全功能元素（如 `playbin` ），允许您轻松构建复杂管道，到在调试时极其有用的小型辅助元素。

以下示例使用 `gst-launch-1.0` 工具（在《基本教程 10：GStreamer 工具》中了解它）。如果您想查看正在协商的 `Pad Caps`，请使用 `-v` 命令行参数。

## Bins 容器
### `playbin`

这个元素在整个教程中已被广泛使用。它管理媒体播放的所有方面，从源到显示，经过解复用和解码。它非常灵活，拥有许多选项，因此有一整套教程专门介绍它。请参阅播放教程了解更多详情。

### `uridecodebin`

这个元素是一个非常方便的元素，用于从 `URI` 解复用和解码媒体。它自动处理解复用和解码，根据 `URI` 确定正确的插件。您可以将 `uridecodebin` 元素添加到您的管道中，而无需手动配置解复用器和解码器。

```bash
gst-launch-1.0 uridecodebin uri=https://gstreamer.freedesktop.org/data/media/sintel_trailer-480p.webm ! videoconvert ! autovideosink
```

```bash
gst-launch-1.0 uridecodebin uri=https://gstreamer.freedesktop.org/data/media/sintel_trailer-480p.webm ! audioconvert ! autoaudiosink
```

### `decodebin`

这个元素会自动使用可用的解码器和解复用器通过自动插接构建解码管道，直到获得原始媒体为止。它被 `uridecodebin` 内部使用，通常更方便，因为它也会创建合适的源元素。它取代了旧的 `decodebin` 元素。它像解复用器一样工作，因此它会提供与媒体中发现的流数一样多的源垫。

```bash
gst-launch-1.0 souphttpsrc location=https://gstreamer.freedesktop.org/data/media/sintel_trailer-480p.webm ! decodebin ! autovideosink
```

## File input/output 文件输入/输出
### `filesrc`

该元素读取本地文件并生成带有 `ANY` 大写的媒体。如果您想获得媒体的正确大写，可以通过使用 `typefind` 元素或设置 `filesrc` 的 `typefind` 属性为 `TRUE` 来探索流。

```bash
gst-launch-1.0 filesrc location=e:/VMware/Share/gstreamer/basic-tutorial-14_handy_elements/sintel_trailer-480p.webm ! decodebin ! autovideosink

gst-launch-1.0 filesrc location=e:/VMware/Share/gstreamer/basic-tutorial-14_handy_elements/sintel_trailer-480p.webm ! matroskademux ! queue ! vp8dec ! autovideosink
```

matroskademux 是 WebM 专用解复用插件（WebM 基于 Matroska 容器），vp8dec 是 VP8 视频解码器，直接指定插件可避免 decodebin 自动匹配失败。

```bash
gst-launch-1.0 playbin uri=file:///e:/VMware/Share/gstreamer/basic-tutorial-14_handy_elements/sintel_trailer-480p.webm
```

### `filesink`

该元素将接收到的所有媒体写入文件。使用 `location` 属性指定文件名。

```bash
gst-launch-1.0 audiotestsrc ! vorbisenc ! oggmux ! filesink location=test.ogg
```

## Network  网络
### `souphttpsrc`

这个元素通过 HTTP 使用 libsoup 库在网络上接收客户端数据。通过 `location` 属性设置要检索的 `URL`。

```bash
gst-launch-1.0 souphttpsrc location=https://gstreamer.freedesktop.org/data/media/sintel_trailer-480p.webm ! decodebin ! autovideosink
```

## Test media generation  测试媒体生成
### `videotestsrc`

这个元素产生一个视频图案（可通过 `pattern` 属性在多种不同选项中选择）。用它来测试视频管道。

```bash
gst-launch-1.0 videotestsrc pattern=ball ! autovideosink

gst-launch-1.0 videotestsrc pattern=ball ! videoconvert ! autovideosink
```

### `audiotestsrc`

这个元素产生一个音频波形（可通过 `wave` 属性在多种不同选项中选择）。用它来测试音频管道。

```bash
gst-launch-1.0 audiotestsrc wave=white-noise ! autoaudiosink

gst-launch-1.0 audiotestsrc ! audioconvert ! autoaudiosink
```

## Video adapters  视频适配器
### `videoconvert`

这个元素将一种颜色空间（例如 RGB）转换为另一种颜色空间（例如 YUV）。它也可以在不同的 YUV 格式（例如 I420、NV12、YUY2 等）之间或 RGB 格式排列（例如 RGBA、ARGB、BGRA 等）之间进行转换。

在解决协商问题时，这通常是你的首选。当不需要时，因为它的上游和下游元素已经可以相互理解，它会以透传模式运行，对性能影响最小。

一般来说，当你在设计时不知道元素的大写字母，比如 `autovideosink` ，或者元素可能因外部因素而变化时，比如解码用户提供的文件，始终使用 `videoconvert` 。

```bash
gst-launch-1.0 videotestsrc ! videoconvert ! autovideosink
```

### `videorate`

这个元素会改变视频的帧率。它可以用于将视频从一个帧率转换为另一个帧率，或者用于调整视频的播放速度。(该元素接收一个带时间戳的视频帧流，并产生一个与源垫帧率匹配的流。校正是通过丢弃和复制帧来完成的，没有使用任何复杂的算法来插帧。)

这有助于允许需要不同帧率的元素进行连接。与其他适配器一样，如果不需要（因为两个垫都可以就帧率达成一致），它将以透传模式运行，不会影响性能。

因此，当设计时实际帧率未知时，始终使用它是个好主意，以防万一。

```bash
gst-launch-1.0 videotestsrc ! video/x-raw,framerate=30/1 ! videorate ! video/x-raw,framerate=1/1 ! videoconvert ! autovideosink
```

### `videoscale`

这个元素调整视频帧的大小。默认情况下，该元素尝试在源和接收端 Pad 上协商相同的大小，因此无需缩放。如果没有缩放需求，将此元素插入管道中是安全的，可以获得更稳健的行为而无需任何成本。

该元素支持广泛的颜色空间，包括各种 YUV 和 RGB 格式，因此通常能够在管道的任何位置工作。

如果视频要输出到由用户控制大小的窗口，最好使用 `videoscale` 元素，因为并非所有视频接收端都能够执行缩放操作。

```bash
gst-launch-1.0 uridecodebin uri=https://gstreamer.freedesktop.org/data/media/sintel_trailer-480p.webm ! videoscale ! video/x-raw,width=178,height=100 ! videoconvert ! autovideosink
```

## Audio adapters  音频适配器
### `audioconvert`

这个元素可以在各种可能的格式之间转换原始音频缓冲区。它支持整数到浮点数的转换、宽度/深度转换、有符号和无符号转换以及声道转换。

就像 `videoconvert` 对视频所做的那样，你用它来解决音频的协商问题，并且通常可以放心地广泛使用它，因为这个元素在不需要时什么也不做。

```bash
gst-launch-1.0 audiotestsrc ! audioconvert ! autoaudiosink
```

### `audioresample`

这个元素使用可配置的窗函数将原始音频缓冲区重采样到不同的采样率，以增强质量。

同样，用它来解决关于采样率的协商问题，并且不要害怕慷慨地使用它。

```bash
gst-launch-1.0 uridecodebin uri=https://gstreamer.freedesktop.org/data/media/sintel_trailer-480p.webm ! audioresample ! audio/x-raw,rate=4000 ! audioconvert ! autoaudiosink
```

### `audiorate`

这个元素接收一个带时间戳的原始音频帧流，并通过插入或删除样本来生成一个完美的流。它不允许像 `videorate` 那样更改采样率，只是填补空白并删除重叠的样本，以确保输出流是连续且“干净”的。

在时间戳可能会丢失的情况下（例如存储到某些文件格式中）以及接收器需要所有样本都存在的情况下，它很有用。由于难以举例说明，因此没有给出示例。

> 注意：大多数情况下， audiorate 并不是你想要的。

## Multithreading  多线程
### `queue`

队列已在《基础教程 7：多线程和 `Pad` 可用性》中解释。基本上，队列执行两个任务：
- 数据会排队，直到达到选定的限制。任何试图将更多缓冲区推入队列的操作都会阻塞推送线程，直到有更多空间可用。
- 队列会在源 Pad 上创建一个新线程，以解耦 `sink` 和 `source Pad` 上的处理。

此外， `queue` 会在它即将变为空或满时触发信号（根据一些可配置的阈值），并且可以在它满时被指示丢弃缓冲区而不是阻塞

一般来说，只要网络缓冲不是你的问题，就优先选择更简单的 `queue` 元素而不是 `queue2` 。参见《基础教程 7：多线程和 Pad 可用性》的示例。

### `queue2`

这个元素不是 queue 的演进版本。它具有相同的设计目标，但遵循了不同的实现方法，导致功能有所不同。不幸的是，通常很难判断哪个队列是最佳选择。

`queue2` 执行上述两个任务针对 `queue` ，并且，此外，还能够将接收到的数据（或部分数据）存储在磁盘文件中，以便后续检索。它还用 Basic tutorial 12: Streaming 中描述的更通用和方便的缓冲消息替换了信号。

一般来说，如果**网络缓冲是一个问题**，优先选择 `queue2` 而不是 `queue` 。参见 `Basic tutorial 12: Streaming` 的示例（ queue2 隐藏在 playbin 中）。

### `multiqueue`

这个元素同时为多个流提供队列，并通过允许某些队列在其它流没有接收数据时增长，或允许某些队列在没有连接到任何地方时丢弃数据（而不是像简单队列那样返回错误）来简化它们的管理。此外，它同步不同的流，确保它们不会彼此过于超前。

这是一个高级元素。它位于 `decodebin` 内部，但在普通的播放应用中，你很少需要自己实例化它。

### `tee`

基本教程 7：多线程和 Pad 可用性已经展示了如何使用一个 `tee` 元素，该元素将数据分割到多个 Pad。分割数据流在例如捕获视频时很有用，其中视频显示在屏幕上，同时被编码并写入文件。另一个例子是播放音乐并连接一个可视化模块。

需要在每个分支中使用单独的 `queue` 元素来为每个分支提供独立的线程。否则，一个分支中的阻塞数据流会停滞其他分支。

```bash
st-launch-1.0 audiotestsrc ! tee name=t ! queue ! audioconvert ! autoaudiosink t. ! queue ! wavescope ! videoconvert ! autovideosink
```

## Capabilities  功能
### `capsfilter`

基本教程 10：GStreamer 工具已经解释了如何使用 Caps 过滤器与 `gst-launch-1.0` 。在程序化构建管道时，Caps 过滤器通过 `capsfilter` 元素实现。该元素本身不修改数据，但会强制执行数据格式的限制。

```bash
gst-launch-1.0 videotestsrc ! video/x-raw, format=GRAY8 ! videoconvert ! autovideosink
```

### `typefind`

该元素确定流中包含的媒体类型。它按类型查找函数的排名顺序应用类型查找函数。一旦检测到类型，它就会将其源 Pad Caps 设置为检测到的媒体类型，并发出 `have-type` 信号。

它由 `decodebin` 内部实例化，你也可以使用它来查找媒体类型，尽管通常可以使用 `GstDiscoverer` ，它提供更多信息（如《基础教程 9：媒体信息收集》中所示）。

## Debugging  调试
### `fakesink`

这个接收元素会简单地吞掉任何输入它的数据。在调试时，用它替换正常的接收元素并排除它们的作用很有用。当与 `gst-launch-1.0` 的 `-v` 开关结合使用时，它会非常冗长，因此使用 `silent` 属性来移除任何不必要的声音。

```bash
gst-launch-1.0 audiotestsrc num-buffers=1000 ! fakesink sync=false
```

### `identity`

这是一个虚拟元素，它将传入的数据未经修改地传递出去。它具有多种有用的诊断功能，例如偏移量和时间戳检查，或缓冲区丢弃。阅读它的文档，了解这个看似无害的元素能做什么。

```bash
gst-launch-1.0 audiotestsrc ! identity drop-probability=0.1 ! audioconvert ! autoaudiosink
```
