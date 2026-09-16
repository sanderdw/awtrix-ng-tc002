"""Run the real firmware and broker; assert the published NG MQTT contract."""
import json
import os
from pathlib import Path
import queue
import socket
import subprocess
import sys
import time
import select
import threading
import urllib.error
import urllib.request

import paho.mqtt.client as mqtt
import pytest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
from paths import webui  # noqa: E402


def port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def wait_for(predicate, timeout=8):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            result = predicate()
            if result:
                return result
        except (OSError, urllib.error.URLError):
            pass
        time.sleep(0.03)
    raise AssertionError("condition did not become true")


class Device:
    def __init__(self, client, messages, http_port):
        self.client, self.messages, self.http_port = client, messages, http_port
        self.base_url = f"http://127.0.0.1:{http_port}"

    def api(self, path, body=None, method="GET"):
        raw = None if body is None else json.dumps(body).encode()
        req = urllib.request.Request(self.base_url+path, raw,
                                     {"Content-Type": "application/json"}, method=method)
        with urllib.request.urlopen(req, timeout=5) as r:
            return json.load(r)

    def drain(self):
        while not self.messages.empty():
            self.messages.get_nowait()

    def receive(self, topic, timeout=4):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                message = self.messages.get(timeout=max(0.01, deadline-time.monotonic()))
                if message.topic == topic:
                    return message
            except queue.Empty:
                break
        return None

    def cmd(self, route, payload="", ok=True):
        self.drain()
        body = payload if isinstance(payload, str) else json.dumps(payload)
        topic = f"tc002test/cmd/{route}"
        self.client.publish(topic, body).wait_for_publish()
        message = self.receive(topic+"/result")
        assert message is not None, topic
        assert not message.retain and message.qos == 0
        result = json.loads(message.payload)
        assert result["ok"] is ok, result
        if ok:
            assert result == {"ok": True}
        return result


