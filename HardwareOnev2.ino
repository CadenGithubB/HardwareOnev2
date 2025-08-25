#include <WiFi.h>
#include <esp_http_server.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <time.h>
#include "mbedtls/base64.h"
#include <lwip/sockets.h>
#include "web_shared.h"
#include "web_auth_required.h"
#include "web_login.h"
#include "web_dashboard.h"
#include "web_cli.h"
#include "web_files.h"
#include "web_settings.h"
#include "web_public.h"
#include "web_sensors.h"
#include <lwip/netdb.h>
#include <arpa/inet.h>
#include <memory>
#include <ctype.h>
#include <Wire.h>
#include "Adafruit_seesaw.h"
#include <Adafruit_LSM6DS3TRC.h>
#include <Adafruit_NeoPixel.h>
#include "Adafruit_APDS9960.h"
#include "vl53l4cx_class.h"
#include <Adafruit_MLX90640.h>
#include <vector>

// Define VL53L4CX constants if not defined by the library
#ifndef VL53L4CX_MAX_NB_OF_OBJECTS_PER_ROI
#define VL53L4CX_MAX_NB_OF_OBJECTS_PER_ROI 4  // Maximum number of objects per region of interest
#endif

// Define VL53L4CX default address if not defined by the library
#ifndef VL53L4CX_DEFAULT_DEVICE_ADDRESS
#define VL53L4CX_DEFAULT_DEVICE_ADDRESS 0x29  // Default I2C address of VL53L4CX
#endif

// Forward declarations
static String processCommand(const String& cmd);
void setupWiFi();
void startHttpServer();

// Sensor initialization helper functions
bool initAPDS9960();
bool initToFSensor();
bool initThermalSensor();
void readAPDSColor();
void readAPDSProximity();
void readAPDSGesture();
void readIMUSensor();
void readGamepad();
float readTOFDistance();
bool readThermalPixels();

// ===== Configuration =====
// Change these before flashing
static const char* WIFI_SSID = "WooFoo";
static const char* WIFI_PASS = "fesBah-kotwi3-qaffic";

// File paths (LittleFS)
static const char* SETTINGS_FILE = "/settings.txt";                // key=value
static const char* SETTINGS_JSON_FILE = "/settings.json";          // unified settings JSON
static const char* USERS_FILE    = "/users.txt";                   // legacy: username:password[:role]
static const char* USERS_JSON_FILE = "/users.json";                // preferred: {"users":[{username,password,role}]}

// SSE endpoint: push per-session notices without polling
esp_err_t handleEvents(httpd_req_t* req);
// Sensors status endpoint
esp_err_t handleSensorsStatus(httpd_req_t* req);
static const char* WIFI_FILE     = "/wifi.txt";                    // ssid:password (first line)
static const char* LOG_OK_FILE   = "/logs/successful_login.txt";   // 8KB cap
static const char* LOG_FAIL_FILE = "/logs/failed_login.txt";       // 8KB cap
static const size_t LOG_CAP_BYTES = 8192; // 8 KB

// Sensor objects and state variables
Adafruit_seesaw ss;
Adafruit_LSM6DS3TRC* lsm6ds = nullptr;
Adafruit_APDS9960* apds = nullptr;
VL53L4CX* tofSensor = nullptr;
Adafruit_MLX90640* thermalSensor = nullptr;

// Sensor connection state
bool gamepadConnected = false;
bool gyrosensorConnected = false;
bool tofConnected = false;
bool tofEnabled = false;
bool apdsConnected = false;
bool rgbgestureConnected = false;
bool thermalConnected = false;
bool thermalEnabled = false;

// MLX90640 thermal sensor variables
float* mlx90640_frame = nullptr;
bool mlx90640_initialized = false;
unsigned long mlx90640_last_read = 0;

// Sensor data cache structure
struct SensorDataCache {
  // Thermal sensor data
  float thermalFrame[768];
  float thermalMinTemp = 0.0;
  float thermalMaxTemp = 0.0;
  float thermalAvgTemp = 0.0;
  unsigned long thermalLastUpdate = 0;
  bool thermalDataValid = false;
  
  // IMU data
  float accelX = 0.0, accelY = 0.0, accelZ = 0.0;
  float gyroX = 0.0, gyroY = 0.0, gyroZ = 0.0;
  float imuTemp = 0.0;
  unsigned long imuLastUpdate = 0;
  bool imuDataValid = false;
  
  // ToF data
  float tofDistance = 0.0;
  unsigned long tofLastUpdate = 0;
  bool tofDataValid = false;
  
  // ToF multi-object data (up to 4 objects)
  struct ToFObject {
    bool detected = false;
    bool valid = false;
    int distance_mm = 0;
    float distance_cm = 0.0;
    int status = 0;
    // Smoothing variables
    float smoothed_distance_mm = 0.0;
    float smoothed_distance_cm = 0.0;
    bool hasHistory = false;
  } tofObjects[4];
  int tofTotalObjects = 0;
  
  // APDS data
  uint16_t apdsRed = 0, apdsGreen = 0, apdsBlue = 0, apdsClear = 0;
  uint8_t apdsProximity = 0;
  uint8_t apdsGesture = 0;
  unsigned long apdsLastUpdate = 0;
  bool apdsDataValid = false;
};

SensorDataCache gSensorCache;
const unsigned long MLX90640_READ_INTERVAL = 62; // 16 Hz

// APDS9960 settings
bool apdsColorEnabled = false;
bool apdsProximityEnabled = false;
bool apdsGestureEnabled = false;

// Global sensor-status sequence for SSE fanout
static volatile uint32_t gSensorStatusSeq = 1;
static inline void sensorStatusBump() { uint32_t s = gSensorStatusSeq + 1; if (s == 0) s = 1; gSensorStatusSeq = s; }

static String buildSensorStatusJson() {
  String j = "{";
  j += "\"seq\":" + String(gSensorStatusSeq);
  j += ",\"thermalEnabled\":" + String(thermalEnabled ? 1 : 0);
  j += ",\"tofEnabled\":" + String(tofEnabled ? 1 : 0);
  j += ",\"apdsColorEnabled\":" + String(apdsColorEnabled ? 1 : 0);
  j += ",\"apdsProximityEnabled\":" + String(apdsProximityEnabled ? 1 : 0);
  j += ",\"apdsGestureEnabled\":" + String(apdsGestureEnabled ? 1 : 0);
  j += "}";
  return j;
}

// Pin configuration
const int TOF_XSHUT_PIN = A1;  // Define XSHUT pin for VL53L4CX sensor

// NeoPixel setup (using board's built-in RGB LED)
#define NUMPIXELS 1
Adafruit_NeoPixel pixels(NUMPIXELS, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);

// Defaults (used if files missing)
static String DEFAULT_AUTH_USER = "admin";
static String DEFAULT_AUTH_PASS = "admin";
static String DEFAULT_WIFI_SSID = "YourWiFi";
static String DEFAULT_WIFI_PASS = "YourPassword";
static String DEFAULT_NTP_SERVER = "pool.ntp.org";
static int DEFAULT_TZ_OFFSET_MINUTES = 0; // UTC

// Globals
httpd_handle_t server = NULL;
Preferences prefs;
bool filesystemReady = false;

// CLI state management
enum CLIState {
  CLI_NORMAL,
  CLI_HELP_MAIN,
  CLI_HELP_SYSTEM,
  CLI_HELP_WIFI,
  CLI_HELP_SENSORS,
  CLI_HELP_SETTINGS
};
static CLIState gCLIState = CLI_NORMAL;
static String gHiddenHistory = "";

// Execution context for CLI admin gating (set for web CLI requests)
static bool   gExecFromWeb = false;
static String gExecUser = "";
static bool   gExecIsAdmin = false;
// Feature capability flags from unified pipeline (default true; can be refined later)
static bool   gCapAdminControls = true;
static bool   gCapSensorConfig = true;

// Helper: require admin for protected areas when invoked from web CLI
static String requireAdminFor(const char* area) {
  if (gExecFromWeb && !gExecIsAdmin) {
    return String("Error: Admin required for ") + area + ".";
  }
  // Feature gates
  if (gExecFromWeb) {
    if (strcmp(area, "adminControls") == 0 && !gCapAdminControls) {
      return String("Error: Feature disabled: ") + area + ".";
    }
    if (strcmp(area, "sensorConfig") == 0 && !gCapSensorConfig) {
      return String("Error: Feature disabled: ") + area + ".";
    }
  }
  return "";
}

// Runtime settings
String gAuthUser = DEFAULT_AUTH_USER;
String gAuthPass = DEFAULT_AUTH_PASS;
String gWifiSSID = DEFAULT_WIFI_SSID;
String gWifiPass = DEFAULT_WIFI_PASS;
String gNtpServer = DEFAULT_NTP_SERVER;
int gTzOffsetMinutes = DEFAULT_TZ_OFFSET_MINUTES;
// Settings structure
struct Settings {
  uint16_t version;
  String wifiSSID;
  String wifiPassword;
  bool wifiAutoReconnect;
  int cliHistorySize;
  String ntpServer;
  int tzOffsetMinutes;
  bool outSerial;     // persist output lanes
  bool outWeb;
  bool outTft;
  // Sensors UI (non-advanced)
  int thermalPollingMs;
  int tofPollingMs;
  int tofStabilityThreshold;
  String thermalPaletteDefault;
  // Advanced UI + firmware-affecting
  float thermalEWMAFactor;
  int thermalTransitionMs;
  int tofTransitionMs;
  int tofUiMaxDistanceMm;
  int i2cClockThermalHz;
  int i2cClockToFHz;
  int thermalTargetFps;
};
Settings gSettings;
// Precomputed Authorization header for fast Basic Auth compare
String gExpectedAuthHeader = ""; // e.g., "Basic dXNlcjpwYXNz"
// Serial CLI buffer
String gSerialCLI = "";

// ---- Multi-SSID WiFi storage ----
#define MAX_WIFI_NETWORKS 8
struct WifiNetwork {
  String ssid;
  String password;
  int priority;      // 1 = highest priority
  bool hidden;       // informational only
  uint32_t lastConnected; // millis when last connected
};
static WifiNetwork gWifiNetworks[MAX_WIFI_NETWORKS];
static int gWifiNetworkCount = 0;

// Prototypes
static void loadWiFiNetworks();
static void saveWiFiNetworks();
static int findWiFiNetwork(const String& ssid);
static void upsertWiFiNetwork(const String& ssid, const String& password, int priority, bool hidden);
static bool removeWiFiNetwork(const String& ssid);
static void sortWiFiByPriority();
static bool connectWiFiIndex(int index0based, unsigned long timeoutMs);
static bool connectWiFiSSID(const String& ssid, unsigned long timeoutMs);

String generateNavigation(const String& activePage, const String& username) {
  String nav =
    "<div class=\"top-menu\">"
    "<div class=\"menu-left\">";
  auto link = [&](const char* href, const char* id, const char* text){
    nav += "<a href=\""; nav += href; nav += "\" class=\"menu-item"; if (activePage==id) nav += " active"; nav += "\">"; nav += text; nav += "</a>"; };
  link("/dashboard","dashboard","Dashboard");
  link("/cli","cli","Command Line");
  link("/sensors","sensors","Sensors");
  link("/files","files","Files");
  link("/settings","settings","Settings");
  link("/public","public","Public");
  nav += "</div>";
  nav += "<div class=\"user-info\">";
  if (username == "guest") {
    nav += "<a href=\"/login\" class=\"login-btn\">Login</a>";
  } else {
    nav += "<div class=\"username\">" + username + "</div>";
    nav += "<a href=\"/logout\" class=\"logout-btn\">Logout</a>";
  }
  nav += "</div></div>";
  return nav;
}

// Universal page streaming function
void streamPageWithContent(httpd_req_t* req, const String& activePage, const String& username, void (*contentStreamer)(httpd_req_t*)) {
  httpd_resp_set_type(req, "text/html");
  
  // Streaming debug: reset per-request counters
  extern void streamDebugReset(const char* tag);
  streamDebugReset(activePage.c_str());

  // Stream page-specific content only - no navigation wrapper
  if (contentStreamer) {
    contentStreamer(req);
  }
  
  // End chunked response
  httpd_resp_send_chunk(req, NULL, 0);

  // Streaming debug: print summary for this response
  extern void streamDebugFlush();
  streamDebugFlush();
}

// Global reusable buffer for chunked streaming to avoid fragmentation
static char gStreamBuffer[5120]; // 5KB buffer

// ==========================
// Streaming debug instrumentation
// ==========================
static bool gStreamHitMaxChunk = false;
static size_t gStreamMaxChunk = 0;
static size_t gStreamTotalBytes = 0;
static String gStreamTag = "";

void streamDebugReset(const char* tag) {
  gStreamHitMaxChunk = false;
  gStreamMaxChunk = 0;
  gStreamTotalBytes = 0;
  gStreamTag = tag ? String(tag) : String("");
}

inline void streamDebugRecord(size_t sz, size_t chunkLimit) {
  if (sz > gStreamMaxChunk) gStreamMaxChunk = sz;
  gStreamTotalBytes += sz;
  if (sz >= chunkLimit) gStreamHitMaxChunk = true;
}

void streamDebugFlush() {
  // One-line summary per response
  Serial.printf("[STREAM] page=%s total=%uB maxChunk=%uB hitMax=%s buf=%uB\n",
                gStreamTag.c_str(), (unsigned)gStreamTotalBytes, (unsigned)gStreamMaxChunk,
                gStreamHitMaxChunk ? "yes" : "no", (unsigned)(sizeof(gStreamBuffer) - 1));
}

void streamContentGeneric(httpd_req_t* req, const String& content) {
  const char* contentStr = content.c_str();
  size_t contentLen = content.length();
  size_t chunkSize = sizeof(gStreamBuffer) - 1; // Leave space for null terminator
  
  for (size_t i = 0; i < contentLen; i += chunkSize) {
    size_t remainingLen = contentLen - i;
    size_t currentChunkSize = (remainingLen < chunkSize) ? remainingLen : chunkSize;
    // Record streaming metrics
    streamDebugRecord(currentChunkSize, chunkSize);
    
    // Copy chunk to reusable buffer
    memcpy(gStreamBuffer, contentStr + i, currentChunkSize);
    gStreamBuffer[currentChunkSize] = '\0';
    
    httpd_resp_sendstr_chunk(req, gStreamBuffer);
  }
}

void streamSensorsContent(httpd_req_t* req) {
  String u;
  isAuthed(req, u);
  String content = getSensorsPage(u);
  streamContentGeneric(req, content);
}

esp_err_t handleSensorsPage(httpd_req_t* req) {
  String u; String ip; getClientIP(req, ip);
  if (!isAuthed(req, u)) { return sendAuthRequiredResponse(req); }
  logAuthAttempt(true, req->uri, u, ip, "");
  
  streamPageWithContent(req, "sensors", u, streamSensorsContent);
  return ESP_OK;
}

