# GStreamer 调试工具

原文地址: <https://gstreamer.freedesktop.org/documentation/tutorials/basic/debugging-tools.html?gi-language=c>

只是翻译了一下
- 从 GStreamer 获取更多调试信息。
- 将自己的调试信息打印到 GStreamer 日志中。
- 如何获取管道图

## 打印调试信息
### 调试日志

GStreamer 及其插件充满了调试跟踪信息，这是代码中打印特别有趣信息的部分，包括时间戳、进程、类别、源代码文件、函数和元素信息。

调试输出由 GST_DEBUG 环境变量控制。这里以 GST_DEBUG=2 为例：

```bash
0:00:00.868050000  1592  09F62420 WARN  filesrc gstfilesrc.c:1044:gst_file_src_start:<filesrc0> error: No such file "non-existing-file.webm"
```

如您所见，这些信息相当多。事实上，GStreamer 调试日志非常详细，当完全启用时，可能会导致应用程序无响应（由于控制台滚动）或填满兆字节的文本文件（当重定向到文件时）。因此，日志被分类，您很少需要一次性启用所有类别。

第一个类别是调试级别，它是一个指定所需输出量的数字：

```mathematica
| # | Name    | Description                                                    |
|---|---------|----------------------------------------------------------------|
| 0 | none    | No debug information is output.                                |
| 1 | ERROR   | Logs all fatal errors. These are errors that do not allow the  |
|   |         | core or elements to perform the requested action. The          |
|   |         | application can still recover if programmed to handle the      |
|   |         | conditions that triggered the error.                           |
| 2 | WARNING | Logs all warnings. Typically these are non-fatal, but          |
|   |         | user-visible problems are expected to happen.                  |
| 3 | FIXME   | Logs all "fixme" messages. Those typically that a codepath that|
|   |         | is known to be incomplete has been triggered. It may work in   |
|   |         | most cases, but may cause problems in specific instances.      |
| 4 | INFO    | Logs all informational messages. These are typically used for  |
|   |         | events in the system that only happen once, or are important   |
|   |         | and rare enough to be logged at this level.                    |
| 5 | DEBUG   | Logs all debug messages. These are general debug messages for  |
|   |         | events that happen only a limited number of times during an    |
|   |         | object's lifetime; these include setup, teardown, change of    |
|   |         | parameters, etc.                                               |
| 6 | LOG     | Logs all log messages. These are messages for events that      |
|   |         | happen repeatedly during an object's lifetime; these include   |
|   |         | streaming and steady-state conditions. This is used for log    |
|   |         | messages that happen on every buffer in an element for example.|
| 7 | TRACE   | Logs all trace messages. Those are message that happen very    |
|   |         | very often. This is for example is each time the reference     |
|   |         | count of a GstMiniObject, such as a GstBuffer or GstEvent, is  |
|   |         | modified.                                                      |
| 9 | MEMDUMP | Logs all memory dump messages. This is the heaviest logging and|
|   |         | may include dumping the content of blocks of memory.           |
+------------------------------------------------------------------------------+
```

要启用调试输出，请将 GST_DEBUG 环境变量设置为所需的调试级别。所有低于该级别的信息也会显示（即，如果您设置 GST_DEBUG=2 ，您将同时获得 ERROR 和 WARNING 消息）。

此外，每个插件或 GStreamer 的每个部分都定义了自己的类别，因此您可以为每个单独的类别指定调试级别。例如， GST_DEBUG=2,audiotestsrc:6 将为 audiotestsrc 元素使用调试级别 6，并为所有其他元素使用级别 2。

因此， GST_DEBUG 环境变量是一个由逗号分隔的类别：级别对列表，可选地以一个级别开头，表示所有类别的默认调试级别。

'\*' 通配符也是可用的。例如， GST_DEBUG=2,audio \*:6 将为所有以 audio 开头的类别使用调试级别 6。 GST_DEBUG=\*:2 等同于 GST_DEBUG=2 。

使用 gst-launch-1.0 --gst-debug-help 获取所有已注册类别的列表。请注意，每个插件都注册自己的类别，因此，在安装或删除插件时，此列表可能会发生变化。

