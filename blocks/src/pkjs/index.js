// Phone-side companion for Bauhaus Blocks watchface.
// 1. Weather: Open-Meteo (no API key) — current temp + today's hi/lo.
// 2. Settings: a self-contained colour-theme picker (data-URI config page,
//    no hosting / no external libs), persisted on the watch.

var API = 'https://api.open-meteo.com/v1/forecast';

// Theme name + its five band swatches (day/date/time/batt/temp), matching the
// THEMES table in src/c/main.c. Indices MUST stay in sync.
var THEMES = [
  { name: 'Bauhaus', swatch: ['#FFFF00', '#55FFAA', '#FFFFFF', '#0000FF', '#FFAA00'] },
  { name: 'Noir',    swatch: ['#FFFFFF', '#AAAAAA', '#000000', '#555555', '#FFFFFF'] },
  { name: 'Pop',     swatch: ['#FF0055', '#FFAA00', '#FFFFFF', '#005500', '#5500FF'] },
  { name: 'Earth',   swatch: ['#AAAA00', '#AA5500', '#FFFFAA', '#000055', '#00AA00'] }
];

function send(dict) {
  Pebble.sendAppMessage(dict, function () {}, function (e) {
    console.log('send failed: ' + JSON.stringify(e));
  });
}

function fetchWeather(lat, lon) {
  var url = API + '?latitude=' + lat + '&longitude=' + lon +
    '&current=temperature_2m' +
    '&daily=temperature_2m_max,temperature_2m_min' +
    '&timezone=auto&forecast_days=1';

  var xhr = new XMLHttpRequest();
  xhr.open('GET', url, true);
  xhr.timeout = 15000;
  xhr.onload = function () {
    try {
      var d = JSON.parse(xhr.responseText);
      send({
        TempNow: Math.round(d.current.temperature_2m),
        TempHi:  Math.round(d.daily.temperature_2m_max[0]),
        TempLo:  Math.round(d.daily.temperature_2m_min[0])
      });
    } catch (e) {
      send({ WErr: 1 });
    }
  };
  xhr.onerror   = function () { send({ WErr: 1 }); };
  xhr.ontimeout = function () { send({ WErr: 1 }); };
  xhr.send();
}

function update() {
  navigator.geolocation.getCurrentPosition(
    function (pos) { fetchWeather(pos.coords.latitude, pos.coords.longitude); },
    function ()    { fetchWeather(52.37, 4.89); },   // fallback: Amsterdam
    { timeout: 15000, maximumAge: 600000 }
  );
}

// --- settings -------------------------------------------------------------

function currentTheme() {
  var n = parseInt(localStorage.getItem('theme'), 10);
  return (n >= 0 && n < THEMES.length) ? n : 0;
}

function configPage(title, themes, sel) {
  var rows = themes.map(function (t, i) {
    var sw = t.swatch.map(function (c) {
      return '<span class="sw" style="background:' + c + '"></span>';
    }).join('');
    return '<label class="row">' +
      '<input type="radio" name="t" value="' + i + '"' + (i === sel ? ' checked' : '') + '>' +
      '<span class="nm">' + t.name + '</span><span class="bar">' + sw + '</span></label>';
  }).join('');

  var html =
    '<!DOCTYPE html><html><head><meta name="viewport" content="width=device-width,initial-scale=1">' +
    '<style>' +
    'body{margin:0;font-family:-apple-system,Helvetica,Arial,sans-serif;background:#111;color:#eee}' +
    'h1{font-size:18px;font-weight:600;padding:18px 16px 6px}' +
    '.row{display:flex;align-items:center;padding:14px 16px;border-top:1px solid #222}' +
    '.row input{margin-right:12px;transform:scale(1.3)}' +
    '.nm{flex:0 0 78px;font-size:16px}' +
    '.bar{display:flex;flex:1;height:22px;border-radius:4px;overflow:hidden}' +
    '.sw{flex:1}' +
    'button{width:calc(100% - 32px);margin:22px 16px 30px;padding:15px;font-size:17px;' +
    'border:0;border-radius:10px;background:#2d8cff;color:#fff}' +
    '</style></head><body>' +
    '<h1>' + title + ' &middot; theme</h1>' + rows +
    '<button id="save">Save</button>' +
    '<script>document.getElementById("save").onclick=function(){' +
    'var v=document.querySelector("input[name=t]:checked").value;' +
    'document.location="pebblejs://close#"+encodeURIComponent(JSON.stringify({theme:+v}));};' +
    '</script></body></html>';
  return 'data:text/html,' + encodeURIComponent(html);
}

Pebble.addEventListener('showConfiguration', function () {
  Pebble.openURL(configPage('Bauhaus Blocks', THEMES, currentTheme()));
});

Pebble.addEventListener('webviewclosed', function (e) {
  if (!e || !e.response) return;
  try {
    var cfg = JSON.parse(decodeURIComponent(e.response));
    if (typeof cfg.theme === 'number') {
      localStorage.setItem('theme', cfg.theme);
      send({ Theme: cfg.theme });
    }
  } catch (err) {
    console.log('config parse failed: ' + err);
  }
});

Pebble.addEventListener('ready', function () { update(); });
Pebble.addEventListener('appmessage', function () { update(); });
