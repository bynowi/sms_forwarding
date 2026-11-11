#include <cassert>
#include <array>
#include <atomic>
#include <cstdarg>
#include <cstddef>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <utility>
#include <vector>
#include "idf_push_response.h"
#include "cJSON.h"

// 只在主机测试安装分配钩子，生产固件不改 cJSON 的全局分配器。
size_t json_live = 0, json_peak = 0;
struct alignas(std::max_align_t) Allocation { size_t size; };
void* tracked_allocate(size_t size) {
    auto* block = static_cast<Allocation*>(std::malloc(sizeof(Allocation) + size));
    if (!block) return nullptr;
    block->size = size;
    json_live += size; json_peak = std::max(json_peak, json_live);
    return block + 1;
}
void tracked_free(void* ptr) {
    if (!ptr) return;
    auto* block = static_cast<Allocation*>(ptr) - 1;
    json_live -= block->size; std::free(block);
}

// @通道结构@
// @蜂窝结构@
using esp_err_t = int;
constexpr int ESP_OK = 0, ESP_FAIL = 1, ESP_ERR_NO_MEM = 2, HTTP_TIMEOUT_MS = 5000;
constexpr int HTTP_METHOD_GET = 0, HTTP_METHOD_POST = 1, HTTP_EVENT_ON_DATA = 1;
bool wifi = true;
int mock_status = 200, mock_error = ESP_OK, chunk_size = 7;
std::string mock_body, last_log, sent_body, sent_url;
std::string mock_signing_error, signed_data, signing_key;
int signing_calls = 0, http_calls = 0;
int64_t mock_millis = 1794205446000;
struct Client;
using esp_http_client_handle_t = Client*;
struct esp_http_client_event_t {
    int event_id, data_len;
    void* data;
    void* user_data;
    esp_http_client_handle_t client;
};
struct esp_http_client_config_t {
    const char* url;
    int timeout_ms, buffer_size_tx;
    bool keep_alive_enable;
    void* crt_bundle_attach;
    esp_err_t (*event_handler)(esp_http_client_event_t*);
    void* user_data;
};
struct Client { esp_http_client_config_t config; };
void* esp_crt_bundle_attach = nullptr;
Client* esp_http_client_init(const esp_http_client_config_t* config) { sent_url = config->url; return new Client{*config}; }
void esp_http_client_set_method(Client*, int) {}
void esp_http_client_set_header(Client*, const char*, const char*) {}
void esp_http_client_set_post_field(Client*, const char* data, size_t n) { sent_body.assign(data, n); }
int esp_http_client_get_status_code(Client*) { return mock_status; }
esp_err_t esp_http_client_perform(Client* client) {
    ++http_calls;
    for (size_t offset = 0; offset < mock_body.size(); offset += chunk_size) {
        size_t n = std::min(static_cast<size_t>(chunk_size), mock_body.size() - offset);
        esp_http_client_event_t event{HTTP_EVENT_ON_DATA, static_cast<int>(n), &mock_body[offset], client->config.user_data, client};
        assert(client->config.event_handler(&event) == ESP_OK);
    }
    return mock_error;
}
void esp_http_client_cleanup(Client* client) { delete client; }
struct Wifi { bool staConnected; };
Wifi idf_wifi_get_status() { return {wifi}; }
struct IdfSimSettingsView { bool dataEnabled = true; std::string apn; };
IdfSimSettingsView idf_config_get_sim_settings_view() { return {}; }
esp_err_t idf_modem_cellular_http_request(const std::string& url, const char*, const char*, const std::string& body,
                                        const IdfCellularHttpConfig&, IdfCellularHttpResult& result) {
    sent_url = url; sent_body = body; result.httpStatus = mock_status;
    ++http_calls;
    result.responseComplete = mock_error == ESP_OK;
    result.response.append(mock_body.data(), mock_body.size());
    return mock_error != ESP_OK ? mock_error : (mock_status >= 400 ? ESP_FAIL : ESP_OK);
}
const char* esp_err_to_name(int) { return "ESP_FAIL"; }
void idf_logf(const char* format, ...) {
    char buf[1024]; va_list args; va_start(args, format); vsnprintf(buf, sizeof(buf), format, args); va_end(args); last_log = buf;
}
void idf_log_line(const char* text) { last_log = text; }
bool channel_valid(const IdfPushChannel& ch) { return ch.enabled; }
std::string local_phone_number() { return {}; }
std::string json_escape(const std::string& s) { return s; }
std::string html_escape(const std::string& s) { return s; }
std::string url_encode(const std::string& s) { return s; }
std::string hmac_sha256_base64(const std::string& data, const std::string& key, std::string* error = nullptr) {
    ++signing_calls; signed_data = data; signing_key = key;
    if (error) *error = mock_signing_error;
    return mock_signing_error.empty() ? "test-signature" : "";
}
int64_t utc_millis() { return mock_millis; }
void json_prop(std::string& out, const char* key, const std::string& value) { out += std::string("\"") + key + "\":\"" + value + "\""; }
std::string apply_push_placeholders(const std::string& s, const std::string&, const std::string&, const std::string&, const std::string&) { return s; }
struct BarkTarget { bool ok = true; std::string url = "https://example.invalid/push", device_key = "key"; };
BarkTarget bark_target_from_channel(const IdfPushChannel&) { return {}; }
void append_bark_params(std::string&, const std::string&, const std::string&, const std::string&, const std::string&, const std::string&) {}
// @HTTP函数@
// @发送函数@
// @队列结构@
std::array<PushJob, 16> s_push_jobs;
std::atomic<bool> s_busy{false};
int s_mutex = 1, completed = 0, cancelled = 0;
bool forwarded = true;
IdfPushChannel queued_channel;
constexpr int portMAX_DELAY = 0, pdTRUE = 1, PUSH_RETRY_MAX = 6;
int pdMS_TO_TICKS(int value) { return value; }
int xSemaphoreTake(int, int) { return pdTRUE; }
void xSemaphoreGive(int) {}
void ensure_init() {}
bool low_heap_defer() { return false; }
bool http_transport_available() { return true; }
int64_t esp_timer_get_time() { return 0; }
bool channel_cooling(uint8_t, int64_t) { return false; }
bool idf_config_get_push_channel(uint8_t, IdfPushChannel& channel) { channel = queued_channel; return true; }
void cancel_forward_completion(uint32_t) { ++cancelled; }
void note_channel_result(uint8_t, bool) {}
void note_forward_target_success(uint32_t) { ++completed; }
void idf_inbox_set_forwarded(uint32_t, bool value) { forwarded = value; }
uint32_t backoff_seconds(uint8_t, uint32_t) { return 20; }
// @队列函数@

