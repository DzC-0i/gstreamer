# GStreamer 官方教程 playback-tutorial-2 的完整示例

原文地址: <https://gstreamer.freedesktop.org/documentation/tutorials/playback/subtitle-management.html?gi-language=c>

功能概述:
- 播放指定网络视频（sintel_trailer-480p.ogv，开源动画《Sintel》预告片）
- 加载外部字幕文件（希腊语字幕 sintel_trailer_gr.srt）
- 自定义字幕样式（字体、字号：Sans 18 号字）
- 自动解析视频内置的音视频流、字幕流（含外部加载的字幕）
- 支持通过键盘输入切换字幕流（输入字幕索引并回车）
- 处理播放错误、流结束（EOS）、状态变更等事件

这段代码是基于 **GStreamer** 的多媒体播放器进阶示例，核心功能是 **播放视频+加载外部字幕**，并支持通过键盘切换字幕流。它在之前“多音轨切换”示例的基础上，重点扩展了字幕相关的配置（外部字幕加载、字幕样式设置、字幕流切换），以下是详细解析：[运行这个教程时，你会发现第一个字幕流没有语言标签]

## 一、与上一版代码的核心差异

| 功能点       | 上一版（音轨切换）                | 当前版（字幕切换）               |
| ------------ | --------------------------------- | -------------------------------- |
| 核心交互     | 切换音频流                        | 切换字幕流                       |
| 字幕配置     | 禁用字幕（`~GST_PLAY_FLAG_TEXT`） | 启用字幕+加载外部字幕+自定义样式 |
| 键盘回调逻辑 | 修改 `current-audio` 属性         | 修改 `current-text` 属性         |
| 流解析重点   | 音频流元数据（语言、比特率）      | 字幕流元数据（语言）             |


## 二、关键代码解析（按流程拆分）
### 1. 结构体与枚举（复用基础定义）

- `CustomData` 结构体：存储播放器核心状态（`playbin` 元素、流数量、当前播放流索引、主循环），与上一版一致。
- `GstPlayFlags` 枚举：定义 `playbin` 的功能开关（视频、音频、字幕），保持原定义。

### 2. 主函数（`main`）：核心配置与初始化

主函数是程序入口，负责初始化、配置播放器、注册事件监听，重点关注 **字幕相关配置**：

#### （1）基础初始化（与上一版一致）

- 初始化 GStreamer 库：`gst_init(&argc, &argv)`
- 创建 `playbin` 元素（全能播放组件，内置解码、渲染逻辑）
- 注册总线消息监听（`handle_message`）和键盘输入监听（`handle_keyboard`）
- 启动播放状态（`GST_STATE_PLAYING`）和 GLib 主循环

#### （2）字幕相关核心配置（新增/修改部分）

这是当前代码的核心亮点，通过 `g_object_set` 给 `playbin` 设置字幕相关属性：

```c
// 1. 设置视频播放 URI（网络视频）
g_object_set(data.playbin, "uri", "https://gstreamer.freedesktop.org/data/media/sintel_trailer-480p.ogv", NULL);

// 2. 设置外部字幕文件 URI（网络上的希腊语 SRT 字幕）
g_object_set(data.playbin, "suburi", "https://gstreamer.freedesktop.org/data/media/sintel_trailer_gr.srt", NULL);

// 3. 自定义字幕样式：字体为 Sans，字号 18（格式："字体名称, 字号"）
g_object_set(data.playbin, "subtitle-font-desc", "Sans, 18", NULL);

// 4. 启用视频、音频、字幕功能（重点：保留 GST_PLAY_FLAG_TEXT，上一版是禁用）
g_object_get(data.playbin, "flags", &flags, NULL);
flags |= GST_PLAY_FLAG_VIDEO | GST_PLAY_FLAG_AUDIO | GST_PLAY_FLAG_TEXT;  // 启用字幕
g_object_set(data.playbin, "flags", flags, NULL);
```

- **`suburi` 属性**：`playbin` 专门用于加载 **外部字幕文件** 的属性，支持 SRT、ASS 等常见字幕格式（需 GStreamer 安装字幕解析插件，如 `gst-plugins-good` 中的 `srtdec`）。
- **`subtitle-font-desc` 属性**：设置字幕的字体描述，格式为 `“字体名称, 字体样式 字号”`(字符串表示的格式是 [FAMILY-LIST] [STYLE-OPTIONS] [SIZE] ， 其中 FAMILY-LIST 是一个逗号分隔的词语列表，可选择以逗号结尾，STYLE_OPTIONS 是一个空白分隔词列表，每个词描述风格、变体、权重或拉伸，SIZE 是一个小数（大小单位）), 以下所有字符串表示都是有效的：
  - "sans, bold 12"        无粗体12
  - "serif, monospace bold italic condensed 16"        衬线，单宽，粗体斜体，简缩16
  - "normal 10"            正常10