// Sensors status endpoint (auth-protected): returns current enable flags and seq
esp_err_t handleSensorsStatus(httpd_req_t* req) {
  String u; String ip; getClientIP(req, ip);
  if (!isAuthed(req, u)) { return sendAuthRequiredResponse(req); }
  httpd_resp_set_type(req, "application/json");
  String j = buildSensorStatusJson();
  httpd_resp_send(req, j.c_str(), HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

// Admin sessions handlers are defined later, after session structures are declared

// ==========================
// LittleFS helpers
// ==========================

bool fsInit() {
  Serial.println("Initializing LittleFS...");
  if (LittleFS.begin(false)) {
    filesystemReady = true;
  } else {
    Serial.println("LittleFS mount failed, attempting format...");
    if (LittleFS.begin(true)) {
      filesystemReady = true;
      Serial.println("LittleFS formatted and mounted.");
    } else {
      Serial.println("ERROR: LittleFS mount failed even after format.");
      filesystemReady = false;
      return false;
    }
  }

  // Show FS stats
  size_t total = LittleFS.totalBytes();
  size_t used  = LittleFS.usedBytes();
  Serial.printf("FS Total: %u bytes, Used: %u, Free: %u\n", total, used, total - used);

  // Ensure logs dir exists
  LittleFS.mkdir("/logs");
  return true;
}

bool readText(const char* path, String& out) {
  out = "";
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  out = f.readString();
  f.close();
  return true;
}

// Read up to maxBytes from a file into a String (more robust than readString for large files)
bool readTextLimited(const char* path, String& out, size_t maxBytes) {
  out = "";
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  out.reserve(maxBytes);
  const size_t chunk = 512;
  char buf[chunk];
  size_t total = 0;
  while (total < maxBytes) {
    size_t toRead = maxBytes - total;
    if (toRead > chunk) toRead = chunk;
    int n = f.readBytes(buf, toRead);
    if (n <= 0) break;
    for (int i = 0; i < n; ++i) out += buf[i];
    total += n;
  }
  f.close();
  return true;
}

bool writeText(const char* path, const String& in) {
  File f = LittleFS.open(path, "w");
  if (!f) return false;
  f.print(in);
  f.close();
  return true;
}

bool appendLineWithCap(const char* path, const String& line, size_t capBytes) {
  // Append line
  {
    File a = LittleFS.open(path, "a");
    if (!a) return false;
    a.println(line);
    a.close();
  }

  // Enforce cap
  File r = LittleFS.open(path, "r");
  if (!r) return false;
  size_t sz = r.size();
  if (sz <= capBytes) { r.close(); return true; }
  String content = r.readString();
  r.close();

  // Trim oldest lines
  int start = 0;
  while (content.length() > 0 && content.length() > (int)capBytes) {
    int nl = content.indexOf('\n', start);
    if (nl < 0) break;
    content.remove(0, nl + 1);
  }
  return writeText(path, content);
}

// ==========================
// Settings and users
// ==========================

static const uint16_t kSettingsVersion = 1;

static void settingsDefaults() {
  gSettings.version = kSettingsVersion;
  gSettings.wifiSSID = DEFAULT_WIFI_SSID;
  gSettings.wifiPassword = DEFAULT_WIFI_PASS;
  gSettings.wifiAutoReconnect = true;
  gSettings.cliHistorySize = 10;
  gSettings.ntpServer = DEFAULT_NTP_SERVER;
  gSettings.tzOffsetMinutes = DEFAULT_TZ_OFFSET_MINUTES;
  gSettings.outSerial = true;  // default serial on
  gSettings.outWeb = false;
  gSettings.outTft = false;
  // Sensors UI defaults
  gSettings.thermalPollingMs = 200;
  gSettings.tofPollingMs = 300;
  gSettings.tofStabilityThreshold = 3;
  gSettings.thermalPaletteDefault = "turbo";
  // Advanced defaults
  gSettings.thermalEWMAFactor = 0.2f; // blend factor for new value (0..1)
  gSettings.thermalTransitionMs = 120;
  gSettings.tofTransitionMs = 200;
  gSettings.tofUiMaxDistanceMm = 3400;
  gSettings.i2cClockThermalHz = 800000;
  gSettings.i2cClockToFHz = 100000;
  gSettings.thermalTargetFps = 5;
}

// Minimal JSON encode (no external deps)
static String settingsToJson() {
  String j = "{";
  j += "\"version\":" + String(gSettings.version);
  j += ",\"wifiSSID\":\"" + gSettings.wifiSSID + "\"";
  j += ",\"wifiPassword\":\"" + gSettings.wifiPassword + "\"";
  j += ",\"wifiAutoReconnect\":" + String(gSettings.wifiAutoReconnect ? 1 : 0);
  j += ",\"cliHistorySize\":" + String(gSettings.cliHistorySize);
  j += ",\"ntpServer\":\"" + gSettings.ntpServer + "\"";
  j += ",\"tzOffsetMinutes\":" + String(gSettings.tzOffsetMinutes);
  j += ",\"outSerial\":" + String(gSettings.outSerial ? 1 : 0);
  j += ",\"outWeb\":" + String(gSettings.outWeb ? 1 : 0);
  j += ",\"outTft\":" + String(gSettings.outTft ? 1 : 0);
  // Sensors UI
  j += ",\"thermalPollingMs\":" + String(gSettings.thermalPollingMs);
  j += ",\"tofPollingMs\":" + String(gSettings.tofPollingMs);
  j += ",\"tofStabilityThreshold\":" + String(gSettings.tofStabilityThreshold);
  j += ",\"thermalPaletteDefault\":\"" + gSettings.thermalPaletteDefault + "\"";
  // Advanced
  j += ",\"thermalEWMAFactor\":" + String(gSettings.thermalEWMAFactor, 3);
  j += ",\"thermalTransitionMs\":" + String(gSettings.thermalTransitionMs);
  j += ",\"tofTransitionMs\":" + String(gSettings.tofTransitionMs);
  j += ",\"tofUiMaxDistanceMm\":" + String(gSettings.tofUiMaxDistanceMm);
  j += ",\"i2cClockThermalHz\":" + String(gSettings.i2cClockThermalHz);
  j += ",\"i2cClockToFHz\":" + String(gSettings.i2cClockToFHz);
  j += ",\"thermalTargetFps\":" + String(gSettings.thermalTargetFps);
  j += "}";
  return j;
}

static bool parseJsonBool(const String& src, const char* key, bool& out) {
  String k = String("\"") + key + "\":";
  int p = src.indexOf(k);
  if (p < 0) return false;
  p += k.length();
  while (p < (int)src.length() && (src[p] == ' ')) p++;
  if (p >= (int)src.length()) return false;
  // Accept 0/1 or true/false (but we only emit 0/1)
  if (src.startsWith("1", p) || src.startsWith("true", p)) { out = true; return true; }
  if (src.startsWith("0", p) || src.startsWith("false", p)) { out = false; return true; }
  return false;
}

static bool parseJsonInt(const String& src, const char* key, int& out) {
  String k = String("\"") + key + "\":";
  int p = src.indexOf(k);
  if (p < 0) return false;
  p += k.length();
  int end = p;
  while (end < (int)src.length() && isdigit((int)src[end])) end++;
  if (end == p) return false;
  out = src.substring(p, end).toInt();
  return true;
}

static bool parseJsonFloat(const String& src, const char* key, float& out) {
  String k = String("\"") + key + "\":";
  int p = src.indexOf(k);
  if (p < 0) return false;
  p += k.length();
  int end = p;
  bool seenDot = false;
  bool seenDigit = false;
  while (end < (int)src.length()) {
    char c = src[end];
    if (isdigit((int)c)) { seenDigit = true; end++; continue; }
    if (c == '.' && !seenDot) { seenDot = true; end++; continue; }
    break;
  }
  if (!seenDigit) return false;
  out = src.substring(p, end).toFloat();
  return true;
}

static bool parseJsonU16(const String& src, const char* key, uint16_t& out) {
  int tmp = 0;
  if (!parseJsonInt(src, key, tmp)) return false;
  out = (uint16_t)tmp;
  return true;
}

static bool parseJsonString(const String& src, const char* key, String& out) {
  String k = String("\"") + key + "\":\"";
  int p = src.indexOf(k);
  if (p < 0) return false;
  p += k.length();
  int end = src.indexOf('\"', p);
  if (end < 0) return false;
  out = src.substring(p, end);
  return true;
}

// Forward-declare; implemented after OUTPUT_* flags are defined
static void applySettings();

static bool loadUnifiedSettings() {
  if (!filesystemReady) return false;
  if (!LittleFS.exists(SETTINGS_JSON_FILE)) return false;
  String txt; if (!readText(SETTINGS_JSON_FILE, txt)) return false;
  // Start with defaults, then overwrite found keys
  settingsDefaults();
  parseJsonU16(txt, "version", gSettings.version);
  parseJsonString(txt, "wifiSSID", gSettings.wifiSSID);
  parseJsonString(txt, "wifiPassword", gSettings.wifiPassword);
  parseJsonBool(txt, "wifiAutoReconnect", gSettings.wifiAutoReconnect);
  parseJsonInt(txt, "cliHistorySize", gSettings.cliHistorySize);
  parseJsonString(txt, "ntpServer", gSettings.ntpServer);
  parseJsonInt(txt, "tzOffsetMinutes", gSettings.tzOffsetMinutes);
  parseJsonBool(txt, "outSerial", gSettings.outSerial);
  parseJsonBool(txt, "outWeb", gSettings.outWeb);
  parseJsonBool(txt, "outTft", gSettings.outTft);
  // Sensors UI
  parseJsonInt(txt, "thermalPollingMs", gSettings.thermalPollingMs);
  parseJsonInt(txt, "tofPollingMs", gSettings.tofPollingMs);
  parseJsonInt(txt, "tofStabilityThreshold", gSettings.tofStabilityThreshold);
  parseJsonString(txt, "thermalPaletteDefault", gSettings.thermalPaletteDefault);
  // Advanced
  parseJsonFloat(txt, "thermalEWMAFactor", gSettings.thermalEWMAFactor);
  parseJsonInt(txt, "thermalTransitionMs", gSettings.thermalTransitionMs);
  parseJsonInt(txt, "tofTransitionMs", gSettings.tofTransitionMs);
  parseJsonInt(txt, "tofUiMaxDistanceMm", gSettings.tofUiMaxDistanceMm);
  parseJsonInt(txt, "i2cClockThermalHz", gSettings.i2cClockThermalHz);
  parseJsonInt(txt, "i2cClockToFHz", gSettings.i2cClockToFHz);
  parseJsonInt(txt, "thermalTargetFps", gSettings.thermalTargetFps);
  return true;
}

static bool saveUnifiedSettings() {
  if (!filesystemReady) return false;
  // Atomic: write temp then rename
  String json = settingsToJson();
  const char* tmp = "/settings.tmp";
  if (!writeText(tmp, json)) return false;
  // Remove old and rename
  LittleFS.remove(SETTINGS_JSON_FILE);
  if (!LittleFS.rename(tmp, SETTINGS_JSON_FILE)) {
    // Fallback: write directly
    return writeText(SETTINGS_JSON_FILE, json);
  }
  return true;
}

void loadSettings() {
  // Defaults first
  gNtpServer = DEFAULT_NTP_SERVER;
  gTzOffsetMinutes = DEFAULT_TZ_OFFSET_MINUTES;
  if (!filesystemReady) return;
  if (!LittleFS.exists(SETTINGS_FILE)) return;
  String txt;
  if (!readText(SETTINGS_FILE, txt)) return;
  int pos = 0;
  while (pos < (int)txt.length()) {
    int eol = txt.indexOf('\n', pos);
    if (eol < 0) eol = txt.length();
    String line = txt.substring(pos, eol);
    pos = eol + 1;
    line.trim();
    if (line.length() == 0 || line[0] == '#') continue;
    int eq = line.indexOf('=');
    if (eq <= 0) continue;
    String key = line.substring(0, eq); key.trim();
    String val = line.substring(eq + 1); val.trim();
    if (key == "ntp_server") gNtpServer = val;
    else if (key == "tz_offset_minutes") gTzOffsetMinutes = val.toInt();
  }
}

bool loadUsersFromFile(String& outUser, String& outPass) {
  if (!filesystemReady) return false;
  // Prefer JSON users
  if (LittleFS.exists(USERS_JSON_FILE)) {
    String json; if (!readText(USERS_JSON_FILE, json)) return false;
    // Very simple parse: find first user object and extract username/password
    int usersIdx = json.indexOf("\"users\"");
    if (usersIdx < 0) return false;
    int uKey = json.indexOf("\"username\"", usersIdx);
    int pKey = json.indexOf("\"password\"", usersIdx);
    if (uKey < 0 || pKey < 0) return false;
    int uq1 = json.indexOf('"', json.indexOf(':', uKey) + 1);
    int uq2 = json.indexOf('"', uq1 + 1);
    int pq1 = json.indexOf('"', json.indexOf(':', pKey) + 1);
    int pq2 = json.indexOf('"', pq1 + 1);
    if (uq1 < 0 || uq2 <= uq1 || pq1 < 0 || pq2 <= pq1) return false;
    outUser = json.substring(uq1 + 1, uq2);
    outPass = json.substring(pq1 + 1, pq2);
    return outUser.length() > 0;
  }
  // Legacy users.txt first line username:password or username:password:role
  String content;
  if (!readText(USERS_FILE, content)) return false;
  int colonIdx = content.indexOf(':');
  if (colonIdx <= 0) return false;
  outUser = content.substring(0, colonIdx);
  outPass = content.substring(colonIdx + 1);
  outPass.trim();
  int secondColon = outPass.indexOf(':');
  if (secondColon >= 0) { outPass = outPass.substring(0, secondColon); }
  return true;
}

bool loadWifiFromFile(String& outSSID, String& outPass) {
  if (!filesystemReady) return false;
  String content;
  if (!readText(WIFI_FILE, content)) return false;
  int colonIdx = content.indexOf(':');
  if (colonIdx <= 0) return false;
  outSSID = content.substring(0, colonIdx);
  outPass = content.substring(colonIdx + 1);
  outPass.trim(); // Remove any trailing newlines
  return true;
}

bool saveWifiToFile(const String& ssid, const String& pass) {
  if (!filesystemReady) return false;
  String content = ssid + ":" + pass;
  return writeText(WIFI_FILE, content);
}

// ==========================
// Multi-SSID helpers
// ==========================

static int findWiFiNetwork(const String& ssid) {
  for (int i = 0; i < gWifiNetworkCount; ++i) if (gWifiNetworks[i].ssid == ssid) return i;
  return -1;
}

static void sortWiFiByPriority() {
  // Simple selection sort by ascending priority (1 is highest)
  for (int i = 0; i < gWifiNetworkCount; ++i) {
    int minIdx = i;
    for (int j = i + 1; j < gWifiNetworkCount; ++j) {
      if (gWifiNetworks[j].priority < gWifiNetworks[minIdx].priority) minIdx = j;
    }
    if (minIdx != i) {
      WifiNetwork tmp = gWifiNetworks[i];
      gWifiNetworks[i] = gWifiNetworks[minIdx];
      gWifiNetworks[minIdx] = tmp;
    }
  }
}

static void upsertWiFiNetwork(const String& ssid, const String& password, int priority, bool hidden) {
  int idx = findWiFiNetwork(ssid);
  if (idx >= 0) {
    gWifiNetworks[idx].password = password;
    if (priority > 0) gWifiNetworks[idx].priority = priority;
    gWifiNetworks[idx].hidden = hidden;
    return;
  }
  if (gWifiNetworkCount >= MAX_WIFI_NETWORKS) {
    Serial.println("[WiFi] Network list full; cannot add");
    return;
  }
  WifiNetwork nw; nw.ssid = ssid; nw.password = password; nw.priority = (priority > 0) ? priority : 1; nw.hidden = hidden; nw.lastConnected = 0;
  gWifiNetworks[gWifiNetworkCount++] = nw;
}

static bool removeWiFiNetwork(const String& ssid) {
  int idx = findWiFiNetwork(ssid);
  if (idx < 0) return false;
  for (int i = idx + 1; i < gWifiNetworkCount; ++i) gWifiNetworks[i - 1] = gWifiNetworks[i];
  gWifiNetworkCount--;
  return true;
}

// File format: priority:ssid:password:hidden:lastConnected (one per line)
static void loadWiFiNetworks() {
  gWifiNetworkCount = 0;
  if (!filesystemReady || !LittleFS.exists(WIFI_FILE)) return;
  File f = LittleFS.open(WIFI_FILE, "r");
  if (!f) return;
  while (f.available() && gWifiNetworkCount < MAX_WIFI_NETWORKS) {
    String line = f.readStringUntil('\n'); line.trim(); if (line.length() == 0) continue;
    int p1 = line.indexOf(':'); if (p1 < 0) continue;
    int p2 = line.indexOf(':', p1 + 1); if (p2 < 0) continue;
    int p3 = line.indexOf(':', p2 + 1); if (p3 < 0) continue;
    int p4 = line.indexOf(':', p3 + 1); // p4 may be -1 if lastConnected absent
    WifiNetwork nw; nw.priority = 1; nw.hidden = false; nw.lastConnected = 0;
    // Full format only: priority:ssid:password:hidden[:lastConnected]
    nw.priority = line.substring(0, p1).toInt(); if (nw.priority <= 0) nw.priority = 1;
    nw.ssid = line.substring(p1 + 1, p2);
    nw.password = line.substring(p2 + 1, p3);
    if (p4 >= 0) {
      nw.hidden = line.substring(p3 + 1, p4).toInt() != 0;
      nw.lastConnected = line.substring(p4 + 1).toInt();
    } else {
      nw.hidden = line.substring(p3 + 1).toInt() != 0;
      nw.lastConnected = 0;
    }
    gWifiNetworks[gWifiNetworkCount++] = nw;
  }
  f.close();
  sortWiFiByPriority();
}

static void saveWiFiNetworks() {
  if (!filesystemReady) return;
  File f = LittleFS.open(WIFI_FILE, "w"); if (!f) return;
  for (int i = 0; i < gWifiNetworkCount; ++i) {
    const WifiNetwork& nw = gWifiNetworks[i];
    f.printf("%d:%s:%s:%d:%u\n", nw.priority, nw.ssid.c_str(), nw.password.c_str(), nw.hidden ? 1 : 0, (unsigned)nw.lastConnected);
  }
  f.close();
}

// Connect to a saved network by index (0-based). Update lastConnected on success.
static bool connectWiFiIndex(int index0based, unsigned long timeoutMs) {
  if (index0based < 0 || index0based >= gWifiNetworkCount) return false;
  const WifiNetwork& nw = gWifiNetworks[index0based];
  Serial.printf("Connecting to [%d] '%s'...\n", index0based + 1, nw.ssid.c_str());
  WiFi.begin(nw.ssid.c_str(), nw.password.c_str());
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
    delay(200);
    Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    broadcastOutput(String("WiFi connected: ") + WiFi.localIP().toString());
    gWifiNetworks[index0based].lastConnected = millis();
    saveWiFiNetworks();
    return true;
  }
  Serial.println("Connection failed.");
  return false;
}

// Connect to a saved network by SSID.
static bool connectWiFiSSID(const String& ssid, unsigned long timeoutMs) {
  int idx = findWiFiNetwork(ssid);
  if (idx < 0) return false;
  return connectWiFiIndex(idx, timeoutMs);
}

// Determine if the given username is admin (any user with role == admin)
bool isAdminUser(const String& who) {
  if (!filesystemReady) return false;
  // Prefer JSON
  if (LittleFS.exists(USERS_JSON_FILE)) {
    String json; if (!readText(USERS_JSON_FILE, json)) return false;
    int usersIdx = json.indexOf("\"users\""); if (usersIdx < 0) return false;
    // Find first user to allow legacy-first-admin fallback
    int firstUKey = json.indexOf("\"username\"", usersIdx);
    String firstUser = "";
    if (firstUKey >= 0) {
      int uq1 = json.indexOf('"', json.indexOf(':', firstUKey) + 1);
      int uq2 = json.indexOf('"', uq1 + 1);
      if (uq1 > 0 && uq2 > uq1) firstUser = json.substring(uq1 + 1, uq2);
    }
    // Search for target user and role
    int pos = usersIdx;
    while (true) {
      int uKey = json.indexOf("\"username\"", pos);
      if (uKey < 0) break;
      int uq1 = json.indexOf('"', json.indexOf(':', uKey) + 1);
      int uq2 = json.indexOf('"', uq1 + 1);
      if (uq1 < 0 || uq2 <= uq1) break;
      String uname = json.substring(uq1 + 1, uq2);
      int rKey = json.indexOf("\"role\"", uKey);
      int nextU = json.indexOf("\"username\"", uKey + 1);
      if (rKey > 0 && (nextU < 0 || rKey < nextU)) {
        int rq1 = json.indexOf('"', json.indexOf(':', rKey) + 1);
        int rq2 = json.indexOf('"', rq1 + 1);
        String role = (rq1 > 0 && rq2 > rq1) ? json.substring(rq1 + 1, rq2) : String("");
        if (uname == who && role == "admin") return true;
      }
      pos = uq2 + 1;
    }
    // Fallback: first user without role is admin
    return (who == firstUser);
  }
  // Legacy users.txt
  if (!LittleFS.exists(USERS_FILE)) return false;
  String txt; if (!readText(USERS_FILE, txt)) return false;
  int pos = 0; bool firstUserChecked = false;
  while (pos < (int)txt.length()) {
    int nl = txt.indexOf('\n', pos); if (nl < 0) nl = txt.length();
    String line = txt.substring(pos, nl); pos = nl + 1; line.trim();
    if (line.length() == 0 || line.startsWith("#")) continue;
    int c1 = line.indexOf(':'); if (c1 <= 0) continue;
    int c2 = line.indexOf(':', c1 + 1);
    String username = line.substring(0, c1); username.trim();
    String role = (c2 >= 0) ? line.substring(c2 + 1) : String(""); role.trim();
    if (username == who && role == "admin") return true;
    if (!firstUserChecked) {
      if (role.length() == 0 && username == who) return true;
      firstUserChecked = true;
    }
  }
  return false;
}

// Validate provided credentials against any entry in users file (username:password[:role])
bool isValidUser(const String& u, const String& p) {
  if (!filesystemReady) return false;
  // JSON first
  if (LittleFS.exists(USERS_JSON_FILE)) {
    String json; if (!readText(USERS_JSON_FILE, json)) return false;
    int pos = json.indexOf("\"users\""); if (pos < 0) return false;
    while (true) {
      int uKey = json.indexOf("\"username\"", pos);
      if (uKey < 0) break;
      int uq1 = json.indexOf('"', json.indexOf(':', uKey) + 1);
      int uq2 = json.indexOf('"', uq1 + 1);
      if (uq1 < 0 || uq2 <= uq1) break;
      String uname = json.substring(uq1 + 1, uq2);
      int pKey = json.indexOf("\"password\"", uKey);
      int nextU = json.indexOf("\"username\"", uKey + 1);
      if (pKey > 0 && (nextU < 0 || pKey < nextU)) {
        int pq1 = json.indexOf('"', json.indexOf(':', pKey) + 1);
        int pq2 = json.indexOf('"', pq1 + 1);
        String pass = (pq1 > 0 && pq2 > pq1) ? json.substring(pq1 + 1, pq2) : String("");
        if (u == uname && p == pass) return true;
      }
      pos = uq2 + 1;
    }
    return false;
  }
  // Legacy users.txt
  if (!LittleFS.exists(USERS_FILE)) return false;
  String txt; if (!readText(USERS_FILE, txt)) return false;
  int pos = 0;
  while (pos < (int)txt.length()) {
    int eol = txt.indexOf('\n', pos); if (eol < 0) eol = txt.length();
    String line = txt.substring(pos, eol); pos = eol + 1; line.trim();
    if (line.length() == 0 || line[0] == '#') continue;
    int c1 = line.indexOf(':'); if (c1 <= 0) continue;
    int c2 = line.indexOf(':', c1 + 1);
    String lu = line.substring(0, c1); lu.trim();
    String lp = (c2 >= 0) ? line.substring(c1 + 1, c2) : line.substring(c1 + 1); lp.trim();
    if (u == lu && p == lp) return true;
  }
  return false;
}

String waitForSerialInput(unsigned long timeoutMs) {
  unsigned long start = millis();
  String input = "";
  while (millis() - start < timeoutMs) {
    if (Serial.available()) {
      input = Serial.readStringUntil('\n');
      input.trim();
      return input;
    }
    delay(10);
  }
  return "";
}

void firstTimeSetupIfNeeded() {
  // Rely solely on presence of users storage; no NVS flag
  bool haveUsers = LittleFS.exists(USERS_JSON_FILE) || LittleFS.exists(USERS_FILE);
  if (haveUsers) return; // already initialized

  Serial.println();
  Serial.println("FIRST-TIME SETUP");
  Serial.println("----------------");
  Serial.println("Enter admin username (default 'admin' if blank): ");
  String u = waitForSerialInput(30000);
  if (u.length() == 0) u = DEFAULT_AUTH_USER;

  String p = "";
  while (p.length() == 0) {
    Serial.println("Enter admin password (cannot be blank): ");
    p = waitForSerialInput(30000);
    p.trim();
  }

  // Create users.json with admin
  String j = "{\n  \"version\": 1,\n  \"users\": [\n    {\n      \"username\": \"" + u + "\",\n      \"password\": \"" + p + "\",\n      \"role\": \"admin\"\n    }\n  ]\n}\n";
  if (!writeText(USERS_JSON_FILE, j)) {
    Serial.println("ERROR: Failed to write users.json");
  } else {
    Serial.println("Saved /users.json");
  }

  // WiFi setup
  Serial.println();
  Serial.println("WiFi Setup");
  Serial.println("----------");
  Serial.println("Enter WiFi SSID (default '" + String(DEFAULT_WIFI_SSID) + "' if blank): ");
  String wifiSSID = waitForSerialInput(30000);
  if (wifiSSID.length() == 0) wifiSSID = DEFAULT_WIFI_SSID;

  String wifiPass = "";
  while (wifiPass.length() == 0) {
    Serial.println("Enter WiFi password (cannot be blank): ");
    wifiPass = waitForSerialInput(30000);
    wifiPass.trim();
  }

  // Initialize unified settings with defaults, then override with user input
  settingsDefaults();
  gSettings.wifiSSID = wifiSSID;
  gSettings.wifiPassword = wifiPass;
  saveUnifiedSettings();
  applySettings();
}

// ==========================
// NTP (minimal)
// ==========================

void setupNTP() {
  long gmtOffset = (long)gTzOffsetMinutes * 60; // seconds
  configTime(gmtOffset, 0, gNtpServer.c_str());
}

time_t nowEpoch() {
  return time(nullptr);
}

// ==========================
// HTTP auth helpers (esp_http_server)
// ==========================

bool getClientIP(httpd_req_t* req, String& ipOut) {
  ipOut = "-";
  int sockfd = httpd_req_to_sockfd(req);
  if (sockfd < 0) return false;
  struct sockaddr_storage addr; socklen_t len = sizeof(addr);
  if (getpeername(sockfd, (struct sockaddr*)&addr, &len) == 0) {
    char buf[64];
    if (addr.ss_family == AF_INET) {
      struct sockaddr_in* a = (struct sockaddr_in*)&addr;
      inet_ntop(AF_INET, &(a->sin_addr), buf, sizeof(buf));
      ipOut = String(buf);
      return true;
    } else if (addr.ss_family == AF_INET6) {
      struct sockaddr_in6* a6 = (struct sockaddr_in6*)&addr;
      inet_ntop(AF_INET6, &(a6->sin6_addr), buf, sizeof(buf));
      ipOut = String(buf);
      return true;
    }
  }
  return false;
}

bool parseBasicAuth(httpd_req_t* req, String& userOut, String& passOut, bool& headerPresent) {
  headerPresent = false;
  userOut = ""; passOut = "";
  size_t len = httpd_req_get_hdr_value_len(req, "Authorization");
  if (len == 0) return false;
  char* buf = (char*)malloc(len + 1);
  if (!buf) return false;
  if (httpd_req_get_hdr_value_str(req, "Authorization", buf, len + 1) != ESP_OK) {
    free(buf);
    return false;
  }
  headerPresent = true;
  String header(buf);
  free(buf);
  // Expect: "Basic base64"
  if (!header.startsWith("Basic ")) return false;
  // Fast path: compare raw header string to precomputed expected
  if (gExpectedAuthHeader.length() && header == gExpectedAuthHeader) {
    userOut = gAuthUser;
    passOut = gAuthPass;
    return true;
  }
  String b64 = header.substring(6);
  b64.trim();
  // Decode base64
  size_t out_len = 0;
  unsigned char out_buf[256];
  int ret = mbedtls_base64_decode(out_buf, sizeof(out_buf), &out_len,
                                  (const unsigned char*)b64.c_str(), b64.length());
  if (ret != 0 || out_len == 0) return false;
  String decoded = String((const char*)out_buf);
  int colon = decoded.indexOf(':');
  if (colon <= 0) return false;
  userOut = decoded.substring(0, colon);
  passOut = decoded.substring(colon + 1);
  return true;
}

// Build the expected Authorization header for fast comparisons
void rebuildExpectedAuthHeader() {
  String creds = gAuthUser + ":" + gAuthPass;
  // Base64-encode creds
  size_t in_len = creds.length();
  size_t out_len = 0;
  // Maximum encoded length ~ 4 * ceil(n/3) + 1
  unsigned char out_buf[256];
  if (in_len > 180) { gExpectedAuthHeader = ""; return; } // safety cap
  if (mbedtls_base64_encode(out_buf, sizeof(out_buf), &out_len,
                            (const unsigned char*)creds.c_str(), in_len) == 0 && out_len > 0) {
    String b64 = String((const char*)out_buf);
    gExpectedAuthHeader = String("Basic ") + b64;
  } else {
    gExpectedAuthHeader = "";
  }
}

void sendAuthRequired(httpd_req_t* req) {
  httpd_resp_set_status(req, "401 Unauthorized");
  httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"ESP32\"");
  const char* msg = "Authentication required";
  httpd_resp_send(req, msg, HTTPD_RESP_USE_STRLEN);
}

// Minimal URL encoder for redirect locations
static String urlEncode(const char* s) {
  String out; if (!s) return out; out.reserve(strlen(s) * 3);
  auto hex = [](uint8_t v)->char { const char* H = "0123456789ABCDEF"; return H[v & 0x0F]; };
  for (const char* p = s; *p; ++p) {
    char c = *p;
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c=='-' || c=='_' || c=='.' || c=='~') {
      out += c;
    } else {
      uint8_t b = static_cast<uint8_t>(c);
      out += '%'; out += hex((b >> 4) & 0x0F); out += hex(b & 0x0F);
    }
  }
  return out;
}

// Minimal URL decoder for application/x-www-form-urlencoded values
static String urlDecode(const String& s) {
  String out; out.reserve(s.length());
  for (size_t i = 0; i < s.length(); ++i) {
    char c = s[i];
    if (c == '+') { out += ' '; }
    else if (c == '%' && i + 2 < s.length()) {
      auto hexv = [](char ch)->int { if (ch>='0'&&ch<='9') return ch-'0'; if (ch>='a'&&ch<='f') return 10+ch-'a'; if (ch>='A'&&ch<='F') return 10+ch-'A'; return -1; };
      int hi = hexv(s[i+1]); int lo = hexv(s[i+2]);
      if (hi >= 0 && lo >= 0) { out += char((hi<<4)|lo); i += 2; }
      else { out += c; }  
    } else { out += c; }
  }
  return out;
}

// Extract a form field from x-www-form-urlencoded body
static String extractFormField(const String& body, const String& key) {
  String k = key + "=";
  int pos = 0;
  while (pos <= (int)body.length()) {
    int amp = body.indexOf('&', pos);
    int end = (amp < 0) ? body.length() : amp;
    String pair = body.substring(pos, end);
    int eq = pair.indexOf('=');
    if (eq > 0) {
      String pk = pair.substring(0, eq);
      if (pk == key) {
        return pair.substring(eq + 1);
      }
    }
    if (amp < 0) break;
    pos = amp + 1;
  }
  return String("");
}

// Redirect helper for UI pages: send user to /login (no next param)
void redirectToLogin(httpd_req_t* req) {
  const char* uri = req->uri ? req->uri : "/";
  const char* loc = "/login";
  Serial.println(String("[auth] redirectToLogin: uri=") + uri + ", loc=" + loc);
  httpd_resp_set_status(req, "302 Found");
  httpd_resp_set_hdr(req, "Location", loc);
  httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate");
  httpd_resp_set_hdr(req, "Pragma", "no-cache");
  httpd_resp_set_type(req, "text/plain");
  httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
}


