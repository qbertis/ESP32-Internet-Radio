#ifndef WEB_SERVER_MANAGER_H
#define WEB_SERVER_MANAGER_H

#include <Arduino.h>
#include <WebServer.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include "SettingsManager.h"
#include "RadioStream.h"
#include "DisplayManager.h"
#include "MyWiFiManager.h"

extern WebServer server;
extern WiFiManager wm;
extern QueueHandle_t commandQueue;

class WebServerManager {
public:
  static void start() {
    server.on("/", handleRoot);
    server.on("/save", handleSave);
    server.on("/delete", handleDelete);
    server.on("/add", handleAdd);
    server.on("/play", handlePlay);
    server.on("/volume", handleVolume);
    server.on("/mute", handleMute);
    server.on("/eq", handleEQ);
    server.on("/status", handleStatus);
    server.on("/wifi", handleWiFiConfig);
    server.on("/wifi/scan", handleWiFiScan);
    server.on("/wifi/connect", handleWiFiConnect);
    server.on("/wifi/reset", handleWiFiReset);
    server.on("/wifi/status", handleWiFiStatus);
    server.on("/stations/export", handleExportStations);
    server.on("/stations/import", HTTP_POST, handleImportStations);
    server.on("/system", handleSystem);
    server.on("/update", handleUpdatePage);
    server.on("/ota", HTTP_POST, handleOTAResult, handleOTAUpload);
    server.on("/stations/deleteall", handleDeleteAllStations);

    server.begin();
    Serial.println("Web server started on port 80");
  }

private:
  // ── Екранування HTML ──
  static String htmlEscape(const char* str) {
    String s = str;
    s.replace("&", "&amp;");
    s.replace("<", "&lt;");
    s.replace(">", "&gt;");
    s.replace("\"", "&quot;");
    s.replace("'", "&#39;");
    return s;
  }

  inline static int lastVolumeBeforeMute = 70;

  // ══════════════════════════════════════════
  //  ГОЛОВНА СТОРІНКА
  // ══════════════════════════════════════════
  static void handleRoot() {
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "text/html", "");

    int cur = SettingsManager::getCurrentStation();
    int vol = SettingsManager::getVolume();
    int bass = SettingsManager::getBass();
    int treble = SettingsManager::getTreble();

