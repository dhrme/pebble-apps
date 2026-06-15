// Clay configuration for the NL Energy watchface.
// Lets the user pick the startup layout and toggle tap-to-switch.
module.exports = [
  { "type": "heading", "defaultValue": "NL Energy watchface" },
  {
    "type": "section",
    "items": [
      { "type": "heading", "defaultValue": "Layout" },
      {
        "type": "radiogroup",
        "messageKey": "CfgLayout",
        "defaultValue": "0",
        "options": [
          { "label": "Clock hero (with graph)", "value": "0" },
          { "label": "Price frame (with graph)", "value": "1" },
          { "label": "Minimal (no graph)", "value": "2" }
        ]
      },
      {
        "type": "toggle",
        "messageKey": "CfgTap",
        "label": "Tap / shake to switch layout",
        "defaultValue": true
      }
    ]
  },
  { "type": "submit", "defaultValue": "Save" }
];
