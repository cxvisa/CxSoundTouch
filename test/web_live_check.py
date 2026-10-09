#!/usr/bin/env python3
"""End to end check of the dashboard's live updates, against test/fake_speaker.py: no real speaker.

It runs the built ./cxstcc against a fake SoundTouch on 127.0.0.2, from a throwaway data directory:

  control --web  reads the volume, and what the speaker is doing, at start; a volume, mute or
                 now-playing change on the speaker reaches a waiting /api/live/wait in milliseconds;
                 with no change the request holds ~3 s and answers unchanged; a fourth waiting at once
                 is told 503; POST /api/volume sets the speaker's volume, and the speaker's report of it
                 reaches every waiting dashboard at once, while anything but a JSON level from 0 to 100
                 is refused; POST /api/playback and /api/power press PLAY, PAUSE and POWER and answer
                 once the speaker has done it, pressing POWER only when it is not already that way;
                 POST /api/preset presses a preset (control plays its station, from standby too; 1
                 then 3 is the combo for 13, and 1, 1, 1 the three-digit combo for 111, while 1, 1
                 waits out the combo window first), /api/skip presses NEXT_TRACK or PREV_TRACK
                 (control starts its stream again), and /api/source switches to Bluetooth or AUX;
                 POST /api/play plays a stream by its name, on a preset or not, as a press for it would,
                 and remembers it by name; PUT /api/streams saves an edited list, refused without
                 If-Match, against a changed file, or breaking the rules, keeping combo_window_ms and a
                 .bak, and control uses it at once; stopping does not sit out a hold
  web            (standalone) has no /api/live/wait, so the page polls; /api/nowplaying still
                 carries the volume, read from the speaker, and the station's preset; the volume,
                 play/pause, power, presets and sources work there too; /api/play hands a stream on no
                 preset to the speaker itself and presses a preset's buttons; saving works there too

It plays its own stations (presets 1, 2, 11, 13 and 111, and one on no preset, on addresses nothing
fetches), so it does not depend on streams.json.

Run: make e2e (or python3 test/web_live_check.py). Exits non-zero if any check fails. To run it beside
a fake already up by hand, move it with E2E_CONTROL_PORT, E2E_WEB_PORT (default 8094 and 8095) and
E2E_SPEAKER_HOST (default 127.0.0.2); E2E_VERBOSE=1 logs the fake.
"""

import json
import os
import shutil
import signal
import subprocess
import sys
import tempfile
import threading
import time
import urllib.error
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from fake_speaker import FAKE_DEVICE_ID, REPO, FakeSpeaker, write_data_dir  # noqa: E402

BINARY = os.path.join(REPO, "cxstcc")
CONTROL_PORT = int(os.environ.get("E2E_CONTROL_PORT", "8094"))
WEB_PORT = int(os.environ.get("E2E_WEB_PORT", "8095"))
SPEAKER_HOST = os.environ.get("E2E_SPEAKER_HOST", "127.0.0.2")
KITCHEN_HOST = os.environ.get("E2E_KITCHEN_HOST", "127.0.0.3")     # a second speaker, for the Speakers page
KITCHEN_ID = "KITCHEN0"
FIRST_RUN_PORT = int(os.environ.get("E2E_FIRST_RUN_PORT", "8097"))
SSDP_PORT = int(os.environ.get("E2E_SSDP_PORT", "19001"))   # where the fakes answer a search for speakers
ONLINE_WINDOW = 10.0    # WebServer::ONLINE_WINDOW
HOLD = 3.0          # WebServer::LIVE_HOLD
PLAYBACK_SETTLE = 5.0   # WebServer::PLAYBACK_SETTLE
PROMPT = 0.5        # a generous bound for "at once"; in practice a few milliseconds
ELSEWHERE = "http://example.invalid/radio"      # a stream that is not control's, so control lets it be

# The stations the check plays, on presets 1, 2, 11, 13 and 111 (so 1 and 11 wait for another digit,
# and 1 then 3, or 1, 1, 1, are combos), and one on no preset, on addresses nothing fetches: the fake
# speaker only reports playing them. The combo window is control's default, 700 ms.
TEST_STREAMS = {
    "combo_window_ms": 700,
    "streams": [
        {"name": "one", "display_name": "Station One", "url": "http://example.invalid/one", "preset": 1},
        {"name": "two", "display_name": "Station Two", "url": "http://example.invalid/two", "preset": 2},
        {"name": "eleven", "display_name": "Station Eleven", "url": "http://example.invalid/eleven", "preset": 11},
        {"name": "thirteen", "display_name": "Station Thirteen", "url": "http://example.invalid/thirteen", "preset": 13},
        {"name": "one-eleven", "display_name": "Station 111", "url": "http://example.invalid/one-eleven", "preset": 111},
        {"name": "free", "display_name": "Station Free", "url": "http://example.invalid/free"},
    ],
}
COMBO_WINDOW = TEST_STREAMS["combo_window_ms"] / 1000.0

results = []


def check(ok, label, detail=""):
    results.append(bool(ok))
    print("%s  %s%s" % ("PASS" if ok else "FAIL", label, "  (%s)" % detail if detail else ""), flush=True)


def fetch(port, path, timeout=15):
    """The status, the body and the seconds taken, for a GET on 127.0.0.1."""
    started = time.monotonic()
    try:
        with urllib.request.urlopen("http://127.0.0.1:%d%s" % (port, path), timeout=timeout) as response:
            status, body = response.status, response.read()
    except urllib.error.HTTPError as error:
        status, body = error.code, error.read()
    return status, body.decode("utf-8", "replace"), time.monotonic() - started


def get(port, path, timeout=15):
    """As fetch, with the body parsed as JSON ({} when it is not)."""
    status, body, taken = fetch(port, path, timeout)
    try:
        data = json.loads(body)
    except ValueError:
        data = {}
    return status, data if isinstance(data, dict) else {}, taken


def send(port, method, path, body=None, content_type=None, timeout=15):
    """The status and the JSON answer ({} when it is not JSON) of a request with a body."""
    request = urllib.request.Request("http://127.0.0.1:%d%s" % (port, path), method=method,
                                     data=None if body is None else body.encode("utf-8"))
    if content_type is not None:
        request.add_header("Content-Type", content_type)
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            status, raw = response.status, response.read()
    except urllib.error.HTTPError as error:
        status, raw = error.code, error.read()
    try:
        data = json.loads(raw.decode("utf-8"))
    except ValueError:
        data = {}
    return status, data if isinstance(data, dict) else {}