当 GStreamer 总线上的错误信息无法帮助你定位问题时，使用 GST_DEBUG 。通常的做法是将输出日志重定向到文件，然后稍后检查，搜索特定消息。

GStreamer 允许自定义调试信息处理器，但使用默认处理器时，调试输出的每一行内容如下：

```bash
# timestamp  process_id  thread_id  level  category  source_file:line  function_name  element_name  debug_message

0:00:00.868050000  1592  09F62420 WARN  filesrc gstfilesrc.c:1044:gst_file_src_start:<filesrc0> error: No such file "non-existing-file.webm"
```

这是信息的格式：

```mathematica
| Example          | Explained                                                 |
|------------------|-----------------------------------------------------------|
|0:00:00.868050000 | Time stamp in HH:MM:SS.sssssssss format since the start of|
|                  | the program.                                              |
|1592              | Process ID from which the message was issued. Useful when |
|                  | your problem involves multiple processes.                 |
|09F62420          | Thread ID from which the message was issued. Useful when  |
|                  | your problem involves multiple threads.                   |
|WARN              | Debug level of the message.                               |
|filesrc           | Debug Category of the message.                            |
|gstfilesrc.c:1044 | Source file and line in the GStreamer source code where   |
|                  | this message was issued.                                  |
|gst_file_src_start| Function that issued the message.                         |
|<filesrc0>        | Name of the object that issued the message. It can be an  |
|                  | element, a pad, or something else. Useful when you have   |
|                  | multiple elements of the same kind and need to distinguish|
|                  | among them. Naming your elements with the name property   |
|                  | makes this debug output more readable but GStreamer       |
|                  | assigns each new element a unique name by default.        |
| error: No such   |                                                           |
| file ....        | The actual message.                                       |
+------------------------------------------------------------------------------+
```

### 添加自己的调试信息

在你的代码中与 GStreamer 交互的部分，使用 GStreamer 的调试功能很有趣。这样，你可以在同一个文件中获取所有调试输出，并且不同消息之间的时间关系得以保留。

为此，使用 `GST_ERROR()` 、 `GST_WARNING()` 、 `GST_INFO()` 、 `GST_LOG()` 和 `GST_DEBUG()` 宏。它们接受与 `printf` 相同的参数，并且使用 `default` 类别（在输出日志中 `default` 将显示为 `Debug` 类别）。

要更改类别为更有意义的名称，在代码顶部添加这两行：

```C
GST_DEBUG_CATEGORY_STATIC (my_category);
#define GST_CAT_DEFAULT my_category
```

然后在用 gst_init() 初始化 GStreamer 之后添加这一行：

```C
GST_DEBUG_CATEGORY_INIT (my_category, "my category", 0, "This is my very own");
```

注册了一个新的类别（在您的应用程序期间有效：它不会存储在任何文件中），并将其设置为代码的默认类别。参见 GST_DEBUG_CATEGORY_INIT() 的文档。

### 获取管道图

对于您的管道开始变得太大而您无法跟踪哪些内容相互连接的情况，GStreamer 具有输出图文件的功能。这些是 .dot 文件，可以使用 GraphViz 等免费程序阅读，它们描述了您的管道拓扑结构以及每个链接协商的 caps。

在使用 playbin 或 uridecodebin 等全功能元素时也非常方便，这些元素内部实例化了多个元素。使用 .dot 文件来了解它们创建了什么管道（并在过程中学习一些 GStreamer）。

要获取 .dot 文件，只需将 GST_DEBUG_DUMP_DOT_DIR 环境变量设置为文件要放置的文件夹路径。 gst-launch-1.0 会在每个状态变化时创建一个 .dot 文件，以便您可以看到加帽协商的演变过程。取消设置该变量以禁用此功能。在您的应用程序中，您可以使用 GST_DEBUG_BIN_TO_DOT_FILE() 和 GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS() 宏在需要时生成 .dot 文件。(具体操作输出情况可以参考[3. 动态管道](../basic-tutorial-3_dynamic_pipelines//README.md#注意事项))

这里有一个示例，展示了 playbin 生成的管道类型。它非常复杂，因为 playbin 可以处理许多不同的情况：您的手动管道通常不需要这么长。如果您的手动管道开始变得很大，请考虑使用 playbin。

![playbin.png](../.img/playbin.png)