    server.sendContent(
      "<!DOCTYPE html><html><head>"
      "<meta charset='UTF-8'>"
      "<meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<link rel='icon' type='image/svg+xml' href=\"data:image/svg+xml,"
      "%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 100 100'%3E"
      "%3Ctext y='.9em' font-size='90'%3E📻%3C/text%3E%3C/svg%3E\">"
      "<style>"
      "*{box-sizing:border-box}"
      "body{font-family:Arial,sans-serif;margin:0;padding:20px;background:#f5f5f5}"
      ".container{max-width:1200px;margin:0 auto}"
      "h2{color:#333;text-align:center}"
      ".card{background:#fff;border-radius:10px;padding:15px;margin-bottom:20px;"
      "box-shadow:0 2px 5px rgba(0,0,0,.1)}"
      ".station-name{font-size:24px;font-weight:bold;color:#2196F3;margin:10px 0}"
      ".vol{display:flex;align-items:center;justify-content:center;gap:15px;margin-top:10px;flex-wrap:wrap}"
      ".controls{display:grid;grid-template-columns:1fr 1fr;gap:20px;margin-bottom:20px}"
      ".slider-wrap{margin:14px 0}"
      ".slider-wrap label{display:block;margin-bottom:6px;font-weight:bold;color:#333}"
      "input[type=text]{width:95%;padding:8px;border:1px solid #ddd;border-radius:4px}"
      "input[type=range]{width:100%}"
      "table{width:100%;border-collapse:collapse;background:#fff;border-radius:10px;"
      "overflow:hidden;box-shadow:0 2px 5px rgba(0,0,0,.1)}"
      "th{background:#2196F3;color:#fff;padding:12px;text-align:left}"
      "td{padding:10px;border-bottom:1px solid #ddd}"
      "button{padding:8px 12px;margin:2px;border:none;border-radius:4px;cursor:pointer}"
      ".bp{background:#4CAF50;color:#fff}.bs{background:#2196F3;color:#fff}"
      ".bd{background:#f44336;color:#fff}.bm{background:#ff9800;color:#fff}"
      ".nav{display:flex;justify-content:center;gap:20px;margin-bottom:20px}"
      ".nav a{text-decoration:none;background:#2196F3;color:#fff;padding:10px 20px;border-radius:5px}"
      ".dot{display:inline-block;width:10px;height:10px;border-radius:50%;margin-right:5px}"
      ".on{background:#4CAF50}.off{background:#f44336}"
      ".active-row td{background:#eaf4ff}"
      ".small{color:#666;font-size:14px}"
      "@media (max-width:900px){.controls{grid-template-columns:1fr}}"
      "</style>"
      "<script>"
      "function playStation(i){fetch('/play?i='+i).then(()=>setTimeout(refreshStatus,500));}"
      "function saveStation(i){"
      " let n=document.getElementById('n'+i).value;"
      " let h=document.getElementById('h'+i).value;"
      " let p=document.getElementById('p'+i).value;"
      " fetch('/save?i='+i+'&name='+encodeURIComponent(n)"
      " +'&host='+encodeURIComponent(h)+'&path='+encodeURIComponent(p))"
      " .then(()=>location.reload());"
      "}"
      "function deleteStation(i){"
      " if(confirm('Видалити станцію?')){"
      "   fetch('/delete?i='+i).then(()=>location.reload());"
      " }"
      "}"
      "function addStation(){"
      " let n=document.getElementById('nn').value;"
      " let h=document.getElementById('nh').value;"
      " let p=document.getElementById('np').value;"
      " if(n&&h&&p){"
      "   fetch('/add?name='+encodeURIComponent(n)"
      "   +'&host='+encodeURIComponent(h)+'&path='+encodeURIComponent(p))"
      "   .then(()=>location.reload());"
      " }"
      "}"
      "function updateVolLabel(v){document.getElementById('volValue').textContent=v+'%';}"
      "function setVolume(v){fetch('/volume?val='+v).then(()=>refreshStatus());}"
      "function toggleMute(){fetch('/mute').then(()=>refreshStatus());}"
      "function updateBassLabel(v){document.getElementById('bassValue').textContent=(v > 0 ? '+' : '') + v + ' dB';}"
      "function updateTrebleLabel(v){document.getElementById('trebleValue').textContent=(v > 0 ? '+' : '') + (v * 1.5).toFixed(1) + ' dB';}"
      "function setBass(v){updateBassLabel(v);fetch('/eq?bass='+v).then(()=>refreshStatus());}"
      "function setTreble(v){updateTrebleLabel(v);fetch('/eq?treble='+v).then(()=>refreshStatus());}"
      "function refreshStatus(){"
      " fetch('/status').then(r=>r.json()).then(d=>{"
      "   let dot=document.getElementById('streamDot');"
      "   if(dot) dot.className='dot '+(d.connected?'on':'off');"
      "   let txt=document.getElementById('streamText');"
      "   if(txt) txt.textContent=d.connected?'Підключено':'Відключено';"
      "   let rssi=document.getElementById('rssiValue');"
      "   if(rssi) rssi.textContent=d.rssi+' dBm';"
      "   let st=document.getElementById('curStationName');"
      "   if(st) st.textContent=d.stationName;"
      "   let vv=document.getElementById('volValue');"
      "   if(vv) vv.textContent=d.volume+'%';"
      "   let vs=document.getElementById('volSlider');"
      "   if(vs) vs.value=d.volume;"
      "   let bb=document.getElementById('bassSlider');"
      "   if(bb) bb.value=d.bass;"
      "   let tt=document.getElementById('trebleSlider');"
      "   if(tt) tt.value=d.treble;"
      "   updateBassLabel(d.bass);"
      "   updateTrebleLabel(d.treble);"
      "   let mb=document.getElementById('muteBtn');"
      "   if(mb) mb.textContent=d.muted?'🔊 Unmute':'🔇 Mute';"
      "   document.querySelectorAll('tr[data-station]').forEach(function(row){"
      "     row.classList.remove('active-row');"
      "   });"
      "   let ar=document.getElementById('row'+d.currentStation);"
      "   if(ar) ar.classList.add('active-row');"
      " });"
      "}"
      "function exportStations(){"
      " window.location='/stations/export';"
      "}"
      "function importStations(){"
      " let f=document.getElementById('importFile').files[0];"
      " if(!f){alert('Оберіть JSON файл');return;}"
      " let mode=document.querySelector('input[name=importMode]:checked').value;"
      " if(mode=='replace' && !confirm('Замінити ВСІ станції?\\nІснуючі будуть видалені!'))return;"
      " let r=new FileReader();"
      " r.onload=function(e){"
      "   fetch('/stations/import?mode='+mode,{method:'POST',"
      "     headers:{'Content-Type':'application/json'},"
      "     body:e.target.result})"
      "   .then(r=>r.json()).then(d=>{"
      "     alert('Імпортовано: '+d.imported+' станцій\\nВсього: '+d.total);"
      "     location.reload();"
      "   }).catch(()=>alert('Помилка імпорту'));"
      " };"
      " r.readAsText(f);"
      "}"
      "function deleteAllStations(){"
      " if(!confirm('Видалити ВСІ станції?'))return;"
      " if(!confirm('Точно видалити? Це не можна скасувати!'))return;"
      " fetch('/stations/deleteall').then(()=>location.reload());"
      "}"
      "setInterval(refreshStatus,3000);"
      "window.onload=refreshStatus;"
      "</script>"
      "</head><body><div class='container'>");

    server.sendContent(
      "<div class='nav'>"
      "<a href='/'>📻 Radio</a>"
      "<a href='/wifi'>📡 WiFi</a>"
      "<a href='/system'>⚙ System</a>"
      "</div>");

    String nowPlaying =
      "<div class='card' style='text-align:center'>"
      "<h3 style='margin:0 0 10px;color:#666'>🎵 ЗАРАЗ ГРАЄ</h3>"
      "<div class='station-name' id='curStationName'>"
      + htmlEscape(SettingsManager::getStationName(cur)) + "</div>"
                                                           "<div class='small'>"
                                                           "<span id='streamDot' class='dot "
      + String(RadioStream::isConnected() ? "on" : "off") + "'></span>"
                                                            "<span id='streamText'>"
      + String(RadioStream::isConnected() ? "Підключено" : "Відключено") + "</span>"
                                                                           " | 📶 <span id='rssiValue'>"
      + String(WiFi.RSSI()) + " dBm</span>"
                              "</div>"
                              "</div>";

    server.sendContent(nowPlaying);

    String controlCards =
      "<div class='controls'>"
      "<div class='card'>"
      "<h3 style='margin-top:0'>🔊 Гучність</h3>"
      "<div class='slider-wrap'>"
      "<label>Volume: <span id='volValue'>"
      + String(vol) + "%</span></label>"
                      "<input type='range' id='volSlider' min='0' max='100' value='"
      + String(vol) + "' "
                      "oninput='updateVolLabel(this.value)' onchange='setVolume(this.value)'>"
                      "</div>"
                      "<button id='muteBtn' class='bm' onclick='toggleMute()'>"
      + String(vol == 0 ? "🔊 Unmute" : "🔇 Mute") + "</button>"
                                                   "</div>"