常见的字体家族有: `Normal`, `Sans`, `Serif` 和 `Monospace`.

可用的样式有: `Normal` (字体直立), `Oblique` (字体倾斜但为罗马风格), `Italic` (字体倾斜，呈斜体样式)

可选重量有: `Ultra-Light`, `Light`, `Normal`, `Bold`, `Ultra-Bold`, `Heavy`.

可用的变体有: `Normal`, `Small_Caps` (小写字母被较小的大写字母替换的字体)

可用的拉伸风格包括: `Ultra-Condensed`, `Extra-Condensed`, `Condensed`, `Semi-Condensed`, `Normal`, `Semi-Expanded`, `Expanded`, `Extra-Expanded`, `Ultra-Expanded`

### 3. `analyze_streams`：解析流元数据（重点扩展字幕流）

当播放器进入 `PLAYING` 状态后调用，用于读取并打印音视频流、字幕流的详细信息，**新增字幕流解析增强**：

```c
// 字幕流解析循环（新增错误处理：无标签时打印提示）
for (i = 0; i < data->n_text; i++) {
    tags = NULL;
    g_print("subtitle stream %d:\n", i);
    // 读取第 i 个字幕流的标签（元数据）
    g_signal_emit_by_name(data->playbin, "get-text-tags", i, &tags);
    if (tags) {
        // 提取字幕语言（如 "gr" 表示希腊语）
        if (gst_tag_list_get_string(tags, GST_TAG_LANGUAGE_CODE, &str)) {
            g_print("  language: %s\n", str);
            g_free(str);
        }
        gst_tag_list_free(tags);
    } else {
        g_print("  no tags found\n");  // 无元数据时提示（避免空指针）
    }
}

// 打印当前播放的流索引（重点：增加字幕流索引提示）
g_print("Currently playing video stream %d, audio stream %d and subtitle stream %d\n",
        data->current_video, data->current_audio, data->current_text);
g_print("Type any number and hit ENTER to select a different subtitle stream\n");
```

- 解析逻辑：通过 `get-text-tags` 信号读取每个字幕流的元数据（如语言代码），外部加载的字幕流也会被识别为一个独立的字幕流（索引通常从 0 开始）。
- 示例输出（期望字幕部分）：

  ```mathematica
  subtitle stream 0:
    language: gr  // 外部加载的希腊语字幕
  ```

### 4. `handle_message`：消息处理（无修改）

与上一版逻辑一致，处理 3 类核心消息：

- **错误消息（GST_MESSAGE_ERROR）**：打印错误信息，退出主循环。
- **流结束（GST_MESSAGE_EOS）**：打印提示，退出主循环。
- **状态变更（GST_MESSAGE_STATE_CHANGED）**：当 `playbin` 进入 `PLAYING` 状态时，调用 `analyze_streams` 解析流信息。

### 5. `handle_keyboard`：键盘输入处理（核心修改）

监听键盘输入，实现 **字幕流切换**，逻辑与上一版音轨切换类似，但目标属性改为 `current-text`：

```c
static gboolean handle_keyboard(GIOChannel *source, GIOCondition cond, CustomData *data) {
    gchar *str = NULL;

    if (g_io_channel_read_line(source, &str, NULL, NULL, NULL) == G_IO_STATUS_NORMAL) {
        int index = atoi(str);  // 读取输入的字幕索引（字符串转整数）
        // 验证索引合法性（0 ≤ 索引 < 字幕流总数）
        if (index < 0 || index >= data->n_text) {
            g_printerr("Index out of bounds\n");
        } else {
            // 修改 playbin 的 "current-text" 属性，切换字幕流
            g_print("Setting current subtitle stream to %d\n", index);
            g_object_set(data->playbin, "current-text", index, NULL);
        }
    }
    g_free(str);
    return TRUE;
}
```

- 关键差异：上一版修改 `current-audio`（音频流索引），当前版修改 `current-text`（字幕流索引）。
- 交互逻辑：输入字幕流索引（如 0）并回车，播放器会立即切换到对应字幕流（若有多个字幕流，如内置+外部，可循环切换）。

