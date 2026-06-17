// Phone-side bridge for Voice Notes.
// Each dictated note is sent to a Google Apps Script web app (the "bridge")
// that creates a Calendar event. The bridge URL + shared secret are entered in
// the app's Settings screen (Pebble app → Voice Notes → gear/Settings) and kept
// on the phone — so the shipped .pbw works for anyone, no rebuild needed.
// See bridge/Code.gs + bridge/README.md to deploy your own bridge.

function getCfg() {
  return {
    url: (localStorage.getItem('vn_url') || '').trim(),
    secret: (localStorage.getItem('vn_secret') || '').trim()
  };
}

function reply(ok, msg) {
  Pebble.sendAppMessage({ CalOk: ok ? '1' : '0', CalMsg: String(msg || '').substring(0, 64) });
}

function sendToCalendar(text) {
  var cfg = getCfg();
  if (!cfg.url || cfg.url.indexOf('PASTE_') === 0) {
    reply(false, 'Open Settings → add bridge URL');
    return;
  }
  // Use GET, not POST: Apps Script 302-redirects its response; the phone's XHR
  // follows a GET redirect correctly but mangles a POST one ("Bad response").
  var url = cfg.url + (cfg.url.indexOf('?') < 0 ? '?' : '&') +
            'secret=' + encodeURIComponent(cfg.secret) +
            '&text=' + encodeURIComponent(text);
  var xhr = new XMLHttpRequest();
  xhr.open('GET', url, true);
  xhr.timeout = 15000;
  xhr.onload = function () {
    try {
      var r = JSON.parse(xhr.responseText);
      if (r.ok) reply(true, r.when || 'Added');
      else reply(false, r.error || 'Calendar error');
    } catch (e) {
      reply(false, 'Bad response');   // e.g. an HTML error page
    }
  };
  xhr.onerror = function () { reply(false, 'No network'); };
  xhr.ontimeout = function () { reply(false, 'Timeout'); };
  xhr.send();
}

Pebble.addEventListener('ready', function () {
  console.log('Voice Notes bridge ready');
});

Pebble.addEventListener('appmessage', function (e) {
  if (e.payload.NoteText) sendToCalendar(e.payload.NoteText);
});

// ---- Settings screen: connect your calendar (paste-your-own-bridge) ----
Pebble.addEventListener('showConfiguration', function () {
  Pebble.openURL(configPage(getCfg()));
});

Pebble.addEventListener('webviewclosed', function (e) {
  if (!e || !e.response) return;
  var s;
  try { s = JSON.parse(decodeURIComponent(e.response)); }
  catch (err) { try { s = JSON.parse(e.response); } catch (e2) { return; } }
  if (!s || s.cancel) return;
  if (typeof s.url === 'string') localStorage.setItem('vn_url', s.url.trim());
  if (typeof s.secret === 'string') localStorage.setItem('vn_secret', s.secret.trim());
});

function configPage(cfg) {
  var cur = JSON.stringify({ url: cfg.url || '', secret: cfg.secret || '' });
  var html =
'<!DOCTYPE html><html><head><meta charset="utf-8">' +
'<meta name="viewport" content="width=device-width,initial-scale=1">' +
'<title>Voice Notes — Calendar setup</title><style>' +
'*{box-sizing:border-box}body{font:16px/1.5 -apple-system,Roboto,Segoe UI,sans-serif;' +
'margin:0;padding:20px;max-width:560px;color:#1a1a1a;background:#fff}' +
'h1{font-size:20px;margin:0 0 4px}p.sub{color:#666;margin:0 0 20px}' +
'ol{padding-left:20px;color:#333}ol li{margin:6px 0}' +
'a{color:#1a6fd4}label{display:block;font-weight:600;margin:16px 0 6px}' +
'input{width:100%;padding:11px;font-size:15px;border:1px solid #ccc;border-radius:8px}' +
'.row{display:flex;gap:8px}.row input{flex:1}' +
'button{font:inherit;border:0;border-radius:8px;padding:11px 16px;cursor:pointer}' +
'.gen{background:#eee;color:#333}.save{width:100%;margin-top:24px;padding:14px;' +
'background:#1a6fd4;color:#fff;font-weight:600;font-size:16px}' +
'.cancel{display:block;text-align:center;margin-top:12px;color:#888;background:none}' +
'details{margin:16px 0;background:#f6f7f9;border-radius:8px;padding:12px 14px}' +
'summary{font-weight:600;cursor:pointer}</style></head><body>' +
'<h1>Connect your calendar</h1>' +
'<p class="sub">Notes you dictate become Google Calendar events. This runs through ' +
'a tiny script in <b>your own</b> Google account — so only you can write to your calendar.</p>' +
'<details open><summary>One-time setup (≈3 min)</summary><ol>' +
'<li>Open <a href="https://script.google.com" target="_blank">script.google.com</a> → <b>New project</b>.</li>' +
'<li>Paste in the bridge code from <b>bridge/Code.gs</b> ' +
'(<a href="https://github.com/dhrme/pebble-apps/blob/main/voice-notes/bridge/Code.gs" target="_blank">copy it here</a>).</li>' +
'<li>Set its <code>SHARED_SECRET</code> to the secret below, run <b>authorize</b> once, allow access.</li>' +
'<li>Deploy → <b>Web app</b> (<i>Execute as: Me</i>, <i>Who has access: Anyone</i>).</li>' +
'<li>Copy the <code>/exec</code> URL into <b>Bridge URL</b> below.</li>' +
'</ol></details>' +
'<label for="u">Bridge URL</label>' +
'<input id="u" type="url" placeholder="https://script.google.com/macros/s/…/exec">' +
'<label for="s">Shared secret</label>' +
'<div class="row"><input id="s" type="text" placeholder="must match Code.gs">' +
'<button type="button" class="gen" onclick="g()">Generate</button></div>' +
'<button class="save" onclick="save(0)">Save</button>' +
'<button class="cancel" onclick="save(1)">Cancel</button>' +
'<script>var CUR=' + cur + ';' +
'document.getElementById("u").value=CUR.url;document.getElementById("s").value=CUR.secret;' +
'if(!CUR.secret)g();' +
'function g(){var h="";for(var i=0;i<24;i++)h+="0123456789abcdef"[Math.floor(Math.random()*16)];' +
'document.getElementById("s").value=h}' +
'function save(c){var o;if(c){o={cancel:1}}else{o={url:document.getElementById("u").value,' +
'secret:document.getElementById("s").value}}' +
'document.location="pebblejs://close#"+encodeURIComponent(JSON.stringify(o))}<\/script>' +
'</body></html>';
  return 'data:text/html,' + encodeURIComponent(html);
}
