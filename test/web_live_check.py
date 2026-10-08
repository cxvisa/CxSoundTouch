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
                 stopping does not sit out a hold
  web            (standalone) has no /api/live/wait, so the page polls; /api/nowplaying still
                 carries the volume, read from the speaker; setting the volume, play/pause and power
                 work there too

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

from fake_speaker import REPO, FakeSpeaker, write_data_dir  # noqa: E402

BINARY = os.path.join(REPO, "cxstcc")
CONTROL_PORT = int(os.environ.get("E2E_CONTROL_PORT", "8094"))
WEB_PORT = int(os.environ.get("E2E_WEB_PORT", "8095"))
SPEAKER_HOST = os.environ.get("E2E_SPEAKER_HOST", "127.0.0.2")
HOLD = 3.0          # WebServer::LIVE_HOLD
PLAYBACK_SETTLE = 5.0   # WebServer::PLAYBACK_SETTLE
PROMPT = 0.5        # a generous bound for "at once"; in practice a few milliseconds
ELSEWHERE = "http://example.invalid/radio"      # a stream that is not control's, so control lets it be

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


def playback(port, action):
    return send(port, "POST", "/api/playback", json.dumps({"action": action}), "application/json")


def power(port, on):
    return send(port, "POST", "/api/power", json.dumps({"on": on}), "application/json")


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
    """Starts cxstcc and waits for its dashboard to answer."""
    log = open(log_path, "w")
    process = subprocess.Popen([BINARY] + arguments, stdout=log, stderr=subprocess.STDOUT)
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


def check_control(speaker, control):
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

    status, page, _ = fetch(CONTROL_PORT, "/")
    check(status == 200 and "/api/live/wait" in page and "setInterval(loadNowPlaying" in page,
          "the dashboard follows /api/live/wait, and polls only where it is absent")

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


def check_standalone(speaker):
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


def main():
    if not os.access(BINARY, os.X_OK):
        sys.exit("Build it first: make")

    data_dir = tempfile.mkdtemp(prefix="cxstcc-e2e-")
    logs = {"control": os.path.join(data_dir, "control.log"), "web": os.path.join(data_dir, "web.log")}
    verbose = os.environ.get("E2E_VERBOSE") == "1"
    speaker = FakeSpeaker(host=SPEAKER_HOST, volume=15, log=(lambda line: print(line, flush=True)) if verbose else None)
    processes = []

    try:
        write_data_dir(data_dir, speaker)
        speaker.start()

        control = launch(["--data-dir", data_dir, "control", "--no-proxy", "--no-resume",
                          "--web", "--web-port", str(CONTROL_PORT), "--web-bind", "127.0.0.1"],
                         CONTROL_PORT, logs["control"])
        processes.append(control)
        check_control(speaker, control)

        web = launch(["--data-dir", data_dir, "web", "--web-port", str(WEB_PORT), "--web-bind", "127.0.0.1"],
                     WEB_PORT, logs["web"])
        processes.append(web)
        check_standalone(speaker)
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
