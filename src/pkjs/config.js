module.exports = [
  {
    "type": "heading",
    "defaultValue": "Watchface Settings"
  },
  {
    "type": "text",
    "defaultValue": "Temperature is fetched from your phone's location every 30 minutes."
  },
  {
    "type": "section",
    "items": [
      {
        "type": "radiogroup",
        "messageKey": "UNIT",
        "label": "Temperature unit",
        "defaultValue": "F",
        "options": [
          { "label": "Fahrenheit (\u00B0F)", "value": "F" },
          { "label": "Celsius (\u00B0C)",    "value": "C" }
        ]
      }
    ]
  },
  {
    "type": "submit",
    "defaultValue": "Save"
  }
];
