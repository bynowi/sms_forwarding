#!/usr/bin/env python3
"""回放 SIM 查询超时、网页蜂窝设置及 IP 响应，编译真实生产函数。"""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
modem = (ROOT / 'components/idf_modem/idf_modem.cpp').read_text(encoding='utf-8')
web = (ROOT / 'components/idf_web/idf_web.cpp').read_text(encoding='utf-8')


def function(source, name):
    start = source.rfind('\n', 0, source.index(name)) + 1
    return source[start:source.index('\n}', start) + 2]


fixture = (ROOT / 'tests/modem_network_test.cpp').read_text(encoding='utf-8')
fixture = fixture.replace('// @模组函数@', '\n\n'.join(function(modem, name) for name in [
    'query_sim_state(', 'query_sim_present(', 'valid_ip_address(',
    'parse_cgpaddr_ip(', 'sample_cell_ip_once(', 'apn_valid_for_at(',
    'apply_configured_data_mode_once(', 'schedule_data_mode_retry(',
    'apply_startup_data_mode(', 'process_data_mode_retry(',
    'void idf_modem_request_data_mode_apply(',
]))
start = web.index('        // 数据设置直接投递')
end = web.index('        ModemApplyTaskArg* arg', start)
fixture = fixture.replace('// @网页保存@', web[start:end])
start = modem.index('            int present_now = query_sim_present();')
end = modem.index('\n        }\n        for (int i', start)
fixture = fixture.replace('// @热插拔检查@', modem[start:end])
host = ROOT / 'build/host'
host.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='test-network-', dir=host) as temp:
    cpp = Path(temp) / 'test.cpp'
    binary = Path(temp) / ('test.exe' if os.name == 'nt' else 'test')
    cpp.write_text(fixture, encoding='utf-8')
    compiler = os.environ.get('CXX') or ('cl' if os.name == 'nt' and shutil.which('cl') else 'g++')
    if Path(compiler).stem.lower() == 'cl':
        args = ['/nologo', '/std:c++17', '/EHsc', '/utf-8', '/D_CRT_SECURE_NO_WARNINGS', str(cpp), f'/Fe:{binary}', '/link', 'ws2_32.lib']
    else:
        args = ['-std=c++17', '-Wall', '-Wextra', '-Werror', str(cpp), '-o', str(binary)]
    subprocess.run([compiler, *args], cwd=temp, check=True)
    subprocess.run([str(binary)], check=True)
