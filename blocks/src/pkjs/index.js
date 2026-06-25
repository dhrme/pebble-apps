// Phone-side companion for Bauhaus Blocks watchface.
// Gets current location, queries Open-Meteo (no API key) for the current
// temperature plus today's high/low, and sends rounded °C ints to the watch.

var API = 'https://api.open-meteo.com/v1/forecast';

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

Pebble.addEventListener('ready', function () { update(); });
Pebble.addEventListener('appmessage', function () { update(); });
