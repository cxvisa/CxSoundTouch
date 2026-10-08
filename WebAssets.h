#ifndef WEB_ASSETS_H
#define WEB_ASSETS_H

// The dashboard, embedded so the binary stays self-contained (no asset files to ship, so the
// scratch container image is unchanged). Vanilla HTML/CSS/JS: all rendering runs in the browser, so
// the appliance only ever serves these bytes and small JSON. Served at "/".
inline const char *DASHBOARD_HTML = R"PAGE(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>SoundTouch</title>
<style>
  :root {
    --bg: #0f1216; --panel: #171c22; --panel2: #1e242c; --line: #2a323c;
    --fg: #e6edf3; --muted: #8b99a7; --accent: #4cc2ff; --ok: #3fb950; --warn: #d29922;
  }
  * { box-sizing: border-box; }
  body {
    margin: 0; background: var(--bg); color: var(--fg);
    font: 15px/1.5 system-ui, -apple-system, Segoe UI, Roboto, sans-serif;
  }
  header {
    display: flex; align-items: center; gap: 12px;
    padding: 16px 22px; border-bottom: 1px solid var(--line); position: sticky; top: 0;
    background: rgba(15,18,22,.85); backdrop-filter: blur(8px);
  }
  header h1 { font-size: 17px; margin: 0; font-weight: 650; letter-spacing: .2px; }
  .dot { width: 9px; height: 9px; border-radius: 50%; background: var(--muted); transition: background .3s; }
  .dot.ok { background: var(--ok); } .dot.bad { background: #f85149; }
  .spacer { flex: 1; }
  button {
    background: var(--panel2); color: var(--fg); border: 1px solid var(--line);
    border-radius: 8px; padding: 7px 13px; cursor: pointer; font-size: 14px;
  }
  button:hover { border-color: var(--accent); }
  main { padding: 22px; max-width: 1040px; margin: 0 auto; display: grid; gap: 18px; }
  .hero {
    background: linear-gradient(135deg, var(--panel), var(--panel2));
    border: 1px solid var(--line); border-radius: 16px; padding: 22px 24px;
    display: flex; align-items: center; gap: 20px; flex-wrap: wrap;
  }
  .hero-main { flex: 1; min-width: 260px; }
  .transport { display: none; align-items: center; gap: 14px; }
  .transport.show { display: flex; }
  .tbtn { width: 44px; height: 44px; padding: 0; flex: none; border-radius: 50%;
          display: inline-flex; align-items: center; justify-content: center; transition: box-shadow .15s, opacity .15s; }
  .tbtn svg { width: 20px; height: 20px; fill: currentColor; }
  .tbtn.power { color: var(--muted); }
  .tbtn.power svg { fill: none; stroke: currentColor; stroke-width: 2.2; stroke-linecap: round; }
  .tbtn.power.on { color: var(--ok); border-color: rgba(63,185,80,.55); }
  .tbtn.play { width: 58px; height: 58px; background: var(--accent); border-color: transparent; color: #06131c; }
  .tbtn.play svg { width: 26px; height: 26px; }
  .tbtn.play .i-pause, .tbtn.play.playing .i-play { display: none; }
  .tbtn.play.playing .i-pause { display: block; }
  .tbtn:disabled { opacity: .35; cursor: default; }
  .tbtn:disabled:hover { border-color: var(--line); }
  .tbtn.play:disabled:hover { border-color: transparent; }
  .tbtn.busy, .tbtn.busy:disabled { opacity: 1; animation: pulse 1s ease-in-out infinite; }
  .tbtn.bad { box-shadow: 0 0 0 3px rgba(248,81,73,.6); }
  @keyframes pulse { 50% { opacity: .45; } }
  .hero .label { color: var(--muted); font-size: 12px; text-transform: uppercase; letter-spacing: .12em; }
  .hero .station { font-size: 26px; font-weight: 700; margin: 4px 0 2px; }
  .hero .song { font-size: 17px; color: var(--fg); }
  .hero .meta { color: var(--muted); font-size: 13px; margin-top: 8px; }
  .hero .vol { display: none; align-items: center; gap: 10px; margin-top: 16px; max-width: 520px; --fill: var(--accent); }
  .hero .vol.show { display: flex; }
  .vol.muted { --fill: var(--warn); }
  .vol .vlabel { color: var(--muted); font-size: 11px; text-transform: uppercase; letter-spacing: .12em; }
  .vslider { -webkit-appearance: none; appearance: none; flex: 1; min-width: 110px; height: 22px; margin: 0;
             background: transparent; cursor: pointer; touch-action: none; --pct: 0%; }
  .vslider:focus { outline: none; }
  .vslider::-webkit-slider-runnable-track { height: 6px; border-radius: 999px; box-shadow: inset 0 0 0 1px var(--line);
             background: linear-gradient(to right, var(--fill) 0 var(--pct), var(--panel2) var(--pct) 100%); }
  .vslider::-webkit-slider-thumb { -webkit-appearance: none; box-sizing: border-box; width: 16px; height: 16px; margin-top: -5px;
             border-radius: 50%; background: var(--fg); border: 3px solid var(--fill); transition: transform .12s ease-out; }
  .vslider:hover::-webkit-slider-thumb, .vslider:focus-visible::-webkit-slider-thumb,
  .vslider:active::-webkit-slider-thumb { transform: scale(1.2); }
  .vslider::-moz-range-track { height: 6px; border-radius: 999px; background: var(--panel2); box-shadow: inset 0 0 0 1px var(--line); }
  .vslider::-moz-range-progress { height: 6px; border-radius: 999px; background: var(--fill); }
  .vslider::-moz-range-thumb { box-sizing: border-box; width: 16px; height: 16px; border-radius: 50%; background: var(--fg);
             border: 3px solid var(--fill); }
  .vstep { width: 30px; height: 30px; padding: 0; flex: none; border-radius: 50%; font-size: 17px; line-height: 1;
           display: inline-flex; align-items: center; justify-content: center;
           user-select: none; -webkit-user-select: none; touch-action: manipulation; }
  .vstep:active { background: var(--line); }
  .vnum { width: 3.4em; height: 30px; flex: none; padding: 0 4px; text-align: center; font: inherit; font-size: 15px;
          font-variant-numeric: tabular-nums; color: var(--fg); background: var(--panel2); border: 1px solid var(--line);
          border-radius: 8px; -moz-appearance: textfield; appearance: textfield; }
  .vnum::-webkit-outer-spin-button, .vnum::-webkit-inner-spin-button { -webkit-appearance: none; margin: 0; }
  .vnum:focus { outline: none; border-color: var(--accent); }
  .vnum.bad { border-color: #f85149; }
  .vmuted { display: none; padding: 1px 8px; border-radius: 999px; border: 1px solid var(--warn); color: var(--warn); font-size: 12px; }
  .vol.muted .vmuted { display: inline-block; }
  .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(260px, 1fr)); gap: 18px; }
  .card { background: var(--panel); border: 1px solid var(--line); border-radius: 14px; padding: 16px 18px; }
  .card h2 { font-size: 13px; text-transform: uppercase; letter-spacing: .1em; color: var(--muted); margin: 0 0 12px; }
  table { width: 100%; border-collapse: collapse; font-size: 14px; }
  th, td { text-align: left; padding: 8px 10px; border-bottom: 1px solid var(--line); vertical-align: top; }
  th { color: var(--muted); font-weight: 600; font-size: 12px; text-transform: uppercase; letter-spacing: .06em; }
  tr:last-child td { border-bottom: none; }
  .pill { display: inline-block; padding: 1px 8px; border-radius: 999px; background: var(--panel2);
          border: 1px solid var(--line); font-size: 12px; color: var(--muted); }
  .pill.on { color: var(--accent); border-color: var(--accent); }
  .kv { display: grid; grid-template-columns: auto 1fr; gap: 6px 14px; font-size: 14px; }
  .kv div:nth-child(odd) { color: var(--muted); }
  a { color: var(--accent); text-decoration: none; word-break: break-all; }
  .muted { color: var(--muted); }
  code { font-family: ui-monospace, SFMono-Regular, Menlo, monospace; font-size: 13px; }
</style>
</head>
<body>
<header>
  <span id="dot" class="dot"></span>
  <h1>SoundTouch</h1>
  <span id="mode" class="pill"></span>
  <span class="spacer"></span>
  <button id="refresh">Refresh</button>
</header>
<main>
  <section class="hero">
    <div class="hero-main">
      <div class="label">Now playing</div>
      <div class="station" id="np-station">&mdash;</div>
      <div class="song" id="np-song"></div>
      <div class="meta" id="np-meta"></div>
      <div class="vol" id="np-volume">
        <span class="vlabel">Vol</span>
        <input class="vslider" id="np-vol-slider" type="range" min="0" max="100" step="1" value="0" aria-label="Volume">
        <button class="vstep" id="np-vol-down" type="button" aria-label="Volume down" title="Volume down (hold to repeat)">&minus;</button>
        <input class="vnum" id="np-vol-num" type="number" min="0" max="100" step="1" inputmode="numeric"
               aria-label="Volume level" title="Type a level, then Enter">
        <button class="vstep" id="np-vol-up" type="button" aria-label="Volume up" title="Volume up (hold to repeat)">+</button>
        <span class="vmuted">Muted</span>
      </div>
    </div>
    <div class="transport" id="np-transport">
      <button class="tbtn power" id="np-power" type="button" aria-label="Power" title="Switch off">
        <svg viewBox="0 0 24 24" aria-hidden="true"><path d="M12 3.5v8"/><path d="M7.05 6.6a7.5 7.5 0 1 0 9.9 0"/></svg>
      </button>
      <button class="tbtn play" id="np-play" type="button" aria-label="Play or pause" title="Play">
        <svg class="i-play" viewBox="0 0 24 24" aria-hidden="true"><path d="M9 5v14l11-7z"/></svg>
        <svg class="i-pause" viewBox="0 0 24 24" aria-hidden="true"><path d="M7 5h3.5v14H7zM13.5 5H17v14h-3.5z"/></svg>
      </button>
    </div>
  </section>

  <div class="grid">
    <div class="card"><h2>Default device</h2><div id="default-device" class="kv"></div></div>
    <div class="card"><h2>Configuration</h2><div id="config" class="kv"></div></div>
  </div>

  <div class="card"><h2>Streams</h2><div id="streams"></div></div>
  <div class="card"><h2>Devices</h2><div id="devices"></div></div>
</main>

<script>
const $ = (id) => document.getElementById(id);
const esc = (s) => String(s == null ? "" : s).replace(/[&<>"]/g, c =>
  ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]));

