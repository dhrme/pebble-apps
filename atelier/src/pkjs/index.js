// Phone-side companion for Atelier. Settings only: a self-contained colour-
// theme picker (data-URI config page, no hosting / no external libs),
// persisted on the watch.

// Theme name + swatches (bg / ink / accent), matching THEMES in src/c/main.c.
// Indices MUST stay in sync.
var THEMES = [
  { name: 'Atelier', swatch: ['#FFFFFF', '#000000', '#FFAA00'] },
  { name: 'Noir',    swatch: ['#000000', '#FFFFFF', '#FFAA00'] },
  { name: 'Sage',    swatch: ['#FFFFFF', '#000000', '#00AA00'] },
  { name: 'Blush',   swatch: ['#FFFFFF', '#000000', '#FF0055'] }
];

function send(dict) {
  Pebble.sendAppMessage(dict, function () {}, function (e) {
    console.log('send failed: ' + JSON.stringify(e));
  });
}

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
    '.bar{display:flex;flex:1;height:22px;border-radius:4px;overflow:hidden;border:1px solid #333}' +
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
  Pebble.openURL(configPage('Atelier', THEMES, currentTheme()));
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
