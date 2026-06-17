/**
 * Voice Notes -> Google Calendar bridge (Google Apps Script web app).
 *
 * Runs as YOU, so there is no OAuth client / consent-screen setup and no token
 * juggling — it just calls CalendarApp. See bridge/README.md for deploy steps.
 *
 * Accepts GET (?secret=...&text=...) or POST (JSON { secret, text }), parses a
 * date/time out of the spoken text, and creates a Calendar event (timed, or
 * all-day if no time is found). The watch uses GET: Apps Script 302-redirects
 * its response, and the phone's XMLHttpRequest follows a GET redirect correctly
 * but not a POST one (that showed up on the watch as "Bad response").
 */

var SHARED_SECRET = 'PASTE_A_RANDOM_SECRET_HERE';   // must match SHARED_SECRET in src/pkjs/config.js
var DEFAULT_EVENT_MINUTES = 30;                   // length of a timed event

// Run this ONCE from the editor (Run > authorize) to grant Calendar access.
function authorize() {
  Logger.log(CalendarApp.getDefaultCalendar().getName());
}

// The watch app calls this via GET: /exec?secret=...&text=...
function doGet(e) {
  return handle(e.parameter.secret, e.parameter.text);
}

// POST (JSON { secret, text }) also works, e.g. for curl testing.
function doPost(e) {
  var body = {};
  try { body = JSON.parse(e.postData.contents); } catch (err) {}
  return handle(body.secret, body.text);
}

function handle(secret, text) {
  try {
    if (secret !== SHARED_SECRET) return json({ ok: false, error: 'bad secret' });
    text = (text || '').trim();
    if (!text) return json({ ok: false, error: 'empty' });

    var p = parseWhen(text);
    var cal = CalendarApp.getDefaultCalendar();
    if (p.allDay) {
      cal.createAllDayEvent(p.title, p.date);
    } else {
      var end = new Date(p.date.getTime() + DEFAULT_EVENT_MINUTES * 60000);
      cal.createEvent(p.title, p.date, end);
    }
    return json({ ok: true, when: p.when, title: p.title });
  } catch (err) {
    return json({ ok: false, error: String(err) });
  }
}

function json(obj) {
  return ContentService.createTextOutput(JSON.stringify(obj))
    .setMimeType(ContentService.MimeType.JSON);
}

// ---- naive natural-language date/time parsing (tune to taste) ----
function parseWhen(raw) {
  var s = ' ' + raw.toLowerCase() + ' ';
  var now = new Date();
  var date = new Date(now.getFullYear(), now.getMonth(), now.getDate()); // midnight today
  var haveDay = false, haveTime = false, hour = 9, min = 0;

  // "in N hours/minutes" -> relative to now
  var rel = s.match(/\bin (\d+)\s*(hours?|hrs?|minutes?|mins?)\b/);
  if (rel) {
    var n = parseInt(rel[1], 10);
    var d = new Date(now.getTime());
    if (/hour|hr/.test(rel[2])) d.setHours(d.getHours() + n);
    else d.setMinutes(d.getMinutes() + n);
    return { date: d, allDay: false, title: cleanTitle(raw), when: fmt(d, false) };
  }

  // day words
  if (/\btoday\b/.test(s)) { haveDay = true; }
  else if (/\bday after tomorrow\b/.test(s)) { date.setDate(date.getDate() + 2); haveDay = true; }
  else if (/\btomorrow\b/.test(s)) { date.setDate(date.getDate() + 1); haveDay = true; }
  else {
    var days = ['sunday','monday','tuesday','wednesday','thursday','friday','saturday'];
    for (var i = 0; i < 7; i++) {
      if (new RegExp('\\b' + days[i] + '\\b').test(s)) {
        var delta = (i - date.getDay() + 7) % 7;
        if (delta === 0) delta = 7;            // "monday" => next monday
        date.setDate(date.getDate() + delta);
        haveDay = true;
        break;
      }
    }
  }

  // named times
  if (/\bnoon\b/.test(s)) { hour = 12; haveTime = true; }
  else if (/\bmidnight\b/.test(s)) { hour = 0; haveTime = true; }
  else if (/\bmorning\b/.test(s)) { hour = 9; haveTime = true; }
  else if (/\bafternoon\b/.test(s)) { hour = 14; haveTime = true; }
  else if (/\bevening\b/.test(s)) { hour = 18; haveTime = true; }
  else if (/\btonight\b/.test(s)) { hour = 20; haveTime = true; }

  // explicit clock time: "at 3", "at 3:30pm", "15:00", "3pm"
  var t = s.match(/\b(?:at\s*)?(\d{1,2})(?::(\d{2}))?\s*(am|pm)?\b/);
  if (t && (t[3] || t[2] || /\bat\s*\d/.test(s))) {   // require am/pm, ":mm", or "at"
    var h = parseInt(t[1], 10);
    var m = t[2] ? parseInt(t[2], 10) : 0;
    if (t[3] === 'pm' && h < 12) h += 12;
    if (t[3] === 'am' && h === 12) h = 0;
    if (h >= 0 && h <= 23 && m >= 0 && m <= 59) { hour = h; min = m; haveTime = true; }
  }

  if (haveTime) date.setHours(hour, min, 0, 0);
  // time given but already past today and no day said -> bump to tomorrow
  if (haveTime && !haveDay && date.getTime() < now.getTime()) date.setDate(date.getDate() + 1);

  var allDay = !haveTime;
  return { date: date, allDay: allDay, title: cleanTitle(raw), when: fmt(date, allDay) };
}

function cleanTitle(raw) {
  return raw.replace(/^\s*(remind me to|reminder to|remember to|note to self to|note to)\s+/i, '')
            .replace(/\s+/g, ' ').trim();
}

function fmt(d, allDay) {
  var tz = Session.getScriptTimeZone();
  return allDay ? Utilities.formatDate(d, tz, 'EEE d MMM') + ' (all day)'
                : Utilities.formatDate(d, tz, 'EEE d MMM HH:mm');
}