async function get(path) {
  const r = await fetch(path, { cache: "no-store" });
  if (!r.ok) throw new Error(path + " -> " + r.status);
  return r.json();
}

function setOnline(ok) {
  $("dot").className = "dot " + (ok ? "ok" : "bad");
}

// Volume. The slider, the - and + buttons (hold to repeat) and the number all ask the speaker for a
// level. One request is in flight at a time, and a newer level replaces any still waiting, so a drag
// never builds a backlog. While the user is changing it the speaker's reports only update the mute
// state, so the slider cannot jump under their hand; the next report after that sets it straight.
const vol = { level: null, want: null, sending: false, quietUntil: 0, dragging: false };

function showVolume(level, muted) {
  const box = $("np-volume"), slider = $("np-vol-slider"), num = $("np-vol-num");
  vol.level = level;
  box.classList.add("show");
  box.classList.toggle("muted", !!muted);
  slider.value = level;
  slider.style.setProperty("--pct", level + "%");
  if (document.activeElement !== num) num.value = level;
}

// The level the speaker reports.
function renderVolume(level, muted) {
  const box = $("np-volume");
  if (level == null) { box.classList.remove("show"); return; }
  if (vol.dragging || vol.sending || vol.want != null || Date.now() < vol.quietUntil) {
    box.classList.toggle("muted", !!muted);
    return;
  }
  showVolume(Number(level), muted);
}

