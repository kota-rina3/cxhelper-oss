# cxhelper-oss

闭源 `CxHelperExt.node`（学习通客户端 `cxhelper@1.1.7` 的 Node 原生辅助模块）
的开源替代实现。**接口完全一致，drop-in 可替换**，可在多个平台编译。

## 背景

客户端通过 `cxhelper/dist/index.js` 封装层加载
`native/<platform>/CxHelperExt.node`。Linux 版原生模块只导出 3 个函数
（其余 `GetAudioVolume`/`GetWindowThumbnail` 等仅 win32 二进制实现，
JS 层自带缺省值兜底）。本工程按闭源 Linux 二进制的逆向分析结果，
用零依赖的 C++17 + 纯 C N-API 重写了这 3 个函数，并修复了闭源实现
的一个功能缺陷（见下文）。

## 导出接口

与闭源版签名、语义完全一致：

| 导出 | 签名 | 行为 |
|---|---|---|
| `GetDesktopWindowInfo` | `() => string` | 递归枚举根窗口树，返回 JSON 数组：`[{"winId":123,"title":"…","x":0,"y":0,"width":1920,"height":961,"level":0}, …]`，按堆叠层级排序（顶层在前）。X11 不可用时返回 `"null"`（与闭源一致） |
| `GetClipboardFilePath` | `() => string` | 向当前 CLIPBOARD selection 请求 `text/uri-list`，解析 `file://` URI（含百分号解码），返回 JSON 数组：`["/path/a.txt", …]` |
| `SetClipboardFilePath` | `(paths: string[]) => boolean` | 将路径编码为标准 `file://` URI 列表并成为 CLIPBOARD 的 owner；参数非字符串数组时抛 `Error: Array of file paths expected` |

### 对闭源版的改进

闭源版 `SetClipboardFilePath` 只调用了 `XSetSelectionOwner` 声明自己
是 CLIPBOARD 的 owner，**从不响应 SelectionRequest**——因此外部程序
（文件管理器等）无法真正从它那里读取文件，Ctrl+V 粘贴文件会失败。
本实现用后台线程维护 selection 服务循环，完整响应
`TARGETS` / `text/uri-list` / `UTF8_STRING` / `STRING` 请求，
已验证与闭源 `GetClipboardFilePath` **跨实现互通**。

### 行为规格（逆向还原）

窗口枚举的收集条件（与闭源汇编逐条对应）：

* `XGetWindowAttributes` 成功
* `c_class == InputOutput`
* `width > 1 && height > 1`
* 标题：先读 `_NET_WM_NAME`（UTF8_STRING），空则 `XFetchName`
* 标题非空且不等于 `"Untitled"`（闭源硬编码排除）

关键点：**过滤与递归相互独立**——无标题的窗口管理器 frame 仍会
递归进入，否则真正的客户窗口（在 frame 内层）会被整体漏掉。

剪贴板协议：

* Get：`XConvertSelection(CLIPBOARD, text/uri-list, …)` 同步等待，
  超时 500ms；系统剪贴板无 uri-list 时回退读本模块持有的数据
* Set：路径 → `file:///…`（RFC 8089 百分号编码）+ CRLF 分隔的
  标准 uri-list；后台线程持有 selection 并响应请求；
  收到 `SelectionClear`（他人接管剪贴板）时自动释放

## 构建

依赖：CMake ≥ 3.15、C++17 编译器；Linux 另需 X11 开发库
（`libx11-dev`）。N-API 头文件自动获取：优先
`-DNODE_HEADERS_DIR=<dir>`，其次系统路径（`/usr/include/node` 等），
最后自动从 nodejs.org 下载。

```bash
# Linux
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
# 产物：build/CxHelperExt.node

# Windows (MSVC)
cmake -B build -DCMAKE_BUILD_TYPE=Release -A x64
cmake --build build --config Release

# macOS
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

## 替换进客户端

```bash
# 建议先备份
cp <app>/node_modules/cxhelper/native/linux-x64/CxHelperExt.node{,.bak}
cp build/CxHelperExt.node \
   <app>/node_modules/cxhelper/native/linux-x64/CxHelperExt.node
```

## 测试

```bash
node test/test.js [闭源.node路径]
# 或在 Electron 下：
ELECTRON_RUN_AS_NODE=1 ./cxstudy test/test.js <闭源路径>
```

实测（deepin 25 / DDE / X11，Electron 42.3.0）：**12/12 PASS**，
含窗口数据与 `xdotool` 交叉验证、剪贴板 Set→Get 回环、
与闭源版跨实现互通。

## 平台支持

| 平台 | 状态 |
|---|---|
| linux-x64 / linux-arm64 | 完整实现（X11） |
| win32-x64 / win32-ia32 / macOS | 桩实现（行为同闭源 JS 层缺省分支），可编译、可加载；欢迎提交真实现（EnumWindows/CF_HDROP、CGWindowList/NSPasteboard） |

CI（`.github/workflows/build.yml`）自动构建 linux-x64、linux-arm64
（交叉编译）、macOS arm64/x64、windows-x64 五个目标并上传产物。

## 工程结构

```
src/
├── cxhelper_ext.cpp     # N-API 模块入口与注册
├── napi_util.h          # 纯 C N-API 辅助（无 node-addon-api 依赖）
├── json_writer.h        # JSON 转义 / URI 编解码
├── window_linux.cpp     # GetDesktopWindowInfo（X11）
├── clipboard_linux.cpp  # 剪贴板 selection（X11 + 后台服务线程）
└── stubs_other.cpp      # win32/macOS 桩
```

## 实现细节备忘

* `XInitThreads()` 必须在模块加载时（任何 Xlib 调用之前）调用——
  放在惰性初始化里太晚，属未定义行为。
* Electron 下 `process.exit()` **不触发** napi env cleanup hook，
  后台 `std::thread` 未 join 会在静态析构时 `std::terminate`；
  必须用 `std::atexit`（按注册逆序先于线程对象的析构执行）兜底。

## License

MIT
