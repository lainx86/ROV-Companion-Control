"""Exercise startup through a PTY with simulated network and stream commands."""

import fcntl
import json
import os
from pathlib import Path
import pty
import select
import struct
import subprocess
import sys
import tempfile
import termios
import time


def scenario(binary, name, cidr="192.0.2.1/30", preferred=None,
             autostart=None, delay=0, action="auto", link="up"):
    with tempfile.TemporaryDirectory(prefix="rov-startup-") as directory:
        root = Path(directory)
        commands = root / "bin"
        commands.mkdir()
        scripts = {
            "ip": """#!/bin/sh
printf '%s\\n' "$*" >> "$ROV_TEST_NETWORK_CALLS"
case "$*" in
  *route*) printf '1.1.1.1 dev wlan0 src 198.51.100.1\\n' ;;
  *link*)
    printf '1: lo: <LOOPBACK,UP,LOWER_UP> state UNKNOWN\\n'
    printf '2: wlan0: <BROADCAST,MULTICAST,UP,LOWER_UP> state UP\\n'
    printf '3: docker0: <BROADCAST,MULTICAST,UP,LOWER_UP> state UP\\n'
    if [ "$ROV_TEST_LINK" = up ]; then
      printf '4: %s: <BROADCAST,MULTICAST,UP,LOWER_UP> state UP\\n' "$ROV_TEST_IFACE"
    elif [ "$ROV_TEST_LINK" = down ]; then
      printf '4: %s: <NO-CARRIER,BROADCAST,MULTICAST,UP> state DOWN\\n' "$ROV_TEST_IFACE"
    fi ;;
  *addr*)
    if [ "$6" = "$ROV_TEST_IFACE" ]; then
      if [ -n "$ROV_TEST_CIDR" ]; then
        printf '4: %s inet %s\\n' "$ROV_TEST_IFACE" "$ROV_TEST_CIDR"
      fi
    else
      printf '2: wlan0 inet 198.51.100.1/30\\n'
    fi ;;
esac
""",
            "ping": "#!/bin/sh\nprintf '%s\\n' \"$*\" >> \"$ROV_TEST_PROBES\"\nsleep \"$ROV_TEST_DELAY\"\nexit 0\n",
            "gst-launch-1.0": "#!/bin/sh\nprintf '%s\\n' \"$@\" >> \"$ROV_TEST_CALLS\"\n",
            "mavproxy.py": "#!/bin/sh\nprintf '%s\\n' \"$@\" >> \"$ROV_TEST_CALLS\"\n",
        }
        for command, contents in scripts.items():
            path = commands / command
            path.write_text(contents)
            path.chmod(0o755)
        config_file = root / ".rov_control.json"
        if preferred is not None or autostart is not None:
            config = {"ip": preferred or "192.168.1.198"}
            if autostart is not None:
                config["autostart"] = autostart
            config_file.write_text(json.dumps(config))
        calls = root / "calls"
        probes = root / "probes"
        network_calls = root / "network_calls"
        env = dict(os.environ)
        # Without HOME, config.cpp uses cwd; the user's config is never touched.
        env.pop("HOME", None)
        env.update(TERM="xterm", PATH=f"{commands}:/usr/bin:/bin",
                   ROV_TEST_CIDR=cidr, ROV_TEST_DELAY=str(delay),
                   ROV_TEST_CALLS=str(calls), ROV_TEST_PROBES=str(probes),
                   ROV_TEST_NETWORK_CALLS=str(network_calls),
                   ROV_TEST_IFACE=ethernet or "missing-wired0", ROV_TEST_LINK=link)
        master, slave = pty.openpty()
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 40, 160, 0, 0))
        child = subprocess.Popen([*emulator, binary], stdin=slave, stdout=slave, stderr=slave,
                                 cwd=root, env=env)
        os.close(slave)
        output = b""

        def expect(text, timeout=5):
            nonlocal output
            deadline = time.monotonic() + timeout
            while text.encode() not in output:
                assert time.monotonic() < deadline, (name, text, output)
                if select.select([master], [], [], 0.05)[0]:
                    output += os.read(master, 65536)

        try:
            expect("ROV STREAM CONTROL")
            if action == "quit":
                os.write(master, b"q")
            else:
                if action == "stop":
                    os.write(master, b"x")
                    expect("Semua stream dihentikan")
                if delay > 1.5:
                    time.sleep(1.6)
                    assert not calls.exists(), "stream started before scan completed"
                expected_ip = preferred or "192.168.1.198"
                if action in ("select", "cancel"):
                    expect("Hasil scan Ethernet")
                    time.sleep(1.6)
                    assert not calls.exists(), "stream started before IP selection"
                    os.write(master, b"\n" if action == "select" else b"\x1b")
                    if action == "select":
                        expected_ip = "192.0.2.2"
                        expect("Target IP diubah ke " + expected_ip)
                    else:
                        expect("Pemilihan IP dibatalkan")
                elif action == "error":
                    expect("Ethernet aktif dengan IPv4 tidak ditemukan")
                else:
                    expected_ip = preferred or "192.0.2.2"
                    expect("Target IP diubah ke " + expected_ip)

                if action not in ("cancel", "error", "stop"):
                    expect("Starting MAVProxy")
                    deadline = time.monotonic() + 3
                    while (not calls.exists() or "--master=" not in calls.read_text()
                           or calls.read_text().count("host=") != 2):
                        assert time.monotonic() < deadline
                        time.sleep(0.05)
                    stream_args = calls.read_text()
                    assert stream_args.count("host=" + expected_ip) == 2
                    assert "port=5070" in stream_args and "port=5090" in stream_args
                    assert stream_args.count("--master=") == 1
                    assert "--out=udp:" + expected_ip + ":14550" in stream_args
                    assert "--out=udp:" + expected_ip + ":14551" in stream_args
                    if action == "manual":
                        output = b""
                        os.write(master, b"n")
                        expect("Hasil scan Ethernet")
                        os.write(master, b"\n")
                        expect("Target IP diubah ke " + expected_ip)
                    elif action == "edit":
                        output = b""
                        os.write(master, b"e")
                        expect("Edit konfigurasi")
                        # Up from the first field wraps to MAV UDP out 2.
                        os.write(master, b"\x1bOA" + b"\x7f" * 5 + b"14600\n")
                        expect("Konfigurasi diterapkan.")
                    time.sleep(0.3)
                    assert calls.read_text() == stream_args, "streams started more than once"
                else:
                    time.sleep(1.6)
                    assert not calls.exists()
                os.write(master, b"q")
            assert child.wait(timeout=5) == 0
            if action != "quit":
                saved = json.loads(config_file.read_text())
                assert saved["ip"] == expected_ip
                assert "autostart" not in saved
                if action == "edit":
                    assert saved["mav_port1"] == 14600
            else:
                assert not calls.exists(), "streams started during shutdown"
            assert "route" not in network_calls.read_text()
            assert "dev wlan0" not in network_calls.read_text()
            assert "dev docker0" not in network_calls.read_text()
            if action == "error":
                assert not probes.exists(), "probed a host without active Ethernet"
            elif probes.exists():
                assert all(line.startswith(f"-I {ethernet} -c 1 -W 1 192.0.2.")
                           for line in probes.read_text().splitlines())
            print(name + ": OK")
        finally:
            if child.poll() is None:
                child.kill()
                child.wait()
            os.close(master)


