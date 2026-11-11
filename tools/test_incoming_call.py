#!/usr/bin/env python3
"""回放分块来电上报，贯通真实 UART 提取、来电状态与转发入队函数。"""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
modem = (ROOT / 'components/idf_modem/idf_modem.cpp').read_text(encoding='utf-8')
sms = (ROOT / 'components/idf_sms/idf_sms.cpp').read_text(encoding='utf-8')


def function(source, name):
    start = source.rfind('\n', 0, source.index(name)) + 1
    return source[start:source.index('\n}', start) + 2]


fixture = (ROOT / 'tests/incoming_call_test.cpp').read_text(encoding='utf-8')
fixture = fixture.replace('// @UART函数@', '\n\n'.join(function(modem, name) for name in [
    'starts_with(', 'looks_like_pdu_line(', 'finish_call_diagnostics_if_due(',
    'record_call_diagnostics(', 'preserve_uart_urc_line(', 'preserve_uart_urcs(',
    'esp_err_t idf_modem_start_call_diagnostics(',
]))
start = sms.index('static int64_t s_call_started_us')
end = sms.index('\nstatic void process_urc_line(', start)
fixture = fixture.replace('// @来电函数@', sms[start:end])
start = sms.index('    // 来电通知：')
end = sms.index('\n    if (s_wait_pdu)', start)
fixture = fixture.replace('// @来电分发@', sms[start:end])
host = ROOT / 'build/host'
host.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='test-call-', dir=host) as temp:
    cpp = Path(temp) / 'test.cpp'
    binary = Path(temp) / ('test.exe' if os.name == 'nt' else 'test')
    cpp.write_text(fixture, encoding='utf-8')
    compiler = os.environ.get('CXX') or ('cl' if os.name == 'nt' and shutil.which('cl') else 'g++')
    include = ROOT / 'components/idf_modem/include'
    if Path(compiler).stem.lower() == 'cl':
        args = ['/nologo', '/std:c++17', '/EHsc', '/utf-8', '/D_CRT_SECURE_NO_WARNINGS',
                f'/I{include}', str(cpp), f'/Fe:{binary}']
    else:
        args = ['-std=c++17', '-Wall', '-Wextra', '-Werror', f'-I{include}', str(cpp), '-o', str(binary)]
    subprocess.run([compiler, *args], cwd=temp, check=True)
    subprocess.run([str(binary)], check=True)
