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
  select, input[type="text"], input[type="number"] {
    background: var(--panel2); color: var(--fg); border: 1px solid var(--line);
    border-radius: 8px; padding: 7px 9px; font: inherit; max-width: 100%;
  }
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
  @keyframes ip-glow-pulse {
    0%, 100% { box-shadow: 0 0 0 0 rgba(210, 153, 34, 0); border-color: var(--line); }
    50% { box-shadow: 0 0 12px 2px rgba(210, 153, 34, 0.7); border-color: var(--warn); }
  }
  .ip-changed-flash {
    animation: ip-glow-pulse 1.8s ease-in-out infinite;
    background: rgba(210, 153, 34, 0.08) !important;
  }
  .info-circle-btn {
    display: inline-flex; align-items: center; justify-content: center;
    width: 20px; height: 20px; min-width: 20px; min-height: 20px;
    padding: 0; margin-left: 6px; border-radius: 50%;
    background: #d29922; color: #0b0e14; font-weight: bold; font-size: 13px; line-height: 1;
    border: none; cursor: pointer; vertical-align: middle;
    box-shadow: 0 0 4px rgba(210, 153, 34, 0.5);
    transition: transform .15s, background .15s;
  }
  .info-circle-btn:hover {
    background: #f0b429; transform: scale(1.15);
  }
  .ip-modal-overlay {
    position: fixed; inset: 0; background: rgba(0,0,0,0.7); backdrop-filter: blur(4px);
    display: flex; align-items: center; justify-content: center; z-index: 9999;
  }
  .ip-modal {
    background: var(--panel); border: 1px solid var(--line); border-radius: 12px;
    padding: 20px; max-width: 440px; width: 90%; box-shadow: 0 10px 30px rgba(0,0,0,0.5);
  }
  .ip-modal h3 { margin: 0 0 12px; font-size: 16px; display: flex; align-items: center; gap: 8px; }
  .ip-modal .ip-kv { display: grid; grid-template-columns: auto 1fr; gap: 8px 14px; font-size: 13.5px; margin-bottom: 16px; }
  .ip-modal .ip-kv div:nth-child(odd) { color: var(--muted); }
  .ip-modal .modal-actions { display: flex; justify-content: flex-end; gap: 10px; }
  .target-star-btn {
    display: inline-flex; align-items: center; justify-content: center;
    width: 34px; height: 34px; padding: 0; border-radius: 8px;
    background: var(--panel2); border: 1px solid var(--line);
    color: var(--muted); cursor: pointer; transition: color .15s, border-color .15s, transform .1s;
  }
  .target-star-btn svg { width: 18px; height: 18px; fill: none; stroke: currentColor; stroke-width: 1.8; stroke-linecap: round; stroke-linejoin: round; }
  .target-star-btn:hover:not(:disabled) { border-color: var(--accent); color: var(--accent); }
  .target-star-btn.is-default { color: var(--accent); border-color: rgba(76,194,255,.45); background: rgba(76,194,255,.12); }
  .target-star-btn.is-default svg { fill: currentColor; }
  .target-star-btn:disabled { opacity: .4; cursor: default; }
  #play-target { color: var(--accent) !important; font-weight: 800 !important; font-size: 15px; }
  #play-target optgroup { color: var(--muted); font-weight: 600; font-size: 12px; }
  #play-target option { color: var(--accent); background: var(--panel2); font-weight: 500; }
  #play-target option:checked,
  #play-target option.is-selected { font-weight: 800 !important; color: var(--accent); }
  #target-msg:empty { display: none; }
  #target-msg.bad { color: #f85149; }
  .target-speaker-label {
    display: inline-flex; align-items: center; justify-content: center;
    color: var(--accent); cursor: pointer; user-select: none;
  }
  .target-speaker-label svg {
    width: 20px; height: 20px; fill: none; stroke: currentColor;
    stroke-width: 2; stroke-linecap: round; stroke-linejoin: round;
  }
)CSS"

