#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <iostream>
#include <string>
#include <vector>
#include "idf_call_urc.h"
#include "idf_call_diagnostics.h"

using esp_err_t = int;
constexpr int ESP_OK = 0, ESP_FAIL = 1, ESP_ERR_INVALID_STATE = 2;
bool s_started = true;
IdfCallDiagnostics s_call_diagnostics;
int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::cerr << "FAIL: " #expr "\n"; ++failures; } } while (0)
int64_t now_us = 1000000;
int64_t esp_timer_get_time() { return now_us; }
std::string s_uart_line_carry, preserved;
int64_t s_uart_line_started_us = 0, s_uart_wait_cmt_until_us = 0;
bool s_uart_wait_cmt_pdu = false, enabled = true, blocked = false, enqueue_ok = true;
std::vector<std::string> commands, logs, forwarded;
void append_urc_text(const std::string& text, int64_t) { preserved += text; }
std::string idf_util_trim_copy(const std::string& text) {
    size_t p = text.find_first_not_of(" \t\r\n");
    return p == std::string::npos ? "" : text.substr(p, text.find_last_not_of(" \t\r\n") - p + 1);
}
void idf_log_line(const char* text) { logs.emplace_back(text); }
void idf_logf(const char* fmt, ...) {
    char text[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    logs.emplace_back(text);
}
bool idf_config_call_notify_enabled() { return enabled; }
struct IdfSmsProcessView { std::string numberBlackList; };
IdfSmsProcessView idf_config_get_sms_process_view() { return {}; }
bool number_blacklisted(const std::string&, const std::string&) { return blocked; }
int idf_config_get_tz_offset() { return 8; }
std::string idf_util_format_epoch_local(uint32_t, int) { return "2026-11-11 13:00:00"; }
bool idf_push_enqueue_forward(const char* number, const char* body, const char*, int) {
    if (enqueue_ok) forwarded.push_back(std::string(number) + ":" + body);
    return enqueue_ok;
}
esp_err_t idf_modem_send_at(const std::string& command, uint32_t, std::string& response) {
    commands.push_back(command);
    response = "ERROR\r\n";  // 与用户实测一致：CLCC 不可用。
    return ESP_FAIL;
}
// @UART函数@
// @来电函数@
void dispatch(const std::string& raw) {
    const std::string line = idf_util_trim_copy(raw);
    // @来电分发@
}
void receive(const std::string& text) {
    preserve_uart_urcs(reinterpret_cast<const uint8_t*>(text.data()), text.size());
    while (true) {
        size_t p = preserved.find('\n');
        if (p == std::string::npos) break;
        std::string line = preserved.substr(0, p);
        preserved.erase(0, p + 1);
        dispatch(line);
    }
}
void reset() {
    now_us = 1000000;
    s_uart_line_carry.clear(); preserved.clear(); s_uart_wait_cmt_pdu = false;
    s_call_started_us = 0; s_call_last_urc_us = 0;
    s_call_notified = false; s_call_saw_ring = false; s_call_number.clear();
    enabled = true; blocked = false; enqueue_ok = true;
    commands.clear(); logs.clear(); forwarded.clear();
}
bool logged(const char* token) {
    return std::any_of(logs.begin(), logs.end(), [token](const std::string& line) {
        return line.find(token) != std::string::npos;
    });
}
void test_diagnostics() {
    reset();
    receive("+VENDORCALL: 1\r\n"); CHECK(logs.empty());
    s_started = false;
    CHECK(idf_modem_start_call_diagnostics() == ESP_ERR_INVALID_STATE);
    s_started = true;
    CHECK(idf_modem_start_call_diagnostics() == ESP_OK);
    logs.clear();
    // 未知厂商上报在原白名单之前留下名字；不能把它臆测为来电并自动转发。
    receive("+VENDORCALL: \"13800138000\",1\r\n");
    receive("+CMT: ,23\r\n00112233445566778899AABBCCDDEEFF00\r\n");
    receive("+CGLA: 36,\"PRIVATE_APDU_DATA\"\r\n");
    receive("+MHTTPURC: \"content\",0,\"SECRET_HTTP_BODY\"\r\n");
    receive("plain SMS body SECRET_SMS_BODY\r\n");
    CHECK(logged("+VENDORCALL len=") && logged("+CGLA len=") && logged("+MHTTPURC len="));
    for (const char* secret : {"13800138000", "PRIVATE_APDU_DATA", "SECRET_HTTP_BODY", "SECRET_SMS_BODY",
                               "00112233445566778899AABBCCDDEEFF00"}) CHECK(!logged(secret));
    CHECK(forwarded.empty() && commands.empty());
    now_us += IdfCallDiagnostics::WINDOW_US;
    finish_call_diagnostics_if_due();
    CHECK(logged("60 秒窗口已到期"));
    size_t count = logs.size();
    receive("+VENDORCALL: 1\r\n"); finish_call_diagnostics_if_due();
    CHECK(logs.size() == count);
    // 高频 UART 也只有固定条数的日志；结束后可以重新启动诊断。
    CHECK(idf_modem_start_call_diagnostics() == ESP_OK);
    logs.clear();
    for (int i = 0; i < 200; ++i) receive("+VENDORCALL: 1\r\n");
    CHECK(logs.size() == IdfCallDiagnostics::MAX_LINES + 1);
    CHECK(logged("64 条上限"));
    CHECK(idf_modem_start_call_diagnostics() == ESP_OK);
    logs.clear(); receive("+VENDORCALL: 1\r\n");
    CHECK(logs.size() == 1);
    now_us += IdfCallDiagnostics::WINDOW_US; finish_call_diagnostics_if_due();
}
int main() {
    const std::string clip = "+CLIP: \"13800138000\",129,,,0\r\n";
    const std::string standard = "\r\nRING\r\n" + clip + "\r\nOK\r\n";
    // 模拟 AT 响应交错和每一个 UART 分包边界；号码来自 CLIP 时不依赖 CLCC。
    for (size_t split = 0; split <= standard.size(); ++split) {
        reset(); receive(standard.substr(0, split)); receive(standard.substr(split));
        now_us += 4000000; flush_pending_call_notify();
        CHECK(forwarded.size() == 1 && forwarded[0].find("13800138000") != std::string::npos);
        CHECK(commands.empty());
        for (const auto& log : logs) CHECK(log.find("13800138000") == std::string::npos);
    }
    // 只有 RING 时，CLCC ERROR 也应转发未知号码提醒。
    reset(); receive("RING\r\n"); now_us += 4000000; flush_pending_call_notify();
    CHECK(forwarded.size() == 1 && forwarded[0].find("未知号码") != std::string::npos);
    // 扩展振铃格式不能在 UART 白名单处被丢弃。
    for (const char* ring : {"+CRING: VOICE\r\n", "+CRING: \"VOICE\"\r\n"}) {
        reset(); receive(ring); now_us += 4000000; flush_pending_call_notify();
        CHECK(forwarded.size() == 1);
    }
    reset(); receive("+CRING: DATA\r\nERROR\r\nOK\r\n");
    now_us += 4000000; flush_pending_call_notify(); CHECK(forwarded.empty());
    // 呼叫结束后 30 秒内再次拨入，应视为新来电，而不是吞掉第二次提醒。
    for (const char* ended : {"NO CARRIER\r\n", "BUSY\r\n", "NO ANSWER\r\n"}) {
        const std::string sequence = "RING\r\n" + clip + ended + "RING\r\n" + clip;
        for (size_t split = 0; split <= sequence.size(); ++split) {
            reset(); receive(sequence.substr(0, split));
            now_us += 5000000; receive(sequence.substr(split)); flush_pending_call_notify();
            CHECK(forwarded.size() == 2);
        }
        // 未到三秒就结束的短来电也提醒；重复结束事件不再重复推送。
        reset(); receive("RING\r\n"); receive(ended); receive(ended);
        now_us += 4000000; flush_pending_call_notify();
        CHECK(forwarded.size() == 1 && commands.empty());
    }
    // 隐号来电只有 CLIP 时，也应有一次未知号码提醒。
    reset(); receive("+CLIP: \"\",128,,,,1\r\n"); now_us += 4000000; flush_pending_call_notify();
    CHECK(forwarded.size() == 1);
    // 关闭通知和黑名单仍生效。
    reset(); enabled = false; receive(standard); CHECK(forwarded.empty());
    reset(); blocked = true; receive(standard); CHECK(forwarded.empty());
    // 振铃穿插直推短信时，不能吞掉后续 PDU 行。
    reset();
    const std::string pdu = "00112233445566778899AABBCCDDEEFF00";
    const std::string interleaved = "+CMT: ,23\r\n+CRING: VOICE\r\n" + pdu + "\r\n";
    preserve_uart_urcs(reinterpret_cast<const uint8_t*>(interleaved.data()), interleaved.size());
    CHECK(preserved.find(pdu) != std::string::npos);
    CHECK(preserved.find("+CRING: VOICE") != std::string::npos);
    for (const auto& command : commands) CHECK(command != "ATH" && command != "AT+CHUP");
    test_diagnostics();
    if (failures) return 1;
    std::cout << "incoming call production-pipeline tests passed\n";
}
