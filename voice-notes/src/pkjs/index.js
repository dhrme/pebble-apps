// Phone-side bridge for Voice Notes.
// Receives each dictated note from the watch and POSTs it to a Google Apps
// Script web app that creates a Calendar event. See bridge/Code.gs + README.md.
//
// Config (the web-app URL + shared secret) lives in config.js, which is
// gitignored. Copy config.example.js to config.js and fill it in before building.

var config = require('./config');
var SCRIPT_URL = config.SCRIPT_URL;
var SHARED_SECRET = config.SHARED_SECRET;

function reply(ok, msg) {
  Pebble.sendAppMessage({ CalOk: ok ? '1' : '0', CalMsg: String(msg || '').substring(0, 64) });
}

function sendToCalendar(text) {
  if (!SCRIPT_URL || SCRIPT_URL.indexOf('PASTE_') === 0) {
    reply(false, 'No script URL set');
    return;
  }
  var xhr = new XMLHttpRequest();
  xhr.open('POST', SCRIPT_URL, true);
  xhr.setRequestHeader('Content-Type', 'application/json');
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
  xhr.send(JSON.stringify({ secret: SHARED_SECRET, text: text }));
}

Pebble.addEventListener('ready', function () {
  console.log('Voice Notes bridge ready');
});

Pebble.addEventListener('appmessage', function (e) {
  if (e.payload.NoteText) sendToCalendar(e.payload.NoteText);
});
