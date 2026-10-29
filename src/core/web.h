#pragma once
#include <Arduino.h>

// Web access: a small HTTP server for a phone or laptop on the same WiFi.
// Off until started from the Web app; it then keeps running in the background
// (main.cpp calls update() every frame) so the live table view follows the
// Initiative and Dice apps. It joins the saved home network, or makes its own
// hotspot if that fails. Every API call needs the PIN shown on the device.
//
// The page itself is web/index.html, gzipped into web_page.h at build time
// (tools/web_embed.py). The API it uses:
//   POST /api/login         body = PIN -> session token (send as X-Token)
//   GET  /api/status        network, heap
//   GET  /api/ls?path=      directory listing as JSON
//   GET  /api/file?path=    download (&off=&len= for part of a file)
//   GET  /api/rec?path=&off= one blank-line-terminated record of an SRD .txt
//   POST /api/file?path=    body = new text contents (written safely via a temp file)
//   POST /api/upload?dir=   multipart file upload
//   POST /api/rm|mkdir|mv   ?path= (&to= for mv)
//   GET  /api/live          initiative state + recent rolls
//   POST /api/wifi          body = "ssid\npassword" (home network for next start)
//   POST /api/roll?title=   body = "label|expr" lines -> results, logged like the device's rolls
//   GET|POST /api/app?app=&op= an app's own web side (App::web); /api/status lists the apps that have one
//   /api/foundry/*          Foundry VTT link, with its own key (core/foundry.h)
// Paths are confined to /gmtools.
namespace web {

enum State { OFF, CONNECTING, RUNNING };

void start();
void stop();
void update();
State state();
bool apMode();           // true when running its own hotspot
String networkName();    // joined network, or the hotspot's name
String apPassword();     // hotspot password
String address();        // e.g. "http://192.168.4.1"
String pin();
uint32_t requests();     // API calls served since start
String savedSsid();
void saveNetwork(const String& ssid, const String& pass);

// A JSON string literal: "..." with escapes.
String js(const String& s);

// Recent rolls, for the live table view.
void logRoll(const String& title, const String& label, const String& expr, int total, const String& breakdown,
             bool crit, int natural);

}  // namespace web
