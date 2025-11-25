# GStreamer 官方教程 playback-tutorial-8 的完整示例

原文地址: <https://gstreamer.freedesktop.org/documentation/tutorials/playback/hardware-accelerated-video-decoding.html?gi-language=c>

只是翻译了一下

视频解码可能是一项非常耗费 CPU 的任务，特别是对于 1080p 高清电视等更高分辨率。幸运的是，现代显卡配备了可编程 GPU，能够处理这项工作，使 CPU 可以专注于其他任务。对于低功耗 CPU 来说，拥有专用硬件变得至关重要，因为它们根本无法足够快地解码此类媒体。

在当前情况下（2016 年 6 月），每个 GPU 制造商都提供了一种不同的方法来访问其硬件（不同的 API），并且尚未形成强大的行业标准。

截至 2016 年 6 月，至少存在 8 种不同的视频解码加速 API：

`VAAPI`（视频加速 API）：最初由英特尔于 2007 年设计，旨在 Unix 系统上的 X Window 系统，现已成为开源项目。它现在也通过 dmabuf 支持 Wayland。目前它不仅限于英特尔 GPU，其他制造商也可以自由使用这个 API，例如 Imagination Technologies 或 S3 Graphics。通过 `va` 插件从 `gst-plugins-bad` 可访问 GStreamer。

`OVD`（开放视频解码）：AMD 图形的另一个 API，设计为平台无关的方法，供软件开发者利用 AMD Radeon 显卡中的通用视频解码（UVD）硬件。目前无法访问 GStreamer。

`DCE`（分布式编解码器引擎）：德州仪器提供的开源软件库（"libdce"）和 API 规范，旨在 Linux 系统和 ARM 平台。通过 gstreamer-ducati 插件可访问 GStreamer。

`Android MediaCodec`：这是 Android 访问设备硬件解码器和编码器的 API（如果可用）。通过 gst-plugins-bad 中的 `androidmedia` 插件可访问。这包括编码和解码。

Apple VideoTool Box 框架：Apple 的硬件访问 API 通过 `applemedia` 插件提供，该插件包含通过 `vtenc` 元素进行编码和通过 `vtdec` 元素进行解码的功能。

Video4Linux：最新的 Linux 内核有一个内核 API，以标准方式暴露硬件编解码器，现在这一功能已通过 `v4l2` 插件在 `gst-plugins-good` 中得到支持。根据平台不同，这可以支持解码和编码。

## 硬件加速视频解码插件的内部工作原理

这些 API 通常提供多种功能，如视频解码、后处理或解码帧的展示。相应地，插件通常为这些功能提供不同的 GStreamer 元素，以便构建满足各种需求的管道。

例如，来自 `gst-plugins-bad` 的 `va` 插件提供了 `vah264dec` 、 `vah264dec` 、 `vahav1dec` 等元素，以及通过 VAAPI 实现硬件加速解码、将原始视频帧上传至 GPU 内存、将 GPU 帧下载至系统内存以及显示 GPU 帧的功能。

需要区分常规 GStreamer 帧（驻留在系统内存中）和硬件加速 API 生成的帧。后者驻留在 GPU 内存中，GStreamer 无法直接访问。当这些帧被映射时，通常可以下载至系统内存并作为常规 GStreamer 帧处理，但保留在 GPU 中并从那里显示效率更高。

不过 GStreamer 需要跟踪这些“硬件缓冲区”的位置，因此常规缓冲区仍然会逐个元素传递。它们看起来像普通缓冲区，但映射其内容速度更慢，因为需要从硬件加速元素使用的特殊内存中检索。这种特殊内存类型通过分配查询机制协商确定。

这意味着，如果系统中存在特定的硬件加速 API，并且相应的 GStreamer 插件也可用，那么像 playbin3 这样的自动插入元素可以自由使用硬件加速来构建它们的管道；应用程序无需进行任何特殊操作来启用它。几乎：

当 `playbin3` 需要在多个同样有效的元素之间进行选择时，比如传统的软件解码（通过 `vp8dec` ，例如）或硬件加速解码（通过 `vavp8dec` ，例如），它会使用它们的排名来决定。排名是每个元素的属性，表示其优先级； `playbin3` 将简单地选择能够构建完整管道且排名最高的元素。

因此， playbin3 是否使用硬件加速将取决于所有能够处理该媒体类型的元素的相对排名。因此，确保硬件加速启用或禁用的最简单方法是通过环境变量 `GST_PLUGIN_FEATURE_RANK` 更改相关元素的排名（有关更多信息，请参阅文档中的“运行和调试 GStreamer 应用程序”）。另一种方法是在您的应用程序中设置排名，如这段代码所示：

```c
static void enable_factory (const gchar *name, gboolean enable) {
    GstRegistry *registry = NULL;
    GstElementFactory *factory = NULL;

    registry = gst_registry_get_default ();
    if (!registry) return;

    factory = gst_element_factory_find (name);
    if (!factory) return;

    if (enable) {
        gst_plugin_feature_set_rank (GST_PLUGIN_FEATURE (factory), GST_RANK_PRIMARY + 1);
    }
    else {
        gst_plugin_feature_set_rank (GST_PLUGIN_FEATURE (factory), GST_RANK_NONE);
    }

    gst_registry_add_feature (registry, GST_PLUGIN_FEATURE (factory));
    return;
}
```

这个方法传入的第一个参数是要修改的元素的名称，例如 `vavp9dec` 或 `fluvadec` 。

关键方法是 `gst_plugin_feature_set_rank()` ，它将把请求的元素工厂的等级设置为期望的级别。为了方便起见，等级分为 `NONE`、`MARGINAL`、`SECONDARY` 和 `PRIMARY`，但任何数字都可以。在启用元素时，我们将其设置为 PRIMARY+1，这样它的等级就比通常具有 PRIMARY 等级的其他元素要高。将元素的等级设置为 NONE 会使自动插拔机制永远不会选择它。