#define IP_MODAL_JS R"JS(
function showIpChangeModal(record, onDismiss) {
  if (!record) return;
  let modal = document.getElementById("ip-change-modal-overlay");
  if (!modal) {
    modal = document.createElement("div");
    modal.id = "ip-change-modal-overlay";
    modal.className = "ip-modal-overlay";
    document.body.appendChild(modal);
  }
  const dateStr = record.timestamp_ms ? new Date(record.timestamp_ms).toLocaleTimeString() : "recently";
  modal.innerHTML = '<div class="ip-modal">' +
    '<h3>\u24d8 Speaker IP Address Changed</h3>' +
    '<div class="ip-kv">' +
    '<div>Speaker:</div><div><b>' + esc(record.device_name || "\u2014") + '</b></div>' +
    '<div>Device ID:</div><div><code>' + esc(record.device_id || "\u2014") + '</code></div>' +
    '<div>Previous IP:</div><div><code style="color:#f85149">' + esc(record.old_ip || "\u2014") + '</code></div>' +
    '<div>New IP:</div><div><code style="color:var(--ok)">' + esc(record.new_ip || "\u2014") + '</code></div>' +
    '<div>Detected At:</div><div>' + esc(dateStr) + '</div>' +
    '<div>Status:</div><div>' + (record.reconnected ? '<span style="color:var(--ok)">Auto-reconnected &amp; zone updated</span>' : '<span style="color:var(--warn)">Updated in config</span>') + '</div>' +
    '</div>' +
    '<div class="modal-actions">' +
    '<button type="button" class="warn" id="dismiss-ip-btn">Dismiss notification</button>' +
    '<button type="button" class="primary" id="close-ip-btn">Close</button>' +
    '</div></div>';
  modal.hidden = false;
  const close = () => { modal.hidden = true; };
  modal.querySelector("#close-ip-btn").onclick = close;
  modal.onclick = (e) => { if (e.target === modal) close(); };
  modal.querySelector("#dismiss-ip-btn").onclick = async () => {
    try {
      await fetch("/api/speakers/dismiss-ip-change", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ id: record.device_id })
      });
    } catch (e) { }
    close();
    if (onDismiss) onDismiss();
  };
}
)JS"

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
  .deck-presets-col { flex: 1; min-width: 300px; display: flex; flex-direction: column; gap: 6px; }
  .deck-heading { font-size: 11px; text-transform: uppercase; letter-spacing: .12em; color: var(--muted); font-weight: 600; }
  .presets { display: grid; grid-template-columns: repeat(6, minmax(0, 1fr)); gap: 8px; }
  .pbtn { display: flex; flex-direction: column; align-items: flex-start; gap: 1px; min-width: 0; padding: 8px 10px;
          border-radius: 10px; text-align: left; user-select: none; -webkit-user-select: none; touch-action: manipulation; }
  .pbtn .pnum { font-size: 17px; font-weight: 700; line-height: 1.15; }
  .pbtn .pname { max-width: 100%; font-size: 12px; color: var(--muted); overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
  .pbtn.empty .pnum, .pbtn.empty .pname { opacity: .5; }
  .pbtn:active { background: var(--line); }
  .deck-sources { display: flex; flex-direction: column; gap: 6px; padding-left: 14px; border-left: 1px solid var(--line); }
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
  .hero .station { font-size: 28px; font-weight: 800; color: var(--accent); margin: 6px 0 2px; letter-spacing: -.01em; }
  .hero .song { font-size: 18px; margin: 4px 0 2px; }
  .hero .song-artist { color: var(--accent); font-weight: 700; }
  .hero .song-track { color: var(--fg); font-weight: 500; }
  .hero .song-sep { color: var(--muted); margin: 0 6px; }
  .hero .meta { color: var(--muted); font-size: 13px; margin-top: 8px; }
  .hero .vol { display: none; align-items: center; gap: 10px; margin-top: 16px; max-width: 520px; --fill: var(--accent); }
  .hero .vol.show { display: flex; }
  .vol.muted { --fill: var(--warn); }
  .vol.split { --fill: var(--warn); }
  .vsplit { display: none; padding: 1px 8px; border-radius: 999px; border: 1px solid var(--warn); color: var(--warn); font-size: 11px; white-space: nowrap; }
  .vol.split .vsplit { display: inline-block; }
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
  <div class="banner" id="preset-warning" hidden style="border-color:var(--warn);background:rgba(210,153,34,.08)">
    <div class="btext"><strong id="pw-title" style="color:var(--warn)">Hardware presets not programmed</strong>
      <span class="muted" id="pw-msg">The active speaker has unprogrammed presets or does not match streams.json. Internet radio cannot be selected on the hardware without presets.</span></div>
    <button class="btn primary" id="pw-program" type="button">Program presets now</button>
  </div>
  <section class="hero">
    <div class="hero-main">
      <div style="display:flex;gap:8px;align-items:center;flex-wrap:wrap;margin-bottom:14px">
        <label for="play-target" class="target-speaker-label" title="Playback target" aria-label="Playback target">
          <svg viewBox="0 0 24 24" aria-hidden="true"><polygon points="11 5 6 9 2 9 2 15 6 15 11 19 11 5" fill="currentColor"/><path d="M15.54 8.46a5 5 0 0 1 0 7.07"/><path d="M19.07 4.93a10 10 0 0 1 0 14.14"/></svg>
        </label>
        <select id="play-target" aria-label="Playback target"></select>
        <button id="target-default" class="target-star-btn" type="button" aria-label="Make default target" title="Make default target">
          <svg viewBox="0 0 24 24" aria-hidden="true"><polygon points="12,2 14.5,7.7 20.7,7 17,12 20.7,17 14.5,16.3 12,22 9.5,16.3 3.3,17 7,12 3.3,7 9.5,7.7"/></svg>
        </button>
        <span id="target-msg" role="status" class="muted"></span>
      </div>
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
        <span class="vsplit" id="np-vol-split" title="Member volumes differ &mdash; adjusting will sync all members">Levels differ</span>
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
      <div class="deck-presets-col">
        <span class="deck-heading">Presets</span>
        <div class="presets" id="np-presets"></div>
      </div>
      <div class="pentry" id="np-pentry">
        <input id="np-pinput" type="text" inputmode="numeric" maxlength="3" autocomplete="off" spellcheck="false"
               placeholder="Preset" aria-label="Preset number" title="Type a preset, such as 111, then Enter">
        <span class="pcount" id="np-pcount"></span>
        <span class="phint" id="np-phint"></span>
      </div>
      <div class="deck-sources">
        <span class="deck-heading">Sources</span>
        <div class="sources">
          <button class="sbtn" id="np-bt" type="button" title="Switch to Bluetooth">
            <svg viewBox="0 0 24 24" aria-hidden="true"><path d="M6.5 6.5l11 11L12 23V1l5.5 5.5-11 11"/></svg>
            <span>Bluetooth</span>
          </button>
          <button class="sbtn" id="np-aux" type="button" title="Switch to AUX">
            <svg viewBox="0 0 24 24" aria-hidden="true"><path d="M12 2v3"/><path d="M10.25 5h3.5v4.5h-3.5z"/><path d="M10.25 7.25h3.5"/><path d="M9 9.5h6V16H9z"/><path d="M12 16v6"/></svg>
            <span>AUX</span>
          </button>
          <button class="sbtn" id="np-airplay" type="button" title="AirPlay streaming (cast from Apple device)">
            <svg viewBox="0 0 24 24" aria-hidden="true"><path d="M5 17H4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h16a2 2 0 0 1 2 2v10a2 2 0 0 1-2 2h-1"/><polygon points="12 15 17 21 7 21 12 15"/></svg>
            <span>AirPlay</span>
          </button>
        </div>
      </div>
    </div>
  </section>

  <div class="grid">
    <div class="card">
      <div class="card-head"><h2>Speaker</h2><span class="spacer"></span><a class="btn" href="/speakers">Manage speakers</a></div>
      <div id="speaker" class="kv"></div>
      <div id="speaker-note" class="muted" style="font-size:12.5px;margin-top:10px" hidden></div>
      <div id="zone-members" style="margin-top:14px"></div>
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

)PAGE" IP_MODAL_JS R"PAGE(

document.addEventListener("click", (e) => {
  const btn = e.target.closest(".info-circle-btn");
  if (btn && btn.dataset.change) {
    try {
      const rec = JSON.parse(btn.dataset.change);
      showIpChangeModal(rec, () => {
        loadTargets();
        loadStatic();
      });
    } catch (_) {}
  }
});

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
const memberVol = {};

function checkVolumeSync(members) {
  const box = $("np-volume");
  if (!box) return;
  if (!targetsState || !targetsState.selected || targetsState.selected.type !== "zone" || !members || members.length <= 1) {
    box.classList.remove("split");
    return;
  }
  const validVols = members.filter(m => m.volume != null).map(m => Number(m.volume));
  const isSplit = validVols.length > 1 && new Set(validVols).size > 1;
  box.classList.toggle("split", isSplit);
}

function syncTargetVolumesOptimistic(level) {
  if (!targetsState || !targetsState.members) return;
  const isZone = targetsState.selected && targetsState.selected.type === "zone";
  const box = $("zone-members");
  for (const m of targetsState.members) {
    if (isZone || m.leader) {
      m.volume = level;
      const mv = memberVol[m.id] || (memberVol[m.id] = { level: null, want: null, sending: false, quietUntil: 0, dragging: false });
      mv.level = level;
      mv.quietUntil = Date.now() + 300;
      if (box) {
        const slider = [...box.querySelectorAll(".m-slider")].find(x => x.dataset.mid === m.id);
        const num = [...box.querySelectorAll(".m-num")].find(x => x.dataset.mid === m.id);
        if (slider) {
          slider.value = level;
          slider.style.setProperty("--pct", level + "%");
        }
        if (num && document.activeElement !== num) {
          num.value = level;
        }
      }
    }
  }
  checkVolumeSync(targetsState.members);
}

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
  syncTargetVolumesOptimistic(level);
  vol.want = level;
  vol.quietUntil = Date.now() + 300;
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
  vol.quietUntil = Date.now() + 300;
  if (vol.want != null) { sendVolume(); return; }
  if (ok && targetsState && targetsState.members) {
    syncTargetVolumesOptimistic(level);
    renderTargets(targetsState);
  }
  if (!ok) {
    // Not taken: say so, and show what the speaker really has.
    const num = $("np-vol-num");
    num.classList.add("bad");
    setTimeout(() => num.classList.remove("bad"), 1500);
    vol.quietUntil = 0;
    vol.level = null;
    loadNowPlaying();
    loadTargets();
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
  const dragEnd = () => { if (vol.dragging) { vol.dragging = false; vol.quietUntil = Date.now() + 300; } };
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

function renderTransport(source, status, waiting) {
  tp.source = source || "";
  tp.status = status || "";
  const box = $("np-transport"), power = $("np-power"), play = $("np-play");
  if (!tp.source && !waiting) { box.classList.remove("show"); return; }
  box.classList.add("show");
  const on = tp.source !== "STANDBY";
  const playing = tp.status === "PLAY_STATE" || tp.status === "BUFFERING_STATE";
  power.classList.toggle("on", on);
  power.title = on ? "Switch off" : "Switch on";
  play.classList.toggle("playing", playing);
  play.title = waiting ? "Waiting for speaker to reconnect..." : (playing ? "Pause" : "Play");
  power.disabled = tp.busy || !!waiting;
  play.disabled = tp.busy || !!waiting || !on || tp.source === "INVALID_SOURCE" || !tp.status || tp.status === "INVALID_PLAY_STATUS";
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
    b.classList.toggle("on", (deck.source === "UPNP" || deck.source === "CONNECTING") && Number(b.dataset.preset) === deck.stationPreset);
  }
  $("np-bt").classList.toggle("on", deck.source === "BLUETOOTH");
  $("np-aux").classList.toggle("on", deck.source === "AUX");
  $("np-airplay").classList.toggle("on", deck.source === "AIRPLAY");
  const canSkip = deck.source !== "" && deck.source !== "STANDBY" && deck.source !== "AUX" && deck.source !== "INVALID_SOURCE" && deck.source !== "CONNECTING";
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
  $("np-airplay").addEventListener("click", () => showAirPlayModal());
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
const SOURCE_NAMES = { STANDBY: "Standby", BLUETOOTH: "Bluetooth", AUX: "AUX", AIRPLAY: "AirPlay", INVALID_SOURCE: "Idle", UPNP: "UPnP", CONNECTING: "Connecting...", UNABLE_TO_PLAY: "Unable to Play" };

function showAirPlayModal() {
  let modal = document.getElementById("airplay-info-modal-overlay");
  if (!modal) {
    modal = document.createElement("div");
    modal.id = "airplay-info-modal-overlay";
    modal.className = "ip-modal-overlay";
    document.body.appendChild(modal);
  }
  const currentTargetName = targetsState && targetsState.speakers
    ? (targetsState.speakers.find(s => s.ip === targetsState.active_ip) || {}).name || "this speaker"
    : "this speaker";
  modal.innerHTML = '<div class="ip-modal">' +
    '<h3><svg style="width:20px;height:20px;fill:none;stroke:currentColor;stroke-width:2;vertical-align:middle;margin-right:6px" viewBox="0 0 24 24"><path d="M5 17H4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h16a2 2 0 0 1 2 2v10a2 2 0 0 1-2 2h-1"/><polygon points="12 15 17 21 7 21 12 15"/></svg>AirPlay Streaming</h3>' +
    '<div style="font-size:13.5px;line-height:1.5;margin:12px 0 18px;color:var(--fg)">' +
    '<p style="margin:0 0 10px">AirPlay playback cannot be initiated directly from the web dashboard.</p>' +
    '<p style="margin:0 0 10px;color:var(--muted)">To stream audio via AirPlay, open <b>Control Center</b> or the audio output selector on your Apple device (iPhone, iPad, or Mac) and select <b>' + esc(currentTargetName) + '</b>.</p>' +
    '<p style="margin:0;font-size:12.5px;color:var(--muted)"><em>Note: AirPlay streams directly to individual speakers. Grouped or follower speakers do not advertise as AirPlay targets.</em></p>' +
    '</div>' +
    '<div class="modal-actions">' +
    '<button type="button" class="primary" id="close-airplay-btn">Got it</button>' +
    '</div></div>';
  modal.hidden = false;
  const close = () => { modal.hidden = true; };
  modal.querySelector("#close-airplay-btn").onclick = close;
  modal.onclick = (e) => { if (e.target === modal) close(); };
}

function renderNowPlaying(np) {
  const standby = np.source === "STANDBY";
  const waiting = !!np.waiting_for_speaker;
  const station = standby ? "Standby" : (np.station || (waiting ? "Connecting..." : (SOURCE_NAMES[np.source] || np.source || "\u2014")));
  $("np-station").textContent = station;
  const songElem = $("np-song");
  if (standby) {
    songElem.textContent = "";
  } else if (np.source === "AIRPLAY") {
    const parts = [];
    if (np.artist) parts.push('<span class="song-artist">' + esc(np.artist) + '</span>');
    if (np.track) parts.push('<span class="song-track">' + esc(np.track) + '</span>');
    songElem.innerHTML = parts.length ? parts.join('<span class="song-sep">\u00b7</span>') : "No track information available";
  } else if (waiting && !np.title) {
    songElem.textContent = "(waiting for speaker to reconnect...)";
  } else if (np.title) {
    const raw = np.title;
    const sepIdx = raw.indexOf(" - ");
    if (sepIdx !== -1) {
      const art = raw.substring(0, sepIdx).trim();
      const trk = raw.substring(sepIdx + 3).trim();
      songElem.innerHTML = '<span class="song-artist">' + esc(art) + '</span><span class="song-sep">\u00b7</span><span class="song-track">' + esc(trk) + '</span>';
    } else {
      songElem.textContent = raw;
    }
  } else {
    songElem.textContent = "";
  }
  const bits = [];
  if (waiting || np.status === "UNABLE_TO_PLAY") {
    bits.push("Unable to play: speaker offline \u2014 waiting to reconnect");
  } else {
    if (np.status) bits.push(np.status);
    if (np.source && !standby && np.source !== station) bits.push(np.source);
    if (np.source === "AIRPLAY" && np.album) bits.push(np.album);
    if (np.relay_running != null) bits.push(np.relay_running ? "relay on" : "relay off");
  }
  $("np-meta").textContent = bits.join("  \u00b7  ");
  renderVolume(np.volume, np.muted);
  if (np.member_volumes && targetsState && targetsState.members) {
    let changed = false;
    for (const [id, info] of Object.entries(np.member_volumes)) {
      const m = targetsState.members.find(x => x.id === id);
      if (m && info && info.volume != null) {
        const v = m.leader && np.volume != null ? Number(np.volume) : Number(info.volume);
        const muted = m.leader && np.muted != null ? !!np.muted : !!info.muted;
        if (m.volume !== v || m.muted !== muted) {
          const mv = memberVol[id];
          const isBusy = m.leader
            ? (vol.dragging || vol.sending || vol.want != null || Date.now() < vol.quietUntil)
            : (mv && (mv.dragging || mv.sending || mv.want != null || Date.now() < mv.quietUntil));
          if (!isBusy) {
            m.volume = v;
            m.muted = muted;
            changed = true;
          }
        }
      }
    }
    if (changed) {
      checkVolumeSync(targetsState.members);
      renderTargets(targetsState);
    }
  } else if (np.volume != null && targetsState && targetsState.members) {
    const leader = targetsState.members.find(x => x.leader);
    if (leader && leader.volume !== Number(np.volume)) {
      const isBusy = (vol.dragging || vol.sending || vol.want != null || Date.now() < vol.quietUntil);
      if (!isBusy) {
        leader.volume = Number(np.volume);
        checkVolumeSync(targetsState.members);
        renderTargets(targetsState);
      }
    }
  }
  renderTransport(np.source, np.status, waiting);
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
  let speakerHtml = "<div class='muted'>none</div><div><a href='/speakers'>Find speakers</a></div>";
  if (def) {
    speakerHtml = "<div>Name</div><div>" + esc(def.name || "\u2014");
    if (def.ip_change) {
      speakerHtml += ' <button class="info-circle-btn" type="button" title="IP address changed" data-change=\'' + esc(JSON.stringify(def.ip_change)) + '\'>&#9432;</button>';
    }
    speakerHtml += "</div>" +
      "<div>IP</div><div><code>" + esc(def.ip) + "</code></div>" +
      (def.type ? "<div>Model</div><div>" + esc(def.type) + "</div>" : "") +
      "<div>ID</div><div><code>" + esc(def.id) + "</code></div>";
    const isRecent = def.ip_change && ((Date.now() - (def.ip_change.timestamp_ms || 0)) < 30000);
    $("speaker").classList.toggle("ip-changed-flash", !!isRecent);
  } else {
    $("speaker").classList.remove("ip-changed-flash");
  }
  $("speaker").innerHTML = speakerHtml;

  // Control picked its speaker when it started, so a different default only takes over on its next start.
  const note = $("speaker-note");
  const switching = sp.embedded && def && sp.active_ip && def.ip !== sp.active_ip;
  note.hidden = !switching;
  if (switching) note.textContent = "Controlling " + sp.active_ip + " until control restarts.";

  $("first-run").hidden = !!sp.has_default || sessionStorage.getItem("cxstcc-first-run-later") === "1";
}

let targetsState = null;
let targetBusy = false;

async function targetCall(path, body) {
  const r = await fetch(path, { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(body) });
  const data = await r.json();
  if (!r.ok) throw Object.assign(new Error(data.error || ("HTTP " + r.status)), { status: r.status });
  return data;
}

function updateTargetSelectStyle() {
  const sel = $("play-target");
  if (!sel) return;
  sel.style.setProperty("color", "var(--accent)", "important");
  sel.style.setProperty("font-weight", "800", "important");
  for (const opt of sel.options) {
    const isSel = opt.value === sel.value;
    opt.classList.toggle("is-selected", isSel);
    opt.style.setProperty("color", "var(--accent)", "important");
    opt.style.setProperty("font-weight", isSel ? "800" : "500", "important");
  }
}

function renderTargets(d) {
  targetsState = d;
  const sel = $("play-target");
  const defBtn = $("target-default");
  const chosen = d.selected || {};
  const selected = chosen.type + ":" + chosen.id;
  const defTarget = d.default || {};
  const isDefault = chosen.type && chosen.id && chosen.type === defTarget.type && chosen.id === defTarget.id;
  if (defBtn) {
    defBtn.classList.toggle("is-default", !!isDefault);
    defBtn.title = isDefault ? "Current default target (click to re-save)" : "Make default target";
    defBtn.setAttribute("aria-label", defBtn.title);
  }
  const old = sel.value;
  const curVal = old && [...sel.options].some(o => o.value === old) ? old : selected;
  let h = '<optgroup label="Speakers" class="opt-speakers" style="color:var(--muted)">';
  for (const s of d.speakers || []) {
    const val = "speaker:" + s.id;
    const isSel = val === curVal;
    h += '<option class="opt-speaker' + (isSel ? ' is-selected' : '') + '" value="' + val + '" style="color:var(--accent);' + (isSel ? 'font-weight:800;' : 'font-weight:500;') + '">' + esc(s.name || s.id) + '</option>';
  }
  h += '</optgroup><optgroup label="Zones" class="opt-zones" style="color:var(--muted)">';
  for (const z of d.zones || []) {
    const val = "zone:" + z.id;
    const isSel = val === curVal;
    h += '<option class="opt-zone' + (isSel ? ' is-selected' : '') + '" value="' + val + '" style="color:var(--accent);' + (isSel ? 'font-weight:800;' : 'font-weight:500;') + '">' + esc(z.name) + '</option>';
  }
  sel.innerHTML = h + '</optgroup>';
  sel.value = curVal;
  updateTargetSelectStyle();
  if (!targetBusy) {
    const msg = $("target-msg");
    if (msg && !msg.classList.contains("bad")) msg.textContent = "";
  }
  const members = d.members || [];
  const leader = members.find(x => x.leader);
  if (leader) {
    let leaderInfo = "<div>Target</div><div>" + esc(chosen.type === "zone" ? (d.zones.find(x => x.id === chosen.id)?.name || chosen.id) : (leader.name || leader.id)) +
      "</div><div>" + (chosen.type === "zone" ? "Leader" : "Speaker") + "</div><div>" + esc(leader.name || leader.id);
    if (leader.ip_change) {
      leaderInfo += ' <button class="info-circle-btn" type="button" title="IP address changed" data-change=\'' + esc(JSON.stringify(leader.ip_change)) + '\'>&#9432;</button>';
    }
    leaderInfo += "</div><div>IP</div><div><code>" + esc(leader.ip) + "</code></div><div>ID</div><div><code>" + esc(leader.id) + "</code></div>";
    $("speaker").innerHTML = leaderInfo;
    const lChange = leader.ip_change;
    const lRecent = lChange && ((Date.now() - (lChange.timestamp_ms || 0)) < 30000);
    $("speaker").classList.toggle("ip-changed-flash", !!lRecent);
    $("speaker-note").hidden = true;
  }
  const box = $("zone-members");
  if (chosen.type !== "zone") { box.innerHTML = ""; checkVolumeSync([]); return; }
  checkVolumeSync(members);

  let header = box.querySelector(".member-vol-hdr");
  if (!header) {
    box.innerHTML = '<div class="member-vol-hdr"><h3>Zone member volumes</h3><p class="muted">Now Playing volume adjusts all members together. Adjust individual members below.</p></div><div class="member-vol-list"></div>';
  }
  const list = box.querySelector(".member-vol-list") || box;

  // Reconcile rows by speaker id
  const existingRows = new Map();
  list.querySelectorAll(".member-vol-row").forEach(r => {
    if (r.dataset.mid) existingRows.set(r.dataset.mid, r);
  });

  const memberIds = new Set(members.map(s => s.id));
  for (const [id, r] of existingRows) {
    if (!memberIds.has(id)) r.remove();
  }

  for (const s of members) {
    const v = s.volume != null ? Number(s.volume) : 0;
    const mv = memberVol[s.id] || (memberVol[s.id] = { level: v, want: null, sending: false, quietUntil: 0, dragging: false });
    let row = existingRows.get(s.id);
    if (!row) {
      row = document.createElement("div");
      row.className = "member-vol-row";
      row.dataset.mid = s.id;
      row.style.cssText = "display:flex;align-items:center;gap:12px;margin:10px 0;flex-wrap:wrap";
      row.innerHTML =
        '<span class="m-label" style="min-width:140px;font-size:14px;font-weight:500"></span>' +
        '<div class="vol show' + (s.muted ? ' muted' : '') + '" style="margin:0;flex:1;min-width:240px;max-width:440px">' +
        '<input class="vslider m-slider" data-mid="' + esc(s.id) + '" type="range" min="0" max="100" step="1" value="' + v + '" style="--pct:' + v + '%" aria-label="' + esc((s.name || s.id) + ' volume') + '">' +
        '<button class="vstep m-down" data-mid="' + esc(s.id) + '" type="button" aria-label="Volume down">&minus;</button>' +
        '<input class="vnum m-num" data-mid="' + esc(s.id) + '" type="number" min="0" max="100" step="1" value="' + (s.volume != null ? v : '') + '" aria-label="' + esc((s.name || s.id) + ' volume level') + '">' +
        '<button class="vstep m-up" data-mid="' + esc(s.id) + '" type="button" aria-label="Volume up">+</button>' +
        '<span class="vmuted">Muted</span>' +
        '</div>' +
        '<span class="m-err muted" style="font-size:12px"></span>';
      list.appendChild(row);
      wireSingleMemberVolume(row, s.id);
    }
    const label = row.querySelector(".m-label");
    if (label) {
      let lhtml = esc(s.name || s.id) + (s.leader ? ' <span class="muted" style="font-size:12px;font-weight:normal">(leader)</span>' : '');
      if (s.ip_change) {
        lhtml += ' <button class="info-circle-btn" type="button" title="IP address changed" data-change=\'' + esc(JSON.stringify(s.ip_change)) + '\'>&#9432;</button>';
      }
      label.innerHTML = lhtml;
    }
    const isRecent = s.ip_change && ((Date.now() - (s.ip_change.timestamp_ms || 0)) < 30000);
    row.classList.toggle("ip-changed-flash", !!isRecent);
    const volBox = row.querySelector(".vol");
    if (volBox) volBox.classList.toggle("muted", !!s.muted);
    const errSpan = row.querySelector(".m-err");
    if (errSpan) errSpan.textContent = s.error || "";

    const slider = row.querySelector(".m-slider");
    const num = row.querySelector(".m-num");
    const isInteracting = mv.dragging || mv.sending || mv.want != null || Date.now() < mv.quietUntil ||
                          (num && document.activeElement === num);
    if (!isInteracting) {
      mv.level = v;
      if (slider) {
        slider.value = v;
        slider.style.setProperty("--pct", v + "%");
      }
      if (num && document.activeElement !== num) {
        num.value = s.volume != null ? v : "";
      }
    }
  }
}

function askMemberVolume(id, level) {
  level = Math.round(Number(level));
  if (!Number.isFinite(level)) return;
  level = Math.max(0, Math.min(100, level));
  const mv = memberVol[id] || (memberVol[id] = { level: null, want: null, sending: false, quietUntil: 0, dragging: false });
  if (level === mv.level && mv.want == null) return;

  const box = $("zone-members");
  const slider = box ? [...box.querySelectorAll(".m-slider")].find(x => x.dataset.mid === id) : null;
  const num = box ? [...box.querySelectorAll(".m-num")].find(x => x.dataset.mid === id) : null;
  mv.level = level;
  if (slider) { slider.value = level; slider.style.setProperty("--pct", level + "%"); }
  if (num && document.activeElement !== num) num.value = level;

  if (targetsState && targetsState.members) {
    const m = targetsState.members.find(x => x.id === id);
    if (m) m.volume = level;
    checkVolumeSync(targetsState.members);
  }

  mv.want = level;
  mv.quietUntil = Date.now() + 300;
  sendMemberVolume(id);
}

async function sendMemberVolume(id) {
  const mv = memberVol[id];
  if (!mv || mv.sending || mv.want == null) return;
  const level = mv.want;
  mv.want = null;
  mv.sending = true;
  let ok = false;
  let errMsg = "";
  try {
    const r = await fetch("/api/member-volume", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ id, volume: level })
    });
    const d = await r.json().catch(() => ({}));
    ok = r.ok;
    if (!ok) errMsg = d.error || ("HTTP " + r.status);
  } catch (err) {
    errMsg = err.message;
  }
  mv.sending = false;
  mv.quietUntil = Date.now() + 300;
  if (mv.want != null) {
    sendMemberVolume(id);
    return;
  }
  if (!ok) {
    const box = $("zone-members");
    const num = box ? [...box.querySelectorAll(".m-num")].find(x => x.dataset.mid === id) : null;
    if (num) { num.classList.add("bad"); setTimeout(() => num.classList.remove("bad"), 1500); }
    $("target-msg").textContent = "Member volume failed: " + errMsg;
    mv.quietUntil = 0;
    loadTargets();
  }
}

