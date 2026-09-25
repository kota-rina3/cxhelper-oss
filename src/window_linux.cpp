// window_linux.cpp — GetDesktopWindowInfo 的 X11 实现（Linux）
// 行为规格依据对闭源 CxHelperExt.node 的逆向分析：
//   · 递归枚举根窗口树（过滤与收集相互独立：无标题的容器/窗口管理器
//     frame 仍会递归进入，其子窗口正常收集）
//   · 仅收集 InputOutput、width>1、height>1 的窗口
//   · 标题取 _NET_WM_NAME（UTF8_STRING），空则取 WM_NAME
//   · 标题为 "Untitled" 的窗口被排除
//   · 返回按堆叠层级排序的 JSON 数组字符串；X11 不可用时返回 "null"
// cxhelper-oss · MIT License
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cstring>

#include "json_writer.h"

namespace cxhelper {

struct WindowInfo {
    Window winId = 0;
    std::string title;
    int x = 0, y = 0;
    unsigned int width = 0, height = 0;
    long level = 0; // 堆叠层级：越大越靠顶层
};

namespace {

// 读取窗口的文本属性（8-bit property）
std::string read_text_property(Display* dpy, Window w, Atom prop, Atom type) {
    std::string result;
    Atom actual = None;
    int fmt = 0;
    unsigned long n = 0, left = 0;
    unsigned char* data = nullptr;
    if (XGetWindowProperty(dpy, w, prop, 0, 4096, False, type,
                           &actual, &fmt, &n, &left, &data) == Success &&
        data) {
        if (fmt == 8 && n > 0) {
            result.assign(reinterpret_cast<char*>(data), n);
            size_t z = result.find('\0');
            if (z != std::string::npos) result.resize(z);
        }
        XFree(data);
    }
    return result;
}

void collect_recursive(Display* dpy, Window w,
                       std::vector<WindowInfo>& out, long level) {
    Window root = None, parent = None;
    Window* children = nullptr;
    unsigned int n = 0;
    if (!XQueryTree(dpy, w, &root, &parent, &children, &n)) return;
    if (children) {
        // XQueryTree 按自底向顶的堆叠顺序返回，越靠后越在顶层
        for (unsigned int i = 0; i < n; ++i) {
            Window c = children[i];
            XWindowAttributes attr;
            if (!XGetWindowAttributes(dpy, c, &attr)) continue;

            // 收集条件（与递归相互独立——frame 无标题也要递归进去）
            if (attr.c_class == InputOutput && attr.width > 1 &&
                attr.height > 1) {
                static Atom utf8 = XInternAtom(dpy, "UTF8_STRING", True);
                Atom net_name = XInternAtom(dpy, "_NET_WM_NAME", True);
                std::string title;
                if (net_name != None && utf8 != None)
                    title = read_text_property(dpy, c, net_name, utf8);
                if (title.empty()) {
                    char* nm = nullptr;
                    if (XFetchName(dpy, c, &nm) && nm) {
                        title = nm;
                        XFree(nm);
                    }
                }
                if (!title.empty() && title != "Untitled") {
                    WindowInfo info;
                    info.winId = c;
                    info.title = std::move(title);
                    info.x = attr.x;
                    info.y = attr.y;
                    info.width = attr.width;
                    info.height = attr.height;
                    info.level = level;
                    out.push_back(std::move(info));
                }
            }

            collect_recursive(dpy, c, out, level + 1);
        }
        XFree(children);
    }
}

} // namespace

// 返回 JSON 数组字符串；display 打不开时返回 "null"（与闭源版一致）
std::string get_desktop_window_info_json() {
    Display* dpy = XOpenDisplay(nullptr);
    if (!dpy) return "null";

    std::vector<WindowInfo> list;
    collect_recursive(dpy, DefaultRootWindow(dpy), list, 0);
    XCloseDisplay(dpy);

    // 顶层窗口排前面
    std::sort(list.begin(), list.end(),
              [](const WindowInfo& a, const WindowInfo& b) {
                  return a.level > b.level;
              });

    std::string json = "[";
    char buf[160];
    for (size_t i = 0; i < list.size(); ++i) {
        if (i) json += ',';
        json += '{';
        json += "\"winId\":";
        snprintf(buf, sizeof(buf), "%lld", (long long)list[i].winId);
        json += buf;
        json += ",\"title\":";
        json_escape_append(json, list[i].title);
        snprintf(buf, sizeof(buf),
                 ",\"x\":%d,\"y\":%d,\"width\":%u,\"height\":%u,\"level\":%ld",
                 list[i].x, list[i].y, list[i].width, list[i].height,
                 list[i].level);
        json += buf;
        json += '}';
    }
    json += ']';
    return json;
}

} // namespace cxhelper
