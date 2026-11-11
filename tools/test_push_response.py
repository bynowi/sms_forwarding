#!/usr/bin/env python3
"""编译真实推送入口和响应解析，模拟网络而不向外部机器人发送消息。"""
from pathlib import Path
import hashlib
import os
import re
import shutil
import subprocess
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/host/push-response'
OUT.mkdir(parents=True, exist_ok=True)
# 与固件托管依赖相同的上游版本，校验下载内容，避免主机测试库漂移。
for name, digest in {
    'cJSON.c': '298581a04a36c0165da4b0aade235c23088cb2faa58651d720ea2f3706ed0b0d',
    'cJSON.h': '25b0145150d500498e4d209cec69c18c42cf818bffcc54690be3b895a2a16dee',
}.items():
    target = OUT / name
    if not target.exists():
        with urllib.request.urlopen(f'https://raw.githubusercontent.com/DaveGamble/cJSON/v1.7.19/{name}', timeout=30) as reply:
            target.write_bytes(reply.read())
    if hashlib.sha256(target.read_bytes()).hexdigest() != digest:
        raise RuntimeError(f'cJSON 校验失败: {target}')

source = (ROOT / 'components/idf_push/idf_push.cpp').read_text(encoding='utf-8')
def block(src, begin, end):
    start = src.index(begin)
    return src[start:src.index(end, start) + len(end)]

fixture = (ROOT / 'tests/push_response_test.cpp').read_text(encoding='utf-8')
fixture = fixture.replace('// @通道结构@', block((ROOT / 'components/idf_config/include/idf_config.h').read_text(encoding='utf-8'), 'struct IdfPushChannel {', '\n};'))
modem = (ROOT / 'components/idf_modem/include/idf_modem.h').read_text(encoding='utf-8')
fixture = fixture.replace('// @蜂窝结构@', modem[modem.index('struct IdfCellularHttpResult'):modem.index('esp_err_t idf_modem_start')])
fixture = fixture.replace('// @HTTP函数@', block(source, 'struct PushHttpResult {', '\n};') + '\n' + block(source, 'static esp_err_t push_http_event(', '\n}') + '\n' + block(source, 'static esp_err_t http_request(', '\n}'))
fixture = fixture.replace('// @发送函数@', block(source, 'static bool send_to_channel(', '\n}'))
fixture = fixture.replace('// @队列结构@', block(source, 'struct PushJob {', '\n};'))
fixture = fixture.replace('// @队列函数@', block(source, 'static bool enqueue_push_job_locked(', '\n}') + '\n' + block(source, 'static bool process_push_one(', '\n}'))
cpp = OUT / 'test.cpp'
cpp.write_text(fixture, encoding='utf-8')
compiler = os.environ.get('CXX') or ('cl' if os.name == 'nt' and shutil.which('cl') else 'g++')
includes = [OUT, ROOT / 'components/idf_push', ROOT / 'components/idf_logbuf/include']
sources = [cpp, ROOT / 'components/idf_push/idf_push_response.cpp', OUT / 'cJSON.c']
binary = OUT / ('test.exe' if os.name == 'nt' else 'test')
if Path(compiler).stem.lower() == 'cl':
    args = ['/nologo', '/std:c++17', '/EHsc', '/utf-8', '/W4', '/D_CRT_SECURE_NO_WARNINGS']
    args += [f'/I{path}' for path in includes] + [str(path) for path in sources] + [f'/Fe:{binary}']
else:
    args = ['-std=c++17', '-Wall', '-Wextra', '-Werror']
    args += [f'-I{path}' for path in includes] + [str(path) for path in sources] + ['-o', str(binary)]
subprocess.run([compiler, *args], cwd=OUT, check=True)
subprocess.run([str(binary)], check=True)

# 使用既有 NVS 替身，执行真实配置保存、重新载入和网页表单解析。
config = (ROOT / 'components/idf_config/idf_config.cpp').read_text(encoding='utf-8')
web = (ROOT / 'components/idf_web/idf_web.cpp').read_text(encoding='utf-8')
def function(src, name):
    match = re.search(rf'^(?:static )?[\w:<>* ]+\b{re.escape(name)}\([^;]*?\)\s*\n\{{', src, re.MULTILINE)
    if not match:
        raise RuntimeError(f'未找到函数 {name}')
    return src[match.start():src.index('\n}', match.end()) + 2]

fixture = (ROOT / 'tests/push_response_config_test.cpp').read_text(encoding='utf-8')
nvs = (ROOT / 'tests/keepalive_config_test.cpp').read_text(encoding='utf-8').split('// @配置函数@')[0]
fixture = fixture.replace('// @NVS替身@', nvs)
fixture = fixture.replace('// @配置函数@', '\n'.join(function(config, name) for name in [
    'read_str', 'read_u8', 'read_bool', 'write_str', 'limit_utf8_bytes', 'trim_copy', 'looks_like_url',
    'channel_default_name', 'begin_field_save', 'commit_field_save', 'normalize_push_channel_for_save', 'idf_config_save_push',
]))
fixture = fixture.replace('// @表单函数@', '\n'.join(function(web, name) for name in [
    'find_field', 'has_field', 'field_text', 'preserve_redacted_push_key', 'parse_push_channels_form',
]))
start = config.index('        for (int i = 0; i < IDF_MAX_PUSH_CHANNELS; ++i) {', config.index('esp_err_t idf_config_load('))
fixture = fixture.replace('// @载入片段@', config[start:config.index('\n\n        for ', start)])
(OUT / 'esp_err.h').write_text('''#pragma once
using esp_err_t = int;
constexpr int ESP_OK = 0, ESP_FAIL = -1, ESP_ERR_TIMEOUT = 1;
constexpr int ESP_ERR_NVS_NOT_FOUND = 2, ESP_ERR_NVS_INVALID_LENGTH = 3;
''', encoding='utf-8')
cpp = OUT / 'config_test.cpp'
cpp.write_text(fixture, encoding='utf-8')
binary = OUT / ('config_test.exe' if os.name == 'nt' else 'config_test')
include = ROOT / 'components/idf_config/include'
if Path(compiler).stem.lower() == 'cl':
    args = ['/nologo', '/std:c++17', '/EHsc', '/utf-8', f'/I{OUT}', f'/I{include}', str(cpp), f'/Fe:{binary}']
else:
    args = ['-std=c++17', '-Wall', '-Wextra', '-Werror', f'-I{OUT}', f'-I{include}', str(cpp), '-o', str(binary)]
subprocess.run([compiler, *args], cwd=OUT, check=True)
subprocess.run([str(binary)], check=True)
