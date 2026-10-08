#!/usr/bin/env python3
"""A fake Bose SoundTouch speaker, for testing cxstcc without a real one.

Python 3 standard library only. It answers on the ports cxstcc uses, on a loopback address of its
own (127.0.0.2 by default, so nothing already listening on 127.0.0.1 is in the way):

  8080  the "gabbo" WebSocket event stream: a SoundTouchSdkInfo hello, then <updates> events
  8090  the Web API: GET /info, /nowPlaying, /volume, /presets; POST /volume, /key

POST /volume does what the real speaker does: it sets the level and pushes a volumeUpdated event to
every connected listener, so the same curl works against either. POST /key takes the remote's keys as
the API sends them (a press, then a release) and acts on the press: PLAY, PAUSE and PLAY_PAUSE change
the play status of whatever is "playing", and POWER goes to standby and back to what it was doing
(the real speaker's waking up may differ); other keys are only noted. Every pressed key is listed in
/fake/state. The XML follows what a SoundTouch 30 actually sends. Nothing else is emulated: UPnP
playback (port 8091) and /storePreset are absent, so a play fails. A test sets what is "playing"
through the hooks below instead.

Test hooks, on port 8090:
  POST /fake/event       body: raw XML, pushed as-is to every listener (any event at all)
  POST /fake/mute        body: true or false; sets mute and pushes volumeUpdated
  POST /fake/refuse      body: true or false; while true, POST /volume and /key are refused with <errors>
  POST /fake/ignore-keys body: true or false; while true, keys are taken but do nothing
  POST /fake/nowplaying  body: JSON with any of source, status, location, track, artist, album;
                         sets what /nowPlaying reports and pushes nowPlayingUpdated
  GET  /fake/state       the fake's state, as JSON

By hand:
  python3 test/fake_speaker.py --data-dir /tmp/fake-st
  ./cxstcc --data-dir /tmp/fake-st control --no-proxy --web --web-port 8094
  curl -X POST -d '<volume>25</volume>' http://127.0.0.2:8090/volume

--data-dir writes a devices.json there that names the fake as the default speaker, and copies the
repository's streams.json unless one is there already. It refuses a directory whose devices.json
names a real speaker. Scripts import FakeSpeaker instead: see test/web_live_check.py.
"""

import argparse
import asyncio
import base64
import hashlib
import json
import os
import re
import shutil
import signal
import struct
import sys
import threading
from xml.sax.saxutils import escape, quoteattr

WS_PORT = 8080
REST_PORT = 8090
WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
XML_DECLARATION = '<?xml version="1.0" encoding="UTF-8" ?>'
XML_TYPE = "text/xml; charset=utf-8"
REASONS = {200: "OK", 400: "Bad Request", 404: "Not Found"}
NOW_PLAYING_FIELDS = ("source", "status", "location", "track", "artist", "album")
REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FAKE_DEVICE_ID = "FAKESPEAKER0"


def _parse_head(head):
    """The request line and the headers (names lower-cased) of an HTTP request head."""
    lines = head.decode("latin-1").split("\r\n")
    headers = {}
    for line in lines[1:]:
        if ":" in line:
            name, value = line.split(":", 1)
            headers[name.strip().lower()] = value.strip()
    return lines[0], headers


def _frame(opcode, payload):
    """One unfragmented WebSocket frame, unmasked as a server sends it."""
    length = len(payload)
    if length < 126:
        header = struct.pack("!BB", 0x80 | opcode, length)
    elif length < 65536:
        header = struct.pack("!BBH", 0x80 | opcode, 126, length)
    else:
        header = struct.pack("!BBQ", 0x80 | opcode, 127, length)
    return header + payload


async def _read_frame(reader):
    """The opcode and payload of the next frame from a client, unmasked."""
    first, second = await reader.readexactly(2)
    length = second & 0x7F
    if length == 126:
        length = struct.unpack("!H", await reader.readexactly(2))[0]
    elif length == 127:
        length = struct.unpack("!Q", await reader.readexactly(8))[0]
    mask = await reader.readexactly(4) if second & 0x80 else None
    payload = await reader.readexactly(length)
    if mask is not None:
        payload = bytes(byte ^ mask[i % 4] for i, byte in enumerate(payload))
    return first & 0x0F, payload


