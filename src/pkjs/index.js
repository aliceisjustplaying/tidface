var Clay = require("pebble-clay");
var clayConfig = require("./config");
var clay = new Clay(clayConfig);

// -------- Airport Info Bridge ------------------------------------------
// Message key IDs must match those in watchface.c
var KEY_REQUEST_TYPE = 200;
var KEY_AIRPORT_CODE = 201;
var KEY_CITY         = 202;
var KEY_COUNTRY      = 203;

var REQUEST_AIRPORT_INFO = 1;

// Remote JSON databank (public, no API key, ~3 MB once) – mwgg/Airports
var DATASET_URL = "https://raw.githubusercontent.com/mwgg/Airports/master/airports.json";
var airportData = null;      // cache after first fetch
var airportIndex = {};       // iata -> {city,country}

function buildIndex(json) {
  Object.keys(json).forEach(function(icao) {
    var entry = json[icao];
    if (entry.iata && entry.city && entry.country) {
      airportIndex[entry.iata.toUpperCase()] = {
        city: entry.city,
        country: entry.country
      };
    }
  });
  console.log("Airport index built: " + Object.keys(airportIndex).length + " entries");
}

function loadAirportData(callback) {
  if (airportData) {
    callback();
    return;
  }
  console.log("Fetching airport dataset...");
  var xhr = new XMLHttpRequest();
  xhr.onload = function() {
    if (xhr.status === 200) {
      try {
        airportData = JSON.parse(xhr.responseText);
        buildIndex(airportData);
      } catch (e) {
        console.log("Dataset parse error: " + e);
      }
    } else {
      console.log("Dataset HTTP error: " + xhr.status);
    }
    callback();
  };
  xhr.onerror = function() {
    console.log("Dataset XHR error");
    callback();
  };
  xhr.open("GET", DATASET_URL);
  xhr.send();
}

function lookupAirport(iata) {
  if (!iata) return null;
  return airportIndex[iata.toUpperCase()] || null;
}

Pebble.addEventListener("appmessage", function(e) {
  var payload = e.payload;
  if (!payload) return;
  if (payload[KEY_REQUEST_TYPE] === REQUEST_AIRPORT_INFO) {
    var code = payload[KEY_AIRPORT_CODE];
    console.log("Request for airport " + code);
    loadAirportData(function() {
      var info = lookupAirport(code);
      var msg = {};
      if (info) {
        msg[KEY_CITY] = info.city;
        msg[KEY_COUNTRY] = info.country;
      } else {
        msg[KEY_CITY] = "?";
        msg[KEY_COUNTRY] = "";
      }
      Pebble.sendAppMessage(msg,
        function() { console.log("Airport info sent"); },
        function(err) { console.log("Send failed: " + JSON.stringify(err)); }
      );
    });
  }
});
