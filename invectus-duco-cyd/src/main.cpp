#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <WiFiManager.h>

#include "CST820.h"
#include "Counter.h"
#include "DSHA1.h"

static constexpr char APP_VERSION[] = "1.0.0";
static constexpr char DUCO_PROTOCOL_VERSION[] = "4.3";
static constexpr char DUCO_POOLPICKER[] = "https://server.duinocoin.com/getPool";
static constexpr char DUCO_BALANCE_BASE[] = "https://server.duinocoin.com/balances/";
static constexpr char START_DIFF[] = "ESP32";

static constexpr uint8_t PIN_TOUCH_SDA = 33;
static constexpr uint8_t PIN_TOUCH_SCL = 32;
static constexpr uint8_t PIN_TOUCH_RST = 25;
static constexpr uint8_t PIN_TOUCH_INT = 21;
static constexpr uint8_t SCREEN_ROTATION = 3;
static constexpr uint16_t SCREEN_W = 320;
static constexpr uint16_t SCREEN_H = 240;

static constexpr uint16_t C_BG = TFT_BLACK;
static constexpr uint16_t C_CARD = 0x18E3;
static constexpr uint16_t C_BORDER = 0x3186;
static constexpr uint16_t C_TEXT = TFT_WHITE;
static constexpr uint16_t C_MUTED = 0x9CF3;
static constexpr uint16_t C_GREEN = 0x47E9;
static constexpr uint16_t C_GOLD = 0xFDC0;
static constexpr uint16_t C_CYAN = 0x4E7F;
static constexpr uint16_t C_RED = 0xF986;

TFT_eSPI tft;
CST820 touch(PIN_TOUCH_SDA, PIN_TOUCH_SCL, PIN_TOUCH_RST, PIN_TOUCH_INT);
Preferences prefs;
WiFiManager wifiManager;

struct AppConfig {
  String username;
  String miningKey;
  String rigName;
};

struct MiningStats {
  float coreHashrate[2] = {0, 0};
  uint32_t accepted = 0;
  uint32_t rejected = 0;
  uint32_t blocks = 0;
  uint32_t submitted = 0;
  uint32_t difficulty = 0;
  uint32_t pingMs = 0;
  char node[48] = "-";
  char lastFeedback[20] = "-";
  bool connected[2] = {false, false};
  uint32_t lastShareAt = 0;
};

AppConfig appConfig;
MiningStats stats;
portMUX_TYPE statsMux = portMUX_INITIALIZER_UNLOCKED;

char cfgUser[41] = {};
char cfgKey[65] = {};
char cfgRig[25] = {};
bool saveConfigRequested = false;

String nodeHost;
uint16_t nodePort = 0;
String nodeName = "-";
SemaphoreHandle_t nodeMutex;

TaskHandle_t minerTask0 = nullptr;
TaskHandle_t minerTask1 = nullptr;

enum class Page : uint8_t { Miner, Account, Network, Settings };
Page page = Page::Miner;
bool redrawAll = true;
uint32_t lastUiRefresh = 0;
uint32_t lastBalanceRefresh = 0;
float accountBalance = NAN;
String accountStatus = "Not loaded";
bool touchWasDown = false;
uint32_t resetPressStarted = 0;

String walletId;
String chipId;

static void safeCopy(char *dst, size_t dstSize, const String &src) {
  if (!dst || dstSize == 0) return;
  strlcpy(dst, src.c_str(), dstSize);
}

static String clipped(const String &s, size_t maxLen) {
  if (s.length() <= maxLen) return s;
  if (maxLen < 4) return s.substring(0, maxLen);
  return s.substring(0, maxLen - 3) + "...";
}

