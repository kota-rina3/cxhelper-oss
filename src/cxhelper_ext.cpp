// cxhelper_ext.cpp — N-API 模块入口
//
// 导出（与闭源 CxHelperExt.node 完全一致，drop-in 可替换）：
//   · GetDesktopWindowInfo() -> string   桌面窗口信息 JSON 数组
//   · GetClipboardFilePath()  -> string   剪贴板文件路径 JSON 数组
//   · SetClipboardFilePath(paths: string[]) -> boolean
//
// cxhelper-oss · MIT License
#include <node_api.h>
#include <string>
#include <vector>
#include <cstdlib>

#ifdef __linux__
#include <X11/Xlib.h>
#endif

#include "napi_util.h"

namespace cxhelper {

#if defined(__linux__)
std::string get_desktop_window_info_json();
std::string get_clipboard_file_paths_json();
bool set_clipboard_file_paths(const std::vector<std::string>& paths);
void shutdown_clipboard();
#else
std::string get_desktop_window_info_json();
std::string get_clipboard_file_paths_json();
bool set_clipboard_file_paths(const std::vector<std::string>& paths);
void shutdown_clipboard();
#endif

namespace {

napi_value N_GetDesktopWindowInfo(napi_env env, napi_callback_info) {
    return from_string(env, get_desktop_window_info_json());
}

napi_value N_GetClipboardFilePath(napi_env env, napi_callback_info) {
    return from_string(env, get_clipboard_file_paths_json());
}

napi_value N_SetClipboardFilePath(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value argv[1];
    napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);
    if (argc < 1 || !is_array_of_strings(env, argv[0])) {
        return throw_error(env, "Array of file paths expected");
    }
    uint32_t n = 0;
    napi_get_array_length(env, argv[0], &n);
    std::vector<std::string> paths;
    paths.reserve(n);
    for (uint32_t i = 0; i < n; ++i) {
        napi_value e = nullptr;
        napi_get_element(env, argv[0], i, &e);
        paths.push_back(to_string(env, e));
    }
    return from_bool(env, set_clipboard_file_paths(paths));
}

napi_value Init(napi_env env, napi_value exports) {
#ifdef __linux__
    // 必须在任何 Xlib 调用之前执行（模块加载是最早时机），
    // 否则多线程各自 XOpenDisplay 是未定义行为
    XInitThreads();
#endif
    const struct {
        const char* name;
        napi_value (*fn)(napi_env, napi_callback_info);
    } table[] = {
        {"GetDesktopWindowInfo", N_GetDesktopWindowInfo},
        {"GetClipboardFilePath", N_GetClipboardFilePath},
        {"SetClipboardFilePath", N_SetClipboardFilePath},
    };
    for (const auto& e : table) {
        napi_value fn = nullptr;
        napi_create_function(env, e.name, NAPI_AUTO_LENGTH, e.fn,
                             nullptr, &fn);
        napi_set_named_property(env, exports, e.name, fn);
    }
    // 进程退出时停掉剪贴板服务线程，避免 std::thread 未 join 在
    // 静态析构时触发 std::terminate。
    // 注意：Electron 下 process.exit() 不触发 napi env cleanup hook，
    // 因此必须用 atexit（按注册逆序，先于 g_worker 的析构执行）。
    std::atexit([]() noexcept { shutdown_clipboard(); });
    napi_add_env_cleanup_hook(env,
        [](void*) noexcept { shutdown_clipboard(); }, nullptr);
    return exports;
}

} // namespace

} // namespace cxhelper

NAPI_MODULE(NODE_GYP_MODULE_NAME, cxhelper::Init)