                                                   "<div class='card'>"
                                                   "<h3 style='margin-top:0'>🎶 Еквалайзер</h3>"

                                                   // Блок BASS
                                                   "<div class='slider-wrap'>"
                                                   "<label>Bass: <span id='bassValue'>+"
      + String(bass) + " dB</span></label>"
                       "<input type='range' id='bassSlider' min='0' max='15' value='"
      + String(bass) + "' "
                       "oninput='updateBassLabel(this.value)' onchange='setBass(this.value)'>"
                       "</div>"

                       // Блок TREBLE (з підтримкою від'ємних значень)
                       "<div class='slider-wrap'>"
                       "<label>Treble: <span id='trebleValue'>"
      + (treble > 0 ? "+" : "") + String(treble * 1.5f, 1) + " dB</span></label>"
                                                             "<input type='range' id='trebleSlider' min='-8' max='7' value='"
      + String(treble) + "' "
                         "oninput='updateTrebleLabel(this.value)' onchange='setTreble(this.value)'>"
                         "</div>"

                         "</div>"
                         "</div>";

    server.sendContent(controlCards);

    server.sendContent(
      "<h2>📻 Список радіостанцій</h2>"
      "<table><tr><th>#</th><th>Назва</th><th>Хост</th><th>Шлях</th><th>Дія</th></tr>");

    for (int i = 0; i < SettingsManager::totalStations; i++) {
      String row =
        "<tr id='row" + String(i) + "' data-station='" + String(i) + "'" + String(i == cur ? " class='active-row'" : "") + ">"
                                                                                                                           "<td>"
        + String(i + 1) + "</td>"
                          "<td><input type='text' id='n"
        + String(i) + "' value='" + htmlEscape(SettingsManager::stations[i].name) + "'></td>"
                                                                                    "<td><input type='text' id='h"
        + String(i) + "' value='" + htmlEscape(SettingsManager::stations[i].host) + "'></td>"
                                                                                    "<td><input type='text' id='p"
        + String(i) + "' value='" + htmlEscape(SettingsManager::stations[i].path) + "'></td>"
                                                                                    "<td style='white-space:nowrap'>"
                                                                                    "<button class='bp' onclick='playStation("
        + String(i) + ")'>▶</button>"
                      "<button class='bs' onclick='saveStation("
        + String(i) + ")'>💾</button>"
                      "<button class='bd' onclick='deleteStation("
        + String(i) + ")'>🗑</button>"
                      "</td></tr>";

      server.sendContent(row);
    }

