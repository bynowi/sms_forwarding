#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>
using TickType_t = uint32_t;
using BaseType_t = int;
constexpr int pdPASS = 1;
#define pdMS_TO_TICKS(ms) (ms)
#define ESP_LOGW(...) ((void)0)
uint32_t now = 0;
TickType_t xTaskGetTickCount() { return now; }
struct Done {};
int delays = 0, stop_after = 0, polls = 0, finished_checks = 0, probe_failures = 0;
std::vector<std::string> commands, starts;
struct IdfModemStatus { bool started = false, atReady = false; std::string phase; } status;
IdfModemStatus idf_modem_get_status() { return status; }
void update_status(const IdfModemStatus& patch) { status = patch; }
void modem_power_hold_on() { starts.emplace_back("power_hold"); }
void idf_log_line(const char*) {}
bool send_ok(const char* command, uint32_t timeout) {
    assert(timeout == 700);
    commands.emplace_back(command);
    if (probe_failures > 0) { --probe_failures; return false; }
    return true;
}
void finish_call_diagnostics_if_due() { ++finished_checks; }
bool poll_unsolicited_uart(uint32_t timeout) { assert(timeout == 20); ++polls; return true; }
void vTaskDelay(uint32_t ms) {
    assert(ms == 100);
    now += ms;
    if (++delays >= stop_after) throw Done{};
}
// @真实旁路任务@
void modem_task(void*) {}
void scheduler_task(void*) {}
void (*selected_task)(void*) = nullptr;
int xTaskCreate(void (*entry)(void*), const char* name, int stack, void*, int, void*) {
    assert(stack == 8192);
    starts.emplace_back(name);
    selected_task = entry;
    return pdPASS;
}
bool s_scheduler_started = false;
void start_scheduler() {
    // @定时任务启动@
}
void idf_esim_init() { starts.emplace_back("esim"); }
int idf_config_get() { return 0; }
int idf_wifi_start(int) { starts.emplace_back("wifi"); return 0; }
int idf_push_start() { starts.emplace_back("push"); return 0; }
int idf_web_start() { starts.emplace_back("web"); start_scheduler(); return 0; }
int idf_modem_start(int) { starts.emplace_back("modem"); return 0; }
int idf_sms_start() { starts.emplace_back("sms"); return 0; }
void log_start_result(const char*, int) {}
void boot() {
    // @应用启动@
}
void select_modem_task() {
    // @任务选择@
    assert(ok == pdPASS);
}
int main() {
    boot();
    std::vector<std::string> expected = SMS_MODEM_PASSTHROUGH
        ? std::vector<std::string>{"wifi", "web", "modem"}
        : std::vector<std::string>{"esim", "wifi", "push", "web", "idf_sched", "modem", "sms"};
    assert(starts == expected);
    assert(s_scheduler_started == !static_cast<bool>(SMS_MODEM_PASSTHROUGH));
    select_modem_task();
    assert(selected_task == (SMS_MODEM_PASSTHROUGH ? modem_passthrough_task : modem_task));
    // 执行真实旁路循环：握手成功后不再自动查询或配置，但始终观察串口。
    starts.clear(); commands.clear(); status = {}; stop_after = 20;
    try { modem_passthrough_task(nullptr); } catch (const Done&) {}
    assert(starts == std::vector<std::string>{"power_hold"});
    assert(commands == std::vector<std::string>{"AT"});
    assert(status.atReady && status.phase == "passthrough");
    assert(polls == 20 && finished_checks == 20);
    // 初次 AT 不通仅重试握手，不能暗中硬重启或跑完整初始化。
    now = 0; delays = 0; status = {}; stop_after = 110; probe_failures = 2;
    commands.clear(); starts.clear();
    try { modem_passthrough_task(nullptr); } catch (const Done&) {}
    assert(commands == std::vector<std::string>({"AT", "AT", "AT"}));
    assert(status.atReady && starts == std::vector<std::string>{"power_hold"});
    std::cout << "passthrough startup/worker production tests passed: mode=" << SMS_MODEM_PASSTHROUGH << "\n";
}
