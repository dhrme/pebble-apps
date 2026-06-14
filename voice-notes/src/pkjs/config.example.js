// Voice Notes — phone-side configuration TEMPLATE.
//
//   cp config.example.js config.js     # then edit config.js
//
// config.js is gitignored, so your real URL/secret are never committed.

module.exports = {
  // Your deployed Google Apps Script web-app URL (ends in /exec).
  // See ../../bridge/README.md for how to deploy bridge/Code.gs.
  SCRIPT_URL: 'PASTE_YOUR_APPS_SCRIPT_WEB_APP_URL_HERE',

  // Any random string. Must be IDENTICAL to SHARED_SECRET in your deployed
  // bridge/Code.gs.  Generate one with:  openssl rand -hex 12
  SHARED_SECRET: 'PASTE_A_RANDOM_SECRET_HERE'
};
