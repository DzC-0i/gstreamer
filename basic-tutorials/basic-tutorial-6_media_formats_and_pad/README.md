# GStreamer 官方教程 basic-tutorial-6 的完整示例

程序原文地址: <https://gstreamer.freedesktop.org/documentation/tutorials/basic/media-formats-and-pad-capabilities.html?gi-language=c#introduction>

## 一、核心概念铺垫

1. Element（元素）：GStreamer 的功能单元（如音频源、音频输出、编码器等），是管道的基本组成部分。
2. Pad（衬垫）：元素的「输入 / 输出接口」，分为 SRC Pad（源垫，输出数据）和 SINK Pad（接收垫，输入数据）。元素必须通过 Pad 连接才能组成管道。
3. Caps（能力）：描述 Pad 支持的数据格式（如音频的采样率、位深、编码格式等）。两个元素能连接的前提是：源元素的 SRC Pad 能力与接收元素的 SINK Pad 能力兼容（即 Caps 协商成功）。
4. Pad Template（Pad 模板）：元素的「Pad 蓝图」，定义了该元素可能创建的 Pad 类型（SRC/SINK）、可用性（始终存在 / 按需创建）、支持的 Caps 等。

## 二、工具函数：辅助打印信息

1. `print_field()`: 格式化打印 GValue 字段
2. `print_caps()`: 打印 GstCaps（媒体能力）信息
3. `print_pad_templates_information()`: 打印元素的 Pad 模板信息
4. `print_pad_capabilities()`: 打印指定 Pad 的当前能力

---

1. `print_field` 函数：打印单个属性字段

```C
static gboolean print_field (GQuark field, const GValue * value, gpointer pfx) {
  gchar *str = gst_value_serialize (value);  // 把 GValue 类型（GStreamer 通用值类型）序列化为字符串
  g_print ("%s  %15s: %s\n", (gchar *) pfx, g_quark_to_string (field), str);  // 格式化打印（前缀 + 字段名 + 字段值）
  g_free (str);  // 释放字符串内存
  return TRUE;
}
```

- 作用：遍历 `GstStructure`（Caps 的内部数据结构）时，打印每个字段（如音频采样率 `rate`、位深 `depth` 等）。
- 参数：
    - `GQuark field`：字段名（GStreamer 用 Quark 类型表示唯一标识符）；
    - `GValue *value`：字段值（如采样率 44100、位深 16）；
    - `pfx`：打印前缀（用于缩进，让输出更整洁）。

2. `print_caps` 函数：打印 Caps（能力信息）

```C
static void print_caps (const GstCaps * caps, const gchar * pfx) {
  g_return_if_fail (caps != NULL);  // 安全检查：如果 caps 为空则返回

  // 处理特殊情况：Caps 为「任意格式」或「空格式」
  if (gst_caps_is_any (caps)) { g_print ("%sANY\n", pfx); return; }
  if (gst_caps_is_empty (caps)) { g_print ("%sEMPTY\n", pfx); return; }

  // 遍历 Caps 中的所有结构（一个 Caps 可能包含多个兼容的格式）
  for (i = 0; i < gst_caps_get_size (caps); i++) {
    GstStructure *structure = gst_caps_get_structure (caps, i);  // 获取单个格式结构
    g_print ("%s%s\n", pfx, gst_structure_get_name (structure));  // 打印格式名称（如 audio/x-raw）
    gst_structure_foreach (structure, print_field, (gpointer) pfx);  // 遍历结构中的字段并打印
  }
}
```

- 作用：将 `GstCaps` 类型（能力集合）打印为可读格式。
- 示例输出（音频原始格式）：
```mathematica
audio/x-raw
    format: S16LE
    rate: 44100
    channels: 1
    layout: interleaved
```

3. `print_pad_templates_information` 函数：打印 Pad 模板信息

