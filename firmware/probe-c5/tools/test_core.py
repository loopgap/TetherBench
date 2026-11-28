"""Build and run portable firmware checks with an installed host C compiler."""
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
CORE = ROOT / "components/probe_core"
compiler = os.environ.get("CC") or shutil.which("gcc") or shutil.which("clang")
if not compiler:
    raise SystemExit("Install or select a host C compiler with CC; no firmware check was run.")
with tempfile.TemporaryDirectory(prefix="tetherbench-core-") as folder:
    executable = pathlib.Path(folder) / ("core_test.exe" if os.name == "nt" else "core_test")
    subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O2",
                    "-I", str(CORE / "include"), str(CORE / "probe_core.c"),
                    str(ROOT / "tests/core_test.c"), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True, timeout=15)
    target_link = ROOT / "components/probe_target_ota"
    subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O2",
                    "-I", str(ROOT / "tests/stubs"), "-I", str(CORE / "include"),
                    "-I", str(target_link / "include"), "-I", str(ROOT / "components/probe_ota/include"),
                    str(CORE / "probe_core.c"),
                    str(target_link / "probe_target_ota.c"), str(ROOT / "tests/target_link_test.c"),
                    "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True, timeout=15)
    wifi = ROOT / "components/probe_wifi"
    subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-sign-compare", "-O2",
                    "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
                    "-I", str(ROOT / "tests/wifi_stubs/include"), "-I", str(wifi / "include"),
                    "-I", str(CORE / "include"), str(ROOT / "tests/wifi_scan_test.c"),
                    str(CORE / "probe_core.c"), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True, timeout=15)
    subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O2",
                    "-I", str(ROOT / "tests/stubs"), "-I", str(CORE / "include"),
                    "-I", str(ROOT / "components/probe_ota/include"),
                    str(CORE / "probe_core.c"), str(ROOT / "tests/ota_test.c"),
                    "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True, timeout=15)
    link = ROOT / "components/probe_link"
    link_stubs = ROOT / "tests/probe_link_stubs"
    subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O2",
                    "-I", str(link_stubs), "-I", str(CORE / "include"),
                    "-I", str(link / "include"), str(CORE / "probe_core.c"),
                    str(link / "probe_link.c"), str(ROOT / "tests/probe_link_test.c"),
                    "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True, timeout=15)
    vendor = ROOT / "third_party/CMSIS-DAP"
    subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Wno-unused-parameter", "-O2",
                    "-DTB_HOST_TEST", "-DTB_HOST_EXPIRE_TEST",
                    "-I", str(CORE / "include"), "-I", str(ROOT / "components/probe_dap/include"),
                    "-I", str(ROOT / "components/probe_dap/private"),
                    "-I", str(ROOT / "tests"), "-I", str(vendor / "Include"),
                    str(CORE / "probe_core.c"), str(vendor / "Source/DAP.c"), str(vendor / "Source/SW_DP.c"),
                    str(ROOT / "tests/dap_test.c"), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True, timeout=15)
    subprocess.run([sys.executable, str(ROOT / "tools/test_pyocd_transport.py")], check=True, timeout=15)
    subprocess.run([sys.executable, str(ROOT / "tests/test_service_policy.py")], check=True, timeout=15)
    subprocess.run([sys.executable, str(ROOT / "tests/test_hil_preflight.py")], check=True, timeout=15)
    subprocess.run([sys.executable, str(ROOT / "tests/test_two_c5_link.py")], check=True, timeout=15)
    subprocess.run([sys.executable, str(ROOT / "tests/test_two_c5_hil.py")], check=True, timeout=15)
