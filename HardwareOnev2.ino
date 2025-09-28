// Forward declarations to satisfy Arduino's auto-generated prototypes
struct AuthContext;
struct CommandContext;
struct Command;
static String originPrefix(const char* source, const String& user, const String& ip);
static void runAutomationCommandUnified(const String& cmd);
static void runUnifiedSystemCommand(const String& cmd);

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <esp_http_server.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <time.h>
#include <esp_timer.h>
#include "mbedtls/base64.h"
#include <lwip/sockets.h>
#include "web_shared.h"
#include "web_auth_required.h"
#include "web_login.h"
#include "web_login_success.h"
#include "web_dashboard.h"
#include "web_cli.h"
#include "web_files.h"
#include "web_settings.h"
#include "web_sensors.h"
#include "web_automations.h"
#include "web_espnow.h"
#include <lwip/netdb.h>
#include <arpa/inet.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <memory>
#include <ctype.h>
#include <Wire.h>
#include <string.h>
#include "Adafruit_seesaw.h"
#include <Adafruit_Sensor.h>
#include <Adafruit_BNO055.h>
#include <utility/imumaths.h>
#include <Adafruit_NeoPixel.h>
#include "Adafruit_APDS9960.h"
#include "vl53l4cx_class.h"
#include <Adafruit_MLX90640.h>
#include <vector>
#include "mem_util.h"


// Now that esp_http_server.h is included, declare helpers that use httpd_req_t
static bool executeUnifiedWebCommand(httpd_req_t* req, AuthContext& ctx, const String& cmd, String& out);


// Pre-allocation snapshots (used by mem_util.h capture helper)
size_t gAllocHeapBefore = 0;
size_t gAllocPsBefore = 0;
// Global flag to indicate CLI dry-run validation mode (no side effects)
static bool gCLIValidateOnly = false;

// Helper: early-return for validate-only mode inside command branches
#define RETURN_VALID_IF_VALIDATE() \
  do { \
    if (gCLIValidateOnly) return String("VALID"); \
  } while (0)

// Forward declaration for debug output sink used in macros
void broadcastOutput(const String& s);

// ---- Debug System ----
// Bitmask flags for runtime log filtering
#define DEBUG_AUTH 0x0001           // Auth & Cookies
#define DEBUG_HTTP 0x0002           // HTTP Routing
#define DEBUG_SSE 0x0004            // Server-Sent Events
#define DEBUG_CLI 0x0008            // CLI Commands
#define DEBUG_SENSORS_FRAME 0x0010  // Thermal Frame Capture
#define DEBUG_SENSORS_DATA 0x0020   // Thermal Data Processing
#define DEBUG_SENSORS 0x0040        // General Sensors
#define DEBUG_WIFI 0x0080           // WiFi & Network
#define DEBUG_STORAGE 0x0100        // Storage & FS
#define DEBUG_PERFORMANCE 0x0200    // Performance Monitoring
#define DEBUG_DATETIME 0x0400       // Date/Time & NTP
#define DEBUG_AUTOMATIONS 0x0800    // Automations Scheduler & Commands
#define DEBUG_CMD_FLOW 0x1000       // Command Flow tracing
#define DEBUG_USERS 0x2000          // User management (register/approve/deny)
#define DEBUG_SECURITY 0x8000       // Security Events (always on, minimal)

static uint32_t gDebugFlags = DEBUG_SECURITY;  // Default: only security debug
static char* gDebugBuffer = nullptr;           // Debug buffer - allocated with ps_alloc

// Ensure debug buffer exists before use; allocate on first use
static inline bool ensureDebugBuffer() {
  if (!gDebugBuffer) {
    gDebugBuffer = (char*)ps_alloc(1024, AllocPref::PreferPSRAM, "debug.buf");
    if (!gDebugBuffer) {
      // As a last resort, avoid crashing by disabling all non-security debug
      return false;
    }
  }
  return true;
}


// Check if a username already exists in users.json content
static bool usernameExistsInUsersJson(const String& json, const String& username) {
  String needle = String("\"username\": \"") + username + "\"";
  return json.indexOf(needle) >= 0;
}

// -----------------------------
// Utilities: JSON duplicate guards
// -----------------------------
static bool automationIdExistsInJson(const String& json, unsigned long id) {
  String needle = String("\"id\": ") + String(id);
  return json.indexOf(needle) >= 0;
}

// Find automations array bounds: returns [arrStartIndexOf'[', arrEndIndexOf']'] or [-1,-1]
static void findAutomationsArrayBounds(const String& json, int& outStart, int& outEnd) {
  outStart = -1;
  outEnd = -1;
  int arrKey = json.indexOf("\"automations\"");
  if (arrKey < 0) return;
  int lb = json.indexOf('[', arrKey);
  if (lb < 0) return;
  int depth = 0;
  for (int i = lb; i < (int)json.length(); ++i) {
    char c = json[i];
    if (c == '[') depth++;
    else if (c == ']') {
      depth--;
      if (depth == 0) {
        outStart = lb;
        outEnd = i;
        return;
      }
    }
  }
}

// Sanitize duplicate IDs in automations array. Mutates jsonRef if duplicates found.
static bool sanitizeAutomationsJson(String& jsonRef) {
  int arrStart, arrEnd;
  findAutomationsArrayBounds(jsonRef, arrStart, arrEnd);
  if (arrStart < 0 || arrEnd <= arrStart) return false;
  // Track seen IDs (simple fixed-size buffer)
  const int kMax = 512;  // generous
  unsigned long seen[kMax];
  int seenCount = 0;
  bool changed = false;
  int i = arrStart;
  while (i < arrEnd) {
    int idPos = jsonRef.indexOf("\"id\"", i);
    if (idPos < 0 || idPos > arrEnd) break;
    int colon = jsonRef.indexOf(':', idPos);
    if (colon < 0 || colon > arrEnd) break;
    int numStart = colon + 1;
    while (numStart < (int)jsonRef.length() && (jsonRef[numStart] == ' ' || jsonRef[numStart] == '\t')) numStart++;
    int numEnd = numStart;
    while (numEnd < (int)jsonRef.length() && isdigit((unsigned char)jsonRef[numEnd])) numEnd++;
    String idStr = jsonRef.substring(numStart, numEnd);
    idStr.trim();
    unsigned long idVal = idStr.toInt();
    bool dup = false;
    for (int k = 0; k < seenCount; ++k) {
      if (seen[k] == idVal) {
        dup = true;
        break;
      }
    }
    if (!dup) {
      if (seenCount < kMax) seen[seenCount++] = idVal;
      i = numEnd;
      continue;
    }
    // Duplicate found: generate a new unique ID
    unsigned long newId = (unsigned long)millis();
    // simple mixing loop to avoid collision
    int guard = 0;
    while ((automationIdExistsInJson(jsonRef, newId)) && guard < 100) {
      newId += 1 + (unsigned long)random(1, 100000);
      guard++;
    }
    // Replace in text
    String before = jsonRef.substring(0, numStart);
    String after = jsonRef.substring(numEnd);
    jsonRef = before + String(newId) + after;
    changed = true;
    // Adjust bounds since string length may have changed
    int delta = (int)String(newId).length() - (numEnd - numStart);
    arrEnd += delta;
    i = numStart + String(newId).length();
    if (seenCount < kMax) seen[seenCount++] = newId;
  }
  return changed;
}

// (per-sensor dedicated tasks are defined later in the file after Settings)

// Forward declarations for globals/types used by per-sensor tasks
class VL53L4CX;              // ToF sensor class forward decl (header also included)
class Adafruit_BNO055;       // IMU class forward decl (header also included)
extern bool tofEnabled;      // Declared later
extern bool tofConnected;    // Declared later
extern VL53L4CX* tofSensor;  // Declared later
// Forward declarations for helpers used before definitions
bool readToFObjects();
void readIMUSensor();
static bool readText(const char* path, String& out);
static bool appendLineWithCap(const char* path, const String& line, size_t capBytes);
static bool isAdminUser(const String& who);
static String setSession(httpd_req_t* req, const String& u);
static String getCookieSID(httpd_req_t* req);
static int findSessionIndexBySID(const String& sid);
static void buildAllSessionsJson(const String& currentSid, String& out);
static bool initIMUSensor();
// Forward declarations for functions defined later but used earlier
static bool approvePendingUserInternal(const String& username, String& errorOut);
static bool denyPendingUserInternal(const String& username, String& errorOut);
static String jsonEscape(const String& in);
void broadcastWithOrigin(const String& channel, const String& user, const String& origin, const String& message);

// (moved per-sensor dedicated tasks below, after Settings definition)

// Debug macros - only emit if flag is set
#define DEBUGF(flag, fmt, ...) \
  do { \
    if (gDebugFlags & (flag)) { \
      if (ensureDebugBuffer()) { \
        snprintf(gDebugBuffer, 1024, fmt, ##__VA_ARGS__); \
        /* Use Serial direct to avoid stressing HTTP task stack via web history writes */ \
        Serial.printf("[" #flag "] %s\n", gDebugBuffer); \
      } \
    } \
  } while (0)
#define DEBUG_AUTHF(fmt, ...) DEBUGF(DEBUG_AUTH, fmt, ##__VA_ARGS__)
#define DEBUG_HTTPF(fmt, ...) DEBUGF(DEBUG_HTTP, fmt, ##__VA_ARGS__)
#define DEBUG_SSEF(fmt, ...) DEBUGF(DEBUG_SSE, fmt, ##__VA_ARGS__)
#define DEBUG_CLIF(fmt, ...) DEBUGF(DEBUG_CLI, fmt, ##__VA_ARGS__)
#define DEBUG_SENSORSF(fmt, ...) DEBUGF(DEBUG_SENSORS, fmt, ##__VA_ARGS__)
#define DEBUG_FRAMEF(fmt, ...) DEBUGF(DEBUG_SENSORS_FRAME, fmt, ##__VA_ARGS__)
#define DEBUG_DATAF(fmt, ...) DEBUGF(DEBUG_SENSORS_DATA, fmt, ##__VA_ARGS__)
#define DEBUG_WIFIF(fmt, ...) DEBUGF(DEBUG_WIFI, fmt, ##__VA_ARGS__)
#define DEBUG_STORAGEF(fmt, ...) DEBUGF(DEBUG_STORAGE, fmt, ##__VA_ARGS__)
#define DEBUG_PERFORMANCEF(fmt, ...) DEBUGF(DEBUG_PERFORMANCE, fmt, ##__VA_ARGS__)
#define DEBUG_DATETIMEF(fmt, ...) DEBUGF(DEBUG_DATETIME, fmt, ##__VA_ARGS__)
#define DEBUG_AUTOSF(fmt, ...) DEBUGF(DEBUG_AUTOMATIONS, fmt, ##__VA_ARGS__)
#define DEBUG_CMD_FLOWF(fmt, ...) DEBUGF(DEBUG_CMD_FLOW, fmt, ##__VA_ARGS__)
#define DEBUG_USERSF(fmt, ...) DEBUGF(DEBUG_USERS, fmt, ##__VA_ARGS__)
#define DEBUG_SECURITYF(fmt, ...) \
  do { Serial.printf("[SECURITY] " fmt "\n", ##__VA_ARGS__); } while (0)  // Always on

// ----------------------------------------------------------------------------
// Color System - 64-color palette with RGB lookup
// ----------------------------------------------------------------------------
struct RGB {
  uint8_t r, g, b;
};
struct ColorEntry {
  const char* name;
  RGB rgb;
};

// 64-color palette stored in PROGMEM to save RAM
const ColorEntry colorTable[] PROGMEM = {
  // Primary colors
  { "red", { 255, 0, 0 } },
  { "green", { 0, 255, 0 } },
  { "blue", { 0, 0, 255 } },
  { "yellow", { 255, 255, 0 } },
  { "cyan", { 0, 255, 255 } },
  { "magenta", { 255, 0, 255 } },
  { "white", { 255, 255, 255 } },
  { "black", { 0, 0, 0 } },

  // Orange family
  { "orange", { 255, 165, 0 } },
  { "darkorange", { 255, 140, 0 } },
  { "orangered", { 255, 69, 0 } },
  { "coral", { 255, 127, 80 } },
  { "tomato", { 255, 99, 71 } },
  { "peach", { 255, 218, 185 } },

  // Red family
  { "darkred", { 139, 0, 0 } },
  { "crimson", { 220, 20, 60 } },
  { "firebrick", { 178, 34, 34 } },
  { "indianred", { 205, 92, 92 } },
  { "lightcoral", { 240, 128, 128 } },
  { "salmon", { 250, 128, 114 } },

  // Pink family
  { "pink", { 255, 192, 203 } },
  { "lightpink", { 255, 182, 193 } },
  { "hotpink", { 255, 105, 180 } },
  { "deeppink", { 255, 20, 147 } },
  { "palevioletred", { 219, 112, 147 } },
  { "mediumvioletred", { 199, 21, 133 } },

  // Purple family
  { "purple", { 128, 0, 128 } },
  { "darkviolet", { 148, 0, 211 } },
  { "blueviolet", { 138, 43, 226 } },
  { "mediumpurple", { 147, 112, 219 } },
  { "plum", { 221, 160, 221 } },
  { "orchid", { 218, 112, 214 } },

  // Blue family
  { "darkblue", { 0, 0, 139 } },
  { "navy", { 0, 0, 128 } },
  { "mediumblue", { 0, 0, 205 } },
  { "royalblue", { 65, 105, 225 } },
  { "steelblue", { 70, 130, 180 } },
  { "lightblue", { 173, 216, 230 } },
  { "skyblue", { 135, 206, 235 } },
  { "lightskyblue", { 135, 206, 250 } },
  { "deepskyblue", { 0, 191, 255 } },
  { "dodgerblue", { 30, 144, 255 } },
  { "cornflowerblue", { 100, 149, 237 } },
  { "cadetblue", { 95, 158, 160 } },

  // Green family
  { "darkgreen", { 0, 100, 0 } },
  { "forestgreen", { 34, 139, 34 } },
  { "seagreen", { 46, 139, 87 } },
  { "mediumseagreen", { 60, 179, 113 } },
  { "springgreen", { 0, 255, 127 } },
  { "limegreen", { 50, 205, 50 } },
  { "lime", { 0, 255, 0 } },
  { "lightgreen", { 144, 238, 144 } },
  { "palegreen", { 152, 251, 152 } },
  { "aquamarine", { 127, 255, 212 } },
  { "mediumaquamarine", { 102, 205, 170 } },

  // Yellow/Gold family
  { "gold", { 255, 215, 0 } },
  { "lightyellow", { 255, 255, 224 } },
  { "lemonchiffon", { 255, 250, 205 } },
  { "lightgoldenrodyellow", { 250, 250, 210 } },
  { "khaki", { 240, 230, 140 } },
  { "darkkhaki", { 189, 183, 107 } },

  // Brown family
  { "brown", { 165, 42, 42 } },
  { "saddlebrown", { 139, 69, 19 } },
  { "sienna", { 160, 82, 45 } },
  { "chocolate", { 210, 105, 30 } },
  { "peru", { 205, 133, 63 } },
  { "tan", { 210, 180, 140 } },
  { "burlywood", { 222, 184, 135 } },
  { "wheat", { 245, 222, 179 } },

  // Gray family
  { "gray", { 128, 128, 128 } },
  { "darkgray", { 169, 169, 169 } },
  { "lightgray", { 211, 211, 211 } },
  { "silver", { 192, 192, 192 } },
  { "dimgray", { 105, 105, 105 } },
  { "gainsboro", { 220, 220, 220 } }
};
const int numColors = sizeof(colorTable) / sizeof(colorTable[0]);

// LED Effect types
enum EffectType {
  EFFECT_NONE = 0,
  EFFECT_FADE = 1,
  EFFECT_PULSE = 2,
  EFFECT_RAINBOW = 3,
  EFFECT_BREATHE = 4
};

// Color utility functions
bool getRGBFromName(String name, RGB& rgbOut);
RGB blendColors(RGB a, RGB b, float ratio);
RGB adjustBrightness(RGB color, float brightness);
RGB rainbowColor(int step, int maxSteps);
String getClosestColorName(uint16_t r, uint16_t g, uint16_t b, RGB& closestRGB);
void setLEDColor(RGB color);
void runLEDEffect(int effectType, RGB startColor, RGB endColor, unsigned long duration);

// ----------------------------------------------------------------------------
// Time utilities (central): boot-epoch offset + cached ms timestamp prefix
// ----------------------------------------------------------------------------
// Maintains an offset to convert monotonic microseconds to epoch microseconds.
// This avoids repeated time() calls and allows ms-precision prefixes cheaply.
static int64_t gBootEpochUsOffset = 0;         // epoch_us - esp_timer_get_time()
static bool gTimeSyncedMarkerWritten = false;  // ensures we only log the time-synced marker once

// Call when SNTP/RTC time becomes valid or changes significantly.
void timeSyncUpdateBootEpoch() {
  time_t now = time(nullptr);
  if (now > 0) {
    gBootEpochUsOffset = (int64_t)now * 1000000LL - (int64_t)esp_timer_get_time();
  }
}


// ----- Output toggle and debug handlers (Batch 4 - Part C) -----
// (moved per-sensor dedicated tasks below unifiedSensorPollingTask)

// Forward declaration required by schedulerTickMinute
static String processCommand(const String& cmd);

// ----------------------------------------------------------------------------
// Minute-based Automations Scheduler (v1: atTime only)
// ----------------------------------------------------------------------------
// Runs once per minute. Reads automations.json and runs enabled atTime commands
// that match current HH:MM and optional day filters. Prevents duplicate runs in
// the same minute via a tiny memo table.

// Memo of last trigger per automation id: key = YYYYMMDDHHMM (int64 via String hash)
static unsigned long gLastMinuteSeen = 0;  // millis()/60000 snapshot
static const int kAutoMemoCap = 32;
static long* gAutoMemoId = nullptr;
static unsigned long* gAutoMemoKey = nullptr;
// When true, scheduler should run immediately once (e.g., after mutations)
static bool gAutosDirty = false;

static unsigned long makeMinuteKey(const struct tm& tminfo) {
  // YYYYMMDDHHMM packed into 32-bit fits until year ~4294; fine for our usage
  unsigned long y = (unsigned long)(tminfo.tm_year + 1900);
  unsigned long mo = (unsigned long)(tminfo.tm_mon + 1);
  unsigned long d = (unsigned long)tminfo.tm_mday;
  unsigned long hh = (unsigned long)tminfo.tm_hour;
  unsigned long mm = (unsigned long)tminfo.tm_min;
  return (((y * 100UL + mo) * 100UL + d) * 100UL + hh) * 100UL + mm;
}

static bool autoMemoSeenAndSet(long id, unsigned long key) {
  // Return true if we've already run id at this key; otherwise record and return false
  for (int i = 0; i < kAutoMemoCap; ++i) {
    if (gAutoMemoId[i] == id) {
      if (gAutoMemoKey[i] == key) return true;
      gAutoMemoKey[i] = key;
      return false;
    }
  }
  // Insert into first empty or overwrite the oldest slot (simple policy)
  for (int i = 0; i < kAutoMemoCap; ++i) {
    if (gAutoMemoId[i] == 0) {
      gAutoMemoId[i] = id;
      gAutoMemoKey[i] = key;
      return false;
    }
  }
  // Fallback: overwrite slot 0
  gAutoMemoId[0] = id;
  gAutoMemoKey[0] = key;
  return false;
}

static bool parseAtTimeMatchDays(const String& daysCsv, int tm_wday) {
  if (daysCsv.length() == 0) return true;  // no filter means all days
  // tm_wday: 0=Sun..6=Sat. Accept tokens: sun,mon,tue,wed,thu,fri,sat
  static const char* names[7] = { "sun", "mon", "tue", "wed", "thu", "fri", "sat" };
  String want = names[tm_wday];
  String s = daysCsv;
  s.toLowerCase();
  s.replace(" ", "");
  // Ensure commas around for simple contains matching: ,mon,
  String wrapped = String(",") + s + String(",");
  String needle = String(",") + want + String(",");
  return wrapped.indexOf(needle) >= 0;
}

// -------- Automation Logging Infrastructure --------
// Forward declaration for filesystemReady (defined later)
extern bool filesystemReady;

static bool gAutoLogActive = false;
static String gAutoLogFile = "";
static String gAutoLogAutomationName = "";

// Append log entry to automation log file (matches existing log format)
static bool appendAutoLogEntry(const String& type, const String& content) {
  if (!gAutoLogActive || gAutoLogFile.length() == 0) return false;
  if (!filesystemReady) return false;
  
  // Get timestamp in same format as existing logs: [YYYY-MM-DD HH:MM:SS.mmm]
  char tsPrefix[32];
  getTimestampPrefixMsCached(tsPrefix, sizeof(tsPrefix));
  
  // Format: [YYYY-MM-DD HH:MM:SS.mmm] | type | content
  String line;
  line.reserve(200);
  if (tsPrefix[0]) line += tsPrefix;  // already includes trailing " | "
  line += type;
  line += " | ";
  line += content;
  line += "\n";
  
  // Append to file (create if doesn't exist)
  File f = LittleFS.open(gAutoLogFile, "a");
  if (!f) {
    // Try to create directory if it doesn't exist
    int lastSlash = gAutoLogFile.lastIndexOf('/');
    if (lastSlash > 0) {
      String dir = gAutoLogFile.substring(0, lastSlash);
      if (!LittleFS.exists(dir)) {
        // Create directory recursively (simple approach for /logs)
        if (dir == "/logs" && !LittleFS.exists("/logs")) {
          LittleFS.mkdir("/logs");
        }
      }
    }
    
    // Try to open again after directory creation
    f = LittleFS.open(gAutoLogFile, "a");
    if (!f) return false;
  }
  
  size_t written = f.print(line);
  f.close();
  
  return written > 0;
}

static void schedulerTickMinute() {
  // Only valid if time is synced
  time_t now = time(nullptr);
  if (now <= 0) return;

  DEBUGF(DEBUG_DATETIME | DEBUG_AUTOMATIONS, "[automations] tick now=%lu", (unsigned long)now);

  // Load automations.json
  String json;
  if (!readText("/automations.json", json)) return;
  DEBUGF(DEBUG_AUTOMATIONS, "[automations] json size=%d", json.length());

  int evaluated = 0, executed = 0;
  bool queueSanitize = false;
  long seenIds[128];
  int seenCount = 0;

  int pos = 0;
  while (true) {
    int idPos = json.indexOf("\"id\"", pos);
    if (idPos < 0) break;
    int colon = json.indexOf(':', idPos);
    if (colon < 0) break;
    int comma = json.indexOf(',', colon + 1);
    int braceEnd = json.indexOf('}', colon + 1);
    if (braceEnd < 0) break;
    if (comma < 0 || comma > braceEnd) comma = braceEnd;
    String idStr = json.substring(colon + 1, comma);
    idStr.trim();
    long id = idStr.toInt();

    // Extract the object substring
    int objStart = json.lastIndexOf('{', idPos);
    if (objStart < 0) {
      pos = braceEnd + 1;
      continue;
    }
    String obj = json.substring(objStart, braceEnd + 1);

    // Duplicate-id guard
    bool dupSeen = false;
    for (int i = 0; i < seenCount; ++i) {
      if (seenIds[i] == id) {
        dupSeen = true;
        break;
      }
    }
    if (dupSeen) {
      DEBUGF(DEBUG_AUTOMATIONS, "[autos] duplicate id detected at runtime id=%ld; skipping and queuing sanitize", id);
      queueSanitize = true;
      pos = braceEnd + 1;
      continue;
    }
    if (seenCount < (int)(sizeof(seenIds) / sizeof(seenIds[0]))) { seenIds[seenCount++] = id; }

    evaluated++;

    // Check if enabled
    bool enabled = (obj.indexOf("\"enabled\": true") >= 0) || (obj.indexOf("\"enabled\":true") >= 0);
    if (!enabled) {
      DEBUGF(DEBUG_AUTOMATIONS, "[autos] id=%ld skip: disabled", id);
      pos = braceEnd + 1;
      continue;
    }

    // Parse nextAt field
    time_t nextAt = 0;
    int nextAtPos = obj.indexOf("\"nextAt\"");
    if (nextAtPos >= 0) {
      int nextAtColon = obj.indexOf(':', nextAtPos);
      int nextAtComma = obj.indexOf(',', nextAtColon);
      int nextAtBrace = obj.indexOf('}', nextAtColon);
      int nextAtEnd = (nextAtComma > 0 && (nextAtBrace < 0 || nextAtComma < nextAtBrace)) ? nextAtComma : nextAtBrace;
      if (nextAtEnd > nextAtColon) {
        String nextAtStr = obj.substring(nextAtColon + 1, nextAtEnd);
        nextAtStr.trim();
        if (nextAtStr != "null" && nextAtStr.length() > 0) {
          nextAt = (time_t)nextAtStr.toInt();
        }
      }
    }

    // If nextAt is missing or invalid, compute it now
    if (nextAt <= 0) {
      nextAt = computeNextRunTime(obj, now);
      if (nextAt > 0) {
        updateAutomationNextAt(id, nextAt);
        DEBUGF(DEBUG_AUTOMATIONS, "[autos] id=%ld computed missing nextAt=%lu", id, (unsigned long)nextAt);
      } else {
        DEBUGF(DEBUG_AUTOMATIONS, "[autos] id=%ld skip: could not compute nextAt", id);
        pos = braceEnd + 1;
        continue;
      }
    }

    // Check if it's time to run
    if (now >= nextAt) {
      // Extract commands
      String cmdsList[64];
      int cmdsCount = 0;
      int cmdsPos = obj.indexOf("\"commands\"");
      bool haveArray = false;
      int arrStart = -1, arrEnd = -1;

      if (cmdsPos >= 0) {
        int cmdsColon = obj.indexOf(':', cmdsPos);
        if (cmdsColon > 0) {
          arrStart = obj.indexOf('[', cmdsColon);
          if (arrStart > 0) {
            int depth = 0;
            for (int i = arrStart; i < (int)obj.length(); ++i) {
              char c = obj[i];
              if (c == '[') depth++;
              else if (c == ']') {
                depth--;
                if (depth == 0) {
                  arrEnd = i;
                  break;
                }
              }
            }
            haveArray = (arrStart > 0 && arrEnd > arrStart);
          }
        }
      }

      if (haveArray) {
        String body = obj.substring(arrStart + 1, arrEnd);
        int i = 0;
        while (i < (int)body.length() && cmdsCount < 64) {
          while (i < (int)body.length() && (body[i] == ' ' || body[i] == ',')) i++;
          if (i >= (int)body.length()) break;
          if (body[i] == '"') {
            int q1 = i;
            int q2 = body.indexOf('"', q1 + 1);
            if (q2 < 0) break;
            String one = body.substring(q1 + 1, q2);
            one.trim();
            if (one.length() && cmdsCount < 64) { cmdsList[cmdsCount++] = one; }
            i = q2 + 1;
          } else {
            int next = body.indexOf(',', i);
            if (next < 0) break;
            i = next + 1;
          }
        }
      } else {
        // Fallback to single command
        int cpos = obj.indexOf("\"command\"");
        if (cpos >= 0) {
          int ccolon = obj.indexOf(':', cpos);
          int cq1 = obj.indexOf('"', ccolon + 1);
          int cq2 = obj.indexOf('"', cq1 + 1);
          if (cq1 > 0 && cq2 > cq1) {
            String cmd = obj.substring(cq1 + 1, cq2);
            cmd.trim();
            if (cmd.length() && cmdsCount < 64) { cmdsList[cmdsCount++] = cmd; }
          }
        }
      }

      if (cmdsCount > 0) {
        // Extract automation name for logging
        String autoName = "Unknown";
        int namePos = obj.indexOf("\"name\"");
        if (namePos >= 0) {
          int colonPos = obj.indexOf(':', namePos);
          if (colonPos >= 0) {
            int q1 = obj.indexOf('"', colonPos + 1);
            int q2 = obj.indexOf('"', q1 + 1);
            if (q1 >= 0 && q2 >= 0) {
              autoName = obj.substring(q1 + 1, q2);
            }
          }
        }

        // Check conditions if present
        String conditions = "";
        int condPos = obj.indexOf("\"conditions\"");
        if (condPos >= 0) {
          int condColon = obj.indexOf(':', condPos);
          if (condColon >= 0) {
            int condQ1 = obj.indexOf('"', condColon + 1);
            int condQ2 = obj.indexOf('"', condQ1 + 1);
            if (condQ1 >= 0 && condQ2 >= 0) {
              conditions = obj.substring(condQ1 + 1, condQ2);
              conditions.trim();
            }
          }
        }
        
        // Evaluate conditions if present
        if (conditions.length() > 0) {
          // Check if this is a conditional chain (contains ELSE/ELSE IF)
          if (conditions.indexOf("ELSE") >= 0) {
            String actionToExecute = evaluateConditionalChain(conditions);
            DEBUGF(DEBUG_AUTOMATIONS, "[autos] id=%ld conditional chain result: '%s'", 
                   id, actionToExecute.c_str());
            
            if (actionToExecute.length() > 0) {
              // Execute the specific action from the chain
              String result = executeConditionalCommand(actionToExecute);
              if (result.startsWith("Error:")) {
                DEBUGF(DEBUG_AUTOMATIONS, "[autos] conditional chain error: %s", result.c_str());
              }
            }
            continue; // Skip normal command execution
          } else {
            // Simple IF/THEN condition
            bool conditionMet = evaluateCondition(conditions);
            DEBUGF(DEBUG_AUTOMATIONS, "[autos] id=%ld condition='%s' result=%s", 
                   id, conditions.c_str(), conditionMet ? "TRUE" : "FALSE");
            
            if (!conditionMet) {
              if (gAutoLogActive) {
                String skipMsg = "Scheduled automation skipped: ID=" + String(id) + " Name=" + autoName + " Condition not met: " + conditions;
                appendAutoLogEntry("AUTO_SKIP", skipMsg);
              }
              DEBUGF(DEBUG_AUTOMATIONS, "[autos] id=%ld skipped - condition not met: %s", id, conditions.c_str());
              continue; // Skip to next automation
            }
          }
        }

        // Log scheduled automation start if logging is active
        if (gAutoLogActive) {
          gAutoLogAutomationName = autoName;
          String startMsg = "Scheduled automation started: ID=" + String(id) + " Name=" + autoName + " User=system";
          appendAutoLogEntry("AUTO_START", startMsg);
        }

        // Execute commands (with conditional logic support)
        for (int ci = 0; ci < cmdsCount; ++ci) {
          DEBUGF(DEBUG_AUTOMATIONS, "[autos] id=%ld run cmd[%d]='%s'", id, ci, cmdsList[ci].c_str());
          
          // Execute command with conditional logic support
          String result = executeConditionalCommand(cmdsList[ci]);
          if (result.startsWith("Error:")) {
            DEBUGF(DEBUG_AUTOMATIONS, "[autos] id=%ld cmd[%d] error: %s", id, ci, result.c_str());
          }
        }
        executed++;

        // Log scheduled automation end if logging is active
        if (gAutoLogActive) {
          String endMsg = "Scheduled automation completed: ID=" + String(id) + " Name=" + autoName + " Commands=" + String(cmdsCount);
          appendAutoLogEntry("AUTO_END", endMsg);
        }

        // Compute and update next run time
        time_t newNextAt = computeNextRunTime(obj, now);
        if (newNextAt > 0) {
          updateAutomationNextAt(id, newNextAt);
          DEBUGF(DEBUG_AUTOMATIONS, "[autos] id=%ld updated nextAt=%lu", id, (unsigned long)newNextAt);
        } else {
          DEBUGF(DEBUG_AUTOMATIONS, "[autos] id=%ld warning: could not compute next nextAt", id);
        }
      } else {
        DEBUGF(DEBUG_AUTOMATIONS, "[autos] id=%ld skip: no commands found", id);
      }
    } else {
      DEBUGF(DEBUG_AUTOMATIONS, "[autos] id=%ld wait: nextAt=%lu now=%lu", id, (unsigned long)nextAt, (unsigned long)now);
    }

    pos = braceEnd + 1;
  }

  DEBUGF(DEBUG_AUTOMATIONS, "[autos] evaluated=%d executed=%d", evaluated, executed);

  // Handle duplicate sanitization
  static unsigned long s_lastAutoSanitizeMs = 0;
  if (queueSanitize) {
    unsigned long nowMs = millis();
    if (nowMs - s_lastAutoSanitizeMs > 5000UL) {
      String fix;
      if (readText("/automations.json", fix)) {
        if (sanitizeAutomationsJson(fix)) {
          writeAutomationsJsonAtomic(fix);
          gAutosDirty = true;
          DEBUGF(DEBUG_AUTOMATIONS, "[autos] Runtime sanitize applied after duplicate detection; scheduler refresh queued");
        } else {
          DEBUGF(DEBUG_AUTOMATIONS, "[autos] Runtime sanitize: no changes needed");
        }
      }
      s_lastAutoSanitizeMs = nowMs;
    } else {
      DEBUGF(DEBUG_AUTOMATIONS, "[autos] Runtime sanitize skipped (debounced)");
    }
  }
}

// Forward declaration - implementation after LOG_* constants are defined
void logTimeSyncedMarkerIfReady();

// Returns a cached, ms-precision prefix like "[YYYY-MM-DD HH:MM:SS.mmm] | ".
// Writes empty string if epoch time invalid.
static void getTimestampPrefixMsCached(char* out, size_t outSize) {
  if (!out || outSize == 0) return;
  out[0] = '\0';
  // Lazy-init: if offset unknown, try to compute from current time()
  if (gBootEpochUsOffset == 0) {
    timeSyncUpdateBootEpoch();
  }
  int64_t epochUs = 0;
  if (gBootEpochUsOffset != 0) {
    epochUs = gBootEpochUsOffset + (int64_t)esp_timer_get_time();
  }
  if (epochUs <= 0) return;  // no valid time

  static time_t s_cachedSec = 0;
  static char* s_base = nullptr;  // "[YYYY-MM-DD HH:MM:SS"
  static bool s_baseValid = false;

  if (!s_base) {
    s_base = (char*)ps_alloc(24, AllocPref::PreferPSRAM, "ts.base");
    if (!s_base) return;
  }

  time_t sec = (time_t)(epochUs / 1000000LL);
  int ms = (int)((epochUs / 1000LL) % 1000LL);
  if (sec != s_cachedSec) {
    s_cachedSec = sec;
    struct tm tminfo;
    if (localtime_r(&sec, &tminfo)) {
      strftime(s_base, 24, "[%Y-%m-%d %H:%M:%S", &tminfo);
      s_baseValid = true;
    } else {
      s_baseValid = false;
    }
  }
  if (!s_baseValid) return;

  snprintf(out, outSize, "%s.%03d] | ", s_base, ms);
}

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
// Output/logging used widely before definition

// Global variables forward declarations
extern bool filesystemReady;

// Centralized command execution with AuthContext (transport-agnostic)
static const char* originFrom(const AuthContext& ctx);
static bool hasAdminPrivilege(const AuthContext& ctx);
static bool isAdminOnlyCommand(const String& cmd);
static String redactCmdForAudit(const String& cmd);

// Global flag to track automation execution context
static bool gInAutomationContext = false;


// -------- Command Executor Task (decl) --------
// Forward declaration of request struct (full definition after CommandContext)
struct ExecReq;
// Queue holding pointers to ExecReq
static QueueHandle_t gCmdExecQ = nullptr;
// Forward declaration of task
static void commandExecTask(void* pv);
static bool executeCommand(AuthContext& ctx, const String& cmd, String& out);
void broadcastOutput(const String& s);
// Auth/session helpers used early
static bool isAuthed(httpd_req_t* req, String& outUser);
void logAuthAttempt(bool success, const char* path, const String& userTried, const String& ip, const String& reason);
// Network helper to get client IP
static void getClientIP(httpd_req_t* req, String& outIP);
// SSE helpers (implemented below)

// ---------- Command Executor Task implementation moved below ExecReq definition ----------
static bool sseWrite(httpd_req_t* req, const char* chunk);
static int sseBindSession(httpd_req_t* req, String& outSid);

// ---------------------------------------------------------------------------
// Logging file constants and serial auth globals (must be before auth helpers)
// ---------------------------------------------------------------------------
// Logs (LittleFS)
static const char* LOG_OK_FILE = "/logs/successful_login.txt";              // ~680KB cap
static const char* LOG_FAIL_FILE = "/logs/failed_login.txt";                // ~680KB cap
static const char* LOG_ALLOC_FILE = "/logs/memory_allocation_history.txt";  // 64KB cap via ALLOC_LOG_CAP
static const size_t LOG_CAP_BYTES = 696969;                                 // ~680 KB

// Serial CLI authentication (session-only)
static bool gSerialAuthed = false;
static String gSerialUser = String();
// gSerialIsAdmin removed - now using real-time isAdminUser() checks

// ESP-NOW state
static bool gEspNowInitialized = false;
static uint8_t gEspNowChannel = 1;

// ESP-NOW chunked message support
#define MAX_CHUNKS 10
#define CHUNK_SIZE 200

struct ChunkedMessage {
  String hash;
  String status;
  String deviceName;
  int totalChunks;
  int receivedChunks;
  String chunks[MAX_CHUNKS];
  unsigned long startTime;
  bool active;
};

static ChunkedMessage gActiveMessage;

// ESP-NOW encryption support
static String gEspNowPassphrase = "";
static uint8_t gEspNowDerivedKey[16] = {0};
static bool gEspNowEncryptionEnabled = false;

// ESP-NOW device name mapping
struct EspNowDevice {
  uint8_t mac[6];
  String name;
  bool encrypted;        // Whether this device uses encryption
  uint8_t key[16];      // Per-device encryption key
};
static EspNowDevice gEspNowDevices[16]; // Support up to 16 paired devices
static int gEspNowDeviceCount = 0;

// ---------------------------------------------------------------------------
// Transport-agnostic auth context and guards (HTTP + Serial today; TFT later)
// ---------------------------------------------------------------------------
enum AuthTransport { AUTH_HTTP = 0,
                     AUTH_SERIAL = 1,
                     AUTH_TFT = 2,
                     AUTH_SYSTEM = 3 };

struct AuthContext {
  AuthTransport transport;
  String path;   // URI for HTTP, command for CLI, view for TFT
  String ip;     // remote IP for HTTP, "local" for serial/TFT
  String user;   // resolved username when authenticated
  String sid;    // HTTP session id (empty for serial/TFT)
  void* opaque;  // httpd_req_t* when HTTP; nullptr otherwise
};

extern "C" void __attribute__((weak)) authSuccessDebug(const char* user,
                                                       const char* ip,
                                                       const char* path,
                                                       const char* sid,
                                                       const char* redirect,
                                                       bool reusedSession) {
  // Weak hook: default no-op
}

// Require auth across transports. Returns true if authenticated; otherwise emits the
// appropriate denial (401/console note) and returns false.
static bool tgRequireAuth(AuthContext& ctx) {
  if (ctx.transport == AUTH_HTTP) {
    httpd_req_t* req = reinterpret_cast<httpd_req_t*>(ctx.opaque);
    if (!req) return false;
    // Prefer cached auth for high-frequency endpoints
    String userTmp;
    bool ok = isAuthed(req, userTmp);
    if (!ok) {
      sendAuthRequiredResponse(req);
      return false;
    }
    ctx.user = userTmp;
    if (ctx.ip.length() == 0) { getClientIP(req, ctx.ip); }
    return true;
  } else if (ctx.transport == AUTH_SERIAL) {
    // Serial console auth state
    if (!gSerialAuthed) {
      Serial.println("ERROR: auth required");
      return false;
    }
    ctx.user = gSerialUser;
    if (ctx.ip.length() == 0) ctx.ip = "local";
    return true;
  } else {  // AUTH_TFT (future)
    // Not implemented yet; always require explicit wiring when TFT lands
    return false;
  }
}

// Admin check across transports; for HTTP, send 403 on failure.
static bool tgRequireAdmin(AuthContext& ctx) {
  if (!tgRequireAuth(ctx)) return false;
  if (ctx.transport == AUTH_HTTP) {
    if (!isAdminUser(ctx.user)) {
      httpd_req_t* req = reinterpret_cast<httpd_req_t*>(ctx.opaque);
      if (req) {
        httpd_resp_set_status(req, "403 Forbidden");
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_send(req, "Forbidden: admin required", HTTPD_RESP_USE_STRLEN);
      }
      return false;
    }
    return true;
  } else if (ctx.transport == AUTH_SERIAL) {
    if (!isAdminUser(ctx.user)) {
      Serial.println("ERROR: admin required");
      return false;
    }
    return true;
  } else {  // AUTH_TFT (future)
    // Not implemented; deny by default
    return false;
  }
}

// Unified successful authentication logger + post-auth flow per transport.
static esp_err_t authSuccessUnified(AuthContext& ctx, const char* redirectTo) {
  // Timestamp prefix with ms precision
  char tsPrefix[40];
  getTimestampPrefixMsCached(tsPrefix, sizeof(tsPrefix));
  String prefix = tsPrefix[0] ? String(tsPrefix) : String("[BOOT ms=") + String(millis()) + "] | ";

  bool reused = false;
  String sidShort;

  if (ctx.transport == AUTH_HTTP) {
    httpd_req_t* req = reinterpret_cast<httpd_req_t*>(ctx.opaque);
    if (req) {
      // Create or reuse session (Safari-safe cookie path inside setSession)
      String sid = setSession(req, ctx.user);
      ctx.sid = sid;
      if (sid.length() > 8) sidShort = sid.substring(0, 8) + "...";
      else sidShort = sid;
      // Heuristic: if session exists for same user+IP, setSession reuses; we mark reused
      // We can't cheaply detect here without additional API; leave 'reused' default false.
      if (ctx.ip.length() == 0) getClientIP(req, ctx.ip);
    }
  } else if (ctx.transport == AUTH_SERIAL) {
    // Establish serial-side session state
    gSerialAuthed = true;
    if (ctx.user.length()) gSerialUser = ctx.user;  // keep provided name
    if (!gSerialUser.length()) gSerialUser = "serial";
    // Admin decision left to existing logic; do not forcibly elevate here
    sidShort = "serial";
    if (ctx.ip.length() == 0) ctx.ip = "local";
  } else {  // AUTH_TFT (future)
    // No TFT session state yet
    sidShort = "tft";
    if (ctx.ip.length() == 0) ctx.ip = "local";
  }

  // Log unified success entry
  String line = prefix + String("ms=") + String(millis()) + " event=auth_success user=" + (ctx.user.length() ? ctx.user : String("<unknown>")) + " ip=" + (ctx.ip.length() ? ctx.ip : String("<none>")) + " path=" + (ctx.path.length() ? ctx.path : String("<none>")) + " sid=" + (sidShort.length() ? sidShort : String("<none>")) + " transport=" + (ctx.transport == AUTH_HTTP ? "http" : (ctx.transport == AUTH_SERIAL ? "serial" : "tft")) + " reused=" + (reused ? "1" : "0") + " redirect=" + (redirectTo ? String(redirectTo) : String("<none>"));
  appendLineWithCap(LOG_OK_FILE, line, LOG_CAP_BYTES);

  // Weak hook for external instrumentation
  authSuccessDebug(ctx.user.c_str(), ctx.ip.c_str(), ctx.path.c_str(), ctx.sid.c_str(), redirectTo ? redirectTo : "", reused);

  // Emit transport-specific success UX
  if (ctx.transport == AUTH_HTTP) {
    httpd_req_t* req = reinterpret_cast<httpd_req_t*>(ctx.opaque);
    if (!req) return ESP_FAIL;
    // Use existing success page logic (web_login_success.h content), with meta refresh
    httpd_resp_set_type(req, "text/html");
    // Existing API expects session ID; redirect handled inside the page implementation
    String html = getLoginSuccessPage(ctx.sid);
    httpd_resp_send(req, html.c_str(), HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  } else if (ctx.transport == AUTH_SERIAL) {
    Serial.println("OK: logged in");
    return ESP_OK;
  } else {  // AUTH_TFT (future)
    // No-op until TFT UI exists
    return ESP_OK;
  }
}
static bool sseSendLogs(httpd_req_t* req, unsigned long seq, const String& buf);
static bool sseSessionAliveAndRefresh(int sessIdx, const String& sid);
static bool sseHeartbeat(httpd_req_t* req);
static bool sseSendNotice(httpd_req_t* req, const String& note);

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
static const char* SETTINGS_FILE = "/settings.txt";              // key=value
static const char* SETTINGS_JSON_FILE = "/settings.json";        // unified settings JSON
static const char* USERS_FILE = "/users.txt";                    // legacy: username:password[:role]
static const char* USERS_JSON_FILE = "/users.json";              // preferred: {"users":[{username,password,role}]}
static const char* AUTOMATIONS_JSON_FILE = "/automations.json";  // {"version":1,"automations":[]}
static const char* ESPNOW_DEVICES_FILE = "/espnow_devices.json"; // ESP-NOW paired devices

// SSE helpers
// ==========================


// SSE endpoint: push per-session notices without polling
esp_err_t handleEvents(httpd_req_t* req);
// Sensors status endpoint
esp_err_t handleSensorsStatus(httpd_req_t* req);
// Logs (LittleFS)
// moved earlier

// Log a one-time marker when NTP/RTC becomes valid; safe to call anytime.
void logTimeSyncedMarkerIfReady() {
  if (gTimeSyncedMarkerWritten) return;
  time_t t = time(nullptr);
  if (t <= 0) return;
  timeSyncUpdateBootEpoch();
  static char* bootTsPrefix = nullptr;
  if (!bootTsPrefix) {
    bootTsPrefix = (char*)ps_alloc(48, AllocPref::PreferPSRAM, "boot.ts");
    if (!bootTsPrefix) return;
  }
  getTimestampPrefixMsCached(bootTsPrefix, 48);
  String prefix = bootTsPrefix[0] ? String(bootTsPrefix) : String("[BOOT ms=") + String(millis()) + "] | ";
  String line = prefix + "Device Powered On | Time Synced via NTP";
  appendLineWithCap(LOG_OK_FILE, line, LOG_CAP_BYTES);
  appendLineWithCap(LOG_FAIL_FILE, line, LOG_CAP_BYTES);
  appendLineWithCap(LOG_ALLOC_FILE, line, LOG_CAP_BYTES);
  gTimeSyncedMarkerWritten = true;
}

// Sensor objects and state variables
Adafruit_seesaw ss;
Adafruit_BNO055* bno = nullptr;
Adafruit_APDS9960* apds = nullptr;
VL53L4CX* tofSensor = nullptr;
Adafruit_MLX90640* thermalSensor = nullptr;

// Sensor connection state
bool gamepadConnected = false;
bool imuEnabled = false;
bool imuConnected = false;
bool tofConnected = false;
bool tofEnabled = false;
bool apdsConnected = false;
bool rgbgestureConnected = false;
bool thermalConnected = false;
bool thermalEnabled = false;

// ---- I2C clock management (Wire1) ----
// Centralize Wire1 clock policy: 100kHz default, temporarily override for specific operations
static uint32_t gWire1DefaultHz = 100000;  // safe default for mixed sensors
static uint32_t gWire1CurrentHz = 0;
static inline void i2cSetWire1Clock(uint32_t hz) {
  if (gWire1CurrentHz != hz) {
    Wire1.setClock(hz);
    gWire1CurrentHz = hz;
    DEBUG_CLIF("[I2C] Wire1 clock -> %lu Hz", (unsigned long)hz);
  }
}
static inline void i2cSetDefaultWire1Clock() {
  i2cSetWire1Clock(gWire1DefaultHz);
}

// Stack-based scope guard so out-of-order destruction still restores to the last active target
static const int kI2CClockStackMax = 8;
static uint32_t* gI2CClockStack = nullptr;
static int gI2CClockStackDepth = 0;

static inline void i2cClockStackPush(uint32_t hz) {
  if (!gI2CClockStack) {
    Serial.println("[I2C] ERROR: clock stack not initialized!");
    return;
  }
  if (gI2CClockStackDepth < kI2CClockStackMax) {
    gI2CClockStack[gI2CClockStackDepth++] = hz;
  } else {
    broadcastOutput("[I2C] WARN: clock stack overflow");
  }
}
static inline void i2cClockStackPop() {
  if (!gI2CClockStack) {
    Serial.println("[I2C] ERROR: clock stack not initialized!");
    return;
  }
  if (gI2CClockStackDepth > 0) {
    gI2CClockStackDepth--;
  } else {
    broadcastOutput("[I2C] WARN: clock stack underflow");
  }
}
static inline uint32_t i2cClockStackTopOrDefault() {
  if (!gI2CClockStack) {
    Serial.println("[I2C] ERROR: clock stack not initialized!");
    return gWire1DefaultHz;
  }
  return (gI2CClockStackDepth > 0) ? gI2CClockStack[gI2CClockStackDepth - 1] : gWire1DefaultHz;
}

struct Wire1ClockScope {
  uint32_t target;
  explicit Wire1ClockScope(uint32_t hz)
    : target(hz) {
    // Only log when actually changing clock speed
    if (hz != gWire1CurrentHz) {
      // broadcastOutput(String("[I2C] enter scope: target=") + String(hz) + String(" Hz, prev=") + String(gWire1CurrentHz));
    }
    i2cClockStackPush(hz);
    i2cSetWire1Clock(hz);
  }
  ~Wire1ClockScope() {
    i2cClockStackPop();
    uint32_t restore = i2cClockStackTopOrDefault();
    // Only log when actually changing clock speed
    if (restore != gWire1CurrentHz) {
      // broadcastOutput(String("[I2C] exit scope: restore=") + String(restore) + String(" Hz"));
    }
    i2cSetWire1Clock(restore);
  }
};

// MLX90640 thermal sensor
bool mlx90640_initialized = false;
volatile bool thermalPendingFirstFrame = false;  // defer status broadcast until first frame
volatile uint32_t thermalArmAtMs = 0;            // skip capture until this time (post-enable)
// New: initialization handoff to thermal task
static volatile bool thermalInitRequested = false;
static volatile bool thermalInitDone = false;
static volatile bool thermalInitResult = false;
// New: initialization handoff to IMU task
static volatile bool imuInitRequested = false;
static volatile bool imuInitDone = false;
static volatile bool imuInitResult = false;
// (unified task removed)

// Stack watermark diagnostics (words). Updated by each task.
static volatile UBaseType_t gToFWatermarkMin = (UBaseType_t)0xFFFFFFFF;
static volatile UBaseType_t gToFWatermarkNow = (UBaseType_t)0;
static volatile UBaseType_t gIMUWatermarkMin = (UBaseType_t)0xFFFFFFFF;
static volatile UBaseType_t gIMUWatermarkNow = (UBaseType_t)0;
static volatile UBaseType_t gThermalWatermarkMin = (UBaseType_t)0xFFFFFFFF;
static volatile UBaseType_t gThermalWatermarkNow = (UBaseType_t)0;

// Sensor data cache structure with thread safety
struct SensorDataCache {
  // Thread safety
  SemaphoreHandle_t mutex = nullptr;

  // Thermal sensor data
  float* thermalFrame = nullptr;
  float thermalMinTemp = 0.0;
  float thermalMaxTemp = 0.0;
  float thermalAvgTemp = 0.0;
  unsigned long thermalLastUpdate = 0;
  bool thermalDataValid = false;
  uint32_t thermalSeq = 0;  // sequence number for change detection

  // IMU data
  float accelX = 0.0, accelY = 0.0, accelZ = 0.0;
  float gyroX = 0.0, gyroY = 0.0, gyroZ = 0.0;
  float imuTemp = 0.0;
  // Orientation (Euler angles, degrees)
  float oriYaw = 0.0, oriPitch = 0.0, oriRoll = 0.0;
  unsigned long imuLastUpdate = 0;
  bool imuDataValid = false;
  uint32_t imuSeq = 0;  // sequence number for change detection

  // ToF data
  unsigned long tofLastUpdate = 0;
  bool tofDataValid = false;
  uint32_t tofSeq = 0;  // sequence number for change detection

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
const unsigned long MLX90640_READ_INTERVAL = 62;  // 16 Hz

// Helper functions for thread-safe sensor cache access
static bool lockSensorCache(TickType_t timeout = portMAX_DELAY) {
  return gSensorCache.mutex && (xSemaphoreTake(gSensorCache.mutex, timeout) == pdTRUE);
}

static void unlockSensorCache() {
  if (gSensorCache.mutex) {
    xSemaphoreGive(gSensorCache.mutex);
  }
}

// APDS9960 settings
bool apdsColorEnabled = false;
bool apdsProximityEnabled = false;
bool apdsGestureEnabled = false;

// Global sensor-status sequence for SSE fanout
static volatile uint32_t gSensorStatusSeq = 1;
// Forward declaration for SSE broadcast
void broadcastSensorStatusToAllSessions();
// Index of a session to skip when flagging updates (set around command handling)
static volatile int gBroadcastSkipSessionIdx = -1;
// Last known cause for a sensor status bump (for diagnostics)
static String gLastStatusCause = "";
// Debounced SSE broadcast state
static volatile bool gSensorStatusDirty = false;
static volatile unsigned long gNextSensorStatusBroadcastDue = 0;
static const unsigned long kSensorStatusDebounceMs = 150;  // 100–200ms window

static inline void sensorStatusBump() {
  uint32_t s = gSensorStatusSeq + 1;
  if (s == 0) s = 1;
  gSensorStatusSeq = s;
  DEBUG_SSEF("sensorStatusBump: seq now %d | cause=%s (debounced)", gSensorStatusSeq, gLastStatusCause.c_str());
  // Mark dirty and schedule debounced broadcast
  gSensorStatusDirty = true;
  unsigned long nowMs = millis();
  if (gNextSensorStatusBroadcastDue == 0 || (long)(nowMs - gNextSensorStatusBroadcastDue) > 0) {
    gNextSensorStatusBroadcastDue = nowMs + kSensorStatusDebounceMs;
  }
}

// Per-sensor task handles and shared I2C gate
TaskHandle_t tofTaskHandle = nullptr;
TaskHandle_t imuTaskHandle = nullptr;
TaskHandle_t thermalTaskHandle = nullptr;
SemaphoreHandle_t i2cMutex = nullptr;

// Helper: set cause then bump (to preserve existing call-sites)
static inline void sensorStatusBumpWith(const char* cause) {
  gLastStatusCause = cause ? String(cause) : String("");
  sensorStatusBump();
}

static String buildSensorStatusJson() {
  String j = "{";
  j += "\"seq\":" + String(gSensorStatusSeq);
  j += ",\"thermalEnabled\":" + String(thermalEnabled ? 1 : 0);
  j += ",\"tofEnabled\":" + String(tofEnabled ? 1 : 0);
  j += ",\"imuEnabled\":" + String(imuEnabled ? 1 : 0);
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

// ----------------------------------------------------------------------------
// Color System Implementation
// ----------------------------------------------------------------------------

bool getRGBFromName(String name, RGB& rgbOut) {
  name.toLowerCase();
  for (int i = 0; i < numColors; i++) {
    ColorEntry entry;
    memcpy_P(&entry, &colorTable[i], sizeof(ColorEntry));
    if (name == String(entry.name)) {
      rgbOut = entry.rgb;
      return true;
    }
  }
  return false;
}

RGB blendColors(RGB a, RGB b, float ratio) {
  // Clamp ratio between 0.0 and 1.0
  if (ratio < 0.0) ratio = 0.0;
  if (ratio > 1.0) ratio = 1.0;

  return {
    (uint8_t)(a.r * (1.0 - ratio) + b.r * ratio),
    (uint8_t)(a.g * (1.0 - ratio) + b.g * ratio),
    (uint8_t)(a.b * (1.0 - ratio) + b.b * ratio)
  };
}

RGB adjustBrightness(RGB color, float brightness) {
  // Clamp brightness between 0.0 and 1.0
  if (brightness < 0.0) brightness = 0.0;
  if (brightness > 1.0) brightness = 1.0;

  return {
    (uint8_t)(color.r * brightness),
    (uint8_t)(color.g * brightness),
    (uint8_t)(color.b * brightness)
  };
}

RGB rainbowColor(int step, int maxSteps) {
  float hue = (float)step / maxSteps * 360.0;

  // Convert HSV to RGB (simplified)
  float c = 1.0;  // Saturation = 1, Value = 1
  float x = c * (1.0 - abs(fmod(hue / 60.0, 2.0) - 1.0));
  float m = 0.0;

  float r, g, b;
  if (hue < 60) {
    r = c;
    g = x;
    b = 0;
  } else if (hue < 120) {
    r = x;
    g = c;
    b = 0;
  } else if (hue < 180) {
    r = 0;
    g = c;
    b = x;
  } else if (hue < 240) {
    r = 0;
    g = x;
    b = c;
  } else if (hue < 300) {
    r = x;
    g = 0;
    b = c;
  } else {
    r = c;
    g = 0;
    b = x;
  }

  return {
    (uint8_t)((r + m) * 255),
    (uint8_t)((g + m) * 255),
    (uint8_t)((b + m) * 255)
  };
}

// Find closest color match from color table using RGB distance
String getClosestColorName(uint16_t r, uint16_t g, uint16_t b, RGB& closestRGB) {
  float minDistance = 999999;
  int closestIndex = 0;

  // Scale down 16-bit sensor values to 8-bit for comparison
  uint8_t r8 = (r > 255) ? 255 : r;
  uint8_t g8 = (g > 255) ? 255 : g;
  uint8_t b8 = (b > 255) ? 255 : b;

  for (int i = 0; i < numColors; i++) {
    ColorEntry entry;
    memcpy_P(&entry, &colorTable[i], sizeof(ColorEntry));

    // Calculate Euclidean distance in RGB space
    float dr = r8 - entry.rgb.r;
    float dg = g8 - entry.rgb.g;
    float db = b8 - entry.rgb.b;
    float distance = sqrt(dr * dr + dg * dg + db * db);

    if (distance < minDistance) {
      minDistance = distance;
      closestIndex = i;
    }
  }

  // Read the closest match from PROGMEM
  ColorEntry closestEntry;
  memcpy_P(&closestEntry, &colorTable[closestIndex], sizeof(ColorEntry));
  closestRGB = closestEntry.rgb;
  return String(closestEntry.name);
}

void setLEDColor(RGB color) {
  pixels.setPixelColor(0, pixels.Color(color.r, color.g, color.b));
  pixels.show();
}

void runLEDEffect(int effectType, RGB startColor, RGB endColor, unsigned long duration) {
  broadcastOutput("✨ Running LED effect " + String(effectType) + " for " + String(duration) + "ms");

  unsigned long startTime = millis();
  int maxSteps = duration / 50;  // Update every 50ms

  for (int step = 0; step <= maxSteps; step++) {
    unsigned long elapsed = millis() - startTime;

    if (elapsed >= duration) break;

    float progress = (float)elapsed / duration;
    RGB currentColor;

    switch (effectType) {
      case EFFECT_FADE:
        currentColor = blendColors(startColor, endColor, progress);
        break;

      case EFFECT_PULSE:
        {
          float pulseValue = (sin(progress * 2 * PI * 3) + 1.0) / 2.0;  // 3 pulses over duration
          currentColor = adjustBrightness(startColor, pulseValue);
          break;
        }

      case EFFECT_RAINBOW:
        currentColor = rainbowColor((int)(progress * 360), 360);
        break;

      case EFFECT_BREATHE:
        {
          float breatheValue = (sin(progress * 2 * PI * 2 - PI / 2) + 1.0) / 2.0;  // 2 breaths over duration
          currentColor = adjustBrightness(startColor, breatheValue);
          break;
        }

      default:
        currentColor = startColor;
        break;
    }

    setLEDColor(currentColor);
    delay(50);  // 50ms between updates

    // Check for serial input to allow interruption
    if (Serial.available()) {
      broadcastOutput("⏹️ LED effect interrupted by new command");
      break;
    }
  }

  // Turn off LED when effect completes
  setLEDColor({ 0, 0, 0 });
  broadcastOutput("✅ LED effect completed");
}

// Legacy auth defaults (still used by loadUsersFromFile)
static String DEFAULT_AUTH_USER = "admin";
static String DEFAULT_AUTH_PASS = "admin";

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

// Global CLI state
CLIState gCLIState = CLI_NORMAL;
String gHiddenHistory = "";
bool gShowAllCommands = false;

// Unified output buffer stored in PSRAM via mem_util.h
struct WebMirrorBuf {
  char* buf;
  size_t cap;  // maximum bytes stored (excluding null)
  size_t len;  // current length
  WebMirrorBuf()
    : buf(nullptr), cap(0), len(0) {}
  void init(size_t capacity) {
    cap = capacity;
    len = 0;
    buf = (char*)ps_alloc(cap + 1, AllocPref::PreferPSRAM, "gWebMirror.buf");
    if (buf) buf[0] = '\0';
  }
  void clear() {
    len = 0;
    if (buf) buf[0] = '\0';
  }
  // Append string s; if needNewline is true and len>0, prepend a '\n'
  void append(const String& s, bool needNewline) {
    if (!buf || cap == 0) return;
    size_t addNL = (needNewline && len > 0) ? 1 : 0;
    size_t slen = s.length();
    size_t need = addNL + slen;
    // If overflow, drop oldest to keep only last cap bytes
    if (need >= cap) {
      // Only last portion of s will remain
      const char* src = s.c_str() + (slen - (cap - 1));
      memcpy(buf, src, cap - 1);
      len = cap - 1;
      buf[len] = '\0';
      return;
    }
    // Ensure space by trimming from front if required
    while (len + need > cap) {
      // remove up to and including first '\n' or at least 1 char
      int nl = -1;
      for (size_t i = 0; i < len; ++i) {
        if (buf[i] == '\n') {
          nl = (int)i;
          break;
        }
      }
      size_t drop = (nl >= 0 ? (size_t)(nl + 1) : (size_t)1);
      memmove(buf, buf + drop, len - drop);
      len -= drop;
      buf[len] = '\0';
    }
    if (addNL) { buf[len++] = '\n'; }
    memcpy(buf + len, s.c_str(), slen);
    len += slen;
    buf[len] = '\0';
  }
  void assignFrom(const String& s) {
    if (!buf || cap == 0) return;
    size_t sl = s.length();
    if (sl >= cap) {
      memcpy(buf, s.c_str() + (sl - (cap - 1)), cap - 1);
      len = cap - 1;
      buf[len] = '\0';
    } else {
      memcpy(buf, s.c_str(), sl);
      len = sl;
      buf[len] = '\0';
    }
  }
  String snapshot() const {
    if (!buf) return String("");
    return String(buf);
  }
} gWebMirror;

static size_t gWebMirrorCap = 8192;               // adjustable capacity
static volatile unsigned long gWebMirrorSeq = 0;  // increments on each append
static String gLastTFTLine;                       // last rendered line for TFT

// Execution context for CLI admin gating (set for web CLI requests)
static bool gExecFromWeb = false;
static String gExecUser = "";
static bool gExecIsAdmin = false;
// Feature capability flags from unified pipeline (default true; can be refined later)
static bool gCapAdminControls = true;
static bool gCapSensorConfig = true;

// requireAdminFor() function removed - admin checks now unified in executeCommand() pipeline

// Runtime settings
String gAuthUser = DEFAULT_AUTH_USER;
String gAuthPass = DEFAULT_AUTH_PASS;
// Settings structure
struct Settings {
  uint16_t version;
  String wifiSSID;
  String wifiPassword;
  bool wifiAutoReconnect;
  int cliHistorySize;
  String ntpServer;
  int tzOffsetMinutes;
  bool outSerial;  // persist output lanes
  bool outWeb;
  bool outTft;
  // Sensors UI (non-advanced)
  int thermalPollingMs;
  int tofPollingMs;
  int tofStabilityThreshold;
  String thermalPaletteDefault;
  // Thermal interpolation settings
  bool thermalInterpolationEnabled;
  int thermalInterpolationSteps;
  int thermalInterpolationBufferSize;
  int thermalWebClientQuality;  // 1x, 2x, 4x, 8x, 16x scaling
  // Advanced UI + firmware-affecting
  float thermalEWMAFactor;
  int thermalTransitionMs;
  int tofTransitionMs;
  int tofUiMaxDistanceMm;
  int i2cClockThermalHz;
  int i2cClockToFHz;
  int thermalTargetFps;
  int thermalWebMaxFps;
  // Device-side sensor settings (affect firmware runtime)
  int thermalDevicePollMs;
  int tofDevicePollMs;
  int imuDevicePollMs;
  // Debug settings
  bool debugAuthCookies;
  bool debugHttp;
  bool debugSse;
  bool debugCli;
  bool debugSensorsFrame;
  bool debugSensorsData;
  bool debugSensorsGeneral;
  bool debugWifi;
  bool debugStorage;
  bool debugPerformance;
  bool debugDateTime;
  bool debugCommandFlow;
  bool debugUsers;
  // ESP-NOW settings
  bool espnowenabled;
};
Settings gSettings;

static void tofTask(void* parameter);
static void imuTask(void* parameter);
static void thermalTask(void* parameter);

// Unified sensor polling task removed; per-sensor tasks are defined below.

// ------------------------------
// Per-sensor dedicated tasks (defined after Settings)
// ------------------------------

static void tofTask(void* parameter) {
  if (gDebugFlags & DEBUG_SENSORS_FRAME) {
    Serial.println("[DEBUG_SENSORS_FRAME] ToF task started");
  }
  unsigned long lastToFRead = 0;
  unsigned long lastStackLog = 0;
  while (true) {
    // Update watermark diagnostics
    UBaseType_t wm = uxTaskGetStackHighWaterMark(NULL);
    gToFWatermarkNow = wm;
    if (wm < gToFWatermarkMin) gToFWatermarkMin = wm;
    unsigned long nowLog = millis();
    if (nowLog - lastStackLog >= 5000UL) {
      lastStackLog = nowLog;
      DEBUG_PERFORMANCEF("[STACK] tof_task watermark_now=%u min=%u words", (unsigned)gToFWatermarkNow, (unsigned)gToFWatermarkMin);
    }
    if (tofEnabled && tofConnected && tofSensor != nullptr) {
      unsigned long tofPollMs = (gSettings.tofDevicePollMs > 0) ? (unsigned long)gSettings.tofDevicePollMs : 100;
      unsigned long nowMs = millis();
      if (nowMs - lastToFRead >= tofPollMs) {
        if (gDebugFlags & DEBUG_SENSORS_FRAME) {
          Serial.println("[DEBUG_SENSORS_FRAME] [ToF task] Calling readToFObjects()");
        }
        if (i2cMutex) xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(300));
        bool ok = readToFObjects();
        if (i2cMutex) xSemaphoreGive(i2cMutex);
        lastToFRead = nowMs;
        if (gDebugFlags & DEBUG_SENSORS_FRAME) {
          Serial.printf("[DEBUG_SENSORS_FRAME] [ToF task] readToFObjects() %s\n", ok ? "ok" : "fail");
        }
      }
      vTaskDelay(pdMS_TO_TICKS(1));
    } else {
      vTaskDelay(pdMS_TO_TICKS(50));
    }
  }
}

static void imuTask(void* parameter) {
  if (gDebugFlags & DEBUG_SENSORS_FRAME) {
    Serial.println("[DEBUG_SENSORS_FRAME] IMU task started");
  }
  unsigned long lastIMURead = 0;
  unsigned long lastStackLog = 0;
  while (true) {
    // Update watermark diagnostics
    UBaseType_t wm = uxTaskGetStackHighWaterMark(NULL);
    gIMUWatermarkNow = wm;
    if (wm < gIMUWatermarkMin) gIMUWatermarkMin = wm;
    unsigned long nowLog = millis();
    if (nowLog - lastStackLog >= 5000UL) {
      lastStackLog = nowLog;
      DEBUG_PERFORMANCEF("[STACK] imu_task watermark_now=%u min=%u words", (unsigned)gIMUWatermarkNow, (unsigned)gIMUWatermarkMin);
    }
    // Handle deferred IMU initialization on task stack
    if (imuEnabled && (!imuConnected || bno == nullptr)) {
      if (imuInitRequested) {
        if (i2cMutex) xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(500));
        bool ok = initIMUSensor();
        if (i2cMutex) xSemaphoreGive(i2cMutex);
        imuInitResult = ok;
        imuInitDone = true;
        imuInitRequested = false;
        if (!ok) {
          // Keep disabled to idle task if init failed
          imuEnabled = false;
        }
      }
    }

    if (imuEnabled && imuConnected && bno != nullptr) {
      unsigned long imuPollMs = (gSettings.imuDevicePollMs > 0) ? (unsigned long)gSettings.imuDevicePollMs : 200;
      unsigned long nowMs = millis();
      if (nowMs - lastIMURead >= imuPollMs) {
        if (i2cMutex) xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(200));
        readIMUSensor();
        if (i2cMutex) xSemaphoreGive(i2cMutex);
        lastIMURead = nowMs;
      }
      vTaskDelay(pdMS_TO_TICKS(1));
    } else {
      vTaskDelay(pdMS_TO_TICKS(50));
    }
  }
}

// Thermal dedicated task: mirrors ToF/IMU pattern
static void thermalTask(void* parameter) {
  if (gDebugFlags & DEBUG_SENSORS_FRAME) {
    Serial.println("[DEBUG_SENSORS_FRAME] Thermal task started");
  }
  unsigned long lastThermalRead = 0;
  unsigned long lastStackLog = 0;
  while (true) {
    // Update watermark diagnostics
    UBaseType_t wm = uxTaskGetStackHighWaterMark(NULL);
    gThermalWatermarkNow = wm;
    if (wm < gThermalWatermarkMin) gThermalWatermarkMin = wm;
    unsigned long nowLog = millis();
    if (nowLog - lastStackLog >= 5000UL) {
      lastStackLog = nowLog;
      DEBUG_PERFORMANCEF("[STACK] thermal_task watermark_now=%u min=%u words", (unsigned)gThermalWatermarkNow, (unsigned)gThermalWatermarkMin);
    }
    // Handle deferred initialization request on the task's large stack
    if (thermalEnabled && (!thermalConnected || thermalSensor == nullptr)) {
      if (thermalInitRequested) {
        if (i2cMutex) xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(1000));
        bool ok = initThermalSensor();
        if (i2cMutex) xSemaphoreGive(i2cMutex);
        thermalInitResult = ok;
        thermalInitDone = true;
        thermalInitRequested = false;
        if (!ok) {
          // Disable on failure so task idles
          thermalEnabled = false;
        }
      }
    }

    if (thermalEnabled && thermalConnected && thermalSensor != nullptr) {
      unsigned long nowMs = millis();
      unsigned long pollMs = (gSettings.thermalDevicePollMs > 0) ? (unsigned long)gSettings.thermalDevicePollMs : 100;
      bool ready = true;
      if (thermalArmAtMs) {
        int32_t dt = (int32_t)(nowMs - thermalArmAtMs);
        if (dt < 0) ready = false;
      }
      if (ready && (nowMs - lastThermalRead) >= pollMs) {
        if (i2cMutex) xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(500));
        bool ok = readThermalPixels();
        if (i2cMutex) xSemaphoreGive(i2cMutex);
        lastThermalRead = millis();
        if (thermalPendingFirstFrame && ok) {
          thermalPendingFirstFrame = false;
          sensorStatusBumpWith("thermalstart@firstframe");
        }
      }
      vTaskDelay(pdMS_TO_TICKS(1));
    } else {
      vTaskDelay(pdMS_TO_TICKS(20));
    }
  }
}

// Precomputed Authorization header for fast Basic Auth compare
String gExpectedAuthHeader = "";  // e.g., "Basic dXNlcjpwYXNz"
// Serial CLI buffer
String gSerialCLI = "";
// moved earlier

// ---- Multi-SSID WiFi storage ----
#define MAX_WIFI_NETWORKS 8
struct WifiNetwork {
  String ssid;
  String password;
  int priority;            // 1 = highest priority
  bool hidden;             // informational only
  uint32_t lastConnected;  // millis when last connected
};
static WifiNetwork* gWifiNetworks = nullptr;
static int gWifiNetworkCount = 0;

// Prototypes
static void loadWiFiNetworks();
static void saveWiFiNetworks();
static int findWiFiNetwork(const String& ssid);
static void upsertWiFiNetwork(const String& ssid, const String& password, int priority, bool hidden);
static bool removeWiFiNetwork(const String& ssid);
static void sortWiFiByPriority();
static void normalizeWiFiPriorities();
static void parseWifiNetworksFromJson(const String& json);
// Grouped settings (nested objects)
static void parseOutputFromJson(const String& json);
static void parseThermalFromJson(const String& json);
static void parseTofFromJson(const String& json);
static void parseDebugFromJson(const String& json);
static bool connectWiFiIndex(int index0based, unsigned long timeoutMs);
static bool connectWiFiSSID(const String& ssid, unsigned long timeoutMs);

// Public navigation - minimal menu with only login
String generatePublicNavigation() {
  String nav =
    "<div class=\"top-menu\">"
    "<div class=\"menu-left\">";
  // No navigation links for public pages
  nav += "</div>";
  nav += "<div class=\"user-info\">";
  nav += "<a href=\"/login\" class=\"login-btn\">Login</a>";
  nav += "</div></div>";
  return nav;
}

// Full navigation for authenticated pages
String generateNavigation(const String& activePage, const String& username) {
  String nav =
    "<div class=\"top-menu\">"
    "<div class=\"menu-left\">";
  auto link = [&](const char* href, const char* id, const char* text) {
    nav += "<a href=\"";
    nav += href;
    nav += "\" class=\"menu-item";
    if (activePage == id) nav += " active";
    nav += "\">";
    nav += text;
    nav += "</a>";
  };
  link("/dashboard", "dashboard", "Dashboard");
  link("/cli", "cli", "Command Line");
  link("/sensors", "sensors", "Sensors");
  link("/espnow", "espnow", "ESP-NOW");
  link("/files", "files", "Files");
  link("/automations", "automations", "Automations");
  link("/settings", "settings", "Settings");
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
static char* gStreamBuffer = nullptr;  // 5KB buffer - allocated with ps_alloc

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
  DEBUG_HTTPF("page=%s total=%uB maxChunk=%uB hitMax=%s buf=5119B", 
              gStreamTag.c_str(), gStreamTotalBytes, gStreamMaxChunk, gStreamHitMaxChunk ? "yes" : "no");
}

void streamContentGeneric(httpd_req_t* req, const String& content) {
  const char* contentStr = content.c_str();
  size_t contentLen = content.length();
  size_t chunkSize = 5119;  // 5KB buffer size - 1 for null terminator
  
  httpd_resp_set_type(req, "text/html; charset=utf-8");
  
  for (size_t i = 0; i < contentLen; i += chunkSize) {
    size_t remainingBytes = contentLen - i;
    size_t currentChunkSize = (remainingBytes > chunkSize) ? chunkSize : remainingBytes;
    httpd_resp_send_chunk(req, contentStr + i, currentChunkSize);
  }
  
  // End chunked response
  httpd_resp_send_chunk(req, NULL, 0);

  // Streaming debug: print summary for this response
  extern void streamDebugFlush();
  streamDebugFlush();
}


// Helper to stream regular strings (for dynamic content)
void streamChunk(httpd_req_t* req, const String& str) {
  httpd_resp_send_chunk(req, str.c_str(), str.length());
}

// Helper to stream C strings
void streamChunk(httpd_req_t* req, const char* str) {
  httpd_resp_send_chunk(req, str, strlen(str));
}

void streamSensorsContent(httpd_req_t* req) {
  String u;
  isAuthed(req, u);
  String content = getSensorsPage(u);
  streamContentGeneric(req, content);
}

void streamEspNowContent(httpd_req_t* req) {
  String u;
  isAuthed(req, u);
  String content = getEspNowPage(u);
  streamContentGeneric(req, content);
}

esp_err_t handleSensorsPage(httpd_req_t* req) {
  AuthContext ctx;
  ctx.transport = AUTH_HTTP;
  ctx.opaque = req;
  ctx.path = req->uri ? req->uri : "/sensors";
  getClientIP(req, ctx.ip);
  if (!tgRequireAuth(ctx)) return ESP_OK;  // 401 already sent
  logAuthAttempt(true, req->uri, ctx.user, ctx.ip, "");

  streamPageWithContent(req, "sensors", ctx.user, streamSensorsContent);
  return ESP_OK;
}

esp_err_t handleEspNowPage(httpd_req_t* req) {
  AuthContext ctx;
  ctx.transport = AUTH_HTTP;
  ctx.opaque = req;
  ctx.path = req->uri ? req->uri : "/espnow";
  getClientIP(req, ctx.ip);
  if (!tgRequireAuth(ctx)) return ESP_OK;  // 401 already sent
  logAuthAttempt(true, req->uri, ctx.user, ctx.ip, "");

  streamPageWithContent(req, "espnow", ctx.user, streamEspNowContent);
  return ESP_OK;
}

// Read raw file contents as text/plain
esp_err_t handleFileRead(httpd_req_t* req) {
  AuthContext ctx;
  ctx.transport = AUTH_HTTP;
  ctx.opaque = req;
  ctx.path = "/api/files/read";
  getClientIP(req, ctx.ip);
  if (!tgRequireAuth(ctx)) return ESP_OK;
  logAuthAttempt(true, req->uri, ctx.user, ctx.ip, "");

  if (!filesystemReady) {
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, "Filesystem not initialized", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }

  char query[256];
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, "No filename specified", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }

  char name[160];
  if (httpd_query_key_value(query, "name", name, sizeof(name)) != ESP_OK) {
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, "Invalid filename", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }

  String path = String(name);
  path.replace("%2F", "/");
  path.replace("%20", " ");

  File f = LittleFS.open(path, "r");
  if (!f) {
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, "File not found", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }
  httpd_resp_set_type(req, "text/plain; charset=utf-8");
  char buf[512];
  while (true) {
    size_t n = f.readBytes(buf, sizeof(buf));
    if (n == 0) break;
    httpd_resp_send_chunk(req, buf, n);
  }
  f.close();
  httpd_resp_send_chunk(req, NULL, 0);
  return ESP_OK;
}

// Write file via x-www-form-urlencoded: name=<path>&content=<urlencoded text>
esp_err_t handleFileWrite(httpd_req_t* req) {
  AuthContext ctx;
  ctx.transport = AUTH_HTTP;
  ctx.opaque = req;
  ctx.path = "/api/files/write";
  getClientIP(req, ctx.ip);
  if (!tgRequireAuth(ctx)) return ESP_OK;
  logAuthAttempt(true, req->uri, ctx.user, ctx.ip, "");

  if (!filesystemReady) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"success\":false,\"error\":\"Filesystem not initialized\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }

  // Read body (form-urlencoded)
  const size_t kMax = 4096;  // allow modest edits
  char* body = (char*)malloc(kMax);
  if (!body) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"success\":false,\"error\":\"OOM\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }
  int ret = httpd_req_recv(req, body, kMax - 1);
  if (ret <= 0) {
    free(body);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"success\":false,\"error\":\"No data\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }
  body[ret] = '\0';

  String s = String(body);
  free(body);

  auto getParam = [&](const char* key) -> String {
    String k = String(key) + "=";
    int p = s.indexOf(k);
    if (p < 0) return String("");
    p += k.length();
    int e = s.indexOf("&", p);
    if (e < 0) e = s.length();
    String v = s.substring(p, e);
    // URL decode common characters
    v.replace("+", " ");
    v.replace("%20", " ");
    v.replace("%0A", "\n");
    v.replace("%0D", "\r");
    v.replace("%2F", "/");
    v.replace("%3A", ":");
    v.replace("%2C", ",");
    v.replace("%7B", "{");
    v.replace("%7D", "}");
    v.replace("%22", "\"");
    v.replace("%5B", "[");
    v.replace("%5D", "]");
    v.replace("%25", "%");  // Keep this last to avoid double-decoding
    return v;
  };

  String name = getParam("name");
  String content = getParam("content");
  if (name.length() == 0) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"success\":false,\"error\":\"Name required\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }

  // Prevent overwriting protected files directly.
  // Disallow edits to logs directory and firmware binaries and critical JSONs
  if (name.endsWith(".bin") || name.startsWith("/logs/") || name == "/logs" || name.startsWith("logs/")
      || name == "/users.json" || name == "/settings.json" || name == "/devices.json") {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"success\":false,\"error\":\"Writes to this path are not allowed\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }

  File f = LittleFS.open(name, "w");
  if (!f) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"success\":false,\"error\":\"Open failed\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }
  size_t left = content.length();
  size_t pos = 0;
  while (left > 0) {
    size_t chunk = left > 512 ? 512 : left;
    f.write((const uint8_t*)content.c_str() + pos, chunk);
    pos += chunk;
    left -= chunk;
  }
  f.close();

  // Post-save hooks for specific files
  if (name == "/automations.json") {
    // Read back and sanitize duplicate IDs; persist atomically if changed
    String json;
    if (readText(AUTOMATIONS_JSON_FILE, json)) {
      if (sanitizeAutomationsJson(json)) {
        writeAutomationsJsonAtomic(json);  // best-effort atomic writeback
        gAutosDirty = true;                // ensure scheduler refreshes
        DEBUGF(DEBUG_AUTOMATIONS, "[autos] Sanitized duplicate IDs after file write; scheduler refresh queued");
      }
    }
  }

  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, "{\"success\":true}", HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

// Sensors status endpoint (auth-protected): returns current enable flags and seq
esp_err_t handleSensorsStatus(httpd_req_t* req) {
  AuthContext ctx;
  ctx.transport = AUTH_HTTP;
  ctx.opaque = req;
  ctx.path = "/api/sensors/status";
  getClientIP(req, ctx.ip);
  if (!tgRequireAuth(ctx)) return ESP_OK;

  httpd_resp_set_type(req, "application/json");
  String j = buildSensorStatusJson();
  // Debug: log payload to serial (truncate if large)
  String jDbg = j.substring(0, j.length() > 200 ? 200 : j.length());
  DEBUG_HTTPF("/api/sensors/status by %s @ %s: seq=%d, json_len=%d, json_snippet=%s",
              ctx.user.c_str(), ctx.ip.c_str(), gSensorStatusSeq, j.length(), jDbg.c_str());
  httpd_resp_send(req, j.c_str(), HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

// Admin sessions handlers are defined later, after session structures are declared

// ==========================
// LittleFS helpers
// ==========================

bool fsInit() {
  // Avoid broadcastOutput before FS is ready to ensure first CLI history alloc is logged
  DEBUG_STORAGEF("Initializing LittleFS...");
  if (LittleFS.begin(false)) {
    filesystemReady = true;
  } else {
    DEBUG_STORAGEF("LittleFS mount failed, attempting format...");
    if (LittleFS.begin(true)) {
      filesystemReady = true;
      DEBUG_STORAGEF("LittleFS formatted and mounted.");
    } else {
      DEBUG_STORAGEF("ERROR: LittleFS mount failed even after format.");
      filesystemReady = false;
      return false;
    }
  }

  // Ensure logs dir exists
  LittleFS.mkdir("/logs");
  // Ensure allocation log exists (align with successful_login.txt creation flow)
  if (!LittleFS.exists(LOG_ALLOC_FILE)) {
    File a = LittleFS.open(LOG_ALLOC_FILE, "a");
    if (a) {
      // Include a timestamp prefix if available; otherwise fall back to BOOT ms
      char tsPrefix[40];
      getTimestampPrefixMsCached(tsPrefix, sizeof(tsPrefix));
      String prefix = tsPrefix[0] ? String(tsPrefix) : String("[BOOT ms=") + String(millis()) + "] | ";
      a.println(prefix + String("memory_allocation_history init ") + String(millis()));
      a.close();
    }
  }

  // Now safe to broadcast (this may trigger CLI history allocation, which will be logged)
  // Show FS stats
  size_t total = LittleFS.totalBytes();
  size_t used = LittleFS.usedBytes();
  broadcastOutput(String("FS Total: ") + String(total) + " bytes, Used: " + String(used) + ", Free: " + String(total - used));

  // Boot-time automations.json sanitation: ensure no duplicate IDs persist from manual edits
  // Temporarily enable DEBUG_AUTOMATIONS to guarantee visibility of messages at boot
  uint32_t _dbgSaved = gDebugFlags;
  gDebugFlags |= DEBUG_AUTOMATIONS;
  if (LittleFS.exists(AUTOMATIONS_JSON_FILE)) {
    String json;
    if (readText(AUTOMATIONS_JSON_FILE, json)) {
      bool modified = false;

      // First: sanitize duplicate IDs
      if (sanitizeAutomationsJson(json)) {
        modified = true;
        DEBUGF(DEBUG_AUTOMATIONS, "[autos] Boot sanitize: fixed duplicate IDs");
      } else {
        DEBUGF(DEBUG_AUTOMATIONS, "[autos] Boot sanitize: no duplicate IDs found");
      }

      // Second: migrate missing nextAt fields
      time_t now = time(nullptr);
      if (now > 0) {
        int migrated = 0, failed = 0;

        // Parse through all automations and add missing nextAt fields
        int pos = 0;
        String newJson = json;
        while (true) {
          int idPos = newJson.indexOf("\"id\"", pos);
          if (idPos < 0) break;
          int colon = newJson.indexOf(':', idPos);
          if (colon < 0) break;
          int comma = newJson.indexOf(',', colon + 1);
          int braceEnd = newJson.indexOf('}', colon + 1);
          if (braceEnd < 0) break;
          if (comma < 0 || comma > braceEnd) comma = braceEnd;
          String idStr = newJson.substring(colon + 1, comma);
          idStr.trim();
          long id = idStr.toInt();

          // Extract automation object
          int objStart = newJson.lastIndexOf('{', idPos);
          if (objStart < 0) {
            pos = braceEnd + 1;
            continue;
          }
          String obj = newJson.substring(objStart, braceEnd + 1);

          // Check if nextAt field exists
          if (obj.indexOf("\"nextAt\"") < 0) {
            // Missing nextAt - compute and add it
            bool enabled = (obj.indexOf("\"enabled\": true") >= 0) || (obj.indexOf("\"enabled\":true") >= 0);
            if (enabled) {
              time_t nextAt = computeNextRunTime(obj, now);
              if (nextAt > 0) {
                // Add nextAt field before closing brace
                int insertAt = obj.lastIndexOf('}');
                if (insertAt > 0) {
                  String before = obj.substring(0, insertAt);
                  String after = obj.substring(insertAt);
                  String trimmed = before;
                  trimmed.trim();
                  if (trimmed.endsWith(",")) {
                    obj = before + "\n  \"nextAt\": " + String((unsigned long)nextAt) + "\n" + after;
                  } else {
                    obj = before + ",\n  \"nextAt\": " + String((unsigned long)nextAt) + "\n" + after;
                  }

                  // Replace object in JSON
                  newJson = newJson.substring(0, objStart) + obj + newJson.substring(braceEnd + 1);
                  migrated++;
                  modified = true;
                  DEBUGF(DEBUG_AUTOMATIONS, "[autos] Boot migrate: added nextAt=%lu for id=%ld", (unsigned long)nextAt, id);
                } else {
                  failed++;
                }
              } else {
                failed++;
                DEBUGF(DEBUG_AUTOMATIONS, "[autos] Boot migrate: could not compute nextAt for id=%ld", id);
              }
            } else {
              // Disabled automation - add null nextAt
              int insertAt = obj.lastIndexOf('}');
              if (insertAt > 0) {
                String before = obj.substring(0, insertAt);
                String after = obj.substring(insertAt);
                String trimmed = before;
                trimmed.trim();
                if (trimmed.endsWith(",")) {
                  obj = before + "\n  \"nextAt\": null\n" + after;
                } else {
                  obj = before + ",\n  \"nextAt\": null\n" + after;
                }

                // Replace object in JSON
                newJson = newJson.substring(0, objStart) + obj + newJson.substring(braceEnd + 1);
                migrated++;
                modified = true;
                DEBUGF(DEBUG_AUTOMATIONS, "[autos] Boot migrate: added nextAt=null for disabled id=%ld", id);
              }
            }
          }

          pos = braceEnd + 1;
        }

        if (migrated > 0) {
          json = newJson;
          DEBUGF(DEBUG_AUTOMATIONS, "[autos] Boot migrate: added nextAt to %d automations (%d failed)", migrated, failed);
        } else {
          DEBUGF(DEBUG_AUTOMATIONS, "[autos] Boot migrate: all automations already have nextAt");
        }
      } else {
        DEBUGF(DEBUG_AUTOMATIONS, "[autos] Boot migrate: skipped (no valid system time)");
      }

      // Write back if any changes were made
      if (modified) {
        writeAutomationsJsonAtomic(json);
        gAutosDirty = true;
        DEBUGF(DEBUG_AUTOMATIONS, "[autos] Boot: wrote updated automations.json; scheduler refresh queued");
      }
    } else {
      DEBUGF(DEBUG_AUTOMATIONS, "[autos] Boot sanitize: failed to read automations.json");
    }
  } else {
    DEBUGF(DEBUG_AUTOMATIONS, "[autos] Boot sanitize: /automations.json not found, skipping");
  }
  gDebugFlags = _dbgSaved;  // restore debug flags
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
  static char* buf = nullptr;
  if (!buf) {
    buf = (char*)ps_alloc(chunk, AllocPref::PreferPSRAM, "file.read");
    if (!buf) return false;
  }
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

// Atomic writer for automations.json: write temp then rename; fallback to direct write
static bool writeAutomationsJsonAtomic(const String& json) {
  const char* tmp = "/automations.tmp";
  if (!writeText(tmp, json)) return false;
  LittleFS.remove(AUTOMATIONS_JSON_FILE);
  if (!LittleFS.rename(tmp, AUTOMATIONS_JSON_FILE)) {
    // Fallback: direct write
    return writeText(AUTOMATIONS_JSON_FILE, json);
  }
  return true;
}

// Helper: update nextAt field in automation JSON and persist to file
static bool updateAutomationNextAt(long automationId, time_t newNextAt) {
  String json;
  if (!readText(AUTOMATIONS_JSON_FILE, json)) return false;

  String needle = String("\"id\": ") + String(automationId);
  int idPos = json.indexOf(needle);
  if (idPos < 0) return false;

  int objStart = json.lastIndexOf('{', idPos);
  if (objStart < 0) return false;

  int depth = 0, objEnd = -1;
  for (int i = objStart; i < (int)json.length(); ++i) {
    char c = json[i];
    if (c == '{') depth++;
    else if (c == '}') {
      depth--;
      if (depth == 0) {
        objEnd = i;
        break;
      }
    }
  }
  if (objEnd < 0) return false;

  String obj = json.substring(objStart, objEnd + 1);

  // Find and replace nextAt field
  int nextAtPos = obj.indexOf("\"nextAt\"");
  if (nextAtPos >= 0) {
    int colon = obj.indexOf(':', nextAtPos);
    int comma = obj.indexOf(',', colon);
    int brace = obj.indexOf('}', colon);
    int end = (comma > 0 && (brace < 0 || comma < brace)) ? comma : brace;
    if (end > colon) {
      String before = obj.substring(0, colon + 1);
      String after = obj.substring(end);
      obj = before + " " + String((unsigned long)newNextAt) + after;
    }
  } else {
    // Add nextAt field before closing brace
    int insertAt = obj.lastIndexOf('}');
    if (insertAt > 0) {
      String before = obj.substring(0, insertAt);
      String after = obj.substring(insertAt);
      // Check if we need a comma
      String trimmed = before;
      trimmed.trim();
      if (trimmed.endsWith(",")) {
        obj = before + "\n  \"nextAt\": " + String((unsigned long)newNextAt) + "\n" + after;
      } else {
        obj = before + ",\n  \"nextAt\": " + String((unsigned long)newNextAt) + "\n" + after;
      }
    }
  }

  // Replace object in full JSON
  json = json.substring(0, objStart) + obj + json.substring(objEnd + 1);

  return writeAutomationsJsonAtomic(json);
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
  if (sz <= capBytes) {
    r.close();
    return true;
  }
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

// ----------------------------------------------------------------------------
// Allocation debug hook (weak): logs to LittleFS /logs/alloc.txt
// ----------------------------------------------------------------------------
extern "C" void __attribute__((weak)) memAllocDebug(const char* op, void* ptr, size_t size,
                                                    bool requestedPS, bool usedPS, const char* tag) {
  // Avoid work if filesystem not ready
  if (!filesystemReady) return;
  // Reentrancy guard to prevent recursion when logging triggers allocations
  static volatile bool s_inMemLog = false;
  if (s_inMemLog) return;
  s_inMemLog = true;
  // Ensure /logs exists (best-effort)
  if (!LittleFS.exists("/logs")) {
    LittleFS.mkdir("/logs");
  }
  // Timestamp prefix with ms precision, via boot-epoch offset and esp_timer
  char tsPrefix[40];
  getTimestampPrefixMsCached(tsPrefix, sizeof(tsPrefix));

  // After values
  size_t heapAfter = ESP.getFreeHeap();
  size_t psTot = ESP.getPsramSize();
  size_t psAfter = (psTot > 0) ? ESP.getFreePsram() : 0;

  // Deltas (positive means memory consumed)
  long heapDelta = (long)gAllocHeapBefore - (long)heapAfter;
  long psDelta = (long)gAllocPsBefore - (long)psAfter;

  // Build one-line entry with before/after info
  // Format: [YYYY-mm-dd HH:MM:SS] | ms=<millis> op=... size=... reqPS=0|1 usedPS=0|1 ptr=0x... tag=...
  //         heapBefore=... heapAfter=... heapDelta=... [psBefore=... psAfter=... psDelta=...]
  String line;
  line.reserve(200);
  // Always include a prefix; if NTP time isn't ready yet, use a BOOT-ms fallback
  String prefix = tsPrefix[0] ? String(tsPrefix) : String("[BOOT ms=") + String(millis()) + "] | ";
  line += prefix;
  line += "ms=";
  line += String(millis());
  line += " op=";
  line += (op ? op : "?");
  line += " size=";
  line += String((unsigned long)size);
  line += " reqPS=";
  line += (requestedPS ? "1" : "0");
  line += " usedPS=";
  line += (usedPS ? "1" : "0");
  line += " ptr=0x";
  line += String((uint32_t)ptr, HEX);
  if (tag && tag[0]) {
    line += " tag=";
    line += tag;
  }

  line += " heapBefore=";
  line += String(gAllocHeapBefore);
  line += " heapAfter=";
  line += String(heapAfter);
  line += " heapDelta=";
  line += String(heapDelta);

  if (psTot > 0) {
    line += " psBefore=";
    line += String(gAllocPsBefore);
    line += " psAfter=";
    line += String(psAfter);
    line += " psDelta=";
    line += String(psDelta);
  }

  static const size_t ALLOC_LOG_CAP = 64 * 1024;  // 64KB cap
  appendLineWithCap(LOG_ALLOC_FILE, line, ALLOC_LOG_CAP);
  s_inMemLog = false;
}

// ==========================
// Settings and users
// ==========================

static const uint16_t kSettingsVersion = 1;

void settingsDefaults() {
  gSettings.version = kSettingsVersion;
  gSettings.wifiSSID = "";
  gSettings.wifiPassword = "";
  gSettings.wifiAutoReconnect = true;
  gSettings.cliHistorySize = 10;
  gSettings.ntpServer = "pool.ntp.org";
  gSettings.tzOffsetMinutes = -240;  // EST (UTC-5)
  gSettings.outSerial = true;        // default serial on
  gSettings.outWeb = false;
  gSettings.outTft = false;
  // Sensors UI defaults
  gSettings.thermalPollingMs = 100;
  // ToF UI polling aligned with device timing budget for stability
  gSettings.tofPollingMs = 220;
  gSettings.tofStabilityThreshold = 3;
  gSettings.thermalPaletteDefault = "grayscale";
  // Thermal interpolation defaults
  gSettings.thermalInterpolationEnabled = true;
  gSettings.thermalInterpolationSteps = 5;
  gSettings.thermalInterpolationBufferSize = 2;
  gSettings.thermalWebClientQuality = 2;  // default 2x quality (64x48)
  // Advanced defaults
  gSettings.thermalEWMAFactor = 0.2f;  // blend factor for new value (0..1)
  gSettings.thermalTransitionMs = 80;
  gSettings.tofTransitionMs = 200;
  gSettings.tofUiMaxDistanceMm = 3400;
  gSettings.i2cClockThermalHz = 800000;
  gSettings.i2cClockToFHz = 200000;
  gSettings.thermalTargetFps = 8;
  gSettings.thermalWebMaxFps = 10;  // max UI polling FPS (1..20)
  // Device-side polling defaults
  gSettings.thermalDevicePollMs = 100;
  // ToF timing budget is 200ms; default poll a bit slower to avoid stale/invalid frames
  gSettings.tofDevicePollMs = 220;
  gSettings.imuDevicePollMs = 200;
  // Debug defaults - enable all for development/troubleshooting
  gSettings.debugAuthCookies = false;
  gSettings.debugHttp = false;
  gSettings.debugSse = false;
  gSettings.debugCli = false;
  gSettings.debugSensorsFrame = false;
  gSettings.debugSensorsData = false;
  gSettings.debugSensorsGeneral = false;
  gSettings.debugWifi = false;
  gSettings.debugStorage = false;
  gSettings.debugPerformance = false;
  gSettings.debugDateTime = false;
  gSettings.debugCommandFlow = false;
  gSettings.debugUsers = false;
  // ESP-NOW defaults
  gSettings.espnowenabled = false;  // default disabled for backward compatibility
}

// Minimal JSON encode (no external deps)
String settingsToJson() {
  String j = "{";
  j += "\"version\":" + String(gSettings.version);
  j += ",\"wifiAutoReconnect\":" + String(gSettings.wifiAutoReconnect ? 1 : 0);
  j += ",\"cliHistorySize\":" + String(gSettings.cliHistorySize);
  j += ",\"ntpServer\":\"" + gSettings.ntpServer + "\"";
  j += ",\"tzOffsetMinutes\":" + String(gSettings.tzOffsetMinutes);
  // Grouped sections only (no duplicate top-level keys)
  j += ",\"output\":{"
       "\"outSerial\":"
       + String(gSettings.outSerial ? 1 : 0) + ","
                                               "\"outWeb\":"
       + String(gSettings.outWeb ? 1 : 0) + ","
                                            "\"outTft\":"
       + String(gSettings.outTft ? 1 : 0) + "}";
  // Grouped debug object
  j += ",\"debug\":{"
       "\"authCookies\":"
       + String(gSettings.debugAuthCookies ? 1 : 0) + ","
                                                      "\"http\":"
       + String(gSettings.debugHttp ? 1 : 0) + ","
                                               "\"sse\":"
       + String(gSettings.debugSse ? 1 : 0) + ","
                                              "\"cli\":"
       + String(gSettings.debugCli ? 1 : 0) + ","
                                              "\"sensorsFrame\":"
       + String(gSettings.debugSensorsFrame ? 1 : 0) + ","
                                                       "\"sensorsData\":"
       + String(gSettings.debugSensorsData ? 1 : 0) + ","
                                                      "\"sensorsGeneral\":"
       + String(gSettings.debugSensorsGeneral ? 1 : 0) + ","
                                                         "\"wifi\":"
       + String(gSettings.debugWifi ? 1 : 0) + ","
                                               "\"storage\":"
       + String(gSettings.debugStorage ? 1 : 0) + ","
                                                  "\"performance\":"
       + String(gSettings.debugPerformance ? 1 : 0) + ","
                                                      "\"dateTime\":"
       + String(gSettings.debugDateTime ? 1 : 0) + ","
                                                   "\"cmdFlow\":"
       + String(gSettings.debugCommandFlow ? 1 : 0) + ","
                                                   "\"users\":"
       + String(gSettings.debugUsers ? 1 : 0) + "}";
  // Group thermal into ui and device sub-objects
  j += ",\"thermal\":{\"ui\":{"
       "\"thermalPollingMs\":"
       + String(gSettings.thermalPollingMs) + ","
                                              "\"thermalPaletteDefault\":\""
       + gSettings.thermalPaletteDefault + "\","
                                           "\"thermalInterpolationEnabled\":"
       + String(gSettings.thermalInterpolationEnabled ? 1 : 0) + ","
                                                                 "\"thermalInterpolationSteps\":"
       + String(gSettings.thermalInterpolationSteps) + ","
                                                       "\"thermalInterpolationBufferSize\":"
       + String(gSettings.thermalInterpolationBufferSize) + ","
                                                            "\"thermalWebClientQuality\":"
       + String(gSettings.thermalWebClientQuality) + ","
                                                     "\"thermalEWMAFactor\":"
       + String(gSettings.thermalEWMAFactor, 3) + ","
                                                  "\"thermalTransitionMs\":"
       + String(gSettings.thermalTransitionMs) + ","
                                                 "\"thermalWebMaxFps\":"
       + String(gSettings.thermalWebMaxFps) + "},\"device\":{"
                                              "\"thermalTargetFps\":"
       + String(gSettings.thermalTargetFps) + ","
                                              "\"thermalDevicePollMs\":"
       + String(gSettings.thermalDevicePollMs) + ","
                                                 "\"i2cClockThermalHz\":"
       + String(gSettings.i2cClockThermalHz) + "}}";
  // Group tof into ui and device sub-objects
  j += ",\"tof\":{\"ui\":{"
       "\"tofPollingMs\":"
       + String(gSettings.tofPollingMs) + ","
                                          "\"tofStabilityThreshold\":"
       + String(gSettings.tofStabilityThreshold) + ","
                                                   "\"tofTransitionMs\":"
       + String(gSettings.tofTransitionMs) + ","
                                             "\"tofUiMaxDistanceMm\":"
       + String(gSettings.tofUiMaxDistanceMm) + "},\"device\":{"
                                                "\"tofDevicePollMs\":"
       + String(gSettings.tofDevicePollMs) + ","
                                             "\"i2cClockToFHz\":"
       + String(gSettings.i2cClockToFHz) + "}}";
  // Un-grouped device-side key(s) that remain top-level
  j += ",\"imuDevicePollMs\":" + String(gSettings.imuDevicePollMs);
  // ESP-NOW settings
  j += ",\"espnowenabled\":" + String(gSettings.espnowenabled ? 1 : 0);
  // Embed multi-SSID list (authoritative store). Includes passwords.
  j += ",\"wifiNetworks\":[";
  for (int i = 0; i < gWifiNetworkCount; ++i) {
    if (i) j += ",";
    const WifiNetwork& nw = gWifiNetworks[i];
    String encryptedPassword = encryptWifiPassword(nw.password);
    j += String("{\"ssid\":\"") + nw.ssid + "\"," + "\"password\":\"" + encryptedPassword + "\"," + "\"priority\":" + String(nw.priority) + "," + "\"hidden\":" + String(nw.hidden ? 1 : 0) + "," + "\"lastConnected\":" + String((unsigned)nw.lastConnected) + "}";
  }
  j += "]";
  j += "}";
  return j;
}

// Tiny JSON parsing helpers (simple string scanning for our generated JSON)
static bool parseJsonBool(const String& src, const char* key, bool& out) {
  String k = String("\"") + key + "\":";
  int p = src.indexOf(k);
  if (p < 0) return false;
  p += k.length();
  while (p < (int)src.length() && src[p] == ' ') p++;
  if (p >= (int)src.length()) return false;
  if (src.startsWith("true", p)) {
    out = true;
    return true;
  }
  if (src.startsWith("false", p)) {
    out = false;
    return true;
  }
  if (src[p] == '1') {
    out = true;
    return true;
  }
  if (src[p] == '0') {
    out = false;
    return true;
  }
  return false;
}

static bool parseJsonInt(const String& src, const char* key, int& out) {
  String k = String("\"") + key + "\":";
  int p = src.indexOf(k);
  if (p < 0) return false;
  p += k.length();
  while (p < (int)src.length() && src[p] == ' ') p++;
  if (p >= (int)src.length()) return false;
  int end = p;
  while (end < (int)src.length()) {
    char c = src[end];
    if ((c >= '0' && c <= '9') || c == '-') {
      end++;
      continue;
    }
    break;
  }
  if (end == p) return false;
  out = src.substring(p, end).toInt();
  return true;
}

static bool parseJsonFloat(const String& src, const char* key, float& out) {
  String k = String("\"") + key + "\":";
  int p = src.indexOf(k);
  if (p < 0) return false;
  p += k.length();
  while (p < (int)src.length() && src[p] == ' ') p++;
  if (p >= (int)src.length()) return false;
  bool seenDigit = false, seenDot = false;
  int end = p;
  while (end < (int)src.length()) {
    char c = src[end];
    if (c >= '0' && c <= '9') {
      seenDigit = true;
      end++;
      continue;
    }
    if (c == '-' && end == p) {
      end++;
      continue;
    }
    if (c == '.' && !seenDot) {
      seenDot = true;
      end++;
      continue;
    }
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

// Extract a nested object slice by key: finds key then returns substring between matching { ... }
static bool extractObjectByKey(const String& src, const char* key, String& outObj) {
  String k = String("\"") + key + "\"";
  int keyPos = src.indexOf(k);
  if (keyPos < 0) return false;
  int colon = src.indexOf(':', keyPos + k.length());
  if (colon < 0) return false;
  int lb = src.indexOf('{', colon);
  if (lb < 0) return false;
  // Find matching closing brace (simple depth counter)
  int depth = 0;
  for (int i = lb; i < (int)src.length(); ++i) {
    char c = src[i];
    if (c == '{') depth++;
    else if (c == '}') {
      depth--;
      if (depth == 0) {
        outObj = src.substring(lb + 1, i);  // contents without outer braces
        return true;
      }
    }
  }
  return false;
}

// Parse grouped "output" object
static void parseOutputFromJson(const String& json) {
  String obj;
  if (!extractObjectByKey(json, "output", obj)) return;
  parseJsonBool(obj, "outSerial", gSettings.outSerial);
  parseJsonBool(obj, "outWeb", gSettings.outWeb);
  parseJsonBool(obj, "outTft", gSettings.outTft);
}

// Parse grouped "thermal" object
static void parseThermalFromJson(const String& json) {
  String obj;
  if (!extractObjectByKey(json, "thermal", obj)) return;
  // Support new grouped structure: thermal.{ui:{...}, device:{...}}
  String ui, dev;
  if (extractObjectByKey(obj, "ui", ui)) {
    parseJsonInt(ui, "thermalPollingMs", gSettings.thermalPollingMs);
    parseJsonString(ui, "thermalPaletteDefault", gSettings.thermalPaletteDefault);
    parseJsonBool(ui, "thermalInterpolationEnabled", gSettings.thermalInterpolationEnabled);
    parseJsonInt(ui, "thermalInterpolationSteps", gSettings.thermalInterpolationSteps);
    parseJsonInt(ui, "thermalInterpolationBufferSize", gSettings.thermalInterpolationBufferSize);
    parseJsonInt(ui, "thermalWebClientQuality", gSettings.thermalWebClientQuality);
    parseJsonFloat(ui, "thermalEWMAFactor", gSettings.thermalEWMAFactor);
    parseJsonInt(ui, "thermalTransitionMs", gSettings.thermalTransitionMs);
    parseJsonInt(ui, "thermalWebMaxFps", gSettings.thermalWebMaxFps);
  }
  if (extractObjectByKey(obj, "device", dev)) {
    parseJsonInt(dev, "thermalTargetFps", gSettings.thermalTargetFps);
    parseJsonInt(dev, "thermalDevicePollMs", gSettings.thermalDevicePollMs);
    parseJsonInt(dev, "i2cClockThermalHz", gSettings.i2cClockThermalHz);
  }
  // Backward-compat: accept legacy flat keys inside thermal object
  parseJsonInt(obj, "thermalPollingMs", gSettings.thermalPollingMs);
  parseJsonString(obj, "thermalPaletteDefault", gSettings.thermalPaletteDefault);
  parseJsonBool(obj, "thermalInterpolationEnabled", gSettings.thermalInterpolationEnabled);
  parseJsonInt(obj, "thermalInterpolationSteps", gSettings.thermalInterpolationSteps);
  parseJsonInt(obj, "thermalInterpolationBufferSize", gSettings.thermalInterpolationBufferSize);
  parseJsonInt(obj, "thermalWebClientQuality", gSettings.thermalWebClientQuality);
  parseJsonFloat(obj, "thermalEWMAFactor", gSettings.thermalEWMAFactor);
  parseJsonInt(obj, "thermalTransitionMs", gSettings.thermalTransitionMs);
  parseJsonInt(obj, "thermalTargetFps", gSettings.thermalTargetFps);
  parseJsonInt(obj, "thermalWebMaxFps", gSettings.thermalWebMaxFps);
  parseJsonInt(obj, "thermalDevicePollMs", gSettings.thermalDevicePollMs);
  parseJsonInt(obj, "i2cClockThermalHz", gSettings.i2cClockThermalHz);
}

// Parse grouped "tof" object
static void parseTofFromJson(const String& json) {
  String obj;
  if (!extractObjectByKey(json, "tof", obj)) return;
  // Support new grouped structure: tof.{ui:{...}, device:{...}}
  String ui, dev;
  if (extractObjectByKey(obj, "ui", ui)) {
    parseJsonInt(ui, "tofPollingMs", gSettings.tofPollingMs);
    parseJsonInt(ui, "tofStabilityThreshold", gSettings.tofStabilityThreshold);
    parseJsonInt(ui, "tofTransitionMs", gSettings.tofTransitionMs);
    parseJsonInt(ui, "tofUiMaxDistanceMm", gSettings.tofUiMaxDistanceMm);
  }
  if (extractObjectByKey(obj, "device", dev)) {
    parseJsonInt(dev, "tofDevicePollMs", gSettings.tofDevicePollMs);
    parseJsonInt(dev, "i2cClockToFHz", gSettings.i2cClockToFHz);
  }
  // Backward-compat: accept legacy flat keys inside tof object
  parseJsonInt(obj, "tofPollingMs", gSettings.tofPollingMs);
  parseJsonInt(obj, "tofStabilityThreshold", gSettings.tofStabilityThreshold);
  parseJsonInt(obj, "tofTransitionMs", gSettings.tofTransitionMs);
  parseJsonInt(obj, "tofUiMaxDistanceMm", gSettings.tofUiMaxDistanceMm);
  parseJsonInt(obj, "tofDevicePollMs", gSettings.tofDevicePollMs);
  parseJsonInt(obj, "i2cClockToFHz", gSettings.i2cClockToFHz);
}

// Parse grouped "debug" object
static void parseDebugFromJson(const String& json) {
  String obj;
  if (!extractObjectByKey(json, "debug", obj)) return;
  parseJsonBool(obj, "authCookies", gSettings.debugAuthCookies);
  parseJsonBool(obj, "http", gSettings.debugHttp);
  parseJsonBool(obj, "sse", gSettings.debugSse);
  parseJsonBool(obj, "cli", gSettings.debugCli);
  parseJsonBool(obj, "sensorsFrame", gSettings.debugSensorsFrame);
  parseJsonBool(obj, "sensorsData", gSettings.debugSensorsData);
  parseJsonBool(obj, "sensorsGeneral", gSettings.debugSensorsGeneral);
  parseJsonBool(obj, "wifi", gSettings.debugWifi);
  parseJsonBool(obj, "storage", gSettings.debugStorage);
  parseJsonBool(obj, "performance", gSettings.debugPerformance);
  parseJsonBool(obj, "dateTime", gSettings.debugDateTime);
  parseJsonBool(obj, "cmdFlow", gSettings.debugCommandFlow);
  parseJsonBool(obj, "users", gSettings.debugUsers);
}

// Forward-declare; implemented after OUTPUT_* flags are defined
static void applySettings();

static bool loadUnifiedSettings() {
  if (!filesystemReady) return false;
  if (!LittleFS.exists(SETTINGS_JSON_FILE)) {
    DEBUG_STORAGEF("Settings file does not exist: %s", SETTINGS_JSON_FILE);
    return false;
  }
  String txt;
  if (!readText(SETTINGS_JSON_FILE, txt)) return false;
  DEBUG_STORAGEF("Loading settings from file, length=%d", txt.length());
  // Start with defaults, then overwrite found keys
  settingsDefaults();
  parseJsonU16(txt, "version", gSettings.version);
  parseJsonBool(txt, "wifiAutoReconnect", gSettings.wifiAutoReconnect);
  parseJsonInt(txt, "cliHistorySize", gSettings.cliHistorySize);
  parseJsonString(txt, "ntpServer", gSettings.ntpServer);
  parseJsonInt(txt, "tzOffsetMinutes", gSettings.tzOffsetMinutes);
  // Legacy flat keys removed: rely on grouped objects only
  parseJsonInt(txt, "imuDevicePollMs", gSettings.imuDevicePollMs);
  // Debug settings
  parseJsonBool(txt, "debugAuthCookies", gSettings.debugAuthCookies);
  parseJsonBool(txt, "debugHttp", gSettings.debugHttp);
  parseJsonBool(txt, "debugSse", gSettings.debugSse);
  parseJsonBool(txt, "debugCli", gSettings.debugCli);
  parseJsonBool(txt, "debugSensorsFrame", gSettings.debugSensorsFrame);
  parseJsonBool(txt, "debugSensorsData", gSettings.debugSensorsData);
  parseJsonBool(txt, "debugSensorsGeneral", gSettings.debugSensorsGeneral);
  parseJsonBool(txt, "debugWifi", gSettings.debugWifi);
  parseJsonBool(txt, "debugStorage", gSettings.debugStorage);
  parseJsonBool(txt, "debugPerformance", gSettings.debugPerformance);
  parseJsonBool(txt, "debugDateTime", gSettings.debugDateTime);
  parseJsonBool(txt, "debugCommandFlow", gSettings.debugCommandFlow);
  parseJsonBool(txt, "debugUsers", gSettings.debugUsers);
  // ESP-NOW settings
  parseJsonBool(txt, "espnowenabled", gSettings.espnowenabled);
  // Load grouped objects (authoritative structure)
  parseOutputFromJson(txt);
  parseThermalFromJson(txt);
  parseTofFromJson(txt);
  parseDebugFromJson(txt);
  // Populate multi-SSID list if present
  parseWifiNetworksFromJson(txt);
  sortWiFiByPriority();
  normalizeWiFiPriorities();
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
  // Legacy settings file parsing - now updates gSettings directly
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
    String key = line.substring(0, eq);
    key.trim();
    String val = line.substring(eq + 1);
    val.trim();
    if (key == "ntp_server") gSettings.ntpServer = val;
    else if (key == "tz_offset_minutes") gSettings.tzOffsetMinutes = val.toInt();
  }
}

bool loadUsersFromFile(String& outUser, String& outPass) {
  if (!filesystemReady) return false;
  // Prefer JSON users
  if (LittleFS.exists(USERS_JSON_FILE)) {
    String json;
    if (!readText(USERS_JSON_FILE, json)) return false;
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

// Legacy WiFi file helpers removed. WiFi is loaded from unified settings.json (see gSettings) or compile-time defaults.

// ==========================
// Multi-SSID helpers
// ==========================

static int findWiFiNetwork(const String& ssid) {
  for (int i = 0; i < gWifiNetworkCount; ++i)
    if (gWifiNetworks[i].ssid == ssid) return i;
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
    broadcastOutput("[WiFi] Network list full; cannot add");
    return;
  }
  WifiNetwork nw;
  nw.ssid = ssid;
  nw.password = password;
  nw.priority = (priority > 0) ? priority : 1;
  nw.hidden = hidden;
  nw.lastConnected = 0;
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
static void parseWifiNetworksFromJson(const String& json) {
  gWifiNetworkCount = 0;
  // Find wifiNetworks array
  int arrKey = json.indexOf("\"wifiNetworks\"");
  if (arrKey < 0) return;
  int colon = json.indexOf(':', arrKey);
  if (colon < 0) return;
  int lb = json.indexOf('[', colon);
  int rb = (lb >= 0) ? json.indexOf(']', lb) : -1;
  if (lb < 0 || rb < 0) return;
  String arr = json.substring(lb + 1, rb);
  int pos = 0;
  while (pos < (int)arr.length() && gWifiNetworkCount < MAX_WIFI_NETWORKS) {
    int ob = arr.indexOf('{', pos);
    if (ob < 0) break;
    int cb = arr.indexOf('}', ob);
    if (cb < 0) break;
    String item = arr.substring(ob + 1, cb);
    WifiNetwork nw;
    nw.priority = 1;
    nw.hidden = false;
    nw.lastConnected = 0;
    // crude key parsing
    parseJsonString(item, "ssid", nw.ssid);
    parseJsonString(item, "password", nw.password);
    // Decrypt password if it's encrypted
    nw.password = decryptWifiPassword(nw.password);
    parseJsonInt(item, "priority", nw.priority);
    parseJsonBool(item, "hidden", nw.hidden);
    {
      int tmp = 0;
      parseJsonInt(item, "lastConnected", tmp);
      nw.lastConnected = (uint32_t)tmp;
    }
    if (nw.ssid.length() > 0) {
      gWifiNetworks[gWifiNetworkCount++] = nw;
    }
    pos = cb + 1;
  }
}

static void normalizeWiFiPriorities() {
  // Ensure priorities are unique and >=1, compacted ascending by sort order
  sortWiFiByPriority();
  int nextPri = 1;
  for (int i = 0; i < gWifiNetworkCount; ++i) {
    gWifiNetworks[i].priority = nextPri++;
  }
}

static void loadWiFiNetworks() {
  gWifiNetworkCount = 0;
  if (!filesystemReady) return;
  if (LittleFS.exists(SETTINGS_JSON_FILE)) {
    String txt;
    if (readText(SETTINGS_JSON_FILE, txt)) {
      parseWifiNetworksFromJson(txt);
      sortWiFiByPriority();
      normalizeWiFiPriorities();
    }
  }
}

static void saveWiFiNetworks() {
  if (!filesystemReady) return;
  sortWiFiByPriority();
  normalizeWiFiPriorities();
  // Persist via unified settings.json only
  saveUnifiedSettings();
}

// Connect to a saved network by index (0-based). Update lastConnected on success.
static bool connectWiFiIndex(int index0based, unsigned long timeoutMs) {
  if (index0based < 0 || index0based >= gWifiNetworkCount) return false;
  const WifiNetwork& nw = gWifiNetworks[index0based];
  broadcastOutput(String("Connecting to [") + String(index0based + 1) + "] '" + nw.ssid + "'...");
  WiFi.begin(nw.ssid.c_str(), nw.password.c_str());
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
    delay(200);
    // Check for user escape input during WiFi connection
    if (Serial.available()) {
      while (Serial.available()) Serial.read(); // consume all pending input
      broadcastOutput("*** WiFi connection cancelled by user ***");
      return false; // connection cancelled
    }
    // progress dots omitted from unified output to reduce noise
  }
  if (WiFi.status() == WL_CONNECTED) {
    broadcastOutput(String("WiFi connected: ") + WiFi.localIP().toString());
    gWifiNetworks[index0based].lastConnected = millis();
    saveWiFiNetworks();
    return true;
  }
  broadcastOutput("Connection failed.");
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
    String json;
    if (!readText(USERS_JSON_FILE, json)) return false;
    int usersIdx = json.indexOf("\"users\"");
    if (usersIdx < 0) return false;
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
  String txt;
  if (!readText(USERS_FILE, txt)) return false;
  int pos = 0;
  bool firstUserChecked = false;
  while (pos < (int)txt.length()) {
    int nl = txt.indexOf('\n', pos);
    if (nl < 0) nl = txt.length();
    String line = txt.substring(pos, nl);
    pos = nl + 1;
    line.trim();
    if (line.length() == 0 || line.startsWith("#")) continue;
    int c1 = line.indexOf(':');
    if (c1 <= 0) continue;
    int c2 = line.indexOf(':', c1 + 1);
    String username = line.substring(0, c1);
    username.trim();
    String role = (c2 >= 0) ? line.substring(c2 + 1) : String("");
    role.trim();
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
    String json;
    if (!readText(USERS_JSON_FILE, json)) return false;
    int pos = json.indexOf("\"users\"");
    if (pos < 0) return false;
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
        if (u == uname && verifyUserPassword(p, pass)) return true;
      }
      pos = uq2 + 1;
    }
    return false;
  }
  // Legacy users.txt
  if (!LittleFS.exists(USERS_FILE)) return false;
  String txt;
  if (!readText(USERS_FILE, txt)) return false;
  int pos = 0;
  while (pos < (int)txt.length()) {
    int eol = txt.indexOf('\n', pos);
    if (eol < 0) eol = txt.length();
    String line = txt.substring(pos, eol);
    pos = eol + 1;
    line.trim();
    if (line.length() == 0 || line[0] == '#') continue;
    int c1 = line.indexOf(':');
    if (c1 <= 0) continue;
    int c2 = line.indexOf(':', c1 + 1);
    String lu = line.substring(0, c1);
    lu.trim();
    String lp = (c2 >= 0) ? line.substring(c1 + 1, c2) : line.substring(c1 + 1);
    lp.trim();
    if (u == lu && verifyUserPassword(p, lp)) return true;
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

// Blocking variant: waits indefinitely until a line is entered over Serial.
// Trims whitespace; caller should validate non-empty as needed.
String waitForSerialInputBlocking() {
  for (;;) {
    if (Serial.available()) {
      String input = Serial.readStringUntil('\n');
      input.trim();
      return input;
    }
    delay(10);
  }
}

void firstTimeSetupIfNeeded() {
  // Rely solely on presence of users storage; no NVS flag
  bool haveUsers = LittleFS.exists(USERS_JSON_FILE) || LittleFS.exists(USERS_FILE);
  if (haveUsers) return;  // already initialized

  broadcastOutput("");
  broadcastOutput("FIRST-TIME SETUP");
  broadcastOutput("----------------");
  broadcastOutput("Enter admin username (cannot be blank): ");
  String u = "";
  while (u.length() == 0) {
    u = waitForSerialInputBlocking();
    if (u.length() == 0) {
      broadcastOutput("Username cannot be blank. Please enter admin username: ");
    }
  }

  String p = "";
  while (p.length() == 0) {
    broadcastOutput("Enter admin password (cannot be blank): ");
    p = waitForSerialInputBlocking();
    p.trim();
    if (p.length() == 0) {
      broadcastOutput("Password cannot be blank. Please enter admin password: ");
    }
  }

  // Create users.json with admin (ID 1) and nextId field - hash the password
  String hashedPassword = hashUserPassword(p);
  String j = "{\n  \"version\": 1,\n  \"nextId\": 2,\n  \"users\": [\n    {\n      \"id\": 1,\n      \"username\": \"" + u + "\",\n      \"password\": \"" + hashedPassword + "\",\n      \"role\": \"admin\"\n    }\n  ]\n}\n";
  if (!writeText(USERS_JSON_FILE, j)) {
    broadcastOutput("ERROR: Failed to write users.json");
  } else {
    broadcastOutput("Saved /users.json");
  }

  // Create automations.json (empty) on first-time setup
  if (!LittleFS.exists(AUTOMATIONS_JSON_FILE)) {
    String a = "{\n  \"version\": 1,\n  \"automations\": []\n}\n";
    if (!writeAutomationsJsonAtomic(a)) {
      broadcastOutput("ERROR: Failed to write automations.json");
    } else {
      broadcastOutput("Saved /automations.json");
    }
  }

  // WiFi setup
  broadcastOutput("");
  broadcastOutput("WiFi Setup");
  broadcastOutput("----------");
  broadcastOutput("Enter WiFi SSID (cannot be blank): ");
  String wifiSSID = "";
  while (wifiSSID.length() == 0) {
    wifiSSID = waitForSerialInputBlocking();
    wifiSSID.trim();
    if (wifiSSID.length() == 0) {
      broadcastOutput("WiFi SSID cannot be blank. Please enter WiFi SSID: ");
    }
  }

  String wifiPass = "";
  while (wifiPass.length() == 0) {
    broadcastOutput("Enter WiFi password (cannot be blank): ");
    wifiPass = waitForSerialInputBlocking();
    wifiPass.trim();
    if (wifiPass.length() == 0) {
      broadcastOutput("WiFi password cannot be blank. Please enter WiFi password: ");
    }
  }

  // Initialize unified settings with defaults
  settingsDefaults();
  // Route commands through unified executor (auth + logging) via helper to avoid type-order issues
  int networksBefore = gWifiNetworkCount;
  runUnifiedSystemCommand(String("wifiadd ") + wifiSSID + " " + wifiPass + " 1 0");
  broadcastOutput("Networks before add: " + String(networksBefore) + ", after add: " + String(gWifiNetworkCount));
  // Note: WiFi connection will be handled by setupWiFi() during normal boot sequence
}

// ==========================
// WiFi Password Encryption (minimal XOR with device-unique key)
// ==========================

static String getDeviceEncryptionKey() {
  // Create device-unique key from Chip ID only (deterministic)
  uint64_t chipId = ESP.getEfuseMac();
  String key = String((uint32_t)(chipId >> 32), HEX) + 
               String((uint32_t)chipId, HEX);
  // Pad key to ensure sufficient length for XOR
  while (key.length() < 32) {
    key += key; // Double the key until we have enough length
  }
  return key;
}

static String encryptWifiPassword(const String& password) {
  if (password.length() == 0) return "";
  
  String key = getDeviceEncryptionKey();
  String encrypted = "";
  
  for(int i = 0; i < password.length(); i++) {
    char encChar = password[i] ^ key[i % key.length()];
    // Convert to hex to ensure JSON-safe characters
    if (encChar < 16) encrypted += "0";
    encrypted += String((uint8_t)encChar, HEX);
  }
  
  return "ENC:" + encrypted; // Prefix to identify encrypted passwords
}

static String decryptWifiPassword(const String& encryptedPassword) {
  if (encryptedPassword.length() == 0) return "";
  if (!encryptedPassword.startsWith("ENC:")) {
    // Not encrypted, return as-is (backward compatibility)
    return encryptedPassword;
  }
  
  String hexData = encryptedPassword.substring(4); // Remove "ENC:" prefix
  String key = getDeviceEncryptionKey();
  String decrypted = "";
  
  // Convert hex back to characters and decrypt
  for(int i = 0; i < hexData.length(); i += 2) {
    if (i + 1 < hexData.length()) {
      String hexByte = hexData.substring(i, i + 2);
      uint8_t encChar = strtol(hexByte.c_str(), NULL, 16);
      char decChar = encChar ^ key[(i/2) % key.length()];
      decrypted += decChar;
    }
  }
  
  return decrypted;
}

// ==========================
// User Password Hashing (SHA-256 with device-specific salt)
// ==========================

static String hashUserPassword(const String& password) {
  if (password.length() == 0) return "";
  
  String salt = getDeviceEncryptionKey(); // Use same device-specific key as salt
  String saltedPassword = password + salt;
  
  // Simple hash implementation using built-in hash function
  // Note: For production, consider using a more robust hash like SHA-256
  uint32_t hash = 0;
  for (int i = 0; i < saltedPassword.length(); i++) {
    hash = hash * 31 + (uint8_t)saltedPassword[i];
    hash ^= (hash >> 16);
  }
  
  // Convert to hex string for storage
  String hashStr = String(hash, HEX);
  while (hashStr.length() < 8) hashStr = "0" + hashStr; // Pad to 8 chars
  
  return "HASH:" + hashStr; // Prefix to identify hashed passwords
}

static bool verifyUserPassword(const String& inputPassword, const String& storedHash) {
  if (inputPassword.length() == 0 || storedHash.length() == 0) return false;
  
  // If stored password doesn't start with HASH:, it's plaintext (backward compatibility)
  if (!storedHash.startsWith("HASH:")) {
    // For migration period, allow plaintext comparison
    // TODO: Remove this after all passwords are migrated
    return (inputPassword == storedHash);
  }
  
  // Hash the input password and compare
  String inputHash = hashUserPassword(inputPassword);
  return (inputHash == storedHash);
}

// Test command for encryption (temporary - for verification)
static String cmd_testencryption(const String& originalCmd) {
  RETURN_VALID_IF_VALIDATE();
  // Admin check now handled by executeCommand pipeline
  
  String args = originalCmd.substring(15); // Remove "testencryption "
  args.trim();
  
  if (args.length() == 0) {
    return "Usage: testencryption <password_to_test>";
  }
  
  String encrypted = encryptWifiPassword(args);
  String decrypted = decryptWifiPassword(encrypted);
  
  String result = "Encryption Test:\n";
  result += "Original:  '" + args + "'\n";
  result += "Encrypted: '" + encrypted + "'\n";
  result += "Decrypted: '" + decrypted + "'\n";
  result += "Match: " + String((args == decrypted) ? "YES" : "NO");
  
  return result;
}

// Test command for password hashing (temporary - for verification)
static String cmd_testpassword(const String& originalCmd) {
  RETURN_VALID_IF_VALIDATE();
  // Admin check now handled by executeCommand pipeline
  
  String args = originalCmd.substring(13); // Remove "testpassword "
  args.trim();
  
  if (args.length() == 0) {
    return "Usage: testpassword <password_to_test>";
  }
  
  String hashed = hashUserPassword(args);
  bool verified = verifyUserPassword(args, hashed);
  bool wrongVerified = verifyUserPassword("wrongpassword", hashed);
  
  String result = "Password Hashing Test:\n";
  result += "Original:  '" + args + "'\n";
  result += "Hashed:    '" + hashed + "'\n";
  result += "Verify Correct: " + String(verified ? "YES" : "NO") + "\n";
  result += "Verify Wrong:   " + String(wrongVerified ? "YES" : "NO") + "\n";
  result += "System Status: " + String((verified && !wrongVerified) ? "WORKING" : "ERROR");
  
  return result;
}

// ==========================
// NTP (minimal)
// ==========================

void setupNTP() {
  long gmtOffset = (long)gSettings.tzOffsetMinutes * 60;  // seconds
  DEBUG_DATETIMEF("Configuring NTP: server=%s, gmtOffset=%ld seconds (%d minutes)",
                  gSettings.ntpServer.c_str(), gmtOffset, gSettings.tzOffsetMinutes);
  configTime(gmtOffset, 0, gSettings.ntpServer.c_str());
}

time_t nowEpoch() {
  return time(nullptr);
}

// ==========================
// HTTP auth helpers (esp_http_server)
// ==========================

static void getClientIP(httpd_req_t* req, String& ipOut) {
  ipOut = "-";
  int sockfd = httpd_req_to_sockfd(req);
  if (sockfd < 0) { return; }
  struct sockaddr_storage addr;
  socklen_t len = sizeof(addr);
  if (getpeername(sockfd, (struct sockaddr*)&addr, &len) == 0) {
    char buf[64];
    if (addr.ss_family == AF_INET) {
      struct sockaddr_in* a = (struct sockaddr_in*)&addr;
      inet_ntop(AF_INET, &(a->sin_addr), buf, sizeof(buf));
      ipOut = String(buf);
      return;
    } else if (addr.ss_family == AF_INET6) {
      struct sockaddr_in6* a6 = (struct sockaddr_in6*)&addr;
      inet_ntop(AF_INET6, &(a6->sin6_addr), buf, sizeof(buf));
      ipOut = String(buf);
      return;
    }
  }
  return;
}

bool parseBasicAuth(httpd_req_t* req, String& userOut, String& passOut, bool& headerPresent) {
  headerPresent = false;
  userOut = "";
  passOut = "";
  size_t len = httpd_req_get_hdr_value_len(req, "Authorization");
  if (len == 0) return false;
  char* buf = (char*)ps_alloc(len + 1, AllocPref::PreferPSRAM, "http.auth");
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
  if (in_len > 180) {
    gExpectedAuthHeader = "";
    return;
  }  // safety cap
  if (mbedtls_base64_encode(out_buf, sizeof(out_buf), &out_len,
                            (const unsigned char*)creds.c_str(), in_len)
        == 0
      && out_len > 0) {
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
  String out;
  if (!s) return out;
  out.reserve(strlen(s) * 3);
  auto hex = [](uint8_t v) -> char {
    const char* H = "0123456789ABCDEF";
    return H[v & 0x0F];
  };
  for (const char* p = s; *p; ++p) {
    char c = *p;
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
      out += c;
    } else {
      uint8_t b = static_cast<uint8_t>(c);
      out += '%';
      out += hex((b >> 4) & 0x0F);
      out += hex(b & 0x0F);
    }
  }
  return out;
}

// Minimal URL decoder for application/x-www-form-urlencoded values
static String urlDecode(const String& s) {
  String out;
  out.reserve(s.length());
  for (size_t i = 0; i < s.length(); ++i) {
    char c = s[i];
    if (c == '+') {
      out += ' ';
    } else if (c == '%' && i + 2 < s.length()) {
      auto hexv = [](char ch) -> int {
        if (ch >= '0' && ch <= '9') return ch - '0';
        if (ch >= 'a' && ch <= 'f') return 10 + ch - 'a';
        if (ch >= 'A' && ch <= 'F') return 10 + ch - 'A';
        return -1;
      };
      int hi = hexv(s[i + 1]);
      int lo = hexv(s[i + 2]);
      if (hi >= 0 && lo >= 0) {
        out += char((hi << 4) | lo);
        i += 2;
      } else {
        out += c;
      }
    } else {
      out += c;
    }
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
  broadcastOutput(String("[auth] redirectToLogin: uri=") + uri + ", loc=" + loc);
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
static String gSessUser;                                                  // pending only
static String gSessToken;                                                 // unused in multi-session, kept for backward compatibility in flow
static unsigned long gSessExpiry = 0;                                     // unused in multi-session
static const unsigned long SESSION_TTL_MS = 24UL * 60UL * 60UL * 1000UL;  // 24h

// Multi-session support
struct SessionEntry {
  String sid;   // session id (cookie value)
  String user;  // username
  String bootId; // boot ID when session was created (for detecting restarts)
  unsigned long createdAt = 0;
  unsigned long lastSeen = 0;
  unsigned long expiresAt = 0;
  String ip;
  String notice;                   // legacy single notice (kept for compatibility)
  // Small ring buffer for notices to avoid drops during reconnects
  String noticeQueue[4];
  int nqHead = 0;
  int nqTail = 0;
  int nqCount = 0;
  // When a notice is queued, enter a short 'burst' window to reconnect faster
  unsigned long noticeBurstUntil = 0;  // millis() timestamp; 0 means idle
  bool needsNotificationTick = false;  // set when there are pending notices to deliver
  uint32_t lastSensorSeqSent = 0;  // last sensor-status sequence sent over SSE
  bool needsStatusUpdate = false;  // flag to trigger status refresh on next request
  int sockfd = -1;                 // socket file descriptor for force disconnect
  bool revoked = false;            // session has been revoked but kept alive for notice delivery
};

// Forward declarations for SSE notice helpers (avoid Arduino autoproto issues)
static inline void sseEnqueueNotice(SessionEntry& s, const String& msg);
static inline bool sseDequeueNotice(SessionEntry& s, String& out);

static const int MAX_SESSIONS = 12;
static SessionEntry* gSessions = nullptr;

// Auth cache for high-frequency endpoints
struct AuthCache {
  String sessionId;
  String user;
  unsigned long validUntil;
  String ip;
};
static AuthCache gAuthCache = { "", "", 0, "" };

// Temporary logout reason storage (IP-based, expires after 60 seconds)
struct LogoutReason {
  String ip;
  String reason;
  unsigned long timestamp;
};
static const int MAX_LOGOUT_REASONS = 8;
static LogoutReason* gLogoutReasons = nullptr;

// Boot ID for session versioning - changes on each reboot
static String gBootId = "";

// Store logout reason for IP address (with rate limiting)
static void storeLogoutReason(const String& ip, const String& reason) {
  if (ip.length() == 0 || reason.length() == 0) return;
  
  unsigned long now = millis();
  int idx = -1;
  
  // Find existing entry for this IP
  for (int i = 0; i < MAX_LOGOUT_REASONS; i++) {
    if (gLogoutReasons[i].ip == ip) {
      idx = i;
      // Rate limiting: don't store same reason within 5 seconds
      if (gLogoutReasons[i].reason == reason && (now - gLogoutReasons[i].timestamp) < 5000) {
        return; // Skip storing duplicate reason too soon
      }
      break;
    }
  }
  
  // Find empty slot if no existing entry
  if (idx == -1) {
    for (int i = 0; i < MAX_LOGOUT_REASONS; i++) {
      if (gLogoutReasons[i].ip.length() == 0) {
        idx = i;
        break;
      }
    }
  }
  
  // If no slot found, use oldest entry
  if (idx == -1) {
    unsigned long oldest = now;
    for (int i = 0; i < MAX_LOGOUT_REASONS; i++) {
      if (gLogoutReasons[i].timestamp < oldest) {
        oldest = gLogoutReasons[i].timestamp;
        idx = i;
      }
    }
  }
  
  // Store the logout reason
  gLogoutReasons[idx].ip = ip;
  gLogoutReasons[idx].reason = reason;
  gLogoutReasons[idx].timestamp = now;
  
  // Debug: Log logout reason storage
  DEBUG_AUTHF("Stored logout reason for IP '%s': '%s'", ip.c_str(), reason.c_str());
}

// Get and clear logout reason for IP address
static String getLogoutReason(const String& ip) {
  unsigned long now = millis();
  for (int i = 0; i < MAX_LOGOUT_REASONS; ++i) {
    if (gLogoutReasons[i].ip == ip && gLogoutReasons[i].ip.length() > 0) {
      // Check if reason has expired (30 seconds)
      if (now - gLogoutReasons[i].timestamp > 30000) {
        gLogoutReasons[i] = LogoutReason();  // Clear expired reason
        continue;
      }
      
      String reason = gLogoutReasons[i].reason;
      // Don't clear immediately - let it expire naturally or be overwritten
      
      // Debug: Log logout reason retrieval
      DEBUG_AUTHF("Retrieved logout reason for IP '%s': '%s'", ip.c_str(), reason.c_str());
      return reason;
    }
  }
  
  // Debug: No logout reason found
  DEBUG_AUTHF("No logout reason found for IP '%s'", ip.c_str());
  return String();
}

// Function called from auth required page to get logout reason
String getLogoutReasonForAuthPage(httpd_req_t* req) {
  // Check for logout reason - first try IP-based storage, then URL parameters
  String logoutReason = "";
  String clientIP;
  getClientIP(req, clientIP);
  if (clientIP.length() > 0) {
    logoutReason = getLogoutReason(clientIP);
    Serial.printf("[AUTH_PAGE_DEBUG] Login page for IP '%s' - logout reason: '%s'\n", clientIP.c_str(), logoutReason.c_str());
  }

  // Fallback to URL parameters if no stored reason
  if (logoutReason.length() == 0 && req && req->uri) {
    String uri = String(req->uri);
    int reasonPos = uri.indexOf("reason=");
    if (reasonPos >= 0) {
      reasonPos += 7;  // Skip "reason="
      int endPos = uri.indexOf('&', reasonPos);
      if (endPos < 0) endPos = uri.length();
      logoutReason = uri.substring(reasonPos, endPos);
      // URL decode basic characters
      logoutReason.replace("%20", " ");
      logoutReason.replace("%21", "!");
      logoutReason.replace("%2E", ".");
    }
  }

  return logoutReason;
}

// Enhanced sensors status endpoint with session update checking
esp_err_t handleSensorsStatusWithUpdates(httpd_req_t* req) {
  AuthContext ctx;
  ctx.transport = AUTH_HTTP;
  ctx.opaque = req;
  ctx.path = "/api/sensors/status-updates";
  getClientIP(req, ctx.ip);
  if (!tgRequireAuth(ctx)) return ESP_OK;

  // Check if this session needs a status update notification
  int sessIdx = findSessionIndexBySID(getCookieSID(req));
  bool needsRefresh = false;
  if (sessIdx >= 0) {
    // Only report refresh if explicitly flagged (do not clear here; SSE will clear after sending)
    if (gSessions[sessIdx].needsStatusUpdate) {
      needsRefresh = true;
      DEBUG_SSEF("Session %d needs status update (reporting via /status); SSE will deliver and clear", sessIdx);
    }
  }

  httpd_resp_set_type(req, "application/json");
  String j = buildSensorStatusJson();
  if (needsRefresh) {
    // Add a refresh flag to trigger UI update
    j.replace("}", ",\"needsRefresh\":true}");
  }
  httpd_resp_send(req, j.c_str(), HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

// System status endpoint for dashboard one-shot fetch
esp_err_t handleSystemStatus(httpd_req_t* req) {
  AuthContext ctx;
  ctx.transport = AUTH_HTTP;
  ctx.opaque = req;
  ctx.path = "/api/system";
  getClientIP(req, ctx.ip);
  if (!tgRequireAuth(ctx)) return ESP_OK;

  httpd_resp_set_type(req, "application/json");

  // Get system info (same as SSE system event)
  unsigned long uptimeMs = millis();
  unsigned long seconds = uptimeMs / 1000UL;
  unsigned long minutes = seconds / 60UL;
  unsigned long hours = minutes / 60UL;
  String uptimeHms = String(hours) + "h " + String(minutes % 60UL) + "m " + String(seconds % 60UL) + "s";
  String ssid = WiFi.SSID();
  String ip = WiFi.localIP().toString();
  int rssi = WiFi.RSSI();

  size_t heapFree = ESP.getFreeHeap();
  size_t heapTotal = ESP.getHeapSize();
  size_t psramFree = ESP.getFreePsram();
  size_t psramTotal = ESP.getPsramSize();

  int heapKb = heapFree / 1024;
  int heapTotalKb = heapTotal / 1024;
  int psramFreeKb = psramFree / 1024;
  int psramTotalKb = psramTotal / 1024;

  String sysJson = String("{") + "\"uptime_hms\":\"" + uptimeHms + "\"," + "\"net\":{\"ssid\":\"" + ssid + "\",\"ip\":\"" + ip + "\",\"rssi\":" + String(rssi) + "}," + "\"mem\":{\"heap_free_kb\":" + String(heapKb) + ",\"heap_total_kb\":" + String(heapTotalKb) + ",\"psram_total_kb\":" + String(psramTotalKb) + ",\"psram_free_kb\":" + String(psramFreeKb) + "}}";

  httpd_resp_send(req, sysJson.c_str(), HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

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
  int oldest = -1;
  unsigned long tOld = 0xFFFFFFFFUL;
  for (int i = 0; i < MAX_SESSIONS; ++i) {
    if (gSessions[i].expiresAt == 0 || (long)(gSessions[i].expiresAt - tOld) < 0) {
      oldest = i;
      tOld = gSessions[i].expiresAt;
    }
  }
  return oldest;
}

static void pruneExpiredSessions() {
  static unsigned long lastPrune = 0;
  unsigned long now = millis();

  // Only prune every 30 seconds to reduce overhead
  if (now - lastPrune < 30000) return;
  lastPrune = now;

  for (int i = 0; i < MAX_SESSIONS; ++i) {
    if (gSessions[i].sid.length() && gSessions[i].expiresAt > 0 && (long)(now - gSessions[i].expiresAt) >= 0) {
      gSessions[i] = SessionEntry();
    }
  }
}

static String getCookieSID(httpd_req_t* req) {
  size_t hdr_len = httpd_req_get_hdr_value_len(req, "Cookie");
  if (hdr_len == 0) {
    DEBUG_AUTHF("No Cookie header for URI: %s", req->uri);
    return "";
  }
  if (hdr_len > 2048) {
    DEBUG_AUTHF("Cookie header unusually large (%d bytes) – capping read to 2048", hdr_len);
    hdr_len = 2048;
  }
  // Read into a PSRAM buffer to avoid heap allocations and also avoid large stack usage
  static char* cookieBuf = nullptr;
  if (!cookieBuf) {
    cookieBuf = (char*)ps_alloc(2049, AllocPref::PreferPSRAM, "cookie.buf");
    if (!cookieBuf) return "";
  }
  if (httpd_req_get_hdr_value_str(req, "Cookie", cookieBuf, 2049) != ESP_OK) {
    DEBUG_AUTHF("Failed to get Cookie header for URI: %s", req->uri);
    return "";
  }
  // Trim leading spaces
  char* p = cookieBuf;
  while (*p == ' ' || *p == '\t') ++p;
  // Find "session=" token
  const char key[] = "session=";
  const size_t klen = sizeof(key) - 1;
  char* s = p;
  char* sidStart = nullptr;
  while (*s) {
    if (strncmp(s, key, klen) == 0) {
      sidStart = s + klen;
      break;
    }
    // advance to next token (semicolon-separated), but also check every position for robustness
    ++s;
  }
  if (!sidStart) {
    DEBUG_AUTHF("No session cookie found");
    return "";
  }
  // Find end of value (semicolon or end)
  char* sidEnd = sidStart;
  while (*sidEnd && *sidEnd != ';') ++sidEnd;
  // Create String from the exact slice by temporarily NUL-terminating
  char saved = *sidEnd;
  *sidEnd = '\0';
  String sid = String(sidStart);
  *sidEnd = saved;
  return sid;
}

// Helper: enqueue a targeted revoke notice for a specific session index.
// Marks session for revocation and sends notice to client for popup/redirect.
// Session will be cleared on next auth check or after notice delivery.
static void enqueueTargetedRevokeForSessionIdx(int idx, const String& reasonMsg) {
  if (idx < 0 || idx >= MAX_SESSIONS) return;
  if (gSessions[idx].sid.length() == 0) return;
  String msg = String("[revoke] ") + (reasonMsg.length() ? reasonMsg : String("Your session has been signed out by an administrator."));
  
  // Mark session as revoked but keep it alive for notice delivery
  gSessions[idx].revoked = true;
  gSessions[idx].notice = msg;
  // Set grace period for SSE delivery (30 seconds from now)
  gSessions[idx].expiresAt = millis() + 30000UL;
  
  // Send SSE notice while session still exists
  sseEnqueueNotice(gSessions[idx], msg);
}

// Cached auth check for high-frequency endpoints (sensors)
static bool isAuthedCached(httpd_req_t* req, String& outUser) {
  String ip;
  getClientIP(req, ip);
  String sid = getCookieSID(req);
  unsigned long now = millis();

  // Check cache first (valid for 30 seconds)
  if (gAuthCache.sessionId == sid && gAuthCache.ip == ip && now < gAuthCache.validUntil && gAuthCache.sessionId.length() > 0) {
    outUser = gAuthCache.user;
    return true;
  }

  // Full auth check on cache miss
  bool result = isAuthed(req, outUser);
  if (result) {
    gAuthCache.sessionId = sid;
    gAuthCache.user = outUser;
    gAuthCache.validUntil = now + 30000;  // Cache for 30 seconds
    gAuthCache.ip = ip;
  } else {
    // Clear cache on auth failure
    gAuthCache = { "", "", 0, "" };
  }
  return result;
}

static bool getHeaderValue(httpd_req_t* req, const char* name, String& out) {
  size_t len = httpd_req_get_hdr_value_len(req, name);
  if (!len) {
    broadcastOutput(String("[auth] header missing: ") + name);
    return false;
  }
  std::unique_ptr<char, void (*)(void*)> buf((char*)ps_alloc(len + 1, AllocPref::PreferPSRAM, "http.header"), free);
  if (httpd_req_get_hdr_value_str(req, name, buf.get(), len + 1) != ESP_OK) return false;
  out = String(buf.get());
  broadcastOutput(String("[auth] got header ") + name + ": " + out);
  return true;
}

static bool getCookieValue(httpd_req_t* req, const char* key, String& out) {
  char buf[256];
  size_t len = sizeof(buf);
  if (httpd_req_get_cookie_val(req, key, buf, &len) == ESP_OK) {
    out = String(buf);
    broadcastOutput(String("[auth] cookie ") + key + "=\"" + out + "\"");
    return true;
  }
  // Do not fall back to manual parsing to avoid misreads; simply report absence.
  broadcastOutput(String("[auth] cookie key not found: ") + key);
  return false;
}

static String makeSessToken() {
  // Hex-only token: 96 bits random + 32 bits time (approx)
  uint32_t r1 = esp_random();
  uint32_t r2 = esp_random();
  uint32_t r3 = esp_random();
  uint32_t t = millis();
  char buf[(8 * 4) + 1];  // 8 hex chars * 4 values + NUL
  snprintf(buf, sizeof(buf), "%08x%08x%08x%08x", r1, r2, r3, t);
  return String(buf);
}

static String setSession(httpd_req_t* req, const String& u) {
  pruneExpiredSessions();

  // Clear auth cache to prevent stale authentication
  gAuthCache = { "", "", 0, "" };

  // Get current client IP to avoid storing logout reason for same IP
  String currentIP;
  getClientIP(req, currentIP);
  unsigned long nowMs = millis();

  // If a valid session already exists for this user from the same IP, reuse it
  for (int i = 0; i < MAX_SESSIONS; ++i) {
    if (gSessions[i].sid.length() > 0 && gSessions[i].user == u) {
      // Validate not expired and not revoked
      if (!(gSessions[i].expiresAt > 0 && (long)(nowMs - gSessions[i].expiresAt) >= 0) && !gSessions[i].revoked) {
        if (gSessions[i].ip == currentIP) {
          // Refresh and reuse existing session
          gSessions[i].lastSeen = nowMs;
          gSessions[i].expiresAt = nowMs + SESSION_TTL_MS;
          char cookieBuf[96];
          snprintf(cookieBuf, sizeof(cookieBuf), "session=%s; Path=/", gSessions[i].sid.c_str());
          esp_err_t sc = httpd_resp_set_hdr(req, "Set-Cookie", cookieBuf);
          DEBUG_AUTHF("Reusing existing session idx=%d user=%s sid=%s | refreshed", i, u.c_str(), gSessions[i].sid.c_str());
          broadcastOutput(String("[auth] reusedSession user=") + u + ", sid=" + gSessions[i].sid + ", exp(ms)=" + String(gSessions[i].expiresAt));
          DEBUG_AUTHF("Set-Cookie (reuse) rc=%d: %s", (int)sc, cookieBuf);
          return gSessions[i].sid;
        }
      }
    }
  }

  // Enforce 1 session per user limit - immediately clear any existing sessions for this user
  for (int i = 0; i < MAX_SESSIONS; ++i) {
    if (gSessions[i].sid.length() > 0 && gSessions[i].user == u) {
      // Found existing session for this user - only store logout reason if different IP
      if (gSessions[i].ip.length() > 0 && gSessions[i].ip != currentIP) {
        storeLogoutReason(gSessions[i].ip, "You were signed out because you logged in from another device.");
      }
      broadcastOutput(String("[auth] Clearing existing session for user: ") + u + " (session limit enforcement)");
      if (gSessions[i].sockfd >= 0) {
        httpd_sess_trigger_close(server, gSessions[i].sockfd);
      }
      gSessions[i] = SessionEntry();  // Clear immediately
    }
  }

  int idx = findFreeSessionIndex();
  if (idx < 0) idx = 0;  // fallback
  SessionEntry s;
  s.sid = makeSessToken();
  s.user = u;
  s.bootId = gBootId;  // Store current boot ID for version checking
  s.createdAt = millis();
  s.lastSeen = s.createdAt;
  s.expiresAt = s.createdAt + SESSION_TTL_MS;
  
  // Debug: Log session creation with boot ID
  DEBUG_AUTHF("Creating session for user '%s' with bootId '%s' (current: '%s')", 
              u.c_str(), s.bootId.c_str(), gBootId.c_str());
  String ip;
  getClientIP(req, ip);
  s.ip = ip;
  s.sockfd = httpd_req_to_sockfd(req);  // Store socket descriptor for force disconnect
  gSessions[idx] = s;
  // New session should reconcile UI immediately on next SSE ping
  gSessions[idx].needsStatusUpdate = true;
  gSessions[idx].lastSensorSeqSent = 0;
  DEBUG_AUTHF("New session created idx=%d user=%s sid=%s | needsStatusUpdate=1", idx, u.c_str(), s.sid.c_str());

  // Set new session cookie with minimal attributes for maximum compatibility
  char cookieBuf[96];
  snprintf(cookieBuf, sizeof(cookieBuf), "session=%s; Path=/", s.sid.c_str());
  esp_err_t sc = httpd_resp_set_hdr(req, "Set-Cookie", cookieBuf);
  DEBUG_AUTHF("Setting session cookie: %s", cookieBuf);
  DEBUG_AUTHF("Set-Cookie rc=%d", (int)sc);

  broadcastOutput(String("[auth] setSession user=") + u + ", sid=" + s.sid + ", exp(ms)=" + String(s.expiresAt));
  return s.sid;
}

static void clearSession(httpd_req_t* req) {
  // Revoke current session by cookie value
  String sid = getCookieSID(req);
  int idx = findSessionIndexBySID(sid);
  if (idx >= 0) { gSessions[idx] = SessionEntry(); }
  // Clear session cookie client-side
  httpd_resp_set_hdr(req, "Set-Cookie", "session=; Path=/; Max-Age=0; HttpOnly; SameSite=Strict");
  broadcastOutput("[auth] clearSession (revoked current if present)");
}

static bool isAuthed(httpd_req_t* req, String& outUser) {
  const char* uri = req && req->uri ? req->uri : "(null)";
  pruneExpiredSessions();
  String sid = getCookieSID(req);
  String ip;
  getClientIP(req, ip);

  if (sid.length() == 0) {
    broadcastOutput(String("[auth] no session cookie for uri=") + uri);
    return false;
  }

  int idx = findSessionIndexBySID(sid);
  if (idx < 0) {
    broadcastOutput(String("[auth] unknown SID for uri=") + uri);
    
    // Rate limit session debug messages per IP
    static String lastDebugIP = "";
    static unsigned long lastDebugTime = 0;
    unsigned long now = millis();
    
    if (ip != lastDebugIP || (now - lastDebugTime) > 5000) {
      DEBUG_AUTHF("No session found for SID, current boot ID: %s", gBootId.c_str());
      
      // If someone has a session cookie but we have no sessions, it's likely due to a reboot
      if (sid.length() > 0) {
        DEBUG_AUTHF("Client has session cookie but no sessions exist - likely system restart");
        storeLogoutReason(ip, "Your session expired due to a system restart. Please log in again.");
      }
      
      lastDebugIP = ip;
      lastDebugTime = now;
    } else {
      // Still store logout reason but don't spam debug logs
      if (sid.length() > 0) {
        storeLogoutReason(ip, "Your session expired due to a system restart. Please log in again.");
      }
    }
    
    return false;
  }

  // Check if session was cleared (sockfd = -1 indicates cleared session)
  if (gSessions[idx].sid.length() == 0) {
    broadcastOutput(String("[auth] cleared session for uri=") + uri);
    return false;
  }

  // Check if session is from a previous boot (boot ID mismatch)
  // Rate limit boot validation debug messages per IP
  static String lastBootDebugIP = "";
  static unsigned long lastBootDebugTime = 0;
  unsigned long bootNow = millis();
  
  if (ip != lastBootDebugIP || (bootNow - lastBootDebugTime) > 5000) {
    DEBUG_AUTHF("Validating session: user='%s', sessionBootId='%s', currentBootId='%s'", 
                gSessions[idx].user.c_str(), gSessions[idx].bootId.c_str(), gBootId.c_str());
    lastBootDebugIP = ip;
    lastBootDebugTime = bootNow;
  }
  
  if (gSessions[idx].bootId != gBootId) {
    if (ip == lastBootDebugIP && (bootNow - lastBootDebugTime) < 1000) {
      DEBUG_AUTHF("BOOT ID MISMATCH! Session from previous boot. Storing restart message.");
    }
    broadcastOutput(String("[auth] session from previous boot for uri=") + uri);
    storeLogoutReason(ip, "Your session expired due to a system restart. Please log in again.");
    // Clear the stale session
    gSessions[idx] = SessionEntry();
    return false;
  } else {
    if (ip == lastBootDebugIP && (bootNow - lastBootDebugTime) < 1000) {
      DEBUG_AUTHF("Boot ID matches - session is valid for current boot");
    }
  }

  // Check if session was revoked
  if (gSessions[idx].revoked) {
    broadcastOutput(String("[auth] revoked session for uri=") + uri);
    return false;
  }

  unsigned long now = millis();
  if (gSessions[idx].expiresAt > 0 && (long)(now - gSessions[idx].expiresAt) >= 0) {
    // expired
    gSessions[idx] = SessionEntry();
    broadcastOutput(String("[auth] expired SID for uri=") + uri);
    return false;
  }

  // refresh
  gSessions[idx].lastSeen = now;
  gSessions[idx].expiresAt = now + SESSION_TTL_MS;
  outUser = gSessions[idx].user;
  return true;
}

// ==========================
// SSE helpers (debug + writers)
// ==========================

// Debug helper for SSE (uses broadcastOutput for consistency with other debug statements)
static inline void sseDebug(const String& msg) {
  if (gDebugFlags & DEBUG_SSE) { broadcastOutput(String("[SSE] ") + msg); }
}

static bool sseWrite(httpd_req_t* req, const char* chunk) {
  if (!req) {
    sseDebug("sseWrite called with null req");
    return false;
  }
  if (chunk == NULL) {
    // terminate chunked response
    httpd_resp_send_chunk(req, NULL, 0);
    sseDebug("sseWrite: terminated chunked response");
    return true;
  }
  size_t n = strlen(chunk);
  esp_err_t r = httpd_resp_send_chunk(req, chunk, n);
  sseDebug(String("sseWrite: sent chunk bytes=") + String((unsigned)n) + (r == ESP_OK ? " OK" : " FAIL"));
  return r == ESP_OK;
}

static int sseBindSession(httpd_req_t* req, String& outSid) {
  outSid = getCookieSID(req);
  int idx = findSessionIndexBySID(outSid);
  String ip;
  getClientIP(req, ip);

  sseDebug(String("sseBindSession: sid=") + (outSid.length() ? outSid : "<none>") + ", idx=" + String(idx));
  DEBUG_SSEF("sseBindSession: IP=%s SID=%s idx=%d", ip.c_str(), (outSid.length() ? (outSid.substring(0, 8) + "...").c_str() : "<none>"), idx);

  // Validate session still exists and hasn't been cleared
  if (idx >= 0) {
    if (gSessions[idx].sid.length() == 0) {
      DEBUG_SSEF("Session was cleared! Rejecting SSE bind for IP: %s", ip.c_str());
      return -1;  // Session was cleared, reject binding
    }
    // Update socket descriptor for this SSE connection
    int newSockfd = httpd_req_to_sockfd(req);
    if (newSockfd >= 0 && newSockfd != gSessions[idx].sockfd) {
      DEBUG_SSEF("Updating session sockfd from %d to %d", gSessions[idx].sockfd, newSockfd);
      gSessions[idx].sockfd = newSockfd;  // Update to SSE socket
    }
  }

  return idx;
}

static bool sseSessionAliveAndRefresh(int sessIdx, const String& sid) {
  if (sessIdx < 0 || sessIdx >= MAX_SESSIONS) {
    DEBUG_SSEF("Invalid session index: %d", sessIdx);
    return false;
  }
  if (gSessions[sessIdx].sid != sid || sid.length() == 0) {
    DEBUG_SSEF("Session SID mismatch or empty - stored: %s... provided: %s", gSessions[sessIdx].sid.substring(0, 8).c_str(), (sid.length() ? (sid.substring(0, 8) + "...").c_str() : "<none>"));
    return false;
  }
  // Check if session was cleared (empty SID indicates revoked session)
  if (gSessions[sessIdx].sid.length() == 0) {
    DEBUG_SSEF("Session was revoked/cleared - terminating SSE");
    sseDebug("session revoked; closing SSE");
    return false;
  }
  unsigned long now = millis();
  
  // Allow revoked sessions to stay alive briefly for notice delivery
  if (gSessions[sessIdx].revoked) {
    // Check if grace period expired (should be set by revocation function)
    if (gSessions[sessIdx].expiresAt > 0 && (long)(now - gSessions[sessIdx].expiresAt) >= 0) {
      DEBUG_SSEF("Revoked session grace period expired - terminating SSE");
      sseDebug("revoked session grace expired; closing SSE");
      return false;
    }
    // Don't refresh lastSeen or extend expiration for revoked sessions
    return true;
  }
  
  if (gSessions[sessIdx].expiresAt > 0 && (long)(now - gSessions[sessIdx].expiresAt) >= 0) {
    DEBUG_SSEF("Session expired - terminating SSE");
    sseDebug("session expired; closing SSE");
    return false;
  }
  gSessions[sessIdx].lastSeen = now;
  gSessions[sessIdx].expiresAt = now + SESSION_TTL_MS;
  // Throttle verbose refresh logs to once every 30 seconds to avoid spam
  static unsigned long lastDbg = 0;
  if ((long)(now - lastDbg) >= 30000) {
    sseDebug(String("session refreshed; next exp=") + String(gSessions[sessIdx].expiresAt));
    lastDbg = now;
  }
  return true;
}

static bool sseHeartbeat(httpd_req_t* req) {
  sseDebug("heartbeat");
  return sseWrite(req, ":hb\n\n");
}

static bool sseSendLogs(httpd_req_t* req, unsigned long seq, const String& buf) {
  // SSE event: logs, include id for client to track
  // Only send the last N lines to avoid huge payloads blocking the UI
  const int MAX_LINES = 200;  // cap
  // Find start index for the last MAX_LINES lines
  int linesFound = 0;
  int startIdx = buf.length();
  while (startIdx > 0 && linesFound <= MAX_LINES) {
    int prev = buf.lastIndexOf('\n', startIdx - 1);
    if (prev < 0) {
      startIdx = 0;
      break;
    }
    startIdx = prev;
    linesFound++;
  }
  if (linesFound > MAX_LINES && startIdx < (int)buf.length()) {
    // Skip the newline at startIdx to start exactly at next line
    startIdx = startIdx + 1;
  } else if (startIdx < 0) {
    startIdx = 0;
  }

  String out;
  out.reserve(64 + (buf.length() - startIdx));
  out += "id: ";
  out += String(seq);
  out += "\n";
  out += "event: logs\n";
  int start = startIdx;
  int lines = 0;
  while (start < (int)buf.length()) {
    int nl = buf.indexOf('\n', start);
    if (nl < 0) nl = buf.length();
    out += "data: ";
    out += buf.substring(start, nl);
    out += "\n";
    lines++;
    start = nl + 1;
  }
  out += "\n";
  bool ok = sseWrite(req, out.c_str());
  sseDebug(String("sendLogs: seq=") + String(seq) + ", lines=" + String(lines) + (ok ? " OK" : " FAIL"));
  return ok;
}

static bool sseSendNotice(httpd_req_t* req, const String& note) {
  // Wrap message as JSON string payload
  String safe = note;  // minimal escaping
  safe.replace("\n", "\\n");
  String out = String("event: notice\n") + "data: {\"msg\":\"" + safe + "\"}" + "\n\n";
  bool ok = sseWrite(req, out.c_str());
  sseDebug(String("sendNotice: len=") + String((unsigned)note.length()) + (ok ? " OK" : " FAIL"));
  return ok;
}

// Send a 'fetch' event instructing the client to perform one-shot GET(s)
// jsonPayload must be a compact JSON string, e.g. {"what":["status","tof"]}
static bool sseSendFetch(httpd_req_t* req, const String& jsonPayload) {
  String out = String("event: fetch\n") + "data: " + jsonPayload + "\n\n";
  bool ok = sseWrite(req, out.c_str());
  sseDebug(String("sendFetch: ") + (ok ? "OK" : "FAIL") + ", json=" + jsonPayload);
  return ok;
}

// SSE endpoint: push per-session notices without polling
esp_err_t handleEvents(httpd_req_t* req) {
  if (!req) {
    sseDebug("handleEvents: null req");
    return ESP_OK;
  }
  String u;
  String ip;
  getClientIP(req, ip);
  sseDebug(String("handleEvents: incoming from ") + (ip.length() ? ip : "<no-ip>") + ", uri=" + (req && req->uri ? req->uri : "<null>"));

  {
    AuthContext ctx;
    ctx.transport = AUTH_HTTP;
    ctx.opaque = req;
    ctx.path = "/api/events";
    getClientIP(req, ctx.ip);
    if (!tgRequireAuth(ctx)) {
      DEBUG_AUTHF("/api/events (SSE) DENIED - no valid session for IP: %s", ip.c_str());
      sseDebug("handleEvents: auth failed; sending 401");
      return ESP_OK;
    }
  }
  DEBUG_AUTHF("/api/events (SSE) ALLOWED for user: %s from IP: %s", u.c_str(), ip.c_str());

  // Prepare SSE headers
  httpd_resp_set_type(req, "text/event-stream");
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
  httpd_resp_set_hdr(req, "Connection", "keep-alive");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Credentials", "true");
  // Disable proxy buffering if any
  httpd_resp_set_hdr(req, "X-Accel-Buffering", "no");
  sseDebug("handleEvents: SSE headers set");

  // Bind session BEFORE sending any body chunks
  String sid;
  int sessIdx = sseBindSession(req, sid);
  if (sessIdx < 0) {
    sseDebug("handleEvents: no session bound; closing");
    sseWrite(req, NULL);
    return ESP_OK;
  }
  sseDebug(String("handleEvents: bound session idx=") + String(sessIdx) + ", sid=" + (sid.length() ? sid : "<none>"));
  DEBUG_SSEF("handleEvents: bound session details | idx=%d sid=%s needsStatusUpdate=%d lastSensorSeqSent=%d",
             sessIdx, (sid.length() ? (sid.substring(0, 8) + "...").c_str() : "<none>"),
             gSessions[sessIdx].needsStatusUpdate ? 1 : 0, gSessions[sessIdx].lastSensorSeqSent);

  // Advise browser to backoff reconnects (15s) and send initial comment to open stream
  // Dynamic retry backoff: fast during notice bursts, slower when idle
  unsigned long nowRetry = millis();
  // Fast retry if we're in a notification burst or explicitly flagged for notification tick
  unsigned long retryMs = (gSessions[sessIdx].needsNotificationTick || (nowRetry < gSessions[sessIdx].noticeBurstUntil)) ? 1000UL : 5000UL;
  String retryLine = String("retry: ") + String((unsigned long)retryMs) + String("\n\n");
  if (!sseWrite(req, retryLine.c_str())) {
    sseDebug("handleEvents: failed to send retry hint");
    return ESP_OK;
  }
  if (!sseWrite(req, ":ok\n\n")) {
    sseDebug("handleEvents: failed to send initial :ok");
    return ESP_OK;
  }
  sseDebug("handleEvents: initial :ok sent");

  // Optional: skip pushing logs here to reduce idle SSE payloads
  DEBUG_SSEF("SSE connection established");

  // Immediately push a 'sensor-status' (if needed) and a 'system' snapshot, then keep streaming
  auto sendStatus = [&](const char* reason) {
    String statusJson = buildSensorStatusJson();
    String eventData = String("event: sensor-status\n") + "data: " + statusJson + "\n\n";
    if (gDebugFlags & DEBUG_SSE) {
      DEBUG_SSEF("Sending 'sensor-status' (%d bytes) reason=%s", eventData.length(), reason);
    }
    if (sseWrite(req, eventData.c_str())) {
      gSessions[sessIdx].needsStatusUpdate = false;
      gSessions[sessIdx].lastSensorSeqSent = gSensorStatusSeq;
    }
  };

  auto sendSystem = [&]() {
    unsigned long uptimeMs = millis();
    unsigned long seconds = uptimeMs / 1000UL;
    unsigned long minutes = seconds / 60UL;
    unsigned long hours = minutes / 60UL;
    String uptimeHms = String(hours) + "h " + String(minutes % 60UL) + "m " + String(seconds % 60UL) + "s";

    String ssid = WiFi.isConnected() ? WiFi.SSID() : String("");
    String ip = WiFi.isConnected() ? WiFi.localIP().toString() : String("");
    ssid.replace("\\", "\\\\");
    ssid.replace("\"", "\\\"");
    ip.replace("\\", "\\\\");
    ip.replace("\"", "\\\"");
    int rssi = WiFi.isConnected() ? WiFi.RSSI() : 0;
    int heapTotalKb = (int)(ESP.getHeapSize() / 1024);
    int heapKb = (int)(ESP.getFreeHeap() / 1024);
    int psramTotalKb = (int)(ESP.getPsramSize() / 1024);
    int psramFreeKb = (int)(ESP.getFreePsram() / 1024);

    String sysJson = String("{") + "\"uptime_hms\":\"" + uptimeHms + "\"," + "\"net\":{\"ssid\":\"" + ssid + "\",\"ip\":\"" + ip + "\",\"rssi\":" + String(rssi) + "}," + "\"mem\":{\"heap_free_kb\":" + String(heapKb) + ",\"heap_total_kb\":" + String(heapTotalKb) + ",\"psram_total_kb\":" + String(psramTotalKb) + ",\"psram_free_kb\":" + String(psramFreeKb) + "}}";
    DEBUG_SSEF("Sending system event snapshot (%d bytes json)", sysJson.length());
    {
      String prev = sysJson.substring(0, 80);
      DEBUG_SSEF("SSE->system json: %.*s%s", prev.length(), prev.c_str(), sysJson.length() > 80 ? "..." : "");
    }
    String sysEvent = String("event: system\n") + "data: " + sysJson + "\n\n";
    sseWrite(req, sysEvent.c_str());
  };

  // Initial push: only send heavy payloads if a status update was requested
  if (gSessions[sessIdx].needsStatusUpdate) {
    sendStatus("refresh");
    sendSystem();
  }

  // Conditional short-hold: only keep connection briefly if there are pending notifications.
  // Otherwise close immediately to minimize interference with sensor loops.
  bool wantHold = gSessions[sessIdx].needsNotificationTick ||
                  (gSessions[sessIdx].nqCount > 0) ||
                  (gSessions[sessIdx].notice.length() > 0);
  if (wantHold) {
    unsigned long holdStart = millis();
    const unsigned long holdMs = 600UL; // shorter hold still catches near-term broadcasts
    while ((long)(millis() - holdStart) < (long)holdMs) {
      String n;
      int sent = 0;
      while (sseDequeueNotice(gSessions[sessIdx], n)) {
        DEBUG_SSEF("SSE notice tick send: %s", n.c_str());
        if (!sseSendNotice(req, n)) {
          DEBUG_SSEF("SSE write failed while sending notice; closing");
          holdStart = 0; // force exit
          break;
        }
        sent++;
        if (sent >= 4) break; // bound per-iteration work
      }
      delay(60);
    }
  }

  // If no more notices queued, clear the flag to allow slower retry
  if (gSessions[sessIdx].nqCount == 0 && gSessions[sessIdx].notice.length() == 0) {
    gSessions[sessIdx].needsNotificationTick = false;
  }

  sseWrite(req, NULL);
  return ESP_OK;
}

// Broadcast sensor status to all active sessions (called when sensor state changes)
void broadcastSensorStatusToAllSessions() {
  DEBUG_SSEF("broadcastSensorStatusToAllSessions called - seq: %d", gSensorStatusSeq);
  int flagged = 0;

  // Flag all active sessions as needing status updates
  // They will receive the update when their background SSE connects
  // Pre-pass: dump session table for diagnostics
  for (int i = 0; i < MAX_SESSIONS; i++) {
    if (gSessions[i].sid.length() > 0) {
      DEBUG_SSEF("session[%d] sid=%s user=%s needsStatusUpdate=%d lastSeqSent=%d",
                 i, gSessions[i].sid.c_str(), gSessions[i].user.c_str(),
                 gSessions[i].needsStatusUpdate ? 1 : 0, gSessions[i].lastSensorSeqSent);
    }
  }

  for (int i = 0; i < MAX_SESSIONS; i++) {
    if (gSessions[i].sid.length() > 0) {
      // Do not skip originator; client-side seq handling de-duplicates UI work
      gSessions[i].needsStatusUpdate = true;
      flagged++;
      DEBUG_SSEF("Flagged session %d (SID: %s) for status update", i, gSessions[i].sid.c_str());
    }
  }

  DEBUG_SSEF("Flagging done; total flagged=%d, skipIdx=%d, cause=%s", flagged, gBroadcastSkipSessionIdx, gLastStatusCause.c_str());
  DEBUG_SSEF("All active sessions flagged for status updates - background SSE will deliver");
}

// ---- Notice queue helpers ----
static inline void sseEnqueueNotice(SessionEntry& s, const String& msg) {
  // Prefer queue; fallback to legacy single notice if queue full
  if (s.nqCount < (int)(sizeof(s.noticeQueue) / sizeof(s.noticeQueue[0]))) {
    s.noticeQueue[s.nqTail] = msg;
    s.nqTail = (s.nqTail + 1) % (int)(sizeof(s.noticeQueue) / sizeof(s.noticeQueue[0]));
    s.nqCount++;
  } else {
    // Overwrite legacy single-slot to avoid dropping entirely
    s.notice = msg;
  }
  // Enter burst mode for faster reconnects for a short period
  s.noticeBurstUntil = millis() + 15000UL; // 15s burst window
  s.needsNotificationTick = true;
}

static inline bool sseDequeueNotice(SessionEntry& s, String& out) {
  if (s.nqCount > 0) {
    out = s.noticeQueue[s.nqHead];
    s.nqHead = (s.nqHead + 1) % (int)(sizeof(s.noticeQueue) / sizeof(s.noticeQueue[0]));
    s.nqCount--;
    return true;
  }
  if (s.notice.length() > 0) {
    out = s.notice; s.notice = ""; return true;
  }
  return false;
}

// Send broadcast notice to all active sessions for popup alerts
void broadcastNoticeToAllSessions(const String& message) {
  DEBUG_SSEF("Broadcasting notice to all sessions: %s", message.c_str());
  for (int i = 0; i < MAX_SESSIONS; i++) {
    if (gSessions[i].sid.length() > 0) {
      sseEnqueueNotice(gSessions[i], message);
      DEBUG_SSEF("Enqueued notice for session %d (user: %s) qCount=%d", i, gSessions[i].user.c_str(), gSessions[i].nqCount);
    }
  }
}

// Send a quick SSE burst to a specific session
void sendSSEBurstToSession(int sessionIndex, const String& eventData) {
  if (sessionIndex < 0 || sessionIndex >= MAX_SESSIONS) return;
  if (gSessions[sessionIndex].sid.length() == 0) return;

  // Create a synthetic HTTP request context for SSE
  // This is a simplified approach - in practice, we'd need to store connection info
  DEBUG_SSEF("Would send SSE burst to session %d: %s...", sessionIndex, eventData.substring(0, 50).c_str());

  // For now, just flag the session for update on next status check
  gSessions[sessionIndex].needsStatusUpdate = true;
}

String formatDateTime(time_t timestamp) {
  struct tm* timeinfo = localtime(&timestamp);
  char buffer[32];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);
  return String(buffer);
}

void logAuthAttempt(bool success, const char* path, const String& userTried, const String& ip, const String& reason) {
  // Timestamp prefix with ms precision, via boot-epoch offset and esp_timer
  char tsPrefix[40];
  getTimestampPrefixMsCached(tsPrefix, sizeof(tsPrefix));
  String status = success ? "SUCCESS" : "FAILED";

  // Clean up the path for better readability
  String cleanPath = String(path);
  cleanPath.replace("%2F", "/");
  cleanPath.replace("%20", " ");

  // Clean up IP address
  String cleanIP = ip;
  cleanIP.replace("::FFFF:", "");

  // Format: [YYYY-MM-DD HH:MM:SS.mmm] | SUCCESS | user=... | ip=... | /path [| reason=...]
  String line;
  line.reserve(160);
  if (tsPrefix[0]) line += tsPrefix;  // already includes trailing " | "
  line += status;
  line += " | user=";
  line += userTried;
  line += " | ip=";
  line += cleanIP;
  line += " | ";
  line += cleanPath;
  if (reason.length()) {
    line += " | reason=";
    line += reason;
  }

  const char* logFile = success ? LOG_OK_FILE : LOG_FAIL_FILE;
  appendLineWithCap(logFile, line, LOG_CAP_BYTES);
}



// ==========================
// Output routing (centralized logger)
// ==========================

#define OUTPUT_SERIAL 0x01
#define OUTPUT_TFT 0x02
#define OUTPUT_WEB 0x04

volatile uint8_t gOutputFlags = OUTPUT_SERIAL;  // default behavior: serial only

// Remove ANSI CSI escape sequences (e.g., ESC[2J, ESC[H, ESC[1;32m) for serial cleanliness
static String stripANSICSI(const String& in) {
  String out;
  out.reserve(in.length());
  size_t i = 0, n = in.length();
  while (i < n) {
    char c = in.charAt(i);
    if (c == 0x1B) {  // ESC
      // Handle CSI sequences that start with ESC '[' and end with a final byte @..~
      if (i + 1 < n && in.charAt(i + 1) == '[') {
        i += 2;  // skip ESC[
        while (i < n) {
          char d = in.charAt(i);
          // Final byte in CSI is in range @ (0x40) to ~ (0x7E)
          if (d >= '@' && d <= '~') {
            i++;
            break;
          }
          i++;
        }
        continue;  // skip entire CSI
      } else {
        // Skip solitary ESC or non-CSI sequences conservatively
        i++;
        continue;
      }
    }
    out += c;
    i++;
  }
  return out;
}

static inline void printToSerial(const String& s) {
  Serial.println(stripANSICSI(s));
}

// ----- Output toggle and debug handlers (relocated after OUTPUT_* and gOutputFlags) -----
static String cmd_outtft(const String& originalCmd) {
  RETURN_VALID_IF_VALIDATE();
  // Syntax:
  //   outtft <0|1> [persist|temp]
  //   outtft [persist|temp] <0|1>
  int sp1 = originalCmd.indexOf(' ');
  String a = (sp1 >= 0) ? originalCmd.substring(sp1 + 1) : String();
  a.trim();
  String t1, t2;
  int sp2 = a.indexOf(' ');
  if (sp2 >= 0) {
    t1 = a.substring(0, sp2);
    t2 = a.substring(sp2 + 1);
    t2.trim();
  } else {
    t1 = a;
  }
  t1.trim();
  bool modeTemp = false;  // default persist
  int v = -1;
  auto parse01 = [](const String& s) -> int {
    String ss = s;
    ss.trim();
    return ss.toInt();
  };
  if (t1.length() && (t1 == "temp" || t1 == "persist")) {
    modeTemp = (t1 == "temp");
    if (t2.length()) v = parse01(t2);
  } else {
    if (t1.length()) v = parse01(t1);
    if (t2.length()) { modeTemp = (t2 == "temp"); }
  }
  if (v != 0) v = 1;
  if (v < 0) return String("Usage: outtft <0|1> [persist|temp]");
  if (modeTemp) {
    if (v) gOutputFlags |= OUTPUT_TFT;
    else gOutputFlags &= ~OUTPUT_TFT;
    return String("outTft (runtime) set to ") + (v ? "1" : "0");
  } else {
    gSettings.outTft = (v != 0);
    saveUnifiedSettings();
    if (v) gOutputFlags |= OUTPUT_TFT;
    else gOutputFlags &= ~OUTPUT_TFT;
    return String("outTft (persisted) set to ") + (gSettings.outTft ? "1" : "0");
  }
}

static String cmd_debugauthcookies(const String& originalCmd) {
  RETURN_VALID_IF_VALIDATE();
  int sp = originalCmd.indexOf(' ');
  String valStr = (sp >= 0) ? originalCmd.substring(sp + 1) : String();
  valStr.trim();
  int sp2 = valStr.indexOf(' ');
  String mode = "";
  if (sp2 >= 0) {
    mode = valStr.substring(sp2 + 1);
    valStr = valStr.substring(0, sp2);
    mode.trim();
  }
  bool modeTemp = mode.equalsIgnoreCase("temp") || mode.equalsIgnoreCase("runtime");
  int v = valStr.toInt();
  if (modeTemp) {
    if (v) gDebugFlags |= DEBUG_AUTH;
    else gDebugFlags &= ~DEBUG_AUTH;
    return String("debugAuthCookies (runtime) set to ") + (v ? "1" : "0");
  } else {
    gSettings.debugAuthCookies = (v != 0);
    saveUnifiedSettings();
    if (v) gDebugFlags |= DEBUG_AUTH;
    else gDebugFlags &= ~DEBUG_AUTH;
    return String("debugAuthCookies (persisted) set to ") + (gSettings.debugAuthCookies ? "1" : "0");
  }
}

static String cmd_debughttp(const String& originalCmd) {
  RETURN_VALID_IF_VALIDATE();
  int sp = originalCmd.indexOf(' ');
  String valStr = (sp >= 0) ? originalCmd.substring(sp + 1) : String();
  valStr.trim();
  int sp2 = valStr.indexOf(' ');
  String mode = "";
  if (sp2 >= 0) {
    mode = valStr.substring(sp2 + 1);
    valStr = valStr.substring(0, sp2);
    mode.trim();
  }
  bool modeTemp = mode.equalsIgnoreCase("temp") || mode.equalsIgnoreCase("runtime");
  int v = valStr.toInt();
  if (modeTemp) {
    if (v) gDebugFlags |= DEBUG_HTTP;
    else gDebugFlags &= ~DEBUG_HTTP;
    return String("debugHttp (runtime) set to ") + (v ? "1" : "0");
  } else {
    gSettings.debugHttp = (v != 0);
    saveUnifiedSettings();
    if (v) gDebugFlags |= DEBUG_HTTP;
    else gDebugFlags &= ~DEBUG_HTTP;
    return String("debugHttp (persisted) set to ") + (gSettings.debugHttp ? "1" : "0");
  }
}

static String cmd_debugsse(const String& originalCmd) {
  RETURN_VALID_IF_VALIDATE();
  int sp = originalCmd.indexOf(' ');
  String valStr = (sp >= 0) ? originalCmd.substring(sp + 1) : String();
  valStr.trim();
  int sp2 = valStr.indexOf(' ');
  String mode = "";
  if (sp2 >= 0) {
    mode = valStr.substring(sp2 + 1);
    valStr = valStr.substring(0, sp2);
    mode.trim();
  }
  bool modeTemp = mode.equalsIgnoreCase("temp") || mode.equalsIgnoreCase("runtime");
  int v = valStr.toInt();
  if (modeTemp) {
    if (v) gDebugFlags |= DEBUG_SSE;
    else gDebugFlags &= ~DEBUG_SSE;
    return String("debugSse (runtime) set to ") + (v ? "1" : "0");
  } else {
    gSettings.debugSse = (v != 0);
    saveUnifiedSettings();
    if (v) gDebugFlags |= DEBUG_SSE;
    else gDebugFlags &= ~DEBUG_SSE;
    return String("debugSse (persisted) set to ") + (gSettings.debugSse ? "1" : "0");
  }
}

static String cmd_debugcli(const String& originalCmd) {
  RETURN_VALID_IF_VALIDATE();
  int sp = originalCmd.indexOf(' ');
  String valStr = (sp >= 0) ? originalCmd.substring(sp + 1) : String();
  valStr.trim();
  int sp2 = valStr.indexOf(' ');
  String mode = "";
  if (sp2 >= 0) {
    mode = valStr.substring(sp2 + 1);
    valStr = valStr.substring(0, sp2);
    mode.trim();
  }
  bool modeTemp = mode.equalsIgnoreCase("temp") || mode.equalsIgnoreCase("runtime");
  int v = valStr.toInt();
  if (modeTemp) {
    if (v) gDebugFlags |= DEBUG_CLI;
    else gDebugFlags &= ~DEBUG_CLI;
    return String("debugCli (runtime) set to ") + (v ? "1" : "0");
  } else {
    gSettings.debugCli = (v != 0);
    saveUnifiedSettings();
    if (v) gDebugFlags |= DEBUG_CLI;
    else gDebugFlags &= ~DEBUG_CLI;
    return String("debugCli (persisted) set to ") + (gSettings.debugCli ? "1" : "0");
  }
}

static String cmd_debugsensorsframe(const String& originalCmd) {
  RETURN_VALID_IF_VALIDATE();
  int sp = originalCmd.indexOf(' ');
  String valStr = (sp >= 0) ? originalCmd.substring(sp + 1) : String();
  valStr.trim();
  int sp2 = valStr.indexOf(' ');
  String mode = "";
  if (sp2 >= 0) {
    mode = valStr.substring(sp2 + 1);
    valStr = valStr.substring(0, sp2);
    mode.trim();
  }
  bool modeTemp = mode.equalsIgnoreCase("temp") || mode.equalsIgnoreCase("runtime");
  int v = valStr.toInt();
  if (modeTemp) {
    if (v) gDebugFlags |= DEBUG_SENSORS_FRAME;
    else gDebugFlags &= ~DEBUG_SENSORS_FRAME;
    return String("debugSensorsFrame (runtime) set to ") + (v ? "1" : "0");
  } else {
    gSettings.debugSensorsFrame = (v != 0);
    saveUnifiedSettings();
    if (v) gDebugFlags |= DEBUG_SENSORS_FRAME;
    else gDebugFlags &= ~DEBUG_SENSORS_FRAME;
    return String("debugSensorsFrame (persisted) set to ") + (gSettings.debugSensorsFrame ? "1" : "0");
  }
}

static String cmd_debugsensorsdata(const String& originalCmd) {
  RETURN_VALID_IF_VALIDATE();
  int sp = originalCmd.indexOf(' ');
  String valStr = (sp >= 0) ? originalCmd.substring(sp + 1) : String();
  valStr.trim();
  int sp2 = valStr.indexOf(' ');
  String mode = "";
  if (sp2 >= 0) {
    mode = valStr.substring(sp2 + 1);
    valStr = valStr.substring(0, sp2);
    mode.trim();
  }
  bool modeTemp = mode.equalsIgnoreCase("temp") || mode.equalsIgnoreCase("runtime");
  int v = valStr.toInt();
  if (modeTemp) {
    if (v) gDebugFlags |= DEBUG_SENSORS_DATA;
    else gDebugFlags &= ~DEBUG_SENSORS_DATA;
    return String("debugSensorsData (runtime) set to ") + (v ? "1" : "0");
  } else {
    gSettings.debugSensorsData = (v != 0);
    saveUnifiedSettings();
    if (v) gDebugFlags |= DEBUG_SENSORS_DATA;
    else gDebugFlags &= ~DEBUG_SENSORS_DATA;
    return String("debugSensorsData (persisted) set to ") + (gSettings.debugSensorsData ? "1" : "0");
  }
}

static String cmd_debugsensorsgeneral(const String& originalCmd) {
  RETURN_VALID_IF_VALIDATE();
  int sp = originalCmd.indexOf(' ');
  String valStr = (sp >= 0) ? originalCmd.substring(sp + 1) : String();
  valStr.trim();
  int sp2 = valStr.indexOf(' ');
  String mode = "";
  if (sp2 >= 0) {
    mode = valStr.substring(sp2 + 1);
    valStr = valStr.substring(0, sp2);
    mode.trim();
  }
  bool modeTemp = mode.equalsIgnoreCase("temp") || mode.equalsIgnoreCase("runtime");
  int v = valStr.toInt();
  if (modeTemp) {
    if (v) gDebugFlags |= DEBUG_SENSORS;
    else gDebugFlags &= ~DEBUG_SENSORS;
    return String("debugSensorsGeneral (runtime) set to ") + (v ? "1" : "0");
  } else {
    gSettings.debugSensorsGeneral = (v != 0);
    saveUnifiedSettings();
    if (v) gDebugFlags |= DEBUG_SENSORS;
    else gDebugFlags &= ~DEBUG_SENSORS;
    return String("debugSensorsGeneral (persisted) set to ") + (gSettings.debugSensorsGeneral ? "1" : "0");
  }
}

static String cmd_debugwifi(const String& originalCmd) {
  RETURN_VALID_IF_VALIDATE();
  int sp = originalCmd.indexOf(' ');
  String valStr = (sp >= 0) ? originalCmd.substring(sp + 1) : String();
  valStr.trim();
  int sp2 = valStr.indexOf(' ');
  String mode = "";
  if (sp2 >= 0) {
    mode = valStr.substring(sp2 + 1);
    valStr = valStr.substring(0, sp2);
    mode.trim();
  }
  bool modeTemp = mode.equalsIgnoreCase("temp") || mode.equalsIgnoreCase("runtime");
  int v = valStr.toInt();
  if (modeTemp) {
    if (v) gDebugFlags |= DEBUG_WIFI;
    else gDebugFlags &= ~DEBUG_WIFI;
    return String("debugWifi (runtime) set to ") + (v ? "1" : "0");
  } else {
    gSettings.debugWifi = (v != 0);
    saveUnifiedSettings();
    if (v) gDebugFlags |= DEBUG_WIFI;
    else gDebugFlags &= ~DEBUG_WIFI;
    return String("debugWifi (persisted) set to ") + (gSettings.debugWifi ? "1" : "0");
  }
}

static String cmd_debugstorage(const String& originalCmd) {
  RETURN_VALID_IF_VALIDATE();
  int sp = originalCmd.indexOf(' ');
  String valStr = (sp >= 0) ? originalCmd.substring(sp + 1) : String();
  valStr.trim();
  int sp2 = valStr.indexOf(' ');
  String mode = "";
  if (sp2 >= 0) {
    mode = valStr.substring(sp2 + 1);
    valStr = valStr.substring(0, sp2);
    mode.trim();
  }
  bool modeTemp = mode.equalsIgnoreCase("temp") || mode.equalsIgnoreCase("runtime");
  int v = valStr.toInt();
  if (modeTemp) {
    if (v) gDebugFlags |= DEBUG_STORAGE;
    else gDebugFlags &= ~DEBUG_STORAGE;
    return String("debugStorage (runtime) set to ") + (v ? "1" : "0");
  } else {
    gSettings.debugStorage = (v != 0);
    saveUnifiedSettings();
    if (v) gDebugFlags |= DEBUG_STORAGE;
    else gDebugFlags &= ~DEBUG_STORAGE;
    return String("debugStorage (persisted) set to ") + (gSettings.debugStorage ? "1" : "0");
  }
}

static String cmd_debugperformance(const String& originalCmd) {
  RETURN_VALID_IF_VALIDATE();
  int sp = originalCmd.indexOf(' ');
  String valStr = (sp >= 0) ? originalCmd.substring(sp + 1) : String();
  valStr.trim();
  int sp2 = valStr.indexOf(' ');
  String mode = "";
  if (sp2 >= 0) {
    mode = valStr.substring(sp2 + 1);
    valStr = valStr.substring(0, sp2);
    mode.trim();
  }
  bool modeTemp = mode.equalsIgnoreCase("temp") || mode.equalsIgnoreCase("runtime");
  int v = valStr.toInt();
  if (modeTemp) {
    if (v) gDebugFlags |= DEBUG_PERFORMANCE;
    else gDebugFlags &= ~DEBUG_PERFORMANCE;
    return String("debugPerformance (runtime) set to ") + (v ? "1" : "0");
  } else {
    gSettings.debugPerformance = (v != 0);
    saveUnifiedSettings();
    if (v) gDebugFlags |= DEBUG_PERFORMANCE;
    else gDebugFlags &= ~DEBUG_PERFORMANCE;
    return String("debugPerformance (persisted) set to ") + (gSettings.debugPerformance ? "1" : "0");
  }
}

static String cmd_debugdatetime(const String& originalCmd) {
  RETURN_VALID_IF_VALIDATE();
  int sp = originalCmd.indexOf(' ');
  String valStr = (sp >= 0) ? originalCmd.substring(sp + 1) : String();
  valStr.trim();
  int sp2 = valStr.indexOf(' ');
  String mode = "";
  if (sp2 >= 0) {
    mode = valStr.substring(sp2 + 1);
    valStr = valStr.substring(0, sp2);
    mode.trim();
  }
  bool modeTemp = mode.equalsIgnoreCase("temp") || mode.equalsIgnoreCase("runtime");
  int v = valStr.toInt();
  if (modeTemp) {
    if (v) gDebugFlags |= DEBUG_DATETIME;
    else gDebugFlags &= ~DEBUG_DATETIME;
    return String("debugDateTime (runtime) set to ") + (v ? "1" : "0");
  } else {
    gSettings.debugDateTime = (v != 0);
    saveUnifiedSettings();
    if (v) gDebugFlags |= DEBUG_DATETIME;
    else gDebugFlags &= ~DEBUG_DATETIME;
    return String("debugDateTime (persisted) set to ") + (gSettings.debugDateTime ? "1" : "0");
  }
}

static String cmd_thermaltargetfps(const String& originalCmd) {
  RETURN_VALID_IF_VALIDATE();
  // Admin check now handled by executeCommand pipeline
  int sp1 = originalCmd.indexOf(' ');
  if (sp1 < 0) return "Usage: thermalTargetFps <1..8>";
  String valStr = originalCmd.substring(sp1 + 1);
  valStr.trim();
  int v = valStr.toInt();
  if (v < 1) v = 1;
  if (v > 8) v = 8;
  gSettings.thermalTargetFps = v;
  saveUnifiedSettings();
  applySettings();
  return String("thermalTargetFps set to ") + String(v);
}

static String cmd_thermalwebmaxfps(const String& originalCmd) {
  RETURN_VALID_IF_VALIDATE();
  // Admin check now handled by executeCommand pipeline
  int sp1 = originalCmd.indexOf(' ');
  if (sp1 < 0) return "Usage: thermalWebMaxFps <1..20>";
  String valStr = originalCmd.substring(sp1 + 1);
  valStr.trim();
  int v = valStr.toInt();
  if (v < 1) v = 1;
  if (v > 20) v = 20;
  gSettings.thermalWebMaxFps = v;
  saveUnifiedSettings();
  return String("thermalWebMaxFps set to ") + String(v);
}

static String cmd_thermalinterpolationenabled(const String& originalCmd) {
  RETURN_VALID_IF_VALIDATE();
  // Admin check now handled by executeCommand pipeline
  int sp1 = originalCmd.indexOf(' ');
  if (sp1 < 0) return "Usage: thermalInterpolationEnabled <0|1>";
  String valStr = originalCmd.substring(sp1 + 1);
  valStr.trim();
  int v = valStr.toInt();
  gSettings.thermalInterpolationEnabled = (v != 0);
  saveUnifiedSettings();
  return String("thermalInterpolationEnabled set to ") + (gSettings.thermalInterpolationEnabled ? "1" : "0");
}

static String cmd_thermalinterpolationsteps(const String& originalCmd) {
  RETURN_VALID_IF_VALIDATE();
  // Admin check now handled by executeCommand pipeline
  int sp1 = originalCmd.indexOf(' ');
  if (sp1 < 0) return "Usage: thermalInterpolationSteps <1..8>";
  String valStr = originalCmd.substring(sp1 + 1);
  valStr.trim();
  int v = valStr.toInt();
  if (v < 1) v = 1;
  if (v > 8) v = 8;
  gSettings.thermalInterpolationSteps = v;
  saveUnifiedSettings();
  return String("thermalInterpolationSteps set to ") + String(v);
}

static String cmd_thermalinterpolationbuffersize(const String& originalCmd) {
  RETURN_VALID_IF_VALIDATE();
  // Admin check now handled by executeCommand pipeline
  int sp1 = originalCmd.indexOf(' ');
  if (sp1 < 0) return "Usage: thermalInterpolationBufferSize <1..10>";
  String valStr = originalCmd.substring(sp1 + 1);
  valStr.trim();
  int v = valStr.toInt();
  if (v < 1) v = 1;
  if (v > 10) v = 10;
  gSettings.thermalInterpolationBufferSize = v;
  saveUnifiedSettings();
  return String("thermalInterpolationBufferSize set to ") + String(v);
}

static String cmd_thermaldevicepollms(const String& originalCmd) {
  RETURN_VALID_IF_VALIDATE();
  // Admin check now handled by executeCommand pipeline
  int sp1 = originalCmd.indexOf(' ');
  if (sp1 < 0) return "Usage: thermalDevicePollMs <100..2000>";
  String valStr = originalCmd.substring(sp1 + 1);
  valStr.trim();
  int v = valStr.toInt();
  if (v < 100) v = 100;
  if (v > 2000) v = 2000;
  gSettings.thermalDevicePollMs = v;
  saveUnifiedSettings();
  applySettings();
  return String("thermalDevicePollMs set to ") + String(v);
}

static String cmd_tofdevicepollms(const String& originalCmd) {
  RETURN_VALID_IF_VALIDATE();
  // Admin check now handled by executeCommand pipeline
  int sp1 = originalCmd.indexOf(' ');
  if (sp1 < 0) return "Usage: tofDevicePollMs <100..2000>";
  String valStr = originalCmd.substring(sp1 + 1);
  valStr.trim();
  int v = valStr.toInt();
  if (v < 100) v = 100;
  if (v > 2000) v = 2000;
  gSettings.tofDevicePollMs = v;
  saveUnifiedSettings();
  applySettings();
  return String("tofDevicePollMs set to ") + String(v);
}

static String cmd_imudevicepollms(const String& originalCmd) {
  RETURN_VALID_IF_VALIDATE();
  // Admin check now handled by executeCommand pipeline
  int sp1 = originalCmd.indexOf(' ');
  if (sp1 < 0) return "Usage: imuDevicePollMs <50..1000>";
  String valStr = originalCmd.substring(sp1 + 1);
  valStr.trim();
  int v = valStr.toInt();
  if (v < 50) v = 50;
  if (v > 1000) v = 1000;
  gSettings.imuDevicePollMs = v;
  saveUnifiedSettings();
  applySettings();
  return String("imuDevicePollMs set to ") + String(v);
}

// ----- User admin and system commands (Final batch) -----
static String cmd_user_approve(const String& originalCmd) {
  RETURN_VALID_IF_VALIDATE();
  // Admin check now handled by executeCommand pipeline
  if (!filesystemReady) return "Error: LittleFS not ready";
  String username = originalCmd.substring(String("user approve ").length());
  username.trim();
  DEBUG_USERSF("[users] CLI approve username=%s", username.c_str());
  String err;
  if (!approvePendingUserInternal(username, err)) return String("Error: ") + err;
  return String("Approved user '") + username + "'";
}

static String cmd_user_deny(const String& originalCmd) {
  RETURN_VALID_IF_VALIDATE();
  // Admin check now handled by executeCommand pipeline
  if (!filesystemReady) return "Error: LittleFS not ready";
  String username = originalCmd.substring(String("user deny ").length());
  username.trim();
  DEBUG_USERSF("[users] CLI deny username=%s", username.c_str());
  String err;
  if (!denyPendingUserInternal(username, err)) return String("Error: ") + err;
  return String("Denied user '") + username + "'";
}

static String cmd_user_promote(const String& originalCmd) {
  RETURN_VALID_IF_VALIDATE();
  // Admin check now handled by executeCommand pipeline
  if (!filesystemReady) return "Error: LittleFS not ready";
  String username = originalCmd.substring(String("user promote ").length());
  username.trim();
  if (username.length() == 0) return "Usage: user promote <username>";
  DEBUG_USERSF("[users] CLI promote username=%s", username.c_str());
  String err;
  if (!promoteUserToAdminInternal(username, err)) return String("Error: ") + err;
  return String("Promoted user '") + username + "' to admin";
}

static String cmd_user_demote(const String& originalCmd) {
  RETURN_VALID_IF_VALIDATE();
  // Admin check now handled by executeCommand pipeline
  if (!filesystemReady) return "Error: LittleFS not ready";
  String username = originalCmd.substring(String("user demote ").length());
  username.trim();
  if (username.length() == 0) return "Usage: user demote <username>";
  DEBUG_USERSF("[users] CLI demote username=%s", username.c_str());
  String err;
  if (!demoteUserFromAdminInternal(username, err)) return String("Error: ") + err;
  return String("Demoted user '") + username + "' to regular user";
}

static String cmd_user_delete(const String& originalCmd) {
  RETURN_VALID_IF_VALIDATE();
  // Admin check now handled by executeCommand pipeline
  if (!filesystemReady) return "Error: LittleFS not ready";
  String username = originalCmd.substring(String("user delete ").length());
  username.trim();
  if (username.length() == 0) return "Usage: user delete <username>";
  DEBUG_USERSF("[users] CLI delete username=%s", username.c_str());
  String err;
  if (!deleteUserInternal(username, err)) return String("Error: ") + err;
  return String("Deleted user '") + username + "'";
}

static String cmd_user_list(const String& originalCmd) {
  RETURN_VALID_IF_VALIDATE();
  // Admin check now handled by executeCommand pipeline
  if (!filesystemReady) return "Error: LittleFS not ready";
  
  // Check if JSON output is requested
  bool jsonOutput = (originalCmd.indexOf(" json") >= 0);
  
  if (!LittleFS.exists(USERS_JSON_FILE)) {
    return jsonOutput ? "[]" : "No users found";
  }
  
  String usersJson;
  if (!readText(USERS_JSON_FILE, usersJson)) {
    return jsonOutput ? "[]" : "Error: Failed to read users file";
  }
  
  // Extract users array from the JSON
  int usersIdx = usersJson.indexOf("\"users\"");
  int openBracket = (usersIdx >= 0) ? usersJson.indexOf('[', usersIdx) : -1;
  int closeBracket = (openBracket >= 0) ? usersJson.indexOf(']', openBracket) : -1;
  
  if (openBracket < 0 || closeBracket <= openBracket) {
    return jsonOutput ? "[]" : "Error: Malformed users file";
  }
  
  if (jsonOutput) {
    // Return just the users array
    return usersJson.substring(openBracket, closeBracket + 1);
  } else {
    // Return human-readable format
    String result = "Users:\n";
    String arrayContent = usersJson.substring(openBracket + 1, closeBracket);
    
    // Simple parsing for display
    int pos = 0;
    int userCount = 0;
    while (pos < arrayContent.length()) {
      int objStart = arrayContent.indexOf('{', pos);
      if (objStart == -1) break;
      int objEnd = arrayContent.indexOf('}', objStart);
      if (objEnd == -1) break;
      
      String obj = arrayContent.substring(objStart, objEnd + 1);
      
      // Extract username and role
      int unStart = obj.indexOf("\"username\":\"");
      if (unStart >= 0) {
        unStart += 12;
        int unEnd = obj.indexOf('\"', unStart);
        if (unEnd > unStart) {
          String username = obj.substring(unStart, unEnd);
          String role = "user"; // default
          
          int roleStart = obj.indexOf("\"role\":\"");
          if (roleStart >= 0) {
            roleStart += 8;
            int roleEnd = obj.indexOf('\"', roleStart);
            if (roleEnd > roleStart) {
              role = obj.substring(roleStart, roleEnd);
            }
          }
          
          result += "  " + username + " (" + role + ")\n";
          userCount++;
        }
      }
      pos = objEnd + 1;
    }
    
    if (userCount == 0) {
      result = "No users found";
    }
    return result;
  }
}

static String cmd_session_list(const String& originalCmd) {
  RETURN_VALID_IF_VALIDATE();
  // Admin check now handled by executeCommand pipeline
  
  // Check if JSON output is requested
  bool jsonOutput = (originalCmd.indexOf(" json") >= 0);
  
  if (jsonOutput) {
    String arr;
    buildAllSessionsJson("", arr); // Empty currentSid since we don't need current flag for CLI
    return "[" + arr + "]";
  } else {
    // Return human-readable format
    String result = "Active Sessions:\n";
    int sessionCount = 0;
    for (int i = 0; i < MAX_SESSIONS; ++i) {
      const SessionEntry& s = gSessions[i];
      if (s.user.length() == 0) continue;  // empty slot
      result += "  " + s.user + " from " + s.ip + " (last: " + String(s.lastSeen) + ")\n";
      sessionCount++;
    }
    
    if (sessionCount == 0) {
      result = "No active sessions";
    }
    return result;
  }
}

// Revoke sessions with a popup notice
// Usage:
//   session revoke sid <sid> [reason]
//   session revoke user <username> [reason]
//   session revoke all [reason]
static String cmd_session_revoke(const String& originalCmd) {
  RETURN_VALID_IF_VALIDATE();
  // Admin check now handled by executeCommand pipeline

  String cmd = originalCmd;
  cmd.trim();
  cmd.toLowerCase();

  auto defaultReason = String("Your session has been signed out by an administrator.");

  // Helper: extract tail after a prefix (case-insensitive already applied to cmd for keywords)
  auto tailAfter = [&](const char* prefix) -> String {
    size_t n = strlen(prefix);
    return originalCmd.substring(n); // preserve original casing/spaces after prefix
  };

  int revoked = 0;

  if (cmd.startsWith("session revoke all")) {
    String reason = tailAfter("session revoke all");
    reason.trim();
    if (!reason.length()) reason = defaultReason;
    for (int i = 0; i < MAX_SESSIONS; ++i) {
      if (!gSessions[i].sid.length()) continue;
      if (gSessions[i].ip.length() > 0) {
        storeLogoutReason(gSessions[i].ip, reason);
      }
      enqueueTargetedRevokeForSessionIdx(i, reason);
      revoked++;
    }
    // Admin audit broadcast
    broadcastWithOrigin("admin", gExecIsAdmin ? String("admin") : String(), String(), String("Admin audit: revoked ALL sessions (count=") + String(revoked) + ") reason='" + reason + "'.");
    return String("Revoked ") + String(revoked) + String(" session(s).");
  }

  if (cmd.startsWith("session revoke sid ")) {
    // Extract SID and optional reason
    String rest = originalCmd.substring(String("session revoke sid ").length());
    rest.trim();
    int sp = rest.indexOf(' ');
    String sid = (sp < 0) ? rest : rest.substring(0, sp);
    String reason = (sp < 0) ? String() : rest.substring(sp + 1);
    reason.trim();
    if (!reason.length()) reason = defaultReason;
    int idx = findSessionIndexBySID(sid);
    if (idx < 0) return String("Session not found for given SID.");
    if (gSessions[idx].ip.length() > 0) {
      storeLogoutReason(gSessions[idx].ip, reason);
    }
    enqueueTargetedRevokeForSessionIdx(idx, reason);
    // Admin audit broadcast
    {
      String who = gSessions[idx].user.length() ? gSessions[idx].user : String("(unknown)");
      broadcastWithOrigin("admin", gExecIsAdmin ? String("admin") : String(), String(), String("Admin audit: revoked session by SID for user '") + who + "' reason='" + reason + "'.");
    }
    return String("Revoked 1 session (sid=") + sid + ")";
  }

  if (cmd.startsWith("session revoke user ")) {
    // Extract username and optional reason
    String rest = originalCmd.substring(String("session revoke user ").length());
    rest.trim();
    int sp = rest.indexOf(' ');
    String username = (sp < 0) ? rest : rest.substring(0, sp);
    String reason = (sp < 0) ? String() : rest.substring(sp + 1);
    reason.trim();
    if (!reason.length()) reason = defaultReason;
    for (int i = 0; i < MAX_SESSIONS; ++i) {
      if (!gSessions[i].sid.length()) continue;
      if (!gSessions[i].user.equalsIgnoreCase(username)) continue;
      if (gSessions[i].ip.length() > 0) {
        storeLogoutReason(gSessions[i].ip, reason);
      }
      enqueueTargetedRevokeForSessionIdx(i, reason);
      revoked++;
    }
    if (revoked > 0) {
      // Admin audit broadcast
      broadcastWithOrigin("admin", gExecIsAdmin ? String("admin") : String(), String(), String("Admin audit: revoked ") + String(revoked) + String(" session(s) for user '") + username + "' reason='" + reason + "'.");
    }
    if (revoked == 0) return String("No active sessions found for user '") + username + "'.";
    return String("Revoked ") + String(revoked) + String(" session(s) for user '") + username + "'.";
  }

  return String("Usage:\n")
         + "  session revoke sid <sid> [reason]\n"
         + "  session revoke user <username> [reason]\n"
         + "  session revoke all [reason]";
}

static String cmd_pending_list(const String& originalCmd) {
  RETURN_VALID_IF_VALIDATE();
  // Admin check now handled by executeCommand pipeline
  if (!filesystemReady) return "Error: LittleFS not ready";
  
  // Check if JSON output is requested
  bool jsonOutput = (originalCmd.indexOf(" json") >= 0);
  
  if (!LittleFS.exists("/pending_users.json")) {
    return jsonOutput ? "[]" : "No pending users";
  }
  
  String pendingJson;
  if (!readText("/pending_users.json", pendingJson)) {
    return jsonOutput ? "[]" : "Error: Failed to read pending users file";
  }
  
  if (jsonOutput) {
    // Return the JSON array directly
    if (pendingJson.startsWith("[") && pendingJson.endsWith("]")) {
      return pendingJson;
    } else {
      return "[]";
    }
  } else {
    // Return human-readable format
    String result = "Pending Users:\n";
    // Simple parsing for display
    if (pendingJson.startsWith("[") && pendingJson.endsWith("]")) {
      String arrayContent = pendingJson.substring(1, pendingJson.length() - 1);
      int pos = 0;
      int userCount = 0;
      while (pos < arrayContent.length()) {
        int objStart = arrayContent.indexOf('{', pos);
        if (objStart == -1) break;
        int objEnd = arrayContent.indexOf('}', objStart);
        if (objEnd == -1) break;
        
        String obj = arrayContent.substring(objStart, objEnd + 1);
        
        // Extract username
        int unStart = obj.indexOf("\"username\":\"");
        if (unStart >= 0) {
          unStart += 12;
          int unEnd = obj.indexOf('\"', unStart);
          if (unEnd > unStart) {
            String username = obj.substring(unStart, unEnd);
            result += "  " + username + " (pending approval)\n";
            userCount++;
          }
        }
        pos = objEnd + 1;
      }
      
      if (userCount == 0) {
        result = "No pending users";
      }
    } else {
      result = "No pending users";
    }
    return result;
  }
}

static String cmd_user_request(const String& originalCmd) {
  //RETURN_VALID_IF_VALIDATE();
  broadcastOutput("[DEBUG] NEW cmd_user_request function called");
  if (!filesystemReady) return "Error: LittleFS not ready";
  // Syntax:
  //   user request <username> <password> [confirmPassword]
  int sp1 = originalCmd.indexOf(' ');                             // after 'user'
  int sp2 = (sp1 >= 0) ? originalCmd.indexOf(' ', sp1 + 1) : -1;  // after 'request'
  if (sp2 < 0) return String("Usage: user request <username> <password> [confirmPassword]");
  String rest = originalCmd.substring(sp2 + 1);
  rest.trim();
  int spU = rest.indexOf(' ');
  if (spU < 0) return String("Usage: user request <username> <password> [confirmPassword]");
  String username = rest.substring(0, spU);
  username.trim();
  String rem = rest.substring(spU + 1);
  rem.trim();
  int spP = rem.indexOf(' ');
  String password = (spP >= 0) ? rem.substring(0, spP) : rem;
  password.trim();
  String confirm = (spP >= 0) ? rem.substring(spP + 1) : String();
  confirm.trim();
  if (username.length() == 0 || password.length() == 0) return String("Error: username and password required");
  if (confirm.length() && confirm != password) return String("Error: passwords do not match");
  // Add to pending_users.json in JSON format
  DEBUG_CMD_FLOWF("[users] Adding user to pending_users.json, filesystemReady=%d", filesystemReady ? 1 : 0);
  
  String json = "[]";
  if (LittleFS.exists("/pending_users.json")) {
    if (!readText("/pending_users.json", json)) {
      DEBUG_CMD_FLOWF("[users] ERROR: Failed to read existing pending_users.json");
      return String("Error: could not read pending list");
    }
  }
  
  // Parse existing JSON array or create new one
  if (json.length() < 2 || !json.startsWith("[")) json = "[]";
  
  // Create new user entry with hashed password
  String hashedPassword = hashUserPassword(password);
  String userEntry = "{\"username\":\"" + username + "\",\"password\":\"" + hashedPassword + "\",\"timestamp\":" + String(millis()) + "}";
  
  // Insert into array
  if (json == "[]") {
    json = "[" + userEntry + "]";
  } else {
    // Insert before closing bracket
    int lastBracket = json.lastIndexOf(']');
    if (lastBracket > 0) {
      String insert = json.substring(1, lastBracket).length() > 0 ? "," + userEntry : userEntry;
      json = json.substring(0, lastBracket) + insert + "]";
    }
  }
  
  // Attempt atomic write with debug details
  DEBUG_USERSF("[users] Attempting to write /pending_users.json (%d bytes)", (int)json.length());
  bool okWrite = writeText("/pending_users.json", json);
  if (!okWrite) {
    DEBUG_CMD_FLOWF("[users] ERROR: writeText failed when writing pending_users.json");
    broadcastOutput("[users] ERROR: writeText failed for /pending_users.json");
    return String("Error: could not write pending list");
  }
  size_t fsz = 0;
  File dbgFile = LittleFS.open("/pending_users.json", "r");
  if (dbgFile) {
    fsz = dbgFile.size();
    dbgFile.close();
  }
  DEBUG_USERSF("[users] writeText success; file size=%d bytes", (int)fsz);
  
  DEBUG_CMD_FLOWF("[users] CLI request username=%s", username.c_str());
  broadcastOutput(String("[register] New user request: ") + username);
  return String("Request submitted for '") + username + "' (JSON)";
}

static String cmd_reboot(const String& originalCmd) {
  RETURN_VALID_IF_VALIDATE();
  // Admin check now handled by executeCommand pipeline
  ESP.restart();
  return "Rebooting system...";
}

// ----- Remaining sensor settings -----
static String cmd_thermalpollingms(const String& originalCmd) {
  RETURN_VALID_IF_VALIDATE();
  // Admin check now handled by executeCommand pipeline
  int sp1 = originalCmd.indexOf(' ');
  if (sp1 < 0) return "Usage: thermalPollingMs <50..5000>";
  String valStr = originalCmd.substring(sp1 + 1);
  valStr.trim();
  int v = valStr.toInt();
  if (v < 50) v = 50;
  if (v > 5000) v = 5000;
  gSettings.thermalPollingMs = v;
  saveUnifiedSettings();
  return String("thermalPollingMs set to ") + String(v);
}

static String cmd_tofpollingms(const String& originalCmd) {
  RETURN_VALID_IF_VALIDATE();
  // Admin check now handled by executeCommand pipeline
  int sp1 = originalCmd.indexOf(' ');
  if (sp1 < 0) return "Usage: tofPollingMs <50..5000>";
  String valStr = originalCmd.substring(sp1 + 1);
  valStr.trim();
  int v = valStr.toInt();
  if (v < 50) v = 50;
  if (v > 5000) v = 5000;
  gSettings.tofPollingMs = v;
  saveUnifiedSettings();
  return String("tofPollingMs set to ") + String(v);
}

static String cmd_tofstabilitythreshold(const String& originalCmd) {
  RETURN_VALID_IF_VALIDATE();
  // Admin check now handled by executeCommand pipeline
  int sp1 = originalCmd.indexOf(' ');
  if (sp1 < 0) return "Usage: tofStabilityThreshold <1..20>";
  String valStr = originalCmd.substring(sp1 + 1);
  valStr.trim();
  int v = valStr.toInt();
  if (v < 1) v = 1;
  if (v > 20) v = 20;
  gSettings.tofStabilityThreshold = v;
  saveUnifiedSettings();
  return String("tofStabilityThreshold set to ") + String(v);
}
static inline void printToTFT(const String& s) {
  gLastTFTLine = s; /* TODO: render on TFT when integrated */
}
static inline void printToWeb(const String& s) {
  if (!gWebMirror.buf) { gWebMirror.init(gWebMirrorCap); }
  gWebMirror.append(s, /*needNewline=*/true);
  gWebMirrorSeq++;
}

// ----- Automations helpers -----

// Helper: compute the next run time (epoch seconds) for an automation
// Returns 0 if unable to compute (invalid data)
//
// Edge case behaviors:
// - atTime: For monthly/yearly recurrence (future feature), skips invalid dates (e.g., Feb 30)
// - DST transitions: Uses system localtime which handles DST automatically
// - Manual runs: Caller should advance nextAt to prevent immediate re-triggering
// - afterDelay: Stays enabled after firing, nextAt = now + delayMs
// - interval: Preserves cadence by adding intervalMs to current nextAt, not current time
static time_t computeNextRunTime(const String& automationJson, time_t fromTime) {
  // Parse automation type
  String type;
  int typePos = automationJson.indexOf("\"type\"");
  if (typePos >= 0) {
    int colon = automationJson.indexOf(':', typePos);
    int q1 = automationJson.indexOf('"', colon + 1);
    int q2 = automationJson.indexOf('"', q1 + 1);
    if (q1 > 0 && q2 > q1) {
      type = automationJson.substring(q1 + 1, q2);
      type.toLowerCase();
    }
  }

  if (type == "attime") {
    // Parse time field (HH:MM)
    String timeStr;
    int timePos = automationJson.indexOf("\"time\"");
    if (timePos >= 0) {
      int colon = automationJson.indexOf(':', timePos);
      int q1 = automationJson.indexOf('"', colon + 1);
      int q2 = automationJson.indexOf('"', q1 + 1);
      if (q1 > 0 && q2 > q1) {
        timeStr = automationJson.substring(q1 + 1, q2);
      }
    }

    // Parse optional days field
    String daysStr;
    int daysPos = automationJson.indexOf("\"days\"");
    if (daysPos >= 0) {
      int colon = automationJson.indexOf(':', daysPos);
      int q1 = automationJson.indexOf('"', colon + 1);
      int q2 = automationJson.indexOf('"', q1 + 1);
      if (q1 > 0 && q2 > q1) {
        daysStr = automationJson.substring(q1 + 1, q2);
      }
    }

    // Validate time format (HH:MM)
    if (timeStr.length() != 5 || timeStr[2] != ':') return 0;
    int hour = timeStr.substring(0, 2).toInt();
    int minute = timeStr.substring(3, 5).toInt();
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59) return 0;

    // Convert fromTime to local time
    struct tm tmNow;
    if (!localtime_r(&fromTime, &tmNow)) return 0;

    // Try today first
    struct tm tmTarget = tmNow;
    tmTarget.tm_hour = hour;
    tmTarget.tm_min = minute;
    tmTarget.tm_sec = 0;
    tmTarget.tm_isdst = -1;  // Let system determine DST

    time_t candidateTime = mktime(&tmTarget);

    // If time has passed today, or if we have day restrictions, find next valid day
    bool needNextDay = (candidateTime <= fromTime);

    // Check day restrictions
    bool dayMatches = true;
    if (daysStr.length() > 0) {
      dayMatches = parseAtTimeMatchDays(daysStr, tmTarget.tm_wday);
      if (!dayMatches) needNextDay = true;
    }

    if (needNextDay) {
      // Search up to 7 days ahead for next valid day
      for (int dayOffset = 1; dayOffset <= 7; dayOffset++) {
        tmTarget = tmNow;
        tmTarget.tm_mday += dayOffset;
        tmTarget.tm_hour = hour;
        tmTarget.tm_min = minute;
        tmTarget.tm_sec = 0;
        tmTarget.tm_isdst = -1;

        candidateTime = mktime(&tmTarget);
        if (candidateTime <= fromTime) continue;  // Still in past somehow

        // Check if this day matches our restrictions
        struct tm tmCheck;
        if (localtime_r(&candidateTime, &tmCheck)) {
          if (daysStr.length() == 0 || parseAtTimeMatchDays(daysStr, tmCheck.tm_wday)) {
            return candidateTime;
          }
        }
      }
      return 0;  // Couldn't find valid day
    }

    return candidateTime;

  } else if (type == "afterdelay") {
    // Parse delayMs field
    int delayMs = 0;
    int delayPos = automationJson.indexOf("\"delayMs\"");
    if (delayPos >= 0) {
      int colon = automationJson.indexOf(':', delayPos);
      int comma = automationJson.indexOf(',', colon);
      int brace = automationJson.indexOf('}', colon);
      int end = (comma > 0 && (brace < 0 || comma < brace)) ? comma : brace;
      if (end > colon) {
        String delayStr = automationJson.substring(colon + 1, end);
        delayStr.trim();
        delayMs = delayStr.toInt();
      }
    }

    if (delayMs <= 0) return 0;
    return fromTime + (delayMs / 1000);  // Convert ms to seconds

  } else if (type == "interval") {
    // Parse intervalMs field
    int intervalMs = 0;
    int intervalPos = automationJson.indexOf("\"intervalMs\"");
    if (intervalPos >= 0) {
      int colon = automationJson.indexOf(':', intervalPos);
      int comma = automationJson.indexOf(',', colon);
      int brace = automationJson.indexOf('}', colon);
      int end = (comma > 0 && (brace < 0 || comma < brace)) ? comma : brace;
      if (end > colon) {
        String intervalStr = automationJson.substring(colon + 1, end);
        intervalStr.trim();
        intervalMs = intervalStr.toInt();
      }
    }

    if (intervalMs <= 0) return 0;
    return fromTime + (intervalMs / 1000);  // Convert ms to seconds
  }

  return 0;  // Unknown type or error
}

// ----- Automations handlers (relocated after dependencies) -----
static String cmd_automation_list() {
  RETURN_VALID_IF_VALIDATE();
  String json;
  if (!readText(AUTOMATIONS_JSON_FILE, json)) return "Error: failed to read automations.json";
  return json;
}

// Validate condition syntax for Phase 1: Simple IF/THEN
static String validateConditionSyntax(const String& condition) {
  String cond = condition;
  cond.trim();
  cond.toUpperCase();
  
  // Must start with IF
  if (!cond.startsWith("IF ")) {
    return "Condition must start with 'IF'";
  }
  
  // Must contain THEN
  int thenPos = cond.indexOf(" THEN ");
  if (thenPos < 0) {
    return "Condition must contain 'THEN'";
  }
  
  // Extract condition part (between IF and THEN)
  String conditionPart = cond.substring(3, thenPos); // Skip "IF "
  conditionPart.trim();
  
  // Extract command part (after THEN)
  String commandPart = cond.substring(thenPos + 6); // Skip " THEN "
  commandPart.trim();
  
  if (conditionPart.length() == 0) {
    return "Missing condition after 'IF'";
  }
  
  if (commandPart.length() == 0) {
    return "Missing command after 'THEN'";
  }
  
  // Validate condition syntax: sensor operator value
  // Supported: temp>75, temp<65, temp=70, humidity>80, motion=detected, time=morning
  bool hasOperator = false;
  String operators[] = {">=", "<=", "!=", ">", "<", "="};
  for (int i = 0; i < 6; i++) {
    if (conditionPart.indexOf(operators[i]) > 0) {
      hasOperator = true;
      break;
    }
  }
  
  if (!hasOperator) {
    return "Condition must contain an operator (>, <, =, >=, <=, !=)";
  }
  
  // Basic validation passed
  return "";
}

// State machine for conditional hierarchy validation
enum ConditionalState {
  EXPECTING_IF,           // Start - only IF allowed
  EXPECTING_ELSE_OR_END,  // After IF - can have ELSE IF, ELSE, or end
  EXPECTING_END           // After ELSE - only end allowed
};

// Validate conditional hierarchy structure
static String validateConditionalHierarchy(const String& conditions) {
  if (conditions.length() == 0) return "VALID";
  
  String input = conditions;
  input.trim();
  input.toUpperCase();
  
  ConditionalState state = EXPECTING_IF;
  int position = 0;
  
  while (position < input.length()) {
    // Skip whitespace
    while (position < input.length() && input[position] == ' ') position++;
    if (position >= input.length()) break;
    
    // Check for keywords
    bool foundIF = input.substring(position).startsWith("IF ");
    bool foundELSEIF = input.substring(position).startsWith("ELSE IF ");
    bool foundELSE = input.substring(position).startsWith("ELSE ");
    
    switch (state) {
      case EXPECTING_IF:
        if (!foundIF) {
          return "Error: Expected IF statement at beginning";
        }
        state = EXPECTING_ELSE_OR_END;
        position += 3; // Skip "IF "
        break;
        
      case EXPECTING_ELSE_OR_END:
        if (foundELSEIF) {
          state = EXPECTING_ELSE_OR_END; // Stay in same state
          position += 8; // Skip "ELSE IF "
        } else if (foundELSE) {
          state = EXPECTING_END;
          position += 5; // Skip "ELSE "
        } else {
          // Look for end of current block (find THEN and skip to next keyword)
          int thenPos = input.indexOf("THEN", position);
          if (thenPos < 0) {
            return "Error: Missing THEN keyword";
          }
          // Skip past the action to look for next conditional
          position = thenPos + 4;
          // Skip the action part
          while (position < input.length() && 
                 !input.substring(position).startsWith("ELSE IF ") && 
                 !input.substring(position).startsWith("ELSE ")) {
            position++;
          }
          continue; // Re-evaluate at new position
        }
        break;
        
      case EXPECTING_END:
        if (foundIF || foundELSEIF || foundELSE) {
          return "Error: No additional conditions allowed after ELSE";
        }
        position++; // Move forward to finish parsing
        break;
    }
    
    // Skip to next potential keyword
    while (position < input.length() && input[position] != 'E' && input[position] != 'I') {
      position++;
    }
  }
  
  return "VALID";
}

// Enhanced conditional chain evaluator
static String evaluateConditionalChain(const String& chainStr) {
  if (chainStr.length() == 0) return "";
  
  String input = chainStr;
  input.trim();
  input.toUpperCase();
  
  int position = 0;
  
  while (position < input.length()) {
    // Skip whitespace
    while (position < input.length() && input[position] == ' ') position++;
    if (position >= input.length()) break;
    
    // Check for keywords
    bool isIF = input.substring(position).startsWith("IF ");
    bool isELSEIF = input.substring(position).startsWith("ELSE IF ");
    bool isELSE = input.substring(position).startsWith("ELSE ");
    
    if (isIF || isELSEIF) {
      // Extract condition and action
      int condStart = position + (isELSEIF ? 8 : 3); // Skip "IF " or "ELSE IF "
      int thenPos = input.indexOf(" THEN ", condStart);
      if (thenPos < 0) return ""; // Invalid syntax
      
      String conditionPart = input.substring(condStart, thenPos);
      conditionPart.trim();
      
      // Find end of action (next IF/ELSE IF/ELSE or end of string)
      int actionStart = thenPos + 6; // Skip " THEN "
      int actionEnd = input.length();
      
      // Look for next conditional keyword
      for (int i = actionStart; i < input.length() - 7; i++) {
        if (input.substring(i).startsWith(" ELSE IF ") || 
            input.substring(i).startsWith(" ELSE ")) {
          actionEnd = i;
          break;
        }
      }
      
      String action = input.substring(actionStart, actionEnd);
      action.trim();
      
      // Evaluate this condition
      String fullCondition = "IF " + conditionPart + " THEN dummy";
      bool conditionMet = evaluateCondition(fullCondition);
      
      if (conditionMet) {
        return action; // Execute this action and stop
      }
      
      position = actionEnd;
    } else if (isELSE) {
      // ELSE - always execute
      int actionStart = position + 5; // Skip "ELSE "
      String action = input.substring(actionStart);
      action.trim();
      return action;
    } else {
      position++; // Move forward
    }
  }
  
  return ""; // No action to execute
}

// Evaluate condition for Phase 1: Simple IF/THEN
static bool evaluateCondition(const String& condition) {
  String cond = condition;
  cond.trim();
  cond.toUpperCase();
  
  // Extract condition part (between IF and THEN)
  int thenPos = cond.indexOf(" THEN ");
  if (thenPos < 0) return false; // Invalid syntax
  
  String conditionPart = cond.substring(3, thenPos); // Skip "IF "
  conditionPart.trim();
  
  // Parse: sensor operator value
  String sensor, op, value;
  int opPos = -1;
  String operators[] = {">=", "<=", "!=", ">", "<", "="};
  
  // Find operator
  for (int i = 0; i < 6; i++) {
    opPos = conditionPart.indexOf(operators[i]);
    if (opPos > 0) {
      sensor = conditionPart.substring(0, opPos);
      op = operators[i];
      value = conditionPart.substring(opPos + op.length());
      sensor.trim();
      value.trim();
      break;
    }
  }
  
  if (opPos < 0) return false; // No operator found
  
  // Get current sensor value
  float currentValue = 0;
  bool isNumeric = true;
  String currentStringValue = "";
  
  if (sensor == "TEMP") {
    currentValue = gSensorCache.thermalAvgTemp;
  } else if (sensor == "HUMIDITY") {
    // No humidity sensor in this cache, return error
    DEBUGF(DEBUG_CLI | DEBUG_AUTOMATIONS, "[condition] Humidity sensor not available");
    return false;
  } else if (sensor == "DISTANCE") {
    // Special handling for distance - check if ANY valid object meets the condition
    float targetValue = value.toFloat();
    bool anyObjectMeetsCondition = false;
    
    DEBUGF(DEBUG_CLI | DEBUG_AUTOMATIONS, "[condition] distance: checking %d objects against %s%.1f", 
           gSensorCache.tofTotalObjects, op.c_str(), targetValue);
    
    for (int j = 0; j < gSensorCache.tofTotalObjects && j < 4; j++) {
      if (gSensorCache.tofObjects[j].valid) {
        float objDistance = gSensorCache.tofObjects[j].distance_cm;
        bool objMeetsCondition = false;
        
        if (op == ">") objMeetsCondition = objDistance > targetValue;
        else if (op == "<") objMeetsCondition = objDistance < targetValue;
        else if (op == "=") objMeetsCondition = abs(objDistance - targetValue) < 0.1;
        else if (op == ">=") objMeetsCondition = objDistance >= targetValue;
        else if (op == "<=") objMeetsCondition = objDistance <= targetValue;
        else if (op == "!=") objMeetsCondition = abs(objDistance - targetValue) >= 0.1;
        
        DEBUGF(DEBUG_CLI | DEBUG_AUTOMATIONS, "[condition] obj[%d]: %.1fcm %s %.1f = %s", 
               j, objDistance, op.c_str(), targetValue, objMeetsCondition ? "TRUE" : "FALSE");
        
        if (objMeetsCondition) {
          anyObjectMeetsCondition = true;
        }
      }
    }
    
    DEBUGF(DEBUG_CLI | DEBUG_AUTOMATIONS, "[condition] distance result: %s", 
           anyObjectMeetsCondition ? "TRUE" : "FALSE");
    return anyObjectMeetsCondition;
  } else if (sensor == "LIGHT") {
    currentValue = gSensorCache.apdsClear; // Use clear light sensor
  } else if (sensor == "MOTION") {
    isNumeric = false;
    // Use proximity sensor as motion detection
    currentStringValue = (gSensorCache.apdsProximity > 50) ? "DETECTED" : "NONE";
  } else if (sensor == "TIME") {
    isNumeric = false;
    time_t now = time(nullptr);
    struct tm* timeinfo = localtime(&now);
    int hour = timeinfo->tm_hour;
    if (hour >= 6 && hour < 12) currentStringValue = "MORNING";
    else if (hour >= 12 && hour < 18) currentStringValue = "AFTERNOON";
    else if (hour >= 18 && hour < 24) currentStringValue = "EVENING";
    else currentStringValue = "NIGHT";
  } else {
    DEBUGF(DEBUG_CLI | DEBUG_AUTOMATIONS, "[condition] Unknown sensor: %s", sensor.c_str());
    return false; // Unknown sensor
  }
  
  // Evaluate condition
  if (isNumeric) {
    float targetValue = value.toFloat();
    if (op == ">") return currentValue > targetValue;
    else if (op == "<") return currentValue < targetValue;
    else if (op == "=") return abs(currentValue - targetValue) < 0.1; // Float equality
    else if (op == ">=") return currentValue >= targetValue;
    else if (op == "<=") return currentValue <= targetValue;
    else if (op == "!=") return abs(currentValue - targetValue) >= 0.1;
  } else {
    value.toUpperCase();
    if (op == "=") return currentStringValue == value;
    else if (op == "!=") return currentStringValue != value;
    // Other operators don't make sense for strings
  }
  
  return false;
}

// Validate conditional command syntax
static String validateConditionalCommand(const String& command) {
  String cmd = command;
  cmd.trim();
  
  String upperCmd = cmd;
  upperCmd.toUpperCase();
  if (!upperCmd.startsWith("IF ")) {
    return ""; // Not a conditional command, no validation needed
  }
  
  int thenPos = upperCmd.indexOf(" THEN ");
  if (thenPos < 0) {
    return "Error: Conditional command missing THEN";
  }
  
  int elsePos = upperCmd.indexOf(" ELSE ");
  
  // Extract condition part
  String conditionPart = cmd.substring(3, thenPos); // Skip "IF "
  conditionPart.trim();
  
  // Validate condition syntax
  String conditionError = validateConditionSyntax("IF " + conditionPart + " THEN dummy");
  if (conditionError.length() > 0) {
    return "Error: " + conditionError;
  }
  
  // Extract and validate THEN command
  String thenCommand;
  if (elsePos > thenPos) {
    thenCommand = cmd.substring(thenPos + 6, elsePos); // Skip " THEN "
  } else {
    thenCommand = cmd.substring(thenPos + 6); // Skip " THEN "
  }
  thenCommand.trim();
  
  if (thenCommand.length() == 0) {
    return "Error: Missing command after THEN";
  }
  
  // Validate THEN command (recursively handle nested conditionals)
  bool prevValidate = gCLIValidateOnly;
  gCLIValidateOnly = true;
  String thenResult = executeConditionalCommand(thenCommand);
  gCLIValidateOnly = prevValidate;
  
  if (thenResult.startsWith("Error:") && thenResult != "Error: Unknown command") {
    return "Error: Invalid THEN command - " + thenResult;
  }
  
  // Validate ELSE command if present
  if (elsePos > thenPos) {
    String elseCommand = cmd.substring(elsePos + 6); // Skip " ELSE "
    elseCommand.trim();
    
    if (elseCommand.length() > 0) {
      bool prevValidate2 = gCLIValidateOnly;
      gCLIValidateOnly = true;
      String elseResult = executeConditionalCommand(elseCommand);
      gCLIValidateOnly = prevValidate2;
      
      if (elseResult.startsWith("Error:") && elseResult != "Error: Unknown command") {
        return "Error: Invalid ELSE command - " + elseResult;
      }
    }
  }
  
  return ""; // Valid conditional command
}

// Execute a command that may contain conditional logic
static String executeConditionalCommand(const String& command) {
  String cmd = command;
  cmd.trim();
  
  // Check if this is a conditional command (starts with IF)
  String upperCmd = cmd;
  upperCmd.toUpperCase();
  if (upperCmd.startsWith("IF ")) {
    // Parse conditional command: IF condition THEN command [ELSE command]
    
    int thenPos = upperCmd.indexOf(" THEN ");
    if (thenPos < 0) {
      return "Error: Conditional command missing THEN";
    }
    
    int elsePos = upperCmd.indexOf(" ELSE ");
    
    // Extract parts
    String conditionPart = cmd.substring(3, thenPos); // Skip "IF "
    conditionPart.trim();
    
    String thenCommand;
    String elseCommand = "";
    
    if (elsePos > thenPos) {
      // Has ELSE clause
      thenCommand = cmd.substring(thenPos + 6, elsePos); // Skip " THEN "
      elseCommand = cmd.substring(elsePos + 6); // Skip " ELSE "
    } else {
      // No ELSE clause
      thenCommand = cmd.substring(thenPos + 6); // Skip " THEN "
    }
    
    thenCommand.trim();
    elseCommand.trim();
    
    // Evaluate condition
    String fullCondition = "IF " + conditionPart + " THEN dummy";
    bool conditionMet = evaluateCondition(fullCondition);
    
    DEBUGF(DEBUG_CLI | DEBUG_AUTOMATIONS, "[conditional] condition='%s' result=%s", 
           conditionPart.c_str(), conditionMet ? "TRUE" : "FALSE");
    
    // Execute appropriate command
    if (conditionMet) {
      if (thenCommand.length() > 0) {
        DEBUGF(DEBUG_CLI | DEBUG_AUTOMATIONS, "[conditional] executing THEN: %s", thenCommand.c_str());
        return processCommand(thenCommand);
      }
    } else {
      if (elseCommand.length() > 0) {
        DEBUGF(DEBUG_CLI | DEBUG_AUTOMATIONS, "[conditional] executing ELSE: %s", elseCommand.c_str());
        return processCommand(elseCommand);
      }
    }
    
    return "Conditional command completed";
  } else {
    // Regular command - execute normally
    return processCommand(cmd);
  }
}

static String cmd_automation_add(const String& originalCmd) {
  // Do not early-return on validate; we want to perform full argument checks
  bool validateOnly = gCLIValidateOnly;
  String args = originalCmd.substring(String("automation add ").length());
  args.trim();
  auto getVal = [&](const String& key) {
    String k = key + "=";
    int p = args.indexOf(k);
    if (p < 0) return String("");
    int start = p + k.length();
    int end = -1;
    
    // Skip leading whitespace
    while (start < (int)args.length() && args[start] == ' ') start++;
    
    // Check if value is quoted
    if (start < (int)args.length() && args[start] == '"') {
      // Find closing quote
      start++; // Skip opening quote
      end = args.indexOf('"', start);
      if (end < 0) end = args.length(); // No closing quote, take rest
      return args.substring(start, end);
    } else {
      // Unquoted value - find next parameter
      // Handle empty values (when next char after = is space or another key=)
      if (start < (int)args.length() && args[start] == ' ') {
        // Check if next non-space character starts a new parameter (contains =)
        int nextNonSpace = start;
        while (nextNonSpace < (int)args.length() && args[nextNonSpace] == ' ') nextNonSpace++;
        if (nextNonSpace < (int)args.length()) {
          int nextEquals = args.indexOf('=', nextNonSpace);
          int nextSpace = args.indexOf(' ', nextNonSpace);
          if (nextEquals > 0 && (nextSpace < 0 || nextEquals < nextSpace)) {
            // Next token is a parameter, so current value is empty
            return String("");
          }
        }
      }
      
      // Find end of current value
      for (int i = start; i < (int)args.length(); i++) {
        if (args[i] == ' ' && i + 1 < (int)args.length()) {
          int nextSpace = args.indexOf(' ', i + 1);
          int nextEquals = args.indexOf('=', i + 1);
          if (nextEquals > 0 && (nextSpace < 0 || nextEquals < nextSpace)) {
            end = i;
            break;
          }
        }
      }
      if (end < 0) end = args.length();
      String result = args.substring(start, end);
      result.trim();
      return result;
    }
  };
  String name = getVal("name");
  String type = getVal("type");
  String timeS = getVal("time");
  String days = getVal("days");
  String delayMs = getVal("delayms");
  String intervalMs = getVal("intervalms");
  String cmdStr = getVal("command");
  String cmdsList = getVal("commands");
  String conditions = getVal("conditions");
  String enabledStr = getVal("enabled");
  bool enabled = (enabledStr.equalsIgnoreCase("1") || enabledStr.equalsIgnoreCase("true") || enabledStr.equalsIgnoreCase("yes"));
  String typeNorm = type;
  typeNorm.trim();
  typeNorm.toLowerCase();
  DEBUGF(DEBUG_CLI | DEBUG_AUTOMATIONS, "[autos add] name='%s' type='%s' time='%s' days='%s' delayms='%s' intervalms='%s' enabled=%d",
         name.c_str(), typeNorm.c_str(), timeS.c_str(), days.c_str(), delayMs.c_str(), intervalMs.c_str(), enabled ? 1 : 0);
  if (name.length() == 0) return "Error: missing name";
  if (typeNorm.length() == 0) return "Error: missing type (atTime|afterDelay|interval)";
  if ((cmdStr.length() == 0 && cmdsList.length() == 0)) return "Error: missing commands (provide commands=<cmd1;cmd2;...> or command=<cmd>)";
  
  // Validate conditions syntax if provided
  if (conditions.length() > 0) {
    conditions.trim();
    String conditionError = validateConditionSyntax(conditions);
    if (conditionError.length() > 0) {
      return "Error: Invalid condition - " + conditionError;
    }
  }
  
  // Validate individual commands
  String combined = cmdsList.length() ? cmdsList : cmdStr;
  int start = 0;
  String s = combined;
  int len = s.length();
  for (int i = 0; i <= len; ++i) {
    if (i == len || s[i] == ';') {
      String part = s.substring(start, i);
      part.trim();
      if (part.length()) {
        // Validate this individual command by calling processCommand in validation mode
        bool prevValidate = gCLIValidateOnly;
        gCLIValidateOnly = true;
        String validationResult = processCommand(part);
        gCLIValidateOnly = prevValidate;
        
        if (validationResult != "VALID") {
          return "Error: Invalid command '" + part + "' - " + validationResult;
        }
      }
      start = i + 1;
    }
  }
  
  auto isNumeric = [&](const String& s) {
    if (!s.length()) return false;
    for (size_t i = 0; i < s.length(); ++i) {
      char c = s[i];
      if (c < '0' || c > '9') return false;
    }
    return true;
  };
  if (typeNorm == "attime") {
    timeS.trim();
    if (timeS.length() == 0) return "Error: atTime requires time=HH:MM";
    if (!(timeS.length() == 5 && timeS[2] == ':' && isdigit(timeS[0]) && isdigit(timeS[1]) && isdigit(timeS[3]) && isdigit(timeS[4]))) { return "Error: time must be HH:MM"; }
  } else if (typeNorm == "afterdelay") {
    if (!isNumeric(delayMs)) return "Error: afterDelay requires numeric delayms (milliseconds)";
  } else if (typeNorm == "interval") {
    if (!isNumeric(intervalMs)) return "Error: interval requires numeric intervalms (milliseconds)";
  } else {
    return "Error: invalid type (expected atTime|afterDelay|interval)";
  }
  String json;
  bool hadFile = readText(AUTOMATIONS_JSON_FILE, json);
  if (!hadFile || json.length() == 0) {
    json = String("{\n  \"version\": 1,\n  \"automations\": []\n}\n");
    if (!validateOnly) {
      writeAutomationsJsonAtomic(json);
      DEBUGF(DEBUG_CLI | DEBUG_AUTOMATIONS, "[autos add] created default automations.json");
    }
  }
  int arrStart = json.indexOf("\"automations\"");
  int bracket = (arrStart >= 0) ? json.indexOf('[', arrStart) : -1;
  int lastBracket = -1;
  if (bracket >= 0) {
    int depth = 0;
    for (int i = bracket; i < (int)json.length(); ++i) {
      char c = json[i];
      if (c == '[') depth++;
      else if (c == ']') {
        depth--;
        if (depth == 0) {
          lastBracket = i;
          break;
        }
      }
    }
  }
  if (lastBracket < 0) return "Error: malformed automations.json";
  String between = json.substring(bracket + 1, lastBracket);
  between.trim();
  bool empty = (between.length() == 0);
  // Generate a unique id not present in current JSON
  unsigned long id = millis();
  int guard = 0;
  while (automationIdExistsInJson(json, id) && guard < 100) {
    id += 1 + (unsigned long)random(1, 100000);
    guard++;
  }
  String obj = "{\n";
  obj += "  \"id\": " + String(id) + ",\n";
  obj += "  \"name\": \"" + jsonEscape(name) + "\",\n";
  obj += "  \"enabled\": " + String(enabled ? "true" : "false") + ",\n";
  obj += "  \"type\": \"" + typeNorm + "\",\n";
  if (typeNorm == "attime" && timeS.length() > 0) obj += "  \"time\": \"" + jsonEscape(timeS) + "\",\n";
  if (typeNorm == "attime" && days.length() > 0) obj += "  \"days\": \"" + jsonEscape(days) + "\",\n";
  if (typeNorm == "afterdelay" && delayMs.length() > 0) obj += "  \"delayMs\": " + delayMs + ",\n";
  if (typeNorm == "interval" && intervalMs.length() > 0) obj += "  \"intervalMs\": " + intervalMs + ",\n";
  // Build commands array: prefer 'commands='; fallback to 'command=' (semicolon-separated allowed)
  // Note: combined variable already declared above for validation
  // Split by ';'
  auto buildCommandsArray = [&](const String& csv) {
    String arr = "[";
    int start = 0;
    bool first = true;
    String s = csv;
    int len = s.length();
    for (int i = 0; i <= len; ++i) {
      if (i == len || s[i] == ';') {
        String part = s.substring(start, i);
        part.trim();
        if (part.length()) {
          if (!first) arr += ", ";
          arr += "\"" + jsonEscape(part) + "\"";
          first = false;
        }
        start = i + 1;
      }
    }
    arr += "]";
    return arr;
  };
  String commandsJson = buildCommandsArray(combined);
  obj += "  \"commands\": " + commandsJson + ",\n";
  if (conditions.length() > 0) obj += "  \"conditions\": \"" + jsonEscape(conditions) + "\",\n";

  // Compute and add nextAt field
  time_t now = time(nullptr);
  if (now > 0) {  // Valid time available
    time_t nextAt = computeNextRunTime(obj + "}", now);
    if (nextAt > 0) {
      obj += "  \"nextAt\": " + String((unsigned long)nextAt) + "\n";
    } else {
      obj += "  \"nextAt\": null\n";
      DEBUGF(DEBUG_CLI | DEBUG_AUTOMATIONS, "[autos add] Warning: could not compute nextAt for automation");
    }
  } else {
    obj += "  \"nextAt\": null\n";
    DEBUGF(DEBUG_CLI | DEBUG_AUTOMATIONS, "[autos add] Warning: no valid system time for nextAt computation");
  }

  obj += "}";
  String insert = empty ? ("\n" + obj + "\n") : (",\n" + obj + "\n");
  json = json.substring(0, lastBracket) + insert + json.substring(lastBracket);
  RETURN_VALID_IF_VALIDATE();
  if (!writeAutomationsJsonAtomic(json)) return "Error: failed to write automations.json";
  DEBUG_CLIF("[autos add] wrote automations.json (len=%d) id=%lu", json.length(), id);
  if (typeNorm != "attime") {
    gAutosDirty = true;
    DEBUG_CLIF("[autos add] immediate scheduler refresh queued (type=%s)", typeNorm.c_str());
  } else {
    DEBUG_CLIF("[autos add] no immediate refresh for atTime");
  }
  return "Added automation '" + name + "' id=" + String(id);
}

static String cmd_automation_enable_disable(const String& originalCmd, bool enable) {
  RETURN_VALID_IF_VALIDATE();
  String args = originalCmd.substring(enable ? String("automation enable ").length() : String("automation disable ").length());
  args.trim();
  auto getVal = [&](const String& key) {
    String k = key + "=";
    int p = args.indexOf(k);
    if (p < 0) return String("");
    int s = p + k.length();
    int e = args.indexOf(' ', s);
    if (e < 0) e = args.length();
    return args.substring(s, e);
  };
  String idStr = getVal("id");
  if (idStr.length() == 0) return String("Usage: automation ") + (enable ? "enable" : "disable") + " id=<id>";
  String json;
  if (!readText(AUTOMATIONS_JSON_FILE, json)) return "Error: failed to read automations.json";
  String needle = String("\"id\": ") + idStr;
  int idPos = json.indexOf(needle);
  if (idPos < 0) return "Error: automation id not found";
  int objStart = json.lastIndexOf('{', idPos);
  if (objStart < 0) return "Error: malformed automations.json (objStart)";
  int depth = 0, objEnd = -1;
  for (int i = objStart; i < (int)json.length(); ++i) {
    char c = json[i];
    if (c == '{') depth++;
    else if (c == '}') {
      depth--;
      if (depth == 0) {
        objEnd = i;
        break;
      }
    }
  }
  if (objEnd < 0) return "Error: malformed automations.json (objEnd)";
  String obj = json.substring(objStart, objEnd + 1);

  // Update enabled field
  int enPos = obj.indexOf("\"enabled\"");
  if (enPos >= 0) {
    int colon = obj.indexOf(':', enPos);
    if (colon > 0) {
      int stop = obj.indexOf(',', colon + 1);
      int stop2 = obj.indexOf('}', colon + 1);
      if (stop < 0 || (stop2 >= 0 && stop2 < stop)) stop = stop2;
      if (stop > 0) {
        String before = obj.substring(0, colon + 1);
        String after = obj.substring(stop);
        obj = before + String(enable ? " true" : " false") + after;
      }
    }
  } else {
    int insertAt = obj.indexOf('\n');
    if (insertAt < 0) insertAt = 1;
    else insertAt++;
    String ins = String("  \"enabled\": ") + (enable ? "true" : "false") + ",\n";
    obj = obj.substring(0, insertAt) + ins + obj.substring(insertAt);
  }

  // If enabling, recompute nextAt
  if (enable) {
    time_t now = time(nullptr);
    if (now > 0) {
      time_t nextAt = computeNextRunTime(obj, now);
      if (nextAt > 0) {
        // Update or add nextAt field
        int nextAtPos = obj.indexOf("\"nextAt\"");
        if (nextAtPos >= 0) {
          int colon = obj.indexOf(':', nextAtPos);
          int comma = obj.indexOf(',', colon);
          int brace = obj.indexOf('}', colon);
          int end = (comma > 0 && (brace < 0 || comma < brace)) ? comma : brace;
          if (end > colon) {
            String before = obj.substring(0, colon + 1);
            String after = obj.substring(end);
            obj = before + " " + String((unsigned long)nextAt) + after;
          }
        } else {
          // Add nextAt field before closing brace
          int insertAt = obj.lastIndexOf('}');
          if (insertAt > 0) {
            String before = obj.substring(0, insertAt);
            String after = obj.substring(insertAt);
            String trimmed = before;
            trimmed.trim();
            if (trimmed.endsWith(",")) {
              obj = before + "\n  \"nextAt\": " + String((unsigned long)nextAt) + "\n" + after;
            } else {
              obj = before + ",\n  \"nextAt\": " + String((unsigned long)nextAt) + "\n" + after;
            }
          }
        }
        DEBUGF(DEBUG_CLI | DEBUG_AUTOMATIONS, "[autos enable] computed nextAt=%lu for id=%s", (unsigned long)nextAt, idStr.c_str());
      } else {
        DEBUGF(DEBUG_CLI | DEBUG_AUTOMATIONS, "[autos enable] warning: could not compute nextAt for id=%s", idStr.c_str());
      }
    }
  }

  json = json.substring(0, objStart) + obj + json.substring(objEnd + 1);
  if (!writeAutomationsJsonAtomic(json)) return "Error: failed to write automations.json";
  gAutosDirty = true;
  return String(enable ? "Enabled" : "Disabled") + " automation id=" + idStr;
}

static String cmd_automation_delete(const String& originalCmd) {
  RETURN_VALID_IF_VALIDATE();
  String args = originalCmd.substring(String("automation delete ").length());
  args.trim();
  auto getVal = [&](const String& key) {
    String k = key + "=";
    int p = args.indexOf(k);
    if (p < 0) return String("");
    int s = p + k.length();
    int e = args.indexOf(' ', s);
    if (e < 0) e = args.length();
    return args.substring(s, e);
  };
  String idStr = getVal("id");
  if (idStr.length() == 0) return "Usage: automation delete id=<id>";
  String json;
  if (!readText(AUTOMATIONS_JSON_FILE, json)) return "Error: failed to read automations.json";
  String needle = String("\"id\": ") + idStr;
  int idPos = json.indexOf(needle);
  if (idPos < 0) return "Error: automation id not found";
  int objStart = json.lastIndexOf('{', idPos);
  if (objStart < 0) return "Error: malformed JSON";
  int objEnd = json.indexOf('}', idPos);
  if (objEnd < 0) return "Error: malformed JSON";
  bool isFirst = (objStart > 0 && json[objStart - 1] == '[');
  bool isLast = (objEnd + 1 < (int)json.length() && json[objEnd + 1] == ']');
  int delStart = objStart, delEnd = objEnd + 1;
  if (!isFirst && !isLast) {
    delStart = json.lastIndexOf(',', objStart);
    if (delStart < 0) delStart = objStart;
  } else if (isFirst && !isLast) {
    delEnd = json.indexOf(',', objEnd);
    if (delEnd >= 0) delEnd++;
    else delEnd = objEnd + 1;
  }
  json = json.substring(0, delStart) + json.substring(delEnd);
  if (!writeAutomationsJsonAtomic(json)) return "Error: failed to write automations.json";
  gAutosDirty = true;
  return "Deleted automation id=" + idStr;
}

static String cmd_automation_run(const String& originalCmd) {
  RETURN_VALID_IF_VALIDATE();
  String args = originalCmd.substring(String("automation run ").length());
  args.trim();
  auto getVal = [&](const String& key) {
    String k = key + "=";
    int p = args.indexOf(k);
    if (p < 0) return String("");
    int s = p + k.length();
    int e = args.indexOf(' ', s);
    if (e < 0) e = args.length();
    return args.substring(s, e);
  };
  String idStr = getVal("id");
  if (idStr.length() == 0) return "Usage: automation run id=<id>";
  String json;
  if (!readText(AUTOMATIONS_JSON_FILE, json)) return "Error: failed to read automations.json";
  String needle = String("\"id\": ") + idStr;
  int idPos = json.indexOf(needle);
  if (idPos < 0) return "Error: automation id not found";
  int objStart = json.lastIndexOf('{', idPos);
  if (objStart < 0) return "Error: malformed automations.json (objStart)";
  int depth = 0, objEnd = -1;
  for (int i = objStart; i < (int)json.length(); ++i) {
    char c = json[i];
    if (c == '{') depth++;
    else if (c == '}') {
      depth--;
      if (depth == 0) {
        objEnd = i;
        break;
      }
    }
  }
  if (objEnd < 0) return "Error: malformed automations.json (objEnd)";
  String obj = json.substring(objStart, objEnd + 1);

  // Extract automation name for logging
  String autoName = "Unknown";
  int namePos = obj.indexOf("\"name\"");
  if (namePos >= 0) {
    int colonPos = obj.indexOf(':', namePos);
    if (colonPos >= 0) {
      int q1 = obj.indexOf('"', colonPos + 1);
      int q2 = obj.indexOf('"', q1 + 1);
      if (q1 >= 0 && q2 >= 0) {
        autoName = obj.substring(q1 + 1, q2);
      }
    }
  }

  // Log automation start if logging is active
  if (gAutoLogActive) {
    gAutoLogAutomationName = autoName;
    String startMsg = "Automation started: ID=" + idStr + " Name=" + autoName + " User=" + gExecUser;
    appendAutoLogEntry("AUTO_START", startMsg);
  }

  // Extract commands array (preferred) or single command (fallback)
  int cmdsPos = obj.indexOf("\"commands\"");
  bool haveArray = false;
  int arrStart = -1, arrEnd = -1;
  if (cmdsPos >= 0) {
    int colon = obj.indexOf(':', cmdsPos);
    if (colon > 0) {
      arrStart = obj.indexOf('[', colon);
      if (arrStart > 0) {
        int depth = 0;
        for (int i = arrStart; i < (int)obj.length(); ++i) {
          char c = obj[i];
          if (c == '[') depth++;
          else if (c == ']') {
            depth--;
            if (depth == 0) {
              arrEnd = i;
              break;
            }
          }
        }
        haveArray = (arrStart > 0 && arrEnd > arrStart);
      }
    }
  }
  String* cmdsList = new String[64];
  struct CmdsListGuard { String* p; CmdsListGuard(String* p):p(p){} ~CmdsListGuard(){ if(p){ delete[] p; } } } _cmdsGuard(cmdsList);
  int cmdsCount = 0;
  if (haveArray) {
    String body = obj.substring(arrStart + 1, arrEnd);
    int i = 0;
    while (i < (int)body.length() && cmdsCount < 64) {
      while (i < (int)body.length() && (body[i] == ' ' || body[i] == ',')) i++;
      if (i >= (int)body.length()) break;
      if (body[i] == '"') {
        int q1 = i;
        int q2 = body.indexOf('"', q1 + 1);
        if (q2 < 0) break;
        String one = body.substring(q1 + 1, q2);
        one.trim();
        if (one.length() && cmdsCount < 64) { cmdsList[cmdsCount++] = one; }
        i = q2 + 1;
      } else {
        int next = body.indexOf(',', i);
        if (next < 0) break;
        i = next + 1;
      }
    }
  } else {
    int cpos = obj.indexOf("\"command\"");
    if (cpos < 0) return "Error: no command(s) found";
    int ccolon = obj.indexOf(':', cpos);
    int cq1 = obj.indexOf('"', ccolon + 1);
    int cq2 = obj.indexOf('"', cq1 + 1);
    if (cq1 < 0 || cq2 < 0) return "Error: bad command field";
    String cmd = obj.substring(cq1 + 1, cq2);
    cmd.trim();
    if (cmd.length() && cmdsCount < 64) { cmdsList[cmdsCount++] = cmd; }
  }
  if (cmdsCount == 0) return "Error: no commands to run";

  // Check conditions if present
  String conditions = "";
  int condPos = obj.indexOf("\"conditions\"");
  if (condPos >= 0) {
    int condColon = obj.indexOf(':', condPos);
    if (condColon >= 0) {
      int condQ1 = obj.indexOf('"', condColon + 1);
      int condQ2 = obj.indexOf('"', condQ1 + 1);
      if (condQ1 >= 0 && condQ2 >= 0) {
        conditions = obj.substring(condQ1 + 1, condQ2);
        conditions.trim();
      }
    }
  }
  
  // Evaluate conditions if present
  if (conditions.length() > 0) {
    // Check if this is a conditional chain (contains ELSE/ELSE IF)
    if (conditions.indexOf("ELSE") >= 0) {
      String actionToExecute = evaluateConditionalChain(conditions);
      DEBUGF(DEBUG_CLI | DEBUG_AUTOMATIONS, "[autos run] id=%s conditional chain result: '%s'", 
             idStr.c_str(), actionToExecute.c_str());
      
      if (actionToExecute.length() > 0) {
        // Execute the specific action from the chain
        String result = executeConditionalCommand(actionToExecute);
        if (result.startsWith("Error:")) {
          return "Automation conditional chain error: " + result;
        }
        return "Ran automation id=" + idStr + " (conditional chain executed: " + actionToExecute + ")";
      } else {
        return "Automation conditional chain evaluated but no action executed";
      }
    } else {
      // Simple IF/THEN condition
      bool conditionMet = evaluateCondition(conditions);
      DEBUGF(DEBUG_CLI | DEBUG_AUTOMATIONS, "[autos run] id=%s condition='%s' result=%s", 
             idStr.c_str(), conditions.c_str(), conditionMet ? "TRUE" : "FALSE");
      
      if (!conditionMet) {
        // Log condition not met if logging is active
        if (gAutoLogActive) {
          String skipMsg = "Automation skipped: ID=" + idStr + " Name=" + autoName + " Condition not met: " + conditions;
          appendAutoLogEntry("AUTO_SKIP", skipMsg);
          gAutoLogAutomationName = "";
        }
        return "Automation skipped - condition not met: " + conditions;
      }
    }
  }

  // Execute all commands (with conditional logic support)
  for (int ci = 0; ci < cmdsCount; ++ci) {
    DEBUGF(DEBUG_CLI | DEBUG_AUTOMATIONS, "[autos run] id=%s cmd[%d]='%s'", idStr.c_str(), ci, cmdsList[ci].c_str());
    // Protect against malformed commands
    if (cmdsList[ci].length() == 0 || cmdsList[ci] == "\\") {
      DEBUGF(DEBUG_CLI | DEBUG_AUTOMATIONS, "[autos run] skipping malformed command: '%s'", cmdsList[ci].c_str());
      continue;
    }
    
    // Execute command with conditional logic support
    String result = executeConditionalCommand(cmdsList[ci]);
    if (result.startsWith("Error:")) {
      DEBUGF(DEBUG_CLI | DEBUG_AUTOMATIONS, "[autos run] id=%s cmd[%d] error: %s", idStr.c_str(), ci, result.c_str());
    }
  }

  // Advance nextAt after manual execution (as requested by user)
  time_t now = time(nullptr);
  if (now > 0) {
    time_t nextAt = computeNextRunTime(obj, now);
    if (nextAt > 0) {
      long id = idStr.toInt();
      if (updateAutomationNextAt(id, nextAt)) {
        DEBUGF(DEBUG_CLI | DEBUG_AUTOMATIONS, "[autos run] advanced nextAt=%lu for id=%s", (unsigned long)nextAt, idStr.c_str());
      } else {
        DEBUGF(DEBUG_CLI | DEBUG_AUTOMATIONS, "[autos run] warning: failed to update nextAt for id=%s", idStr.c_str());
      }
    } else {
      DEBUGF(DEBUG_CLI | DEBUG_AUTOMATIONS, "[autos run] warning: could not compute nextAt for id=%s", idStr.c_str());
    }
  }

  // Log automation end if logging is active
  if (gAutoLogActive) {
    String endMsg = "Automation completed: ID=" + idStr + " Name=" + autoName + " Commands=" + String(cmdsCount);
    appendAutoLogEntry("AUTO_END", endMsg);
  }
  
  return "Ran automation id=" + idStr + " (" + String(cmdsCount) + " command" + (cmdsCount == 1 ? "" : "s") + ")";
}

static String cmd_broadcast(const String& originalCmd) {
  RETURN_VALID_IF_VALIDATE();
  // Admin check now handled by executeCommand pipeline
  
  String args = originalCmd.substring(10); // Remove "broadcast "
  args.trim();
  if (args.length() == 0) return "Usage: broadcast [--user <username>] <message>";
  
  String targetUser = "";
  String msg = args;
  
  // Parse --user argument
  if (args.startsWith("--user ")) {
    int userStart = 7; // Length of "--user "
    int userEnd = args.indexOf(' ', userStart);
    if (userEnd < 0) {
      return "Usage: broadcast --user <username> <message>";
    }
    targetUser = args.substring(userStart, userEnd);
    targetUser.trim();
    msg = args.substring(userEnd + 1);
    msg.trim();
    if (msg.length() == 0) {
      return "Usage: broadcast --user <username> <message>";
    }
  }
  
  if (targetUser.length() > 0) {
    // Send to specific user - clean format: [sender@recipient]
    broadcastWithOrigin("", gExecIsAdmin ? gExecUser : String(), targetUser, msg);
    return String("Broadcast sent to user '") + targetUser + "': " + msg;
  } else {
    // Send to all users - clean format: [sender]
    broadcastWithOrigin("", gExecIsAdmin ? gExecUser : String(), String(), msg);
    
    // Send broadcast notifications to all active sessions for popup alerts
    // Now safe to call from automation context due to dedicated executor task
    broadcastNoticeToAllSessions(msg);
    
    return String("Broadcast sent to all users: ") + msg;
  }
}

// ----- Wait/Sleep (delay) command -----
static String cmd_wait_modern(const String& originalCmd) {
  RETURN_VALID_IF_VALIDATE();
  // Accept both 'wait <ms>' and 'sleep <ms>'
  String cmd = originalCmd;
  cmd.trim();
  int sp = cmd.indexOf(' ');
  if (sp < 0) return "Usage: wait <milliseconds>";
  String valStr = cmd.substring(sp + 1);
  valStr.trim();
  if (valStr.length() == 0) return "Usage: wait <milliseconds>";
  long ms = valStr.toInt();
  if (ms < 0) ms = 0;
  // Clamp to a reasonable maximum to avoid excessively long blocks
  if (ms > 600000) ms = 600000; // 10 minutes
  // Perform the delay in the current task context
  if (ms > 0) {
    vTaskDelay(pdMS_TO_TICKS(ms));
  } else {
    taskYIELD();
  }
  return "Waited " + String(ms) + " ms";
}

// ----- I2C clock handlers (relocated) -----
static String cmd_i2cclockthermalhz(const String& originalCmd) {
  RETURN_VALID_IF_VALIDATE();
  // Admin check now handled by executeCommand pipeline
  int sp1 = originalCmd.indexOf(' ');
  if (sp1 < 0) return "Usage: i2cClockThermalHz <400000..1000000>";
  String valStr = originalCmd.substring(sp1 + 1);
  valStr.trim();
  int v = valStr.toInt();
  if (v < 400000) v = 400000;
  if (v > 1000000) v = 1000000;
  gSettings.i2cClockThermalHz = v;
  saveUnifiedSettings();
  applySettings();
  return "i2cClockThermalHz set to " + String(v);
}

static String cmd_i2cclocktofhz(const String& originalCmd) {
  RETURN_VALID_IF_VALIDATE();
  // Admin check now handled by executeCommand pipeline
  int sp1 = originalCmd.indexOf(' ');
  if (sp1 < 0) return "Usage: i2cClockToFHz <50000..400000>";
  String valStr = originalCmd.substring(sp1 + 1);
  valStr.trim();
  int v = valStr.toInt();
  if (v < 50000) v = 50000;
  if (v > 400000) v = 400000;
  gSettings.i2cClockToFHz = v;
  saveUnifiedSettings();
  applySettings();
  return "i2cClockToFHz set to " + String(v);
}

// ----- Download Automation from GitHub -----
// Parse JSON commands array into semicolon-separated string for automation system
static String parseCommandsArray(const String& commandsJson) {
  // Simple JSON array parser for commands
  // Expected format: ["cmd1", "cmd2", "cmd3"]
  
  String trimmed = commandsJson;
  trimmed.trim();
  
  if (!trimmed.startsWith("[") || !trimmed.endsWith("]")) {
    return "Error: commands must be a JSON array [\"cmd1\", \"cmd2\", ...]";
  }
  
  // Remove brackets
  String content = trimmed.substring(1, trimmed.length() - 1);
  content.trim();
  
  if (content.length() == 0) {
    return "Error: commands array cannot be empty";
  }
  
  String result = "";
  int start = 0;
  bool inQuotes = false;
  bool escapeNext = false;
  
  for (int i = 0; i <= content.length(); i++) {
    char c = (i < content.length()) ? content[i] : ','; // Treat end as comma
    
    if (escapeNext) {
      escapeNext = false;
      continue;
    }
    
    if (c == '\\') {
      escapeNext = true;
      continue;
    }
    
    if (c == '"') {
      inQuotes = !inQuotes;
      continue;
    }
    
    if (!inQuotes && c == ',') {
      // Extract command
      String cmd = content.substring(start, i);
      cmd.trim();
      
      // Remove quotes if present
      if (cmd.startsWith("\"") && cmd.endsWith("\"")) {
        cmd = cmd.substring(1, cmd.length() - 1);
      }
      
      cmd.trim();
      if (cmd.length() > 0) {
        if (result.length() > 0) result += ";";
        result += cmd;
      }
      
      start = i + 1;
    }
  }
  
  if (result.length() == 0) {
    return "Error: No valid commands found in array";
  }
  
  return result;
}

static String cmd_downloadautomation(const String& originalCmd) {
  RETURN_VALID_IF_VALIDATE();
  // Admin check now handled by executeCommand pipeline
  
  String args = originalCmd.substring(String("downloadautomation ").length());
  args.trim();
  
  auto getVal = [&](const String& key) {
    String k = key + "=";
    int p = args.indexOf(k);
    if (p < 0) return String("");
    int s = p + k.length();
    int e = args.indexOf(' ', s);
    if (e < 0) e = args.length();
    return args.substring(s, e);
  };
  
  String url = getVal("url");
  String name = getVal("name");
  
  if (url.length() == 0) {
    return "Usage: downloadautomation url=<github-raw-url> [name=<automation-name>]";
  }
  
  // URL decode the URL parameter (it may be double-encoded from web interface)
  auto hexVal = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
  };
  auto urlDecode = [&](const String& s) -> String {
    String out; out.reserve(s.length());
    for (size_t i = 0; i < s.length(); ++i) {
      char c = s[i];
      if (c == '%' && i + 2 < s.length()) {
        int hi = hexVal(s[i+1]);
        int lo = hexVal(s[i+2]);
        if (hi >= 0 && lo >= 0) {
          out += (char)((hi << 4) | lo);
          i += 2;
          continue;
        }
      }
      // Convert plus to space only if we ever send x-www-form-urlencoded (defensive)
      if (c == '+') { out += ' '; } else { out += c; }
    }
    return out;
  };
  url = urlDecode(url);
  
  broadcastOutput("Original URL after decoding: " + url);
  
  // Convert GitHub URLs to raw format if needed
  if (url.indexOf("github.com") >= 0 && url.indexOf("raw.githubusercontent.com") < 0) {
    broadcastOutput("Converting GitHub blob URL to raw format...");
    // Convert https://github.com/user/repo/blob/main/file.json to raw format
    url.replace("github.com", "raw.githubusercontent.com");
    url.replace("/blob/", "/");
    broadcastOutput("Converted URL: " + url);
  }

  // Normalize raw.githubusercontent.com URLs that use refs/heads in the path
  if (url.indexOf("raw.githubusercontent.com") >= 0) {
    // Example: https://raw.githubusercontent.com/user/repo/refs/heads/main/path/file.json
    // should be      https://raw.githubusercontent.com/user/repo/main/path/file.json
    url.replace("/refs/heads/", "/");
  }
  
  broadcastOutput("Final URL for download: " + url);
  
  // Use a separate task with larger stack for HTTP operations to avoid stack overflow
  struct DownloadTaskParams {
    String url;
    String* result;
    bool* completed;
    SemaphoreHandle_t semaphore;
  };
  
  String downloadResult;
  bool downloadCompleted = false;
  SemaphoreHandle_t downloadSemaphore = xSemaphoreCreateBinary();
  
  DownloadTaskParams params = {url, &downloadResult, &downloadCompleted, downloadSemaphore};
  
  auto downloadTask = [](void* pvParameters) {
    DownloadTaskParams* p = (DownloadTaskParams*)pvParameters;
    
    UBaseType_t stackStart = uxTaskGetStackHighWaterMark(NULL);
    DEBUG_SSEF("Download task started - Stack available: %d bytes", stackStart);
    
    WiFiClientSecure client;
    UBaseType_t stackAfterClient = uxTaskGetStackHighWaterMark(NULL);
    DEBUG_SSEF("After WiFiClientSecure creation - Stack available: %d bytes (used: %d)", 
               stackAfterClient, stackStart - stackAfterClient);
    
    client.setInsecure(); // For simplicity, skip certificate validation
    
    HTTPClient http;
    UBaseType_t stackAfterHttp = uxTaskGetStackHighWaterMark(NULL);
    DEBUG_SSEF("After HTTPClient creation - Stack available: %d bytes (used: %d)", 
               stackAfterHttp, stackAfterClient - stackAfterHttp);
    
    DEBUG_SSEF("Initializing HTTP client for URL: %s", p->url.c_str());
    
    // Try to resolve the hostname first
    String hostname = "raw.githubusercontent.com";
    IPAddress ip;
    if (WiFi.hostByName(hostname.c_str(), ip)) {
      DEBUG_SSEF("DNS resolution successful: %s -> %s", hostname.c_str(), ip.toString().c_str());
    } else {
      DEBUG_SSEF("DNS resolution failed for: %s", hostname.c_str());
    }
    
    if (!http.begin(client, p->url)) {
      UBaseType_t stackAfterBegin = uxTaskGetStackHighWaterMark(NULL);
      DEBUG_SSEF("HTTP begin failed - Stack available: %d bytes", stackAfterBegin);
      *(p->result) = "Error: Failed to initialize HTTP client";
      *(p->completed) = true;
      xSemaphoreGive(p->semaphore);
      vTaskDelete(NULL);
      return;
    }
    
    UBaseType_t stackAfterBegin = uxTaskGetStackHighWaterMark(NULL);
    DEBUG_SSEF("After HTTP begin - Stack available: %d bytes (used: %d)", 
               stackAfterBegin, stackAfterHttp - stackAfterBegin);
    
    http.setTimeout(10000); // 10 second timeout
    http.addHeader("User-Agent", "ESP32-QtPy-Automation-Downloader");
    
    UBaseType_t stackBeforeGet = uxTaskGetStackHighWaterMark(NULL);
    DEBUG_SSEF("Before HTTP GET - Stack available: %d bytes", stackBeforeGet);
    
    int httpCode = http.GET();
    
    UBaseType_t stackAfterGet = uxTaskGetStackHighWaterMark(NULL);
    DEBUG_SSEF("After HTTP GET - Stack available: %d bytes (used: %d), HTTP code: %d", 
               stackAfterGet, stackBeforeGet - stackAfterGet, httpCode);
    
    // Get response headers for debugging
    String location = http.header("Location");
    String contentType = http.header("Content-Type");
    String server = http.header("Server");
    DEBUG_SSEF("Response headers - Location: '%s', Content-Type: '%s', Server: '%s'", 
               location.c_str(), contentType.c_str(), server.c_str());
    
    if (httpCode != HTTP_CODE_OK) {
      // Get the error response body for more info
      String errorBody = http.getString();
      DEBUG_SSEF("HTTP error response body: '%s'", errorBody.c_str());
      http.end();
      *(p->result) = "Error: HTTP " + String(httpCode) + " - " + errorBody;
      *(p->completed) = true;
      xSemaphoreGive(p->semaphore);
      vTaskDelete(NULL);
      return;
    }
    
    DEBUG_SSEF("Getting response payload - Stack available: %d bytes", uxTaskGetStackHighWaterMark(NULL));
    
    String payload = http.getString();
    
    UBaseType_t stackAfterPayload = uxTaskGetStackHighWaterMark(NULL);
    DEBUG_SSEF("After getString - Stack available: %d bytes, payload size: %d bytes", 
               stackAfterPayload, payload.length());
    
    http.end();
    
    UBaseType_t stackFinal = uxTaskGetStackHighWaterMark(NULL);
    DEBUG_SSEF("Download completed - Final stack available: %d bytes, total used: %d bytes", 
               stackFinal, stackStart - stackFinal);
    
    *(p->result) = payload;
    *(p->completed) = true;
    xSemaphoreGive(p->semaphore);
    vTaskDelete(NULL);
  };
  
  // Create task with 16KB stack (same as thermal sensor init)
  TaskHandle_t downloadTaskHandle;
  BaseType_t taskResult = xTaskCreate(
    downloadTask,
    "DownloadAutomation",
    16384, // 16KB stack
    &params,
    1, // Priority
    &downloadTaskHandle
  );
  
  if (taskResult != pdPASS) {
    vSemaphoreDelete(downloadSemaphore);
    return "Error: Failed to create download task";
  }
  
  DEBUG_SSEF("Download task created, waiting for completion");
  
  // Wait for download to complete (max 15 seconds)
  if (xSemaphoreTake(downloadSemaphore, pdMS_TO_TICKS(15000)) != pdTRUE) {
    vTaskDelete(downloadTaskHandle);
    vSemaphoreDelete(downloadSemaphore);
    return "Error: Download timeout";
  }
  
  vSemaphoreDelete(downloadSemaphore);
  
  if (!downloadCompleted) {
    return "Error: Download task failed to complete";
  }
  
  String payload = downloadResult;
  // If the download task reported a specific error, propagate it
  if (payload.startsWith("Error:")) {
    return payload;
  }
  
  if (payload.length() == 0) {
    return "Error: Downloaded content is empty";
  }
  
  broadcastOutput("Downloaded " + String(payload.length()) + " bytes");
  
  // Try to parse as JSON to validate
  if (payload.indexOf('{') < 0 || payload.indexOf('}') < 0) {
    return "Error: Downloaded content does not appear to be valid JSON";
  }
  
  // If name is provided, try to set it in the automation
  if (name.length() > 0) {
    // Look for "name" field and replace it
    int namePos = payload.indexOf("\"name\"");
    if (namePos >= 0) {
      int colonPos = payload.indexOf(':', namePos);
      if (colonPos >= 0) {
        int valueStart = payload.indexOf('"', colonPos);
        if (valueStart >= 0) {
          int valueEnd = payload.indexOf('"', valueStart + 1);
          if (valueEnd >= 0) {
            String before = payload.substring(0, valueStart + 1);
            String after = payload.substring(valueEnd);
            payload = before + name + after;
          }
        }
      }
    }
  }
  
  // Use the existing automation add command to add the downloaded automation
  // Parse the JSON to extract parameters for the add command
  String addCmd = "automation add ";
  
  // Extract common fields from JSON
  auto extractJsonValue = [&](const String& key) -> String {
    String needle = "\"" + key + "\"";
    int pos = payload.indexOf(needle);
    if (pos < 0) return "";
    
    int colonPos = payload.indexOf(':', pos);
    if (colonPos < 0) return "";
    
    // Skip whitespace after colon
    int valueStart = colonPos + 1;
    while (valueStart < payload.length() && (payload[valueStart] == ' ' || payload[valueStart] == '\t')) {
      valueStart++;
    }
    
    if (valueStart >= payload.length()) return "";
    
    String value;
    if (payload[valueStart] == '"') {
      // String value - extract without quotes
      valueStart++; // Skip opening quote
      int valueEnd = payload.indexOf('"', valueStart);
      if (valueEnd < 0) return "";
      value = payload.substring(valueStart, valueEnd);
    } else if (payload[valueStart] == 't' || payload[valueStart] == 'f') {
      // Boolean value
      if (payload.substring(valueStart, valueStart + 4) == "true") {
        value = "true";
      } else if (payload.substring(valueStart, valueStart + 5) == "false") {
        value = "false";
      }
    } else {
      // Number or other value
      int valueEnd = valueStart;
      while (valueEnd < payload.length() && 
             payload[valueEnd] != ',' && 
             payload[valueEnd] != '}' && 
             payload[valueEnd] != '\n' && 
             payload[valueEnd] != ' ') {
        valueEnd++;
      }
      value = payload.substring(valueStart, valueEnd);
      value.trim();
    }
    
    return value;
  };
  
  String autoName = extractJsonValue("name");
  String autoType = extractJsonValue("type");
  String enabled = extractJsonValue("enabled");
  String atTime = extractJsonValue("time");  // Look for 'time' field
  String days = extractJsonValue("days");
  String afterDelay = extractJsonValue("delay");  // Look for 'delay' field
  String interval = extractJsonValue("interval");  // Look for 'interval' field
  String conditions = extractJsonValue("conditions");  // Look for 'conditions' field
  
  // Debug output for extracted fields
  broadcastOutput("Extracted fields - name: '" + autoName + "', type: '" + autoType + "', conditions: '" + conditions + "'");
  // Extract commands array (special handling for JSON array)
  String commands;
  {
    String needle = "\"commands\"";
    int pos = payload.indexOf(needle);
    if (pos >= 0) {
      int colonPos = payload.indexOf(':', pos);
      if (colonPos >= 0) {
        // Skip whitespace after colon
        int arrayStart = colonPos + 1;
        while (arrayStart < payload.length() && (payload[arrayStart] == ' ' || payload[arrayStart] == '\t' || payload[arrayStart] == '\n')) {
          arrayStart++;
        }
        
        if (arrayStart < payload.length() && payload[arrayStart] == '[') {
          // Find matching closing bracket
          int bracketCount = 0;
          int arrayEnd = arrayStart;
          for (int i = arrayStart; i < payload.length(); i++) {
            if (payload[i] == '[') bracketCount++;
            else if (payload[i] == ']') bracketCount--;
            
            if (bracketCount == 0) {
              arrayEnd = i + 1;
              break;
            }
          }
          
          if (bracketCount == 0) {
            commands = payload.substring(arrayStart, arrayEnd);
          }
        }
      }
    }
  }
  
  if (autoName.length() == 0) autoName = name.length() > 0 ? name : "Downloaded Automation";
  if (autoType.length() == 0) return "Error: Automation missing required 'type' field";
  if (commands.length() == 0) return "Error: Automation missing required 'commands' array";
  
  // Parse JSON array of commands into semicolon-separated string
  String finalCommand = parseCommandsArray(commands);
  if (finalCommand.startsWith("Error:")) return finalCommand;
  
  // Build the automation add command (proper quoting for CLI parsing)
  String nameParam = autoName;
  if (autoName.indexOf(' ') >= 0 && !autoName.startsWith("\"")) {
    nameParam = "\"" + autoName + "\"";
  }
  
  // Quote the command if it contains spaces (for proper CLI parsing)
  String commandParam = finalCommand;
  if (finalCommand.indexOf(' ') >= 0 && !finalCommand.startsWith("\"")) {
    commandParam = "\"" + finalCommand + "\"";
  }
  addCmd += "name=" + nameParam + " type=" + autoType + " command=" + commandParam;
  
  if (enabled.length() > 0) addCmd += " enabled=" + enabled;
  if (atTime.length() > 0) addCmd += " time=" + atTime;  // Use 'time=' not 'atTime='
  if (days.length() > 0) addCmd += " days=" + days;
  if (afterDelay.length() > 0) addCmd += " delay=" + afterDelay;  // Use 'delay=' not 'afterDelay='
  if (interval.length() > 0) addCmd += " interval=" + interval;
  if (conditions.length() > 0) addCmd += " conditions=" + conditions;
  
  broadcastOutput("Executing: " + addCmd);
  
  // Execute the automation add command
  String result = cmd_automation_add(addCmd);
  
  if (result.startsWith("Error:")) {
    return "Download succeeded but failed to add automation: " + result;
  }
  
  return "Successfully downloaded and added automation: " + autoName + " (" + result + ")";
}

// ----- Automation command dispatcher (relocated) -----
static String cmd_automation(const String& originalCmd) {
  RETURN_VALID_IF_VALIDATE();
  String command = originalCmd;
  command.trim();
  command.toLowerCase();

  if (command == "automation list") {
    String json;
    if (!readText(AUTOMATIONS_JSON_FILE, json)) return "Error: failed to read automations.json";
    return json;
  } else if (command.startsWith("automation add ")) {
    return cmd_automation_add(originalCmd);
  } else if (command.startsWith("automation enable ")) {
    String args = originalCmd.substring(String("automation enable ").length());
    args.trim();
    auto getVal = [&](const String& key) {
      String k = key + "=";
      int p = args.indexOf(k);
      if (p < 0) return String("");
      int s = p + k.length();
      int e = args.indexOf(' ', s);
      if (e < 0) e = args.length();
      return args.substring(s, e);
    };
    String idStr = getVal("id");
    if (idStr.length() == 0) return "Usage: automation enable id=<id>";
    String json;
    if (!readText(AUTOMATIONS_JSON_FILE, json)) return "Error: failed to read automations.json";
    String needle = String("\"id\": ") + idStr;
    int idPos = json.indexOf(needle);
    if (idPos < 0) return "Error: automation id not found";
    int enabledPos = json.indexOf("\"enabled\":", idPos);
    if (enabledPos < 0) return "Error: malformed automation";
    int valueStart = json.indexOf(':', enabledPos) + 1;
    while (valueStart < (int)json.length() && json[valueStart] == ' ') valueStart++;
    int valueEnd = json.indexOf(',', valueStart);
    if (valueEnd < 0) valueEnd = json.indexOf('}', valueStart);
    if (valueEnd < 0) return "Error: malformed JSON";
    json = json.substring(0, valueStart) + "true" + json.substring(valueEnd);
    if (!writeAutomationsJsonAtomic(json)) return "Error: failed to write automations.json";
    gAutosDirty = true;
    return "Enabled automation id=" + idStr;
  } else if (command.startsWith("automation disable ")) {
    String args = originalCmd.substring(String("automation disable ").length());
    args.trim();
    auto getVal = [&](const String& key) {
      String k = key + "=";
      int p = args.indexOf(k);
      if (p < 0) return String("");
      int s = p + k.length();
      int e = args.indexOf(' ', s);
      if (e < 0) e = args.length();
      return args.substring(s, e);
    };
    String idStr = getVal("id");
    if (idStr.length() == 0) return "Usage: automation disable id=<id>";
    String json;
    if (!readText(AUTOMATIONS_JSON_FILE, json)) return "Error: failed to read automations.json";
    String needle = String("\"id\": ") + idStr;
    int idPos = json.indexOf(needle);
    if (idPos < 0) return "Error: automation id not found";
    int enabledPos = json.indexOf("\"enabled\":", idPos);
    if (enabledPos < 0) return "Error: malformed automation";
    int valueStart = json.indexOf(':', enabledPos) + 1;
    while (valueStart < (int)json.length() && json[valueStart] == ' ') valueStart++;
    int valueEnd = json.indexOf(',', valueStart);
    if (valueEnd < 0) valueEnd = json.indexOf('}', valueStart);
    if (valueEnd < 0) return "Error: malformed JSON";
    json = json.substring(0, valueStart) + "false" + json.substring(valueEnd);
    if (!writeAutomationsJsonAtomic(json)) return "Error: failed to write automations.json";
    gAutosDirty = true;
    return "Disabled automation id=" + idStr;
  } else if (command.startsWith("automation delete ")) {
    String args = originalCmd.substring(String("automation delete ").length());
    args.trim();
    auto getVal = [&](const String& key) {
      String k = key + "=";
      int p = args.indexOf(k);
      if (p < 0) return String("");
      int s = p + k.length();
      int e = args.indexOf(' ', s);
      if (e < 0) e = args.length();
      return args.substring(s, e);
    };
    String idStr = getVal("id");
    if (idStr.length() == 0) return "Usage: automation delete id=<id>";
    String json;
    if (!readText(AUTOMATIONS_JSON_FILE, json)) return "Error: failed to read automations.json";
    String needle = String("\"id\": ") + idStr;
    int idPos = json.indexOf(needle);
    if (idPos < 0) return "Error: automation id not found";

    // Find array bounds
    int arrayStart = json.indexOf('[');
    if (arrayStart < 0) return "Error: malformed JSON - no array";
    int arrayEnd = json.lastIndexOf(']');
    if (arrayEnd < 0) return "Error: malformed JSON - no array end";

    // Find object bounds
    int objStart = json.lastIndexOf('{', idPos);
    if (objStart < 0) return "Error: malformed JSON";
    int objEnd = json.indexOf('}', idPos);
    if (objEnd < 0) return "Error: malformed JSON";

    // Check if this is the only object in the array
    String arrayContent = json.substring(arrayStart + 1, arrayEnd);
    arrayContent.trim();
    bool isOnlyObject = (arrayContent.indexOf('{') == arrayContent.lastIndexOf('{'));

    if (isOnlyObject) {
      // If it's the only object, replace with empty array
      json = json.substring(0, arrayStart + 1) + json.substring(arrayEnd);
    } else {
      // Multiple objects - handle comma removal
      int delStart = objStart, delEnd = objEnd + 1;

      // Look for comma after the object
      if (delEnd < (int)json.length() && json[delEnd] == ',') {
        delEnd++;  // Include trailing comma
      } else {
        // No trailing comma, look for leading comma
        int commaPos = json.lastIndexOf(',', objStart);
        if (commaPos > arrayStart) {
          delStart = commaPos;  // Include leading comma
        }
      }

      json = json.substring(0, delStart) + json.substring(delEnd);
    }

    if (!writeAutomationsJsonAtomic(json)) return "Error: failed to write automations.json";
    gAutosDirty = true;
    return "Deleted automation id=" + idStr;
  } else if (command == "automation sanitize") {
    String json;
    if (!readText(AUTOMATIONS_JSON_FILE, json)) return "Error: failed to read automations.json";
    if (sanitizeAutomationsJson(json)) {
      if (!writeAutomationsJsonAtomic(json)) return "Error: failed to write automations.json";
      gAutosDirty = true;
      DEBUGF(DEBUG_AUTOMATIONS, "[autos] CLI sanitize: fixed duplicate IDs; scheduler refresh queued");
      return "Sanitized automations.json: fixed duplicate IDs";
    } else {
      DEBUGF(DEBUG_AUTOMATIONS, "[autos] CLI sanitize: no duplicate IDs found");
      return "Sanitize: no changes needed";
    }
  } else if (command == "automation recompute") {
    RETURN_VALID_IF_VALIDATE();
    // Admin check now handled by executeCommand pipeline

    String json;
    if (!readText(AUTOMATIONS_JSON_FILE, json)) return "Error: failed to read automations.json";

    time_t now = time(nullptr);
    if (now <= 0) return "Error: no valid system time for recompute";

    int recomputed = 0, failed = 0;
    bool modified = false;

    // Parse through all automations and recompute nextAt
    int pos = 0;
    while (true) {
      int idPos = json.indexOf("\"id\"", pos);
      if (idPos < 0) break;
      int colon = json.indexOf(':', idPos);
      if (colon < 0) break;
      int comma = json.indexOf(',', colon + 1);
      int braceEnd = json.indexOf('}', colon + 1);
      if (braceEnd < 0) break;
      if (comma < 0 || comma > braceEnd) comma = braceEnd;
      String idStr = json.substring(colon + 1, comma);
      idStr.trim();
      long id = idStr.toInt();

      // Extract automation object
      int objStart = json.lastIndexOf('{', idPos);
      if (objStart < 0) {
        pos = braceEnd + 1;
        continue;
      }
      String obj = json.substring(objStart, braceEnd + 1);

      // Check if enabled
      bool enabled = (obj.indexOf("\"enabled\": true") >= 0) || (obj.indexOf("\"enabled\":true") >= 0);
      if (!enabled) {
        DEBUGF(DEBUG_CLI | DEBUG_AUTOMATIONS, "[autos recompute] id=%ld skip: disabled", id);
        pos = braceEnd + 1;
        continue;
      }

      // Compute nextAt
      time_t nextAt = computeNextRunTime(obj, now);
      if (nextAt > 0) {
        if (updateAutomationNextAt(id, nextAt)) {
          recomputed++;
          modified = true;
          DEBUGF(DEBUG_CLI | DEBUG_AUTOMATIONS, "[autos recompute] id=%ld nextAt=%lu", id, (unsigned long)nextAt);
        } else {
          failed++;
          DEBUGF(DEBUG_CLI | DEBUG_AUTOMATIONS, "[autos recompute] id=%ld failed to update", id);
        }
      } else {
        failed++;
        DEBUGF(DEBUG_CLI | DEBUG_AUTOMATIONS, "[autos recompute] id=%ld could not compute nextAt", id);
      }

      pos = braceEnd + 1;
    }

    if (modified) {
      gAutosDirty = true;
      DEBUGF(DEBUG_AUTOMATIONS, "[autos recompute] scheduler refresh queued");
    }

    return "Recomputed nextAt: " + String(recomputed) + " succeeded, " + String(failed) + " failed";
  } else if (command.startsWith("automation run ")) {
    return cmd_automation_run(originalCmd);
  } else {
    return "Usage: automation list|add|enable|disable|delete|run|sanitize|recompute";
  }
}

// ----- Debug command flow handler (relocated) -----
static String cmd_debugcommandflow(const String& originalCmd) {
  RETURN_VALID_IF_VALIDATE();
  // Admin check now handled by executeCommand pipeline
  int sp1 = originalCmd.indexOf(' ');
  if (sp1 < 0) return "Usage: debugcommandflow <0|1>";
  String valStr = originalCmd.substring(sp1 + 1);
  valStr.trim();
  int v = valStr.toInt();
  gSettings.debugCommandFlow = (v == 1);
  saveUnifiedSettings();
  applySettings();
  return "debugCommandFlow set to " + String(v);
}

// ----- Debug users handler -----
static String cmd_debugusers(const String& originalCmd) {
  RETURN_VALID_IF_VALIDATE();
  // Admin check now handled by executeCommand pipeline
  int sp1 = originalCmd.indexOf(' ');
  if (sp1 < 0) return "Usage: debugusers <0|1>";
  String valStr = originalCmd.substring(sp1 + 1);
  valStr.trim();
  int v = valStr.toInt();
  gSettings.debugUsers = (v == 1);
  saveUnifiedSettings();
  applySettings();
  return String("debugUsers set to ") + String(v);
}

// ----- Set command handler (relocated) -----
static String cmd_set(const String& originalCmd) {
  RETURN_VALID_IF_VALIDATE();
  String args = originalCmd.substring(4);
  args.trim();
  int spacePos = args.indexOf(' ');
  if (spacePos < 0) return "Usage: set <setting> <value>";
  String setting = args.substring(0, spacePos);
  String value = args.substring(spacePos + 1);
  setting.trim();
  value.trim();
  setting.toLowerCase();
  if (setting == "tzoffsetminutes") {
    int offset = value.toInt();
    if (offset < -720 || offset > 720) return "Error: timezone offset must be between -720 and 720 minutes";
    gSettings.tzOffsetMinutes = offset;
    saveUnifiedSettings();
    setupNTP();
    return "Timezone offset set to " + String(offset) + " minutes";
  } else if (setting == "ntpserver") {
    if (value.length() == 0) return "Error: NTP server cannot be empty";
    WiFiUDP udp;
    IPAddress ntpIP;
    if (!WiFi.hostByName(value.c_str(), ntpIP)) { return "Error: Cannot resolve NTP server hostname '" + value + "'"; }
    byte ntpPacket[48];
    memset(ntpPacket, 0, 48);
    ntpPacket[0] = 0b11100011;
    ntpPacket[2] = 6;
    ntpPacket[3] = 0xEC;
    udp.begin(8888);
    if (!udp.beginPacket(ntpIP, 123)) {
      udp.stop();
      return "Error: Cannot connect to NTP server '" + value + "'";
    }
    udp.write(ntpPacket, 48);
    if (!udp.endPacket()) {
      udp.stop();
      return "Error: Failed to send NTP request to '" + value + "'";
    }
    unsigned long startTime = millis();
    int packetSize = 0;
    while (millis() - startTime < 5000) {
      packetSize = udp.parsePacket();
      if (packetSize >= 48) break;
      delay(10);
    }
    udp.stop();
    if (packetSize < 48) {
      DEBUG_DATETIMEF("NTP validation failed: no valid response from %s (packet size: %d)", value.c_str(), packetSize);
      return "Error: No response from NTP server '" + value + "'. Server may be down or not an NTP server.";
    }
    DEBUG_DATETIMEF("NTP validation successful: %s responded with %d byte packet", value.c_str(), packetSize);
    gSettings.ntpServer = value;
    saveUnifiedSettings();
    setupNTP();
    DEBUG_DATETIMEF("NTP server updated and applied: %s", value.c_str());
    return "NTP server set to " + value + " (connectivity verified)";
  } else if (setting == "thermalpollingms") {
    int v = value.toInt();
    if (v < 50 || v > 5000) return "Error: thermalPollingMs must be 50..5000";
    gSettings.thermalPollingMs = v;
    saveUnifiedSettings();
    return String("thermalPollingMs set to ") + v;
  } else if (setting == "tofpollingms") {
    int v = value.toInt();
    if (v < 50 || v > 5000) return "Error: tofPollingMs must be 50..5000";
    gSettings.tofPollingMs = v;
    saveUnifiedSettings();
    return String("tofPollingMs set to ") + v;
  } else if (setting == "tofstabilitythreshold") {
    int v = value.toInt();
    if (v < 0 || v > 50) return "Error: tofStabilityThreshold must be 0..50";
    gSettings.tofStabilityThreshold = v;
    saveUnifiedSettings();
    return String("tofStabilityThreshold set to ") + v;
  } else if (setting == "thermalpalettedefault") {
    String v = value;
    v.trim();
    v.toLowerCase();
    if (!(v == "grayscale" || v == "iron" || v == "rainbow" || v == "hot" || v == "coolwarm")) return "Error: thermalPaletteDefault must be grayscale|iron|rainbow|hot|coolwarm";
    gSettings.thermalPaletteDefault = v;
    saveUnifiedSettings();
    return String("thermalPaletteDefault set to ") + v;
  } else if (setting == "thermalwebmaxfps") {
    int v = value.toInt();
    if (v < 1 || v > 60) return "Error: thermalWebMaxFps must be 1..60";
    gSettings.thermalWebMaxFps = v;
    saveUnifiedSettings();
    return String("thermalWebMaxFps set to ") + v;
  } else if (setting == "thermalewmafactor") {
    float f = value.toFloat();
    if (f < 0.0f || f > 1.0f) return "Error: thermalEWMAFactor must be 0..1";
    gSettings.thermalEWMAFactor = f;
    saveUnifiedSettings();
    return String("thermalEWMAFactor set to ") + String(f, 3);
  } else if (setting == "thermaltransitionms") {
    int v = value.toInt();
    if (v < 0 || v > 5000) return "Error: thermalTransitionMs must be 0..5000";
    gSettings.thermalTransitionMs = v;
    saveUnifiedSettings();
    return String("thermalTransitionMs set to ") + v;
  } else if (setting == "toftransitionms") {
    int v = value.toInt();
    if (v < 0 || v > 5000) return "Error: tofTransitionMs must be 0..5000";
    gSettings.tofTransitionMs = v;
    saveUnifiedSettings();
    return String("tofTransitionMs set to ") + v;
  } else if (setting == "tofuimaxdistancemm") {
    int v = value.toInt();
    if (v < 100 || v > 10000) return "Error: tofUiMaxDistanceMm must be 100..10000";
    gSettings.tofUiMaxDistanceMm = v;
    saveUnifiedSettings();
    return String("tofUiMaxDistanceMm set to ") + v;
  } else if (setting == "thermalinterpolationenabled") {
    String vl = value;
    vl.trim();
    vl.toLowerCase();
    int v = (vl == "1" || vl == "true") ? 1 : 0;
    gSettings.thermalInterpolationEnabled = (v == 1);
    saveUnifiedSettings();
    return String("thermalInterpolationEnabled set to ") + (gSettings.thermalInterpolationEnabled ? "1" : "0");
  } else if (setting == "thermalinterpolationsteps") {
    int v = value.toInt();
    if (v < 1 || v > 8) return "Error: thermalInterpolationSteps must be 1..8";
    gSettings.thermalInterpolationSteps = v;
    saveUnifiedSettings();
    return String("thermalInterpolationSteps set to ") + v;
  } else if (setting == "thermalinterpolationbuffersize") {
    int v = value.toInt();
    if (v < 1 || v > 10) return "Error: thermalInterpolationBufferSize must be 1..10";
    gSettings.thermalInterpolationBufferSize = v;
    saveUnifiedSettings();
    return String("thermalInterpolationBufferSize set to ") + v;
  } else if (setting == "thermalwebclientquality") {
    int v = value.toInt();
    if (v < 1 || v > 4) return "Error: thermalWebClientQuality must be 1..4";
    gSettings.thermalWebClientQuality = v;
    saveUnifiedSettings();
    return String("thermalWebClientQuality set to ") + v;
  } else if (setting == "espnowenabled") {
    String vl = value;
    vl.trim();
    vl.toLowerCase();
    int v = (vl == "1" || vl == "true") ? 1 : 0;
    gSettings.espnowenabled = (v == 1);
    saveUnifiedSettings();
    return String("espnowenabled set to ") + (gSettings.espnowenabled ? "1" : "0") + " (takes effect after reboot)";
  } else {
    return "Error: unknown setting '" + setting + "'";
  }
}

// Append a command line with a source tag into the unified feed
static inline void appendCommandToFeed(const char* source, const String& cmd, const String& user = String(), const String& ip = String()) {
  String line = "[";
  line += source;
  if (user.length() || ip.length()) {
    line += " ";
    if (user.length()) { line += user; }
    if (ip.length()) {
      line += "@";
      line += ip;
    }
  }
  line += "] $ ";
  line += cmd;
  printToWeb(line);
}

void broadcastOutput(const String& s) {
  // Always append to unified history buffer so the web CLI reflects
  // all activity (serial/web/TFT/auth), regardless of Web output flag.
  printToWeb(s);
  if (gOutputFlags & OUTPUT_SERIAL) printToSerial(s);
  if (gOutputFlags & OUTPUT_TFT) printToTFT(s);
}

// Build a standardized origin prefix like: [web user@ip]
static String originPrefix(const char* source, const String& user, const String& ip) {
  String p = "[";
  p += source;
  if (user.length() || ip.length()) {
    p += " ";
    if (user.length()) { p += user; }
    if (ip.length()) {
      p += "@";
      p += ip;
    }
  }
  p += "] ";
  return p;
}

static inline void broadcastWithOrigin(const char* source, const String& user, const String& ip, const String& msg) {
  Serial.println("[DEBUG-BROADCAST] broadcastWithOrigin (inline) called:");
  Serial.println("  source: '" + String(source ? source : "NULL") + "'");
  Serial.println("  user: '" + user + "'");
  Serial.println("  ip: '" + ip + "'");
  Serial.println("  msg: '" + msg + "'");
  
  // Debug: Show all active sessions
  Serial.println("[DEBUG-SESSIONS] Active sessions:");
  for (int i = 0; i < MAX_SESSIONS; i++) {
    if (gSessions[i].user.length() > 0) {
      Serial.println("  [" + String(i) + "] user='" + gSessions[i].user + "' sid='" + gSessions[i].sid + "' sockfd=" + String(gSessions[i].sockfd) + " expires=" + String(gSessions[i].expiresAt) + " ip='" + gSessions[i].ip + "'");
    }
  }
  Serial.println("[DEBUG-SESSIONS] Total sessions checked: " + String(MAX_SESSIONS));
  
  // Check if this is a targeted message (ip parameter contains username instead of IP)
  bool isTargetedMessage = false;
  String targetUser = "";
  
  // If ip doesn't contain ":" or "." it's likely a username, not an IP
  if (ip.length() > 0 && ip.indexOf(':') == -1 && ip.indexOf('.') == -1) {
    isTargetedMessage = true;
    targetUser = ip;
    Serial.println("[DEBUG-BROADCAST] Detected targeted message to user: '" + targetUser + "'");
  }
  
  if (isTargetedMessage) {
    // Find the target user's session
    bool userFound = false;
    for (int i = 0; i < MAX_SESSIONS; i++) {
      if (gSessions[i].user.length() > 0 && gSessions[i].user == targetUser) {
        Serial.println("[DEBUG-BROADCAST] Found target user session [" + String(i) + "] - sending targeted message");
        
        // Create the message with proper prefix
        String targetedMsg = originPrefix(source ? source : "system", user, targetUser) + msg;
        
        // Send message directly to this specific session's notice queue
        Serial.println("[DEBUG-BROADCAST] Sending targeted message to session: sockfd=" + String(gSessions[i].sockfd) + " sid=" + gSessions[i].sid);
        sseEnqueueNotice(gSessions[i], targetedMsg);
        Serial.println("[DEBUG-BROADCAST] Message queued for user '" + targetUser + "' (qCount=" + String(gSessions[i].nqCount) + ")");
        
        userFound = true;
        break;
      }
    }
    
    if (!userFound) {
      Serial.println("[DEBUG-BROADCAST] Target user '" + targetUser + "' not found in active sessions");
      broadcastOutput("[ERROR] User '" + targetUser + "' not found or not logged in");
    }
  } else {
    // Regular broadcast to all users
    Serial.println("[DEBUG-BROADCAST] Regular broadcast to all users");
    
    // Session-only: if origin is serial and serial sink is disabled, enable for this session
    if (source && strcmp(source, "serial") == 0) {
      if (!(gOutputFlags & OUTPUT_SERIAL)) {
        gOutputFlags |= OUTPUT_SERIAL;  // session-only; do not modify persisted settings
      }
    }
    // Prefix and broadcast via simple sinks
    broadcastOutput(originPrefix(source ? source : "system", user, ip) + msg);
  }
}

// Now that OUTPUT_* and gOutputFlags are defined, implement applySettings
static void applySettings() {
  // Apply persisted output lanes
  uint8_t flags = 0;
  if (gSettings.outSerial) flags |= OUTPUT_SERIAL;
  if (gSettings.outTft) flags |= OUTPUT_TFT;
  if (gSettings.outWeb) flags |= OUTPUT_WEB;
  gOutputFlags = flags;  // replace current routing with persisted lanes
  // CLI history size would be applied where buffer is managed (placeholder here)


  // Apply debug settings to runtime flags
  gDebugFlags = DEBUG_SECURITY;  // Always include security logs
  if (gSettings.debugAuthCookies) gDebugFlags |= DEBUG_AUTH;
  if (gSettings.debugHttp) gDebugFlags |= DEBUG_HTTP;
  if (gSettings.debugSse) gDebugFlags |= DEBUG_SSE;
  if (gSettings.debugCli) gDebugFlags |= DEBUG_CLI;
  if (gSettings.debugSensorsFrame) gDebugFlags |= DEBUG_SENSORS_FRAME;
  if (gSettings.debugSensorsData) gDebugFlags |= DEBUG_SENSORS_DATA;
  if (gSettings.debugSensorsGeneral) gDebugFlags |= DEBUG_SENSORS;
  if (gSettings.debugWifi) gDebugFlags |= DEBUG_WIFI;
  if (gSettings.debugStorage) gDebugFlags |= DEBUG_STORAGE;
  if (gSettings.debugPerformance) gDebugFlags |= DEBUG_PERFORMANCE;
  if (gSettings.debugDateTime) gDebugFlags |= DEBUG_DATETIME;
  if (gSettings.debugCommandFlow) gDebugFlags |= DEBUG_CMD_FLOW;
  if (gSettings.debugUsers) gDebugFlags |= DEBUG_USERS;

  // NTP/timezone settings now used directly from gSettings in setupNTP()

  // Test debug statements for each logging category
  DEBUG_AUTHF("Auth logging enabled - session validation active");
  DEBUG_HTTPF("HTTP logging enabled - request routing active");
  DEBUG_SSEF("SSE logging enabled - event streaming active");
  DEBUG_CLIF("CLI logging enabled - command processing active");
  DEBUG_SENSORSF("General sensor logging enabled - device monitoring active");
  DEBUG_FRAMEF("Thermal frame logging enabled - MLX90640 capture tracking active");
  DEBUG_DATAF("Thermal data logging enabled - temperature processing active");
  DEBUG_WIFIF("WiFi logging enabled - network operations active");
  DEBUG_STORAGEF("Storage logging enabled - filesystem operations active");
  DEBUG_PERFORMANCEF("Performance logging enabled - loop monitoring active");
  DEBUG_SECURITYF("Security logging always active - login attempts tracked");

  // Apply thermal sensor refresh rate dynamically if initialized
  if (thermalSensor != nullptr) {
    int fps = gSettings.thermalTargetFps;
    if (fps < 1) fps = 1;
    if (fps > 8) fps = 8;
    // Map requested FPS to supported MLX90640 refresh enums (use nearest not exceeding)
    mlx90640_refreshrate_t rate = MLX90640_1_HZ;
    if (fps >= 8) rate = MLX90640_8_HZ;
    else if (fps >= 4) rate = MLX90640_4_HZ;
    else if (fps >= 2) rate = MLX90640_2_HZ;
    else rate = MLX90640_1_HZ;

    // Use configured I2C clock while applying sensor config
    Wire1ClockScope guard((gSettings.i2cClockThermalHz > 0) ? (uint32_t)gSettings.i2cClockThermalHz : 400000);

    // Use chess mode for full spatial resolution
    thermalSensor->setMode(MLX90640_CHESS);

    thermalSensor->setRefreshRate(rate);
  }
}

// ==========================
// URL query helpers
// ==========================

static bool getQueryParam(httpd_req_t* req, const char* key, String& out) {
  out = "";
  size_t qlen = httpd_req_get_url_query_len(req);
  if (qlen == 0) return false;
  std::unique_ptr<char, void (*)(void*)> qbuf((char*)ps_alloc(qlen + 1, AllocPref::PreferPSRAM, "http.query"), free);
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
  String out;
  out.reserve(in.length() + 8);
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

// Output flags API
// ==========================

// GET /api/output -> returns persisted (gSettings) and runtime (gOutputFlags) for serial/web/tft
static esp_err_t handleOutputGet(httpd_req_t* req) {
  AuthContext ctx;
  ctx.transport = AUTH_HTTP;
  ctx.opaque = req;
  ctx.path = "/api/output";
  getClientIP(req, ctx.ip);
  if (!tgRequireAuth(ctx)) return ESP_OK;
  int rtSerial = (gOutputFlags & OUTPUT_SERIAL) ? 1 : 0;
  int rtWeb = (gOutputFlags & OUTPUT_WEB) ? 1 : 0;
  int rtTft = (gOutputFlags & OUTPUT_TFT) ? 1 : 0;
  String json = String("{\"success\":true,\"persisted\":{");
  json += "\"serial\":" + String(gSettings.outSerial ? 1 : 0) + ",";
  json += "\"web\":" + String(gSettings.outWeb ? 1 : 0) + ",";
  json += "\"tft\":" + String(gSettings.outTft ? 1 : 0) + "},";
  json += "\"runtime\":{";
  json += "\"serial\":" + String(rtSerial) + ",";
  json += "\"web\":" + String(rtWeb) + ",";
  json += "\"tft\":" + String(rtTft) + "}}";
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, json.c_str(), HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

// POST /api/output/temp (x-www-form-urlencoded): serial=0/1&web=0/1&tft=0/1
static esp_err_t handleOutputTemp(httpd_req_t* req) {
  AuthContext ctx;
  ctx.transport = AUTH_HTTP;
  ctx.opaque = req;
  ctx.path = "/api/output/temp";
  getClientIP(req, ctx.ip);
  if (!tgRequireAuth(ctx)) return ESP_OK;

  // Read form body
  char buf[256];
  int total = 0;
  int remaining = req->content_len;
  while (remaining > 0 && total < (int)sizeof(buf) - 1) {
    int toRead = remaining;
    if (toRead > (int)sizeof(buf) - 1 - total) toRead = (int)sizeof(buf) - 1 - total;
    int r = httpd_req_recv(req, buf + total, toRead);
    if (r <= 0) break;
    total += r;
    remaining -= r;
  }
  buf[total] = '\0';

  // Parse values
  auto getVal = [&](const char* key) -> int {
    char val[8];
    if (httpd_query_key_value(buf, key, val, sizeof(val)) == ESP_OK) {
      return atoi(val);
    }
    return -1;  // not provided
  };
  int vSerial = getVal("serial");
  int vWeb = getVal("web");
  int vTft = getVal("tft");

  // Apply to runtime flags only
  if (vSerial == 0) {
    gOutputFlags &= ~OUTPUT_SERIAL;
  } else if (vSerial == 1) {
    gOutputFlags |= OUTPUT_SERIAL;
  }

  if (vWeb == 0) {
    gOutputFlags &= ~OUTPUT_WEB;
  } else if (vWeb == 1) {
    gOutputFlags |= OUTPUT_WEB;
  }

  if (vTft == 0) {
    gOutputFlags &= ~OUTPUT_TFT;
  } else if (vTft == 1) {
    gOutputFlags |= OUTPUT_TFT;
  }

  // Respond with updated runtime snapshot
  int rtSerial = (gOutputFlags & OUTPUT_SERIAL) ? 1 : 0;
  int rtWeb = (gOutputFlags & OUTPUT_WEB) ? 1 : 0;
  int rtTft = (gOutputFlags & OUTPUT_TFT) ? 1 : 0;
  String json = String("{\"success\":true,\"runtime\":{");
  json += "\"serial\":" + String(rtSerial) + ",";
  json += "\"web\":" + String(rtWeb) + ",";
  json += "\"tft\":" + String(rtTft) + "}}";
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, json.c_str(), HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

// Notice endpoint: returns and clears per-session notice
esp_err_t handleNotice(httpd_req_t* req) {
  httpd_resp_set_type(req, "application/json");
  
  // Check authentication manually to return JSON on failure
  String user;
  String ip;
  getClientIP(req, ip);
  
  if (!isAuthed(req, user)) {
    // Return 401 with JSON response instead of HTML
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_send(req, "{\"success\":false,\"error\":\"Authentication required\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }

  String sid = getCookieSID(req);
  int idx = findSessionIndexBySID(sid);
  String note = "";
  if (idx >= 0) {
    note = gSessions[idx].notice;
    gSessions[idx].notice = "";  // clear on read
    // If this is a revoke notice, immediately clear the session and expire cookie
    if (note.startsWith("[revoke]")) {
      gSessions[idx] = SessionEntry();
      httpd_resp_set_hdr(req, "Set-Cookie", "session=; Path=/; Max-Age=0; HttpOnly; SameSite=Strict");
    }
  }
  String json = String("{\"success\":true,\"notice\":\"") + jsonEscape(note) + "\"}";
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


// Auth-protected text log endpoint returning mirrored output
esp_err_t handleLogs(httpd_req_t* req) {
  AuthContext ctx;
  ctx.transport = AUTH_HTTP;
  ctx.opaque = req;
  ctx.path = "/api/logs";
  getClientIP(req, ctx.ip);
  if (!tgRequireAuth(ctx)) return ESP_OK;
  httpd_resp_set_type(req, "text/plain");
  if (!gWebMirror.buf) { gWebMirror.init(gWebMirrorCap); }
  httpd_resp_send(req, gWebMirror.buf, HTTPD_RESP_USE_STRLEN);
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
    String page = getLoginPage("", "", req);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, page.c_str(), HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }
  // POST
  int total_len = req->content_len;
  if (total_len <= 0) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No body");
    return ESP_FAIL;
  }
  std::unique_ptr<char, void (*)(void*)> buf((char*)ps_alloc(total_len + 1, AllocPref::PreferPSRAM, "http.login"), free);
  int received = 0;
  while (received < total_len) {
    int r = httpd_req_recv(req, buf.get() + received, total_len - received);
    if (r <= 0) {
      httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Read err");
      return ESP_FAIL;
    }
    received += r;
  }
  buf.get()[received] = '\0';
  String body(buf.get());
  String u = urlDecode(extractFormField(body, "username"));
  String p = urlDecode(extractFormField(body, "password"));
  broadcastOutput(String("[login] POST attempt: username='") + u + "', password_len=" + String(p.length()));

  bool validUser = isValidUser(u, p);
  broadcastOutput(String("[login] isValidUser result: ") + (validUser ? "true" : "false"));

  if (u.length() == 0 || p.length() == 0 || !validUser) {
    // Log failed authentication attempt
    String ip;
    getClientIP(req, ip);
    logAuthAttempt(false, req->uri, u, ip, "Invalid credentials");

    String page = getLoginPage(u, "Invalid username or password", req);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, page.c_str(), HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }
  // Success -> create session immediately and set cookie in this response
  broadcastOutput(String("[login] Login successful for user: ") + u);

  // Log successful authentication attempt
  String ip;
  getClientIP(req, ip);
  logAuthAttempt(true, req->uri, u, ip, "Login successful");

  // Clear auth cache immediately
  gAuthCache = { "", "", 0, "" };

  // Clear any existing logout reason for this IP to prevent false "signed out" messages
  String clientIP;
  getClientIP(req, clientIP);
  if (clientIP.length() > 0) {
    getLogoutReason(clientIP);  // This clears the reason by reading it
  }

  // Create session and capture SID for client-side fallback
  String sid = setSession(req, u);

  // Send styled login success page with Safari-compatible redirect
  httpd_resp_set_type(req, "text/html");
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
  httpd_resp_set_hdr(req, "Pragma", "no-cache");
  httpd_resp_set_hdr(req, "Expires", "0");

  String html = getLoginSuccessPage(sid);
  httpd_resp_send(req, html.c_str(), HTTPD_RESP_USE_STRLEN);

  broadcastOutput(String("[login] Safari-compatible session and cookie set for user: ") + u);
  return ESP_OK;
}

// Session setter: Step 2 of login process - set fresh cookies after clearing old ones
esp_err_t handleLoginSetSession(httpd_req_t* req) {
  // Check if we have a pending session to set
  if (gSessUser.length() == 0) {
    broadcastOutput("[login] No pending session, redirecting to login");
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/login");
    httpd_resp_send(req, "", 0);
    return ESP_OK;
  }

  // Create a new session entry and set cookies
  String user = gSessUser;  // capture then clear
  setSession(req, user);
  gSessUser = "";
  broadcastOutput(String("[login] Session set for user: ") + user);

  // Send HTML page with JavaScript redirect and cookie verification
  httpd_resp_set_type(req, "text/html");
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
  String html = "<!DOCTYPE html><html><head><title>Login Success</title></head><body>";
  html += "<script>";
  html += "console.log('Cookie verification page loaded');";
  html += "console.log('Document.cookie:', document.cookie);";
  html += "if(document.cookie.indexOf('session=') >= 0) {";
  html += "  console.log('Session cookie found, redirecting to dashboard');";
  html += "  window.location.href = '/dashboard';";
  html += "} else {";
  html += "  console.log('No session cookie found, waiting 1 second and retrying');";
  html += "  setTimeout(function() {";
  html += "    console.log('Retry - Document.cookie:', document.cookie);";
  html += "    if(document.cookie.indexOf('session=') >= 0) {";
  html += "      window.location.href = '/dashboard';";
  html += "    } else {";
  html += "      console.log('Cookie still not found, redirecting to login');";
  html += "      window.location.href = '/login';";
  html += "    }";
  html += "  }, 1000);";
  html += "}";
  html += "</script>";
  html += "<p>Login successful, checking session...</p>";
  html += "</body></html>";
  httpd_resp_send(req, html.c_str(), HTTPD_RESP_USE_STRLEN);
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
  AuthContext ctx;
  ctx.transport = AUTH_HTTP;
  ctx.opaque = req;
  ctx.path = req->uri ? req->uri : "/dashboard";
  getClientIP(req, ctx.ip);
  if (!tgRequireAuth(ctx)) return ESP_OK;
  logAuthAttempt(true, req->uri, ctx.user, ctx.ip, "");

  streamPageWithContent(req, "dashboard", ctx.user, streamDashboardContent);
  return ESP_OK;
}

void streamSettingsContent(httpd_req_t* req) {
  String u;
  isAuthed(req, u);
  String content = getSettingsPage(u);
  streamContentGeneric(req, content);
}

esp_err_t handleSettingsPage(httpd_req_t* req) {
  AuthContext ctx;
  ctx.transport = AUTH_HTTP;
  ctx.opaque = req;
  ctx.path = req->uri ? req->uri : "/settings";
  getClientIP(req, ctx.ip);
  if (!tgRequireAuth(ctx)) return ESP_OK;
  logAuthAttempt(true, req->uri, ctx.user, ctx.ip, "");

  streamPageWithContent(req, "settings", ctx.user, streamSettingsContent);
  return ESP_OK;
}

String getToFDataJSON();

esp_err_t handleSensorData(httpd_req_t* req) {
  AuthContext ctx;
  ctx.transport = AUTH_HTTP;
  ctx.opaque = req;
  ctx.path = req->uri ? req->uri : "/api/sensor";
  getClientIP(req, ctx.ip);
  if (!tgRequireAuth(ctx)) return ESP_OK;

  // Add CORS headers to prevent access control errors
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");

  // Get query parameter to determine which sensor data to return
  char query[256];
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
    char sensor[32];
    if (httpd_query_key_value(query, "sensor", sensor, sizeof(sensor)) == ESP_OK) {
      String sensorType = String(sensor);

      if (sensorType == "thermal") {
        // Always return thermal data, even if cache is stale - with mutex protection
        String json = "";
        if (lockSensorCache(pdMS_TO_TICKS(100))) {  // 100ms timeout for HTTP response
          json = "{\"v\":" + String(gSensorCache.thermalDataValid ? 1 : 0) + ",\"seq\":" + String(gSensorCache.thermalSeq) + ",\"mn\":" + String(gSensorCache.thermalMinTemp, 1) + ",\"mx\":" + String(gSensorCache.thermalMaxTemp, 1) + ",\"f\":[";
          if (gSensorCache.thermalFrame) {
            for (int i = 0; i < 768; i++) {
              json += String((int)gSensorCache.thermalFrame[i]);  // Integer temps only
              if (i < 767) json += ",";
            }
          }
          json += "]}";
          unlockSensorCache();
        } else {
          // Timeout - return error response
          json = "{\"error\":\"Sensor data temporarily unavailable\"}";
        }

        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json.c_str(), json.length());
        return ESP_OK;
      } else if (sensorType == "tof") {
        // Always return ToF data, even if cache is stale
        if (gDebugFlags & DEBUG_SENSORS_FRAME) {
          Serial.println("[DEBUG_SENSORS_FRAME] handleSensorData: ToF data requested via /api/sensors?sensor=tof");
        }
        String json = getToFDataJSON();
        if (gDebugFlags & DEBUG_SENSORS_FRAME) {
          Serial.printf("[DEBUG_SENSORS_FRAME] handleSensorData: ToF JSON response length=%d\n", json.length());
        }
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json.c_str(), json.length());
        return ESP_OK;
      } else if (sensorType == "imu") {
        // Return cached IMU data with mutex protection
        String json = "";
        if (lockSensorCache(pdMS_TO_TICKS(100))) {  // 100ms timeout for HTTP response
          json = "{";
          json += "\"valid\":" + String(gSensorCache.imuDataValid ? "true" : "false");
          json += ",\"seq\":" + String(gSensorCache.imuSeq);
          json += ",\"accel\":{";
          json += "\"x\":" + String(gSensorCache.accelX, 3) + ",\"y\":" + String(gSensorCache.accelY, 3) + ",\"z\":" + String(gSensorCache.accelZ, 3) + "}";
          json += ",\"gyro\":{";
          json += "\"x\":" + String(gSensorCache.gyroX, 3) + ",\"y\":" + String(gSensorCache.gyroY, 3) + ",\"z\":" + String(gSensorCache.gyroZ, 3) + "}";
          json += ",\"ori\":{";
          json += "\"yaw\":" + String(gSensorCache.oriYaw, 2) + ",\"pitch\":" + String(gSensorCache.oriPitch, 2) + ",\"roll\":" + String(gSensorCache.oriRoll, 2) + "}";
          json += ",\"temp\":" + String(gSensorCache.imuTemp, 1);
          json += ",\"timestamp\":" + String(gSensorCache.imuLastUpdate);
          json += "}";
          unlockSensorCache();
        } else {
          // Timeout - return error response
          json = "{\"error\":\"IMU cache timeout\"}";
        }
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
  AuthContext ctx;
  ctx.transport = AUTH_HTTP;
  ctx.opaque = req;
  ctx.path = req->uri ? req->uri : "/api/settings";
  getClientIP(req, ctx.ip);
  if (!tgRequireAuth(ctx)) return ESP_OK;

  String json = "{\"success\":true,\"settings\":{";
  json += "\"ntpServer\":\"" + gSettings.ntpServer + "\",";
  json += "\"tzOffsetMinutes\":" + String(gSettings.tzOffsetMinutes) + ",";
  // Grouped output
  json += "\"output\":{\"outSerial\":" + String(gSettings.outSerial ? 1 : 0) + ",\"outWeb\":" + String(gSettings.outWeb ? 1 : 0) + ",\"outTft\":" + String(gSettings.outTft ? 1 : 0) + "},";
  // Grouped debug flags
  json += "\"debug\":{\"authCookies\":" + String((gDebugFlags & DEBUG_AUTH) ? 1 : 0) + ",\"http\":" + String((gDebugFlags & DEBUG_HTTP) ? 1 : 0) + ",\"sse\":" + String((gDebugFlags & DEBUG_SSE) ? 1 : 0) + ",\"cli\":" + String((gDebugFlags & DEBUG_CLI) ? 1 : 0) + ",\"sensorsFrame\":" + String((gDebugFlags & DEBUG_SENSORS_FRAME) ? 1 : 0) + ",\"sensorsData\":" + String((gDebugFlags & DEBUG_SENSORS_DATA) ? 1 : 0) + ",\"sensorsGeneral\":" + String((gDebugFlags & DEBUG_SENSORS) ? 1 : 0) + ",\"wifi\":" + String((gDebugFlags & DEBUG_WIFI) ? 1 : 0) + ",\"storage\":" + String((gDebugFlags & DEBUG_STORAGE) ? 1 : 0) + ",\"performance\":" + String((gDebugFlags & DEBUG_PERFORMANCE) ? 1 : 0) + ",\"dateTime\":" + String((gDebugFlags & DEBUG_DATETIME) ? 1 : 0) + "},";
  // Add Command Flow flag for UI
  json.remove(json.length() - 2);  // remove trailing "},"
  json += ",\"cmdFlow\":" + String((gDebugFlags & DEBUG_CMD_FLOW) ? 1 : 0) + "},";
  // Grouped thermal (ui/device)
  json += "\"thermal\":{\"ui\":{\"thermalPollingMs\":" + String(gSettings.thermalPollingMs) + ",\"thermalPaletteDefault\":\"" + gSettings.thermalPaletteDefault + "\",\"thermalInterpolationEnabled\":" + String(gSettings.thermalInterpolationEnabled ? 1 : 0) + ",\"thermalInterpolationSteps\":" + String(gSettings.thermalInterpolationSteps) + ",\"thermalInterpolationBufferSize\":" + String(gSettings.thermalInterpolationBufferSize) + ",\"thermalWebClientQuality\":" + String(gSettings.thermalWebClientQuality) + ",\"thermalEWMAFactor\":" + String(gSettings.thermalEWMAFactor, 3) + ",\"thermalTransitionMs\":" + String(gSettings.thermalTransitionMs) + ",\"thermalWebMaxFps\":" + String(gSettings.thermalWebMaxFps) + "},\"device\":{\"thermalTargetFps\":" + String(gSettings.thermalTargetFps) + ",\"thermalDevicePollMs\":" + String(gSettings.thermalDevicePollMs) + ",\"i2cClockThermalHz\":" + String(gSettings.i2cClockThermalHz) + "}},";
  // Grouped tof (ui/device)
  json += "\"tof\":{\"ui\":{\"tofPollingMs\":" + String(gSettings.tofPollingMs) + ",\"tofStabilityThreshold\":" + String(gSettings.tofStabilityThreshold) + ",\"tofTransitionMs\":" + String(gSettings.tofTransitionMs) + ",\"tofUiMaxDistanceMm\":" + String(gSettings.tofUiMaxDistanceMm) + "},\"device\":{\"tofDevicePollMs\":" + String(gSettings.tofDevicePollMs) + ",\"i2cClockToFHz\":" + String(gSettings.i2cClockToFHz) + "}},";
  // Un-grouped device-side key(s) that remain top-level
  json += "\"imuDevicePollMs\":" + String(gSettings.imuDevicePollMs) + ",";
  // ESP-NOW settings
  json += "\"espnowenabled\":" + String(gSettings.espnowenabled ? 1 : 0) + ",";
  // Current WiFi connection info
  String currentSSID = WiFi.isConnected() ? WiFi.SSID() : String("");
  currentSSID.replace("\\", "\\\\");
  currentSSID.replace("\"", "\\\"");
  json += "\"wifiPrimarySSID\":\"" + currentSSID + "\",";
  json += "\"wifiAutoReconnect\":" + String(gSettings.wifiAutoReconnect ? "true" : "false") + ",";
  // Include saved wifiNetworks (no passwords)
  json += "\"wifiNetworks\":[";
  for (int i = 0; i < gWifiNetworkCount; ++i) {
    if (i) json += ",";
    json += String("{\"ssid\":\"") + gWifiNetworks[i].ssid + "\"," + "\"priority\":" + String(gWifiNetworks[i].priority) + "," + "\"hidden\":" + String(gWifiNetworks[i].hidden ? 1 : 0) + "}";
  }
  json += "]},";
  // Attach user and feature capabilities for client-side gating
  json += "\"user\":{\"username\":\"" + ctx.user + "\",\"isAdmin\":" + String(isAdminUser(ctx.user) ? "true" : "false") + "},";
  // Feature flags (toggleable centrally)
  json += "\"features\":{\"adminSessions\":" + String(isAdminUser(ctx.user) ? "true" : "false") + ",\"userApprovals\":true,\"adminControls\":true,\"sensorConfig\":true}}";

  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, json.c_str(), json.length());
  return ESP_OK;
}

// Device Registry API (GET): return device registry as JSON
esp_err_t handleDeviceRegistryGet(httpd_req_t* req) {
  AuthContext ctx;
  ctx.transport = AUTH_HTTP;
  ctx.opaque = req;
  ctx.path = req->uri ? req->uri : "/api/devices";
  getClientIP(req, ctx.ip);
  if (!tgRequireAuth(ctx)) return ESP_OK;

  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
  
  ensureDeviceRegistryFile();
  
  if (!LittleFS.exists("/devices.json")) {
    httpd_resp_send(req, "{\"error\":\"Device registry not found\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }
  
  File file = LittleFS.open("/devices.json", "r");
  if (!file) {
    httpd_resp_send(req, "{\"error\":\"Could not read device registry\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }
  
  String json = file.readString();
  file.close();
  
  httpd_resp_send(req, json.c_str(), json.length());
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
    if (!first) outJsonArr += ",";
    first = false;
    outJsonArr += String("{")
                  + "\"sid\":\"" + s.sid + "\","
                  + "\"createdAt\":" + String(s.createdAt) + ","
                  + "\"lastSeen\":" + String(s.lastSeen) + ","
                  + "\"expiresAt\":" + String(s.expiresAt) + ","
                  + "\"ip\":\"" + (s.ip.length() ? s.ip : "-") + "\","
                  + "\"current\":" + (s.sid == currentSid ? "true" : "false")
                  + "}";
  }
}

esp_err_t handleSessionsList(httpd_req_t* req) {
  // Admin-only: list all active sessions
  AuthContext ctx;
  ctx.transport = AUTH_HTTP;
  ctx.opaque = req;
  ctx.path = "/api/sessions";
  getClientIP(req, ctx.ip);
  if (!tgRequireAuth(ctx)) return ESP_OK;
  if (!isAdminUser(ctx.user)) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"success\":false,\"error\":\"Admin access required\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }
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
  AuthContext ctx;
  ctx.transport = AUTH_HTTP;
  ctx.opaque = req;
  ctx.path = "/api/sessions/revoke";
  getClientIP(req, ctx.ip);
  if (!tgRequireAuth(ctx)) return ESP_OK;
  if (!isAdminUser(ctx.user)) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"success\":false,\"error\":\"Admin access required\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }

  // Read body (application/x-www-form-urlencoded): sid=...
  char buf[256];
  int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
  if (ret <= 0) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"success\":false,\"error\":\"No data\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }
  buf[ret] = '\0';
  String body(buf);
  String sid = "";
  int p = body.indexOf("sid=");
  if (p >= 0) {
    p += 4;
    int e = body.indexOf('&', p);
    if (e < 0) e = body.length();
    sid = body.substring(p, e);
  }
  sid.replace("%3D", "=");
  sid.replace("%2F", "/");
  sid.replace("%25", "%");

  if (!sid.length()) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"success\":false,\"error\":\"sid required\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }
  int idx = findSessionIndexBySID(sid);
  if (idx < 0) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"success\":false,\"error\":\"not found\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }

  // Set a targeted revoke notice; session will be cleared on next /api/notice poll
  gSessions[idx].notice = "[revoke] Your session has been signed out by an administrator.";
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, "{\"success\":true}", HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

// Helper to require admin; sends JSON error if not authed/admin
static bool requireAdmin(httpd_req_t* req, String& uOut) {
  String u;
  String ip;
  getClientIP(req, ip);
  if (!isAuthed(req, u)) {
    sendAuthRequiredResponse(req);
    return false;
  }
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
    if (!first) outJsonArr += ",";
    first = false;
    outJsonArr += String("{")
                  + "\"sid\":\"" + s.sid + "\","
                  + "\"user\":\"" + s.user + "\","
                  + "\"createdAt\":" + String(s.createdAt) + ","
                  + "\"lastSeen\":" + String(s.lastSeen) + ","
                  + "\"expiresAt\":" + String(s.expiresAt) + ","
                  + "\"ip\":\"" + (s.ip.length() ? s.ip : "-") + "\","
                  + "\"current\":" + (s.sid == currentSid ? "true" : "false")
                  + "}";
  }
}

// Admin: list all sessions
esp_err_t handleAdminSessionsList(httpd_req_t* req) {
  AuthContext ctx;
  ctx.transport = AUTH_HTTP;
  ctx.opaque = req;
  ctx.path = "/api/admin/sessions";
  getClientIP(req, ctx.ip);
  if (!tgRequireAuth(ctx)) return ESP_OK;
  if (!isAdminUser(ctx.user)) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"success\":false,\"error\":\"Admin access required\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }
  String currentSid = getCookieSID(req);
  String arr;
  buildAllSessionsJson(currentSid, arr);
  String json = String("{\"success\":true,\"sessions\":[") + arr + "]}";
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, json.c_str(), HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

// Admin: revoke any session by SID
esp_err_t handleAdminSessionsRevoke(httpd_req_t* req) {
  AuthContext ctx;
  ctx.transport = AUTH_HTTP;
  ctx.opaque = req;
  ctx.path = "/api/admin/sessions/revoke";
  getClientIP(req, ctx.ip);
  if (!tgRequireAuth(ctx)) return ESP_OK;
  if (!isAdminUser(ctx.user)) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"success\":false,\"error\":\"Admin access required\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }
  // Read body (application/x-www-form-urlencoded): sid=...
  char buf[256];
  int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
  if (ret <= 0) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"success\":false,\"error\":\"No data\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }
  buf[ret] = '\0';
  String body(buf);
  String sid = "";
  int p = body.indexOf("sid=");
  if (p >= 0) {
    p += 4;
    int e = body.indexOf('&', p);
    if (e < 0) e = body.length();
    sid = body.substring(p, e);
  }
  sid.replace("%3D", "=");
  sid.replace("%2F", "/");
  sid.replace("%25", "%");
  if (!sid.length()) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"success\":false,\"error\":\"sid required\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }
  int idx = findSessionIndexBySID(sid);
  if (idx < 0) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"success\":false,\"error\":\"not found\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }
  // Preserve target username and IP for logging
  String targetUser = gSessions[idx].user;
  String targetIP = gSessions[idx].ip;
  int targetSockfd = gSessions[idx].sockfd;

  // Store logout reason for the target IP before force closing
  if (targetIP.length() > 0) {
    Serial.printf("[ADMIN_DEBUG] Admin revocation: storing admin revoke message for IP '%s'\n", targetIP.c_str());
    storeLogoutReason(targetIP, "Your session was revoked by an administrator.");
  }

  // Force close the connection immediately using native ESP32 HTTP server function
  if (targetSockfd >= 0) {
    esp_err_t closeResult = httpd_sess_trigger_close(server, targetSockfd);
    DEBUG_AUTHF("Force closing socket %d for user %s - %s", targetSockfd, targetUser.c_str(), closeResult == ESP_OK ? "SUCCESS" : "FAILED");
    broadcastOutput(String("[admin] Force closing socket ") + String(targetSockfd) + " for user " + targetUser + (closeResult == ESP_OK ? " - SUCCESS" : " - FAILED"));
  } else {
    DEBUG_AUTHF("No socket to close for user %s (sockfd=%d)", targetUser.c_str(), targetSockfd);
  }

  // Clear the session immediately
  gSessions[idx] = SessionEntry();

  // Broadcast general admin feed message (use legacy helper here; CommandContext not available yet in this scope)
  broadcastWithOrigin("admin", ctx.user, String(), String("Admin notice: session forcibly disconnected") + (targetUser.length() ? String(" for user '") + targetUser + "'" : String("")) + ".");
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, "{\"success\":true}", HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

// (Removed) handleSettingsUpdate: settings are saved via discrete CLI commands at /api/cli

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
  String page = htmlPublicShellWithNav(inner);
  httpd_resp_set_type(req, "text/html");
  httpd_resp_send(req, page.c_str(), HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

// ==========================
// Unified command executor
// ==========================

static const char* originFrom(const AuthContext& ctx) {
  // Only map known transports to stable strings; avoid assuming future ones exist
  switch (ctx.transport) {
    case AUTH_HTTP: return "web";
    default: break;
  }
  // Treat anything else as system/unknown unless explicitly HTTP
  return (ctx.transport == AUTH_SYSTEM) ? "system" : "unknown";
}

static bool hasAdminPrivilege(const AuthContext& ctx) {
  // System-origin or real admin user
  if (ctx.transport == AUTH_SYSTEM) return true;
  return isAdminUser(ctx.user);
}

static bool isAdminOnlyCommand(const String& cmd) {
  String c = cmd;
  c.trim();
  c.toLowerCase();
  
  // Comprehensive admin command protection - all commands requiring admin privileges
  return c.startsWith("wifissid") || c.startsWith("reboot") || c.startsWith("erase") || 
         c.startsWith("i2cclock") || c.startsWith("thermal") || c.startsWith("tof") ||
         c.startsWith("wifiadd") || c.startsWith("wifirm") || c.startsWith("wifipromote") ||
         c.startsWith("wifidisconnect") || c.startsWith("wifiautoreconnect") ||
         c.startsWith("mkdir") || c.startsWith("rmdir") || c.startsWith("filecreate") ||
         c.startsWith("filedelete") || c.startsWith("autolog") || c.startsWith("outweb") ||
         c.startsWith("outserial") || c.startsWith("outtft") || c.startsWith("automation") ||
         c.startsWith("broadcast") || 
         // User management commands
         c.startsWith("user approve") || c.startsWith("user deny") || 
         c.startsWith("user promote") || c.startsWith("user demote") || 
         c.startsWith("user delete") || c.startsWith("user list") ||
         // Session management commands  
         c.startsWith("session list") || c.startsWith("session revoke") ||
         c.startsWith("pending list") ||
         // Debug commands
         c.startsWith("debugcommandflow") || c.startsWith("debugusers") ||
         // Test commands (temporary)
         c.startsWith("testencryption") || c.startsWith("testpassword") ||
         // Download commands
         c.startsWith("downloadautomation") ||
         // CPU frequency (safety critical)
         c.startsWith("cpufreq ");
}

static String redactCmdForAudit(const String& cmd) {
  // Simple redaction: mask values for known sensitive keys
  String c = cmd;
  String cl = c;
  cl.toLowerCase();
  // Explicitly redact wifiadd's password argument (3rd token)
  if (cl.startsWith("wifiadd ")) {
    // Tokenize by spaces (SSID cannot contain spaces with current parser)
    int sp1 = c.indexOf(' ');
    if (sp1 > 0) {
      int sp2 = c.indexOf(' ', sp1 + 1);
      if (sp2 > 0) {
        int sp3 = c.indexOf(' ', sp2 + 1);
        // c = "wifiadd <ssid> <pass> [rest...]" -> mask <pass>
        String head = c.substring(0, sp2 + 1);
        String tail = (sp3 > 0) ? c.substring(sp3) : String();
        c = head + "***" + tail;
        return c;
      }
    }
  }
  // Redact user request secrets: mask everything after <username>
  // Syntax: "user request <username> <password> [confirm]"
  if (cl.startsWith("user request ")) {
    int sp1 = c.indexOf(' ');  // after "user"
    if (sp1 > 0) {
      int sp2 = c.indexOf(' ', sp1 + 1);  // after "request"
      if (sp2 > 0) {
        int sp3 = c.indexOf(' ', sp2 + 1);  // after <username>
        if (sp3 > 0) {
          String head = c.substring(0, sp3 + 1);
          c = head + "***";  // mask password and any optional confirm
          return c;
        }
      }
    }
  }
  return c;
}

static bool executeCommand(AuthContext& ctx, const String& cmd, String& out) {
  // Set execution context for downstream checks (used by handlers)
  gExecFromWeb = (ctx.transport == AUTH_HTTP);
  gExecUser = ctx.user;
  gExecIsAdmin = isAdminUser(ctx.user);
  DEBUG_CMD_FLOWF("[execCmd] user=%s ip=%s path=%s cmd=%s", ctx.user.c_str(), ctx.ip.c_str(), ctx.path.c_str(), redactCmdForAudit(cmd).c_str());

  // Admin-only protection for user-origin calls; allow system-origin through
  if (isAdminOnlyCommand(cmd) && !hasAdminPrivilege(ctx)) {
    // Extract command name for better error message
    String cmdName = cmd;
    int spacePos = cmd.indexOf(' ');
    if (spacePos > 0) {
      cmdName = cmd.substring(0, spacePos);
    }
    out = "Error: Admin access required for command '" + cmdName + "'. Contact an administrator.";
    logAuthAttempt(false, ctx.path.c_str(), ctx.user, ctx.ip, String("cmd=") + redactCmdForAudit(cmd));
    return false;
  }

  // Log command execution if automation logging is active
  if (gAutoLogActive && gInAutomationContext) {
    String cmdMsg = cmd;
    if (gAutoLogAutomationName.length() > 0) {
      cmdMsg = "[" + gAutoLogAutomationName + "] " + cmd;
    }
    appendAutoLogEntry("COMMAND", cmdMsg);
  }

  // Execute the command via the existing processor
  out = processCommand(cmd);

  // Log command output if automation logging is active
  if (gAutoLogActive && gInAutomationContext) {
    // Truncate very long outputs for readability
    String logOutput = out;
    if (logOutput.length() > 200) {
      logOutput = logOutput.substring(0, 197) + "...";
    }
    // Replace newlines with spaces for single-line log format
    logOutput.replace("\n", " ");
    logOutput.replace("\r", " ");
    appendAutoLogEntry("OUTPUT", logOutput);
  }

  // We don't have structured success/failure from processCommand; assume success for audit purposes
  logAuthAttempt(true, ctx.path.c_str(), ctx.user, ctx.ip, String("cmd=") + redactCmdForAudit(cmd));
  DEBUG_CMD_FLOWF("[execCmd] out_len=%d", out.length());
  return true;
}

// Registration submit: use unified CLI command for consistency
esp_err_t handleRegisterSubmit(httpd_req_t* req) {
  // Parse form fields (reuse login parsing pattern)
  String body;
  int total_len = req->content_len;
  if (total_len > 0) {
    std::unique_ptr<char, void (*)(void*)> buf((char*)ps_alloc(total_len + 1, AllocPref::PreferPSRAM, "http.reg.post"), free);
    if (buf) {
      int received = 0;
      while (received < total_len) {
        int ret = httpd_req_recv(req, buf.get() + received, total_len - received);
        if (ret <= 0) break;
        received += ret;
      }
      buf.get()[received] = '\0';
      body = String(buf.get());
    }
  }
  String username = urlDecode(extractFormField(body, "username"));
  String password = extractFormField(body, "password");
  String confirmPassword = extractFormField(body, "confirm_password");

  if (username.length() == 0 || password.length() == 0 || confirmPassword.length() == 0) {
    String inner = "<div style='text-align:center;padding:2rem'>";
    inner += "<h2 style='color:#dc3545'>Registration Failed</h2>";
    inner += "<p>All fields are required.</p>";
    inner += "<p><a class='menu-item' href='/register'>Try Again</a></p>";
    inner += "</div>";
    String page = htmlPublicShellWithNav(inner);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, page.c_str(), HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }

  if (password != confirmPassword) {
    String inner = "<div style='text-align:center;padding:2rem'>";
    inner += "<h2 style='color:#dc3545'>Registration Failed</h2>";
    inner += "<p>Passwords do not match. Please try again.</p>";
    inner += "<p><a class='menu-item' href='/register'>Try Again</a></p>";
    inner += "</div>";
    String page = htmlPublicShellWithNav(inner);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, page.c_str(), HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }

  // Execute the built-in command via unified pipeline so it is logged/audited
  AuthContext ctx;
  ctx.transport = AUTH_HTTP;
  ctx.opaque = req;
  ctx.path = "/register/submit";
  getClientIP(req, ctx.ip);
  String cmdline = String("user request ") + username + " " + password + " " + confirmPassword;
  String out;
  bool ok = executeUnifiedWebCommand(req, ctx, cmdline, out);
  // Some commands always return true; also check textual success marker
  ok = ok || (out.indexOf("Request submitted for") >= 0);
  String inner = "<div style='text-align:center;padding:2rem'>";
  if (ok) {
    inner += "<h2 style='color:#28a745'>Request Submitted</h2>";
    inner += "<div style='background:#d4edda;border:1px solid #c3e6cb;border-radius:8px;padding:1.5rem;margin:1rem 0'>";
    inner += "<p style='color:#155724;margin-bottom:1rem'>Your account request has been submitted successfully!</p>";
    inner += "<p style='color:#155724;font-size:0.9rem'>An administrator will review your request and approve access to the system.</p>";
    inner += "</div>";
    inner += "<p><a class='menu-item' href='/login'>Return to Sign In</a></p>";
  } else {
    inner += "<h2 style='color:#dc3545'>Registration Failed</h2>";
    inner += String("<p>") + (out.length() ? out.c_str() : "An error occurred.") + "</p>";
    inner += "<p><a class='menu-item' href='/register'>Try Again</a></p>";
  }
  inner += "</div>";
  String page = htmlPublicShellWithNav(inner);
  httpd_resp_set_type(req, "text/html");
  httpd_resp_send(req, page.c_str(), HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

// ---- Unified Command Context & Output Routing (scaffold) ----
enum CommandOrigin { ORIGIN_SERIAL,
                     ORIGIN_WEB,
                     ORIGIN_AUTOMATION,
                     ORIGIN_SYSTEM };
// Note: avoid name collision with existing OUTPUT_* macros used for device output flags
enum CmdOutputMask { CMD_OUT_SERIAL = 1 << 0,
                     CMD_OUT_WEB = 1 << 1,
                     CMD_OUT_LOG = 1 << 2,
                     CMD_OUT_BROADCAST = 1 << 3 };
struct CommandContext {
  CommandOrigin origin;
  AuthContext auth;
  uint32_t id;
  uint32_t timestampMs;
  uint32_t outputMask;
  bool validateOnly;
  void* replyHandle;     // placeholder for future sync replies
  httpd_req_t* httpReq;  // used by web origin if needed
};
struct Command {
  String line;
  CommandContext ctx;
};

// -------- Command Executor Task (definition) --------
struct ExecReq {
  String line;               // Command string
  CommandContext ctx;        // Full execution context
  String out;                // Result from executeCommand()
  SemaphoreHandle_t done;    // Signals completion
  bool ok;                   // Success flag from executeCommand()
};

// Now that ExecReq is fully defined we can implement the task
static void commandExecTask(void* pv) {
  DEBUG_CMD_FLOWF("[cmd_exec] task started");
  for (;;) {
    ExecReq* r;
    DEBUG_CMD_FLOWF("[cmd_exec] waiting for command...");
    if (xQueueReceive(gCmdExecQ, &r, portMAX_DELAY) == pdTRUE) {
      DEBUG_CMD_FLOWF("[cmd_exec] received cmd='%s'", r->line.c_str());
      setCurrentCommandContext(r->ctx);
      bool prevValidate = gCLIValidateOnly;
      gCLIValidateOnly = r->ctx.validateOnly;
      r->ok = executeCommand((AuthContext&)r->ctx.auth, r->line, r->out);
      DEBUG_CMD_FLOWF("[cmd_exec] executed ok=%d out_len=%d", r->ok ? 1 : 0, r->out.length());
      gCLIValidateOnly = prevValidate;
      xSemaphoreGive(r->done);
    } else {
      DEBUG_CMD_FLOWF("[cmd_exec] queue receive failed");
    }
  }
}
static void setCurrentCommandContext(const CommandContext& ctx) {
  // Temporary bridge to legacy globals; will be removed once handlers read from ctx
  gExecFromWeb = (ctx.auth.transport == AUTH_HTTP);
  gExecUser = ctx.auth.user;
}
static void routeOutput(const String& s, const CommandContext& ctx) {
  // Minimal router: preserve current behavior using existing helpers
  // Build origin label
  const char* source = "sys";
  switch (ctx.origin) {
    case ORIGIN_SERIAL: source = "serial"; break;
    case ORIGIN_WEB: source = "web"; break;
    case ORIGIN_AUTOMATION: source = "auto"; break;
    case ORIGIN_SYSTEM:
    default: source = "system"; break;
  }
  String prefixed = originPrefix(source, ctx.auth.user, ctx.auth.ip) + s;

  // Serial sink (respect global output flags like broadcastOutput does)
  if (ctx.outputMask & CMD_OUT_SERIAL) {
    if (gOutputFlags & OUTPUT_SERIAL) {
      printToSerial(prefixed);
    }
  }
  // Web sink: always append to web history so CLI reflects it
  if (ctx.outputMask & CMD_OUT_WEB) {
    printToWeb(prefixed);
  }
  // Log sink: append to web history with [log] prefix for visibility
  if (ctx.outputMask & CMD_OUT_LOG) {
    printToWeb(originPrefix("log", ctx.auth.user, ctx.auth.ip) + s);
  }
  DEBUG_CMD_FLOWF("[route] sinks: serial=%d web=%d log=%d len=%d", (ctx.outputMask & CMD_OUT_SERIAL) ? 1 : 0, (ctx.outputMask & CMD_OUT_WEB) ? 1 : 0, (ctx.outputMask & CMD_OUT_LOG) ? 1 : 0, s.length());
}
static bool submitAndExecuteSync(const Command& cmd, String& out) {
  // If executor queue isn't ready (very early boot) fallback to direct call
  if (gCmdExecQ == nullptr) {
    setCurrentCommandContext(cmd.ctx);
    return executeCommand((AuthContext&)cmd.ctx.auth, cmd.line, out);
  }

  // AVOID DEADLOCK: If we're already running in the executor task context,
  // execute directly instead of queuing (which would cause deadlock)
  if (cmd.ctx.origin == ORIGIN_AUTOMATION) {
    DEBUG_CMD_FLOWF("[submit] AUTOMATION - executing directly to avoid deadlock");
    setCurrentCommandContext(cmd.ctx);
    bool prevValidate = gCLIValidateOnly;
    gCLIValidateOnly = cmd.ctx.validateOnly;
    bool ok = executeCommand((AuthContext&)cmd.ctx.auth, cmd.line, out);
    gCLIValidateOnly = prevValidate;
    DEBUG_CMD_FLOWF("[submit] done ok=%d len=%d", ok ? 1 : 0, out.length());
    return ok;
  }

  // Package request
  ExecReq* r = new ExecReq();
  r->line = cmd.line;
  r->ctx = cmd.ctx;
  r->done = xSemaphoreCreateBinary();
  r->ok = false;

  DEBUG_CMD_FLOWF("[submit] origin=%d user=%s path=%s cmd=%s", (int)cmd.ctx.origin, cmd.ctx.auth.user.c_str(), cmd.ctx.auth.path.c_str(), cmd.line.c_str());

  // Enqueue and wait
  DEBUG_CMD_FLOWF("[submit] sending to queue...");
  BaseType_t queueResult = xQueueSend(gCmdExecQ, &r, portMAX_DELAY);
  DEBUG_CMD_FLOWF("[submit] queue send result=%d, waiting for completion...", queueResult);
  xSemaphoreTake(r->done, portMAX_DELAY);
  DEBUG_CMD_FLOWF("[submit] command completed");

  out = std::move(r->out);
  bool ok = r->ok;

  vSemaphoreDelete(r->done);
  delete r;

  DEBUG_CMD_FLOWF("[submit] done ok=%d len=%d", ok ? 1 : 0, out.length());
  return ok;
}

// Convenience wrapper: execute a command with an existing context and return output
static String execCommandUnified(const CommandContext& baseCtx, const String& line) {
  DEBUG_CMD_FLOWF("[exec] enter origin=%d user=%s path=%s cmd=%s", (int)baseCtx.origin, baseCtx.auth.user.c_str(), baseCtx.auth.path.c_str(), line.c_str());
  Command c;
  c.line = line;
  c.ctx = baseCtx;
  String out;
  (void)submitAndExecuteSync(c, out);
  DEBUG_CMD_FLOWF("[exec] exit len=%d", out.length());
  return out;
}

// Helper used by schedulerTickMinute to avoid using incomplete types before definitions
static void runAutomationCommandUnified(const String& cmd) {
  // Set automation context flag to prevent SSE recursion
  gInAutomationContext = true;
  
  // Log automation command execution if logging is active
  if (gAutoLogActive) {
    String cmdMsg = cmd;
    if (gAutoLogAutomationName.length() > 0) {
      cmdMsg = "[" + gAutoLogAutomationName + "] " + cmd;
    }
    appendAutoLogEntry("AUTO_CMD", cmdMsg);
  }
  
  // Monitor stack usage
  UBaseType_t stackBefore = uxTaskGetStackHighWaterMark(NULL);
  size_t heapBefore = ESP.getFreeHeap();
  
  DEBUGF(DEBUG_CLI | DEBUG_AUTOMATIONS, "[auto] ENTER cmd='%s' stack=%u heap=%u", cmd.c_str(), stackBefore, heapBefore);
  
  AuthContext actx;
  // SECURITY: Automations should run with the privileges of the user who triggered them,
  // NOT with system privileges. This prevents privilege escalation attacks.
  if (gExecUser.length() > 0) {
    // Manual run: use the actual user's privileges
    actx.transport = gExecFromWeb ? AUTH_HTTP : AUTH_SERIAL;
    actx.user = gExecUser;
    actx.ip = gExecFromWeb ? String("automation-web") : String("automation-serial");
  } else {
    // Scheduled run: use system privileges only for scheduler-triggered automations
    actx.transport = AUTH_SYSTEM;
    actx.user = String("system");
    actx.ip = String("automation-scheduled");
  }
  actx.path = "/automation/schedule";
  
  UBaseType_t stackAfterSetup = uxTaskGetStackHighWaterMark(NULL);
  DEBUGF(DEBUG_CLI | DEBUG_AUTOMATIONS, "[auto] SETUP stack=%u", stackAfterSetup);
  
  Command uc;
  uc.line = cmd;
  uc.ctx.origin = ORIGIN_AUTOMATION;
  uc.ctx.auth = actx;
  uc.ctx.id = (uint32_t)millis();
  uc.ctx.timestampMs = (uint32_t)millis();
  uc.ctx.outputMask = CMD_OUT_SERIAL | CMD_OUT_WEB; // Enable both serial and web output
  uc.ctx.validateOnly = false;
  uc.ctx.replyHandle = nullptr;
  uc.ctx.httpReq = nullptr;
  
  UBaseType_t stackBeforeSubmit = uxTaskGetStackHighWaterMark(NULL);
  DEBUGF(DEBUG_CLI | DEBUG_AUTOMATIONS, "[auto] BEFORE_SUBMIT stack=%u", stackBeforeSubmit);
  
  DEBUG_CMD_FLOWF("[auto] dispatch cmd='%s'", cmd.c_str());
  
  // Add recursion tracking
  static int automationCallDepth = 0;
  automationCallDepth++;
  DEBUGF(DEBUG_CLI | DEBUG_AUTOMATIONS, "[auto] RECURSION_DEPTH=%d", automationCallDepth);
  
  if (automationCallDepth > 3) {
    DEBUGF(DEBUG_CLI | DEBUG_AUTOMATIONS, "[auto] RECURSION_LIMIT_HIT depth=%d, aborting", automationCallDepth);
    automationCallDepth--;
    gInAutomationContext = false;
    return;
  }
  
  String out;
  (void)submitAndExecuteSync(uc, out);
  
  automationCallDepth--;
  
  UBaseType_t stackAfterSubmit = uxTaskGetStackHighWaterMark(NULL);
  size_t heapAfter = ESP.getFreeHeap();
  DEBUGF(DEBUG_CLI | DEBUG_AUTOMATIONS, "[auto] AFTER_SUBMIT stack=%u heap=%u", stackAfterSubmit, heapAfter);
  
  routeOutput(out, uc.ctx);
  
  UBaseType_t stackAfterRoute = uxTaskGetStackHighWaterMark(NULL);
  DEBUGF(DEBUG_CLI | DEBUG_AUTOMATIONS, "[auto] EXIT stack=%u", stackAfterRoute);
  
  // Clear automation context flag
  gInAutomationContext = false;
}

// Helper: run a command as SYSTEM origin with logging (used during first-time setup)
static void runUnifiedSystemCommand(const String& cmd) {
  AuthContext actx;
  actx.transport = AUTH_SYSTEM;
  actx.user = "system";
  actx.ip = String();
  actx.path = "/system";
  actx.opaque = nullptr;
  Command uc;
  uc.line = cmd;
  uc.ctx.origin = ORIGIN_SYSTEM;
  uc.ctx.auth = actx;
  uc.ctx.id = (uint32_t)millis();
  uc.ctx.timestampMs = (uint32_t)millis();
  uc.ctx.outputMask = CMD_OUT_LOG;
  uc.ctx.validateOnly = false;
  uc.ctx.replyHandle = nullptr;
  uc.ctx.httpReq = nullptr;
  String out;
  (void)submitAndExecuteSync(uc, out);
  routeOutput(out, uc.ctx);
}

// Helper used by web settings and other web endpoints to run a CLI-equivalent through unified path
static bool executeUnifiedWebCommand(httpd_req_t* req, AuthContext& ctx, const String& cmd, String& out) {
  Command uc;
  uc.line = cmd;
  uc.ctx.origin = ORIGIN_WEB;
  uc.ctx.auth = ctx;
  uc.ctx.id = (uint32_t)millis();
  uc.ctx.timestampMs = (uint32_t)millis();
  uc.ctx.outputMask = CMD_OUT_WEB | CMD_OUT_LOG;
  uc.ctx.validateOnly = false;
  uc.ctx.replyHandle = nullptr;
  uc.ctx.httpReq = req;
  bool ok = submitAndExecuteSync(uc, out);
  routeOutput(out, uc.ctx);
  return ok;
}

esp_err_t handleCLICommand(httpd_req_t* req) {
  AuthContext ctx;
  ctx.transport = AUTH_HTTP;
  ctx.opaque = req;
  ctx.path = "/api/cli";
  getClientIP(req, ctx.ip);
  DEBUG_CMD_FLOWF("[web.cli] enter ip=%s content_len=%d", ctx.ip.c_str(), (int)req->content_len);
  if (!tgRequireAuth(ctx)) {
    // Security log: unauthorized CLI attempt
    logAuthAttempt(false, "/api/cli", String(), ctx.ip, "unauthorized");
    return ESP_OK;  // 401 already sent
  }
  // Read x-www-form-urlencoded body
  std::unique_ptr<char, void (*)(void*)> buf((char*)ps_alloc(req->content_len + 1, AllocPref::PreferPSRAM, "http.cli.exec"), free);
  int received = 0;
  String body;
  if (req->content_len > 0) {
    while (received < (int)req->content_len) {
      int ret = httpd_req_recv(req, buf.get() + received, req->content_len - received);
      if (ret <= 0) break;
      received += ret;
    }
    buf.get()[received] = '\0';
    body = String(buf.get());
  }
  String cmd = urlDecode(extractFormField(body, "cmd"));
  String validateStr = extractFormField(body, "validate");
  bool doValidate = (validateStr == "1" || validateStr == "true");
  DEBUG_CMD_FLOWF("[web.cli] authed user=%s cmd='%s' validate=%d", ctx.user.c_str(), cmd.c_str(), doValidate ? 1 : 0);

  // Record the command in the unified feed (skip if validation-only), then execute centrally
  if (!doValidate) {
    appendCommandToFeed("web", cmd, ctx.user, ctx.ip);
  }

  // Determine originating session index and set skip for SSE broadcast during this command
  String sidForCmd = getCookieSID(req);
  int originIdx = findSessionIndexBySID(sidForCmd);
  int prevSkip = gBroadcastSkipSessionIdx;
  gBroadcastSkipSessionIdx = originIdx;
  DEBUG_SSEF("CLI origin session idx=%d, sid=%s; will skip flagging this session on broadcast", originIdx, (sidForCmd.length() ? (sidForCmd.substring(0, 8) + "...").c_str() : "<none>"));
  DEBUG_CMD_FLOWF("[web.cli] build ctx user=%s originIdx=%d", ctx.user.c_str(), originIdx);

  // Build unified command and execute synchronously (queue to be added later)
  Command uc;
  uc.line = cmd;
  uc.ctx.origin = ORIGIN_WEB;
  uc.ctx.auth = ctx;
  uc.ctx.id = (uint32_t)millis();
  uc.ctx.timestampMs = (uint32_t)millis();
  uc.ctx.outputMask = CMD_OUT_WEB | CMD_OUT_LOG;
  uc.ctx.validateOnly = doValidate;
  uc.ctx.replyHandle = nullptr;
  uc.ctx.httpReq = req;

  String out;
  bool ok = submitAndExecuteSync(uc, out);
  DEBUG_CMD_FLOWF("[web.cli] executed ok=%d out_len=%d", ok ? 1 : 0, out.length());
  DEBUG_CLIF("Command result: %s", out.c_str());
  if (!doValidate) {
    // Route output to configured sinks (web + log). HTTP response is sent below.
    DEBUG_CMD_FLOWF("[web.cli] routing output len=%d", out.length());
    routeOutput(out, uc.ctx);
  }

  // Restore skip index after broadcast side-effects complete
  gBroadcastSkipSessionIdx = prevSkip;
  DEBUG_SSEF("Restored gBroadcastSkipSessionIdx to %d", prevSkip);

  // For web CLI, return the output directly for immediate display
  httpd_resp_set_type(req, "text/plain");
  httpd_resp_send(req, out.c_str(), HTTPD_RESP_USE_STRLEN);
  DEBUG_CMD_FLOWF("[web.cli] exit");
  return ESP_OK;
}

void streamCLIContent(httpd_req_t* req) {
  String u;
  isAuthed(req, u);
  String content = getCLIPage(u);
  streamContentGeneric(req, content);
}

esp_err_t handleCLIPage(httpd_req_t* req) {
  AuthContext ctx;
  ctx.transport = AUTH_HTTP;
  ctx.opaque = req;
  ctx.path = req->uri ? req->uri : "/cli";
  getClientIP(req, ctx.ip);
  if (!tgRequireAuth(ctx)) return ESP_OK;

  logAuthAttempt(true, req->uri, ctx.user, ctx.ip, "");

  streamPageWithContent(req, "cli", ctx.user, streamCLIContent);
  return ESP_OK;
}

void streamFilesContent(httpd_req_t* req) {
  String u;
  isAuthed(req, u);
  String content = getFilesPage(u);
  streamContentGeneric(req, content);
}

// Automations page streamer
void streamAutomationsContent(httpd_req_t* req) {
  String u;
  isAuthed(req, u);
  String content = getAutomationsPage(u);
  streamContentGeneric(req, content);
}

// Automations page handler (authenticated for all users)
esp_err_t handleAutomationsPage(httpd_req_t* req) {
  AuthContext ctx;
  ctx.transport = AUTH_HTTP;
  ctx.opaque = req;
  ctx.path = req->uri ? req->uri : "/automations";
  getClientIP(req, ctx.ip);
  if (!tgRequireAuth(ctx)) return ESP_OK;
  logAuthAttempt(true, req->uri, ctx.user, ctx.ip, "");

  streamPageWithContent(req, "automations", ctx.user, streamAutomationsContent);
  return ESP_OK;
}

// GET /api/automations: return raw automations.json
esp_err_t handleAutomationsGet(httpd_req_t* req) {
  AuthContext ctx;
  ctx.transport = AUTH_HTTP;
  ctx.opaque = req;
  ctx.path = "/api/automations";
  getClientIP(req, ctx.ip);
  if (!tgRequireAuth(ctx)) return ESP_OK;  // any authed user may read

  httpd_resp_set_type(req, "application/json");
  String json;
  if (!readText(AUTOMATIONS_JSON_FILE, json)) {
    httpd_resp_send(req, "{\"success\":false,\"error\":\"Failed to read automations.json\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }
  // Sanitize duplicate IDs if any and persist back
  if (sanitizeAutomationsJson(json)) {
    writeAutomationsJsonAtomic(json);  // best-effort writeback
  }
  httpd_resp_send(req, json.c_str(), HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

// Extract array contents by key: finds key then returns substring between matching [ ... ]
static bool extractArrayByKey(const String& src, const char* key, String& outArray) {
  String k = String("\"") + key + "\"";
  int keyPos = src.indexOf(k);
  if (keyPos < 0) return false;
  int colon = src.indexOf(':', keyPos + k.length());
  if (colon < 0) return false;
  int lb = src.indexOf('[', colon);
  if (lb < 0) return false;
  // Find matching closing bracket (simple depth counter)
  int depth = 0;
  for (int i = lb; i < (int)src.length(); ++i) {
    char c = src[i];
    if (c == '[') depth++;
    else if (c == ']') {
      depth--;
      if (depth == 0) {
        outArray = src.substring(lb + 1, i);  // contents without outer brackets
        return true;
      }
    }
  }
  return false;
}

// Extract individual items from array string, updating pos to next item
static bool extractArrayItem(const String& arrayStr, int& pos, String& outItem) {
  // Skip whitespace and commas
  while (pos < arrayStr.length() && (arrayStr[pos] == ' ' || arrayStr[pos] == '\t' || arrayStr[pos] == '\n' || arrayStr[pos] == ',')) {
    pos++;
  }
  if (pos >= arrayStr.length()) return false;
  
  if (arrayStr[pos] == '{') {
    // Extract object
    int depth = 0;
    int start = pos;
    for (int i = pos; i < arrayStr.length(); ++i) {
      char c = arrayStr[i];
      if (c == '{') depth++;
      else if (c == '}') {
        depth--;
        if (depth == 0) {
          outItem = arrayStr.substring(start, i + 1);
          pos = i + 1;
          return true;
        }
      }
    }
  }
  return false;
}

// GET /api/automations/export: export automations for download
esp_err_t handleAutomationsExport(httpd_req_t* req) {
  AuthContext ctx;
  ctx.transport = AUTH_HTTP;
  ctx.opaque = req;
  ctx.path = "/api/automations/export";
  getClientIP(req, ctx.ip);
  if (!tgRequireAuth(ctx)) return ESP_OK;  // any authed user may export

  // Parse query parameters
  char query[512] = {0};
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
    char idParam[32] = {0};
    if (httpd_query_key_value(query, "id", idParam, sizeof(idParam)) == ESP_OK) {
      // Export single automation
      String json;
      if (!readText(AUTOMATIONS_JSON_FILE, json)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read automations");
        return ESP_OK;
      }
      
      // Find specific automation using string parsing
      int targetId = atoi(idParam);
      String automationsArray;
      if (!extractArrayByKey(json, "automations", automationsArray)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No automations array found");
        return ESP_OK;
      }
      
      // Parse automations array to find target
      String targetAuto;
      int pos = 0;
      while (pos < automationsArray.length()) {
        String item;
        if (!extractArrayItem(automationsArray, pos, item)) break;
        
        // Check if this automation has the target ID
        int autoId = 0;
        if (parseJsonInt(item, "id", autoId) && autoId == targetId) {
          targetAuto = item;
          break;
        }
      }
      
      if (targetAuto.length() == 0) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Automation not found");
        return ESP_OK;
      }
      
      // Generate filename from automation name
      String name;
      if (!parseJsonString(targetAuto, "name", name) || name.length() == 0) {
        name = "automation";
      }
      // Sanitize filename
      name.replace(" ", "_");
      name.replace("/", "_");
      name.replace("\\", "_");
      String filename = name + ".json";
      
      // Set download headers
      httpd_resp_set_type(req, "application/json");
      httpd_resp_set_hdr(req, "Content-Disposition", ("attachment; filename=\"" + filename + "\"").c_str());
      
      // Send single automation JSON (targetAuto already includes braces)
      httpd_resp_send(req, targetAuto.c_str(), HTTPD_RESP_USE_STRLEN);
      return ESP_OK;
    }
  }
  
  // Export all automations (bulk export)
  String json;
  if (!readText(AUTOMATIONS_JSON_FILE, json)) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read automations");
    return ESP_OK;
  }
  
  // Generate timestamp for filename
  time_t now;
  time(&now);
  struct tm* timeinfo = localtime(&now);
  char timestamp[32];
  strftime(timestamp, sizeof(timestamp), "%Y-%m-%d_%H-%M-%S", timeinfo);
  String filename = String("automations-backup-") + timestamp + ".json";
  
  // Set download headers
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Content-Disposition", ("attachment; filename=\"" + filename + "\"").c_str());
  
  httpd_resp_send(req, json.c_str(), HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

// Removed duplicate - using the existing streamFilesContent above

// Persistent buffers for file viewing (prefer PSRAM, fallback to heap)
static char* gFileReadBuf = nullptr;
static char* gFileOutBuf = nullptr;
static const size_t kFileReadBufSize = 2048;
static const size_t kFileOutBufSize = 2048;

static bool ensureFileViewBuffers() {
  if (!gFileReadBuf) {
    gFileReadBuf = (char*)ps_alloc(kFileReadBufSize, AllocPref::PreferPSRAM, "http.file.read");
    if (!gFileReadBuf) gFileReadBuf = (char*)malloc(kFileReadBufSize);
  }
  if (!gFileOutBuf) {
    gFileOutBuf = (char*)ps_alloc(kFileOutBufSize, AllocPref::PreferPSRAM, "http.file.out");
    if (!gFileOutBuf) gFileOutBuf = (char*)malloc(kFileOutBufSize);
  }
  return gFileReadBuf && gFileOutBuf;
}

esp_err_t handleFilesPage(httpd_req_t* req) {
  AuthContext ctx;
  ctx.transport = AUTH_HTTP;
  ctx.opaque = req;
  ctx.path = req->uri ? req->uri : "/files";
  getClientIP(req, ctx.ip);
  if (!tgRequireAuth(ctx)) return ESP_OK;
  logAuthAttempt(true, req->uri, ctx.user, ctx.ip, "");

  streamPageWithContent(req, "files", ctx.user, streamFilesContent);
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
      out = "";  // caller will wrap error
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
    out = "";  // array body only
  }

  File file = root.openNextFile();
  while (file) {
    // Extract display name (strip leading directory)
    String fileName = String(file.name());
    if (dirPath != "/") {
      String expectedPrefix = dirPath;
      if (!expectedPrefix.endsWith("/")) expectedPrefix += "/";
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
        String subPath = dirPath;
        if (!subPath.endsWith("/")) subPath += "/";
        subPath += fileName;
        int itemCount = 0;
        File subDir = LittleFS.open(subPath);
        if (subDir && subDir.isDirectory()) {
          File child = subDir.openNextFile();
          while (child) {
            itemCount++;
            child = subDir.openNextFile();
          }
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
        String subPath = dirPath;
        if (!subPath.endsWith("/")) subPath += "/";
        subPath += fileName;
        int itemCount = 0;
        File subDir = LittleFS.open(subPath);
        if (subDir && subDir.isDirectory()) {
          File child = subDir.openNextFile();
          while (child) {
            itemCount++;
            child = subDir.openNextFile();
          }
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
  AuthContext ctx;
  ctx.transport = AUTH_HTTP;
  ctx.opaque = req;
  ctx.path = "/api/files/list";
  getClientIP(req, ctx.ip);
  if (!tgRequireAuth(ctx)) return ESP_OK;
  logAuthAttempt(true, req->uri, ctx.user, ctx.ip, "");

  // Check if filesystem is ready
  if (!filesystemReady) {
    broadcastOutput("[files] ERROR: Filesystem not ready");
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
      broadcastOutput(String("[files] Listing directory: ") + dirPath);
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
  AuthContext ctx;
  ctx.transport = AUTH_HTTP;
  ctx.opaque = req;
  ctx.path = "/api/files/create";
  getClientIP(req, ctx.ip);
  if (!tgRequireAuth(ctx)) return ESP_OK;
  logAuthAttempt(true, req->uri, ctx.user, ctx.ip, "");

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
    // Use CLI command for consistent validation and error handling
    String cmd = "mkdir " + path;
    String result;
    bool success = executeCommand(ctx, cmd, result);
    
    httpd_resp_set_type(req, "application/json");
    if (success && result.startsWith("Created folder:")) {
      httpd_resp_send(req, "{\"success\":true}", HTTPD_RESP_USE_STRLEN);
    } else {
      // Extract error message and return as JSON
      String errorMsg = success ? result : result;
      errorMsg.replace("\"", "\\\""); // Escape quotes for JSON
      String jsonResponse = "{\"success\":false,\"error\":\"" + errorMsg + "\"}";
      httpd_resp_send(req, jsonResponse.c_str(), HTTPD_RESP_USE_STRLEN);
    }
  } else {
    // Normalize extension in path
    if (!name.endsWith("." + type)) {
      path = "/" + name + "." + type;
    }
    // Route through unified executor using CLI-equivalent command
    String cmd = String("filecreate ") + path;
    String out;
    bool ok = executeUnifiedWebCommand(req, ctx, cmd, out);
    httpd_resp_set_type(req, "application/json");
    if (ok) {
      httpd_resp_send(req, "{\"success\":true}", HTTPD_RESP_USE_STRLEN);
    } else {
      String resp = String("{\"success\":false,\"error\":\"") + out + String("\"}");
      httpd_resp_send(req, resp.c_str(), HTTPD_RESP_USE_STRLEN);
    }
  }

  return ESP_OK;
}

esp_err_t handleFileView(httpd_req_t* req) {
  AuthContext ctx;
  ctx.transport = AUTH_HTTP;
  ctx.opaque = req;
  ctx.path = "/api/files/view";
  getClientIP(req, ctx.ip);
  if (!tgRequireAuth(ctx)) return ESP_OK;
  logAuthAttempt(true, req->uri, ctx.user, ctx.ip, "");

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

  broadcastOutput(String("[files] Viewing file: ") + path);

  File file = LittleFS.open(path, "r");
  if (!file) {
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, "File not found", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }

  // Ensure persistent buffers are ready
  if (!ensureFileViewBuffers()) {
    file.close();
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, "Memory allocation failed", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }

  // Set content type and handle JSON formatting
  String filename = String(name);
  // URL decode the filename for display
  String displayName = filename;
  displayName.replace("%2F", "/");
  displayName.replace("%20", " ");
  bool isJson = filename.endsWith(".json");

  // Always prefer inline rendering in the browser, never force download
  String dispo = String("inline; filename=\"") + displayName + "\"";
  httpd_resp_set_hdr(req, "Content-Disposition", dispo.c_str());

  if (isJson) {
    // Determine view mode: pretty (default) or raw
    char mode[16];
    bool raw = false;
    if (httpd_query_key_value(query, "mode", mode, sizeof(mode)) == ESP_OK) {
      if (strcmp(mode, "raw") == 0) raw = true;
    }
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    // Send HTML wrapper for formatted JSON display
    httpd_resp_send_chunk(req, "<!DOCTYPE html><html><head><title>", HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, filename.c_str(), filename.length());
    httpd_resp_send_chunk(req, "</title><style>body{font-family:monospace;margin:20px;background:#f5f5f5;font-size:14px;}pre{background:white;padding:15px;border-radius:5px;border:1px solid #ddd;overflow-x:auto;font-size:14px;line-height:1.4;} .bar{margin:8px 0 12px 0} .btn{display:inline-block;padding:4px 8px;border:1px solid #ccc;border-radius:4px;background:#fff;color:#000;text-decoration:none;margin-right:6px} .btn.active{background:#e9ecef;}</style></head><body><h2>", HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, displayName.c_str(), displayName.length());
    // Toggle bar
    String base = String("/api/files/view?name=") + filename;
    String prettyHref = base + "&mode=pretty";
    String rawHref = base + "&mode=raw";
    httpd_resp_send_chunk(req, "</h2><div class='bar'>", HTTPD_RESP_USE_STRLEN);
    if (raw) {
      httpd_resp_send_chunk(req, "<a class='btn' href='", HTTPD_RESP_USE_STRLEN);
      httpd_resp_send_chunk(req, prettyHref.c_str(), prettyHref.length());
      httpd_resp_send_chunk(req, "'>Pretty</a>", HTTPD_RESP_USE_STRLEN);
      httpd_resp_send_chunk(req, "<span class='btn active'>Raw</span>", HTTPD_RESP_USE_STRLEN);
    } else {
      httpd_resp_send_chunk(req, "<span class='btn active'>Pretty</span>", HTTPD_RESP_USE_STRLEN);
      httpd_resp_send_chunk(req, "<a class='btn' href='", HTTPD_RESP_USE_STRLEN);
      httpd_resp_send_chunk(req, rawHref.c_str(), rawHref.length());
      httpd_resp_send_chunk(req, "'>Raw</a>", HTTPD_RESP_USE_STRLEN);
    }
    httpd_resp_send_chunk(req, "</div><pre>", HTTPD_RESP_USE_STRLEN);
    // Stream pretty-printed JSON or raw JSON without large Strings
    if (raw) {
      while (file.available()) {
        int bytesRead = file.readBytes(gFileReadBuf, kFileReadBufSize);
        if (bytesRead > 0) httpd_resp_send_chunk(req, gFileReadBuf, bytesRead);
      }
      file.close();
    } else {
      // Pretty mode
      int indent = 0;
      bool inString = false;
      bool escaped = false;
      size_t outLen = 0;
      auto flushOut = [&](bool force) {
        if (outLen && (force || outLen > (kFileOutBufSize - 64))) {
          httpd_resp_send_chunk(req, gFileOutBuf, outLen);
          outLen = 0;
        }
      };
      // Safe emit helpers to avoid buffer overflow
      auto emit = [&](char ch) {
        if (outLen >= kFileOutBufSize - 1) flushOut(false);
        gFileOutBuf[outLen++] = ch;
      };
      auto emitIndent = [&]() {
        int spaces = indent * 2;
        for (int j = 0; j < spaces; j++) {
          if (outLen >= kFileOutBufSize - 1) flushOut(false);
          gFileOutBuf[outLen++] = ' ';
        }
      };
      while (file.available()) {
        int bytesRead = file.readBytes(gFileReadBuf, kFileReadBufSize);
        for (int i = 0; i < bytesRead; i++) {
          char c = gFileReadBuf[i];
          if (!inString) {
            if (c == '"' && !escaped) {
              inString = true;
              emit(c);
            } else if (c == '{' || c == '[') {
              emit(c);
              emit('\n');
              indent++;
              emitIndent();
            } else if (c == '}' || c == ']') {
              emit('\n');
              if (indent > 0) indent--;
              emitIndent();
              emit(c);
            } else if (c == ',') {
              emit(c);
              emit('\n');
              emitIndent();
            } else if (c == ':') {
              emit(c);
              emit(' ');
            } else if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
              emit(c);
            }
          } else {
            emit(c);
            if (c == '"' && !escaped) { inString = false; }
          }
          // Track escape state
          escaped = (c == '\\' && !escaped);
          if (outLen >= kFileOutBufSize - 4) flushOut(false);
        }
        flushOut(false);
      }
      file.close();
      flushOut(true);
    }
    httpd_resp_send_chunk(req, "</pre></body></html>", HTTPD_RESP_USE_STRLEN);
  } else if (filename.endsWith(".txt")) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    // Send HTML wrapper for text file display
    httpd_resp_send_chunk(req, "<!DOCTYPE html><html><head><title>", HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, filename.c_str(), filename.length());
    httpd_resp_send_chunk(req, "</title><style>body{font-family:monospace;margin:20px;background:#f5f5f5;font-size:14px;}pre{background:white;padding:15px;border-radius:5px;border:1px solid #ddd;overflow-x:auto;font-size:14px;line-height:1.4;white-space:pre-wrap;word-wrap:break-word;}</style></head><body><h2>", HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, displayName.c_str(), displayName.length());
    httpd_resp_send_chunk(req, "</h2><pre>", HTTPD_RESP_USE_STRLEN);
    while (file.available()) {
      int bytesRead = file.readBytes(gFileReadBuf, kFileReadBufSize);
      if (bytesRead > 0) httpd_resp_send_chunk(req, gFileReadBuf, bytesRead);
    }
    file.close();
    httpd_resp_send_chunk(req, "</pre></body></html>", HTTPD_RESP_USE_STRLEN);
  } else if (filename.endsWith(".csv")) {
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    while (file.available()) {
      int bytesRead = file.readBytes(gFileReadBuf, kFileReadBufSize);
      if (bytesRead > 0) httpd_resp_send_chunk(req, gFileReadBuf, bytesRead);
    }
    file.close();
  } else if (filename.endsWith(".rtf")) {
    httpd_resp_set_type(req, "application/rtf");
    while (file.available()) {
      int bytesRead = file.readBytes(gFileReadBuf, kFileReadBufSize);
      if (bytesRead > 0) httpd_resp_send_chunk(req, gFileReadBuf, bytesRead);
    }
    file.close();
  } else {
    httpd_resp_set_type(req, "text/plain");
    while (file.available()) {
      int bytesRead = file.readBytes(gFileReadBuf, kFileReadBufSize);
      if (bytesRead > 0) httpd_resp_send_chunk(req, gFileReadBuf, bytesRead);
    }
    file.close();
  }

  httpd_resp_send_chunk(req, NULL, 0);  // End chunked response
  return ESP_OK;
}



esp_err_t handleFileDelete(httpd_req_t* req) {
  AuthContext ctx;
  ctx.transport = AUTH_HTTP;
  ctx.opaque = req;
  ctx.path = "/api/files/delete";
  getClientIP(req, ctx.ip);
  if (!tgRequireAuth(ctx)) return ESP_OK;
  logAuthAttempt(true, req->uri, ctx.user, ctx.ip, "");

  // Accept name from POST body (x-www-form-urlencoded) or URL query as fallback
  String nameStr = "";
  {
    // Try to read POST body
    char buf[256];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret > 0) {
      buf[ret] = '\0';
      String body = String(buf);
      int nameStart = body.indexOf("name=");
      if (nameStart >= 0) {
        nameStart += 5;
        int nameEnd = body.indexOf("&", nameStart);
        if (nameEnd < 0) nameEnd = body.length();
        nameStr = body.substring(nameStart, nameEnd);
      }
    }
  }
  if (nameStr.length() == 0) {
    // Fallback to query parameter
    char query[256];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
      char name[128];
      if (httpd_query_key_value(query, "name", name, sizeof(name)) == ESP_OK) {
        nameStr = String(name);
      }
    }
  }
  if (nameStr.length() == 0) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"success\":false,\"error\":\"No filename specified\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }
  // URL decode minimal subset
  nameStr.replace("%2F", "/");
  nameStr.replace("%20", " ");
  // Normalize: strip leading '/' if present to prevent double slashes
  if (nameStr.startsWith("/")) {
    nameStr = nameStr.substring(1);
  }
  String path = "/" + nameStr;

  // Basic safeguards: disallow deleting critical files and anything in /logs
  if (nameStr.length() == 0 || nameStr == "." || nameStr == ".." || path == "/settings.json" || path == "/users.json" || path == "/automations.json" || path == "/devices.json" || path == "/logs" || path.startsWith("/logs/")) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"success\":false,\"error\":\"Deletion not allowed\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }

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
  AuthContext ctx;
  ctx.transport = AUTH_HTTP;
  ctx.opaque = req;
  ctx.path = "/api/admin/pending";
  getClientIP(req, ctx.ip);
  if (!tgRequireAuth(ctx)) return ESP_OK;
  logAuthAttempt(true, req->uri, ctx.user, ctx.ip, "");

  // Preserve JSON error contract for this endpoint
  if (!isAdminUser(ctx.user)) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"success\":false,\"error\":\"Admin access required\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }

  String json = "{\"success\":true,\"pending\":[]}";
  
  if (LittleFS.exists("/pending_users.json")) {
    String pendingJson;
    if (readText("/pending_users.json", pendingJson)) {
      // Extract just the array part and insert into response
      if (pendingJson.startsWith("[") && pendingJson.endsWith("]")) {
        String arrayContent = pendingJson.substring(1, pendingJson.length() - 1);
        json = "{\"success\":true,\"pending\":[" + arrayContent + "]}";
      }
    }
  }
  
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, json.c_str(), HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}


// ...
// -------------------------------
// Pending user moderation helpers
// -------------------------------
static bool approvePendingUserInternal(const String& username, String& errorOut) {
  DEBUG_USERSF("[users] approve internal username=%s", username.c_str());
  if (username.length() == 0) {
    errorOut = "Username required";
    return false;
  }
  // Load pending list and extract approved user
  String pendingJson = "[]";
  String userPassword = "";
  bool found = false;
  
  if (LittleFS.exists("/pending_users.json")) {
    if (!readText("/pending_users.json", pendingJson)) {
      errorOut = "Could not read pending list";
      return false;
    }
  }
  
  // Parse JSON and rebuild without approved user
  String newJson = "[";
  bool first = true;
  
  if (pendingJson.startsWith("[") && pendingJson.endsWith("]")) {
    String content = pendingJson.substring(1, pendingJson.length() - 1);
    int pos = 0;
    while (pos < content.length()) {
      int objStart = content.indexOf('{', pos);
      if (objStart == -1) break;
      int objEnd = content.indexOf('}', objStart);
      if (objEnd == -1) break;
      
      String obj = content.substring(objStart, objEnd + 1);
      
      // Extract username from this object
      int userStart = obj.indexOf("\"username\":\"");
      if (userStart >= 0) {
        userStart += 12; // length of "username":""
        int userEnd = obj.indexOf('"', userStart);
        if (userEnd > userStart) {
          String objUsername = obj.substring(userStart, userEnd);
          if (objUsername == username) {
            // Extract password for approved user
            int passStart = obj.indexOf("\"password\":\"");
            if (passStart >= 0) {
              passStart += 12;
              int passEnd = obj.indexOf('"', passStart);
              if (passEnd > passStart) {
                userPassword = obj.substring(passStart, passEnd);
              }
            }
            found = true;
          } else {
            // Keep this user in the list
            if (!first) newJson += ",";
            newJson += obj;
            first = false;
          }
        }
      }
      pos = objEnd + 1;
    }
  }
  newJson += "]";
  
  if (!found) {
    errorOut = "User not found in pending list";
    return false;
  }
  
  // Write updated pending list
  if (!writeText("/pending_users.json", newJson)) {
    errorOut = "Could not update pending list";
    return false;
  }
  
  // Remove file if empty
  if (newJson == "[]") {
    LittleFS.remove("/pending_users.json");
  }

  // Append approved user to users.json (JSON-only policy)
  String usersJson;
  if (!LittleFS.exists(USERS_JSON_FILE)) {
    // Create users.json with the first user (ID 1)
    usersJson = String("{\n  \"version\": 1,\n  \"nextId\": 2,\n  \"users\": [\n    {\n      \"id\": 1,\n      \"username\": \"") + username + "\",\n      \"password\": \"" + userPassword + "\",\n      \"role\": \"admin\"\n    }\n  ]\n}\n";
    if (!writeText(USERS_JSON_FILE, usersJson)) {
      errorOut = "Failed to create users.json";
      return false;
    }
  } else {
    if (!readText(USERS_JSON_FILE, usersJson)) {
      errorOut = "Failed to read users.json";
      return false;
    }
    
    // Extract nextId
    int nextIdIdx = usersJson.indexOf("\"nextId\":");
    if (nextIdIdx < 0) {
      errorOut = "Malformed users.json - missing nextId";
      return false;
    }
    int nextIdStart = nextIdIdx + 9; // length of "nextId":
    while (nextIdStart < usersJson.length() && (usersJson[nextIdStart] == ' ' || usersJson[nextIdStart] == '\t')) nextIdStart++;
    int nextIdEnd = nextIdStart;
    while (nextIdEnd < usersJson.length() && usersJson[nextIdEnd] >= '0' && usersJson[nextIdEnd] <= '9') nextIdEnd++;
    if (nextIdEnd == nextIdStart) {
      errorOut = "Malformed users.json - invalid nextId";
      return false;
    }
    int nextId = usersJson.substring(nextIdStart, nextIdEnd).toInt();
    
    int usersIdx = usersJson.indexOf("\"users\"");
    int openBracket = (usersIdx >= 0) ? usersJson.indexOf('[', usersIdx) : -1;
    int closeBracket = (openBracket >= 0) ? usersJson.indexOf(']', openBracket) : -1;
    if (openBracket < 0 || closeBracket <= openBracket) {
      errorOut = "Malformed users.json";
      return false;
    }
    // Ensure user doesn't already exist
    if (usernameExistsInUsersJson(usersJson, username)) {
      errorOut = "Username already exists";
      return false;
    }
    
    // Create new user object with ID
    String obj = String("{\n      \"id\": ") + String(nextId) + ",\n      \"username\": \"" + username + "\",\n      \"password\": \"" + userPassword + "\",\n      \"role\": \"user\"\n    }";
    String before = usersJson.substring(0, openBracket + 1);
    String middle = usersJson.substring(openBracket + 1, closeBracket);
    String after = usersJson.substring(closeBracket);
    middle.trim();
    if (middle.length() > 0) middle += ",\n";
    middle += obj;
    usersJson = before + middle + after;
    
    // Update nextId in the JSON
    String beforeNextId = usersJson.substring(0, nextIdStart);
    String afterNextId = usersJson.substring(nextIdEnd);
    usersJson = beforeNextId + String(nextId + 1) + afterNextId;
    
    if (!writeText(USERS_JSON_FILE, usersJson)) {
      errorOut = "Failed to write users.json";
      return false;
    }
  }
  broadcastOutput(String("[admin] Approved user: ") + username + " with requested password");
  return true;
}

static bool denyPendingUserInternal(const String& username, String& errorOut) {
  DEBUG_USERSF("[users] deny internal username=%s", username.c_str());
  if (username.length() == 0) {
    errorOut = "Username required";
    return false;
  }
  String pendingJson = "[]";
  bool found = false;
  
  if (LittleFS.exists("/pending_users.json")) {
    if (!readText("/pending_users.json", pendingJson)) {
      errorOut = "Could not read pending list";
      return false;
    }
  }
  
  // Parse JSON and rebuild without denied user
  String newJson = "[";
  bool first = true;
  
  if (pendingJson.startsWith("[") && pendingJson.endsWith("]")) {
    String content = pendingJson.substring(1, pendingJson.length() - 1);
    int pos = 0;
    while (pos < content.length()) {
      int objStart = content.indexOf('{', pos);
      if (objStart == -1) break;
      int objEnd = content.indexOf('}', objStart);
      if (objEnd == -1) break;
      
      String obj = content.substring(objStart, objEnd + 1);
      
      // Extract username from this object
      int userStart = obj.indexOf("\"username\":\"");
      if (userStart >= 0) {
        userStart += 12;
        int userEnd = obj.indexOf('"', userStart);
        if (userEnd > userStart) {
          String objUsername = obj.substring(userStart, userEnd);
          if (objUsername == username) {
            found = true;
          } else {
            // Keep this user in the list
            if (!first) newJson += ",";
            newJson += obj;
            first = false;
          }
        }
      }
      pos = objEnd + 1;
    }
  }
  newJson += "]";
  
  if (!found) {
    errorOut = "User not found in pending list";
    return false;
  }
  
  // Write updated pending list
  if (!writeText("/pending_users.json", newJson)) {
    errorOut = "Could not update pending list";
    return false;
  }
  
  // Remove file if empty
  if (newJson == "[]") {
    LittleFS.remove("/pending_users.json");
  }
  return true;
}

// Promote an existing user in users.json to admin (JSON-only)
static bool promoteUserToAdminInternal(const String& username, String& errorOut) {
  DEBUG_USERSF("[users] promote internal username=%s", username.c_str());
  if (username.length() == 0) { errorOut = "Username required"; return false; }
  if (!LittleFS.exists(USERS_JSON_FILE)) { errorOut = "users.json not found"; return false; }
  String json;
  if (!readText(USERS_JSON_FILE, json)) { errorOut = "Failed to read users.json"; return false; }
  int usersIdx = json.indexOf("\"users\"");
  int openBracket = (usersIdx >= 0) ? json.indexOf('[', usersIdx) : -1;
  int closeBracket = (openBracket >= 0) ? json.indexOf(']', openBracket) : -1;
  if (openBracket < 0 || closeBracket <= openBracket) { errorOut = "Malformed users.json"; return false; }

  // Find the object for this username within the users array
  int searchPos = openBracket + 1;
  bool updated = false;
  while (true) {
    int objStart = json.indexOf('{', searchPos);
    if (objStart < 0 || objStart > closeBracket) break;
    int objEnd = json.indexOf('}', objStart);
    if (objEnd < 0 || objEnd > closeBracket) break;
    String obj = json.substring(objStart, objEnd + 1);
    
    int un = obj.indexOf("\"username\": \"");
    if (un >= 0) {
      un += 13; // skip "username": "
      int unEnd = obj.indexOf('"', un);
      if (unEnd > un) {
        String name = obj.substring(un, unEnd);
        if (name == username) {
          // Check for ID field (founder protection) - only for the target user
          int idStart = obj.indexOf("\"id\": ");
          if (idStart >= 0) {
            idStart += 6; // skip "id": 
            int idEnd = idStart;
            while (idEnd < obj.length() && obj[idEnd] >= '0' && obj[idEnd] <= '9') idEnd++;
            if (idEnd > idStart) {
              int userId = obj.substring(idStart, idEnd).toInt();
              if (userId == 1) {
                errorOut = "Cannot modify the first admin account";
                return false;
              }
            }
          }
          
          // Look for role field in the full JSON
          int roleFieldStart = json.indexOf("\"role\": \"", objStart);
          if (roleFieldStart >= 0 && roleFieldStart < objEnd) {
            int roleValueStart = roleFieldStart + 9; // skip "role": "
            int roleValueEnd = json.indexOf('"', roleValueStart);
            if (roleValueEnd > roleValueStart && roleValueEnd < objEnd) {
              // Replace the entire role value with "admin"
              String before = json.substring(0, roleValueStart);
              String after = json.substring(roleValueEnd);
              json = before + String("admin") + after;
              updated = true;
              break;
            }
          } else {
            // No role field; insert before closing brace
            String ins = String(",\n      \"role\": \"admin\"");
            String before = json.substring(0, objEnd);
            String after = json.substring(objEnd);
            json = before + ins + after;
            updated = true;
            break;
          }
        }
      }
    }
    searchPos = objEnd + 1;
  }
  if (!updated) { errorOut = "User not found"; return false; }
  if (!writeText(USERS_JSON_FILE, json)) { errorOut = "Failed to write users.json"; return false; }
  broadcastOutput(String("[admin] Promoted user to admin: ") + username);
  
  // Serial admin status now checked in real-time via isAdminUser()
  if (gSerialAuthed && gSerialUser == username) {
    broadcastOutput("[serial] Your admin privileges have been updated");
  }
  
  return true;
}

// Demote an existing admin user in users.json to regular user (JSON-only)
static bool demoteUserFromAdminInternal(const String& username, String& errorOut) {
  DEBUG_USERSF("[users] demote internal username=%s", username.c_str());
  if (username.length() == 0) { errorOut = "Username required"; return false; }
  if (!LittleFS.exists(USERS_JSON_FILE)) { errorOut = "users.json not found"; return false; }
  String json;
  if (!readText(USERS_JSON_FILE, json)) { errorOut = "Failed to read users.json"; return false; }
  int usersIdx = json.indexOf("\"users\"");
  int openBracket = (usersIdx >= 0) ? json.indexOf('[', usersIdx) : -1;
  int closeBracket = (openBracket >= 0) ? json.indexOf(']', openBracket) : -1;
  if (openBracket < 0 || closeBracket <= openBracket) { errorOut = "Malformed users.json"; return false; }

  // Find the object for this username within the users array
  int searchPos = openBracket + 1;
  bool updated = false;
  while (true) {
    int objStart = json.indexOf('{', searchPos);
    if (objStart < 0 || objStart > closeBracket) break;
    int objEnd = json.indexOf('}', objStart);
    if (objEnd < 0 || objEnd > closeBracket) break;
    String obj = json.substring(objStart, objEnd + 1);
    
    int un = obj.indexOf("\"username\": \"");
    if (un >= 0) {
      un += 13; // skip "username": "
      int unEnd = obj.indexOf('"', un);
      if (unEnd > un) {
        String name = obj.substring(un, unEnd);
        if (name == username) {
          // Check for ID field (founder protection) - only for the target user
          int idStart = obj.indexOf("\"id\": ");
          if (idStart >= 0) {
            idStart += 6; // skip "id": 
            int idEnd = idStart;
            while (idEnd < obj.length() && obj[idEnd] >= '0' && obj[idEnd] <= '9') idEnd++;
            if (idEnd > idStart) {
              int userId = obj.substring(idStart, idEnd).toInt();
              if (userId == 1) {
                errorOut = "Cannot modify the first admin account";
                return false;
              }
            }
          }
          
          // Look for role field in the full JSON
          int roleFieldStart = json.indexOf("\"role\": \"", objStart);
          if (roleFieldStart >= 0 && roleFieldStart < objEnd) {
            int roleValueStart = roleFieldStart + 9; // skip "role": "
            int roleValueEnd = json.indexOf('"', roleValueStart);
            if (roleValueEnd > roleValueStart && roleValueEnd < objEnd) {
              String currentRole = json.substring(roleValueStart, roleValueEnd);
              if (currentRole != "admin") {
                errorOut = "User is not an admin";
                return false;
              }
              // Replace the entire role value with "user"
              String before = json.substring(0, roleValueStart);
              String after = json.substring(roleValueEnd);
              json = before + String("user") + after;
              updated = true;
              break;
            }
          } else {
            // No role field means user role (default), already demoted
            errorOut = "User is already a regular user";
            return false;
          }
        }
      }
    }
    searchPos = objEnd + 1;
  }
  if (!updated) { errorOut = "User not found"; return false; }
  if (!writeText(USERS_JSON_FILE, json)) { errorOut = "Failed to write users.json"; return false; }
  broadcastOutput(String("[admin] Demoted user from admin: ") + username);
  
  // Serial admin status now checked in real-time via isAdminUser()
  if (gSerialAuthed && gSerialUser == username) {
    broadcastOutput("[serial] Your admin privileges have been revoked");
  }
  
  return true;
}

// Delete an existing user from users.json (JSON-only)
static bool deleteUserInternal(const String& username, String& errorOut) {
  DEBUG_USERSF("[users] delete internal username=%s", username.c_str());
  if (username.length() == 0) { errorOut = "Username required"; return false; }
  if (!LittleFS.exists(USERS_JSON_FILE)) { errorOut = "users.json not found"; return false; }
  String json;
  if (!readText(USERS_JSON_FILE, json)) { errorOut = "Failed to read users.json"; return false; }
  int usersIdx = json.indexOf("\"users\"");
  int openBracket = (usersIdx >= 0) ? json.indexOf('[', usersIdx) : -1;
  int closeBracket = (openBracket >= 0) ? json.indexOf(']', openBracket) : -1;
  if (openBracket < 0 || closeBracket <= openBracket) { errorOut = "Malformed users.json"; return false; }

  // Find the object for this username within the users array
  int searchPos = openBracket + 1;
  bool deleted = false;
  while (true) {
    int objStart = json.indexOf('{', searchPos);
    if (objStart < 0 || objStart > closeBracket) break;
    int objEnd = json.indexOf('}', objStart);
    if (objEnd < 0 || objEnd > closeBracket) break;
    String obj = json.substring(objStart, objEnd + 1);
    
    int un = obj.indexOf("\"username\": \"");
    if (un >= 0) {
      un += 13; // skip "username": "
      int unEnd = obj.indexOf('"', un);
      if (unEnd > un) {
        String name = obj.substring(un, unEnd);
        if (name == username) {
          // Check for ID field (founder protection) - only for the target user
          int idStart = obj.indexOf("\"id\": ");
          if (idStart >= 0) {
            idStart += 6; // skip "id": 
            int idEnd = idStart;
            while (idEnd < obj.length() && obj[idEnd] >= '0' && obj[idEnd] <= '9') idEnd++;
            if (idEnd > idStart) {
              int userId = obj.substring(idStart, idEnd).toInt();
              if (userId == 1) {
                errorOut = "Cannot delete the first admin account";
                return false;
              }
            }
          }
          
          // Find the start and end of this user object including commas
          int deleteStart = objStart;
          int deleteEnd = objEnd + 1;
          
          // Check if there's a comma before this object
          int commaBeforePos = deleteStart - 1;
          while (commaBeforePos > openBracket && (json[commaBeforePos] == ' ' || json[commaBeforePos] == '\n' || json[commaBeforePos] == '\r' || json[commaBeforePos] == '\t')) {
            commaBeforePos--;
          }
          bool hasCommaBefore = (commaBeforePos > openBracket && json[commaBeforePos] == ',');
          
          // Check if there's a comma after this object
          int commaAfterPos = deleteEnd;
          while (commaAfterPos < closeBracket && (json[commaAfterPos] == ' ' || json[commaAfterPos] == '\n' || json[commaAfterPos] == '\r' || json[commaAfterPos] == '\t')) {
            commaAfterPos++;
          }
          bool hasCommaAfter = (commaAfterPos < closeBracket && json[commaAfterPos] == ',');
          
          // Determine what to delete
          if (hasCommaBefore && hasCommaAfter) {
            // Middle object: delete from start to after comma
            deleteEnd = commaAfterPos + 1;
          } else if (hasCommaBefore && !hasCommaAfter) {
            // Last object: delete from before comma to end
            deleteStart = commaBeforePos;
          } else if (!hasCommaBefore && hasCommaAfter) {
            // First object: delete from start to after comma
            deleteEnd = commaAfterPos + 1;
          }
          // If no commas (only object), just delete the object itself
          
          // Remove the user object
          String before = json.substring(0, deleteStart);
          String after = json.substring(deleteEnd);
          json = before + after;
          deleted = true;
          break;
        }
      }
    }
    searchPos = objEnd + 1;
  }
  if (!deleted) { errorOut = "User not found"; return false; }
  if (!writeText(USERS_JSON_FILE, json)) { errorOut = "Failed to write users.json"; return false; }
  
  // Force logout all sessions for the deleted user
  int revokedSessions = 0;
  String reason = "Account deleted by administrator";
  
  // Revoke web sessions
  for (int i = 0; i < MAX_SESSIONS; ++i) {
    if (!gSessions[i].sid.length()) continue;
    if (!gSessions[i].user.equalsIgnoreCase(username)) continue;
    if (gSessions[i].ip.length() > 0) {
      storeLogoutReason(gSessions[i].ip, reason);
    }
    enqueueTargetedRevokeForSessionIdx(i, reason);
    revokedSessions++;
  }
  
  // Force logout serial session if this user is logged in
  if (gSerialAuthed && gSerialUser.equalsIgnoreCase(username)) {
    gSerialAuthed = false;
    gSerialUser = String();
    broadcastOutput("[serial] Your account has been deleted. You have been logged out.");
    revokedSessions++; // Count serial session too
  }
  
  broadcastOutput(String("[admin] Deleted user: ") + username + 
                  (revokedSessions > 0 ? String(" (") + String(revokedSessions) + " active session(s) terminated)" : ""));
  return true;
}

esp_err_t handleAdminApproveUser(httpd_req_t* req) {
  AuthContext ctx;
  ctx.transport = AUTH_HTTP;
  ctx.opaque = req;
  ctx.path = "/api/admin/approve";
  getClientIP(req, ctx.ip);
  if (!tgRequireAuth(ctx)) return ESP_OK;
  logAuthAttempt(true, req->uri, ctx.user, ctx.ip, "");

  if (!isAdminUser(ctx.user)) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"success\":false,\"error\":\"Admin access required\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }

  // Read POST body inline (reuse pattern from other handlers)
  String body;
  int total_len = req->content_len;
  if (total_len > 0) {
    std::unique_ptr<char, void (*)(void*)> buf((char*)ps_alloc(total_len + 1, AllocPref::PreferPSRAM, "http.admin"), free);
    if (buf) {
      int received = 0;
      while (received < total_len) {
        int r = httpd_req_recv(req, buf.get() + received, total_len - received);
        if (r <= 0) break;
        received += r;
      }
      buf.get()[received] = '\0';
      body = String(buf.get());
    }
  }
  String username = urlDecode(extractFormField(body, "username"));
  if (username.length() == 0) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"success\":false,\"error\":\"Username required\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }

  String err;
  bool ok = approvePendingUserInternal(username, err);
  httpd_resp_set_type(req, "application/json");
  if (ok) {
    httpd_resp_send(req, "{\"success\":true}", HTTPD_RESP_USE_STRLEN);
  } else {
    httpd_resp_send(req, (String("{\"success\":false,\"error\":\"") + err + "\"}").c_str(), HTTPD_RESP_USE_STRLEN);
  }
  return ESP_OK;
}

esp_err_t handleAdminDenyUser(httpd_req_t* req) {
  AuthContext ctx;
  ctx.transport = AUTH_HTTP;
  ctx.opaque = req;
  ctx.path = "/api/admin/reject";
  getClientIP(req, ctx.ip);
  if (!tgRequireAuth(ctx)) return ESP_OK;
  logAuthAttempt(true, req->uri, ctx.user, ctx.ip, "");

  // Preserve JSON error contract for this endpoint
  if (!isAdminUser(ctx.user)) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"success\":false,\"error\":\"Admin access required\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }

  // Read full body and decode
  int total_len = req->content_len;
  if (total_len <= 0) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"success\":false,\"error\":\"No data\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }
  std::unique_ptr<char, void (*)(void*)> buf((char*)ps_alloc(total_len + 1, AllocPref::PreferPSRAM, "http.passwd"), free);
  int received = 0;
  while (received < total_len) {
    int r = httpd_req_recv(req, buf.get() + received, total_len - received);
    if (r <= 0) {
      httpd_resp_set_type(req, "application/json");
      httpd_resp_send(req, "{\"success\":false,\"error\":\"Read error\"}", HTTPD_RESP_USE_STRLEN);
      return ESP_OK;
    }
    received += r;
  }
  buf.get()[received] = '\0';
  String body = String(buf.get());
  String username = urlDecode(extractFormField(body, "username"));

  if (username.length() == 0) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"success\":false,\"error\":\"Username required\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }

  String err;
  if (!denyPendingUserInternal(username, err)) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, (String("{\"success\":false,\"error\":\"") + err + "\"}").c_str(), HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }

  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, "{\"success\":true}", HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

void setup() {
  // --- Initialise Serial early ---
  Serial.begin(115200);
  delay(500); // Longer delay for serial connection

  
  // Generate unique boot ID for session versioning
  uint64_t chipId = ESP.getEfuseMac();
  gBootId = String((uint32_t)(chipId >> 32), HEX) + String((uint32_t)chipId, HEX) + "_" + String(millis());
  
  // Debug: Log the boot ID generation with extra visibility
  Serial.println(""); // Blank line for visibility
  Serial.printf("=== [BOOT_DEBUG] Generated new boot ID: %s ===\n", gBootId.c_str());
  Serial.println(""); // Blank line for visibility
  Serial.flush(); // Ensure debug message is sent immediately
  
  // Build identifier banner
  broadcastOutput("[build] Firmware: reg-json-debug-1");
  Serial.printf("[BOOT_DEBUG] Setup continuing after banner...\n");

  // --- Command Executor Task init ---
  if (!gCmdExecQ) {
    gCmdExecQ = xQueueCreate(6, sizeof(ExecReq*));
    if (!gCmdExecQ) {
      Serial.println("FATAL: Failed to create command exec queue");
      while (1) delay(1000);
    }
    const uint32_t cmdExecStackWords = 4096; // words (≈16 KB)
    if (xTaskCreate(commandExecTask, "cmd_exec", cmdExecStackWords, nullptr, 1, nullptr) != pdPASS) {
      Serial.println("FATAL: Failed to create command exec task");
      while (1) delay(1000);
    }
  }

  // Initialize large buffers with PSRAM preference
  if (!gStreamBuffer) {
    gStreamBuffer = (char*)ps_alloc(5120, AllocPref::PreferPSRAM, "stream.buf");
    if (!gStreamBuffer) {
      Serial.println("FATAL: Failed to allocate stream buffer");
      while (1) delay(1000);
    }
  }

  if (!gDebugBuffer) {
    gDebugBuffer = (char*)ps_alloc(1024, AllocPref::PreferPSRAM, "debug.buf");
    if (!gDebugBuffer) {
      Serial.println("FATAL: Failed to allocate debug buffer");
      while (1) delay(1000);
    }
  }

  // Initialize automation memo arrays
  if (!gAutoMemoId) {
    gAutoMemoId = (long*)ps_alloc(kAutoMemoCap * sizeof(long), AllocPref::PreferPSRAM, "auto.memo.id");
    if (!gAutoMemoId) {
      Serial.println("FATAL: Failed to allocate automation memo ID array");
      while (1) delay(1000);
    }
    memset(gAutoMemoId, 0, kAutoMemoCap * sizeof(long));
  }

  // Initialize sensor cache mutex
  gSensorCache.mutex = xSemaphoreCreateMutex();
  if (!gSensorCache.mutex) {
    Serial.println("FATAL: Failed to create sensor cache mutex");
    while (1) delay(1000);
  }
  // Create shared I2C gate as a binary semaphore (not ownership-tracked mutex)
  // Rationale: multiple tasks and CLI may take/give around I2C operations; a binary
  // semaphore avoids FreeRTOS mutex owner-assertions under complex interleavings.
  i2cMutex = xSemaphoreCreateBinary();
  if (!i2cMutex) {
    Serial.println("FATAL: Failed to create I2C semaphore");
    while (1) delay(1000);
  }
  // Start in 'available' state
  xSemaphoreGive(i2cMutex);

  // Per-sensor tasks will be created lazily on first start to conserve RAM

  if (!gAutoMemoKey) {
    gAutoMemoKey = (unsigned long*)ps_alloc(kAutoMemoCap * sizeof(unsigned long), AllocPref::PreferPSRAM, "auto.memo.key");
    if (!gAutoMemoKey) {
      Serial.println("FATAL: Failed to allocate automation memo key array");
      while (1) delay(1000);
    }
    memset(gAutoMemoKey, 0, kAutoMemoCap * sizeof(unsigned long));
  }

  // Initialize I2C clock stack
  if (!gI2CClockStack) {
    gI2CClockStack = (uint32_t*)ps_alloc(kI2CClockStackMax * sizeof(uint32_t), AllocPref::PreferPSRAM, "i2c.stack");
    if (!gI2CClockStack) {
      Serial.println("FATAL: Failed to allocate I2C clock stack");
      while (1) delay(1000);
    }
    memset(gI2CClockStack, 0, kI2CClockStackMax * sizeof(uint32_t));
  }

  // Initialize WiFi networks array
  if (!gWifiNetworks) {
    gWifiNetworks = (WifiNetwork*)ps_alloc(MAX_WIFI_NETWORKS * sizeof(WifiNetwork), AllocPref::PreferPSRAM, "wifi.networks");
    if (!gWifiNetworks) {
      Serial.println("FATAL: Failed to allocate WiFi networks array");
      while (1) delay(1000);
    }
    // Initialize with placement new to call constructors
    for (int i = 0; i < MAX_WIFI_NETWORKS; i++) {
      new (&gWifiNetworks[i]) WifiNetwork();
    }
  }

  // Initialize session entries array
  if (!gSessions) {
    gSessions = (SessionEntry*)ps_alloc(MAX_SESSIONS * sizeof(SessionEntry), AllocPref::PreferPSRAM, "sessions");
    if (!gSessions) {
      Serial.println("FATAL: Failed to allocate sessions array");
      while (1) delay(1000);
    }
    // Initialize with placement new to call constructors
    for (int i = 0; i < MAX_SESSIONS; i++) {
      new (&gSessions[i]) SessionEntry();
    }
  }

  // Initialize logout reasons array
  if (!gLogoutReasons) {
    gLogoutReasons = (LogoutReason*)ps_alloc(MAX_LOGOUT_REASONS * sizeof(LogoutReason), AllocPref::PreferPSRAM, "logout.reasons");
    if (!gLogoutReasons) {
      Serial.println("FATAL: Failed to allocate logout reasons array");
      while (1) delay(1000);
    }
    // Initialize with placement new to call constructors
    for (int i = 0; i < MAX_LOGOUT_REASONS; i++) {
      new (&gLogoutReasons[i]) LogoutReason();
    }
  }

  // Filesystem FIRST to enable early allocation logging
  fsInit();

  // Now safe to emit output (may allocate and will be logged)
  broadcastOutput("");
  broadcastOutput("Booting ESP32 Minimal Auth");

  // First-time setup if needed (prompts on Serial)
  firstTimeSetupIfNeeded();

  // Load legacy simple settings (ntp/tz) and unified JSON settings
  loadSettings();
  bool unifiedLoaded = loadUnifiedSettings();
  String fu, fp;
  if (loadUsersFromFile(fu, fp)) {
    gAuthUser = fu;
    gAuthPass = fp;
  }
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
  
  // NTP setup and sync - only if WiFi is connected
  if (WiFi.isConnected()) {
    setupNTP();
    
    // Wait for NTP sync by checking time validity (up to 10 seconds)
    bool ntpSynced = false;
    for (int i = 0; i < 50 && !ntpSynced; i++) {
      delay(200);
      time_t now = time(nullptr);
      struct tm timeinfo;
      if (getLocalTime(&timeinfo)) {
        logTimeSyncedMarkerIfReady();
        ntpSynced = true;
        break;
      }
    }
    if (ntpSynced) {
      broadcastOutput("NTP time synchronized successfully");
    } else {
      broadcastOutput("NTP sync timeout - continuing with local time");
    }
  } else {
    broadcastOutput("NTP setup skipped - no WiFi connection");
  }

  // Initialize device registry (after I2C system is ready)
  Serial.println("[BOOT] Starting device discovery...");
  ensureDeviceRegistryFile();
  discoverI2CDevices();
  Serial.println("[BOOT] Device discovery completed");

  // ESP-NOW auto-initialization (if enabled in settings)
  if (gSettings.espnowenabled) {
    Serial.println("[ESP-NOW] Auto-init: Enabled by setting");
    runUnifiedSystemCommand("espnow init");
    Serial.println("[ESP-NOW] Auto-init: Command executed");
  } else {
    Serial.println("[ESP-NOW] Auto-init: Disabled by setting");
  }

  // HTTP server - only start if WiFi is connected
  if (WiFi.isConnected()) {
    if (server == NULL) startHttpServer();
    broadcastOutput("Try: http://" + WiFi.localIP().toString());
  } else {
    broadcastOutput("HTTP server not started - no WiFi connection");
  }
}

// Performance monitoring function
static void performanceCounter() {
  static unsigned long perfCounter = 0;
  static unsigned long lastPerfReport = 0;
  perfCounter++;

  // Report performance every 5 seconds
  if (millis() - lastPerfReport > 5000) {
    unsigned long loopsPerSec = perfCounter / 5;
    DEBUG_PERFORMANCEF("Performance: %lu loops/sec", loopsPerSec);
    perfCounter = 0;
    lastPerfReport = millis();
  }
}

void loop() {
  // Performance monitoring (gated by debug flag)
  if (gDebugFlags & DEBUG_PERFORMANCE) {
    performanceCounter();
  }
  
  // ESP-NOW chunked message timeout cleanup
  cleanupExpiredChunkedMessage();
  // Debounced SSE sensor-status broadcast
  if (gSensorStatusDirty) {
    unsigned long nowMs = millis();
    if (gNextSensorStatusBroadcastDue != 0 && (long)(nowMs - gNextSensorStatusBroadcastDue) >= 0) {
      // Dump quick snapshot for diagnostics
      DEBUG_SENSORSF("flags before broadcast | thermal=%d tof=%d imu=%d apdsColor=%d apdsProx=%d apdsGest=%d",
                     thermalEnabled ? 1 : 0, tofEnabled ? 1 : 0, imuEnabled ? 1 : 0,
                     apdsColorEnabled ? 1 : 0, apdsProximityEnabled ? 1 : 0, apdsGestureEnabled ? 1 : 0);
      broadcastSensorStatusToAllSessions();
      gSensorStatusDirty = false;
      gNextSensorStatusBroadcastDue = 0;
    }
  }

  // Non-blocking Serial CLI
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      String cmd = gSerialCLI;
      cmd.trim();

      // Serial auth gate: require login before executing any commands
      if (!gSerialAuthed) {
        if (cmd.startsWith("login ")) {
          // Parse: login <user> <pass>
          String rest = cmd.substring(6);
          rest.trim();
          int sp = rest.indexOf(' ');
          if (sp <= 0) {
            broadcastOutput("Usage: login <username> <password>");
          } else {
            String u = rest.substring(0, sp);
            String p = rest.substring(sp + 1);
            if (isValidUser(u, p)) {
              // Unified auth success flow for Serial transport
              AuthContext ctx;
              ctx.transport = AUTH_SERIAL;
              ctx.user = u;
              ctx.ip = "local";
              ctx.path = "serial/login";
              ctx.sid = String();
              authSuccessUnified(ctx, nullptr);
              // Check admin status in real-time
              bool isCurrentlyAdmin = isAdminUser(u);
              broadcastOutput(String("Login successful. User: ") + u + (isCurrentlyAdmin ? " (admin)" : ""));
            } else {
              broadcastOutput("Authentication failed.");
            }
          }
        } else if (cmd.length() > 0) {
          // Block everything else (including 'clear') until login
          broadcastOutput("Authentication required. Use: login <username> <password>");
        }
      } else {
        // Authenticated: handle local session commands first
        if (cmd == "logout") {
          gSerialAuthed = false;
          gSerialUser = String();
          // gSerialIsAdmin no longer needed - using real-time checks
          broadcastOutput("Logged out.");
        } else if (cmd == "whoami") {
          bool isCurrentlyAdmin = gSerialUser.length() ? isAdminUser(gSerialUser) : false;
          broadcastOutput(String("You are ") + (gSerialUser.length() ? gSerialUser : String("(unknown)")) + (isCurrentlyAdmin ? " (admin)" : ""));
        } else {
          // Record the entered command into the unified feed with source tag (only after auth)
          appendCommandToFeed("serial", cmd);

          // Build unified command context for Serial origin
          AuthContext actx;
          actx.transport = AUTH_SERIAL;
          actx.user = gSerialUser;
          actx.ip = "local";
          actx.path = "serial";
          Command uc;
          uc.line = cmd;
          uc.ctx.origin = ORIGIN_SERIAL;
          uc.ctx.auth = actx;
          uc.ctx.id = (uint32_t)millis();
          uc.ctx.timestampMs = (uint32_t)millis();
          uc.ctx.outputMask = CMD_OUT_SERIAL | CMD_OUT_LOG;
          uc.ctx.validateOnly = false;
          uc.ctx.replyHandle = nullptr;
          uc.ctx.httpReq = nullptr;

          String out;
          (void)submitAndExecuteSync(uc, out);
          routeOutput(out, uc.ctx);
        }
      }
      gSerialCLI = "";
      Serial.print("$ ");
    } else {
      gSerialCLI += c;
      // Optional: echo
      // Serial.print(c);
    }
  }

  // Automations scheduler: run immediately if a mutation recently occurred
  // Skip if already in automation context to prevent recursion
  if (gAutosDirty && !gInAutomationContext) {
    gAutosDirty = false;
    schedulerTickMinute();
  }

  // Minute-based automations scheduler (runs once per minute)
  // Skip if already in automation context to prevent recursion
  if (!gInAutomationContext) {
    unsigned long minuteNow = millis() / 60000UL;
    if (minuteNow != gLastMinuteSeen) {
      gLastMinuteSeen = minuteNow;
      schedulerTickMinute();
    }
  }

  // All sensor polling now handled by unified sensor polling task - no loop processing needed

  // esp_http_server handles requests internally
  delay(1);
}

// -------------------------------
// Shared helpers for CLI help UI
// -------------------------------
static String renderHelpMain(bool showAll = false) {
  String help = String("\033[2J\033[H") + "Help";
  if (showAll) {
    help += " (All Commands)";
  } else {
    help += " (QT PY and Connected Sensors Only)";
  }
  help += "\n~~~~~\n\n"
          "Sections:\n"
          "  system    - System status, uptime, memory, files, broadcast, reboot\n"
          "  wifi      - WiFi network management (info, list, add/rm, connect/scan)\n"
          "  sensors   - Sensors and peripherals";
  
  if (!showAll) {
    // Show connected sensor summary
    String connectedSensors = "";
    if (isSensorConnected("thermal")) connectedSensors += " thermal";
    if (isSensorConnected("tof")) connectedSensors += " ToF";
    if (isSensorConnected("imu")) connectedSensors += " IMU";
    if (isSensorConnected("apds")) connectedSensors += " APDS";
    
    if (connectedSensors.length() > 0) {
      help += " (connected:" + connectedSensors + ")";
    } else {
      help += " (none detected)";
    }
  }
  
  help += "\n"
          "  settings  - Device configuration grouped like the Settings page:\n"
          "              - WiFi Network (via wifi commands)\n"
          "              - System Time (tzoffsetminutes, ntpserver)\n"
          "              - Output Channels (outserial/outweb/outtft)\n"
          "              - CLI History Size (clihistorysize)\n"
          "              - Sensors UI (thermalPollingMs, tofPollingMs, ...)\n"
          "              - Device-side Sensor Settings (i2c clocks, poll rates)\n"
          "              - Debug Controls (debug* toggles)\n\n"
          "Tips:\n"
          "  - Type a section name to view its commands (e.g., 'settings').\n"
          "  - Type 'help all' to see all commands regardless of connected sensors.\n"
          "  - Type 'back' to return to this menu; 'exit' to return to CLI.\n";
  
  return help;
}

static String renderHelpSystem() {
  return String("\033[2J\033[H") + "System Commands\n"
                                   "~~~~~~~~~~~~~~~\n\n"
                                   "  status              - Show system status\n"
                                   "  uptime              - Show system uptime\n"
                                   "  memory              - Show memory usage\n"
                                   "  psram               - Show PSRAM usage details\n"
                                   "  fsusage             - Show filesystem usage (total/used/free)\n"
                                   "  files [path]        - List files in LittleFS (default '/')\n"
                                   "  mkdir <path>        - Create a new folder\n"
                                   "  rmdir <path>        - Remove an empty folder\n"
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
  return String("\033[2J\033[H") + "Settings / Configuration (mirrors Settings page)\n"
                                   "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n\n"
                                   "System Time:\n"
                                   "  set tzOffsetMinutes <-720..720>   - Minutes offset from UTC (e.g., -240 for UTC-4)\n"
                                   "  set ntpServer <host>              - Validate and save NTP server host\n\n"
                                   "Output Channels:\n"
                                   "  outserial [persist|temp] <0|1>    - Serial output (persisted or runtime)\n"
                                   "  outweb    [persist|temp] <0|1>    - Web output (persisted or runtime)\n"
                                   "  outtft    [persist|temp] <0|1>    - TFT output (persisted or runtime)\n"
                                   "  Note: Order is flexible (e.g., 'outserial 1 temp').\n\n"
                                   "CLI History Size:\n"
                                   "  clihistorysize <1..100>           - Number of commands kept in history\n\n"
                                   "Sensors UI Settings (client-side):\n"
                                   "  set thermalPollingMs <50..5000>         - Thermal UI polling (ms)\n"
                                   "  set tofPollingMs <50..5000>             - ToF UI polling (ms)\n"
                                   "  set tofStabilityThreshold <0..50>       - Stable samples before update\n"
                                   "  set thermalPaletteDefault <grayscale|coolwarm>\n"
                                   "  set thermalWebMaxFps <1..60>            - UI refresh cap\n"
                                   "  set thermalEWMAFactor <0.0..1.0>        - Smoothing factor\n"
                                   "  set thermalTransitionMs <0..5000>       - UI animation duration\n"
                                   "  set tofTransitionMs <0..5000>           - UI animation duration\n"
                                   "  set tofUiMaxDistanceMm <100..10000>     - UI distance clamp (mm)\n"
                                   "  set thermalInterpolationEnabled <0|1>   - Enable frame interpolation\n"
                                   "  set thermalInterpolationSteps <1..8>    - Interpolated frames per step\n"
                                   "  set thermalInterpolationBufferSize <1..10>\n"
                                   "  set thermalWebClientQuality <1..4>      - 1x..4x scaling\n\n"
                                   "Device-side Sensor Settings:\n"
                                   "  thermaltargetfps <1..8>\n"
                                   "  thermaldevicepollms <50..5000>\n"
                                   "  tofdevicepollms <50..5000>\n"
                                   "  imudevicepollms <5..1000>\n"
                                   "  i2cclockthermalhz <100000..1000000>\n"
                                   "  i2cclocktofhz <50000..400000>\n\n"
                                   "Debug Controls (toggles):\n"
                                   "  debugAuthCookies|debugHttp|debugSse|debugCli|debugSensorsFrame\n"
                                   "  debugSensorsData|debugSensorsGeneral|debugWifi|debugStorage\n"
                                   "  debugPerformance|debugDateTime|debugCommandFlow|debugUsers   <0|1>\n\n"
                                   "User Admin:\n"
                                   "  user promote <username>\n\n"
                                   "Other Settings:\n"
                                   "  wifiautoreconnect <0|1>\n\n"
                                   "Type 'back' to return to help menu or 'exit' to return to CLI.";
}

static String renderHelpWifi() {
  return String("\033[2J\033[H") + "WiFi Network\n"
                                   "~~~~~~~~~~~~~\n\n"
                                   "  wifiinfo     - Show current WiFi connection info\n"
                                   "  wifilist     - List saved WiFi networks (priority asc)\n"
                                   "  wifiadd <ssid> <pass> [priority] [hidden0|1] - Add/update network\n"
                                   "  wifirm <ssid>  - Remove saved network\n"
                                   "  wifipromote <ssid> [newPriority] - Change priority (default promote to 1)\n"
                                   "  wificonnect [--best | --index <n>]\n"
                                   "                  - Connect to best saved network (default) or a specific saved entry by number\n"
                                   "  wifidisconnect (admin) - Disconnect from current WiFi\n"
                                   "  wifiscan [json] - Scan for nearby access points (use 'json' for machine-readable)\n\n"
                                   "Type 'back' to return to help menu or 'exit' to return to CLI.";
}

static String renderHelpSensors() {
  String help = String("\033[2J\033[H") + "Sensor Commands";
  if (gShowAllCommands) {
    help += " (All Available)";
  } else {
    help += " (Connected Only)";
  }
  help += "\n~~~~~~~~~~~~~~~\n\n";
  
  // APDS9960 RGB/Gesture Sensor
  if (gShowAllCommands || isSensorConnected("apds")) {
    help += "APDS9960 RGB/Gesture Sensor";
    if (!gShowAllCommands) help += " ✓ Connected";
    help += ":\n"
            "  apdscolorstart/stop     - Enable/disable color sensing\n"
            "  apdsproximitystart/stop - Enable/disable proximity sensing\n"
            "  apdsgesturestart/stop   - Enable/disable gesture sensing\n"
            "  apdscolor               - Read color values\n"
            "  apdsproximity           - Read proximity value\n"
            "  apdsgesture             - Read gesture\n\n";
  }
  
  // VL53L4CX ToF Distance Sensor
  if (gShowAllCommands || isSensorConnected("tof")) {
    help += "VL53L4CX ToF Distance Sensor";
    if (!gShowAllCommands) help += " ✓ Connected";
    help += ":\n"
            "  tofstart/stop           - Enable/disable ToF sensor\n"
            "  tof                     - Read distance measurement\n\n";
  }
  
  // MLX90640 Thermal Camera
  if (gShowAllCommands || isSensorConnected("thermal")) {
    help += "MLX90640 Thermal Camera";
    if (!gShowAllCommands) help += " ✓ Connected";
    help += ":\n"
            "  thermalstart/stop       - Enable/disable thermal sensor\n"
            "  thermal                 - Read thermal pixel array\n\n";
  }
  
  // IMU & Other Sensors
  if (gShowAllCommands || isSensorConnected("imu")) {
    help += "IMU";
    if (!gShowAllCommands) help += " ✓ Connected";
    help += ":\n"
            "  imustart/stop           - Enable/disable IMU sensor\n"
            "  imu                     - Read IMU data (accel/gyro/temp)\n\n";
  }
  
  // Always show LED and gamepad (not I2C dependent)
  help += "Other Controls:\n"
          "  gamepad                 - Read gamepad state\n"
          "  ledcolor <color>        - Set LED to named color\n"
          "  ledclear                - Turn off LED\n"
          "  ledeffect <type> <color1> [color2] [duration] - Run LED effects\n\n";
  
  if (!gShowAllCommands) {
    help += "Note: Only showing commands for connected sensors.\n"
            "Type 'help all' then 'sensors' to see all available sensor commands.\n\n";
  }
  
  help += "Type 'back' to return to help menu or 'exit' to return to CLI.";
  
  return help;
}

static String exitToNormalBanner() {
  gCLIState = CLI_NORMAL;
  // Restore hidden history when leaving help
  String banner = "Returned to normal CLI mode.";
  String restored = gHiddenHistory;
  gHiddenHistory = "";
  if (!gWebMirror.buf) { gWebMirror.init(gWebMirrorCap); }
  gWebMirror.assignFrom(restored);  // restore prior history to visible buffer
  return banner;
}

static String exitHelpAndExecute(const String& originalCmd) {
  // Exit help, show banner, and then execute the original command in normal mode
  String banner = exitToNormalBanner() + "\n";
  AuthContext ctx;
  ctx.transport = gExecFromWeb ? AUTH_HTTP : AUTH_SERIAL;
  ctx.user = gExecUser;
  ctx.ip = gExecFromWeb ? String() : String("local");
  ctx.path = "/help/exit";
  String out;
  (void)executeCommand(ctx, originalCmd, out);
  return banner + out;
}

// Core command handlers (Batch 1)
static String cmd_status_core() {
  String out = "System Status:\n";
  out += "  WiFi: " + String(WiFi.isConnected() ? "Connected" : "Disconnected") + "\n";
  out += "  IP: " + WiFi.localIP().toString() + "\n";
  out += "  Filesystem: " + String(LittleFS.begin() ? "Ready" : "Error") + "\n";
  out += "  Free Heap: " + String(ESP.getFreeHeap()) + " bytes\n";
  size_t psTot = ESP.getPsramSize();
  if (psTot > 0) {
    out += "  Free PSRAM: " + String(ESP.getFreePsram()) + " bytes\n";
    out += "  Total PSRAM: " + String(psTot) + " bytes\n";
  }
  return out;
}

// Modern command handler for 'status' - handles validation internally
static String cmd_status_modern(const String& cmd) {
  RETURN_VALID_IF_VALIDATE();
  return cmd_status_core();
}

static String cmd_uptime_core() {
  unsigned long uptimeMs = millis();
  unsigned long seconds = uptimeMs / 1000;
  unsigned long minutes = seconds / 60;
  unsigned long hours = minutes / 60;
  return "Uptime: " + String(hours) + "h " + String(minutes % 60) + "m " + String(seconds % 60) + "s";
}

// Modern command handler for 'uptime' - handles validation internally
static String cmd_uptime_modern(const String& cmd) {
  RETURN_VALID_IF_VALIDATE();
  return cmd_uptime_core();
}

// ESP-NOW device name helper functions
static void addEspNowDevice(const uint8_t* mac, const String& name) {
  // Check if device already exists (update name)
  for (int i = 0; i < gEspNowDeviceCount; i++) {
    if (memcmp(gEspNowDevices[i].mac, mac, 6) == 0) {
      gEspNowDevices[i].name = name;
      return;
    }
  }
  
  // Add new device if space available
  if (gEspNowDeviceCount < 16) {
    memcpy(gEspNowDevices[gEspNowDeviceCount].mac, mac, 6);
    gEspNowDevices[gEspNowDeviceCount].name = name;
    gEspNowDeviceCount++;
  }
}

static void removeEspNowDevice(const uint8_t* mac) {
  for (int i = 0; i < gEspNowDeviceCount; i++) {
    if (memcmp(gEspNowDevices[i].mac, mac, 6) == 0) {
      // Shift remaining devices down
      for (int j = i; j < gEspNowDeviceCount - 1; j++) {
        gEspNowDevices[j] = gEspNowDevices[j + 1];
      }
      gEspNowDeviceCount--;
      return;
    }
  }
}

static String getEspNowDeviceName(const uint8_t* mac) {
  for (int i = 0; i < gEspNowDeviceCount; i++) {
    if (memcmp(gEspNowDevices[i].mac, mac, 6) == 0) {
      return gEspNowDevices[i].name;
    }
  }
  return ""; // No name found
}

// Save ESP-NOW devices to filesystem
static void saveEspNowDevices() {
  if (!LittleFS.begin()) return;
  
  File file = LittleFS.open("/espnow_devices.json", "w");
  if (!file) return;
  
  file.println("{");
  file.println("  \"devices\": [");
  
  for (int i = 0; i < gEspNowDeviceCount; i++) {
    file.print("    {");
    file.print("\"mac\": \"");
    file.print(formatMacAddress(gEspNowDevices[i].mac));
    file.print("\", \"name\": \"");
    file.print(gEspNowDevices[i].name);
    file.print("\", \"encrypted\": ");
    file.print(gEspNowDevices[i].encrypted ? "true" : "false");
    
    if (gEspNowDevices[i].encrypted) {
      file.print(", \"key\": \"");
      for (int j = 0; j < 16; j++) {
        if (gEspNowDevices[i].key[j] < 16) file.print("0");
        file.print(String(gEspNowDevices[i].key[j], HEX));
      }
      file.print("\"");
    }
    
    file.print("}");
    if (i < gEspNowDeviceCount - 1) file.print(",");
    file.println();
  }
  
  file.println("  ]");
  file.println("}");
  file.close();
}

// Load ESP-NOW devices from filesystem
static void loadEspNowDevices() {
  if (!LittleFS.begin()) return;
  
  File file = LittleFS.open("/espnow_devices.json", "r");
  if (!file) return;
  
  String content = file.readString();
  file.close();
  
  // Simple JSON parsing for device list
  gEspNowDeviceCount = 0;
  int pos = 0;
  
  while ((pos = content.indexOf("\"mac\":", pos)) >= 0) {
    if (gEspNowDeviceCount >= 16) break; // Max devices reached
    
    // Extract MAC address
    int macStart = content.indexOf("\"", pos + 6) + 1;
    int macEnd = content.indexOf("\"", macStart);
    if (macStart <= 0 || macEnd <= macStart) break;
    
    String macStr = content.substring(macStart, macEnd);
    
    // Extract device name
    int namePos = content.indexOf("\"name\":", macEnd);
    if (namePos < 0) break;
    
    int nameStart = content.indexOf("\"", namePos + 7) + 1;
    int nameEnd = content.indexOf("\"", nameStart);
    if (nameStart <= 0 || nameEnd <= nameStart) break;
    
    String name = content.substring(nameStart, nameEnd);
    
    // Check for encryption flag
    bool encrypted = false;
    int encPos = content.indexOf("\"encrypted\":", nameEnd);
    if (encPos >= 0 && encPos < content.indexOf("}", nameEnd)) {
      int encStart = content.indexOf(":", encPos) + 1;
      String encValue = content.substring(encStart, content.indexOf(",", encStart));
      encValue.trim();
      encrypted = (encValue == "true");
    }
    
    // Parse MAC address and store device
    uint8_t mac[6];
    if (parseMacAddress(macStr, mac)) {
      memcpy(gEspNowDevices[gEspNowDeviceCount].mac, mac, 6);
      gEspNowDevices[gEspNowDeviceCount].name = name;
      gEspNowDevices[gEspNowDeviceCount].encrypted = encrypted;
      
      // Load encryption key if present
      if (encrypted) {
        int keyPos = content.indexOf("\"key\":", nameEnd);
        if (keyPos >= 0 && keyPos < content.indexOf("}", nameEnd)) {
          int keyStart = content.indexOf("\"", keyPos + 6) + 1;
          int keyEnd = content.indexOf("\"", keyStart);
          if (keyStart > 0 && keyEnd > keyStart) {
            String keyHex = content.substring(keyStart, keyEnd);
            // Parse hex key (32 hex chars = 16 bytes)
            if (keyHex.length() == 32) {
              for (int j = 0; j < 16; j++) {
                String byteStr = keyHex.substring(j * 2, j * 2 + 2);
                gEspNowDevices[gEspNowDeviceCount].key[j] = strtol(byteStr.c_str(), NULL, 16);
              }
            }
          }
        }
      } else {
        // Clear key for unencrypted devices
        memset(gEspNowDevices[gEspNowDeviceCount].key, 0, 16);
      }
      
      gEspNowDeviceCount++;
    }
    
    pos = nameEnd;
  }
}

// Restore ESP-NOW peers from saved devices
static void restoreEspNowPeers() {
  if (!gEspNowInitialized) return;
  
  for (int i = 0; i < gEspNowDeviceCount; i++) {
    bool success = addEspNowPeerWithEncryption(
      gEspNowDevices[i].mac, 
      gEspNowDevices[i].encrypted, 
      gEspNowDevices[i].encrypted ? gEspNowDevices[i].key : nullptr
    );
    
    if (success) {
      String encStatus = gEspNowDevices[i].encrypted ? " (encrypted)" : " (unencrypted)";
      broadcastOutput("[ESP-NOW] Restored device: " + gEspNowDevices[i].name + " (" + formatMacAddress(gEspNowDevices[i].mac) + ")" + encStatus);
    }
  }
}

// ESP-NOW status command
static String cmd_espnow_status_core() {
  String result = "ESP-NOW Status:\n";
  result += "  Initialized: " + String(gEspNowInitialized ? "Yes" : "No") + "\n";
  result += "  Channel: " + String(gEspNowChannel) + "\n";
  
  if (gEspNowInitialized) {
    result += "  MAC Address: ";
    uint8_t mac[6];
    WiFi.macAddress(mac);
    for (int i = 0; i < 6; i++) {
      if (i > 0) result += ":";
      result += String(mac[i], HEX);
    }
    result += "\n";
    result += "  Paired Devices: " + String(gEspNowDeviceCount) + "\n";
  }
  
  return result;
}

static String cmd_espnow_status_modern(const String& cmd) {
  RETURN_VALID_IF_VALIDATE();
  return cmd_espnow_status_core();
}

// Derive encryption key from passphrase
static void deriveKeyFromPassphrase(const String& passphrase, uint8_t* key) {
  if (passphrase.length() == 0) {
    // No passphrase = no encryption
    memset(key, 0, 16);
    gEspNowEncryptionEnabled = false;
    return;
  }
  
  // Create consistent input for all devices (no device-specific salt)
  // All devices with the same passphrase will derive the same key
  String saltedInput = passphrase + ":ESP-NOW-SHARED-KEY";
  
  // Use SHA-256 to derive key (first 16 bytes)
  uint8_t hash[32];
  mbedtls_sha256((uint8_t*)saltedInput.c_str(), saltedInput.length(), hash, 0);
  memcpy(key, hash, 16);
  
  gEspNowEncryptionEnabled = true;
  
  // DEBUG: Show detailed key derivation info
  uint8_t mac[6];
  WiFi.macAddress(mac);
  String macStr = "";
  for (int i = 0; i < 6; i++) {
    if (i > 0) macStr += ":";
    if (mac[i] < 16) macStr += "0";
    macStr += String(mac[i], HEX);
  }
  
  String keyStr = "";
  for (int i = 0; i < 16; i++) {
    if (key[i] < 16) keyStr += "0";
    keyStr += String(key[i], HEX);
  }
  
  broadcastOutput("[ESP-NOW] DEBUG KEY DERIVATION:");
  broadcastOutput("  Device MAC: " + macStr + " (not used in key derivation)");
  broadcastOutput("  Passphrase: " + passphrase);
  broadcastOutput("  Salt Input: " + saltedInput);
  broadcastOutput("  Derived Key: " + keyStr);
  broadcastOutput("[ESP-NOW] Encryption key derived from passphrase");
}

// Set ESP-NOW passphrase and derive encryption key
static void setEspNowPassphrase(const String& passphrase) {
  gEspNowPassphrase = passphrase;
  deriveKeyFromPassphrase(passphrase, gEspNowDerivedKey);
}

// Add ESP-NOW peer with optional encryption
static bool addEspNowPeerWithEncryption(const uint8_t* mac, bool useEncryption, const uint8_t* encryptionKey) {
  // Check if peer already exists
  if (esp_now_is_peer_exist(mac)) {
    // Remove existing peer first
    esp_now_del_peer(mac);
  }
  
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, mac, 6);
  peerInfo.channel = gEspNowChannel;
  peerInfo.ifidx = WIFI_IF_STA;
  
  if (useEncryption && encryptionKey) {
    peerInfo.encrypt = true;
    memcpy(peerInfo.lmk, encryptionKey, 16);  // Local Master Key
    
    // DEBUG: Show encryption key being used for this peer
    String keyStr = "";
    for (int i = 0; i < 16; i++) {
      if (encryptionKey[i] < 16) keyStr += "0";
      keyStr += String(encryptionKey[i], HEX);
    }
    
    broadcastOutput("[ESP-NOW] DEBUG PEER ENCRYPTION:");
    broadcastOutput("  Peer MAC: " + formatMacAddress(mac));
    broadcastOutput("  Encryption Key: " + keyStr);
    broadcastOutput("[ESP-NOW] Adding encrypted peer: " + formatMacAddress(mac));
  } else {
    peerInfo.encrypt = false;
    broadcastOutput("[ESP-NOW] Adding unencrypted peer: " + formatMacAddress(mac));
  }
  
  esp_err_t result = esp_now_add_peer(&peerInfo);
  if (result != ESP_OK) {
    broadcastOutput("[ESP-NOW] Failed to add peer: " + String(result));
    return false;
  }
  
  return true;
}

// Send chunked ESP-NOW response
static void sendChunkedResponse(const uint8_t* targetMac, bool success, const String& result, const String& senderName) {
  // For short results, send single message (backwards compatibility)
  if (result.length() <= CHUNK_SIZE) {
    String responseMessage = String("RESULT:") + (success ? "SUCCESS" : "FAILED") + ":" + result;
    if (responseMessage.length() > 240) {
      responseMessage = responseMessage.substring(0, 237) + "...";
    }
    
    esp_err_t sendResult = esp_now_send(targetMac, (uint8_t*)responseMessage.c_str(), responseMessage.length());
    if (sendResult == ESP_OK) {
      broadcastOutput("[ESP-NOW] Response sent to " + senderName);
    } else {
      broadcastOutput("[ESP-NOW] Failed to send response to " + senderName + ": " + String(sendResult));
    }
    return;
  }
  
  // Multi-chunk transmission for long results
  String hash = String(millis() % 10000); // Simple hash for message identification
  int totalChunks = (result.length() + CHUNK_SIZE - 1) / CHUNK_SIZE;
  if (totalChunks > MAX_CHUNKS) totalChunks = MAX_CHUNKS; // Limit to max chunks
  
  broadcastOutput("[ESP-NOW] Sending chunked response to " + senderName + " (" + String(totalChunks) + " chunks)");
  
  // Send START message
  String startMsg = String("RESULT_START:") + (success ? "SUCCESS" : "FAILED") + 
                   ":" + String(totalChunks) + ":" + String(result.length()) + ":" + hash;
  esp_now_send(targetMac, (uint8_t*)startMsg.c_str(), startMsg.length());
  delay(50); // Small delay between messages
  
  // Send chunks
  for (int i = 0; i < totalChunks; i++) {
    int start = i * CHUNK_SIZE;
    int end = min(start + CHUNK_SIZE, (int)result.length());
    String chunk = result.substring(start, end);
    
    String chunkMsg = String("RESULT_CHUNK:") + String(i + 1) + ":" + chunk;
    esp_now_send(targetMac, (uint8_t*)chunkMsg.c_str(), chunkMsg.length());
    delay(50); // Prevent message flooding
  }
  
  // Send END message
  String endMsg = String("RESULT_END:") + hash;
  esp_now_send(targetMac, (uint8_t*)endMsg.c_str(), endMsg.length());
  
  broadcastOutput("[ESP-NOW] Chunked response transmission complete");
}

// Cleanup expired chunked messages (5 second timeout)
static void cleanupExpiredChunkedMessage() {
  if (gActiveMessage.active && (millis() - gActiveMessage.startTime > 5000)) {
    broadcastOutput("[ESP-NOW] Chunked message timeout from " + gActiveMessage.deviceName + " - showing partial result:");
    
    // Show partial result
    String partialResult = "";
    for (int i = 0; i < gActiveMessage.receivedChunks && i < MAX_CHUNKS; i++) {
      if (gActiveMessage.chunks[i].length() > 0) {
        partialResult += gActiveMessage.chunks[i];
      }
    }
    
    if (partialResult.length() > 0) {
      broadcastOutput(partialResult);
    }
    broadcastOutput("[ESP-NOW] Error: Incomplete message (" + String(gActiveMessage.receivedChunks) + "/" + String(gActiveMessage.totalChunks) + " chunks received)");
    
    // Reset state
    gActiveMessage.active = false;
    for (int i = 0; i < MAX_CHUNKS; i++) {
      gActiveMessage.chunks[i] = "";
    }
  }
}

// Handle chunked message assembly
static void handleChunkedMessage(const String& message, const String& deviceName) {
  if (message.startsWith("RESULT_START:")) {
    // Parse: RESULT_START:SUCCESS:4:800:ABC123
    cleanupExpiredChunkedMessage(); // Clean up any previous incomplete message
    
    int colon1 = message.indexOf(':', 13); // After "RESULT_START:"
    int colon2 = message.indexOf(':', colon1 + 1);
    int colon3 = message.indexOf(':', colon2 + 1);
    int colon4 = message.indexOf(':', colon3 + 1);
    
    if (colon1 > 0 && colon2 > 0 && colon3 > 0 && colon4 > 0) {
      gActiveMessage.status = message.substring(13, colon1);
      gActiveMessage.totalChunks = message.substring(colon1 + 1, colon2).toInt();
      // Skip total length (colon2 to colon3)
      gActiveMessage.hash = message.substring(colon4 + 1);
      gActiveMessage.deviceName = deviceName;
      gActiveMessage.receivedChunks = 0;
      gActiveMessage.startTime = millis();
      gActiveMessage.active = true;
      
      // Clear chunk array
      for (int i = 0; i < MAX_CHUNKS; i++) {
        gActiveMessage.chunks[i] = "";
      }
      
      broadcastOutput("[ESP-NOW] Starting chunked message from " + deviceName + " (" + String(gActiveMessage.totalChunks) + " chunks expected)");
    }
    
  } else if (message.startsWith("RESULT_CHUNK:") && gActiveMessage.active) {
    // Parse: RESULT_CHUNK:1:chunk_data
    int colon1 = message.indexOf(':', 13); // After "RESULT_CHUNK:"
    int colon2 = message.indexOf(':', colon1 + 1);
    
    if (colon1 > 0 && colon2 > 0) {
      int chunkNum = message.substring(13, colon1).toInt();
      String chunkData = message.substring(colon2 + 1);
      
      if (chunkNum >= 1 && chunkNum <= MAX_CHUNKS) {
        gActiveMessage.chunks[chunkNum - 1] = chunkData; // Convert to 0-based index
        gActiveMessage.receivedChunks++;
        
        broadcastOutput("[ESP-NOW] Received chunk " + String(chunkNum) + "/" + String(gActiveMessage.totalChunks) + " from " + deviceName);
      }
    }
    
  } else if (message.startsWith("RESULT_END:") && gActiveMessage.active) {
    // Parse: RESULT_END:ABC123
    String endHash = message.substring(11);
    
    if (endHash == gActiveMessage.hash) {
      // Assemble all chunks and display
      String fullResult = "";
      for (int i = 0; i < gActiveMessage.totalChunks && i < MAX_CHUNKS; i++) {
        fullResult += gActiveMessage.chunks[i];
      }
      
      broadcastOutput("[ESP-NOW] Remote result from " + gActiveMessage.deviceName + " (" + gActiveMessage.status + "):");
      broadcastOutput(fullResult);
      
      if (gActiveMessage.receivedChunks < gActiveMessage.totalChunks) {
        broadcastOutput("[ESP-NOW] Warning: Missing " + String(gActiveMessage.totalChunks - gActiveMessage.receivedChunks) + " chunks");
      }
      
      // Cleanup
      gActiveMessage.active = false;
      for (int i = 0; i < MAX_CHUNKS; i++) {
        gActiveMessage.chunks[i] = "";
      }
    }
  }
}

// Handle ESP-NOW remote command execution
static void handleEspNowRemoteCommand(const String& message, const uint8_t* senderMac) {
  // Parse format: REMOTE:username:password:command
  if (!message.startsWith("REMOTE:")) return;
  
  // Find colons: REMOTE:username:password:command
  //                    ^7      ^first   ^second (command starts after second colon)
  int firstColon = message.indexOf(':', 7);   // First colon after "REMOTE:"
  int secondColon = message.indexOf(':', firstColon + 1);
  
  if (firstColon < 0 || secondColon < 0) {
    broadcastOutput("[ESP-NOW] Remote command: Invalid format - need format REMOTE:user:pass:command");
    return;
  }
  
  String username = message.substring(7, firstColon);
  String password = message.substring(firstColon + 1, secondColon);
  String command = message.substring(secondColon + 1);
  
  // Get sender device name for logging
  String senderName = getEspNowDeviceName(senderMac);
  if (senderName.length() == 0) {
    senderName = formatMacAddress(senderMac);
  }
  
  broadcastOutput("[ESP-NOW] Remote command from " + senderName + ": user='" + username + "' cmd='" + command + "'");
  
  // Authenticate user with existing system
  if (!isValidUser(username, password)) {
    broadcastOutput("[ESP-NOW] Remote command: Authentication failed for user '" + username + "'");
    // TODO: Send failure response back to sender
    return;
  }
  
  broadcastOutput("[ESP-NOW] Remote command: Authentication successful for user '" + username + "'");
  
  // Execute the command and capture result
  AuthContext authCtx;
  authCtx.transport = AUTH_SYSTEM;
  authCtx.user = username;  // Use the authenticated user
  authCtx.ip = String();
  authCtx.path = "/espnow-remote";
  authCtx.opaque = nullptr;
  
  String result;
  bool success = executeCommand(authCtx, command, result);
  
  // Log the execution result
  if (success) {
    broadcastOutput("[ESP-NOW] Remote command executed: " + (result.length() > 100 ? result.substring(0, 100) + "..." : result));
  } else {
    broadcastOutput("[ESP-NOW] Remote command failed: " + (result.length() > 100 ? result.substring(0, 100) + "..." : result));
  }
  
  // Send result back to sender device using chunked transmission
  sendChunkedResponse(senderMac, success, result, senderName);
}

// ESP-NOW callback for receiving data
static void onEspNowDataReceived(const esp_now_recv_info *recv_info, const uint8_t *incomingData, int len) {
  String macStr = "";
  for (int i = 0; i < 6; i++) {
    if (i > 0) macStr += ":";
    if (recv_info->src_addr[i] < 16) macStr += "0";
    macStr += String(recv_info->src_addr[i], HEX);
  }
  
  // Check if this device is encrypted
  bool isEncrypted = false;
  String deviceName = "";
  String expectedKey = "";
  for (int i = 0; i < gEspNowDeviceCount; i++) {
    if (memcmp(gEspNowDevices[i].mac, recv_info->src_addr, 6) == 0) {
      isEncrypted = gEspNowDevices[i].encrypted;
      deviceName = gEspNowDevices[i].name;
      
      // Show expected key for this device
      for (int j = 0; j < 16; j++) {
        if (gEspNowDevices[i].key[j] < 16) expectedKey += "0";
        expectedKey += String(gEspNowDevices[i].key[j], HEX);
      }
      break;
    }
  }
  
  // Check encryption status for display
  String encStatus = isEncrypted ? " [ENCRYPTED]" : " [UNENCRYPTED]";
  
  String message = "";
  for (int i = 0; i < len && i < 250; i++) {
    message += (char)incomingData[i];
  }
  
  // DEBUG: Show comprehensive receive info
  broadcastOutput("[ESP-NOW] DEBUG MESSAGE RECEIVED:");
  broadcastOutput("  From MAC: " + macStr);
  broadcastOutput("  Device Name: " + (deviceName.length() > 0 ? deviceName : "UNKNOWN"));
  broadcastOutput("  Expected Encrypted: " + String(isEncrypted ? "YES" : "NO"));
  if (isEncrypted) {
    broadcastOutput("  Expected Key: " + expectedKey);
  }
  broadcastOutput("  Message Length: " + String(len));
  broadcastOutput("  Raw Message: '" + message + "'");
  
  // Check if this is a remote command
  if (message.startsWith("REMOTE:")) {
    handleEspNowRemoteCommand(message, recv_info->src_addr);
    return; // Don't show remote commands as regular messages
  }
  
  // Check if this is a remote command result (single or chunked)
  if (message.startsWith("RESULT:") || message.startsWith("RESULT_START:") || 
      message.startsWith("RESULT_CHUNK:") || message.startsWith("RESULT_END:")) {
    
    String deviceName = getEspNowDeviceName(recv_info->src_addr);
    if (deviceName.length() == 0) {
      deviceName = formatMacAddress(recv_info->src_addr);
    }
    
    // Handle chunked messages
    if (message.startsWith("RESULT_START:") || message.startsWith("RESULT_CHUNK:") || message.startsWith("RESULT_END:")) {
      handleChunkedMessage(message, deviceName);
      return;
    }
    
    // Handle legacy single RESULT: messages (backwards compatibility)
    if (message.startsWith("RESULT:")) {
      // Parse: RESULT:SUCCESS/FAILED:output
      int firstColon = message.indexOf(':', 7);
      if (firstColon > 0) {
        String status = message.substring(7, firstColon);
        String output = message.substring(firstColon + 1);
        
        broadcastOutput("[ESP-NOW] Remote result from " + deviceName + " (" + status + "):");
        broadcastOutput(output);
      } else {
        broadcastOutput("[ESP-NOW] Remote result from " + deviceName + ": " + message.substring(7));
      }
      return;
    }
  }
  
  // Display the received message (format compatible with web interface parser)
  if (deviceName.length() > 0) {
    broadcastOutput("[ESP-NOW] Received from " + deviceName + ": " + message + encStatus);
  } else {
    broadcastOutput("[ESP-NOW] Received from " + macStr + ": " + message + encStatus);
  }
}

// ESP-NOW callback for send status
static void onEspNowDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  String macStr = "";
  for (int i = 0; i < 6; i++) {
    if (i > 0) macStr += ":";
    macStr += String(mac_addr[i], HEX);
  }
  
  String statusStr = (status == ESP_NOW_SEND_SUCCESS) ? "Success" : "Failed";
  
  // Check if we have a friendly name for this device
  String deviceName = getEspNowDeviceName(mac_addr);
  if (deviceName.length() > 0) {
    broadcastOutput("[ESP-NOW] Send to " + deviceName + ": " + statusStr);
  } else {
    broadcastOutput("[ESP-NOW] Send to " + macStr + ": " + statusStr);
  }
}

// Initialize ESP-NOW
static bool initEspNow() {
  if (gEspNowInitialized) return true;
  
  // Set WiFi mode to STA+AP to enable ESP-NOW
  WiFi.mode(WIFI_AP_STA);
  
  // Get current WiFi channel and use it for ESP-NOW
  wifi_config_t conf;
  esp_wifi_get_config(WIFI_IF_STA, &conf);
  gEspNowChannel = conf.sta.channel;
  if (gEspNowChannel == 0) {
    // Fallback: get channel from WiFi status
    gEspNowChannel = WiFi.channel();
  }
  if (gEspNowChannel == 0) {
    gEspNowChannel = 1; // Final fallback
  }
  
  // Initialize ESP-NOW
  if (esp_now_init() != ESP_OK) {
    broadcastOutput("[ESP-NOW] Failed to initialize ESP-NOW");
    return false;
  }
  
  // Register callbacks
  esp_now_register_recv_cb(onEspNowDataReceived);
  esp_now_register_send_cb(onEspNowDataSent);
  
  gEspNowInitialized = true;
  broadcastOutput("[ESP-NOW] Initialized successfully on channel " + String(gEspNowChannel));
  
  // Load and restore saved devices
  loadEspNowDevices();
  restoreEspNowPeers();
  
  return true;
}

// ESP-NOW init command
static String cmd_espnow_init_core() {
  if (gEspNowInitialized) {
    return "ESP-NOW already initialized";
  }
  
  if (initEspNow()) {
    return "ESP-NOW initialized successfully";
  } else {
    return "Failed to initialize ESP-NOW";
  }
}

static String cmd_espnow_init_modern(const String& cmd) {
  RETURN_VALID_IF_VALIDATE();
  return cmd_espnow_init_core();
}

// Helper: Parse MAC address from string (flexible format)
static bool parseMacAddress(const String& macStr, uint8_t mac[6]) {
  String cleanMac = macStr;
  cleanMac.toUpperCase();
  
  // Handle different separators
  cleanMac.replace("-", ":");
  cleanMac.replace(" ", ":");
  
  // Split by colons and parse each byte
  int byteIndex = 0;
  int startPos = 0;
  
  for (int i = 0; i <= cleanMac.length() && byteIndex < 6; i++) {
    if (i == cleanMac.length() || cleanMac[i] == ':') {
      if (byteIndex >= 6) return false;
      
      String byteStr = cleanMac.substring(startPos, i);
      byteStr.trim();
      
      if (byteStr.length() == 0 || byteStr.length() > 2) return false;
      
      char* endPtr;
      long val = strtol(byteStr.c_str(), &endPtr, 16);
      if (*endPtr != '\0' || val < 0 || val > 255) return false;
      
      mac[byteIndex] = (uint8_t)val;
      byteIndex++;
      startPos = i + 1;
    }
  }
  
  return (byteIndex == 6);
}

// Helper: Format MAC address as string
static String formatMacAddress(const uint8_t mac[6]) {
  String result = "";
  for (int i = 0; i < 6; i++) {
    if (i > 0) result += ":";
    if (mac[i] < 16) result += "0";
    result += String(mac[i], HEX);
  }
  result.toUpperCase();
  return result;
}

// ESP-NOW pair device command
static String cmd_espnow_pair_core(const String& originalCmd) {
  if (!gEspNowInitialized) {
    return "ESP-NOW not initialized. Run 'espnow init' first.";
  }
  
  // Parse: espnow pair <mac> <name>
  String cmd = originalCmd;
  cmd.trim();
  
  int firstSpace = cmd.indexOf(' ', 8); // after "espnow pair"
  if (firstSpace < 0) return "Usage: espnow pair <mac> <name>";
  
  int secondSpace = cmd.indexOf(' ', firstSpace + 1);
  if (secondSpace < 0) return "Usage: espnow pair <mac> <name>";
  
  String macStr = cmd.substring(firstSpace + 1, secondSpace);
  String name = cmd.substring(secondSpace + 1);
  macStr.trim();
  name.trim();
  
  if (macStr.length() == 0 || name.length() == 0) {
    return "Usage: espnow pair <mac> <name>";
  }
  
  // Parse MAC address
  uint8_t mac[6];
  if (!parseMacAddress(macStr, mac)) {
    return "Invalid MAC address format. Use AA:BB:CC:DD:EE:FF";
  }
  
  // Check if device already exists
  for (int i = 0; i < gEspNowDeviceCount; i++) {
    if (memcmp(gEspNowDevices[i].mac, mac, 6) == 0) {
      return "Device already paired. Use 'espnow unpair " + macStr + "' first.";
    }
  }
  
  // Check device limit
  if (gEspNowDeviceCount >= 16) {
    return "Maximum number of devices (16) already paired.";
  }
  
  // Add unencrypted peer to ESP-NOW
  if (!addEspNowPeerWithEncryption(mac, false, nullptr)) {
    return "Failed to add unencrypted peer to ESP-NOW.";
  }
  
  // Store device info without encryption
  memcpy(gEspNowDevices[gEspNowDeviceCount].mac, mac, 6);
  gEspNowDevices[gEspNowDeviceCount].name = name;
  gEspNowDevices[gEspNowDeviceCount].encrypted = false;
  memset(gEspNowDevices[gEspNowDeviceCount].key, 0, 16);
  gEspNowDeviceCount++;
  
  // Save devices to filesystem
  saveEspNowDevices();
  
  return "Unencrypted device paired successfully: " + name + " (" + macStr + ")";
}

static String cmd_espnow_pair_modern(const String& cmd) {
  RETURN_VALID_IF_VALIDATE();
  return cmd_espnow_pair_core(cmd);
}

// ESP-NOW send message command
static String cmd_espnow_send_core(const String& originalCmd) {
  if (!gEspNowInitialized) {
    return "ESP-NOW not initialized. Run 'espnow init' first.";
  }
  
  // Parse: espnow send <mac> <message>
  String cmd = originalCmd;
  cmd.trim();
  
  int firstSpace = cmd.indexOf(' ', 8); // after "espnow send"
  if (firstSpace < 0) return "Usage: espnow send <mac> <message>";
  
  int secondSpace = cmd.indexOf(' ', firstSpace + 1);
  if (secondSpace < 0) return "Usage: espnow send <mac> <message>";
  
  String macStr = cmd.substring(firstSpace + 1, secondSpace);
  String message = cmd.substring(secondSpace + 1);
  macStr.trim();
  message.trim();
  
  if (macStr.length() == 0 || message.length() == 0) {
    return "Usage: espnow send <mac> <message>";
  }
  
  // Parse MAC address
  uint8_t mac[6];
  if (!parseMacAddress(macStr, mac)) {
    return "Invalid MAC address format. Use AA:BB:CC:DD:EE:FF";
  }
  
  // Check if this device is encrypted
  bool isEncrypted = false;
  String deviceName = "";
  for (int i = 0; i < gEspNowDeviceCount; i++) {
    if (memcmp(gEspNowDevices[i].mac, mac, 6) == 0) {
      isEncrypted = gEspNowDevices[i].encrypted;
      deviceName = gEspNowDevices[i].name;
      break;
    }
  }
  
  // DEBUG: Show comprehensive send info
  String encStatus = isEncrypted ? " [ENCRYPTED]" : " [UNENCRYPTED]";
  String keyStr = "";
  if (isEncrypted) {
    for (int i = 0; i < 16; i++) {
      if (gEspNowDerivedKey[i] < 16) keyStr += "0";
      keyStr += String(gEspNowDerivedKey[i], HEX);
    }
  }
  
  broadcastOutput("[ESP-NOW] DEBUG MESSAGE SENDING:");
  broadcastOutput("  To MAC: " + formatMacAddress(mac));
  broadcastOutput("  Device Name: " + (deviceName.length() > 0 ? deviceName : "UNKNOWN"));
  broadcastOutput("  Using Encryption: " + String(isEncrypted ? "YES" : "NO"));
  if (isEncrypted) {
    broadcastOutput("  Sending Key: " + keyStr);
  }
  broadcastOutput("  Message Length: " + String(message.length()));
  broadcastOutput("  Message Content: '" + message + "'");
  
  // Send message
  esp_err_t result = esp_now_send(mac, (uint8_t*)message.c_str(), message.length());
  if (result != ESP_OK) {
    return "Failed to send message: " + String(result);
  }
  
  return "Message sent to " + formatMacAddress(mac);
}

static String cmd_espnow_send_modern(const String& cmd) {
  RETURN_VALID_IF_VALIDATE();
  return cmd_espnow_send_core(cmd);
}

// ESP-NOW list paired devices command
static String cmd_espnow_list_core() {
  if (!gEspNowInitialized) {
    return "ESP-NOW not initialized. Run 'espnow init' first.";
  }
  
  esp_now_peer_info_t peer;
  esp_now_peer_info_t *from_head = NULL;
  int count = 0;
  String result = "Paired ESP-NOW Devices:\n";
  
  // Get first peer
  esp_err_t ret = esp_now_fetch_peer(true, &peer);
  while (ret == ESP_OK) {
    String macStr = formatMacAddress(peer.peer_addr);
    String deviceName = getEspNowDeviceName(peer.peer_addr);
    
    // Find encryption status for this device
    bool isEncrypted = false;
    for (int i = 0; i < gEspNowDeviceCount; i++) {
      if (memcmp(gEspNowDevices[i].mac, peer.peer_addr, 6) == 0) {
        isEncrypted = gEspNowDevices[i].encrypted;
        break;
      }
    }
    
    String encStatus = isEncrypted ? " [ENCRYPTED]" : " [UNENCRYPTED]";
    
    if (deviceName.length() > 0) {
      result += "  " + deviceName + " (" + macStr + ") Channel: " + String(peer.channel) + encStatus + "\n";
    } else {
      result += "  " + macStr + " (Channel: " + String(peer.channel) + ")" + encStatus + "\n";
    }
    count++;
    
    // Get next peer
    ret = esp_now_fetch_peer(false, &peer);
  }
  
  if (count == 0) {
    result += "  No devices paired\n";
  } else {
    result += "Total: " + String(count) + " device(s)\n";
  }
  
  return result;
}

static String cmd_espnow_list_modern(const String& cmd) {
  RETURN_VALID_IF_VALIDATE();
  return cmd_espnow_list_core();
}

// ESP-NOW unpair device command
static String cmd_espnow_unpair_core(const String& originalCmd) {
  if (!gEspNowInitialized) {
    return "ESP-NOW not initialized. Run 'espnow init' first.";
  }
  
  // Parse: espnow unpair <mac>
  String cmd = originalCmd;
  cmd.trim();
  
  int firstSpace = cmd.indexOf(' ', 10); // after "espnow unpair"
  if (firstSpace < 0) return "Usage: espnow unpair <mac>";
  
  String macStr = cmd.substring(firstSpace + 1);
  macStr.trim();
  
  if (macStr.length() == 0) {
    return "Usage: espnow unpair <mac>";
  }
  
  // Parse MAC address
  uint8_t mac[6];
  if (!parseMacAddress(macStr, mac)) {
    return "Invalid MAC address format. Use AA:BB:CC:DD:EE:FF";
  }
  
  // Get device name before removing
  String deviceName = getEspNowDeviceName(mac);
  
  // Remove peer from ESP-NOW
  esp_err_t result = esp_now_del_peer(mac);
  if (result != ESP_OK) {
    return "Failed to unpair device: " + String(result);
  }
  
  // Remove device name mapping
  removeEspNowDevice(mac);
  
  // Save devices to filesystem
  saveEspNowDevices();
  
  if (deviceName.length() > 0) {
    return "Unpaired device: " + deviceName + " (" + formatMacAddress(mac) + ")";
  } else {
    return "Unpaired device: " + formatMacAddress(mac);
  }
}

static String cmd_espnow_unpair_modern(const String& cmd) {
  RETURN_VALID_IF_VALIDATE();
  return cmd_espnow_unpair_core(cmd);
}

// ESP-NOW broadcast message command
static String cmd_espnow_broadcast_core(const String& originalCmd) {
  if (!gEspNowInitialized) {
    return "ESP-NOW not initialized. Run 'espnow init' first.";
  }
  
  // Parse: espnow broadcast <message>
  String cmd = originalCmd;
  cmd.trim();
  
  int firstSpace = cmd.indexOf(' ', 15); // after "espnow broadcast"
  if (firstSpace < 0) return "Usage: espnow broadcast <message>";
  
  String message = cmd.substring(firstSpace + 1);
  message.trim();
  
  if (message.length() == 0) {
    return "Usage: espnow broadcast <message>";
  }
  
  // Send to all paired devices
  esp_now_peer_info_t peer;
  int sent = 0;
  int failed = 0;
  
  // Get first peer
  esp_err_t ret = esp_now_fetch_peer(true, &peer);
  while (ret == ESP_OK) {
    esp_err_t sendResult = esp_now_send(peer.peer_addr, (uint8_t*)message.c_str(), message.length());
    if (sendResult == ESP_OK) {
      sent++;
    } else {
      failed++;
    }
    
    // Get next peer
    ret = esp_now_fetch_peer(false, &peer);
  }
  
  if (sent == 0 && failed == 0) {
    return "No paired devices to broadcast to";
  }
  
  return "Broadcast sent to " + String(sent) + " device(s)" + 
         (failed > 0 ? " (" + String(failed) + " failed)" : "");
}

static String cmd_espnow_broadcast_modern(const String& cmd) {
  RETURN_VALID_IF_VALIDATE();
  return cmd_espnow_broadcast_core(cmd);
}

// ESP-NOW remote command execution
// ESP-NOW set passphrase command
static String cmd_espnow_setpassphrase_core(const String& originalCmd) {
  if (!gEspNowInitialized) {
    return "ESP-NOW not initialized. Run 'espnow init' first.";
  }
  
  // Parse: espnow setpassphrase "MySecretPhrase123"
  // Find the space after "setpassphrase"
  int passphraseStart = originalCmd.indexOf("setpassphrase");
  if (passphraseStart < 0) {
    return "Usage: espnow setpassphrase \"your_passphrase_here\"\n"
           "       espnow setpassphrase clear";
  }
  
  passphraseStart += 13; // Length of "setpassphrase"
  
  // Skip any spaces after "setpassphrase"
  while (passphraseStart < originalCmd.length() && originalCmd.charAt(passphraseStart) == ' ') {
    passphraseStart++;
  }
  
  if (passphraseStart >= originalCmd.length()) {
    return "Usage: espnow setpassphrase \"your_passphrase_here\"\n"
           "       espnow setpassphrase clear";
  }
  
  String passphrase = originalCmd.substring(passphraseStart);
  passphrase.trim();
  
  // Handle clear command
  if (passphrase == "clear") {
    setEspNowPassphrase("");
    return "ESP-NOW encryption disabled. All future pairings will be unencrypted.";
  }
  
  // Remove quotes if present
  if (passphrase.startsWith("\"") && passphrase.endsWith("\"")) {
    passphrase = passphrase.substring(1, passphrase.length() - 1);
  }
  
  if (passphrase.length() < 8) {
    return "Error: Passphrase must be at least 8 characters long.";
  }
  
  if (passphrase.length() > 128) {
    return "Error: Passphrase must be 128 characters or less.";
  }
  
  setEspNowPassphrase(passphrase);
  return "ESP-NOW encryption passphrase set. Use 'espnow pairsecure' to pair with encryption.\n"
         "Key derived from: " + passphrase.substring(0, 3) + "..." + passphrase.substring(passphrase.length() - 3);
}

// ESP-NOW show encryption status command
static String cmd_espnow_encstatus_core() {
  if (!gEspNowInitialized) {
    return "ESP-NOW not initialized. Run 'espnow init' first.";
  }
  
  String result = "ESP-NOW Encryption Status:\n";
  result += "  Encryption Enabled: " + String(gEspNowEncryptionEnabled ? "Yes" : "No") + "\n";
  
  if (gEspNowEncryptionEnabled) {
    result += "  Passphrase Set: " + String(gEspNowPassphrase.length() > 0 ? "Yes" : "No") + "\n";
    if (gEspNowPassphrase.length() > 0) {
      result += "  Passphrase Hint: " + gEspNowPassphrase.substring(0, 3) + "..." + 
                gEspNowPassphrase.substring(gEspNowPassphrase.length() - 3) + "\n";
    }
    
    // Show key fingerprint (first 4 bytes in hex)
    result += "  Key Fingerprint: ";
    for (int i = 0; i < 4; i++) {
      if (gEspNowDerivedKey[i] < 16) result += "0";
      result += String(gEspNowDerivedKey[i], HEX);
    }
    result += "...\n";
  }
  
  return result;
}

// ESP-NOW secure pairing command
static String cmd_espnow_pairsecure_core(const String& originalCmd) {
  if (!gEspNowInitialized) {
    return "ESP-NOW not initialized. Run 'espnow init' first.";
  }
  
  if (!gEspNowEncryptionEnabled) {
    return "Encryption not enabled. Run 'espnow setpassphrase \"your_phrase\"' first.";
  }
  
  // Parse: espnow pairsecure AA:BB:CC:DD:EE:FF devicename
  // Find the start of arguments after "pairsecure"
  int pairsecureStart = originalCmd.indexOf("pairsecure");
  if (pairsecureStart < 0) {
    return "Usage: espnow pairsecure <mac_address> <device_name>";
  }
  
  pairsecureStart += 10; // Length of "pairsecure"
  
  // Skip spaces after "pairsecure"
  while (pairsecureStart < originalCmd.length() && originalCmd.charAt(pairsecureStart) == ' ') {
    pairsecureStart++;
  }
  
  if (pairsecureStart >= originalCmd.length()) {
    return "Usage: espnow pairsecure <mac_address> <device_name>";
  }
  
  // Get the rest of the command
  String args = originalCmd.substring(pairsecureStart);
  args.trim();
  
  // Find the space between MAC and device name
  int spacePos = args.indexOf(' ');
  if (spacePos < 0) {
    return "Usage: espnow pairsecure <mac_address> <device_name>";
  }
  
  String macStr = args.substring(0, spacePos);
  String deviceName = args.substring(spacePos + 1);
  macStr.trim();
  deviceName.trim();
  
  if (macStr.length() == 0 || deviceName.length() == 0) {
    return "Usage: espnow pairsecure <mac_address> <device_name>";
  }
  
  // Parse MAC address
  uint8_t mac[6];
  if (!parseMacAddress(macStr, mac)) {
    return "Invalid MAC address format. Use AA:BB:CC:DD:EE:FF";
  }
  
  // Check if device already exists
  for (int i = 0; i < gEspNowDeviceCount; i++) {
    if (memcmp(gEspNowDevices[i].mac, mac, 6) == 0) {
      return "Device already paired. Use 'espnow unpair " + macStr + "' first.";
    }
  }
  
  // Check device limit
  if (gEspNowDeviceCount >= 16) {
    return "Maximum number of devices (16) already paired.";
  }
  
  // Add encrypted peer to ESP-NOW
  if (!addEspNowPeerWithEncryption(mac, true, gEspNowDerivedKey)) {
    return "Failed to add encrypted peer to ESP-NOW.";
  }
  
  // Store device info with encryption
  memcpy(gEspNowDevices[gEspNowDeviceCount].mac, mac, 6);
  gEspNowDevices[gEspNowDeviceCount].name = deviceName;
  gEspNowDevices[gEspNowDeviceCount].encrypted = true;
  memcpy(gEspNowDevices[gEspNowDeviceCount].key, gEspNowDerivedKey, 16);
  gEspNowDeviceCount++;
  
  // Save to persistent storage
  saveEspNowDevices();
  
  return "Encrypted device paired successfully: " + deviceName + " (" + macStr + ")\n" +
         "Key fingerprint: " + String(gEspNowDerivedKey[0], HEX) + String(gEspNowDerivedKey[1], HEX) + 
         String(gEspNowDerivedKey[2], HEX) + String(gEspNowDerivedKey[3], HEX) + "...";
}

static String cmd_espnow_setpassphrase_modern(const String& cmd) {
  RETURN_VALID_IF_VALIDATE();
  return cmd_espnow_setpassphrase_core(cmd);
}

static String cmd_espnow_encstatus_modern(const String& cmd) {
  RETURN_VALID_IF_VALIDATE();
  return cmd_espnow_encstatus_core();
}

static String cmd_espnow_pairsecure_modern(const String& cmd) {
  RETURN_VALID_IF_VALIDATE();
  return cmd_espnow_pairsecure_core(cmd);
}

static String cmd_espnow_remote_core(const String& originalCmd) {
  if (!gEspNowInitialized) {
    return "ESP-NOW not initialized. Run 'espnow init' first.";
  }
  
  // Parse: espnow remote <target> <username> <password> <command>
  String cmd = originalCmd;
  cmd.trim();
  
  int firstSpace = cmd.indexOf(' ', 13); // after "espnow remote"
  if (firstSpace < 0) return "Usage: espnow remote <target> <username> <password> <command>";
  
  int secondSpace = cmd.indexOf(' ', firstSpace + 1);
  if (secondSpace < 0) return "Usage: espnow remote <target> <username> <password> <command>";
  
  int thirdSpace = cmd.indexOf(' ', secondSpace + 1);
  if (thirdSpace < 0) return "Usage: espnow remote <target> <username> <password> <command>";
  
  int fourthSpace = cmd.indexOf(' ', thirdSpace + 1);
  if (fourthSpace < 0) return "Usage: espnow remote <target> <username> <password> <command>";
  
  String target = cmd.substring(firstSpace + 1, secondSpace);
  String username = cmd.substring(secondSpace + 1, thirdSpace);
  String password = cmd.substring(thirdSpace + 1, fourthSpace);
  String command = cmd.substring(fourthSpace + 1);
  
  target.trim();
  username.trim();
  password.trim();
  command.trim();
  
  if (target.length() == 0 || username.length() == 0 || password.length() == 0 || command.length() == 0) {
    return "Usage: espnow remote <target> <username> <password> <command>";
  }
  
  // Format remote command message
  String remoteMessage = "REMOTE:" + username + ":" + password + ":" + command;
  
  // Find target device by name or MAC
  uint8_t targetMac[6];
  bool found = false;
  
  // First try to find by device name
  for (int i = 0; i < gEspNowDeviceCount; i++) {
    if (gEspNowDevices[i].name.equalsIgnoreCase(target)) {
      memcpy(targetMac, gEspNowDevices[i].mac, 6);
      found = true;
      break;
    }
  }
  
  // If not found by name, try to parse as MAC address
  if (!found && parseMacAddress(target, targetMac)) {
    found = true;
  }
  
  if (!found) {
    return "Target device '" + target + "' not found. Use device name or MAC address.";
  }
  
  // Send remote command
  esp_err_t result = esp_now_send(targetMac, (uint8_t*)remoteMessage.c_str(), remoteMessage.length());
  if (result != ESP_OK) {
    return "Failed to send remote command: " + String(result);
  }
  
  return "Remote command sent to " + target + ": " + command;
}

static String cmd_espnow_remote_modern(const String& cmd) {
  RETURN_VALID_IF_VALIDATE();
  return cmd_espnow_remote_core(cmd);
}

static String cmd_memory_core() {
  String result = "Memory Usage:\n"
                  "  Free Heap: "
                  + String(ESP.getFreeHeap()) + " bytes\n"
                                                "  Total Heap: "
                  + String(ESP.getHeapSize()) + " bytes\n"
                                                "  Used Heap: "
                  + String(ESP.getHeapSize() - ESP.getFreeHeap()) + " bytes\n"
                                                                    "  Free PSRAM: "
                  + String(ESP.getFreePsram()) + " bytes";
  return result;
}

static String cmd_psram_core() {
  size_t totalPs = ESP.getPsramSize();
  size_t freePs = ESP.getFreePsram();
  size_t usedPs = (totalPs > 0 && freePs <= totalPs) ? (totalPs - freePs) : 0;
  String out = "PSRAM:\n";
  out += String("  Supported: ") + (totalPs > 0 ? "Yes" : "No") + "\n";
  out += "  Total: " + String(totalPs) + " bytes\n";
  out += "  Free:  " + String(freePs) + " bytes\n";
  out += "  Used:  " + String(usedPs) + " bytes";
  return out;
}

static String cmd_fsusage_core() {
  if (!filesystemReady) return "Error: LittleFS not ready";
  size_t totalBytes = LittleFS.totalBytes();
  size_t usedBytes = LittleFS.usedBytes();
  return "Filesystem Usage:\n"
         "  Total: "
         + String(totalBytes) + " bytes\n"
                                "  Used:  "
         + String(usedBytes) + " bytes\n"
                               "  Free:  "
         + String(totalBytes - usedBytes) + " bytes\n"
                                            "  Usage: "
         + String((usedBytes * 100) / (totalBytes == 0 ? 1 : totalBytes)) + "%";
}

// Modern command handlers for core commands
static String cmd_memory_modern(const String& cmd) {
  RETURN_VALID_IF_VALIDATE();
  return cmd_memory_core();
}

static String cmd_psram_modern(const String& cmd) {
  RETURN_VALID_IF_VALIDATE();
  return cmd_psram_core();
}

static String cmd_fsusage_modern(const String& cmd) {
  RETURN_VALID_IF_VALIDATE();
  return cmd_fsusage_core();
}

// Modern WiFi command handlers
static String cmd_wifiinfo_modern(const String& cmd) {
  return cmd_wifiinfo();  // Already has validation
}

static String cmd_wifilist_modern(const String& cmd) {
  return cmd_wifilist();  // Already has validation
}

static String cmd_wifiadd_modern(const String& cmd) {
  return cmd_wifiadd(cmd);  // Already has validation
}

static String cmd_wifirm_modern(const String& cmd) {
  return cmd_wifirm(cmd);  // Already has validation
}

static String cmd_wifipromote_modern(const String& cmd) {
  return cmd_wifipromote(cmd);  // Already has validation
}

static String cmd_wificonnect_modern(const String& cmd) {
  return cmd_wificonnect(cmd);  // Already has validation
}

static String cmd_wifidisconnect_modern(const String& cmd) {
  return cmd_wifidisconnect();  // Already has validation
}

static String cmd_wifiscan_modern(const String& cmd) {
  return cmd_wifiscan(cmd);  // Already has validation
}

// Modern sensor command handlers
static String cmd_tof_modern(const String& cmd) {
  return cmd_tof();  // Already has validation
}

static String cmd_tofstart_modern(const String& cmd) {
  return cmd_tofstart();  // Already has validation
}

static String cmd_tofstop_modern(const String& cmd) {
  return cmd_tofstop();  // Already has validation
}

static String cmd_thermalstart_modern(const String& cmd) {
  return cmd_thermalstart();  // Already has validation
}

static String cmd_thermalstop_modern(const String& cmd) {
  return cmd_thermalstop();  // Already has validation
}

static String cmd_imu_modern(const String& cmd) {
  return cmd_imu();  // Already has validation
}

static String cmd_imustart_modern(const String& cmd) {
  return cmd_imustart();  // Already has validation
}

static String cmd_imustop_modern(const String& cmd) {
  return cmd_imustop();  // Already has validation
}

// Modern APDS sensor command handlers
static String cmd_apdscolor_modern(const String& cmd) {
  return cmd_apdscolor();  // Already has validation
}

static String cmd_apdsproximity_modern(const String& cmd) {
  return cmd_apdsproximity();  // Already has validation
}

static String cmd_apdsgesture_modern(const String& cmd) {
  return cmd_apdsgesture();  // Already has validation
}

static String cmd_apdscolorstart_modern(const String& cmd) {
  return cmd_apdscolorstart();  // Already has validation
}

static String cmd_apdscolorstop_modern(const String& cmd) {
  return cmd_apdscolorstop();  // Already has validation
}

static String cmd_apdsproximitystart_modern(const String& cmd) {
  return cmd_apdsproximitystart();  // Already has validation
}

static String cmd_apdsproximitystop_modern(const String& cmd) {
  return cmd_apdsproximitystop();  // Already has validation
}

static String cmd_apdsgesturestart_modern(const String& cmd) {
  return cmd_apdsgesturestart();  // Already has validation
}

static String cmd_apdsgesturestop_modern(const String& cmd) {
  return cmd_apdsgesturestop();  // Already has validation
}

// Modern LED command handlers
static String cmd_ledclear_modern(const String& cmd) {
  return cmd_ledclear();  // Already has validation
}

static String cmd_ledcolor_modern(const String& cmd) {
  return cmd_ledcolor(cmd);  // Already has validation
}

static String cmd_ledeffect_modern(const String& cmd) {
  return cmd_ledeffect(cmd);  // Already has validation
}

// Modern file system command handlers
static String cmd_files_modern(const String& cmd) {
  return cmd_files(cmd);  // Already has validation
}

static String cmd_mkdir_modern(const String& cmd) {
  return cmd_mkdir(cmd);  // Already has validation
}

static String cmd_rmdir_modern(const String& cmd) {
  return cmd_rmdir(cmd);  // Already has validation
}

static String cmd_filecreate_modern(const String& cmd) {
  return cmd_filecreate(cmd);  // Already has validation
}

static String cmd_fileview_modern(const String& cmd) {
  return cmd_fileview(cmd);  // Already has validation
}

static String cmd_filedelete_modern(const String& cmd) {
  return cmd_filedelete(cmd);  // Already has validation
}

// Modern automation and logging command handlers
static String cmd_autolog_modern(const String& cmd) {
  return cmd_autolog(cmd);  // Already has validation
}

static String cmd_automation_modern(const String& cmd) {
  return cmd_automation(cmd);  // Already has validation
}

static String cmd_downloadautomation_modern(const String& cmd) {
  return cmd_downloadautomation(cmd);  // Already has validation
}

// Modern user management command handlers
static String cmd_user_approve_modern(const String& cmd) {
  return cmd_user_approve(cmd);  // Already has validation
}

static String cmd_user_deny_modern(const String& cmd) {
  return cmd_user_deny(cmd);  // Already has validation
}

static String cmd_user_promote_modern(const String& cmd) {
  return cmd_user_promote(cmd);  // Already has validation
}

static String cmd_user_demote_modern(const String& cmd) {
  return cmd_user_demote(cmd);  // Already has validation
}

static String cmd_user_delete_modern(const String& cmd) {
  return cmd_user_delete(cmd);  // Already has validation
}

static String cmd_user_list_modern(const String& cmd) {
  return cmd_user_list(cmd);  // Already has validation
}

static String cmd_user_request_modern(const String& cmd) {
  return cmd_user_request(cmd);  // Already has validation
}

static String cmd_session_list_modern(const String& cmd) {
  return cmd_session_list(cmd);  // Already has validation
}

static String cmd_session_revoke_modern(const String& cmd) {
  return cmd_session_revoke(cmd);  // Already has validation
}

// Modern output routing command handlers
static String cmd_outserial_modern(const String& cmd) {
  return cmd_outserial(cmd);  // Already has validation
}

static String cmd_outweb_modern(const String& cmd) {
  return cmd_outweb(cmd);  // Already has validation
}

static String cmd_outtft_modern(const String& cmd) {
  return cmd_outtft(cmd);  // Already has validation
}

// Modern settings command handlers
static String cmd_wifiautoreconnect_modern(const String& cmd) {
  return cmd_wifiautoreconnect(cmd);  // Already has validation
}

static String cmd_clihistorysize_modern(const String& cmd) {
  return cmd_clihistorysize(cmd);  // Already has validation
}

// Modern debug command handlers
static String cmd_debugauthcookies_modern(const String& cmd) {
  return cmd_debugauthcookies(cmd);  // Already has validation
}

static String cmd_debughttp_modern(const String& cmd) {
  return cmd_debughttp(cmd);  // Already has validation
}

static String cmd_debugsse_modern(const String& cmd) {
  return cmd_debugsse(cmd);  // Already has validation
}

static String cmd_debugcli_modern(const String& cmd) {
  return cmd_debugcli(cmd);  // Already has validation
}

static String cmd_debugsensorsframe_modern(const String& cmd) {
  return cmd_debugsensorsframe(cmd);  // Already has validation
}

static String cmd_debugsensorsdata_modern(const String& cmd) {
  return cmd_debugsensorsdata(cmd);  // Already has validation
}

static String cmd_debugsensorsgeneral_modern(const String& cmd) {
  return cmd_debugsensorsgeneral(cmd);  // Already has validation
}

static String cmd_debugwifi_modern(const String& cmd) {
  return cmd_debugwifi(cmd);  // Already has validation
}

static String cmd_debugstorage_modern(const String& cmd) {
  return cmd_debugstorage(cmd);  // Already has validation
}

static String cmd_debugperformance_modern(const String& cmd) {
  return cmd_debugperformance(cmd);  // Already has validation
}

static String cmd_debugdatetime_modern(const String& cmd) {
  return cmd_debugdatetime(cmd);  // Already has validation
}

static String cmd_debugcommandflow_modern(const String& cmd) {
  return cmd_debugcommandflow(cmd);  // Already has validation
}

static String cmd_debugusers_modern(const String& cmd) {
  return cmd_debugusers(cmd);  // Already has validation
}

// Modern thermal/sensor polling command handlers
static String cmd_thermaltargetfps_modern(const String& cmd) {
  return cmd_thermaltargetfps(cmd);  // Already has validation
}

static String cmd_thermalwebmaxfps_modern(const String& cmd) {
  return cmd_thermalwebmaxfps(cmd);  // Already has validation
}

static String cmd_thermalinterpolationenabled_modern(const String& cmd) {
  return cmd_thermalinterpolationenabled(cmd);  // Already has validation
}

static String cmd_thermalinterpolationsteps_modern(const String& cmd) {
  return cmd_thermalinterpolationsteps(cmd);  // Already has validation
}

static String cmd_thermalinterpolationbuffersize_modern(const String& cmd) {
  return cmd_thermalinterpolationbuffersize(cmd);  // Already has validation
}

static String cmd_thermaldevicepollms_modern(const String& cmd) {
  return cmd_thermaldevicepollms(cmd);  // Already has validation
}

static String cmd_tofdevicepollms_modern(const String& cmd) {
  return cmd_tofdevicepollms(cmd);  // Already has validation
}

static String cmd_imudevicepollms_modern(const String& cmd) {
  return cmd_imudevicepollms(cmd);  // Already has validation
}

static String cmd_thermalpollingms_modern(const String& cmd) {
  return cmd_thermalpollingms(cmd);  // Already has validation
}

static String cmd_tofpollingms_modern(const String& cmd) {
  return cmd_tofpollingms(cmd);  // Already has validation
}

static String cmd_tofstabilitythreshold_modern(const String& cmd) {
  return cmd_tofstabilitythreshold(cmd);  // Already has validation
}

static String cmd_i2cclockthermalhz_modern(const String& cmd) {
  return cmd_i2cclockthermalhz(cmd);  // Already has validation
}

static String cmd_i2cclocktofhz_modern(const String& cmd) {
  return cmd_i2cclocktofhz(cmd);  // Already has validation
}

// Modern sensor UI setting handlers (delegate to set command)
static String cmd_thermalpalettedefault_modern(const String& cmd) {
  // Extract value from command like "thermalpalettedefault grayscale"
  int sp = cmd.indexOf(' ');
  if (sp < 0) return "Usage: thermalpalettedefault <grayscale|coolwarm>";
  String value = cmd.substring(sp + 1);
  value.trim();
  return cmd_set("set thermalpalettedefault " + value);
}

static String cmd_thermalewmafactor_modern(const String& cmd) {
  // Extract value from command like "thermalewmafactor 0.2"
  int sp = cmd.indexOf(' ');
  if (sp < 0) return "Usage: thermalewmafactor <0.0..1.0>";
  String value = cmd.substring(sp + 1);
  value.trim();
  return cmd_set("set thermalewmafactor " + value);
}

static String cmd_thermaltransitionms_modern(const String& cmd) {
  // Extract value from command like "thermaltransitionms 80"
  int sp = cmd.indexOf(' ');
  if (sp < 0) return "Usage: thermaltransitionms <0..5000>";
  String value = cmd.substring(sp + 1);
  value.trim();
  return cmd_set("set thermaltransitionms " + value);
}

static String cmd_toftransitionms_modern(const String& cmd) {
  // Extract value from command like "toftransitionms 200"
  int sp = cmd.indexOf(' ');
  if (sp < 0) return "Usage: toftransitionms <0..5000>";
  String value = cmd.substring(sp + 1);
  value.trim();
  return cmd_set("set toftransitionms " + value);
}

static String cmd_tofuimaxdistancemm_modern(const String& cmd) {
  // Extract value from command like "tofuimaxdistancemm 3400"
  int sp = cmd.indexOf(' ');
  if (sp < 0) return "Usage: tofuimaxdistancemm <100..10000>";
  String value = cmd.substring(sp + 1);
  value.trim();
  return cmd_set("set tofuimaxdistancemm " + value);
}

// Modern misc command handlers
static String cmd_set_modern(const String& cmd) {
  return cmd_set(cmd);  // Already has validation
}

static String cmd_reboot_modern(const String& cmd) {
  return cmd_reboot(cmd);  // Already has validation
}

static String cmd_broadcast_modern(const String& cmd) {
  return cmd_broadcast(cmd);  // Already has validation
}

static String cmd_pending_list_modern(const String& cmd) {
  return cmd_pending_list(cmd);  // Already has validation
}

static String cmd_validate_conditions_modern(const String& cmd) {
  String conditions = cmd.substring(20); // Skip "validate-conditions "
  String validationResult = validateConditionalHierarchy(conditions);
  // If we're in validation mode and validation passes, return "VALID"
  // Otherwise return the actual validation result (which could be an error)
  if (gCLIValidateOnly && validationResult == "VALID") {
    return "VALID";
  }
  return validationResult;
}

// Modern special command handlers
static String cmd_clear_modern(const String& cmd) {
  RETURN_VALID_IF_VALIDATE();
  if (!gWebMirror.buf) { gWebMirror.init(gWebMirrorCap); }
  gWebMirror.clear();
  gHiddenHistory = "";  // Also clear hidden history
  return "\033[2J\033[H"
         "CLI history cleared.";
}

static String cmd_stack_modern(const String& cmd) {
  RETURN_VALID_IF_VALIDATE();
  String result = "Task Stack Watermarks (words):\n";
  
  // ToF task watermarks
  result += "ToF Task: current=" + String((unsigned)gToFWatermarkNow) + 
            ", minimum=" + String((unsigned)gToFWatermarkMin) + "\n";
  
  // IMU task watermarks  
  result += "IMU Task: current=" + String((unsigned)gIMUWatermarkNow) + 
            ", minimum=" + String((unsigned)gIMUWatermarkMin) + "\n";
  
  // Thermal task watermarks
  result += "Thermal Task: current=" + String((unsigned)gThermalWatermarkNow) + 
            ", minimum=" + String((unsigned)gThermalWatermarkMin) + "\n";
  
  // Main task watermark
  UBaseType_t mainWatermark = uxTaskGetStackHighWaterMark(NULL);
  result += "Main Task: current=" + String((unsigned)mainWatermark) + "\n";
  
  // Memory usage
  result += "\nMemory Usage:\n";
  result += "Free Heap: " + String(ESP.getFreeHeap()) + " bytes\n";
  result += "Min Free Heap: " + String(ESP.getMinFreeHeap()) + " bytes\n";
  
  if (psramFound()) {
    result += "Free PSRAM: " + String(ESP.getFreePsram()) + " bytes\n";
    result += "Min Free PSRAM: " + String(ESP.getMinFreePsram()) + " bytes\n";
  }
  
  return result;
}

static String cmd_help_modern(const String& cmd) {
  RETURN_VALID_IF_VALIDATE();
  
  // Check if "all" parameter is provided for full help
  String args = cmd.substring(4); // Remove "help"
  args.trim();
  bool showAll = (args == "all");
  
  // Handle CLI state transitions and help system
  if (gCLIState == CLI_NORMAL) {
    // Swap history: hide current CLI output while in help
    gHiddenHistory = gWebMirror.snapshot();
    if (!gWebMirror.buf) { gWebMirror.init(gWebMirrorCap); }
    gWebMirror.clear();
    gCLIState = CLI_HELP_MAIN;
    gShowAllCommands = showAll;
    return renderHelpMain(showAll);
  } else {
    // If already in help mode, just re-render main help
    gCLIState = CLI_HELP_MAIN;
    gShowAllCommands = showAll;
    return renderHelpMain(showAll);
  }
}

// ----- WiFi command handlers (Batch 2) -----
static String cmd_wifiinfo() {
  RETURN_VALID_IF_VALIDATE();
  if (WiFi.isConnected()) {
    return String("WiFi Status:\n") + "  SSID: " + WiFi.SSID() + "\n" + "  IP: " + WiFi.localIP().toString() + "\n" + "  RSSI: " + String(WiFi.RSSI()) + " dBm\n" + "  MAC: " + WiFi.macAddress();
  } else {
    return String("WiFi: Not connected\n") + "  Saved SSID: " + gSettings.wifiSSID + "\n" + "  MAC: " + WiFi.macAddress();
  }
}

static String cmd_wifilist() {
  RETURN_VALID_IF_VALIDATE();
  loadWiFiNetworks();
  if (gWifiNetworkCount == 0) return "No saved networks.";
  String out = "Saved Networks (priority asc, numbered)\nUse 'wificonnect <index>' to connect to a specific entry.\n";
  for (int i = 0; i < gWifiNetworkCount; ++i) {
    out += "  " + String(i + 1) + ". [" + String(gWifiNetworks[i].priority) + "] '" + gWifiNetworks[i].ssid + "'";
    if (gWifiNetworks[i].hidden) out += " (hidden)";
    if (i == 0) out += "  <- primary";
    out += "\n";
  }
  return out;
}

static String cmd_wifiadd(const String& originalCmd) {
  RETURN_VALID_IF_VALIDATE();
  // Admin check now handled by executeCommand pipeline
  // wifiadd <ssid> <pass> [priority] [hidden0|1]
  String args = originalCmd.substring(8);
  args.trim();
  int sp1 = args.indexOf(' ');
  if (sp1 <= 0) return "Usage: wifiadd <ssid> <pass> [priority] [hidden0|1]";
  String ssid = args.substring(0, sp1);
  String rest = args.substring(sp1 + 1);
  rest.trim();
  int sp2 = rest.indexOf(' ');
  String pass = (sp2 < 0) ? rest : rest.substring(0, sp2);
  String more = (sp2 < 0) ? "" : rest.substring(sp2 + 1);
  more.trim();
  int pri = 0;
  bool hid = (ssid.length() == 0);
  if (more.length() > 0) {
    int sp3 = more.indexOf(' ');
    String priStr = (sp3 < 0) ? more : more.substring(0, sp3);
    pri = priStr.toInt();
    if (pri <= 0) pri = 1;
    String hidStr = (sp3 < 0) ? "" : more.substring(sp3 + 1);
    hid = (hidStr == "1" || hidStr == "true");
  }
  loadWiFiNetworks();
  upsertWiFiNetwork(ssid, pass, pri, hid);
  sortWiFiByPriority();
  saveWiFiNetworks();
  return "Saved network '" + ssid + "' with priority " + String(pri == 0 ? 1 : pri) + (hid ? " (hidden)" : "");
}

static String cmd_wifirm(const String& originalCmd) {
  RETURN_VALID_IF_VALIDATE();
  // Admin check now handled by executeCommand pipeline
  String ssid = originalCmd.substring(7);
  ssid.trim();
  if (ssid.length() == 0) return "Usage: wifirm <ssid>";
  loadWiFiNetworks();
  bool ok = removeWiFiNetwork(ssid);
  if (ok) {
    saveWiFiNetworks();
    return "Removed network '" + ssid + "'";
  }
  return "Network not found: '" + ssid + "'";
}

static String cmd_wifipromote(const String& originalCmd) {
  RETURN_VALID_IF_VALIDATE();
  // Admin check now handled by executeCommand pipeline
  // wifipromote <ssid> [newPriority]
  String rest = originalCmd.substring(12);
  rest.trim();
  if (rest.length() == 0) return "Usage: wifipromote <ssid> [newPriority]";
  int sp = rest.indexOf(' ');
  String ssid = (sp < 0) ? rest : rest.substring(0, sp);
  int newPri = (sp < 0) ? 1 : rest.substring(sp + 1).toInt();
  if (newPri <= 0) newPri = 1;
  loadWiFiNetworks();
  int idx = findWiFiNetwork(ssid);
  if (idx < 0) return "Network not found: '" + ssid + "'";
  gWifiNetworks[idx].priority = newPri;
  sortWiFiByPriority();
  saveWiFiNetworks();
  return "Priority updated for '" + ssid + "' -> " + String(newPri);
}

static String cmd_wificonnect(const String& originalCmd) {
  RETURN_VALID_IF_VALIDATE();
  // Allow normal users to connect; support flags while preserving legacy usage
  // Only load from file if no networks in memory (avoid overwriting plaintext passwords)
  if (gWifiNetworkCount == 0) {
    loadWiFiNetworks();
  }
  String arg = "";
  if (originalCmd.length() > 11) {
    arg = originalCmd.substring(11);
    arg.trim();
  }
  String prevSSID = WiFi.isConnected() ? WiFi.SSID() : String("");
  bool connected = false;

  // Parse flags: --best, --index N, or legacy positional index
  bool useBest = false;
  int index1 = -1;
  if (arg.length() == 0) {
    useBest = true;  // default behavior
  } else if (arg.startsWith("--best")) {
    useBest = true;
  } else if (arg.startsWith("--index ")) {
    String n = arg.substring(8);
    n.trim();
    index1 = n.toInt();
    if (index1 <= 0 || index1 > gWifiNetworkCount) return String("Usage: wificonnect --index <1..") + String(gWifiNetworkCount) + ">";
  } else {
    // Legacy: numeric positional index
    int sel = arg.toInt();
    if (sel > 0) index1 = sel;
    else return String("Usage: wificonnect [--best | --index <1..") + String(gWifiNetworkCount) + ">]";
  }

  if (useBest) {
    // Try saved networks by priority; fall back to single setting if none succeed
    if (gWifiNetworkCount > 0) {
      sortWiFiByPriority();
      for (int i = 0; i < gWifiNetworkCount && !connected; ++i) {
        const WifiNetwork& nw = gWifiNetworks[i];
        broadcastOutput(String("Connecting to '") + nw.ssid + "' (priority " + String(nw.priority) + ") ...");
        WiFi.begin(nw.ssid.c_str(), nw.password.c_str());
        unsigned long start = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) { 
          delay(200); 
          // Check for user escape input during WiFi connection
          if (Serial.available()) {
            while (Serial.available()) Serial.read(); // consume all pending input
            broadcastOutput("*** WiFi connection cancelled by user - skipping remaining attempts ***");
            return "WiFi connection cancelled by user";
          }
        }
        if (WiFi.status() == WL_CONNECTED) {
          broadcastOutput(String("WiFi connected: ") + WiFi.localIP().toString());
          gWifiNetworks[i].lastConnected = millis();
          saveWiFiNetworks();
          connected = true;
        } else {
          broadcastOutput(String("Failed connecting to '") + nw.ssid + "' - WiFi status: " + String(WiFi.status()));
        }
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
    
    // Configure NTP now that WiFi is connected
    setupNTP();
    broadcastOutput("NTP configured - time sync will occur in background");
    
    gOutputFlags |= OUTPUT_WEB;
    gSettings.outWeb = true;
    saveUnifiedSettings();
    String ss = WiFi.SSID();
    String ip = WiFi.localIP().toString();
    return String("Connected to WiFi: ") + ss + "\nIP: " + ip + "\nHTTP server restarted and web output enabled.";
  }
  return "Failed to connect. If there was a previous connection, attempted rollback. Check 'wifiinfo'.";
}

static String cmd_wifidisconnect() {
  RETURN_VALID_IF_VALIDATE();
  // Admin check now handled by executeCommand pipeline
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
  if (!gWebMirror.buf) { gWebMirror.init(gWebMirrorCap); }
  gWebMirror.clear();
  WiFi.disconnect();
  return "WiFi disconnected. HTTP server stopped and web output disabled to free heap.";
}

static String cmd_wifiscan(const String& command) {
  RETURN_VALID_IF_VALIDATE();
  bool json = (command == "wifiscan json" || command == "wifiscan  json");
  int n = WiFi.scanNetworks(/*async=*/false, /*hidden=*/true);
  if (n < 0) return "WiFi scan failed";
  if (json) {
    String out = "[\n";
    for (int i = 0; i < n; ++i) {
      if (i) out += ",\n";
      out += String("  { \"ssid\": \"") + WiFi.SSID(i) + "\", \"rssi\": " + String(WiFi.RSSI(i)) + ", \"bssid\": \"" + WiFi.BSSIDstr(i) + "\" }";
    }
    out += "\n]";
    return out;
  }
  String out = String(n) + " networks found:\n";
  for (int i = 0; i < n; ++i) {
    out += String("  ") + (i + 1) + ") '" + WiFi.SSID(i) + "'  RSSI=" + WiFi.RSSI(i) + "  BSSID=" + WiFi.BSSIDstr(i) + "\n";
  }
  return out;
}

// ----- Sensor command handlers (Batch 3) -----
static String cmd_tof() {
  RETURN_VALID_IF_VALIDATE();
  float distance = readTOFDistance();
  if (distance < 999.0) {
    return String("Distance: ") + String(distance, 1) + " cm";
  } else {
    return "No valid distance measurement";
  }
}

static String cmd_tofstart() {
  RETURN_VALID_IF_VALIDATE();
  DEBUG_CLIF("tofstart: prev=%d", tofEnabled ? 1 : 0);

  // Check memory before creating task
  if (ESP.getFreeHeap() < 16384) {  // Need ~16KB minimum
    return "Insufficient memory for ToF sensor (need 16KB free)";
  }

  // Add startup delay if other sensors recently started
  static unsigned long lastSensorStart = 0;
  unsigned long now = millis();
  if (now - lastSensorStart < 500) {
    delay(500 - (now - lastSensorStart));
  }
  lastSensorStart = millis();

  // Initialize ToF sensor synchronously (like thermal sensor)
  if (!tofConnected || tofSensor == nullptr) {
    DEBUG_CLIF("tofstart: sensor not connected, initializing...");
    
    // Try initialization with retry
    bool initSuccess = false;
    for (int attempt = 0; attempt < 2 && !initSuccess; attempt++) {
      if (attempt > 0) {
        DEBUG_CLIF("tofstart: retry attempt %d", attempt + 1);
        delay(200);  // Brief delay between attempts
      }
      initSuccess = initToFSensor();
    }
    
    if (!initSuccess) {
      DEBUG_CLIF("tofstart: init failed after retries");
      // Ensure ToF stays disabled on init failure
      tofEnabled = false;
      tofConnected = false;
      return "Failed to initialize VL53L4CX ToF sensor (tried 2x)";
    }
  } else {
    DEBUG_CLIF("tofstart: sensor already connected (tofConnected=%d, tofSensor=%p)", tofConnected ? 1 : 0, tofSensor);
  }

  // Create ToF task lazily
  if (tofTaskHandle == nullptr) {
    const uint32_t tofStack = 3072;  // words; ~12KB
    if (xTaskCreate(tofTask, "tof_task", tofStack, nullptr, 1, &tofTaskHandle) != pdPASS) {
      DEBUG_CLIF("tofstart: FAILED to create ToF task");
      return "Failed to create ToF task";
    }
  }
  // Hold I2C clock steady for ToF while enabled (bounded 50k..400k)
  {
    uint32_t prevClock = gWire1DefaultHz;  // Save previous clock
    uint32_t tofHz = (gSettings.i2cClockToFHz > 0) ? (uint32_t)gSettings.i2cClockToFHz : 200000;
    if (tofHz < 50000) tofHz = 50000;
    if (tofHz > 400000) tofHz = 400000;
    
    // Only change clock if different, add settling delay
    if (prevClock != tofHz) {
      DEBUG_CLIF("[I2C] Changing Wire1 clock: %lu -> %lu Hz", (unsigned long)prevClock, (unsigned long)tofHz);
      gWire1DefaultHz = tofHz;
      i2cSetDefaultWire1Clock();
      delay(100);  // Let I2C bus settle after clock change
    }
    DEBUG_CLIF("[I2C] Wire1 clock confirmed for ToF: %lu Hz", (unsigned long)tofHz);
  }
  DEBUG_CLIF("tofstart: enabling ToF sensor (persistent ToF task will poll)");
  {
    bool prev = tofEnabled;
    tofEnabled = true;
    if (tofEnabled != prev) sensorStatusBumpWith("tofstart@CLI");
  }
  DEBUG_CLIF("tofstart: now=%d, seq=%d", tofEnabled ? 1 : 0, gSensorStatusSeq);
  return "SUCCESS: ToF sensor started successfully";
}

static String cmd_tofstop() {
  RETURN_VALID_IF_VALIDATE();
  DEBUG_CLIF("tofstop: prev=%d", tofEnabled ? 1 : 0);

  // Initialize ToF sensor synchronously (like thermal sensor)
  if (!tofConnected || tofSensor == nullptr) {
    DEBUG_CLIF("tofstart: sensor not connected, initializing...");
    if (!initToFSensor()) {
      DEBUG_CLIF("tofstart: init failed");
      // Ensure ToF stays disabled on init failure
      tofEnabled = false;
      tofConnected = false;
      return "Failed to initialize VL53L4CX ToF sensor";
    }
  } else {
    DEBUG_CLIF("tofstart: sensor already connected (tofConnected=%d, tofSensor=%p)", tofConnected ? 1 : 0, tofSensor);
  }

  // First, disable the flag so the ToF task will not start a new read
  {
    bool prev = tofEnabled;
    tofEnabled = false;
    if (tofEnabled != prev) sensorStatusBumpWith("tofstop@CLI");
  }
  // Serialize with the ToF task: take I2C mutex to ensure the task isn't inside a read
  if (i2cMutex) xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(500));
  VL53L4CX_Error stop_status = tofSensor->VL53L4CX_StopMeasurement();
  if (stop_status != VL53L4CX_ERROR_NONE) {
    if (i2cMutex) xSemaphoreGive(i2cMutex);
    return "ERROR: Failed to stop ToF measurement";
  } else {
    DEBUG_CLIF("tofstop: prev=%d", tofEnabled ? 1 : 0);
    {
      // ToF uses a persistent dedicated task; no task deletion required here

      // Clean up ToF sensor state and object while holding the mutex so task can't access it
      tofConnected = false;
      if (tofSensor != nullptr) {
        DEBUG_CLIF("tofstop: deleting tofSensor object");
        delete tofSensor;
        tofSensor = nullptr;
      }
      // Reset ToF cache validity to avoid stale UI
      if (lockSensorCache(pdMS_TO_TICKS(20))) {
        gSensorCache.tofDataValid = false;
        gSensorCache.tofTotalObjects = 0;
        for (int j = 0; j < 4; j++) {
          gSensorCache.tofObjects[j].detected = false;
          gSensorCache.tofObjects[j].valid = false;
        }
        unlockSensorCache();
      }
    }
    if (i2cMutex) xSemaphoreGive(i2cMutex);
    // Reclaim task stack by deleting the task; it will be recreated on next start
    if (tofTaskHandle) {
      vTaskDelete(tofTaskHandle);
      tofTaskHandle = nullptr;
    }
    // Restore I2C default: prefer thermal clock if thermal still enabled; otherwise 100k safe default
    if (thermalEnabled) {
      uint32_t thz = (gSettings.i2cClockThermalHz > 0) ? (uint32_t)gSettings.i2cClockThermalHz : 800000;
      gWire1DefaultHz = thz;
    } else {
      gWire1DefaultHz = 100000;
    }
    i2cSetDefaultWire1Clock();
    DEBUG_CLIF("[I2C] Wire1 default clock restored after ToF stop -> %lu Hz", (unsigned long)gWire1DefaultHz);
    DEBUG_CLIF("tofstop: now=%d, seq=%d", tofEnabled ? 1 : 0, gSensorStatusSeq);
    return "SUCCESS: ToF sensor stopped successfully";
  }
}

static String cmd_thermalstart() {
  RETURN_VALID_IF_VALIDATE();
  
  // Check memory before creating large thermal task (needs ~40KB)
  if (ESP.getFreeHeap() < 40960) {
    return "Insufficient memory for Thermal sensor (need 40KB free)";
  }

  // Add startup delay if other sensors recently started (thermal needs longest delay)
  static unsigned long lastSensorStart = 0;
  unsigned long now = millis();
  if (now - lastSensorStart < 800) {  // Longer delay for thermal
    delay(800 - (now - lastSensorStart));
  }
  lastSensorStart = millis();

  // Preflight: ensure I2C bus clock is set for thermal before init
  uint32_t prevClock = gWire1DefaultHz;
  uint32_t thermalHz = gSettings.i2cClockThermalHz > 0 ? (uint32_t)gSettings.i2cClockThermalHz : 800000;
  
  // Only change clock if different, add settling delay
  if (prevClock != thermalHz) {
    DEBUG_CLIF("[I2C] Changing Wire1 clock for thermal: %lu -> %lu Hz", (unsigned long)prevClock, (unsigned long)thermalHz);
    gWire1DefaultHz = thermalHz;
    i2cSetDefaultWire1Clock();
    delay(150);  // Longer delay for thermal's high clock speed
  }

  // Smart coexistence: try to start thermal without stopping ToF first
  // Only stop ToF if thermal initialization actually fails due to I2C conflicts
  bool tofWasRunning = (tofEnabled && tofSensor);
  DEBUG_CMD_FLOWF("thermalstart: attempting coexistence with ToF (tofRunning=%d)", tofWasRunning ? 1 : 0);

  if (!thermalConnected || thermalSensor == nullptr) {
    // Defer initialization to thermalTask (larger stack); wait for completion to keep CLI behavior
    thermalInitDone = false;
    thermalInitResult = false;
    thermalInitRequested = true;
    // Enable thermal so task runs
    DEBUG_CLIF("thermalstart: requesting deferred init in thermalTask");
  }

  // Create Thermal task lazily
  if (thermalTaskHandle == nullptr) {
    const uint32_t thermalStack = 8192;  // words; ~32KB
    if (xTaskCreate(thermalTask, "thermal_task", thermalStack, nullptr, 1, &thermalTaskHandle) != pdPASS) {
      DEBUG_CLIF("thermalstart: FAILED to create Thermal task");
      return "Failed to create Thermal task";
    }
  }
  // Enable thermal sensor - persistent thermal task will poll
  DEBUG_CLIF("thermalstart: prev=%d", thermalEnabled ? 1 : 0);
  bool prev = thermalEnabled;
  thermalEnabled = true;
  if (thermalEnabled && !prev) {
    thermalPendingFirstFrame = true;
    thermalArmAtMs = millis() + 150;  // small arming delay to let system settle
  }
  // If init was requested above, block briefly for result so caller gets success/fail
  if (thermalInitRequested || !thermalConnected || thermalSensor == nullptr) {
    unsigned long start = millis();
    while (!thermalInitDone && (millis() - start) < 3000UL) {
      delay(10);
    }
    if (!thermalInitDone || !thermalInitResult) {
      // If thermal init failed and ToF was running, try stopping ToF and retrying thermal
      if (tofWasRunning && tofEnabled && tofSensor) {
        DEBUG_CMD_FLOWF("thermalstart: init failed with ToF running, stopping ToF and retrying...");
        VL53L4CX_Error st = tofSensor->VL53L4CX_StopMeasurement();
        if (st == VL53L4CX_ERROR_NONE) {
          bool prevTof = tofEnabled;
          tofEnabled = false;
          if (tofEnabled != prevTof) sensorStatusBumpWith("tofstop@thermalstart-retry");
          DEBUG_CMD_FLOWF("thermalstart: ToF stopped, retrying thermal init...");
          
          // Retry thermal initialization
          thermalInitDone = false;
          thermalInitResult = false;
          thermalInitRequested = true;
          
          // Wait for retry result
          unsigned long retryStart = millis();
          while (!thermalInitDone && (millis() - retryStart) < 3000UL) {
            delay(10);
          }
          
          if (!thermalInitDone || !thermalInitResult) {
            DEBUG_CMD_FLOWF("thermalstart: retry failed even after stopping ToF");
            // Cleanup flags on failure
            thermalEnabled = false;
            thermalPendingFirstFrame = false;
            thermalArmAtMs = 0;
            return "Failed to initialize MLX90640 thermal sensor (retry after stopping ToF failed)";
          } else {
            DEBUG_CMD_FLOWF("thermalstart: retry succeeded after stopping ToF");
          }
        } else {
          DEBUG_CMD_FLOWF("thermalstart: could not stop ToF for retry (err=%d)", (int)st);
          // Cleanup flags on failure
          thermalEnabled = false;
          thermalPendingFirstFrame = false;
          thermalArmAtMs = 0;
          return "Failed to initialize MLX90640 thermal sensor (could not stop ToF for retry)";
        }
      } else {
        // Cleanup flags on failure
        thermalEnabled = false;
        thermalPendingFirstFrame = false;
        thermalArmAtMs = 0;
        return "Failed to initialize MLX90640 thermal sensor (deferred)";
      }
    }
  }
  DEBUG_CLIF("thermalstart: now=%d, seq=%d", thermalEnabled ? 1 : 0, gSensorStatusSeq);
  DEBUG_CLIF("thermalstart: web client display quality=%dx", gSettings.thermalWebClientQuality);
  return "SUCCESS: MLX90640 thermal sensor started";
}

static String cmd_thermalstop() {
  RETURN_VALID_IF_VALIDATE();
  DEBUG_CLIF("thermalstop: prev=%d", thermalEnabled ? 1 : 0);
  // Disable first so the task idles
  bool was = thermalEnabled;
  thermalEnabled = false;
  if (was) sensorStatusBumpWith("thermalstop@CLI");
  // Serialize with thermal task and safely delete sensor object
  if (i2cMutex) xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(500));
  thermalConnected = false;
  if (thermalSensor != nullptr) {
    DEBUG_CLIF("thermalstop: deleting thermalSensor object");
    delete thermalSensor;
    thermalSensor = nullptr;
  }
  // Clear any thermal cache validity if applicable (handled inside read function/UI elsewhere)
  if (i2cMutex) xSemaphoreGive(i2cMutex);
  // Restore to safe mixed-sensor default when thermal stops
  gWire1DefaultHz = 100000;
  i2cSetDefaultWire1Clock();
  // Reclaim task stack by deleting the task; it will be recreated on next start
  if (thermalTaskHandle) {
    vTaskDelete(thermalTaskHandle);
    thermalTaskHandle = nullptr;
  }
  DEBUG_CLIF("thermalstop: now=%d, seq=%d", thermalEnabled ? 1 : 0, gSensorStatusSeq);
  return "Thermal sensor stopped";
}

static String cmd_imu() {
  if (!imuConnected || bno == nullptr) {
    if (!initIMUSensor()) {
      return "Failed to initialize IMU sensor";
    }
  }
  readIMUSensor();
  return "IMU data read (check serial output)";
}

static String cmd_imustart() {
  // Check memory before creating IMU task (needs ~20KB)
  if (ESP.getFreeHeap() < 20480) {
    return "Insufficient memory for IMU sensor (need 20KB free)";
  }

  // Add startup delay if other sensors recently started (IMU starts first, shortest delay)
  static unsigned long lastSensorStart = 0;
  unsigned long now = millis();
  if (now - lastSensorStart < 300) {
    delay(300 - (now - lastSensorStart));
  }
  lastSensorStart = millis();

  // Create IMU task lazily
  if (imuTaskHandle == nullptr) {
    const uint32_t imuStack = 4096;  // words; ~16KB
    if (xTaskCreate(imuTask, "imu_task", imuStack, nullptr, 1, &imuTaskHandle) != pdPASS) {
      return "Failed to create IMU task (insufficient memory or resources)";
    }
    DEBUG_CLIF("imustart: IMU task created successfully");
  }
  
  // Defer initialization to imuTask; wait briefly for result
  if (bno == nullptr || !imuConnected) {
    DEBUG_CLIF("imustart: requesting IMU initialization");
    imuInitDone = false;
    imuInitResult = false;
    imuInitRequested = true;
  }
  bool prev = imuEnabled;
  imuEnabled = true;  // task will run and perform init
  if (imuEnabled != prev) sensorStatusBumpWith("imustart@CLI");

  // If init was requested, block up to 3s for a result so CLI returns accurate status
  if (imuInitRequested || bno == nullptr || !imuConnected) {
    DEBUG_CLIF("imustart: waiting for initialization result...");
    unsigned long start = millis();
    while (!imuInitDone && (millis() - start) < 3000UL) {
      delay(10);
    }
    if (!imuInitDone) {
      imuEnabled = false;
      return "Failed to initialize IMU sensor (timeout after 3s)";
    }
    if (!imuInitResult) {
      imuEnabled = false;
      return "Failed to initialize IMU sensor (initialization failed)";
    }
    DEBUG_CLIF("imustart: initialization completed successfully");
  }
  return "SUCCESS: IMU sensor started successfully";
}

static String cmd_imustop() {
  // Disable first so the task idles
  bool wasRunning = imuEnabled;
  imuEnabled = false;
  if (wasRunning) sensorStatusBumpWith("imustop@CLI");
  // Serialize with IMU task and safely delete sensor object if present
  if (i2cMutex) xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(300));
  imuConnected = false;
  if (bno != nullptr) {
    DEBUG_CLIF("imustop: deleting IMU object");
    delete bno;
    bno = nullptr;
  }
  if (i2cMutex) xSemaphoreGive(i2cMutex);
  // Reclaim task stack by deleting the task; it will be recreated on next start
  if (imuTaskHandle) {
    vTaskDelete(imuTaskHandle);
    imuTaskHandle = nullptr;
  }
  return "IMU sensor stopped";
}

// ----- APDS9960 command handlers (Batch 3 continued) -----
static String cmd_apdscolor() {
  RETURN_VALID_IF_VALIDATE();
  readAPDSColor();
  return "APDS color data read (check serial output)";
}

static String cmd_apdsproximity() {
  RETURN_VALID_IF_VALIDATE();
  readAPDSProximity();
  return "APDS proximity data read (check serial output)";
}

static String cmd_apdsgesture() {
  RETURN_VALID_IF_VALIDATE();
  readAPDSGesture();
  return "APDS gesture data read (check serial output)";
}

static String cmd_apdscolorstart() {
  RETURN_VALID_IF_VALIDATE();
  if (rgbgestureConnected) {
    if (!initAPDS9960()) {
      return "ERROR: Failed to initialize APDS9960 sensor";
    }
    if (apds != nullptr) {
      apds->enableColor(true);
      bool prev = apdsColorEnabled;
      apdsColorEnabled = true;
      if (apdsColorEnabled != prev) sensorStatusBumpWith("apdscolorstart@CLI");
      return "APDS color sensing enabled";
    }
    return "ERROR: APDS sensor object is null";
  } else {
    return "APDS sensor not detected";
  }
}

static String cmd_apdscolorstop() {
  RETURN_VALID_IF_VALIDATE();
  if (apdsConnected && apds != nullptr) {
    apds->enableColor(false);
    bool prev = apdsColorEnabled;
    apdsColorEnabled = false;
    if (apdsColorEnabled != prev) sensorStatusBumpWith("apdscolorstop@CLI");
    return "APDS color sensing disabled";
  } else {
    return "APDS sensor not initialized";
  }
}

static String cmd_apdsproximitystart() {
  RETURN_VALID_IF_VALIDATE();
  if (rgbgestureConnected) {
    if (!initAPDS9960()) {
      return "ERROR: Failed to initialize APDS9960 sensor";
    }
    if (apds != nullptr) {
      apds->enableProximity(true);
      bool prev = apdsProximityEnabled;
      apdsProximityEnabled = true;
      if (apdsProximityEnabled != prev) sensorStatusBumpWith("apdsproxstart@CLI");
      return "APDS proximity sensing enabled";
    }
    return "ERROR: APDS sensor object is null";
  } else {
    return "APDS sensor not detected";
  }
}

static String cmd_apdsproximitystop() {
  RETURN_VALID_IF_VALIDATE();
  if (apdsConnected && apds != nullptr) {
    apds->enableProximity(false);
    bool prev = apdsProximityEnabled;
    apdsProximityEnabled = false;
    if (apdsProximityEnabled != prev) sensorStatusBumpWith("apdsproxstop@CLI");
    return "APDS proximity sensing disabled";
  } else {
    return "APDS sensor not initialized";
  }
}

static String cmd_apdsgesturestart() {
  RETURN_VALID_IF_VALIDATE();
  if (rgbgestureConnected) {
    if (!initAPDS9960()) {
      return "ERROR: Failed to initialize APDS9960 sensor";
    }
    if (apds != nullptr) {
      apds->enableProximity(true);
      apds->enableGesture(true);
      bool prev = apdsGestureEnabled;
      apdsGestureEnabled = true;
      if (apdsGestureEnabled != prev) sensorStatusBumpWith("apdsgesturestart@CLI");
      return "APDS gesture sensing enabled";
    }
    return "ERROR: APDS sensor object is null";
  } else {
    return "APDS sensor not detected";
  }
}

static String cmd_apdsgesturestop() {
  RETURN_VALID_IF_VALIDATE();
  if (apdsConnected && apds != nullptr) {
    apds->enableGesture(false);
    apds->enableProximity(false);
    bool prev = apdsGestureEnabled;
    apdsGestureEnabled = false;
    if (apdsGestureEnabled != prev) sensorStatusBumpWith("apdsgesturestop@CLI");
    return "APDS gesture sensing disabled";
  } else {
    return "APDS sensor not initialized";
  }
}

// ----- LED command handlers (Batch 4 - Part A) -----
static String cmd_ledcolor(const String& command) {
  RETURN_VALID_IF_VALIDATE();
  String colorName = "";
  if (command.startsWith("ledcolor ")) {
    colorName = command.substring(String("ledcolor ").length());
    colorName.trim();
    colorName.toLowerCase();
  }
  if (colorName.length() == 0) {
    return "Usage: ledcolor <red|green|blue|yellow|magenta|cyan|white|orange|purple|pink>";
  }
  if (colorName == "red") setLEDColor({ 255, 0, 0 });
  else if (colorName == "green") setLEDColor({ 0, 255, 0 });
  else if (colorName == "blue") setLEDColor({ 0, 0, 255 });
  else if (colorName == "yellow") setLEDColor({ 255, 255, 0 });
  else if (colorName == "magenta") setLEDColor({ 255, 0, 255 });
  else if (colorName == "cyan") setLEDColor({ 0, 255, 255 });
  else if (colorName == "white") setLEDColor({ 255, 255, 255 });
  else if (colorName == "orange") setLEDColor({ 255, 165, 0 });
  else if (colorName == "purple") setLEDColor({ 128, 0, 128 });
  else if (colorName == "pink") setLEDColor({ 255, 105, 180 });
  else {
    return "Unknown color: " + colorName + ". Use 'ledcolor' to see available colors.";
  }
  return String("LED set to ") + colorName;
}

static String cmd_ledclear() {
  RETURN_VALID_IF_VALIDATE();
  setLEDColor({ 0, 0, 0 });
  return "LED cleared (turned off)";
}

static String cmd_ledeffect(const String& command) {
  RETURN_VALID_IF_VALIDATE();
  // Pass through to existing effect parser in legacy chain if args present later
  // For now, implement a minimal alias to turn off or do a simple blink
  String args;
  if (command.startsWith("ledeffect ")) {
    args = command.substring(String("ledeffect ").length());
    args.trim();
  }
  if (args == "off" || args == "none" || args.length() == 0) {
    setLEDColor({ 0, 0, 0 });
    return "LED effect: off";
  }
  // Simple blink: blink red twice as a placeholder (non-blocking behavior should exist elsewhere)
  setLEDColor({ 255, 0, 0 });
  delay(100);
  setLEDColor({ 0, 0, 0 });
  delay(100);
  setLEDColor({ 255, 0, 0 });
  delay(100);
  setLEDColor({ 0, 0, 0 });
  return String("LED effect executed: ") + args;
}

// ----- Filesystem command handlers (Batch 4 - Part B) -----
static String cmd_files(const String& originalCmd) {
  RETURN_VALID_IF_VALIDATE();
  if (!filesystemReady) {
    return "Error: LittleFS not ready";
  }
  // Parse optional path argument from original input
  String path = "/";
  int sp1 = originalCmd.indexOf(' ');
  if (sp1 >= 0) {
    String rest = originalCmd.substring(sp1 + 1);
    rest.trim();
    if (rest.length() > 0) path = rest;
  }
  String out;
  bool ok = buildFilesListing(path, out, /*asJson=*/false);
  if (!ok) return out;  // contains error message
  return out;
}

static String cmd_mkdir(const String& originalCmd) {
  RETURN_VALID_IF_VALIDATE();
  // Admin check now handled by executeCommand pipeline
  if (!filesystemReady) return "Error: LittleFS not ready";
  int sp1 = originalCmd.indexOf(' ');
  if (sp1 < 0) return "Usage: mkdir <path>";
  String path = originalCmd.substring(sp1 + 1);
  path.trim();
  if (path.length() == 0) return "Usage: mkdir <path>";
  if (!path.startsWith("/")) path = String("/") + path;
  // Disallow creating folders under /logs
  if (path == "/logs" || path.startsWith("/logs/")) {
    return String("Error: Creation not allowed: ") + path;
  }
  if (LittleFS.mkdir(path)) {
    return String("Created folder: ") + path;
  } else {
    return String("Error: Failed to create folder: ") + path;
  }
}

static String cmd_rmdir(const String& originalCmd) {
  RETURN_VALID_IF_VALIDATE();
  // Admin check now handled by executeCommand pipeline
  if (!filesystemReady) return "Error: LittleFS not ready";
  int sp1 = originalCmd.indexOf(' ');
  if (sp1 < 0) return "Usage: rmdir <path>";
  String path = originalCmd.substring(sp1 + 1);
  path.trim();
  if (path.length() == 0) return "Usage: rmdir <path>";
  if (!path.startsWith("/")) path = String("/") + path;
  // Disallow removing /logs or anything under it
  if (path == "/logs" || path.startsWith("/logs/")) {
    return String("Error: Removal not allowed: ") + path;
  }
  // LittleFS rmdir removes empty directories only
  if (LittleFS.rmdir(path)) {
    return String("Removed folder: ") + path;
  } else {
    return String("Error: Failed to remove folder (ensure it is empty): ") + path;
  }
}

static String cmd_filecreate(const String& originalCmd) {
  RETURN_VALID_IF_VALIDATE();
  // Admin check now handled by executeCommand pipeline
  if (!filesystemReady) return "Error: LittleFS not ready";
  int sp1 = originalCmd.indexOf(' ');
  if (sp1 < 0) return "Usage: filecreate <path>";
  String path = originalCmd.substring(sp1 + 1);
  path.trim();
  if (path.length() == 0) return "Usage: filecreate <path>";
  if (!path.startsWith("/")) path = String("/") + path;
  if (path.endsWith("/")) return "Error: Path must be a file (not a directory)";
  // Disallow creating files in /logs and protected filenames
  if (path == "/users.json" || path == "/settings.json" || path == "/automations.json" || path == "/logs" || path.startsWith("/logs/")) {
    return String("Error: Creation not allowed: ") + path;
  }
  File f = LittleFS.open(path, "w");
  if (!f) return String("Error: Failed to create file: ") + path;
  f.close();
  return String("Created file: ") + path;
}

static String cmd_fileview(const String& originalCmd) {
  RETURN_VALID_IF_VALIDATE();
  if (!filesystemReady) return "Error: LittleFS not ready";
  int sp1 = originalCmd.indexOf(' ');
  if (sp1 < 0) return "Usage: fileview <path>";
  String path = originalCmd.substring(sp1 + 1);
  path.trim();
  if (path.length() == 0) return "Usage: fileview <path>";
  if (!path.startsWith("/")) path = String("/") + path;
  if (!LittleFS.exists(path)) return String("Error: File not found: ") + path;
  File f = LittleFS.open(path, "r");
  if (!f) return String("Error: Unable to open: ") + path;
  String content = f.readString();
  f.close();
  const size_t MAX_SHOW = 2048;
  if (content.length() > MAX_SHOW) {
    String head = content.substring(0, MAX_SHOW);
    return String("--- BEGIN (truncated) ") + path + " ---\n" + head + "\n--- TRUNCATED (" + String(content.length()) + " bytes total) ---";
  }
  return content;
}

static String cmd_filedelete(const String& originalCmd) {
  RETURN_VALID_IF_VALIDATE();
  // Admin check now handled by executeCommand pipeline
  if (!filesystemReady) return "Error: LittleFS not ready";
  int sp1 = originalCmd.indexOf(' ');
  if (sp1 < 0) return "Usage: filedelete <path>";
  String path = originalCmd.substring(sp1 + 1);
  path.trim();
  if (path.length() == 0) return "Usage: filedelete <path>";
  if (!path.startsWith("/")) path = String("/") + path;
  if (!LittleFS.exists(path)) return "Error: File does not exist";
  if (!LittleFS.remove(path)) return "Error: Failed to delete file";
  return String("Deleted file: ") + path;
}

// -------- Automation Logging Commands --------
static String cmd_autolog(const String& originalCmd) {
  DEBUG_CLIF("cmd_autolog called with: '%s'", originalCmd.c_str());
  RETURN_VALID_IF_VALIDATE();
  // Admin check now handled by executeCommand pipeline
  
  String args = originalCmd.substring(String("autolog ").length());
  args.trim();
  
  if (args.startsWith("start ")) {
    String filename = args.substring(6);
    filename.trim();
    if (filename.length() == 0) return "Usage: autolog start <filename>";
    
    gAutoLogActive = true;
    gAutoLogFile = filename;
    gAutoLogAutomationName = ""; // Will be set when automation starts
    
    // Create initial log entry
    if (!appendAutoLogEntry("LOG_START", "Automation logging started")) {
      gAutoLogActive = false;
      gAutoLogFile = "";
      return "Error: Failed to create log file: " + filename;
    }
    
    return "Automation logging started: " + filename;
    
  } else if (args == "stop") {
    if (!gAutoLogActive) return "Automation logging is not active";
    
    // Add final log entry
    appendAutoLogEntry("LOG_STOP", "Automation logging stopped");
    
    String result = "Automation logging stopped: " + gAutoLogFile;
    gAutoLogActive = false;
    gAutoLogFile = "";
    gAutoLogAutomationName = "";
    
    return result;
    
  } else if (args == "status") {
    if (gAutoLogActive) {
      return "Automation logging ACTIVE: " + gAutoLogFile + 
             (gAutoLogAutomationName.length() > 0 ? " (automation: " + gAutoLogAutomationName + ")" : "");
    } else {
      return "Automation logging INACTIVE";
    }
    
  } else {
    return "Usage: autolog start <filename> | autolog stop | autolog status";
  }
}

// ----- Settings/select toggles handlers (Batch 4 - Part B to reach 10) -----
static String cmd_wifiautoreconnect(const String& originalCmd) {
  RETURN_VALID_IF_VALIDATE();
  // Admin check now handled by executeCommand pipeline
  String valStr = originalCmd.substring(18);  // after "wifiautoreconnect "
  valStr.trim();
  int v = valStr.toInt();
  gSettings.wifiAutoReconnect = (v != 0);
  saveUnifiedSettings();
  return String("wifiAutoReconnect set to ") + (gSettings.wifiAutoReconnect ? "1" : "0");
}

static String cmd_clihistorysize(const String& originalCmd) {
  RETURN_VALID_IF_VALIDATE();
  int sp1 = originalCmd.indexOf(' ');
  if (sp1 < 0) return "Usage: cliHistorySize <1..100>";
  String valStr = originalCmd.substring(sp1 + 1);
  valStr.trim();
  int v = valStr.toInt();
  if (v < 1) v = 1;
  if (v > 100) v = 100;
  gSettings.cliHistorySize = v;
  saveUnifiedSettings();
  return String("cliHistorySize set to ") + String(v);
}

static String cmd_outserial(const String& originalCmd) {
  RETURN_VALID_IF_VALIDATE();
  // Syntax:
  //   outserial <0|1> [persist|temp]
  //   outserial [persist|temp] <0|1>
  int sp1 = originalCmd.indexOf(' ');
  String a = (sp1 >= 0) ? originalCmd.substring(sp1 + 1) : String();
  a.trim();
  String t1, t2;
  int sp2 = a.indexOf(' ');
  if (sp2 >= 0) {
    t1 = a.substring(0, sp2);
    t2 = a.substring(sp2 + 1);
    t2.trim();
  } else {
    t1 = a;
  }
  t1.trim();
  bool modeTemp = false;  // default persist
  int v = -1;
  auto parse01 = [](const String& s) -> int {
    String ss = s;
    ss.trim();
    return ss.toInt();
  };
  if (t1.length() && (t1 == "temp" || t1 == "persist")) {
    modeTemp = (t1 == "temp");
    if (t2.length()) v = parse01(t2);
  } else {
    if (t1.length()) v = parse01(t1);
    if (t2.length()) { modeTemp = (t2 == "temp"); }
  }
  if (v != 0) v = 1;  // normalize
  if (v < 0) return String("Usage: outserial <0|1> [persist|temp]");
  if (modeTemp) {
    // runtime only
    if (v) gOutputFlags |= OUTPUT_SERIAL;
    else gOutputFlags &= ~OUTPUT_SERIAL;
    return String("outSerial (runtime) set to ") + (v ? "1" : "0");
  } else {
    // persist and apply only this flag
    gSettings.outSerial = (v != 0);
    saveUnifiedSettings();
    if (v) gOutputFlags |= OUTPUT_SERIAL;
    else gOutputFlags &= ~OUTPUT_SERIAL;
    return String("outSerial (persisted) set to ") + (gSettings.outSerial ? "1" : "0");
  }
}

static String cmd_outweb(const String& originalCmd) {
  RETURN_VALID_IF_VALIDATE();
  // Admin check now handled by executeCommand pipeline
  // Syntax:
  //   outweb <0|1> [persist|temp]
  //   outweb [persist|temp] <0|1>
  int sp1 = originalCmd.indexOf(' ');
  String a = (sp1 >= 0) ? originalCmd.substring(sp1 + 1) : String();
  a.trim();
  String t1, t2;
  int sp2 = a.indexOf(' ');
  if (sp2 >= 0) {
    t1 = a.substring(0, sp2);
    t2 = a.substring(sp2 + 1);
    t2.trim();
  } else {
    t1 = a;
  }
  t1.trim();
  bool modeTemp = false;  // default persist
  int v = -1;
  auto parse01 = [](const String& s) -> int {
    String ss = s;
    ss.trim();
    return ss.toInt();
  };
  if (t1.length() && (t1 == "temp" || t1 == "persist")) {
    modeTemp = (t1 == "temp");
    if (t2.length()) v = parse01(t2);
  } else {
    if (t1.length()) v = parse01(t1);
    if (t2.length()) { modeTemp = (t2 == "temp"); }
  }
  if (v != 0) v = 1;
  if (v < 0) return String("Usage: outweb <0|1> [persist|temp]");
  if (modeTemp) {
    if (v) gOutputFlags |= OUTPUT_WEB;
    else gOutputFlags &= ~OUTPUT_WEB;
    return String("outWeb (runtime) set to ") + (v ? "1" : "0");
  } else {
    gSettings.outWeb = (v != 0);
    saveUnifiedSettings();
    if (v) gOutputFlags |= OUTPUT_WEB;
    else gOutputFlags &= ~OUTPUT_WEB;
    return String("outWeb (persisted) set to ") + (gSettings.outWeb ? "1" : "0");
  }
}

// ---- System Diagnostics Command Implementations ----

static String cmd_temperature_modern(const String& originalCmd) {
  RETURN_VALID_IF_VALIDATE();
  
  // ESP32 internal temperature sensor
  float tempC = temperatureRead();
  float tempF = (tempC * 9.0 / 5.0) + 32.0;
  
  String result = "ESP32 Internal Temperature:\n";
  result += "  " + String(tempC, 1) + "°C (" + String(tempF, 1) + "°F)";
  
  return result;
}

static String cmd_voltage_modern(const String& originalCmd) {
  RETURN_VALID_IF_VALIDATE();
  
  // ESP32 doesn't have built-in VCC measurement like ESP8266
  // We can read from an ADC pin if voltage divider is connected
  // For now, provide system power estimates based on operation
  
  String result = "Power Supply Information:\n";
  result += "========================\n";
  
  // Estimate power consumption based on active components
  float estimatedCurrent = 80; // Base ESP32 current in mA
  
  if (WiFi.isConnected()) {
    estimatedCurrent += 120; // WiFi active
    result += "WiFi: Active (+120mA)\n";
  } else {
    result += "WiFi: Inactive\n";
  }
  
  if (thermalConnected && thermalEnabled) {
    estimatedCurrent += 23; // MLX90640 typical
    result += "Thermal Sensor: Active (+23mA)\n";
  }
  
  if (imuConnected && imuEnabled) {
    estimatedCurrent += 12; // BNO055 typical
    result += "IMU Sensor: Active (+12mA)\n";
  }
  
  if (tofConnected && tofEnabled) {
    estimatedCurrent += 20; // VL53L4CX typical
    result += "ToF Sensor: Active (+20mA)\n";
  }
  
  if (apdsConnected) {
    estimatedCurrent += 3; // APDS9960 typical
    result += "APDS Sensor: Active (+3mA)\n";
  }
  
  result += "\nEstimated Current Draw: " + String(estimatedCurrent, 0) + "mA\n";
  result += "Estimated Power (3.3V): " + String((estimatedCurrent * 3.3) / 1000.0, 2) + "W\n";
  result += "\nNote: Direct voltage measurement requires external ADC connection";
  
  return result;
}

static String cmd_cpufreq_modern(const String& originalCmd) {
  RETURN_VALID_IF_VALIDATE();
  
  String args = originalCmd.substring(7); // "cpufreq "
  args.trim();
  
  uint32_t currentFreq = getCpuFrequencyMhz();
  
  if (args.length() == 0) {
    // Get current frequency
    String result = "CPU Frequency:\n";
    result += "  Current: " + String(currentFreq) + " MHz\n";
    result += "  XTAL: " + String(getXtalFrequencyMhz()) + " MHz\n";
    result += "  APB: " + String(getApbFrequency() / 1000000) + " MHz";
    return result;
  } else {
    // Set frequency (admin only for safety)
    // Admin check now handled by executeCommand pipeline
    
    uint32_t newFreq = args.toInt();
    if (newFreq < 80 || newFreq > 240) {
      return "Error: CPU frequency must be between 80-240 MHz";
    }
    
    // Validate common frequencies
    if (newFreq != 80 && newFreq != 160 && newFreq != 240) {
      return "Error: Supported frequencies are 80, 160, or 240 MHz";
    }
    
    setCpuFrequencyMhz(newFreq);
    return "CPU frequency set to " + String(newFreq) + " MHz";
  }
}

static String cmd_taskstats_modern(const String& originalCmd) {
  RETURN_VALID_IF_VALIDATE();
  
  String result = "Task Statistics:\n";
  result += "=================\n";
  
  // Get task count
  UBaseType_t taskCount = uxTaskGetNumberOfTasks();
  result += "Total Tasks: " + String(taskCount) + "\n\n";
  
  // Allocate memory for task status array
  TaskStatus_t* taskArray = (TaskStatus_t*)ps_alloc(taskCount * sizeof(TaskStatus_t), AllocPref::PreferPSRAM, "taskstats");
  if (!taskArray) {
    return "Error: Unable to allocate memory for task statistics";
  }
  
  // Get detailed task information
  UBaseType_t actualCount = uxTaskGetSystemState(taskArray, taskCount, nullptr);
  
  result += "Task Name          State  Prio  Stack  Core\n";
  result += "================== ===== ===== ====== ====\n";
  
  for (UBaseType_t i = 0; i < actualCount; i++) {
    String taskName = String(taskArray[i].pcTaskName);
    
    // Pad task name to 18 characters
    while (taskName.length() < 18) taskName += " ";
    if (taskName.length() > 18) taskName = taskName.substring(0, 18);
    
    String state;
    switch (taskArray[i].eCurrentState) {
      case eRunning: state = "RUN  "; break;
      case eReady: state = "READY"; break;
      case eBlocked: state = "BLOCK"; break;
      case eSuspended: state = "SUSP "; break;
      case eDeleted: state = "DEL  "; break;
      default: state = "UNK  "; break;
    }
    
    String prio = String(taskArray[i].uxCurrentPriority);
    while (prio.length() < 4) prio = " " + prio;
    
    String stack = String(taskArray[i].usStackHighWaterMark);
    while (stack.length() < 5) stack = " " + stack;
    
    String core = String(taskArray[i].xCoreID);
    
    result += taskName + " " + state + " " + prio + " " + stack + "   " + core + "\n";
  }
  
  free(taskArray);
  return result;
}

static String cmd_heapfrag_modern(const String& originalCmd) {
  RETURN_VALID_IF_VALIDATE();
  
  String result = "Heap Fragmentation Analysis:\n";
  result += "============================\n";
  
  // Internal heap
  size_t freeHeap = ESP.getFreeHeap();
  size_t minFreeHeap = ESP.getMinFreeHeap();
  size_t maxAllocHeap = ESP.getMaxAllocHeap();
  
  result += "Internal Heap:\n";
  result += "  Free: " + String(freeHeap) + " bytes\n";
  result += "  Min Free: " + String(minFreeHeap) + " bytes\n";
  result += "  Max Alloc: " + String(maxAllocHeap) + " bytes\n";
  
  // Calculate fragmentation percentage
  float fragmentation = 0.0;
  if (freeHeap > 0) {
    fragmentation = ((float)(freeHeap - maxAllocHeap) / (float)freeHeap) * 100.0;
  }
  result += "  Fragmentation: " + String(fragmentation, 1) + "%\n\n";
  
  // PSRAM if available
  size_t psramSize = ESP.getPsramSize();
  if (psramSize > 0) {
    size_t freePsram = ESP.getFreePsram();
    size_t minFreePsram = ESP.getMinFreePsram();
    size_t maxAllocPsram = ESP.getMaxAllocPsram();
    
    result += "PSRAM:\n";
    result += "  Total: " + String(psramSize) + " bytes\n";
    result += "  Free: " + String(freePsram) + " bytes\n";
    result += "  Min Free: " + String(minFreePsram) + " bytes\n";
    result += "  Max Alloc: " + String(maxAllocPsram) + " bytes\n";
    
    float psramFrag = 0.0;
    if (freePsram > 0) {
      psramFrag = ((float)(freePsram - maxAllocPsram) / (float)freePsram) * 100.0;
    }
    result += "  Fragmentation: " + String(psramFrag, 1) + "%\n";
  } else {
    result += "PSRAM: Not available\n";
  }
  
  return result;
}

// I2C Sensor Registry Structure (moved before usage)
struct I2CSensorEntry {
  uint8_t address;           // I2C address (7-bit)
  const char* name;          // Sensor name
  const char* description;   // Brief description
  const char* manufacturer;  // Manufacturer
  bool multiAddress;         // True if sensor supports multiple addresses
  uint8_t altAddress;        // Alternative address (if multiAddress is true)
};

// I2C Sensor Database (Adafruit STEMMA QT and common sensors)
static const I2CSensorEntry kI2CSensors[] = {
  // Temperature & Humidity Sensors
  { 0x38, "AHT20", "Temperature & Humidity", "Adafruit", false, 0x00 },
  { 0x44, "SHT40", "Temperature & Humidity", "Adafruit", true, 0x45 },
  { 0x70, "HTU21D", "Temperature & Humidity", "Adafruit", false, 0x00 },
  { 0x77, "BME280", "Temperature, Humidity & Pressure", "Adafruit", true, 0x76 },
  { 0x76, "BME680", "Environmental (T/H/P/Gas)", "Adafruit", true, 0x77 },
  { 0x18, "MCP9808", "High Precision Temperature", "Adafruit", true, 0x1F },
  
  // Motion & Orientation Sensors  
  { 0x28, "BNO055", "9-DOF IMU", "Adafruit", true, 0x29 },
  { 0x6A, "LSM6DS33", "6-DOF IMU", "Adafruit", true, 0x6B },
  { 0x68, "MPU6050", "6-DOF IMU", "Adafruit", true, 0x69 },
  { 0x0C, "LIS3MDL", "3-Axis Magnetometer", "Adafruit", true, 0x1E },
  { 0x53, "ADXL343", "3-Axis Accelerometer", "Adafruit", true, 0x1D },
  
  // Light & Color Sensors
  { 0x39, "APDS9960", "RGB, Gesture & Proximity", "Adafruit", false, 0x00 },
  { 0x10, "VEML7700", "Ambient Light", "Adafruit", false, 0x00 },
  { 0x52, "TCS34725", "RGB Color Sensor", "Adafruit", false, 0x00 },
  { 0x44, "AS7341", "11-Channel Spectral", "Adafruit", false, 0x00 },
  
  // Distance & Proximity Sensors
  { 0x29, "VL53L4CX", "ToF Distance (up to 6m)", "Adafruit", false, 0x00 },
  { 0x60, "MPR121", "12-Channel Capacitive Touch", "Adafruit", false, 0x00 },
  
  // Audio Sensors
  { 0x36, "MAX9744", "Audio Amplifier", "Adafruit", false, 0x00 },
  { 0x1A, "MAX98357", "I2S Audio Amplifier", "Adafruit", false, 0x00 },
  
  // Haptic / Motor Drivers
  { 0x5A, "DRV2605", "Haptic Motor Driver", "Adafruit", false, 0x00 },
  
  // Display Controllers
  { 0x3C, "SSD1306", "OLED Display Controller", "Adafruit", true, 0x3D },
  { 0x60, "SH1107", "OLED Display Controller", "Adafruit", true, 0x61 },
  
  // GPIO Expanders & Controllers
  { 0x36, "Seesaw", "GPIO/PWM/ADC Expander", "Adafruit", true, 0x49 },
  { 0x20, "MCP23017", "16-Bit I/O Expander", "Adafruit", true, 0x27 },
  { 0x48, "ADS1015", "12-Bit ADC", "Adafruit", true, 0x4B },
  { 0x62, "MCP4725", "12-Bit DAC", "Adafruit", true, 0x63 },
  
  // Real-Time Clock
  { 0x68, "DS3231", "Precision RTC", "Adafruit", false, 0x00 },
  { 0x6F, "DS1307", "Basic RTC", "Adafruit", false, 0x00 },
  
  // Power Management
  { 0x36, "LC709203F", "Battery Monitor", "Adafruit", false, 0x00 },
  { 0x6B, "MAX17048", "Battery Monitor", "Adafruit", false, 0x00 },
  
  // Thermal Sensors
  { 0x33, "MLX90640", "32x24 Thermal Camera", "Adafruit", false, 0x00 },
  { 0x5A, "MLX90614", "IR Temperature", "Adafruit", false, 0x00 },
  
  // Gas & Air Quality
  { 0x58, "SGP30", "Air Quality (TVOC/eCO2)", "Adafruit", false, 0x00 },
  { 0x5B, "CCS811", "Air Quality (TVOC/eCO2)", "Adafruit", true, 0x5A },
  
  // Pressure Sensors
  { 0x77, "BMP280", "Barometric Pressure", "Adafruit", true, 0x76 },
  { 0x60, "MPL3115A2", "Altimeter/Pressure", "Adafruit", false, 0x00 },
  
  // Current/Voltage Sensors
  { 0x40, "INA219", "Current/Power Monitor", "Adafruit", true, 0x4F },
  { 0x45, "INA260", "Current/Power Monitor", "Adafruit", true, 0x44 },
};

// Runtime Device Registry Structure
struct ConnectedDevice {
  uint8_t address;
  const char* name;
  const char* description;
  const char* manufacturer;
  bool isConnected;
  unsigned long lastSeen;
  unsigned long firstDiscovered;
  uint8_t bus;  // 0 = Wire, 1 = Wire1
};

// Device Registry Global Variables
#define MAX_CONNECTED_DEVICES 16
ConnectedDevice gConnectedDevices[MAX_CONNECTED_DEVICES];
int gConnectedDeviceCount = 0;
int gDiscoveryCount = 0;

// Device Registry File Management Functions
static void ensureDeviceRegistryFile() {
  if (!LittleFS.exists("/devices.json")) {
    createEmptyDeviceRegistry();
  }
}

static void createEmptyDeviceRegistry() {
  File file = LittleFS.open("/devices.json", "w");
  if (file) {
    file.println("{");
    file.println("  \"lastDiscovery\": 0,");
    file.println("  \"discoveryCount\": 0,");
    file.println("  \"devices\": []");
    file.println("}");
    file.close();
  }
}

static int findSensorIndexByAddress(uint8_t address) {
  // Pass 1: prefer exact primary address matches
  for (size_t i = 0; i < (sizeof(kI2CSensors) / sizeof(kI2CSensors[0])); i++) {
    if (kI2CSensors[i].address == address) {
      return i;
    }
  }
  // Pass 2: then allow alternate address matches if declared
  for (size_t i = 0; i < (sizeof(kI2CSensors) / sizeof(kI2CSensors[0])); i++) {
    if (kI2CSensors[i].multiAddress && kI2CSensors[i].altAddress == address) {
      return i;
    }
  }
  return -1;  // Not found
}

// Device Discovery Functions
static void addDiscoveredDevice(uint8_t address, uint8_t bus) {
  if (gConnectedDeviceCount >= MAX_CONNECTED_DEVICES) return;
  
  int sensorIndex = findSensorIndexByAddress(address);
  unsigned long now = millis();
  
  ConnectedDevice& device = gConnectedDevices[gConnectedDeviceCount++];
  device.address = address;
  device.bus = bus;
  device.isConnected = true;
  device.lastSeen = now;
  device.firstDiscovered = now;
  
  Serial.print("[DISCOVERY] Found device at 0x");
  Serial.print(address, HEX);
  Serial.print(" on bus " + String(bus));
  
  if (sensorIndex >= 0) {
    device.name = kI2CSensors[sensorIndex].name;
    device.description = kI2CSensors[sensorIndex].description;
    device.manufacturer = kI2CSensors[sensorIndex].manufacturer;
    Serial.println(" - " + String(device.name) + " (" + String(device.description) + ")");
  } else {
    device.name = "Unknown";
    device.description = "Unidentified Device";
    device.manufacturer = "Unknown";
    Serial.println(" - Unknown device");
  }
}

static void scanBusForDevices(uint8_t busNumber) {
  TwoWire* wire = (busNumber == 0) ? &Wire : &Wire1;
  
  // Initialize I2C bus if not already done
  if (busNumber == 0) {
    Wire.begin();  // Default pins SDA=21, SCL=22
  } else {
    Wire1.begin(22, 19);  // Your project's Wire1 configuration: SDA=22, SCL=19
  }
  
  // Small delay to let bus stabilize
  delay(10);
  
  for (uint8_t addr = 1; addr < 127; addr++) {
    wire->beginTransmission(addr);
    if (wire->endTransmission() == 0) {
      addDiscoveredDevice(addr, busNumber);
    }
  }
}

static void discoverI2CDevices() {
  Serial.println("[DISCOVERY] Starting I2C device discovery...");
  ensureDeviceRegistryFile();  // Always ensure file exists first
  
  // Clear existing registry
  gConnectedDeviceCount = 0;
  gDiscoveryCount++;
  
  // Scan both I2C buses
  Serial.println("[DISCOVERY] Scanning Wire (SDA=21, SCL=22)...");
  scanBusForDevices(0);  // Wire
  Serial.println("[DISCOVERY] Scanning Wire1 (SDA=19, SCL=22)...");
  scanBusForDevices(1);  // Wire1
  
  Serial.println("[DISCOVERY] Found " + String(gConnectedDeviceCount) + " total devices");
  
  // Save results to JSON file
  Serial.println("[DISCOVERY] Saving device registry to /devices.json...");
  saveDeviceRegistryToJSON();
  Serial.println("[DISCOVERY] Device registry saved successfully");
}

// JSON Save Function
static void saveDeviceRegistryToJSON() {
  ensureDeviceRegistryFile();  // Always ensure file exists
  
  File file = LittleFS.open("/devices.json", "w");
  if (!file) return;
  
  file.println("{");
  file.println("  \"lastDiscovery\": " + String(millis()) + ",");
  file.println("  \"discoveryCount\": " + String(gDiscoveryCount) + ",");
  file.println("  \"devices\": [");
  
  for (int i = 0; i < gConnectedDeviceCount; i++) {
    ConnectedDevice& device = gConnectedDevices[i];
    
    String hexAddr = String(device.address, HEX);
    if (device.address < 16) hexAddr = "0" + hexAddr;
    hexAddr.toUpperCase();
    
    file.print("    {");
    file.print("\"address\": " + String(device.address) + ", ");
    file.print("\"addressHex\": \"0x" + hexAddr + "\", ");
    file.print("\"name\": \"" + String(device.name) + "\", ");
    file.print("\"description\": \"" + String(device.description) + "\", ");
    file.print("\"manufacturer\": \"" + String(device.manufacturer) + "\", ");
    file.print("\"bus\": " + String(device.bus) + ", ");
    file.print("\"isConnected\": " + String(device.isConnected ? "true" : "false") + ", ");
    file.print("\"lastSeen\": " + String(device.lastSeen) + ", ");
    file.print("\"firstDiscovered\": " + String(device.firstDiscovered));
    file.print("}");
    
    if (i < gConnectedDeviceCount - 1) file.print(",");
    file.println();
  }
  
  file.println("  ]");
  file.println("}");
  file.close();
}

// Device Registry Display Functions
static String displayDeviceRegistry() {
  String result = "Connected I2C Devices:\n";
  result += "=====================\n";
  
  if (gConnectedDeviceCount == 0) {
    result += "No devices discovered. Run 'discover' to scan for devices.\n";
    return result;
  }
  
  result += "Bus  Addr Name         Description                    Status    Last Seen\n";
  result += "---- ---- ------------ ------------------------------ --------- ---------\n";
  
  for (int i = 0; i < gConnectedDeviceCount; i++) {
    ConnectedDevice& device = gConnectedDevices[i];
    
    String busStr = (device.bus == 0) ? "W0" : "W1";
    String hexAddr = "0x" + String(device.address, HEX);
    if (device.address < 16) hexAddr = "0x0" + String(device.address, HEX);
    hexAddr.toUpperCase();
    
    String name = String(device.name);
    if (name.length() > 12) name = name.substring(0, 12);
    
    String desc = String(device.description);
    if (desc.length() > 30) desc = desc.substring(0, 30);
    
    String status = device.isConnected ? "Connected" : "Disconnected";
    
    unsigned long timeSince = (millis() - device.lastSeen) / 1000;
    String lastSeen = String(timeSince) + "s ago";
    
    result += busStr + "   " + hexAddr + " " + name;
    while (result.length() % 4 != 0) result += " ";
    result += " " + desc;
    while (result.length() % 4 != 0) result += " ";
    result += " " + status + " " + lastSeen + "\n";
  }
  
  result += "\nTotal: " + String(gConnectedDeviceCount) + " devices";
  result += " (Discovery #" + String(gDiscoveryCount) + ")";
  
  return result;
}

// New Device Management Commands
static String cmd_devices_modern(const String& originalCmd) {
  RETURN_VALID_IF_VALIDATE();
  ensureDeviceRegistryFile();
  return displayDeviceRegistry();
}

static String cmd_discover_modern(const String& originalCmd) {
  RETURN_VALID_IF_VALIDATE();
  ensureDeviceRegistryFile();
  discoverI2CDevices();
  return "Device discovery completed. Found " + String(gConnectedDeviceCount) + " devices.\nRegistry saved to /devices.json\n\n" + displayDeviceRegistry();
}

static String cmd_devicefile_modern(const String& originalCmd) {
  RETURN_VALID_IF_VALIDATE();
  
  if (!LittleFS.exists("/devices.json")) {
    return "Device registry file not found. Run 'discover' to create it.";
  }
  
  File file = LittleFS.open("/devices.json", "r");
  if (!file) {
    return "Error: Could not open /devices.json";
  }
  
  String content = file.readString();
  file.close();
  
  return "Device Registry JSON (/devices.json):\n" + content;
}

// Helper functions for sensor connectivity checking
static bool isSensorConnected(const String& sensorType) {
  for (int i = 0; i < gConnectedDeviceCount; i++) {
    ConnectedDevice& device = gConnectedDevices[i];
    if (!device.isConnected) continue;
    
    // Check sensor type based on I2C address
    if (sensorType == "thermal" && device.address == 0x33) return true;  // MLX90640
    if (sensorType == "tof" && device.address == 0x29) return true;      // VL53L4CX
    if (sensorType == "apds" && device.address == 0x39) return true;     // APDS9960
    if (sensorType == "imu" && (device.address == 0x68 || device.address == 0x69)) return true; // MPU6050/ICM20948
  }
  return false;
}

// Helper function to identify sensor by I2C address
static String identifySensor(uint8_t address) {
  for (size_t i = 0; i < (sizeof(kI2CSensors) / sizeof(kI2CSensors[0])); i++) {
    const I2CSensorEntry& sensor = kI2CSensors[i];
    if (sensor.address == address || (sensor.multiAddress && sensor.altAddress == address)) {
      String result = sensor.name;
      result += " (";
      result += sensor.description;
      result += ")";
      return result;
    }
  }
  return "Unknown Device";
}

static String cmd_i2cscan_modern(const String& originalCmd) {
  RETURN_VALID_IF_VALIDATE();
  
  String result = "I2C Bus Scan with Device Identification:\n";
  result += "========================================\n";
  
  // Scan both I2C buses if available
  result += "Wire (SDA=21, SCL=22):\n";
  Wire.begin();
  int count0 = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      String hexAddr = String(addr, HEX);
      if (addr < 16) hexAddr = "0" + hexAddr;
      hexAddr.toUpperCase();
      
      String identification = identifySensor(addr);
      result += "  0x" + hexAddr + " (" + String(addr) + ") - " + identification + "\n";
      count0++;
    }
  }
  if (count0 == 0) result += "  No devices found\n";
  
  result += "\nWire1 (SDA=19, SCL=22):\n";
  Wire1.begin(22, 19);  // Your project's Wire1 configuration
  int count1 = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire1.beginTransmission(addr);
    if (Wire1.endTransmission() == 0) {
      String hexAddr = String(addr, HEX);
      if (addr < 16) hexAddr = "0" + hexAddr;
      hexAddr.toUpperCase();
      
      String identification = identifySensor(addr);
      result += "  0x" + hexAddr + " (" + String(addr) + ") - " + identification + "\n";
      count1++;
    }
  }
  if (count1 == 0) result += "  No devices found\n";
  
  result += "\nTotal devices found: " + String(count0 + count1);
  result += "\nUse 'sensors' to see full sensor database";
  result += "\nUse 'sensorinfo <name>' for detailed sensor information";
  
  return result;
}

static String cmd_i2cstats_modern(const String& originalCmd) {
  RETURN_VALID_IF_VALIDATE();
  
  String result = "I2C Bus Statistics:\n";
  result += "==================\n";
  
  // Wire bus info
  result += "Wire (Primary I2C):\n";
  result += "  SDA Pin: 21\n";
  result += "  SCL Pin: 22\n";
  result += "  Clock: " + String(Wire.getClock()) + " Hz\n";
  result += "  Timeout: " + String(Wire.getTimeOut()) + " ms\n\n";
  
  // Wire1 bus info (your project's sensor bus)
  result += "Wire1 (Sensor I2C):\n";
  result += "  SDA Pin: 19\n";
  result += "  SCL Pin: 22\n";
  result += "  Clock: " + String(gWire1CurrentHz) + " Hz\n";
  result += "  Default Clock: " + String(gWire1DefaultHz) + " Hz\n\n";
  
  // Sensor connection status
  result += "Connected Sensors:\n";
  if (gamepadConnected) result += "  Gamepad (seesaw)\n";
  if (imuConnected) result += "  IMU (BNO055)\n";
  if (apdsConnected) result += "  APDS9960\n";
  if (tofConnected) result += "  ToF (VL53L4CX)\n";
  if (thermalConnected) result += "  Thermal (MLX90640)\n";
  
  if (!gamepadConnected && !imuConnected && !apdsConnected && !tofConnected && !thermalConnected) {
    result += "  No sensors connected\n";
  }
  
  return result;
}

static String cmd_sensors_modern(const String& originalCmd) {
  RETURN_VALID_IF_VALIDATE();
  
  String args = originalCmd.substring(7); // "sensors"
  args.trim();
  
  String result = "I2C Sensor Database:\n";
  result += "===================\n";
  
  // Check for filter arguments
  String filter = "";
  if (args.length() > 0) {
    filter = args;
    filter.toLowerCase();
    result += "Filter: '" + args + "'\n\n";
  }
  
  result += "Addr Name         Description                    Manufacturer\n";
  result += "---- ------------ ------------------------------ ------------\n";
  
  int count = 0;
  for (size_t i = 0; i < (sizeof(kI2CSensors) / sizeof(kI2CSensors[0])); i++) {
    const I2CSensorEntry& sensor = kI2CSensors[i];
    
    // Apply filter if specified
    if (filter.length() > 0) {
      String sensorName = String(sensor.name);
      String sensorDesc = String(sensor.description);
      String sensorMfg = String(sensor.manufacturer);
      sensorName.toLowerCase();
      sensorDesc.toLowerCase();
      sensorMfg.toLowerCase();
      
      if (sensorName.indexOf(filter) < 0 && 
          sensorDesc.indexOf(filter) < 0 && 
          sensorMfg.indexOf(filter) < 0) {
        continue;
      }
    }
    
    String hexAddr = String(sensor.address, HEX);
    if (sensor.address < 16) hexAddr = "0" + hexAddr;
    hexAddr.toUpperCase();
    
    String name = String(sensor.name);
    while (name.length() < 12) name += " ";
    if (name.length() > 12) name = name.substring(0, 12);
    
    String desc = String(sensor.description);
    while (desc.length() < 30) desc += " ";
    if (desc.length() > 30) desc = desc.substring(0, 30);
    
    result += "0x" + hexAddr + " " + name + " " + desc + " " + String(sensor.manufacturer);
    
    if (sensor.multiAddress) {
      String altHex = String(sensor.altAddress, HEX);
      if (sensor.altAddress < 16) altHex = "0" + altHex;
      altHex.toUpperCase();
      result += " (alt: 0x" + altHex + ")";
    }
    result += "\n";
    count++;
  }
  
  result += "\nTotal sensors in database: " + String(sizeof(kI2CSensors) / sizeof(kI2CSensors[0]));
  if (filter.length() > 0) {
    result += " (showing " + String(count) + " matches)";
  }
  result += "\n\nUsage: sensors [filter] - filter by name, description, or manufacturer";
  result += "\nExample: sensors temperature, sensors adafruit, sensors imu";
  
  return result;
}

static String cmd_sensorinfo_modern(const String& originalCmd) {
  RETURN_VALID_IF_VALIDATE();
  
  String args = originalCmd.substring(10); // "sensorinfo"
  args.trim();
  
  if (args.length() == 0) {
    return "Usage: sensorinfo <sensor_name>\nExample: sensorinfo BNO055";
  }
  
  // Find sensor by name (case insensitive)
  const I2CSensorEntry* foundSensor = nullptr;
  String searchName = args;
  searchName.toLowerCase();
  
  for (size_t i = 0; i < (sizeof(kI2CSensors) / sizeof(kI2CSensors[0])); i++) {
    String sensorName = String(kI2CSensors[i].name);
    sensorName.toLowerCase();
    if (sensorName == searchName) {
      foundSensor = &kI2CSensors[i];
      break;
    }
  }
  
  if (!foundSensor) {
    String result = "Sensor '" + args + "' not found in database.\n\n";
    result += "Available sensors:\n";
    for (size_t i = 0; i < (sizeof(kI2CSensors) / sizeof(kI2CSensors[0])); i++) {
      result += "  " + String(kI2CSensors[i].name) + "\n";
      if (i > 10) {
        result += "  ... and " + String((sizeof(kI2CSensors) / sizeof(kI2CSensors[0])) - i - 1) + " more\n";
        break;
      }
    }
    result += "\nUse 'sensors' to see the full list";
    return result;
  }
  
  String result = "Sensor Information:\n";
  result += "==================\n";
  result += "Name: " + String(foundSensor->name) + "\n";
  result += "Description: " + String(foundSensor->description) + "\n";
  result += "Manufacturer: " + String(foundSensor->manufacturer) + "\n";
  
  String hexAddr = String(foundSensor->address, HEX);
  if (foundSensor->address < 16) hexAddr = "0" + hexAddr;
  hexAddr.toUpperCase();
  result += "I2C Address: 0x" + hexAddr + " (" + String(foundSensor->address) + ")\n";
  
  if (foundSensor->multiAddress) {
    String altHex = String(foundSensor->altAddress, HEX);
    if (foundSensor->altAddress < 16) altHex = "0" + altHex;
    altHex.toUpperCase();
    result += "Alternative Address: 0x" + altHex + " (" + String(foundSensor->altAddress) + ")\n";
  }
  
  // Check if this sensor is currently connected
  bool connectedWire0 = false, connectedWire1 = false;
  
  Wire.begin();
  Wire.beginTransmission(foundSensor->address);
  if (Wire.endTransmission() == 0) connectedWire0 = true;
  
  Wire1.begin(22, 19);
  Wire1.beginTransmission(foundSensor->address);
  if (Wire1.endTransmission() == 0) connectedWire1 = true;
  
  if (foundSensor->multiAddress) {
    Wire.beginTransmission(foundSensor->altAddress);
    if (Wire.endTransmission() == 0) connectedWire0 = true;
    
    Wire1.beginTransmission(foundSensor->altAddress);
    if (Wire1.endTransmission() == 0) connectedWire1 = true;
  }
  
  result += "\nConnection Status:\n";
  if (connectedWire0) result += "  ✓ Connected on Wire (SDA=21, SCL=22)\n";
  if (connectedWire1) result += "  ✓ Connected on Wire1 (SDA=19, SCL=22)\n";
  if (!connectedWire0 && !connectedWire1) {
    result += "  ✗ Not currently connected\n";
  }
  
  return result;
}

// Command registry structure (moved outside function to avoid stack overflow)
struct CommandEntry {
  const char* name;    // canonical command
  const char* help;    // short help text
  bool requiresAdmin;  // whether admin is required (UI will still gate via features)
  String (*handler)(const String& cmd);  // function pointer to command handler (nullptr = use legacy routing)
};

// Static command registry (moved outside function to avoid stack allocation)
static const CommandEntry kCommands[] = {
    // ---- Core / General ----
    { "help", "Show available commands and usage. Use 'help all' to see all commands.", false, cmd_help_modern },
    { "stack", "Show task stack watermarks and memory usage.", false, cmd_stack_modern },
    { "clear", "Clear CLI/web output history.", false, cmd_clear_modern },
    { "status", "Show system status (WiFi, FS, memory).", false, cmd_status_modern },
    { "uptime", "Show device uptime.", false, cmd_uptime_modern },
    { "memory", "Show heap/PSRAM usage.", false, cmd_memory_modern },
    { "psram", "Show PSRAM stats.", false, cmd_psram_modern },
    { "fsusage", "Show filesystem usage.", false, cmd_fsusage_modern },
    { "espnow status", "Show ESP-NOW status and configuration.", false, cmd_espnow_status_modern },
    { "espnow init", "Initialize ESP-NOW communication.", false, cmd_espnow_init_modern },
    { "espnow pair", "Pair ESP-NOW device: 'espnow pair <mac> <name>'.", false, cmd_espnow_pair_modern },
    { "espnow unpair", "Unpair ESP-NOW device: 'espnow unpair <mac>'.", false, cmd_espnow_unpair_modern },
    { "espnow list", "List all paired ESP-NOW devices.", false, cmd_espnow_list_modern },
    { "espnow send", "Send message: 'espnow send <mac> <message>'.", false, cmd_espnow_send_modern },
    { "espnow broadcast", "Broadcast message: 'espnow broadcast <message>'.", false, cmd_espnow_broadcast_modern },
    { "espnow remote", "Execute remote command: 'espnow remote <target> <user> <pass> <cmd>'.", false, cmd_espnow_remote_modern },
    { "espnow setpassphrase", "Set encryption passphrase: 'espnow setpassphrase \"phrase\"'.", false, cmd_espnow_setpassphrase_modern },
    { "espnow encstatus", "Show ESP-NOW encryption status and key fingerprint.", false, cmd_espnow_encstatus_modern },
    { "espnow pairsecure", "Pair device with encryption: 'espnow pairsecure <mac> <name>'.", false, cmd_espnow_pairsecure_modern },
    { "testencryption", "Test WiFi password encryption (admin only).", true, cmd_testencryption },
    { "testpassword", "Test user password hashing (admin only).", true, cmd_testpassword },

    // ---- Settings: WiFi Network (matches Settings page WiFi section) ----
    { "wifiinfo", "Show WiFi details.", false, cmd_wifiinfo_modern },
    { "wifilist", "List saved WiFi networks.", false, cmd_wifilist_modern },
    { "wifiadd", "Add/overwrite a WiFi network.", true, cmd_wifiadd_modern },
    { "wifirm", "Remove a saved WiFi network.", true, cmd_wifirm_modern },
    { "wifipromote", "Change priority of a saved WiFi network.", true, cmd_wifipromote_modern },
    { "wificonnect", "Connect to WiFi (best/index).", false, cmd_wificonnect_modern },
    { "wifidisconnect", "Disconnect WiFi and stop HTTP server.", true, cmd_wifidisconnect_modern },
    { "wifiscan", "Scan WiFi networks (plain/json).", false, cmd_wifiscan_modern },

    // ---- Settings: System Time (timezone/NTP via 'set' command) ----
    // Use: set tzoffsetminutes <mins>, set ntpserver <host>

    // ---- Settings: Output Channels (persisted + runtime) ----
    // (Moved to Output Routing section below)

    // ---- Settings: CLI History ----
    // (Moved to Settings section below)

    // ---- Settings: Sensors UI (client-side visualization) ----
    { "thermalpalettedefault", "Set thermal default palette.", true, cmd_thermalpalettedefault_modern },
    { "thermalewmafactor", "Set thermal EWMA factor.", true, cmd_thermalewmafactor_modern },
    { "thermaltransitionms", "Set thermal transition time.", true, cmd_thermaltransitionms_modern },
    { "toftransitionms", "Set ToF transition time.", true, cmd_toftransitionms_modern },
    { "tofuimaxdistancemm", "Set ToF UI max distance.", true, cmd_tofuimaxdistancemm_modern },
    // Note: thermalpollingms, tofpollingms, tofstabilitythreshold, thermalwebmaxfps moved to Thermal/Sensor Polling Settings section

    // ---- Settings: Device-side Sensor Settings ----
    // (Moved to Thermal/Sensor Polling Settings section below)

    // ---- Settings: Debug Controls (matches Settings page Debug section) ----
    // (Moved to Debug Commands section below)

    // ---- Sensors / Peripherals (start/stop and single reads) ----
    { "thermalstart", "Start MLX90640 thermal sensor.", false, cmd_thermalstart_modern },
    { "thermalstop", "Stop MLX90640 thermal sensor.", false, cmd_thermalstop_modern },
    { "tofstart", "Start VL53L4CX ToF sensor.", false, cmd_tofstart_modern },
    { "tofstop", "Stop VL53L4CX ToF sensor.", false, cmd_tofstop_modern },
    { "tof", "Read a single ToF distance.", false, cmd_tof_modern },
    { "imustart", "Start IMU sensor.", false, cmd_imustart_modern },
    { "imustop", "Stop IMU sensor.", false, cmd_imustop_modern },
    { "imu", "Read IMU data once.", false, cmd_imu_modern },
    { "apdscolor", "Read APDS9960 color values.", false, cmd_apdscolor_modern },
    { "apdsproximity", "Read APDS9960 proximity value.", false, cmd_apdsproximity_modern },
    { "apdsgesture", "Read APDS9960 gesture.", false, cmd_apdsgesture_modern },
    { "apdscolorstart", "Start APDS9960 color sensing.", false, cmd_apdscolorstart_modern },
    { "apdscolorstop", "Stop APDS9960 color sensing.", false, cmd_apdscolorstop_modern },
    { "apdsproximitystart", "Start APDS9960 proximity sensing.", false, cmd_apdsproximitystart_modern },
    { "apdsproximitystop", "Stop APDS9960 proximity sensing.", false, cmd_apdsproximitystop_modern },
    { "apdsgesturestart", "Start APDS9960 gesture sensing.", false, cmd_apdsgesturestart_modern },
    { "apdsgesturestop", "Stop APDS9960 gesture sensing.", false, cmd_apdsgesturestop_modern },

    // ---- LED Controls ----
    { "ledcolor", "Set LED color by name.", false, cmd_ledcolor_modern },
    { "ledclear", "Turn off LED.", false, cmd_ledclear_modern },
    { "ledeffect", "Run a predefined LED effect.", false, cmd_ledeffect_modern },

    // ---- Files / FS ----
    { "files", "List/inspect files.", false, cmd_files_modern },
    { "mkdir", "Create directory in LittleFS.", true, cmd_mkdir_modern },
    { "rmdir", "Remove directory in LittleFS.", true, cmd_rmdir_modern },
    { "filecreate", "Create a file (optionally with content).", true, cmd_filecreate_modern },
    { "fileview", "View a file (supports offsets).", false, cmd_fileview_modern },
    { "filedelete", "Delete a file.", true, cmd_filedelete_modern },
    { "autolog", "Automation logging: autolog start <file> | autolog stop | autolog status.", true, cmd_autolog_modern },

    // ---- Automations ----
    { "automation", "Automation list/add/enable/disable/delete/run.", true, cmd_automation_modern },
    { "downloadautomation", "Download automation from GitHub: downloadautomation url=<github-raw-url> [name=<custom-name>].", true, cmd_downloadautomation_modern },
    { "validate-conditions", "Validate conditional automation syntax: validate-conditions IF temp>75 THEN ledcolor red.", false, cmd_validate_conditions_modern },

    // ---- Users / Admin (Admin Controls section on Settings page) ----
    { "user approve", "Approve pending user.", true, cmd_user_approve_modern },
    { "user deny", "Deny pending user.", true, cmd_user_deny_modern },
    { "user promote", "Promote an existing user to admin.", true, cmd_user_promote_modern },
    { "user demote", "Demote an admin user to regular user.", true, cmd_user_demote_modern },
    { "user delete", "Delete an existing user.", true, cmd_user_delete_modern },
    { "user list", "List all users.", true, cmd_user_list_modern },
    { "user request", "List/submit access request.", false, cmd_user_request_modern },
    { "session list", "List all active sessions.", true, cmd_session_list_modern },
    { "session revoke", "Revoke sessions: 'session revoke sid <sid> [reason]' | 'session revoke user <username> [reason]' | 'session revoke all [reason]'.", true, cmd_session_revoke_modern },
    // Note: pending list and broadcast moved to Misc section below

    // ---- Output Routing ----
    { "outserial", "Enable/disable serial output.", true, cmd_outserial_modern },
    { "outweb", "Enable/disable web output.", true, cmd_outweb_modern },
    { "outtft", "Enable/disable TFT output.", true, cmd_outtft_modern },

    // ---- Settings ----
    { "wifiautoreconnect", "Enable/disable WiFi auto-reconnect.", true, cmd_wifiautoreconnect_modern },
    { "clihistorysize", "Set CLI history size.", true, cmd_clihistorysize_modern },

    // ---- Debug Commands ----
    { "debugauthcookies", "Debug authentication cookies.", true, cmd_debugauthcookies_modern },
    { "debughttp", "Debug HTTP requests.", true, cmd_debughttp_modern },
    { "debugsse", "Debug Server-Sent Events.", true, cmd_debugsse_modern },
    { "debugcli", "Debug CLI processing.", true, cmd_debugcli_modern },
    { "debugsensorsframe", "Debug sensor frame processing.", true, cmd_debugsensorsframe_modern },
    { "debugsensorsdata", "Debug sensor data.", true, cmd_debugsensorsdata_modern },
    { "debugsensorsgeneral", "Debug general sensor operations.", true, cmd_debugsensorsgeneral_modern },
    { "debugwifi", "Debug WiFi operations.", true, cmd_debugwifi_modern },
    { "debugstorage", "Debug storage operations.", true, cmd_debugstorage_modern },
    { "debugperformance", "Debug performance metrics.", true, cmd_debugperformance_modern },
    { "debugdatetime", "Debug date/time operations.", true, cmd_debugdatetime_modern },
    { "debugcommandflow", "Debug command flow.", true, cmd_debugcommandflow_modern },
    { "debugusers", "Debug user management.", true, cmd_debugusers_modern },

    // ---- Thermal/Sensor Polling Settings ----
    { "thermaltargetfps", "Set thermal sensor target FPS.", true, cmd_thermaltargetfps_modern },
    { "thermalwebmaxfps", "Set thermal web max FPS.", true, cmd_thermalwebmaxfps_modern },
    { "thermalinterpolationenabled", "Enable/disable thermal interpolation.", true, cmd_thermalinterpolationenabled_modern },
    { "thermalinterpolationsteps", "Set thermal interpolation steps.", true, cmd_thermalinterpolationsteps_modern },
    { "thermalinterpolationbuffersize", "Set thermal interpolation buffer size.", true, cmd_thermalinterpolationbuffersize_modern },
    { "thermaldevicepollms", "Set thermal device polling interval.", true, cmd_thermaldevicepollms_modern },
    { "tofdevicepollms", "Set ToF device polling interval.", true, cmd_tofdevicepollms_modern },
    { "imudevicepollms", "Set IMU device polling interval.", true, cmd_imudevicepollms_modern },
    { "thermalpollingms", "Set thermal polling interval.", true, cmd_thermalpollingms_modern },
    { "tofpollingms", "Set ToF polling interval.", true, cmd_tofpollingms_modern },
    { "tofstabilitythreshold", "Set ToF stability threshold.", true, cmd_tofstabilitythreshold_modern },
    { "i2cclockthermalhz", "Set I2C clock for thermal sensor.", true, cmd_i2cclockthermalhz_modern },
    { "i2cclocktofhz", "Set I2C clock for ToF sensor.", true, cmd_i2cclocktofhz_modern },

    // ---- System Diagnostics ----
    { "temperature", "Read ESP32 internal temperature.", false, cmd_temperature_modern },
    { "voltage", "Read supply voltage.", false, cmd_voltage_modern },
    { "cpufreq", "Get/set CPU frequency.", false, cmd_cpufreq_modern },
    { "taskstats", "Detailed task statistics.", false, cmd_taskstats_modern },
    { "heapfrag", "Analyze heap fragmentation.", false, cmd_heapfrag_modern },
    { "i2cscan", "Scan I2C bus for devices.", false, cmd_i2cscan_modern },
    { "i2cstats", "I2C bus statistics and errors.", false, cmd_i2cstats_modern },
    { "sensors", "List known I2C sensor database.", false, cmd_sensors_modern },
    { "sensorinfo", "Get detailed info about a specific sensor.", false, cmd_sensorinfo_modern },
    { "devices", "Show discovered I2C device registry.", false, cmd_devices_modern },
    { "discover", "Discover and register I2C devices.", false, cmd_discover_modern },
    { "devicefile", "Show device registry JSON file.", false, cmd_devicefile_modern },

    // ---- Misc ----
    { "set", "Set a named setting (key value).", false, cmd_set_modern },
    { "reboot", "Reboot the device.", true, cmd_reboot_modern },
    { "broadcast", "Send message to all or specific user.", true, cmd_broadcast_modern },
    { "pending list", "List pending user approvals.", true, cmd_pending_list_modern },
    { "wait", "Delay execution for N milliseconds: wait <ms>.", false, cmd_wait_modern },
    { "sleep", "Alias for wait: sleep <ms>.", false, cmd_wait_modern },
  };

// Minimal CLI processor used by Serial loop
static String processCommand(const String& cmd) {
  String command = cmd;
  command.trim();
  
  // Check for conditional commands first (before lowercasing to preserve case in commands)
  String upperCommand = command;
  upperCommand.toUpperCase();
  if (upperCommand.startsWith("IF ")) {
    // Validate conditional command syntax if in validation mode
    if (gCLIValidateOnly) {
      String validationResult = validateConditionalCommand(command);
      if (validationResult.length() > 0) {
        return validationResult;
      }
      return "VALID";
    }
    // Execute conditional command
    return executeConditionalCommand(command);
  }
  
  // Preserve original for argument casing; use a lowercased copy for matching
  String originalForArgs = command;
  String lc = command; lc.toLowerCase();
  // TEMP DEBUG (gated by DEBUG_CMD_FLOW): show raw command and ASCII codes for first 40 chars
  {
    String dbg = "";
    for (unsigned i = 0; i < lc.length() && i < 40; ++i) {
      char c = lc[i];
      dbg += String((int)c) + " ";
    }
    DEBUG_CMD_FLOWF("[router] raw='%s', ascii=[%s]", lc.c_str(), dbg.c_str());
    DEBUG_CMD_FLOWF("[router] startsWith(\"user request \")=%s", lc.startsWith("user request ") ? "YES" : "NO");
    DEBUG_CMD_FLOWF("[router] indexOf(\"user request\")==%d", lc.indexOf("user request"));
  }

  auto splitFirstToken = [](const String& s, String& head, String& tail) {
    int sp = s.indexOf(' ');
    if (sp < 0) {
      head = s;
      tail = String();
    } else {
      head = s.substring(0, sp);
      tail = s.substring(sp + 1);
      tail.trim();
    }
  };

  auto isMatch = [](const String& a, const char* b) {
    return a.equalsIgnoreCase(String(b));
  };

  auto findCanonical = [&](const String& verb, const CommandEntry*& out) -> bool {
    for (size_t i = 0; i < (sizeof(kCommands) / sizeof(kCommands[0])); ++i) {
      const CommandEntry& e = kCommands[i];
      if (isMatch(verb, e.name)) {
        out = &e;
        return true;
      }
    }
    out = nullptr;
    return false;
  };

  auto buildHelp = [&]() {
    String h;
    h.reserve(512);
    h += "Available commands:\n";
    for (size_t i = 0; i < (sizeof(kCommands) / sizeof(kCommands[0])); ++i) {
      const CommandEntry& e = kCommands[i];
      h += "  ";
      h += e.name;
      h += "  - ";
      h += e.help;
      h += "\n";
    }
    h += "\nTip: many commands also accept arguments. Use the Settings page for convenience.";
    return h;
  };

  // Handle CLI state transitions and help navigation BEFORE command registry
  if (gCLIState == CLI_HELP_MAIN) {
    if (command == "system") {
      gCLIState = CLI_HELP_SYSTEM;
      return renderHelpSystem();
    } else if (command == "wifi") {
      gCLIState = CLI_HELP_WIFI;
      return renderHelpWifi();
    } else if (command == "sensors") {
      gCLIState = CLI_HELP_SENSORS;
      return renderHelpSensors();
    } else if (command == "settings") {
      gCLIState = CLI_HELP_SETTINGS;
      return renderHelpSettings();
    } else if (command == "back") {
      // Already at main help menu: re-render help main and stay in help
      gCLIState = CLI_HELP_MAIN;
      return renderHelpMain();
    } else if (command == "exit") {
      return exitToNormalBanner();
    } else {
      // Exit help main and execute the entered command in normal mode
      String savedCmd = cmd;  // preserve original input (with args/case)
      return exitHelpAndExecute(savedCmd);
    }
  } else if (gCLIState == CLI_HELP_SYSTEM || gCLIState == CLI_HELP_WIFI || gCLIState == CLI_HELP_SENSORS || gCLIState == CLI_HELP_SETTINGS) {
    if (command == "back") {
      gCLIState = CLI_HELP_MAIN;
      return renderHelpMain();
    } else if (command == "exit") {
      return exitToNormalBanner();
    } else {
      // Exit help and execute the entered command in normal mode
      String savedCmd = cmd;  // preserve original input (with args/case)
      return exitHelpAndExecute(savedCmd);
    }
  }

  // Option A: prefix-based registry matching to support multi-word commands
  const CommandEntry* found = nullptr;
  size_t foundLen = 0;
  for (size_t i = 0; i < (sizeof(kCommands) / sizeof(kCommands[0])); ++i) {
    const char* nm = kCommands[i].name;
    size_t nlen = strlen(nm);
    if (lc.length() >= nlen && lc.substring(0, nlen).equalsIgnoreCase(String(nm)) &&
        (lc.length() == nlen || lc.charAt(nlen) == ' ')) {
      found = &kCommands[i];
      foundLen = nlen;
      break;
    }
  }

  if (found) {
    // Admin gating now handled by executeCommand pipeline
    // (Legacy requiresAdmin flag no longer needed)
    // Normalize: rebuild input using the canonical name + trailing args (preserve original arg casing)
    String args = originalForArgs.substring(foundLen);
    args.trim();
    command = String(found->name);
    if (args.length()) {
      command += " ";
      command += args;
    }

    // During validation, do not execute handlers; just report VALID if recognized
    if (gCLIValidateOnly) {
      return "VALID";
    }
    // Check if command has a modern function pointer handler
    if (found->handler != nullptr) {
      return found->handler(command);
    }

    // No legacy commands remaining - all commands now use modern routing!
  } else {
    // Command not found in registry - handle validation properly
    if (gCLIValidateOnly) {
      return "Error: Unknown command '" + cmd + "'. Type 'help' for available commands.";
    }
    // Avoid Serial-only debug; return result for unified broadcast by caller
    String result = "Unknown command: " + cmd + "\nType 'help' for available commands";
    return result;
  }

  // (Removed temporary fallback routing; prefix-based registry now handles multi-word commands)

  DEBUG_CLIF("processCommand called with: '%s' -> '%s'", redactCmdForAudit(cmd).c_str(), redactCmdForAudit(command).c_str());
  DEBUG_CMD_FLOWF("[proc] normalized='%s'", command.c_str());

  // Handle clear command first, regardless of CLI state
  if (command == "clear") {
    RETURN_VALID_IF_VALIDATE();
    if (!gWebMirror.buf) { gWebMirror.init(gWebMirrorCap); }
    gWebMirror.clear();
    gHiddenHistory = "";  // Also clear hidden history
    return "\033[2J\033[H"
           "CLI history cleared.";
  }

  // Handle help command in normal mode
  if (gCLIState == CLI_NORMAL && command == "help") {
    // Swap history: hide current CLI output while in help
    gHiddenHistory = gWebMirror.snapshot();
    if (!gWebMirror.buf) { gWebMirror.init(gWebMirrorCap); }
    gWebMirror.clear();
    gCLIState = CLI_HELP_MAIN;
    return renderHelpMain();
  }

  // Normal CLI commands (processed after help mode logic)
REPROCESS_NORMAL_COMMANDS:
  // Automations commands (open to all authenticated users for now; TODO: admin-gate mutations later)
  
  // DISABLED: automation commands - now handled by early routing
  /*
  if (command == "automation list") {
    RETURN_VALID_IF_VALIDATE();
    String json;
    if (!readText(AUTOMATIONS_JSON_FILE, json)) return "Error: failed to read automations.json";
    return json;
  } else if (command.startsWith("automation add ")) {
    RETURN_VALID_IF_VALIDATE();
    // Parse from original cmd to preserve casing/spacing in values
    String args = cmd.substring(String("automation add ").length());
    args.trim();
    auto getVal = [&](const String& key) {
      String k = key + "=";
      int p = args.indexOf(k);
      if (p < 0) return String("");
      int start = p + k.length();
      int end = -1;
      
      // Find the next key= pattern or end of string
      for (int i = start; i < (int)args.length(); i++) {
        if (args[i] == ' ' && i + 1 < (int)args.length()) {
          // Look ahead for key= pattern
          int nextSpace = args.indexOf(' ', i + 1);
          int nextEquals = args.indexOf('=', i + 1);
          if (nextEquals > 0 && (nextSpace < 0 || nextEquals < nextSpace)) {
            end = i;
            break;
          }
        }
      }
      if (end < 0) end = args.length();
      return args.substring(start, end);
    };
    String name = getVal("name");
    String type = getVal("type");
    String timeS = getVal("time");
    String days = getVal("days");
    String delayMs = getVal("delayms");
    String intervalMs = getVal("intervalms");
    String cmdStr = getVal("command");
    String enabledStr = getVal("enabled");
    bool enabled = (enabledStr.equalsIgnoreCase("1") || enabledStr.equalsIgnoreCase("true") || enabledStr.equalsIgnoreCase("yes"));
    String typeNorm = type; typeNorm.trim(); typeNorm.toLowerCase();
    DEBUGF(DEBUG_CLI | DEBUG_AUTOMATIONS, "[autos add] name='%s' type='%s' time='%s' days='%s' delayms='%s' intervalms='%s' enabled=%d",
               name.c_str(), typeNorm.c_str(), timeS.c_str(), days.c_str(), delayMs.c_str(), intervalMs.c_str(), enabled?1:0);
    if (name.length() == 0 || typeNorm.length() == 0 || cmdStr.length() == 0) {
      return "Usage: automation add name=<name> type=<atTime|afterDelay|interval> command=<cmd> [time=HH:MM] [days=mon,tue,...] [delayms=N] [intervalms=N] [enabled=0|1]";
    }
    // Type-specific validation
    auto isNumeric = [&](const String& s){ if(!s.length()) return false; for (size_t i=0;i<s.length();++i){ char c=s[i]; if (c<'0'||c>'9') return false; } return true; };
    if (typeNorm == "attime") {
      timeS.trim();
      if (timeS.length() == 0) return "Error: atTime requires time=HH:MM";
      if (!(timeS.length()==5 && timeS[2]==':' && isdigit(timeS[0]) && isdigit(timeS[1]) && isdigit(timeS[3]) && isdigit(timeS[4]))) {
        return "Error: time must be HH:MM";
      }
    } else if (typeNorm == "afterdelay") {
      if (!isNumeric(delayMs)) return "Error: afterDelay requires numeric delayms (milliseconds)";
    } else if (typeNorm == "interval") {
      if (!isNumeric(intervalMs)) return "Error: interval requires numeric intervalms (milliseconds)";
    } else {
      return "Error: invalid type (expected atTime|afterDelay|interval)";
    }
    // Ensure automations.json exists; create default if missing
    String json;
    bool hadFile = readText(AUTOMATIONS_JSON_FILE, json);
    if (!hadFile || json.length() == 0) {
      json = String("{\n  \"version\": 1,\n  \"automations\": []\n}\n");
      writeAutomationsJsonAtomic(json); // best-effort create file
      DEBUGF(DEBUG_CLI | DEBUG_AUTOMATIONS, "[autos add] created default automations.json");
    }
    int arrStart = json.indexOf("\"automations\"");
    int bracket = (arrStart >= 0) ? json.indexOf('[', arrStart) : -1;
    int lastBracket = -1;
    if (bracket >= 0) {
      int depth = 0;
      for (int i = bracket; i < (int)json.length(); ++i) {
        char c = json[i];
        if (c == '[') depth++;
        else if (c == ']') { depth--; if (depth == 0) { lastBracket = i; break; } }
      }
    }
    if (lastBracket < 0) return "Error: malformed automations.json";
    String between = json.substring(bracket + 1, lastBracket); between.trim();
    bool empty = (between.length() == 0);
    unsigned long id = millis();
    String obj = "{\n";
    obj += "  \"id\": " + String(id) + ",\n";
    obj += "  \"name\": \"" + jsonEscape(name) + "\",\n";
    obj += "  \"enabled\": " + String(enabled ? "true" : "false") + ",\n";
    obj += "  \"type\": \"" + typeNorm + "\",\n";
    if (typeNorm == "attime" && timeS.length() > 0) obj += "  \"time\": \"" + jsonEscape(timeS) + "\",\n";
    if (typeNorm == "attime" && days.length() > 0) obj += "  \"days\": \"" + jsonEscape(days) + "\",\n";
    if (typeNorm == "afterdelay" && delayMs.length() > 0) obj += "  \"delayMs\": " + delayMs + ",\n";
    if (typeNorm == "interval" && intervalMs.length() > 0) obj += "  \"intervalMs\": " + intervalMs + ",\n";
    obj += "  \"command\": \"" + jsonEscape(cmdStr) + "\"\n";
    obj += "}";
    String insert = empty ? ("\n" + obj + "\n") : (",\n" + obj + "\n");
    json = json.substring(0, lastBracket) + insert + json.substring(lastBracket);
    if (!writeAutomationsJsonAtomic(json)) return "Error: failed to write automations.json";
    DEBUG_CLIF("[autos add] wrote automations.json (len=%d) id=%lu", json.length(), id);
    // Immediate scheduler refresh only for afterDelay/interval (not atTime)
    if (typeNorm != "attime") {
      gAutosDirty = true;
      DEBUG_CLIF("[autos add] immediate scheduler refresh queued (type=%s)", typeNorm.c_str());
    } else {
      DEBUG_CLIF("[autos add] no immediate refresh for atTime");
    }
    return "Added automation '" + name + "' id=" + String(id);
  } else if (command.startsWith("automation enable ") || command.startsWith("automation disable ")) {
    RETURN_VALID_IF_VALIDATE();
    bool enable = command.startsWith("automation enable ");
    String args = cmd.substring(enable ? String("automation enable ").length() : String("automation disable ").length());
    args.trim();
    auto getVal = [&](const String& key){ String k = key + "="; int p = args.indexOf(k); if (p<0) return String(""); int s=p+k.length(); int e=args.indexOf(' ', s); if(e<0)e=args.length(); return args.substring(s,e); };
    String idStr = getVal("id"); if(idStr.length()==0) return String("Usage: automation ") + (enable?"enable":"disable") + " id=<id>";
    String json; if(!readText(AUTOMATIONS_JSON_FILE,json)) return "Error: failed to read automations.json";
    String needle = String("\"id\": ") + idStr; int idPos = json.indexOf(needle); if (idPos<0) return "Error: automation id not found";
    int objStart = json.lastIndexOf('{', idPos); if(objStart<0) return "Error: malformed automations.json (objStart)";
    int depth=0, objEnd=-1; for (int i=objStart;i<(int)json.length();++i){ char c=json[i]; if(c=='{')depth++; else if(c=='}'){ depth--; if(depth==0){ objEnd=i; break; } } }
    if (objEnd<0) return "Error: malformed automations.json (objEnd)";
    String obj = json.substring(objStart, objEnd+1);
    int enPos = obj.indexOf("\"enabled\"");
    if (enPos>=0) {
      int colon = obj.indexOf(':', enPos);
      if (colon>0) {
        int stop = obj.indexOf(',', colon+1);
        int stop2 = obj.indexOf('}', colon+1);
        if (stop<0 || (stop2>=0 && stop2<stop)) stop = stop2;
        if (stop>0) {
          String before = obj.substring(0, colon+1);
          String after = obj.substring(stop);
          obj = before + String(enable ? " true" : " false") + after;
        }
      }
    } else {
      int insertAt = obj.indexOf('\n'); if (insertAt<0) insertAt = 1; else insertAt++;
      String ins = String("  \"enabled\": ") + (enable?"true":"false") + ",\n";
      obj = obj.substring(0, insertAt) + ins + obj.substring(insertAt);
    }
    json = json.substring(0, objStart) + obj + json.substring(objEnd+1);
    if (!writeAutomationsJsonAtomic(json)) return "Error: failed to write automations.json";
    gAutosDirty = true;
    return String(enable?"Enabled":"Disabled") + " automation id=" + idStr;
  } else if (command.startsWith("automation delete ")) {
    RETURN_VALID_IF_VALIDATE();
    String args = cmd.substring(String("automation delete ").length());
    args.trim();
    auto getVal = [&](const String& key){ String k = key + "="; int p = args.indexOf(k); if (p<0) return String(""); int s=p+k.length(); int e=args.indexOf(' ', s); if(e<0)e=args.length(); return args.substring(s,e); };
    String idStr = getVal("id"); if(idStr.length()==0) return "Usage: automation delete id=<id>";
    String json; if(!readText(AUTOMATIONS_JSON_FILE,json)) return "Error: failed to read automations.json";
    String needle = String("\"id\": ") + idStr; int idPos = json.indexOf(needle); if (idPos<0) return "Error: automation id not found";
    int objStart = json.lastIndexOf('{', idPos); if (objStart<0) return "Error: malformed JSON";
    int objEnd = json.indexOf('}', idPos); if (objEnd<0) return "Error: malformed JSON";
    bool isFirst = (objStart > 0 && json[objStart-1] == '[');
    bool isLast = (objEnd+1 < (int)json.length() && json[objEnd+1] == ']');
    int delStart = objStart, delEnd = objEnd + 1;
    if (!isFirst && !isLast) { delStart = json.lastIndexOf(',', objStart); if (delStart<0) delStart=objStart; }
    else if (isFirst && !isLast) { delEnd = json.indexOf(',', objEnd); if (delEnd>=0) delEnd++; else delEnd=objEnd+1; }
    json = json.substring(0, delStart) + json.substring(delEnd);
    if (!writeAutomationsJsonAtomic(json)) return "Error: failed to write automations.json";
    gAutosDirty = true;
    return "Deleted automation id=" + idStr;
  } else if (command.startsWith("set ")) {
    RETURN_VALID_IF_VALIDATE();
    String args = cmd.substring(4); // "set "
    args.trim();
    int spacePos = args.indexOf(' ');
    if (spacePos < 0) return "Usage: set <setting> <value>";
    String setting = args.substring(0, spacePos);
    String value = args.substring(spacePos + 1);
    setting.trim();
    value.trim();
    setting.toLowerCase();
    
    if (setting == "tzoffsetminutes") {
      int offset = value.toInt();
      if (offset < -720 || offset > 720) return "Error: timezone offset must be between -720 and 720 minutes";
      gSettings.tzOffsetMinutes = offset;
      saveUnifiedSettings();
      setupNTP(); // Re-configure time with new offset
      return "Timezone offset set to " + String(offset) + " minutes";
    } else if (setting == "ntpserver") {
      if (value.length() == 0) return "Error: NTP server cannot be empty";
      
      // Test NTP server connectivity with direct UDP request
      WiFiUDP udp;
      IPAddress ntpIP;
      
      // Try to resolve hostname
      if (!WiFi.hostByName(value.c_str(), ntpIP)) {
        return "Error: Cannot resolve NTP server hostname '" + value + "'";
      }
      
      // Send NTP request packet
      byte ntpPacket[48];
      memset(ntpPacket, 0, 48);
      ntpPacket[0] = 0b11100011;   // LI, Version, Mode
      ntpPacket[1] = 0;           // Stratum
      ntpPacket[2] = 6;           // Polling Interval
      ntpPacket[3] = 0xEC;        // Peer Clock Precision
      
      udp.begin(8888);
      if (!udp.beginPacket(ntpIP, 123)) {
        udp.stop();
        return "Error: Cannot connect to NTP server '" + value + "'";
      }
      
      udp.write(ntpPacket, 48);
      if (!udp.endPacket()) {
        udp.stop();
        return "Error: Failed to send NTP request to '" + value + "'";
      }
      
      // Wait for response (up to 5 seconds)
      unsigned long startTime = millis();
      int packetSize = 0;
      while (millis() - startTime < 5000) {
        packetSize = udp.parsePacket();
        if (packetSize >= 48) break;
        delay(10);
      }
      
      udp.stop();
      
      if (packetSize < 48) {
        DEBUG_DATETIMEF("NTP validation failed: no valid response from %s (packet size: %d)", value.c_str(), packetSize);
        return "Error: No response from NTP server '" + value + "'. Server may be down or not an NTP server.";
      }
      
      DEBUG_DATETIMEF("NTP validation successful: %s responded with %d byte packet", value.c_str(), packetSize);
      
      // NTP test passed, save settings
      gSettings.ntpServer = value;
      saveUnifiedSettings();
      setupNTP(); // Apply new NTP server
      DEBUG_DATETIMEF("NTP server updated and applied: %s", value.c_str());
      return "NTP server set to " + value + " (connectivity verified)";
    } else if (setting == "thermalpollingms") {
      int v = value.toInt(); if (v < 50 || v > 5000) return "Error: thermalPollingMs must be 50..5000"; gSettings.thermalPollingMs = v; saveUnifiedSettings(); return String("thermalPollingMs set to ") + v;
    } else if (setting == "tofpollingms") {
      int v = value.toInt(); if (v < 50 || v > 5000) return "Error: tofPollingMs must be 50..5000"; gSettings.tofPollingMs = v; saveUnifiedSettings(); return String("tofPollingMs set to ") + v;
    } else if (setting == "tofstabilitythreshold") {
      int v = value.toInt(); if (v < 0 || v > 50) return "Error: tofStabilityThreshold must be 0..50"; gSettings.tofStabilityThreshold = v; saveUnifiedSettings(); return String("tofStabilityThreshold set to ") + v;
    } else if (setting == "thermalpalettedefault") {
      String v = value; v.trim(); v.toLowerCase(); if (!(v == "grayscale" || v == "iron" || v == "rainbow" || v == "hot" || v == "coolwarm")) return "Error: thermalPaletteDefault must be grayscale|iron|rainbow|hot|coolwarm"; gSettings.thermalPaletteDefault = v; saveUnifiedSettings(); return String("thermalPaletteDefault set to ") + v;
    } else if (setting == "thermalwebmaxfps") {
      int v = value.toInt(); if (v < 1 || v > 60) return "Error: thermalWebMaxFps must be 1..60"; gSettings.thermalWebMaxFps = v; saveUnifiedSettings(); return String("thermalWebMaxFps set to ") + v;
    } else if (setting == "thermalewmafactor") {
      float f = value.toFloat(); if (f < 0.0f || f > 1.0f) return "Error: thermalEWMAFactor must be 0..1"; gSettings.thermalEWMAFactor = f; saveUnifiedSettings(); return String("thermalEWMAFactor set to ") + String(f,3);
    } else if (setting == "thermaltransitionms") {
      int v = value.toInt(); if (v < 0 || v > 5000) return "Error: thermalTransitionMs must be 0..5000"; gSettings.thermalTransitionMs = v; saveUnifiedSettings(); return String("thermalTransitionMs set to ") + v;
    } else if (setting == "toftransitionms") {
      int v = value.toInt(); if (v < 0 || v > 5000) return "Error: tofTransitionMs must be 0..5000"; gSettings.tofTransitionMs = v; saveUnifiedSettings(); return String("tofTransitionMs set to ") + v;
    } else if (setting == "tofuimaxdistancemm") {
      int v = value.toInt(); if (v < 100 || v > 10000) return "Error: tofUiMaxDistanceMm must be 100..10000"; gSettings.tofUiMaxDistanceMm = v; saveUnifiedSettings(); return String("tofUiMaxDistanceMm set to ") + v;
    } else if (setting == "thermalinterpolationenabled") {
      String vl = value; vl.trim(); vl.toLowerCase(); int v = (vl == "1" || vl == "true") ? 1 : 0; gSettings.thermalInterpolationEnabled = (v == 1); saveUnifiedSettings(); return String("thermalInterpolationEnabled set to ") + (gSettings.thermalInterpolationEnabled ? "1" : "0");
    } else if (setting == "thermalinterpolationsteps") {
      int v = value.toInt(); if (v < 1 || v > 8) return "Error: thermalInterpolationSteps must be 1..8"; gSettings.thermalInterpolationSteps = v; saveUnifiedSettings(); return String("thermalInterpolationSteps set to ") + v;
    } else if (setting == "thermalinterpolationbuffersize") {
      int v = value.toInt(); if (v < 1 || v > 10) return "Error: thermalInterpolationBufferSize must be 1..10"; gSettings.thermalInterpolationBufferSize = v; saveUnifiedSettings(); return String("thermalInterpolationBufferSize set to ") + v;
    } else if (setting == "thermalwebclientquality") {
      int v = value.toInt(); if (v < 1 || v > 4) return "Error: thermalWebClientQuality must be 1..4"; gSettings.thermalWebClientQuality = v; saveUnifiedSettings(); return String("thermalWebClientQuality set to ") + v;
    } else {
      return "Error: unknown setting '" + setting + "'";
    }
  // DISABLED: automation run - now handled by early routing
  /*
  } else if (command.startsWith("automation run ")) {
    RETURN_VALID_IF_VALIDATE();
    String args = cmd.substring(String("automation run ").length()); args.trim();
    auto getVal = [&](const String& key){ String k = key + "="; int p = args.indexOf(k); if (p<0) return String(""); int s=p+k.length(); int e=args.indexOf(' ', s); if(e<0)e=args.length(); return args.substring(s,e); };
    String idStr = getVal("id"); if(idStr.length()==0) return "Usage: automation run id=<id>";
    String json; if(!readText(AUTOMATIONS_JSON_FILE,json)) return "Error: failed to read automations.json";
    String needle = String("\"id\": ") + idStr; int idPos = json.indexOf(needle); if(idPos<0) return "Error: automation id not found";
    int objStart = json.lastIndexOf('{', idPos); if(objStart<0) return "Error: malformed automations.json (objStart)";
    int depth=0, objEnd=-1; for (int i=objStart;i<(int)json.length();++i){ char c=json[i]; if(c=='{')depth++; else if(c=='}'){ depth--; if(depth==0){ objEnd=i; break; } } }
    if (objEnd<0) return "Error: malformed automations.json (objEnd)";
    String obj = json.substring(objStart, objEnd+1);
    String k = "\"command\""; int p = obj.indexOf(k); if(p<0) return "Error: automation has no command";
    int colon = obj.indexOf(':', p); if (colon<0) return "Error: malformed command field";
    int q1 = obj.indexOf('"', colon+1); if (q1<0) return "Error: malformed command value";
    int q2 = obj.indexOf('"', q1+1); if (q2<0) return "Error: malformed command value";
    String innerCmd = obj.substring(q1+1, q2);
    return String("[automation run] executing: ") + innerCmd + "\n" + processCommand(innerCmd);
  */

  // DISABLED: All commands below already have early routing handlers
  // Only keeping commands that don't have early routing

  if (command == "gamepad") {
    readGamepad();
    return "Gamepad data read (check serial output)";
  } else if (command.length() == 0) {
    return "";
  } else {
    // This should never be reached for commands found in registry
    if (gCLIValidateOnly) {
      return "Error: Unknown command '" + cmd + "'. Type 'help' for available commands.";
    }
    String result = "Unknown command: " + cmd + "\nType 'help' for available commands";
    return result;
  }
}

// Sensor initialization functions (restored)
bool initAPDS9960() {
  if (apds != nullptr) {
    return true;
  }
  // Ensure I2C configured
  Wire1.begin(22, 19);
  // Create object and begin
  apds = new Adafruit_APDS9960();
  if (!apds) return false;
  if (!apds->begin()) {
    delete apds;
    apds = nullptr;
    return false;
  }
  apdsConnected = true;
  rgbgestureConnected = true;
  return true;
}

bool initThermalSensor() {
  if (thermalSensor != nullptr) {
    return true;
  }
  // Configure Wire1 for STEMMA QT (SDA=22, SCL=19)
  Wire1.begin(22, 19);
  i2cSetDefaultWire1Clock();
  // Allocate sensor and begin at safe I2C speed
  thermalSensor = new Adafruit_MLX90640();
  if (!thermalSensor) return false;
  {
    Wire1ClockScope guard(100000);
    if (!thermalSensor->begin(MLX90640_I2CADDR_DEFAULT, &Wire1)) {
      delete thermalSensor;
      thermalSensor = nullptr;
      return false;
    }
  }
  // Configure sensor
  thermalSensor->setMode(MLX90640_CHESS);
  thermalSensor->setResolution(MLX90640_ADC_16BIT);
  int fps = gSettings.thermalTargetFps;
  if (fps < 1) fps = 1;
  if (fps > 8) fps = 8;
  mlx90640_refreshrate_t rate = MLX90640_1_HZ;
  if (fps >= 8) rate = MLX90640_8_HZ;
  else if (fps >= 4) rate = MLX90640_4_HZ;
  else if (fps >= 2) rate = MLX90640_2_HZ;
  else rate = MLX90640_1_HZ;
  thermalSensor->setRefreshRate(rate);
  thermalConnected = true;
  mlx90640_initialized = true;
  return true;
}

bool initToFSensor() {
  if (tofSensor != nullptr) {
    return true;
  }
  // Ensure I2C configured
  Wire1.begin(22, 19);
  i2cSetDefaultWire1Clock();
  // Probe and allocate
  {
    uint32_t tofHz = (gSettings.i2cClockToFHz > 0) ? (uint32_t)gSettings.i2cClockToFHz : 50000;
    if (tofHz < 50000) tofHz = 50000;
    if (tofHz > 400000) tofHz = 400000;
    Wire1ClockScope guard(tofHz);
    delay(200);
    Wire1.beginTransmission(0x29);
    if (Wire1.endTransmission() != 0) {
      return false;
    }
    tofSensor = new VL53L4CX();
    if (!tofSensor) return false;
  }
  // Configure and start
  tofSensor->setI2cDevice(&Wire1);
  tofSensor->setXShutPin(A1);
  VL53L4CX_Error status = tofSensor->begin();
  if (status != VL53L4CX_ERROR_NONE) {
    delete tofSensor;
    tofSensor = nullptr;
    return false;
  }
  tofSensor->VL53L4CX_Off();
  status = tofSensor->InitSensor(VL53L4CX_DEFAULT_DEVICE_ADDRESS);
  if (status != VL53L4CX_ERROR_NONE) {
    delete tofSensor;
    tofSensor = nullptr;
    return false;
  }
  (void)tofSensor->VL53L4CX_SetDistanceMode(VL53L4CX_DISTANCEMODE_LONG);
  (void)tofSensor->VL53L4CX_SetMeasurementTimingBudgetMicroSeconds(200000);
  status = tofSensor->VL53L4CX_StartMeasurement();
  if (status != VL53L4CX_ERROR_NONE) {
    delete tofSensor;
    tofSensor = nullptr;
    return false;
  }
  tofConnected = true;
  tofEnabled = true;
  return true;
}

bool initIMUSensor() {
  if (bno != nullptr) {
    broadcastOutput("IMU sensor already initialized!");
    return true;
  }

  DEBUG_SENSORSF("Starting BNO055 IMU initialization (STEMMA QT)...");

  // Configure Wire1 for STEMMA QT (SDA=22, SCL=19)
  Wire1.begin(22, 19);

  // Use scoped I2C clock management for IMU initialization
  Wire1ClockScope guard(100000);  // BNO055 max is 400kHz, use 100kHz for reliability
  DEBUG_SENSORSF("Set I2C clock to 100kHz for IMU initialization");

  // BNO055 needs time after power-up/reset before responding reliably
  delay(700);

  // Probe for possible I2C addresses (A: 0x28, B: 0x29)
  uint8_t candidateAddrs[2] = { BNO055_ADDRESS_A, BNO055_ADDRESS_B };
  int foundIndex = -1;
  for (int i = 0; i < 2; i++) {
    Wire1.beginTransmission(candidateAddrs[i]);
    uint8_t err = Wire1.endTransmission();
    if (err == 0) {
      foundIndex = i;
      break;
    }
  }

  if (foundIndex < 0) {
    Serial.println("WARNING: BNO055 not detected at 0x28 or 0x29 on Wire1 (initial probe). Will attempt init anyway with retries.");
  } else {
    DEBUG_SENSORSF("Detected BNO055 at address 0x%02X", candidateAddrs[foundIndex]);
  }

  // Retry loop with conservative I2C clocks (BNO055 doesn't like high speeds)
  const int maxAttempts = 3;
  uint32_t clocks[maxAttempts] = { 100000, 50000, 100000 };
  for (int attempt = 1; attempt <= maxAttempts; ++attempt) {
    DEBUG_SENSORSF("IMU init attempt %d/3 at I2C %lu Hz", attempt, clocks[attempt - 1]);

    // Use nested scope for each attempt's clock setting
    {
      Wire1ClockScope attemptGuard(clocks[attempt - 1]);

      // Add I2C bus recovery after clock change
      Wire1.end();
      delay(50);
      Wire1.begin(22, 19);
      delay(100);

      // If we previously created an object, clean it up before retrying
      if (bno != nullptr) {
        delete bno;
        bno = nullptr;
      }

      // If we detected an address, try that first; otherwise try both
      bool begun = false;
      for (int i = 0; i < 2 && !begun; i++) {
        uint8_t addr = (foundIndex >= 0) ? candidateAddrs[foundIndex] : candidateAddrs[i];
        DEBUG_SENSORSF("Trying BNO055 address 0x%02X", addr);
        bno = new Adafruit_BNO055(55, addr, &Wire1);
        if (bno == nullptr) {
          Serial.println("ERROR: Failed to allocate memory for BNO055 object");
          return false;
        }
        delay(20);
        if (bno->begin()) {
          begun = true;
          break;
        }
        // Failed begin on this addr
        delete bno;
        bno = nullptr;
        delay(100);
      }

      if (begun) {
        // Success! Configure the sensor
        bno->setExtCrystalUse(true);
        delay(100);

        imuConnected = true;
        imuEnabled = true;
        Serial.println("SUCCESS: BNO055 IMU sensor initialized successfully!");

        return true;
      }
    }  // attemptGuard scope ends, clock restored

    // Failed this attempt, wait before next retry
    delay(500);
  }

  // All attempts failed
  if (bno != nullptr) {
    delete bno;
    bno = nullptr;
  }

  Serial.println("ERROR: All BNO055 initialization attempts failed");
  return false;
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
  while (!apds->colorDataReady()) {
    delay(5);
  }

  // Get the data and print the different channels
  apds->getColorData(&red, &green, &blue, &clear);

  // Find closest matching color and set NeoPixel
  // RGB closestRGB;
  // String colorName = getClosestColorName(red, green, blue, closestRGB);
  // setLEDColor(closestRGB);

  String colorData = "Red: " + String(red) + ", Green: " + String(green) + ", Blue: " + String(blue) + ", Clear: " + String(clear);
  // colorData += " -> Detected: " + colorName + " (" + String(closestRGB.r) + "," + String(closestRGB.g) + "," + String(closestRGB.b) + ")";
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
  if (gesture == APDS9960_DOWN) broadcastOutput("Gesture: DOWN");
  if (gesture == APDS9960_UP) broadcastOutput("Gesture: UP");
  if (gesture == APDS9960_LEFT) broadcastOutput("Gesture: LEFT");
  if (gesture == APDS9960_RIGHT) broadcastOutput("Gesture: RIGHT");
  if (gesture == 0) broadcastOutput("No gesture detected");
}

void readIMUSensor() {
  if (!imuEnabled || !imuConnected || bno == nullptr) {
    return;
  }

  // Ensure configured I2C speed for IMU reads (scoped)
  Wire1ClockScope guard(100000);  // BNO055 safe speed

  sensors_event_t accelEvent;
  sensors_event_t gyroEvent;
  sensors_event_t oriEvent;

  // Read accelerometer (m/s^2)
  bno->getEvent(&accelEvent, Adafruit_BNO055::VECTOR_ACCELEROMETER);

  // Read gyroscope (rad/s)
  bno->getEvent(&gyroEvent, Adafruit_BNO055::VECTOR_GYROSCOPE);

  // Orientation (Euler angles)
  bno->getEvent(&oriEvent, Adafruit_BNO055::VECTOR_EULER);

  // Temperature (C)
  int8_t t = bno->getTemp();

  // Update sensor cache with thread safety first (reduce stack usage)
  if (lockSensorCache(pdMS_TO_TICKS(50))) {  // 50ms timeout
    gSensorCache.accelX = accelEvent.acceleration.x;
    gSensorCache.accelY = accelEvent.acceleration.y;
    gSensorCache.accelZ = accelEvent.acceleration.z;
    gSensorCache.gyroX = gyroEvent.gyro.x;
    gSensorCache.gyroY = gyroEvent.gyro.y;
    gSensorCache.gyroZ = gyroEvent.gyro.z;
    gSensorCache.oriYaw = oriEvent.orientation.x;
    gSensorCache.oriPitch = oriEvent.orientation.y;
    gSensorCache.oriRoll = oriEvent.orientation.z;
    gSensorCache.imuTemp = (float)t;
    gSensorCache.imuLastUpdate = millis();
    gSensorCache.imuDataValid = true;
    gSensorCache.imuSeq++;  // Increment sequence number
    unlockSensorCache();

    // Broadcast simple status message (avoid large String objects on stack)
    broadcastOutput("IMU data updated");
  } else {
    if (gDebugFlags & DEBUG_SENSORS_FRAME) {
      Serial.println("[DEBUG_SENSORS_FRAME] readIMUSensor() failed to lock cache - skipping update");
    }
  }
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

  // I2C clock held steady while ToF is enabled; no per-read toggling here

  VL53L4CX_MultiRangingData_t MultiRangingData;
  VL53L4CX_MultiRangingData_t* pMultiRangingData = &MultiRangingData;
  uint8_t NewDataReady = 0;
  VL53L4CX_Error status;

  // Wait for data ready with optimized timeout for 200ms timing budget
  unsigned long startTime = millis();
  do {
    status = tofSensor->VL53L4CX_GetMeasurementDataReady(&NewDataReady);
    if (millis() - startTime > 250) {  // 250ms timeout for 200ms measurement + margin
      return false;                    // Timeout - sensor may be stuck
    }
    if (status != VL53L4CX_ERROR_NONE) {
      return false;  // Communication error
    }
  } while (!NewDataReady);

  if ((!status) && (NewDataReady != 0)) {
    status = tofSensor->VL53L4CX_GetMultiRangingData(pMultiRangingData);

    // Check for data retrieval errors like ST's example
    if (status != VL53L4CX_ERROR_NONE) {
      return false;
    }

    int no_of_object_found = pMultiRangingData->NumberOfObjectsFound;

    // Update ToF cache with thread safety
    if (!lockSensorCache(pdMS_TO_TICKS(50))) {  // 50ms timeout
      return false;                             // Failed to acquire lock
    }

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
      bool isValid = (range_status != VL53L4CX_RANGESTATUS_SIGNAL_FAIL && range_status != VL53L4CX_RANGESTATUS_SIGMA_FAIL && range_status != VL53L4CX_RANGESTATUS_WRAP_TARGET_FAIL && range_status != VL53L4CX_RANGESTATUS_XTALK_SIGNAL_FAIL);

      // Distance-based signal quality requirements
      float minSignalRate;
      if (range_mm < 1000) {
        minSignalRate = 0.1;  // Close range: require good signal
      } else if (range_mm < 3000) {
        minSignalRate = 0.05;  // Medium range: lower threshold
      } else {
        minSignalRate = 0.02;  // Long range: very low threshold for wall detection
      }

      bool hasGoodSignal = (signal_rate > minSignalRate);

      if (gDebugFlags & DEBUG_SENSORS_FRAME) {
        Serial.printf("[DEBUG_SENSORS_FRAME] ToF obj[%d]: range=%dmm, status=%d, signal=%.3f (min=%.3f), isValid=%d, hasGoodSignal=%d\n",
                      j, range_mm, range_status, signal_rate, minSignalRate, isValid ? 1 : 0, hasGoodSignal ? 1 : 0);
      }

      if (isValid && hasGoodSignal && range_mm > 0 && range_mm <= 6000 && validObjectIndex < 4) {
        float distance_cm = range_mm / 10.0;

        // Apply distance-based smoothing - more smoothing for far objects
        float alpha;
        if (range_mm > 3000) {
          alpha = 0.15;  // Heavy smoothing for long range to reduce flickering
        } else if (range_mm > 1000) {
          alpha = 0.25;  // Medium smoothing for medium range
        } else {
          alpha = 0.4;  // Light smoothing for close range (more responsive)
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
    gSensorCache.tofSeq++;  // Increment sequence number

    if (gDebugFlags & DEBUG_SENSORS_FRAME) {
      Serial.printf("[DEBUG_SENSORS_FRAME] readToFObjects: found=%d, valid=%d, seq=%d\n",
                    no_of_object_found, validObjectIndex, gSensorCache.tofSeq);
    }

    unlockSensorCache();

    // Clear interrupt and restart
    tofSensor->VL53L4CX_ClearInterruptAndStartMeasurement();

    return true;
  }

  return false;
}

String getToFDataJSON() {
  String json = "";

  if (lockSensorCache(pdMS_TO_TICKS(100))) {  // 100ms timeout for HTTP response
    if (!gSensorCache.tofDataValid) {
      if (gDebugFlags & DEBUG_SENSORS_FRAME) {
        Serial.printf("[DEBUG_SENSORS_FRAME] getToFDataJSON: tofDataValid=false, tofEnabled=%d, tofConnected=%d, lastUpdate=%lu\n",
                      tofEnabled ? 1 : 0, tofConnected ? 1 : 0, gSensorCache.tofLastUpdate);
      }
      json = "{\"error\":\"ToF sensor not ready\"}";
      unlockSensorCache();
      return json;
    }

    // Build JSON response from cached data
    json = "{\"objects\":[";

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
    json += "\"seq\":" + String(gSensorCache.tofSeq) + ",";
    json += "\"timestamp\":" + String(gSensorCache.tofLastUpdate) + "}";

    unlockSensorCache();
  } else {
    // Timeout - return error response
    json = "{\"error\":\"ToF cache timeout\"}";
  }

  return json;
}

float readTOFDistance() {
  if (!tofConnected || !tofEnabled || tofSensor == nullptr) {
    broadcastOutput("ToF sensor not ready. Use 'tofstart' first.");
    return 999.9;
  }

  // Ensure configured I2C speed for ToF reads (scoped)
  Wire1ClockScope guard((gSettings.i2cClockToFHz > 0) ? (uint32_t)gSettings.i2cClockToFHz : 100000);

  VL53L4CX_MultiRangingData_t MultiRangingData;
  VL53L4CX_MultiRangingData_t* pMultiRangingData = &MultiRangingData;
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
  return 999.9;  // No valid measurement
}


bool readThermalPixels() {
  // DEBUG_FRAMEF("readThermalPixels() entry - sensor=%p enabled=%d frame=%p initInProgress=%d armAtMs=%lu",
  //              thermalSensor, thermalEnabled?1:0, mlx90640_frame, 0, thermalArmAtMs);
  if (gDebugFlags & DEBUG_SENSORS_FRAME) {
    Serial.println("[DEBUG_SENSORS_FRAME] readThermalPixels() entry");
  }

  if (thermalSensor == nullptr) {
    // DEBUG_FRAMEF("readThermalPixels() exit: sensor null");
    if (gDebugFlags & DEBUG_SENSORS_FRAME) {
      Serial.println("[DEBUG_SENSORS_FRAME] readThermalPixels() exit: sensor null");
    }
    return false;
  }
  if (!thermalEnabled) {
    // DEBUG_FRAMEF("readThermalPixels() exit: disabled");
    if (gDebugFlags & DEBUG_SENSORS_FRAME) {
      Serial.println("[DEBUG_SENSORS_FRAME] readThermalPixels() exit: disabled");
    }
    return false;
  }
  // Ensure thermal frame buffer is allocated
  if (!gSensorCache.thermalFrame) {
    if (lockSensorCache(pdMS_TO_TICKS(100))) {  // 100ms timeout for allocation
      if (!gSensorCache.thermalFrame) {
        gSensorCache.thermalFrame = (float*)ps_alloc(768 * sizeof(float), AllocPref::PreferPSRAM, "cache.thermal");
        if (!gSensorCache.thermalFrame) {
          if (gDebugFlags & DEBUG_SENSORS_FRAME) {
            Serial.println("[DEBUG_SENSORS_FRAME] readThermalPixels() exit: failed to allocate frame buffer");
          }
          unlockSensorCache();
          return false;
        }
        if (gDebugFlags & DEBUG_SENSORS_FRAME) {
          Serial.println("[DEBUG_SENSORS_FRAME] readThermalPixels() allocated thermal frame buffer");
        }
      }
      unlockSensorCache();
    } else {
      if (gDebugFlags & DEBUG_SENSORS_FRAME) {
        Serial.println("[DEBUG_SENSORS_FRAME] readThermalPixels() exit: failed to lock cache for allocation");
      }
      return false;
    }
  }
  // (removed thermalInitInProgress gate; init now handled in thermalTask before polling)
  if (thermalArmAtMs) {
    int32_t dt = (int32_t)(millis() - thermalArmAtMs);
    if (dt < 0) {
      // DEBUG_FRAMEF("readThermalPixels() exit: arming delay %dms remaining", (int)(-dt));
      if (gDebugFlags & DEBUG_SENSORS_FRAME) {
        Serial.printf("[DEBUG_SENSORS_FRAME] readThermalPixels() exit: arming delay %dms remaining\n", (int)(-dt));
      }
      return false;
    } else {
      thermalArmAtMs = 0;
      // DEBUG_FRAMEF("readThermalPixels() arming delay expired, proceeding");
      if (gDebugFlags & DEBUG_SENSORS_FRAME) {
        Serial.println("[DEBUG_SENSORS_FRAME] readThermalPixels() arming delay expired, proceeding");
      }
    }
  }

  // Ensure configured I2C speed for thermal reads (scoped)
  Wire1ClockScope guard((gSettings.i2cClockThermalHz > 0) ? (uint32_t)gSettings.i2cClockThermalHz : 800000);

  // Capture thermal frame with detailed error reporting
  static uint32_t lastFrameEndMs = 0;
  static float emaFps = 0.0f;
  static uint32_t frameCount = 0;
  uint32_t startTime = millis();

  // Check if sensor was properly initialized before attempting getFrame
  if (!thermalSensor || !mlx90640_initialized) {
    DEBUG_FRAMEF("Thermal sensor not properly initialized - skipping frame capture");
    return false;
  }

  // Use Adafruit method for reliable thermal data - read directly into cache
  int result = thermalSensor->getFrame(gSensorCache.thermalFrame);

  uint32_t afterCapture = millis();
  uint32_t captureTime = afterCapture - startTime;

  if (result != 0) {
    if (!gExecFromWeb) {
      broadcastOutput(String("MLX90640 frame capture failed: error=") + String(result) + ", time=" + String(captureTime) + "ms, heap=" + String(ESP.getFreeHeap()));
      // Check I2C bus status
      Wire1.beginTransmission(MLX90640_I2CADDR_DEFAULT);
      uint8_t i2c_error = Wire1.endTransmission();
      broadcastOutput(String("I2C bus check: error=") + String(i2c_error) + " (0=OK, 1=data_too_long, 2=addr_nack, 3=data_nack, 4=other)");
      if (i2c_error != 0) {
        broadcastOutput("I2C communication failure - sensor may be disconnected or bus locked");
      }
    }
    // I2C clock will be restored by Wire1ClockScope RAII

    return false;
  }

  // Process thermal data using spatial downsampling for speed
  static bool useSpatialDownsampling = true;
  static bool use8BitQuantization = false;
  int32_t sumTemp = 0;
  float minTemp = gSensorCache.thermalFrame[0];
  float maxTemp = gSensorCache.thermalFrame[0];
  int hottestX = 0, hottestY = 0;

  // 8-bit quantization: compress temperature range to uint8_t for faster processing
  static uint8_t* quantFrame = nullptr;
  if (!quantFrame) {
    quantFrame = (uint8_t*)ps_alloc(768, AllocPref::PreferPSRAM, "thermal.quant");
    if (!quantFrame) {
      DEBUG_FRAMEF("Failed to allocate thermal quantization buffer");
      return false;
    }
  }

  if (useSpatialDownsampling) {
    // Spatial downsampling: process every 4th pixel for 4x speed improvement
    // This reduces effective resolution from 32x24 to 16x12 but increases temporal resolution
    for (int row = 0; row < 24; row += 2) {
      for (int col = 0; col < 32; col += 2) {
        int i = row * 32 + col;
        if (i < 768) {
          float temp = gSensorCache.thermalFrame[i];
          sumTemp += (int32_t)(temp * 100);

          if (temp < minTemp) minTemp = temp;
          if (temp > maxTemp) {
            maxTemp = temp;
            hottestX = col;
            hottestY = row;
          }
        }
      }
    }
    // Adjust sum for reduced sample count (192 instead of 768)
    sumTemp = sumTemp * 4;  // Scale back to full frame equivalent
  } else if (use8BitQuantization) {
    // Find temperature range for quantization
    for (int i = 0; i < 768; i++) {
      float temp = gSensorCache.thermalFrame[i];
      if (temp < minTemp) minTemp = temp;
      if (temp > maxTemp) maxTemp = temp;
    }

    // Quantize to 8-bit with dynamic range mapping
    float tempRange = maxTemp - minTemp;
    float scale = (tempRange > 0) ? 255.0f / tempRange : 1.0f;

    for (int i = 0; i < 768; i++) {
      float temp = gSensorCache.thermalFrame[i];
      quantFrame[i] = (uint8_t)((temp - minTemp) * scale);
      sumTemp += (int32_t)(temp * 100);

      if (temp > gSensorCache.thermalFrame[hottestY * 32 + hottestX]) {
        hottestX = i % 32;
        hottestY = i / 32;
      }
    }
  } else {
    // Original full processing
    for (int i = 0; i < 768; i++) {
      float temp = gSensorCache.thermalFrame[i];
      sumTemp += (int32_t)(temp * 100);

      if (temp < minTemp) minTemp = temp;
      if (temp > maxTemp) {
        maxTemp = temp;
        hottestX = i % 32;
        hottestY = i / 32;
      }
    }
  }

  int32_t avgTempInt = sumTemp / 768;  // Integer average (scaled by 100)
  float avgTemp = avgTempInt / 100.0f;

  // Second pass: detect and filter outliers
  // Calculate standard deviation
  float variance = 0.0;
  for (int i = 0; i < 768; i++) {
    float diff = gSensorCache.thermalFrame[i] - avgTemp;
    variance += diff * diff;
  }
  float stdDev = sqrt(variance / 768.0);

  // Filter outliers (pixels more than 3 standard deviations from mean)
  float outlierThreshold = 3.0 * stdDev;
  float filteredMin = avgTemp + 50.0;  // Initialize to high value
  float filteredMax = avgTemp - 50.0;  // Initialize to low value
  float filteredSum = 0.0;
  int validPixels = 0;

  for (int i = 0; i < 768; i++) {
    float temp = gSensorCache.thermalFrame[i];
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
          if (dx == 0 && dy == 0) continue;  // Skip center pixel
          int nx = x + dx;
          int ny = y + dy;
          if (nx >= 0 && nx < 32 && ny >= 0 && ny < 24) {
            int neighborIdx = ny * 32 + nx;
            float neighborTemp = gSensorCache.thermalFrame[neighborIdx];
            if (abs(neighborTemp - avgTemp) <= outlierThreshold) {
              localSum += neighborTemp;
              localCount++;
            }
          }
        }
      }

      if (localCount > 0) {
        gSensorCache.thermalFrame[i] = localSum / localCount;  // Replace outlier with local average
      } else {
        gSensorCache.thermalFrame[i] = avgTemp;  // Fallback to global average
      }
    }
  }

  // Use filtered values if we have enough valid pixels
  if (validPixels > 600) {  // At least 78% of pixels are valid
    minTemp = filteredMin;
    maxTemp = filteredMax;
    avgTemp = filteredSum / validPixels;
  }

  // Update sensor cache with thread safety (buffer already allocated)
  if (lockSensorCache(pdMS_TO_TICKS(50))) {  // 50ms timeout
    // Data is already in cache - no copy needed

    // Update cache metadata
    gSensorCache.thermalMinTemp = minTemp;
    gSensorCache.thermalMaxTemp = maxTemp;
    gSensorCache.thermalAvgTemp = avgTemp;
    gSensorCache.thermalLastUpdate = millis();
    gSensorCache.thermalDataValid = true;
    gSensorCache.thermalSeq++;  // Increment sequence number

    unlockSensorCache();

    // If this was the first good frame after enabling, broadcast status now
    if (thermalPendingFirstFrame) {
      thermalPendingFirstFrame = false;
      sensorStatusBumpWith("thermal-ready");
    }
  } else {
    DEBUG_FRAMEF("Failed to lock sensor cache for thermal update - skipping");
    return false;
  }

  // I2C clock will be restored by Wire1ClockScope RAII

  // --- Debug timing and FPS telemetry ---
  uint32_t endTime = millis();
  uint32_t processingTime = endTime - afterCapture;  // stats + caching
  uint32_t totalTime = endTime - startTime;          // capture + processing
  float instFps = 0.0f;
  if (lastFrameEndMs != 0) {
    uint32_t interFrame = endTime - lastFrameEndMs;
    if (interFrame > 0) instFps = 1000.0f / (float)interFrame;
  }
  // Smooth FPS (EMA)
  if (emaFps == 0.0f && instFps > 0.0f) emaFps = instFps;
  else emaFps = 0.3f * instFps + 0.7f * emaFps;
  lastFrameEndMs = endTime;
  frameCount++;

  // Derive effective refresh enum used from target FPS mapping (1/2/4/8 Hz)
  int tfps = gSettings.thermalTargetFps;
  if (tfps < 1) tfps = 1;
  if (tfps > 8) tfps = 8;
  int effFps = (tfps >= 8) ? 8 : (tfps >= 4) ? 4
                               : (tfps >= 2) ? 2
                                             : 1;

  static uint32_t dbgCounter = 0;
  // Thermal debug flags now controlled by debug system

  // Use DEBUG_FRAMEF for thermal frame debug output when enabled
  if ((gDebugFlags & DEBUG_SENSORS_FRAME) && ((dbgCounter++ % 10) == 0)) {
    DEBUG_FRAMEF("THERM frame: cap=%dms, proc=%dms, total=%dms, fps_i=%.2f, fps_ema=%.2f, i2cHz=%d, tgtFps=%d(eff=%d), heap=%d",
                 captureTime, processingTime, totalTime, instFps, emaFps,
                 gSettings.i2cClockThermalHz, gSettings.thermalTargetFps, effFps, ESP.getFreeHeap());
  }

  if (!gExecFromWeb && (gDebugFlags & DEBUG_SENSORS_FRAME) && ((dbgCounter++ % 10) == 0)) {
    String msg = String("THERM frame: cap=") + captureTime + "ms, proc=" + processingTime + "ms, total=" + totalTime + "ms, fps_i=" + String(instFps, 2) + ", fps_ema=" + String(emaFps, 2) + ", i2cHz=" + String(gSettings.i2cClockThermalHz) + ", tgtFps=" + String(gSettings.thermalTargetFps) + "(eff=" + String(effFps) + ")" + ", heap=" + String(ESP.getFreeHeap());
    broadcastOutput(msg);

    // Optional: Print entire thermal data array for debugging
    if (gDebugFlags & DEBUG_SENSORS_DATA) {
      broadcastOutput("THERM DATA START:");
      for (int row = 0; row < 24; row++) {
        String rowData = "Row" + String(row) + ": ";
        for (int col = 0; col < 32; col++) {
          int idx = row * 32 + col;
          rowData += String(gSensorCache.thermalFrame[idx], 1) + " ";
        }
        broadcastOutput(rowData);
      }
      broadcastOutput("THERM DATA END");
    }
  }

  return true;
}

// ==========================
// WiFi and HTTP server setup
// ==========================

void setupWiFi() {
  WiFi.mode(WIFI_STA);
  
  // Inform user they can escape WiFi connection attempts
  broadcastOutput("Starting WiFi connection... (Press any key in Serial Monitor to skip)");

  // First try multi-SSID saved list
  loadWiFiNetworks();
  broadcastOutput("DEBUG: Found " + String(gWifiNetworkCount) + " saved networks");
  if (gWifiNetworkCount > 0) {
    broadcastOutput("DEBUG: First network SSID: '" + gWifiNetworks[0].ssid + "'");
  }
  bool connected = false;
  bool usedBest = false;
  if (gWifiNetworkCount > 0) {
    // Route through unified executor with a trusted system context
    AuthContext sysCtx;
    sysCtx.transport = AUTH_SYSTEM;
    sysCtx.user = "system";
    sysCtx.ip = "";
    sysCtx.path = "/system";
    CommandContext cc;
    cc.origin = ORIGIN_SYSTEM;
    cc.auth = sysCtx;
    cc.id = (uint32_t)millis();
    cc.timestampMs = (uint32_t)millis();
    cc.outputMask = CMD_OUT_LOG;
    cc.validateOnly = false;
    cc.replyHandle = nullptr;
    cc.httpReq = nullptr;
    (void)execCommandUnified(cc, String("wificonnect --best"));
    connected = WiFi.isConnected();
    usedBest = true;
  }
  if (connected) {
    // Avoid duplicate: the wificonnect --best path already logs via broadcastOutput
    if (!usedBest) {
      broadcastOutput(String("WiFi connected: ") + WiFi.localIP().toString());
    }
  } else {
    broadcastOutput("WiFi connect timed out; continuing without network");
  }
}

void startHttpServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.max_uri_handlers = 100;
  config.lru_purge_enable = true;
  if (httpd_start(&server, &config) != ESP_OK) {
    broadcastOutput("ERROR: Failed to start HTTP server");
    return;
  }

  // Define URIs
  static httpd_uri_t root = { .uri = "/", .method = HTTP_GET, .handler = handleRoot, .user_ctx = NULL };
  static httpd_uri_t loginGet = { .uri = "/login", .method = HTTP_GET, .handler = handleLogin, .user_ctx = NULL };
  static httpd_uri_t loginPost = { .uri = "/login", .method = HTTP_POST, .handler = handleLogin, .user_ctx = NULL };
  static httpd_uri_t loginSetSess = { .uri = "/login/setsession", .method = HTTP_GET, .handler = handleLoginSetSession, .user_ctx = NULL };
  static httpd_uri_t logout = { .uri = "/logout", .method = HTTP_GET, .handler = handleLogout, .user_ctx = NULL };
  static httpd_uri_t ping = { .uri = "/api/ping", .method = HTTP_GET, .handler = handlePing, .user_ctx = NULL };
  static httpd_uri_t dash = { .uri = "/dashboard", .method = HTTP_GET, .handler = handleDashboard, .user_ctx = NULL };
  static httpd_uri_t logs = { .uri = "/logs.txt", .method = HTTP_GET, .handler = handleLogs, .user_ctx = NULL };
  static httpd_uri_t settingsPage = { .uri = "/settings", .method = HTTP_GET, .handler = handleSettingsPage, .user_ctx = NULL };
  static httpd_uri_t settingsGet = { .uri = "/api/settings", .method = HTTP_GET, .handler = handleSettingsGet, .user_ctx = NULL };
  static httpd_uri_t devicesGet = { .uri = "/api/devices", .method = HTTP_GET, .handler = handleDeviceRegistryGet, .user_ctx = NULL };
  static httpd_uri_t apiNotice = { .uri = "/api/notice", .method = HTTP_GET, .handler = handleNotice, .user_ctx = NULL };
  static httpd_uri_t apiEvents = { .uri = "/api/events", .method = HTTP_GET, .handler = handleEvents, .user_ctx = NULL };
  static httpd_uri_t filesPage = { .uri = "/files", .method = HTTP_GET, .handler = handleFilesPage, .user_ctx = NULL };
  static httpd_uri_t filesList = { .uri = "/api/files/list", .method = HTTP_GET, .handler = handleFilesList, .user_ctx = NULL };
  static httpd_uri_t filesCreate = { .uri = "/api/files/create", .method = HTTP_POST, .handler = handleFilesCreate, .user_ctx = NULL };
  static httpd_uri_t filesView = { .uri = "/api/files/view", .method = HTTP_GET, .handler = handleFileView, .user_ctx = NULL };
  static httpd_uri_t filesDelete = { .uri = "/api/files/delete", .method = HTTP_POST, .handler = handleFileDelete, .user_ctx = NULL };
  static httpd_uri_t filesRead = { .uri = "/api/files/read", .method = HTTP_GET, .handler = handleFileRead, .user_ctx = NULL };
  static httpd_uri_t filesWrite = { .uri = "/api/files/write", .method = HTTP_POST, .handler = handleFileWrite, .user_ctx = NULL };
  static httpd_uri_t cliPage = { .uri = "/cli", .method = HTTP_GET, .handler = handleCLIPage, .user_ctx = NULL };
  static httpd_uri_t cliCmd = { .uri = "/api/cli", .method = HTTP_POST, .handler = handleCLICommand, .user_ctx = NULL };
  static httpd_uri_t logsGet = { .uri = "/api/cli/logs", .method = HTTP_GET, .handler = handleLogs, .user_ctx = NULL };
  static httpd_uri_t sensorsPage = { .uri = "/sensors", .method = HTTP_GET, .handler = handleSensorsPage, .user_ctx = NULL };
  static httpd_uri_t espnowPage = { .uri = "/espnow", .method = HTTP_GET, .handler = handleEspNowPage, .user_ctx = NULL };
  static httpd_uri_t automationsPage = { .uri = "/automations", .method = HTTP_GET, .handler = handleAutomationsPage, .user_ctx = NULL };
  static httpd_uri_t sensorData = { .uri = "/api/sensors", .method = HTTP_GET, .handler = handleSensorData, .user_ctx = NULL };
  static httpd_uri_t sensorsStatus = { .uri = "/api/sensors/status", .method = HTTP_GET, .handler = handleSensorsStatusWithUpdates, .user_ctx = NULL };
  static httpd_uri_t systemStatus = { .uri = "/api/system", .method = HTTP_GET, .handler = handleSystemStatus, .user_ctx = NULL };
  static httpd_uri_t automationsGet = { .uri = "/api/automations", .method = HTTP_GET, .handler = handleAutomationsGet, .user_ctx = NULL };
  static httpd_uri_t automationsExport = { .uri = "/api/automations/export", .method = HTTP_GET, .handler = handleAutomationsExport, .user_ctx = NULL };
  static httpd_uri_t outputGet = { .uri = "/api/output", .method = HTTP_GET, .handler = handleOutputGet, .user_ctx = NULL };
  static httpd_uri_t outputTemp = { .uri = "/api/output/temp", .method = HTTP_POST, .handler = handleOutputTemp, .user_ctx = NULL };
  static httpd_uri_t regPage = { .uri = "/register", .method = HTTP_GET, .handler = handleRegisterPage, .user_ctx = NULL };
  static httpd_uri_t regSubmit = { .uri = "/register/submit", .method = HTTP_POST, .handler = handleRegisterSubmit, .user_ctx = NULL };
  static httpd_uri_t adminPending = { .uri = "/api/admin/pending", .method = HTTP_GET, .handler = handleAdminPending, .user_ctx = NULL };
  static httpd_uri_t adminApprove = { .uri = "/api/admin/approve", .method = HTTP_POST, .handler = handleAdminApproveUser, .user_ctx = NULL };
  static httpd_uri_t adminDeny = { .uri = "/api/admin/reject", .method = HTTP_POST, .handler = handleAdminDenyUser, .user_ctx = NULL };

  // Register
  httpd_register_uri_handler(server, &root);
  httpd_register_uri_handler(server, &loginGet);
  httpd_register_uri_handler(server, &loginPost);
  httpd_register_uri_handler(server, &loginSetSess);
  httpd_register_uri_handler(server, &logout);
  httpd_register_uri_handler(server, &ping);
  httpd_register_uri_handler(server, &dash);
  httpd_register_uri_handler(server, &logs);
  httpd_register_uri_handler(server, &settingsPage);
  httpd_register_uri_handler(server, &settingsGet);
  httpd_register_uri_handler(server, &devicesGet);
  httpd_register_uri_handler(server, &apiNotice);
  httpd_register_uri_handler(server, &filesPage);
  httpd_register_uri_handler(server, &filesList);
  httpd_register_uri_handler(server, &filesCreate);
  httpd_register_uri_handler(server, &filesView);
  httpd_register_uri_handler(server, &filesDelete);
  httpd_register_uri_handler(server, &filesRead);
  httpd_register_uri_handler(server, &filesWrite);
  httpd_register_uri_handler(server, &cliPage);
  httpd_register_uri_handler(server, &cliCmd);
  httpd_register_uri_handler(server, &logsGet);
  httpd_register_uri_handler(server, &sensorsPage);
  httpd_register_uri_handler(server, &espnowPage);
  httpd_register_uri_handler(server, &sensorData);
  httpd_register_uri_handler(server, &sensorsStatus);
  // SSE events endpoint for server-driven notices
  httpd_register_uri_handler(server, &apiEvents);
  httpd_register_uri_handler(server, &systemStatus);
  httpd_register_uri_handler(server, &automationsPage);
  httpd_register_uri_handler(server, &automationsGet);
  httpd_register_uri_handler(server, &automationsExport);
  httpd_register_uri_handler(server, &outputGet);
  httpd_register_uri_handler(server, &outputTemp);
  httpd_register_uri_handler(server, &regPage);
  httpd_register_uri_handler(server, &regSubmit);
  httpd_register_uri_handler(server, &adminPending);
  httpd_register_uri_handler(server, &adminApprove);
  httpd_register_uri_handler(server, &adminDeny);
  broadcastOutput("HTTP server started");
}