@pytest.fixture(scope="module")
def device(tmp_path_factory):
    folder = tmp_path_factory.mktemp("tc002-integration")
    broker_port, http_port = port(), port()
    serial = os.environ.get("AWTRIX_DEVICE_SERIAL")
    from paths import adb as find_adb
    adb = find_adb()
    broker_host = os.environ.get("AWTRIX_TEST_BROKER_HOST", "127.0.0.1")
    relay_binary = os.environ.get("AWTRIX_TEST_RELAY")
    reverse = serial and broker_host == "127.0.0.1" and not relay_binary
    device_port = 18883 if reverse or relay_binary else broker_port
    (folder/"device.json").write_text(json.dumps({"mqttEnabled": True, "mqttHost": "127.0.0.1" if relay_binary else broker_host,
        "mqttPort": device_port, "mqttPrefix": "tc002test", "haDiscovery": True,
        "statsInterval": 1000, "hostname": "tc002-test", "ntpServer": ""}))
    broker_log = (folder/"broker.log").open("w")
    fw_log = (folder/"firmware.log").open("w")
    broker = subprocess.Popen([sys.executable, str(ROOT/"tests/broker.py"), str(broker_port)],
                              stdout=broker_log, stderr=subprocess.STDOUT)
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="tc002-tester")
    messages = queue.Queue()
    client.on_message = lambda _c, _u, m: messages.put(m)
    def connect():
        return client.connect(broker_host, broker_port) == 0
    firmware = None
    relay_process = relay_thread = None
    relay_stop = threading.Event()
    forward_port = port()
    try:
        wait_for(connect)
        client.loop_start()
        wait_for(client.is_connected)
        client.subscribe("#")
        binary = Path(os.environ.get("AWTRIX_TEST_BINARY", ROOT/"build-host/awtrix-tc002"))
        if serial:
            def device_adb(*args, **kwargs):
                return subprocess.run([adb, "-s", serial, *args], check=True, **kwargs)
            subprocess.run([adb, "connect", serial], check=True, stdout=fw_log)
            if relay_binary:
                device_adb("push", relay_binary, "/tmp/awtrix-mqtt-relay", stdout=fw_log)
                device_adb("forward", f"tcp:{forward_port}", "tcp:18885", stdout=fw_log)
                relay_process = subprocess.Popen([adb,"-s",serial,"shell","/tmp/awtrix-mqtt-relay"], stdout=fw_log, stderr=subprocess.STDOUT)
                def relay():
                    while not relay_stop.is_set():
                        try:
                            with socket.create_connection(("127.0.0.1",forward_port),timeout=1) as device_socket, \
                                 socket.create_connection((broker_host,broker_port),timeout=1) as broker_socket:
                                peers = [device_socket,broker_socket]
                                while not relay_stop.is_set():
                                    readable,_,_ = select.select(peers,[],[],0.2)
                                    for source in readable:
                                        data = source.recv(4096)
                                        if not data: raise ConnectionError("relay disconnected")
                                        peers[1-peers.index(source)].sendall(data)
                        except OSError:
                            relay_stop.wait(0.1)
                relay_thread = threading.Thread(target=relay,daemon=True)
                relay_thread.start()
            if reverse:
                device_adb("reverse", "tcp:18883", f"tcp:{broker_port}", stdout=fw_log)
            device_adb("shell", "mkdir -p /tmp/awtrix-integration-data", stdout=fw_log)
            device_adb("push", str(folder/"device.json"), "/tmp/awtrix-integration-data/device.json", stdout=fw_log)
            device_adb("push", str(binary), "/tmp/awtrix-integration-bin", stdout=fw_log)
            device_adb("push", str(webui()), "/tmp/awtrix-integration.html", stdout=fw_log)
            # EXIT restores the stock service even if the native app fails. The
            # run-for bound also restores it if the test client disconnects.
            command = "trap 'setprop ctl.start zkswe' EXIT; setprop ctl.stop zkswe; " \
                "/tmp/awtrix-integration-bin --hardware --no-matrix --data /tmp/awtrix-integration-data " \
                "--webui /tmp/awtrix-integration.html --port 18081 --run-for 60 " \
                "--pidfile /tmp/awtrix-integration.pid"
            firmware = subprocess.Popen([adb,"-s",serial,"shell",command], stdout=fw_log, stderr=subprocess.STDOUT)
        else:
            firmware = subprocess.Popen([str(binary), "--no-matrix", "--data", str(folder),
                "--webui", str(webui()), "--port", str(http_port)],
                stdout=fw_log, stderr=subprocess.STDOUT)
        d = Device(client, messages, http_port)
        if serial:
            d.base_url = "http://"+serial.split(":")[0]+":18081"
        wait_for(lambda: d.api("/api/v1/device")["mqtt"]["state"] == "connected")
        yield d
    finally:
        if firmware:
            if serial:
                pid = subprocess.run([adb,"-s",serial,"shell","cat /tmp/awtrix-integration.pid"],
                    capture_output=True, text=True).stdout.strip()
                if pid.isdecimal():
                    subprocess.run([adb,"-s",serial,"shell","kill "+pid], stdout=fw_log)
                firmware.wait(timeout=65)
                if reverse:
                    subprocess.run([adb,"-s",serial,"reverse","--remove","tcp:18883"], stdout=fw_log)
            else:
                firmware.terminate()
                try: firmware.wait(timeout=5)
                except subprocess.TimeoutExpired: firmware.kill(); firmware.wait()
        client.disconnect(); client.loop_stop()
        relay_stop.set()
        if relay_thread: relay_thread.join(timeout=3)
        if relay_process:
            pid = subprocess.run([adb,"-s",serial,"shell","cat /tmp/awtrix-relay.pid"],
                capture_output=True,text=True).stdout.strip()
            if pid.isdecimal():
                subprocess.run([adb,"-s",serial,"shell","kill "+pid],stdout=fw_log)
            relay_process.wait(timeout=95)
            subprocess.run([adb,"-s",serial,"forward","--remove",f"tcp:{forward_port}"],stdout=fw_log)
        broker.terminate(); broker.wait(timeout=5)
        if serial:
            subprocess.run([adb,'-s',serial,'shell',
                'rm -f /tmp/awtrix-integration-bin /tmp/awtrix-integration.html /tmp/awtrix-mqtt-relay'],stdout=fw_log)
        fw_log.close(); broker_log.close()


def test_geometry(device):
    screen = device.api("/api/v1/display/screen")
    assert (screen["width"], screen["height"], len(screen["pixels"])) == (52, 16, 832)
    assert device.api("/api/v1/device")["boardType"] == "tc002"


def test_pushed_apps_and_switch(device):
    device.cmd("apps/pushed/weather", {"text": "21C", "durationMs": 10000})
    device.cmd("apps/switch", "weather")
    device.cmd("apps/switch", {"name": "weather", "fast": True})
    device.cmd("apps/next")
    device.cmd("apps/previous")
    device.cmd("apps/order", {"order": ["Time", "weather"], "disabled": ["Date"]})
    device.cmd("apps/pushed/weather", "{}")
    device.cmd("apps/pushed/weather", {"text": "again"})
    device.cmd("apps/pushed/weather", "")
    result = device.cmd("apps/pushed/invalid/name", {"text": "no"}, ok=False)
    assert result["error"]["code"] == "invalidName"


