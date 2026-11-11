"""使用隔离的 SDK 入口执行真实 CMake，核对普通/旁路固件元数据版本。"""
from pathlib import Path
import os
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class FirmwareVersionTest(unittest.TestCase):
    def test_version_matches_ui_and_diagnostic_suffix(self):
        cmake = os.environ.get('CMAKE') or shutil.which('cmake')
        if not cmake:
            self.skipTest('CMake unavailable')
        version = re.search(r'IDF_FW_VERSION = "([^"]+)"',
                            (ROOT / 'components/idf_config/include/idf_config.h').read_text(encoding='utf-8')).group(1)
        host = ROOT / 'build/host'
        host.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='version-test-', dir=host) as temp:
            sdk = Path(temp) / 'sdk'
            entry = sdk / 'tools/cmake/project.cmake'
            entry.parent.mkdir(parents=True)
            entry.write_text('macro(project name)\nfile(WRITE "${CMAKE_BINARY_DIR}/version.txt" "${PROJECT_VER}")\nendmacro()\n')
            env = dict(os.environ, IDF_PATH=str(sdk))
            for mode, suffix in [('OFF', ''), ('ON', '-uartdiag')]:
                build = Path(temp) / mode
                subprocess.run([cmake, '-S', str(ROOT), '-B', str(build),
                                f'-DSMS_MODEM_PASSTHROUGH={mode}'], env=env, check=True, capture_output=True)
                self.assertEqual((build / 'version.txt').read_text(), version + suffix)


if __name__ == '__main__':
    unittest.main()
