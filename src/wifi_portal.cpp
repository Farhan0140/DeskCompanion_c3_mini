#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include "wifi_portal.h"
#include "config.h"
#include "globals.h"

static WebServer g_server(80);
static DNSServer g_dns;
static bool g_active = false;
static bool g_credsReady = false;
static const byte DNS_PORT = 53;

bool wifiCredsLoad(char *ssid, size_t ssidLen, char *pass, size_t passLen) {
  String s = prefs.getString("wifi_ssid", "");
  if (s.length() == 0) return false;
  s.toCharArray(ssid, ssidLen);
  prefs.getString("wifi_pass", "").toCharArray(pass, passLen);
  return true;
}

static void wifiCredsSave(const char *ssid, const char *pass) {
  prefs.putString("wifi_ssid", ssid);
  prefs.putString("wifi_pass", pass);
}

// ---- Setup page, served straight from flash (PROGMEM) — no RAM cost,
// no external CSS/JS/fonts (the phone has no internet while joined to
// our AP, so anything external would just fail to load anyway). ----
static const char PAGE_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>DeskBuddy WiFi Setup</title>
<style>
  * { box-sizing: border-box; }
  body {
    margin: 0; padding: 24px 16px 60px;
    font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
    background: linear-gradient(160deg, #4f46e5, #0ea5e9);
    min-height: 100vh; color: #111;
  }
  h1 { color: #fff; font-size: 20px; text-align: center; margin: 0 0 4px; }
  p.sub { color: #e0e7ff; text-align: center; margin: 0 0 20px; font-size: 13px; }
  .card {
    background: #fff; border-radius: 14px; padding: 6px;
    max-width: 380px; margin: 0 auto; box-shadow: 0 10px 30px rgba(0,0,0,.25);
  }
  .net {
    display: flex; align-items: center; justify-content: space-between;
    padding: 12px 14px; border-radius: 10px; cursor: pointer;
  }
  .net.selected { background: #eef2ff; }
  .net .name { font-weight: 600; font-size: 15px; }
  .net .meta { font-size: 12px; color: #6b7280; margin-top: 2px; }
  .bars { display: flex; align-items: flex-end; gap: 2px; height: 14px; }
  .bars span { width: 3px; background: #cbd5e1; border-radius: 1px; }
  .bars span.on { background: #4f46e5; }
  .lock { margin-left: 8px; opacity: .5; }
  .form { padding: 14px; border-top: 1px solid #eee; display: none; }
  .form.show { display: block; }
  .form input {
    width: 100%; padding: 10px 12px; border: 1px solid #d1d5db; border-radius: 8px;
    font-size: 15px; margin-bottom: 10px;
  }
  .form button {
    width: 100%; padding: 11px; border: none; border-radius: 8px;
    background: #4f46e5; color: #fff; font-size: 15px; font-weight: 600;
  }
  .rescan {
    display: block; margin: 16px auto 0; background: rgba(255,255,255,.15);
    color: #fff; border: 1px solid rgba(255,255,255,.4); border-radius: 20px;
    padding: 8px 18px; font-size: 13px;
  }
  .status { text-align: center; color: #fff; font-size: 13px; margin-top: 10px; min-height: 16px; }
  .empty { text-align: center; color: #9ca3af; padding: 24px; font-size: 14px; }
</style>
</head>
<body>
  <h1>DeskBuddy WiFi Setup</h1>
  <p class="sub">Pick your network and enter the password</p>
  <div class="card" id="list"><div class="empty">Scanning...</div></div>
  <button class="rescan" onclick="scan()">Rescan</button>
  <div class="status" id="status"></div>

<script>
let selected = null;

function bars(rssi) {
  var n = rssi > -55 ? 4 : rssi > -65 ? 3 : rssi > -75 ? 2 : 1;
  var html = '<div class="bars">';
  for (var i = 1; i <= 4; i++) {
    html += '<span class="' + (i <= n ? 'on' : '') + '" style="height:' + (i*3+2) + 'px"></span>';
  }
  return html + '</div>';
}

function scan() {
  document.getElementById('list').innerHTML = '<div class="empty">Scanning...</div>';
  fetch('/scan').then(function(r){ return r.json(); }).then(function(nets){
    if (!nets.length) {
      document.getElementById('list').innerHTML = '<div class="empty">No networks found</div>';
      return;
    }
    nets.sort(function(a,b){ return b.rssi - a.rssi; });
    var html = '';
    nets.forEach(function(n, i){
      html += '<div class="net" data-i="' + i + '" onclick="select(' + i + ',\'' +
              n.ssid.replace(/'/g,"\\'") + '\',' + n.secure + ')">'
            + '<div><div class="name">' + n.ssid + '</div>'
            + '<div class="meta">' + n.rssi + ' dBm</div></div>'
            + bars(n.rssi) + (n.secure ? '<span class="lock">&#128274;</span>' : '')
            + '</div>';
    });
    html += '<div class="form" id="form">'
          + '<input type="password" id="pw" placeholder="Password">'
          + '<button onclick="connect()">Connect</button></div>';
    document.getElementById('list').innerHTML = html;
  }).catch(function(){
    document.getElementById('list').innerHTML = '<div class="empty">Scan failed, try again</div>';
  });
}

function select(i, ssid, secure) {
  var all = document.querySelectorAll('.net');
  for (var j = 0; j < all.length; j++) all[j].classList.remove('selected');
  document.querySelector('.net[data-i="' + i + '"]').classList.add('selected');
  selected = ssid;
  var form = document.getElementById('form');
  form.classList.add('show');
  document.getElementById('pw').style.display = secure ? 'block' : 'none';
  document.getElementById('status').textContent = '';
}

function connect() {
  if (!selected) return;
  var pw = document.getElementById('pw').value;
  document.getElementById('status').textContent = 'Saving...';
  var body = 'ssid=' + encodeURIComponent(selected) + '&password=' + encodeURIComponent(pw);
  fetch('/save', { method: 'POST', headers: {'Content-Type':'application/x-www-form-urlencoded'}, body: body })
    .then(function(r){ return r.text(); }).then(function(){
      document.body.innerHTML = '<h1 style="margin-top:80px">Saved!</h1>'
        + '<p class="sub">DeskBuddy is connecting to ' + selected + ' now.<br>You can close this page.</p>';
    }).catch(function(){ document.getElementById('status').textContent = 'Failed, try again'; });
}

scan();
</script>
</body>
</html>
)HTML";

static void handleRoot() {
  g_server.send_P(200, "text/html", PAGE_HTML);
}

static void handleScan() {
  int n = WiFi.scanNetworks();
  String json = "[";
  for (int i = 0; i < n; i++) {
    if (i) json += ",";
    String ssid = WiFi.SSID(i);
    ssid.replace("\"", "\\\""); // defend against a network name containing a literal quote
    json += "{\"ssid\":\"" + ssid + "\",";
    json += "\"rssi\":" + String(WiFi.RSSI(i)) + ",";
    json += "\"secure\":" + String(WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "false" : "true") + "}";
  }
  json += "]";
  WiFi.scanDelete();
  g_server.send(200, "application/json", json);
}

static void handleSave() {
  String ssid = g_server.arg("ssid");
  String pass = g_server.arg("password");
  if (ssid.length() == 0) {
    g_server.send(400, "text/plain", "SSID required");
    return;
  }
  wifiCredsSave(ssid.c_str(), pass.c_str());
  g_server.send(200, "text/plain", "OK");
  g_credsReady = true; // menu.cpp picks this up next tick, stops the portal, and reconnects
}

// Catch-all instead of a 404: bounces any unmatched request (including
// the probes iOS/Android send to detect a captive portal) straight to
// the setup page — the on-screen IP is still the reliable fallback if
// a phone doesn't auto-popup it.
static void handleNotFound() {
  handleRoot();
}

void wifiPortalStart() {
  if (g_active) return;
  g_credsReady = false;

  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_AP_STA); // AP for the phone to join, STA (unassociated) so /scan can see nearby networks
  WiFi.softAP(WIFI_AP_SSID);
  delay(100); // AP needs a moment to come up before DNS/HTTP start serving

  g_dns.start(DNS_PORT, "*", WiFi.softAPIP());

  g_server.on("/", HTTP_GET, handleRoot);
  g_server.on("/scan", HTTP_GET, handleScan);
  g_server.on("/save", HTTP_POST, handleSave);
  g_server.onNotFound(handleNotFound);
  g_server.begin();

  g_active = true;
  Serial.print(F("[WIFI-SETUP] AP started, IP: "));
  Serial.println(WiFi.softAPIP());
}

void wifiPortalStop() {
  if (!g_active) return;
  g_server.stop();
  g_dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA); // hand the radio back to netsync.cpp in its expected mode
  g_active = false;
}

void wifiPortalUpdate() {
  if (!g_active) return;
  g_dns.processNextRequest();
  g_server.handleClient();
}

bool wifiPortalActive() { return g_active; }
IPAddress wifiPortalIP() { return WiFi.softAPIP(); }
const char* wifiPortalApSsid() { return WIFI_AP_SSID; }
bool wifiPortalCredsReady() { return g_credsReady; }
void wifiPortalClearCredsReady() { g_credsReady = false; }
