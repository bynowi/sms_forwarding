#include <cctype>
#include <cstdlib>
#include <utility>
// @NVS替身@
// @配置函数@

using IdfFormFields = std::vector<std::pair<std::string, std::string>>;
bool field_blank(const std::string& s) { return s.find_first_not_of(" \t\r\n") == std::string::npos; }
uint8_t field_u8(const IdfFormFields& fields, const char* name, uint8_t fallback);
// @表单函数@
uint8_t field_u8(const IdfFormFields& fields, const char* name, uint8_t fallback) {
    const std::string* value = find_field(fields, name);
    return value ? static_cast<uint8_t>(std::atoi(value->c_str())) : fallback;
}
void reload_push() {
    IdfConfig next;
    nvs_handle_t nvs = 1;
    // @载入片段@
    s_config = next;
}

int main() {
    IdfPushChannel previous[IDF_MAX_PUSH_CHANNELS], channels[IDF_MAX_PUSH_CHANNELS];
    previous[0].successRule = "code=0";
    IdfFormFields fields{{"push0en","1"},{"push0type","7"},{"push0rule"," data.status=\"ok\" "}};
    parse_push_channels_form(fields, previous, channels);
    assert(idf_config_save_push(true, channels) == ESP_OK);
    assert(s_config.pushChannels[0].successRule == "data.status=\"ok\"");
    reload_push();
    assert(s_config.pushChannels[0].successRule == "data.status=\"ok\"");
    assert(s_config.pushChannels[1].successRule.empty());
    // 旧网页不提交新增字段时保留；明确提交空值则清除，删除则整槽清空。
    fields = {{"push0type","1"}};
    parse_push_channels_form(fields, previous, channels);
    assert(channels[0].successRule == "code=0");
    fields.push_back({"push0rule",""});
    parse_push_channels_form(fields, previous, channels);
    assert(channels[0].successRule.empty());
    fields.push_back({"push0delete","1"});
    parse_push_channels_form(fields, previous, channels);
    assert(channels[0].successRule.empty() && !channels[0].enabled);
    // NVS 写入失败不能让运行配置显示成已保存。
    channels[0].successRule = "success=true";
    const std::string old = s_config.pushChannels[0].successRule;
    fail_commit = true;
    assert(idf_config_save_push(true, channels) != ESP_OK);
    assert(s_config.pushChannels[0].successRule == old);
    fail_commit = false;
    puts("push response configuration regressions passed: form, NVS reload, defaults and failed commit");
}
