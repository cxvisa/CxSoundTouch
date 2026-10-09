#ifndef WEB_ASSETS_H
#define WEB_ASSETS_H

// What every page's style starts with: the colours, the header and its navigation between the pages,
// cards, tables and buttons. Each page adds its own after it.
#define WEB_BASE_CSS R"CSS(  :root {
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
  @keyframes pulse { 50% { opacity: .45; } }
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
  [hidden] { display: none !important; }
  .card-head { display: flex; align-items: center; gap: 8px; flex-wrap: wrap; margin: 0 0 12px; }
  .card-head h2 { margin: 0 6px 0 0; }
  .smsg { flex: 1; min-width: 0; font-size: 12.5px; color: var(--muted); }
  .smsg.ok { color: var(--ok); }
  .smsg.bad { color: #f85149; }
  .smsg button { padding: 2px 9px; font-size: 12.5px; }
  button.primary { background: var(--accent); color: #06131c; border-color: transparent; font-weight: 600; }
  button.primary:disabled { opacity: .4; cursor: default; }
  button.warn { color: #f85149; border-color: #f85149; }
  .tablewrap { overflow-x: auto; }
  header .brand { display: flex; align-items: center; gap: 10px; color: var(--fg); word-break: normal; }
  nav.tabs { display: flex; gap: 4px; }
  nav.tabs a { padding: 5px 12px; border-radius: 8px; color: var(--muted); font-size: 14px; word-break: normal; }
  nav.tabs a:hover { color: var(--fg); background: var(--panel2); }
  nav.tabs a.on { color: var(--fg); background: var(--panel2); box-shadow: inset 0 -2px 0 var(--accent); }
  a.btn { display: inline-block; background: var(--panel2); color: var(--fg); border: 1px solid var(--line);
          border-radius: 8px; padding: 7px 13px; font-size: 14px; word-break: normal; }
  a.btn:hover { border-color: var(--accent); }
  a.btn.primary { background: var(--accent); color: #06131c; border-color: transparent; font-weight: 600; }
  .banner { display: flex; align-items: center; gap: 12px; flex-wrap: wrap; padding: 14px 18px; border-radius: 14px;
            border: 1px solid var(--warn); background: rgba(210,153,34,.08); }
  .banner .btext { flex: 1; min-width: 220px; }
  .banner strong { display: block; }
)CSS"

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
)PAGE" WEB_BASE_CSS R"PAGE(  .hero {
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
  .tbtn.skip svg { width: 18px; height: 18px; }
  .deck { display: none; flex-basis: 100%; align-items: stretch; gap: 14px; flex-wrap: wrap;
          padding-top: 16px; border-top: 1px solid var(--line); }
  .deck.show { display: flex; }
  .presets { flex: 1; min-width: 300px; display: grid; grid-template-columns: repeat(6, minmax(0, 1fr)); gap: 8px; }
  .pbtn { display: flex; flex-direction: column; align-items: flex-start; gap: 1px; min-width: 0; padding: 8px 10px;
          border-radius: 10px; text-align: left; user-select: none; -webkit-user-select: none; touch-action: manipulation; }
  .pbtn .pnum { font-size: 17px; font-weight: 700; line-height: 1.15; }
  .pbtn .pname { max-width: 100%; font-size: 12px; color: var(--muted); overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
  .pbtn.empty .pnum, .pbtn.empty .pname { opacity: .5; }
  .pbtn:active { background: var(--line); }
  .sources { display: flex; gap: 8px; }
  .sbtn { display: flex; flex-direction: column; align-items: center; justify-content: center; gap: 4px; min-width: 84px;
          padding: 8px 12px; border-radius: 10px; font-size: 12px; color: var(--muted);
          user-select: none; -webkit-user-select: none; touch-action: manipulation; }
  .sbtn svg { width: 22px; height: 22px; fill: none; stroke: currentColor; stroke-width: 1.9; stroke-linecap: round; stroke-linejoin: round; }
  .pbtn.on, .sbtn.on { border-color: var(--accent); background: rgba(76,194,255,.12); }
  .pbtn.on .pnum, .sbtn.on { color: var(--accent); }
  .pbtn.busy, .sbtn.busy { animation: pulse 1s ease-in-out infinite; }
  .pbtn.bad, .sbtn.bad { box-shadow: 0 0 0 3px rgba(248,81,73,.6); }
  .pentry { display: flex; flex-direction: column; justify-content: center; gap: 4px; width: 136px; }
  .pentry input { width: 100%; height: 32px; padding: 0 8px; text-align: center; font: inherit; font-size: 16px; font-weight: 650;
                  letter-spacing: .14em; font-variant-numeric: tabular-nums; color: var(--fg); background: var(--panel2);
                  border: 1px solid var(--line); border-radius: 8px; }
  .pentry input::placeholder { color: var(--muted); font-weight: 400; letter-spacing: normal; }
  .pentry input:focus { outline: none; border-color: var(--accent); }
  .pentry.ok input { border-color: var(--ok); }
  .pentry.bad input { border-color: #f85149; }
  .pentry.busy input { animation: pulse 1s ease-in-out infinite; }
  .pcount { display: block; height: 2px; width: 0; border-radius: 2px; background: var(--accent); }
  .pcount.run { animation-name: countdown; animation-timing-function: linear; animation-fill-mode: forwards; }
  @keyframes countdown { from { width: 100%; } to { width: 0; } }
  .phint { font-size: 11.5px; line-height: 1.25; color: var(--muted); white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
  .pentry.ok .phint { color: var(--ok); }
  .pentry.bad .phint { color: #f85149; }
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
  table.streams td { vertical-align: middle; }
  td.pcell { width: 1%; padding-right: 2px; }
  .rplay { width: 30px; height: 30px; padding: 0; border-radius: 50%; display: inline-flex; align-items: center;
           justify-content: center; transition: box-shadow .15s, opacity .15s; }
  .rplay svg { width: 14px; height: 14px; fill: currentColor; }
  .rplay .i-pause, .rplay.playing .i-play { display: none; }
  .rplay.playing .i-pause { display: block; }
  .rplay.playing { background: var(--accent); border-color: transparent; color: #06131c; }
  .rplay:disabled { opacity: .3; cursor: default; }
  .rplay:disabled:hover { border-color: var(--line); }
  .rplay.busy, .rplay.busy:disabled { opacity: 1; animation: pulse 1s ease-in-out infinite; }
  .rplay.bad { box-shadow: 0 0 0 3px rgba(248,81,73,.6); }
  tr.current td { background: rgba(76,194,255,.09); }
  tr.current.paused td { background: rgba(76,194,255,.045); }
  tr.current td:first-child { box-shadow: inset 3px 0 0 var(--accent); }
  tr.current .sname { color: var(--accent); font-weight: 600; }
  .desc { font-size: 12px; color: var(--muted); }
  table.editing { table-layout: fixed; min-width: 760px; }
  table.editing col.c-play { width: 44px; }
  table.editing col.c-preset { width: 72px; }
  table.editing col.c-name { width: 17%; }
  table.editing col.c-display { width: 25%; }
  table.editing col.c-del { width: 48px; }
  table.editing td { padding: 6px 6px; }
  table.editing input { width: 100%; height: 30px; padding: 0 8px; font: inherit; font-size: 13.5px; color: var(--fg);
                        background: var(--panel2); border: 1px solid var(--line); border-radius: 7px; }
  table.editing input::placeholder { color: var(--muted); opacity: .7; }
  table.editing input:focus { outline: none; border-color: var(--accent); }
  table.editing input.changed { border-color: rgba(210,153,34,.7); }
  table.editing input.bad { border-color: #f85149; }
  table.editing input.f-name, table.editing input.f-url { font-family: ui-monospace, SFMono-Regular, Menlo, monospace; font-size: 12.5px; }
  table.editing input.f-preset { text-align: center; font-weight: 650; letter-spacing: .08em; }
  table.editing input.f-desc { margin-top: 4px; height: 26px; font-size: 12px; }
  table.editing tr.new td:first-child { box-shadow: inset 3px 0 0 var(--ok); }
  table.editing tr.dirty td:first-child { box-shadow: inset 3px 0 0 var(--warn); }
  table.editing tr.deleted td:first-child { box-shadow: inset 3px 0 0 #f85149; }
  table.editing tr.deleted input { text-decoration: line-through; opacity: .45; }
  .rerr { font-size: 12px; line-height: 1.3; color: #f85149; margin-top: 3px; }
  .rerr:empty { display: none; }
  .rdel { width: 32px; height: 30px; padding: 0; color: var(--muted); }
  .rdel:hover { color: #f85149; border-color: #f85149; }
  tr.deleted .rdel { color: var(--ok); }
  tr.deleted .rdel:hover { border-color: var(--ok); }

</style>
</head>
<body>
<header>
  <a class="brand" href="/"><span id="dot" class="dot"></span><h1>SoundTouch</h1></a>
  <nav class="tabs"><a href="/" class="on" aria-current="page">Dashboard</a><a href="/speakers">Speakers</a></nav>
  <span id="mode" class="pill"></span>
  <span class="spacer"></span>
  <button id="refresh">Refresh</button>
</header>
<main>
  <div class="banner" id="first-run" hidden>
    <div class="btext"><strong>No speaker chosen yet</strong>
      <span class="muted">Find the SoundTouch speakers on your network and pick the one this dashboard controls.</span></div>
    <a class="btn primary" href="/speakers">Find speakers</a>
    <button id="first-run-later" type="button">Not now</button>
  </div>
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
      <button class="tbtn skip" id="np-prev" type="button" aria-label="Previous" title="Previous">
        <svg viewBox="0 0 24 24" aria-hidden="true"><path d="M6 5h2.5v14H6zM19 5.5v13L9.5 12z"/></svg>
      </button>
      <button class="tbtn play" id="np-play" type="button" aria-label="Play or pause" title="Play">
        <svg class="i-play" viewBox="0 0 24 24" aria-hidden="true"><path d="M9 5v14l11-7z"/></svg>
        <svg class="i-pause" viewBox="0 0 24 24" aria-hidden="true"><path d="M7 5h3.5v14H7zM13.5 5H17v14h-3.5z"/></svg>
      </button>
      <button class="tbtn skip" id="np-next" type="button" aria-label="Next" title="Next">
        <svg viewBox="0 0 24 24" aria-hidden="true"><path d="M15.5 5H18v14h-2.5zM5 5.5v13l9.5-6.5z"/></svg>
      </button>
    </div>
    <div class="deck" id="np-deck">
      <div class="presets" id="np-presets"></div>
      <div class="pentry" id="np-pentry">
        <input id="np-pinput" type="text" inputmode="numeric" maxlength="3" autocomplete="off" spellcheck="false"
               placeholder="Preset" aria-label="Preset number" title="Type a preset, such as 111, then Enter">
        <span class="pcount" id="np-pcount"></span>
        <span class="phint" id="np-phint"></span>
      </div>
      <div class="sources">
        <button class="sbtn" id="np-bt" type="button" title="Switch to Bluetooth">
          <svg viewBox="0 0 24 24" aria-hidden="true"><path d="M6.5 6.5l11 11L12 23V1l5.5 5.5-11 11"/></svg>
          <span>Bluetooth</span>
        </button>
        <button class="sbtn" id="np-aux" type="button" title="Switch to AUX">
          <svg viewBox="0 0 24 24" aria-hidden="true"><path d="M12 2v3"/><path d="M10.25 5h3.5v4.5h-3.5z"/><path d="M10.25 7.25h3.5"/><path d="M9 9.5h6V16H9z"/><path d="M12 16v6"/></svg>
          <span>AUX</span>
        </button>
      </div>
    </div>
  </section>

  <div class="grid">
    <div class="card">
      <div class="card-head"><h2>Speaker</h2><span class="spacer"></span><a class="btn" href="/speakers">Manage speakers</a></div>
      <div id="speaker" class="kv"></div>
      <div id="speaker-note" class="muted" style="font-size:12.5px;margin-top:10px" hidden></div>
    </div>
    <div class="card"><h2>Configuration</h2><div id="config" class="kv"></div></div>
  </div>

  <div class="card">
    <div class="card-head">
      <h2>Streams</h2>
      <span class="smsg" id="streams-msg" role="status"></span>
      <button id="st-add" type="button" hidden>+ Add stream</button>
      <button id="st-discard" type="button" hidden>Discard</button>
      <button id="st-save" class="primary" type="button" hidden>Save</button>
      <button id="st-edit" type="button" title="Add, delete and change streams">Edit</button>
    </div>
    <div id="streams" class="tablewrap"></div>
  </div>
</main>

<script>
const $ = (id) => document.getElementById(id);
const esc = (s) => String(s == null ? "" : s).replace(/[&<>"']/g, c =>
  ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));

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

// Presets, the skip keys and the sources: the remote's buttons, and the speaker's own. They reach
// the speaker one after another in the order pressed, so that 1 then 1 makes the combo for preset 11
// as on the remote. A button pulses while a press of it is on its way, and rings red if one was not
// taken; what it led to shows as the speaker reports it.
let remoteQueue = Promise.resolve();
const deck = { source: "", stationPreset: null };

function remoteAction(button, path, body) {
  button.dataset.pending = String(Number(button.dataset.pending || 0) + 1);
  button.classList.add("busy");
  remoteQueue = remoteQueue.then(async () => {
    let ok = false;
    try {
      const r = await fetch(path, { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(body) });
      ok = r.ok;
    } catch (e) { }
    const pending = Number(button.dataset.pending) - 1;
    button.dataset.pending = String(pending);
    if (pending <= 0) button.classList.remove("busy");
    if (!ok) {
      button.classList.add("bad");
      setTimeout(() => button.classList.remove("bad"), 1500);
    }
  });
}

function renderPresets(streams) {
  presetNames = new Map(streams.filter(s => s.preset != null && s.preset > 0).map(s => [s.preset, s.display_name || s.name]));
  let html = "";
  for (let n = 1; n <= 6; n++) {
    const st = streams.find(s => s.preset === n);
    const name = st ? (st.display_name || st.name) : "";
    html += '<button class="pbtn' + (st ? "" : " empty") + '" type="button" data-preset="' + n + '" title="' +
            esc(st ? "Preset " + n + ": " + name : "Preset " + n + " (nothing on it)") + '">' +
            '<span class="pnum">' + n + '</span><span class="pname">' + (st ? esc(name) : "\u2014") + "</span></button>";
  }
  $("np-presets").innerHTML = html;
  renderDeck(deck.source, deck.stationPreset);
  if (!entry.timer) renderEntry();
}

// The preset entry. Type a preset and press Enter, or press the tiles: they collect digits as the
// remote's buttons do, but allow more time between presses than a hand on the remote needs: a mouse
// has further to travel. A press waits KEYPAD_MS for the next (never less than control's own combo
// window), and a combo missed by being too slow would play the wrong station, so it is generous. A
// preset no longer one can follow is played at once; any other when that time is up, or on Enter.
// Either way the whole number goes to /api/select, which presses its buttons well inside control's
// combo window. What is entered is judged as it is entered: a preset with a station on it, the start
// of a longer one, or neither.
const KEYPAD_MS = 1500;
const entry = { digits: "", keypad: false, timer: null };
let comboWindowMs = 700;
let presetNames = new Map();

function keypadWindow() { return Math.max(KEYPAD_MS, comboWindowMs); }

function canGoOn(digits) {
  for (const n of presetNames.keys()) {
    const s = String(n);
    if (s.length > digits.length && s.startsWith(digits)) return true;
  }
  return false;
}

function judge(digits) {
  if (!digits) return { state: "", text: "1\u20136, or a combo such as 111" };
  if (/[^1-6]/.test(digits)) return { state: "bad", text: "Buttons are 1 to 6" };
  const name = presetNames.get(Number(digits));
  if (name) return { state: "ok", text: "\u2192 " + name };
  if (canGoOn(digits)) return { state: "more", text: digits + "\u2026" };
  return { state: "bad", text: "Nothing on preset " + digits };
}

function renderEntry(text, state) {
  const judged = (text == null) ? judge(entry.digits) : { text: text, state: state || "" };
  const input = $("np-pinput");
  if (input.value !== entry.digits) input.value = entry.digits;
  $("np-pentry").classList.toggle("ok", judged.state === "ok");
  $("np-pentry").classList.toggle("bad", judged.state === "bad");
  $("np-phint").textContent = judged.text;
}

function countdown(ms) {
  const bar = $("np-pcount");
  bar.classList.remove("run");
  if (!ms) return;
  void bar.offsetWidth;                  // so that the animation starts over
  bar.style.animationDuration = ms + "ms";
  bar.classList.add("run");
}

function stopWaiting() {
  clearTimeout(entry.timer);
  entry.timer = null;
  countdown(0);
}

function keypad(digit) {
  if (!entry.keypad || entry.digits.length >= 3) entry.digits = "";
  entry.keypad = true;
  entry.digits += digit;
  stopWaiting();
  const judged = judge(entry.digits);
  renderEntry();
  if (judged.state === "ok" && !canGoOn(entry.digits)) { sendPreset(); return; }
  if (judged.state === "ok" || judged.state === "more") {
    countdown(keypadWindow());
    entry.timer = setTimeout(() => {
      entry.timer = null;
      countdown(0);
      if (judge(entry.digits).state === "ok") { sendPreset(); return; }
      renderEntry("Nothing on preset " + entry.digits, "bad");
      entry.keypad = false;
    }, keypadWindow());
    return;
  }
  entry.keypad = false;                  // shown as wrong; the next press starts afresh
}

function sendPreset() {
  stopWaiting();
  const digits = entry.digits;
  entry.keypad = false;
  if (judge(digits).state !== "ok") { renderEntry(); return; }
  const box = $("np-pentry");
  box.classList.add("busy");
  remoteQueue = remoteQueue.then(async () => {
    let answer = {}, ok = false;
    try {
      const r = await fetch("/api/select", { method: "POST", headers: { "Content-Type": "application/json" },
                                             body: JSON.stringify({ preset: Number(digits) }) });
      ok = r.ok;
      answer = await r.json().catch(() => ({}));
    } catch (e) { }
    box.classList.remove("busy");
    if (entry.digits !== digits || entry.timer) return;      // something new was entered meanwhile
    if (ok) {
      entry.digits = "";
      renderEntry("Playing " + (answer.station || presetNames.get(Number(digits)) || digits), "ok");
      setTimeout(() => { if (!entry.digits && !entry.timer) renderEntry(); }, 2500);
    } else {
      renderEntry(answer.error || "The speaker did not take it", "bad");
    }
  });
}

function wireEntry() {
  const input = $("np-pinput");
  input.addEventListener("input", () => {
    stopWaiting();
    entry.keypad = false;
    entry.digits = input.value.replace(/\D/g, "").slice(0, 3);
    renderEntry();
  });
  input.addEventListener("keydown", (e) => {
    if (e.key === "Enter") { e.preventDefault(); sendPreset(); }
    else if (e.key === "Escape") { stopWaiting(); entry.digits = ""; entry.keypad = false; renderEntry(); input.blur(); }
  });
  renderEntry();
}

function renderDeck(source, stationPreset) {
  deck.source = source || "";
  deck.stationPreset = (stationPreset == null) ? null : Number(stationPreset);
  $("np-deck").classList.toggle("show", deck.source !== "");
  for (const b of document.querySelectorAll("#np-presets .pbtn")) {
    b.classList.toggle("on", deck.source === "UPNP" && Number(b.dataset.preset) === deck.stationPreset);
  }
  $("np-bt").classList.toggle("on", deck.source === "BLUETOOTH");
  $("np-aux").classList.toggle("on", deck.source === "AUX");
  const canSkip = deck.source !== "" && deck.source !== "STANDBY" && deck.source !== "AUX" && deck.source !== "INVALID_SOURCE";
  $("np-prev").disabled = !canSkip;
  $("np-next").disabled = !canSkip;
}

function wireDeck() {
  $("np-presets").addEventListener("click", (e) => {
    const b = e.target.closest(".pbtn");
    if (b) keypad(b.dataset.preset);
  });
  wireEntry();
  $("np-prev").addEventListener("click", () => remoteAction($("np-prev"), "/api/skip", { direction: "previous" }));
  $("np-next").addEventListener("click", () => remoteAction($("np-next"), "/api/skip", { direction: "next" }));
  $("np-bt").addEventListener("click", () => remoteAction($("np-bt"), "/api/source", { source: "bluetooth" }));
  $("np-aux").addEventListener("click", () => remoteAction($("np-aux"), "/api/source", { source: "aux" }));
}

// Streams: the list, with a play button beside each stream, and its editor. The button plays a
// stream as pressing its preset would, whether it has a preset or not (/api/play), and pauses and
// resumes the one playing (/api/playback); the stream playing is highlighted. Edit turns the list
// into a form: streams are added, deleted and changed there, and nothing is sent until Save, which
// sends the whole list at once (PUT /api/streams). It goes with the ETag the list was read with, so
// a change made meanwhile elsewhere is refused (412) rather than overwritten unseen.
const PLAY_ICONS = '<svg class="i-play" viewBox="0 0 24 24" aria-hidden="true"><path d="M8 5v14l11-7z"/></svg>' +
                   '<svg class="i-pause" viewBox="0 0 24 24" aria-hidden="true"><path d="M7 5h3.5v14H7zM13.5 5H17v14h-3.5z"/></svg>';
const FIELDS = ["preset", "name", "display_name", "description", "url"];
const FIELD_LABELS = { preset: "Preset", name: "Name", display_name: "Display name", description: "Description", url: "URL" };
const sp = { now: { name: "", source: "", status: "" }, busy: new Set() };
const ed = { on: false, saved: [], etag: null, rows: [], nextId: 1, showAll: false, saving: false,
             discardArmed: null, emptyArmed: null, msgTimer: null };
let embedded = false;

async function getStreams() {
  const r = await fetch("/api/streams", { cache: "no-store" });
  if (!r.ok) throw new Error("/api/streams -> " + r.status);
  return { list: await r.json(), etag: r.headers.get("ETag") };
}

function streamMsg(text, state, fadeMs) {
  const m = $("streams-msg");
  clearTimeout(ed.msgTimer);
  m.className = "smsg" + (state ? " " + state : "");
  m.textContent = text || "";
  if (fadeMs) ed.msgTimer = setTimeout(() => streamMsg(""), fadeMs);
}

function nowOf(name) {
  const n = sp.now;
  const current = !!name && n.source === "UPNP" && n.name === name;
  return { current: current,
           playing: current && (n.status === "PLAY_STATE" || n.status === "BUFFERING_STATE"),
           paused: current && n.status === "PAUSE_STATE" };
}

// Marks the stream playing, and sets each play button to what a click on it would do.
function renderStreamPlay() {
  for (const tr of document.querySelectorAll("#streams tr[data-id]")) {
    const name = tr.dataset.name || "";
    const s = nowOf(name);
    const b = tr.querySelector(".rplay");
    tr.classList.toggle("current", s.current);
    tr.classList.toggle("paused", s.paused);
    if (!b) continue;
    const locked = !name || tr.dataset.locked === "1";
    const label = tr.dataset.label || name;
    b.classList.toggle("playing", s.playing);
    b.classList.toggle("busy", sp.busy.has(name));
    b.disabled = locked && !s.current;
    b.title = (locked && !s.current) ? "Save first to play this"
            : s.playing ? "Pause " + label : s.paused ? "Resume " + label : "Play " + label;
    b.setAttribute("aria-label", b.title);
  }
}

async function streamPlay(tr) {
  const name = tr.dataset.name;
  if (!name || sp.busy.has(name)) return;
  const s = nowOf(name);
  const [path, body] = s.playing ? ["/api/playback", { action: "pause" }]
                     : s.paused ? ["/api/playback", { action: "play" }]
                     : ["/api/play", { stream: name }];
  sp.busy.add(name);
  renderStreamPlay();
  let ok = false, answer = {};
  try {
    const r = await fetch(path, { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(body) });
    ok = r.ok;
    answer = await r.json().catch(() => ({}));
  } catch (e) { }
  sp.busy.delete(name);
  renderStreamPlay();
  if (!ok) {
    const b = document.querySelector('#streams tr[data-name="' + CSS.escape(name) + '"] .rplay');
    if (b) {
      b.classList.add("bad");
      setTimeout(() => b.classList.remove("bad"), 1500);
    }
    streamMsg(answer.error || "The speaker did not take it", "bad", 6000);
  }
  await loadNowPlaying();
}

function streamLink(url) {
  return /^https?:\/\//i.test(url || "")
    ? '<a href="' + esc(url) + '" target="_blank" rel="noopener noreferrer">' + esc(url) + "</a>"
    : esc(url);
}

function renderStreams() {
  if (ed.on) { renderEditor(); return; }
  let s = '<table class="streams"><tr><th></th><th>Preset</th><th>Name</th><th>Display</th><th>URL</th></tr>';
  ed.saved.forEach((st, i) => {
    const label = st.display_name || st.name;
    s += '<tr data-id="v' + i + '" data-name="' + esc(st.name) + '" data-label="' + esc(label) + '">' +
         '<td class="pcell"><button class="rplay" type="button">' + PLAY_ICONS + "</button></td>" +
         "<td>" + (st.preset == null ? '<span class="muted">\u2014</span>' : esc(st.preset)) + "</td>" +
         "<td><code>" + esc(st.name) + "</code></td>" +
         '<td><span class="sname">' + esc(label) + "</span>" +
           (st.description && st.description !== st.display_name ? '<div class="desc">' + esc(st.description) + "</div>" : "") + "</td>" +
         "<td>" + streamLink(st.url) + "</td></tr>";
  });
  if (!ed.saved.length) s += '<tr><td colspan="5" class="muted">No streams yet \u2014 Edit, then Add stream</td></tr>';
  $("streams").innerHTML = s + "</table>";
  renderStreamPlay();
  renderStreamHead();
}

// The editor. Each row holds its stream's fields as typed, and the stream as saved (orig, null for
// a new one); nothing is sent until Save. A field is checked as it is typed, by the server's rules;
// a new stream's empty fields are only called out once left, or at Save.
function fieldsOf(item) {
  return { preset: item.preset == null ? "" : String(item.preset), name: item.name || "",
           display_name: item.display_name || "", description: item.description || "", url: item.url || "" };
}

function normalized(f) {
  const preset = f.preset.trim();
  return { name: f.name.trim(), display_name: f.display_name.trim(), description: f.description.trim(),
           url: f.url.trim(), preset: preset === "" ? null : Number(preset) };
}

function fieldChanged(row, key) {
  return !row.orig || normalized(row.f)[key] !== normalized(fieldsOf(row.orig))[key];
}

function rowChanged(row) {
  return !row.orig || row.deleted || FIELDS.some(k => fieldChanged(row, k));
}

function changeCount() {
  return ed.rows.filter(rowChanged).length;
}

function urlOk(url) {
  const m = /^https?:\/\/([^/?#]*)/i.exec(url);
  if (!m || /[\s\x00-\x1f\x7f]/.test(url) || url.length > 2048) return false;
  const host = m[1].slice(m[1].lastIndexOf("@") + 1);
  return host !== "" && host[0] !== ":";
}

// What is wrong with each stream kept, by row id: { field: message }.
function problemsOf() {
  const out = new Map(), names = new Map(), presets = new Map();
  for (const row of ed.rows) {
    if (row.deleted) continue;
    const v = normalized(row.f), p = {}, preset = row.f.preset.trim();
    if (!v.name) p.name = "a name is needed";
    else if (!/^[A-Za-z0-9._-]{1,64}$/.test(v.name)) p.name = "letters, digits, '.', '_' and '-' only";
    else names.set(v.name, (names.get(v.name) || []).concat(row));
    if (!v.url) p.url = "a URL is needed";
    else if (!urlOk(v.url)) p.url = "an http:// or https:// address, with no spaces in it";
    if (preset && !/^[1-6]{1,3}$/.test(preset)) p.preset = "buttons 1 to 6, up to three of them, such as 1, 13 or 111";
    else if (preset) presets.set(preset, (presets.get(preset) || []).concat(row));
    out.set(row.id, p);
  }
  for (const [name, rows] of names) if (rows.length > 1) for (const row of rows) out.get(row.id).name = "another stream is called " + name;
  for (const [preset, rows] of presets) if (rows.length > 1) for (const row of rows) out.get(row.id).preset = "another stream is on preset " + preset;
  for (const row of ed.rows) {
    if (row.deleted) continue;
    for (const [key, message] of Object.entries(row.server)) if (!out.get(row.id)[key]) out.get(row.id)[key] = message;
  }
  return out;
}

function rowOf(element) {
  const tr = element.closest("tr[data-id]");
  return tr ? ed.rows.find(r => String(r.id) === tr.dataset.id) : null;
}

function newRow(item) {
  return { id: ed.nextId++, orig: item || null, deleted: false, f: item ? fieldsOf(item) : fieldsOf({}),
           touched: new Set(), server: {} };
}

function renderEditor() {
  let s = '<table class="streams editing"><colgroup><col class="c-play"><col class="c-preset"><col class="c-name">' +
          '<col class="c-display"><col><col class="c-del"></colgroup>' +
          "<tr><th></th><th>Preset</th><th>Name</th><th>Display name</th><th>URL</th><th></th></tr>";
  const input = (field, value, extra) =>
    '<input data-f="' + field + '" value="' + esc(value) + '" aria-label="' + FIELD_LABELS[field] + '" ' + extra + ">";
  for (const row of ed.rows) {
    const o = row.orig;
    s += '<tr data-id="' + row.id + '"' +
         (o ? ' data-name="' + esc(o.name) + '" data-label="' + esc(o.display_name || o.name) + '"' : "") + ">" +
         '<td class="pcell"><button class="rplay" type="button">' + PLAY_ICONS + "</button></td>" +
         "<td>" + input("preset", row.f.preset, 'class="f-preset" inputmode="numeric" maxlength="3" autocomplete="off" placeholder="\u2014"') + "</td>" +
         "<td>" + input("name", row.f.name, 'class="f-name" maxlength="64" autocomplete="off" spellcheck="false" placeholder="name"') + "</td>" +
         "<td>" + input("display_name", row.f.display_name, 'maxlength="100" placeholder="Display name"') +
                  input("description", row.f.description, 'class="f-desc" maxlength="500" placeholder="Description"') + "</td>" +
         "<td>" + input("url", row.f.url, 'class="f-url" maxlength="2048" autocomplete="off" spellcheck="false" placeholder="http://\u2026"') +
                  '<div class="rerr"></div></td>' +
         '<td><button class="rdel" type="button"></button></td></tr>';
  }
  if (!ed.rows.length) s += '<tr><td colspan="6" class="muted">No streams \u2014 Add stream makes one</td></tr>';
  $("streams").innerHTML = s + "</table>";
  refreshEditor();
}

// Brings the editor's marks up to date with what has been typed, without redrawing its fields.
function refreshEditor() {
  const problems = problemsOf();
  for (const tr of document.querySelectorAll("#streams tr[data-id]")) {
    const row = rowOf(tr);
    if (!row) continue;
    const p = problems.get(row.id) || {}, shown = [];
    const changed = rowChanged(row);
    tr.classList.toggle("new", !row.orig);
    tr.classList.toggle("dirty", !!row.orig && !row.deleted && changed);
    tr.classList.toggle("deleted", row.deleted);
    tr.dataset.locked = changed ? "1" : "";
    for (const field of tr.querySelectorAll("input[data-f]")) {
      const key = field.dataset.f;
      const show = !row.deleted && !!p[key] && (!!row.orig || ed.showAll || row.touched.has(key));
      field.disabled = row.deleted;
      field.classList.toggle("changed", !!row.orig && !row.deleted && fieldChanged(row, key));
      field.classList.toggle("bad", show);
      field.title = show ? p[key] : "";
      if (show) shown.push(FIELD_LABELS[key] + ": " + p[key]);
    }
    tr.querySelector(".rerr").textContent = shown.join("  \u00b7  ");
    const del = tr.querySelector(".rdel");
    del.textContent = row.deleted ? "\u21ba" : "\u2715";
    del.title = row.deleted ? "Keep this stream" : row.orig ? "Delete this stream" : "Remove this new stream";
    del.setAttribute("aria-label", del.title);
  }
  renderStreamHead();
  renderStreamPlay();
}

function renderStreamHead() {
  const on = ed.on;
  $("st-edit").hidden = on;
  $("st-add").hidden = !on;
  $("st-discard").hidden = !on;
  $("st-save").hidden = !on;
  if (!on) return;
  const n = changeCount(), changes = n + (n === 1 ? " change" : " changes");
  const save = $("st-save"), discard = $("st-discard");
  save.disabled = ed.saving || n === 0;
  save.textContent = ed.saving ? "Saving\u2026" : ed.emptyArmed ? "Delete them all?" : n ? "Save " + changes : "Save";
  save.classList.toggle("warn", !!ed.emptyArmed);
  discard.disabled = ed.saving;
  discard.textContent = ed.discardArmed ? "Discard " + changes + "?" : n ? "Discard" : "Done";
  discard.classList.toggle("warn", !!ed.discardArmed);
}

// Asks for a second click within 3 s, by the given flag, before something that loses work.
function armed(flag) {
  if (ed[flag]) { clearTimeout(ed[flag]); ed[flag] = null; return true; }
  ed[flag] = setTimeout(() => { ed[flag] = null; renderStreamHead(); }, 3000);
  renderStreamHead();
  return false;
}

async function startEditing() {
  const button = $("st-edit");
  button.disabled = true;
  try {
    // The latest list, so the save is checked against what is really there now.
    const { list, etag } = await getStreams();
    ed.saved = list;
    ed.etag = etag;
    renderPresets(list);
  } catch (e) {
    button.disabled = false;
    streamMsg("Could not load the streams to edit", "bad", 6000);
    return;
  }
  button.disabled = false;
  ed.on = true;
  ed.showAll = false;
  ed.rows = ed.saved.map(newRow);
  streamMsg("");
  renderStreams();
}

function stopEditing() {
  for (const flag of ["discardArmed", "emptyArmed"]) { clearTimeout(ed[flag]); ed[flag] = null; }
  ed.on = false;
  ed.rows = [];
  ed.showAll = false;
  renderStreams();
}

function discardEdits() {
  if (changeCount() && !armed("discardArmed")) return;
  streamMsg("");
  stopEditing();
}

function addStream() {
  const row = newRow(null);
  ed.rows.push(row);
  renderEditor();
  const name = document.querySelector('#streams tr[data-id="' + row.id + '"] input[data-f="name"]');
  if (name) { name.focus(); name.scrollIntoView({ block: "nearest" }); }
}

function deleteRow(row) {
  if (row.orig) { row.deleted = !row.deleted; refreshEditor(); return; }
  ed.rows = ed.rows.filter(r => r !== row);
  renderEditor();
}

async function saveStreams() {
  if (ed.saving || !changeCount()) return;
  const problems = problemsOf();
  const bad = ed.rows.find(r => !r.deleted && Object.keys(problems.get(r.id) || {}).length);
  if (bad) {
    ed.showAll = true;
    refreshEditor();
    const field = document.querySelector('#streams tr[data-id="' + bad.id + '"] input.bad');
    if (field) field.focus();
    streamMsg("Fix the fields marked in red, then save", "bad");
    return;
  }
  const kept = ed.rows.filter(r => !r.deleted);
  if (!kept.length && ed.saved.length && !armed("emptyArmed")) {
    streamMsg("That deletes every stream: click again to save it", "bad", 3000);
    return;
  }
  ed.saving = true;
  renderStreamHead();
  streamMsg("Saving\u2026");
  let r = null, answer = {};
  try {
    const headers = { "Content-Type": "application/json" };
    if (ed.etag) headers["If-Match"] = ed.etag;
    r = await fetch("/api/streams", { method: "PUT", headers: headers,
                                      body: JSON.stringify({ streams: kept.map(row => normalized(row.f)) }) });
    answer = await r.json().catch(() => ({}));
  } catch (e) { }
  ed.saving = false;
  if (r && r.ok) {
    ed.saved = Array.isArray(answer.streams) ? answer.streams : [];
    ed.etag = r.headers.get("ETag");
    stopEditing();
    streamMsg(!answer.changed ? "Nothing to save" : embedded ? "Saved, and in use"
              : "Saved to streams.json; a control running elsewhere uses it once restarted", "ok", 8000);
    renderPresets(ed.saved);
    return;
  }
  if (r && r.status === 400 && Array.isArray(answer.problems)) {
    for (const p of answer.problems) {
      const row = kept[p.index];
      if (row && FIELDS.includes(p.field)) row.server[p.field] = p.error;
    }
    ed.showAll = true;
    refreshEditor();
    streamMsg(answer.error || "Some streams need fixing", "bad");
  } else if (r && r.status === 412) {
    renderStreamHead();
    const m = $("streams-msg");
    streamMsg("The streams were changed elsewhere since you began. ", "bad");
    const reload = document.createElement("button");
    reload.type = "button";
    reload.textContent = "Load them (drops your changes)";
    reload.addEventListener("click", () => { stopEditing(); streamMsg(""); loadStatic(); });
    m.appendChild(reload);
  } else {
    renderStreamHead();
    streamMsg("Not saved: " + (answer.error || "no answer from the server"), "bad");
  }
}

function wireStreams() {
  const box = $("streams");
  box.addEventListener("click", (e) => {
    const play = e.target.closest(".rplay");
    if (play) { streamPlay(play.closest("tr")); return; }
    const del = e.target.closest(".rdel");
    const row = del && rowOf(del);
    if (row) deleteRow(row);
  });
  box.addEventListener("input", (e) => {
    const field = e.target.closest("input[data-f]");
    const row = field && rowOf(field);
    if (!row) return;
    const key = field.dataset.f;
    if (key === "preset" && /\D/.test(field.value)) field.value = field.value.replace(/\D/g, "");
    row.f[key] = field.value;
    delete row.server[key];
    if (ed.emptyArmed) { clearTimeout(ed.emptyArmed); ed.emptyArmed = null; }
    refreshEditor();
  });
  box.addEventListener("focusout", (e) => {
    const field = e.target.closest("input[data-f]");
    const row = field && rowOf(field);
    if (row && !row.touched.has(field.dataset.f)) { row.touched.add(field.dataset.f); refreshEditor(); }
  });
  $("st-edit").addEventListener("click", startEditing);
  $("st-add").addEventListener("click", addStream);
  $("st-discard").addEventListener("click", discardEdits);
  $("st-save").addEventListener("click", saveStreams);
  window.addEventListener("beforeunload", (e) => {
    if (ed.on && changeCount()) { e.preventDefault(); e.returnValue = ""; }
  });
}

// What to call a source when no station of control's is playing on it.
const SOURCE_NAMES = { STANDBY: "Standby", BLUETOOTH: "Bluetooth", AUX: "AUX", INVALID_SOURCE: "Idle", UPNP: "UPnP" };

function renderNowPlaying(np) {
  const standby = np.source === "STANDBY";
  const station = standby ? "Standby" : (np.station || SOURCE_NAMES[np.source] || np.source || "\u2014");
  $("np-station").textContent = station;
  $("np-song").textContent = standby ? "" : (np.title || "");
  const bits = [];
  if (np.status) bits.push(np.status);
  if (np.source && !standby && np.source !== station) bits.push(np.source);
  if (np.relay_running != null) bits.push(np.relay_running ? "relay on" : "relay off");
  $("np-meta").textContent = bits.join("  \u00b7  ");
  renderVolume(np.volume, np.muted);
  renderTransport(np.source, np.status);
  renderDeck(np.source, np.station_preset);
  sp.now = { name: np.station_name || "", source: np.source || "", status: np.status || "" };
  renderStreamPlay();
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

// The speaker this dashboard drives; choosing one is the Speakers page's job.
function renderSpeaker(sp) {
  const def = (sp.speakers || []).find(x => x.default);
  $("speaker").innerHTML = def
    ? "<div>Name</div><div>" + esc(def.name || "\u2014") + "</div>" +
      "<div>IP</div><div><code>" + esc(def.ip) + "</code></div>" +
      (def.type ? "<div>Model</div><div>" + esc(def.type) + "</div>" : "") +
      "<div>ID</div><div><code>" + esc(def.id) + "</code></div>"
    : "<div class='muted'>none</div><div><a href='/speakers'>Find speakers</a></div>";

  // Control picked its speaker when it started, so a different default only takes over on its next start.
  const note = $("speaker-note");
  const switching = sp.embedded && def && sp.active_ip && def.ip !== sp.active_ip;
  note.hidden = !switching;
  if (switching) note.textContent = "Controlling " + sp.active_ip + " until control restarts.";

  $("first-run").hidden = !!sp.has_default || sessionStorage.getItem("cxstcc-first-run-later") === "1";
}

async function loadStatic() {
  try {
    const [streams, speakers, config, live] = await Promise.all([
      getStreams(), get("/api/speakers"), get("/api/config"),
      get("/api/live").catch(() => ({}))
    ]);

    $("mode").textContent = config.embedded ? "control --web" : "web";
    $("mode").className = "pill" + (config.embedded ? " on" : "");
    embedded = !!config.embedded;

    comboWindowMs = Number(config.combo_window_ms) || 700;
    renderPresets(streams.list);

    // The streams, unless they are being edited: the edit carries on against the list it began with.
    if (!ed.on) {
      ed.saved = streams.list;
      ed.etag = streams.etag;
      renderStreams();
    }

    renderSpeaker(speakers);

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
      "<div>Combo window</div><div>" + esc(c.combo_window_ms) + " ms</div>" +
      "<div>Version</div><div><code>" + esc(c.version) + "</code></div>";

    setOnline(true);
  } catch (e) { setOnline(false); }
}

$("refresh").onclick = () => { loadStatic(); loadNowPlaying(); };
$("first-run-later").onclick = () => { sessionStorage.setItem("cxstcc-first-run-later", "1"); $("first-run").hidden = true; };
wireVolume();
wireTransport();
wireDeck();
wireStreams();
loadStatic();
followLive().then(() => { loadNowPlaying(); setInterval(loadNowPlaying, 3000); });
</script>
</body>
</html>
)PAGE";

// The Speakers page, at "/speakers": finds the SoundTouch speakers on the network, keeps looking while it is
// open, and picks the default one. Speakers live here so groups of them can join later.
inline const char *SPEAKERS_HTML = R"PAGE(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Speakers &middot; SoundTouch</title>
<style>
)PAGE" WEB_BASE_CSS R"PAGE(
  .search { display: flex; align-items: center; gap: 8px; font-size: 13px; color: var(--muted); }
  .spin { width: 12px; height: 12px; border-radius: 50%; border: 2px solid var(--line); border-top-color: var(--accent); }
  .spin.on { animation: spin .8s linear infinite; }
  @keyframes spin { to { transform: rotate(360deg); } }
  td.st { width: 1%; white-space: nowrap; padding-right: 0; }
  td.st .dot { display: inline-block; margin-top: 7px; }
  td.act { width: 1%; white-space: nowrap; text-align: right; }
  .sname { font-weight: 600; }
  .sname .pill { margin-left: 6px; font-weight: 400; }
  .pill.new { color: var(--ok); border-color: rgba(63,185,80,.55); }
  .seen { font-size: 12px; color: var(--muted); }
  .moved { font-size: 12px; color: var(--warn); }
  tr.default td:first-child { box-shadow: inset 3px 0 0 var(--accent); }
  tr.gone td { opacity: .6; }
  td.empty { color: var(--muted); padding: 22px 10px; text-align: center; }
  .notice { font-size: 13.5px; padding: 10px 14px; border-radius: 10px; border: 1px solid var(--warn);
            background: rgba(210,153,34,.08); }
  .intro { margin: 0; color: var(--muted); font-size: 14px; }
</style>
</head>
<body>
<header>
  <a class="brand" href="/" title="Back to the dashboard"><span id="dot" class="dot"></span><h1>SoundTouch</h1></a>
  <nav class="tabs"><a href="/" id="to-dashboard">Dashboard</a><a href="/speakers" class="on" aria-current="page">Speakers</a></nav>
  <span id="mode" class="pill"></span>
  <span class="spacer"></span>
  <a class="btn" href="/">&larr; Dashboard</a>
</header>
<main>
  <p class="intro">Speakers on your network show up here while this page is open. The default speaker is the one the dashboard and the commands control.</p>

  <div class="notice" id="restart-note" hidden></div>

  <div class="card">
    <div class="card-head">
      <h2>Speakers</h2>
      <span class="smsg" id="msg" role="status"></span>
      <span class="search" id="search"><span class="spin" id="spin"></span><span id="search-text">Starting search&hellip;</span></span>
    </div>
    <div id="speakers" class="tablewrap"></div>
  </div>

  <div class="card">
    <h2>Groups</h2>
    <div class="muted" id="groups">Grouping speakers to play to several at once is coming later.</div>
  </div>
</main>

<script>
const $ = (id) => document.getElementById(id);
const esc = (s) => String(s == null ? "" : s).replace(/[&<>"']/g, c =>
  ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));

const POLL_MS = 2000;
let last = null;      // the latest /api/speakers answer
let busy = false;     // a "Make default" is in flight
let timer = 0;

async function call(method, path, body) {
  const opt = { method, headers: {} };
  if (body !== undefined) { opt.headers["Content-Type"] = "application/json"; opt.body = JSON.stringify(body); }
  const r = await fetch(path, opt);
  let data = {};
  try { data = await r.json(); } catch (e) { /* not JSON */ }
  if (!r.ok) throw Object.assign(new Error(data.error || ("HTTP " + r.status)), { data });
  return data;
}

function setOnline(ok) { $("dot").className = "dot " + (ok ? "ok" : "bad"); }

function msg(text, kind, html) {
  const m = $("msg");
  m.className = "smsg" + (kind ? " " + kind : "");
  if (html) m.innerHTML = html; else m.textContent = text || "";
}

function ago(ms) {
  if (ms == null) return "";
  const s = Math.round(ms / 1000);
  if (s < 5) return "just now";
  if (s < 60) return s + " s ago";
  return Math.round(s / 60) + " min ago";
}

function renderSearch(d) {
  const q = d.discovery || {};
  $("spin").className = "spin" + (q.active ? " on" : "");
  let t;
  if (!q.active) t = "Search paused";
  else if (!q.passes) t = "Searching\u2026";
  else t = "Searching \u00b7 " + q.passes + (q.passes === 1 ? " pass" : " passes");
  $("search-text").textContent = t;
}

function status(sp) {
  if (sp.online === true) return '<span class="dot ok" title="Answering"></span>';
  if (sp.online === false) return '<span class="dot bad" title="Not found lately"></span>';
  return '<span class="dot" title="Not looked for yet"></span>';
}

function render(d) {
  last = d;
  $("mode").textContent = d.embedded ? "control --web" : "web";
  $("mode").className = "pill" + (d.embedded ? " on" : "");
  renderSearch(d);

  const list = d.speakers || [];
  let h = "<table><tr><th></th><th>Name</th><th>IP</th><th>Model</th><th>ID</th><th></th></tr>";
  for (const sp of list) {
    const pills = (sp.default ? '<span class="pill on">default</span>' : "") +
                  (sp.active ? '<span class="pill">in use</span>' : "") +
                  (sp.saved ? "" : '<span class="pill new">new</span>');
    let seen = "";
    if (sp.online === true) seen = "online";
    else if (sp.online === false) seen = sp.last_seen_ms != null ? "last seen " + ago(sp.last_seen_ms) : "not found";
    const act = sp.default
      ? '<span class="muted" style="font-size:13px">Default</span>'
      : !sp.id
      ? '<span class="muted" style="font-size:13px" title="It did not say who it is">Unknown</span>'
      : '<button type="button" class="primary" data-id="' + esc(sp.id) + '"' + (busy ? " disabled" : "") + ">Make default</button>";
    h += '<tr class="' + (sp.default ? "default" : "") + (sp.online === false ? " gone" : "") + '" data-row="' + esc(sp.id) + '">' +
         '<td class="st">' + status(sp) + "</td>" +
         '<td><div class="sname">' + esc(sp.name || "\u2014") + pills + "</div>" + (seen ? '<div class="seen">' + esc(seen) + "</div>" : "") + "</td>" +
         "<td><code>" + esc(sp.ip) + "</code>" +
           (sp.saved_ip ? '<div class="moved">was <code>' + esc(sp.saved_ip) + "</code></div>" : "") + "</td>" +
         "<td>" + esc(sp.type || "\u2014") + "</td>" +
         "<td><code>" + esc(sp.id) + "</code></td>" +
         '<td class="act">' + act + "</td></tr>";
  }
  if (!list.length) {
    const passes = (d.discovery || {}).passes || 0;
    h += '<tr><td colspan="6" class="empty">' + (passes
      ? "No speakers found yet. Make sure they are switched on and on the same network as this computer."
      : "Looking for speakers\u2026") + "</td></tr>";
  }
  $("speakers").innerHTML = h + "</table>";

  // Control chose its speaker when it started; a new default waits for its next start.
  const def = list.find(x => x.default);
  const note = $("restart-note");
  const pending = d.embedded && def && d.active_ip && def.ip !== d.active_ip;
  note.hidden = !pending;
  if (pending) note.innerHTML = "Control is still driving <code>" + esc(d.active_ip) + "</code>. Restart it to switch to <b>" +
                                esc(def.name || def.ip) + "</b>.";
}

async function makeDefault(id) {
  if (busy) return;
  busy = true;
  if (last) render(last);
  const sp = ((last && last.speakers) || []).find(x => x.id === id) || { id };
  msg("Setting " + (sp.name || id) + " as the default\u2026");
  try {
    const d = await call("PUT", "/api/speakers/default", { id });
    busy = false;
    render(d);
    const name = esc(sp.name || sp.ip || id);
    msg("", "ok", name + " is now the default speaker" + (d.restart_needed ? " (after control restarts)" : "") +
        '. <a href="/">Back to the dashboard</a>');
  } catch (e) {
    busy = false;
    if (e.data && e.data.speakers) render(e.data); else if (last) render(last);
    msg("Could not set the default: " + e.message, "bad");
  }
}

// Each poll also keeps the speaker's search going: it runs while someone is looking at this page and
// winds down shortly after the page is closed or hidden.
async function poll() {
  clearTimeout(timer);
  try {
    const d = document.visibilityState === "visible"
      ? await call("POST", "/api/speakers/discover", {})
      : await call("GET", "/api/speakers");
    setOnline(true);
    if (!busy) render(d);
  } catch (e) {
    setOnline(false);
    $("spin").className = "spin";
    $("search-text").textContent = "Cannot reach the dashboard";
  }
  timer = setTimeout(poll, POLL_MS);
}

$("speakers").addEventListener("click", (ev) => {
  const b = ev.target.closest("button[data-id]");
  if (b) makeDefault(b.dataset.id);
});
document.addEventListener("visibilitychange", () => { if (document.visibilityState === "visible") poll(); });
document.addEventListener("keydown", (ev) => {
  if (ev.key === "Escape" && !ev.target.closest("input, textarea")) location.href = "/";
});
poll();
</script>
</body>
</html>
)PAGE";

#endif
