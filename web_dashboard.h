#ifndef WEB_DASHBOARD_H
#define WEB_DASHBOARD_H

String getDashboardPage(const String& username) {
  String inner;
  inner += "<h2>Dashboard</h2><p>Welcome, <strong>"+username+"</strong>.</p>";
  inner += "<p>WiFi IP: "+WiFi.localIP().toString()+"</p>";
  
  // Sensor Status Overview
  inner += "<div style='margin:2rem 0'>";
  inner += "<h3>Sensor Status</h3>";
  inner += "<div id='sensor-loading' style='text-align:center;padding:2rem;color:#87ceeb'>";
  inner += "<div style='font-size:1.1rem;margin-bottom:0.5rem'>Loading sensor status...</div>";
  inner += "<div style='font-size:0.9rem;opacity:0.7'>Checking connected sensors</div>";
  inner += "</div>";
  inner += "<div class='sensor-status-grid' id='sensor-grid' style='display:none;grid-template-columns:repeat(auto-fit,minmax(200px,1fr));gap:1rem;margin:1rem 0'>";
  inner += "</div>";

  inner += "</div>"; // End sensor-status-grid
  inner += "</div>"; // End sensor status section
  
  // System Stats Section
  inner += "<div style='margin:2rem 0'>";
  inner += "<h3>System Stats</h3>";
  inner += "<div class='system-grid' style='display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:1rem;margin:1rem 0'>";
  inner += "  <div class='sys-card' style='background:rgba(255,255,255,0.08);border-radius:8px;padding:0.75rem;border:1px solid rgba(255,255,255,0.15)'>Uptime: <strong id='sys-uptime'>--</strong></div>";
  inner += "  <div class='sys-card' style='background:rgba(255,255,255,0.08);border-radius:8px;padding:0.75rem;border:1px solid rgba(255,255,255,0.15)'>SSID: <strong id='sys-ssid'>--</strong></div>";
  inner += "  <div class='sys-card' style='background:rgba(255,255,255,0.08);border-radius:8px;padding:0.75rem;border:1px solid rgba(255,255,255,0.15)'>IP: <strong id='sys-ip'>--</strong></div>";
  inner += "  <div class='sys-card' style='background:rgba(255,255,255,0.08);border-radius:8px;padding:0.75rem;border:1px solid rgba(255,255,255,0.15)'>RSSI: <strong id='sys-rssi'>--</strong></div>";
  inner += "  <div class='sys-card' style='background:rgba(255,255,255,0.08);border-radius:8px;padding:0.75rem;border:1px solid rgba(255,255,255,0.15)'>Free Heap: <strong id='sys-heap'>--</strong></div>";
  inner += "  <div class='sys-card' style='background:rgba(255,255,255,0.08);border-radius:8px;padding:0.75rem;border:1px solid rgba(255,255,255,0.15)'>Free PSRAM: <strong id='sys-psram'>--</strong></div>";
  inner += "</div>"; // end system-grid
  inner += "</div>"; // end system stats section
  
  inner += "<p>Pages: <a href='/cli'>CLI</a> • <a href='/settings'>Settings</a> • <a href='/files'>Files</a> • <a href='/sensors'>Sensors</a></p>";
  
  // Add CSS for status indicators and SSE integration
  inner += "<style>";
  inner += ".status-indicator{display:inline-block;width:12px;height:12px;border-radius:50%;margin-right:8px}";
  inner += ".status-enabled{background:#28a745;animation:pulse 2s infinite}";
  inner += ".status-disabled{background:#dc3545}";
  inner += "@keyframes pulse{0%{opacity:1}50%{opacity:0.5}100%{opacity:1}}";
  inner += "</style>";
  
  // Dashboard JavaScript - broken into logical sections with debug checkpoints
  inner += "<script>console.log('[Dashboard] Section 1: Pre-script sentinel');</script>";
  
  // Section 1: Core Dashboard Object
  inner += "<script>";
  inner += "console.log('[Dashboard] Section 2: Starting core object definition');";
  inner += "(function(){";
  inner += "  console.log('[Dashboard] Section 2a: Inside IIFE wrapper');";
  inner += "  const Dash = {";
  inner += "    log: function(){ try{ console.log.apply(console, arguments); }catch(_){ } },";
  inner += "    setText: function(id, v){";
  inner += "      var el=document.getElementById(id);";
  inner += "      if(el) el.textContent=v;";
  inner += "    }";
  inner += "  };";
  inner += "  console.log('[Dashboard] Section 2b: Basic Dash object created');";
  inner += "  window.Dash = Dash;";
  inner += "})();";
  inner += "</script>";
  
  // Section 2: Indicator Functions
  inner += "<script>";
  inner += "console.log('[Dashboard] Section 3: Adding indicator functions');";
  inner += "if (window.Dash) {";
  inner += "  window.Dash.setIndicator = function(id, on){";
  inner += "    var el=document.getElementById(id);";
  inner += "    if(el){ el.className = on ? 'status-indicator status-enabled' : 'status-indicator status-disabled'; }";
  inner += "  };";
  inner += "  console.log('[Dashboard] Section 3a: setIndicator added');";
  inner += "} else { console.error('[Dashboard] Section 3: Dash object not found!'); }";
  inner += "</script>";
  
  // Section 3: Sensor Status Functions
  inner += "<script>";
  inner += "console.log('[Dashboard] Section 4: Adding sensor status functions');";
  inner += "if (window.Dash) {";
  inner += "  window.Dash.updateSensorStatus = function(d){";
  inner += "    if(!d) return;";
  inner += "    try{";
  inner += "      var imuOn=!!(d.imuEnabled||d.imu);";
  inner += "      var thermOn=!!(d.thermalEnabled||d.thermal);";
  inner += "      var tofOn=!!(d.tofEnabled||d.tof);";
  inner += "      var apdsOn=!!(d.apdsColorEnabled||d.apdsProximityEnabled||d.apdsGestureEnabled);";
  inner += "      var gameOn=!!(d.gamepadEnabled||d.gamepad);";
  inner += "      window.Dash.setIndicator('dash-imu-status', imuOn);";
  inner += "      window.Dash.setIndicator('dash-thermal-status', thermOn);";
  inner += "      window.Dash.setIndicator('dash-tof-status', tofOn);";
  inner += "      window.Dash.setIndicator('dash-apds-status', apdsOn);";
  inner += "      window.Dash.setIndicator('dash-gamepad-status', gameOn);";
  inner += "    }catch(e){ console.warn('[Dashboard] Sensor status update error', e); }";
  inner += "  };";
  inner += "  window.Dash.updateDeviceVisibility = function(registry){";
  inner += "    if(!registry || !registry.devices) return;";
  inner += "    try{";
  inner += "      var devices = registry.devices;";
  inner += "      var hasIMU = devices.some(function(d){ return d.name === 'BNO055'; });";
  inner += "      var hasThermal = devices.some(function(d){ return d.name === 'MLX90640'; });";
  inner += "      var hasToF = devices.some(function(d){ return d.name === 'VL53L4CX'; });";
  inner += "      var hasAPDS = devices.some(function(d){ return d.name === 'APDS9960'; });";
  inner += "      var hasGamepad = devices.some(function(d){ return d.name === 'Seesaw'; });";
  inner += "      var hasDRV = devices.some(function(d){ return d.name === 'DRV2605'; });";
  inner += "      window.Dash.showHideCard('dash-imu-card', hasIMU);";
  inner += "      window.Dash.showHideCard('dash-thermal-card', hasThermal);";
  inner += "      window.Dash.showHideCard('dash-tof-card', hasToF);";
  inner += "      window.Dash.showHideCard('dash-apds-card', hasAPDS);";
  inner += "      window.Dash.showHideCard('dash-gamepad-card', hasGamepad);";
  inner += "      window.Dash.showHideCard('dash-haptic-card', hasDRV);";
  inner += "      console.log('[Dashboard] Device visibility updated - IMU:'+hasIMU+' Thermal:'+hasThermal+' ToF:'+hasToF+' APDS:'+hasAPDS+' Gamepad:'+hasGamepad+' Haptic:'+hasDRV);";
  inner += "    }catch(e){ console.warn('[Dashboard] Device visibility update error', e); }";
  inner += "  };";
  inner += "  window.Dash.showHideCard = function(cardId, show){";
  inner += "    var card = document.getElementById(cardId);";
  inner += "    if(card) card.style.display = show ? 'block' : 'none';";
  inner += "  };";
  inner += "  console.log('[Dashboard] Section 4a: updateSensorStatus added');";
  inner += "} else { console.error('[Dashboard] Section 4: Dash object not found!'); }";
  inner += "</script>";
  
  // Section 4: System Status Functions
  inner += "<script>";
  inner += "console.log('[Dashboard] Section 5: Adding system status functions');";
  inner += "if (window.Dash) {";
  inner += "  window.Dash.updateSystem = function(d){";
  inner += "    try {";
  inner += "      if (!d) return;";
  inner += "      if (d.uptime_hms) window.Dash.setText('sys-uptime', d.uptime_hms);";
  inner += "      if (d.net) {";
  inner += "        if (d.net.ssid != null) window.Dash.setText('sys-ssid', d.net.ssid);";
  inner += "        if (d.net.ip   != null) window.Dash.setText('sys-ip',   d.net.ip);";
  inner += "        if (d.net.rssi != null) window.Dash.setText('sys-rssi', d.net.rssi + ' dBm');";
  inner += "      }";
  inner += "      if (d.mem) {";
  inner += "        var heapTxt = null;";
  inner += "        if (d.mem.heap_free_kb != null) {";
  inner += "          if (d.mem.heap_total_kb != null) {";
  inner += "            heapTxt = d.mem.heap_free_kb + '/' + d.mem.heap_total_kb + ' KB';";
  inner += "          } else {";
  inner += "            heapTxt = d.mem.heap_free_kb + ' KB';";
  inner += "          }";
  inner += "        }";
  inner += "        if (heapTxt != null) window.Dash.setText('sys-heap', heapTxt);";
  inner += "        var psTxt = null;";
  inner += "        var hasPs = (d.mem.psram_free_kb != null) || (d.mem.psram_total_kb != null);";
  inner += "        if (hasPs) {";
  inner += "          var pf = (d.mem.psram_free_kb  != null) ? d.mem.psram_free_kb  : null;";
  inner += "          var pt = (d.mem.psram_total_kb != null) ? d.mem.psram_total_kb : null;";
  inner += "          if (pf != null && pt != null) psTxt = pf + '/' + pt + ' KB';";
  inner += "          else if (pf != null) psTxt = pf + ' KB';";
  inner += "        }";
  inner += "        if (psTxt != null) window.Dash.setText('sys-psram', psTxt);";
  inner += "      }";
  inner += "    } catch(e) { console.warn('[Dashboard] System update error', e); }";
  inner += "  };";
  inner += "  console.log('[Dashboard] Section 5a: updateSystem added');";
  inner += "} else { console.error('[Dashboard] Section 5: Dash object not found!'); }";
  inner += "</script>";
  
  // Section 5: Global Variables
  inner += "<script>";
  inner += "console.log('[Dashboard] Section 6: Setting up global variables');";
  inner += "window.__sensorStatusSeq = 0;";
  inner += "window.__probeCooldownMs = 10000;";
  inner += "window.__lastProbeAt = 0;";
  inner += "console.log('[Dashboard] Section 6a: Global variables set');";
  inner += "</script>";
  
  // Section 6: Sensor Status Functions - Part 1
  inner += "<script>";
  inner += "console.log('[Dashboard] Section 7a: Adding applySensorStatus function');";
  inner += "window.applySensorStatus = function(s){";
  inner += "  console.log('[Dashboard] applySensorStatus called with:', s);";
  inner += "  if(!s) return;";
  inner += "  window.__sensorStatusSeq = s.seq || 0;";
  inner += "};";
  inner += "console.log('[Dashboard] Section 7a: applySensorStatus function added');";
  inner += "</script>";
  
  // Section 6: Sensor Status Functions - Part 2  
  inner += "<script>";
  inner += "console.log('[Dashboard] Section 7b: Adding sensor card creation');";
  inner += "window.createSensorCards = function(sensorStatus, deviceRegistry){";
  inner += "  console.log('[Dashboard] createSensorCards called with status:', sensorStatus, 'registry:', deviceRegistry);";
  inner += "  var loading = document.getElementById('sensor-loading');";
  inner += "  var grid = document.getElementById('sensor-grid');";
  inner += "  if(loading) loading.style.display = 'none';";
  inner += "  if(grid) { grid.style.display = 'grid'; grid.innerHTML = ''; }";
  inner += "  var availableSensors = window.getAvailableSensors(deviceRegistry);";
  inner += "  console.log('[Dashboard] Available sensors from getAvailableSensors:', availableSensors);";
  inner += "  var cardCount = 0;";
  inner += "  for(var i = 0; i < availableSensors.length; i++){";
  inner += "    var sensor = availableSensors[i];";
  inner += "    var enabled = window.getSensorEnabled(sensor.key, sensorStatus);";
  inner += "    var card = document.createElement('div');";
  inner += "    card.className = 'sensor-status-card';";
  inner += "    card.id = 'dash-' + sensor.key + '-card';";
  inner += "    card.style.cssText = 'background:rgba(255,255,255,0.1);border-radius:8px;padding:1rem;border:1px solid rgba(255,255,255,0.2)';";
  inner += "    var statusClass = enabled ? 'status-enabled' : 'status-disabled';";
  inner += "    var statusText = enabled ? 'Running' : 'Available';";
  inner += "    var statusColor = enabled ? '#28a745' : '#87ceeb';";
  inner += "    card.innerHTML = '<div style=\"display:flex;align-items:center;gap:0.5rem;margin-bottom:0.5rem\"><span class=\"status-indicator ' + statusClass + '\" id=\"dash-' + sensor.key + '-status\"></span><strong>' + sensor.name + '</strong><span style=\"margin-left:auto;font-size:0.8rem;color:' + statusColor + '\">' + statusText + '</span></div><div style=\"font-size:0.9rem;color:#87ceeb\">' + sensor.desc + '</div>';";
  inner += "    grid.appendChild(card);";
  inner += "    cardCount++;";
  inner += "  }";
  inner += "  if(cardCount === 0 && grid){";
  inner += "    grid.innerHTML = '<div style=\"grid-column:1/-1;text-align:center;padding:2rem;color:#87ceeb;font-style:italic\">No sensors detected</div>';";
  inner += "  }";
  inner += "  console.log('[Dashboard] Created', cardCount, 'sensor cards');";
  inner += "};";
  inner += "console.log('[Dashboard] Section 7b: createSensorCards function added');";
  inner += "</script>";
  
  // Section 6: Sensor Status Functions - Part 3
  inner += "<script>";
  inner += "console.log('[Dashboard] Section 7c: Adding helper functions');";
  inner += "window.getAvailableSensors = function(deviceRegistry){";
  inner += "  console.log('[Dashboard] getAvailableSensors called with:', deviceRegistry);";
  inner += "  var sensors = [];";
  inner += "  if(!deviceRegistry || !deviceRegistry.devices) {";
  inner += "    console.log('[Dashboard] No device registry or devices array');";
  inner += "    return sensors;";
  inner += "  }";
  inner += "  console.log('[Dashboard] Processing', deviceRegistry.devices.length, 'devices');";
  inner += "  deviceRegistry.devices.forEach(function(device){";
  inner += "    console.log('[Dashboard] Checking device:', device.name);";
  inner += "    if(device.name === 'BNO055'){";
  inner += "      sensors.push({key:'imu', name:'IMU (BNO055)', desc:'Gyroscope & Accelerometer'});";
  inner += "      console.log('[Dashboard] Added IMU sensor');";
  inner += "    } else if(device.name === 'MLX90640'){";
  inner += "      sensors.push({key:'thermal', name:'Thermal (MLX90640)', desc:'32x24 IR Camera'});";
  inner += "      console.log('[Dashboard] Added Thermal sensor');";
  inner += "    } else if(device.name === 'VL53L4CX'){";
  inner += "      sensors.push({key:'tof', name:'ToF (VL53L4CX)', desc:'Distance Measurement'});";
  inner += "      console.log('[Dashboard] Added ToF sensor');";
  inner += "    } else if(device.name === 'APDS-9960'){";
  inner += "      sensors.push({key:'apds', name:'RGB (APDS-9960)', desc:'Color & Gesture'});";
  inner += "      console.log('[Dashboard] Added APDS sensor');";
  inner += "    } else if(device.name === 'DRV2605'){";
  inner += "      sensors.push({key:'haptic', name:'Haptic (DRV2605)', desc:'Motor Driver'});";
  inner += "      console.log('[Dashboard] Added Haptic sensor');";
  inner += "    } else {";
  inner += "      console.log('[Dashboard] Unknown device name:', device.name);";
  inner += "    }";
  inner += "  });";
  inner += "  console.log('[Dashboard] getAvailableSensors returning', sensors.length, 'sensors:', sensors);";
  inner += "  return sensors;";
  inner += "};";
  inner += "window.getSensorEnabled = function(key, status){";
  inner += "  if(!status) return false;";
  inner += "  if(key === 'imu') return !!status.imuEnabled;";
  inner += "  if(key === 'thermal') return !!status.thermalEnabled;";
  inner += "  if(key === 'tof') return !!status.tofEnabled;";
  inner += "  if(key === 'apds') return !!(status.apdsColorEnabled || status.apdsProximityEnabled || status.apdsGestureEnabled);";
  inner += "  if(key === 'haptic') return !!status.hapticEnabled;";
  inner += "  return false;";
  inner += "};";
  inner += "console.log('[Dashboard] Section 7c: Helper functions added');";
  inner += "</script>";
  
  // Section 6: Sensor Status Functions - Part 4
  inner += "<script>";
  inner += "console.log('[Dashboard] Section 7d: Updating applySensorStatus to use helpers');";
  inner += "window.__deviceRegistry = null;";
  inner += "window.applySensorStatus = function(s){";
  inner += "  console.log('[Dashboard] applySensorStatus called with:', s);";
  inner += "  if(!s) return;";
  inner += "  window.__sensorStatusSeq = s.seq || 0;";
  inner += "  if(window.__deviceRegistry){";
  inner += "    console.log('[Dashboard] Using cached device registry:', window.__deviceRegistry);";
  inner += "    window.createSensorCards(s, window.__deviceRegistry);";
  inner += "  } else {";
  inner += "    console.log('[Dashboard] Device registry not loaded yet, fetching...');";
  inner += "    window.fetchDeviceRegistry().then(function(registry){ ";
  inner += "      console.log('[Dashboard] Fetch complete, calling createSensorCards with:', registry);";
  inner += "      window.createSensorCards(s, registry || window.__deviceRegistry); ";
  inner += "    });";
  inner += "  }";
  inner += "  if (window.Dash) window.Dash.updateSensorStatus(s);";
  inner += "};";
  inner += "window.fetchDeviceRegistry = function(){";
  inner += "  console.log('[Dashboard] fetchDeviceRegistry called');";
  inner += "  return fetch('/api/devices', { credentials: 'include', cache: 'no-store' })";
  inner += "    .then(function(r){ console.log('[Dashboard] Device registry fetch response:', r.status); return r.json(); })";
  inner += "    .then(function(d){ ";
  inner += "      console.log('[Dashboard] Setting window.__deviceRegistry to:', d);";
  inner += "      window.__deviceRegistry = d; ";
  inner += "      console.log('[Dashboard] Device registry loaded and stored:', window.__deviceRegistry);";
  inner += "      if (window.Dash) window.Dash.updateDeviceVisibility(d);";
  inner += "      return d;";
  inner += "    })";
  inner += "    .catch(function(e){ console.warn('[Dashboard] Device registry fetch failed:', e); return null; });";
  inner += "};";
  inner += "console.log('[Dashboard] Section 7d: applySensorStatus updated');";
  inner += "</script>";
  
  // Section 6: Sensor Status Functions - Part 5
  inner += "<script>";
  inner += "console.log('[Dashboard] Section 7e: Adding fetchSensorStatus');";
  inner += "window.fetchSensorStatus = function(){";
  inner += "  console.log('[Dashboard] Fetching sensor status...');";
  inner += "  return fetch('/api/sensors/status', { credentials: 'include', cache: 'no-store' })";
  inner += "    .then(function(r){ console.log('[Dashboard] Sensor status response:', r.status); return r.json(); })";
  inner += "    .then(function(j){ ";
  inner += "      console.log('[Dashboard] Raw sensor status data:', JSON.stringify(j, null, 2)); ";
  inner += "      console.log('[Dashboard] Individual sensor states:');";
  inner += "      console.log('  - imuEnabled:', j.imuEnabled);";
  inner += "      console.log('  - thermalEnabled:', j.thermalEnabled);";
  inner += "      console.log('  - tofEnabled:', j.tofEnabled);";
  inner += "      console.log('  - apdsColorEnabled:', j.apdsColorEnabled);";
  inner += "      window.applySensorStatus(j); ";
  inner += "    })";
  inner += "    .catch(function(e){ console.warn('[Dashboard] sensor status fetch failed', e); });";
  inner += "};";
  inner += "console.log('[Dashboard] Section 7e: fetchSensorStatus added');";
  inner += "</script>";
  
  // Section 7: SSE Functions
  inner += "<script>";
  inner += "console.log('[Dashboard] Section 8: Adding SSE functions');";
  inner += "window.createSSEIfNeeded = function(){";
  inner += "  try {";
  inner += "    console.log('[Dashboard] Creating SSE connection...');";
  inner += "    if (!window.EventSource) { console.warn('[Dashboard] EventSource not supported'); return false; }";
  inner += "    if (window.__es) {";
  inner += "      var rs = -1;";
  inner += "      try {";
  inner += "        if (typeof window.__es.readyState !== 'undefined') rs = window.__es.readyState;";
  inner += "      } catch(_) {}";
  inner += "      console.log('[Dashboard] Existing SSE readyState:', rs);";
  inner += "      if (rs === 2) {";
  inner += "        console.log('[Dashboard] Closing existing SSE connection');";
  inner += "        try { window.__es.close(); } catch(_) {}";
  inner += "        window.__es = null;";
  inner += "      }";
  inner += "    }";
  inner += "    if (window.__es) { console.log('[Dashboard] Using existing SSE connection'); return true; }";
  inner += "    console.log('[Dashboard] Opening new SSE to /api/events');";
  inner += "    var es = new EventSource('/api/events');";
  inner += "    es.onopen = function(){ console.log('[Dashboard] SSE connection opened'); };";
  inner += "    es.onerror = function(e){";
  inner += "      console.warn('[Dashboard] SSE error:', e);";
  inner += "      try { es.close(); } catch(_) {}";
  inner += "      window.__es = null;";
  inner += "    };";
  inner += "    window.__es = es;";
  inner += "    return true;";
  inner += "  } catch(e) { console.error('[Dashboard] SSE creation failed:', e); return false; }";
  inner += "};";
  inner += "console.log('[Dashboard] Section 8a: createSSEIfNeeded added');";
  inner += "</script>";
  
  // Section 8: SSE Attachment
  inner += "<script>";
  inner += "console.log('[Dashboard] Section 9: Adding SSE attachment');";
  inner += "window.attachSSE = function(){";
  inner += "  try {";
  inner += "    console.log('[Dashboard] Attaching SSE event listeners...');";
  inner += "    if (!window.__es) { console.warn('[Dashboard] No SSE connection to attach to'); return false; }";
  inner += "    var handler = function(e){";
  inner += "      try {";
  inner += "        console.log('[Dashboard] Received sensor-status event:', e.data);";
  inner += "        var dj = JSON.parse(e.data || '{}');";
  inner += "        var seq = (dj && dj.seq) ? dj.seq : 0;";
  inner += "        var cur = window.__sensorStatusSeq || 0;";
  inner += "        if (seq <= cur) return;";
  inner += "        window.__sensorStatusSeq = seq;";
  inner += "        if (window.applySensorStatus) window.applySensorStatus(dj);";
  inner += "      } catch(err) { console.warn('[Dashboard] SSE sensor-status parse error', err); }";
  inner += "    };";
  inner += "    window.__es.addEventListener('sensor-status', handler);";
  inner += "    console.log('[Dashboard] Added sensor-status listener');";
  inner += "    window.__es.addEventListener('system', function(e){";
  inner += "      try {";
  inner += "        console.log('[Dashboard] Received system event:', e.data);";
  inner += "        var dj = JSON.parse(e.data || '{}');";
  inner += "        if (window.Dash) {";
  inner += "          console.log('[Dashboard] Calling updateSystem with:', dj);";
  inner += "          window.Dash.updateSystem(dj);";
  inner += "        } else {";
  inner += "          console.warn('[Dashboard] Dash object not available for system update');";
  inner += "        }";
  inner += "      } catch(err){";
  inner += "        console.warn('[Dashboard] SSE system parse error', err);";
  inner += "      }";
  inner += "    });";
  inner += "    console.log('[Dashboard] Added system listener');";
  inner += "    return true;";
  inner += "  } catch(e) { console.error('[Dashboard] SSE attachment failed:', e); return false; }";
  inner += "};";
  inner += "console.log('[Dashboard] Section 9a: attachSSE added');";
  inner += "</script>";
  
  // Section 9: Utility Functions
  inner += "<script>";
  inner += "console.log('[Dashboard] Section 10: Adding utility functions');";
  // fetchDeviceRegistry is defined in Section 7d - don't duplicate it here";
  inner += "window.fetchSystemStatus = function(){";
  inner += "  console.log('[Dashboard] Fetching system status via API...');";
  inner += "  return fetch('/api/system', { credentials: 'include', cache: 'no-store' })";
  inner += "    .then(function(r){ ";
  inner += "      console.log('[Dashboard] System status response:', r.status); ";
  inner += "      if (!r.ok) throw new Error('HTTP ' + r.status);";
  inner += "      return r.json(); ";
  inner += "    })";
  inner += "    .then(function(j){ ";
  inner += "      console.log('[Dashboard] System status data:', j); ";
  inner += "      if (window.Dash) window.Dash.updateSystem(j);";
  inner += "    })";
  inner += "    .catch(function(e){ console.warn('[Dashboard] System status fetch failed:', e); });";
  inner += "};";
  inner += "window.setupSensorSSE = function(){";
  inner += "  console.log('[Dashboard] Setting up sensor-only SSE...');";
  inner += "  if (window.createSSEIfNeeded) window.createSSEIfNeeded();";
  inner += "  if (window.attachSSE) window.attachSSE();";
  inner += "};";
  inner += "console.log('[Dashboard] Section 10a: Utility functions added');";
  inner += "</script>";
  
  // Section 10: Initialization
  inner += "<script>";
  inner += "console.log('[Dashboard] Section 11: DOM initialization');";
  inner += "document.addEventListener('DOMContentLoaded', function(){";
  inner += "  try {";
  inner += "    console.log('[Dashboard] Section 11a: DOM loaded, initializing...');";
  inner += "    if (window.fetchDeviceRegistry) window.fetchDeviceRegistry();";
  inner += "    if (window.fetchSensorStatus) window.fetchSensorStatus();";
  inner += "    if (window.fetchSystemStatus) window.fetchSystemStatus();";
  inner += "    if (window.createSSEIfNeeded) window.createSSEIfNeeded();";
  inner += "    if (window.attachSSE) window.attachSSE();";
  inner += "    console.log('[Dashboard] Section 11b: All initialization complete');";
  inner += "  } catch(e) { console.error('[Dashboard] DOM init error', e); }";
  inner += "});";
  inner += "console.log('[Dashboard] Section 11c: DOM listener registered');";
  inner += "</script>";
  
  return htmlShellWithNav(username, "dashboard", inner);
}

#endif