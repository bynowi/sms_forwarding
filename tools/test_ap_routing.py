#!/usr/bin/env python3
"""执行真实页面选择和鉴权函数，验证 AP/STA 共存时按连接目标地址分流。"""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
web = (ROOT / 'components/idf_web/idf_web.cpp').read_text(encoding='utf-8')
wifi = (ROOT / 'components/idf_wifi/idf_wifi.cpp').read_text(encoding='utf-8')


def function(name, source=web):
    start = source.rfind('\n', 0, source.index(name)) + 1
    return source[start:source.index('\n}', start) + 2]


names = ['is_ap_open_uri(', 'check_auth(', 'handle_root(', 'handle_not_found(']
if 'static bool request_targets_provisioning_ap(' in web:
    names.insert(0, 'static bool request_targets_provisioning_ap(')
fixture = (ROOT / 'tests/ap_routing_test.cpp').read_text(encoding='utf-8')
fixture = fixture.replace('// @真实页面函数@', '\n\n'.join(function(name) for name in names))
fixture = fixture.replace('// @AP地址查询@', function('uint32_t idf_wifi_get_ap_ipv4(', wifi))
host = ROOT / 'build/host'
host.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='test-ap-routing-', dir=host) as temp:
    cpp = Path(temp) / 'test.cpp'
    cpp.write_text(fixture, encoding='utf-8')
    compiler = os.environ.get('CXX') or ('cl' if os.name == 'nt' and shutil.which('cl') else 'g++')
    for ipv6 in [0, 1]:
        binary = Path(temp) / (f'test{ipv6}.exe' if os.name == 'nt' else f'test{ipv6}')
        if Path(compiler).stem.lower() == 'cl':
            args = ['/nologo', '/std:c++17', '/EHsc', '/utf-8', '/D_CRT_SECURE_NO_WARNINGS', f'/DLWIP_IPV6={ipv6}', str(cpp), f'/Fe:{binary}', '/link', 'ws2_32.lib']
        else:
            args = ['-std=c++17', '-Wall', '-Wextra', '-Werror', f'-DLWIP_IPV6={ipv6}', str(cpp), '-o', str(binary)]
        subprocess.run([compiler, *args], cwd=temp, check=True)
        subprocess.run([str(binary)], check=True)