IdfPushResponseResult check(int type, const std::string& body, const std::string& rule = "") {
    IdfHttpResponseBody response; response.append(body.data(), body.size());
    return idf_push_check_response(static_cast<uint8_t>(type), 200, response, rule);
}

int main() {
    cJSON_Hooks hooks{tracked_allocate, tracked_free};
    cJSON_InitHooks(&hooks);
    struct Case { int type; const char* good; const char* bad; };
    const Case cases[] = {
        {1, "{\"success\":true}", "{\"success\":false}"},
        {2, "{\"code\":200}", "{\"code\":400}"},
        {3, "{\"code\":0}", "{\"code\":500}"},
        {4, "{\"errcode\":0}", "{\"errcode\":310000,\"errmsg\":\"keywords not in content SECRET\"}"},
        {5, "{\"code\":200}", "{\"code\":903}"},
        {6, "{\"code\":0}", "{\"code\":40001}"},
        {7, "{\"ok\":true}", "{\"ok\":false}"},
        {8, "{\"code\":0}", "{\"code\":19024,\"msg\":\"SECRET\"}"},
        {9, "{\"id\":123,\"appid\":1}", "{\"errorCode\":401}"},
        {10, "{\"ok\":true,\"result\":{\"message_id\":1}}", "{\"ok\":false,\"error_code\":400,\"description\":\"SECRET\"}"},
    };
    // 从真实发送入口经过 WiFi 分块回调/蜂窝接口到日志与返回值，防止只修了未被调用的解析器。
    for (bool connected : {true, false}) for (const auto& item : cases) {
        wifi = connected;
        IdfPushChannel ch; ch.enabled = true; ch.type = static_cast<uint8_t>(item.type); ch.url = "https://example.invalid"; ch.name = "测试";
        ch.key1 = "SCTtest"; ch.key2 = "token"; ch.customBody = "{}";
        std::string detail;
        mock_body = item.bad;
        assert(!send_to_channel(ch, "sender", "body", "time", false, &detail));
        assert(last_log.find("SECRET") == std::string::npos && detail.find("SECRET") == std::string::npos);
        assert(detail.find("HTTP 200") != std::string::npos);
        mock_body = item.good;
        assert(send_to_channel(ch, "sender", "body", "time", false, &detail));
        mock_status = 403;
        assert(!send_to_channel(ch, "sender", "body", "time"));
        mock_status = 200;
        mock_error = ESP_FAIL;
        assert(!send_to_channel(ch, "sender", "body", "time"));
        mock_error = ESP_OK;
    }
    assert(check(8, "{\"StatusCode\":0,\"StatusMessage\":\"success\"}").ok);
    assert(!check(8, "{\"code\":19024,\"StatusCode\":0}").ok);
    for (int type : {2,4,5,6,8,9,10}) {
        for (const char* body : {"", "OK", "{}", "{bad}", "{\"code\":0}garbage"}) assert(!check(type, body).ok);
    }
    assert(!check(4, "{\"errcode\":\"0\"}").ok);
    assert(!check(4, "{\"errcode\":0.5}").ok);
    assert(!check(4, "{\"errcode\":0,\"errcode\":310000}").ok);
    assert(!check(10, "{\"ok\":true}").ok);
    assert(!check(1, "{\"code\":0,\"success\":false}").ok);
    assert(check(1, "OK").httpOnly && check(1, "{}").httpOnly);
    assert(!check(1, "{bad}").ok);
    assert(check(7, "{\"data\":{\"status\":\"ok\"}}", "data.status=\"ok\"").ok);
    assert(!check(7, "{\"data\":{\"status\":\"bad\"}}", "data.status=\"ok\"").ok);
    assert(!check(7, "{\"code\":\"0\"}", "code=0").ok);
    assert(!check(7, "{}", "success=true").ok);
    for (const char* rule : {"code=0", " success = true ", "data.status=\"ok\"", ""}) assert(idf_push_validate_success_rule(rule));
    for (const char* rule : {"code", ".code=0", "data..code=0", "code=", "code={}", "code=null", "code=0 garbage"}) assert(!idf_push_validate_success_rule(rule));
    assert(!check(4, std::string(13, '[') + "0" + std::string(13, ']')).ok);
    assert(!check(4, std::string("{\"errcode\":0}\0SECRET", 20)).ok);
    IdfHttpResponseBody long_body; std::string data(IDF_HTTP_RESPONSE_MAX + 1, 'x');
    long_body.append(data.data(), data.size());
    assert(long_body.body.size() == IDF_HTTP_RESPONSE_MAX && long_body.truncated);
    assert(!idf_push_check_response(1, 200, long_body).ok);
    long_body = {}; long_body.invalid = true;
    assert(!idf_push_check_response(1, 200, long_body).ok);
    assert(idf_push_serverchan_url("SCTtest", "") == "https://sctapi.ftqq.com/SCTtest.send");
    assert(idf_push_serverchan_url("sctp123tABC", "") == "https://123.push.ft07.com/send/sctp123tABC.send");
    assert(idf_push_serverchan_url("sctpBAD", "").empty());
    assert(idf_push_serverchan_url("sctp123tABC", "https://custom.invalid/api.send") == "https://custom.invalid/api.send");
    // 时间检查先于签名；两个原因必须分别呈现，均不得发出 HTTP 请求。
    for (int type : {4, 8}) {
        IdfPushChannel ch; ch.enabled = true; ch.type = static_cast<uint8_t>(type);
        ch.url = "https://example.invalid"; ch.key1 = "SECtest";
        std::string detail;
        signing_calls = http_calls = 0; mock_millis = 0;
        assert(!send_to_channel(ch, "sender", "body", "time", false, &detail));
        assert(detail.find("时间尚未同步") != std::string::npos && signing_calls == 0 && http_calls == 0);
        mock_millis = 1794205446000; mock_signing_error = "HMAC-SHA256 计算失败 (PSA -134)";
        assert(!send_to_channel(ch, "sender", "body", "time", false, &detail));
        assert(detail.find("签名生成失败") != std::string::npos && detail.find("PSA -134") != std::string::npos);
        assert(detail.find("时间尚未同步") == std::string::npos && http_calls == 0);
        mock_signing_error.clear();
        mock_body = type == 4 ? "{\"errcode\":0}" : "{\"code\":0}";
        assert(send_to_channel(ch, "sender", "body", "time", false, &detail));
        if (type == 4) {
            assert(signing_key == "SECtest" && signed_data == "1794205446000\nSECtest");
            assert(sent_url.find("timestamp=1794205446000") != std::string::npos);
        } else {
            assert(signing_key == "1794205446\nSECtest" && signed_data.empty());
            assert(sent_body.find("\"timestamp\":\"1794205446\"") != std::string::npos);
        }
    }
    // 真实后台任务必须把平台拒绝送回重试队列，而不能标记为转发完成。
    queued_channel.enabled = true; queued_channel.type = 8; queued_channel.url = "https://example.invalid";
    for (bool connected : {true, false}) {
        wifi = connected; mock_body = "{\"code\":19024}";
        completed = cancelled = 0; forwarded = true; s_push_jobs = {};
        s_push_jobs[0].used = true; s_push_jobs[0].inboxId = 1; s_push_jobs[0].completionId = 1;
        assert(process_push_one());
        assert(completed == 0 && s_push_jobs[0].used && s_push_jobs[0].attempts == 1);
        s_push_jobs[0].nextUs = 0; s_push_jobs[0].attempts = PUSH_RETRY_MAX - 1;
        assert(process_push_one());
        assert(completed == 0 && cancelled == 1 && !forwarded && !s_push_jobs[0].used);
        mock_body = "{\"code\":0}";
        s_push_jobs[0].used = true; s_push_jobs[0].completionId = 1;
        assert(process_push_one());
        assert(completed == 1 && !s_push_jobs[0].used);
    }
    // 测量最坏方向的响应：大量小节点加上长字符串，不只测试常见的几十字节响应。
    std::string stress = "{\"code\":0,\"data\":[";
    for (int i = 0; i < 240; ++i) stress += i ? ",\"\"" : "\"\"";
    stress += "],\"padding\":\"" + std::string(6000, 'x') + "\"}";
    IdfHttpResponseBody memory_response;
    for (char byte : stress) memory_response.append(&byte, 1);
    json_peak = 0;
    auto verdict = idf_push_check_response(6, 200, memory_response);
    assert(json_live == 0);
    printf("memory probe: accepted=%d, response size=%zu capacity=%zu, JSON allocation peak=%zu bytes\n",
           verdict.ok, memory_response.body.size(), memory_response.body.capacity(), json_peak);
    puts("push response regressions passed: 10 channels, WiFi/cellular, rejection/success/privacy/rules");
}