class FakeSpeaker:
    """The fake, serving on its own asyncio loop in a background thread. The set_* methods and push
    are safe from any thread, and return once the change has been sent to every listener."""

    def __init__(self, host="127.0.0.2", device_id=FAKE_DEVICE_ID, name="Fake SoundTouch",
                 volume=15, muted=False, log=None):
        self.host = host
        self.device_id = device_id
        self.name = name
        self.volume = volume
        self.muted = muted
        self.now = dict.fromkeys(NOW_PLAYING_FIELDS, "")
        self.now["source"] = "STANDBY"
        self.refusing = False           # while True, POST /volume and /key are refused
        self.ignoring_keys = False      # while True, keys are taken but do nothing
        self.keys = []                  # every key pressed, in order
        self._before_standby = None     # what it was doing when POWER put it in standby
        self.log = log                  # takes one line; None for silence
        self._listeners = set()
        self._loop = None
        self._thread = None

    # Running it

    def start(self):
        """Starts serving; raises OSError when a port cannot be taken."""
        started = threading.Event()
        failure = []

        def run():
            loop = asyncio.new_event_loop()
            asyncio.set_event_loop(loop)
            servers = []
            try:
                for handler, port in ((self._serve_events, WS_PORT), (self._serve_api, REST_PORT)):
                    servers.append(loop.run_until_complete(asyncio.start_server(handler, self.host, port)))
            except OSError as error:
                failure.append(error)
            self._loop = loop
            started.set()
            if not failure:
                loop.run_forever()
            for server in servers:
                server.close()
            for writer in list(self._listeners):
                writer.close()
            tasks = asyncio.all_tasks(loop)
            for task in tasks:
                task.cancel()
            if tasks:
                loop.run_until_complete(asyncio.gather(*tasks, return_exceptions=True))
            loop.close()

        self._thread = threading.Thread(target=run, name="fake-speaker", daemon=True)
        self._thread.start()
        started.wait(10)
        if failure:
            self._thread.join(10)
            self._thread = None
            raise failure[0]
        self._say("listening on %s (events :%d, api :%d)" % (self.host, WS_PORT, REST_PORT))
        return self

    def stop(self):
        if self._thread is not None:
            self._loop.call_soon_threadsafe(self._loop.stop)
            self._thread.join(10)
            self._thread = None

    def __enter__(self):
        return self.start()

    def __exit__(self, *_):
        self.stop()

    # Driving it from a test

    def set_volume(self, level):
        self._call(self._apply_volume(level=level))

    def set_muted(self, muted):
        self._call(self._apply_volume(muted=muted))

    def set_refusing(self, refusing):
        self.refusing = bool(refusing)

    def set_ignoring_keys(self, ignoring):
        self.ignoring_keys = bool(ignoring)

    def set_now_playing(self, **fields):
        unknown = set(fields) - set(NOW_PLAYING_FIELDS)
        if unknown:
            raise ValueError("not a now-playing field: %s" % ", ".join(sorted(unknown)))
        self._call(self._apply_now_playing(fields))

    def push(self, xml):
        self._call(self._push(xml))

    def listeners(self):
        return len(self._listeners)

    def state(self):
        return {"device_id": self.device_id, "name": self.name, "host": self.host,
                "volume": self.volume, "muted": self.muted, "refusing": self.refusing,
                "ignoring_keys": self.ignoring_keys, "keys": list(self.keys),
                "now_playing": dict(self.now), "listeners": len(self._listeners)}

    def _call(self, coroutine):
        asyncio.run_coroutine_threadsafe(coroutine, self._loop).result(10)

    def _say(self, line):
        if self.log is not None:
            self.log("fake speaker: " + line)

    # What it says, in the speaker's own XML

    def _volume_fields(self):
        return ("<targetvolume>%d</targetvolume><actualvolume>%d</actualvolume><muteenabled>%s</muteenabled>"
                % (self.volume, self.volume, "true" if self.muted else "false"))

    def _now_playing_xml(self):
        now = self.now
        device = quoteattr(self.device_id)
        if now["source"] == "UPNP":
            return ('<nowPlaying deviceID=%s source="UPNP" sourceAccount="UPnPUserName">'
                    '<ContentItem source="UPNP" location=%s sourceAccount="UPnPUserName" isPresetable="false">'
                    '<itemName>%s</itemName></ContentItem><track>%s</track><artist>%s</artist>'
                    '<album>%s</album><stationName></stationName><art artImageStatus="SHOW_DEFAULT_IMAGE" />'
                    '<time total="0">0</time><playStatus>%s</playStatus><skipPreviousEnabled />'
                    '<streamType>TRACK_ONDEMAND</streamType></nowPlaying>'
                    % (device, quoteattr(now["location"]), escape(now["track"]), escape(now["track"]),
                       escape(now["artist"]), escape(now["album"]), escape(now["status"])))
        status = "<playStatus>%s</playStatus>" % escape(now["status"]) if now["status"] else ""
        source = quoteattr(now["source"])
        return ('<nowPlaying deviceID=%s source=%s><ContentItem source=%s isPresetable="true" />%s</nowPlaying>'
                % (device, source, source, status))

    async def _apply_volume(self, level=None, muted=None):
        if level is not None:
            self.volume = max(0, min(100, int(level)))
        if muted is not None:
            self.muted = bool(muted)
        await self._push('<updates deviceID=%s><volumeUpdated><volume>%s</volume></volumeUpdated></updates>'
                         % (quoteattr(self.device_id), self._volume_fields()))

    async def _apply_now_playing(self, fields):
        for name, value in fields.items():
            self.now[name] = str(value)
        await self._push('<updates deviceID=%s><nowPlayingUpdated>%s</nowPlayingUpdated></updates>'
                         % (quoteattr(self.device_id), self._now_playing_xml()))

    async def _press(self, name):
        """What the speaker does on a key's press."""
        if name == "POWER":
            if self.now["source"] != "STANDBY":
                self._before_standby = dict(self.now)
                fields = dict.fromkeys(NOW_PLAYING_FIELDS, "")
                fields["source"] = "STANDBY"
            else:
                fields = self._before_standby or dict(dict.fromkeys(NOW_PLAYING_FIELDS, ""), source="INVALID_SOURCE")
            await self._apply_now_playing(fields)
            return
        if self.now["source"] in ("STANDBY", "INVALID_SOURCE"):
            return
        playing = self.now["status"] in ("PLAY_STATE", "BUFFERING_STATE")
        if name == "PAUSE" or (name == "PLAY_PAUSE" and playing):
            if playing:
                await self._apply_now_playing({"status": "PAUSE_STATE"})
        elif name in ("PLAY", "PLAY_PAUSE") and not playing:
            await self._apply_now_playing({"status": "PLAY_STATE"})

    def _errors_xml(self, message):
        return (XML_DECLARATION + '<errors deviceID=%s><error value="1019" name="CLIENT_XML_ERROR" severity="Unknown">'
                '%s</error></errors>' % (quoteattr(self.device_id), escape(message)))

    async def _push(self, xml):
        self._say("push " + xml)
        frame = _frame(0x1, xml.encode("utf-8"))
        for writer in list(self._listeners):
            try:
                writer.write(frame)
                await writer.drain()
            except ConnectionError:
                self._listeners.discard(writer)

    # The event stream (port 8080)

    async def _serve_events(self, reader, writer):
        try:
            _, headers = _parse_head(await asyncio.wait_for(reader.readuntil(b"\r\n\r\n"), 10))
        except (asyncio.IncompleteReadError, asyncio.LimitOverrunError, asyncio.TimeoutError, ConnectionError):
            writer.close()
            return
        key = headers.get("sec-websocket-key", "")
        if not key or headers.get("upgrade", "").lower() != "websocket":
            writer.write(b"HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n")
            writer.close()
            return
        accept = base64.b64encode(hashlib.sha1((key + WS_GUID).encode("ascii")).digest()).decode("ascii")
        answer = ["HTTP/1.1 101 Switching Protocols", "Upgrade: websocket", "Connection: Upgrade",
                  "Sec-WebSocket-Accept: " + accept]
        if "gabbo" in [name.strip() for name in headers.get("sec-websocket-protocol", "").split(",")]:
            answer.append("Sec-WebSocket-Protocol: gabbo")
        writer.write(("\r\n".join(answer) + "\r\n\r\n").encode("latin-1"))
        writer.write(_frame(0x1, b'<SoundTouchSdkInfo serverVersion="4" serverBuild="fake speaker" />'))
        try:
            await writer.drain()
        except ConnectionError:
            writer.close()
            return
        self._listeners.add(writer)
        self._say("listener connected (%d now)" % len(self._listeners))
        try:
            while True:
                opcode, payload = await _read_frame(reader)
                if opcode == 0x8:                   # close: answer it, then hang up
                    writer.write(_frame(0x8, payload[:2]))
                    await writer.drain()
                    break
                if opcode == 0x9:                   # ping
                    writer.write(_frame(0xA, payload))
                    await writer.drain()
        except (asyncio.IncompleteReadError, ConnectionError):
            pass
        finally:
            self._listeners.discard(writer)
            writer.close()
            self._say("listener gone (%d now)" % len(self._listeners))

    # The Web API (port 8090)

    async def _serve_api(self, reader, writer):
        try:
            request_line, headers = _parse_head(await asyncio.wait_for(reader.readuntil(b"\r\n\r\n"), 10))
            method, target, _ = request_line.split(" ", 2)
            if headers.get("expect", "").lower() == "100-continue":
                writer.write(b"HTTP/1.1 100 Continue\r\n\r\n")
            length = int(headers.get("content-length") or 0)
            body = (await reader.readexactly(length)).decode("utf-8", "replace") if length else ""
            status, content_type, payload = await self._route(method, target.split("?", 1)[0], body)
            self._say("%s %s -> %d" % (method, target, status))
        except ConnectionError:
            writer.close()
            return
        except (asyncio.IncompleteReadError, asyncio.LimitOverrunError, asyncio.TimeoutError, ValueError):
            status, content_type, payload = 400, "text/plain", "bad request\n"
        data = payload.encode("utf-8")
        try:
            writer.write(("HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %d\r\nConnection: close\r\n\r\n"
                          % (status, REASONS.get(status, "OK"), content_type, len(data))).encode("latin-1") + data)
            await writer.drain()
        except ConnectionError:
            pass
        finally:
            writer.close()

    async def _route(self, method, path, body):
        if method == "GET":
            if path == "/info":
                return 200, XML_TYPE, (XML_DECLARATION + '<info deviceID=%s><name>%s</name><type>SoundTouch 30</type>'
                                       '<networkInfo type="SCM"><ipAddress>%s</ipAddress></networkInfo></info>'
                                       % (quoteattr(self.device_id), escape(self.name), escape(self.host)))
            if path in ("/nowPlaying", "/now_playing"):
                return 200, XML_TYPE, XML_DECLARATION + self._now_playing_xml()
            if path == "/volume":
                return 200, XML_TYPE, (XML_DECLARATION + "<volume deviceID=%s>%s</volume>"
                                       % (quoteattr(self.device_id), self._volume_fields()))
            if path == "/presets":
                return 200, XML_TYPE, XML_DECLARATION + "<presets />"
            if path == "/fake/state":
                return 200, "application/json", json.dumps(self.state()) + "\n"
        elif method == "POST":
            if path == "/volume":
                level = re.search(r"<volume>\s*(\d+)\s*</volume>", body)
                if level is None or self.refusing:
                    return 400, XML_TYPE, self._errors_xml("refused" if self.refusing else "expected <volume>N</volume>")
                await self._apply_volume(level=int(level.group(1)))
                return 200, XML_TYPE, XML_DECLARATION + "<status>/volume</status>"
            if path == "/key":
                key = re.search(r'<key\s+state="(press|release)"[^>]*>\s*([A-Z0-9_]+)\s*</key>', body)
                if key is None or self.refusing:
                    return 400, XML_TYPE, self._errors_xml("refused" if self.refusing else "expected <key state=...>KEY</key>")
                if key.group(1) == "press":
                    self.keys.append(key.group(2))
                    if not self.ignoring_keys:
                        await self._press(key.group(2))
                return 200, XML_TYPE, XML_DECLARATION + "<status>/key</status>"
            if path == "/fake/event":
                await self._push(body)
                return 200, "text/plain", "pushed to %d listener(s)\n" % len(self._listeners)
            if path == "/fake/mute":
                await self._apply_volume(muted=body.strip().lower() in ("1", "true", "on", "yes"))
                return 200, "text/plain", ("muted\n" if self.muted else "unmuted\n")
            if path == "/fake/refuse":
                self.refusing = body.strip().lower() in ("1", "true", "on", "yes")
                return 200, "text/plain", ("refusing /volume and /key\n" if self.refusing else "taking /volume and /key\n")
            if path == "/fake/ignore-keys":
                self.ignoring_keys = body.strip().lower() in ("1", "true", "on", "yes")
                return 200, "text/plain", ("ignoring keys\n" if self.ignoring_keys else "acting on keys\n")
            if path == "/fake/nowplaying":
                try:
                    fields = json.loads(body or "{}")
                except ValueError:
                    fields = None
                if not isinstance(fields, dict) or not set(fields) <= set(NOW_PLAYING_FIELDS):
                    return 400, "text/plain", "expected a JSON object of %s\n" % ", ".join(NOW_PLAYING_FIELDS)
                await self._apply_now_playing(fields)
                return 200, "text/plain", "ok\n"
        return 404, XML_TYPE, (XML_DECLARATION + '<errors deviceID=%s><error value="404" name="HTTP_STATUS_NOT_FOUND"'
                               ' severity="Unknown">not emulated by the fake speaker</error></errors>'
                               % quoteattr(self.device_id))


