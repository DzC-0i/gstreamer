# GStreamer 官方教程 basic-tutorial-7 的完整示例

程序原文地址: <https://gstreamer.freedesktop.org/documentation/tutorials/basic/multithreading-and-pad-availability.html?gi-language=c>

GStreamer 音频分流+可视化管道代码解析, 该代码实现了「音频生成→分流→播放+可视化」的完整流程。核心功能是：通过 `audiotestsrc` 生成测试音频，利用 `tee` 元素（分流器）将音频数据分为两路——一路通过音频设备播放，另一路转换为可视化波形视频显示。代码重点演示了 **动态请求 Pad（Request Pad）** 的核心用法，同时覆盖元素配置、手动链接、资源释放等关键流程。

## 一、核心元素与管道流程图
### 1.1 管道数据流架构

```mathematica
audiotestsrc（音频源）→ tee（分流器）→ 分支1（音频播放）→ audio_queue → audioconvert → audioresample → autoaudiosink（音频输出）
                          ↓
                        分支2（视频可视化）→ video_queue → wavescope（音频转视频）→ videoconvert → autovideosink（视频输出）
```

### 1.2 关键元素功能说明
| 元素名          | 功能描述                                                                 |
|-----------------|--------------------------------------------------------------------------|
| `audiotestsrc`  | 生成测试音频（默认正弦波），支持配置频率等参数。                         |
| `tee`           | 数据流分流器：将输入的一路数据复制为多路输出（需动态请求输出 Pad）。     |
| `queue`         | 数据缓存队列：解决不同分支处理速度不匹配问题，避免管道阻塞。             |
| `audioconvert`  | 音频格式转换：统一音频采样格式、声道数，确保与后续元素兼容。             |
| `audioresample` | 音频重采样：适配音频设备支持的采样率（如 44100Hz ↔ 48000Hz）。           |
| `autoaudiosink` | 自动音频输出：自动识别系统默认音频设备（扬声器/耳机）。                  |
| `wavescope`     | 音频可视化：将音频数据转换为波形视频（示波器风格效果）。                 |
| `videoconvert`  | 视频格式转换：适配视频设备的颜色空间（如 RGB ↔ YUV）。                   |
| `autovideosink` | 自动视频输出：自动识别系统默认视频设备（显示器窗口）。                  |
| `pipeline`      | 元素容器：管理所有元素的生命周期和数据流调度。                           |

## 二、代码逐段解析
### 2.1 头文件与变量声明

```c
#include <gst/gst.h>

int main(int argc, char *argv[]) {
  // 元素指针声明（音频分支+视频分支+管道）
  GstElement *pipeline, *audio_source, *tee, *audio_queue, *audio_convert, *audio_resample, *audio_sink;
  GstElement *video_queue, *visual, *video_convert, *video_sink;
  GstBus *bus;          // 消息总线（接收管道状态、错误等消息）
  GstMessage *msg;      // 消息对象（存储总线接收的消息）
  GstPad *tee_audio_pad, *tee_video_pad;  // tee 动态请求的输出 Pad
  GstPad *queue_audio_pad, *queue_video_pad;  // 队列的静态输入 Pad
```

- 变量分类：元素（`GstElement*`）、消息系统（`GstBus*`/`GstMessage*`）、Pad 接口（`GstPad*`）；
- 关键：`tee` 的输出 Pad 是「动态请求类型」，需单独声明变量存储。

### 2.2 GStreamer 初始化与元素创建

```c
  /* Initialize GStreamer */
  gst_init (&argc, &argv);  // 初始化 GStreamer 库，解析命令行参数

  /* Create the elements */
  audio_source = gst_element_factory_make ("audiotestsrc", "audio_source");
  tee = gst_element_factory_make ("tee", "tee");
  audio_queue = gst_element_factory_make ("queue", "audio_queue");
  audio_convert = gst_element_factory_make ("audioconvert", "audio_convert");
  audio_resample = gst_element_factory_make ("audioresample", "audio_resample");
  audio_sink = gst_element_factory_make ("autoaudiosink", "audio_sink");
  video_queue = gst_element_factory_make ("queue", "video_queue");
  visual = gst_element_factory_make ("wavescope", "visual");  // 音频可视化元素
  video_convert = gst_element_factory_make ("videoconvert", "csp");  // 颜色空间转换
  video_sink = gst_element_factory_make ("autovideosink", "video_sink");
```

- `gst_element_factory_make`：简化的元素创建函数，直接指定元素类型和名称；
- 元素命名规范：采用「功能+类型」的命名方式（如 `audio_source` 表示音频源），便于调试。