    server.sendContent(
      "</table>"
      "<div class='card' style='margin-top:20px'>"
      "<h3 style='margin-top:0'>➕ Додати станцію</h3>"
      "<input type='text' id='nn' placeholder='Назва' style='width:100%;margin-bottom:8px'><br>"
      "<input type='text' id='nh' placeholder='Хост' style='width:100%;margin-bottom:8px'><br>"
      "<input type='text' id='np' placeholder='Шлях' style='width:100%;margin-bottom:8px'><br>"
      "<button class='bp' onclick='addStation()' style='padding:10px 20px'>➕ Додати</button>"
      "</div>"

      "<div class='card'>"
      "<h3 style='margin-top:0'>💾 Імпорт / Експорт станцій</h3>"
      "<button class='bs' onclick='exportStations()' style='width:100%;padding:10px;margin-bottom:8px'>"
      "📥 Експорт в JSON</button>"
      "<input type='file' id='importFile' accept='.json' style='margin-bottom:8px'>"
      "<div style='margin-bottom:8px'>"
      "<label><input type='radio' name='importMode' value='add' checked> Додати до існуючих</label><br>"
      "<label><input type='radio' name='importMode' value='replace'> Замінити всі станції</label>"
      "</div>"
      "<button class='bp' onclick='importStations()' style='width:100%;padding:10px;margin-bottom:8px'>"
      "📤 Імпорт з JSON</button>"
      "<button class='bd' onclick='deleteAllStations()' style='width:100%;padding:10px'>"
      "🗑 Видалити всі станції</button>"
      "</div>"
      "</div></body></html>");
  }

  // ══════════════════════════════════════════
  //  ПРОГРАВАННЯ СТАНЦІЇ
  // ══════════════════════════════════════════
  static void handlePlay() {
    int i = server.arg("i").toInt();

    if (i < 0 || i >= SettingsManager::totalStations) {
      server.send(400, "text/plain", "Invalid index");
      return;
    }

    SettingsManager::currentStation = i;
    SettingsManager::markChanged();

    // Надсилаємо команду в Audio Task через чергу
    if (commandQueue != NULL) {
      Command cmd;
      cmd.type = CMD_CONNECT_STATION;
      cmd.value = i;
      xQueueSend(commandQueue, &cmd, pdMS_TO_TICKS(100));
    } else {
      // fallback якщо черга не готова
      RadioStream::connectToHost();
    }

    DisplayManager::showMessage(SettingsManager::getStationName(i));
    server.send(200, "text/plain", "OK");
  }

  // ══════════════════════════════════════════
  //  ГУЧНІСТЬ
  // ══════════════════════════════════════════
  static void handleVolume() {
    int vol = SettingsManager::getVolume();

    if (server.hasArg("val")) {
      vol = server.arg("val").toInt();
    } else {
      String dir = server.arg("dir");
      if (dir == "up") {
        vol += 5;
      } else if (dir == "down") {
        vol -= 5;
      }
    }

    vol = constrain(vol, 0, 100);

    if (vol > 0) {
      lastVolumeBeforeMute = vol;
    }

    SettingsManager::setVolume(vol);
    AudioPlayer::setVolume(vol);

    server.send(200, "text/plain", String(vol));
  }

  static void handleMute() {
    int vol = SettingsManager::getVolume();

    if (vol > 0) {
      lastVolumeBeforeMute = vol;
      vol = 0;
    } else {
      vol = constrain(lastVolumeBeforeMute, 1, 100);
    }

    SettingsManager::setVolume(vol);
    AudioPlayer::setVolume(vol);

    server.send(200, "text/plain", String(vol));
  }

  static void handleEQ() {
    bool changed = false;

    if (server.hasArg("bass")) {
      int bass = constrain(server.arg("bass").toInt(), BASS_AMP_MIN, BASS_AMP_MAX);
      SettingsManager::setBass(bass);
      changed = true;
    }

    if (server.hasArg("treble")) {
      int treble = constrain(server.arg("treble").toInt(), TREBLE_AMP_MIN, TREBLE_AMP_MAX);
      SettingsManager::setTreble(treble);
      changed = true;
    }

    if (changed) {
      AudioPlayer::updateEQ();
    }

    JsonDocument doc;
    doc["bass"] = SettingsManager::getBass();
    doc["treble"] = SettingsManager::getTreble();

    String json;
    serializeJson(doc, json);
    server.send(200, "application/json", json);
  }

  // ══════════════════════════════════════════
  //  СТАТУС (JSON)
  // ══════════════════════════════════════════
  static void handleStatus() {
    JsonDocument doc;
    doc["connected"] = RadioStream::isConnected();
    doc["volume"] = SettingsManager::getVolume();
    doc["muted"] = (SettingsManager::getVolume() == 0);
    doc["currentStation"] = SettingsManager::getCurrentStation();
    doc["stationName"] = SettingsManager::getStationName(SettingsManager::getCurrentStation());
    doc["rssi"] = WiFi.RSSI();
    doc["bass"] = SettingsManager::getBass();
    doc["treble"] = SettingsManager::getTreble();

    String json;
    serializeJson(doc, json);
    server.send(200, "application/json", json);
  }

  // ══════════════════════════════════════════
  //  ЗБЕРЕГТИ СТАНЦІЮ
  // ══════════════════════════════════════════
  static void handleSave() {
    int i = server.arg("i").toInt();

    if (i < 0 || i >= SettingsManager::totalStations) {
      server.send(400, "text/plain", "Invalid index");
      return;
    }

    SettingsManager::updateStation(
      i,
      server.arg("name").c_str(),
      server.arg("host").c_str(),
      server.arg("path").c_str());

    server.send(200, "text/plain", "OK");
  }

  // ══════════════════════════════════════════
  //  ВИДАЛИТИ СТАНЦІЮ
  // ══════════════════════════════════════════
  static void handleDelete() {
    int i = server.arg("i").toInt();

    if (i < 0 || i >= SettingsManager::totalStations) {
      server.send(400, "text/plain", "Invalid index");
      return;
    }

    SettingsManager::deleteStation(i);
    server.send(200, "text/plain", "Deleted");
  }

  // ══════════════════════════════════════════
  //  ДОДАТИ СТАНЦІЮ
  // ══════════════════════════════════════════
  static void handleAdd() {
    String name = server.arg("name");
    String host = server.arg("host");
    String path = server.arg("path");

    if (name.isEmpty() || host.isEmpty() || path.isEmpty()) {
      server.send(400, "text/plain", "Missing fields");
      return;
    }

    SettingsManager::addStation(name.c_str(), host.c_str(), path.c_str());
    server.send(200, "text/plain", "Added");
  }

  // ══════════════════════════════════════════
  //  ЕКСПОРТ СТАНЦІЙ (JSON файл)
  // ══════════════════════════════════════════
  static void handleExportStations() {
    JsonDocument doc;
    JsonArray arr = doc["stations"].to<JsonArray>();

    for (int i = 0; i < SettingsManager::totalStations; i++) {
      JsonObject st = arr.add<JsonObject>();
      st["name"] = SettingsManager::stations[i].name;
      st["host"] = SettingsManager::stations[i].host;
      st["path"] = SettingsManager::stations[i].path;
    }

    String json;
    serializeJsonPretty(doc, json);

    server.sendHeader("Content-Disposition", "attachment; filename=\"stations.json\"");
    server.send(200, "application/json", json);
  }

  // ══════════════════════════════════════════
  //  ІМПОРТ СТАНЦІЙ (JSON файл)
  // ══════════════════════════════════════════
  static void handleImportStations() {
    if (!server.hasArg("plain")) {
      server.send(400, "application/json", "{\"error\":\"No data\"}");
      return;
    }

    String body = server.arg("plain");

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);

    if (err) {
      server.send(400, "application/json",
                  "{\"error\":\"Invalid JSON: " + String(err.c_str()) + "\"}");
      return;
    }

    JsonArray arr = doc["stations"].as<JsonArray>();
    if (arr.isNull() || arr.size() == 0) {
      server.send(400, "application/json", "{\"error\":\"No stations found\"}");
      return;
    }

    // Перевіряємо режим: replace=true видаляє всі старі станції
    bool replaceAll = false;
    if (server.hasArg("mode")) {
      replaceAll = (server.arg("mode") == "replace");
    }

    if (replaceAll) {
      SettingsManager::totalStations = 0;
      SettingsManager::currentStation = 0;
    }

    int imported = 0;
    for (JsonObject st : arr) {
      const char* name = st["name"] | "";
      const char* host = st["host"] | "";
      const char* path = st["path"] | "";

      if (strlen(name) > 0 && strlen(host) > 0 && strlen(path) > 0) {
        if (SettingsManager::totalStations < MAX_STATIONS) {
          SettingsManager::addStation(name, host, path);
          imported++;
        }
      }
    }

    SettingsManager::saveAll();

    JsonDocument resp;
    resp["imported"] = imported;
    resp["total"] = SettingsManager::totalStations;
    resp["replaced"] = replaceAll;

    String json;
    serializeJson(resp, json);
    server.send(200, "application/json", json);
  }

  static void handleDeleteAllStations() {
    SettingsManager::totalStations = 0;
    SettingsManager::currentStation = 0;
    SettingsManager::saveAll();

    Serial.println("All stations deleted");
    server.send(200, "text/plain", "OK");
  }

  // ══════════════════════════════════════════
  //  WIFI СТОРІНКА
  // ══════════════════════════════════════════
  static void handleWiFiConfig() {
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "text/html", "");

    server.sendContent(
      "<!DOCTYPE html><html><head>"
      "<meta charset='UTF-8'>"
      "<meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<style>"
      "body{font-family:Arial,sans-serif;margin:0;padding:20px;background:#f5f5f5}"
      ".container{max-width:600px;margin:0 auto}"
      ".card{background:#fff;border-radius:10px;padding:20px;margin-bottom:20px;"
      "box-shadow:0 2px 5px rgba(0,0,0,.1)}"
      "h2,h3{color:#333;margin-top:0}"
      "select,button,input{width:100%;padding:10px;margin:8px 0;"
      "border:1px solid #ddd;border-radius:5px;font-size:14px}"
      "button{background:#2196F3;color:#fff;border:none;cursor:pointer}"
      ".btn-red{background:#f44336}.btn-green{background:#4CAF50}"
      ".nav{display:flex;justify-content:center;gap:20px;margin-bottom:20px}"
      ".nav a{text-decoration:none;background:#2196F3;color:#fff;"
      "padding:10px 20px;border-radius:5px}"
      ".dot{display:inline-block;width:10px;height:10px;border-radius:50%;margin-right:5px}"
      ".on{background:#4CAF50}.off{background:#f44336}"
      ".dbg{background:#fff3e0;padding:10px;border-radius:5px;"
      "font-family:monospace;font-size:12px;white-space:pre}"
      "</style>"
      "<script>"
      "function scanNetworks(){"
      "  document.getElementById('scanBtn').textContent='Scanning...';"
      "  fetch('/wifi/scan').then(r=>r.json()).then(d=>{"
      "    let s=document.getElementById('ssid');"
      "    s.innerHTML='<option value=\"\">Select...</option>';"
      "    if(d.networks)d.networks.forEach(n=>{"
      "      let o=document.createElement('option');"
      "      o.value=n.ssid;"
      "      o.text=n.ssid+' ('+n.rssi+' dBm)'+(n.encrypted?' 🔒':'');"
      "      s.appendChild(o);"
      "    });"
      "    document.getElementById('scanBtn').textContent='🔍 Scan Networks';"
      "  });"
      "}"
      "function connectWiFi(){"
      "  let ssid=document.getElementById('ssid').value;"
      "  let pass=document.getElementById('pass').value;"
      "  if(!ssid){alert('Select a network');return;}"
      "  fetch('/wifi/connect?ssid='+encodeURIComponent(ssid)+'&pass='+encodeURIComponent(pass));"
      "  alert('Connecting to '+ssid+'... Device will reboot');"
      "}"
      "function resetWiFi(){"
      "  if(confirm('Reset WiFi settings?\\nDevice will reboot as AP: ESP32-Radio')){"
      "    fetch('/wifi/reset');"
      "    alert('Rebooting...');"
      "  }"
      "}"
      "function refreshStatus(){"
      "  fetch('/wifi/status').then(r=>r.json()).then(d=>{"
      "    let s=document.getElementById('wifiStatus');"
      "    s.innerHTML=d.connected"
      "      ?'<span class=\"dot on\"></span>Connected to <b>'+d.ssid+'</b><br>📶 '+d.rssi+' dBm | 🌐 '+d.ip"
      "      :'<span class=\"dot off\"></span>Not connected | AP: 192.168.4.1';"
      "  });"
      "}"
      "setInterval(refreshStatus,3000);"
      "window.onload=refreshStatus;"
      "</script>"
      "</head><body><div class='container'>");

    server.sendContent(
      "<div class='nav'><a href='/'>📻 Radio</a><a href='/wifi'>📡 WiFi</a><a href='/system'>⚙ System</a></div>"
      "<div class='card'><h2>📡 WiFi Configuration</h2>"
      "<div id='wifiStatus'>Loading...</div></div>"
      "<div class='card'><h3>🌐 Connect to Network</h3>"
      "<select id='ssid'><option value=''>Click Scan first</option></select>"
      "<button id='scanBtn' class='btn-green' onclick='scanNetworks()'>🔍 Scan Networks</button>"
      "<input type='password' id='pass' placeholder='Password' style='width: 96%;'>"
      "<button onclick='connectWiFi()'>🔌 Connect</button></div>"
      "<div class='card'><h3>🗑 Reset Settings</h3>"
      "<button class='btn-red' onclick='resetWiFi()'>Reset WiFi Settings</button></div>");

    // Діагностика
    String dbg =
      "<div class='card'><h3>🔧 Diagnostic</h3><div class='dbg'>"
      "Status: "
      + String(WiFi.status()) + "\n"
                                "SSID:   "
      + WiFi.SSID() + "\n"
                      "RSSI:   "
      + String(WiFi.RSSI()) + " dBm\n"
                              "IP:     "
      + WiFi.localIP().toString() + "\n"
                                    "MAC:    "
      + WiFi.macAddress() + "</div></div>";

    server.sendContent(dbg);
    server.sendContent("</div></body></html>");
  }

  // ══════════════════════════════════════════
  //  WIFI: SCAN
  // ══════════════════════════════════════════
  static void handleWiFiScan() {
    int n = WiFi.scanComplete();

    if (n == -2) {
      WiFi.scanNetworks(true);
      server.send(200, "application/json", "{\"scanning\":true}");
      return;
    }

    if (n == -1) {
      server.send(200, "application/json", "{\"scanning\":true}");
      return;
    }

    JsonDocument doc;
    JsonArray networks = doc["networks"].to<JsonArray>();

    for (int i = 0; i < n; i++) {
      JsonObject net = networks.add<JsonObject>();
      net["ssid"] = WiFi.SSID(i);
      net["rssi"] = WiFi.RSSI(i);
      net["encrypted"] = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
    }

    WiFi.scanDelete();

    String json;
    serializeJson(doc, json);
    server.send(200, "application/json", json);
  }

  // ══════════════════════════════════════════
  //  WIFI: CONNECT
  // ══════════════════════════════════════════
  static void handleWiFiConnect() {
    String ssid = server.arg("ssid");
    String pass = server.arg("pass");

    if (ssid.isEmpty()) {
      server.send(400, "text/plain", "Missing SSID");
      return;
    }

    Serial.printf("[WiFi] Connecting to: %s\n", ssid.c_str());

    // Спочатку відповідь, потім дії
    server.send(200, "text/plain", "OK");

    WiFi.begin(ssid.c_str(), pass.c_str());
    vTaskDelay(pdMS_TO_TICKS(1000));
    ESP.restart();
  }

  // ══════════════════════════════════════════
  //  WIFI: RESET
  // ══════════════════════════════════════════
  static void handleWiFiReset() {
    Serial.println("[WiFi] Reset requested");

    server.send(200, "text/plain", "OK");

    wm.resetSettings();
    vTaskDelay(pdMS_TO_TICKS(500));
    ESP.restart();
  }

  // ══════════════════════════════════════════
  //  WIFI: STATUS (JSON)
  // ══════════════════════════════════════════
  static void handleWiFiStatus() {
    JsonDocument doc;
    doc["connected"] = (WiFi.status() == WL_CONNECTED);
    doc["ssid"] = WiFi.SSID();
    doc["rssi"] = WiFi.RSSI();
    doc["ip"] = WiFi.localIP().toString();

    String json;
    serializeJson(doc, json);
    server.send(200, "application/json", json);
  }

  // ══════════════════════════════════════════
  //  SYSTEM INFO
  // ══════════════════════════════════════════
  static void handleSystem() {
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "text/html", "");

    server.sendContent(
      "<!DOCTYPE html><html><head>"
      "<meta charset='UTF-8'>"
      "<meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<link rel='icon' type='image/svg+xml' href=\"data:image/svg+xml,"
      "%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 100 100'%3E"
      "%3Ctext y='.9em' font-size='90'%3E📻%3C/text%3E%3C/svg%3E\">"
      "<style>"
      "body{font-family:Arial,sans-serif;margin:0;padding:20px;background:#f5f5f5}"
      ".container{max-width:600px;margin:0 auto}"
      ".card{background:#fff;border-radius:10px;padding:20px;margin-bottom:20px;"
      "box-shadow:0 2px 5px rgba(0,0,0,.1)}"
      "h2,h3{color:#333;margin-top:0}"
      ".nav{display:flex;justify-content:center;gap:20px;margin-bottom:20px}"
      ".nav a{text-decoration:none;background:#2196F3;color:#fff;"
      "padding:10px 20px;border-radius:5px}"
      ".info-grid{display:grid;grid-template-columns:1fr 1fr;gap:10px}"
      ".info-item{background:#f8f9fa;padding:12px;border-radius:8px}"
      ".info-item .label{color:#666;font-size:12px;margin-bottom:4px}"
      ".info-item .value{font-size:18px;font-weight:bold;color:#333}"
      ".bar{height:20px;background:#e0e0e0;border-radius:10px;overflow:hidden;margin-top:5px}"
      ".bar-fill{height:100%;border-radius:10px}"
      ".btn-ota{background:#ff9800;color:#fff;border:none;padding:12px 24px;"
      "border-radius:5px;cursor:pointer;font-size:16px;width:100%;margin-top:10px}"
      ".btn-red{background:#f44336;color:#fff;border:none;padding:12px 24px;"
      "border-radius:5px;cursor:pointer;font-size:16px;width:100%;margin-top:10px}"
      "</style>"
      "<script>"
      "function refreshSysInfo(){"
      " fetch('/status').then(r=>r.json()).then(d=>{"
      "  let r=document.getElementById('rssi');"
      "  if(r)r.textContent=d.rssi+' dBm';"
      " });"
      "}"
      "setInterval(refreshSysInfo,5000);"
      "</script>"
      "</head><body><div class='container'>"
      "<div class='nav'>"
      "<a href='/'>📻 Radio</a>"
      "<a href='/wifi'>📡 WiFi</a>"
      "<a href='/system'>⚙ System</a>"
      "</div>");

    unsigned long uptimeSec = millis() / 1000;
    unsigned long uptimeMin = uptimeSec / 60;
    unsigned long uptimeHrs = uptimeMin / 60;
    unsigned long uptimeDays = uptimeHrs / 24;

    char uptimeStr[32];
    snprintf(uptimeStr, sizeof(uptimeStr), "%lud %luh %lum",
             uptimeDays, uptimeHrs % 24, uptimeMin % 60);

    uint32_t totalHeap = ESP.getHeapSize();
    uint32_t freeHeap = ESP.getFreeHeap();
    uint32_t minHeap = ESP.getMinFreeHeap();
    uint32_t usedHeap = totalHeap - freeHeap;
    int heapPercent = (usedHeap * 100) / totalHeap;

    uint32_t sketchSize = ESP.getSketchSize();
    uint32_t sketchFree = ESP.getFreeSketchSpace();
    uint32_t sketchTotal = sketchSize + sketchFree;
    int flashPercent = (sketchSize * 100) / sketchTotal;

    String sysInfo =
      "<div class='card'>"
      "<h2>⚙ System Information</h2>"
      "<div class='info-grid'>"

      "<div class='info-item'>"
      "<div class='label'>Uptime</div>"
      "<div class='value'>"
      + String(uptimeStr) + "</div>"
                            "</div>"

                            "<div class='info-item'>"
                            "<div class='label'>WiFi RSSI</div>"
                            "<div class='value' id='rssi'>"
      + String(WiFi.RSSI()) + " dBm</div>"
                              "</div>"

                              "<div class='info-item'>"
                              "<div class='label'>IP Address</div>"
                              "<div class='value'>"
      + WiFi.localIP().toString() + "</div>"
                                    "</div>"

                                    "<div class='info-item'>"
                                    "<div class='label'>Stations</div>"
                                    "<div class='value'>"
      + String(SettingsManager::totalStations) + "</div>"
                                                 "</div>"

                                                 "<div class='info-item'>"
                                                 "<div class='label'>ESP32 Chip</div>"
                                                 "<div class='value'>"
      + String(ESP.getChipModel()) + "</div>"
                                     "</div>"

                                     "<div class='info-item'>"
                                     "<div class='label'>CPU Freq</div>"
                                     "<div class='value'>"
      + String(ESP.getCpuFreqMHz()) + " MHz</div>"
                                      "</div>"

                                      "</div></div>";

    server.sendContent(sysInfo);

    String heapCard =
      "<div class='card'>"
      "<h3>💾 Memory</h3>"
      "<div class='info-grid'>"
      "<div class='info-item'>"
      "<div class='label'>Heap Used</div>"
      "<div class='value'>"
      + String(usedHeap / 1024) + " / " + String(totalHeap / 1024) + " KB</div>"
                                                                     "<div class='bar'><div class='bar-fill' style='width:"
      + String(heapPercent) + "%;background:" + String(heapPercent > 80 ? "#f44336" : heapPercent > 60 ? "#ff9800"
                                                                                                       : "#4CAF50")
      + "'></div></div>"
        "</div>"
        "<div class='info-item'>"
        "<div class='label'>Min Free Heap</div>"
        "<div class='value'>"
      + String(minHeap / 1024) + " KB</div>"
                                 "</div>"
                                 "</div></div>";

    server.sendContent(heapCard);

    String flashCard =
      "<div class='card'>"
      "<h3>📦 Firmware</h3>"
      "<div class='info-grid'>"
      "<div class='info-item'>"
      "<div class='label'>Sketch Used</div>"
      "<div class='value'>"
      + String(sketchSize / 1024) + " / " + String(sketchTotal / 1024) + " KB</div>"
                                                                         "<div class='bar'><div class='bar-fill' style='width:"
      + String(flashPercent) + "%;background:" + String(flashPercent > 80 ? "#f44336" : flashPercent > 60 ? "#ff9800"
                                                                                                          : "#4CAF50")
      + "'></div></div>"
        "</div>"
        "<div class='info-item'>"
        "<div class='label'>SDK Version</div>"
        "<div class='value' style='font-size:14px'>"
      + String(ESP.getSdkVersion()) + "</div>"
                                      "</div>"
                                      "</div>"
                                      "<a href='/update'><button class='btn-ota'>📦 OTA Update</button></a>"
                                      "<button class='btn-red' onclick=\"if(confirm('Restart?'))fetch('/wifi/reset')\">🔄 Restart</button>"
                                      "</div>";

    server.sendContent(flashCard);
    server.sendContent("</div></body></html>");
  }

  // ══════════════════════════════════════════
  //  OTA UPDATE PAGE
  // ══════════════════════════════════════════
  static void handleUpdatePage() {
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "text/html", "");

    server.sendContent(
      "<!DOCTYPE html><html><head>"
      "<meta charset='UTF-8'>"
      "<meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<link rel='icon' type='image/svg+xml' href=\"data:image/svg+xml,"
      "%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 100 100'%3E"
      "%3Ctext y='.9em' font-size='90'%3E📻%3C/text%3E%3C/svg%3E\">"
      "<style>"
      "body{font-family:Arial,sans-serif;margin:0;padding:20px;background:#f5f5f5}"
      ".container{max-width:600px;margin:0 auto}"
      ".card{background:#fff;border-radius:10px;padding:20px;margin-bottom:20px;"
      "box-shadow:0 2px 5px rgba(0,0,0,.1)}"
      "h2{color:#333;margin-top:0}"
      ".nav{display:flex;justify-content:center;gap:20px;margin-bottom:20px}"
      ".nav a{text-decoration:none;background:#2196F3;color:#fff;padding:10px 20px;border-radius:5px}"
      ".upload-area{border:3px dashed #ccc;border-radius:10px;padding:40px;text-align:center;"
      "margin:20px 0;transition:border-color .3s}"
      ".upload-area:hover{border-color:#2196F3}"
      "input[type=file]{margin:10px 0}"
      ".btn-upload{background:#ff9800;color:#fff;border:none;padding:15px 30px;"
      "border-radius:5px;cursor:pointer;font-size:18px;width:100%}"
      ".btn-upload:disabled{background:#ccc;cursor:not-allowed}"
      ".progress{display:none;margin:20px 0}"
      ".progress-bar{height:30px;background:#e0e0e0;border-radius:15px;overflow:hidden}"
      ".progress-fill{height:100%;background:#4CAF50;border-radius:15px;"
      "transition:width .3s;text-align:center;line-height:30px;color:#fff;font-weight:bold}"
      ".status{text-align:center;margin:10px 0;font-size:18px}"
      ".warn{background:#fff3e0;border-left:4px solid #ff9800;padding:15px;margin:15px 0;border-radius:4px}"
      "</style>"
      "</head><body><div class='container'>"
      "<div class='nav'>"
      "<a href='/'>📻 Radio</a>"
      "<a href='/wifi'>📡 WiFi</a>"
      "<a href='/system'>⚙ System</a>"
      "</div>"
      "<div class='card'>"
      "<h2>📦 OTA Firmware Update</h2>"
      "<div class='warn'>"
      "⚠️ <b>Увага!</b> Під час оновлення не вимикайте пристрій і не закривайте сторінку!"
      "</div>"
      "<form id='otaForm'>"
      "<div class='upload-area'>"
      "<p style='font-size:18px'>📁 Оберіть файл прошивки (.bin)</p>"
      "<input type='file' id='firmware' name='firmware' accept='.bin'>"
      "</div>"
      "<div class='progress' id='progressDiv'>"
      "<div class='progress-bar'><div class='progress-fill' id='progressBar'>0%</div></div>"
      "</div>"
      "<div class='status' id='statusText'></div>"
      "<button type='submit' class='btn-upload' id='uploadBtn'>📦 Upload & Flash</button>"
      "</form>"
      "</div>"
      "<script>"
      "document.getElementById('otaForm').addEventListener('submit',function(e){"
      "  e.preventDefault();"
      "  let f=document.getElementById('firmware').files[0];"
      "  if(!f){alert('Оберіть .bin файл');return;}"
      "  if(!f.name.endsWith('.bin')){alert('Тільки .bin файли!');return;}"
      "  let btn=document.getElementById('uploadBtn');"
      "  btn.disabled=true;btn.textContent='Uploading...';"
      "  document.getElementById('progressDiv').style.display='block';"
      "  let fd=new FormData();"
      "  fd.append('firmware',f);"
      "  let xhr=new XMLHttpRequest();"
      "  xhr.open('POST','/ota',true);"
      "  xhr.upload.onprogress=function(e){"
      "    if(e.lengthComputable){"
      "      let p=Math.round((e.loaded/e.total)*100);"
      "      let bar=document.getElementById('progressBar');"
      "      bar.style.width=p+'%';bar.textContent=p+'%';"
      "      document.getElementById('statusText').textContent='Uploading: '+p+'%';"
      "    }"
      "  };"
      "  xhr.onload=function(){"
      "    if(xhr.status==200){"
      "      document.getElementById('statusText').innerHTML="
      "        '<b style=\"color:#4CAF50\">✅ Update successful! Rebooting...</b>';"
      "      setTimeout(function(){location.href='/';},10000);"
      "    }else{"
      "      document.getElementById('statusText').innerHTML="
      "        '<b style=\"color:#f44336\">❌ Error: '+xhr.responseText+'</b>';"
      "      btn.disabled=false;btn.textContent='📦 Upload & Flash';"
      "    }"
      "  };"
      "  xhr.onerror=function(){"
      "    document.getElementById('statusText').innerHTML="
      "      '<b style=\"color:#f44336\">❌ Connection error</b>';"
      "    btn.disabled=false;btn.textContent='📦 Upload & Flash';"
      "  };"
      "  xhr.send(fd);"
      "});"
      "</script>"
      "</div></body></html>");
  }

  // ══════════════════════════════════════════
  //  OTA UPLOAD HANDLER
  // ══════════════════════════════════════════
  static void handleOTAUpload() {
    HTTPUpload& upload = server.upload();

    if (upload.status == UPLOAD_FILE_START) {
      Serial.printf("[OTA] Start: %s (%u bytes)\n",
                    upload.filename.c_str(), upload.totalSize);

      // Зупиняємо аудіо
      AudioPlayer::setVolume(0);
      AudioPlayer::stop();
      DisplayManager::showMessage("UPDATE...");

      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
        Serial.printf("[OTA] Begin error: %s\n",
                      Update.errorString());
      }

    } else if (upload.status == UPLOAD_FILE_WRITE) {
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        Serial.printf("[OTA] Write error: %s\n",
                      Update.errorString());
      }

    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) {
        Serial.printf("[OTA] Success! %u bytes\n", upload.totalSize);
      } else {
        Serial.printf("[OTA] End error: %s\n",
                      Update.errorString());
      }

    } else if (upload.status == UPLOAD_FILE_ABORTED) {
      Update.abort();
      Serial.println("[OTA] Aborted");
    }
  }

  // ══════════════════════════════════════════
  //  OTA RESULT
  // ══════════════════════════════════════════
  static void handleOTAResult() {
    if (Update.hasError()) {
      server.send(500, "text/plain",
                  "Update failed: " + String(Update.errorString()));
    } else {
      server.send(200, "text/plain", "OK");
      delay(1000);
      ESP.restart();
    }
  }
};

#endif