def test_notifications(device):
    device.cmd("notify", {"name": "door", "text": "Doorbell", "hold": True})
    device.cmd("notify/dismiss/door")
    device.cmd("notify", {"text": "Hello", "durationMs": 1000})
    device.cmd("notify/dismiss")
    assert device.cmd("notify/dismiss/missing", ok=False)["error"]["code"] == "notFound"


def test_settings_atomic_validation(device):
    device.cmd("settings", {"brightness": 77, "autoBrightness": False})
    before = device.api("/api/v1/settings")
    result = device.cmd("settings", {"brightness": 999, "autoBrightness": True}, ok=False)
    assert result["error"]["field"] == "brightness"
    assert device.api("/api/v1/settings") == before


def test_display_moodlight_and_indicators(device):
    device.cmd("display", {"power": False})
    assert device.api("/api/v1/device")["matrixPower"] is False
    device.cmd("display", {"power": True, "overlay": None})
    device.cmd("display/moodlight", {"color": "#3366FF", "brightness": 120})
    device.cmd("display/moodlight", "")
    for i in (1, 2, 3):
        device.cmd(f"indicators/{i}", {"color": "#FF0000", "blinkMs": 500})
        device.cmd(f"indicators/{i}", {"color": 0})
        state = device.api("/api/v1/device")["indicators"][i-1]
        assert state["color"] == "#FF0000" and not state["on"] and state["blinkMs"] == 500
        device.cmd(f"indicators/{i}", "{}")
        assert device.api("/api/v1/device")["indicators"][i-1]["blinkMs"] == 0


@pytest.mark.parametrize("suffix", ["cmd", "cmd/", "cmd/typo", "state/settings",
    "cmd/indicators/4", "cmd/indicators/01", "cmd/apps/pushed/", "cmd/device/factory-reset",
    "cmd/settings/result"])
def test_unknown_topics_and_reply_echo_are_silent(device, suffix):
    device.drain()
    topic = f"tc002test/{suffix}"
    device.client.publish(topic, "{}").wait_for_publish()
    assert device.receive(topic+"/result", timeout=0.3) is None


def test_screen_mqtt(device):
    device.drain()
    device.client.publish("tc002test/cmd/screen/get", "")
    message = device.receive("tc002test/state/screen")
    assert message and not message.retain
    screen = json.loads(message.payload)
    assert (screen["width"], screen["height"], len(screen["pixels"])) == (52, 16, 832)
    result = device.receive("tc002test/cmd/screen/get/result")
    assert result and json.loads(result.payload) == {"ok": True}


def test_audio_routes_validate(device):
    assert device.cmd("audio/play", {}, ok=False)["error"]["code"] == "validationFailed"
    device.cmd("audio/stations", {"stations": []})
    device.cmd("audio/stop")
    device.cmd("audio/stop", {"scope": "sounds"})


def test_sleep_validation(device):
    for value in (0, -1, 1.5, "100"):
        result = device.cmd("device/sleep", {"durationMs": value}, ok=False)
        assert result["error"]["field"] == "durationMs"


def test_retained_state_and_discovery(device):
    device.drain()
    device.client.unsubscribe("#")
    time.sleep(0.1)
    device.client.subscribe("#")
    seen = {}
    deadline = time.monotonic()+3
    while time.monotonic()<deadline:
        try:
            m=device.messages.get(timeout=0.1)
            if m.retain: seen[m.topic]=m.payload
        except queue.Empty:
            pass
    for suffix in ("availability", "state/device", "state/settings", "state/apps/active", "state/audio",
                   "state/capabilities", "state/prefix", "state/buttons/left", "state/buttons/select", "state/buttons/right"):
        assert f"tc002test/{suffix}" in seen
    assert seen["tc002test/state/prefix"] == b"tc002test"
    assert any(k.startswith("homeassistant/device/") and k.endswith("/config") for k in seen)

@pytest.mark.parametrize('change', [{'panelWidth':32},{'panels':2},{'pinMatrix':21}])
def test_tc002_wiring_cannot_be_changed(device,change):
    before=device.api('/api/v1/system')
    with pytest.raises(urllib.error.HTTPError) as rejected:
        device.api('/api/v1/system',change,method='PUT')
    assert rejected.value.code==422
    after=device.api('/api/v1/system')
    for key in change:
        assert after[key]==before[key]