### 2.3 管道创建与元素有效性检查

```c
  /* Create the empty pipeline */
  pipeline = gst_pipeline_new ("test-pipeline");  // 创建管道容器

  // 检查所有元素是否创建成功（避免空指针导致崩溃）
  if (!pipeline || !audio_source || !tee || !audio_queue || !audio_convert || !audio_resample || !audio_sink ||
      !video_queue || !visual || !video_convert || !video_sink) {
    g_printerr ("Not all elements could be created.\n");
    return -1;
  }
```

- 管道是 GStreamer 程序的核心容器，所有元素必须添加到管道中才能协同工作；
- 空指针检查是必要步骤：元素创建失败（如插件未安装）会返回 `NULL`，直接运行会导致崩溃。

### 2.4 元素参数配置

```c
  /* Configure elements */
  g_object_set (audio_source, "freq", 215.0f, NULL);  // 设置音频频率为 215Hz
  g_object_set (visual, "shader", 0, "style", 1, NULL);  // 配置可视化效果
```

- `g_object_set`：GObject 框架的通用配置函数（GStreamer 元素基于 GObject 实现）；
- 参数说明：`audiotestsrc` 的 `freq` 属性控制波浪的频率（215Hz 使波浪在窗口中几乎静止），而 `wavescope` 的这种样式和着色器使波浪连续。
  - `audio_source` 的 `freq`：音频频率（单位 Hz），默认 440Hz（标准音），此处改为 215Hz；
  - `visual`（`wavescope`）的 `shader`：渲染器类型（0=默认），`style`：显示样式（1=线条模式，0=填充模式）。

### 2.5 元素添加与静态 Pad 链接

```c
  /* Link all elements that can be automatically linked (Always pads) */
  gst_bin_add_many (GST_BIN (pipeline), audio_source, tee, audio_queue, audio_convert, audio_resample, audio_sink,
      video_queue, visual, video_convert, video_sink, NULL);  // 元素添加到管道

  // 分分支自动链接（仅支持静态 Pad 元素）
  if (gst_element_link_many (audio_source, tee, NULL) != TRUE ||  // 音频源 → tee（输入 Pad 静态）
      gst_element_link_many (audio_queue, audio_convert, audio_resample, audio_sink, NULL) != TRUE ||  // 音频分支
      gst_element_link_many (video_queue, visual, video_convert, video_sink, NULL) != TRUE) {  // 视频分支
    g_printerr ("Elements could not be linked.\n");
    gst_object_unref (pipeline);
    return -1;
  }
```

- 核心逻辑：
  - `gst_bin_add_many`：批量将元素添加到管道（管道是 `GstBin` 的子类，需强制类型转换）；
  - `gst_element_link_many`：批量链接元素，仅支持「静态 Pad（Always 类型）」的元素；
  - 无法自动链接 `tee` 的输出 Pad：因为 `tee` 的输出 Pad 是「Request 类型」（按需创建），需手动处理。

### 2.6 动态 Pad 请求与手动链接（核心难点）

```C
  /* Manually link the Tee (Request pads) */
  // 1. 请求 tee 的音频分支输出 Pad（模板名 "src_%u"，%u 自动编号）
  tee_audio_pad = gst_element_request_pad_simple (tee, "src_%u");
  g_print ("Obtained request pad %s for audio branch.\n", gst_pad_get_name (tee_audio_pad));
  // 2. 获取音频队列的静态输入 Pad（名称 "sink"）
  queue_audio_pad = gst_element_get_static_pad (audio_queue, "sink");

  // 3. 请求 tee 的视频分支输出 Pad
  tee_video_pad = gst_element_request_pad_simple (tee, "src_%u");
  g_print ("Obtained request pad %s for video branch.\n", gst_pad_get_name (tee_video_pad));
  // 4. 获取视频队列的静态输入 Pad
  queue_video_pad = gst_element_get_static_pad (video_queue, "sink");

  // 5. 手动链接 Pad（源 Pad → 接收 Pad）
  if (gst_pad_link (tee_audio_pad, queue_audio_pad) != GST_PAD_LINK_OK ||
      gst_pad_link (tee_video_pad, queue_video_pad) != GST_PAD_LINK_OK) {
    g_printerr ("Tee could not be linked.\n");
    gst_object_unref (pipeline);
    return -1;
  }

  // 6. 释放队列 Pad 引用（链接成功后无需单独持有）
  gst_object_unref (queue_audio_pad);
  gst_object_unref (queue_video_pad);
```

