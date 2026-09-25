// json_writer.h — 极简 JSON 字符串构建器（无第三方依赖）
// cxhelper-oss · MIT License
#pragma once
#include <string>
#include <cstdint>

namespace cxhelper {

// 追加一个带转义的 JSON 字符串字面量（含首尾引号）
inline void json_escape_append(std::string& out, const std::string& s) {
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    out += '"';
}

// 解析 file:// URI 中的百分号编码（RFC 3986）
inline std::string uri_decode(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '%' && i + 2 < in.size() &&
            isxdigit(static_cast<unsigned char>(in[i + 1])) &&
            isxdigit(static_cast<unsigned char>(in[i + 2]))) {
            auto hex = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                return c - 'A' + 10;
            };
            out += static_cast<char>((hex(in[i + 1]) << 4) | hex(in[i + 2]));
            i += 2;
        } else if (in[i] == '+') {
            // text/uri-list 中 '+' 是字面量，不做空格转换
            out += '+';
        } else {
            out += in[i];
        }
    }
    return out;
}

inline std::string uri_encode_path(const std::string& path) {
    static const char* hexd = "0123456789ABCDEF";
    std::string out;
    out.reserve(path.size() + 8);
    for (unsigned char c : path) {
        // RFC 8089 file-path：保留 unreserved 与 '/'
        bool keep = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                    (c >= '0' && c <= '9') ||
                    c == '/' || c == '-' || c == '_' || c == '.' || c == '~';
        if (keep) {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += hexd[c >> 4];
            out += hexd[c & 0xF];
        }
    }
    return out;
}

} // namespace cxhelper