```C
static void print_pad_templates_information (GstElementFactory * factory) {
  g_print ("Pad Templates for %s:\n", gst_element_factory_get_longname (factory));  // 打印元素全称
  if (!gst_element_factory_get_num_pad_templates (factory)) {  // 无 Pad 模板时提示
    g_print ("  none\n");
    return;
  }

  // 遍历元素工厂的所有 Pad 模板
  const GList *pads = gst_element_factory_get_static_pad_templates (factory);
  while (pads) {
    GstStaticPadTemplate *padtemplate = pads->data;
    pads = g_list_next (pads);

    // 打印 Pad 方向（SRC/SINK）和名称模板
    if (padtemplate->direction == GST_PAD_SRC)
      g_print ("  SRC template: '%s'\n", padtemplate->name_template);
    else if (padtemplate->direction == GST_PAD_SINK)
      g_print ("  SINK template: '%s'\n", padtemplate->name_template);

    // 打印 Pad 可用性（始终存在/有时存在/按需创建）
    if (padtemplate->presence == GST_PAD_ALWAYS) g_print ("    Availability: Always\n");
    else if (padtemplate->presence == GST_PAD_SOMETIMES) g_print ("    Availability: Sometimes\n");
    else if (padtemplate->presence == GST_PAD_REQUEST) g_print ("    Availability: On request\n");

    // 打印 Pad 模板支持的 Caps（能力）
    if (padtemplate->static_caps.string) {
      GstCaps *caps = gst_static_caps_get (&padtemplate->static_caps);
      g_print ("    Capabilities:\n");
      print_caps (caps, "      ");  // 调用 print_caps 打印能力
      gst_caps_unref (caps);  // 释放 Caps 内存
    }
    g_print ("\n");
  }
}
```

- 作用：通过「元素工厂」（GstElementFactory）获取元素的 Pad 模板，打印其方向、可用性、支持的格式等信息。
- 元素工厂：GStreamer 中创建元素的「工厂类」，可以先通过工厂查询元素信息（如 Pad 模板），再创建实际元素。

4. `print_pad_capabilities` 函数：打印 Pad 的当前能力

```C
static void print_pad_capabilities (GstElement *element, gchar *pad_name) {
  GstPad *pad = gst_element_get_static_pad (element, pad_name);  // 获取元素的静态 Pad（名称固定的 Pad）
  if (!pad) { g_printerr ("Could not retrieve pad '%s'\n", pad_name); return; }

  // 获取 Pad 的「当前协商后 Caps」（如果协商未完成，则获取「支持的 Caps」）
  GstCaps *caps = gst_pad_get_current_caps (pad);
  if (!caps) caps = gst_pad_query_caps (pad, NULL);

  // 打印 Caps 并释放资源
  g_print ("Caps for the %s pad:\n", pad_name);
  print_caps (caps, "      ");
  gst_caps_unref (caps);
  gst_object_unref (pad);
}
```

- 作用：打印某个元素的指定 Pad 在「当前状态」下的实际能力（协商后的格式，如实际播放时的采样率）。
- 关键区别：Pad 模板的 Caps 是「支持的所有格式」，而 Pad 的当前 Caps 是「实际使用的格式」。

## 三、主函数 `main`

主函数是程序的入口，按「初始化 → 创建组件 → 构建管道 → 运行 → 清理」的流程执行。

### 步骤 1：初始化 GStreamer

```C
gst_init (&argc, &argv);  // 初始化 GStreamer 库，解析命令行参数
```

### 步骤 2：创建元素工厂并查询 Pad 模板

```C
// 查找「音频测试源」和「自动音频输出」的元素工厂
source_factory = gst_element_factory_find ("audiotestsrc");  // 生成测试音频（如正弦波）
sink_factory = gst_element_factory_find ("autoaudiosink");   // 自动选择系统音频输出设备（如扬声器）

if (!source_factory || !sink_factory) {  // 工厂查找失败（如元素未安装）
  g_printerr ("Not all element factories could be created.\n");
  return -1;
}

// 打印两个元素的 Pad 模板信息（关键：理解元素的接口能力）
print_pad_templates_information (source_factory);
print_pad_templates_information (sink_factory);
```

### 步骤 3：创建元素和管道

```C
// 通过工厂创建实际元素
source = gst_element_factory_create (source_factory, "source");  // 音频源元素（名称：source）
sink = gst_element_factory_create (sink_factory, "sink");        // 音频输出元素（名称：sink）

// 创建空管道（管道是元素的容器，负责管理元素的生命周期和数据流）
pipeline = gst_pipeline_new ("test-pipeline");

// 安全检查：元素/管道创建失败（如内存不足）
if (!pipeline || !source || !sink) {
  g_printerr ("Not all elements could be created.\n");
  return -1;
}
```

