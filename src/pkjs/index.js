var Clay = require('@rebble/clay');
var clayConfig = require('./config');
var clay = new Clay(clayConfig);

function getUnit() {
  return localStorage.getItem('unit') || 'F';
}

function sendToWatch(tempC) {
  var payload = {
    TEMPERATURE_C: Math.round(tempC),
    UNIT: getUnit()
  };
  Pebble.sendAppMessage(payload,
    function() { console.log('weather sent: ' + JSON.stringify(payload)); },
    function(e) { console.log('weather send failed: ' + JSON.stringify(e)); }
  );
}

function sendUnitOnly() {
  Pebble.sendAppMessage({ UNIT: getUnit() });
}

function fetchWeather() {
  if (!navigator.geolocation) {
    console.log('no geolocation available');
    return;
  }
  navigator.geolocation.getCurrentPosition(
    function(pos) {
      var url = 'https://api.open-meteo.com/v1/forecast'
        + '?latitude='  + pos.coords.latitude
        + '&longitude=' + pos.coords.longitude
        + '&current=temperature_2m';
      var xhr = new XMLHttpRequest();
      xhr.onload = function() {
        if (xhr.status >= 200 && xhr.status < 300) {
          try {
            var data = JSON.parse(xhr.responseText);
            sendToWatch(data.current.temperature_2m);
          } catch (e) {
            console.log('weather parse error: ' + e);
          }
        } else {
          console.log('weather http ' + xhr.status);
        }
      };
      xhr.onerror = function() { console.log('weather xhr error'); };
      xhr.open('GET', url, true);
      xhr.send();
    },
    function(err) { console.log('geolocation error: ' + err.message); },
    { timeout: 15000, maximumAge: 600000 }
  );
}

Pebble.addEventListener('ready', function() {
  console.log('PebbleKit JS ready');
  fetchWeather();
});

Pebble.addEventListener('appmessage', function(e) {
  if (e.payload && e.payload.FETCH_WEATHER) {
    fetchWeather();
  }
});

Pebble.addEventListener('webviewclosed', function(e) {
  if (!e || !e.response) return;
  var settings = clay.getSettings(e.response);
  if (settings.UNIT) {
    localStorage.setItem('unit', settings.UNIT);
  }
  sendUnitOnly();
  fetchWeather();
});
