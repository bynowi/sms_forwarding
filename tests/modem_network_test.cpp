#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <iostream>
#include <string>
#include <utility>
#include <vector>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

using esp_err_t = int;
constexpr int ESP_OK = 0, ESP_FAIL = 1;
bool reply_ok = true, busy = false, lock_ok = true;
std::string reply_body, cell_ip;
std::vector<std::string> commands, logs;
int failures = 0;
using TickType_t = uint32_t;
#define pdMS_TO_TICKS(ms) (ms)
uint32_t now = 0;
uint32_t xTaskGetTickCount() { return now; }
constexpr uint32_t MODEM_DATA_MODE_RETRY_GAP_MS = 10000;
constexpr uint8_t MODEM_DATA_MODE_RETRY_MAX = 3;
bool s_data_mode_retry_pending = false;
uint8_t s_data_mode_retry_count = 0;
TickType_t s_next_data_mode_retry = 0;
std::atomic<bool> s_data_mode_apply_requested{false};
struct IdfSimSettingsView { bool dataEnabled = true, roamingEnabled = true; std::string apn; } config;
IdfSimSettingsView idf_config_get_sim_settings_view() { return config; }
bool model_skips_cgact() { return false; }
bool at_channel_idle_now() { return !busy && lock_ok; }
#define CHECK(expr) do { if (!(expr)) { std::cerr << "FAIL: " #expr "\n"; ++failures; } } while (0)
bool send_ok(const char* cmd, uint32_t, std::string* resp) {
    commands.emplace_back(cmd);
    *resp = reply_body;
    return reply_ok;
}
void idf_log_line(const char* text) { logs.emplace_back(text); }
void idf_logf(const char* fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    logs.emplace_back(buf);
}
std::string idf_util_trim_copy(const std::string& text) {
    size_t start = text.find_first_not_of(" \r\n\t");
    return start == std::string::npos ? "" : text.substr(start, text.find_last_not_of(" \r\n\t") - start + 1);
}
void set_status_cell_ip(const std::string& ip) { cell_ip = ip; }
struct IdfModemStatus { bool modemReady = true, atReady = true; int ceregStat = 1; } modem_status;
IdfModemStatus idf_modem_get_status() { return modem_status; }
// @模组函数@
int invalidations = 0, resets = 0;
void idf_modem_invalidate_sim_identity() { ++invalidations; }
void idf_modem_request_reset(bool) { ++resets; }
void set_sim_status(const char*, bool, const char*) {}
void set_phase(const char*) {}
void check_sim(int& sim_present) {
    bool registered = true, post_register_done = true, sms_reconfigure_pending = false, sim_ready = true;
    int dereg_count = 0;
    // @热插拔检查@
    (void)registered; (void)post_register_done; (void)sms_reconfigure_pending;
    (void)sim_ready; (void)dereg_count;
}

bool logged(const char* text) {
    return std::any_of(logs.begin(), logs.end(), [text](const std::string& line) { return line.find(text) != std::string::npos; });
}
esp_err_t apply_web() {
    bool data_changed = true, operator_changed = false;
    // @网页保存@
    return ESP_OK;
}
int main() {
    // 查询未知必须与确实无卡区分，不能触发清空号码和硬重启。
    reply_ok = false; reply_body = "";
    CHECK(query_sim_present() == -1);
    int present = 1;
    for (int i = 0; i < 10; ++i) check_sim(present);
    CHECK(present == 1 && invalidations == 0 && resets == 0);
    reply_body = "+CME ERROR: 100\r\n";
    CHECK(query_sim_present() == -1);  // 不能把 100 当成无卡错误 10。
    reply_body = "+CME ERROR: 10\r\n";
    CHECK(query_sim_present() == 0);
    check_sim(present);
    CHECK(present == 0 && invalidations == 1 && resets == 0);
    reply_ok = true;
    for (const char* state : {"READY", "SIM PIN", "SIM PUK"}) {
        reply_body = std::string("+CPIN: ") + state + "\r\nOK";
        CHECK(query_sim_present() == 1);
    }
    check_sim(present);
    CHECK(present == 1 && invalidations == 2 && resets == 1);
    // 一次超时不能擦掉已取得的 IP；明确返回无地址才清除。
    cell_ip = "10.1.2.3";
    reply_ok = false;
    CHECK(!sample_cell_ip_once());
    CHECK(cell_ip == "10.1.2.3");
    reply_ok = true; reply_body = "+CGPADDR: 1,0.0.0.0\r\nOK";
    CHECK(!sample_cell_ip_once()); CHECK(cell_ip.empty());
    reply_body = "+CGPADDR: 1,\"10.54.1.2\"\r\nOK";
    CHECK(sample_cell_ip_once()); CHECK(cell_ip == "10.54.1.2");
    reply_body = "+CGPADDR: 1,\"0.0.0.0\",\"240e:1234::1\"\r\nOK";
    CHECK(sample_cell_ip_once()); CHECK(cell_ip == "240e:1234::1");
    // 网页必须把数据设置交给模组队列，不能吞掉 AT 失败后声称已启用。
    commands.clear(); logs.clear(); reply_ok = false;
    apply_web();
    CHECK(!logged("蜂窝数据已启用"));
    CHECK(s_data_mode_apply_requested);
    CHECK(commands.empty());
    // 未注册/蜂窝任务繁忙时，数据请求也必须保留，不能直接丢掉。
    modem_status.modemReady = false;
    modem_status.atReady = false;
    apply_web(); CHECK(!process_data_mode_retry()); CHECK(s_data_mode_apply_requested);
    modem_status.modemReady = true; busy = true;
    modem_status.atReady = true;
    apply_web(); CHECK(!process_data_mode_retry()); CHECK(s_data_mode_apply_requested);
    busy = false; lock_ok = false;
    apply_web(); CHECK(!process_data_mode_retry()); CHECK(s_data_mode_apply_requested);
    lock_ok = true;
    // 真正下发但未取得 IP 时进入有界重试，不能打印启用成功。
    CHECK(process_data_mode_retry());
    CHECK(!s_data_mode_apply_requested && s_data_mode_retry_pending);
    CHECK(!logged("已按配置启用蜂窝数据"));
    for (int i = 0; i < 3; ++i) {
        now += MODEM_DATA_MODE_RETRY_GAP_MS;
        CHECK(process_data_mode_retry());
    }
    CHECK(!s_data_mode_retry_pending);
    CHECK(!process_data_mode_retry());
    // 下次保存可恢复；只有取得实际 IP 后才打印成功并更新概览缓存。
    reply_ok = true; reply_body = "+CGPADDR: 1,10.2.3.4\r\nOK";
    apply_web(); CHECK(process_data_mode_retry());
    CHECK(cell_ip == "10.2.3.4");
    CHECK(logged("已按配置启用蜂窝数据"));
    CHECK(!s_data_mode_retry_pending);
    // 关闭数据及漫游限制均清除地址，不得误报激活成功。
    commands.clear(); logs.clear(); config.dataEnabled = false;
    apply_web(); CHECK(process_data_mode_retry());
    CHECK(cell_ip.empty()); CHECK(commands.size() == 1 && commands[0] == "AT+CGACT=0,1");
    config.dataEnabled = true; config.roamingEnabled = false; modem_status.ceregStat = 5;
    commands.clear(); logs.clear();
    apply_web(); CHECK(process_data_mode_retry());
    CHECK(!logged("已按配置启用蜂窝数据"));
    CHECK(commands.size() == 1 && commands[0] == "AT+CGACT=0,1");
    if (failures) return 1;
    std::cout << "modem network production-function tests passed\n";
}
