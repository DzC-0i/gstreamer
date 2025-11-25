# GStreamer 官方教程 playback-tutorial-7 的完整示例

原文地址: <https://gstreamer.freedesktop.org/documentation/tutorials/playback/custom-playbin-sinks.html?gi-language=c>

这段代码是基于 **GStreamer** 的 **自定义音频处理流水线** 示例，核心功能是：播放网络视频（`sintel_trailer-480p.webm`），并通过 **自定义音频 sink bin** 对音频流进行额外处理——插入 3 段均衡器（`equalizer-3bands`），压低低中频（band1/band2）音量，最后通过默认音频输出设备播放。

它的核心价值是演示 **`playbin` 的灵活性**：`playbin` 作为“全能播放器”，支持替换其内部的音视频输出组件（`audio-sink`/`video-sink`），让开发者无需手动构建完整流水线（如 `src→decoder→filter→sink`），即可插入自定义处理逻辑（如均衡器、音效、格式转换等）。


### 一、核心概念铺垫

在解析代码前，先明确 2 个关键概念：
1. **`playbin` 的结构**：`playbin` 内部已封装了“解封装→解码→音视频渲染”的完整流程，其中音频部分的默认流水线是 `音频解码器 → audioconvert → autoaudiosink`（自动音频输出）。
2. **Bin 与 Ghost Pad**：
   - **Bin**：GStreamer 中的“元素容器”，可将多个元素打包成一个“虚拟元素”，简化流水线管理（对外暴露统一的 Pad，内部元素自行链接）。
   - **Ghost Pad（幽灵 Pad）**：Bin 的“对外接口”，本质是对 Bin 内部某个元素的 Pad 的“引用”，让外部元素能通过 Ghost Pad 与 Bin 内部元素通信（无需关心 Bin 内部结构）。


### 二、核心流程与关键函数解析

代码的核心逻辑是：**创建自定义音频处理 Bin（均衡器+格式转换+音频输出）→ 替换 `playbin` 的默认音频输出（`audio-sink`）→ 启动播放**。

#### 1. 主函数（`main`）：初始化与流水线构建
##### （1）初始化 GStreamer

```c
gst_init(&argc, &argv);  /* 初始化 GStreamer 库，解析命令行参数 */
```

##### （2）创建 `playbin` 主流水线

```c
// 创建 playbin，指定网络视频 URI（默认会自动解码音视频并播放）
pipeline = gst_parse_launch("playbin uri=https://gstreamer.freedesktop.org/data/media/sintel_trailer-480p.webm", NULL);
```
- 此时 `playbin` 用默认的音频输出组件（`autoaudiosink`），没有均衡器处理。

##### （3）创建自定义音频处理的内部元素

```c
// 1. 3段均衡器（调节低/中/高3个频段的音量）
equalizer = gst_element_factory_make("equalizer-3bands", "equalizer");
// 2. 音频格式转换器（确保均衡器的输出格式与音频输出设备兼容）
convert = gst_element_factory_make("audioconvert", "convert");
// 3. 自动音频输出设备（根据系统选择默认扬声器/耳机）
sink = gst_element_factory_make("autoaudiosink", "audio_sink");

// 检查元素创建是否成功（GStreamer 元素创建失败会返回 NULL）
if (!equalizer || !convert || !sink) {
    g_printerr("Not all elements could be created.\n");
    return -1;
}
```
- 关键元素说明：
  - `equalizer-3bands`：3 段均衡器，支持调节 3 个频段：
    - `band1`：低频（~60Hz，如鼓声）；
    - `band2`：中频（~300Hz，如人声）；
    - `band3`：高频（~10kHz，如吉他、高音）；
  - `audioconvert`：格式转换“万能工具”，解决不同元素间的音频格式（如采样率、位深、声道数）不兼容问题，**必须添加**（否则均衡器输出可能无法被音频输出设备识别）。

##### （4）创建自定义音频 Bin（打包内部元素）

将 `equalizer→convert→sink` 打包成一个 Bin，对外暴露统一的 `sink` Pad（供 `playbin` 输出音频数据）：
```c
// 1. 创建一个名为 "audio_sink_bin" 的 Bin
bin = gst_bin_new("audio_sink_bin");

// 2. 将 equalizer、convert、sink 添加到 Bin 中
gst_bin_add_many(GST_BIN(bin), equalizer, convert, sink, NULL);

// 3. 链接 Bin 内部的元素（流水线：equalizer 的 src → convert 的 sink；convert 的 src → sink 的 sink）
gst_element_link_many(equalizer, convert, sink, NULL);

// 4. 创建 Ghost Pad（Bin 的对外接口）
// 4.1 获取 equalizer 的 "sink" Pad（Bin 内部的输入 Pad）
pad = gst_element_get_static_pad(equalizer, "sink");
// 4.2 创建 Ghost Pad，命名为 "sink"，引用 equalizer 的 "sink" Pad
ghost_pad = gst_ghost_pad_new("sink", pad);
// 4.3 激活 Ghost Pad（必须激活才能接收数据）
gst_pad_set_active(ghost_pad, TRUE);
// 4.4 将 Ghost Pad 添加到 Bin 上（Bin 现在对外暴露 "sink" Pad）
gst_element_add_pad(bin, ghost_pad);
// 4.5 释放原始 Pad 的引用（Ghost Pad 已持有引用，无需再保留）
gst_object_unref(pad);
```
- 最终 Bin 的结构：
  ```
  [audio_sink_bin]  // 对外暴露 "sink" Ghost Pad
  ├─ equalizer（sink Pad 被 Ghost Pad 引用）
  ├─ convert
  └─ sink
  内部链接：equalizer → convert → sink
  ```

##### （5）配置均衡器参数