### 步骤 4：构建管道（添加元素 + 链接元素）

```C
// 将元素添加到管道（管道是一种特殊的 Bin，Bin 是元素的容器）
gst_bin_add_many (GST_BIN (pipeline), source, sink, NULL);

// 链接源元素和输出元素（source 的 SRC Pad → sink 的 SINK Pad）
if (gst_element_link (source, sink) != TRUE) {  // 链接失败（Caps 不兼容）
  g_printerr ("Elements could not be linked.\n");
  gst_object_unref (pipeline);
  return -1;
}
```

### 步骤 5：打印初始状态（NULL 状态）的 Caps

```C
g_print ("In NULL state:\n");
print_pad_capabilities (sink, "sink");  // 打印 sink 元素的 SINK Pad 在 NULL 状态的能力
```

- GStreamer 元素默认处于 NULL 状态（未初始化），此时 Pad 的 Caps 通常是「支持的所有格式」（未协商）。

### 步骤 6：启动管道并处理消息

```C
// 将管道设置为 PLAYING 状态（开始生成和播放音频）
ret = gst_element_set_state (pipeline, GST_STATE_PLAYING);
if (ret == GST_STATE_CHANGE_FAILURE) {  // 状态切换失败（如设备占用）
  g_printerr ("Unable to set the pipeline to the playing state.\n");
}

// 获取管道的消息总线（用于接收元素的消息：错误、EOS、状态变化等）
bus = gst_element_get_bus (pipeline);
do {
  // 等待消息（无超时，一直等待），过滤只接收 3 类消息：错误、EOS、状态变化
  msg = gst_bus_timed_pop_filtered (bus, GST_CLOCK_TIME_NONE, 
          GST_MESSAGE_ERROR | GST_MESSAGE_EOS | GST_MESSAGE_STATE_CHANGED);

  if (msg != NULL) {
    switch (GST_MESSAGE_TYPE (msg)) {
      case GST_MESSAGE_ERROR:  // 错误消息（如音频设备不可用）
        gst_message_parse_error (msg, &err, &debug_info);
        g_printerr ("Error from %s: %s\n", GST_OBJECT_NAME (msg->src), err->message);
        g_printerr ("Debug: %s\n", debug_info ? debug_info : "none");
        g_clear_error (&err);
        g_free (debug_info);
        terminate = TRUE;  // 终止循环
        break;

      case GST_MESSAGE_EOS:  // 流结束消息（本示例不会触发，因为 audiotestsrc 持续生成音频）
        g_print ("End-Of-Stream reached.\n");
        terminate = TRUE;
        break;

      case GST_MESSAGE_STATE_CHANGED:  // 状态变化消息
        // 只关注管道本身的状态变化（忽略元素的状态变化）
        if (GST_MESSAGE_SRC (msg) == GST_OBJECT (pipeline)) {
          GstState old_state, new_state, pending_state;
          gst_message_parse_state_changed (msg, &old_state, &new_state, &pending_state);
          g_print ("\nPipeline state changed from %s to %s:\n",
              gst_element_state_get_name (old_state), gst_element_state_get_name (new_state));
          // 打印 sink 元素的当前 Caps（状态变化后，Caps 可能已协商完成）
          print_pad_capabilities (sink, "sink");
        }
        break;

      default:  // 意外消息（理论上不会触发）
        g_printerr ("Unexpected message received.\n");
        break;
    }
    gst_message_unref (msg);  // 释放消息内存
  }
} while (!terminate);  // 直到收到终止信号（错误/EOS）
```

### 步骤 7：清理资源

```C
gst_object_unref (bus);  // 释放消息总线
gst_element_set_state (pipeline, GST_STATE_NULL);  // 停止管道，释放资源
gst_object_unref (pipeline);  // 释放管道
gst_object_unref (source_factory);  // 释放元素工厂
gst_object_unref (sink_factory);
return 0;
```

## 🚀 程序运行效果

```bash
# 编译
gcc basic-tutorial-6.c -o basic-tutorial-6 `pkg-config --cflags --libs gtk+-3.0 gstreamer-1.0`

# 运行
./basic-tutorial-6
```
