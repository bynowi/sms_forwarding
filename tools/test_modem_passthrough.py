#!/usr/bin/env python3
"""编译两种真实启动分支，验证旁路只握手并观察 UART，不启动业务工作任务。"""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
modem = (ROOT / 'components/idf_modem/idf_modem.cpp').read_text(encoding='utf-8')
main = (ROOT / 'main/app_main.cpp').read_text(encoding='utf-8')
web = (ROOT / 'components/idf_web/idf_web.cpp').read_text(encoding='utf-8')


def function(name):
    start = modem.rfind('\n', 0, modem.index(name)) + 1
    return modem[start:modem.index('\n}', start) + 2]


fixture = (ROOT / 'tests/modem_passthrough_test.cpp').read_text(encoding='utf-8')
fixture = fixture.replace('// @真实旁路任务@', '\n\n'.join(function(name) for name in [
    'bool idf_modem_is_passthrough(', 'static void modem_passthrough_task(',
]))
start = main.index('    bool passthrough =')
fixture = fixture.replace('// @应用启动@', main[start:main.index('\n}', start)])
start = modem.index('    BaseType_t ok = xTaskCreate(idf_modem_is_passthrough()')
fixture = fixture.replace('// @任务选择@', modem[start:modem.index('    if (ok != pdPASS)', start)])
start = web.index('    if (!s_scheduler_started && !idf_modem_is_passthrough())')
fixture = fixture.replace('// @定时任务启动@', web[start:web.index('    ESP_LOGI(TAG, "ESP-IDF web server', start)])
host = ROOT / 'build/host'
host.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='test-passthrough-', dir=host) as temp:
    compiler = os.environ.get('CXX') or ('cl' if os.name == 'nt' and shutil.which('cl') else 'g++')
    cpp = Path(temp) / 'test.cpp'
    cpp.write_text(fixture, encoding='utf-8')
    for mode in [0, 1]:
        binary = Path(temp) / (f'test{mode}.exe' if os.name == 'nt' else f'test{mode}')
        if Path(compiler).stem.lower() == 'cl':
            args = ['/nologo', '/std:c++17', '/EHsc', '/utf-8', '/D_CRT_SECURE_NO_WARNINGS',
                    f'/DSMS_MODEM_PASSTHROUGH={mode}', str(cpp), f'/Fe:{binary}']
        else:
            args = ['-std=c++17', '-Wall', '-Wextra', '-Werror', f'-DSMS_MODEM_PASSTHROUGH={mode}',
                    str(cpp), '-o', str(binary)]
        subprocess.run([compiler, *args], cwd=temp, check=True)
        subprocess.run([str(binary)], check=True)