// A level the user asks for.
function askVolume(level) {
  level = Math.round(Number(level));
  if (!Number.isFinite(level)) return;
  level = Math.max(0, Math.min(100, level));
  if (level === vol.level) return;
  showVolume(level, $("np-volume").classList.contains("muted"));
  vol.want = level;
  vol.quietUntil = Date.now() + 800;
  sendVolume();
}

async function sendVolume() {
  if (vol.sending || vol.want == null) return;
  const level = vol.want;
  vol.want = null;
  vol.sending = true;
  let ok = false;
  try {
    const r = await fetch("/api/volume", { method: "POST", headers: { "Content-Type": "application/json" },
                                           body: JSON.stringify({ volume: level }) });
    ok = r.ok;
  } catch (e) { }
  vol.sending = false;
  vol.quietUntil = Date.now() + 800;
  if (vol.want != null) { sendVolume(); return; }
  if (!ok) {
    // Not taken: say so, and show what the speaker really has.
    const num = $("np-vol-num");
    num.classList.add("bad");
    setTimeout(() => num.classList.remove("bad"), 1500);
    vol.quietUntil = 0;
    vol.level = null;
    loadNowPlaying();
  }
}

function stepper(button, delta) {
  let timer = null;
  const stop = () => { clearTimeout(timer); clearInterval(timer); timer = null; };
  const step = () => askVolume((vol.level || 0) + delta);
  button.addEventListener("pointerdown", (e) => {
    if (e.button !== 0) return;
    e.preventDefault();
    stop();
    step();
    timer = setTimeout(() => { timer = setInterval(step, 90); }, 400);
  });
  for (const type of ["pointerup", "pointerleave", "pointercancel"]) button.addEventListener(type, stop);
  button.addEventListener("click", (e) => { if (e.detail === 0) step(); });   // Enter or Space
  button.addEventListener("contextmenu", (e) => e.preventDefault());          // a long press on touch
}

