# Gstreamer 平台特定元素

原文地址: <https://gstreamer.freedesktop.org/documentation/tutorials/basic/platform-specific-elements.html?gi-language=c>

只是翻译了一下，以及去掉一些不重要的

尽管 GStreamer 是一个跨平台框架，但并非所有元素在所有平台上都可用。 使用 playbin 或 autovideosink 等元素时，你通常不需要担心这一点，但对于需要使用仅在特定平台上可用的输出端的情况，你需要注意这一点。

## Cross Platform  跨平台
### `glimagesink`

该视频输出端基于 `OpenGL` 或 `OpenGL ES`。它支持缩放图像的重新缩放和滤波，以减轻混叠。它实现了 `VideoOverlay` 接口，因此视频窗口可以被重新父化（嵌入在其他窗口中）。这是大多数平台（除 Windows 外）推荐的视频输出端（在 Windows 上推荐 `d3d11videosink` ）。特别是在 Android 和 iOS 上，它是唯一可用的视频输出端。它可以分解为 `glupload ! glcolorconvert ! glimagesinkelement` 以将更多的 OpenGL 硬件加速处理插入到管道中。

## Linux 平台
### `ximagesink`

一个标准的基于 X 的仅 RGB 视频接收器。它实现了 `VideoOverlay` 接口，因此视频窗口可以被重新父化（嵌入在其他窗口中）。它不支持缩放或 RGB 以外的颜色格式；这必须通过不同方式完成（例如使用 `videoscale` 元素）。

### `xvimagesink`

一个基于 X 的视频接收器，使用 X 视频扩展（Xv）。它实现了 VideoOverlay 接口，因此视频窗口可以被重新父化（嵌入在其他窗口中）。它可以在 GPU 上高效地执行缩放。只有在硬件和相应驱动程序支持 Xv 扩展时才可用。

### `alsasink`

这个音频接收器通过高级 Linux 声音架构（ALSA）输出到声卡。这个接收器在几乎所有的 Linux 平台上都可用。它通常被视为声卡的“低级”接口，配置可能比较复杂（参见播放教程 9：数字音频直通）。

### `pulsesink`

这个接收器向 PulseAudio 服务器播放音频。它比 ALSA 更高层次的声卡抽象，因此使用更简单，并提供更多高级功能。不过它在一些较旧的 Linux 发行版上可能不稳定。

## Mac OS X
### `osxvideosink`

这是 Mac OS X 上 GStreamer 可用的视频接收器。也可以使用 glimagesink 通过 OpenGL 进行绘制。

### `osxaudiosink`

这是 Mac OS X 上 GStreamer 唯一可用的音频接收器。

## Windows 平台
### `d3d11videosink`

这个视频接收器基于 Direct3D11，是 Windows 上推荐使用的元素。它支持 VideoOverlay 接口，并以零拷贝方式进行缩放/色彩空间转换。这是 Windows 上性能最高、功能最丰富的视频接收器元素。

### `d3dvideosink`

这个视频接收器基于 Direct3D9。它支持对缩放图像进行重新缩放和过滤以减轻锯齿。它实现了 VideoOverlay 接口，因此视频窗口可以被重新父化（嵌入在其他窗口中）。不建议将此元素用于针对 Windows 8 或更新版本的应用程序。

## `dshowvideosink (deprecated)`

这个视频接收器基于 Direct Show。它可以使用不同的渲染后端，如 EVR、VMR9 或 VMR7，其中 EVR 仅在 Windows Vista 或更新版本上可用。它支持对缩放图像进行重新缩放和过滤以减轻锯齿。它实现了 VideoOverlay 接口，因此视频窗口可以被重新父化（嵌入在其他窗口中）。在大多数情况下不建议使用此元素。

### `wasapisink` and `wasapi2sink`   `wasapisink` 和 `wasapi2sink` 

这些元素是 Windows 上的默认音频接收器元素，基于 WASAPI，该接口在 Vista 或更新版本上可用。请注意， `wasapi2sink` 是 `wasapisink` 和 `wasapi2sink` 的替代品，而 `wasapi2sink` 是 Windows 8 或更新版本的默认值。否则， `wasapisink` 将是默认的音频接收器元素。

### `directsoundsink (deprecated)`

这个音频输出元素基于 DirectSound，在所有 Windows 版本中可用。

## 其他平台(不做翻译搬运)
