#!/usr/bin/env python3
"""以 Python HMAC 为独立参考，用系统加密库验证固件真实签名函数。"""
from pathlib import Path
import base64
import hashlib
import hmac
import os
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/host/push-signing'
OUT.mkdir(parents=True, exist_ok=True)
source = (ROOT / 'components/idf_push/idf_push.cpp').read_text(encoding='utf-8')
start = source.index('static std::string hmac_sha256_base64(')
function = source[start:source.index('\n}', start) + 2]
fixture = (ROOT / 'tests/push_signing_test.cpp').read_text(encoding='utf-8').replace('// @真实签名函数@', function)
start = source.index('static std::string url_encode(')
fixture = fixture.replace('// @URL编码函数@', source[start:source.index('\n}', start) + 2])
vectors = [
    (b'\x0b' * 20, b'Hi There'),
    (b'Jefe', b'what do ya want for nothing?'),
    (b'\xaa' * 131, b'Test Using Larger Than Block-Size Key - Hash Key First'),
    (b'\x00\xffsecret', b'binary\x00message'),
    (b'SECtest', b'1794205446000\nSECtest'),
    (b'1794205446\nSECtest', b''),
]
def cpp_bytes(data):
    return 'std::string({' + ','.join(f'static_cast<char>({byte})' for byte in data) + '})'
rows = []
for key, data in vectors:
    expected = base64.b64encode(hmac.new(key, data, hashlib.sha256).digest()).decode('ascii')
    rows.append('{' + cpp_bytes(key) + ',' + cpp_bytes(data) + ',"' + expected + '"},')
fixture = fixture.replace('// @独立参考向量@', '\n'.join(rows))
cpp = OUT / 'test.cpp'
cpp.write_text(fixture, encoding='utf-8')
compiler = os.environ.get('CXX') or ('cl' if os.name == 'nt' and shutil.which('cl') else 'g++')
binary = OUT / ('test.exe' if os.name == 'nt' else 'test')
if Path(compiler).stem.lower() == 'cl':
    args = ['/nologo', '/std:c++17', '/EHsc', '/utf-8', '/W4', str(cpp), f'/Fe:{binary}', '/link', 'bcrypt.lib', 'crypt32.lib']
else:
    args = ['-std=c++17', '-Wall', '-Wextra', '-Werror', str(cpp), '-o', str(binary), '-lcrypto']
subprocess.run([compiler, *args], cwd=OUT, check=True)
subprocess.run([str(binary)], check=True)