// ==========================
// Session-based auth (cookies)
// ==========================
// Pending user name used only during login redirect flow
static String gSessUser; // pending only
static String gSessToken; // unused in multi-session, kept for backward compatibility in flow
static unsigned long gSessExpiry = 0; // unused in multi-session
static const unsigned long SESSION_TTL_MS = 24UL*60UL*60UL*1000UL; // 24h

// Multi-session support
struct SessionEntry {
  String sid;        // session id (cookie value)
  String user;       // username
  unsigned long createdAt = 0;
  unsigned long lastSeen = 0;
  unsigned long expiresAt = 0;
  String ip;
  String notice;     // pending targeted notice for this session (emptied on read)
  uint32_t lastSensorSeqSent = 0; // last sensor-status sequence sent over SSE
};

static const int MAX_SESSIONS = 12;
static SessionEntry gSessions[MAX_SESSIONS];

static int findSessionIndexBySID(const String& sid) {
  if (sid.length() == 0) return -1;
  for (int i = 0; i < MAX_SESSIONS; ++i) {
    if (gSessions[i].sid == sid) return i;
  }
  return -1;
}

static int findFreeSessionIndex() {
  for (int i = 0; i < MAX_SESSIONS; ++i) {
    if (gSessions[i].sid.length() == 0) return i;
  }
  // No free slot: evict the oldest or expired
  int oldest = -1; unsigned long tOld = 0xFFFFFFFFUL;
  for (int i = 0; i < MAX_SESSIONS; ++i) {
    if (gSessions[i].expiresAt == 0 || (long)(gSessions[i].expiresAt - tOld) < 0) {
      oldest = i; tOld = gSessions[i].expiresAt;
    }
  }
  return oldest;
}

static void pruneExpiredSessions() {
  unsigned long now = millis();
  for (int i = 0; i < MAX_SESSIONS; ++i) {
    if (gSessions[i].sid.length() && gSessions[i].expiresAt > 0 && (long)(now - gSessions[i].expiresAt) >= 0) {
      gSessions[i] = SessionEntry();
    }
  }
}

static String getCookieSID(httpd_req_t* req) {
  String sessionToken = "";
  char cookie_buf[256];
  size_t cookie_len = sizeof(cookie_buf);
  const char* cookieNamesArr[] = {"session", "JSESSIONID", "sid"};
  for (int i = 0; i < 3; i++) {
    cookie_len = sizeof(cookie_buf);
    if (httpd_req_get_cookie_val(req, cookieNamesArr[i], cookie_buf, &cookie_len) == ESP_OK) {
      sessionToken = String(cookie_buf);
      break;
    }
  }
  if (sessionToken.length() == 0) {
    size_t hdr_len = httpd_req_get_hdr_value_len(req, "Cookie");
    if (hdr_len > 0) {
      char* cookie_hdr = (char*)malloc(hdr_len + 1);
      if (httpd_req_get_hdr_value_str(req, "Cookie", cookie_hdr, hdr_len + 1) == ESP_OK) {
        String cookies = String(cookie_hdr);
        String names[] = {"session=", "JSESSIONID=", "sid="};
        for (int i = 0; i < 3; i++) {
          int pos = cookies.indexOf(names[i]);
          if (pos >= 0) {
            pos += names[i].length();
            int end = cookies.indexOf(";", pos);
            if (end < 0) end = cookies.length();
            sessionToken = cookies.substring(pos, end);
            sessionToken.trim();
            break;
          }
        }
      }
      free(cookie_hdr);
    }
  }
  return sessionToken;
}

static bool getHeaderValue(httpd_req_t* req, const char* name, String& out) {
  size_t len = httpd_req_get_hdr_value_len(req, name);
  if (!len) {
    Serial.println(String("[auth] header missing: ") + name);
    return false;
  }
  std::unique_ptr<char[]> buf(new char[len+1]);
  if (httpd_req_get_hdr_value_str(req, name, buf.get(), len+1) != ESP_OK) return false;
  out = String(buf.get());
  Serial.println(String("[auth] got header ") + name + ": " + out);
  return true;
}

static bool getCookieValue(httpd_req_t* req, const char* key, String& out) {
  char buf[256];
  size_t len = sizeof(buf);
  if (httpd_req_get_cookie_val(req, key, buf, &len) == ESP_OK) {
    out = String(buf);
    Serial.println(String("[auth] cookie ") + key + "=\"" + out + "\"");
    return true;
  }
  // Do not fall back to manual parsing to avoid misreads; simply report absence.
  Serial.println(String("[auth] cookie key not found: ") + key);
  return false;
}

static String makeSessToken() {
  // Hex-only token: 96 bits random + 32 bits time (approx)
  uint32_t r1 = esp_random();
  uint32_t r2 = esp_random();
  uint32_t r3 = esp_random();
  uint32_t t  = millis();
  char buf[ (8*4) + 1 ]; // 8 hex chars * 4 values + NUL
  snprintf(buf, sizeof(buf), "%08x%08x%08x%08x", r1, r2, r3, t);
  return String(buf);
}

static void setSession(httpd_req_t* req, const String& u) {
  pruneExpiredSessions();
  int idx = findFreeSessionIndex();
  if (idx < 0) idx = 0; // fallback
  SessionEntry s;
  s.sid = makeSessToken();
  s.user = u;
  s.createdAt = millis();
  s.lastSeen  = s.createdAt;
  s.expiresAt = s.createdAt + SESSION_TTL_MS;
  String ip; getClientIP(req, ip); s.ip = ip;
  gSessions[idx] = s;

  // Clear old cookies, then set new cookies with SID
  String clear1 = String("session=; Path=/; Max-Age=0");
  String clear2 = String("JSESSIONID=; Path=/; Max-Age=0");
  String clear3 = String("sid=; Path=/; Max-Age=0");
  httpd_resp_set_hdr(req, "Set-Cookie", clear1.c_str());
  httpd_resp_set_hdr(req, "Set-Cookie", clear2.c_str());
  httpd_resp_set_hdr(req, "Set-Cookie", clear3.c_str());

  String sc1 = String("session=") + s.sid + "; Path=/";
  String sc2 = String("JSESSIONID=") + s.sid + "; Path=/";
  String sc3 = String("sid=") + s.sid + "; Path=/";
  httpd_resp_set_hdr(req, "Set-Cookie", sc1.c_str());
  httpd_resp_set_hdr(req, "Set-Cookie", sc2.c_str());
  httpd_resp_set_hdr(req, "Set-Cookie", sc3.c_str());

  Serial.println(String("[auth] setSession user=") + u + ", sid=" + s.sid + ", exp(ms)=" + String(s.expiresAt));
}

static void clearSession(httpd_req_t* req) {
  // Revoke current session by cookie value
  String sid = getCookieSID(req);
  int idx = findSessionIndexBySID(sid);
  if (idx >= 0) { gSessions[idx] = SessionEntry(); }
  // Clear all known cookies client-side
  httpd_resp_set_hdr(req, "Set-Cookie", "session=; Path=/; Max-Age=0");
  httpd_resp_set_hdr(req, "Set-Cookie", "JSESSIONID=; Path=/; Max-Age=0");
  httpd_resp_set_hdr(req, "Set-Cookie", "sid=; Path=/; Max-Age=0");
  Serial.println("[auth] clearSession (revoked current if present)");
}

static bool isAuthed(httpd_req_t* req, String& outUser) {
  const char* uri = req && req->uri ? req->uri : "(null)";
  pruneExpiredSessions();
  String sid = getCookieSID(req);
  if (sid.length() == 0) { Serial.println(String("[auth] no session cookie for uri=") + uri); return false; }
  int idx = findSessionIndexBySID(sid);
  if (idx < 0) { Serial.println(String("[auth] unknown SID for uri=") + uri); return false; }
  unsigned long now = millis();
  if (gSessions[idx].expiresAt > 0 && (long)(now - gSessions[idx].expiresAt) >= 0) {
    // expired
    gSessions[idx] = SessionEntry();
    Serial.println(String("[auth] expired SID for uri=") + uri);
    return false;
  }
  // refresh
  gSessions[idx].lastSeen = now;
  gSessions[idx].expiresAt = now + SESSION_TTL_MS;
  outUser = gSessions[idx].user;
  Serial.println(String("[auth] authenticated user: ") + outUser + ", sid=" + sid);
  return outUser.length() > 0;
}

// SSE endpoint: push per-session notices without polling
esp_err_t handleEvents(httpd_req_t* req) {
  String u; String ip; getClientIP(req, ip);
  if (!isAuthed(req, u)) { return sendAuthRequiredResponse(req); }

  // Prepare SSE headers
  httpd_resp_set_type(req, "text/event-stream");
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
  httpd_resp_set_hdr(req, "Connection", "keep-alive");
  // Disable proxy buffering if any
  httpd_resp_set_hdr(req, "X-Accel-Buffering", "no");

  // Send initial comment to open stream
  if (httpd_resp_send_chunk(req, ":ok\n\n", HTTPD_RESP_USE_STRLEN) != ESP_OK) {
    return ESP_OK;
  }

  // Event loop (up to ~5 minutes)
  unsigned long startMs = millis();
  unsigned long lastHb = millis();
  while ((long)(millis() - startMs) < 5UL * 60UL * 1000UL) {
    pruneExpiredSessions();
    String sid = getCookieSID(req);
    int idx = findSessionIndexBySID(sid);
    if (idx < 0) break;

    // Heartbeat every ~20s
    if ((long)(millis() - lastHb) >= 20000) {
      if (httpd_resp_send_chunk(req, ":hb\n\n", HTTPD_RESP_USE_STRLEN) != ESP_OK) break;
      lastHb = millis();
    }

    // Check notice
    if (gSessions[idx].notice.length()) {
      String note = gSessions[idx].notice;
      gSessions[idx].notice = ""; // clear before send

      // Build JSON payload
      String payload = String("{\"msg\":\"") + jsonEscape(note) + "\"";
      if (note.startsWith("[revoke]")) { payload += ",\"type\":\"revoke\""; }
      payload += "}";

      String ev = String("event: notice\n") + "data: " + payload + "\n\n";
      if (httpd_resp_send_chunk(req, ev.c_str(), HTTPD_RESP_USE_STRLEN) != ESP_OK) break;

      // If revoke, also clear the session now
      if (note.startsWith("[revoke]")) {
        gSessions[idx] = SessionEntry();
        // Cannot set cookies over SSE; client should redirect or next request will be unauth'd
      }
    }

    // Small sleep to yield CPU
    delay(500);
  }

  // Terminate stream
  httpd_resp_send_chunk(req, NULL, 0);
  return ESP_OK;
}

String formatDateTime(time_t timestamp) {
  struct tm* timeinfo = localtime(&timestamp);
  char buffer[32];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);
  return String(buffer);
}

void logAuthAttempt(bool success, const char* path, const String& userTried, const String& ip, const String& reason) {
  time_t t = nowEpoch();
  String datetime = formatDateTime(t);
  String status = success ? "SUCCESS" : "FAILED";
  
  // Clean up the path for better readability
  String cleanPath = String(path);
  cleanPath.replace("%2F", "/");
  cleanPath.replace("%20", " ");
  
  // Clean up IP address
  String cleanIP = ip;
  cleanIP.replace("::FFFF:", "");
  
  // Format: [2024-08-22 20:45:30] SUCCESS | user=pop | ip=192.168.0.103 | /dashboard
  String line = "[" + datetime + "] " + status + " | user=" + userTried + " | ip=" + cleanIP + " | " + cleanPath;
  if (!success && reason.length()) line += " | reason=" + reason;
  
  const char* logFile = success ? LOG_OK_FILE : LOG_FAIL_FILE;
  appendLineWithCap(logFile, line, LOG_CAP_BYTES);
}



// ==========================
// Output routing (centralized logger)
// ==========================

#define OUTPUT_SERIAL 0x01
#define OUTPUT_TFT    0x02
#define OUTPUT_WEB    0x04

volatile uint8_t gOutputFlags = OUTPUT_SERIAL; // default behavior: serial only
static String gLastTFTLine;
static String gWebMirror; // unified feed buffer (web + serial/tft mirrors)
static size_t gWebMirrorCap = 8192; // adjustable capacity

static inline void printToSerial(const String& s) { Serial.println(s); }
static inline void printToTFT(const String& s) { gLastTFTLine = s; /* TODO: render on TFT when integrated */ }
static inline void printToWeb(const String& s) {
  // Append with newline, cap at configured size
  if (gWebMirror.length()) gWebMirror += "\n";
  gWebMirror += s;
  if (gWebMirror.length() > gWebMirrorCap) {
    gWebMirror = gWebMirror.substring(gWebMirror.length()-gWebMirrorCap);
  }
}

// Append a command line with a source tag into the unified feed
static inline void appendCommandToFeed(const char* source, const String& cmd, const String& user = String(), const String& ip = String()) {
  if (!(gOutputFlags & OUTPUT_WEB)) return;
  String line = "[";
  line += source;
  if (user.length() || ip.length()) {
    line += " ";
    if (user.length()) { line += user; }
    if (ip.length())   { line += "@"; line += ip; }
  }
  line += "] $ ";
  line += cmd;
  printToWeb(line);
}

void broadcastOutput(const String& s) {
  if (gOutputFlags & OUTPUT_SERIAL) printToSerial(s);
  if (gOutputFlags & OUTPUT_TFT)    printToTFT(s);
  if (gOutputFlags & OUTPUT_WEB)    printToWeb(s);
}

// Build a standardized origin prefix like: [web user@ip] 
static String originPrefix(const char* source, const String& user = String(), const String& ip = String()) {
  String p = "[";
  p += source;
  if (user.length() || ip.length()) {
    p += " ";
    if (user.length()) { p += user; }
    if (ip.length())   { p += "@"; p += ip; }
  }
  p += "] ";
  return p;
}

static inline void broadcastWithOrigin(const char* source, const String& user, const String& ip, const String& msg) {
  broadcastOutput(originPrefix(source, user, ip) + msg);
}

// Now that OUTPUT_* and gOutputFlags are defined, implement applySettings
static void applySettings() {
  // Apply persisted output lanes
  uint8_t flags = 0;
  if (gSettings.outSerial) flags |= OUTPUT_SERIAL;
  if (gSettings.outTft)    flags |= OUTPUT_TFT;
  if (gSettings.outWeb)    flags |= OUTPUT_WEB;
  gOutputFlags = flags; // replace current routing with persisted lanes
  // CLI history size would be applied where buffer is managed (placeholder here)
}

// ==========================
// URL query helpers
// ==========================

static bool getQueryParam(httpd_req_t* req, const char* key, String& out) {
  out = "";
  size_t qlen = httpd_req_get_url_query_len(req);
  if (qlen == 0) return false;
  std::unique_ptr<char[]> qbuf(new char[qlen + 1]);
  if (httpd_req_get_url_query_str(req, qbuf.get(), qlen + 1) != ESP_OK) return false;
  char val[256];
  if (httpd_query_key_value(qbuf.get(), key, val, sizeof(val)) == ESP_OK) {
    out = String(val);
    return true;
  }
  return false;
}

// Minimal JSON escape for string values
static String jsonEscape(const String& in) {
  String out; out.reserve(in.length()+8);
  for (size_t i = 0; i < in.length(); ++i) {
    char c = in.charAt(i);
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default: out += c; break;
    }
  }
  return out;
}

// Notice endpoint: returns and clears per-session notice
esp_err_t handleNotice(httpd_req_t* req) {
  String u; String ip; getClientIP(req, ip);
  if (!isAuthed(req, u)) { return sendAuthRequiredResponse(req); }
  
  String sid = getCookieSID(req);
  int idx = findSessionIndexBySID(sid);
  String note = "";
  if (idx >= 0) {
    note = gSessions[idx].notice;
    gSessions[idx].notice = ""; // clear on read
    // If this is a revoke notice, immediately clear the session and expire cookie
    if (note.startsWith("[revoke]")) {
      gSessions[idx] = SessionEntry();
      httpd_resp_set_hdr(req, "Set-Cookie", "session=; Path=/; Max-Age=0");
    }
  }
  String json = String("{\"success\":true,\"notice\":\"") + jsonEscape(note) + "\"}";
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, json.c_str(), HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}
// ==========================
// HTTP handlers
// ==========================