## 三、关键 GStreamer 字幕相关知识点
### 1. `playbin` 字幕相关核心属性

| 属性名               | 功能描述                                  | 示例值                                     |
| -------------------- | ----------------------------------------- | ------------------------------------------ |
| `suburi`             | 加载外部字幕文件的 URI（本地/网络均可）   | `https://xxx/xxx.srt` 或 `file:///xxx.srt` |
| `subtitle-font-desc` | 字幕字体描述（格式：`字体名称, 字号`）    | `"Sans, 18"`、`"Arial, 24"`                |
| `n-text`             | 字幕流总数（含视频内置字幕+外部加载字幕） | 1（仅外部字幕）、2（内置+外部）            |
| `current-text`       | 当前激活的字幕流索引（-1 表示禁用字幕）   | 0（第一个字幕流）、1（第二个字幕流）       |
| `flags`（含 TEXT）   | 启用/禁用字幕功能（`GST_PLAY_FLAG_TEXT`） | 启用：`flags \| = GST_PLAY_FLAG_TEXT`      |

### 2. 字幕格式支持

- `playbin` 支持的字幕格式取决于安装的 GStreamer 插件：
  - SRT（最常见）：需 `gst-plugins-good` 中的 `srtdec` 插件。
  - ASS/SSA（带样式字幕）：需 `gst-plugins-good` 中的 `assdec` 插件。
  - 内置字幕（如 MKV 内嵌 SRT）：自动识别，无需额外配置。

### 3. 字幕流索引规则

- 视频内置的字幕流优先排序（索引从 0 开始）。
- 外部加载的字幕（`suburi`）会作为“额外字幕流”，索引紧跟在最后一个内置字幕流之后。
- 若输入 `-1` 作为 `current-text` 值，会禁用所有字幕。

## 四、编译与运行说明
### 1. 依赖插件

需安装 GStreamer 核心库及字幕相关插件（以 Ubuntu 为例）：

```bash
gcc playback-tutorial-2.c -o playback-tutorial-2 `pkg-config --cflags --libs gstreamer-1.0`
```
- `gstreamer1.0-plugins-base`：核心音视频渲染插件。
- `gstreamer1.0-plugins-good`：包含 SRT/ASS 字幕解析插件（`srtdec`/`assdec`）。
- `gstreamer1.0-plugins-ugly`：支持部分专利编码（如 MP3），确保视频音频正常解码。

### 2. 编译命令

```bash
gcc -o subtitle_switcher subtitle_switcher.c `pkg-config --cflags --libs gstreamer-1.0 glib-2.0`
```

### 3. 运行效果

- 启动后自动播放视频，加载希腊语外部字幕（屏幕底部显示 Sans 18号字字幕）。
- 终端打印流信息（示例）：
  ```mathematica
  1 video stream(s), 1 audio stream(s), 1 text stream(s)

  video stream 0:
    codec: Theora

  audio stream 0:
    codec: Vorbis
    language: en
    bitrate: 128000

  subtitle stream 0:
    language: gr  // 外部加载的希腊语字幕

  Currently playing video stream 0, audio stream 0 and subtitle stream 0
  Type any number and hit ENTER to select a different subtitle stream
  ```
- 输入字幕索引（如 0）并回车，会切换到对应字幕流（若只有 1 个字幕流，输入 0 无变化；若有多个，会切换）。

## 六、常见问题排查
### 1. 字幕不显示

- 检查 `flags` 是否启用字幕（`GST_PLAY_FLAG_TEXT`）。
- 检查 `suburi` 是否正确（网络字幕需确保联网，本地字幕需用 `file:///绝对路径`）。
- 检查是否安装 `gst-plugins-good` 插件（`srtdec` 是关键）。
- 验证字幕文件是否与视频时间轴匹配（避免字幕提前/延迟）。

### 2. 字幕样式不生效

- 确保系统安装了指定的字体（如 `Sans` 是系统默认字体，若指定 `Arial` 需安装对应字体）。
- 字体描述格式是否正确（必须是 `“字体名称, 字号”`，逗号后需加空格）。

### 3. 切换字幕流无反应

- 检查输入的索引是否在 `[0, n_text-1]` 范围内（终端会打印 `n_text` 即字幕流总数）。
- 若只有 1 个字幕流，切换索引 0 无明显变化（正常现象）。
