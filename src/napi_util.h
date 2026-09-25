// napi_util.h — 纯 C N-API 辅助函数（无 node-addon-api 依赖）
// cxhelper-oss · MIT License
#pragma once
#include <node_api.h>
#include <string>

namespace cxhelper {

inline std::string to_string(napi_env env, napi_value v) {
    size_t len = 0;
    napi_get_value_string_utf8(env, v, nullptr, 0, &len);
    std::string s(len, '\0');
    if (len) napi_get_value_string_utf8(env, v, &s[0], len + 1, &len);
    return s;
}

inline napi_value from_string(napi_env env, const std::string& s) {
    napi_value v = nullptr;
    napi_create_string_utf8(env, s.c_str(), s.size(), &v);
    return v;
}

inline napi_value from_bool(napi_env env, bool b) {
    napi_value v = nullptr;
    napi_get_boolean(env, b, &v);
    return v;
}

inline bool is_array_of_strings(napi_env env, napi_value v) {
    bool is = false;
    if (napi_is_array(env, v, &is) != napi_ok || !is) return false;
    uint32_t n = 0;
    napi_get_array_length(env, v, &n);
    for (uint32_t i = 0; i < n; ++i) {
        napi_value e = nullptr;
        napi_get_element(env, v, i, &e);
        napi_valuetype t = napi_undefined;
        napi_typeof(env, e, &t);
        if (t != napi_string) return false;
    }
    return true;
}

// 抛出 Error 并返回 nullptr
inline napi_value throw_error(napi_env env, const std::string& msg) {
    napi_throw_error(env, nullptr, msg.c_str());
    return nullptr;
}

} // namespace cxhelper
