#!/usr/bin/env python3
"""Native zone/dashboard end-to-end checks using two in-process fake speakers.

Run after building cxstcc: /usr/bin/python test/zone_live_check.py.
No real speakers, third-party dependencies, proxy, stream fetching, or automatic resume.
Scratch data stays under the repository and is removed on success; failed runs retain logs.
"""

import json
import os
import shutil
import signal
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.request
import uuid

from fake_speaker import FakeSpeaker, REPO, REST_PORT, UPNP_PORT, WS_PORT

OPENER = urllib.request.build_opener(urllib.request.ProxyHandler({}))
ZONE_ID = "test-pair"
FIRST_ID = "ZONEFIRST"
SECOND_ID = "ZONESECOND"
STREAMS = {"streams": [{"name": "zone-station", "display_name": "Zone Station",
                        "url": "http://example.invalid/zone-station"}]}


def request(base, method, path, body=None, etag=None, xml=False):
    data = body.encode() if xml else None if body is None else json.dumps(body).encode()
    req = urllib.request.Request(base + path, data=data, method=method)
    if data is not None:
        req.add_header("Content-Type", "text/xml" if xml else "application/json")
    if etag is not None:
        req.add_header("If-Match", etag)
    try:
        response = OPENER.open(req, timeout=15)
    except urllib.error.HTTPError as error:
        response = error
    with response:
        raw = response.read().decode()
        try:
            answer = json.loads(raw)
        except ValueError:
            answer = raw
        return response.code, answer, response.headers.get("ETag")


def require(condition, label, detail=""):
    if not condition:
        raise AssertionError("%s: %s" % (label, detail))
    print("PASS  " + label, flush=True)


def wait_for(predicate, label, timeout=10):
    deadline = time.monotonic() + timeout
    last = None
    while time.monotonic() < deadline:
        try:
            last = predicate()
            if last:
                return last
        except (OSError, urllib.error.URLError):
            pass
        time.sleep(0.05)
    raise AssertionError("timeout waiting for %s (last=%r)" % (label, last))


def free_port(preferred=8098, kind=socket.SOCK_STREAM):
    with socket.socket(type=kind) as listener:
        try:
            listener.bind(("127.0.0.1", preferred))
        except OSError:
            listener.bind(("127.0.0.1", 0))
        return listener.getsockname()[1]


def free_hosts():
    hosts = []
    for index in range(20, 100):
        host = "127.0.0.%d" % index
        sockets = []
        try:
            for port in (WS_PORT, REST_PORT, UPNP_PORT):
                listener = socket.socket()
                sockets.append(listener)
                listener.bind((host, port))
            hosts.append(host)
        except OSError:
            pass
        finally:
            for listener in sockets:
                listener.close()
        if len(hosts) == 2:
            return hosts
    raise RuntimeError("no two free loopback speaker addresses")


def native(speaker, path, members):
    body = '<zone master="%s">%s</zone>' % (
        speaker.device_id, "".join('<member ipaddress="%s">%s</member>' %
                                  (member.host, member.device_id) for member in members))
    return request("http://%s:%d" % (speaker.host, REST_PORT), "POST", path, body, xml=True)


def assert_selected(answer, kind, identity):
    require(answer.get("selected") == {"type": kind, "id": identity},
            "selected target is %s %s" % (kind, identity), repr(answer))