def set_volume(port, level):
    return send(port, "POST", "/api/volume", json.dumps({"volume": level}), "application/json")


def discover(port):
    """Keeps the search for speakers going, as the open Speakers page does; the answer's speakers."""
    return send(port, "POST", "/api/speakers/discover", "{}", "application/json")


def make_default(port, device_id):
    return send(port, "PUT", "/api/speakers/default", json.dumps({"id": device_id}), "application/json")


def speaker_entry(answer, device_id):
    return next((item for item in answer.get("speakers", []) if item.get("id") == device_id), {})


def wait_found(port, condition, timeout=15.0):
    """Searches, as the open page does every 2 s, until condition holds for the answer; the last answer."""
    deadline = time.monotonic() + timeout
    answer = {}
    while time.monotonic() < deadline:
        answer = discover(port)[1]
        if condition(answer):
            return answer
        time.sleep(1.0)
    return answer


def both_online(answer):
    return (speaker_entry(answer, FAKE_DEVICE_ID).get("online") is True
            and speaker_entry(answer, KITCHEN_ID).get("online") is True)


def playback(port, action):
    return send(port, "POST", "/api/playback", json.dumps({"action": action}), "application/json")


def power(port, on):
    return send(port, "POST", "/api/power", json.dumps({"on": on}), "application/json")


def preset(port, number):
    return send(port, "POST", "/api/preset", json.dumps({"preset": number}), "application/json")


def skip(port, direction):
    return send(port, "POST", "/api/skip", json.dumps({"direction": direction}), "application/json")


def source(port, which):
    return send(port, "POST", "/api/source", json.dumps({"source": which}), "application/json")


def select(port, number):
    return send(port, "POST", "/api/select", json.dumps({"preset": number}), "application/json")


def play_stream(port, name):
    return send(port, "POST", "/api/play", json.dumps({"stream": name}), "application/json")


def streams_with_tag(port):
    """The stream list and the ETag it came with."""
    with urllib.request.urlopen("http://127.0.0.1:%d/api/streams" % port, timeout=15) as response:
        return json.loads(response.read().decode("utf-8")), response.headers.get("ETag")


def put_streams(port, streams, tag, content_type="application/json", raw=None):
    """The status, the JSON answer and the ETag of a PUT of this stream list, sent with tag as If-Match
    (none when tag is None); raw, when given, is sent instead."""
    body = raw if raw is not None else json.dumps({"streams": streams})
    request = urllib.request.Request("http://127.0.0.1:%d/api/streams" % port, method="PUT", data=body.encode("utf-8"))
    if content_type is not None:
        request.add_header("Content-Type", content_type)
    if tag is not None:
        request.add_header("If-Match", tag)
    try:
        with urllib.request.urlopen(request, timeout=15) as response:
            status, answer, headers = response.status, response.read(), response.headers
    except urllib.error.HTTPError as error:
        status, answer, headers = error.code, error.read(), error.headers
    try:
        data = json.loads(answer.decode("utf-8"))
    except ValueError:
        data = {}
    return status, data if isinstance(data, dict) else {}, headers.get("ETag")


