// stubs_other.cpp — win32 / macOS 平台的桩实现
//
// 闭源版在这两个平台上实现了更多功能（音频音量、窗口缩略图、文件列表等），
// 但 Linux 版仅实现窗口枚举与剪贴板三个接口。为保证"任何平台都能编译
// 并 drop-in 替换"，本文件提供与闭源 JS 封装层缺省行为一致的桩：
//   · GetDesktopWindowInfo -> "null"
//   · GetClipboardFilePath -> "[]"
//   · SetClipboardFilePath -> false
// 欢迎提交 win32（EnumWindows / CF_HDROP）与 macOS（CGWindowList /
// NSPasteboard）的真实现。
//
// cxhelper-oss · MIT License
#include <string>
#include <vector>

namespace cxhelper {

std::string get_desktop_window_info_json() { return "null"; }

std::string get_clipboard_file_paths_json() { return "[]"; }

bool set_clipboard_file_paths(const std::vector<std::string>&) { return false; }

void shutdown_clipboard() {}

} // namespace cxhelper
