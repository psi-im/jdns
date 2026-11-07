"""Check JDNS install paths when GNUInstallDirs and legacy LIB_SUFFIX coexist."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class LibraryInstallPath(unittest.TestCase):
    def test_gnuinstalldirs_does_not_duplicate_legacy_suffix(self):
        cases = (
            ("lib64", "64", "/usr/lib64"),
            ("lib", "64", "/usr/lib64"),
            ("lib/x86_64-linux-gnu", "64", "/usr/lib/x86_64-linux-gnu"),
            ("/usr/lib64", "64", "/usr/lib64"),
            ("lib64", "", "/usr/lib64"),
        )
        for libdir, suffix, expected in cases:
            with self.subTest(libdir=libdir, suffix=suffix):
                with tempfile.TemporaryDirectory() as directory:
                    build = Path(directory) / "build"
                    result = subprocess.run(
                        [
                            "cmake", "-S", str(ROOT), "-B", str(build),
                            "-DCMAKE_INSTALL_PREFIX=/usr",
                            "-DBUILD_QJDNS=OFF", "-DBUILD_JDNS_TOOL=OFF",
                            "-DJDNS_ENABLE_INSTALL=ON",
                            f"-DCMAKE_INSTALL_LIBDIR={libdir}",
                            f"-DLIB_SUFFIX={suffix}",
                        ],
                        text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                    )
                    self.assertEqual(result.returncode, 0, result.stdout)
                    cache = (build / "CMakeCache.txt").read_text()
                    self.assertIn(f"LIB_INSTALL_DIR:PATH={expected}\n", cache)


if __name__ == "__main__":
    unittest.main()
