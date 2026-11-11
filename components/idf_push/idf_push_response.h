#pragma once

#include <stdint.h>
#include <string>
#include "idf_http_response.h"

enum : uint8_t {
    PUSH_TYPE_NONE = 0, PUSH_TYPE_POST_JSON = 1, PUSH_TYPE_BARK = 2,
    PUSH_TYPE_GET = 3, PUSH_TYPE_DINGTALK = 4, PUSH_TYPE_PUSHPLUS = 5,
    PUSH_TYPE_SERVERCHAN = 6, PUSH_TYPE_CUSTOM = 7, PUSH_TYPE_FEISHU = 8,
    PUSH_TYPE_GOTIFY = 9, PUSH_TYPE_TELEGRAM = 10,
};

struct IdfPushResponseResult {
    bool ok = false;
    // 只确认 HTTP 的旧通用接口必须明确标识，不冒充平台已接收。
    bool httpOnly = false;
    std::string message;
};

bool idf_push_validate_success_rule(const std::string& rule);
IdfPushResponseResult idf_push_check_response(uint8_t type, int status,
                                            const IdfHttpResponseBody& response,
                                            const std::string& rule = "");
// 默认根据 SendKey 选择 Turbo/Server酱³；显式 URL 保持不变。
std::string idf_push_serverchan_url(std::string key, std::string base);
