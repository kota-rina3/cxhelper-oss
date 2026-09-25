// clipboard_linux.cpp — 剪贴板文件路径的 X11 selection 实现（Linux）
//
// 协议（与闭源版一致，且修复了其"只声明 owner 不响应请求"的缺陷）：
//   · SetClipboardFilePath(list)：把路径编码为 text/uri-list（file:// URI，
//     CRLF 分隔），成为 CLIPBOARD selection 的 owner；后台线程服务
//     SelectionRequest（TARGETS / text/uri-list / UTF8_STRING / STRING），
//     因此外部程序（文件管理器等）可以直接 Ctrl+V 粘贴文件。
//   · GetClipboardFilePath()：向当前 CLIPBOARD owner 请求 text/uri-list，
//     解析 file:// URI 得到本地路径，返回 JSON 数组字符串。
//
// cxhelper-oss · MIT License
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <sys/select.h>

#include "json_writer.h"

namespace cxhelper {

namespace {

constexpr int kSelectTimeoutMs = 500;

std::mutex g_mtx;                  // 保护 g_paths / g_has_data / g_quit
std::thread g_worker;
std::atomic<bool> g_started{false};
std::atomic<bool> g_quit{false};
std::condition_variable g_cv;      // worker 挂起等待新数据
std::vector<std::string> g_paths;  // 当前 owner 持有的路径
bool g_has_data = false;

std::string join_uris(const std::vector<std::string>& paths) {
    std::string data;
    for (const auto& p : paths) {
        data += "file://" + uri_encode_path(p) + "\r\n";
    }
    return data;
}

// 从 text/uri-list 文本解析出本地路径列表
std::vector<std::string> parse_uris(const std::string& data) {
    std::vector<std::string> out;
    size_t pos = 0;
    while (pos < data.size()) {
        size_t eol = data.find('\n', pos);
        if (eol == std::string::npos) eol = data.size();
        std::string line = data.substr(pos, eol - pos);
        pos = eol + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;   // 注释行
        if (line.rfind("file://", 0) == 0) {
            out.push_back(uri_decode(line.substr(7)));
        } else if (line.rfind("file:/", 0) == 0) {
            out.push_back(uri_decode(line.substr(6)));
        } else {
            out.push_back(uri_decode(line)); // 容错：裸路径
        }
    }
    return out;
}

// 用 XConvertSelection 同步读取当前 CLIPBOARD 的 uri-list（带超时）
std::string request_uri_list() {
    Display* dpy = XOpenDisplay(nullptr);
    if (!dpy) return std::string();

    Window w = XCreateSimpleWindow(dpy, DefaultRootWindow(dpy),
                                   0, 0, 1, 1, 0, 0, 0);
    Atom clipboard = XInternAtom(dpy, "CLIPBOARD", False);
    Atom uri = XInternAtom(dpy, "text/uri-list", False);
    Atom prop = XInternAtom(dpy, "CXHELPER_DATA", False);

    std::string result;
    XConvertSelection(dpy, clipboard, uri, prop, w, CurrentTime);
    XFlush(dpy);

    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(kSelectTimeoutMs);
    bool done = false;
    while (!done) {
        if (std::chrono::steady_clock::now() >= deadline) break;
        if (!XPending(dpy)) {
            struct timeval tv{0, 20000}; // 20ms 轮询
            fd_set fds;
            FD_ZERO(&fds);
            int fd = ConnectionNumber(dpy);
            FD_SET(fd, &fds);
            select(fd + 1, &fds, nullptr, nullptr, &tv);
            continue;
        }
        XEvent ev;
        XNextEvent(dpy, &ev);
        if (ev.type == SelectionNotify) {
            XSelectionEvent* se = &ev.xselection;
            if (se->property != None) {
                Atom actual = None;
                int fmt = 0;
                unsigned long n = 0, left = 0;
                unsigned char* data = nullptr;
                if (XGetWindowProperty(dpy, w, se->property, 0, 1 << 20,
                                       False, AnyPropertyType, &actual,
                                       &fmt, &n, &left, &data) == Success &&
                    data) {
                    result.assign(reinterpret_cast<char*>(data),
                                  fmt == 8 ? n : n * (fmt / 8));
                    XFree(data);
                }
                XDeleteProperty(dpy, w, se->property);
            }
            done = true;
        }
    }
    XDestroyWindow(dpy, w);
    XCloseDisplay(dpy);
    return result;
}

// 服务线程：维持 owner 身份并响应 SelectionRequest
void worker_loop() {
    Display* dpy = XOpenDisplay(nullptr);
    if (!dpy) return;

    Window w = XCreateSimpleWindow(dpy, DefaultRootWindow(dpy),
                                   0, 0, 1, 1, 0, 0, 0);
    Atom clipboard = XInternAtom(dpy, "CLIPBOARD", False);
    Atom targets = XInternAtom(dpy, "TARGETS", False);
    Atom uri = XInternAtom(dpy, "text/uri-list", False);
    Atom utf8 = XInternAtom(dpy, "UTF8_STRING", False);
    Atom string_atom = XInternAtom(dpy, "STRING", False);
    while (!g_quit.load()) {
        bool has = false;
        {
            std::unique_lock<std::mutex> lk(g_mtx);
            g_cv.wait(lk, [] { return g_has_data || g_quit.load(); });
            if (g_quit.load()) break;
            has = g_has_data;
        }
        if (!has) { continue; }

        // 有新数据：接管 CLIPBOARD
        XSetSelectionOwner(dpy, clipboard, w, CurrentTime);
        XFlush(dpy);

        // 服务请求直到数据被清（SelectionClear）或有更新
        while (!g_quit.load()) {
            {
                std::lock_guard<std::mutex> lk(g_mtx);
                if (!g_has_data) break;
            }
            if (!XPending(dpy)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(30));
                continue;
            }
            XEvent ev;
            XNextEvent(dpy, &ev);
            if (ev.type == SelectionRequest) {
                XSelectionRequestEvent* req = &ev.xselectionrequest;
                XEvent reply{};
                reply.xselection.type = SelectionNotify;
                reply.xselection.display = req->display;
                reply.xselection.requestor = req->requestor;
                reply.xselection.selection = req->selection;
                reply.xselection.target = req->target;
                reply.xselection.time = req->time;
                reply.xselection.property = req->property;

                std::string data;
                {
                    std::lock_guard<std::mutex> lk(g_mtx);
                    data = join_uris(g_paths);
                }
                bool ok = false;
                if (req->target == targets) {
                    Atom t[3] = {targets, uri, string_atom};
                    XChangeProperty(dpy, req->requestor, req->property,
                                    XA_ATOM, 32, PropModeReplace,
                                    reinterpret_cast<unsigned char*>(t), 3);
                    ok = true;
                } else if (req->target == uri || req->target == utf8 ||
                           req->target == XA_STRING) {
                    XChangeProperty(dpy, req->requestor, req->property,
                                    req->target, 8, PropModeReplace,
                                    reinterpret_cast<const unsigned char*>(
                                        data.c_str()),
                                    static_cast<int>(data.size()));
                    ok = true;
                }
                if (!ok) reply.xselection.property = None;
                XSendEvent(dpy, req->requestor, False, 0, &reply);
                XFlush(dpy);
            } else if (ev.type == SelectionClear) {
                // 别的应用接管了剪贴板
                std::lock_guard<std::mutex> lk(g_mtx);
                g_has_data = false;
                g_paths.clear();
            }
        }
    }
    XDestroyWindow(dpy, w);
    XCloseDisplay(dpy);
}

void ensure_worker() {
    if (g_started.load()) return;
    std::lock_guard<std::mutex> lk(g_mtx);
    if (!g_started.exchange(true)) {
        // XInitThreads 已在模块 Init（任何 Xlib 调用之前）调用
        g_worker = std::thread(worker_loop);
    }
}

} // namespace

// SetClipboardFilePath(paths: string[]) -> boolean
bool set_clipboard_file_paths(const std::vector<std::string>& paths) {
    if (paths.empty()) return false;
    ensure_worker();
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        g_paths = paths;
        g_has_data = true;
    }
    g_cv.notify_all();
    return true;
}

// GetClipboardFilePath() -> JSON 数组字符串
std::string get_clipboard_file_paths_json() {
    // 优先读系统剪贴板（可能来自其它应用）
    std::string data = request_uri_list();
    std::vector<std::string> paths = parse_uris(data);

    // 系统剪贴板无 uri-list 时，回退读自己持有的数据
    if (paths.empty()) {
        std::lock_guard<std::mutex> lk(g_mtx);
        if (g_has_data) paths = g_paths;
    }

    std::string json = "[";
    for (size_t i = 0; i < paths.size(); ++i) {
        if (i) json += ',';
        json_escape_append(json, paths[i]);
    }
    json += ']';
    return json;
}

void shutdown_clipboard() {
    static std::atomic<bool> done{false};
    if (done.exchange(true)) return;
    g_quit.store(true);
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        g_has_data = false;
    }
    g_cv.notify_all();
    if (g_worker.joinable()) g_worker.join();
}

} // namespace cxhelper
