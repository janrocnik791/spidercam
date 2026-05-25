#pragma once
// =============================================================================
// html_ui.h — Web UI for Argus V1.3
// Stored in flash (PROGMEM). Served by hRoot() in v1_3_foundation.ino.
// Edit this file to change the UI. The .ino recompiles it automatically.
// =============================================================================

const char HTML[] PROGMEM = R"html(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1,user-scalable=no">
<title>Argus V1.3</title>
<style>
*{box-sizing:border-box;margin:0;padding:0;-webkit-tap-highlight-color:transparent}
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;background:#18181b;color:#f4f4f5;padding:16px;max-width:500px;margin:0 auto}
h1{font-size:18px;font-weight:700;letter-spacing:-.3px;margin-bottom:4px}
#st{font-size:12px;color:#a1a1aa;margin-bottom:16px;min-height:16px}
.card{background:#27272a;border-radius:10px;padding:14px 16px;margin-bottom:12px}
.card h2{font-size:10px;font-weight:700;color:#71717a;text-transform:uppercase;letter-spacing:1px;margin-bottom:12px}
/* Cable display */
.cgrid{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin-bottom:10px}
.cbox{background:#3f3f46;border-radius:8px;padding:10px;text-align:center}
.clbl{font-size:10px;color:#a1a1aa;margin-bottom:3px}
.cval{font-size:22px;font-weight:700;font-variant-numeric:tabular-nums;letter-spacing:-.5px}
.cunit{font-size:10px;color:#71717a}
/* Buttons */
button{display:block;padding:14px 8px;border:none;border-radius:8px;font-size:13px;font-weight:700;cursor:pointer;width:100%;letter-spacing:.2px}
button:active{filter:brightness(.85)}
button:disabled{background:#3f3f46!important;color:#52525b!important;cursor:not-allowed;filter:none}
.pgrid{display:grid;grid-template-columns:repeat(5,1fr);gap:6px}
.blue{background:#2563eb;color:#fff}
.green{background:#16a34a;color:#fff}
.red{background:#dc2626;color:#fff}
.amber{background:#d97706;color:#fff}
.muted{background:#3f3f46;color:#a1a1aa;font-size:11px;padding:10px 8px}
/* Motor jog rows */
.mrow{display:grid;grid-template-columns:36px 1fr 1fr;gap:6px;align-items:center;margin-bottom:6px}
.mrow:last-child{margin-bottom:0}
.mlbl{font-size:13px;font-weight:700;color:#71717a;text-align:center}
/* Rotation input */
.rrow{display:flex;align-items:center;gap:10px;margin-bottom:12px}
.rrow label{font-size:12px;color:#a1a1aa;white-space:nowrap}
.rrow input{flex:1;background:#3f3f46;border:1.5px solid #52525b;border-radius:6px;color:#f4f4f5;padding:8px 10px;font-size:16px;text-align:center;outline:none;width:0}
/* Settings rows */
.srow{display:flex;align-items:center;gap:8px;margin-bottom:8px}
.srow:last-of-type{margin-bottom:0}
.srow label{font-size:12px;color:#a1a1aa;min-width:72px;flex-shrink:0}
.srow input{flex:1;background:#3f3f46;border:1.5px solid #52525b;border-radius:6px;color:#f4f4f5;padding:8px 10px;font-size:14px;outline:none;width:0}
.srow button{width:56px;padding:8px;flex-shrink:0;font-size:12px}
.calinfo{font-size:11px;color:#52525b;margin-top:8px;text-align:center}
/* Auto-pass */
.passSeq{display:flex;flex-wrap:wrap;gap:4px;align-items:center;min-height:28px;margin-bottom:10px;padding:6px 8px;background:#18181b;border-radius:7px}
.chip{background:#3f3f46;border-radius:5px;padding:3px 8px;font-size:12px;font-weight:700;white-space:nowrap}
.arr{color:#52525b;margin:0 2px;font-size:12px}
.passCount{font-size:10px;color:#52525b;margin-bottom:8px;text-align:right}
.passAddRow{display:grid;grid-template-columns:repeat(5,1fr);gap:5px;margin-bottom:8px}
.passAddRow button{font-size:11px;padding:8px 4px;background:#3f3f46;color:#a1a1aa}
.passActions{display:flex;gap:8px}
.passActions button{flex:1;padding:12px}
</style>
</head>
<body>

<h1>Argus V1.3</h1>
<div id="st">Connecting…</div>

<div class="card">
  <h2>Current Cable Lengths</h2>
  <div class="cgrid">
    <div class="cbox"><div class="clbl">M1</div><div class="cval" id="v0">—</div><div class="cunit">cm</div></div>
    <div class="cbox"><div class="clbl">M2</div><div class="cval" id="v1">—</div><div class="cunit">cm</div></div>
    <div class="cbox"><div class="clbl">M3</div><div class="cval" id="v2">—</div><div class="cunit">cm</div></div>
    <div class="cbox"><div class="clbl">M4</div><div class="cval" id="v3">—</div><div class="cunit">cm</div></div>
  </div>
  <button class="muted" onclick="anchor()">Anchor — declare I am physically at HOME</button>
</div>

<div class="card">
  <h2>Go to Position</h2>
  <div class="pgrid">
    <button class="blue" onclick="go('home')">Home</button>
    <button class="blue" onclick="go('c1')">C1</button>
    <button class="blue" onclick="go('c2')">C2</button>
    <button class="blue" onclick="go('c3')">C3</button>
    <button class="blue" onclick="go('c4')">C4</button>
  </div>
</div>

<div class="card">
  <h2>Motor Jog</h2>
  <div class="rrow">
    <label>Rotations</label>
    <input type="number" id="rot" value="0.5" step="0.25" min="0.25" max="20">
  </div>
  <div class="mrow"><span class="mlbl">M1</span><button class="green" onclick="jog(0,1)">&#8593; Wind</button><button class="red" onclick="jog(0,-1)">&#8595; Unwind</button></div>
  <div class="mrow"><span class="mlbl">M2</span><button class="green" onclick="jog(1,1)">&#8593; Wind</button><button class="red" onclick="jog(1,-1)">&#8595; Unwind</button></div>
  <div class="mrow"><span class="mlbl">M3</span><button class="green" onclick="jog(2,1)">&#8593; Wind</button><button class="red" onclick="jog(2,-1)">&#8595; Unwind</button></div>
  <div class="mrow"><span class="mlbl">M4</span><button class="green" onclick="jog(3,1)">&#8593; Wind</button><button class="red" onclick="jog(3,-1)">&#8595; Unwind</button></div>
</div>

<div class="card">
  <h2>Record Position — jog to spot first, then save</h2>
  <div class="pgrid">
    <button class="amber" onclick="setpos('home')">Home</button>
    <button class="amber" onclick="setpos('c1')">C1</button>
    <button class="amber" onclick="setpos('c2')">C2</button>
    <button class="amber" onclick="setpos('c3')">C3</button>
    <button class="amber" onclick="setpos('c4')">C4</button>
  </div>
</div>

<div class="card">
  <h2>Auto Pass</h2>
  <div class="passSeq" id="passSeq"><span style="color:#52525b">Empty</span></div>
  <div class="passCount" id="passCount">0 / 20 steps</div>
  <div style="font-size:10px;color:#71717a;text-transform:uppercase;letter-spacing:1px;margin-bottom:6px">Add position to pass</div>
  <div class="passAddRow">
    <button onclick="passAdd('home')">Home</button>
    <button onclick="passAdd('c1')">C1</button>
    <button onclick="passAdd('c2')">C2</button>
    <button onclick="passAdd('c3')">C3</button>
    <button onclick="passAdd('c4')">C4</button>
  </div>
  <div class="passActions">
    <button class="red" onclick="passClear()">Clear</button>
    <button class="green" onclick="passRun()">&#9654; Run Pass</button>
  </div>
</div>

<div class="card">
  <h2>Settings</h2>
  <div class="srow">
    <label>Speed (µs)</label>
    <input type="number" id="spd" step="100" min="500" max="20000">
    <button class="blue" onclick="applySpeed()">Set</button>
  </div>
  <div class="srow">
    <label>cm / rot</label>
    <input type="number" id="cpr" step="0.1" min="1">
    <button class="blue" onclick="applyCpr()">Set</button>
  </div>
  <div class="calinfo" id="cal"></div>
</div>

<script>
var busy = false;

function setStatus(t) { document.getElementById('st').textContent = t; }

function setAllDisabled(d) {
  document.querySelectorAll('button').forEach(function(b) { b.disabled = d; });
}

function update(data) {
  var c = data.current;
  for (var i = 0; i < 4; i++)
    document.getElementById('v' + i).textContent = c[i].toFixed(1);
  document.getElementById('spd').value = data.go_us;
  document.getElementById('cpr').value = data.cpr.toFixed(2);
  document.getElementById('cal').textContent =
    data.cpr.toFixed(2) + ' cm/rot  →  ' + data.spc.toFixed(4) + ' steps/cm';
}

function req(url, label) {
  if (busy) return;
  busy = true;
  setAllDisabled(true);
  setStatus((label || 'Working') + '…');
  fetch(url, { method: 'POST' })
    .then(function(r) { return r.json(); })
    .then(function(d) { update(d); setStatus('Ready'); })
    .catch(function() { setStatus('Error — check serial monitor'); })
    .finally(function() { busy = false; setAllDisabled(false); });
}

function go(pos)     { req('/go?pos=' + pos, 'Moving to ' + pos.toUpperCase()); }
function anchor()    { req('/anchor', 'Anchoring'); }
function applySpeed(){ req('/speed?us=' + document.getElementById('spd').value, 'Setting speed'); }
function applyCpr()  { req('/cpr?val=' + document.getElementById('cpr').value, 'Setting calibration'); }

function jog(m, dir) {
  var rot = (parseFloat(document.getElementById('rot').value) || 0.5) * dir;
  req('/jog?m=' + m + '&rot=' + rot, 'Jogging M' + (m + 1));
}

function setpos(pos) {
  if (!confirm('Save current cables as "' + pos + '"?\nThis overwrites the stored reference.')) return;
  req('/setpos?pos=' + pos, 'Recording ' + pos.toUpperCase());
}

function renderPass(d) {
  var chips = d.pass.map(function(p) {
    return '<span class="chip">' + p.toUpperCase() + '</span>';
  });
  document.getElementById('passSeq').innerHTML =
    chips.length ? chips.join('<span class="arr">→</span>') : '<span style="color:#52525b">Empty</span>';
  document.getElementById('passCount').textContent = d.len + ' / ' + d.max + ' steps';
}

function updatePass() {
  fetch('/pass/status')
    .then(function(r) { return r.json(); })
    .then(renderPass);
}

function passAdd(pos) {
  if (busy) return;
  busy = true; setAllDisabled(true); setStatus('Adding ' + pos.toUpperCase() + '…');
  fetch('/pass/add?pos=' + pos, { method: 'POST' })
    .then(function(r) { return r.json(); })
    .then(function(d) { renderPass(d); setStatus('Ready'); })
    .catch(function() { setStatus('Error'); })
    .finally(function() { busy = false; setAllDisabled(false); });
}

function passClear() {
  if (busy) return;
  busy = true; setAllDisabled(true); setStatus('Clearing pass…');
  fetch('/pass/clear', { method: 'POST' })
    .then(function(r) { return r.json(); })
    .then(function(d) { renderPass(d); setStatus('Ready'); })
    .catch(function() { setStatus('Error'); })
    .finally(function() { busy = false; setAllDisabled(false); });
}

function passRun() {
  if (busy) return;
  busy = true; setAllDisabled(true); setStatus('Running pass — motors moving…');
  fetch('/pass/run', { method: 'POST' })
    .then(function(r) { return r.json(); })
    .then(function() {
      setStatus('Pass running, waiting for motors…');
      function pollStatus(attempts) {
        fetch('/status')
          .then(function(r) { return r.json(); })
          .then(function(d) { update(d); setStatus('Pass complete'); busy = false; setAllDisabled(false); })
          .catch(function() {
            if (attempts > 0) setTimeout(function(){ pollStatus(attempts - 1); }, 2000);
            else { setStatus('Pass done — press any button to refresh'); busy = false; setAllDisabled(false); }
          });
      }
      setTimeout(function(){ pollStatus(15); }, 1000);
    })
    .catch(function() { setStatus('Error — check serial monitor'); busy = false; setAllDisabled(false); });
}

// Initial load — use data inlined by hRoot() if available (avoids a round-trip fetch)
if (window._st) { update(window._st); setStatus('Ready'); }
else {
  fetch('/status')
    .then(function(r) { return r.json(); })
    .then(function(d) { update(d); setStatus('Ready'); })
    .catch(function() { setStatus('Cannot reach ESP — check WiFi'); });
}
if (window._ps) { renderPass(window._ps); }
else { updatePass(); }
</script>

</body>
</html>)html";
