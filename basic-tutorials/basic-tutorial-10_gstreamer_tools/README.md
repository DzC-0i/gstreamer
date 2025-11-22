# GStreamer 工具

原文地址: <https://gstreamer.freedesktop.org/documentation/tutorials/basic/gstreamer-tools.html?gi-language=c>

只是翻译了一下

- 从命令行构建和运行 GStreamer 管道
- 查找你拥有的 GStreamer 元素及其功能。
- 发现媒体文件的内部结构。

## gst-launch-1.0

这个工具接受一个管道的文本描述，实例化它，并将其设置为 PLAYING 状态。它允许你在使用 GStreamer API 调用进行实际实现之前，快速检查给定的管道是否工作。

请记住，它只能创建简单的管道。特别是，它只能模拟管道与应用程序的交互达到一定水平。无论如何，快速测试管道非常方便，并且被全球的 GStreamer 开发者每天使用。

请注意， gst-launch-1.0 主要是开发者的调试工具。你不应该在其上构建应用程序。相反，应使用 GStreamer API 的 gst_parse_launch() 功能，作为从管道描述中构建管道的便捷方式。

尽管构建管道描述的规则非常简单，但多个元素的组合很快会使这些描述看起来像黑魔法。不必害怕，因为最终每个人都会学会 gst-launch-1.0 语法。

gst-launch-1.0 的命令行由一系列选项后跟 PIPELINE-DESCRIPTION 组成。接下来给出一些简化说明，请参考 gst-launch-1.0 的参考页面查看完整文档。

### Elements  元素

在简单形式中，PIPELINE-DESCRIPTION 是一系列用感叹号(!)分隔的元素类型。现在请输入以下命令：

```bash
gst-launch-1.0 videotestsrc ! videoconvert ! autovideosink
```

你应该看到一个带有动画视频图案的窗口。在终端上使用 CTRL+C 来停止程序。

这实例化了一个类型为 videotestsrc 的新元素（一个生成样本视频图案的元素）、一个 videoconvert （一个执行原始视频格式转换的元素，确保其他元素能够相互理解），以及一个 autovideosink （一个用于渲染视频的窗口）。然后，GStreamer 尝试将每个元素的输出链接到描述中其右侧元素的输入。如果有多个输入或输出 Pad 可用，则使用 Pad Caps 来找到两个兼容的 Pad。

### Properties  属性

可以将属性附加到元素上，形式为*property=value*（可以指定多个属性，用空格分隔）。使用 gst-inspect-1.0 工具（下一节将解释）来查找元素的可用属性。

```bash
gst-launch-1.0 videotestsrc pattern=11 ! videoconvert ! autovideosink
```

你应该看到一个由圆圈组成的静态视频图案。

### Named elements  命名的元素

元素可以使用 name 属性进行命名，通过这种方式可以创建包含分支的复杂管道。命名允许链接到描述中先前创建的元素，并且对于使用具有多个输出垫的元素（例如解复用器或 tee）来说不可或缺。

命名的元素使用其名称后跟一个点来引用。

```bash
gst-launch-1.0 videotestsrc ! videoconvert ! tee name=t ! queue ! autovideosink t. ! queue ! autovideosink
```

你应该看到两个视频窗口，显示相同的样本视频模式。如果你只看到一个，尝试移动它，因为它可能位于第二个窗口之上。

这个例子实例化了一个 videotestsrc ，连接到一个 videoconvert ，再连接到一个 tee （记得在《基础教程 7：多线程和 Pad 可用性》中提到，一个 tee 会将其输入 Pad 接收到的所有内容复制到每个输出 Pad）。这个 tee 被简单地命名为‘t’（使用 name 属性），然后连接到一个 queue 和一个 autovideosink 。相同的 tee 使用‘t.’（注意点号）来引用，然后连接到第二个 queue 和第二个 autovideosink 。

要了解为什么需要队列，请阅读《基础教程 7：多线程和 Pad 可用性》。

### Pads  接口