def run_checks(base, first, second, directory, restart):
    def targets():
        status, answer, etag = request(base, "GET", "/api/targets")
        require(status == 200 and isinstance(answer, dict) and etag,
                "targets returns snapshot and ETag", repr((status, answer, etag)))
        return answer, etag

    def post(path, body):
        status, answer, _ = request(base, "POST", path, body)
        require(status == 200, path + " succeeds", repr((status, answer)))
        return answer

    def save(zones, expected=200, etag=None):
        if etag is None:
            _, etag = targets()
        status, answer, _ = request(base, "PUT", "/api/zones", {"zones": zones}, etag)
        require(status == expected, "zone save returns %d" % expected, repr((status, answer)))

    def selected(kind, identity, **options):
        post("/api/target", dict({"type": kind, "id": identity, "takeover": False}, **options))
        answer, _ = targets()
        assert_selected(answer, kind, identity)
        return answer

    zone = {"id": ZONE_ID, "name": "Test Pair", "leader_id": FIRST_ID,
            "members": [FIRST_ID, SECOND_ID]}
    def unsaved_found():
        status, discovered, _ = request(base, "POST", "/api/speakers/discover", {})
        return status == 200 and any(item["id"] == SECOND_ID and not item["saved"]
                                    for item in discovered["speakers"])
    wait_for(unsaved_found, "discovered unsaved second speaker")
    require(True, "second speaker discovered without saving first")
    selected("speaker", SECOND_ID)
    with open(os.path.join(directory, "devices.json")) as source:
        discovered_config = json.load(source)
    require(any(item["device_id"] == SECOND_ID for item in discovered_config["devices"]),
            "selecting discovered unsaved speaker persists its identity")
    selected("speaker", FIRST_ID)
    answer, original_etag = targets()
    require({item["id"] for item in answer["speakers"]} == {FIRST_ID, SECOND_ID},
            "both saved speakers appear")
    assert_selected(answer, "speaker", FIRST_ID)
    for body, expected in (({"type": "speaker", "id": "UNKNOWN"}, 404),
                           ({"type": "zone", "id": "UNKNOWN"}, 404),
                           ({"type": "invalid", "id": FIRST_ID}, 400)):
        status, detail, _ = request(base, "POST", "/api/target", body)
        require(status == expected, "invalid/unavailable target is rejected", repr((status, detail)))
    status, _, _ = request(base, "PUT", "/api/zones", {"zones": [zone]})
    require(status == 428, "zone edits require If-Match")
    save([dict(zone, members=[FIRST_ID, "UNKNOWN"])], expected=400)
    save([zone])
    save([zone], expected=412, etag=original_etag)
    require(not first.zone()["master"] and not second.zone()["master"] and
            not first.plays and not second.plays, "saving a zone does not group or play")
    renamed = dict(zone, name="Renamed Pair")
    save([renamed])
    answer, _ = targets()
    require(any(item["id"] == ZONE_ID and item["name"] == "Renamed Pair" for item in answer["zones"]),
            "saved zone rename is visible")
    save([])
    require(not targets()[0]["zones"], "unused saved zone can be deleted")
    save([zone])

    second.zone_supported = False
    status, detail, _ = request(base, "POST", "/api/target",
                              {"type": "zone", "id": ZONE_ID, "takeover": False})
    require(status >= 400 and not first.zone()["master"],
            "unsupported member cannot partially establish a zone", repr((status, detail)))
    second.zone_supported = True
    second.set_refusing(True)
    status, detail, _ = request(base, "POST", "/api/target",
                              {"type": "zone", "id": ZONE_ID, "takeover": False})
    require(status >= 400 and not first.zone()["master"],
            "refused native grouping is surfaced without partial success", repr((status, detail)))
    second.set_refusing(False)
    selected("zone", ZONE_ID)
    require(first.zone()["master"] == FIRST_ID and second.zone() == first.zone(),
            "selecting zone establishes verified native topology")
    require(not first.plays and not second.plays, "target selection does not start playback")
    post("/api/play", {"stream": "zone-station"})
    wait_for(lambda: first.now["status"] == second.now["status"] == "PLAY_STATE",
             "group playback")
    require(len(first.plays) == 1 and not second.plays and first.now == second.now,
            "only leader receives UPnP stream; native members mirror playback")
    post("/api/playback", {"action": "pause"})
    wait_for(lambda: first.now["status"] == second.now["status"] == "PAUSE_STATE", "group pause")
    post("/api/playback", {"action": "play"})
    wait_for(lambda: first.now["status"] == second.now["status"] == "PLAY_STATE", "group resume")
    before_plays = list(first.plays)
    selected("zone", ZONE_ID)
    require(first.plays == before_plays and first.now["status"] == "PLAY_STATE",
            "same-leader target selection does not reset playback")

    post("/api/member-volume", {"id": SECOND_ID, "volume": 37})
    require(second.volume == 37 and first.volume == 11, "member volume remains independent")
    post("/api/member-volume", {"all": True, "volume": 42})
    require(first.volume == second.volume == 42, "explicit group volume reaches all members")
    post("/api/volume", {"volume": 48})
    require(first.volume == second.volume == 48, "main volume adjusts all zone members together")
    second.set_volume(37)
    post("/api/source", {"source": "aux"})
    wait_for(lambda: first.now["source"] == second.now["source"] == "AUX", "group source")
    post("/api/power", {"on": False})
    wait_for(lambda: first.now["source"] == second.now["source"] == "STANDBY", "group power off")
    require(True, "group power off reaches every native member")
    post("/api/power", {"on": True})
    wait_for(lambda: first.now["source"] == second.now["source"] == "AUX", "group power on")
    require(True, "group power on reaches every native member")

    status, detail, _ = request(base, "POST", "/api/target",
                              {"type": "speaker", "id": SECOND_ID, "takeover": False})
    require(status == 409, "solo detachment requires explicit takeover", repr(detail))
    selected("speaker", SECOND_ID, takeover=True)
    require(not first.zone()["master"] and not second.zone()["master"],
            "solo selection detaches native grouping")
    require(second.volume == 37 and first.volume == 48, "target switching preserves member volume")
    require(not second.plays, "hot-switched solo selection does not autoplay")
    post("/api/play", {"stream": "zone-station"})
    wait_for(lambda: len(second.plays) == 1 and second.now["source"] == "UPNP", "new leader playback")
    post("/api/playback", {"action": "pause"})
    require(second.now["status"] == "PAUSE_STATE" and first.now["source"] == "AUX",
            "hot-switched controls reach new solo leader only")
    selected("zone", ZONE_ID, make_default=True)
    with open(os.path.join(directory, "devices.json")) as source:
        saved = json.load(source)
    require(saved["default_target"] == {"type": "zone", "id": ZONE_ID} and
            saved["default_device"] == FIRST_ID, "default zone persists with compatible default_device")
    post("/api/zones/dissolve", {"id": ZONE_ID})
    require(not first.zone()["master"] and not second.zone()["master"], "dissolve removes native topology")
    plays_before_restart = (len(first.plays), len(second.plays))
    restart()
    answer, _ = targets()
    assert_selected(answer, "zone", ZONE_ID)
    require(answer["default"] == {"type": "zone", "id": ZONE_ID},
            "saved default zone is restored after controller restart")
    require(not first.zone()["master"] and not second.zone()["master"] and
            (len(first.plays), len(second.plays)) == plays_before_restart,
            "default zone startup neither establishes grouping nor starts playback")

    require(native(second, "/setZone", [first])[0] == 200, "external reverse-leader zone established")
    answer, _ = targets()
    external_zone = next(item for item in answer["zones"] if item["id"] == ZONE_ID)
    require(external_zone.get("state") == "different",
            "targets reports externally changed topology", repr(external_zone))
    status, answer, _ = request(base, "POST", "/api/target",
                               {"type": "zone", "id": ZONE_ID, "takeover": False})
    require(status == 409, "external incompatible topology requires explicit takeover", repr(answer))
    require(first.zone()["master"] == SECOND_ID, "conflict rejection leaves external topology untouched")
    selected("zone", ZONE_ID, takeover=True)
    require(first.zone()["master"] == FIRST_ID, "explicit takeover establishes desired leader")
    reverse = {"id": "test-reverse", "name": "Reverse Pair", "leader_id": SECOND_ID,
               "members": [FIRST_ID, SECOND_ID]}
    topology_before_save = first.zone()
    save([zone, reverse])
    require(first.zone() == topology_before_save, "editing saved zones leaves active native grouping unchanged")
    post("/api/play", {"stream": "zone-station"})
    require(native(second, "/setZone", [first])[0] == 200, "playing old leader externally becomes follower")
    old_stops = first.upnp_actions.count("Stop")
    plays = (len(first.plays), len(second.plays))
    selected("zone", reverse["id"])
    require(first.upnp_actions.count("Stop") == old_stops and
            (len(first.plays), len(second.plays)) == plays,
            "switching to new native leader neither stops old follower nor starts another stream")
    post("/api/playback", {"action": "pause"})
    require(first.now["status"] == second.now["status"] == "PAUSE_STATE",
            "reverse-leader hot switch controls native group")
    topology_before_delete = first.zone()
    save([zone])
    answer, _ = targets()
    require(all(item["id"] != reverse["id"] for item in answer["zones"]),
            "selected saved zone definition can be deleted")
    require(first.zone() == topology_before_delete and second.zone() == topology_before_delete,
            "deleting selected definition does not dissolve native topology")
    selected("speaker", FIRST_ID, takeover=True)
    second.stop()
    status, answer, _ = request(base, "POST", "/api/target",
                               {"type": "zone", "id": ZONE_ID, "takeover": False})
    require(status >= 400, "offline zone member gives explicit failure", repr(answer))
    require(not first.zone()["master"], "offline preflight failure does not partially group leader")
    answer, _ = targets()
    assert_selected(answer, "speaker", FIRST_ID)
    unavailable = next(item for item in answer["zones"] if item["id"] == ZONE_ID)
    require(unavailable["state"] == "unavailable", "targets explicitly reports unavailable zone member")


