#pragma once

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string_view>

#include "idf_call_urc.h"

// 仅保留上报名称/长度/字段数；不缓存原文、号码、短信、APDU 或 HTTP 正文。
// 复用既有日志环形缓冲，限时且限条数，不分配额外任务或原始 UART 缓冲。
class IdfCallDiagnostics {
public:
    static constexpr uint32_t MAX_LINES = 64;
    static constexpr int64_t WINDOW_US = 60LL * 1000 * 1000;

    void start(int64_t now_us)
    {
        remaining_.store(MAX_LINES, std::memory_order_relaxed);
        until_us_.store(now_us + WINDOW_US, std::memory_order_relaxed);
    }

    bool record(std::string_view line, int64_t now_us, char* out, std::size_t capacity)
    {
        int64_t until = until_us_.load(std::memory_order_relaxed);
        if (until == 0 || now_us >= until || line.empty() || !out || capacity == 0) return false;
        if (line == "OK" || line == "ERROR" || line.substr(0, 2) == "AT") return false;

        std::size_t colon = line.find(':');
        bool label = colon >= 2 && colon <= 32 &&
                     (line[0] == '+' || line[0] == '*' || line[0] == '^' || line[0] == '%') &&
                     line[1] >= 'A' && line[1] <= 'Z';
        if (label) {
            for (std::size_t i = 1; i < colon; ++i) {
                char ch = line[i];
                if (!((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_')) label = false;
            }
        }
        // 不保留任意文本内容；即使某行是短信正文，也只能输出长度。
        if (label) {
            unsigned fields = 1;
            bool quoted = false;
            for (std::size_t i = colon + 1; i < line.size(); ++i) {
                if (line[i] == '"') quoted = !quoted;
                else if (line[i] == ',' && !quoted) ++fields;
            }
            snprintf(out, capacity, "%.*s len=%u fields=%u", static_cast<int>(colon), line.data(),
                     static_cast<unsigned>(line.size()), fields);
        } else if (line == "RING" || idf_call_urc::ended(line)) {
            snprintf(out, capacity, "%.*s", static_cast<int>(line.size()), line.data());
        } else {
            snprintf(out, capacity, "其他接收行 len=%u（内容隐藏）", static_cast<unsigned>(line.size()));
        }
        uint32_t budget = remaining_.load(std::memory_order_relaxed);
        while (budget != 0) {
            if (remaining_.compare_exchange_weak(budget, budget - 1, std::memory_order_relaxed)) return true;
        }
        return false;
    }

    // 0=仍在等待/未启用，1=时间到，2=达到条数上限；仅一个调用方获得结束通知。
    int finish_if_due(int64_t now_us)
    {
        int64_t until = until_us_.load(std::memory_order_relaxed);
        if (until == 0) return 0;
        int reason = now_us >= until ? 1 : (remaining_.load(std::memory_order_relaxed) == 0 ? 2 : 0);
        if (reason && until_us_.compare_exchange_strong(until, 0, std::memory_order_relaxed)) return reason;
        return 0;
    }

private:
    std::atomic<int64_t> until_us_{0};
    std::atomic<uint32_t> remaining_{0};
};
