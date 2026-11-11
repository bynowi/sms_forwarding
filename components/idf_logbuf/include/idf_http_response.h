#pragma once

#include <algorithm>
#include <string>

// WiFi 与蜂窝共用响应上限；继续排空超长响应，但绝不将截断 JSON 判为成功。
static constexpr size_t IDF_HTTP_RESPONSE_MAX = 8192;
struct IdfHttpResponseBody {
    std::string body;
    bool truncated = false;
    bool invalid = false;

    void append(const char* data, size_t size) {
        size_t take = std::min(size, IDF_HTTP_RESPONSE_MAX - body.size());
        body.append(data, take);
        truncated = truncated || take < size;
    }
};