与其让 GStreamer 在连接两个元素时选择使用哪个 Pad，你可能希望直接指定 Pad。你可以通过在元素名称后加上点号和 Pad 名称来实现（它必须是一个命名的元素）。使用 gst-inspect-1.0 工具来学习一个元素的 Pad 名称。

例如，当你想要从解复用器中获取一个特定的流时，这很有用：

```bash
gst-launch-1.0 souphttpsrc location=https://gstreamer.freedesktop.org/data/media/sintel_trailer-480p.webm ! matroskademux name=d d.video_0 ! matroskamux ! filesink location=sintel_video.mkv
```

使用 souphttpsrc 从互联网获取媒体文件，该文件为 webm 格式（一种特殊的 Matroska 容器，参见《基础教程 2：GStreamer 概念》）。然后我们使用 matroskademux 打开该容器。这个媒体包含音频和视频，因此 matroskademux 将创建两个输出 Pad，分别命名为 video_0 和 audio_0 。我们将 video_0 连接到一个 matroskamux 元素，以将视频流重新打包到新的容器中，最后将其连接到 filesink ，该元素会将流写入名为"sintel_video.mkv"的文件（ location 属性指定了文件名）。

总的来说，我们从一个 webm 文件中提取了视频，生成了一个新的 matroska 文件。如果我们只想保留音频：

```bash
gst-launch-1.0 souphttpsrc location=https://gstreamer.freedesktop.org/data/media/sintel_trailer-480p.webm ! matroskademux name=d d.audio_0 ! vorbisparse ! matroskamux ! filesink location=sintel_audio.mka
```

vorbisparse 元素用于从流中提取一些信息并将其放入 Pad Caps 中，以便下一个元素 matroskamux 知道如何处理该流。对于视频来说，这不是必要的，因为 matroskademux 已经提取了这些信息并将其添加到了 Caps 中。

请注意，在上述两个示例中，没有进行媒体解码或播放。我们只是从一个容器移动到另一个容器（再次进行解复用和复用）。

### Caps filters  Caps 过滤器

当一个元素有多个输出垫时，可能会出现链接到下一个元素不明确的情况：下一个元素可能有多个兼容的输入垫，或者其输入垫可能与所有输出垫的 Pad Caps 都兼容。在这些情况下，GStreamer 将使用第一个可用的垫进行链接，这基本上相当于说 GStreamer 会随机选择一个输出垫。

考虑以下管道：

```bash
gst-launch-1.0 souphttpsrc location=https://gstreamer.freedesktop.org/data/media/sintel_trailer-480p.webm ! matroskademux ! filesink location=test
```

这与前一个示例中的相同媒体文件和解复用器。 filesink 的输入 Pad Caps 是 ANY ，这意味着它可以接受任何类型的媒体。 matroskademux 的两个输出垫中，哪一个将与 filesink 链接？ video_0 还是 audio_0 ？你无法知道。

你可以通过使用命名 pad（如前一节所述）或使用 Caps Filters 来消除这种歧义：
```bash
gst-launch-1.0 souphttpsrc location=https://gstreamer.freedesktop.org/data/media/sintel_trailer-480p.webm ! matroskademux ! video/x-vp8 ! matroskamux ! filesink location=sintel_video.mkv
```

Caps Filter 表现得像一个透传元素，它什么也不做，只接受给定 Caps 的媒体，从而有效解决歧义。在这个例子中，在 matroskademux 和 matroskamux 之间我们添加了一个 video/x-vp8 Caps Filter 来指定我们对 matroskademux 的输出 pad 感兴趣，这个 pad 可以产生这种类型的视频。

要找出一个元素接受和产生的 Caps，使用 gst-inspect-1.0 工具。要找出一个特定文件中包含的 Caps，使用 gst-discoverer-1.0 工具。要找出一个元素为特定管道产生的 Caps，像往常一样运行 gst-launch-1.0 ，并使用 –v 选项来打印 Caps 信息。

### Examples  示例

使用 playbin 播放媒体文件（如《基础教程 1：你好世界！》所示）：