function stepperForMember(button, id, delta) {
  let timer = null;
  const stop = () => { clearTimeout(timer); clearInterval(timer); timer = null; };
  const getVal = () => {
    const mv = memberVol[id];
    if (mv && mv.level != null) return mv.level;
    const box = $("zone-members");
    const slider = box ? [...box.querySelectorAll(".m-slider")].find(x => x.dataset.mid === id) : null;
    return slider ? Number(slider.value) : 0;
  };
  const step = () => {
    askMemberVolume(id, getVal() + delta);
  };
  button.addEventListener("pointerdown", (e) => {
    if (e.button !== 0) return;
    e.preventDefault();
    stop();
    step();
    timer = setTimeout(() => { timer = setInterval(step, 90); }, 400);
  });
  for (const type of ["pointerup", "pointerleave", "pointercancel"]) button.addEventListener(type, stop);
  button.addEventListener("click", (e) => { if (e.detail === 0) step(); });
  button.addEventListener("contextmenu", (e) => e.preventDefault());
}

function wireSingleMemberVolume(row, id) {
  const mv = memberVol[id] || (memberVol[id] = { level: null, want: null, sending: false, quietUntil: 0, dragging: false });
  const slider = row.querySelector(".m-slider");
  const num = row.querySelector(".m-num");
  const downBtn = row.querySelector(".m-down");
  const upBtn = row.querySelector(".m-up");

  if (downBtn) stepperForMember(downBtn, id, -1);
  if (upBtn) stepperForMember(upBtn, id, 1);

  if (slider) {
    const dragEnd = () => {
      if (mv.dragging) {
        mv.dragging = false;
        mv.quietUntil = Date.now() + 300;
      }
    };
    slider.addEventListener("pointerdown", () => { mv.dragging = true; });
    window.addEventListener("pointerup", dragEnd);
    window.addEventListener("pointercancel", dragEnd);
    slider.addEventListener("change", dragEnd);
    slider.addEventListener("input", () => askMemberVolume(id, slider.value));
  }

  if (num) {
    let keepSelection = false;
    num.addEventListener("focus", () => { num.select(); keepSelection = true; });
    num.addEventListener("mouseup", (e) => { if (keepSelection) { e.preventDefault(); keepSelection = false; } });
    num.addEventListener("blur", () => { keepSelection = false; });
    num.addEventListener("keydown", (e) => {
      keepSelection = false;
      if (e.key === "Enter") { e.preventDefault(); num.blur(); }
      else if (e.key === "Escape") {
        num.value = mv.level != null ? mv.level : "";
        num.blur();
      } else if (e.key === "ArrowUp" || e.key === "ArrowDown") {
        e.preventDefault();
        askMemberVolume(id, (mv.level || 0) + (e.key === "ArrowUp" ? 1 : -1));
        num.value = mv.level != null ? mv.level : "";
      }
    });
    num.addEventListener("change", () => {
      if (num.value.trim() !== "") askMemberVolume(id, num.valueAsNumber);
      num.value = mv.level != null ? mv.level : "";
    });
  }
}