def wait_until(condition, timeout=8.0):
    """Polls condition until it is true or timeout passes; whether it came true."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if condition():
            return True
        time.sleep(0.05)
    return condition()


class Waiter(threading.Thread):
    """A GET in the background that notes when its answer came."""

    def __init__(self, port, path):
        super().__init__(daemon=True)
        self.port = port
        self.path = path
        self.status = None
        self.data = {}
        self.answered = None

    def run(self):
        try:
            self.status, self.data, _ = get(self.port, self.path)
        except OSError:
            self.status = "connection lost"
        self.answered = time.monotonic()


def launch(arguments, port, log_path):
    """Starts cxstcc and waits for its dashboard to answer. Its searches for speakers go to the fakes."""
    log = open(log_path, "w")
    env = dict(os.environ, CXSTCC_SSDP_TARGETS=",".join("%s:%d" % (host, SSDP_PORT)
                                                        for host in (SPEAKER_HOST, KITCHEN_HOST)))
    process = subprocess.Popen([BINARY] + arguments, stdout=log, stderr=subprocess.STDOUT, env=env)
    log.close()
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline and process.poll() is None:
        try:
            if fetch(port, "/api/health", timeout=1)[0] == 200:
                return process
        except OSError:
            pass
        time.sleep(0.1)
    process.kill()
    process.wait()
    raise RuntimeError("cxstcc %s did not come up on port %d; see %s" % (arguments[2], port, log_path))


def wait_held(port, seq):
    """A request for /api/live/wait that is being held, given the server has had a moment."""
    waiter = Waiter(port, "/api/live/wait?since=%d" % seq)
    waiter.start()
    time.sleep(0.5)
    return waiter


def check_setting_volume(speaker):
    seq = get(CONTROL_PORT, "/api/live/wait")[1].get("seq", 0)
    waiter = wait_held(CONTROL_PORT, seq)
    asked = time.monotonic()
    status, answer = set_volume(CONTROL_PORT, 33)
    waiter.join(HOLD + 5)
    latency = (waiter.answered or asked) - asked
    check(status == 200 and answer.get("volume") == 33 and speaker.volume == 33,
          "POST /api/volume sets the speaker's volume", "status %d, speaker at %d" % (status, speaker.volume))
    check(waiter.status == 200 and waiter.data.get("volume") == 33 and 0 <= latency < PROMPT,
          "the speaker's report of it reaches a waiting dashboard at once",
          "%.1f ms from asking" % (latency * 1000))

    status, _ = send(CONTROL_PORT, "POST", "/api/volume", '{"volume": 50}', "text/plain")
    check(status == 415 and speaker.volume == 33,
          "anything but JSON is refused (415), so a form on another site cannot change it", "status %d" % status)

    statuses = [set_volume(CONTROL_PORT, 101)[0], set_volume(CONTROL_PORT, -1)[0], set_volume(CONTROL_PORT, "40")[0],
                send(CONTROL_PORT, "POST", "/api/volume", '{"volume": ', "application/json")[0]]
    check(statuses == [400] * 4 and speaker.volume == 33,
          "a level outside 0..100, not a number, or malformed JSON is refused (400)", "statuses %s" % statuses)

    statuses = [fetch(CONTROL_PORT, "/api/volume")[0], send(CONTROL_PORT, "OPTIONS", "/api/volume")[0]]
    check(statuses == [405, 405],
          "GET, and OPTIONS (a cross-site preflight), are refused on /api/volume (405)", "statuses %s" % statuses)

    status, _ = send(CONTROL_PORT, "POST", "/api/volume", json.dumps({"volume": 1, "pad": "x" * 6000}), "application/json")
    check(status == 413 and speaker.volume == 33, "a body over 4 KB is refused (413), and the refusal arrives",
          "status %d" % status)

    speaker.set_refusing(True)
    status, _ = set_volume(CONTROL_PORT, 40)
    speaker.set_refusing(False)
    check(status == 502 and speaker.volume == 33, "when the speaker refuses, the dashboard is told so (502)",
          "status %d" % status)


def check_transport(speaker):
    seq = get(CONTROL_PORT, "/api/live/wait")[1].get("seq", 0)
    waiter = wait_held(CONTROL_PORT, seq)
    changed = time.monotonic()
    speaker.set_now_playing(source="UPNP", status="PLAY_STATE", location=ELSEWHERE, track="A Song", artist="An Artist")
    waiter.join(HOLD + 5)
    latency = (waiter.answered or changed) - changed
    check(waiter.status == 200 and waiter.data.get("source") == "UPNP" and waiter.data.get("status") == "PLAY_STATE"
          and 0 <= latency < PROMPT,
          "what the speaker is playing reaches a waiting dashboard at once", "%.1f ms" % (latency * 1000))

    keys = len(speaker.keys)
    started = time.monotonic()
    status, _ = playback(CONTROL_PORT, "pause")
    taken = time.monotonic() - started
    live = get(CONTROL_PORT, "/api/live")[1]
    check(status == 200 and speaker.keys[keys:] == ["PAUSE"] and speaker.now["status"] == "PAUSE_STATE"
          and live.get("status") == "PAUSE_STATE" and taken < 1.0,
          "pause presses PAUSE once, and answers once the speaker has paused", "%.0f ms" % (taken * 1000))

    keys = len(speaker.keys)
    status, _ = playback(CONTROL_PORT, "play")
    check(status == 200 and speaker.keys[keys:] == ["PLAY"] and speaker.now["status"] == "PLAY_STATE",
          "play presses PLAY once, and answers once the speaker plays", "status %d" % status)

    keys = len(speaker.keys)
    statuses = [playback(CONTROL_PORT, "stop")[0],
                send(CONTROL_PORT, "POST", "/api/playback", "{}", "application/json")[0],
                send(CONTROL_PORT, "POST", "/api/playback", '{"action": ', "application/json")[0],
                send(CONTROL_PORT, "POST", "/api/playback", '{"action": "pause"}', "text/plain")[0],
                power(CONTROL_PORT, "off")[0],
                fetch(CONTROL_PORT, "/api/playback")[0], fetch(CONTROL_PORT, "/api/power")[0]]
    check(statuses == [400, 400, 400, 415, 400, 405, 405] and len(speaker.keys) == keys,
          "anything but play/pause or a true/false power as JSON is refused, pressing nothing",
          "statuses %s" % statuses)

    speaker.set_refusing(True)
    status, _ = playback(CONTROL_PORT, "pause")
    speaker.set_refusing(False)
    check(status == 502 and speaker.now["status"] == "PLAY_STATE", "a key the speaker refuses is reported (502)",
          "status %d" % status)

    speaker.set_ignoring_keys(True)
    started = time.monotonic()
    status, _ = playback(CONTROL_PORT, "pause")
    taken = time.monotonic() - started
    speaker.set_ignoring_keys(False)
    check(status == 504 and PLAYBACK_SETTLE - 0.5 <= taken <= PLAYBACK_SETTLE + 2,
          "a speaker that takes a key but does not pause is reported (504) after ~%g s" % PLAYBACK_SETTLE,
          "status %d after %.1f s" % (status, taken))

    keys = len(speaker.keys)
    status, answer = power(CONTROL_PORT, False)
    check(status == 200 and answer.get("changed") is True and speaker.keys[keys:] == ["POWER"]
          and speaker.now["source"] == "STANDBY" and get(CONTROL_PORT, "/api/live")[1].get("source") == "STANDBY",
          "power off presses POWER once, and answers once the speaker is in standby", "status %d" % status)

    keys = len(speaker.keys)
    status, answer = power(CONTROL_PORT, False)
    check(status == 200 and answer.get("changed") is False and len(speaker.keys) == keys,
          "power off when it is already off presses nothing", "status %d, changed %r" % (status, answer.get("changed")))

    status, _ = playback(CONTROL_PORT, "play")
    check(status == 409 and len(speaker.keys) == keys, "play while it is off is refused (409), pressing nothing",
          "status %d" % status)

    status, answer = power(CONTROL_PORT, True)
    check(status == 200 and answer.get("changed") is True and speaker.now["source"] == "UPNP"
          and speaker.now["status"] == "PLAY_STATE",
          "power on wakes it (on the fake, back to what it was playing)", "status %d" % status)

    keys = len(speaker.keys)
    answers = []
    clickers = [threading.Thread(target=lambda: answers.append(power(CONTROL_PORT, False))) for _ in range(2)]
    for clicker in clickers:
        clicker.start()
    for clicker in clickers:
        clicker.join(15)
    check(sorted(status for status, _ in answers) == [200, 200] and speaker.keys[keys:] == ["POWER"]
          and speaker.now["source"] == "STANDBY",
          "two power-offs at once press POWER once, so they cannot undo each other",
          "keys %s" % speaker.keys[keys:])


def check_deck(speaker):
    def live():
        return get(CONTROL_PORT, "/api/live")[1]

    def playing(station, number):
        state = live()
        return (state.get("station") == station and state.get("station_preset") == number
                and state.get("source") == "UPNP" and state.get("status") == "PLAY_STATE")

    # In standby, as check_transport leaves it: a preset wakes it, as on the remote.
    keys, plays = len(speaker.keys), len(speaker.plays)
    started = time.monotonic()
    status, _ = preset(CONTROL_PORT, 1)
    came = wait_until(lambda: playing("Station One", 1))
    check(status == 200 and came and speaker.keys[keys:] == ["PRESET_1"] and speaker.plays[plays:] == ["http://example.invalid/one"],
          "preset 1 presses PRESET_1, and control plays its station, from standby too",
          "%.1f s to playing, keys %s" % (time.monotonic() - started, speaker.keys[keys:]))

    keys = len(speaker.keys)
    first, _ = preset(CONTROL_PORT, 1)
    second, _ = preset(CONTROL_PORT, 3)
    came = wait_until(lambda: playing("Station Thirteen", 13))
    check(first == 200 and second == 200 and came and speaker.keys[keys:] == ["PRESET_1", "PRESET_3"],
          "1 then 3, pressed quickly, make the combo for preset 13, as on the remote",
          "keys %s, station %r" % (speaker.keys[keys:], live().get("station")))

    keys = len(speaker.keys)
    statuses = [preset(CONTROL_PORT, 1)[0] for _ in range(3)]
    came = wait_until(lambda: playing("Station 111", 111))
    check(statuses == [200] * 3 and came and speaker.keys[keys:] == ["PRESET_1"] * 3,
          "1, 1, 1, pressed quickly, make the three-digit combo for preset 111",
          "keys %s, station %r" % (speaker.keys[keys:], live().get("station")))

    keys = len(speaker.keys)
    statuses = [preset(CONTROL_PORT, 1)[0] for _ in range(2)]
    pressed = time.monotonic()
    came = wait_until(lambda: playing("Station Eleven", 11))
    waited = time.monotonic() - pressed
    check(statuses == [200] * 2 and came and speaker.keys[keys:] == ["PRESET_1"] * 2 and waited >= COMBO_WINDOW - 0.1,
          "1, 1 waits out the combo window for a third digit (preset 111 could follow), then plays preset 11",
          "%.2f s after the second press, window %.1f s" % (waited, COMBO_WINDOW))

    preset(CONTROL_PORT, 2)
    check(wait_until(lambda: playing("Station Two", 2)), "preset 2 plays its station", "station %r" % live().get("station"))

    status, config, _ = get(CONTROL_PORT, "/api/config")
    check(status == 200 and config.get("combo_window_ms") == TEST_STREAMS["combo_window_ms"],
          "/api/config gives control's combo window, for the page's keypad", "%r ms" % config.get("combo_window_ms"))

    for number, station, buttons in ((13, "Station Thirteen", ["PRESET_1", "PRESET_3"]),
                                     (111, "Station 111", ["PRESET_1"] * 3)):
        keys = len(speaker.keys)
        status, answer = select(CONTROL_PORT, number)
        came = wait_until(lambda: playing(station, number))
        check(status == 200 and answer.get("station") == station and speaker.keys[keys:] == buttons and came,
              "/api/select %d presses %s within the combo window, and control plays %s" % (number, ", ".join(buttons), station),
              "status %d, keys %s" % (status, speaker.keys[keys:]))

    keys = len(speaker.keys)
    statuses = [select(CONTROL_PORT, 17), select(CONTROL_PORT, 34), select(CONTROL_PORT, 0), select(CONTROL_PORT, 667),
                select(CONTROL_PORT, "13"), send(CONTROL_PORT, "POST", "/api/select", '{"preset": 13}', "text/plain")]
    codes = [status for status, _ in statuses]
    check(codes == [400, 404, 400, 400, 400, 415] and len(speaker.keys) == keys
          and "1 to 6" in statuses[0][1].get("error", "") and "nothing on preset 34" in statuses[1][1].get("error", "")
          and fetch(CONTROL_PORT, "/api/select")[0] == 405,
          "/api/select refuses a number not made of buttons 1 to 6 (400) or with no station (404), pressing nothing",
          "statuses %s" % codes)

    status, _ = select(CONTROL_PORT, 2)
    check(status == 200 and wait_until(lambda: playing("Station Two", 2)), "/api/select 2 plays preset 2",
          "station %r" % live().get("station"))

    for direction, key in (("next", "NEXT_TRACK"), ("previous", "PREV_TRACK")):
        keys, plays = len(speaker.keys), len(speaker.plays)
        status, _ = skip(CONTROL_PORT, direction)
        came = wait_until(lambda: len(speaker.plays) > plays and speaker.now["status"] == "PLAY_STATE")
        check(status == 200 and speaker.keys[keys:] == [key] and came and speaker.plays[plays:] == ["http://example.invalid/two"],
              "%s presses %s; the speaker cannot skip the stream, so control starts it again (no relay here)" % (direction, key),
              "keys %s, plays %s" % (speaker.keys[keys:], speaker.plays[plays:]))

    for which, name in (("bluetooth", "BLUETOOTH"), ("aux", "AUX")):
        status, _ = source(CONTROL_PORT, which)
        state = live()
        check(status == 200 and speaker.now["source"] == name and state.get("source") == name
              and state.get("station_preset") is None and state.get("want_playing") is False,
              "%s switches the speaker to %s, answering once it is there; control keeps out of its way" % (which, name),
              "status %d, source %r" % (status, state.get("source")))

    keys = len(speaker.keys)
    preset(CONTROL_PORT, 2)
    check(wait_until(lambda: playing("Station Two", 2)) and speaker.keys[keys:] == ["PRESET_2"],
          "a preset brings the radio back from AUX, as on the remote", "station %r" % live().get("station"))

    keys = len(speaker.keys)
    statuses = [preset(CONTROL_PORT, 0)[0], preset(CONTROL_PORT, 7)[0], preset(CONTROL_PORT, "1")[0],
                skip(CONTROL_PORT, "prev")[0], source(CONTROL_PORT, "upnp")[0],
                send(CONTROL_PORT, "POST", "/api/preset", '{"preset": 1}', "text/plain")[0],
                fetch(CONTROL_PORT, "/api/preset")[0], fetch(CONTROL_PORT, "/api/skip")[0], fetch(CONTROL_PORT, "/api/source")[0]]
    check(statuses == [400, 400, 400, 400, 400, 415, 405, 405, 405] and len(speaker.keys) == keys,
          "a preset outside 1..6, an unknown direction or source, or anything but JSON is refused, pressing nothing",
          "statuses %s" % statuses)

    speaker.set_refusing(True)
    status, _ = source(CONTROL_PORT, "aux")
    speaker.set_refusing(False)
    check(status == 502 and speaker.now["source"] == "UPNP", "a source the speaker refuses is reported (502)", "status %d" % status)

    power(CONTROL_PORT, False)
    keys = len(speaker.keys)
    status, _ = skip(CONTROL_PORT, "next")
    check(status == 409 and len(speaker.keys) == keys, "skipping while it is off is refused (409), pressing nothing",
          "status %d" % status)


def check_streams(speaker, data_dir):
    """Playing streams by name, and saving an edited stream list, which control uses at once."""
    def live():
        return get(CONTROL_PORT, "/api/live")[1]

    def on(name):
        state = live()
        return state.get("station_name") == name and state.get("source") == "UPNP" and state.get("status") == "PLAY_STATE"

    def file_bytes(name="streams.json"):
        with open(os.path.join(data_dir, name), "rb") as data:
            return data.read()

    listing, tag = streams_with_tag(CONTROL_PORT)
    check(len(listing) == len(TEST_STREAMS["streams"]) and (tag or "").startswith('"'),
          "/api/streams comes with an ETag, the version of streams.json it was read from", "ETag %s" % tag)

    # In standby, as check_deck leaves it.
    keys, plays = len(speaker.keys), len(speaker.plays)
    status, answer = play_stream(CONTROL_PORT, "free")
    state = live()
    check(status == 200 and answer.get("station") == "Station Free" and speaker.plays[plays:] == ["http://example.invalid/free"]
          and len(speaker.keys) == keys and on("free") and state.get("station") == "Station Free"
          and state.get("station_preset") is None,
          "/api/play plays a stream that is on no preset, from standby too, answering once it plays; no key is pressed",
          "status %d, plays %s, station %r" % (status, speaker.plays[plays:], state.get("station")))

    # Noted once the play is done with, a moment after the speaker reports playing.
    noted = wait_until(lambda: live().get("last_stream") == "free", timeout=3)
    with open(os.path.join(data_dir, "state.json")) as saved:
        remembered = json.load(saved)
    check(noted and remembered == {"last_preset": 0, "last_stream": "free"}
          and get(CONTROL_PORT, "/api/state")[1].get("station") == "Station Free",
          "what played is remembered by its name, to be brought back, preset or not", "state.json %r" % remembered)

    keys, plays = len(speaker.keys), len(speaker.plays)
    started = time.monotonic()
    status, _ = play_stream(CONTROL_PORT, "thirteen")
    taken = time.monotonic() - started
    check(status == 200 and on("thirteen") and live().get("station_preset") == 13 and len(speaker.keys) == keys
          and speaker.plays[plays:] == ["http://example.invalid/thirteen"] and taken < COMBO_WINDOW,
          "/api/play plays a preset's stream by name too, straight away: no buttons, no combo window",
          "%.2f s, keys %s" % (taken, speaker.keys[keys:]))

    keys, plays = len(speaker.keys), len(speaker.plays)
    statuses = [play_stream(CONTROL_PORT, "nowhere")[0], play_stream(CONTROL_PORT, "two words")[0],
                send(CONTROL_PORT, "POST", "/api/play", '{"stream": "free"}', "text/plain")[0],
                send(CONTROL_PORT, "POST", "/api/play", "{}", "application/json")[0], fetch(CONTROL_PORT, "/api/play")[0]]
    check(statuses == [404, 400, 415, 400, 405] and len(speaker.keys) == keys and len(speaker.plays) == plays,
          "/api/play refuses a name no stream has (404), one no stream could have, or anything but JSON, playing nothing",
          "statuses %s" % statuses)

    # Saves that are refused leave the file, and its ETag, as they were.
    before = file_bytes()
    original = [dict(item) for item in listing]
    bad = [dict(original[0], preset=7), dict(original[1], name="one"), dict(original[2], url="ftp://example.invalid/x")]
    too_large = '{"streams": [%s]}' % ",".join('{"name": "s%d", "url": "http://example.invalid/%s"}' % (i, "x" * 300)
                                              for i in range(250))
    refused = [put_streams(CONTROL_PORT, original, None), put_streams(CONTROL_PORT, original, '"0000000000000000-1"'),
               put_streams(CONTROL_PORT, original, tag, content_type="text/plain"), put_streams(CONTROL_PORT, bad, tag),
               put_streams(CONTROL_PORT, None, tag, raw=too_large)]
    statuses = [status for status, _, _ in refused]
    problems = sorted((problem.get("index"), problem.get("field")) for problem in refused[3][1].get("problems", []))
    check(statuses == [428, 412, 415, 400, 413] and refused[1][2] == tag and file_bytes() == before
          and streams_with_tag(CONTROL_PORT)[1] == tag,
          "a save is refused without If-Match (428), against a list changed since (412, with the ETag now), as anything but "
          "JSON (415), breaking the rules (400) or too large (413); nothing is written", "statuses %s" % statuses)
    check(problems == [(0, "name"), (0, "preset"), (1, "name"), (2, "url")],
          "a refused list says what is wrong with which stream: a preset not made of buttons, a name used twice, a URL "
          "that is not http", "problems %s" % problems)

    # A save while "free" plays: preset 2 moves from "two" to "free", "eleven" is renamed, "thirteen" goes and
    # "added" comes, its display name left for the server to fill in.
    play_stream(CONTROL_PORT, "free")
    edited = [dict(item) for item in original if item["name"] != "thirteen"]
    for item in edited:
        if item["name"] == "two":
            item["preset"] = None
        if item["name"] == "free":
            item["preset"] = 2
        if item["name"] == "eleven":
            item["display_name"] = "Station 11"
    edited.append({"name": "added", "display_name": "", "description": "", "url": " http://example.invalid/added ", "preset": None})
    status, answer, new_tag = put_streams(CONTROL_PORT, edited, tag)
    saved, listed_tag = streams_with_tag(CONTROL_PORT)
    on_disk = json.loads(file_bytes())
    check(status == 200 and answer.get("changed") is True and answer.get("applied") is True and new_tag == listed_tag
          and new_tag != tag and answer.get("streams") == saved and list(on_disk) == ["combo_window_ms", "streams"]
          and on_disk["combo_window_ms"] == 700
          and [item["name"] for item in on_disk["streams"]] == ["one", "two", "eleven", "one-eleven", "free", "added"]
          and on_disk["streams"][-1] == {"name": "added", "display_name": "added", "url": "http://example.invalid/added"}
          and file_bytes("streams.json.bak") == before,
          "PUT /api/streams saves the whole list, keeping combo_window_ms, and the file it replaced as streams.json.bak",
          "status %d, streams %s" % (status, [item["name"] for item in on_disk.get("streams", [])]))

    state = live()
    check(on("free") and state.get("station_preset") == 2,
          "the stream playing shows its new preset at once, without being played again", "preset %r" % state.get("station_preset"))

    keys, plays = len(speaker.keys), len(speaker.plays)
    preset(CONTROL_PORT, 2)
    came = wait_until(lambda: len(speaker.plays) > plays and on("free"))
    check(came and speaker.keys[keys:] == ["PRESET_2"] and speaker.plays[plays:] == ["http://example.invalid/free"],
          "control uses the saved list at once: preset 2 plays the stream moved onto it", "plays %s" % speaker.plays[plays:])

    status, _ = play_stream(CONTROL_PORT, "added")
    check(status == 200 and on("added") and live().get("station") == "added",
          "a stream added by the save plays at once", "status %d, station %r" % (status, live().get("station")))

    keys = len(speaker.keys)
    status, _ = select(CONTROL_PORT, 13)
    check(status == 404 and len(speaker.keys) == keys, "a preset the save took away has nothing on it", "status %d" % status)

    status, answer, same_tag = put_streams(CONTROL_PORT, saved, new_tag)
    check(status == 200 and answer.get("changed") is False and same_tag == new_tag and file_bytes("streams.json.bak") == before,
          "saving the list as it is writes nothing", "status %d, changed %r" % (status, answer.get("changed")))

    # Back to the check's own stations, and to Station Two in standby, as the standalone checks expect.
    status, _, _ = put_streams(CONTROL_PORT, TEST_STREAMS["streams"], same_tag)
    play_stream(CONTROL_PORT, "two")
    power(CONTROL_PORT, False)
    check(status == 200 and [item["name"] for item in streams_with_tag(CONTROL_PORT)[0]]
          == [item["name"] for item in TEST_STREAMS["streams"]] and speaker.now["source"] == "STANDBY",
          "the original list saves back", "status %d" % status)


def check_speakers_control():
    status, answer, _ = get(CONTROL_PORT, "/api/speakers")
    mine = speaker_entry(answer, FAKE_DEVICE_ID)
    check(status == 200 and answer.get("embedded") is True and answer.get("active_ip") == SPEAKER_HOST
          and answer.get("default_id") == FAKE_DEVICE_ID and mine.get("default") is True and mine.get("active") is True
          and mine.get("online") is None and answer.get("discovery", {}).get("active") is False
          and answer.get("groups") == [],
          "control's /api/speakers names the speaker it drives, and nothing is searched for until asked",
          "status %d, %s" % (status, answer))

    answer = wait_found(CONTROL_PORT, both_online)
    kitchen = speaker_entry(answer, KITCHEN_ID)
    check(both_online(answer) and kitchen.get("saved") is False and kitchen.get("name") == "Kitchen"
          and kitchen.get("type") == "SoundTouch 30" and answer.get("discovery", {}).get("active") is True,
          "searching finds both speakers, the new one with its name and model from the speaker itself",
          "%s" % answer.get("speakers"))

    status, answer = make_default(CONTROL_PORT, KITCHEN_ID)
    check(status == 200 and answer.get("changed") is True and answer.get("restart_needed") is True
          and answer.get("default_id") == KITCHEN_ID,
          "inside control, a new default is saved and taken up when control next starts",
          "status %d, restart_needed %r" % (status, answer.get("restart_needed")))
    status, answer = make_default(CONTROL_PORT, FAKE_DEVICE_ID)
    check(status == 200 and answer.get("restart_needed") is False,
          "choosing the speaker control drives again needs no restart", "status %d" % status)


def check_speakers_standalone(speaker, kitchen, data_dir):
    status, page, _ = fetch(WEB_PORT, "/speakers")
    check(status == 200 and "/api/speakers/discover" in page and 'href="/"' in page and "Make default" in page,
          "/speakers serves the Speakers page, which searches while open and links back to the dashboard",
          "status %d" % status)
    status, page, _ = fetch(WEB_PORT, "/")
    check(status == 200 and 'href="/speakers"' in page and 'id="first-run"' in page,
          "the dashboard links to the Speakers page", "status %d" % status)

    status, answer, _ = get(WEB_PORT, "/api/speakers")
    check(status == 200 and answer.get("embedded") is False and answer.get("active_ip") == ""
          and answer.get("default_id") == FAKE_DEVICE_ID and speaker_entry(answer, KITCHEN_ID).get("saved") is True,
          "standalone /api/speakers lists the saved speakers and drives none of them itself",
          "status %d" % status)

    status, _ = send(WEB_PORT, "POST", "/api/speakers/discover", "{}", "text/plain")
    check(status == 415, "a search asked for without JSON is refused", "status %d" % status)
    status, _, _ = fetch(WEB_PORT, "/api/speakers/discover")
    check(status == 405, "a search is asked for with POST only", "status %d" % status)

    answer = wait_found(WEB_PORT, both_online)
    check(both_online(answer), "standalone searching finds both speakers", "%s" % answer.get("speakers"))

    devices_path = os.path.join(data_dir, "devices.json")
    with open(devices_path) as existing:
        devices = json.load(existing)
    devices["groups"] = [{"name": "Downstairs", "members": [FAKE_DEVICE_ID, KITCHEN_ID]}]
    with open(devices_path, "w") as out:
        json.dump(devices, out, indent=2)

    status, answer = make_default(WEB_PORT, KITCHEN_ID)
    with open(devices_path) as saved:
        devices = json.load(saved)
    check(status == 200 and answer.get("restart_needed") is False and devices.get("default_device") == KITCHEN_ID
          and devices.get("groups", [{}])[0].get("name") == "Downstairs",
          "standalone saves a new default, keeping what else devices.json holds",
          "status %d, %s" % (status, devices))

    before = speaker.volume
    status, _ = set_volume(WEB_PORT, 27)
    check(status == 200 and kitchen.volume == 27 and speaker.volume == before,
          "standalone takes the new default up at once: the volume goes to the kitchen",
          "kitchen %d, first %d" % (kitchen.volume, speaker.volume))

    status, answer = make_default(WEB_PORT, "NOSUCHSPEAKER")
    check(status == 404, "a default that is no speaker known is refused", "status %d" % status)
    status, _ = send(WEB_PORT, "PUT", "/api/speakers/default", "{}", "application/json")
    check(status == 400, "a default with no id is refused", "status %d" % status)
    status, _ = send(WEB_PORT, "PUT", "/api/speakers/default", json.dumps({"id": KITCHEN_ID}), "text/plain")
    check(status == 415, "a default given without JSON is refused", "status %d" % status)
    status, _ = make_default(WEB_PORT, FAKE_DEVICE_ID)
    check(status == 200, "and the first speaker can be the default again", "status %d" % status)

    kitchen.set_announcing(False)
    answer = wait_found(WEB_PORT, lambda found: speaker_entry(found, KITCHEN_ID).get("online") is False,
                        ONLINE_WINDOW + 8)
    gone = speaker_entry(answer, KITCHEN_ID)
    check(gone.get("online") is False and (gone.get("last_seen_ms") or 0) >= ONLINE_WINDOW * 1000
          and speaker_entry(answer, FAKE_DEVICE_ID).get("online") is True,
          "a speaker that stops answering is shown offline, with when it was last seen", "%s" % gone)
    kitchen.set_announcing(True)

    stopped = wait_until(lambda: get(WEB_PORT, "/api/speakers")[1].get("discovery", {}).get("active") is False, 20)
    check(stopped, "the search stops by itself once the page stops asking for it")


def check_first_run(data_dir, processes):
    """A dashboard with no speaker saved: it says so, and choosing one creates devices.json."""
    empty = os.path.join(data_dir, "first-run")
    os.makedirs(empty)
    web = launch(["--data-dir", empty, "web", "--web-port", str(FIRST_RUN_PORT), "--web-bind", "127.0.0.1"],
                 FIRST_RUN_PORT, os.path.join(data_dir, "first-run.log"))
    processes.append(web)

    status, answer, _ = get(FIRST_RUN_PORT, "/api/speakers")
    check(status == 200 and answer.get("has_default") is False and answer.get("speakers") == [],
          "with no speaker saved, /api/speakers says there is no default, for the dashboard to offer a search",
          "status %d, %s" % (status, answer))

    answer = wait_found(FIRST_RUN_PORT, both_online)
    status, answer = make_default(FIRST_RUN_PORT, KITCHEN_ID)
    devices_path = os.path.join(empty, "devices.json")
    devices = {}
    if os.path.exists(devices_path):
        with open(devices_path) as saved:
            devices = json.load(saved)
    check(status == 200 and answer.get("has_default") is True and devices.get("default_device") == KITCHEN_ID
          and devices.get("devices", [{}])[0].get("ip_address") == KITCHEN_HOST,
          "choosing a speaker found creates devices.json with it as the default",
          "status %d, %s" % (status, devices))

    web.terminate()
    web.wait(10)


def check_control(speaker, control, data_dir):
    status, live, _ = get(CONTROL_PORT, "/api/live")
    check(status == 200 and live.get("volume") == 15 and live.get("muted") is False,
          "control reads the speaker's volume at start", "volume %r" % live.get("volume"))
    check(live.get("source") == "STANDBY" and live.get("status") == "",
          "control reads what the speaker is doing at start", "source %r" % live.get("source"))

    status, first, taken = get(CONTROL_PORT, "/api/live/wait")
    seq = first.get("seq")
    check(status == 200 and isinstance(seq, int) and taken < PROMPT,
          "/api/live/wait with no since answers at once", "%.0f ms, seq %r" % (taken * 1000, seq))
    if not isinstance(seq, int):
        return

    status, held, taken = get(CONTROL_PORT, "/api/live/wait?since=%d" % seq)
    check(status == 200 and held.get("seq") == seq and HOLD - 0.5 <= taken <= HOLD + 1.5,
          "with nothing changing it holds ~%g s, then answers unchanged" % HOLD, "%.2f s" % taken)

    waiter = wait_held(CONTROL_PORT, seq)
    changed = time.monotonic()
    speaker.set_volume(23)
    waiter.join(HOLD + 5)
    latency = (waiter.answered or changed) - changed
    check(waiter.status == 200 and waiter.data.get("volume") == 23 and waiter.data.get("seq", 0) > seq
          and 0 <= latency < PROMPT,
          "a volume change on the speaker wakes the waiting request at once",
          "%.1f ms after the speaker sent it" % (latency * 1000))
    seq = waiter.data.get("seq", seq)

    waiter = wait_held(CONTROL_PORT, seq)
    changed = time.monotonic()
    speaker.set_muted(True)
    waiter.join(HOLD + 5)
    latency = (waiter.answered or changed) - changed
    check(waiter.status == 200 and waiter.data.get("muted") is True and 0 <= latency < PROMPT,
          "muting reaches it at once too", "%.1f ms" % (latency * 1000))
    speaker.set_muted(False)

    seq = get(CONTROL_PORT, "/api/live/wait")[1].get("seq", 0)
    waiters = [Waiter(CONTROL_PORT, "/api/live/wait?since=%d" % seq) for _ in range(4)]
    for waiter in waiters:
        waiter.start()
    time.sleep(0.5)
    early = [waiter for waiter in waiters if waiter.answered is not None]
    check(len(early) == 1 and early[0].status == 503,
          "a fourth request waiting at once is told 503; three are held",
          "%d answered early, %s" % (len(early), [waiter.status for waiter in early]))
    changed = time.monotonic()
    speaker.set_volume(15)
    for waiter in waiters:
        waiter.join(HOLD + 5)
    woken = [waiter for waiter in waiters
             if waiter.status == 200 and waiter.data.get("volume") == 15 and 0 <= waiter.answered - changed < PROMPT]
    check(len(woken) == 3, "the next change wakes all three held requests at once", "%d woken" % len(woken))

    check_setting_volume(speaker)
    check_transport(speaker)
    check_deck(speaker)
    check_streams(speaker, data_dir)
    check_speakers_control()

    status, page, _ = fetch(CONTROL_PORT, "/")
    check(status == 200 and "/api/live/wait" in page and "setInterval(loadNowPlaying" in page,
          "the dashboard follows /api/live/wait, and polls only where it is absent")
    check('id="st-edit"' in page and '"/api/play"' in page and 'method: "PUT"' in page,
          "the dashboard has a play button for each stream, and an editor that saves the list")

    seq = get(CONTROL_PORT, "/api/live/wait")[1].get("seq", 0)
    waiter = wait_held(CONTROL_PORT, seq)
    stopping = time.monotonic()
    control.send_signal(signal.SIGTERM)
    try:
        code = control.wait(10)
    except subprocess.TimeoutExpired:
        code = None
    taken = time.monotonic() - stopping
    waiter.join(5)
    check(code == 0 and taken < 1.5,
          "control stops promptly with a request held, rather than after the %g s hold" % HOLD,
          "%.2f s, exit %r" % (taken, code))


def check_standalone(speaker, kitchen, data_dir):
    status, _, _ = get(WEB_PORT, "/api/live/wait")
    check(status == 404, "the standalone web command has no /api/live/wait, so the page polls",
          "status %d" % status)

    speaker.set_volume(31)
    status, now, _ = get(WEB_PORT, "/api/nowplaying")
    check(status == 200 and now.get("volume") == 31 and now.get("muted") is False and now.get("source") == "STANDBY",
          "standalone /api/nowplaying carries the volume, read from the speaker",
          "volume %r, source %r" % (now.get("volume"), now.get("source")))

    status, _ = set_volume(WEB_PORT, 12)
    _, now, _ = get(WEB_PORT, "/api/nowplaying")
    check(status == 200 and speaker.volume == 12 and now.get("volume") == 12,
          "standalone POST /api/volume sets it, and the next look shows it rather than a cached one",
          "status %d, speaker at %d, shown %r" % (status, speaker.volume, now.get("volume")))

    status, answer = power(WEB_PORT, True)
    _, now, _ = get(WEB_PORT, "/api/nowplaying")
    check(status == 200 and answer.get("changed") is True and now.get("source") == "UPNP",
          "standalone power on works, watching the speaker for the change", "status %d, source %r" % (status, now.get("source")))

    status, _ = playback(WEB_PORT, "pause")
    _, now, _ = get(WEB_PORT, "/api/nowplaying")
    check(status == 200 and now.get("status") == "PAUSE_STATE",
          "standalone pause works, and the next look shows it", "status %d, shown %r" % (status, now.get("status")))
    check(now.get("station") == "Station Two" and now.get("station_preset") == 2,
          "standalone /api/nowplaying names the station playing and its preset", "station %r, preset %r"
          % (now.get("station"), now.get("station_preset")))

    status, _ = source(WEB_PORT, "aux")
    check(status == 200 and speaker.now["source"] == "AUX", "standalone source switching works, watching the speaker for it",
          "status %d, source %r" % (status, speaker.now["source"]))

    keys = len(speaker.keys)
    status, _ = preset(WEB_PORT, 1)
    check(status == 200 and speaker.keys[keys:] == ["PRESET_1"],
          "standalone preset presses PRESET_1 (playing it is control's part, wherever control runs)",
          "status %d, keys %s" % (status, speaker.keys[keys:]))

    keys = len(speaker.keys)
    status, answer = select(WEB_PORT, 13)
    check(status == 200 and answer.get("station") == "Station Thirteen" and speaker.keys[keys:] == ["PRESET_1", "PRESET_3"],
          "standalone /api/select presses the buttons of preset 13", "status %d, keys %s" % (status, speaker.keys[keys:]))

    keys, plays = len(speaker.keys), len(speaker.plays)
    status, _ = play_stream(WEB_PORT, "free")
    _, now, _ = get(WEB_PORT, "/api/nowplaying")
    check(status == 200 and speaker.plays[plays:] == ["http://example.invalid/free"] and len(speaker.keys) == keys
          and now.get("station_name") == "free" and now.get("station") == "Station Free",
          "standalone /api/play hands a stream on no preset to the speaker itself, as `cxstcc play` does",
          "status %d, plays %s" % (status, speaker.plays[plays:]))

    keys = len(speaker.keys)
    status, answer = play_stream(WEB_PORT, "thirteen")
    check(status == 200 and answer.get("preset") == 13 and speaker.keys[keys:] == ["PRESET_1", "PRESET_3"],
          "standalone /api/play of a preset's stream presses its buttons, for control to play wherever it runs",
          "status %d, keys %s" % (status, speaker.keys[keys:]))

    listing, tag = streams_with_tag(WEB_PORT)
    listing[0]["description"] = "Saved standalone"
    status, answer, _ = put_streams(WEB_PORT, listing, tag)
    check(status == 200 and answer.get("changed") is True and answer.get("applied") is False
          and streams_with_tag(WEB_PORT)[0][0].get("description") == "Saved standalone",
          "standalone saves the list too, saying no control here has taken it up", "status %d" % status)

    check_speakers_standalone(speaker, kitchen, data_dir)


def main():
    if not os.access(BINARY, os.X_OK):
        sys.exit("Build it first: make")

    data_dir = tempfile.mkdtemp(prefix="cxstcc-e2e-")
    logs = {"control": os.path.join(data_dir, "control.log"), "web": os.path.join(data_dir, "web.log"),
            "first-run": os.path.join(data_dir, "first-run.log")}
    verbose = os.environ.get("E2E_VERBOSE") == "1"
    speaker = FakeSpeaker(host=SPEAKER_HOST, volume=15, ssdp_port=SSDP_PORT,
                          log=(lambda line: print(line, flush=True)) if verbose else None)
    kitchen = FakeSpeaker(host=KITCHEN_HOST, device_id=KITCHEN_ID, name="Kitchen", volume=20, ssdp_port=SSDP_PORT)
    processes = []

    try:
        with open(os.path.join(data_dir, "streams.json"), "w") as out:
            json.dump(TEST_STREAMS, out, indent=2)
        write_data_dir(data_dir, speaker)
        speaker.start()
        kitchen.start()

        control = launch(["--data-dir", data_dir, "control", "--no-proxy", "--no-resume",
                          "--web", "--web-port", str(CONTROL_PORT), "--web-bind", "127.0.0.1"],
                         CONTROL_PORT, logs["control"])
        processes.append(control)
        check_control(speaker, control, data_dir)

        web = launch(["--data-dir", data_dir, "web", "--web-port", str(WEB_PORT), "--web-bind", "127.0.0.1"],
                     WEB_PORT, logs["web"])
        processes.append(web)
        check_standalone(speaker, kitchen, data_dir)
        check_first_run(data_dir, processes)
    except (OSError, RuntimeError) as error:
        check(False, "setting up", str(error))
    finally:
        for process in processes:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(10)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
        speaker.stop()
        kitchen.stop()
        if not all(results):
            for name, path in logs.items():
                if os.path.exists(path):
                    with open(path) as log:
                        print("\n--- %s log (last 40 lines) ---\n%s" % (name, "".join(log.readlines()[-40:])))
        shutil.rmtree(data_dir, ignore_errors=True)

    print("\n%d/%d end-to-end checks passed" % (sum(results), len(results)))
    return 0 if results and all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
