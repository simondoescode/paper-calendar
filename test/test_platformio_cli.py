"""Discovery tests independent of an installed PlatformIO toolchain."""
import importlib.util
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import platformio_cli as cli


class DiscoveryTests(unittest.TestCase):
    def test_path_order(self):
        with patch.object(cli.shutil, "which", side_effect=["/tools/pio"]):
            self.assertEqual(cli.platformio_command(), ["/tools/pio"])
        with patch.object(cli.shutil, "which", side_effect=[None, "/tools/platformio"]):
            self.assertEqual(cli.platformio_command(), ["/tools/platformio"])

    def test_core_paths_and_spaces(self):
        for name in ("pio", "platformio"):
            for directory in ("Scripts", "bin"):
                suffix = ".exe" if cli.os.name == "nt" else ""
                expected = Path("custom core") / "penv" / directory / (name + suffix)
                with patch.object(cli.shutil, "which", return_value=None), \
                     patch.object(Path, "is_file", lambda p: p == expected), \
                     patch.object(cli.os, "access", return_value=True):
                    self.assertEqual(cli.platformio_command("custom core"), [str(expected)])

    def test_standard_install(self):
        suffix = ".exe" if cli.os.name == "nt" else ""
        expected = Path.home() / ".platformio" / "penv" / "Scripts" / ("pio" + suffix)
        with patch.object(cli.shutil, "which", return_value=None), \
             patch.object(Path, "is_file", lambda p: p == expected), \
             patch.object(cli.os, "access", return_value=True):
            self.assertEqual(cli.platformio_command(), [str(expected)])

    def test_module_fallback_and_error(self):
        with patch.object(cli.shutil, "which", return_value=None), \
             patch.object(Path, "is_file", return_value=False), \
             patch.object(importlib.util, "find_spec", return_value=object()):
            self.assertEqual(cli.platformio_command(), [sys.executable, "-m", "platformio"])
        with patch.object(cli.shutil, "which", return_value=None), \
             patch.object(Path, "is_file", return_value=False), \
             patch.object(importlib.util, "find_spec", return_value=None):
            with self.assertRaisesRegex(RuntimeError, "PLATFORMIO_CORE_DIR"):
                cli.platformio_command()


if __name__ == "__main__":
    unittest.main()