async function loadTargets() {
  if (targetBusy) return;
  try { renderTargets(await get("/api/targets")); }
  catch (e) {
    const msg = $("target-msg");
    if (msg) { msg.textContent = "Targets unavailable: " + e.message; msg.className = "bad"; }
  }
}

async function useTarget(makeDefault) {
  if (targetBusy) return;
  const sel = $("play-target");
  const value = sel.value;
  const split = value.indexOf(":");
  if (split < 0) return;
  const currentChosen = targetsState?.selected ? (targetsState.selected.type + ":" + targetsState.selected.id) : "";
  if (!makeDefault && value === currentChosen) return;

  const body = { type: value.slice(0, split), id: value.slice(split + 1), make_default: makeDefault };
  targetBusy = true;
  sel.disabled = true;
  const defBtn = $("target-default");
  if (defBtn) defBtn.disabled = true;
  const msg = $("target-msg");
  if (msg) { msg.textContent = "Selecting…"; msg.className = "muted"; }
  try {
    let d;
    try { d = await targetCall("/api/target", body); }
    catch (e) {
      if (e.status !== 409 || !confirm(e.message + "\nMove these speakers to the selected target? This may interrupt their current group or playback.")) {
        // User cancelled takeover or error occurred; restore select value to previous chosen target
        if (currentChosen && [...sel.options].some(o => o.value === currentChosen)) {
          sel.value = currentChosen;
        }
        throw e;
      }
      d = await targetCall("/api/target", { ...body, takeover: true });
    }
    targetBusy = false;
    renderTargets(d);
    if (msg) {
      if (makeDefault) {
        msg.textContent = "Saved as default";
        msg.className = "muted";
        setTimeout(() => { if (msg.textContent === "Saved as default") msg.textContent = ""; }, 2500);
      } else {
        msg.textContent = "";
      }
    }
    await loadNowPlaying();
    await loadStatic();
    await checkPresetsWarning();
  } catch (e) {
    if (msg) { msg.textContent = "Not selected: " + e.message; msg.className = "bad"; }
    if (currentChosen && [...sel.options].some(o => o.value === currentChosen)) {
      sel.value = currentChosen;
    }
  }
  finally {
    targetBusy = false;
    sel.disabled = false;
    if (defBtn) defBtn.disabled = false;
    updateTargetSelectStyle();
  }
}

