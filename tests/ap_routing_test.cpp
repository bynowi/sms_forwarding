#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using socklen_t = int;
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#endif
using esp_err_t = int;
constexpr int ESP_OK = 0, ESP_FAIL = 1, HTTPD_404_NOT_FOUND = 404;
bool ap_mode = true, socket_ok = true, mutex_ok = true, netif_ok = true;
int family = AF_INET, failures = 0;
std::string local_ip = "192.168.1.1";
uint32_t ap_ipv4 = 0;
struct httpd_req_t {
    const char* uri = "/";
    bool authorized = false, cross_origin = false;
    int status = 200, asset = 0;
    std::string redirect;
};
#define CHECK(expr) do { if (!(expr)) { std::cerr << "FAIL: " #expr "\n"; ++failures; } } while (0)
bool idf_wifi_is_ap_mode() { return ap_mode; }
bool& s_ap_mode = ap_mode;
void* s_ap_netif = reinterpret_cast<void*>(1);
void* s_state_mutex = reinterpret_cast<void*>(1);
constexpr int pdTRUE = 1;
#define pdMS_TO_TICKS(ms) (ms)
int xSemaphoreTake(void*, int) { return mutex_ok ? pdTRUE : 0; }
void xSemaphoreGive(void*) {}
struct esp_netif_ip_info_t { struct { uint32_t addr; } ip; };
int esp_netif_get_ip_info(void*, esp_netif_ip_info_t* out) { out->ip.addr = ap_ipv4; return netif_ok ? ESP_OK : ESP_FAIL; }
// @AP地址查询@
struct IdfWifiStatus { bool apMode = true; };
IdfWifiStatus idf_wifi_get_status() { return {ap_mode}; }
int httpd_req_to_sockfd(httpd_req_t*) { return 5; }
int fake_getsockname(int fd, sockaddr* out, socklen_t*) {
    CHECK(fd == 5);
    if (!socket_ok) return -1;
    if (family == AF_INET) {
        auto* a = reinterpret_cast<sockaddr_in*>(out);
        a->sin_family = AF_INET;
        CHECK(inet_pton(AF_INET, local_ip.c_str(), &a->sin_addr) == 1);
    } else {
        auto* a = reinterpret_cast<sockaddr_in6*>(out);
        a->sin6_family = AF_INET6;
        CHECK(inet_pton(AF_INET6, local_ip.c_str(), &a->sin6_addr) == 1);
    }
    return 0;
}
#define getsockname fake_getsockname
bool reject_cross_origin(httpd_req_t* req) { return req->cross_origin; }
esp_err_t httpd_req_get_hdr_value_str(httpd_req_t* req, const char* key, char* out, size_t cap) {
    CHECK(std::string(key) == "Authorization");
    if (!req->authorized) return ESP_FAIL;
    snprintf(out, cap, "Basic VALID"); return ESP_OK;
}
bool auth_matches_config(const char* value) { return std::string(value) == "Basic VALID"; }
void httpd_resp_set_status(httpd_req_t* req, const char* value) { req->status = std::stoi(value); }
void httpd_resp_set_type(httpd_req_t*, const char*) {}
void httpd_resp_set_hdr(httpd_req_t* req, const char* key, const char* value) {
    if (std::string(key) == "Location") req->redirect = value;
}
esp_err_t httpd_resp_sendstr(httpd_req_t*, const char*) { return ESP_OK; }
esp_err_t httpd_resp_send(httpd_req_t*, const char*, size_t) { return ESP_OK; }
void httpd_resp_send_err(httpd_req_t* req, int code, const char*) { req->status = code; }
constexpr int WEB_AP = 1, WEB_INDEX = 2;
esp_err_t send_gzip_asset(httpd_req_t* req, int asset, const char*) { req->asset = asset; return ESP_OK; }
// @真实页面函数@
int main() {
    CHECK(inet_pton(AF_INET, "192.168.1.1", &ap_ipv4) == 1);
    httpd_req_t req;
    handle_root(&req); CHECK(req.asset == WEB_AP);
    // 热点仍开着，用户访问路由器分配的地址：必须进入正常登录后台。
    local_ip = "192.168.0.111";
    req = {}; req.authorized = true;
    handle_root(&req); CHECK(req.asset == WEB_INDEX);
    req = {};
    handle_root(&req); CHECK(req.status == 401 && req.asset == 0);
    req = {}; req.uri = "/wifiscan";
    CHECK(!check_auth(&req) && req.status == 401);
    req = {}; req.uri = "/generate_204";
    handle_not_found(&req); CHECK(req.status == 404 && req.redirect.empty());
    // AP 地址保留原配网入口，敏感接口仍要求认证。
    local_ip = "192.168.1.1";
    req = {}; req.uri = "/apstatus"; CHECK(check_auth(&req));
    req = {}; req.uri = "/config.json"; CHECK(!check_auth(&req));
    req = {}; req.uri = "/generate_204";
    handle_not_found(&req); CHECK(req.status == 302 && !req.redirect.empty());
    // 兼容 HTTP 服务端的 IPv4-mapped IPv6 连接，原生 IPv6 不能获得 AP 免认证。
    family = AF_INET6; local_ip = "::ffff:192.168.1.1";
#if LWIP_IPV6
    req = {}; handle_root(&req); CHECK(req.asset == WEB_AP);
#else
    req = {}; handle_root(&req); CHECK(req.status == 401);
#endif
    local_ip = "fd00::1234";
    req = {}; req.authorized = true; handle_root(&req); CHECK(req.asset == WEB_INDEX);
    req = {}; req.uri = "/apstatus"; CHECK(!check_auth(&req));
    socket_ok = true; family = AF_INET; local_ip = "192.168.1.1";
    mutex_ok = false;
    req = {}; req.uri = "/apstatus"; CHECK(!check_auth(&req));
    mutex_ok = true; netif_ok = false;
    req = {}; req.uri = "/apstatus"; CHECK(!check_auth(&req));
    netif_ok = true;
    // 无法确认连接地址时，不能误放行未认证的请求。
    socket_ok = false;
    req = {}; req.uri = "/apstatus"; CHECK(!check_auth(&req));
    // 纯 STA 模式仍是完整后台。
    ap_mode = false;
    req = {}; req.authorized = true; handle_root(&req); CHECK(req.asset == WEB_INDEX);
    if (failures) return 1;
    std::cout << "AP/STA page routing and authentication tests passed\n";
}