function wireVolume() {
  const slider = $("np-vol-slider"), num = $("np-vol-num");
  const dragEnd = () => { if (vol.dragging) { vol.dragging = false; vol.quietUntil = Date.now() + 800; } };
  slider.addEventListener("pointerdown", () => { vol.dragging = true; });
  window.addEventListener("pointerup", dragEnd);
  window.addEventListener("pointercancel", dragEnd);
  slider.addEventListener("change", dragEnd);
  slider.addEventListener("input", () => askVolume(slider.value));
  stepper($("np-vol-down"), -1);
  stepper($("np-vol-up"), 1);
  // Clicking into the number selects it all, so typing replaces it; the click's own mouseup would
  // otherwise drop the selection, and "40" typed after "15" would ask for 1540.
  let keepSelection = false;
  num.addEventListener("focus", () => { num.select(); keepSelection = true; });
  num.addEventListener("mouseup", (e) => { if (keepSelection) { e.preventDefault(); keepSelection = false; } });
  num.addEventListener("blur", () => { keepSelection = false; });
  num.addEventListener("keydown", (e) => {
    keepSelection = false;
    if (e.key === "Enter") { e.preventDefault(); num.blur(); }
    else if (e.key === "Escape") { num.value = vol.level; num.blur(); }
    else if (e.key === "ArrowUp" || e.key === "ArrowDown") {
      e.preventDefault();
      askVolume((vol.level || 0) + (e.key === "ArrowUp" ? 1 : -1));
      num.value = vol.level;
    }
  });
  num.addEventListener("change", () => {
    if (num.value.trim() !== "") askVolume(num.valueAsNumber);
    num.value = vol.level;
  });
}

// Play/pause and power: the remote's keys. Each request is answered once the speaker has done it, so
// the button pulses until then; refused or not done in time, it rings red, and the page shows what
// the speaker is really doing.
const tp = { busy: false, source: "", status: "" };

function renderTransport(source, status) {
  tp.source = source || "";
  tp.status = status || "";
  const box = $("np-transport"), power = $("np-power"), play = $("np-play");
  if (!tp.source) { box.classList.remove("show"); return; }
  box.classList.add("show");
  const on = tp.source !== "STANDBY";
  const playing = tp.status === "PLAY_STATE" || tp.status === "BUFFERING_STATE";
  power.classList.toggle("on", on);
  power.title = on ? "Switch off" : "Switch on";
  play.classList.toggle("playing", playing);
  play.title = playing ? "Pause" : "Play";
  power.disabled = tp.busy;
  play.disabled = tp.busy || !on || tp.source === "INVALID_SOURCE" || !tp.status || tp.status === "INVALID_PLAY_STATUS";
}

async function transportAction(button, path, body) {
  if (tp.busy) return;
  tp.busy = true;
  button.classList.add("busy");
  renderTransport(tp.source, tp.status);
  let ok = false;
  try {
    const r = await fetch(path, { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(body) });
    ok = r.ok;
  } catch (e) { }
  tp.busy = false;
  button.classList.remove("busy");
  if (!ok) {
    button.classList.add("bad");
    setTimeout(() => button.classList.remove("bad"), 1500);
  }
  await loadNowPlaying();
}

function wireTransport() {
  const play = $("np-play"), power = $("np-power");
  play.addEventListener("click", () =>
    transportAction(play, "/api/playback", { action: play.classList.contains("playing") ? "pause" : "play" }));
  power.addEventListener("click", () =>
    transportAction(power, "/api/power", { on: !power.classList.contains("on") }));
}

function renderNowPlaying(np) {
  const standby = np.source === "STANDBY";
  const station = standby ? "Standby" : (np.station || np.source || "\u2014");
  $("np-station").textContent = station;
  $("np-song").textContent = standby ? "" : (np.title || "");
  const bits = [];
  if (np.status) bits.push(np.status);
  if (np.source && !standby && np.source !== station) bits.push(np.source);
  if (np.relay_running != null) bits.push(np.relay_running ? "relay on" : "relay off");
  $("np-meta").textContent = bits.join("  \u00b7  ");
  renderVolume(np.volume, np.muted);
  renderTransport(np.source, np.status);
}