```c
// 压低 band1（低频）音量 24dB（范围通常为 -24dB ~ +12dB）
g_object_set(G_OBJECT(equalizer), "band1", (gdouble)-24.0, NULL);
// 压低 band2（中频）音量 24dB
g_object_set(G_OBJECT(equalizer), "band2", (gdouble)-24.0, NULL);
// band3（高频）未配置，使用默认值（0dB，无增益/衰减）
```

- 效果：播放时低频和中频被大幅压低，只能听到高频声音（如视频中的背景音乐高音、环境音细节）。

##### （6）替换 `playbin` 的默认音频输出

```c
// 将 playbin 的 "audio-sink" 属性设置为我们的自定义 Bin
g_object_set(GST_OBJECT(pipeline), "audio-sink", bin, NULL);
```

- 此时 `playbin` 的音频流水线变为：
  `playbin 内部音频解码器 → 自定义 Bin 的 sink Ghost Pad → equalizer → convert → autoaudiosink`

##### （7）启动播放与等待事件

```c
// 启动流水线播放
gst_element_set_state(pipeline, GST_STATE_PLAYING);

// 获取流水线总线，等待错误（ERROR）或流结束（EOS）消息（阻塞直到事件发生）
bus = gst_element_get_bus(pipeline);
msg = gst_bus_timed_pop_filtered(bus, GST_CLOCK_TIME_NONE, GST_MESSAGE_ERROR | GST_MESSAGE_EOS);
```

##### （8）资源释放

```c
if (msg != NULL)
    gst_message_unref(msg);  // 释放消息
gst_object_unref(bus);      // 释放总线
gst_element_set_state(pipeline, GST_STATE_NULL);  // 停止流水线，释放内部资源
gst_object_unref(pipeline); // 释放 playbin
return 0;
```

### 三、关键技术点详解
#### 1. 为什么要用 Bin 打包元素？

如果不打包成 Bin，直接将 `equalizer→convert→sink` 作为 `audio-sink` 传给 `playbin` 会失败——`playbin` 的 `audio-sink` 属性要求接收一个“单一元素”（或 Bin，因为 Bin 是特殊的元素），而不是多个独立元素。

打包成 Bin 后，`playbin` 只需与 Bin 的 Ghost Pad 交互，无需关心内部元素的链接和结构，大幅简化了外部集成。

#### 2. Ghost Pad 的作用

- Bin 本身是“容器”，没有自己的 Pad，必须通过 Ghost Pad 对外提供接口；
- 这里 Ghost Pad 引用了 `equalizer` 的 `sink` Pad，意味着：外部元素（`playbin` 的音频解码器）向 Bin 的 `sink` Pad 发送数据，本质是直接发送给 `equalizer` 的 `sink` Pad；
- Ghost Pad 是“透明”的，不改变数据流向，仅提供统一的外部接口。

#### 3. `audioconvert` 的必要性

- `equalizer-3bands` 的输出音频格式（如采样率、位深）可能与 `autoaudiosink` 支持的格式不兼容（不同系统/设备支持的格式不同）；
- `audioconvert` 会自动将输入格式转换为输出设备支持的格式，避免“格式不兼容导致播放失败”。

#### 4. `playbin` 的可配置属性

`playbin` 提供了多个可配置属性，用于替换内部组件或开启功能，常见的有：
- `audio-sink`：替换音频输出组件（本示例用自定义 Bin）；
- `video-sink`：替换视频输出组件（如 `xvimagesink`、`gtksink`）；
- `vis-plugin`：音频可视化插件（之前的音频可视化示例用过）；
- `flags`：功能开关（如 `GST_PLAY_FLAG_VIS` 启用可视化）。


### 四、编译与运行说明
#### 1. 编译命令

```bash
gcc playback-tutorial-7.c -o playback-tutorial-7 `pkg-config --cflags --libs gstreamer-1.0`
```

#### 2. 运行效果

- 视频窗口弹出，正常播放视频；
- 音频部分：低频（如鼓声）和中频（如人声）被大幅压低，只能听到高频声音（如背景音乐的高音、风声等细节）；
- 按 `Ctrl+C` 或视频播放完成后，程序自动退出。


### 五、功能扩展方向
1. 调节均衡器参数：修改 `band1`/`band2`/`band3` 的值，实现不同音效（如增强低音、突出人声）；
2. 添加更多音频处理：在 Bin 中插入其他音频滤镜（如 `volume` 调节音量、`audiopanorama` 调节立体声平衡）；
3. 动态调节均衡器：通过键盘快捷键实时修改 `equalizer` 的频段值（参考之前的色彩调节示例）；
4. 替换视频输出：类似 `audio-sink`，通过 `video-sink` 属性替换 `playbin` 的视频输出组件（如插入视频滤镜 Bin）。
5. 使用 `GStreamer` 提供的众多有趣视频滤镜之一，构建一个视频 `bin` 而不是音频 `bin，例如` `solarize` 、 `vertigotv` 或 `effectv` 插件中的任何元素。如果由于不兼容的 `caps` 导致管道无法连接，请记得使用颜色空间转换元素 `videoconvert` 。


### 总结
这段代码的核心是 **`playbin` 的 `audio-sink` 替换能力** 和 **Bin+Ghost Pad 的组件封装技巧**。它展示了如何在不破坏 `playbin` 原有功能的前提下，插入自定义音频处理逻辑，适用于需要对音视频进行二次处理的场景（如播放器的音效调节、格式转换、滤镜应用等）。

关键点回顾：
- Bin 用于打包多个元素，简化外部集成；
- Ghost Pad 是 Bin 的对外接口，引用内部元素的 Pad；
- `audioconvert` 是音频处理的“兼容性保障”，必须添加；
- `playbin` 的属性配置是扩展其功能的核心方式。
