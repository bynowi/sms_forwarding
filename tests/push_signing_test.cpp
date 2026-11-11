#include <cassert>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#include <wincrypt.h>
#else
#include <openssl/hmac.h>
#include <openssl/evp.h>
#endif

// 主机用系统加密库实现 PSA 接口替身；固件真实 helper 和参数原样参与测试。
// 实际 ESP-IDF PSA 驱动由在线固件构建检查，主机不声称模拟设备的硬件驱动。
using psa_status_t = int32_t;
using mbedtls_svc_key_id_t = uint32_t;
constexpr int PSA_SUCCESS = 0, PSA_ERROR_INVALID_ARGUMENT = -135, PSA_ERROR_CORRUPTION_DETECTED = -151;
constexpr int PSA_KEY_TYPE_HMAC = 0x1100, PSA_KEY_USAGE_SIGN_MESSAGE = 0x400;
constexpr int PSA_ALG_SHA_256 = 9, PSA_KEY_LIFETIME_VOLATILE = 0;
constexpr int PSA_ALG_HMAC(int hash) { return 0x03800000 | hash; }
struct psa_key_attributes_t { int type = 0, usage = 0, algorithm = 0, lifetime = 0; };
#define PSA_KEY_ATTRIBUTES_INIT {}
std::string failure, imported_key;
int live_keys = 0, destroy_calls = 0, reset_calls = 0;
int psa_crypto_init() { return failure == "init" ? -132 : PSA_SUCCESS; }
void psa_set_key_type(psa_key_attributes_t* a, int value) { a->type = value; }
void psa_set_key_usage_flags(psa_key_attributes_t* a, int value) { a->usage = value; }
void psa_set_key_algorithm(psa_key_attributes_t* a, int value) { a->algorithm = value; }
void psa_set_key_lifetime(psa_key_attributes_t* a, int value) { a->lifetime = value; }
void psa_reset_key_attributes(psa_key_attributes_t*) { ++reset_calls; }
psa_status_t psa_import_key(const psa_key_attributes_t* a, const unsigned char* key, size_t size, mbedtls_svc_key_id_t* id) {
    assert(a->type == PSA_KEY_TYPE_HMAC && a->usage == PSA_KEY_USAGE_SIGN_MESSAGE);
    assert(a->algorithm == PSA_ALG_HMAC(PSA_ALG_SHA_256) && a->lifetime == PSA_KEY_LIFETIME_VOLATILE);
    if (failure == "import") return -141;
    assert(live_keys == 0);
    imported_key.assign(reinterpret_cast<const char*>(key), size); ++live_keys; *id = 1;
    return PSA_SUCCESS;
}
psa_status_t psa_destroy_key(mbedtls_svc_key_id_t id) {
    assert(id == 1 && live_keys == 1); --live_keys; ++destroy_calls; imported_key.clear();
    return failure == "destroy" ? -132 : PSA_SUCCESS;
}
psa_status_t psa_mac_compute(mbedtls_svc_key_id_t id, int algorithm, const unsigned char* data, size_t size,
                             unsigned char* out, size_t capacity, size_t* length) {
    assert(id == 1 && live_keys == 1 && algorithm == PSA_ALG_HMAC(PSA_ALG_SHA_256) && capacity >= 32);
    if (failure == "mac") return -134;
#ifdef _WIN32
    BCRYPT_ALG_HANDLE provider = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptOpenAlgorithmProvider(&provider, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG) != 0) return -134;
    ULONG object_size = 0, received = 0;
    assert(BCryptGetProperty(provider, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&object_size), sizeof(object_size), &received, 0) == 0);
    std::vector<unsigned char> object(object_size);
    assert(BCryptCreateHash(provider, &hash, object.data(), object_size,
                           reinterpret_cast<PUCHAR>(&imported_key[0]), static_cast<ULONG>(imported_key.size()), 0) == 0);
    if (size) assert(BCryptHashData(hash, const_cast<PUCHAR>(data), static_cast<ULONG>(size), 0) == 0);
    assert(BCryptFinishHash(hash, out, 32, 0) == 0);
    BCryptDestroyHash(hash); BCryptCloseAlgorithmProvider(provider, 0);
    *length = 32;
#else
    unsigned int result_size = 0;
    if (!HMAC(EVP_sha256(), imported_key.data(), static_cast<int>(imported_key.size()), data, size, out, &result_size)) return -134;
    *length = result_size;
#endif
    if (failure == "length") *length = 31;
    return PSA_SUCCESS;
}
int mbedtls_base64_encode(unsigned char* out, size_t capacity, size_t* size, const unsigned char* data, size_t length) {
    if (failure == "base64") return -42;
    assert(capacity >= ((length + 2) / 3) * 4 + 1);
#ifdef _WIN32
    DWORD n = static_cast<DWORD>(capacity);
    if (!CryptBinaryToStringA(data, static_cast<DWORD>(length), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, reinterpret_cast<char*>(out), &n)) return -42;
    *size = std::strlen(reinterpret_cast<const char*>(out));
#else
    *size = static_cast<size_t>(EVP_EncodeBlock(out, data, static_cast<int>(length)));
#endif
    return 0;
}
// @真实签名函数@
// @URL编码函数@

struct Vector { std::string key, data, base64; };
int main() {
    const std::vector<Vector> vectors = {
        // @独立参考向量@
    };
    for (const auto& item : vectors) {
        std::string error;
        const auto result = hmac_sha256_base64(item.data, item.key, &error);
        if (result != item.base64 || !error.empty() || live_keys != 0) {
            std::fprintf(stderr, "FAIL: HMAC/base64 known-answer mismatch or leaked key\n");
            return 1;
        }
    }
    // 计算失败、输出长度异常、编码失败时仍必须释放临时密钥，并给出无敏感内容的原因。
    for (const char* stage : {"init", "import", "mac", "destroy", "length", "base64"}) {
        failure = stage; destroy_calls = reset_calls = 0;
        std::string error;
        assert(hmac_sha256_base64("message", "SECRET-TEST", &error).empty());
        assert(!error.empty() && error.find("SECRET-TEST") == std::string::npos && live_keys == 0);
        assert(destroy_calls == ((failure == "init" || failure == "import") ? 0 : 1));
        assert(reset_calls == (failure == "init" ? 0 : 1));
    }
    failure.clear();
    std::string empty_key_error;
    assert(hmac_sha256_base64("message", "", &empty_key_error).empty());
    assert(!empty_key_error.empty() && live_keys == 0);
    assert(url_encode("a+b/c=") == "a%2Bb%2Fc%3D");
    // 重复签名不能耗尽 PSA 的临时密钥槽位。
    for (int i = 0; i < 100; ++i) {
        assert(!hmac_sha256_base64("", "timestamp\nSECRET-TEST").empty());
        assert(live_keys == 0);
    }
    std::puts("push signing regressions passed: known answers, empty message, failures and key cleanup");
}
