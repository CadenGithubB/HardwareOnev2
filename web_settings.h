#ifndef WEB_SETTINGS_H
#define WEB_SETTINGS_H

String getSettingsPage(const String& username) {
  String inner;
  inner += "<h2>System Settings</h2>";
  inner += "<p>Configure your HardwareOne device settings</p>";
  // Page sections below render the live settings; top summary removed.
  inner += "<script>try{ console.debug('[SNSR] Top header/element ready'); }catch(_){}</script>";
  
  // WiFi Network Section (collapsible)
  inner += "<div style='background:#f8f9fa;border-radius:8px;padding:1.0rem 1.5rem;margin:1rem 0;border-left:4px solid #667eea;color:#333'>";
  inner += "  <div style='display:flex;align-items:center;justify-content:space-between'>";
  inner += "    <div><div style='font-size:1.2rem;font-weight:bold;color:#333'>WiFi Network</div><div style='color:#666;font-size:0.9rem'>Current WiFi network and connection settings.</div></div>";
  inner += "    <button class='btn' id='btn-wifi-toggle' onclick=\"togglePane('wifi-pane','btn-wifi-toggle')\">Expand</button>";
  inner += "  </div>";
  inner += "  <div id='wifi-pane' style='display:none;margin-top:0.75rem'>";
  inner += "  <div style='margin-bottom:1rem'>";
  inner += "    <span style='color:#333'>SSID: <span style='font-weight:bold;color:#667eea' id='wifi-ssid'>-</span></span>";
  inner += "  </div>";
  inner += "  <div style='display:flex;align-items:center;gap:1rem;margin-bottom:1rem;flex-wrap:wrap'>";
  inner += "    <span style='color:#333' title='Automatically reconnect to saved WiFi networks after power loss or disconnection'>Auto-Reconnect: <span style='font-weight:bold;color:#667eea' id='wifi-value'>-</span></span>";
  inner += "    <button class='btn' onclick='toggleWifi()' id='wifi-btn' title='Enable/disable automatic WiFi reconnection on boot'>Toggle</button>";
  inner += "  </div>";
  inner += "  <div style='display:flex;align-items:center;gap:1rem;flex-wrap:wrap'>";
  inner += "    <button class='btn' onclick='disconnectWifi()' title='Disconnect from current WiFi network (may lose connection to device)'>Disconnect WiFi</button>";
  inner += "    <button class='btn' onclick='scanNetworks()' title='Scan for available WiFi networks in range'>Scan Networks</button>";
  inner += "  </div>";
  inner += "  <div id='wifi-scan-results' style='margin-top:1rem'></div>";
  inner += "  <div id='wifi-connect-panel' style='display:none;margin-top:0.75rem'>";
  inner += "    <div style='margin-bottom:0.5rem'>Selected SSID: <strong id='sel-ssid'>-</strong></div>";
  inner += "    <input type='password' id='sel-pass' placeholder='WiFi password (leave blank if open)' style='padding:0.5rem;border:1px solid #ddd;border-radius:4px;width:260px'>";
  inner += "    <button class='btn' onclick=\"(function(){ var ssid=(document.getElementById('sel-ssid')||{}).textContent||''; var pass=(document.getElementById('sel-pass')||{}).value||''; if(!ssid){ alert('No SSID selected'); return; } var cmd1='wifiadd '+ssid+' '+pass+' 1 0'; fetch('/api/cli',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},credentials:'same-origin',body:'cmd='+encodeURIComponent(cmd1)}).then(function(r){return r.text();}).then(function(t1){ if(!confirm('Credentials saved for \"'+ssid+'\". Attempt to connect now? You may temporarily lose access while switching.')){ alert('Saved. You can connect later from this page.'); if(typeof refreshSettings==='function') refreshSettings(); return null; } return fetch('/api/cli',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},credentials:'same-origin',body:'cmd='+encodeURIComponent('wificonnect')}); }).then(function(r){ if(!r) return ''; return r.text(); }).then(function(t2){ if(t2){ alert(t2||'Connect attempted'); } if(typeof refreshSettings==='function') refreshSettings(); }).catch(function(e){ alert('Action failed: '+e.message); }); })();\">Connect</button>";
  inner += "  </div>";
  inner += "  <div id='wifi-manual-panel' style='display:none;margin-top:0.75rem'>";
  inner += "    <div style='margin-bottom:0.5rem'>Enter hidden network credentials</div>";
  inner += "    <input type='text' id='manual-ssid' placeholder='Hidden SSID' style='padding:0.5rem;border:1px solid #ddd;border-radius:4px;width:240px;margin-right:6px'>";
  inner += "    <input type='password' id='manual-pass' placeholder='Password (leave blank if open)' style='padding:0.5rem;border:1px solid #ddd;border-radius:4px;width:240px;margin-right:6px'>";
  inner += "    <button class='btn' onclick=\"(function(){ var ssid=(document.getElementById('manual-ssid')||{}).value||''; var pass=(document.getElementById('manual-pass')||{}).value||''; if(!ssid){ alert('Enter SSID'); return; } var cmd1='wifiadd '+ssid+' '+pass+' 1 1'; fetch('/api/cli',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},credentials:'same-origin',body:'cmd='+encodeURIComponent(cmd1)}).then(function(r){return r.text();}).then(function(t1){ if(!confirm('Credentials saved for hidden network \"'+ssid+'\". Attempt to connect now? You may temporarily lose access while switching.')){ alert('Saved. You can connect later from this page.'); if(typeof refreshSettings==='function') refreshSettings(); return null; } return fetch('/api/cli',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},credentials:'same-origin',body:'cmd='+encodeURIComponent('wificonnect')}); }).then(function(r){ if(!r) return ''; return r.text(); }).then(function(t2){ if(t2){ alert(t2||'Connect attempted'); } if(typeof refreshSettings==='function') refreshSettings(); }).catch(function(e){ alert('Action failed: '+e.message); }); })();\">Connect</button>";
  inner += "  </div>";
  inner += "  </div>";
  inner += "</div>";
  inner += "<script>try{ console.debug('[SNSR] Wifi Pane ready'); }catch(_){}</script>";

  // System Time Section (collapsible)
  inner += "<div style='background:#f8f9fa;border-radius:8px;padding:1.0rem 1.5rem;margin:1rem 0;border-left:4px solid #667eea;color:#333'>";
  inner += "  <div style='display:flex;align-items:center;justify-content:space-between'>";
  inner += "    <div><div style='font-size:1.2rem;font-weight:bold;color:#333'>System Time</div><div style='color:#666;font-size:0.9rem'>Configure timezone offset and NTP server for accurate time synchronization.</div></div>";
  inner += "    <button class='btn' id='btn-time-toggle' onclick=\"togglePane('time-pane','btn-time-toggle')\">Expand</button>";
  inner += "  </div>";
  inner += "  <div id='time-pane' style='display:none;margin-top:0.75rem'>";
  inner += "  <div style='display:flex;align-items:center;gap:1rem;margin-bottom:1rem;flex-wrap:wrap'>";
  inner += "    <span style='color:#333' title='Current timezone'>Timezone: <span style='font-weight:bold;color:#667eea' id='tz-value'>-</span></span>";
  inner += "    <select id='tz-select' style='padding:0.5rem;border:1px solid #ddd;border-radius:4px;width:250px' title='Select timezone'>";
  inner += "      <option value='-720'>UTC-12 (Baker Island)</option>";
  inner += "      <option value='-660'>UTC-11 (Hawaii-Aleutian)</option>";
  inner += "      <option value='-600'>UTC-10 (Hawaii)</option>";
  inner += "      <option value='-540'>UTC-9 (Alaska)</option>";
  inner += "      <option value='-480'>UTC-8 (Pacific)</option>";
  inner += "      <option value='-420'>UTC-7 (Mountain)</option>";
  inner += "      <option value='-360'>UTC-6 (Central)</option>";
  inner += "      <option value='-300'>UTC-5 (Eastern)</option>";
  inner += "      <option value='-240'>UTC-4 (Atlantic)</option>";
  inner += "      <option value='-180'>UTC-3 (Argentina)</option>";
  inner += "      <option value='-120'>UTC-2 (Mid-Atlantic)</option>";
  inner += "      <option value='-60'>UTC-1 (Azores)</option>";
  inner += "      <option value='0'>UTC+0 (London/Dublin)</option>";
  inner += "      <option value='60'>UTC+1 (Berlin/Paris)</option>";
  inner += "      <option value='120'>UTC+2 (Cairo/Athens)</option>";
  inner += "      <option value='180'>UTC+3 (Moscow/Baghdad)</option>";
  inner += "      <option value='240'>UTC+4 (Dubai/Baku)</option>";
  inner += "      <option value='300'>UTC+5 (Karachi/Tashkent)</option>";
  inner += "      <option value='330'>UTC+5:30 (Mumbai/Delhi)</option>";
  inner += "      <option value='360'>UTC+6 (Dhaka/Almaty)</option>";
  inner += "      <option value='420'>UTC+7 (Bangkok/Jakarta)</option>";
  inner += "      <option value='480'>UTC+8 (Beijing/Singapore)</option>";
  inner += "      <option value='540'>UTC+9 (Tokyo/Seoul)</option>";
  inner += "      <option value='570'>UTC+9:30 (Adelaide)</option>";
  inner += "      <option value='600'>UTC+10 (Sydney/Melbourne)</option>";
  inner += "      <option value='660'>UTC+11 (Solomon Islands)</option>";
  inner += "      <option value='720'>UTC+12 (Fiji/Auckland)</option>";
  inner += "    </select>";
  inner += "    <button class='btn' onclick='updateTimezone()' title='Save selected timezone'>Update</button>";
  inner += "  </div>";
  inner += "  <div style='display:flex;align-items:center;gap:1rem;flex-wrap:wrap'>";
  inner += "    <span style='color:#333' title='NTP server for time synchronization'>NTP Server: <span style='font-weight:bold;color:#667eea' id='ntp-value'>-</span></span>";
  inner += "    <input type='text' id='ntp-input' placeholder='pool.ntp.org' style='padding:0.5rem;border:1px solid #ddd;border-radius:4px;width:200px' title='Set NTP server hostname'>";
  inner += "    <button class='btn' onclick='updateNtpServer()' title='Save new NTP server'>Update</button>";
  inner += "  </div>";
  inner += "  </div>";
  inner += "</div>";
  inner += "<script>try{ console.debug('[SNSR] System Time ready'); }catch(_){}</script>";

  // Output Channels Section (collapsible)
  inner += "<div style='background:#f8f9fa;border-radius:8px;padding:1.0rem 1.5rem;margin:1rem 0;border-left:4px solid #667eea;color:#333'>";
  inner += "  <div style='display:flex;align-items:center;justify-content:space-between'>";
  inner += "    <div><div style='font-size:1.2rem;font-weight:bold;color:#333'>Output Channels</div><div style='color:#666;font-size:0.9rem'>Configure persistent settings and see current runtime state. Use \'Temp On/Off\' to affect only this session.</div></div>";
  inner += "    <button class='btn' id='btn-output-toggle' onclick=\"togglePane('output-pane','btn-output-toggle')\">Expand</button>";
  inner += "  </div>";
  inner += "  <div id='output-pane' style='display:none;margin-top:0.75rem'>";
  inner += "  <div style='display:grid;grid-template-columns:repeat(auto-fit,minmax(260px,1fr));gap:1rem'>";
  inner += "    <div style='display:flex;flex-direction:column;gap:0.35rem'>";
  inner += "      <div style='display:flex;align-items:center;gap:0.5rem'><span style='color:#333' title='Enable/disable sensor data output to serial console (saved to device memory)'>Serial (persisted): <span style='font-weight:bold;color:#667eea' id='serial-value'>-</span></span><button class='btn' onclick=\"toggleOutput('outSerial','serial')\" id='serial-btn' title='Toggle persistent serial output setting'>Toggle</button></div>";
  inner += "      <div style='display:flex;align-items:center;gap:0.5rem'><span style='color:#333' title='Current session serial output status (temporary, resets on reboot)'>Serial (runtime): <span style='font-weight:bold' id='serial-runtime'>-</span></span><button class='btn' id='serial-temp-on' onclick=\"setOutputRuntime('serial',1)\" title='Enable serial output for this session only'>Temp On</button><button class='btn' id='serial-temp-off' onclick=\"setOutputRuntime('serial',0)\" title='Disable serial output for this session only'>Temp Off</button></div>";
  inner += "    </div>";
  inner += "    <div style='display:flex;flex-direction:column;gap:0.35rem'>";
  inner += "      <div style='display:flex;align-items:center;gap:0.5rem'><span style='color:#333' title='Enable/disable sensor data output to web interface (saved to device memory)'>Web (persisted): <span style='font-weight:bold;color:#667eea' id='web-value'>-</span></span><button class='btn' onclick=\"toggleOutput('outWeb','web')\" id='web-btn' title='Toggle persistent web output setting'>Toggle</button></div>";
  inner += "      <div style='display:flex;align-items:center;gap:0.5rem'><span style='color:#333' title='Current session web output status (temporary, resets on reboot)'>Web (runtime): <span style='font-weight:bold' id='web-runtime'>-</span></span><button class='btn' id='web-temp-on' onclick=\"setOutputRuntime('web',1)\" title='Enable web output for this session only'>Temp On</button><button class='btn' id='web-temp-off' onclick=\"setOutputRuntime('web',0)\" title='Disable web output for this session only'>Temp Off</button></div>";
  inner += "    </div>";
  inner += "    <div style='display:flex;flex-direction:column;gap:0.35rem'>";
  inner += "      <div style='display:flex;align-items:center;gap:0.5rem'><span style='color:#333' title='Enable/disable sensor data output to TFT display (saved to device memory)'>TFT (persisted): <span style='font-weight:bold;color:#667eea' id='tft-value'>-</span></span><button class='btn' onclick=\"toggleOutput('outTft','tft')\" id='tft-btn' title='Toggle persistent TFT display output setting'>Toggle</button></div>";
  inner += "      <div style='display:flex;align-items:center;gap:0.5rem'><span style='color:#333' title='Current session TFT display status (temporary, resets on reboot)'>TFT (runtime): <span style='font-weight:bold' id='tft-runtime'>-</span></span><button class='btn' id='tft-temp-on' onclick=\"setOutputRuntime('tft',1)\" title='Enable TFT display for this session only'>Temp On</button><button class='btn' id='tft-temp-off' onclick=\"setOutputRuntime('tft',0)\" title='Disable TFT display for this session only'>Temp Off</button></div>";
  inner += "    </div>";
  inner += "  </div>"; // grid
  inner += "</div>";     // output-pane
  inner += "</div>";     // output wrapper
  inner += "<script>try{ console.debug('[SNSR] Output channels ready'); }catch(_){}</script>";
  
  // CLI History Section (collapsible)
  inner += "<div style='background:#f8f9fa;border-radius:8px;padding:1.0rem 1.5rem;margin:1rem 0;border-left:4px solid #667eea;color:#333'>";
  inner += "  <div style='display:flex;align-items:center;justify-content:space-between'>";
  inner += "    <div><div style='font-size:1.2rem;font-weight:bold;color:#333'>CLI History Size</div><div style='color:#666;font-size:0.9rem'>Number of commands to keep in CLI history buffer.</div></div>";
  inner += "    <button class='btn' id='btn-cli-toggle' onclick=\"togglePane('cli-pane','btn-cli-toggle')\">Expand</button>";
  inner += "  </div>";
  inner += "  <div id='cli-pane' style='display:none;margin-top:0.75rem'>";
  inner += "  <div style='display:flex;align-items:center;gap:1rem;flex-wrap:wrap'>";
  inner += "    <span style='color:#333' title='Number of CLI commands stored in history buffer for recall'>Current: <span style='font-weight:bold;color:#667eea' id='cli-value'>-</span></span>";
  inner += "    <input type='number' id='cli-input' min='1' max='100' value='10' style='padding:0.5rem;border:1px solid #ddd;border-radius:4px;width:80px' title='Set CLI history buffer size (1-100 commands)'>";
  inner += "    <button class='btn' onclick='updateCliHistory()' title='Save new CLI history buffer size'>Update</button>";
  inner += "    <button class='btn' onclick='clearCliHistory()' title='Clear all stored CLI command history'>Clear History</button>";
  inner += "  </div>"; // controls
  inner += "</div>";     // cli-pane
  inner += "</div>";     // cli wrapper
  inner += "<script>try{ console.debug('[SNSR] CLIHistorySize ready'); }catch(_){}</script>";

  // ESP-NOW Section (collapsible)
  inner += "<div style='background:#f8f9fa;border-radius:8px;padding:1.0rem 1.5rem;margin:1rem 0;border-left:4px solid #28a745;color:#333'>";
  inner += "  <div style='display:flex;align-items:center;justify-content:space-between'>";
  inner += "    <div><div style='font-size:1.2rem;font-weight:bold;color:#333'>ESP-NOW Communication</div><div style='color:#666;font-size:0.9rem'>Enable ESP-NOW wireless protocol for direct device-to-device messaging.</div></div>";
  inner += "    <button class='btn' id='btn-espnow-toggle' onclick=\"togglePane('espnow-pane','btn-espnow-toggle')\">Expand</button>";
  inner += "  </div>";
  inner += "  <div id='espnow-pane' style='display:none;margin-top:0.75rem'>";
  inner += "  <div style='display:flex;align-items:center;gap:1rem;margin-bottom:1rem;flex-wrap:wrap'>";
  inner += "    <span style='color:#333' title='Enable ESP-NOW protocol on boot (requires reboot to take effect)'>ESP-NOW Enabled: <span style='font-weight:bold;color:#28a745' id='espnow-value'>-</span></span>";
  inner += "    <button class='btn' onclick='toggleEspNow()' id='espnow-btn' title='Enable/disable ESP-NOW protocol'>Toggle</button>";
  inner += "  </div>";
  inner += "  <div style='background:#e8f5e8;border:1px solid #c3e6c3;border-radius:6px;padding:0.75rem;margin-top:0.75rem'>";
  inner += "    <div style='color:#155724;font-size:0.9rem'>";
  inner += "      <strong>Note:</strong> ESP-NOW changes take effect after reboot. When enabled, the device will automatically initialize ESP-NOW on startup.";
  inner += "    </div>";
  inner += "  </div>";
  inner += "  </div>";
  inner += "</div>";
  inner += "<script>try{ console.debug('[SNSR] ESP-NOW ready'); }catch(_){}</script>";

  // Sensors UI Settings (collapsible)
  inner += "<div style='background:#f8f9fa;border-radius:8px;padding:1.0rem 1.5rem;margin:1rem 0;border-left:4px solid #667eea;color:#333'>";
  inner += "  <div style='display:flex;align-items:center;justify-content:space-between'>";
  inner += "    <div><div style='font-size:1.2rem;font-weight:bold;color:#333'>Sensors UI Settings</div><div style='color:#666;font-size:0.9rem'>Client-side visualization behavior. Applies without reboot (may require page reload).</div></div>";
  inner += "    <button class='btn' id='btn-sui-toggle' onclick=\"togglePane('sui-pane','btn-sui-toggle')\">Expand</button>";
  inner += "  </div>";
  inner += "  <div id='sui-pane' style='display:none;margin-top:0.75rem'>";
  inner += "  <div style='display:grid;grid-template-columns:repeat(auto-fit,minmax(260px,1fr));gap:1rem'>";
  inner += "    <label title=\"How often the thermal camera UI fetches frames. Higher = less CPU/network, lower = smoother updates.\">Thermal Polling (ms)<br><input type='number' id='thermalPollingMs' min='100' max='2000' step='50' value='200' style='padding:0.5rem;border:1px solid #ddd;border-radius:4px;width:140px' title='Polling interval for thermal UI'></label>";
  inner += "    <label title=\"How often the ToF UI fetches distance data.\">ToF Polling (ms)<br><input type='number' id='tofPollingMs' min='100' max='2000' step='50' value='300' style='padding:0.5rem;border:1px solid #ddd;border-radius:4px;width:140px' title='Polling interval for ToF UI'></label>";
  inner += "    <label title=\"Number of consecutive stable ToF readings required before updating the displayed value.\">ToF Stability Threshold<br><input type='number' id='tofStabilityThreshold' min='1' max='10' step='1' value='3' style='padding:0.5rem;border:1px solid #ddd;border-radius:4px;width:140px' title='Stability filter for ToF display'></label>";
  inner += "    <label title=\"Default color palette for thermal visualization.\">Thermal Default Palette<br><select id='thermalPaletteDefault' class='menu-item' style='padding:0.4rem;width:160px' title='Default thermal palette'><option value='grayscale'>Grayscale</option><option value='iron'>Iron</option><option value='rainbow'>Rainbow</option><option value='hot'>Hot</option><option value='coolwarm'>Coolwarm</option></select></label>";
  inner += "    <label title=\"Maximum UI update rate for thermal rendering (client throttle).\">Thermal Web Max FPS<br><input type='number' id='thermalWebMaxFps' min='1' max='20' step='1' value='10' style='padding:0.5rem;border:1px solid #ddd;border-radius:4px;width:140px' title='Max FPS for web UI polling'></label>";
  inner += "    <label title=\"Smoothing factor for thermal values (0 = no smoothing, 1 = very slow changes).\">Thermal EWMA Factor (0..1)<br><input type='number' id='thermalEWMAFactor' min='0' max='1' step='0.05' value='0.2' style='padding:0.5rem;border:1px solid #ddd;border-radius:4px;width:140px' title='EWMA smoothing factor'></label>";
  inner += "    <label title=\"Animation duration for thermal color updates.\">Thermal Transition (ms)<br><input type='number' id='thermalTransitionMs' min='0' max='500' step='10' value='120' style='padding:0.5rem;border:1px solid #ddd;border-radius:4px;width:140px' title='Thermal transition duration'></label>";
  inner += "    <label title=\"Animation duration for ToF UI updates.\">ToF Transition (ms)<br><input type='number' id='tofTransitionMs' min='0' max='500' step='10' value='200' style='padding:0.5rem;border:1px solid #ddd;border-radius:4px;width:140px' title='ToF transition duration'></label>";
  inner += "    <label title=\"Maximum distance shown in ToF UI bar/graph.\">ToF UI Max Distance (mm)<br><input type='number' id='tofUiMaxDistanceMm' min='500' max='6000' step='50' value='3400' style='padding:0.5rem;border:1px solid #ddd;border-radius:4px;width:160px' title='ToF UI maximum distance'></label>";
  inner += "  </div>"; // grid
  inner += "  <div style='margin-top:1rem;padding:1rem;background:#e8f4f8;border-radius:6px;border-left:3px solid #17a2b8'>";
  inner += "    <div style='font-weight:bold;color:#0c5460;margin-bottom:0.5rem'>Thermal Frame Interpolation (Client-Side)</div>";
  inner += "    <div style='color:#0c5460;font-size:0.9rem;margin-bottom:0.75rem'>Smooths thermal visualization by interpolating between frames. Makes 4Hz sensor data appear smoother (~12-16Hz visual).</div>";
  inner += "    <div style='display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,1fr));gap:1rem;align-items:end'>";
  inner += "      <label><input type='checkbox' id='thermalInterpolationEnabled' style='margin-right:0.5rem'> Enable Frame Interpolation</label>";
  inner += "      <label title=\"Number of intermediate frames between real frames\">Interpolation Steps<br><input type='number' id='thermalInterpolationSteps' min='1' max='8' step='1' value='3' style='padding:0.5rem;border:1px solid #ddd;border-radius:4px;width:120px'></label>";
  inner += "      <label title=\"Size of frame buffer for temporal smoothing\">Buffer Size<br><input type='number' id='thermalInterpolationBufferSize' min='1' max='10' step='1' style='padding:0.5rem;border:1px solid #ddd;border-radius:4px;width:80px'></label>";
  inner += "      <label title=\"Web display quality scaling (higher = more detail)\">Display Quality<br><select id='thermalWebClientQuality' style='padding:0.5rem;border:1px solid #ddd;border-radius:4px;width:120px'><option value='1'>1x (32×24)</option><option value='2'>2x (64×48)</option><option value='3'>3x (96×72)</option><option value='4'>4x (128×96)</option></select></label>";
  inner += "    </div>";
  inner += "  </div>";
  inner += "  <div style='margin-top:1rem'><button class='btn' onclick=\"saveSensorsUISettings()\">Save Sensors UI</button></div>";
  inner += "  </div>";
  inner += "</div>";
  inner += "<script>try{ console.debug('[SNSR] Sensors UI Settings ready'); }catch(_){}</script>";

  // Device-side Sensor Settings (collapsible)
  inner += "<div style='background:#f8f9fa;border-radius:8px;padding:1.0rem 1.5rem;margin:1rem 0;border-left:4px solid #17a2b8;color:#333'>";
  inner += "  <div style='display:flex;align-items:center;justify-content:space-between'>";
  inner += "    <div><div style='font-size:1.2rem;font-weight:bold;color:#333'>Device-side Sensor Settings</div><div style='color:#666;font-size:0.9rem'>These control device sampling and bus timing. Changes take effect immediately; some (e.g., Thermal FPS) apply on next sensor init.</div></div>";
  inner += "    <button class='btn' id='btn-dev-toggle' onclick=\"togglePane('dev-pane','btn-dev-toggle')\">Expand</button>";
  inner += "  </div>";
  inner += "  <div id='dev-pane' style='display:none;margin-top:0.75rem'>";
  inner += "  <div style='display:grid;grid-template-columns:repeat(auto-fit,minmax(260px,1fr));gap:1rem'>";
  inner += "    <label title=\"Target frames per second for the MLX90640 sensor. Limited to 1-8 Hz due to sensor hardware constraints (full-frame capture).\">Thermal Target FPS<br><input type='number' id='thermalTargetFps' min='1' max='8' step='1' value='8' style='padding:0.5rem;border:1px solid #ddd;border-radius:4px;width:120px' title='Thermal frames per second (1-8 Hz max)'></label>";
  inner += "    <label title=\"Device loop polling interval for thermal capture.\">Thermal Device Poll (ms)<br><input type='number' id='thermalDevicePollMs' min='100' max='2000' step='5' value='125' style='padding:0.5rem;border:1px solid #ddd;border-radius:4px;width:140px' title='Thermal device poll interval'></label>";
  inner += "    <label title=\"Device loop polling interval for ToF reads.\">ToF Device Poll (ms)<br><input type='number' id='tofDevicePollMs' min='100' max='2000' step='5' value='200' style='padding:0.5rem;border:1px solid #ddd;border-radius:4px;width:140px' title='ToF device poll interval'></label>";
  inner += "    <label title=\"Device loop polling interval for IMU reads.\">IMU Device Poll (ms)<br><input type='number' id='imuDevicePollMs' min='50' max='1000' step='5' value='200' style='padding:0.5rem;border:1px solid #ddd;border-radius:4px;width:140px' title='IMU device poll interval'></label>";
  inner += "    <label title=\"I2C bus speed for thermal sensor.\">I2C Clock Thermal (Hz)<br><input type='number' id='i2cClockThermalHz' min='400000' max='1000000' step='50000' value='800000' style='padding:0.5rem;border:1px solid #ddd;border-radius:4px;width:180px' title='I2C clock for thermal sensor'></label>";
  inner += "    <label title=\"I2C bus speed for ToF sensor.\">I2C Clock ToF (Hz)<br><input type='number' id='i2cClockToFHz' min='50000' max='400000' step='50000' value='100000' style='padding:0.5rem;border:1px solid #ddd;border-radius:4px;width:180px' title='I2C clock for ToF sensor'></label>";
  inner += "  </div>"; // grid
  inner += "  <div style='margin-top:1rem'><button class='btn' onclick=\"saveDeviceSensorSettings()\">Save Device Settings</button></div>";
  inner += "  </div>";
  inner += "</div>";

  
  // Debug Section (collapsible)
  inner += "<div style='background:#f8f9fa;border-radius:8px;padding:1.0rem 1.5rem;margin:1rem 0;border-left:4px solid #667eea;color:#333'>";
  inner += "  <div style='display:flex;align-items:center;justify-content:space-between'>";
  inner += "    <div><div style='font-size:1.2rem;font-weight:bold;color:#333'>Debug Controls</div><div style='color:#666;font-size:0.9rem'>Toggle debugging output categories to serial console. Default is quiet (minimal spam).</div></div>";
  inner += "    <button class='btn' id='btn-debug-toggle' onclick=\"togglePane('debug-pane','btn-debug-toggle')\">Expand</button>";
  inner += "  </div>";
  inner += "  <div id='debug-pane' style='display:none;margin-top:0.75rem'>";
  inner += "  <div style='display:grid;grid-template-columns:repeat(auto-fit,minmax(280px,1fr));gap:1rem'>";
  
  // System & Core column
  inner += "    <div style='background:#fff;border:1px solid #e5e7eb;border-radius:6px;padding:1rem'>";
  inner += "      <div style='font-weight:bold;margin-bottom:0.75rem;color:#374151;border-bottom:1px solid #e5e7eb;padding-bottom:0.5rem'>System & Core</div>";
  inner += "      <div style='display:flex;flex-direction:column;gap:0.35rem'>";
  inner += "        <div style='display:flex;align-items:center;gap:0.5rem'><span style='color:#333' title='Enable/disable CLI command debug output'>CLI Commands: <span style='font-weight:bold;color:#667eea' id='debugCli-value'>-</span></span><button class='btn' onclick=\"toggleDebug('debugCli')\" id='debugCli-btn' title='Toggle CLI debugging'>Toggle</button></div>";
  inner += "        <div style='display:flex;align-items:center;gap:0.5rem'><span style='color:#333' title='Enable/disable command flow debug output (parsing, routing, execution)'>Command Flow: <span style='font-weight:bold;color:#667eea' id='debugCommandFlow-value'>-</span></span><button class='btn' onclick=\"toggleDebug('debugCommandFlow')\" id='debugCommandFlow-btn' title='Toggle command flow debugging'>Toggle</button></div>";
  inner += "        <div style='display:flex;align-items:center;gap:0.5rem'><span style='color:#333' title='Enable/disable user management debug output (registration/approve/deny)'>Users: <span style='font-weight:bold;color:#667eea' id='debugUsers-value'>-</span></span><button class='btn' onclick=\"toggleDebug('debugUsers')\" id='debugUsers-btn' title='Toggle user management debugging'>Toggle</button></div>";
  inner += "        <div style='display:flex;align-items:center;gap:0.5rem'><span style='color:#333' title='Enable/disable authentication and cookie debug output'>Auth & Cookies: <span style='font-weight:bold;color:#667eea' id='debugAuthCookies-value'>-</span></span><button class='btn' onclick=\"toggleDebug('debugAuthCookies')\" id='debugAuthCookies-btn' title='Toggle auth/cookie debugging'>Toggle</button></div>";
  inner += "        <div style='display:flex;align-items:center;gap:0.5rem'><span style='color:#333' title='Enable/disable storage/filesystem debug output'>Storage & FS: <span style='font-weight:bold;color:#667eea' id='debugStorage-value'>-</span></span><button class='btn' onclick=\"toggleDebug('debugStorage')\" id='debugStorage-btn' title='Toggle storage debugging'>Toggle</button></div>";
  inner += "        <div style='display:flex;align-items:center;gap:0.5rem'><span style='color:#333' title='Enable/disable date/time and NTP debug output'>Date/Time & NTP: <span style='font-weight:bold;color:#667eea' id='debugDateTime-value'>-</span></span><button class='btn' onclick=\"toggleDebug('debugDateTime')\" id='debugDateTime-btn' title='Toggle date/time debugging'>Toggle</button></div>";
  inner += "        <div style='display:flex;align-items:center;gap:0.5rem'><span style='color:#333' title='Enable/disable performance monitoring debug output'>Performance: <span style='font-weight:bold;color:#667eea' id='debugPerformance-value'>-</span></span><button class='btn' onclick=\"toggleDebug('debugPerformance')\" id='debugPerformance-btn' title='Toggle performance monitoring'>Toggle</button></div>";
  inner += "      </div>";
  inner += "    </div>";
  
  // Network & Web column  
  inner += "    <div style='background:#fff;border:1px solid #e5e7eb;border-radius:6px;padding:1rem'>";
  inner += "      <div style='font-weight:bold;margin-bottom:0.75rem;color:#374151;border-bottom:1px solid #e5e7eb;padding-bottom:0.5rem'>Network & Web</div>";
  inner += "      <div style='display:flex;flex-direction:column;gap:0.35rem'>";
  inner += "        <div style='display:flex;align-items:center;gap:0.5rem'><span style='color:#333' title='Enable/disable WiFi connection debug output'>WiFi Network: <span style='font-weight:bold;color:#667eea' id='debugWifi-value'>-</span></span><button class='btn' onclick=\"toggleDebug('debugWifi')\" id='debugWifi-btn' title='Toggle WiFi debugging'>Toggle</button></div>";
  inner += "        <div style='display:flex;align-items:center;gap:0.5rem'><span style='color:#333' title='Enable/disable HTTP routing and request debug output'>HTTP Routing: <span style='font-weight:bold;color:#667eea' id='debugHttp-value'>-</span></span><button class='btn' onclick=\"toggleDebug('debugHttp')\" id='debugHttp-btn' title='Toggle HTTP routing debugging'>Toggle</button></div>";
  inner += "        <div style='display:flex;align-items:center;gap:0.5rem'><span style='color:#333' title='Enable/disable Server-Sent Events debug output'>Server Events (SSE): <span style='font-weight:bold;color:#667eea' id='debugSse-value'>-</span></span><button class='btn' onclick=\"toggleDebug('debugSse')\" id='debugSse-btn' title='Toggle SSE debugging'>Toggle</button></div>";
  inner += "      </div>";
  inner += "    </div>";
  
  // Sensors & Hardware column
  inner += "    <div style='background:#fff;border:1px solid #e5e7eb;border-radius:6px;padding:1rem'>";
  inner += "      <div style='font-weight:bold;margin-bottom:0.75rem;color:#374151;border-bottom:1px solid #e5e7eb;padding-bottom:0.5rem'>Sensors & Hardware</div>";
  inner += "      <div style='display:flex;flex-direction:column;gap:0.35rem'>";
  inner += "        <div style='display:flex;align-items:center;gap:0.5rem'><span style='color:#333' title='Enable/disable general sensor debug output'>General Sensors: <span style='font-weight:bold;color:#667eea' id='debugSensorsGeneral-value'>-</span></span><button class='btn' onclick=\"toggleDebug('debugSensorsGeneral')\" id='debugSensorsGeneral-btn' title='Toggle general sensor debugging'>Toggle</button></div>";
  inner += "        <div style='display:flex;align-items:center;gap:0.5rem'><span style='color:#333' title='Enable/disable sensor frame capture debug output'>Thermal Frames: <span style='font-weight:bold;color:#667eea' id='debugSensorsFrame-value'>-</span></span><button class='btn' onclick=\"toggleDebug('debugSensorsFrame')\" id='debugSensorsFrame-btn' title='Toggle thermal frame debugging'>Toggle</button></div>";
  inner += "        <div style='display:flex;align-items:center;gap:0.5rem'><span style='color:#333' title='Enable/disable sensor data processing debug output'>Sensor Data: <span style='font-weight:bold;color:#667eea' id='debugSensorsData-value'>-</span></span><button class='btn' onclick=\"toggleDebug('debugSensorsData')\" id='debugSensorsData-btn' title='Toggle sensor data debugging'>Toggle</button></div>";
  inner += "      </div>";
  inner += "    </div>";
  
  inner += "  </div>"; // grid
  inner += "  <div style='margin-top:1rem'><button class='btn' onclick=\"saveDebugSettings()\">Save Debug Settings</button></div>";
  inner += "  </div>";     // debug-pane
  inner += "</div>";     // debug wrapper
  
  // Chunk: Debug area ready marker  
  inner += "<script>try{ console.debug('[Page] Debug controls ready'); }catch(_){}</script>";
  
  // Admin section (collapsible)
  inner += "<div id='admin-section' style='display:none;background:#fff;border-radius:8px;padding:1.0rem 1.5rem;margin:1rem 0;border-left:4px solid #ffc107;color:#333'>";
  inner += "  <div style='display:flex;align-items:center;justify-content:space-between'>";
  inner += "    <div style='font-size:1.2rem;font-weight:bold;color:#333'>Admin Controls</div>";
  inner += "    <button class='btn' id='btn-admin-toggle' onclick=\"togglePane('admin-pane','btn-admin-toggle')\">Expand</button>";
  inner += "  </div>";
  inner += "  <div id='admin-pane' style='display:none;margin-top:0.75rem'>";
  inner += "  <div style='display:grid;grid-template-columns:1fr;gap:1rem'>";
  inner += "    <div style='background:#f8f9fa;border:1px solid #e5e7eb;border-radius:8px;padding:1rem'>";
  inner += "      <div style='display:flex;align-items:center;justify-content:space-between;margin-bottom:0.5rem'>";
  inner += "        <div style='font-weight:bold;color:#333'>User Management</div>";
  inner += "        <button class='btn' id='btn-users-toggle' onclick=\"togglePane('users-pane','btn-users-toggle')\">Expand</button>";
  inner += "      </div>";
  inner += "      <div style='color:#666;margin-bottom:0.75rem;font-size:0.9rem'>Manage existing users and their roles.</div>";
  inner += "      <div id='users-pane' style='display:none;margin-top:0.75rem'>";
  inner += "        <div id='users-list' style='min-height:24px;color:#333;margin-bottom:0.75rem'>Loading...</div>";
  inner += "        <button class='btn' onclick='refreshUsers()' title='Reload list of users'>Refresh Users</button>";
  inner += "      </div>";
  inner += "    </div>";
  inner += "  </div>";   // grid
  inner += "  </div>";   // admin-pane
  inner += "</div>";     // admin-section
  // Page controls (standalone)
  inner += "<div style='text-align:center;margin-top:2rem'>";
  inner += "  <button class='btn' onclick='refreshSettings()' title='Reload all settings from device memory'>Refresh Settings</button>";
  inner += "</div>";
  // Script chunks for reliability on ESP32
  // Chunk 1: Build tag and error handlers
  inner += "<script>(function(){ try {";
  inner += "window.settingsBuildTag='settings-minimal-chunked-v2';";
  inner += "console.log('[SETTINGS BUILD]', window.settingsBuildTag, new Date().toISOString());";
  inner += "window.__S=window.__S||{};";
  inner += "window.addEventListener('error',function(e){console.error('[SETTINGS ERROR]',e.message,'at',e.filename+':'+e.lineno+':'+e.colno);});";
  inner += "window.addEventListener('unhandledrejection',function(e){console.error('[SETTINGS PROMISE REJECTION]',e.reason);});";
  inner += "console.debug('[Page] Settings core init ready');";
  inner += "} catch(err){console.error('[Page] Settings chunk1 fail',err);} })();</script>";

  // Chunk 2: State and DOM helpers
  inner += "<script>(function(){ try {";
  inner += "window.$=function(id){return document.getElementById(id);};";
  inner += "__S.state={savedSSIDs:[],currentSSID:''};";
  inner += "__S.renderSettings=function(s){ try {";
  inner += "console.debug('[Settings] Rendering settings data', s && typeof s=== 'object' ? Object.assign({}, s, {wifiNetworks: undefined}) : s);";
  inner += "__S.state.currentSSID=(s.wifiPrimarySSID||s.wifiSSID||''); $('wifi-ssid').textContent=__S.state.currentSSID;";
  inner += "var primary=__S.state.currentSSID||''; var list=Array.isArray(s.wifiNetworks)?s.wifiNetworks.map(function(n){return n&&n.ssid;}).filter(function(x){return !!x;}):[]; __S.state.savedSSIDs=[]; if(primary) __S.state.savedSSIDs.push(primary); if(list&&list.length) __S.state.savedSSIDs=__S.state.savedSSIDs.concat(list);";
  inner += "$('wifi-value').textContent = s.wifiAutoReconnect ? 'Enabled':'Disabled';";
  inner += "$('cli-value').textContent = s.cliHistorySize; $('cli-input').value = s.cliHistorySize;";
  inner += "$('wifi-btn').textContent = s.wifiAutoReconnect ? 'Disable':'Enable';";
  inner += "$('espnow-value').textContent = s.espnowenabled ? 'Enabled':'Disabled'; $('espnow-btn').textContent = s.espnowenabled ? 'Disable':'Enable';";
  inner += "var tzOffset = s.tzOffsetMinutes; var tzName = 'UTC' + (tzOffset >= 0 ? '+' : '') + (tzOffset / 60); if (tzOffset === -720) tzName = 'UTC-12 (Baker Island)'; else if (tzOffset === -660) tzName = 'UTC-11 (Hawaii-Aleutian)'; else if (tzOffset === -600) tzName = 'UTC-10 (Hawaii)'; else if (tzOffset === -540) tzName = 'UTC-9 (Alaska)'; else if (tzOffset === -480) tzName = 'UTC-8 (Pacific)'; else if (tzOffset === -420) tzName = 'UTC-7 (Mountain)'; else if (tzOffset === -360) tzName = 'UTC-6 (Central)'; else if (tzOffset === -300) tzName = 'UTC-5 (Eastern)'; else if (tzOffset === -240) tzName = 'UTC-4 (Atlantic)'; else if (tzOffset === -180) tzName = 'UTC-3 (Argentina)'; else if (tzOffset === -120) tzName = 'UTC-2 (Mid-Atlantic)'; else if (tzOffset === -60) tzName = 'UTC-1 (Azores)'; else if (tzOffset === 0) tzName = 'UTC+0 (London/Dublin)'; else if (tzOffset === 60) tzName = 'UTC+1 (Berlin/Paris)'; else if (tzOffset === 120) tzName = 'UTC+2 (Cairo/Athens)'; else if (tzOffset === 180) tzName = 'UTC+3 (Moscow/Baghdad)'; else if (tzOffset === 240) tzName = 'UTC+4 (Dubai/Baku)'; else if (tzOffset === 300) tzName = 'UTC+5 (Karachi/Tashkent)'; else if (tzOffset === 330) tzName = 'UTC+5:30 (Mumbai/Delhi)'; else if (tzOffset === 360) tzName = 'UTC+6 (Dhaka/Almaty)'; else if (tzOffset === 420) tzName = 'UTC+7 (Bangkok/Jakarta)'; else if (tzOffset === 480) tzName = 'UTC+8 (Beijing/Singapore)'; else if (tzOffset === 540) tzName = 'UTC+9 (Tokyo/Seoul)'; else if (tzOffset === 570) tzName = 'UTC+9:30 (Adelaide)'; else if (tzOffset === 600) tzName = 'UTC+10 (Sydney/Melbourne)'; else if (tzOffset === 660) tzName = 'UTC+11 (Solomon Islands)'; else if (tzOffset === 720) tzName = 'UTC+12 (Fiji/Auckland)'; $('tz-value').textContent = tzName; var tzSelect = document.getElementById('tz-select'); if (tzSelect) tzSelect.value = tzOffset;";
  inner += "$('ntp-value').textContent = s.ntpServer; $('ntp-input').value = s.ntpServer;";
  inner += "var out=(s.output||{}), th=(s.thermal||{}), tof=(s.tof||{}), dbg=(s.debug||{});";
  inner += "var thUI=(th.ui||{}), thDev=(th.device||{}), tofUI=(tof.ui||{}), tofDev=(tof.device||{});";
  inner += "var outSerial = (out.outSerial!==undefined?out.outSerial:s.outSerial);";
  inner += "var outWeb = (out.outWeb!==undefined?out.outWeb:s.outWeb);";
  inner += "var outTft = (out.outTft!==undefined?out.outTft:s.outTft);";
  inner += "$('serial-value').textContent = outSerial ? 'Enabled':'Disabled'; $('serial-btn').textContent = outSerial ? 'Disable':'Enable';";
  inner += "$('web-value').textContent = outWeb ? 'Enabled':'Disabled'; $('web-btn').textContent = outWeb ? 'Disable':'Enable';";
  inner += "$('tft-value').textContent = outTft ? 'Enabled':'Disabled'; $('tft-btn').textContent = outTft ? 'Disable':'Enable';";
  inner += "var thermalPollingMs = (thUI.thermalPollingMs!==undefined?thUI.thermalPollingMs:s.thermalPollingMs); if(thermalPollingMs!==undefined) $('thermalPollingMs').value=thermalPollingMs;";
  inner += "var tofPollingMs = (tofUI.tofPollingMs!==undefined?tofUI.tofPollingMs:s.tofPollingMs); if(tofPollingMs!==undefined) $('tofPollingMs').value=tofPollingMs;";
  inner += "var tofStabilityThreshold = (tofUI.tofStabilityThreshold!==undefined?tofUI.tofStabilityThreshold:s.tofStabilityThreshold); if(tofStabilityThreshold!==undefined) $('tofStabilityThreshold').value=tofStabilityThreshold;";
  inner += "var thermalPaletteDefault=(thUI.thermalPaletteDefault||s.thermalPaletteDefault); if(thermalPaletteDefault) $('thermalPaletteDefault').value=thermalPaletteDefault;";
  inner += "var thermalEWMAFactor=(thUI.thermalEWMAFactor!==undefined?thUI.thermalEWMAFactor:s.thermalEWMAFactor); if(thermalEWMAFactor!==undefined) $('thermalEWMAFactor').value=thermalEWMAFactor;";
  inner += "var thermalTransitionMs=(thUI.thermalTransitionMs!==undefined?thUI.thermalTransitionMs:s.thermalTransitionMs); if(thermalTransitionMs!==undefined) $('thermalTransitionMs').value=thermalTransitionMs;";
  inner += "var tofTransitionMs=(tofUI.tofTransitionMs!==undefined?tofUI.tofTransitionMs:s.tofTransitionMs); if(tofTransitionMs!==undefined) $('tofTransitionMs').value=tofTransitionMs;";
  inner += "var tofUiMaxDistanceMm=(tofUI.tofUiMaxDistanceMm!==undefined?tofUI.tofUiMaxDistanceMm:s.tofUiMaxDistanceMm); if(tofUiMaxDistanceMm!==undefined) $('tofUiMaxDistanceMm').value=tofUiMaxDistanceMm;";
  inner += "var thermalWebMaxFps=(thUI.thermalWebMaxFps!==undefined?thUI.thermalWebMaxFps:s.thermalWebMaxFps); if(thermalWebMaxFps!==undefined) $('thermalWebMaxFps').value=thermalWebMaxFps;";
  inner += "if(thUI.thermalInterpolationEnabled!==undefined) $('thermalInterpolationEnabled').checked=!!thUI.thermalInterpolationEnabled; else if(s.thermalInterpolationEnabled!==undefined) $('thermalInterpolationEnabled').checked=!!s.thermalInterpolationEnabled;";
  inner += "if(thUI.thermalInterpolationSteps!==undefined) $('thermalInterpolationSteps').value=thUI.thermalInterpolationSteps; else if(s.thermalInterpolationSteps!==undefined) $('thermalInterpolationSteps').value=s.thermalInterpolationSteps;";
  inner += "if(thUI.thermalInterpolationBufferSize!==undefined) $('thermalInterpolationBufferSize').value=thUI.thermalInterpolationBufferSize; else if(s.thermalInterpolationBufferSize!==undefined) $('thermalInterpolationBufferSize').value=s.thermalInterpolationBufferSize;";
  inner += "var thermalWebClientQuality=(thUI.thermalWebClientQuality!==undefined?thUI.thermalWebClientQuality:s.thermalWebClientQuality); if(thermalWebClientQuality!==undefined) $('thermalWebClientQuality').value=thermalWebClientQuality;";
  inner += "if(thDev.thermalTargetFps!==undefined) $('thermalTargetFps').value=thDev.thermalTargetFps; else if(s.thermalTargetFps!==undefined) $('thermalTargetFps').value=s.thermalTargetFps;";
  inner += "if(thDev.thermalDevicePollMs!==undefined) $('thermalDevicePollMs').value=thDev.thermalDevicePollMs; else if(s.thermalDevicePollMs!==undefined) $('thermalDevicePollMs').value=s.thermalDevicePollMs;";
  inner += "if(tofDev.tofDevicePollMs!==undefined) $('tofDevicePollMs').value=tofDev.tofDevicePollMs; else if(s.tofDevicePollMs!==undefined) $('tofDevicePollMs').value=s.tofDevicePollMs;";
  inner += "if(s.imuDevicePollMs!==undefined) $('imuDevicePollMs').value=s.imuDevicePollMs;";
  inner += "var i2cTherm = (thDev.i2cClockThermalHz!==undefined?thDev.i2cClockThermalHz:s.i2cClockThermalHz); if(i2cTherm!==undefined) $('i2cClockThermalHz').value=i2cTherm;";
  inner += "var i2cTof = (tofDev.i2cClockToFHz!==undefined?tofDev.i2cClockToFHz:s.i2cClockToFHz); if(i2cTof!==undefined) $('i2cClockToFHz').value=i2cTof;";
  inner += "var dAuth=(dbg.authCookies!==undefined?dbg.authCookies:s.debugAuthCookies); $('debugAuthCookies-value').textContent = dAuth ? 'Enabled':'Disabled'; $('debugAuthCookies-btn').textContent = dAuth ? 'Disable':'Enable';";
  inner += "var dHttp=(dbg.http!==undefined?dbg.http:s.debugHttp); $('debugHttp-value').textContent = dHttp ? 'Enabled':'Disabled'; $('debugHttp-btn').textContent = dHttp ? 'Disable':'Enable';";
  inner += "var dSse=(dbg.sse!==undefined?dbg.sse:s.debugSse); $('debugSse-value').textContent = dSse ? 'Enabled':'Disabled'; $('debugSse-btn').textContent = dSse ? 'Disable':'Enable';";
  inner += "var dCli=(dbg.cli!==undefined?dbg.cli:s.debugCli); $('debugCli-value').textContent = dCli ? 'Enabled':'Disabled'; $('debugCli-btn').textContent = dCli ? 'Disable':'Enable';";
  inner += "var dWifi=(dbg.wifi!==undefined?dbg.wifi:s.debugWifi); $('debugWifi-value').textContent = dWifi ? 'Enabled':'Disabled'; $('debugWifi-btn').textContent = dWifi ? 'Disable':'Enable';";
  inner += "var dStorage=(dbg.storage!==undefined?dbg.storage:s.debugStorage); $('debugStorage-value').textContent = dStorage ? 'Enabled':'Disabled'; $('debugStorage-btn').textContent = dStorage ? 'Disable':'Enable';";
  inner += "var dPerformance=(dbg.performance!==undefined?dbg.performance:s.debugPerformance); $('debugPerformance-value').textContent = dPerformance ? 'Enabled':'Disabled'; $('debugPerformance-btn').textContent = dPerformance ? 'Disable':'Enable';";
  inner += "var dCmdFlow=(dbg.cmdFlow!==undefined?dbg.cmdFlow:s.debugCommandFlow); var dCFEl=$('debugCommandFlow-value'); if(dCFEl){ dCFEl.textContent = dCmdFlow ? 'Enabled':'Disabled'; var btn=$('debugCommandFlow-btn'); if(btn){ btn.textContent = dCmdFlow ? 'Disable':'Enable'; } }";
  inner += "var dUsers=(dbg.users!==undefined?dbg.users:s.debugUsers); var dUEl=$('debugUsers-value'); if(dUEl){ dUEl.textContent = dUsers ? 'Enabled':'Disabled'; var ubtn=$('debugUsers-btn'); if(ubtn){ ubtn.textContent = dUsers ? 'Disable':'Enable'; } }";
  inner += "var dDateTime=(dbg.dateTime!==undefined?dbg.dateTime:s.debugDateTime); $('debugDateTime-value').textContent = dDateTime ? 'Enabled':'Disabled'; $('debugDateTime-btn').textContent = dDateTime ? 'Disable':'Enable';";
  inner += "var dSF=(dbg.sensorsFrame!==undefined?dbg.sensorsFrame:s.debugSensorsFrame); $('debugSensorsFrame-value').textContent = dSF ? 'Enabled':'Disabled'; $('debugSensorsFrame-btn').textContent = dSF ? 'Disable':'Enable';";
  inner += "var dSD=(dbg.sensorsData!==undefined?dbg.sensorsData:s.debugSensorsData); $('debugSensorsData-value').textContent = dSD ? 'Enabled':'Disabled'; $('debugSensorsData-btn').textContent = dSD ? 'Disable':'Enable';";
  inner += "var dSG=(dbg.sensorsGeneral!==undefined?dbg.sensorsGeneral:s.debugSensorsGeneral); $('debugSensorsGeneral-value').textContent = dSG ? 'Enabled':'Disabled'; $('debugSensorsGeneral-btn').textContent = dSG ? 'Disable':'Enable';";
  inner += "var isAdm = (s && s.user && (s.user.isAdmin===true)) || (__S && __S.user && (__S.user.isAdmin===true)); var hasFeat = (__S && __S.features && __S.features.adminSessions===true); var admin = isAdm && hasFeat; var sec=document.getElementById('admin-section'); if(sec){ sec.style.display = admin ? 'block' : 'none'; } if(admin){ try{ if(typeof window.refreshUsers==='function'){ refreshUsers(); } if(typeof window.refreshPendingUsers==='function'){ refreshPendingUsers(); } }catch(e){} }";
  inner += "} catch(e){ console.error('[Settings] Render failed',e); alert('Render error: '+e.message); } };";
  inner += "window.renderOutputRuntime=function(obj){ try{ obj=obj||{}; var r=obj.runtime||{}; var p=obj.persisted||{}; var set=function(id,val){ var el=$(id); if(!el) return; var on = (String(val)=='1'||val===1||val===true||String(val).toLowerCase()=='true'); el.textContent = on ? 'On' : 'Off'; el.style.color = on ? '#28a745' : '#dc3545'; }; var setHidden=function(id,hide){ var el=$(id); if(!el) return; try{ if(hide){ el.classList.add('vis-gone'); el.classList.remove('vis-hidden'); el.classList.remove('hidden'); } else { el.classList.remove('vis-gone'); el.classList.remove('vis-hidden'); el.classList.remove('hidden'); } }catch(_){ el.style.display = hide ? 'none' : ''; } }; set('serial-runtime', r.serial); set('web-runtime', r.web); set('tft-runtime', r.tft); try{ if(p && typeof p==='object'){ if(p.serial!==undefined){ var pv=$('serial-value'); if(pv) pv.textContent = p.serial? 'Enabled':'Disabled'; var pb=$('serial-btn'); if(pb) { pb.textContent = p.serial? 'Disable':'Enable'; } } if(p.web!==undefined){ var pv2=$('web-value'); if(pv2) pv2.textContent = p.web? 'Enabled':'Disabled'; var pb2=$('web-btn'); if(pb2) { pb2.textContent = p.web? 'Disable':'Enable'; } } if(p.tft!==undefined){ var pv3=$('tft-value'); if(pv3) pv3.textContent = p.tft? 'Enabled':'Disabled'; var pb3=$('tft-btn'); if(pb3) { pb3.textContent = p.tft? 'Disable':'Enable'; } } } var curSerial = (r.serial!==undefined) ? (String(r.serial)=='1'||r.serial===1||r.serial===true||String(r.serial).toLowerCase()=='true') : !!(p&&p.serial); var curWeb = (r.web!==undefined) ? (String(r.web)=='1'||r.web===1||r.web===true||String(r.web).toLowerCase()=='true') : !!(p&&p.web); var curTft = (r.tft!==undefined) ? (String(r.tft)=='1'||r.tft===1||r.tft===true||String(r.tft).toLowerCase()=='true') : !!(p&&p.tft); setHidden('serial-temp-on', curSerial); setHidden('serial-temp-off', !curSerial); setHidden('web-temp-on', curWeb); setHidden('web-temp-off', !curWeb); setHidden('tft-temp-on', curTft); setHidden('tft-temp-off', !curTft); }catch(_){ } }catch(e){ console.error('[SETTINGS] renderOutputRuntime fail', e); } };";
  inner += "window.refreshOutput=function(){ return fetch('/api/output',{credentials:'same-origin'}).then(function(r){ return r.text(); }).then(function(t){ var d=null; try{ d=JSON.parse(t||'{}'); }catch(e){ console.error('[Settings] Output JSON parse fail; first 120 chars:', (t||'').slice(0,120)); return; } if(d&&d.success){ window.renderOutputRuntime(d); } }).catch(function(e){ console.warn('[Settings] RefreshOutput error', e&&e.message); }); };";
  inner += "window.setOutputRuntime=function(channel, val){ try{ if(!channel) return; var map={ serial:'outserial', web:'outweb', tft:'outtft' }; var key=map[channel]; if(!key) return; var v = val?1:0; var cmd = key + ' temp ' + v; fetch('/api/cli',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},credentials:'same-origin',body:'cmd='+encodeURIComponent(cmd)})";
  inner += ".then(function(r){ return r.text(); })";
  inner += ".then(function(_t){ try{ if(typeof window.refreshOutput==='function'){ window.refreshOutput(); } }catch(_){ } }).catch(function(e){ alert('Error: '+e.message); }); }catch(e){ alert('Error: '+e.message); } };";
  inner += "window.onload=function(){ try{ refreshSettings(); }catch(e){console.error('[Settings] onload fail',e);} };";
  inner += "} catch(err){console.error('[Page] Settings chunk6 fail',err);} })();</script>";

  // Chunk 3: API helpers
  inner += "<script>(function(){ try {";
  inner += "window.refreshSettings=function(){ console.debug('[Settings] Refreshing settings'); fetch('/api/settings',{credentials:'same-origin'})";
  inner += ".then(function(r){ return r.text(); })";
  inner += ".then(function(t){ var d=null; try{ d=JSON.parse(t||'{}'); } catch(e){ console.error('[Settings] JSON parse fail; first 120 chars:', (t||'').slice(0,120)); alert('Error fetching settings (not JSON) — possibly logged out.'); return; } if(d&&d.success){ try{ window.__S=window.__S||{}; __S.user=d.user||null; __S.features=d.features||null; }catch(_){ } __S.renderSettings(d.settings||{}); } else { alert('Error: '+(d&&d.error||'Unknown')); } })";
  inner += ".then(function(){ try{ if(typeof window.refreshOutput==='function'){ window.refreshOutput(); } }catch(_){ } })";
  inner += ".catch(function(e){ alert('Error: '+e.message); }); };";
  inner += "console.debug('[Page] Settings API helpers ready');";
  inner += "} catch(err){console.error('[Page] Settings chunk3 fail',err);} })();</script>";

  // Chunk 4: UI actions
  inner += "<script>(function(){ try {";
  inner += "window.toggleWifi=function(){ var cur = ($('wifi-value').textContent==='Enabled')?1:0; var v=cur?0:1; var cmd='wifiautoreconnect '+v; fetch('/api/cli',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},credentials:'same-origin',body:'cmd='+encodeURIComponent(cmd)}).then(function(r){return r.text();}).then(function(t){ if(t.indexOf('Error')>=0){ alert(t); } refreshSettings(); }).catch(function(e){ alert('Error: '+e.message); }); };";
  inner += "window.toggleEspNow=function(){ var cur = ($('espnow-value').textContent==='Enabled')?1:0; var v=cur?0:1; var cmd='set espnowenabled '+v; $('espnow-btn').textContent='...'; $('espnow-btn').disabled=true; fetch('/api/cli',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},credentials:'same-origin',body:'cmd='+encodeURIComponent(cmd)}).then(function(r){return r.text();}).then(function(t){ if(t.indexOf('Error')>=0){ alert(t); } else { if(v===1){ alert('ESP-NOW enabled. Will initialize on next reboot.'); } else { alert('ESP-NOW disabled. Will not initialize on next reboot.'); } } refreshSettings(); }).catch(function(e){ alert('Error: '+e.message); refreshSettings(); }); };";
  inner += "window.updateCliHistory=function(){ var v=parseInt($('cli-input').value); if(v<1){ alert('Must be at least 1'); return; } var cmd='clihistorysize '+v; fetch('/api/cli',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},credentials:'same-origin',body:'cmd='+encodeURIComponent(cmd)}).then(function(r){return r.text();}).then(function(){ refreshSettings(); }).catch(function(e){ alert('Error: '+e.message); }); };";
  inner += "window.clearCliHistory=function(){ var cmd='clear'; fetch('/api/cli',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},credentials:'same-origin',body:'cmd='+encodeURIComponent(cmd)}).then(function(r){return r.text();}).then(function(t){ try{ if(t && t.indexOf('Error')>=0){ alert(t); } else { console.debug('[Settings] CLI clear responded:', t); } }catch(_){ } if(typeof refreshSettings==='function') refreshSettings(); }).catch(function(e){ alert('Error: '+e.message); }); };";
  inner += "window.updateTimezone=function(){";
  inner += "  const select = document.getElementById('tz-select');";
  inner += "  if (!select) return;";
  inner += "  const value = select.value;";
  inner += "  if (!value) {";
  inner += "    alert('Please select a timezone');";
  inner += "    return;";
  inner += "  }";
  inner += "  const cmd = 'set tzOffsetMinutes ' + value;";
  inner += "  fetch('/api/cli',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},credentials:'same-origin',body:'cmd='+encodeURIComponent(cmd)})";
  inner += ".then(function(r){ return r.text(); })";
  inner += ".then(function(response){";
  inner += "  alert(response);";
  inner += "  if (typeof refreshSettings === 'function') refreshSettings();";
  inner += "}).catch(function(e){";
  inner += "  alert('Failed to update timezone: ' + e.message);";
  inner += "});";
  inner += "};";
  inner += "window.updateNtpServer=function(){ var val=$('ntp-input').value.trim(); if(!val){ alert('Enter NTP server'); return; } fetch('/api/cli',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},credentials:'same-origin',body:'cmd='+encodeURIComponent('set ntpServer '+val)}).then(function(r){return r.text();}).then(function(t){ if(t.indexOf('Error')>=0){ alert(t); } else { refreshSettings(); } }).catch(function(e){ alert('Error: '+e.message); }); };";
  inner += "window.toggleOutput=function(setting, channel){ var valueId=(channel||setting.replace('out','').toLowerCase())+'-value'; var btnId=(channel||setting.replace('out','').toLowerCase())+'-btn'; var valueEl=$(valueId); var btnEl=$(btnId); if(!valueEl||!btnEl) return; var cur = (valueEl.textContent==='Enabled')?1:0; var newVal = cur?0:1; btnEl.textContent='...'; btnEl.disabled=true; valueEl.textContent = newVal ? 'Enabled' : 'Disabled'; var cmdMap={ outSerial:'outserial', outWeb:'outweb', outTft:'outtft' }; var key = cmdMap[setting]||setting.toLowerCase(); var cmd = key + ' ' + newVal; fetch('/api/cli',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},credentials:'same-origin',body:'cmd='+encodeURIComponent(cmd)}) .then(function(r){ return r.text(); }) .then(function(_t){ btnEl.textContent = newVal ? 'Disable' : 'Enable'; btnEl.disabled=false; try{ if(typeof window.refreshOutput==='function'){ window.refreshOutput(); } }catch(_){ } }).catch(function(e){ valueEl.textContent = cur ? 'Enabled' : 'Disabled'; btnEl.textContent = cur ? 'Disable' : 'Enable'; btnEl.disabled=false; alert('Error: '+e.message); }); };";
  inner += "window.toggleDebug=function(setting){ var valueId=setting+'-value'; var btnId=setting+'-btn'; var valueEl=$(valueId); var btnEl=$(btnId); if(!valueEl||!btnEl) return; var cur = (valueEl.textContent==='Enabled')?1:0; var newVal=cur?0:1; btnEl.textContent='...'; btnEl.disabled=true; valueEl.textContent=newVal?'Enabled':'Disabled'; var map={ debugAuthCookies:'debugauthcookies', debugHttp:'debughttp', debugSse:'debugsse', debugCli:'debugcli', debugCommandFlow:'debugcommandflow', debugUsers:'debugusers', debugWifi:'debugwifi', debugStorage:'debugstorage', debugPerformance:'debugperformance', debugDateTime:'debugdatetime', debugSensorsGeneral:'debugsensorsgeneral', debugSensorsFrame:'debugsensorsframe', debugSensorsData:'debugsensorsdata' }; var key=map[setting]||setting.toLowerCase(); var cmd= key + ' ' + newVal; fetch('/api/cli',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},credentials:'same-origin',body:'cmd='+encodeURIComponent(cmd)}).then(function(r){return r.text();}).then(function(_t){ btnEl.textContent=newVal?'Disable':'Enable'; btnEl.disabled=false; }).catch(function(e){ valueEl.textContent=cur?'Enabled':'Disabled'; btnEl.textContent=cur?'Disable':'Enable'; btnEl.disabled=false; alert('Error: '+e.message); }); };";
  inner += "window.toggleDebug=function(setting){ var valueId=setting+'-value'; var btnId=setting+'-btn'; var valueEl=$(valueId); var btnEl=$(btnId); if(!valueEl||!btnEl) return; var cur = (valueEl.textContent==='Enabled')?1:0; var newVal=cur?0:1; btnEl.textContent='...'; btnEl.disabled=true; valueEl.textContent=newVal?'Enabled':'Disabled'; var map={ debugAuthCookies:'debugauthcookies', debugHttp:'debughttp', debugSse:'debugsse', debugCli:'debugcli', debugCommandFlow:'debugcommandflow', debugUsers:'debugusers', debugWifi:'debugwifi', debugStorage:'debugstorage', debugPerformance:'debugperformance', debugDateTime:'debugdatetime', debugSensorsGeneral:'debugsensorsgeneral', debugSensorsFrame:'debugsensorsframe', debugSensorsData:'debugsensorsdata' }; var key=map[setting]||setting.toLowerCase(); var cmd= key + ' ' + newVal + ' temp'; fetch('/api/cli',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},credentials:'same-origin',body:'cmd='+encodeURIComponent(cmd)}).then(function(r){return r.text();}).then(function(_t){ btnEl.textContent=newVal?'Disable':'Enable'; btnEl.disabled=false; }).catch(function(e){ valueEl.textContent=cur?'Enabled':'Disabled'; btnEl.textContent=cur?'Disable':'Enable'; btnEl.disabled=false; alert('Error: '+e.message); }); };";
  inner += "window.saveDebugSettings=function(){ var cmds=[]; var getVal=function(k){ var el=$(k+'-value'); return el && (el.textContent==='Enabled'); }; var push=function(cmd){ cmds.push(cmd); }; if(getVal('debugAuthCookies')!==null){ push('debugauthcookies '+(getVal('debugAuthCookies')?1:0)); } push('debughttp '+(getVal('debugHttp')?1:0)); push('debugsse '+(getVal('debugSse')?1:0)); push('debugcli '+(getVal('debugCli')?1:0)); push('debugcommandflow '+(getVal('debugCommandFlow')?1:0)); push('debugusers '+(getVal('debugUsers')?1:0)); push('debugwifi '+(getVal('debugWifi')?1:0)); push('debugstorage '+(getVal('debugStorage')?1:0)); push('debugperformance '+(getVal('debugPerformance')?1:0)); push('debugdatetime '+(getVal('debugDateTime')?1:0)); push('debugsensorsgeneral '+(getVal('debugSensorsGeneral')?1:0)); push('debugsensorsframe '+(getVal('debugSensorsFrame')?1:0)); push('debugsensorsdata '+(getVal('debugSensorsData')?1:0)); Promise.all(cmds.map(function(c){ return fetch('/api/cli',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},credentials:'same-origin',body:'cmd='+encodeURIComponent(c)}).then(function(r){return r.text();}); })).then(function(){ alert('Debug settings saved.'); }).catch(function(){ alert('One or more debug commands failed.'); }); };";
  inner += "window.saveDeviceSensorSettings=function(){ var cmds=[]; var getInt=function(id,def){ var el=$(id); if(!el) return def; var n=parseInt(el.value,10); return isNaN(n)?def:n; }; var map=[ ['thermalTargetFps','thermaltargetfps'], ['thermalDevicePollMs','thermaldevicepollms'], ['tofDevicePollMs','tofdevicepollms'], ['imuDevicePollMs','imudevicepollms'], ['i2cClockThermalHz','i2cclockthermalHz'], ['i2cClockToFHz','i2cclocktofHz'] ]; map.forEach(function(pair){ var id=pair[0], cmdKey=pair[1]; var v=getInt(id,null); if(v!==null && v!==undefined){ cmds.push(cmdKey+' '+v); } }); if(cmds.length===0){ alert('No device settings to save.'); return; } Promise.all(cmds.map(function(c){ return fetch('/api/cli',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},credentials:'same-origin',body:'cmd='+encodeURIComponent(c)}).then(function(r){return r.text();}); })).then(function(){ alert('Device sensor settings saved.'); }).catch(function(){ alert('One or more device commands failed.'); }); };";
  inner += "window.saveSensorsUISettings=function(){ try { var $=function(id){return document.getElementById(id);}; var cmds=[]; var pushCmd=function(k,v){ cmds.push('set '+k+' '+v); }; var getInt=function(id){ var el=$(id); if(!el) return null; var n=parseInt(el.value,10); return isNaN(n)?null:n; }; var getStr=function(id){ var el=$(id); if(!el) return null; return String(el.value||''); }; var getBool=function(id){ var el=$(id); if(!el) return null; return el.checked ? 1 : 0; }; var tp=getInt('thermalPollingMs'); if(tp!==null) pushCmd('thermalPollingMs', tp); var tpf=getInt('tofPollingMs'); if(tpf!==null) pushCmd('tofPollingMs', tpf); var tss=getInt('tofStabilityThreshold'); if(tss!==null) pushCmd('tofStabilityThreshold', tss); var pal=getStr('thermalPaletteDefault'); if(pal) pushCmd('thermalPaletteDefault', pal); var twf=getInt('thermalWebMaxFps'); if(twf!==null) pushCmd('thermalWebMaxFps', twf); var ewma=getStr('thermalEWMAFactor'); if(ewma!==null) pushCmd('thermalEWMAFactor', ewma); var ttm=getInt('thermalTransitionMs'); if(ttm!==null) pushCmd('thermalTransitionMs', ttm); var ttm2=getInt('tofTransitionMs'); if(ttm2!==null) pushCmd('tofTransitionMs', ttm2); var tmax=getInt('tofUiMaxDistanceMm'); if(tmax!==null) pushCmd('tofUiMaxDistanceMm', tmax); var tie=getBool('thermalInterpolationEnabled'); if(tie!==null) pushCmd('thermalInterpolationEnabled', tie); var tis=getInt('thermalInterpolationSteps'); if(tis!==null) pushCmd('thermalInterpolationSteps', tis); var tib=getInt('thermalInterpolationBufferSize'); if(tib!==null) pushCmd('thermalInterpolationBufferSize', tib); var twq=getInt('thermalWebClientQuality'); if(twq!==null) pushCmd('thermalWebClientQuality', twq); if(cmds.length===0){ alert('No Sensors UI settings to save.'); return; } Promise.all(cmds.map(function(c){ return fetch('/api/cli',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},credentials:'same-origin',body:'cmd='+encodeURIComponent(c)}).then(function(r){return r.text();}); })).then(function(){ try{ if(typeof window.refreshSettings==='function'){ window.refreshSettings(); } }catch(_){ } alert('Sensors UI settings saved.'); }).catch(function(){ alert('One or more Sensors UI commands failed.'); }); } catch(e){ alert('Error: '+e.message); } };";
  inner += "window.disconnectWifi=function(){ if(confirm('Are you sure you want to disconnect from WiFi? You may lose connection to this device.')){ fetch('/api/cli',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},credentials:'same-origin',body:'cmd='+encodeURIComponent('wifidisconnect')}).then(function(r){return r.text();}).then(function(t){ alert(t||'Disconnected'); }).catch(function(e){ alert('Error: '+e.message); }); } };";
  inner += "window.toggleUserDropdown=function(username){ var dropdown=$('dropdown-'+username); if(!dropdown) return; var isVisible = dropdown.style.display==='block'; dropdown.style.display = isVisible?'none':'block'; };";
  inner += "window.revokeUserSessions=function(username){ if(!username||!confirm('Revoke all sessions for user: '+username+'?')) return; var cmd='session revoke user '+username; fetch('/api/cli',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},credentials:'same-origin',body:'cmd='+encodeURIComponent(cmd)}).then(function(r){return r.text();}).then(function(t){ alert(t||'Sessions revoked'); try{ refreshUsers(); }catch(_){} }).catch(function(e){ alert('Error: '+e.message); }); };";
  inner += "window.refreshUsers=function(){ var container=$('users-list'); if(!container) return; container.innerHTML='Loading...'; Promise.all([fetch('/api/cli',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},credentials:'same-origin',body:'cmd='+encodeURIComponent('user list json')}).then(function(r){return r.text();}),fetch('/api/cli',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},credentials:'same-origin',body:'cmd='+encodeURIComponent('session list json')}).then(function(r){return r.text();}),fetch('/api/cli',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},credentials:'same-origin',body:'cmd='+encodeURIComponent('pending list json')}).then(function(r){return r.text();})]).then(function(results){ var users=[],sessions=[],pending=[]; try{ users=JSON.parse(results[0]||'[]'); sessions=JSON.parse(results[1]||'[]'); pending=JSON.parse(results[2]||'[]'); }catch(e){ container.innerHTML='<div style=\"color:#dc3545\">Error parsing data: '+e.message+'</div>'; return; } if(!Array.isArray(users)||!Array.isArray(sessions)||!Array.isArray(pending)){ container.innerHTML='<div style=\"color:#dc3545\">Invalid data format</div>'; return; } var sessionsByUser={}; sessions.forEach(function(s){ var u=s.user||''; if(!sessionsByUser[u]) sessionsByUser[u]=[]; sessionsByUser[u].push(s); }); var html='<div style=\"display:grid;gap:0.5rem\">'; if(pending.length>0){ html+='<div style=\"background:#fff3cd;border:1px solid #ffeaa7;border-radius:4px;padding:0.75rem;margin-bottom:0.5rem\"><div style=\"font-weight:bold;color:#856404;margin-bottom:0.5rem\">Pending Approvals</div>'; pending.forEach(function(pendingUser){ var username=pendingUser.username||''; html+='<div style=\"margin-bottom:0.25rem\">'; html+='<div onclick=\"toggleUserDropdown(\\''+username+'-pending\\')\" style=\"display:flex;align-items:center;justify-content:space-between;padding:0.5rem;background:#fff;border:1px solid #ddd;border-radius:4px;cursor:pointer\">'; html+='<div><strong>'+username+'</strong> <span style=\"color:#856404;font-size:0.85rem;margin-left:0.5rem\">(pending)</span></div>'; html+='<div style=\"font-size:0.8rem;color:#666\">▼</div>'; html+='</div>'; html+='<div id=\"dropdown-'+username+'-pending\" style=\"display:none;background:#fff;border:1px solid #ddd;border-top:none;border-radius:0 0 4px 4px;box-shadow:0 2px 8px rgba(0,0,0,0.1);padding:0.5rem;margin-top:-1px\">'; html+='<button class=\"btn\" onclick=\"approveUserByName(\\''+username+'\\'); toggleUserDropdown(\\''+username+'-pending\\')\" style=\"width:100%;margin-bottom:0.25rem;font-size:0.8rem;padding:0.25rem 0.5rem\" title=\"Approve user\">Approve</button>'; html+='<button class=\"btn\" onclick=\"denyUserByName(\\''+username+'\\'); toggleUserDropdown(\\''+username+'-pending\\')\" style=\"width:100%;font-size:0.8rem;padding:0.25rem 0.5rem\" title=\"Deny user\">Deny</button>'; html+='</div>'; html+='</div>'; }); html+='</div>'; } if(users.length>0){ html+='<div style=\"background:#f8f9fa;border:1px solid #e5e7eb;border-radius:4px;padding:0.75rem\"><div style=\"font-weight:bold;color:#333;margin-bottom:0.5rem\">Active Users</div>'; users.forEach(function(user){ var username=user.username||''; var role=user.role||'user'; var isAdmin=(role==='admin'); var roleColor=isAdmin?'#28a745':'#6c757d'; var userSessions=sessionsByUser[username]||[]; var sessionInfo=''; var sessionCount=userSessions.length; if(sessionCount>0){ var activeSession=userSessions[0]; var lastSeenTime=activeSession.lastSeen; var lastSeenStr='Unknown'; try{ if(typeof lastSeenTime==='number'&&lastSeenTime>0){ lastSeenStr=new Date(lastSeenTime*1000).toLocaleString(); } else if(typeof lastSeenTime==='string'){ lastSeenStr=lastSeenTime; } }catch(_){} sessionInfo='<div style=\"font-size:0.75rem;color:#666;margin-top:0.25rem\">'+sessionCount+' session'+(sessionCount>1?'s':'')+' | IP: '+(activeSession.ip||'Unknown')+' | Last: '+lastSeenStr+'</div>'; } var actionBtn=''; var revokeBtn=sessionCount>0?'<button class=\"btn\" onclick=\"revokeUserSessions(\\''+username+'\\'); toggleUserDropdown(\\''+username+'\\')\" style=\"width:100%;margin-bottom:0.25rem;font-size:0.8rem;padding:0.25rem 0.5rem;background:#dc3545;color:white\" title=\"Revoke all sessions for this user\">Revoke Sessions ('+sessionCount+')</button>':''; if(isAdmin){ actionBtn=revokeBtn+'<button class=\"btn\" onclick=\"demoteUserByName(\\''+username+'\\'); toggleUserDropdown(\\''+username+'\\')\" style=\"width:100%;margin-bottom:0.25rem;font-size:0.8rem;padding:0.25rem 0.5rem\" title=\"Demote to regular user\">Demote</button><button class=\"btn\" onclick=\"deleteUserByName(\\''+username+'\\'); toggleUserDropdown(\\''+username+'\\')\" style=\"width:100%;font-size:0.8rem;padding:0.25rem 0.5rem\" title=\"Delete user\">Delete</button>'; } else { actionBtn=revokeBtn+'<button class=\"btn\" onclick=\"promoteUserByName(\\''+username+'\\'); toggleUserDropdown(\\''+username+'\\')\" style=\"width:100%;margin-bottom:0.25rem;font-size:0.8rem;padding:0.25rem 0.5rem\" title=\"Promote to admin\">Promote</button><button class=\"btn\" onclick=\"deleteUserByName(\\''+username+'\\'); toggleUserDropdown(\\''+username+'\\')\" style=\"width:100%;font-size:0.8rem;padding:0.25rem 0.5rem\" title=\"Delete user\">Delete</button>'; } html+='<div style=\"margin-bottom:0.25rem\">'; html+='<div onclick=\"toggleUserDropdown(\\''+username+'\\')\" style=\"padding:0.5rem;background:#fff;border:1px solid #ddd;border-radius:4px;cursor:pointer\">'; html+='<div style=\"display:flex;align-items:center;justify-content:space-between\">'; html+='<div><strong>'+username+'</strong> <span style=\"color:'+roleColor+';font-size:0.85rem;margin-left:0.5rem\">('+role+')</span>'+sessionInfo+'</div>'; html+='<div style=\"font-size:0.8rem;color:#666\">▼</div>'; html+='</div>'; html+='</div>'; html+='<div id=\"dropdown-'+username+'\" style=\"display:none;background:#fff;border:1px solid #ddd;border-top:none;border-radius:0 0 4px 4px;box-shadow:0 2px 8px rgba(0,0,0,0.1);padding:0.5rem;margin-top:-1px\">'; html+=actionBtn; html+='</div>'; html+='</div>'; }); html+='</div>'; } if(users.length===0&&pending.length===0){ html='<div style=\"color:#666\">No users or pending approvals found.</div>'; } html+='</div>'; container.innerHTML=html; }).catch(function(e){ container.innerHTML='<div style=\"color:#dc3545\">Error: '+e.message+'</div>'; }); };";
  inner += "window.promoteUserByName=function(username){ if(!username){ alert('Username required'); return; } if(!confirm('Promote user \"'+username+'\" to admin?')){ return; } var cmd='user promote '+username; fetch('/api/cli',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},credentials:'same-origin',body:'cmd='+encodeURIComponent(cmd)}).then(function(r){return r.text();}).then(function(t){ if(t.indexOf('Error')>=0){ alert('Error: '+t); } else { alert(t); try{ refreshUsers(); }catch(_){} } }).catch(function(e){ alert('Error: '+e.message); }); };";
  inner += "window.approveUserByName=function(username){ if(!username){ alert('Username required'); return; } if(!confirm('Approve user \"'+username+'\"?')){ return; } var cmd='user approve '+username; fetch('/api/cli',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},credentials:'same-origin',body:'cmd='+encodeURIComponent(cmd)}).then(function(r){return r.text();}).then(function(t){ if(t.indexOf('Error')>=0){ alert('Error: '+t); } else { alert(t); try{ refreshUsers(); }catch(_){} } }).catch(function(e){ alert('Error: '+e.message); }); };";
  inner += "window.denyUserByName=function(username){ if(!username){ alert('Username required'); return; } if(!confirm('Deny user \"'+username+'\"? This will permanently reject their registration.')){ return; } var cmd='user deny '+username; fetch('/api/cli',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},credentials:'same-origin',body:'cmd='+encodeURIComponent(cmd)}).then(function(r){return r.text();}).then(function(t){ if(t.indexOf('Error')>=0){ alert('Error: '+t); } else { alert(t); try{ refreshUsers(); }catch(_){} } }).catch(function(e){ alert('Error: '+e.message); }); };";
  inner += "window.demoteUserByName=function(username){ if(!username){ alert('Username required'); return; } if(!confirm('Demote admin user \"'+username+'\" to regular user?')){ return; } var cmd='user demote '+username; fetch('/api/cli',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},credentials:'same-origin',body:'cmd='+encodeURIComponent(cmd)}).then(function(r){return r.text();}).then(function(t){ if(t.indexOf('Error')>=0){ alert('Error: '+t); } else { alert(t); try{ refreshUsers(); }catch(_){} } }).catch(function(e){ alert('Error: '+e.message); }); };";
  inner += "window.deleteUserByName=function(username){ if(!username){ alert('Username required'); return; } if(!confirm('Delete user \"'+username+'\"? This action cannot be undone.')){ return; } var cmd='user delete '+username; fetch('/api/cli',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},credentials:'same-origin',body:'cmd='+encodeURIComponent(cmd)}).then(function(r){return r.text();}).then(function(t){ if(t.indexOf('Error')>=0){ alert('Error: '+t); } else { alert(t); try{ refreshUsers(); }catch(_){} } }).catch(function(e){ alert('Error: '+e.message); }); };";
  inner += "window.scanNetworks=function(){";
  inner += "  var container=$('wifi-scan-results');";
  inner += "  container.innerHTML='Scanning...';";
  inner += "  fetch('/api/cli',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},credentials:'same-origin',body:'cmd='+encodeURIComponent('wifiscan json')})";
  inner += "    .then(function(r){return r.text();})";
  inner += "    .then(function(txt){";
  inner += "      var data=[];";
  inner += "      try{ data=JSON.parse(txt||'[]'); }catch(_){ try{ data=JSON.parse((txt||'').substring((txt||'').indexOf('['))); }catch(__){ data=[]; } }";
  inner += "      if(!Array.isArray(data)){ data=[]; }";
  inner += "      var hiddenCount=0, visible=[];";
  inner += "      data.forEach(function(ap){ var isHidden=(!ap.ssid||ap.ssid.length===0||ap.hidden===true||ap.hidden==='true'); if(isHidden){ hiddenCount++; } else { visible.push(ap); } });";
  inner += "      visible.sort(function(a,b){ return (b.rssi||-999)-(a.rssi||-999); });";
  inner += "      var html='<div style=\\'margin-top:0.5rem\\'><strong>Nearby Networks</strong></div>';";
  inner += "      if(hiddenCount>0){ html+='<div style=\\'color:#666;font-size:0.85rem;margin-top:4px\\'>'+hiddenCount+' hidden network'+(hiddenCount>1?'s':'')+' detected</div>'; }";
  inner += "      if(visible.length===0){ html+='<div style=\\'color:#666\\'>No networks found.</div>'; } else {";
  inner += "        html+='<div style=\\'display:grid;grid-template-columns:repeat(auto-fit,minmax(260px,1fr));gap:0.5rem;margin-top:0.5rem\\'>';";
  inner += "        visible.forEach(function(ap){";
  inner += "          var ssid=(ap.ssid||'(hidden)');";
  inner += "          var lock=(ap.auth&&ap.auth!=='0')?'🔒':'🔓';";
  inner += "          var rssi=ap.rssi||-999; var ch=ap.channel||0;";
  inner += "          var saved=(__S.state.savedSSIDs||[]).indexOf(ssid)!==-1;";
  inner += "          var isCur=(ssid&&ssid===__S.state.currentSSID);";
  inner += "          var border=isCur?'#007bff':(saved?'#28a745':'#ddd');";
  inner += "          var badgeTxt=isCur?'(Connected)':(saved?'(Saved)':'');";
  inner += "          var badgeColor=isCur?'#007bff':(saved?'#28a745':'#666');";
  inner += "          var badge=badgeTxt?'<span style=\\'color:'+badgeColor+';font-weight:bold;margin-left:6px\\'>'+badgeTxt+'</span>':'';";
  inner += "          var esc=encodeURIComponent(ssid); var needsPass=(ap.auth&&ap.auth!=='0')?'true':'false';";
  inner += "          var btnCls = isCur ? ' vis-hidden' : '';";
  inner += "          html+='<div style=\\'background:#fff;border:1px solid '+border+';border-radius:6px;padding:0.5rem;display:flex;align-items:center;justify-content:space-between\\'>' +";
  inner += "               '<div><div style=\\'font-weight:bold\\'>'+ssid+' '+badge+'</div><div style=\\'color:#666;font-size:0.85rem\\'>RSSI '+rssi+' | CH '+ch+'</div></div>' +";
  inner += "               '<button class=\\'btn'+btnCls+'\\' data-ssid=\\''+esc+'\\' data-locked=\\''+needsPass+'\\' onclick=\\'(function(b){selectSsid(decodeURIComponent(b.dataset.ssid), b.dataset.locked===\"true\");})(this)\\'>Select '+lock+'</button>' +";
  inner += "               '</div>';";
  inner += "        });";
  inner += "        html+='</div>';";
  inner += "      }";
  inner += "      html+='<div style=\\'margin-top:0.5rem\\'><button class=\\'btn\\' onclick=\\'toggleManualConnect()\\'>Hidden network...</button></div>';";
  inner += "      container.innerHTML=html;";
  inner += "    }).catch(function(e){ container.textContent='Scan failed: '+e.message; });";
  inner += "};";
  inner += "window.selectSsid=function(ssid, needsPass){ try{ var p=$('wifi-connect-panel'); if(p){ p.style.display='block'; } var s=$('sel-ssid'); if(s){ s.textContent=ssid||''; } var inp=$('sel-pass'); if(inp){ inp.value=''; if(needsPass){ inp.placeholder='WiFi password'; inp.disabled=false; } else { inp.placeholder='(open network)'; inp.disabled=false; } } }catch(e){ console.error('[Page] SelectSsid fail', e); } };";
  inner += "window.connectSelected=function(){ try{ var ssidEl=$('sel-ssid'); var passEl=$('sel-pass'); var ssid=(ssidEl&&ssidEl.textContent)||''; var pass=(passEl&&passEl.value)||''; if(!ssid){ alert('No SSID selected'); return; } var cmd1='wifiadd '+ssid+' '+pass+' 1 0'; fetch('/api/cli',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},credentials:'same-origin',body:'cmd='+encodeURIComponent(cmd1)})";
  inner += ".then(function(r){ return r.text(); })";
  inner += ".then(function(t1){ return fetch('/api/cli',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},credentials:'same-origin',body:'cmd='+encodeURIComponent('wificonnect')}); })";
  inner += ".then(function(r){ return r.text(); })";
  inner += ".then(function(t2){ try{ alert(t2||'Connect attempted'); }catch(_){ } try{ refreshSettings(); }catch(_){ } })";
  inner += ".catch(function(e){ alert('Connect failed: '+e.message); }); }catch(e){ alert('Connect error: '+e.message); } };";
  inner += "window.connectManual=function(){ try{ var ssid=($('manual-ssid')&&$('manual-ssid').value)||''; var pass=($('manual-pass')&&$('manual-pass').value)||''; if(!ssid){ alert('Enter SSID'); return; } var cmd1='wifiadd '+ssid+' '+pass+' 1 1'; fetch('/api/cli',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},credentials:'same-origin',body:'cmd='+encodeURIComponent(cmd1)})";
  inner += ".then(function(r){ return r.text(); })";
  inner += ".then(function(t1){ return fetch('/api/cli',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},credentials:'same-origin',body:'cmd='+encodeURIComponent('wificonnect')}); })";
  inner += ".then(function(r){ return r.text(); })";
  inner += ".then(function(t2){ try{ alert(t2||'Connect attempted'); }catch(_){ } try{ refreshSettings(); }catch(_){ } })";
  inner += ".catch(function(e){ alert('Connect failed: '+e.message); }); }catch(e){ alert('Connect error: '+e.message); } };";
  inner += "window.toggleManualConnect=function(){ var p=$('wifi-manual-panel'); if(!p) return; p.style.display=(p.style.display==='none'||!p.style.display)?'block':'none'; };";
  inner += "window.togglePane=function(paneId,btnId){ var p=document.getElementById(paneId); var b=document.getElementById(btnId); if(!p||!b) return; var isHidden=(p.style.display==='none'||!p.style.display); p.style.display=isHidden?'block':'none'; b.textContent=isHidden?'Collapse':'Expand'; };";
  inner += "window.refreshPendingUsers=function(){ var container=$('pending-users'); if(!container) return; container.innerHTML='Loading...'; fetch('/api/admin/pending',{credentials:'same-origin'}).then(function(r){return r.json();}).then(function(data){ if(!data||!data.success){ container.innerHTML='<div style=\"color:#dc3545\">Failed to load pending users</div>'; return; } var pending=data.pending||[]; if(pending.length===0){ container.innerHTML='<div style=\"color:#666\">No pending approvals</div>'; return; } var html=''; pending.forEach(function(p){ var userDisplay=p.username||'Unknown'; var requestedDisplay=p.requested||'Unknown'; html+='<div style=\"background:#fff;border:1px solid #e5e7eb;border-radius:6px;padding:0.75rem;margin-bottom:0.5rem;display:flex;justify-content:space-between;align-items:center\">'; html+='<div><div style=\"font-weight:bold\">'+userDisplay+'</div><div style=\"color:#666;font-size:0.85rem\">Requested: '+requestedDisplay+'</div></div>'; html+='<div><button class=\"btn\" onclick=\"approveUser(\\''+userDisplay+'\\')\" style=\"margin-right:0.5rem\" title=\"Approve this user\">Approve</button>";
  inner += "<button class=\"btn\" onclick=\"rejectUser(\\''+userDisplay+'\\')\" title=\"Reject this user\">Reject</button></div>'; html+='</div>'; }); container.innerHTML=html; }).catch(function(e){ container.innerHTML='<div style=\"color:#dc3545\">Error: '+e.message+'</div>'; }); };";
  inner += "window.approveUser=function(username){ if(!username||!confirm('Approve user: '+username+'?')) return; fetch('/api/admin/approve',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},credentials:'same-origin',body:'username='+encodeURIComponent(username)}).then(function(r){return r.json();}).then(function(data){ if(data&&data.success){ alert('User approved'); refreshPendingUsers(); } else { alert('Failed to approve user: '+(data&&data.error||'Unknown error')); } }).catch(function(e){ alert('Error: '+e.message); }); };";
  inner += "window.rejectUser=function(username){ if(!username||!confirm('Reject user: '+username+'?')) return; fetch('/api/admin/reject',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},credentials:'same-origin',body:'username='+encodeURIComponent(username)}).then(function(r){return r.json();}).then(function(data){ if(data&&data.success){ alert('User rejected'); refreshPendingUsers(); } else { alert('Failed to reject user: '+(data&&data.error||'Unknown error')); } }).catch(function(e){ alert('Error: '+e.message); }); };";
  inner += "} catch(err){console.error('[Page] Settings chunk5 fail',err);} })();</script>";
  
  return htmlShellWithNav(username, "settings", inner);
}

#endif
