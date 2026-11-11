#include "idf_push_response.h"

#include <ctype.h>
#include <cmath>
#include <memory>
#include "cJSON.h"

namespace {
using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;

std::string trim(const std::string& value)
{
    size_t a = 0, b = value.size();
    while (a < b && isspace(static_cast<unsigned char>(value[a]))) ++a;
    while (b > a && isspace(static_cast<unsigned char>(value[b - 1]))) --b;
    return value.substr(a, b - a);
}

Json parse(const std::string& value)
{
    // cJSON 默认允许很深的递归；先限制结构复杂度，保护 ESP32 的任务栈和堆。
    bool quoted = false, escaped = false;
    int depth = 0, tokens = 0;
    for (char ch : value) {
        if (quoted) {
            if (escaped) escaped = false;
            else if (ch == '\\') escaped = true;
            else if (ch == '"') quoted = false;
        } else if (ch == '"') quoted = true;
        else if (ch == '{' || ch == '[') { if (++depth > 12) return {nullptr, cJSON_Delete}; }
        else if (ch == '}' || ch == ']') --depth;
        if (!quoted && (ch == ',' || ch == ':') && ++tokens > 256) return {nullptr, cJSON_Delete};
    }
    const char* end = nullptr;
    Json json(cJSON_ParseWithLengthOpts(value.c_str(), value.size() + 1, &end, true), cJSON_Delete);
    if (end != value.c_str() + value.size()) json.reset();
    // 重复状态字段会让业务判定产生歧义，拒绝整个响应。
    if (cJSON_IsObject(json.get())) {
        for (const cJSON* a = json->child; a; a = a->next) {
            for (const cJSON* b = a->next; b; b = b->next) {
                if (a->string && b->string && std::string(a->string) == b->string) {
                    json.reset();
                    return json;
                }
            }
        }
    }
    return json;
}

const cJSON* field(const cJSON* json, const char* name)
{
    return cJSON_GetObjectItemCaseSensitive(json, name);
}

bool integer(const cJSON* item)
{
    return cJSON_IsNumber(item) && std::isfinite(item->valuedouble) &&
           item->valuedouble == std::trunc(item->valuedouble);
}

bool number_is(const cJSON* item, double expected)
{
    return integer(item) && item->valuedouble == expected;
}

bool scalar(const cJSON* item)
{
    return cJSON_IsBool(item) || cJSON_IsString(item) || (cJSON_IsNumber(item) && std::isfinite(item->valuedouble));
}

bool split_rule(const std::string& rule, std::string& path, Json& expected)
{
    if (rule.size() > 160) return false;
    size_t eq = rule.find('=');
    if (eq == std::string::npos) return false;
    path = trim(rule.substr(0, eq));
    if (path.empty() || path.size() > 80 || path.front() == '.' || path.back() == '.') return false;
    char previous = 0;
    for (unsigned char ch : path) {
        if (!(isalnum(ch) || ch == '_' || ch == '.') || (ch == '.' && previous == '.')) return false;
        previous = static_cast<char>(ch);
    }
    expected = parse(trim(rule.substr(eq + 1)));
    return scalar(expected.get());
}

IdfPushResponseResult rejected(uint8_t type, const cJSON* json, const char* key)
{
    IdfPushResponseResult result;
    result.message = "平台拒绝请求";
    const cJSON* code = field(json, key);
    if (integer(code) && std::abs(code->valuedouble) <= 2147483647) {
        int n = static_cast<int>(code->valuedouble);
        result.message += " " + std::string(key) + "=" + std::to_string(n);
        if (type == PUSH_TYPE_FEISHU) {
            if (n == 19024) result.message += "：关键词不匹配";
            else if (n == 19021) result.message += "：签名错误或设备时间不准确";
            else if (n == 19022) result.message += "：出口 IP 不在白名单";
            else if (n == 19001) result.message += "：Webhook 无效";
        } else if (type == PUSH_TYPE_DINGTALK && n == 310000) {
            // 只输出预定义说明；平台原文可能回显手机号、正文、URL 或密钥。
            const cJSON* msg = field(json, "errmsg");
            std::string text = cJSON_IsString(msg) ? msg->valuestring : "";
            if (text.find("keywords") != std::string::npos) result.message += "：关键词不匹配";
            else if (text.find("timestamp") != std::string::npos) result.message += "：签名或设备时间校验失败";
            else if (text.find("sign") != std::string::npos) result.message += "：签名校验失败";
            else if (text.find("whitelist") != std::string::npos) result.message += "：出口 IP 不在白名单";
            else result.message += "：机器人安全校验失败";
        } else if (type == PUSH_TYPE_PUSHPLUS) {
            if (n == 903) result.message += "：Token 无效";
            else if (n == 900) result.message += "：账号受限，请停止重复测试并检查平台额度";
            else if (n == 905) result.message += "：账号未实名认证";
            else if (n == 888) result.message += "：积分不足";
        }
    }
    return result;
}
}  // 匿名命名空间

bool idf_push_validate_success_rule(const std::string& rule)
{
    if (trim(rule).empty()) return true;
    std::string path;
    Json expected(nullptr, cJSON_Delete);
    return split_rule(rule, path, expected);
}

