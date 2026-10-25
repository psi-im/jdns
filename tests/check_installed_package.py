"""Validate version and Qt-major selection against an installed QJDns SDK."""
import pathlib
import sys
import subprocess
import tempfile

qt = int(sys.argv[2])
prefix = pathlib.Path(sys.argv[1]).resolve()
assert not (prefix / 'include/jdns/qjdns_datagramtransport.h').exists()
for version in ('2.0', '3.0', '4.0'):
    with tempfile.TemporaryDirectory() as directory:
        root = pathlib.Path(directory)
        (root / 'CMakeLists.txt').write_text(f'''
cmake_minimum_required(VERSION 3.19)
project(jdns_probe LANGUAGES CXX)
set(QT_DEFAULT_MAJOR_VERSION {qt})
find_package(Qt{qt} REQUIRED COMPONENTS Core Network)
find_package(QJDns {version} CONFIG REQUIRED)
get_target_property(actual QJDns::QJDns INTERFACE_QJDNS_QT_MAJOR_VERSION)
if(NOT actual EQUAL {qt})
message(FATAL_ERROR "wrong Qt")
endif()
''')
        result = subprocess.run(['cmake', '-S', str(root), '-B', str(root / 'build'),
                                 f'-DCMAKE_PREFIX_PATH={prefix}'], capture_output=True, text=True)
        assert (result.returncode == 0) == (version == '3.0'), result.stdout + result.stderr
with tempfile.TemporaryDirectory() as directory:
    root = pathlib.Path(directory)
    other = 5 if qt == 6 else 6
    (root / 'CMakeLists.txt').write_text(f'''
cmake_minimum_required(VERSION 3.19)
project(jdns_wrong_qt LANGUAGES CXX)
set(QT_DEFAULT_MAJOR_VERSION {other})
find_package(Qt{other} REQUIRED COMPONENTS Core Network)
find_package(QJDns 3.0 CONFIG QUIET)
if(QJDns_FOUND OR TARGET QJDns::QJDns)
message(FATAL_ERROR "accepted a package for the wrong Qt major")
endif()
''')
    result = subprocess.run(['cmake', '-S', str(root), '-B', str(root / 'build'),
                             f'-DQJDns_DIR={prefix}/lib/cmake/qjdns-qt{qt}'], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
print(f'Qt {qt}: installed version and Qt-major rejection checks passed', flush=True)