async function loadNowPlaying() {
  try {
    renderNowPlaying(await get("/api/nowplaying"));
    setOnline(true);
  } catch (e) { setOnline(false); }
}

const sleep = (ms) => new Promise(done => setTimeout(done, ms));

// Inside control the server holds this request until something live changes (the volume, at
// once) or ~3 s pass, and the page asks again straight away: a change shows immediately, and a
// quiet speaker costs what polling did. Resolves only where there is no such request (the
// standalone "web" command), which then polls instead.
async function followLive() {
  let seq = null;
  for (;;) {
    const started = Date.now();
    try {
      const r = await fetch("/api/live/wait" + (seq == null ? "" : "?since=" + seq), { cache: "no-store" });
      if (r.status === 404) return;
      if (!r.ok) throw new Error("/api/live/wait -> " + r.status);
      const np = await r.json();
      if (typeof np.seq !== "number") return;
      seq = np.seq;
      renderNowPlaying(np);
      setOnline(true);
    } catch (e) {
      // Busy or unreachable: show what can be shown, then try holding again.
      await loadNowPlaying();
      await sleep(3000);
      continue;
    }
    // Paces a burst, such as the volume knob being turned, to at most ~20 requests a second.
    const spent = Date.now() - started;
    if (spent < 50) await sleep(50 - spent);
  }
}

async function loadStatic() {
  try {
    const [streams, devices, config, live] = await Promise.all([
      get("/api/streams"), get("/api/devices"), get("/api/config"),
      get("/api/live").catch(() => ({}))
    ]);

    $("mode").textContent = config.embedded ? "control --web" : "web";
    $("mode").className = "pill" + (config.embedded ? " on" : "");

    // Streams table
    let s = "<table><tr><th>Preset</th><th>Name</th><th>Display</th><th>URL</th></tr>";
    for (const st of streams) {
      s += "<tr><td>" + (st.preset == null ? '<span class="muted">\u2014</span>' : esc(st.preset)) +
           "</td><td><code>" + esc(st.name) + "</code></td><td>" + esc(st.display_name) +
           "</td><td><a href='" + esc(st.url) + "'>" + esc(st.url) + "</a></td></tr>";
    }
    $("streams").innerHTML = s + "</table>";

    // Devices table
    let d = "<table><tr><th></th><th>Name</th><th>IP</th><th>Device ID</th></tr>";
    for (const dv of devices.devices || []) {
      d += "<tr><td>" + (dv.is_default ? '<span class="pill on">default</span>' : "") +
           "</td><td>" + esc(dv.device_name) + "</td><td><code>" + esc(dv.ip_address) +
           "</code></td><td><code>" + esc(dv.device_id) + "</code></td></tr>";
    }
    if (!(devices.devices || []).length) d += "<tr><td colspan=4 class='muted'>No devices.json \u2014 run discover --save</td></tr>";
    $("devices").innerHTML = d + "</table>";

    // Default device
    const def = (devices.devices || []).find(x => x.is_default);
    $("default-device").innerHTML = def
      ? "<div>Name</div><div>" + esc(def.device_name || "\u2014") + "</div>" +
        "<div>IP</div><div><code>" + esc(def.ip_address) + "</code></div>" +
        "<div>ID</div><div><code>" + esc(def.device_id) + "</code></div>"
      : "<div class='muted'>none</div><div></div>";

    // Config
    const c = config;
    $("config").innerHTML =
      "<div>Data dir</div><div><code>" + esc(c.data_dir) + "</code></div>" +
      "<div>Web</div><div><code>" + esc(c.web_bind) + ":" + esc(c.web_port) + "</code></div>" +
      "<div>Relay port</div><div><code>" + esc(c.relay_port) + "</code></div>" +
      "<div>Relay buffer</div><div>" + esc(c.relay_buffer_mb) + " MB" +
        (c.pause_minutes != null ? " <span class='muted'>(~" + esc(c.pause_minutes) + " min pause)</span>" : "") + "</div>" +
      "<div>Title offset</div><div>" + esc(c.title_offset_seconds) + " s</div>" +
      "<div>Resume</div><div>" + (c.resume ? "on" : "off") + "</div>" +
      "<div>Version</div><div><code>" + esc(c.version) + "</code></div>";

    setOnline(true);
  } catch (e) { setOnline(false); }
}

$("refresh").onclick = () => { loadStatic(); loadNowPlaying(); };
wireVolume();
wireTransport();
loadStatic();
followLive().then(() => { loadNowPlaying(); setInterval(loadNowPlaying, 3000); });
</script>
</body>
</html>
)PAGE";

#endif
