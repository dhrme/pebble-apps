// Phone-side companion for the NL Energy watchapp.
// Fetches dynamic electricity prices from EnergyZero (no auth), builds the
// forward-24h hourly series, finds the cheapest contiguous 4h block, and
// sends a compact packet to the watch.

var Clay = require('pebble-clay');
var clayConfig = require('./config');
var clay = new Clay(clayConfig);   // auto-handles config page + sends settings to the watch

var API = 'https://api.energyzero.nl/v1/energyprices';

function pad(n) { return (n < 10 ? '0' : '') + n; }

function isoZ(d) {
  return d.getUTCFullYear() + '-' + pad(d.getUTCMonth() + 1) + '-' + pad(d.getUTCDate()) +
    'T' + pad(d.getUTCHours()) + ':' + pad(d.getUTCMinutes()) + ':' + pad(d.getUTCSeconds()) + '.000Z';
}

function fetchPrices(cb) {
  var now = new Date();
  // Pull a window that safely covers the current hour through +24h.
  var from = new Date(now.getTime() - 4 * 3600 * 1000);
  var till = new Date(now.getTime() + 27 * 3600 * 1000);
  var url = API + '?fromDate=' + isoZ(from) + '&tillDate=' + isoZ(till) +
    '&interval=4&usageType=1&inclBtw=true';

  var xhr = new XMLHttpRequest();
  xhr.open('GET', url, true);
  xhr.timeout = 15000;
  xhr.onload = function () {
    try {
      var data = JSON.parse(xhr.responseText);
      cb(null, (data && data.Prices) ? data.Prices : []);
    } catch (e) {
      cb('parse error', null);
    }
  };
  xhr.onerror = function () { cb('no network', null); };
  xhr.ontimeout = function () { cb('timeout', null); };
  xhr.send();
}

// Build the price series: a short past lead-in + the upcoming hours.
function build(prices) {
  var PAST = 2;                      // hours of past context to show
  var d = new Date();
  d.setMinutes(0, 0, 0);
  var nowMs = d.getTime();          // start of the current hour

  var map = {};
  for (var i = 0; i < prices.length; i++) {
    map[new Date(prices[i].readingDate).getTime()] = prices[i].price;
  }

  var series = [], nowIndex = -1, firstT = null;
  for (var h = -PAST; h < 24; h++) {
    var t = nowMs + h * 3600 * 1000;
    var v = map[t];
    if (v === undefined) {
      if (h < 0) continue;          // missing past hour: just show fewer
      break;                        // missing future hour: stop here
    }
    if (series.length === 0) firstT = t;
    if (h === 0) nowIndex = series.length;
    series.push(v);
  }
  if (series.length === 0) return null;
  if (nowIndex < 0) nowIndex = 0;

  var min = series[0], max = series[0];
  for (var k = 0; k < series.length; k++) {
    if (series[k] < min) min = series[k];
    if (series[k] > max) max = series[k];
  }

  // cheapest contiguous block, searched over UPCOMING hours only
  var L = Math.min(4, series.length - nowIndex);
  var bestStart = nowIndex, bestSum = null;
  for (var s = nowIndex; s + L <= series.length; s++) {
    var sum = 0;
    for (var c = 0; c < L; c++) sum += series[s + c];
    if (bestSum === null || sum < bestSum) { bestSum = sum; bestStart = s; }
  }

  return {
    series: series, min: min, max: max,
    now: series[nowIndex], nowIndex: nowIndex,
    bestStart: bestStart, bestLen: L, bestAvg: bestSum / L,
    nowHour: new Date(nowMs).getHours(),
    startHour: new Date(firstT).getHours()
  };
}

function M(x) { return Math.round(x * 1000); }   // EUR/kWh -> integer milli-euro

function send(m) {
  var bytes = [];
  for (var i = 0; i < m.series.length; i++) {
    var v = Math.round(m.series[i] * 1000) & 0xFFFF;   // int16 LE
    bytes.push(v & 0xFF);
    bytes.push((v >> 8) & 0xFF);
  }
  var dict = {
    NowPriceM: M(m.now), MinM: M(m.min), MaxM: M(m.max),
    Count: m.series.length, NowHour: m.nowHour, StartHour: m.startHour,
    NowIndex: m.nowIndex,
    BestStart: m.bestStart, BestLen: m.bestLen, BestAvgM: M(m.bestAvg),
    Series: bytes
  };
  Pebble.sendAppMessage(dict, function () {}, function (e) {
    console.log('AppMessage send failed: ' + JSON.stringify(e));
  });
}

function update() {
  fetchPrices(function (err, prices) {
    if (err) { Pebble.sendAppMessage({ Err: err }); return; }
    var m = build(prices);
    if (!m) { Pebble.sendAppMessage({ Err: 'no data' }); return; }
    send(m);
  });
}

Pebble.addEventListener('ready', function () { update(); });
Pebble.addEventListener('appmessage', function () { update(); });