esp_err_t handleRoot(httpd_req_t* req) {
  // Redirect to dashboard to unify UI
  httpd_resp_set_status(req, "302 Found");
  httpd_resp_set_hdr(req, "Location", "/dashboard");
  httpd_resp_send(req, "", HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

esp_err_t handlePublic(httpd_req_t* req) {
  String page = getPublicPage();
  httpd_resp_set_type(req, "text/html");
  httpd_resp_send(req, page.c_str(), HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

// Auth-protected text log endpoint returning mirrored output
esp_err_t handleLogs(httpd_req_t* req) {
  String u; String ip; getClientIP(req, ip);
  if (!isAuthed(req, u)) { return sendAuthRequiredResponse(req); }
  httpd_resp_set_type(req, "text/plain");
  httpd_resp_send(req, gWebMirror.c_str(), HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

// Full-page Login: GET shows form, POST validates and sets cookie session
esp_err_t handleLogin(httpd_req_t* req) {
  if (req->method == HTTP_GET) {
    // If already authed, redirect directly to dashboard
    String u;
    if (isAuthed(req, u)) {
      httpd_resp_set_status(req, "303 See Other");
      httpd_resp_set_hdr(req, "Location", "/dashboard");
      httpd_resp_send(req, "", 0);
      return ESP_OK;
    }
    // Render login form (no next param)
    String page = getLoginPage();
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, page.c_str(), HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }
  // POST
  int total_len = req->content_len; if (total_len <= 0) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No body"); return ESP_FAIL; }
  std::unique_ptr<char[]> buf(new char[total_len+1]); int received=0; while (received<total_len){int r=httpd_req_recv(req, buf.get()+received, total_len-received); if(r<=0){httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Read err"); return ESP_FAIL;} received+=r;} buf[received]='\0'; String body(buf.get());
  String u = urlDecode(extractFormField(body, "username"));
  String p = urlDecode(extractFormField(body, "password"));
  Serial.println(String("[login] POST attempt: username='") + u + "', password_len=" + String(p.length()));
  
  bool validUser = isValidUser(u, p);
  Serial.println(String("[login] isValidUser result: ") + (validUser ? "true" : "false"));
  
  if (u.length()==0 || p.length()==0 || !validUser) {
    // Log failed authentication attempt
    String ip; getClientIP(req, ip);
    logAuthAttempt(false, req->uri, u, ip, "Invalid credentials");
    
    String page = getLoginPage(u, "Invalid username or password");
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, page.c_str(), HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }
  // Success -> clear old cookies first, then redirect to set new session
  Serial.println(String("[login] Login successful for user: ") + u);
  
  // Log successful authentication attempt
  String ip; getClientIP(req, ip);
  logAuthAttempt(true, req->uri, u, ip, "Login successful");
  
  // Step 1: Clear all existing cookies and redirect to session setter
  String clear1 = String("session=; Path=/; Max-Age=0");
  String clear2 = String("JSESSIONID=; Path=/; Max-Age=0"); 
  String clear3 = String("sid=; Path=/; Max-Age=0");
  
  httpd_resp_set_hdr(req, "Set-Cookie", clear1.c_str());
  httpd_resp_set_hdr(req, "Set-Cookie", clear2.c_str());
  httpd_resp_set_hdr(req, "Set-Cookie", clear3.c_str());
  
  // Store login info temporarily for session creation in the next step
  gSessUser = u;
  
  Serial.println(String("[login] Cleared cookies, redirecting to set session for: ") + u);
  
  httpd_resp_set_status(req, "302 Found");
  httpd_resp_set_hdr(req, "Location", "/login/setsession");
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
  httpd_resp_send(req, "", 0);
  return ESP_OK;
}

// Session setter: Step 2 of login process - set fresh cookies after clearing old ones
esp_err_t handleLoginSetSession(httpd_req_t* req) {
  // Check if we have a pending session to set
  if (gSessUser.length() == 0) {
    Serial.println("[login] No pending session, redirecting to login");
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/login");
    httpd_resp_send(req, "", 0);
    return ESP_OK;
  }
  
  // Create a new session entry and set cookies
  String user = gSessUser; // capture then clear
  setSession(req, user);
  gSessUser = "";
  Serial.println(String("[login] Session set for user: ") + user);
  
  // Now redirect to dashboard
  httpd_resp_set_status(req, "302 Found");
  httpd_resp_set_hdr(req, "Location", "/dashboard");
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
  httpd_resp_send(req, "", 0);
  return ESP_OK;
}

// Logout: clear session and go to login
esp_err_t handleLogout(httpd_req_t* req) {
  clearSession(req);
  httpd_resp_set_status(req, "302 Found");
  httpd_resp_set_hdr(req, "Location", "/login");
  httpd_resp_set_type(req, "text/plain");
  httpd_resp_send(req, "Logged out", HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

// Protected dashboard
void streamDashboardContent(httpd_req_t* req) {
  String u;
  isAuthed(req, u);
  String content = getDashboardPage(u);
  streamContentGeneric(req, content);
}

esp_err_t handleDashboard(httpd_req_t* req) {
  String u; String ip; getClientIP(req, ip);
  if (!isAuthed(req, u)) { return sendAuthRequiredResponse(req); }
  logAuthAttempt(true, req->uri, u, ip, "");
  
  streamPageWithContent(req, "dashboard", u, streamDashboardContent);
  return ESP_OK;
}

void streamSettingsContent(httpd_req_t* req) {
  String u;
  isAuthed(req, u);
  String content = getSettingsPage(u);
  streamContentGeneric(req, content);
}

esp_err_t handleSettingsPage(httpd_req_t* req) {
  String u; String ip; getClientIP(req, ip);
  if (!isAuthed(req, u)) { return sendAuthRequiredResponse(req); }
  logAuthAttempt(true, req->uri, u, ip, "");
  
  streamPageWithContent(req, "settings", u, streamSettingsContent);
  return ESP_OK;
}

esp_err_t handleSensorData(httpd_req_t* req) {
  // Skip authentication for sensor data API to reduce overhead
  String u = "thermal_user"; String ip; getClientIP(req, ip);
  
  // Get query parameter to determine which sensor data to return
  char query[256];
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
    char sensor[32];
    if (httpd_query_key_value(query, "sensor", sensor, sizeof(sensor)) == ESP_OK) {
      String sensorType = String(sensor);
      
      if (sensorType == "thermal") {
        if (gSensorCache.thermalDataValid) {
          // Simplified JSON - only essential data
          String json = "{\"v\":1,\"mn\":" + String(gSensorCache.thermalMinTemp, 1) +
                       ",\"mx\":" + String(gSensorCache.thermalMaxTemp, 1) +
                       ",\"f\":[";
          for (int i = 0; i < 768; i++) {
            json += String((int)gSensorCache.thermalFrame[i]); // Integer temps only
            if (i < 767) json += ",";
          }
          json += "]}";
          
          httpd_resp_set_type(req, "application/json");
          httpd_resp_send(req, json.c_str(), json.length());
          return ESP_OK;
        } else {
          httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Thermal data not available");
          return ESP_FAIL;
        }
      } else if (sensorType == "tof") {
        // Always return ToF data, even if cache is stale
        String json = getToFDataJSON();
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json.c_str(), json.length());
        return ESP_OK;
      }
    }
  }
  
  // Default response for invalid/missing sensor parameter
  String json = "{\"valid\":false,\"error\":\"Invalid sensor parameter\"}";
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, json.c_str(), json.length());
  return ESP_OK;
}

// Simple unauthenticated health check
esp_err_t handlePing(httpd_req_t* req) {
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

// Settings API (GET): return current settings as JSON
esp_err_t handleSettingsGet(httpd_req_t* req) {
  String u; String ip; getClientIP(req, ip);
  if (!isAuthed(req, u)) { return sendAuthRequiredResponse(req); }
  
  String json = "{\"success\":true,\"settings\":{";
  // Primary SSID is first in saved list if present, otherwise from gSettings
  loadWiFiNetworks();
  String primarySSID = (gWifiNetworkCount > 0) ? gWifiNetworks[0].ssid : gSettings.wifiSSID;
  json += "\"wifiSSID\":\"" + gSettings.wifiSSID + "\",";
  json += "\"wifiPrimarySSID\":\"" + primarySSID + "\",";
  json += "\"wifiAutoReconnect\":" + String(gSettings.wifiAutoReconnect ? "true" : "false") + ",";
  json += "\"cliHistorySize\":" + String(gSettings.cliHistorySize) + ",";
  json += "\"outSerial\":" + String(gSettings.outSerial ? "true" : "false") + ",";
  json += "\"outWeb\":" + String(gSettings.outWeb ? "true" : "false") + ",";
  json += "\"outTft\":" + String(gSettings.outTft ? "true" : "false") + ",";
  // Sensors UI
  json += "\"thermalPollingMs\":" + String(gSettings.thermalPollingMs) + ",";
  json += "\"tofPollingMs\":" + String(gSettings.tofPollingMs) + ",";
  json += "\"tofStabilityThreshold\":" + String(gSettings.tofStabilityThreshold) + ",";
  json += "\"thermalPaletteDefault\":\"" + gSettings.thermalPaletteDefault + "\",";
  // Advanced
  json += "\"thermalEWMAFactor\":" + String(gSettings.thermalEWMAFactor,3) + ",";
  json += "\"thermalTransitionMs\":" + String(gSettings.thermalTransitionMs) + ",";
  json += "\"tofTransitionMs\":" + String(gSettings.tofTransitionMs) + ",";
  json += "\"tofUiMaxDistanceMm\":" + String(gSettings.tofUiMaxDistanceMm) + ",";
  json += "\"i2cClockThermalHz\":" + String(gSettings.i2cClockThermalHz) + ",";
  json += "\"i2cClockToFHz\":" + String(gSettings.i2cClockToFHz) + ",";
  json += "\"thermalTargetFps\":" + String(gSettings.thermalTargetFps) + ",";
  // Include saved wifiNetworks (no passwords)
  json += "\"wifiNetworks\":[";
  for (int i = 0; i < gWifiNetworkCount; ++i) {
    if (i) json += ",";
    json += String("{\"ssid\":\"") + gWifiNetworks[i].ssid + "\"," +
            "\"priority\":" + String(gWifiNetworks[i].priority) + "," +
            "\"hidden\":" + String(gWifiNetworks[i].hidden ? "true" : "false") + "}";
  }
  json += "]},";
  // Attach user and feature capabilities for client-side gating
  json += "\"user\":{\"username\":\"" + u + "\",\"isAdmin\":" + String(isAdminUser(u) ? "true" : "false") + "},";
  // Feature flags (toggleable centrally)
  json += "\"features\":{\"adminSessions\":" + String(isAdminUser(u) ? "true" : "false") + 
          ",\"userApprovals\":true,\"adminControls\":true,\"sensorConfig\":true}}";
  
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, json.c_str(), HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

// ==========================
// Sessions API (list + revoke)
// ==========================
static void buildUserSessionsJson(const String& user, const String& currentSid, String& outJsonArr) {
  bool first = true;
  unsigned long now = millis();
  for (int i = 0; i < MAX_SESSIONS; ++i) {
    const SessionEntry& s = gSessions[i];
    if (!s.sid.length()) continue;
    if (s.user != user) continue;
    if (!first) outJsonArr += ","; first = false;
    outJsonArr += String("{")
      + "\"sid\":\"" + s.sid + "\"," 
      + "\"createdAt\":" + String(s.createdAt) + ","
      + "\"lastSeen\":" + String(s.lastSeen) + ","
      + "\"expiresAt\":" + String(s.expiresAt) + ","
      + "\"ip\":\"" + (s.ip.length()?s.ip:"-") + "\"," 
      + "\"current\":" + (s.sid == currentSid ? "true" : "false")
      + "}";
  }
}

esp_err_t handleSessionsList(httpd_req_t* req) {
  // Admin-only: list all active sessions
  String u; if (!requireAdmin(req, u)) { return ESP_OK; }
  String currentSid = getCookieSID(req);
  String arr;
  buildAllSessionsJson(currentSid, arr);
  String json = String("{\"success\":true,\"sessions\":[") + arr + "]}";
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, json.c_str(), HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

esp_err_t handleSessionsRevoke(httpd_req_t* req) {
  // Admin-only: revoke any session by SID
  String u; if (!requireAdmin(req, u)) { return ESP_OK; }

  // Read body (application/x-www-form-urlencoded): sid=...
  char buf[256];
  int ret = httpd_req_recv(req, buf, sizeof(buf)-1);
  if (ret <= 0) { httpd_resp_set_type(req, "application/json"); httpd_resp_send(req, "{\"success\":false,\"error\":\"No data\"}", HTTPD_RESP_USE_STRLEN); return ESP_OK; }
  buf[ret] = '\0'; String body(buf);
  String sid = ""; int p = body.indexOf("sid="); if (p >= 0) { p += 4; int e = body.indexOf('&', p); if (e < 0) e = body.length(); sid = body.substring(p, e); }
  sid.replace("%3D", "="); sid.replace("%2F", "/"); sid.replace("%25", "%");

  if (!sid.length()) { httpd_resp_set_type(req, "application/json"); httpd_resp_send(req, "{\"success\":false,\"error\":\"sid required\"}", HTTPD_RESP_USE_STRLEN); return ESP_OK; }
  int idx = findSessionIndexBySID(sid);
  if (idx < 0) { httpd_resp_set_type(req, "application/json"); httpd_resp_send(req, "{\"success\":false,\"error\":\"not found\"}", HTTPD_RESP_USE_STRLEN); return ESP_OK; }

  // Set a targeted revoke notice; session will be cleared on next /api/notice poll
  gSessions[idx].notice = "[revoke] Your session has been signed out by an administrator.";
  httpd_resp_set_type(req, "application/json"); httpd_resp_send(req, "{\"success\":true}", HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

// Helper to require admin; sends JSON error if not authed/admin
static bool requireAdmin(httpd_req_t* req, String& uOut) {
  String u; String ip; getClientIP(req, ip);
  if (!isAuthed(req, u)) { sendAuthRequiredResponse(req); return false; }
  logAuthAttempt(true, req->uri, u, ip, "");
  if (!isAdminUser(u)) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"success\":false,\"error\":\"Admin access required\"}", HTTPD_RESP_USE_STRLEN);
    return false;
  }
  uOut = u;
  return true;
}

// Build JSON for all sessions (admin view)
static void buildAllSessionsJson(const String& currentSid, String& outJsonArr) {
  bool first = true;
  for (int i = 0; i < MAX_SESSIONS; ++i) {
    const SessionEntry& s = gSessions[i];
    if (!s.sid.length()) continue;
    if (!first) outJsonArr += ","; first = false;
    outJsonArr += String("{")
      + "\"sid\":\"" + s.sid + "\"," 
      + "\"user\":\"" + s.user + "\"," 
      + "\"createdAt\":" + String(s.createdAt) + ","
      + "\"lastSeen\":" + String(s.lastSeen) + ","
      + "\"expiresAt\":" + String(s.expiresAt) + ","
      + "\"ip\":\"" + (s.ip.length()?s.ip:"-") + "\"," 
      + "\"current\":" + (s.sid == currentSid ? "true" : "false")
      + "}";
  }
}

// Admin: list all sessions
esp_err_t handleAdminSessionsList(httpd_req_t* req) {
  String u; if (!requireAdmin(req, u)) { return ESP_OK; }
  String currentSid = getCookieSID(req);
  String arr; buildAllSessionsJson(currentSid, arr);
  String json = String("{\"success\":true,\"sessions\":[") + arr + "]}";
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, json.c_str(), HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

// Admin: revoke any session by SID
esp_err_t handleAdminSessionsRevoke(httpd_req_t* req) {
  String u; if (!requireAdmin(req, u)) { return ESP_OK; }
  // Read body (application/x-www-form-urlencoded): sid=...
  char buf[256];
  int ret = httpd_req_recv(req, buf, sizeof(buf)-1);
  if (ret <= 0) { httpd_resp_set_type(req, "application/json"); httpd_resp_send(req, "{\"success\":false,\"error\":\"No data\"}", HTTPD_RESP_USE_STRLEN); return ESP_OK; }
  buf[ret] = '\0'; String body(buf);
  String sid = ""; int p = body.indexOf("sid="); if (p >= 0) { p += 4; int e = body.indexOf('&', p); if (e < 0) e = body.length(); sid = body.substring(p, e); }
  sid.replace("%3D", "="); sid.replace("%2F", "/"); sid.replace("%25", "%");
  if (!sid.length()) { httpd_resp_set_type(req, "application/json"); httpd_resp_send(req, "{\"success\":false,\"error\":\"sid required\"}", HTTPD_RESP_USE_STRLEN); return ESP_OK; }
  int idx = findSessionIndexBySID(sid);
  if (idx < 0) { httpd_resp_set_type(req, "application/json"); httpd_resp_send(req, "{\"success\":false,\"error\":\"not found\"}", HTTPD_RESP_USE_STRLEN); return ESP_OK; }
  // Preserve target username (if any) for logging
  String targetUser = gSessions[idx].user;
  // Set revoke notice for the specific session; it will be cleared on notice delivery
  gSessions[idx].notice = "[revoke] Your session has been signed out by an administrator.";
  // Optional: broadcast an admin feed message
  broadcastWithOrigin("admin", u, String(), String("Admin notice: a session was revoked") + (targetUser.length()? String(" for user '")+targetUser+"'" : String("")) + ".");
  httpd_resp_set_type(req, "application/json"); httpd_resp_send(req, "{\"success\":true}", HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}
// Settings API (POST): update setting value
esp_err_t handleSettingsUpdate(httpd_req_t* req) {
  String u; String ip; getClientIP(req, ip);
  if (!isAuthed(req, u)) { return sendAuthRequiredResponse(req); }
  
  // Read POST body
  String body;
  int total_len = req->content_len;
  if (total_len > 0) {
    std::unique_ptr<char[]> buf(new char[total_len+1]);
    int received = 0;
    while (received < total_len) {
      int r = httpd_req_recv(req, buf.get()+received, total_len-received);
      if (r <= 0) break; received += r;
    }
    buf[received] = '\0'; body = String(buf.get());
  }
  
  // Parse form data: setting=name&value=val
  String settingName = urlDecode(extractFormField(body, "setting"));
  String settingValue = urlDecode(extractFormField(body, "value"));
  
  if (settingName.length() == 0) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"success\":false,\"error\":\"Missing setting name\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }
  
  // Update setting in unified JSON settings
  bool updated = false;
  if (settingName == "wifiAutoReconnect") {
    gSettings.wifiAutoReconnect = (settingValue.toInt() != 0);
    updated = true;
  } else if (settingName == "cliHistorySize") {
    int val = settingValue.toInt();
    if (val < 1) val = 1;
    if (val > 100) val = 100;
    gSettings.cliHistorySize = val;
    updated = true;
  } else if (settingName == "wifiSSID") {
    gSettings.wifiSSID = settingValue;
    updated = true;
  } else if (settingName == "wifiPassword") {
    gSettings.wifiPassword = settingValue;
    updated = true;
  } else if (settingName == "outSerial") {
    gSettings.outSerial = (settingValue.toInt() != 0);
    updated = true;
  } else if (settingName == "outWeb") {
    gSettings.outWeb = (settingValue.toInt() != 0);
    updated = true;
  } else if (settingName == "outTft") {
    gSettings.outTft = (settingValue.toInt() != 0);
    updated = true;
  } else if (settingName == "thermalPollingMs") {
    int v = settingValue.toInt(); if (v < 50) v = 50; if (v > 5000) v = 5000; gSettings.thermalPollingMs = v; updated = true;
  } else if (settingName == "tofPollingMs") {
    int v = settingValue.toInt(); if (v < 50) v = 50; if (v > 5000) v = 5000; gSettings.tofPollingMs = v; updated = true;
  } else if (settingName == "tofStabilityThreshold") {
    int v = settingValue.toInt(); if (v < 1) v = 1; if (v > 20) v = 20; gSettings.tofStabilityThreshold = v; updated = true;
  } else if (settingName == "thermalPaletteDefault") {
    // Accept a small set of known palettes
    String v = settingValue;
    if (v != "turbo" && v != "ironbow" && v != "grayscale" && v != "rainbow") {
      v = "turbo";
    }
    gSettings.thermalPaletteDefault = v; updated = true;
  } else if (settingName == "thermalEWMAFactor") {
    float v = settingValue.toFloat(); if (v < 0.0f) v = 0.0f; if (v > 1.0f) v = 1.0f; gSettings.thermalEWMAFactor = v; updated = true;
  } else if (settingName == "thermalTransitionMs") {
    int v = settingValue.toInt(); if (v < 0) v = 0; if (v > 1000) v = 1000; gSettings.thermalTransitionMs = v; updated = true;
  } else if (settingName == "tofTransitionMs") {
    int v = settingValue.toInt(); if (v < 0) v = 0; if (v > 1000) v = 1000; gSettings.tofTransitionMs = v; updated = true;
  } else if (settingName == "tofUiMaxDistanceMm") {
    int v = settingValue.toInt(); if (v < 100) v = 100; if (v > 12000) v = 12000; gSettings.tofUiMaxDistanceMm = v; updated = true;
  } else if (settingName == "i2cClockThermalHz") {
    int v = settingValue.toInt(); if (v < 400000) v = 400000; if (v > 1000000) v = 1000000; gSettings.i2cClockThermalHz = v; updated = true;
  } else if (settingName == "i2cClockToFHz") {
    int v = settingValue.toInt(); if (v < 50000) v = 50000; if (v > 400000) v = 400000; gSettings.i2cClockToFHz = v; updated = true;
  } else if (settingName == "thermalTargetFps") {
    int v = settingValue.toInt(); if (v < 1) v = 1; if (v > 8) v = 8; gSettings.thermalTargetFps = v; updated = true;
  }
  
  if (!updated) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"success\":false,\"error\":\"Unknown setting\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }
  
  // Save to JSON file
  if (!saveUnifiedSettings()) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"success\":false,\"error\":\"Failed to save settings\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }
  
  Serial.println(String("[settings] Updated ") + settingName + " = " + settingValue);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, "{\"success\":true}", HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

// (Removed) HTTP WiFi disconnect endpoint; use CLI command 'wifidisconnect' instead.

// Registration page (guest)
esp_err_t handleRegisterPage(httpd_req_t* req) {
  String inner;
  inner += "<h2>Request Account</h2>";
  inner += "<form method='POST' action='/register/submit'>";
  inner += "  <label>Username<br><input name='username'></label><br><br>";
  inner += "  <label>Password<br><input type='password' name='password'></label><br><br>";
  inner += "  <label>Confirm Password<br><input type='password' name='confirm_password'></label><br><br>";
  inner += "  <button class='menu-item' type='submit'>Submit</button>";
  inner += "  <a class='menu-item' href='/login' style='margin-left:.5rem'>Back to Sign In</a>";
  inner += "</form>";
  String page = htmlShellWithNav("guest", "register", inner);
  httpd_resp_set_type(req, "text/html");
  httpd_resp_send(req, page.c_str(), HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

// Registration submit: save user request for admin approval
esp_err_t handleRegisterSubmit(httpd_req_t* req) {
  String body;
  int total_len = req->content_len;
  if (total_len > 0) {
    char* buf = (char*)malloc(total_len + 1);
    if (buf) {
      int received = 0;
      while (received < total_len) {
        int ret = httpd_req_recv(req, buf + received, total_len - received);
        if (ret <= 0) break;
        received += ret;
      }
      buf[received] = '\0';
      body = String(buf);
      free(buf);
    }
  }
  
  // Parse form data
  String username = "";
  String password = "";
  String confirmPassword = "";
  
  int usernameStart = body.indexOf("username=");
  if (usernameStart != -1) {
    usernameStart += 9; // length of "username="
    int usernameEnd = body.indexOf("&", usernameStart);
    if (usernameEnd == -1) usernameEnd = body.length();
    username = body.substring(usernameStart, usernameEnd);
    username.replace("+", " ");
    username.replace("%40", "@");
  }
  
  int passwordStart = body.indexOf("password=");
  if (passwordStart != -1) {
    passwordStart += 9; // length of "password="
    int passwordEnd = body.indexOf("&", passwordStart);
    if (passwordEnd == -1) passwordEnd = body.length();
    password = body.substring(passwordStart, passwordEnd);
  }
  
  int confirmStart = body.indexOf("confirm_password=");
  if (confirmStart != -1) {
    confirmStart += 17; // length of "confirm_password="
    int confirmEnd = body.indexOf("&", confirmStart);
    if (confirmEnd == -1) confirmEnd = body.length();
    confirmPassword = body.substring(confirmStart, confirmEnd);
  }
  
  if (username.length() == 0 || password.length() == 0 || confirmPassword.length() == 0) {
    String inner = "<div style='text-align:center;padding:2rem'>";
    inner += "<h2 style='color:#dc3545'>Registration Failed</h2>";
    inner += "<p>All fields are required.</p>";
    inner += "<p><a class='menu-item' href='/register'>Try Again</a></p>";
    inner += "</div>";
    String page = htmlShellWithNav("guest", "register", inner);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, page.c_str(), HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }
  
  // Check if passwords match
  if (password != confirmPassword) {
    String inner = "<div style='text-align:center;padding:2rem'>";
    inner += "<h2 style='color:#dc3545'>Registration Failed</h2>";
    inner += "<p>Passwords do not match. Please try again.</p>";
    inner += "<p><a class='menu-item' href='/register'>Try Again</a></p>";
    inner += "</div>";
    String page = htmlShellWithNav("guest", "register", inner);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, page.c_str(), HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }
  
  // Save pending user request (username:password for approval, but admin won't see password)
  String pendingFile = "/pending_users.txt";
  String entry = username + ":" + password + "\n";
  
  File file = LittleFS.open(pendingFile, "a");
  if (file) {
    file.print(entry);
    file.close();
    Serial.println(String("[register] New user request: ") + username);
  }
  
  // Show success page
  String inner = "<div style='text-align:center;padding:2rem'>";
  inner += "<h2 style='color:#28a745'>Request Submitted</h2>";
  inner += "<div style='background:#d4edda;border:1px solid #c3e6cb;border-radius:8px;padding:1.5rem;margin:1rem 0'>";
  inner += "<p style='color:#155724;margin-bottom:1rem'>Your account request has been submitted successfully!</p>";
  inner += "<p style='color:#155724;font-size:0.9rem'>An administrator will review your request and approve access to the system.</p>";
  inner += "</div>";
  inner += "<p><a class='menu-item' href='/login'>Return to Sign In</a></p>";
  inner += "</div>";
  String page = htmlShellWithNav("guest", "register", inner);
  httpd_resp_set_type(req, "text/html");
  httpd_resp_send(req, page.c_str(), HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}


esp_err_t handleCLICommand(httpd_req_t* req) {
  String u; String ip; getClientIP(req, ip);
  if (!isAuthed(req, u)) { return sendAuthRequiredResponse(req); }
  // Set execution context for admin gating during this request
  gExecFromWeb = true;
  gExecUser = u;
  gExecIsAdmin = isAdminUser(u);
  // Read x-www-form-urlencoded body
  std::unique_ptr<char[]> buf(new char[req->content_len + 1]);
  int received = 0;
  String body;
  if (req->content_len > 0) {
    while (received < (int)req->content_len) {
      int ret = httpd_req_recv(req, buf.get() + received, req->content_len - received);
      if (ret <= 0) break;
      received += ret;
    }
    buf[received] = '\0'; body = String(buf.get());
  }
  String cmd = urlDecode(extractFormField(body, "cmd"));
  
  // Record the command in the unified feed
  appendCommandToFeed("web", cmd, u, ip);
  
  // Process command and broadcast output through routing system
  String out = processCommand(cmd);
  broadcastWithOrigin("web", u, ip, out);
  
  // For web CLI, return the output directly for immediate display
  httpd_resp_set_type(req, "text/plain");
  httpd_resp_send(req, out.c_str(), HTTPD_RESP_USE_STRLEN);
  // Clear execution context after handling
  gExecFromWeb = false;
  gExecUser = "";
  gExecIsAdmin = false;
  return ESP_OK;
}

void streamCLIContent(httpd_req_t* req) {
  String u;
  isAuthed(req, u);
  String content = getCLIPage(u);
  streamContentGeneric(req, content);
}

esp_err_t handleCLIPage(httpd_req_t* req) {
  String u; String ip; getClientIP(req, ip);
  if (!isAuthed(req, u)) { return sendAuthRequiredResponse(req); }
  
  logAuthAttempt(true, req->uri, u, ip, "");
  
  streamPageWithContent(req, "cli", u, streamCLIContent);
  return ESP_OK;
}

void streamFilesContent(httpd_req_t* req) {
  String u;
  isAuthed(req, u);
  String content = getFilesPage(u);
  streamContentGeneric(req, content);
}

// Removed duplicate - using the existing streamFilesContent above

esp_err_t handleFilesPage(httpd_req_t* req) {
  String u; String ip; getClientIP(req, ip);
  if (!isAuthed(req, u)) { return sendAuthRequiredResponse(req); }
  logAuthAttempt(true, req->uri, u, ip, "");
  
  streamPageWithContent(req, "files", u, streamFilesContent);
  return ESP_OK;
}

// Shared helper: enumerate a directory and either produce JSON entries array body
// or a human-readable text listing. Returns true on success.
static bool buildFilesListing(const String& inPath, String& out, bool asJson) {
  String dirPath = inPath;
  if (dirPath.length() == 0) dirPath = "/";
  if (!dirPath.startsWith("/")) dirPath = String("/") + dirPath;

  File root = LittleFS.open(dirPath);
  if (!root || !root.isDirectory()) {
    if (asJson) {
      out = ""; // caller will wrap error
    } else {
      out = String("Error: Cannot open directory '") + dirPath + "'";
    }
    return false;
  }

  bool first = true;
  int fileCount = 0;
  if (!asJson) {
    out = String("LittleFS Files (") + dirPath + "):\n";
  } else {
    out = ""; // array body only
  }

  File file = root.openNextFile();
  while (file) {
    // Extract display name (strip leading directory)
    String fileName = String(file.name());
    if (dirPath != "/") {
      String expectedPrefix = dirPath; if (!expectedPrefix.endsWith("/")) expectedPrefix += "/";
      if (fileName.startsWith(expectedPrefix)) fileName = fileName.substring(expectedPrefix.length());
    } else {
      if (fileName.startsWith("/")) fileName = fileName.substring(1);
    }
    // Skip nested paths that still contain '/'
    if (fileName.length() == 0 || fileName.indexOf('/') != -1) {
      file = root.openNextFile();
      continue;
    }

    bool isDirEntry = file.isDirectory();
    if (asJson) {
      if (!first) out += ",";
      first = false;
      if (isDirEntry) {
        // Count children in subdirectory
        String subPath = dirPath; if (!subPath.endsWith("/")) subPath += "/"; subPath += fileName;
        int itemCount = 0; File subDir = LittleFS.open(subPath);
        if (subDir && subDir.isDirectory()) {
          File child = subDir.openNextFile();
          while (child) { itemCount++; child = subDir.openNextFile(); }
          subDir.close();
        }
        out += String("{\"name\":\"") + fileName + "\",";
        out += String("\"type\":\"folder\",");
        out += String("\"size\":\"") + String(itemCount) + " items\",";
        out += String("\"count\":") + String(itemCount) + "}";
      } else {
        out += String("{\"name\":\"") + fileName + "\",";
        out += String("\"type\":\"file\",");
        out += String("\"size\":\"") + String(file.size()) + " bytes\"}";
      }
    } else {
      // Human-readable text
      out += "  " + fileName + " (";
      if (isDirEntry) {
        // Count children for display
        String subPath = dirPath; if (!subPath.endsWith("/")) subPath += "/"; subPath += fileName;
        int itemCount = 0; File subDir = LittleFS.open(subPath);
        if (subDir && subDir.isDirectory()) {
          File child = subDir.openNextFile();
          while (child) { itemCount++; child = subDir.openNextFile(); }
          subDir.close();
        }
        out += String(itemCount) + " items)\n";
      } else {
        out += String(file.size()) + " bytes)\n";
      }
      fileCount++;
    }

    file = root.openNextFile();
  }
  root.close();

  if (!asJson) {
    if (fileCount == 0) {
      out += "  No files found\n";
    } else {
      out += String("\nTotal: ") + String(fileCount) + " entries";
    }
  }
  return true;
}

esp_err_t handleFilesList(httpd_req_t* req) {
  String u; String ip; getClientIP(req, ip);
  if (!isAuthed(req, u)) { return sendAuthRequiredResponse(req); }
  logAuthAttempt(true, req->uri, u, ip, "");
  
  // Check if filesystem is ready
  if (!filesystemReady) {
    Serial.println("[files] ERROR: Filesystem not ready");
    String json = "{\"success\":false,\"error\":\"Filesystem not initialized\"}";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json.c_str(), HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }
  
  // Get path parameter
  char pathParam[256];
  String dirPath = "/";
  if (httpd_req_get_url_query_str(req, pathParam, sizeof(pathParam)) == ESP_OK) {
    char pathValue[256];
    if (httpd_query_key_value(pathParam, "path", pathValue, sizeof(pathValue)) == ESP_OK) {
      dirPath = String(pathValue);
      // URL decode the path
      dirPath.replace("%2F", "/");
      dirPath.replace("%20", " ");
      Serial.println(String("[files] Listing directory: ") + dirPath);
    }
  }
  String body;
  bool ok = buildFilesListing(dirPath, body, /*asJson=*/true);
  String json;
  if (ok) {
    json = String("{\"success\":true,\"files\":[") + body + "]}";
  } else {
    json = "{\"success\":false,\"error\":\"Directory not found or not accessible\"}";
  }

  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, json.c_str(), HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

esp_err_t handleFilesCreate(httpd_req_t* req) {
  String u; String ip; getClientIP(req, ip);
  if (!isAuthed(req, u)) { return sendAuthRequiredResponse(req); }
  logAuthAttempt(true, req->uri, u, ip, "");
  
  char buf[256];
  int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
  if (ret <= 0) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"success\":false,\"error\":\"No data received\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }
  buf[ret] = '\0';
  
  String body = String(buf);
  String name = "";
  String type = "";
  
  // Parse form data
  int nameStart = body.indexOf("name=");
  int typeStart = body.indexOf("type=");
  
  if (nameStart >= 0) {
    nameStart += 5;
    int nameEnd = body.indexOf("&", nameStart);
    if (nameEnd < 0) nameEnd = body.length();
    name = body.substring(nameStart, nameEnd);
    name.replace("%20", " ");
    name.replace("%2F", "/");
  }
  
  if (typeStart >= 0) {
    typeStart += 5;
    int typeEnd = body.indexOf("&", typeStart);
    if (typeEnd < 0) typeEnd = body.length();
    type = body.substring(typeStart, typeEnd);
  }
  
  if (name.length() == 0) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"success\":false,\"error\":\"Name required\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }
  // Normalize: strip leading '/' if present to prevent double slashes
  if (name.startsWith("/")) {
    name = name.substring(1);
  }
  String path = "/" + name;
  
  if (type == "folder") {
    if (LittleFS.mkdir(path)) {
      httpd_resp_set_type(req, "application/json");
      httpd_resp_send(req, "{\"success\":true}", HTTPD_RESP_USE_STRLEN);
    } else {
      httpd_resp_set_type(req, "application/json");
      httpd_resp_send(req, "{\"success\":false,\"error\":\"Failed to create folder\"}", HTTPD_RESP_USE_STRLEN);
    }
  } else {
    // Add extension if not present
    if (!name.endsWith("." + type)) {
      path = "/" + name + "." + type;
    }
    
    File file = LittleFS.open(path, "w");
    if (file) {
      // Write basic content based on file type
      if (type == "txt") {
        file.println("# New Text File");
        file.println("Created on ESP32 device");
      } else if (type == "csv") {
        file.println("Column1,Column2,Column3");
        file.println("Value1,Value2,Value3");
      } else if (type == "rtf") {
        file.println("{\\rtf1\\ansi\\deff0 {\\fonttbl {\\f0 Times New Roman;}}");
        file.println("\\f0\\fs24 New RTF Document}");
      }
      file.close();
      
      httpd_resp_set_type(req, "application/json");
      httpd_resp_send(req, "{\"success\":true}", HTTPD_RESP_USE_STRLEN);
    } else {
      httpd_resp_set_type(req, "application/json");
      httpd_resp_send(req, "{\"success\":false,\"error\":\"Failed to create file\"}", HTTPD_RESP_USE_STRLEN);
    }
  }
  
  return ESP_OK;
}

esp_err_t handleFileView(httpd_req_t* req) {
  String u; String ip; getClientIP(req, ip);
  if (!isAuthed(req, u)) { return sendAuthRequiredResponse(req); }
  logAuthAttempt(true, req->uri, u, ip, "");
  
  char query[256];
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, "No filename specified", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }
  
  char name[128];
  if (httpd_query_key_value(query, "name", name, sizeof(name)) != ESP_OK) {
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, "Invalid filename", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }
  
  String path = String(name);
  // URL decode the path
  path.replace("%2F", "/");
  path.replace("%20", " ");
  
  Serial.println(String("[files] Viewing file: ") + path);
  
  File file = LittleFS.open(path, "r");
  if (!file) {
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, "File not found", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }
  
  // Set content type based on file extension
  String filename = String(name);
  if (filename.endsWith(".txt")) {
    httpd_resp_set_type(req, "text/plain");
  } else if (filename.endsWith(".csv")) {
    httpd_resp_set_type(req, "text/csv");
  } else if (filename.endsWith(".rtf")) {
    httpd_resp_set_type(req, "application/rtf");
  } else {
    httpd_resp_set_type(req, "text/plain");
  }
  
  // Stream file content
  char buffer[512];
  while (file.available()) {
    int bytesRead = file.readBytes(buffer, sizeof(buffer));
    httpd_resp_send_chunk(req, buffer, bytesRead);
  }
  file.close();
  
  httpd_resp_send_chunk(req, NULL, 0); // End chunked response
  return ESP_OK;
}

esp_err_t handleFileDelete(httpd_req_t* req) {
  String u; String ip; getClientIP(req, ip);
  if (!isAuthed(req, u)) { return sendAuthRequiredResponse(req); }
  logAuthAttempt(true, req->uri, u, ip, "");
  
  char query[256];
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"success\":false,\"error\":\"No filename specified\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }
  
  char name[128];
  if (httpd_query_key_value(query, "name", name, sizeof(name)) != ESP_OK) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"success\":false,\"error\":\"Invalid filename\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }
  
  // Normalize: strip leading '/' if present to prevent double slashes
  String nameStr = String(name);
  if (nameStr.startsWith("/")) {
    nameStr = nameStr.substring(1);
  }
  String path = "/" + nameStr;
  
  // Check if it's a directory
  File file = LittleFS.open(path, "r");
  if (!file) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"success\":false,\"error\":\"File not found\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }
  
  bool isDir = file.isDirectory();
  file.close();
  
  bool success = false;
  if (isDir) {
    success = LittleFS.rmdir(path);
  } else {
    success = LittleFS.remove(path);
  }
  
  if (success) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"success\":true}", HTTPD_RESP_USE_STRLEN);
  } else {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"success\":false,\"error\":\"Failed to delete\"}", HTTPD_RESP_USE_STRLEN);
  }
  
  return ESP_OK;
}

esp_err_t handleAdminPending(httpd_req_t* req) {
  String u; String ip; getClientIP(req, ip);
  if (!isAuthed(req, u)) { return sendAuthRequiredResponse(req); }
  logAuthAttempt(true, req->uri, u, ip, "");
  
  // Check if user is admin
  if (!isAdminUser(u)) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"success\":false,\"error\":\"Admin access required\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }
  
  String json = "{\"success\":true,\"users\":[";
  bool first = true;
  
  File pendingFile = LittleFS.open("/pending_users.txt", "r");
  if (pendingFile) {
    while (pendingFile.available()) {
      String line = pendingFile.readStringUntil('\n');
      line.trim();
      if (line.length() > 0) {
        int colonPos = line.indexOf(':');
        String username = (colonPos > 0) ? line.substring(0, colonPos) : line;
        
        if (!first) json += ",";
        first = false;
        json += "{\"username\":\"" + username + "\"}";
      }
    }
    pendingFile.close();
  }
  
  json += "]}";
  
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, json.c_str(), HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

esp_err_t handleAdminApproveUser(httpd_req_t* req) {
  String u; String ip; getClientIP(req, ip);
  if (!isAuthed(req, u)) { return sendAuthRequiredResponse(req); }
  logAuthAttempt(true, req->uri, u, ip, "");
  
  // Check if user is admin
  if (!isAdminUser(u)) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"success\":false,\"error\":\"Admin access required\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }
  
  // Read full body
  int total_len = req->content_len;
  if (total_len <= 0) { httpd_resp_set_type(req, "application/json"); httpd_resp_send(req, "{\"success\":false,\"error\":\"No data\"}", HTTPD_RESP_USE_STRLEN); return ESP_OK; }
  std::unique_ptr<char[]> buf(new char[total_len+1]); int received=0; while (received<total_len) { int r=httpd_req_recv(req, buf.get()+received, total_len-received); if (r<=0){ httpd_resp_set_type(req, "application/json"); httpd_resp_send(req, "{\"success\":false,\"error\":\"Read error\"}", HTTPD_RESP_USE_STRLEN); return ESP_OK; } received+=r; } buf[received]='\0';
  String body(buf.get());
  // Extract and decode
  String username = urlDecode(extractFormField(body, "username"));
  
  if (username.length() == 0) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"success\":false,\"error\":\"Username required\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }
  
  // Move user from pending to approved
  String pendingContent = "";
  String approvedLine = "";
  bool found = false;
  
  File pendingFile = LittleFS.open("/pending_users.txt", "r");
  if (pendingFile) {
    while (pendingFile.available()) {
      String line = pendingFile.readStringUntil('\n');
      line.trim();
      if (line.length() > 0) {
        int colonPos = line.indexOf(':');
        String lineUsername = (colonPos > 0) ? line.substring(0, colonPos) : line;
        if (lineUsername == username) {
          approvedLine = line;
          found = true;
        } else {
          pendingContent += line + "\n";
        }
      }
    }
    pendingFile.close();
  }
  
  if (!found) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"success\":false,\"error\":\"User not found in pending list\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }
  
  // Write back pending file without approved user
  File pendingWrite = LittleFS.open("/pending_users.txt", "w");
  if (pendingWrite) {
    pendingWrite.print(pendingContent);
    pendingWrite.close();
  }
  
  // Use the original password from the pending request
  int colonPos = approvedLine.indexOf(':');
  String userPassword = (colonPos > 0) ? approvedLine.substring(colonPos + 1) : "temp" + String(random(1000, 9999));
  String userEntry = username + ":" + userPassword;
  
  // Add to users storage (prefer JSON)
  if (LittleFS.exists(USERS_JSON_FILE)) {
    String json; readText(USERS_JSON_FILE, json);
    // naive append: insert before last ] in users array
    int arrStart = json.indexOf("\"users\"");
    int bracket = (arrStart >= 0) ? json.indexOf('[', arrStart) : -1;
    int lastBracket = (bracket >= 0) ? json.indexOf(']', bracket) : -1;
    if (lastBracket > 0) {
      String uname = username;
      String upass = userPassword;
      // Determine if array already has entries (look for '{' between bracket and lastBracket)
      bool hasAny = json.indexOf('{', bracket) > bracket;
      String insert = String(hasAny ? ",\n    {\n" : "\n    {\n");
      insert += "      \"username\": \"" + uname + "\",\n";
      insert += "      \"password\": \"" + upass + "\",\n";
      insert += "      \"role\": \"user\"\n";
      insert += "    }\n";
      json = json.substring(0, lastBracket) + insert + json.substring(lastBracket);
      writeText(USERS_JSON_FILE, json);
    }
  } else {
    // Legacy users.txt append
    File usersFile = LittleFS.open("/users.txt", "a");
    if (usersFile) {
      usersFile.println(userEntry);
      usersFile.close();
    }
  }
  
  Serial.println(String("[admin] Approved user: ") + username + " with requested password");
  
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, "{\"success\":true}", HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

esp_err_t handleAdminDenyUser(httpd_req_t* req) {
  String u; String ip; getClientIP(req, ip);
  if (!isAuthed(req, u)) { return sendAuthRequiredResponse(req); }
  logAuthAttempt(true, req->uri, u, ip, "");
  
  // Check if user is admin
  if (!isAdminUser(u)) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"success\":false,\"error\":\"Admin access required\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }
  
  // Read full body and decode
  int total_len = req->content_len;
  if (total_len <= 0) { httpd_resp_set_type(req, "application/json"); httpd_resp_send(req, "{\"success\":false,\"error\":\"No data\"}", HTTPD_RESP_USE_STRLEN); return ESP_OK; }
  std::unique_ptr<char[]> buf(new char[total_len+1]); int received=0; while (received<total_len) { int r=httpd_req_recv(req, buf.get()+received, total_len-received); if (r<=0){ httpd_resp_set_type(req, "application/json"); httpd_resp_send(req, "{\"success\":false,\"error\":\"Read error\"}", HTTPD_RESP_USE_STRLEN); return ESP_OK; } received+=r; } buf[received]='\0';
  String body = String(buf.get());
  String username = urlDecode(extractFormField(body, "username"));
  
  if (username.length() == 0) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"success\":false,\"error\":\"Username required\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }
  
  // Remove user from pending list
  String pendingContent = "";
  bool found = false;
  
  File pendingFile = LittleFS.open("/pending_users.txt", "r");
  if (pendingFile) {
    while (pendingFile.available()) {
      String line = pendingFile.readStringUntil('\n');
      line.trim();
      if (line.length() > 0) {
        int colonPos = line.indexOf(':');
        String lineUsername = (colonPos > 0) ? line.substring(0, colonPos) : line;
        if (lineUsername == username) {
          found = true;
          // Skip this line (don't add to pendingContent)
        } else {
          pendingContent += line + "\n";
        }
      }
    }
    pendingFile.close();
  }
  
  if (!found) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"success\":false,\"error\":\"User not found in pending list\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }
  
  // Write back pending file without denied user
  File pendingWrite = LittleFS.open("/pending_users.txt", "w");
  if (pendingWrite) {
    pendingWrite.print(pendingContent);
    pendingWrite.close();
  }
  
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, "{\"success\":true}", HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("Booting ESP32 Minimal Auth (esp_http_server)");

  // Filesystem
  fsInit();

  // First-time setup if needed (prompts on Serial)
  firstTimeSetupIfNeeded();

  // Load legacy simple settings (ntp/tz) and unified JSON settings
  loadSettings();
  bool unifiedLoaded = loadUnifiedSettings();
  String fu, fp;
  if (loadUsersFromFile(fu, fp)) { gAuthUser = fu; gAuthPass = fp; }
  rebuildExpectedAuthHeader();
  // Apply unified settings if present; else use defaults if first setup already done
  if (unifiedLoaded) {
    applySettings();
  } else {
    // If firstSetup is already 1 but settings file missing (rare), create defaults now
    prefs.begin("settings", true);
    int firstSetup = prefs.getInt("firstSetup", 0);
    prefs.end();
    if (firstSetup == 1) {
      settingsDefaults();
      saveUnifiedSettings();
      applySettings();
    }
  }

  // Network
  setupWiFi();
  setupNTP();

  // HTTP server
  startHttpServer();

  if (WiFi.isConnected()) {
    Serial.println("Try: http://" + WiFi.localIP().toString());
  }
}

void loop() {
  // Non-blocking Serial CLI
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      // Record the entered command into the unified feed with source tag
      appendCommandToFeed("serial", gSerialCLI);
      String out = processCommand(gSerialCLI);
      broadcastWithOrigin("serial", String(), String(), out);
      gSerialCLI = "";
      Serial.print("$ ");
    } else {
      gSerialCLI += c;
      // Optional: echo
      // Serial.print(c);
    }
  }
  
  // Continuous thermal sensor polling when enabled with rate limiting
  static unsigned long lastThermalRead = 0;
  static unsigned long lastSuccessfulRead = 0;
  static int consecutiveFailures = 0;
  
  if (thermalEnabled && (millis() - lastThermalRead > 200)) { // Poll every 200ms (5 FPS)
    // Enforce minimum 150ms between attempts - MLX90640 needs time to prepare new frame
    if (millis() - lastSuccessfulRead < 150) {
      lastThermalRead = millis();
      return;
    }
    
    bool success = readThermalPixels();
    lastThermalRead = millis();
    
    if (success) {
      lastSuccessfulRead = millis();
      consecutiveFailures = 0;
    } else {
      consecutiveFailures++;
      // Back off if too many failures - increase interval temporarily
      if (consecutiveFailures > 5) {
        delay(50); // Brief pause to let I2C bus recover
      }
    }
  }
  
  // Continuous ToF sensor polling when enabled - optimized for 5Hz sensor rate
  static unsigned long lastToFRead = 0;
  if (tofEnabled && (millis() - lastToFRead > 200)) { // Poll every 200ms to match sensor rate
    readToFObjects();
    lastToFRead = millis();
  }
  
  // esp_http_server handles requests internally
  delay(1);
}

// -------------------------------
// Shared helpers for CLI help UI
// -------------------------------
static String renderHelpMain() {
  return String("\033[2J\033[H") +
         "Help\n"
         "~~~~~\n\n"
         "system   - Help for system commands (status, uptime, memory, etc.)\n"
         "wifi     - Help for WiFi commands (connect, disconnect, etc.)\n"
         "sensors  - Help for sensor commands (ToF, thermal, IMU, etc.)\n"
         "settings - Help for settings/config commands (I2C clocks, polling, UI, output)\n"
         "exit     - Return to normal CLI\n\n"
         "Enter a category name to view commands:";
}

static String renderHelpSystem() {
  return String("\033[2J\033[H") +
         "System Commands\n"
         "~~~~~~~~~~~~~~~\n\n"
         "  status              - Show system status\n"
         "  uptime              - Show system uptime\n"
         "  memory              - Show memory usage\n"
         "  fsusage             - Show filesystem usage (total/used/free)\n"
         "  files [path]        - List files in LittleFS (default '/')\n"
         "  filecreate <path>   - Create an empty file at path\n"
         "  fileview <path>     - View text file content (truncated)\n"
         "  filedelete <path>   - Delete the specified file\n"
         "  broadcast <message> (admin)\n"
         "                      - Send a message to all users\n"
         "  broadcast --user <username> <message> (admin)\n"
         "                      - Send a message to a specific user\n"
         "  reboot              - Restart the system\n"
         "  clear               - Clear CLI history\n\n"
         "Type 'back' to return to help menu or 'exit' to return to CLI.";
}

static String renderHelpSettings() {
  return String("\033[2J\033[H") +
         "Settings / Configuration Commands\n"
         "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n\n"
         "Output Destinations:\n"
         "  outserial <0|1>      - Enable/disable serial output\n"
         "  outweb <0|1>         - Enable/disable web output\n"
         "  outtft <0|1>         - Enable/disable TFT output\n\n"
         "Polling Intervals (ms):\n"
         "  thermalpollms <50..5000>  (alias: thermalpoll)\n"
         "  tofpollms <50..5000>      (alias: tofpoll)\n\n"
         "UI/Filtering:\n"
         "  thermalpalette <turbo|ironbow|grayscale|rainbow>\n"
         "  thermalewma <0.0..1.0>\n"
         "  thermaltransitionms <0..1000> (alias: thermaltransition)\n"
         "  toftransitionms <0..1000>     (alias: toftransition)\n"
         "  tofmaxmm <100..12000>         (alias: tofmax)\n"
         "  tofstability <1..20>\n\n"
         "I2C Clocks (Hz):\n"
         "  i2cclockthermal <400000..1000000> (alias: i2cthermal)\n"
         "  i2cclocktof <50000..400000>       (alias: i2ctof)\n\n"
         "Thermal Frame Rate:\n"
         "  thermaltargetfps <1..8> (alias: thermalfps)\n\n"
         "WiFi/CLI Settings:\n"
         "  wifiautoreconnect <0|1>\n"
         "  clihistorysize <1..100> (alias: clihistory)\n\n"
         "Type 'back' to return to help menu or 'exit' to return to CLI.";
}

static String renderHelpWifi() {
  return String("\033[2J\033[H") +
         "WiFi Commands\n"
         "~~~~~~~~~~~~~\n\n"
         "  wifiinfo     - Show current WiFi connection info\n"
         "  wifilist     - List saved WiFi networks (priority asc)\n"
         "  wifiadd <ssid> <pass> [priority] [hidden0|1] - Add/update network\n"
         "  wifirm <ssid>  - Remove saved network\n"
         "  wifipromote <ssid> [newPriority] - Change priority (default promote to 1)\n"
         "  wifisetssid  - (legacy) Set single SSID (stores/updates in list)\n"
         "  wifisetpass  - (legacy) Set password for current SSID\n"
         "  wificonnect [--best | --index <n>]\n"
         "                  - Connect to best saved network (default) or a specific saved entry by number\n"
         "  wifidisconnect - Disconnect from current WiFi\n"
         "  wifiscan [json] - Scan for nearby access points (use 'json' for machine-readable)\n\n"
         "Type 'back' to return to help menu or 'exit' to return to CLI.";
}

static String renderHelpSensors() {
  return String("\033[2J\033[H") +
         "Sensor Commands\n"
         "~~~~~~~~~~~~~~~\n\n"
         "APDS9960 RGB/Gesture Sensor:\n"
         "  apdscolorstart/stop     - Enable/disable color sensing\n"
         "  apdsproximitystart/stop - Enable/disable proximity sensing\n"
         "  apdsgesturestart/stop   - Enable/disable gesture sensing\n"
         "  apdscolor               - Read color values\n"
         "  apdsproximity           - Read proximity value\n"
         "  apdsgesture             - Read gesture\n\n"
         "VL53L4CX ToF Distance Sensor:\n"
         "  tofstart/stop           - Enable/disable ToF sensor\n"
         "  tof                     - Read distance measurement\n\n"
         "MLX90640 Thermal Camera:\n"
         "  thermalstart/stop       - Enable/disable thermal sensor\n"
         "  thermal                 - Read thermal pixel array\n\n"
         "IMU & Other Sensors:\n"
         "  imustart/stop           - Enable/disable IMU sensor\n"
         "  imu                     - Read IMU data (accel/gyro/temp)\n"
         "  gamepad                 - Read gamepad state\n\n"
         "Type 'back' to return to help menu or 'exit' to return to CLI.";
}

static String exitToNormalBanner() {
  gCLIState = CLI_NORMAL;
  gWebMirror = gHiddenHistory;
  gHiddenHistory = "";
  return String("\033[2J\033[H") + gWebMirror + "\nReturned to normal CLI mode.";
}

static String exitHelpAndExecute(const String& originalCmd) {
  // Exit help, show banner, and then execute the original command in normal mode
  String banner = exitToNormalBanner() + "\n";
  return banner + processCommand(originalCmd);
}

// Minimal CLI processor used by Serial loop
static String processCommand(const String& cmd) {
  String command = cmd;
  command.trim();
  command.toLowerCase();
  
  // Serial.println("[DEBUG] processCommand called with: '" + cmd + "' -> '" + command + "'");
  
  // Handle clear command first, regardless of CLI state
  if (command == "clear") {
    gWebMirror = "";
    gHiddenHistory = "";  // Also clear hidden history
    return "\033[2J\033[H" "CLI history cleared.";
  }
  
    // Handle CLI state transitions and help system
  if (gCLIState == CLI_NORMAL) {
    if (command == "help") {
      gCLIState = CLI_HELP_MAIN;
      gHiddenHistory = gWebMirror;
      gWebMirror = "";
      return renderHelpMain();
    }
  }
  else if (gCLIState == CLI_HELP_MAIN) {
    if (command == "system") {
      gCLIState = CLI_HELP_SYSTEM;
      return renderHelpSystem();
    }
    else if (command == "wifi") {
      gCLIState = CLI_HELP_WIFI;
      return renderHelpWifi();
    }
    else if (command == "sensors") {
      gCLIState = CLI_HELP_SENSORS;
      return renderHelpSensors();
    }
    else if (command == "settings") {
      gCLIState = CLI_HELP_SETTINGS;
      return renderHelpSettings();
    }
    else if (command == "back") {
      // Already at main help menu: re-render help main and stay in help
      gCLIState = CLI_HELP_MAIN;
      return renderHelpMain();
    }
    else if (command == "exit") {
      return exitToNormalBanner();
    }
    else {
      // Exit help main and execute the entered command in normal mode
      // Prepend the standard exit banner so output is not trapped in help buffer
      String savedCmd = cmd; // preserve original input (with args/case)
      return exitHelpAndExecute(savedCmd);
    }
  }
  else if (gCLIState == CLI_HELP_SYSTEM || gCLIState == CLI_HELP_WIFI || gCLIState == CLI_HELP_SENSORS || gCLIState == CLI_HELP_SETTINGS) {
    if (command == "back") {
      gCLIState = CLI_HELP_MAIN;
      return renderHelpMain();
    }
    else if (command == "exit") {
      return exitToNormalBanner();
    }
    else {
      // In a help submenu: treat any other input as a normal CLI command
      // Exit help with banner, then execute the original input
      String savedCmd = cmd; // preserve original input
      return exitHelpAndExecute(savedCmd);
    }
  }

  // Normal CLI commands (processed after help mode logic)
REPROCESS_NORMAL_COMMANDS:
  if (command == "status") {
    return "System Status:\n"
           "  WiFi: " + String(WiFi.isConnected() ? "Connected" : "Disconnected") + "\n"
           "  IP: " + WiFi.localIP().toString() + "\n"
           "  Filesystem: " + String(LittleFS.begin() ? "Ready" : "Error") + "\n"
           "  Free Heap: " + String(ESP.getFreeHeap()) + " bytes";
  }
  else if (command == "uptime") {
    unsigned long uptimeMs = millis();
    unsigned long seconds = uptimeMs / 1000;
    unsigned long minutes = seconds / 60;
    unsigned long hours = minutes / 60;
    return "Uptime: " + String(hours) + "h " + String(minutes % 60) + "m " + String(seconds % 60) + "s";
  }
  else if (command == "wifiinfo") {
    if (WiFi.isConnected()) {
      return "WiFi Status:\n"
             "  SSID: " + WiFi.SSID() + "\n"
             "  IP: " + WiFi.localIP().toString() + "\n"
             "  RSSI: " + String(WiFi.RSSI()) + " dBm\n"
             "  MAC: " + WiFi.macAddress();
    } else {
      return "WiFi: Not connected\n"
             "  Saved SSID: " + gSettings.wifiSSID + "\n"
             "  MAC: " + WiFi.macAddress();
    }
  }
  else if (command == "wifilist") {
    loadWiFiNetworks();
    if (gWifiNetworkCount == 0) return "No saved networks.";
    String out = "Saved Networks (priority asc, numbered)\nUse 'wificonnect <index>' to connect to a specific entry.\n";
    for (int i = 0; i < gWifiNetworkCount; ++i) {
      out += "  " + String(i+1) + ". [" + String(gWifiNetworks[i].priority) + "] '" + gWifiNetworks[i].ssid + "'";
      if (gWifiNetworks[i].hidden) out += " (hidden)";
      if (i == 0) out += "  <- primary";
      out += "\n";
    }
    return out;
  }
  else if (command.startsWith("wifiadd ")) {
    {
      String err = requireAdminFor("adminControls");
      if (err.length()) return err;
    }
    // wifiadd <ssid> <pass> [priority] [hidden0|1]
    String args = cmd.substring(8); args.trim();
    int sp1 = args.indexOf(' '); if (sp1 <= 0) return "Usage: wifiadd <ssid> <pass> [priority] [hidden0|1]";
    String ssid = args.substring(0, sp1);
    String rest = args.substring(sp1 + 1); rest.trim();
    int sp2 = rest.indexOf(' ');
    String pass = (sp2 < 0) ? rest : rest.substring(0, sp2);
    String more = (sp2 < 0) ? "" : rest.substring(sp2 + 1);
    more.trim();
    int pri = 0; bool hid = (ssid.length() == 0);
    if (more.length() > 0) {
      int sp3 = more.indexOf(' ');
      String priStr = (sp3 < 0) ? more : more.substring(0, sp3);
      pri = priStr.toInt(); if (pri <= 0) pri = 1;
      String hidStr = (sp3 < 0) ? "" : more.substring(sp3 + 1);
      hid = (hidStr == "1" || hidStr == "true");
    }
    loadWiFiNetworks();
    upsertWiFiNetwork(ssid, pass, pri, hid);
    sortWiFiByPriority();
    saveWiFiNetworks();
    return "Saved network '" + ssid + "' with priority " + String(pri == 0 ? 1 : pri) + (hid ? " (hidden)" : "");
  }
  else if (command.startsWith("wifirm ")) {
    {
      String err = requireAdminFor("adminControls");
      if (err.length()) return err;
    }
    String ssid = cmd.substring(7); ssid.trim(); if (ssid.length() == 0) return "Usage: wifirm <ssid>";
    loadWiFiNetworks();
    bool ok = removeWiFiNetwork(ssid);
    if (ok) { saveWiFiNetworks(); return "Removed network '" + ssid + "'"; }
    return "Network not found: '" + ssid + "'";
  }
  else if (command.startsWith("wifipromote ")) {
    {
      String err = requireAdminFor("adminControls");
      if (err.length()) return err;
    }
    // wifipromote <ssid> [newPriority]
    String rest = cmd.substring(12); rest.trim(); if (rest.length() == 0) return "Usage: wifipromote <ssid> [newPriority]";
    int sp = rest.indexOf(' ');
    String ssid = (sp < 0) ? rest : rest.substring(0, sp);
    int newPri = (sp < 0) ? 1 : rest.substring(sp + 1).toInt(); if (newPri <= 0) newPri = 1;
    loadWiFiNetworks();
    int idx = findWiFiNetwork(ssid);
    if (idx < 0) return "Network not found: '" + ssid + "'";
    gWifiNetworks[idx].priority = newPri;
    sortWiFiByPriority();
    saveWiFiNetworks();
    return "Priority updated for '" + ssid + "' -> " + String(newPri);
  }
  else if (command.startsWith("wifisetssid ")) {
    {
      String err = requireAdminFor("adminControls");
      if (err.length()) return err;
    }
    String ssid = cmd.substring(12); // Remove "wifisetssid "
    ssid.trim();
    if (ssid.length() == 0) {
      return "Usage: wifisetssid <network_name>\nExample: wifisetssid MyHomeNetwork";
    }
    // Update settings for legacy UI and also add to multi-SSID list if password already known
    gSettings.wifiSSID = ssid; saveUnifiedSettings();
    loadWiFiNetworks();
    upsertWiFiNetwork(ssid, gSettings.wifiPassword, 1, (ssid.length()==0));
    saveWiFiNetworks();
    return "WiFi SSID set to: " + ssid + "\nUse 'wifisetpass' or 'wifiadd' then 'wificonnect'.";
  }
  else if (command.startsWith("wifisetpass ")) {
    {
      String err = requireAdminFor("adminControls");
      if (err.length()) return err;
    }
    String pass = cmd.substring(12); // Remove "wifisetpass "
    pass.trim();
    if (pass.length() == 0) {
      return "Usage: wifisetpass <password>\nExample: wifisetpass MyPassword123";
    }
    gSettings.wifiPassword = pass; saveUnifiedSettings();
    // Update or add current SSID in multi-SSID list
    if (gSettings.wifiSSID.length() > 0) {
      loadWiFiNetworks();
      upsertWiFiNetwork(gSettings.wifiSSID, pass, 1, (gSettings.wifiSSID.length()==0));
      saveWiFiNetworks();
    }
    return "WiFi password updated.\nUse 'wificonnect' to connect.";
  }
  else if (command.startsWith("broadcast --user ")) {
    {
      String err = requireAdminFor("broadcast");
      if (err.length()) return err;
    }
    int pos = cmd.indexOf("--user ");
    if (pos < 0) return "Usage: broadcast --user <username> <message>";
    int uStart = pos + 7;
    int space = cmd.indexOf(' ', uStart);
    if (space < 0) return "Usage: broadcast --user <username> <message>";
    String targetUser = cmd.substring(uStart, space);
    targetUser.trim();
    String msg = cmd.substring(space + 1);
    msg.trim();
    if (targetUser.length() == 0 || msg.length() == 0) return "Usage: broadcast --user <username> <message>";
    int count = 0;
    for (int i = 0; i < MAX_SESSIONS; ++i) {
      if (gSessions[i].sid.length() && gSessions[i].user == targetUser) {
        gSessions[i].notice = msg;
        ++count;
      }
    }
    if (count == 0) return String("No active sessions found for user '") + targetUser + "'.";
    broadcastWithOrigin("admin", String("admin"), String(), String("Sent private message to '") + targetUser + "' (" + count + ")");
    return String("Delivered to ") + count + " session(s) for user '" + targetUser + "'.";
  }
  else if (command.startsWith("broadcast ")) {
    {
      String err = requireAdminFor("broadcast");
      if (err.length()) return err;
    }
    // Preserve original casing and content after the space
    String msg = cmd.substring(10);
    msg.trim();
    if (msg.length() == 0) return "Usage: broadcast <message>";
    broadcastWithOrigin("admin", gExecIsAdmin ? String("admin") : String(), String(), msg);
    return String("Broadcast sent: ") + msg;
  }
  else if (command == "wificonnect" || command.startsWith("wificonnect ")) {
    // Allow normal users to connect; support flags while preserving legacy usage
    loadWiFiNetworks();
    String arg = "";
    if (cmd.length() > 11) { arg = cmd.substring(11); arg.trim(); }
    String prevSSID = WiFi.isConnected() ? WiFi.SSID() : String("");
    bool connected = false;

    // Parse flags: --best, --index N, or legacy positional index
    bool useBest = false;
    int index1 = -1;
    if (arg.length() == 0) {
      useBest = true; // default behavior
    } else if (arg.startsWith("--best")) {
      useBest = true;
    } else if (arg.startsWith("--index ")) {
      String n = arg.substring(8); n.trim();
      index1 = n.toInt();
      if (index1 <= 0 || index1 > gWifiNetworkCount) return String("Usage: wificonnect --index <1..") + String(gWifiNetworkCount) + ">";
    } else {
      // Legacy: numeric positional index
      int sel = arg.toInt();
      if (sel > 0) index1 = sel; else return String("Usage: wificonnect [--best | --index <1..") + String(gWifiNetworkCount) + ">]";
    }

    if (useBest) {
      // Try saved networks by priority; fall back to single setting if none succeed
      if (gWifiNetworkCount > 0) {
        sortWiFiByPriority();
        for (int i = 0; i < gWifiNetworkCount && !connected; ++i) {
          const WifiNetwork& nw = gWifiNetworks[i];
          Serial.printf("Connecting to '%s' (priority %d) ...\n", nw.ssid.c_str(), nw.priority);
          WiFi.begin(nw.ssid.c_str(), nw.password.c_str());
          unsigned long start = millis();
          while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) { delay(200); Serial.print("."); }
          Serial.println();
          if (WiFi.status() == WL_CONNECTED) {
            broadcastOutput(String("WiFi connected: ") + WiFi.localIP().toString());
            gWifiNetworks[i].lastConnected = millis();
            saveWiFiNetworks();
            connected = true;
          } else {
            Serial.printf("Failed connecting to '%s'\n", nw.ssid.c_str());
          }
        }
      }
      if (!connected && gSettings.wifiSSID.length() > 0) {
        WiFi.begin(gSettings.wifiSSID.c_str(), gSettings.wifiPassword.c_str());
        unsigned long start = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) { delay(200); }
        connected = WiFi.status() == WL_CONNECTED;
        if (connected) {
          int idxPrev = findWiFiNetwork(gSettings.wifiSSID);
          if (idxPrev >= 0) { gWifiNetworks[idxPrev].lastConnected = millis(); saveWiFiNetworks(); }
        }
      }
    } else if (index1 > 0) {
      connected = connectWiFiIndex(index1 - 1, 20000);
      if (!connected && prevSSID.length() > 0) {
        connectWiFiSSID(prevSSID, 15000);
      }
    }

    if (connected) {
      if (server == NULL) startHttpServer();
      gOutputFlags |= OUTPUT_WEB; gSettings.outWeb = true; saveUnifiedSettings();
      String ss = WiFi.SSID();
      String ip = WiFi.localIP().toString();
      return String("Connected to WiFi: ") + ss + "\nIP: " + ip + "\nHTTP server restarted and web output enabled.";
    }
    return "Failed to connect. If there was a previous connection, attempted rollback. Check 'wifiinfo'.";
  }
  else if (command == "wifidisconnect") {
    String err = requireAdminFor("adminControls");
    if (err.length()) return err;
    // Stop HTTP server to free heap
    if (server != NULL) {
      httpd_stop(server);
      server = NULL;
    }
    
    // Disable web output flag
    gOutputFlags &= ~OUTPUT_WEB;
    gSettings.outWeb = false;
    saveUnifiedSettings();
    
    // Clear web mirror buffer to free memory
    gWebMirror = "";
    
    WiFi.disconnect();
    return "WiFi disconnected. HTTP server stopped and web output disabled to free heap.";
  }
  else if (command == "memory") {
    Serial.println("[DEBUG] Executing memory command");
    String result = "Memory Usage:\n"
           "  Free Heap: " + String(ESP.getFreeHeap()) + " bytes\n"
           "  Total Heap: " + String(ESP.getHeapSize()) + " bytes\n"
           "  Used Heap: " + String(ESP.getHeapSize() - ESP.getFreeHeap()) + " bytes\n"
           "  Free PSRAM: " + String(ESP.getFreePsram()) + " bytes";
    Serial.println("[DEBUG] Memory command result: " + result);
    return result;
  }
  else if (command == "fsusage") {
    if (!filesystemReady) return "Error: LittleFS not ready";
    size_t totalBytes = LittleFS.totalBytes();
    size_t usedBytes = LittleFS.usedBytes();
    return "Filesystem Usage:\n"
           "  Total: " + String(totalBytes) + " bytes\n"
           "  Used: " + String(usedBytes) + " bytes\n"
           "  Free: " + String(totalBytes - usedBytes) + " bytes\n"
           "  Usage: " + String((usedBytes * 100) / (totalBytes == 0 ? 1 : totalBytes)) + "%";
  }
  else if (command == "files" || command.startsWith("files ")) {
    if (!filesystemReady) {
      return "Error: LittleFS not ready";
    }
    // Parse optional path argument from original input
    String path = "/";
    int sp1 = cmd.indexOf(' ');
    if (sp1 >= 0) {
      String rest = cmd.substring(sp1 + 1);
      rest.trim();
      if (rest.length() > 0) path = rest;
    }
    String out;
    bool ok = buildFilesListing(path, out, /*asJson=*/false);
    if (!ok) return out; // contains error message
    return out;
  }
  else if (command.startsWith("filecreate ") || command.startsWith("filecreate")) {
    {
      String err = requireAdminFor("adminControls");
      if (err.length()) return err;
    }
    if (!filesystemReady) return "Error: LittleFS not ready";
    int sp1 = cmd.indexOf(' ');
    if (sp1 < 0) return "Usage: filecreate <path>";
    String path = cmd.substring(sp1 + 1);
    path.trim();
    if (path.length() == 0) return "Usage: filecreate <path>";
    if (!path.startsWith("/")) path = String("/") + path;
    if (path.endsWith("/")) return "Error: Path must be a file (not a directory)";
    File f = LittleFS.open(path, "w");
    if (!f) return "Error: Failed to create file: " + path;
    f.close();
    return "Created file: " + path;
  }
  else if (command.startsWith("fileview ") || command == "fileview") {
    if (!filesystemReady) return "Error: LittleFS not ready";
    int sp1 = cmd.indexOf(' ');
    if (sp1 < 0) return "Usage: fileview <path>";
    String path = cmd.substring(sp1 + 1);
    path.trim();
    if (path.length() == 0) return "Usage: fileview <path>";
    if (!path.startsWith("/")) path = String("/") + path;
    if (!LittleFS.exists(path)) return "Error: File not found: " + path;
    File f = LittleFS.open(path, "r");
    if (!f) return "Error: Unable to open: " + path;
    String content = f.readString();
    f.close();
    const size_t MAX_SHOW = 2048;
    if (content.length() > MAX_SHOW) {
      String head = content.substring(0, MAX_SHOW);
      return "--- BEGIN (truncated) " + path + " ---\n" + head + "\n--- TRUNCATED (" + String(content.length()) + " bytes total) ---";
    }
    return content;
  }
  else if (command.startsWith("filedelete ") || command == "filedelete") {
    {
      String err = requireAdminFor("adminControls");
      if (err.length()) return err;
    }
    if (!filesystemReady) return "Error: LittleFS not ready";
    int sp1 = cmd.indexOf(' ');
    if (sp1 < 0) return "Usage: filedelete <path>";
    String path = cmd.substring(sp1 + 1);
    path.trim();
    if (path.length() == 0) return "Usage: filedelete <path>";
    if (!path.startsWith("/")) path = String("/") + path;
    if (!LittleFS.exists(path)) return "Error: File not found: " + path;
    if (LittleFS.remove(path)) {
      return "Deleted: " + path;
    } else {
      return "Error: Failed to delete: " + path;
    }
  }
  // Settings update commands (individual keys)
  else if (command.startsWith("wifiautoreconnect ")) {
    {
      String err = requireAdminFor("adminControls");
      if (err.length()) return err;
    }
    String valStr = cmd.substring(18); // after "wifiautoreconnect "
    valStr.trim();
    int v = valStr.toInt();
    gSettings.wifiAutoReconnect = (v != 0);
    saveUnifiedSettings();
    applySettings();
    return String("wifiAutoReconnect set to ") + (gSettings.wifiAutoReconnect ? "1" : "0");
  }
  else if (command.startsWith("clihistory ") || command.startsWith("clihistorysize ")) {
    String valStr;
    if (cmd.startsWith("clihistorysize ")) valStr = cmd.substring(15); else valStr = cmd.substring(11);
    valStr.trim(); int v = valStr.toInt(); if (v < 1) v = 1; if (v > 100) v = 100;
    gSettings.cliHistorySize = v; saveUnifiedSettings();
    return "cliHistorySize set to " + String(v);
  }
  else if (command.startsWith("outserial ")) {
    String valStr = cmd.substring(10); valStr.trim(); int v = valStr.toInt();
    gSettings.outSerial = (v != 0); saveUnifiedSettings(); applySettings();
    return String("outSerial set to ") + (gSettings.outSerial ? "1" : "0");
  }
  else if (command.startsWith("outweb ")) {
    {
      String err = requireAdminFor("adminControls");
      if (err.length()) return err;
    }
    String valStr = cmd.substring(7); valStr.trim(); int v = valStr.toInt();
    gSettings.outWeb = (v != 0); saveUnifiedSettings(); applySettings();
    return String("outWeb set to ") + (gSettings.outWeb ? "1" : "0");
  }
  else if (command.startsWith("outtft ")) {
    String valStr = cmd.substring(7); valStr.trim(); int v = valStr.toInt();
    gSettings.outTft = (v != 0); saveUnifiedSettings(); applySettings();
    return String("outTft set to ") + (gSettings.outTft ? "1" : "0");
  }
  else if (command.startsWith("thermalpoll ") || command.startsWith("thermalpollms ")) {
    {
      String err = requireAdminFor("sensorConfig");
      if (err.length()) return err;
    }
    String valStr; if (cmd.startsWith("thermalpollms ")) valStr = cmd.substring(14); else valStr = cmd.substring(12); valStr.trim(); int v = valStr.toInt();
    if (v < 50) v = 50; if (v > 5000) v = 5000; gSettings.thermalPollingMs = v; saveUnifiedSettings();
    return "thermalPollingMs set to " + String(v);
  }
  else if (command.startsWith("tofpoll ") || command.startsWith("tofpollms ")) {
    {
      String err = requireAdminFor("sensorConfig");
      if (err.length()) return err;
    }
    String valStr; if (cmd.startsWith("tofpollms ")) valStr = cmd.substring(10); else valStr = cmd.substring(8); valStr.trim(); int v = valStr.toInt();
    if (v < 50) v = 50; if (v > 5000) v = 5000; gSettings.tofPollingMs = v; saveUnifiedSettings();
    return "tofPollingMs set to " + String(v);
  }
  else if (command.startsWith("tofstability ")) {
    {
      String err = requireAdminFor("sensorConfig");
      if (err.length()) return err;
    }
    String valStr = cmd.substring(12); valStr.trim(); int v = valStr.toInt();
    if (v < 1) v = 1; if (v > 20) v = 20; gSettings.tofStabilityThreshold = v; saveUnifiedSettings();
    return "tofStabilityThreshold set to " + String(v);
  }
  else if (command.startsWith("thermalpalette ")) {
    {
      String err = requireAdminFor("sensorConfig");
      if (err.length()) return err;
    }
    String v = cmd.substring(15); v.trim(); v.toLowerCase();
    if (v != "turbo" && v != "ironbow" && v != "grayscale" && v != "rainbow") { v = "turbo"; }
    gSettings.thermalPaletteDefault = v; saveUnifiedSettings();
    return "thermalPaletteDefault set to " + v;
  }
  else if (command.startsWith("thermalewma ")) {
    {
      String err = requireAdminFor("sensorConfig");
      if (err.length()) return err;
    }
    String valStr = cmd.substring(12); valStr.trim(); float v = valStr.toFloat();
    if (v < 0.0f) v = 0.0f; if (v > 1.0f) v = 1.0f; gSettings.thermalEWMAFactor = v; saveUnifiedSettings();
    return "thermalEWMAFactor set to " + String(v, 3);
  }
  else if (command.startsWith("thermaltransition ") || command.startsWith("thermaltransitionms ")) {
    {
      String err = requireAdminFor("sensorConfig");
      if (err.length()) return err;
    }
    String valStr; if (cmd.startsWith("thermaltransitionms ")) valStr = cmd.substring(21); else valStr = cmd.substring(18); valStr.trim(); int v = valStr.toInt();
    if (v < 0) v = 0; if (v > 1000) v = 1000; gSettings.thermalTransitionMs = v; saveUnifiedSettings();
    return "thermalTransitionMs set to " + String(v);
  }
  else if (command.startsWith("toftransition ") || command.startsWith("toftransitionms ")) {
    {
      String err = requireAdminFor("sensorConfig");
      if (err.length()) return err;
    }
    String valStr; if (cmd.startsWith("toftransitionms ")) valStr = cmd.substring(16); else valStr = cmd.substring(14); valStr.trim(); int v = valStr.toInt();
    if (v < 0) v = 0; if (v > 1000) v = 1000; gSettings.tofTransitionMs = v; saveUnifiedSettings();
    return "tofTransitionMs set to " + String(v);
  }
  else if (command.startsWith("tofmax ") || command.startsWith("tofmaxmm ")) {
    {
      String err = requireAdminFor("sensorConfig");
      if (err.length()) return err;
    }
    String valStr; if (cmd.startsWith("tofmaxmm ")) valStr = cmd.substring(9); else valStr = cmd.substring(7); valStr.trim(); int v = valStr.toInt();
    if (v < 100) v = 100; if (v > 12000) v = 12000; gSettings.tofUiMaxDistanceMm = v; saveUnifiedSettings();
    return "tofUiMaxDistanceMm set to " + String(v);
  }
  else if (command.startsWith("i2cclockthermal ") || command.startsWith("i2cthermal ")) {
    {
      String err = requireAdminFor("sensorConfig");
      if (err.length()) return err;
    }
    String valStr; if (cmd.startsWith("i2cclockthermal ")) valStr = cmd.substring(16); else valStr = cmd.substring(11); valStr.trim(); int v = valStr.toInt();
    if (v < 400000) v = 400000; if (v > 1000000) v = 1000000; gSettings.i2cClockThermalHz = v; saveUnifiedSettings(); applySettings();
    return "i2cClockThermalHz set to " + String(v);
  }
  else if (command.startsWith("i2cclocktof ") || command.startsWith("i2ctof ")) {
    {
      String err = requireAdminFor("sensorConfig");
      if (err.length()) return err;
    }
    String valStr; if (cmd.startsWith("i2cclocktof ")) valStr = cmd.substring(12); else valStr = cmd.substring(7); valStr.trim(); int v = valStr.toInt();
    if (v < 50000) v = 50000; if (v > 400000) v = 400000; gSettings.i2cClockToFHz = v; saveUnifiedSettings(); applySettings();
    return "i2cClockToFHz set to " + String(v);
  }
  else if (command.startsWith("thermalfps ") || command.startsWith("thermaltargetfps ")) {
    {
      String err = requireAdminFor("sensorConfig");
      if (err.length()) return err;
    }
    String valStr; if (cmd.startsWith("thermaltargetfps ")) valStr = cmd.substring(17); else valStr = cmd.substring(11); valStr.trim(); int v = valStr.toInt();
    if (v < 1) v = 1; if (v > 8) v = 8; gSettings.thermalTargetFps = v; saveUnifiedSettings(); applySettings();
    return "thermalTargetFps set to " + String(v);
  }
  else if (command == "reboot") {
    String err = requireAdminFor("adminControls");
    if (err.length()) return err;
    ESP.restart();
    return "Rebooting system...";
  }
  else if (command == "wifidisconnect") {
    String err = requireAdminFor("adminControls");
    if (err.length()) return err;
    bool wasConnected = WiFi.isConnected();
    WiFi.disconnect(true /*wifioff*/);
    delay(50);
    WiFi.mode(WIFI_STA); // keep STA mode so future connects work
    if (wasConnected) return "WiFi disconnected";
    return "WiFi already disconnected";
  }
  else if (command == "wifiscan" || command.startsWith("wifiscan ")) {
    bool json = (command == "wifiscan json" || command == "wifiscan  json");
    int n = WiFi.scanNetworks(/*async=*/false, /*hidden=*/true);
    if (json) {
      String out = "[";
      for (int i = 0; i < n; ++i) {
        if (i) out += ",";
        String ssid = WiFi.SSID(i);
        int32_t rssi = WiFi.RSSI(i);
        int32_t channel = WiFi.channel(i);
        wifi_auth_mode_t auth = WiFi.encryptionType(i);
        bool hidden = (ssid.length() == 0);
        String authStr = String((int)auth);
        out += String("{\"ssid\":\"") + ssid + "\",";
        out += String("\"rssi\":") + String(rssi) + ",";
        out += String("\"channel\":") + String(channel) + ",";
        out += String("\"auth\":\"") + authStr + "\",";
        out += String("\"hidden\":") + (hidden ? "true" : "false") + "}";
      }
      out += "]";
      return out;
    } else {
      String out;
      out.reserve(256 + n * 48);
      out += "Found "; out += String(n); out += " AP(s)\n";
      out += "SSID                             RSSI  CH  SEC  HID\n";
      out += "----------------------------------------------------\n";
      for (int i = 0; i < n; ++i) {
        String ssid = WiFi.SSID(i);
        int32_t rssi = WiFi.RSSI(i);
        int32_t channel = WiFi.channel(i);
        wifi_auth_mode_t auth = WiFi.encryptionType(i);
        bool hidden = (ssid.length() == 0);
        String sec = (auth == WIFI_AUTH_OPEN ? "OPEN" : "LOCK");
        // pad/trim SSID to 30 chars for table
        String name = ssid;
        if (name.length() > 30) name = name.substring(0, 30);
        while (name.length() < 30) name += ' ';
        out += name + "  ";
        out += String(rssi);
        if (rssi > -100) out += " ";
        if (rssi > -10) out += " ";
        out += "  ";
        if (channel < 10) out += " ";
        out += String(channel) + "  ";
        out += sec + "   ";
        out += (hidden ? "Y" : "N");
        out += "\n";
      }
      return out;
    }
  }
  // APDS9960 sensor commands
  else if (command == "apdscolor") {
    readAPDSColor();
    return "APDS color data read (check serial output)";
  }
  else if (command == "apdsproximity") {
    readAPDSProximity();
    return "APDS proximity data read (check serial output)";
  }
  else if (command == "apdsgesture") {
    readAPDSGesture();
    return "APDS gesture data read (check serial output)";
  }
  else if (command == "apdscolorstart") {
    if (rgbgestureConnected) {
      if (!initAPDS9960()) {
        return "ERROR: Failed to initialize APDS9960 sensor";
      }
      if (apds != nullptr) {
        apds->enableColor(true);
        bool prev = apdsColorEnabled;
        apdsColorEnabled = true;
        if (apdsColorEnabled != prev) sensorStatusBump();
        return "APDS color sensing enabled";
      }
      return "ERROR: APDS sensor object is null";
    } else {
      return "APDS sensor not detected";
    }
  }
  else if (command == "apdscolorstop") {
    if (apdsConnected && apds != nullptr) {
      apds->enableColor(false);
      bool prev = apdsColorEnabled;
      apdsColorEnabled = false;
      if (apdsColorEnabled != prev) sensorStatusBump();
      return "APDS color sensing disabled";
    } else {
      return "APDS sensor not initialized";
    }
  }
  else if (command == "apdsproximitystart") {
    if (rgbgestureConnected) {
      if (!initAPDS9960()) {
        return "ERROR: Failed to initialize APDS9960 sensor";
      }
      if (apds != nullptr) {
        apds->enableProximity(true);
        bool prev = apdsProximityEnabled;
        apdsProximityEnabled = true;
        if (apdsProximityEnabled != prev) sensorStatusBump();
        return "APDS proximity sensing enabled";
      }
      return "ERROR: APDS sensor object is null";
    } else {
      return "APDS sensor not detected";
    }
  }
  else if (command == "apdsproximitystop") {
    if (apdsConnected && apds != nullptr) {
      apds->enableProximity(false);
      bool prev = apdsProximityEnabled;
      apdsProximityEnabled = false;
      if (apdsProximityEnabled != prev) sensorStatusBump();
      return "APDS proximity sensing disabled";
    } else {
      return "APDS sensor not initialized";
    }
  }
  else if (command == "apdsgesturestart") {
    if (rgbgestureConnected) {
      if (!initAPDS9960()) {
        return "ERROR: Failed to initialize APDS9960 sensor";
      }
      if (apds != nullptr) {
        apds->enableProximity(true);
        apds->enableGesture(true);
        bool prev = apdsGestureEnabled;
        apdsGestureEnabled = true;
        if (apdsGestureEnabled != prev) sensorStatusBump();
        return "APDS gesture sensing enabled";
      }
      return "ERROR: APDS sensor object is null";
    } else {
      return "APDS sensor not detected";
    }
  }
  else if (command == "apdsgesturestop") {
    if (apdsConnected && apds != nullptr) {
      apds->enableGesture(false);
      apds->enableProximity(false);
      bool prev = apdsGestureEnabled;
      apdsGestureEnabled = false;
      if (apdsGestureEnabled != prev) sensorStatusBump();
      return "APDS gesture sensing disabled";
    } else {
      return "APDS sensor not initialized";
    }
  }
  // ToF sensor commands
  else if (command == "tof") {
    float distance = readTOFDistance();
    if (distance < 999.0) {
      return "Distance: " + String(distance, 1) + " cm";
    } else {
      return "No valid distance measurement";
    }
  }
  else if (command == "tofstart") {
    if (tofSensor == nullptr) {
      bool init_success = initToFSensor();
      if (!init_success) {
        tofEnabled = false;
        tofConnected = false;
        return "ERROR: Failed to initialize ToF sensor";
      }
    }
    
    if (tofSensor == nullptr) {
      tofEnabled = false;
      tofConnected = false;
      return "ERROR: ToF sensor object is still null after initialization";
    }
    
    VL53L4CX_Error start_status = tofSensor->VL53L4CX_StartMeasurement();
    if (start_status != VL53L4CX_ERROR_NONE) {
      bool prev = tofEnabled;
      tofEnabled = false;
      if (tofEnabled != prev) sensorStatusBump();
      return "ERROR: Failed to start ToF measurement";
    }
    
    { bool prev = tofEnabled; tofEnabled = true; if (tofEnabled != prev) sensorStatusBump(); }
    return "SUCCESS: ToF sensor started successfully";
  }
  else if (command == "tofstop") {
    if (!tofConnected || tofSensor == nullptr) {
      { bool prev = tofEnabled; tofEnabled = false; if (tofEnabled != prev) sensorStatusBump(); }
      return "ERROR: ToF sensor not initialized";
    }
    
    VL53L4CX_Error stop_status = tofSensor->VL53L4CX_StopMeasurement();
    if (stop_status != VL53L4CX_ERROR_NONE) {
      return "ERROR: Failed to stop ToF measurement";
    } else {
      { bool prev = tofEnabled; tofEnabled = false; if (tofEnabled != prev) sensorStatusBump(); }
      return "SUCCESS: ToF sensor stopped successfully";
    }
  }
  // Thermal sensor commands
  else if (command == "thermal") {
    if (thermalSensor == nullptr) {
      if (!initThermalSensor()) {
        return "Failed to initialize MLX90640 thermal sensor";
      }
    }
    
    readThermalPixels();
    return "Thermal data read (check serial output)";
  }
  else if (command == "thermalstart") {
    if (thermalSensor == nullptr) {
      if (!initThermalSensor()) {
        return "Failed to initialize MLX90640 thermal sensor";
      }
    }
    // Enable locally to permit read, but do not bump SSE until we capture an initial frame
    bool prev = thermalEnabled;
    thermalEnabled = true;
    bool ok = readThermalPixels();
    if (!ok) {
      // Revert state on failure and do not notify clients
      thermalEnabled = prev;
      return "ERROR: Failed initial MLX90640 frame capture";
    }
    // Initial frame captured and cached; now notify clients that thermal is enabled
    if (thermalEnabled != prev) sensorStatusBump();
    return "SUCCESS: MLX90640 thermal sensor started (authorized + initial frame ready)";
  }
  else if (command == "thermalstop") {
    { bool prev = thermalEnabled; thermalEnabled = false; if (thermalEnabled != prev) sensorStatusBump(); }
    return "MLX90640 thermal sensor stopped";
  }
  // IMU sensor commands
  else if (command == "imu") {
    if (!gyrosensorConnected || lsm6ds == nullptr) {
      if (!initIMUSensor()) {
        return "Failed to initialize IMU sensor";
      }
    }
    readIMUSensor();
    return "IMU data read (check serial output)";
  }
  else if (command == "imustart") {
    if (!initIMUSensor()) {
      return "Failed to initialize IMU sensor";
    }
    return "IMU sensor started successfully";
  }
  else if (command == "imustop") {
    if (lsm6ds != nullptr) {
      delete lsm6ds;
      lsm6ds = nullptr;
      gyrosensorConnected = false;
      return "IMU sensor stopped";
    } else {
      return "IMU sensor not initialized";
    }
  }
  // Gamepad commands
  else if (command == "gamepad") {
    readGamepad();
    return "Gamepad data read (check serial output)";
  }
  else if (command.length() == 0) {
    return "";
  }
  else {
    Serial.println("[DEBUG] Unknown command fallback for: '" + command + "'");
    String result = "Unknown command: " + cmd + "\nType 'help' for available commands";
    Serial.println("[DEBUG] Unknown command result: " + result);
    return result;
  }
}

//----------------------------------------------------
// Sensor Initialization Functions
//----------------------------------------------------

// Follow the APDS9960 example program style exactly
bool initAPDS9960() {
  // Check if already initialized to avoid duplicate initialization
  if (apds != nullptr) {
    Serial.println("APDS9960 already initialized!");
    return true;
  }
  
  // Create a new APDS9960 object
  apds = new Adafruit_APDS9960();
  if (apds == nullptr) {
    Serial.println("Failed to allocate memory for APDS9960!");
    return false;
  }
  
  // Make sure Wire1 is properly configured with our pins
  Wire1.begin(22, 19);
  
  // Simple begin with no parameters - as shown in example code
  if (!apds->begin()) {
    Serial.println("Failed to initialize APDS9960! Please check your wiring.");
    delete apds;
    apds = nullptr;
    return false;
  }
  
  Serial.println("APDS9960 initialized successfully!");
  apdsConnected = true;
  rgbgestureConnected = true;
  return true;
}

bool initThermalSensor() {
  // Check if already initialized to avoid duplicate initialization
  if (thermalSensor != nullptr) {
    Serial.println("MLX90640 thermal sensor already initialized!");
    return true;
  }
  
  Serial.println("DEBUG: Starting MLX90640 initialization...");
  
  // Create a new MLX90640 object
  thermalSensor = new Adafruit_MLX90640();
  if (thermalSensor == nullptr) {
    Serial.println("Failed to allocate memory for MLX90640!");
    return false;
  }
  
  // Allocate frame buffer for 768 pixels (32x24)
  if (mlx90640_frame == nullptr) {
    mlx90640_frame = (float*)malloc(768 * sizeof(float));
    if (mlx90640_frame == nullptr) {
      Serial.println("Failed to allocate MLX90640 frame buffer!");
      delete thermalSensor;
      thermalSensor = nullptr;
      return false;
    }
  }
  
  // Configure Wire1 for STEMMA QT (SDA=22, SCL=19)
  Wire1.begin(22, 19);
  Wire1.setClock(400000); // 400kHz I2C clock
  
  Serial.println("DEBUG: Wire1 configured, attempting sensor initialization...");
  
  // Initialize the sensor with Wire1 interface
  if (!thermalSensor->begin(MLX90640_I2CADDR_DEFAULT, &Wire1)) {
    Serial.println("Failed to initialize MLX90640 with Wire1!");
    delete thermalSensor;
    thermalSensor = nullptr;
    if (mlx90640_frame) {
      free(mlx90640_frame);
      mlx90640_frame = nullptr;
    }
    return false;
  }
  
  // Configure MLX90640 settings
  thermalSensor->setMode(MLX90640_CHESS);
  thermalSensor->setResolution(MLX90640_ADC_18BIT);
  thermalSensor->setRefreshRate(MLX90640_16_HZ);
  
  Serial.println("MLX90640 thermal sensor initialized successfully!");
  thermalConnected = true;
  mlx90640_initialized = true;
  return true;
}

bool initToFSensor() {
  Serial.println("DEBUG: ToF sensor initialized using proven example approach");
  
  if (tofSensor != nullptr) {
    Serial.println("DEBUG: ToF sensor already initialized");
    return true;
  }
  
  // Initialize I2C with slower clock for reliability
  Wire1.begin(22, 19);
  Wire1.setClock(50000); // 50kHz for better compatibility
  delay(200);
  
  // Check for ToF sensor using the example's address detection method
  Serial.println("DEBUG: Checking for VL53L4CX at address 0x29...");
  Wire1.beginTransmission(0x29);
  byte error = Wire1.endTransmission();
  
  if (error != 0) {
    Serial.printf("ERROR: VL53L4CX not found at address 0x29 (error: %d)\n", error);
    tofConnected = false;
    return false;
  }
  Serial.println("DEBUG: VL53L4CX found at address 0x29");
  
  // Create sensor object using example pattern
  tofSensor = new VL53L4CX();
  if (tofSensor == nullptr) {
    Serial.println("ERROR: Failed to allocate memory for VL53L4CX sensor");
    return false;
  }
  
  // Configure sensor exactly like the working example
  tofSensor->setI2cDevice(&Wire1);
  tofSensor->setXShutPin(A1);
  
  // Initialize using the exact example sequence
  VL53L4CX_Error status = tofSensor->begin();
  if (status != VL53L4CX_ERROR_NONE) {
    Serial.printf("ERROR: VL53L4CX begin() failed with status: %d\n", status);
    delete tofSensor;
    tofSensor = nullptr;
    return false;
  }
  
  // Switch off before initialization (like example)
  tofSensor->VL53L4CX_Off();
  
  // Initialize sensor (like example)
  status = tofSensor->InitSensor(VL53L4CX_DEFAULT_DEVICE_ADDRESS);
  if (status != VL53L4CX_ERROR_NONE) {
    Serial.printf("ERROR: InitSensor() failed with status: %d\n", status);
    delete tofSensor;
    tofSensor = nullptr;
    return false;
  }
  
  // Advanced VL53L4CX configuration for optimal performance
  Serial.println("DEBUG: Applying advanced VL53L4CX configuration...");
  
  // Set distance mode to LONG for maximum 6m range
  VL53L4CX_Error distance_status = tofSensor->VL53L4CX_SetDistanceMode(VL53L4CX_DISTANCEMODE_LONG);
  if (distance_status == VL53L4CX_ERROR_NONE) {
    Serial.println("DEBUG: Distance mode set to LONG for maximum 6m range");
  } else {
    Serial.printf("WARNING: Failed to set distance mode: %d\n", distance_status);
  }
  
  // Optimize timing budget for balance of speed and accuracy (200ms is optimal)
  VL53L4CX_Error timing_status = tofSensor->VL53L4CX_SetMeasurementTimingBudgetMicroSeconds(200000);
  if (timing_status == VL53L4CX_ERROR_NONE) {
    Serial.println("DEBUG: Timing budget optimized to 200ms for 5Hz measurement rate");
  } else {
    Serial.printf("WARNING: Failed to set timing budget: %d\n", timing_status);
  }
  
  // Note: VL53L4CX library doesn't expose intermeasurement or limit check APIs
  // These are handled internally by the 200ms timing budget configuration
  Serial.println("DEBUG: Advanced configuration applied via timing budget optimization");
  
  // Start measurements (like example)
  status = tofSensor->VL53L4CX_StartMeasurement();
  if (status != VL53L4CX_ERROR_NONE) {
    Serial.printf("ERROR: VL53L4CX_StartMeasurement() failed with status: %d\n", status);
    delete tofSensor;
    tofSensor = nullptr;
    return false;
  }
  
  tofConnected = true;
  tofEnabled = true;
  Serial.println("SUCCESS: VL53L4CX ToF sensor initialized with long-range configuration");
  return true;
}

bool initIMUSensor() {
  if (lsm6ds != nullptr) {
    Serial.println("IMU sensor already initialized!");
    return true;
  }
  
  Serial.println("DEBUG: Starting LSM6DS3TRC IMU initialization...");
  
  // Create a new LSM6DS3TRC object
  lsm6ds = new Adafruit_LSM6DS3TRC();
  if (lsm6ds == nullptr) {
    Serial.println("Failed to allocate memory for LSM6DS3TRC!");
    return false;
  }
  
  // Make sure Wire1 is properly configured with our pins
  Wire1.begin(22, 19);
  Wire1.setClock(100000); // 100kHz for compatibility
  delay(100);
  
  // Initialize the sensor with Wire1 interface
  if (!lsm6ds->begin_I2C(LSM6DS_I2CADDR_DEFAULT, &Wire1)) {
    Serial.println("Failed to initialize LSM6DS3TRC with Wire1! Trying default Wire...");
    
    // Try with default Wire as fallback
    if (!lsm6ds->begin_I2C()) {
      Serial.println("Failed to initialize LSM6DS3TRC with both Wire interfaces!");
      delete lsm6ds;
      lsm6ds = nullptr;
      return false;
    } else {
      Serial.println("WARNING: LSM6DS3TRC initialized with default Wire instead of Wire1");
    }
  }
  
  Serial.println("LSM6DS3TRC IMU sensor initialized successfully!");
  gyrosensorConnected = true;
  return true;
}

//----------------------------------------------------
// Sensor Reading Functions
//----------------------------------------------------

void readAPDSColor() {
  if (!apdsConnected || apds == nullptr) {
    broadcastOutput("APDS9960 sensor not connected or initialized");
    return;
  }
  
  if (!apdsColorEnabled) {
    broadcastOutput("Color sensing not enabled. Use 'apdscolorstart' first.");
    return;
  }
  
  // Create variables to store the color data in
  uint16_t red, green, blue, clear;
  
  // Wait for color data to be ready
  while(!apds->colorDataReady()) {
    delay(5);
  }
  
  // Get the data and print the different channels
  apds->getColorData(&red, &green, &blue, &clear);
  
  String colorData = "Red: " + String(red) + ", Green: " + String(green) + ", Blue: " + String(blue) + ", Clear: " + String(clear);
  broadcastOutput(colorData);
}

void readAPDSProximity() {
  if (!apdsConnected || apds == nullptr) {
    broadcastOutput("APDS9960 sensor not connected or initialized");
    return;
  }
  
  if (!apdsProximityEnabled) {
    broadcastOutput("Proximity sensing not enabled. Use 'apdsproximitystart' first.");
    return;
  }
  
  uint8_t proximity = apds->readProximity();
  String proximityData = "Proximity: " + String(proximity);
  broadcastOutput(proximityData);
}

void readAPDSGesture() {
  if (!apdsConnected || apds == nullptr) {
    broadcastOutput("APDS9960 sensor not connected or initialized");
    return;
  }
  
  if (!apdsGestureEnabled) {
    broadcastOutput("Gesture sensing not enabled. Use 'apdsgesturestart' first.");
    return;
  }
  
  uint8_t gesture = apds->readGesture();
  if(gesture == APDS9960_DOWN) broadcastOutput("Gesture: DOWN");
  if(gesture == APDS9960_UP) broadcastOutput("Gesture: UP");
  if(gesture == APDS9960_LEFT) broadcastOutput("Gesture: LEFT");
  if(gesture == APDS9960_RIGHT) broadcastOutput("Gesture: RIGHT");
  if(gesture == 0) broadcastOutput("No gesture detected");
}

void readIMUSensor() {
  if (!gyrosensorConnected || lsm6ds == nullptr) {
    broadcastOutput("IMU sensor not connected or initialized");
    return;
  }
  
  sensors_event_t accel;
  sensors_event_t gyro;
  sensors_event_t temp;
  lsm6ds->getEvent(&accel, &gyro, &temp);
  
  String accelData = "Accel X: " + String(accel.acceleration.x, 2) + ", Y: " + String(accel.acceleration.y, 2) + ", Z: " + String(accel.acceleration.z, 2) + " m/s^2";
  broadcastOutput(accelData);
  
  String gyroData = "Gyro X: " + String(gyro.gyro.x, 2) + ", Y: " + String(gyro.gyro.y, 2) + ", Z: " + String(gyro.gyro.z, 2) + " rad/s";
  broadcastOutput(gyroData);
  
  String tempData = "Temperature: " + String(temp.temperature, 2) + " C";
  broadcastOutput(tempData);
}

void readGamepad() {
  if (!gamepadConnected) {
    broadcastOutput("Gamepad not connected");
    return;
  }
  
  uint32_t buttons = ss.digitalReadBulk(0xFFFFFFFF);
  int16_t x = ss.analogRead(14);
  int16_t y = ss.analogRead(15);
  
  String gamepadData = "Buttons: 0x" + String(buttons, HEX) + ", X: " + String(x) + ", Y: " + String(y);
  broadcastOutput(gamepadData);
}

bool readToFObjects() {
  if (!tofConnected || !tofEnabled || tofSensor == nullptr) {
    return false;
  }
  
  // Set slower I2C speed for ToF sensor compatibility
  Wire1.setClock(100000); // 100kHz - compromise between 50kHz and thermal needs
  
  VL53L4CX_MultiRangingData_t MultiRangingData;
  VL53L4CX_MultiRangingData_t *pMultiRangingData = &MultiRangingData;
  uint8_t NewDataReady = 0;
  VL53L4CX_Error status;
  
  // Wait for data ready with optimized timeout for 200ms timing budget
  unsigned long startTime = millis();
  do {
    status = tofSensor->VL53L4CX_GetMeasurementDataReady(&NewDataReady);
    if (millis() - startTime > 250) { // 250ms timeout for 200ms measurement + margin
      return false; // Timeout - sensor may be stuck
    }
    if (status != VL53L4CX_ERROR_NONE) {
      return false; // Communication error
    }
  } while (!NewDataReady);
  
  if ((!status) && (NewDataReady != 0)) {
    status = tofSensor->VL53L4CX_GetMultiRangingData(pMultiRangingData);
    
    // Check for data retrieval errors like ST's example
    if (status != VL53L4CX_ERROR_NONE) {
      return false;
    }
    
    int no_of_object_found = pMultiRangingData->NumberOfObjectsFound;
    
    // Cache ToF data for web interface
    gSensorCache.tofTotalObjects = no_of_object_found;
    
    // First, clear all objects (but preserve smoothing history)
    for (int j = 0; j < 4; j++) {
      gSensorCache.tofObjects[j].detected = false;
      gSensorCache.tofObjects[j].distance_mm = 0;
      gSensorCache.tofObjects[j].distance_cm = 0.0;
      gSensorCache.tofObjects[j].status = 0;
      gSensorCache.tofObjects[j].valid = false;
      // Keep hasHistory and smoothed values for continuity
    }
    
    // Process all detected objects and compact valid ones into sequential slots
    int validObjectIndex = 0;
    for (int j = 0; j < no_of_object_found && j < 4; j++) {
      int range_mm = pMultiRangingData->RangeData[j].RangeMilliMeter;
      int range_status = pMultiRangingData->RangeData[j].RangeStatus;
      
      // Get signal quality info like ST's official example
      float signal_rate = (float)pMultiRangingData->RangeData[j].SignalRateRtnMegaCps / 65536.0;
      float ambient_rate = (float)pMultiRangingData->RangeData[j].AmbientRateRtnMegaCps / 65536.0;
      
      // Less restrictive validation - accept more status codes like ST's example
      // Only reject clearly invalid readings
      bool isValid = (range_status != VL53L4CX_RANGESTATUS_SIGNAL_FAIL &&
                     range_status != VL53L4CX_RANGESTATUS_SIGMA_FAIL &&
                     range_status != VL53L4CX_RANGESTATUS_WRAP_TARGET_FAIL &&
                     range_status != VL53L4CX_RANGESTATUS_XTALK_SIGNAL_FAIL);
      
      // Distance-based signal quality requirements
      float minSignalRate;
      if (range_mm < 1000) {
        minSignalRate = 0.1; // Close range: require good signal
      } else if (range_mm < 3000) {
        minSignalRate = 0.05; // Medium range: lower threshold
      } else {
        minSignalRate = 0.02; // Long range: very low threshold for wall detection
      }
      
      bool hasGoodSignal = (signal_rate > minSignalRate);
      
      if (isValid && hasGoodSignal && range_mm > 0 && range_mm <= 6000 && validObjectIndex < 4) {
        float distance_cm = range_mm / 10.0;
        
        // Apply distance-based smoothing - more smoothing for far objects
        float alpha;
        if (range_mm > 3000) {
          alpha = 0.15; // Heavy smoothing for long range to reduce flickering
        } else if (range_mm > 1000) {
          alpha = 0.25; // Medium smoothing for medium range
        } else {
          alpha = 0.4; // Light smoothing for close range (more responsive)
        }
        float smoothed_mm, smoothed_cm;
        
        if (gSensorCache.tofObjects[validObjectIndex].hasHistory) {
          // Apply exponential moving average
          smoothed_mm = alpha * range_mm + (1.0 - alpha) * gSensorCache.tofObjects[validObjectIndex].smoothed_distance_mm;
          smoothed_cm = alpha * distance_cm + (1.0 - alpha) * gSensorCache.tofObjects[validObjectIndex].smoothed_distance_cm;
        } else {
          // First reading - no smoothing
          smoothed_mm = range_mm;
          smoothed_cm = distance_cm;
          gSensorCache.tofObjects[validObjectIndex].hasHistory = true;
        }
        
        gSensorCache.tofObjects[validObjectIndex].detected = true;
        gSensorCache.tofObjects[validObjectIndex].distance_mm = (int)smoothed_mm;
        gSensorCache.tofObjects[validObjectIndex].distance_cm = smoothed_cm;
        gSensorCache.tofObjects[validObjectIndex].smoothed_distance_mm = smoothed_mm;
        gSensorCache.tofObjects[validObjectIndex].smoothed_distance_cm = smoothed_cm;
        gSensorCache.tofObjects[validObjectIndex].status = range_status;
        gSensorCache.tofObjects[validObjectIndex].valid = true;
        
        validObjectIndex++;
      }
    }
    
    // Update total objects to reflect actual valid objects found
    gSensorCache.tofTotalObjects = validObjectIndex;
    
    gSensorCache.tofLastUpdate = millis();
    gSensorCache.tofDataValid = true;
    
    // Clear interrupt and restart
    tofSensor->VL53L4CX_ClearInterruptAndStartMeasurement();
    
    return true;
  }
  
  return false;
}

String getToFDataJSON() {
  if (!gSensorCache.tofDataValid) {
    return "{\"error\":\"ToF sensor not ready\"}";
  }
  
  // Build JSON response from cached data
  String json = "{\"objects\":[";
  
  for (int j = 0; j < 4; j++) {
    if (j > 0) json += ",";
    
    json += "{\"id\":" + String(j + 1) + ",";
    json += "\"detected\":" + String(gSensorCache.tofObjects[j].detected ? "true" : "false") + ",";
    
    if (gSensorCache.tofObjects[j].detected) {
      json += "\"distance_mm\":" + String(gSensorCache.tofObjects[j].distance_mm) + ",";
      json += "\"distance_cm\":" + String(gSensorCache.tofObjects[j].distance_cm, 1) + ",";
      json += "\"status\":" + String(gSensorCache.tofObjects[j].status) + ",";
      json += "\"valid\":" + String(gSensorCache.tofObjects[j].valid ? "true" : "false");
    } else {
      json += "\"distance_mm\":null,";
      json += "\"distance_cm\":null,";
      json += "\"status\":null,";
      json += "\"valid\":false";
    }
    
    json += "}";
  }
  
  json += "],\"total_objects\":" + String(gSensorCache.tofTotalObjects) + ",";
  json += "\"timestamp\":" + String(gSensorCache.tofLastUpdate) + "}";
  
  return json;
}

float readTOFDistance() {
  if (!tofConnected || !tofEnabled || tofSensor == nullptr) {
    broadcastOutput("ToF sensor not ready. Use 'tofstart' first.");
    return 999.9;
  }
  
  // Set slower I2C speed for ToF sensor compatibility
  Wire1.setClock(100000); // 100kHz - compromise between 50kHz and thermal needs
  
  VL53L4CX_MultiRangingData_t MultiRangingData;
  VL53L4CX_MultiRangingData_t *pMultiRangingData = &MultiRangingData;
  uint8_t NewDataReady = 0;
  VL53L4CX_Error status;
  
  // Wait for data ready
  do {
    status = tofSensor->VL53L4CX_GetMeasurementDataReady(&NewDataReady);
  } while (!NewDataReady);
  
  if ((!status) && (NewDataReady != 0)) {
    status = tofSensor->VL53L4CX_GetMultiRangingData(pMultiRangingData);
    
    if (!status) {
      int no_of_object_found = pMultiRangingData->NumberOfObjectsFound;
      
      // Find best valid measurement
      float best_distance = 999.9;
      bool found_valid = false;
      
      for (int j = 0; j < no_of_object_found; j++) {
        if (pMultiRangingData->RangeData[j].RangeStatus == VL53L4CX_RANGESTATUS_RANGE_VALID) {
          float distance_cm = pMultiRangingData->RangeData[j].RangeMilliMeter / 10.0;
          
          // Use closest valid object
          if (distance_cm < best_distance) {
            best_distance = distance_cm;
            found_valid = true;
          }
        }
      }
      
      // Clear interrupt and restart
      tofSensor->VL53L4CX_ClearInterruptAndStartMeasurement();
      
      if (found_valid) {
        String distanceData = "Distance: " + String(best_distance, 1) + " cm";
        broadcastOutput(distanceData);
        return best_distance;
      }
    }
    
    // Clear interrupt even on error
    tofSensor->VL53L4CX_ClearInterruptAndStartMeasurement();
  }
  
  broadcastOutput("No valid distance measurement");
  return 999.9; // No valid measurement
}

bool readThermalPixels() {
  if (thermalSensor == nullptr) {
    return false;
  }
  if (!thermalEnabled) {
    return false;
  }
  
  // Set optimal I2C speed for MLX90640 before frame capture
  Wire1.setClock(800000); // 800kHz - fast but stable
  
  // Capture thermal frame with detailed error reporting
  uint32_t startTime = millis();
  int result = thermalSensor->getFrame(mlx90640_frame);
  uint32_t captureTime = millis() - startTime;
  
  if (result != 0) {
    Serial.printf("MLX90640 frame capture failed: error=%d, time=%ums, heap=%u\n", 
                  result, captureTime, ESP.getFreeHeap());
    
    // Check I2C bus status
    Wire1.beginTransmission(MLX90640_I2CADDR_DEFAULT);
    uint8_t i2c_error = Wire1.endTransmission();
    Serial.printf("I2C bus check: error=%d (0=OK, 1=data_too_long, 2=addr_nack, 3=data_nack, 4=other)\n", i2c_error);
    
    // Check if sensor is still responding
    if (i2c_error != 0) {
      Serial.println("I2C communication failure - sensor may be disconnected or bus locked");
    }
    // Restore I2C to ToF-safe speed after thermal attempt
    Wire1.setClock(100000);
    
    return false;
  }
  
  // Calculate min, max, and average temperatures with outlier filtering
  float minTemp = mlx90640_frame[0];
  float maxTemp = mlx90640_frame[0];
  float sumTemp = 0.0;
  int hottestX = 0, hottestY = 0;
  
  // First pass: calculate basic stats
  for (int i = 0; i < 768; i++) {
    float temp = mlx90640_frame[i];
    if (temp < minTemp) minTemp = temp;
    if (temp > maxTemp) maxTemp = temp;
    sumTemp += temp;
  }
  
  float avgTemp = sumTemp / 768.0;
  
  // Second pass: detect and filter outliers
  // Calculate standard deviation
  float variance = 0.0;
  for (int i = 0; i < 768; i++) {
    float diff = mlx90640_frame[i] - avgTemp;
    variance += diff * diff;
  }
  float stdDev = sqrt(variance / 768.0);
  
  // Filter outliers (pixels more than 3 standard deviations from mean)
  float outlierThreshold = 3.0 * stdDev;
  float filteredMin = avgTemp + 50.0; // Initialize to high value
  float filteredMax = avgTemp - 50.0; // Initialize to low value
  float filteredSum = 0.0;
  int validPixels = 0;
  
  for (int i = 0; i < 768; i++) {
    float temp = mlx90640_frame[i];
    float deviation = abs(temp - avgTemp);
    
    if (deviation <= outlierThreshold) {
      // Valid pixel
      if (temp < filteredMin) filteredMin = temp;
      if (temp > filteredMax) {
        filteredMax = temp;
        hottestX = i % 32;
        hottestY = i / 32;
      }
      filteredSum += temp;
      validPixels++;
    } else {
      // Outlier detected - replace with local average
      int x = i % 32;
      int y = i / 32;
      float localSum = 0.0;
      int localCount = 0;
      
      // Average of neighboring pixels
      for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
          if (dx == 0 && dy == 0) continue; // Skip center pixel
          int nx = x + dx;
          int ny = y + dy;
          if (nx >= 0 && nx < 32 && ny >= 0 && ny < 24) {
            int neighborIdx = ny * 32 + nx;
            float neighborTemp = mlx90640_frame[neighborIdx];
            if (abs(neighborTemp - avgTemp) <= outlierThreshold) {
              localSum += neighborTemp;
              localCount++;
            }
          }
        }
      }
      
      if (localCount > 0) {
        mlx90640_frame[i] = localSum / localCount; // Replace outlier with local average
      } else {
        mlx90640_frame[i] = avgTemp; // Fallback to global average
      }
    }
  }
  
  // Use filtered values if we have enough valid pixels
  if (validPixels > 600) { // At least 78% of pixels are valid
    minTemp = filteredMin;
    maxTemp = filteredMax;
    avgTemp = filteredSum / validPixels;
  }
  
  // Cache thermal data for web interface
  for(int i = 0; i < 768; i++) {
    gSensorCache.thermalFrame[i] = mlx90640_frame[i];
  }
  gSensorCache.thermalMinTemp = minTemp;
  gSensorCache.thermalMaxTemp = maxTemp;
  gSensorCache.thermalAvgTemp = avgTemp;
  gSensorCache.thermalLastUpdate = millis();
  gSensorCache.thermalDataValid = true;
  
  // Restore I2C to ToF-safe speed after successful thermal read
  Wire1.setClock(100000);
  
  return true;
}

// ==========================
// WiFi and HTTP server setup
// ==========================

void setupWiFi() {
  WiFi.mode(WIFI_STA);
  
  // First try multi-SSID saved list
  loadWiFiNetworks();
  bool connected = false;
  if (gWifiNetworkCount > 0) {
    // Route through CLI for unified behavior
    processCommand("wificonnect --best");
    connected = WiFi.isConnected();
  }
  if (!connected) {
    // Fallback to unified settings
    String ssid = gSettings.wifiSSID;
    String pass = gSettings.wifiPassword;
    // Fallback to legacy file-based WiFi if settings are empty
    if (ssid.length() == 0) {
      String fileSSID, filePass;
      if (loadWifiFromFile(fileSSID, filePass)) {
        ssid = fileSSID;
        pass = filePass;
        // Migrate to unified settings
        gSettings.wifiSSID = ssid;
        gSettings.wifiPassword = pass;
        saveUnifiedSettings();
      } else {
        // Use hardcoded defaults
        ssid = DEFAULT_WIFI_SSID;
        pass = DEFAULT_WIFI_PASS;
      }
    }
    WiFi.begin(ssid.c_str(), pass.c_str());
    Serial.printf("Connecting to WiFi SSID '%s'...\n", ssid.c_str());
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
      delay(200);
      Serial.print(".");
    }
    Serial.println();
    connected = WiFi.isConnected();
  }
  if (connected) {
    Serial.print("WiFi connected: "); Serial.println(WiFi.localIP());
  } else {
    Serial.println("WiFi connect timed out; continuing without network");
  }
}

void startHttpServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.max_uri_handlers = 100;
  config.lru_purge_enable = true;
  if (httpd_start(&server, &config) != ESP_OK) {
    Serial.println("ERROR: Failed to start HTTP server");
    return;
  }

  // Define URIs
  static httpd_uri_t root         = { .uri = "/",              .method = HTTP_GET,  .handler = handleRoot,           .user_ctx = NULL };
  static httpd_uri_t pub          = { .uri = "/public",        .method = HTTP_GET,  .handler = handlePublic,         .user_ctx = NULL };
  static httpd_uri_t loginGet     = { .uri = "/login",         .method = HTTP_GET,  .handler = handleLogin,          .user_ctx = NULL };
  static httpd_uri_t loginPost    = { .uri = "/login",         .method = HTTP_POST, .handler = handleLogin,          .user_ctx = NULL };
  static httpd_uri_t loginSetSess = { .uri = "/login/setsession", .method = HTTP_GET, .handler = handleLoginSetSession, .user_ctx = NULL };
  static httpd_uri_t logout       = { .uri = "/logout",        .method = HTTP_GET,  .handler = handleLogout,         .user_ctx = NULL };
  static httpd_uri_t ping         = { .uri = "/api/ping",      .method = HTTP_GET,  .handler = handlePing,           .user_ctx = NULL };
  static httpd_uri_t dash         = { .uri = "/dashboard",     .method = HTTP_GET,  .handler = handleDashboard,      .user_ctx = NULL };
  static httpd_uri_t logs         = { .uri = "/logs.txt",      .method = HTTP_GET,  .handler = handleLogs,           .user_ctx = NULL };
  static httpd_uri_t settingsPage = { .uri = "/settings",      .method = HTTP_GET,  .handler = handleSettingsPage,   .user_ctx = NULL };
  static httpd_uri_t settingsGet  = { .uri = "/api/settings",  .method = HTTP_GET,  .handler = handleSettingsGet,    .user_ctx = NULL };
  static httpd_uri_t settingsUpd  = { .uri = "/api/settings/update",  .method = HTTP_POST, .handler = handleSettingsUpdate,  .user_ctx = NULL };
  static httpd_uri_t apiNotice    = { .uri = "/api/notice",   .method = HTTP_GET,  .handler = handleNotice,        .user_ctx = NULL };
  static httpd_uri_t apiEvents    = { .uri = "/api/events",   .method = HTTP_GET,  .handler = handleEvents,        .user_ctx = NULL };
  static httpd_uri_t filesPage    = { .uri = "/files",         .method = HTTP_GET,  .handler = handleFilesPage,      .user_ctx = NULL };
  static httpd_uri_t filesList    = { .uri = "/api/files/list",.method = HTTP_GET,  .handler = handleFilesList,      .user_ctx = NULL };
  static httpd_uri_t filesCreate  = { .uri = "/api/files/create",.method = HTTP_POST,.handler = handleFilesCreate,    .user_ctx = NULL };
  static httpd_uri_t filesView    = { .uri = "/api/files/view", .method = HTTP_GET,  .handler = handleFileView,       .user_ctx = NULL };
  static httpd_uri_t filesDelete  = { .uri = "/api/files/delete", .method = HTTP_POST, .handler = handleFileDelete,     .user_ctx = NULL };
  static httpd_uri_t cliPage      = { .uri = "/cli",           .method = HTTP_GET,  .handler = handleCLIPage,        .user_ctx = NULL };
  static httpd_uri_t cliCmd       = { .uri = "/api/cli",       .method = HTTP_POST, .handler = handleCLICommand,     .user_ctx = NULL };
  static httpd_uri_t sensorsPage  = { .uri = "/sensors",        .method = HTTP_GET,  .handler = handleSensorsPage,    .user_ctx = NULL };
  static httpd_uri_t sensorData   = { .uri = "/api/sensors",   .method = HTTP_GET,  .handler = handleSensorData,     .user_ctx = NULL };
  static httpd_uri_t sensorsStatus= { .uri = "/api/sensors/status", .method = HTTP_GET, .handler = handleSensorsStatus, .user_ctx = NULL };
  static httpd_uri_t regPage      = { .uri = "/register",        .method = HTTP_GET,  .handler = handleRegisterPage,   .user_ctx = NULL };
  static httpd_uri_t regSubmit    = { .uri = "/register/submit", .method = HTTP_POST, .handler = handleRegisterSubmit, .user_ctx = NULL };
  static httpd_uri_t adminPending = { .uri = "/admin/pending", .method = HTTP_GET,  .handler = handleAdminPending,   .user_ctx = NULL };
  static httpd_uri_t adminApprove = { .uri = "/admin/approve", .method = HTTP_POST, .handler = handleAdminApproveUser, .user_ctx = NULL };
  static httpd_uri_t adminDeny    = { .uri = "/admin/deny",    .method = HTTP_POST, .handler = handleAdminDenyUser,  .user_ctx = NULL };
  static httpd_uri_t adminSessList  = { .uri = "/admin/sessions",        .method = HTTP_GET,  .handler = handleAdminSessionsList,   .user_ctx = NULL };
  static httpd_uri_t adminSessRevoke= { .uri = "/admin/sessions/revoke", .method = HTTP_POST, .handler = handleAdminSessionsRevoke, .user_ctx = NULL };
  static httpd_uri_t sessionsList = { .uri = "/api/sessions",   .method = HTTP_GET,  .handler = handleSessionsList,   .user_ctx = NULL };
  static httpd_uri_t sessionsRevoke = { .uri = "/api/sessions/revoke", .method = HTTP_POST, .handler = handleSessionsRevoke, .user_ctx = NULL };

  // Register
  httpd_register_uri_handler(server, &root);
  httpd_register_uri_handler(server, &pub);
  httpd_register_uri_handler(server, &loginGet);
  httpd_register_uri_handler(server, &loginPost);
  httpd_register_uri_handler(server, &loginSetSess);
  httpd_register_uri_handler(server, &logout);
  httpd_register_uri_handler(server, &ping);
  httpd_register_uri_handler(server, &dash);
  httpd_register_uri_handler(server, &logs);
  httpd_register_uri_handler(server, &settingsPage);
  httpd_register_uri_handler(server, &settingsGet);
  httpd_register_uri_handler(server, &settingsUpd);
  httpd_register_uri_handler(server, &apiNotice);
  httpd_register_uri_handler(server, &filesPage);
  httpd_register_uri_handler(server, &filesList);
  httpd_register_uri_handler(server, &filesCreate);
  httpd_register_uri_handler(server, &apiEvents);
  httpd_register_uri_handler(server, &filesView);
  httpd_register_uri_handler(server, &filesDelete);
  httpd_register_uri_handler(server, &cliPage);
  httpd_register_uri_handler(server, &cliCmd);
  httpd_register_uri_handler(server, &sensorsPage);
  httpd_register_uri_handler(server, &sensorData);
  httpd_register_uri_handler(server, &sensorsStatus);
  httpd_register_uri_handler(server, &regPage);
  httpd_register_uri_handler(server, &regSubmit);
  httpd_register_uri_handler(server, &adminPending);
  httpd_register_uri_handler(server, &adminApprove);
  httpd_register_uri_handler(server, &adminDeny);
  httpd_register_uri_handler(server, &adminSessList);
  httpd_register_uri_handler(server, &adminSessRevoke);
  httpd_register_uri_handler(server, &sessionsList);
  httpd_register_uri_handler(server, &sessionsRevoke);
  Serial.println("HTTP server started");
}