static String formatUptime(uint32_t seconds) {
  uint32_t days = seconds / 86400UL;
  seconds %= 86400UL;
  uint32_t hours = seconds / 3600UL;
  seconds %= 3600UL;
  uint32_t mins = seconds / 60UL;
  char buf[24];
  if (days) snprintf(buf, sizeof(buf), "%lud %02lu:%02lu", (unsigned long)days, (unsigned long)hours, (unsigned long)mins);
  else snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu", (unsigned long)hours, (unsigned long)mins, (unsigned long)(seconds % 60UL));
  return String(buf);
}

static void drawText(const String &text, int x, int y, uint16_t color, uint8_t size = 1) {
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(color, C_BG);
  tft.setTextSize(size);
  tft.drawString(text, x, y);
}

static void drawCard(int x, int y, int w, int h) {
  tft.fillRoundRect(x, y, w, h, 8, C_CARD);
  tft.drawRoundRect(x, y, w, h, 8, C_BORDER);
}

static void drawHeader(const char *title) {
  tft.fillRect(0, 0, SCREEN_W, 32, C_BG);
  drawText("INVECTUS DUCO", 8, 7, C_GOLD, 2);
  int rssi = WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : -127;
  String wifi = WiFi.status() == WL_CONNECTED ? (String("WiFi ") + String(rssi) + "dBm") : "WiFi offline";
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(WiFi.status() == WL_CONNECTED ? C_GREEN : C_RED, C_BG);
  tft.setTextSize(1);
  tft.drawString(wifi, 312, 6);
  tft.setTextColor(C_MUTED, C_BG);
  tft.drawString(title, 312, 18);
}

static void drawTabs() {
  static const char *labels[] = {"MINER", "ACCOUNT", "NETWORK", "SETTINGS"};
  const int y = 208;
  const int w = 80;
  for (int i = 0; i < 4; ++i) {
    bool active = static_cast<int>(page) == i;
    tft.fillRect(i * w, y, w, 32, active ? C_GOLD : C_CARD);
    tft.drawRect(i * w, y, w, 32, C_BORDER);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(active ? TFT_BLACK : C_TEXT, active ? C_GOLD : C_CARD);
    tft.setTextSize(1);
    tft.drawString(labels[i], i * w + w / 2, y + 16);
  }
}

static MiningStats snapshotStats() {
  MiningStats s;
  portENTER_CRITICAL(&statsMux);
  s = stats;
  portEXIT_CRITICAL(&statsMux);
  return s;
}

static void setNodeStat(const String &name) {
  portENTER_CRITICAL(&statsMux);
  safeCopy(stats.node, sizeof(stats.node), name);
  portEXIT_CRITICAL(&statsMux);
}

static void updateFeedback(const String &feedback, int core, float hashRate, uint32_t diff, uint32_t pingMs) {
  portENTER_CRITICAL(&statsMux);
  stats.coreHashrate[core] = hashRate;
  stats.difficulty = diff;
  stats.pingMs = pingMs;
  stats.submitted++;
  stats.lastShareAt = millis();
  safeCopy(stats.lastFeedback, sizeof(stats.lastFeedback), feedback);
  if (feedback == "GOOD") {
    stats.accepted++;
  } else if (feedback == "BLOCK") {
    stats.accepted++;
    stats.blocks++;
  } else {
    stats.rejected++;
  }
  portEXIT_CRITICAL(&statsMux);
}

static void setCoreConnected(int core, bool connected) {
  portENTER_CRITICAL(&statsMux);
  stats.connected[core] = connected;
  portEXIT_CRITICAL(&statsMux);
}

static bool httpGet(const String &url, String &payload, uint32_t timeoutMs = 8000) {
  if (WiFi.status() != WL_CONNECTED) return false;
  WiFiClientSecure tls;
  tls.setInsecure();
  HTTPClient http;
  http.setTimeout(timeoutMs);
  if (!http.begin(tls, url)) return false;
  http.addHeader("Accept", "application/json");
  int code = http.GET();
  bool ok = code == HTTP_CODE_OK || code == HTTP_CODE_MOVED_PERMANENTLY;
  if (ok) payload = http.getString();
  http.end();
  return ok;
}