def write_data_dir(directory, speaker):
    """Makes directory a cxstcc data dir whose default speaker is this fake. Refuses one whose
    devices.json names some other speaker, so a real data dir is never overwritten."""
    os.makedirs(directory, exist_ok=True)
    devices_path = os.path.join(directory, "devices.json")
    if os.path.exists(devices_path):
        try:
            with open(devices_path) as existing:
                current = json.load(existing).get("default_device", "")
        except (OSError, ValueError, AttributeError):
            current = None
        if current != speaker.device_id:
            raise ValueError("%s already names a real speaker; give the fake an empty directory" % devices_path)
    devices = {"default_device": speaker.device_id,
               "devices": [{"device_id": speaker.device_id, "device_name": speaker.name,
                            "ip_address": speaker.host}]}
    with open(devices_path, "w") as out:
        json.dump(devices, out, indent=2)
        out.write("\n")
    streams_path = os.path.join(directory, "streams.json")
    if not os.path.exists(streams_path):
        shutil.copyfile(os.path.join(REPO, "streams.json"), streams_path)


def main():
    parser = argparse.ArgumentParser(description="A fake Bose SoundTouch speaker, for testing cxstcc.")
    parser.add_argument("--host", default="127.0.0.2", help="address to listen on (default 127.0.0.2)")
    parser.add_argument("--volume", type=int, default=15, help="starting volume, 0 to 100 (default 15)")
    parser.add_argument("--muted", action="store_true", help="start muted")
    parser.add_argument("--data-dir", help="also make this directory a cxstcc data dir that uses the fake")
    parser.add_argument("--quiet", action="store_true", help="do not log requests and events")
    args = parser.parse_args()

    speaker = FakeSpeaker(args.host, volume=args.volume, muted=args.muted,
                          log=None if args.quiet else (lambda line: print(line, flush=True)))
    if args.data_dir:
        try:
            write_data_dir(args.data_dir, speaker)
        except (OSError, ValueError) as error:
            sys.exit("fake speaker: %s" % error)
    try:
        speaker.start()
    except OSError as error:
        sys.exit("fake speaker: cannot listen on %s ports %d and %d: %s" % (args.host, WS_PORT, REST_PORT, error))

    api = "http://%s:%d" % (args.host, REST_PORT)
    if args.data_dir:
        print("Run cxstcc against it:\n  ./cxstcc --data-dir %s control --no-proxy --web --web-port 8094" % args.data_dir)
    print("Change the volume:  curl -X POST -d '<volume>25</volume>' %s/volume" % api)
    print("Mute it:            curl -X POST -d true %s/fake/mute" % api)
    print("Ctrl-C to stop.", flush=True)

    stopped = threading.Event()
    signal.signal(signal.SIGINT, lambda *_: stopped.set())
    signal.signal(signal.SIGTERM, lambda *_: stopped.set())
    while not stopped.wait(0.5):
        pass
    speaker.stop()


if __name__ == "__main__":
    main()