```bash
gst-launch-1.0 playbin uri=https://gstreamer.freedesktop.org/data/media/sintel_trailer-480p.webm
```

一个完整的播放流程，包含音频和视频（大致与 playbin 内部创建的流程相同）：

```bash
gst-launch-1.0 souphttpsrc location=https://gstreamer.freedesktop.org/data/media/sintel_trailer-480p.webm ! matroskademux name=d ! queue ! vp8dec ! videoconvert ! autovideosink d. ! queue ! vorbisdec ! audioconvert ! audioresample ! autoaudiosink
```

一个转码流程，打开 webm 容器并解码两个流（通过 uridecodebin），然后使用不同的编解码器（H.264 + AAC）重新编码音频和视频分支，并将它们重新组合到 MP4 容器中（纯属演示）。由于 x264enc 编码器默认行为（在输出任何内容前会消耗数秒钟的视频输入），我们需要增加音频分支中的队列大小以确保流程可以预滚并启动。另一个解决方案是使用 x264enc tune=zerolatency ，但这会导致质量降低，更适合直播场景。

```bash
gst-launch-1.0 uridecodebin uri=https://gstreamer.freedesktop.org/data/media/sintel_trailer-480p.webm name=d ! queue ! videoconvert ! x264enc ! video/x-h264,profile=high ! mp4mux name=m ! filesink location=sintel.mp4 d. ! queue max-size-time=5000000000 max-size-bytes=0 max-size-buffers=0 ! audioconvert ! audioresample ! voaacenc ! m.
```

一个缩放流程。 videoscale 元素在输入和输出 caps 大小不同时会执行缩放操作。输出 caps 由 Caps Filter 设置为 320x200。

```bash
gst-launch-1.0 uridecodebin uri=https://gstreamer.freedesktop.org/data/media/sintel_trailer-480p.webm ! queue ! videoscale ! video/x-raw,width=320,height=200 ! videoconvert ! autovideosink
```

这个关于 gst-launch-1.0 的简短描述应该足以让你开始使用。请记住，完整的文档在这里可以查阅。

## gst-inspect-1.0

这个工具有三种操作模式：

如果没有参数，它会列出所有可用的元素类型，也就是说，你可以用来实例化新元素的类型。

如果带有一个文件名作为参数，它会将文件视为一个 GStreamer 插件，尝试打开它，并列出其中描述的所有元素。

以 GStreamer 元素名称作为参数，它会列出与该元素相关的所有信息。

让我们看看第三种模式的示例：

```bash
gst-inspect-1.0 vp8dec

Factory Details:
  Rank                     primary (256)
  Long-name                On2 VP8 Decoder
  Klass                    Codec/Decoder/Video
  Description              Decode VP8 video streams
  Author                   David Schleef <ds@entropywave.com>, Sebastian Dröge <sebastian.droege@collabora.co.uk>

Plugin Details:
  Name                     vpx
  Description              VP8 plugin
  Filename                 /usr/lib64/gstreamer-1.0/libgstvpx.so
  Version                  1.6.4
  License                  LGPL
  Source module            gst-plugins-good
  Source release date      2016-04-14
  Binary package           Fedora GStreamer-plugins-good package
  Origin URL               http://download.fedoraproject.org

GObject
 +----GInitiallyUnowned
       +----GstObject
             +----GstElement
                   +----GstVideoDecoder
                         +----GstVP8Dec

Pad Templates:
  SINK template: 'sink'
    Availability: Always
    Capabilities:
      video/x-vp8

  SRC template: 'src'
    Availability: Always
    Capabilities:
      video/x-raw
                 format: I420
                  width: [ 1, 2147483647 ]
                 height: [ 1, 2147483647 ]
              framerate: [ 0/1, 2147483647/1 ]


Element Flags:
  no flags set

Element Implementation:
  Has change_state() function: gst_video_decoder_change_state

Element has no clocking capabilities.
Element has no URI handling capabilities.

Pads:
  SINK: 'sink'
    Pad Template: 'sink'
  SRC: 'src'
    Pad Template: 'src'

Element Properties:
  name                : The name of the object
                        flags: readable, writable
                        String. Default: "vp8dec0"
  parent              : The parent of the object
                        flags: readable, writable
                        Object of type "GstObject"
  post-processing     : Enable post processing
                        flags: readable, writable
                        Boolean. Default: false
  post-processing-flags: Flags to control post processing
                        flags: readable, writable
                        Flags "GstVP8DecPostProcessingFlags" Default: 0x00000403, "mfqe+demacroblock+deblock"
                           (0x00000001): deblock          - Deblock
                           (0x00000002): demacroblock     - Demacroblock
                           (0x00000004): addnoise         - Add noise
                           (0x00000400): mfqe             - Multi-frame quality enhancement
  deblocking-level    : Deblocking level
                        flags: readable, writable
                        Unsigned Integer. Range: 0 - 16 Default: 4
  noise-level         : Noise level
                        flags: readable, writable
                        Unsigned Integer. Range: 0 - 16 Default: 0
  threads             : Maximum number of decoding threads
                        flags: readable, writable
                        Unsigned Integer. Range: 1 - 16 Default: 0
```