static bool selectNode() {
  String payload;
  if (!httpGet(DUCO_POOLPICKER, payload, 10000)) return false;

  DynamicJsonDocument doc(512);
  DeserializationError err = deserializeJson(doc, payload);
  if (err) return false;

  String host = doc["ip"] | "";
  int port = doc["port"] | 0;
  String name = doc["name"] | "Duino-Coin";
  if (host.isEmpty() || port <= 0 || port > 65535) return false;

  if (xSemaphoreTake(nodeMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
    nodeHost = host;
    nodePort = static_cast<uint16_t>(port);
    nodeName = name;
    xSemaphoreGive(nodeMutex);
  }
  setNodeStat(name);
  return true;
}

static void loadConfig() {
  prefs.begin("inv-duco", true);
  appConfig.username = prefs.getString("username", "");
  appConfig.miningKey = prefs.getString("key", "");
  appConfig.rigName = prefs.getString("rig", "Invectus-CYD");
  prefs.end();
}

static void saveConfig() {
  appConfig.username = String(cfgUser);
  appConfig.miningKey = String(cfgKey);
  appConfig.rigName = String(cfgRig);
  appConfig.username.trim();
  appConfig.rigName.trim();
  if (appConfig.rigName.isEmpty()) appConfig.rigName = "Invectus-CYD";

  prefs.begin("inv-duco", false);
  prefs.putString("username", appConfig.username);
  prefs.putString("key", appConfig.miningKey);
  prefs.putString("rig", appConfig.rigName);
  prefs.end();
}

static void onSaveParams() {
  saveConfigRequested = true;
}

static void setupPortal() {
  safeCopy(cfgUser, sizeof(cfgUser), appConfig.username);
  safeCopy(cfgKey, sizeof(cfgKey), appConfig.miningKey);
  safeCopy(cfgRig, sizeof(cfgRig), appConfig.rigName);

  WiFiManagerParameter userParam("duco_usr", "Duino-Coin username", cfgUser, sizeof(cfgUser));
  WiFiManagerParameter keyParam("duco_key", "Mining key (optional)", cfgKey, sizeof(cfgKey));
  WiFiManagerParameter rigParam("duco_rig", "Rig name", cfgRig, sizeof(cfgRig));

  wifiManager.setSaveParamsCallback(onSaveParams);
  wifiManager.addParameter(&userParam);
  wifiManager.addParameter(&keyParam);
  wifiManager.addParameter(&rigParam);
  wifiManager.setConnectTimeout(20);
  wifiManager.setConfigPortalTimeout(0);

  String apName = "INVECTUS-DUCO-" + chipId.substring(chipId.length() - 4);
  tft.fillScreen(C_BG);
  drawText("INVECTUS DUCO SETUP", 12, 18, C_GOLD, 2);
  drawText("Connect to:", 12, 64, C_MUTED, 1);
  drawText(apName, 12, 84, C_CYAN, 2);
  drawText("Password: invectus", 12, 116, C_TEXT, 1);
  drawText("Then open 192.168.4.1", 12, 138, C_TEXT, 1);
  drawText("Enter Wi-Fi + DUCO account", 12, 160, C_MUTED, 1);

  bool ok = false;
  if (appConfig.username.isEmpty()) {
    ok = wifiManager.startConfigPortal(apName.c_str(), "invectus");
  } else {
    ok = wifiManager.autoConnect(apName.c_str(), "invectus");
  }

  if (!ok) {
    delay(1200);
    ESP.restart();
  }

  strlcpy(cfgUser, userParam.getValue(), sizeof(cfgUser));
  strlcpy(cfgKey, keyParam.getValue(), sizeof(cfgKey));
  strlcpy(cfgRig, rigParam.getValue(), sizeof(cfgRig));

  if (saveConfigRequested || appConfig.username.isEmpty()) saveConfig();
  loadConfig();

  if (appConfig.username.isEmpty()) {
    wifiManager.resetSettings();
    prefs.begin("inv-duco", false);
    prefs.clear();
    prefs.end();
    ESP.restart();
  }
}

static bool readLine(WiFiClient &client, String &out, uint32_t timeoutMs) {
  out = "";
  uint32_t start = millis();
  while (client.connected() && millis() - start < timeoutMs) {
    while (client.available()) {
      char c = static_cast<char>(client.read());
      if (c == '\n') {
        out.trim();
        return !out.isEmpty();
      }
      if (c != '\r' && out.length() < 768) out += c;
    }
    vTaskDelay(pdMS_TO_TICKS(2));
  }
  return false;
}

static bool hexToHash(const String &hex, uint8_t out[20]) {
  if (hex.length() < 40) return false;
  auto nibble = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  for (int i = 0; i < 20; ++i) {
    int a = nibble(hex[i * 2]);
    int b = nibble(hex[i * 2 + 1]);
    if (a < 0 || b < 0) return false;
    out[i] = static_cast<uint8_t>((a << 4) | b);
  }
  return true;
}

static bool getNodeCopy(String &host, uint16_t &port, String &name) {
  if (xSemaphoreTake(nodeMutex, pdMS_TO_TICKS(500)) != pdTRUE) return false;
  host = nodeHost;
  port = nodePort;
  name = nodeName;
  xSemaphoreGive(nodeMutex);
  return !host.isEmpty() && port != 0;
}

static void minerWorker(void *arg) {
  const int core = reinterpret_cast<intptr_t>(arg);
  WiFiClient client;
  client.setTimeout(12);
  DSHA1 sha;
  sha.warmup();
  uint8_t expected[20];
  uint8_t resultHash[20];
  uint32_t lastNodeRefresh = 0;

  for (;;) {
    if (WiFi.status() != WL_CONNECTED) {
      setCoreConnected(core, false);
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }

    String host, name;
    uint16_t port = 0;
    if (!getNodeCopy(host, port, name)) {
      if (core == 0 && millis() - lastNodeRefresh > 5000) {
        selectNode();
        lastNodeRefresh = millis();
      }
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }

    if (!client.connected()) {
      setCoreConnected(core, false);
      client.stop();
      if (!client.connect(host.c_str(), port, 5000)) {
        vTaskDelay(pdMS_TO_TICKS(1200));
        continue;
      }
      String banner;
      if (!readLine(client, banner, 7000)) {
        client.stop();
        continue;
      }
      setCoreConnected(core, true);
    }

    String jobRequest = "JOB," + appConfig.username + "," + START_DIFF + "," + appConfig.miningKey + "\n";
    if (client.print(jobRequest) == 0) {
      client.stop();
      continue;
    }

    String jobLine;
    if (!readLine(client, jobLine, 12000)) {
      client.stop();
      continue;
    }

    int comma1 = jobLine.indexOf(',');
    int comma2 = comma1 >= 0 ? jobLine.indexOf(',', comma1 + 1) : -1;
    if (comma1 <= 0 || comma2 <= comma1) {
      client.stop();
      continue;
    }

    String lastBlock = jobLine.substring(0, comma1);
    String expectedHex = jobLine.substring(comma1 + 1, comma2);
    uint32_t diffBase = static_cast<uint32_t>(jobLine.substring(comma2 + 1).toInt());
    uint32_t difficulty = diffBase * 100UL + 1UL;
    if (difficulty < 1 || !hexToHash(expectedHex, expected)) {
      client.stop();
      continue;
    }

    DSHA1 base;
    base.reset().write(reinterpret_cast<const unsigned char *>(lastBlock.c_str()), lastBlock.length());

    uint32_t started = micros();
    bool found = false;
    uint32_t resultCounter = 0;

    for (Counter<10> counter; static_cast<uint32_t>(counter) < difficulty; ++counter) {
      DSHA1 ctx = base;
      ctx.write(reinterpret_cast<const unsigned char *>(counter.c_str()), counter.strlen()).finalize(resultHash);
      if (memcmp(expected, resultHash, 20) == 0) {
        resultCounter = static_cast<uint32_t>(counter);
        found = true;
        break;
      }
      if ((static_cast<uint32_t>(counter) & 0x1FFFu) == 0) taskYIELD();
    }

    if (!found) {
      client.stop();
      continue;
    }

    uint32_t elapsedUs = micros() - started;
    if (elapsedUs == 0) elapsedUs = 1;
    float elapsedSeconds = elapsedUs / 1000000.0f;
    float hashRate = resultCounter / elapsedSeconds;

    String submission = String(resultCounter) + "," +
                        String(hashRate, 2) + "," +
                        "Invectus CYD ESP32 Miner " + APP_VERSION + "," +
                        appConfig.rigName + "," +
                        "DUCOID" + chipId + "," +
                        walletId + "\n";

    uint32_t pingStart = millis();
    if (client.print(submission) == 0) {
      client.stop();
      continue;
    }

    String feedback;
    if (!readLine(client, feedback, 12000)) {
      client.stop();
      continue;
    }
    uint32_t ping = millis() - pingStart;
    feedback.trim();
    updateFeedback(feedback, core, hashRate, diffBase, ping);

    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

static void refreshBalance() {
  if (WiFi.status() != WL_CONNECTED || appConfig.username.isEmpty()) return;
  String payload;
  String url = String(DUCO_BALANCE_BASE) + appConfig.username;
  if (!httpGet(url, payload, 7000)) {
    accountStatus = "API unavailable";
    return;
  }
  DynamicJsonDocument doc(1024);
  if (deserializeJson(doc, payload)) {
    accountStatus = "Invalid API reply";
    return;
  }
  bool success = doc["success"] | false;
  if (!success) {
    accountStatus = "Account lookup failed";
    return;
  }
  accountBalance = doc["result"]["balance"] | NAN;
  accountStatus = "Updated";
}

static void drawMinerPage() {
  drawHeader("MINER");
  MiningStats s = snapshotStats();

  drawCard(7, 38, 198, 78);
  float totalKh = (s.coreHashrate[0] + s.coreHashrate[1]) / 1000.0f;
  drawText("TOTAL HASHRATE", 17, 48, C_MUTED, 1);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(C_GREEN, C_CARD);
  tft.setTextSize(4);
  tft.drawString(String(totalKh, 1), 16, 68);
  tft.setTextColor(C_MUTED, C_CARD);
  tft.setTextSize(2);
  tft.drawString("kH/s", 143, 82);

  drawCard(213, 38, 100, 78);
  tft.setTextColor(C_MUTED, C_CARD);
  tft.setTextSize(1);
  tft.drawString("SHARES", 223, 48);
  tft.setTextColor(C_TEXT, C_CARD);
  tft.setTextSize(2);
  tft.drawString(String(s.accepted) + "/" + String(s.rejected), 223, 67);
  tft.setTextColor(C_GOLD, C_CARD);
  tft.setTextSize(1);
  tft.drawString(String(s.blocks) + " block(s)", 223, 94);

  drawCard(7, 124, 306, 76);
  tft.setTextColor(C_MUTED, C_CARD);
  tft.setTextSize(1);
  tft.drawString("Core 0", 17, 134);
  tft.drawString("Core 1", 17, 154);
  tft.drawString("Ping", 170, 134);
  tft.drawString("Diff", 170, 154);
  tft.drawString("Uptime", 17, 178);

  tft.setTextColor(C_TEXT, C_CARD);
  tft.drawString(String(s.coreHashrate[0] / 1000.0f, 1) + " kH/s", 70, 134);
  tft.drawString(String(s.coreHashrate[1] / 1000.0f, 1) + " kH/s", 70, 154);
  tft.drawString(String(s.pingMs) + " ms", 213, 134);
  tft.drawString(String(s.difficulty), 213, 154);
  tft.drawString(formatUptime(millis() / 1000UL), 70, 178);
  tft.setTextColor((s.connected[0] || s.connected[1]) ? C_GREEN : C_RED, C_CARD);
  tft.drawString(clipped(String(s.node), 18), 213, 178);
}

static void drawAccountPage() {
  drawHeader("ACCOUNT");
  drawCard(7, 38, 306, 70);
  drawText("DUINO-COIN ACCOUNT", 18, 48, C_MUTED, 1);
  tft.setTextColor(C_TEXT, C_CARD);
  tft.setTextSize(2);
  tft.drawString(clipped(appConfig.username, 22), 18, 69);
  drawText("Rig: " + clipped(appConfig.rigName, 20), 18, 91, C_MUTED, 1);

  drawCard(7, 116, 306, 84);
  tft.setTextColor(C_MUTED, C_CARD);
  tft.setTextSize(1);
  tft.drawString("BALANCE", 18, 126);
  tft.setTextColor(C_GOLD, C_CARD);
  tft.setTextSize(3);
  String bal = isnan(accountBalance) ? "--" : String(accountBalance, 6);
  tft.drawString(bal + " DUCO", 18, 148);
  tft.setTextColor(C_MUTED, C_CARD);
  tft.setTextSize(1);
  tft.drawString(accountStatus, 18, 184);
}

static void drawNetworkPage() {
  drawHeader("NETWORK");
  MiningStats s = snapshotStats();
  drawCard(7, 38, 306, 162);

  tft.setTextColor(C_MUTED, C_CARD);
  tft.setTextSize(1);
  tft.drawString("Pool node", 18, 50);
  tft.drawString("Local IP", 18, 78);
  tft.drawString("Wi-Fi RSSI", 18, 106);
  tft.drawString("Core links", 18, 134);
  tft.drawString("Last result", 18, 162);
  tft.drawString("Firmware", 18, 186);

  tft.setTextColor(C_TEXT, C_CARD);
  tft.drawString(clipped(String(s.node), 23), 108, 50);
  tft.drawString(WiFi.localIP().toString(), 108, 78);
  tft.drawString(String(WiFi.RSSI()) + " dBm", 108, 106);
  tft.drawString(String(s.connected[0] ? "UP" : "DOWN") + " / " + (s.connected[1] ? "UP" : "DOWN"), 108, 134);
  tft.setTextColor((String(s.lastFeedback) == "GOOD" || String(s.lastFeedback) == "BLOCK") ? C_GREEN : C_TEXT, C_CARD);
  tft.drawString(String(s.lastFeedback), 108, 162);
  tft.setTextColor(C_TEXT, C_CARD);
  tft.drawString(String("Invectus ") + APP_VERSION + " / DUCO " + DUCO_PROTOCOL_VERSION, 108, 186);
}

static void drawSettingsPage() {
  drawHeader("SETTINGS");
  drawCard(7, 38, 306, 103);
  tft.setTextColor(C_MUTED, C_CARD);
  tft.setTextSize(1);
  tft.drawString("Account", 18, 50);
  tft.drawString("Rig", 18, 76);
  tft.drawString("Setup IP", 18, 102);
  tft.drawString("AP password", 18, 126);

  tft.setTextColor(C_TEXT, C_CARD);
  tft.drawString(clipped(appConfig.username, 24), 100, 50);
  tft.drawString(clipped(appConfig.rigName, 24), 100, 76);
  tft.drawString(WiFi.localIP().toString(), 100, 102);
  tft.drawString("invectus", 100, 126);

  tft.fillRoundRect(30, 154, 260, 43, 8, C_RED);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, C_RED);
  tft.setTextSize(2);
  tft.drawString("HOLD TO RESET SETUP", 160, 175);
}

static void drawCurrentPage() {
  tft.fillScreen(C_BG);
  switch (page) {
    case Page::Miner: drawMinerPage(); break;
    case Page::Account: drawAccountPage(); break;
    case Page::Network: drawNetworkPage(); break;
    case Page::Settings: drawSettingsPage(); break;
  }
  drawTabs();
  redrawAll = false;
}

static void eraseSetupAndRestart() {
  wifiManager.resetSettings();
  prefs.begin("inv-duco", false);
  prefs.clear();
  prefs.end();

  tft.fillScreen(C_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_GOLD, C_BG);
  tft.setTextSize(2);
  tft.drawString("SETUP ERASED", 160, 95);
  tft.setTextColor(C_TEXT, C_BG);
  tft.setTextSize(1);
  tft.drawString("Rebooting to configuration portal...", 160, 130);
  delay(1300);
  ESP.restart();
}

static void handleTouch() {
  uint16_t x = 0, y = 0;
  bool down = touch.getTouch(&x, &y);
  if (down && !touchWasDown) {
    if (y >= 208) {
      int tab = min(3, static_cast<int>(x / 80));
      page = static_cast<Page>(tab);
      redrawAll = true;
      if (page == Page::Account) lastBalanceRefresh = 0;
    } else if (page == Page::Settings && x >= 30 && x <= 290 && y >= 154 && y <= 198) {
      resetPressStarted = millis();
    }
  }

  if (down && page == Page::Settings && resetPressStarted != 0) {
    if (!(x >= 30 && x <= 290 && y >= 154 && y <= 198)) {
      resetPressStarted = 0;
    } else if (millis() - resetPressStarted >= 2500) {
      resetPressStarted = 0;
      eraseSetupAndRestart();
    }
  }

  if (!down) resetPressStarted = 0;
  touchWasDown = down;
}

void setup() {
  Serial.begin(115200);
  delay(100);

  uint64_t mac = ESP.getEfuseMac();
  char chip[13];
  snprintf(chip, sizeof(chip), "%04X%08X", static_cast<uint16_t>(mac >> 32), static_cast<uint32_t>(mac));
  chipId = chip;
  walletId = String(esp_random() % 2811);

  nodeMutex = xSemaphoreCreateMutex();

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  tft.init();
  tft.setRotation(SCREEN_ROTATION);
  tft.fillScreen(C_BG);
  touch.begin(SCREEN_W, SCREEN_H, SCREEN_ROTATION);

  drawText("INVECTUS DUCO", 20, 36, C_GOLD, 3);
  drawText("ESP32-2432S024C", 20, 78, C_TEXT, 2);
  drawText("Firmware " + String(APP_VERSION), 20, 110, C_MUTED, 1);
  drawText("Loading configuration...", 20, 146, C_CYAN, 1);

  loadConfig();
  setupPortal();

  drawText("Connected: " + WiFi.localIP().toString(), 20, 170, C_GREEN, 1);
  delay(500);

  if (!selectNode()) {
    drawText("Poolpicker unavailable - retrying...", 20, 190, C_RED, 1);
  }

  xTaskCreatePinnedToCore(minerWorker, "duco0", 8192, reinterpret_cast<void *>(0), 1, &minerTask0, 0);
  xTaskCreatePinnedToCore(minerWorker, "duco1", 8192, reinterpret_cast<void *>(1), 1, &minerTask1, 1);

  redrawAll = true;
}

void loop() {
  handleTouch();

  if (WiFi.status() != WL_CONNECTED) {
    WiFi.reconnect();
  }

  if (page == Page::Account && millis() - lastBalanceRefresh > 60000UL) {
    lastBalanceRefresh = millis();
    refreshBalance();
    redrawAll = true;
  }

  if (millis() - lastUiRefresh > 1000UL) {
    lastUiRefresh = millis();
    redrawAll = true;
  }

  if (redrawAll) drawCurrentPage();
  delay(20);
}