def main():
    directory = os.path.join(REPO, ".zone-live-check-" + uuid.uuid4().hex[:12])
    os.mkdir(directory)
    first_host, second_host = free_hosts()
    ssdp_port = free_port(0, socket.SOCK_DGRAM)
    first = FakeSpeaker(first_host, device_id=FIRST_ID, name="Zone First", volume=11, ssdp_port=ssdp_port)
    second = FakeSpeaker(second_host, device_id=SECOND_ID, name="Zone Second", volume=23, ssdp_port=ssdp_port)
    process = None
    success = False
    port = free_port()
    base = "http://127.0.0.1:%d" % port
    try:
        first.start()
        second.start()
        devices = {"default_device": FIRST_ID,
                   "devices": [{"device_id": speaker.device_id, "device_name": speaker.name,
                                "ip_address": speaker.host} for speaker in (first,)]}
        for filename, value in (("devices.json", devices), ("streams.json", STREAMS)):
            with open(os.path.join(directory, filename), "w") as out:
                json.dump(value, out, indent=2)
                out.write("\n")
        def launch():
            nonlocal process
            with open(os.path.join(directory, "control.log"), "a") as log:
                process = subprocess.Popen(
                    [os.path.join(REPO, "cxstcc"), "--data-dir", directory, "control",
                     "--no-proxy", "--no-resume", "--web", "--web-port", str(port)],
                    stdout=log, stderr=subprocess.STDOUT,
                    env=dict(os.environ, CXSTCC_SSDP_TARGETS=",".join(
                        "%s:%d" % (speaker.host, ssdp_port) for speaker in (first, second))))
            wait_for(lambda: process.poll() is None and request(base, "GET", "/api/health")[0] == 200,
                     "control dashboard startup")
        def restart():
            process.send_signal(signal.SIGTERM)
            process.wait(10)
            launch()
        launch()
        run_checks(base, first, second, directory, restart)
        success = True
        print("All zone end-to-end checks passed.", flush=True)
    except (AssertionError, OSError, ValueError, urllib.error.URLError) as error:
        print("FAIL  %s\nLogs/data retained in %s" % (error, directory), file=sys.stderr)
    finally:
        if process is not None and process.poll() is None:
            process.send_signal(signal.SIGTERM)
            try:
                process.wait(10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        second.stop()
        first.stop()
        if success:
            shutil.rmtree(directory)
    return 0 if success else 1


if __name__ == "__main__":
    sys.exit(main())
