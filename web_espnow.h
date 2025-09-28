#ifndef WEB_ESPNOW_H
#define WEB_ESPNOW_H

#include <Arduino.h>
#include "web_shared.h"

String getEspNowContent() {
  String inner = 
    "<style>"
    ".espnow-container { max-width: 1200px; margin: 0 auto; padding: 20px; }"
    ".espnow-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(400px, 1fr)); gap: 20px; margin-bottom: 30px; }"
    ".espnow-card { background: rgba(255,255,255,0.9); border-radius: 15px; padding: 20px; box-shadow: 0 4px 6px rgba(0,0,0,0.1); border: 1px solid rgba(255,255,255,0.2); }"
    ".espnow-title { font-size: 1.3em; font-weight: bold; margin-bottom: 10px; color: #333; display: flex; align-items: center; gap: 10px; }"
    ".espnow-description { color: #666; margin-bottom: 15px; font-size: 0.9em; }"
    ".espnow-controls { display: flex; gap: 10px; margin-bottom: 15px; flex-wrap: wrap; }"
    ".espnow-data { background: #f8f9fa; border-radius: 8px; padding: 15px; font-family: 'Courier New', monospace; font-size: 0.9em; border-left: 4px solid #007bff; min-height: 60px; color: #111; }"
    ".status-indicator { display: inline-block; width: 12px; height: 12px; border-radius: 50%; margin-right: 8px; }"
    ".status-enabled { background: #28a745; animation: pulse 2s infinite; }"
    ".status-disabled { background: #dc3545; }"
    "@keyframes pulse { 0% { opacity: 1; } 50% { opacity: 0.5; } 100% { opacity: 1; } }"
    ".device-list { background: #f8f9fa; border-radius: 8px; padding: 15px; margin-bottom: 15px; }"
    ".device-item { display: flex; justify-content: space-between; align-items: center; padding: 8px 0; border-bottom: 1px solid #dee2e6; }"
    ".device-item:last-child { border-bottom: none; }"
    ".device-mac { font-family: 'Courier New', monospace; font-weight: bold; color: #007bff; }"
    ".device-channel { color: #666; font-size: 0.9em; }"
    ".device-actions { display: flex; gap: 5px; }"
    ".btn-small { padding: 4px 8px; font-size: 0.8em; }"
    ".device-encrypted { color: #28a745; font-weight: bold; }"
    ".device-unencrypted { color: #6c757d; }"
    ".encryption-indicator { display: inline-block; width: 8px; height: 8px; border-radius: 50%; margin-left: 8px; }"
    ".encryption-enabled { background: #28a745; }"
    ".encryption-disabled { background: #6c757d; }"
    ".message-log { background: #f8f9fa; border-radius: 8px; padding: 15px; font-family: 'Courier New', monospace; font-size: 0.9em; max-height: 200px; overflow-y: auto; border: 1px solid #dee2e6; color: #111; }"
    ".message-item { margin-bottom: 5px; padding: 2px 0; }"
    ".message-log .message-item { color: #111 !important; }"
    ".message-received { color: #28a745; }"
    ".message-sent { color: #007bff; }"
    ".message-error { color: #dc3545; }"
    ".input-group { display: flex; gap: 10px; margin-bottom: 10px; }"
    ".input-group input { flex: 1; }"
    ".mac-input { font-family: 'Courier New', monospace; }"
    ".espnow-container > .espnow-card + .espnow-card { margin-top: 30px; }"
    "</style>"
    
    "<div class='espnow-container'>"
    "<div class='espnow-grid'>"
    
    // ESP-NOW Status Card
    "<div class='espnow-card'>"
    "<div class='espnow-title'>"
    "<span>ESP-NOW Status</span>"
    "<span class='status-indicator status-disabled' id='espnow-status-indicator'></span>"
    "</div>"
    "<div class='espnow-description'>ESP-NOW wireless communication protocol for direct device-to-device messaging.</div>"
    "<div class='espnow-controls'>"
    "<button class='btn' id='btn-espnow-init'>Initialize ESP-NOW</button>"
    "<button class='btn' id='btn-espnow-refresh'>Refresh Status</button>"
    "</div>"
    "<div class='espnow-data' id='espnow-status-data'>Click 'Refresh Status' to check ESP-NOW status...</div>"
    "</div>"
    
    // Encryption Settings Card
    "<div class='espnow-card'>"
    "<div class='espnow-title'>Encryption Settings</div>"
    "<div class='espnow-description'>Set passphrase for encrypted ESP-NOW communication. All devices must use the same passphrase.</div>"
    
    "<div class='input-group'>"
    "<input type='password' id='encryption-passphrase' placeholder='Enter encryption passphrase' maxlength='64'>"
    "<button class='btn' id='btn-set-passphrase'>Set Passphrase</button>"
    "<button class='btn' id='btn-clear-passphrase'>Clear</button>"
    "</div>"
    
    "<div class='espnow-data' id='encryption-status'>No encryption passphrase set</div>"
    "</div>"
    
    // Device Management Card
    "<div class='espnow-card'>"
    "<div class='espnow-title'>Device Management</div>"
    "<div class='espnow-description'>Pair and manage ESP-NOW devices for communication.</div>"
    
    "<div class='input-group'>"
    "<input type='text' id='pair-mac' class='mac-input' placeholder='e8:6b:ea:30:4:d4' maxlength='17'>"
    "<input type='text' id='pair-name' placeholder='Device Name'>"
    "<button class='btn' id='btn-pair-device'>Pair (Unencrypted)</button>"
    "<button class='btn' id='btn-pair-secure'>Pair (Encrypted)</button>"
    "</div>"
    
    "<div class='espnow-controls'>"
    "<button class='btn' id='btn-list-devices'>List Devices</button>"
    "</div>"
    
    "<div class='device-list' id='device-list'>"
    "<div style='color: #666; text-align: center;'>No devices paired yet</div>"
    "</div>"
    "</div>"
    
    "</div>" // End first row
    
    // Messaging Card (full width)
    "<div class='espnow-card'>"
    "<div class='espnow-title'>Messaging</div>"
    "<div class='espnow-description'>Send messages to paired ESP-NOW devices and view received messages.</div>"
    
    "<div class='input-group'>"
    "<input type='text' id='send-mac' class='mac-input' placeholder='Target MAC (or leave empty for broadcast)' maxlength='17'>"
    "<input type='text' id='send-message' placeholder='Message to send'>"
    "<button class='btn' id='btn-send-message'>Send Message</button>"
    "<button class='btn' id='btn-broadcast-message'>Broadcast</button>"
    "</div>"
    
    "<div class='espnow-controls'>"
    "<button class='btn' id='btn-clear-log'>Clear Log</button>"
    "</div>"
    
    "<div class='message-log' id='message-log'>"
    "<div style='color: #666; text-align: center;'>Message log will appear here...</div>"
    "</div>"
    "</div>"
    
    // Remote Commands Card (full width with proper spacing)
    "<div class='espnow-card'>"
    "<div class='espnow-title'>Remote Commands</div>"
    "<div class='espnow-description'>Execute commands remotely on paired ESP-NOW devices with authentication.</div>"
    
    "<div class='input-group'>"
    "<input type='text' id='remote-device' class='mac-input' placeholder='Target device name or MAC' maxlength='17'>"
    "<input type='text' id='remote-username' placeholder='Username'>"
    "<input type='password' id='remote-password' placeholder='Password'>"
    "</div>"
    
    "<div class='input-group'>"
    "<input type='text' id='remote-command' placeholder='Command to execute (e.g., sensors, memory, ledcolor red)' style='flex: 2;'>"
    "<button class='btn' id='btn-send-remote'>Execute Remote Command</button>"
    "</div>"
    
    "<div class='espnow-controls'>"
    "<button class='btn btn-small' id='btn-clear-remote-log'>Clear Results</button>"
    "</div>"
    
    "<div class='message-log' id='remote-results-log'>"
    "<div style='color: #666; text-align: center;'>Remote command results will appear here...</div>"
    "</div>"
    "</div>"
    
    "</div>" // End container
    ;

  // JavaScript - Chunk 1: Global Variables
  inner += "<script>";
  inner += "(function() {";
  inner += "  try {";
  inner += "    console.log('[ESP-NOW] Chunk 1: Global variables start');";
  inner += "    window.messageCount = 0;";
  inner += "    window.maxMessages = 50;";
  inner += "    window.__espnowLogPoll = null;";
  inner += "    window.__espnowSeen = new Set();";
  inner += "    window.DEBUG_ESPNOW_RX = false;";
  inner += "    console.log('[ESP-NOW] Chunk 1: Global variables ready');";
  inner += "  } catch(e) { console.error('[ESP-NOW] Chunk 1 error:', e); }";
  inner += "})();";
  inner += "</script>";

  // JavaScript - Chunk 1b: RX Watcher Functions
  inner += "<script>";
  inner += "(function() {";
  inner += "  try {";
  inner += "    console.log('[ESP-NOW] Chunk 1b: RX functions start');";
  inner += "    window.parseAndAppendReceives = function(text) {";
  inner += "      try {";
  inner += "        const lines = (text || '').split('\\n');";
  inner += "        let found = 0;";
  inner += "        for (let i = 0; i < lines.length; i++) {";
  inner += "          const line = lines[i];";
  inner += "          if (!line || line.indexOf('[ESP-NOW] Received from ') < 0) continue;";
  inner += "          if (window.__espnowSeen.has(line)) continue;";
  inner += "          window.__espnowSeen.add(line);";
  inner += "          const fromIdx = line.indexOf('Received from ') + 'Received from '.length;";
  inner += "          let mac = line.substring(fromIdx).trim();";
  inner += "          const colonIdx = mac.indexOf(':');";
  inner += "          if (colonIdx >= 0) {";
  inner += "            const rest = mac.substring(colonIdx + 1).trim();";
  inner += "            mac = mac.substring(0, colonIdx).trim();";
  inner += "            const msg = rest.replace(/^:\\s*/, '');";
  inner += "            if (typeof addMessageToLog === 'function') {";
  inner += "              addMessageToLog('RECEIVED', 'From ' + mac + ': ' + msg);";
  inner += "              found++;";
  inner += "            }";
  inner += "          }";
  inner += "        }";
  inner += "        if (window.DEBUG_ESPNOW_RX && found > 0) {";
  inner += "          console.log('[ESP-NOW][RX] Found', found, 'new messages');";
  inner += "        }";
  inner += "      } catch(e) { console.warn('[ESP-NOW][RX] Parse error:', e); }";
  inner += "    };";
  inner += "    window.startEspNowReceiveWatcher = function() {";
  inner += "      try {";
  inner += "        if (window.__espnowLogPoll) return;";
  inner += "        console.log('[ESP-NOW] Starting RX watcher...');";
  inner += "        window.__espnowLogPoll = setInterval(function() {";
  inner += "          fetch('/logs.txt', { cache: 'no-store', credentials: 'same-origin' })";
  inner += "            .then(r => r.text())";
  inner += "            .then(t => window.parseAndAppendReceives(t))";
  inner += "            .catch(e => { if (window.DEBUG_ESPNOW_RX) console.warn('[ESP-NOW][RX] Fetch error:', e); });";
  inner += "        }, 2000);";
  inner += "      } catch(e) { console.warn('[ESP-NOW] RX watcher start error:', e); }";
  inner += "    };";
  inner += "    window.stopEspNowReceiveWatcher = function() {";
  inner += "      if (window.__espnowLogPoll) {";
  inner += "        clearInterval(window.__espnowLogPoll);";
  inner += "        window.__espnowLogPoll = null;";
  inner += "        console.log('[ESP-NOW] RX watcher stopped');";
  inner += "      }";
  inner += "    };";
  inner += "    console.log('[ESP-NOW] Chunk 1b: RX functions ready');";
  inner += "  } catch(e) { console.error('[ESP-NOW] Chunk 1b error:', e); }";
  inner += "})();";
  inner += "</script>";

  // JavaScript - Chunk 2: Status Management Functions
  inner += "<script>";
  inner += "(function() {";
  inner += "  try {";
  inner += "    console.log('[ESP-NOW] Chunk 2: Status functions start');";
  inner += "    window.refreshStatus = function() {";
  inner += "      fetch('/api/cli', {";
  inner += "        method: 'POST',";
  inner += "        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },";
  inner += "        body: 'cmd=' + encodeURIComponent('espnow status')";
  inner += "      })";
  inner += "      .then(response => response.text())";
  inner += "      .then(output => {";
  inner += "        document.getElementById('espnow-status-data').textContent = output;";
  inner += "        const indicator = document.getElementById('espnow-status-indicator');";
  inner += "        if (output.includes('Initialized: Yes')) {";
  inner += "          indicator.className = 'status-indicator status-enabled';";
  inner += "        } else {";
  inner += "          indicator.className = 'status-indicator status-disabled';";
  inner += "        }";
  inner += "      })";
  inner += "      .catch(error => {";
  inner += "        document.getElementById('espnow-status-data').textContent = 'Error: ' + error;";
  inner += "      });";
  inner += "    };";
  inner += "    console.log('[ESP-NOW] Chunk 2: Status functions ready');";
  inner += "  } catch(e) { console.error('[ESP-NOW] Chunk 2 error:', e); }";
  inner += "})();";
  inner += "</script>";

  // JavaScript - Chunk 3: Device Management Functions
  inner += "<script>";
  inner += "(function() {";
  inner += "  try {";
  inner += "    console.log('[ESP-NOW] Chunk 3: Device functions start');";
  inner += "    window.listDevices = function() {";
  inner += "      fetch('/api/cli', {";
  inner += "        method: 'POST',";
  inner += "        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },";
  inner += "        body: 'cmd=' + encodeURIComponent('espnow list')";
  inner += "      })";
  inner += "      .then(response => response.text())";
  inner += "      .then(output => {";
  inner += "        const deviceList = document.getElementById('device-list');";
  inner += "        if (output.includes('No devices paired')) {";
  inner += "          deviceList.innerHTML = '<div style=\"color: #666; text-align: center;\">No devices paired yet</div>';";
  inner += "        } else {";
  inner += "          const lines = output.split('\\n');";
  inner += "          let html = '';";
  inner += "          for (const line of lines) {";
  inner += "            if (line.includes('Channel:')) {";
  inner += "              const trimmed = line.trim();";
  inner += "              let deviceName = '';";
  inner += "              let mac = '';";
  inner += "              let channel = '?';";
  inner += "              ";
  inner += "              /* Parse format: 'left (E8:6B:EA:30:04:D4) Channel: 11' or 'E8:6B:EA:30:04:D4 (Channel: 11)' */";
  inner += "              const channelMatch = trimmed.match(/Channel:\\s*(\\d+)/);";
  inner += "              if (channelMatch) channel = channelMatch[1];";
  inner += "              ";
  inner += "              /* Check for encryption status */";
  inner += "              const isEncrypted = trimmed.includes('[ENCRYPTED]');";
  inner += "              const encryptionClass = isEncrypted ? 'device-encrypted' : 'device-unencrypted';";
  inner += "              const encryptionIndicator = isEncrypted ? 'encryption-enabled' : 'encryption-disabled';";
  inner += "              const encryptionText = isEncrypted ? 'Encrypted' : 'Unencrypted';";
  inner += "              ";
  inner += "              const macMatch = trimmed.match(/([A-Fa-f0-9:]{17})/);";
  inner += "              if (macMatch) {";
  inner += "                mac = macMatch[1].toUpperCase();";
  inner += "                ";
  inner += "                /* Check if format has device name: 'name (MAC) Channel: X' */";
  inner += "                const nameMatch = trimmed.match(/^\\s*(.+?)\\s*\\([A-Fa-f0-9:]{17}\\)/);";
  inner += "                if (nameMatch) {";
  inner += "                  deviceName = nameMatch[1].trim();";
  inner += "                }";
  inner += "                ";
  inner += "                html += '<div class=\"device-item\">';";
  inner += "                html += '<div>';";
  inner += "                if (deviceName) {";
  inner += "                  html += '<div class=\"device-mac\"><strong>' + deviceName + '</strong><span class=\"encryption-indicator ' + encryptionIndicator + '\" title=\"' + encryptionText + '\"></span></div>';";
  inner += "                  html += '<div class=\"device-channel ' + encryptionClass + '\">' + mac + ' • Channel: ' + channel + ' • ' + encryptionText + '</div>';";
  inner += "                } else {";
  inner += "                  html += '<div class=\"device-mac\">' + mac + '<span class=\"encryption-indicator ' + encryptionIndicator + '\" title=\"' + encryptionText + '\"></span></div>';";
  inner += "                  html += '<div class=\"device-channel ' + encryptionClass + '\">Channel: ' + channel + ' • ' + encryptionText + '</div>';";
  inner += "                }";
  inner += "                html += '</div>';";
  inner += "                html += '<div class=\"device-actions\">';";
  inner += "                html += '<button class=\"btn btn-small\" onclick=\"sendToDevice(\\'' + mac + '\\')\">Send</button>';";
  inner += "                html += '<button class=\"btn btn-small\" onclick=\"unpairDevice(\\'' + mac + '\\')\">Unpair</button>';";
  inner += "                html += '</div>';";
  inner += "                html += '</div>';";
  inner += "              }";
  inner += "            }";
  inner += "          }";
  inner += "          deviceList.innerHTML = html || '<div style=\"color: #666; text-align: center;\">No devices found</div>';";
  inner += "        }";
  inner += "      })";
  inner += "      .catch(error => {";
  inner += "        document.getElementById('device-list').innerHTML = '<div style=\"color: #dc3545;\">Error loading devices: ' + error + '</div>';";
  inner += "      });";
  inner += "    };";
  inner += "    window.sendToDevice = function(mac) {";
  inner += "      document.getElementById('send-mac').value = mac;";
  inner += "      document.getElementById('send-message').focus();";
  inner += "    };";
  inner += "    window.unpairDevice = function(mac) {";
  inner += "      if (confirm('Unpair device ' + mac + '?')) {";
  inner += "        fetch('/api/cli', {";
  inner += "          method: 'POST',";
  inner += "          headers: { 'Content-Type': 'application/x-www-form-urlencoded' },";
  inner += "          body: 'cmd=' + encodeURIComponent('espnow unpair ' + mac)";
  inner += "        })";
  inner += "        .then(response => response.text())";
  inner += "        .then(text => {";
  inner += "          addMessageToLog('UNPAIR', text);";
  inner += "          listDevices();";
  inner += "        })";
  inner += "        .catch(error => {";
  inner += "          addMessageToLog('ERROR', 'Unpair error: ' + error);";
  inner += "        });";
  inner += "      }";
  inner += "    };";
  inner += "    console.log('[ESP-NOW] Chunk 3: Device functions ready');";
  inner += "  } catch(e) { console.error('[ESP-NOW] Chunk 3 error:', e); }";
  inner += "})();";
  inner += "</script>";

  // JavaScript - Chunk 4: Messaging Functions
  inner += "<script>";
  inner += "(function() {";
  inner += "  try {";
  inner += "    console.log('[ESP-NOW] Chunk 4: Messaging functions start');";
  inner += "    window.sendMessage = function(mac, message) {";
  inner += "      fetch('/api/cli', {";
  inner += "        method: 'POST',";
  inner += "        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },";
  inner += "        body: 'cmd=' + encodeURIComponent('espnow send ' + mac + ' ' + message)";
  inner += "      })";
  inner += "      .then(response => response.text())";
  inner += "      .then(text => {";
  inner += "        addMessageToLog('SENT', 'To ' + mac + ': ' + message);";
  inner += "        addMessageToLog('RESULT', text);";
  inner += "        if (text && text.indexOf('Message sent') >= 0) {";
  inner += "          document.getElementById('send-message').value = '';";
  inner += "        }";
  inner += "      })";
  inner += "      .catch(error => {";
  inner += "        addMessageToLog('ERROR', 'Send error: ' + error);";
  inner += "      });";
  inner += "    };";
  inner += "    window.addMessageToLog = function(type, message) {";
  inner += "      const log = document.getElementById('message-log');";
  inner += "      const timestamp = new Date().toLocaleTimeString();";
  inner += "      let className = 'message-item';";
  inner += "      if (type === 'SENT' || type === 'BROADCAST') className += ' message-sent';";
  inner += "      else if (type === 'RECEIVED') className += ' message-received';";
  inner += "      else if (type === 'ERROR') className += ' message-error';";
  inner += "      const messageDiv = document.createElement('div');";
  inner += "      messageDiv.className = className;";
  inner += "      messageDiv.textContent = '[' + timestamp + '] ' + type + ': ' + message;";
  inner += "      if (log.children.length === 1 && log.children[0].textContent.includes('Message log will appear')) {";
  inner += "        log.innerHTML = '';";
  inner += "      }";
  inner += "      log.appendChild(messageDiv);";
  inner += "      window.messageCount++;";
  inner += "      if (window.messageCount > window.maxMessages) {";
  inner += "        log.removeChild(log.firstChild);";
  inner += "        window.messageCount--;";
  inner += "      }";
  inner += "      log.scrollTop = log.scrollHeight;";
  inner += "    };";
  inner += "    console.log('[ESP-NOW] Chunk 4: Messaging functions ready');";
  inner += "  } catch(e) { console.error('[ESP-NOW] Chunk 4 error:', e); }";
  inner += "})();";
  inner += "</script>";

  // JavaScript - Chunk 5: Button Event Handlers
  inner += "<script>";
  inner += "(function() {";
  inner += "  try {";
  inner += "    console.log('[ESP-NOW] Chunk 5: Button handlers start');";
  inner += "    window.setupButtonHandlers = function() {";
  inner += "      document.getElementById('btn-espnow-init').addEventListener('click', function() {";
  inner += "        fetch('/api/cli', {";
  inner += "          method: 'POST',";
  inner += "          headers: { 'Content-Type': 'application/x-www-form-urlencoded' },";
  inner += "          body: 'cmd=' + encodeURIComponent('espnow init')";
  inner += "        })";
  inner += "        .then(response => response.text())";
  inner += "        .then(text => {";
  inner += "          document.getElementById('espnow-status-data').textContent = text;";
  inner += "          refreshStatus();";
  inner += "        })";
  inner += "        .catch(error => {";
  inner += "          document.getElementById('espnow-status-data').textContent = 'Error: ' + error;";
  inner += "        });";
  inner += "      });";
  inner += "      document.getElementById('btn-espnow-refresh').addEventListener('click', refreshStatus);";
  inner += "      document.getElementById('btn-pair-device').addEventListener('click', function() {";
  inner += "        const mac = document.getElementById('pair-mac').value.trim();";
  inner += "        const name = document.getElementById('pair-name').value.trim();";
  inner += "        if (!mac || !name) {";
  inner += "          alert('Please enter both MAC address and device name');";
  inner += "          return;";
  inner += "        }";
  inner += "        fetch('/api/cli', {";
  inner += "          method: 'POST',";
  inner += "          headers: { 'Content-Type': 'application/x-www-form-urlencoded' },";
  inner += "          body: 'cmd=' + encodeURIComponent('espnow pair ' + mac + ' ' + name)";
  inner += "        })";
  inner += "        .then(response => response.text())";
  inner += "        .then(text => {";
  inner += "          addMessageToLog('PAIR', text);";
  inner += "          if (text && text.indexOf('Paired device') >= 0) {";
  inner += "            document.getElementById('pair-mac').value = '';";
  inner += "            document.getElementById('pair-name').value = '';";
  inner += "            listDevices();";
  inner += "          }";
  inner += "        })";
  inner += "        .catch(error => {";
  inner += "          addMessageToLog('ERROR', 'Pair error: ' + error);";
  inner += "        });";
  inner += "      });";
  inner += "      document.getElementById('btn-pair-secure').addEventListener('click', function() {";
  inner += "        const mac = document.getElementById('pair-mac').value.trim();";
  inner += "        const name = document.getElementById('pair-name').value.trim();";
  inner += "        if (!mac || !name) {";
  inner += "          alert('Please enter both MAC address and device name');";
  inner += "          return;";
  inner += "        }";
  inner += "        fetch('/api/cli', {";
  inner += "          method: 'POST',";
  inner += "          headers: { 'Content-Type': 'application/x-www-form-urlencoded' },";
  inner += "          body: 'cmd=' + encodeURIComponent('espnow pairsecure ' + mac + ' ' + name)";
  inner += "        })";
  inner += "        .then(response => response.text())";
  inner += "        .then(text => {";
  inner += "          addMessageToLog('PAIR_SECURE', text);";
  inner += "          if (text && text.indexOf('paired successfully') >= 0) {";
  inner += "            document.getElementById('pair-mac').value = '';";
  inner += "            document.getElementById('pair-name').value = '';";
  inner += "            listDevices();";
  inner += "          }";
  inner += "        })";
  inner += "        .catch(error => {";
  inner += "          addMessageToLog('ERROR', 'Secure pair error: ' + error);";
  inner += "        });";
  inner += "      });";
  inner += "      document.getElementById('btn-set-passphrase').addEventListener('click', function() {";
  inner += "        const passphrase = document.getElementById('encryption-passphrase').value.trim();";
  inner += "        if (!passphrase) {";
  inner += "          alert('Please enter a passphrase');";
  inner += "          return;";
  inner += "        }";
  inner += "        fetch('/api/cli', {";
  inner += "          method: 'POST',";
  inner += "          headers: { 'Content-Type': 'application/x-www-form-urlencoded' },";
  inner += "          body: 'cmd=' + encodeURIComponent('espnow setpassphrase \"' + passphrase + '\"')";
  inner += "        })";
  inner += "        .then(response => response.text())";
  inner += "        .then(text => {";
  inner += "          document.getElementById('encryption-status').textContent = 'Passphrase set successfully';";
  inner += "          document.getElementById('encryption-passphrase').value = '';";
  inner += "          addMessageToLog('ENCRYPTION', text);";
  inner += "        })";
  inner += "        .catch(error => {";
  inner += "          document.getElementById('encryption-status').textContent = 'Error setting passphrase';";
  inner += "          addMessageToLog('ERROR', 'Passphrase error: ' + error);";
  inner += "        });";
  inner += "      });";
  inner += "      document.getElementById('btn-clear-passphrase').addEventListener('click', function() {";
  inner += "        fetch('/api/cli', {";
  inner += "          method: 'POST',";
  inner += "          headers: { 'Content-Type': 'application/x-www-form-urlencoded' },";
  inner += "          body: 'cmd=' + encodeURIComponent('espnow clearpassphrase')";
  inner += "        })";
  inner += "        .then(response => response.text())";
  inner += "        .then(text => {";
  inner += "          document.getElementById('encryption-status').textContent = 'Passphrase cleared';";
  inner += "          addMessageToLog('ENCRYPTION', text);";
  inner += "        })";
  inner += "        .catch(error => {";
  inner += "          document.getElementById('encryption-status').textContent = 'Error clearing passphrase';";
  inner += "          addMessageToLog('ERROR', 'Clear passphrase error: ' + error);";
  inner += "        });";
  inner += "      });";
  inner += "      document.getElementById('btn-list-devices').addEventListener('click', listDevices);";
  inner += "      document.getElementById('btn-send-message').addEventListener('click', function() {";
  inner += "        const mac = document.getElementById('send-mac').value.trim();";
  inner += "        const message = document.getElementById('send-message').value.trim();";
  inner += "        if (!message) {";
  inner += "          alert('Please enter a message to send');";
  inner += "          return;";
  inner += "        }";
  inner += "        if (!mac) {";
  inner += "          alert('Please enter a MAC address or use Broadcast button');";
  inner += "          return;";
  inner += "        }";
  inner += "        sendMessage(mac, message);";
  inner += "      });";
  inner += "      document.getElementById('btn-broadcast-message').addEventListener('click', function() {";
  inner += "        const message = document.getElementById('send-message').value.trim();";
  inner += "        if (!message) {";
  inner += "          alert('Please enter a message to broadcast');";
  inner += "          return;";
  inner += "        }";
  inner += "        fetch('/api/cli', {";
  inner += "          method: 'POST',";
  inner += "          headers: { 'Content-Type': 'application/x-www-form-urlencoded' },";
  inner += "          body: 'cmd=' + encodeURIComponent('espnow broadcast ' + message)";
  inner += "        })";
  inner += "        .then(response => response.text())";
  inner += "        .then(text => {";
  inner += "          addMessageToLog('BROADCAST', text);";
  inner += "          if (text && text.indexOf('Broadcast sent') >= 0) {";
  inner += "            document.getElementById('send-message').value = '';";
  inner += "          }";
  inner += "        })";
  inner += "        .catch(error => {";
  inner += "          addMessageToLog('ERROR', 'Broadcast error: ' + error);";
  inner += "        });";
  inner += "      });";
  inner += "      document.getElementById('btn-clear-log').addEventListener('click', function() {";
  inner += "        document.getElementById('message-log').innerHTML = '<div style=\"color: #666; text-align: center;\">Message log cleared</div>';";
  inner += "        window.messageCount = 0;";
  inner += "      });";
  inner += "      /* Remote command button handlers */";
  inner += "      document.getElementById('btn-send-remote').addEventListener('click', executeRemoteCommand);";
  inner += "      document.getElementById('btn-clear-remote-log').addEventListener('click', function() {";
  inner += "        document.getElementById('remote-results-log').innerHTML = '<div style=\"color: #666; text-align: center;\">Remote command results cleared</div>';";
  inner += "      });";
  inner += "      /* Enter key support for remote command */";
  inner += "      document.getElementById('remote-command').addEventListener('keypress', function(e) {";
  inner += "        if (e.key === 'Enter') {";
  inner += "          executeRemoteCommand();";
  inner += "        }";
  inner += "      });";
  inner += "    };";
  inner += "    console.log('[ESP-NOW] Chunk 5: Button handlers ready');";
  inner += "  } catch(e) { console.error('[ESP-NOW] Chunk 5 error:', e); }";
  inner += "})();";
  inner += "</script>";

  // JavaScript - Chunk 5b: Remote Command Functions
  inner += "<script>";
  inner += "(function() {";
  inner += "  try {";
  inner += "    console.log('[ESP-NOW] Chunk 5b: Remote command functions start');";
  inner += "    window.setRemoteCommand = function(command) {";
  inner += "      document.getElementById('remote-command').value = command;";
  inner += "    };";
  inner += "    window.addRemoteResultToLog = function(type, message) {";
  inner += "      const log = document.getElementById('remote-results-log');";
  inner += "      const timestamp = new Date().toLocaleTimeString();";
  inner += "      let className = 'message-item';";
  inner += "      if (type === 'SUCCESS') className += ' message-received';";
  inner += "      else if (type === 'ERROR' || type === 'FAILED') className += ' message-error';";
  inner += "      else if (type === 'SENT') className += ' message-sent';";
  inner += "      const messageDiv = document.createElement('div');";
  inner += "      messageDiv.className = className;";
  inner += "      if (type === 'RESULT') {";
  inner += "        /* Multi-line result formatting */";
  inner += "        messageDiv.innerHTML = '<pre style=\"margin: 0; white-space: pre-wrap; font-family: inherit;\">' + message + '</pre>';";
  inner += "      } else {";
  inner += "        messageDiv.textContent = '[' + timestamp + '] ' + type + ': ' + message;";
  inner += "      }";
  inner += "      if (log.children.length === 1 && log.children[0].textContent.includes('Remote command results will appear')) {";
  inner += "        log.innerHTML = '';";
  inner += "      }";
  inner += "      log.appendChild(messageDiv);";
  inner += "      /* Limit log size */";
  inner += "      if (log.children.length > 20) {";
  inner += "        log.removeChild(log.firstChild);";
  inner += "      }";
  inner += "      log.scrollTop = log.scrollHeight;";
  inner += "    };";
  inner += "    window.executeRemoteCommand = function() {";
  inner += "      const device = document.getElementById('remote-device').value.trim();";
  inner += "      const username = document.getElementById('remote-username').value.trim();";
  inner += "      const password = document.getElementById('remote-password').value.trim();";
  inner += "      const command = document.getElementById('remote-command').value.trim();";
  inner += "      ";
  inner += "      if (!device || !username || !password || !command) {";
  inner += "        alert('Please fill in all fields: device, username, password, and command');";
  inner += "        return;";
  inner += "      }";
  inner += "      ";
  inner += "      const remoteCmd = 'espnow remote ' + device + ' ' + username + ' ' + password + ' ' + command;";
  inner += "      addRemoteResultToLog('SENT', 'Executing on ' + device + ': ' + command);";
  inner += "      ";
  inner += "      fetch('/api/cli', {";
  inner += "        method: 'POST',";
  inner += "        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },";
  inner += "        body: 'cmd=' + encodeURIComponent(remoteCmd)";
  inner += "      })";
  inner += "      .then(response => response.text())";
  inner += "      .then(text => {";
  inner += "        if (text.includes('Remote command sent')) {";
  inner += "          addRemoteResultToLog('SUCCESS', 'Command sent successfully - waiting for response...');";
  inner += "          /* Start polling for remote results */";
  inner += "          setTimeout(function() { pollForRemoteResults(device); }, 1000);";
  inner += "        } else {";
  inner += "          addRemoteResultToLog('ERROR', text);";
  inner += "        }";
  inner += "      })";
  inner += "      .catch(error => {";
  inner += "        addRemoteResultToLog('ERROR', 'Send error: ' + error);";
  inner += "      });";
  inner += "    };";
  inner += "    window.pollForRemoteResults = function(device) {";
  inner += "      /* Poll logs for remote results - look for chunked or single results */";
  inner += "      let pollCount = 0;";
  inner += "      const maxPolls = 10; /* 10 seconds max wait */";
  inner += "      ";
  inner += "      const pollInterval = setInterval(function() {";
  inner += "        pollCount++;";
  inner += "        ";
  inner += "        fetch('/logs.txt', { cache: 'no-store', credentials: 'same-origin' })";
  inner += "          .then(r => r.text())";
  inner += "          .then(logText => {";
  inner += "            /* Look for remote results from the target device */";
  inner += "            const lines = logText.split('\\n');";
  inner += "            let foundResult = false;";
  inner += "            let resultText = '';";
  inner += "            ";
  inner += "            for (let i = lines.length - 1; i >= 0 && i >= lines.length - 50; i--) {";
  inner += "              const line = lines[i];";
  inner += "              if (line.includes('[ESP-NOW] Remote result from ' + device)) {";
  inner += "                foundResult = true;";
  inner += "                /* Extract the result status */";
  inner += "                if (line.includes('(SUCCESS)')) {";
  inner += "                  addRemoteResultToLog('SUCCESS', 'Command completed successfully');";
  inner += "                } else if (line.includes('(FAILED)')) {";
  inner += "                  addRemoteResultToLog('FAILED', 'Command failed');";
  inner += "                }";
  inner += "                break;";
  inner += "              }";
  inner += "              /* Look for the actual result content (next line after result header) */";
  inner += "              if (foundResult && line.trim().length > 0 && !line.includes('[ESP-NOW]')) {";
  inner += "                resultText = line + '\\n' + resultText;";
  inner += "              }";
  inner += "            }";
  inner += "            ";
  inner += "            if (foundResult) {";
  inner += "              clearInterval(pollInterval);";
  inner += "              if (resultText.trim().length > 0) {";
  inner += "                addRemoteResultToLog('RESULT', resultText.trim());";
  inner += "              }";
  inner += "            } else if (pollCount >= maxPolls) {";
  inner += "              clearInterval(pollInterval);";
  inner += "              addRemoteResultToLog('ERROR', 'Timeout waiting for response from ' + device);";
  inner += "            }";
  inner += "          })";
  inner += "          .catch(e => {";
  inner += "            if (pollCount >= maxPolls) {";
  inner += "              clearInterval(pollInterval);";
  inner += "              addRemoteResultToLog('ERROR', 'Error polling for results: ' + e);";
  inner += "            }";
  inner += "          });";
  inner += "      }, 1000);";
  inner += "    };";
  inner += "    console.log('[ESP-NOW] Chunk 5b: Remote command functions ready');";
  inner += "  } catch(e) { console.error('[ESP-NOW] Chunk 5b error:', e); }";
  inner += "})();";
  inner += "</script>";

  // JavaScript - Chunk 6: Main Initialization
  inner += "<script>";
  inner += "(function() {";
  inner += "  try {";
  inner += "    console.log('[ESP-NOW] Chunk 6: Main init start');";
  inner += "    document.addEventListener('DOMContentLoaded', function() {";
  inner += "      console.log('[ESP-NOW] DOMContentLoaded');";
  inner += "      setupButtonHandlers();";
  inner += "      refreshStatus();";
  inner += "      listDevices();";
  inner += "      /* Start RX watcher - functions should be available by now */";
  inner += "      try {";
  inner += "        if (typeof window.startEspNowReceiveWatcher === 'function') {";
  inner += "          console.log('[ESP-NOW] Starting RX watcher...');";
  inner += "          window.startEspNowReceiveWatcher();";
  inner += "        } else {";
  inner += "          console.warn('[ESP-NOW] RX watcher function not available');";
  inner += "        }";
  inner += "      } catch (e) { console.warn('[ESP-NOW] Error starting RX watcher:', e); }";
  inner += "    });";
  inner += "    console.log('[ESP-NOW] Chunk 6: Main init ready');";
  inner += "  } catch(e) { console.error('[ESP-NOW] Chunk 6 error:', e); }";
  inner += "})();";
  inner += "</script>";

  return inner;
}

String getEspNowPage(const String& username) {
  return htmlShellWithNav(username, "espnow", getEspNowContent());
}

#endif