IdfPushResponseResult idf_push_check_response(uint8_t type, int status,
                                            const IdfHttpResponseBody& response,
                                            const std::string& rule)
{
    if (status < 200 || status >= 300) {
        Json error = response.truncated || response.invalid ? Json(nullptr, cJSON_Delete) : parse(response.body);
        const char* key = type == PUSH_TYPE_DINGTALK ? "errcode" :
                          type == PUSH_TYPE_TELEGRAM ? "error_code" :
                          type == PUSH_TYPE_GOTIFY ? "errorCode" : "code";
        if (cJSON_IsObject(error.get()) && field(error.get(), key)) return rejected(type, error.get(), key);
        return {false, false, "HTTP 状态异常"};
    }
    if (response.truncated) return {false, false, "响应超过 8KB，无法确认业务结果"};
    if (response.invalid) return {false, false, "响应数据不完整或编码无效"};
    const bool generic = type == PUSH_TYPE_POST_JSON || type == PUSH_TYPE_GET || type == PUSH_TYPE_CUSTOM;
    Json json = parse(response.body);
    if (generic && !trim(rule).empty()) {
        std::string path;
        Json expected(nullptr, cJSON_Delete);
        if (!split_rule(rule, path, expected)) return {false, false, "响应成功条件无效"};
        const cJSON* actual = json.get();
        size_t start = 0;
        while (start < path.size() && actual) {
            size_t dot = path.find('.', start);
            std::string key = path.substr(start, dot == std::string::npos ? dot : dot - start);
            actual = field(actual, key.c_str());
            if (dot == std::string::npos) break;
            start = dot + 1;
        }
        bool ok = actual && cJSON_Compare(actual, expected.get(), true);
        return {ok, false, ok ? "响应成功条件已满足" : "响应未满足成功条件"};
    }
    if (!cJSON_IsObject(json.get())) {
        // 老的文本/空响应通用端点仍可使用，但不再显示为平台确认成功。
        std::string text = trim(response.body);
        if (generic && (text.empty() || (text.front() != '{' && text.front() != '['))) {
            return {true, true, "HTTP 请求完成，未确认业务结果"};
        }
        return {false, false, "未收到有效 JSON 对象，无法确认业务结果"};
    }
    const cJSON* root = json.get();
    const char* key = nullptr;
    double success = 0;
    switch (type) {
        case PUSH_TYPE_DINGTALK: key = "errcode"; break;
        case PUSH_TYPE_FEISHU: key = field(root, "code") ? "code" : "StatusCode"; break;
        case PUSH_TYPE_BARK: key = "code"; success = 200; break;
        case PUSH_TYPE_PUSHPLUS: key = "code"; success = 200; break;
        case PUSH_TYPE_SERVERCHAN:
            if (field(field(root, "data"), "errno") && !number_is(field(field(root, "data"), "errno"), 0))
                return rejected(type, field(root, "data"), "errno");
            key = "code";
            break;
        case PUSH_TYPE_TELEGRAM:
            if (!cJSON_IsTrue(field(root, "ok"))) return rejected(type, root, "error_code");
            if (!integer(field(field(root, "result"), "message_id")))
                return {false, false, "平台未返回消息编号，无法确认业务结果"};
            break;
        case PUSH_TYPE_GOTIFY:
            if (field(root, "errorCode")) return rejected(type, root, "errorCode");
            if (!integer(field(root, "id")) || field(root, "id")->valuedouble <= 0 ||
                !integer(field(root, "appid")))
                return {false, false, "平台未返回消息记录，无法确认业务结果"};
            break;
        default: {
            if (!generic) return {false, false, "未知推送渠道"};
            bool recognized = false;
            const cJSON* error = field(root, "error");
            if (cJSON_IsTrue(error) || (cJSON_IsString(error) && error->valuestring[0] != '\0'))
                return {false, false, "平台返回 error 字段，已拒绝请求"};
            for (const char* name : {"ok", "success"}) {
                const cJSON* value = field(root, name);
                if (!value) continue;
                recognized = true;
                if (!cJSON_IsTrue(value)) return rejected(type, root, name);
            }
            for (const char* name : {"errcode", "errno", "StatusCode", "errorCode", "code"}) {
                const cJSON* value = field(root, name);
                if (!value) continue;
                recognized = true;
                if (!number_is(value, 0) && !(std::string(name) == "code" && number_is(value, 200)))
                    return rejected(type, root, name);
            }
            return {true, !recognized, recognized ? "平台已受理" : "HTTP 请求完成，未确认业务结果"};
        }
    }
    if (key && !number_is(field(root, key), success)) return rejected(type, root, key);
    return {true, false, type == PUSH_TYPE_PUSHPLUS ? "平台已受理，等待异步投递" : "平台已受理"};
}

std::string idf_push_serverchan_url(std::string key, std::string base)
{
    key = trim(key); base = trim(base);
    auto url = [](const std::string& s) { return s.rfind("https://", 0) == 0 || s.rfind("http://", 0) == 0; };
    if (key.empty() && !base.empty() && !url(base)) { key = base; base.clear(); }
    if (url(key)) { base = key; key.clear(); }
    if (base.empty() && key.rfind("sctp", 0) == 0) {
        size_t end = key.find('t', 4);
        if (end == std::string::npos || end == 4) return {};
        std::string uid = key.substr(4, end - 4);
        for (unsigned char ch : uid) if (!isdigit(ch)) return {};
        return "https://" + uid + ".push.ft07.com/send/" + key + ".send";
    }
    if (base.empty()) base = "https://sctapi.ftqq.com";
    while (!base.empty() && base.back() == '/') base.pop_back();
    return base.find(".send") != std::string::npos ? base : base + "/" + key + ".send";
}