$("play-target").onchange = () => { updateTargetSelectStyle(); useTarget(false); };
$("target-default").onclick = () => useTarget(true);

async function loadStatic() {
  try {
    const [streams, speakers, config, live] = await Promise.all([
      getStreams(), get("/api/speakers"), get("/api/config"),
      get("/api/live").catch(() => ({}))
    ]);

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

let activePresetsData = null;

async function checkPresetsWarning() {
  try {
    const res = await get("/api/speakers/presets");
    activePresetsData = res;
    const active = res.active_target || {};
    const banner = $("preset-warning");
    if (active.status === "unprogrammed" || active.status === "mismatch") {
      banner.hidden = false;
      $("pw-title").textContent = active.status === "unprogrammed"
        ? "Hardware presets not programmed on " + (active.name || "speaker")
        : "Hardware presets mismatch on " + (active.name || "speaker");
      $("pw-msg").textContent = active.status === "unprogrammed"
        ? "This speaker has zero presets stored. Internet radio cannot be selected on the hardware without presets."
        : "Presets on this speaker do not match streams.json.";
      $("pw-program").disabled = false;
      $("pw-program").textContent = "Program presets now";
    } else {
      banner.hidden = true;
    }
  } catch (e) { }
}

$("pw-program").onclick = async () => {
  const btn = $("pw-program");
  btn.disabled = true;
  btn.textContent = "Programming…";
  try {
    const targetId = activePresetsData?.active_target?.leader_id;
    if (targetId) {
      await targetCall("/api/speakers/presets", { id: targetId });
    } else {
      await targetCall("/api/speakers/presets", { all: true });
    }
    await checkPresetsWarning();
  } catch (err) {
    alert("Could not program presets: " + err.message);
    btn.disabled = false;
    btn.textContent = "Program presets now";
  }
};

$("refresh").onclick = () => { loadStatic(); loadNowPlaying(); checkPresetsWarning(); };
$("first-run-later").onclick = () => { sessionStorage.setItem("cxstcc-first-run-later", "1"); $("first-run").hidden = true; };
wireVolume();
wireTransport();
wireDeck();
wireStreams();
loadStatic();
loadTargets();
checkPresetsWarning();
setInterval(() => { if (!document.hidden) loadTargets(); }, 20000);
followLive().then(() => { loadNowPlaying(); setInterval(loadNowPlaying, 10000); });
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
  .badge-synced { color: var(--ok); border: 1px solid rgba(63,185,80,.55); border-radius: 12px; padding: 2px 7px; font-size: 11.5px; font-weight: 500; }
  .badge-unprogrammed { color: #f85149; border: 1px solid rgba(248,81,73,.55); border-radius: 12px; padding: 2px 7px; font-size: 11.5px; font-weight: 500; }
  .badge-mismatch { color: var(--warn); border: 1px solid rgba(210,153,34,.55); border-radius: 12px; padding: 2px 7px; font-size: 11.5px; font-weight: 500; }
  .intro { margin: 0; color: var(--muted); font-size: 14px; }
</style>
</head>
<body>
<header>
  <a class="brand" href="/" title="Back to the dashboard"><span id="dot" class="dot"></span><h1>SoundTouch</h1></a>
  <nav class="tabs"><a href="/" id="to-dashboard">Dashboard</a><a href="/speakers" class="on" aria-current="page">Speakers</a></nav>
  <span class="spacer"></span>
  <a class="btn" href="/">&larr; Dashboard</a>
</header>
<main>
  <p class="intro">Speakers on your network show up here while this page is open. Save zones below; choose a speaker or zone on the dashboard. The legacy default speaker is kept for existing commands.</p>

  <div class="notice" id="restart-note" hidden></div>

  <div class="card">
    <div class="card-head">
      <h2>Speakers</h2>
      <span class="smsg" id="msg" role="status"></span>
      <button id="prog-all-presets" type="button" style="margin-right:8px">Program presets on all speakers</button>
      <span class="search" id="search"><span class="spin" id="spin"></span><span id="search-text">Starting search&hellip;</span></span>
    </div>
    <div id="speakers" class="tablewrap"></div>
  </div>

  <div class="card">
    <div class="card-head"><h2>Zones</h2><span class="smsg" id="zones-msg" role="status"></span><button id="zone-new" type="button">+ New zone</button></div>
    <p class="muted">Save reusable speaker groups here. Saving does not group speakers or start music. Use a zone to activate it, then play from the dashboard.</p>
    <div id="groups" class="tablewrap"></div>
    <form id="zone-editor" hidden style="margin-top:18px">
      <label>Zone name <input id="zone-name" type="text" maxlength="80" required></label>
      <div id="zone-choices" style="margin:12px 0"></div>
      <label>Leader <select id="zone-leader" required></select></label>
      <p class="muted">Choose at least two speakers, including the leader. Members keep their individual volumes.</p>
      <button type="submit" class="primary">Save zone</button>
      <button id="zone-cancel" type="button">Cancel</button>
    </form>
  </div>
</main>

<script>
const $ = (id) => document.getElementById(id);
const esc = (s) => String(s == null ? "" : s).replace(/[&<>"']/g, c =>
  ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));

)PAGE" IP_MODAL_JS R"PAGE(

document.addEventListener("click", (e) => {
  const btn = e.target.closest(".info-circle-btn");
  if (btn && btn.dataset.change) {
    try {
      const rec = JSON.parse(btn.dataset.change);
      showIpChangeModal(rec, () => {
        poll();
      });
    } catch (_) {}
  }
});

const POLL_MS = 2000;
let last = null;      // the latest /api/speakers answer
let busy = false;     // a "Make default" is in flight
let timer = 0;
let zoneState = null;
let zoneEtag = "";
let editingZone = null;
let zoneBusy = false;

function zoneMessage(text) { $("zones-msg").textContent = text; }

async function loadZones() {
  if (zoneBusy || !$("zone-editor").hidden) return;
  try {
    const r = await fetch("/api/targets", { cache: "no-store" });
    const d = await r.json();
    if (!r.ok) throw new Error(d.error || ("HTTP " + r.status));
    zoneState = d; zoneEtag = r.headers.get("ETag") || "";
    renderZones();
  } catch (e) { zoneMessage("Cannot load zones: " + e.message); }
}

function renderZones() {
  let h = '<table><tr><th>Name</th><th>Leader / members</th><th>State</th><th>Actions</th></tr>';
  for (const z of (zoneState?.zones || [])) {
    const name = id => (zoneState.speakers || []).find(s => s.id === id)?.name || id;
    const isDef = zoneState?.default && zoneState.default.type === 'zone' && zoneState.default.id === z.id;
    const defBadge = isDef ? '<span class="pill on" style="margin-left:6px">default</span>' : '';
    const defBtn = isDef ? '' : ' <button data-zone-default="' + esc(z.id) + '">Make default</button>';
    h += '<tr><td><b>' + esc(z.name) + '</b>' + defBadge + '</td><td><b>' + esc(name(z.leader_id)) + '</b><div class="muted">' +
      esc(z.members.map(name).join(", ")) + '</div></td><td>' + esc(z.state) + (z.error ? '<div class="muted">' + esc(z.error) + '</div>' : '') +
      '</td><td><button data-zone-use="' + esc(z.id) + '">Use</button>' + defBtn + ' <button data-zone-edit="' + esc(z.id) +
      '">Edit</button> <button data-zone-dissolve="' + esc(z.id) + '">Dissolve live group</button> <button data-zone-delete="' + esc(z.id) + '">Delete saved zone</button></td></tr>';
  }
  if (!(zoneState?.zones || []).length) h += '<tr><td colspan="4" class="empty">No saved zones. Discover speakers, then create a zone.</td></tr>';
  $("groups").innerHTML = h + '</table>';
}

function editZone(id) {
  editingZone = id ? zoneState.zones.find(z => z.id === id) : null;
  $("zone-name").value = editingZone?.name || "";
  $("zone-choices").innerHTML = (zoneState?.speakers || []).map(s =>
    '<label style="display:block;margin:8px 0"><input type="checkbox" value="' + esc(s.id) + '"' +
    (editingZone?.members.includes(s.id) ? ' checked' : '') + '> ' + esc(s.name || s.id) + '</label>').join("");
  $("zone-leader").innerHTML = (zoneState?.speakers || []).map(s => '<option value="' + esc(s.id) + '">' + esc(s.name || s.id) + '</option>').join("");
  if (editingZone) $("zone-leader").value = editingZone.leader_id;
  $("zone-editor").hidden = false;
  $("zone-name").focus();
}

async function saveZoneList(zones) {
  const r = await fetch("/api/zones", { method: "PUT", headers: { "Content-Type": "application/json", "If-Match": zoneEtag }, body: JSON.stringify({ zones }) });
  const d = await r.json();
  if (!r.ok) throw new Error(d.error || ("HTTP " + r.status));
  $("zone-editor").hidden = true;
  zoneBusy = false;
  await loadZones();
}

$("zone-new").onclick = () => editZone("");
$("zone-cancel").onclick = () => { $("zone-editor").hidden = true; loadZones(); };
$("zone-editor").onsubmit = async e => {
  e.preventDefault();
  if (zoneBusy) return;
  const members = [...$("zone-choices").querySelectorAll("input:checked")].map(x => x.value);
  const leader = $("zone-leader").value;
  const name = $("zone-name").value.trim();
  if (!name || members.length < 2 || !members.includes(leader)) { zoneMessage("Choose a name and at least two members, including the leader."); return; }
  const id = editingZone?.id || ("zone-" + Date.now().toString(36) + "-" + Math.random().toString(36).slice(2, 10));
  const zones = (zoneState.zones || []).filter(z => z.id !== id).map(({ id, name, leader_id, members }) => ({ id, name, leader_id, members }));
  zones.push({ id, name, leader_id: leader, members });
  zoneBusy = true;
  try { await saveZoneList(zones); zoneMessage("Saved. No playback or grouping was changed."); }
  catch (err) { zoneMessage("Not saved: " + err.message + ". Cancel and reopen to reload a changed list."); }
  finally { zoneBusy = false; }
};

$("groups").addEventListener("click", async e => {
  const b = e.target.closest("button");
  if (!b || zoneBusy) return;
  if (b.dataset.zoneEdit) { editZone(b.dataset.zoneEdit); return; }
  if (b.dataset.zoneDefault) {
    const id = b.dataset.zoneDefault;
    zoneBusy = true;
    try {
      await call("POST", "/api/target", { type: "zone", id, make_default: true });
      zoneMessage("Default target set to zone.");
    } catch (err) {
      zoneMessage("Could not set default zone: " + err.message);
    } finally {
      zoneBusy = false;
      await loadZones();
    }
    return;
  }
  const id = b.dataset.zoneUse || b.dataset.zoneDissolve || b.dataset.zoneDelete;
  if (!id) return;
  zoneBusy = true;
  try {
    if (b.dataset.zoneDelete) {
      if (!confirm("Delete this saved zone? The live Bose group is left unchanged.")) return;
      await saveZoneList(zoneState.zones.filter(z => z.id !== id).map(({ id, name, leader_id, members }) => ({ id, name, leader_id, members })));
      zoneMessage("Saved zone deleted; live grouping unchanged.");
    } else if (b.dataset.zoneDissolve) {
      if (!confirm("Separate this zone's live speakers? Members will leave the leader's playback.")) return;
      await call("POST", "/api/zones/dissolve", { id });
      zoneMessage("Live group dissolved; saved zone kept.");
    } else {
      try { await call("POST", "/api/target", { type: "zone", id }); }
      catch (err) {
        if (err.status !== 409 || !confirm(err.message + "\nTake over these speakers? This may interrupt another group or playback.")) throw err;
        await call("POST", "/api/target", { type: "zone", id, takeover: true });
      }
      zoneMessage("Zone selected. Play a stream from the dashboard.");
    }
  } catch (err) { zoneMessage(err.message); }
  finally { zoneBusy = false; await loadZones(); }
});

async function call(method, path, body) {
  const opt = { method, headers: {} };
  if (body !== undefined) { opt.headers["Content-Type"] = "application/json"; opt.body = JSON.stringify(body); }
  const r = await fetch(path, opt);
  let data = {};
  try { data = await r.json(); } catch (e) { /* not JSON */ }
  if (!r.ok) throw Object.assign(new Error(data.error || ("HTTP " + r.status)), { data, status: r.status });
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

let presetsInfo = null;

async function loadPresetsInfo() {
  try {
    presetsInfo = await call("GET", "/api/speakers/presets");
    if (last) render(last);
  } catch (e) { }
}

function render(d) {
  last = d;
  renderSearch(d);

  const list = d.speakers || [];
  let h = "<table><tr><th></th><th>Name</th><th>IP</th><th>Presets</th><th>Model</th><th>ID</th><th></th></tr>";
  for (const sp of list) {
    const pills = (sp.default ? '<span class="pill on">default</span>' : "") +
                  (sp.active ? '<span class="pill">in use</span>' : "") +
                  (sp.saved ? "" : '<span class="pill new">new</span>');
    let seen = "";
    if (sp.online === true) seen = "online";
    else if (sp.online === false) seen = sp.last_seen_ms != null ? "last seen " + ago(sp.last_seen_ms) : "not found";

    const pData = (presetsInfo?.speakers || []).find(x => x.id === sp.id || x.ip === sp.ip);
    let pCol = '<span class="muted">\u2014</span>';
    if (pData) {
      if (pData.status === "synced") {
        pCol = '<span class="badge-synced">Synced</span>';
      } else if (pData.status === "unprogrammed") {
        pCol = '<span class="badge-unprogrammed" title="No presets stored">Unprogrammed</span> <button type="button" data-prog-id="' + esc(sp.id) + '">Program</button>';
      } else if (pData.status === "mismatch") {
        pCol = '<span class="badge-mismatch" title="Presets do not match streams.json">Mismatch</span> <button type="button" data-prog-id="' + esc(sp.id) + '">Program</button>';
      } else if (pData.status === "unreachable") {
        pCol = '<span class="muted">Unreachable</span>';
      }
    }

    const act = (sp.default
      ? '<span class="muted" style="font-size:13px">Default</span>'
      : !sp.id
      ? '<span class="muted" style="font-size:13px" title="It did not say who it is">Unknown</span>'
      : '<button type="button" class="primary" data-id="' + esc(sp.id) + '"' + (busy ? " disabled" : "") + ">Make default</button>") +
      (sp.id ? ' <a class="btn" style="padding:4px 8px;font-size:12.5px" href="/speaker-details?id=' + encodeURIComponent(sp.id) + '">Details</a>' : '');
    const nameLink = sp.id
      ? '<a href="/speaker-details?id=' + encodeURIComponent(sp.id) + '" style="color:inherit;text-decoration:underline">' + esc(sp.name || "\u2014") + '</a>'
      : esc(sp.name || "\u2014");
    const isRecent = sp.ip_change && ((Date.now() - (sp.ip_change.timestamp_ms || 0)) < 30000);
    let ipCol = "<td><code>" + esc(sp.ip) + "</code>";
    if (sp.ip_change) {
      ipCol += ' <button class="info-circle-btn" type="button" title="IP address changed" data-change=\'' + esc(JSON.stringify(sp.ip_change)) + '\'>&#9432;</button>';
    }
    if (sp.saved_ip) {
      ipCol += '<div class="moved">was <code>' + esc(sp.saved_ip) + "</code></div>";
    }
    ipCol += "</td>";
    h += '<tr class="' + (sp.default ? "default" : "") + (sp.online === false ? " gone" : "") + (isRecent ? " ip-changed-flash" : "") + '" data-row="' + esc(sp.id) + '">' +
         '<td class="st">' + status(sp) + "</td>" +
         '<td><div class="sname">' + nameLink + pills + "</div>" + (seen ? '<div class="seen">' + esc(seen) + "</div>" : "") + "</td>" +
         ipCol +
         "<td>" + pCol + "</td>" +
         "<td>" + esc(sp.type || "\u2014") + "</td>" +
         "<td><code>" + esc(sp.id) + "</code></td>" +
         '<td class="act">' + act + "</td></tr>";
  }
  if (!list.length) {
    const passes = (d.discovery || {}).passes || 0;
    h += '<tr><td colspan="7" class="empty">' + (passes
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
    await loadZones();
  } catch (e) {
    setOnline(false);
    $("spin").className = "spin";
    $("search-text").textContent = "Cannot reach the dashboard";
  }
  timer = setTimeout(poll, POLL_MS);
}

async function programPresets(id, all) {
  if (busy) return;
  busy = true;
  const label = all ? "all speakers" : "speaker";
  msg("Programming presets on " + label + "\u2026");
  try {
    const res = await call("POST", "/api/speakers/presets", all ? { all: true } : { id });
    busy = false;
    await loadPresetsInfo();
    msg("", "ok", "Presets programmed successfully on " + label + ".");
  } catch (err) {
    busy = false;
    await loadPresetsInfo();
    msg("Failed to program presets: " + err.message, "bad");
  }
}

$("speakers").addEventListener("click", (ev) => {
  const bDef = ev.target.closest("button[data-id]");
  if (bDef) { makeDefault(bDef.dataset.id); return; }
  const bProg = ev.target.closest("button[data-prog-id]");
  if (bProg) { programPresets(bProg.dataset.progId, false); return; }
});
$("prog-all-presets").onclick = () => programPresets(null, true);
document.addEventListener("visibilitychange", () => { if (document.visibilityState === "visible") { poll(); loadPresetsInfo(); } });
document.addEventListener("keydown", (ev) => {
  if (ev.key === "Escape" && !ev.target.closest("input, textarea")) location.href = "/";
});
poll();
loadPresetsInfo();
setInterval(loadPresetsInfo, 8000);
</script>
</body>
</html>
)PAGE";

// The Speaker Details page, at "/speaker-details?id=...": live inspection of firmware versions, components,
// network interfaces, MAC addresses and serial numbers.
inline const char *SPEAKER_DETAILS_HTML = R"PAGE(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Speaker Details &middot; SoundTouch</title>
<style>
)PAGE" WEB_BASE_CSS R"PAGE(
  .sec-head { font-size: 14px; text-transform: uppercase; letter-spacing: .08em; color: var(--muted); margin: 18px 0 8px; }
  .grid2 { display: grid; grid-template-columns: repeat(auto-fit, minmax(320px, 1fr)); gap: 16px; }
  .box { background: var(--panel2); border: 1px solid var(--line); border-radius: 10px; padding: 12px 14px; }
  .box h3 { margin: 0 0 8px; font-size: 14px; color: var(--fg); font-weight: 600; }
  .kv-table { width: 100%; border-collapse: collapse; }
  .kv-table td { padding: 6px 8px; border-bottom: 1px solid rgba(255,255,255,.05); font-size: 13.5px; }
  .kv-table tr:last-child td { border-bottom: none; }
  .kv-table td:first-child { width: 38%; color: var(--muted); }
  .loading-msg { padding: 40px; text-align: center; color: var(--muted); }
  .err-msg { padding: 18px; border-radius: 10px; border: 1px solid #f85149; background: rgba(248,81,73,.1); color: #f85149; }
</style>
</head>
<body>
<header>
  <a class="brand" href="/" title="Back to the dashboard"><span id="dot" class="dot"></span><h1 id="hdr-title">Speaker Details</h1></a>
  <nav class="tabs">
    <a href="/">Dashboard</a>
    <a href="/speakers">Speakers</a>
    <a href="#" class="on" aria-current="page">Details</a>
  </nav>
  <span class="spacer"></span>
  <a class="btn" href="/speakers">&larr; Speakers</a>
</header>
<main>
  <div id="content" class="loading-msg">Loading speaker details&hellip;</div>
</main>
<script>
const $ = (id) => document.getElementById(id);
const esc = (s) => String(s == null ? "" : s).replace(/[&<>"']/g, c =>
  ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));

)PAGE" IP_MODAL_JS R"PAGE(

document.addEventListener("click", (e) => {
  const btn = e.target.closest(".info-circle-btn");
  if (btn && btn.dataset.change) {
    try {
      const rec = JSON.parse(btn.dataset.change);
      showIpChangeModal(rec, () => {
        loadDetails();
      });
    } catch (_) {}
  }
});

const params = new URLSearchParams(location.search);
const id = params.get("id");

async function loadDetails() {
  if (!id) {
    $("content").innerHTML = '<div class="err-msg">No speaker ID provided in URL (?id=...).</div>';
    return;
  }
  try {
    const res = await fetch("/api/speakers/info?id=" + encodeURIComponent(id), { cache: "no-store" });
    const d = await res.json();
    if (!res.ok) throw new Error(d.error || ("HTTP " + res.status));
    renderDetails(d);
    $("dot").className = "dot ok";
  } catch (err) {
    $("content").innerHTML = '<div class="err-msg">Failed to load speaker info: ' + esc(err.message) + '</div>';
    $("dot").className = "dot bad";
  }
}

function renderDetails(d) {
  $("hdr-title").textContent = (d.name ? d.name + " " : "") + "Details";

  const isRecent = d.ip_change && ((Date.now() - (d.ip_change.timestamp_ms || 0)) < 30000);
  let ipRow = '<tr><td>IP Address</td><td><code>' + esc(d.ip || "\u2014") + '</code>';
  if (d.ip_change) {
    ipRow += ' <button class="info-circle-btn" type="button" title="IP address changed" data-change=\'' + esc(JSON.stringify(d.ip_change)) + '\'>&#9432;</button>';
  }
  ipRow += '</td></tr>';

  let h = '<div class="card' + (isRecent ? ' ip-changed-flash' : '') + '">';
  h += '<div class="card-head"><h2>Overview</h2></div>';
  h += '<table class="kv-table">';
  h += '<tr><td>Device Name</td><td><b>' + esc(d.name || "\u2014") + '</b></td></tr>';
  h += '<tr><td>Device ID / MAC</td><td><code>' + esc(d.device_id || id) + '</code></td></tr>';
  h += '<tr><td>Model / Type</td><td>' + esc(d.type || "\u2014") + '</td></tr>';
  h += '<tr><td>Module Type</td><td>' + esc(d.module_type || "\u2014") + '</td></tr>';
  h += '<tr><td>Variant / Mode</td><td>' + esc(d.variant || "\u2014") + (d.variant_mode ? " (" + esc(d.variant_mode) + ")" : "") + '</td></tr>';
  h += '<tr><td>Country / Region</td><td>' + esc(d.country_code || "\u2014") + (d.region_code ? " / " + esc(d.region_code) : "") + '</td></tr>';
  h += ipRow;
  h += '<tr><td>Bose Account UUID</td><td><code>' + esc(d.marge_account_uuid || "\u2014") + '</code></td></tr>';
  h += '</table>';
  h += '</div>';

  if (d.components && d.components.length) {
    h += '<div class="card">';
    h += '<div class="card-head"><h2>Components &amp; Firmware</h2></div>';
    h += '<div class="grid2">';
    for (const c of d.components) {
      h += '<div class="box">';
      h += '<h3>' + esc(c.category || "Component") + '</h3>';
      h += '<table class="kv-table">';
      if (c.software_version) h += '<tr><td>Software Version</td><td><b>' + esc(c.software_version) + '</b></td></tr>';
      if (c.serial_number) h += '<tr><td>Serial Number</td><td><code>' + esc(c.serial_number) + '</code></td></tr>';
      h += '</table></div>';
    }
    h += '</div></div>';
  }

  if (d.network_info && d.network_info.length) {
    h += '<div class="card">';
    h += '<div class="card-head"><h2>Network Interfaces</h2></div>';
    h += '<div class="grid2">';
    for (const net of d.network_info) {
      h += '<div class="box">';
      h += '<h3>' + esc(net.type || "Interface") + '</h3>';
      h += '<table class="kv-table">';
      h += '<tr><td>MAC Address</td><td><code>' + esc(net.mac_address || "\u2014") + '</code></td></tr>';
      h += '<tr><td>IP Address</td><td><code>' + esc(net.ip_address || "\u2014") + '</code></td></tr>';
      h += '</table></div>';
    }
    h += '</div></div>';
  }

  $("content").className = "";
  $("content").innerHTML = h;
}

loadDetails();
</script>
</body>
</html>
)PAGE";

#endif