在 `tee` 元素的文档中我们看到，它有两个 Pad 模板名称，分别是 `sink`（用于其 `sink Pad`）和 `src_%u`（用于请求 Pad）。我们使用 `gst_element_request_pad_simple()` 从 `tee` 请求两个 `Pad`（用于音频和视频分支）。
- 关键概念：
  - `tee` 的输出 Pad 模板：名称为 `src_%u`（`%u` 是通配符，自动生成编号如 `src_0`、`src_1`）；
  - `gst_element_request_pad_simple`：简化版动态 Pad 请求函数（GStreamer 1.16+ 支持），参数为「元素+Pad 模板名」；
  - `gst_pad_link`：手动链接两个 Pad，必须满足「SRC Pad → SINK Pad」的方向要求；
  - 资源管理：队列的静态 Pad 链接后可立即释放引用，避免内存泄漏。

### 2.7 管道启动与消息监听

```C
  /* Start playing the pipeline */
  gst_element_set_state (pipeline, GST_STATE_PLAYING);  // 启动管道（生成音频+视频）

  /* Wait until error or EOS */
  bus = gst_element_get_bus (pipeline);  // 获取管道消息总线
  // 监听消息：无超时，仅过滤错误（ERROR）和流结束（EOS）消息
  msg = gst_bus_timed_pop_filtered (bus, GST_CLOCK_TIME_NONE, GST_MESSAGE_ERROR | GST_MESSAGE_EOS);
```

- 管道状态：`GST_STATE_PLAYING` 是运行状态，会触发元素开始生成/处理数据；
- 消息监听：`audiotestsrc` 会持续生成音频，不会主动发送 EOS 消息，程序会一直运行直到手动关闭窗口或出错。

### 2.8 资源释放（避免内存泄漏）

```C
  /* Release request pads from Tee */
  gst_element_release_request_pad (tee, tee_audio_pad);  // 释放动态 Pad
  gst_element_release_request_pad (tee, tee_video_pad);
  gst_object_unref (tee_audio_pad);  // 解引用 Pad 对象
  gst_object_unref (tee_video_pad);

  /* Free all resources */
  if (msg != NULL)
    gst_message_unref (msg);  // 释放消息对象
  gst_object_unref (bus);     // 释放消息总线
  gst_element_set_state (pipeline, GST_STATE_NULL);  // 停止管道，释放内部资源
  gst_object_unref (pipeline);  // 释放管道

  return 0;
}
```

- 资源释放顺序：
  1. 先释放动态请求的 Pad（`gst_element_release_request_pad`），再解引用；
  2. 释放消息相关对象（`msg`、`bus`）；
  3. 将管道设置为 `GST_STATE_NULL`（停止数据流，释放缓冲区），最后解引用管道；
- 关键：动态 Pad 必须先释放再解引用，否则会导致资源泄漏。

## 三、程序运行效果

```bash
# 编译
gcc basic-tutorial-7.c -o basic-tutorial-7 `pkg-config --cflags --libs gstreamer-1.0`

# 运行
./basic-tutorial-7
```

运行后会出现两个结果：
1. 扬声器播放 215Hz 的持续正弦波音频；
2. 弹出视频窗口，显示音频的线条式波形可视化（随音频频率实时变化）。


## 四、核心难点与注意事项
### 4.1 动态 Pad 与静态 Pad 的区别

| 类型       | 特点                          | 示例元素                |
|------------|-------------------------------|-------------------------|
| 静态 Pad   | 元素创建时自动生成，始终存在  | `audiotestsrc` 的 SRC Pad、`queue` 的 SINK Pad |
| 动态 Pad   | 需手动请求才能生成，按需创建  | `tee` 的输出 Pad        |

### 4.2 常见错误排查

1. **编译错误：`gst_element_request_pad_simple` 未定义**：
   - 原因：GStreamer 版本 < 1.16，或缺少 `gobject/gobject.h` 头文件；
   - 解决方案：升级 GStreamer 或替换为兼容 API `gst_element_request_pad`。 (注意: 在使用之前先需要通过 `gst_element_get_pad_template` 获取到Pad 模板)
2. **链接失败：`Tee could not be linked`**：
   - 原因：Pad 方向错误（如 SRC Pad → SRC Pad），或 Pad 模板名称错误；
   - 排查：通过 `gst_pad_get_direction()` 检查 Pad 方向。
3. **运行无声音/无画面**：
   - 原因：缺少插件（如 `wavescope`、`autoaudiosink`）；
   - 解决方案：安装对应插件（如 Ubuntu 系统：`sudo apt install gstreamer1.0-plugins-good`）。