最相关的部分是：

Pad 模板：这列出了该元素可以拥有的所有 Pad 类型及其功能。这是你用来判断一个元素是否可以与其他元素连接的地方。在此情况下，它只有一个接收 Pad 模板，只接受 video/x-vp8 （VP8 格式的编码视频数据），以及一个源 Pad 模板，只产生 video/x-raw （解码视频数据）。

元素属性：这列出了元素的属性、类型和接受的值。

如需更多信息，可以查看 gst-inspect-1.0 的文档页面。

## gst-discoverer-1.0

这个工具是 Basic tutorial 9: 媒体信息收集中所示 GstDiscoverer 对象的一个包装器。它接受命令行中的 URI，并打印出 GStreamer 可以提取的所有媒体信息。它有助于找出用于生成媒体的容器和编解码器，从而确定在播放媒体时需要在管道中添加哪些元素。

使用 gst-discoverer-1.0 --help 获取可用选项列表，这些选项基本上控制输出的详细程度。

让我们看一个例子：

```bash
gst-discoverer-1.0 https://gstreamer.freedesktop.org/data/media/sintel_trailer-480p.webm -v

Analyzing https://gstreamer.freedesktop.org/data/media/sintel_trailer-480p.webm
Done discovering https://gstreamer.freedesktop.org/data/media/sintel_trailer-480p.webm
Topology:
  container: video/webm
    audio: audio/x-vorbis, channels=(int)2, rate=(int)48000
      Codec:
        audio/x-vorbis, channels=(int)2, rate=(int)48000
      Additional info:
        None
      Language: en
      Channels: 2
      Sample rate: 48000
      Depth: 0
      Bitrate: 80000
      Max bitrate: 0
      Tags:
        taglist, language-code=(string)en, container-format=(string)Matroska, audio-codec=(string)Vorbis, application-name=(string)ffmpeg2theora-0.24, encoder=(string)"Xiph.Org\ libVorbis\ I\ 20090709", encoder-version=(uint)0, nominal-bitrate=(uint)80000, bitrate=(uint)80000;
    video: video/x-vp8, width=(int)854, height=(int)480, framerate=(fraction)25/1
      Codec:
        video/x-vp8, width=(int)854, height=(int)480, framerate=(fraction)25/1
      Additional info:
        None
      Width: 854
      Height: 480
      Depth: 0
      Frame rate: 25/1
      Pixel aspect ratio: 1/1
      Interlaced: false
      Bitrate: 0
      Max bitrate: 0
      Tags:
        taglist, video-codec=(string)"VP8\ video", container-format=(string)Matroska;

Properties:
  Duration: 0:00:52.250000000
  Seekable: yes
  Tags:
      video codec: VP8 video
      language code: en
      container format: Matroska
      application name: ffmpeg2theora-0.24
      encoder: Xiph.Org libVorbis I 20090709
      encoder version: 0
      audio codec: Vorbis
      nominal bitrate: 80000
      bitrate: 80000
```