binary = str(Path(sys.argv[1]).resolve())
emulator = sys.argv[2:]
# Only NIC metadata is real. Link state, IP addresses and probes are simulated.
# Hardware-independent classification coverage lives in core_tests.
ethernet = next((path.name for path in sorted(Path("/sys/class/net").iterdir())
                 if (path / "device").exists()
                 and not (path / "wireless").exists()
                 and not (path / "phy80211").exists()
                 and (path / "type").read_text().strip() == "1"), None)
if ethernet:
    scenario(binary, "first launch selects Ethernet peer despite Wi-Fi route")
    scenario(binary, "legacy autostart=false still starts all streams", autostart=False)
    scenario(binary, "saved target preferred among several peers",
             cidr="192.0.2.1/29", preferred="192.0.2.5")
    scenario(binary, "ambiguous scan waits for selection", cidr="192.0.2.1/29",
             action="select")
    scenario(binary, "cancel selection suppresses autostart", cidr="192.0.2.1/29",
             autostart=True, action="cancel")
    scenario(binary, "autostart waits for slow scan", delay=2, autostart=True)
    scenario(binary, "stop during scan cancels automatic start", delay=2, action="stop")
    scenario(binary, "manual scan does not restart streams", action="manual")
    scenario(binary, "edit navigation after removing autostart option", action="edit")
    scenario(binary, "quit while scanning", delay=2, action="quit")
else:
    print("SKIP successful-scan PTY scenarios: no physical Ethernet metadata")
scenario(binary, "Ethernet without IPv4 preserves saved target", cidr="",
         preferred="192.0.2.5", autostart=True, action="error")
scenario(binary, "Wi-Fi only never falls back", preferred="192.0.2.5",
         autostart=True, action="error", link="absent")
scenario(binary, "disconnected Ethernet never falls back", preferred="192.0.2.5",
         autostart=True, action="error", link="down")
