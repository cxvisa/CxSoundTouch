# Running `control` as a system service

`soundtouch.service` runs `soundtouch control --update-track-info` at boot and restarts it if it
dies. It runs as its own unprivileged user (`DynamicUser=yes`) with only its state directory
writable, and logs to the journal.

| What | Where |
|---|---|
| Binary | `/usr/local/bin/soundtouch` |
| Unit | `/etc/systemd/system/soundtouch.service` |
| Settings | `/etc/soundtouch/soundtouch.env` (title offset, relay buffer and port, extra options) |
| Data | `/var/lib/soundtouch`: `streams.json`, `devices.json`, `state.json` |

`/var/lib/soundtouch` is a symlink that systemd creates to `/var/lib/private/soundtouch`, which is
where the files really are. At start, systemd gives that directory and everything in it to the
service's user.

## Install

From the repository, after `make`, either with the make targets:

```bash
sudo make install install-service SEED_FROM=/path/to/old/data/dir
```

`install` copies the binary; `install-service` installs the unit and the settings file, seeds
`/var/lib/private/soundtouch` with `streams.json` (and `devices.json` and `state.json` from
`SEED_FROM`, if given), and reloads systemd. Settings and data already in place are left alone,
and neither target enables or starts the service. `sudo make -n install install-service` shows the
commands without running them.

Or by hand, step by step:

```bash
# 1. The binary.
sudo install -D -m 0755 soundtouch /usr/local/bin/soundtouch

# 2. The settings. Edit them afterwards, e.g. SOUNDTOUCH_TITLE_OFFSET=-3.1.
sudo install -D -m 0644 systemd/soundtouch.env /etc/soundtouch/soundtouch.env

# 3. The data directory, seeded before the first start. devices.json and state.json are optional:
#    copy them from wherever control ran before to keep the speaker and the last preset.
sudo install -d -m 0700 /var/lib/private
sudo install -d -m 0755 /var/lib/private/soundtouch
sudo install -m 0644 streams.json /var/lib/private/soundtouch/
sudo install -m 0644 /path/to/devices.json /path/to/state.json /var/lib/private/soundtouch/

# 4. The unit.
sudo install -m 0644 systemd/soundtouch.service /etc/systemd/system/soundtouch.service
sudo systemctl daemon-reload
systemd-analyze verify /etc/systemd/system/soundtouch.service
```

Without a `devices.json`, commands use `192.168.3.53`. To find the speaker instead:

```bash
sudo /usr/local/bin/soundtouch --data-dir /var/lib/private/soundtouch discover --save
```

## Start

Only one `control` may run at a time: two would fight over port 8899 and both answer the buttons.
Stop any other one first, then:

```bash
sudo systemctl enable --now soundtouch
systemctl status soundtouch
journalctl -u soundtouch -f
```

A good start looks like `Relay listening on port 8899 ...`, `CONNECTED`, then `Resuming <station>
from last time.` (if `state.json` was copied) and `<station> is playing.`

## Day to day

```bash
sudo systemctl restart soundtouch    # after changing settings or installing a new binary
sudo systemctl stop soundtouch       # the music stops with it: the relay is in the audio path
journalctl -u soundtouch --since today
```

To change `streams.json` or `devices.json`, edit them in `/var/lib/private/soundtouch` with `sudo`,
then restart. A file added later as root is not handed to the service's user again (systemd only
does that when the directory itself is not the service's), so make it readable:
`sudo chmod 0644 /var/lib/private/soundtouch/*.json`.

## Remove

```bash
sudo systemctl disable --now soundtouch
sudo rm /etc/systemd/system/soundtouch.service
sudo systemctl daemon-reload
# and, if wanted: /etc/soundtouch, /var/lib/private/soundtouch, /usr/local/bin/soundtouch
```
