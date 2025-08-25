#ifndef WEB_SENSORS_H
#define WEB_SENSORS_H

#include <Arduino.h>
#include "web_shared.h"

String getSensorsContent() {
  String inner = 
    "<style>"
    ".sensors-container { max-width: 1200px; margin: 0 auto; padding: 20px; }"
    ".sensor-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(350px, 1fr)); gap: 20px; margin-bottom: 30px; }"
    ".sensor-card { background: rgba(255,255,255,0.9); border-radius: 15px; padding: 20px; box-shadow: 0 4px 6px rgba(0,0,0,0.1); border: 1px solid rgba(255,255,255,0.2); }"
    ".sensor-title { font-size: 1.3em; font-weight: bold; margin-bottom: 10px; color: #333; display: flex; align-items: center; gap: 10px; }"
    ".sensor-description { color: #666; margin-bottom: 15px; font-size: 0.9em; }"
    ".sensor-controls { display: flex; gap: 10px; margin-bottom: 15px; flex-wrap: wrap; }"
    ".sensor-data { background: #f8f9fa; border-radius: 8px; padding: 15px; font-family: 'Courier New', monospace; font-size: 0.9em; border-left: 4px solid #007bff; min-height: 60px; }"
    ".status-indicator { display: inline-block; width: 12px; height: 12px; border-radius: 50%; margin-right: 8px; }"
    ".status-enabled { background: #28a745; animation: pulse 2s infinite; }"
    ".status-disabled { background: #dc3545; }"
    "@keyframes pulse { 0% { opacity: 1; } 50% { opacity: 0.5; } 100% { opacity: 1; } }"
    ".thermal-pixel { width: 100%; height: 100%; background-clip: padding-box; border-radius: 0; transition: background-color 0.12s linear; }"
    "#thermalGrid { width: 320px; height: 240px; display: grid; grid-template-columns: repeat(32, 1fr); grid-auto-rows: 1fr; }"
    ".tof-objects-container { display: flex; flex-direction: column; gap: 8px; }"
    ".tof-object-row { display: flex; align-items: center; gap: 10px; padding: 8px; background: #ffffff; border: 1px solid #dee2e6; border-radius: 4px; box-shadow: 0 1px 2px rgba(0,0,0,0.06); }"
    ".object-label { min-width: 70px; font-size: 0.9em; font-weight: bold; color: #212529; }"
    ".distance-bar-container { flex: 1; height: 18px; background: #f1f3f5; border-radius: 0; position: relative; overflow: hidden; border: 1px solid #ced4da; }"
    ".distance-bar { height: 100%; background: #4caf50; border-radius: 0; transition: width 0.2s ease; width: 0%; }"
    ".distance-bar.invalid { background: #9e9e9e; opacity: 0.4; }"
    ".object-info { min-width: 80px; font-size: 0.9em; text-align: right; color: #212529; font-weight: 600; }"
    "</style>"
    
    "<div class='sensors-container'>"
    "<div class='sensor-grid'>"
    
    // Gyroscope & Accelerometer (LSM6DS3)
    "<div class='sensor-card'>"
    "<div class='sensor-title'>"
    "<span>Gyroscope & Accelerometer (LSM6DS3)</span>"
    "<span class='status-indicator status-disabled' id='gyro-status-indicator'></span>"
    "</div>"
    "<div class='sensor-description'>9-axis IMU sensor for precise orientation, acceleration, and gyroscope data.</div>"
    "<div class='sensor-controls'>"
    "<button class='btn' id='btn-imu-start'>Start IMU</button>"
    "<button class='btn' id='btn-imu-stop'>Stop IMU</button>"
    "<button class='btn' id='btn-imu-read'>Read Data</button>"
    "</div>"
    "<div class='sensor-data' id='gyro-data'>IMU sensor data will appear here...</div>"
    "</div>"
    
    
    // RGB Gesture Sensor
    "<div class='sensor-card'>"
    "<div class='sensor-title'>"
    "<span>RGB Gesture Sensor (APDS-9960)</span>"
    "<span class='status-indicator status-disabled' id='apds-status-indicator'></span>"
    "</div>"
    "<div class='sensor-description'>RGB color, gesture, and proximity sensor for ambient light and motion detection.</div>"
    "<div class='sensor-controls'>"
    "<button class='btn' id='btn-apdscolor-start'>Start Color</button>"
    "<button class='btn' id='btn-apdscolor-stop'>Stop Color</button>"
    "<button class='btn' id='btn-apdscolor-read'>Read Color</button>"
    "<button class='btn' id='btn-apdsproximity-start'>Start Proximity</button>"
    "<button class='btn' id='btn-apdsproximity-stop'>Stop Proximity</button>"
    "<button class='btn' id='btn-apdsproximity-read'>Read Proximity</button>"
    "<button class='btn' id='btn-apdsgesture-start'>Start Gesture</button>"
    "<button class='btn' id='btn-apdsgesture-stop'>Stop Gesture</button>"
    "<button class='btn' id='btn-apdsgesture-read'>Read Gesture</button>"
    "</div>"
    "<div class='sensor-data' id='apds-data'>APDS-9960 sensor data will appear here...</div>"
    "</div>"
    
    // Thermal Camera
    "<div class='sensor-card'>"
    "<div class='sensor-title'>"
    "<span>Thermal Camera (MLX90640)</span>"
    "<span class='status-indicator status-disabled' id='thermal-status-indicator'></span>"
    "</div>"
    "<div class='sensor-description'>32x24 thermal infrared camera for high-resolution temperature imaging and heat detection.</div>"
    "<div class='sensor-controls'>"
    "<button class='btn' id='btn-thermal-start'>Start Thermal</button>"
    "<button class='btn' id='btn-thermal-stop'>Stop Thermal</button>"
    "<label for='thermal-palette-select' style='margin-left:8px; font-size:0.9em; color:#555;'>Palette:</label>"
    "<select id='thermal-palette-select' class='menu-item' style='margin-left:4px; padding:4px;'>"
    "<option value='turbo' selected>Turbo</option>"
    "<option value='ironbow'>Ironbow</option>"
    "<option value='grayscale'>Grayscale</option>"
    "<option value='rainbow'>Rainbow</option>"
    "</select>"
    "</div>"
    "<div class='sensor-data' id='thermal-data'>"
    "<div id='thermal-stats'>Min: <span id='thermalMin'>--</span>&deg;C, Max: <span id='thermalMax'>--</span>&deg;C, Avg: <span id='thermalAvg'>--</span>&deg;C, FPS: --</div>"
    "<div id='thermal-performance' style='font-size: 0.9em; color: #888; margin-top: 5px;'>Capture: --ms, Hottest: [--,--] = --&deg;C</div>"
    "<div id='thermalGrid' style='margin-top: 10px; display: grid; grid-template-columns: repeat(32, 1fr); gap: 0px; width: 320px; height: 240px;'>"
    "<!-- 768 thermal pixels (32x24 grid) generated by JavaScript -->"
    "</div>"
    "</div>"
    "</div>"
    
    // ToF Distance Sensor
    "<div class='sensor-card'>"
    "<div class='sensor-title'>"
    "<span>ToF Distance Sensor</span>"
    "<span class='status-indicator status-disabled' id='tof-status-indicator'></span>"
    "</div>"
    "<div class='sensor-description'>VL53L4CX Time-of-Flight sensor for precise distance measurement up to 4 meters.</div>"
    "<div class='sensor-controls'>"
    "<button class='btn' id='btn-tof-start'>Start ToF</button>"
    "<button class='btn' id='btn-tof-stop'>Stop ToF</button>"
    "</div>"
    "<div class='sensor-data' id='tof-data'>ToF sensor data will appear here...</div>"
    "<div id='tof-objects-display' style='margin-top: 15px; display: none;'>"
    "<div style='font-weight: bold; margin-bottom: 10px; color: #333;'>Multi-Object Detection (0-<span id='tof-range-mm'>3400</span>mm)</div>"
    "<div class='tof-objects-container'>"
    "<div class='tof-object-row' id='tof-object-1'>"
    "<div class='object-label'>Object 1:</div>"
    "<div class='distance-bar-container'>"
    "<div class='distance-bar' id='distance-bar-1'></div>"
    "</div>"
    "<div class='object-info' id='object-info-1'>---</div>"
    "</div>"
    "<div class='tof-object-row' id='tof-object-2'>"
    "<div class='object-label'>Object 2:</div>"
    "<div class='distance-bar-container'>"
    "<div class='distance-bar' id='distance-bar-2'></div>"
    "</div>"
    "<div class='object-info' id='object-info-2'>---</div>"
    "</div>"
    "<div class='tof-object-row' id='tof-object-3'>"
    "<div class='object-label'>Object 3:</div>"
    "<div class='distance-bar-container'>"
    "<div class='distance-bar' id='distance-bar-3'></div>"
    "</div>"
    "<div class='object-info' id='object-info-3'>---</div>"
    "</div>"
    "<div class='tof-object-row' id='tof-object-4'>"
    "<div class='object-label'>Object 4:</div>"
    "<div class='distance-bar-container'>"
    "<div class='distance-bar' id='distance-bar-4'></div>"
    "</div>"
    "<div class='object-info' id='object-info-4'>---</div>"
    "</div>"
    "</div>"
    "<div id='tof-objects-summary' style='font-size: 0.9em; color: #212529; text-align: center; margin-top: 10px; padding: 8px; background: #e3f2fd; border-radius: 4px; font-weight: 500;'>"
    "Multi-object detection ready..."
    "</div>"
    "</div>"
    "</div>"
    
    // Gamepad Controller
    "<div class='sensor-card'>"
    "<div class='sensor-title'>"
    "<span>Gamepad Controller</span>"
    "<span class='status-indicator status-disabled' id='gamepad-status-indicator'></span>"
    "</div>"
    "<div class='sensor-description'>Game controller input for interactive control and navigation.</div>"
    "<div class='sensor-controls'>"
    "<button class='btn' id='btn-gamepad-read'>Read Gamepad</button>"
    "</div>"
    "<div class='sensor-data' id='gamepad-data'>Gamepad data will appear here...</div>"
    "</div>"
    
    "</div>" // End sensor-grid
    "</div>"; // End sensors-container
    
  // Script A-pre: tiniest early marker
  inner += "<script>try{ (window.__s_markers=window.__s_markers||[]).push('A-pre'); }catch(_){ };</script>";

  // Script A1: markers + global error handler
  inner += "<script>";
  inner += "try{";
  inner += "window.__s_markers=(window.__s_markers||[]);";
  inner += "console.debug('[Sensors][A1] start');";
  inner += "window.__s_markers.push('A1');";
  inner += "window.addEventListener('error', function(e){try{console.error('[Sensors][GlobalError]', e.message, e.filename, e.lineno, e.colno);}catch(_){}});";
  inner += "console.debug('[Sensors][A1] html_len=', document.documentElement.innerHTML.length);";
  inner += "}catch(e){console.error('[Sensors][A1] init error', e);}";
  inner += "</script>";

  // Script A2: minimal fallbacks
  inner += "<script>";
  inner += "try{";
  inner += "window.__s_markers=(window.__s_markers||[]); window.__s_markers.push('A2');";
  inner += "window.__sp_ints = window.__sp_ints || {};";
  inner += "if(typeof window.thermalPollingInterval==='undefined'){ window.thermalPollingInterval=null; }";
  inner += "if(typeof window.thermalPollingMs==='undefined'){ window.thermalPollingMs=200; }";
  inner += "if(typeof window.applyThermalTransition!=='function'){ window.applyThermalTransition=function(_ms){}; }";
  inner += "if(typeof thermalPollingInterval==='undefined'){ var thermalPollingInterval = window.thermalPollingInterval; }";
  inner += "if(typeof thermalPollingMs==='undefined'){ var thermalPollingMs = window.thermalPollingMs; }";
  inner += "window.controlSensor = window.controlSensor || function(sensor, action){ try{ var command = String(sensor||'') + String(action||''); return fetch('/api/cli', {method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'}, body:'cmd='+encodeURIComponent(command)}).then(function(r){return r.text();}).then(function(t){ console.log('[Sensors] control result', t); return t; }).catch(function(e){ console.error('[Sensors] control error', e); throw e; }); }catch(e){ console.warn('[Sensors] controlSensor fallback error', e); return Promise.reject(e); } };";
  inner += "window.readSensor = window.readSensor || function(sensor){ try{ return fetch('/api/cli', {method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'}, body:'cmd='+encodeURIComponent(String(sensor||''))}).then(function(r){return r.text();}).then(function(t){ var el=document.getElementById((String(sensor||'')+'-data')); if(el){ var ts=new Date().toLocaleTimeString(); el.textContent='['+ts+'] '+t; } return t; }).catch(function(e){ console.error('[Sensors] read error', e); throw e; }); }catch(e){ console.warn('[Sensors] readSensor fallback error', e); return Promise.reject(e); } };";
  inner += "window.startSensorPolling = window.startSensorPolling || function(sensor){ try{ if(window.__sp_ints[sensor]) return; if(sensor==='thermal' && typeof startThermalPolling==='function'){ startThermalPolling(); return; } if(sensor==='tof' && typeof startToFPolling==='function'){ startToFPolling(); return; } window.__sp_ints[sensor]=setInterval(function(){ try{ window.readSensor(sensor); }catch(_){ } }, (sensor==='imu')?200:500); }catch(e){ console.warn('[Sensors] startSensorPolling fallback error', e);} };";
  inner += "window.stopSensorPolling = window.stopSensorPolling || function(sensor){ try{ if(window.__sp_ints[sensor]){ clearInterval(window.__sp_ints[sensor]); delete window.__sp_ints[sensor]; } if(sensor==='thermal' && typeof stopThermalPolling==='function'){ stopThermalPolling(); } else if(sensor==='tof' && typeof stopToFPolling==='function'){ stopToFPolling(); } }catch(e){ console.warn('[Sensors] stopSensorPolling fallback error', e);} };";
  inner += "}catch(e){console.error('[Sensors][A2] init error', e);}";
  inner += "</script>";

  // Script B1a-1: Core helpers (control/read)
  inner += "<script>";
  inner += "try{";
  inner += "window.__s_markers=(window.__s_markers||[]);";
  inner += "var sensorIntervals = {};";
  inner += "function controlSensor(sensor, action){ var command = sensor + action; return fetch('/api/cli', {method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'}, body:'cmd=' + encodeURIComponent(command)}).then(function(response){return response.text();}).then(function(result){ console.log('Sensor control result:', result); return result; }).catch(function(error){ console.error('Sensor control error:', error); throw error; }); }";
  inner += "function readSensor(sensor){ return fetch('/api/cli', {method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'}, body:'cmd=' + encodeURIComponent(sensor)}).then(function(response){return response.text();}).then(function(result){ var dataElement = document.getElementById(getSensorDataId(sensor)); if(dataElement){ var timestamp = new Date().toLocaleTimeString(); dataElement.textContent='['+timestamp+'] '+ result; } return result; }).catch(function(error){ console.error('Sensor read error:', error); throw error; }); }";
  inner += "console.debug('[Sensors][B1a-1] loaded'); window.__s_markers.push('B1a-1');";
  inner += "}catch(e){console.error('[Sensors][B1a-1] init error', e);}";
  inner += "</script>";

  // Script B1a-2: Core helpers (ids + polling helpers)
  inner += "<script>";
  inner += "try{";
  inner += "window.__s_markers=(window.__s_markers||[]);";
  inner += "function getSensorDataId(sensor){ if(sensor.indexOf('imu')!==-1) return 'gyro-data'; if(sensor.indexOf('tof')!==-1) return 'tof-data'; if(sensor.indexOf('apds')!==-1) return 'apds-data'; if(sensor.indexOf('thermal')!==-1) return 'thermal-data'; if(sensor.indexOf('gamepad')!==-1) return 'gamepad-data'; return sensor + '-data'; }";
  inner += "function getSensorStatusId(sensor){ if(sensor.indexOf('imu')!==-1) return 'gyro-status-indicator'; if(sensor.indexOf('tof')!==-1) return 'tof-status-indicator'; if(sensor.indexOf('apds')!==-1) return 'apds-status-indicator'; if(sensor.indexOf('thermal')!==-1) return 'thermal-status-indicator'; if(sensor.indexOf('gamepad')!==-1) return 'gamepad-status-indicator'; return sensor + '-status-indicator'; }";
  inner += "function updateSensorStatus(sensor, action, result){ var statusElement = document.getElementById(getSensorStatusId(sensor)); if(statusElement){ if(action==='start' && result.indexOf('ERROR')===-1){ statusElement.className='status-indicator status-enabled'; } else if(action==='stop'){ statusElement.className='status-indicator status-disabled'; } } }";
  inner += "function startSensorPolling(sensor){ if(sensorIntervals[sensor]) return; if(sensor==='thermal'){ if(typeof startThermalPolling==='function'){ startThermalPolling(); } return; } else if(sensor==='tof'){ if(typeof startToFPolling==='function'){ startToFPolling(); } return; } else { readSensor(sensor); sensorIntervals[sensor] = setInterval(function(){ readSensor(sensor); }, sensor==='imu'?200:500); } }";
  inner += "function stopSensorPolling(sensor){ if(sensorIntervals[sensor]){ clearInterval(sensorIntervals[sensor]); delete sensorIntervals[sensor]; } if(sensor==='thermal'){ if(typeof stopThermalPolling==='function'){ stopThermalPolling(); } } else if(sensor==='tof'){ if(typeof stopToFPolling==='function'){ stopToFPolling(); } } }";
  inner += "console.debug('[Sensors][B1a-2] loaded'); window.__s_markers.push('B1a-2');";
  inner += "}catch(e){console.error('[Sensors][B1a-2] init error', e);}";
  inner += "</script>";

  // B1b-pre: tiny prelude marker
  inner += "<script>try{ (window.__s_markers=window.__s_markers||[]).push('B1b-pre'); }catch(_){ };</script>";

  // B1b-h1: Thermal vars (tiny)
  inner += "<script>";
  inner += "try{ window.__s_markers=(window.__s_markers||[]); window.__s_markers.push('B1b-h1');";
  inner += "window.thermalPollingInterval = (typeof window.thermalPollingInterval==='undefined')?null:window.thermalPollingInterval;";
  inner += "window.thermalPollingMs = (typeof window.thermalPollingMs==='undefined')?200:window.thermalPollingMs;";
  inner += "if(typeof thermalPollingInterval==='undefined'){ var thermalPollingInterval = window.thermalPollingInterval; }";
  inner += "if(typeof thermalPollingMs==='undefined'){ var thermalPollingMs = window.thermalPollingMs; }";
  inner += "window.gThermalMinTemp = (typeof window.gThermalMinTemp==='undefined')?20.0:window.gThermalMinTemp;";
  inner += "window.gThermalMaxTemp = (typeof window.gThermalMaxTemp==='undefined')?30.0:window.gThermalMaxTemp;";
  inner += "window.thermalSmoothedMin = (typeof window.thermalSmoothedMin==='undefined')?null:window.thermalSmoothedMin;";
  inner += "window.thermalSmoothedMax = (typeof window.thermalSmoothedMax==='undefined')?null:window.thermalSmoothedMax;";
  inner += "window.thermalPalette = (typeof window.thermalPalette==='undefined')?'turbo':window.thermalPalette;";
  inner += "window.thermalEwmaAlpha = (typeof window.thermalEwmaAlpha==='undefined')?0.2:window.thermalEwmaAlpha;";
  inner += "}catch(e){ console.warn('[Sensors][B1b-h1] init error', e); }";
  inner += "</script>";

  // B1b-h2: Thermal transition (tiny)
  inner += "<script>";
  inner += "try{ window.__s_markers=(window.__s_markers||[]); window.__s_markers.push('B1b-h2');";
  inner += "function applyThermalTransition(ms){ try{ var grid=document.getElementById('thermalGrid'); if(grid && grid.children.length===768){ var s=(ms/1000).toFixed(3)+'s'; for(var i=0;i<768;i++){ grid.children[i].style.transition='background-color '+s+' linear'; } } }catch(e){console.warn('[Sensors] applyThermalTransition', e);} }";
  inner += "}catch(e){ console.warn('[Sensors][B1b-h2] init error', e); }";
  inner += "</script>";

  // B1b-h3: Thermal palette function (tiny)
  inner += "<script>";
  inner += "try{ window.__s_markers=(window.__s_markers||[]); window.__s_markers.push('B1b-h3');";
  inner += "function tempToColor(tn){ tn=Math.max(0,Math.min(1,Number(tn)||0)); var p=window.thermalPalette||'turbo'; if(p==='grayscale'){ var gray=Math.round(255*tn); return 'rgb('+gray+','+gray+','+gray+')'; } if(p==='rainbow'){ var r=tn<0.5?Math.round(255*(1-2*tn)):Math.round(255*(2*tn-1)); var g=tn<0.5?Math.round(255*(2*tn)):Math.round(255*(2-2*tn)); var b=tn<0.5?0:Math.round(255*(2*tn-1)); return 'rgb('+r+','+g+','+b+')'; } var r2=Math.round(255*tn), g2=Math.round(255*(1-Math.abs(2*tn-1))), b2=Math.round(255*(1-tn)); return 'rgb('+r2+','+g2+','+b2+')'; }";
  inner += "function changeThermalPalette(palette){ window.thermalPalette = palette; }";
  inner += "}catch(e){ console.warn('[Sensors][B1b-h3] init error', e); }";
  inner += "</script>";

  // B1b-p1: Thermal fetch + store (very small)
  inner += "<script>";
  inner += "try{";
  inner += "window.__s_markers=(window.__s_markers||[]); window.__s_markers.push('B1b-p1');";
  inner += "if(typeof window.__thermalSeeded==='undefined'){ window.__thermalSeeded=false; }";
  inner += "if(typeof window.__thermalFetching==='undefined'){ window.__thermalFetching=false; }";
  inner += "if(typeof window.__thermalData==='undefined'){ window.__thermalData=null; }";
  inner += "function __fetchThermal(){ if(window.__thermalFetching){ return Promise.resolve(false); } window.__thermalFetching=true; var _uT='/api/sensors?sensor=thermal&ts='+Date.now(); return fetch(_uT,{cache:'no-store'}).then(function(r){return r.json();}).then(function(d){ window.__thermalData=d||null; return true; }).catch(function(_e){ return false; }).finally(function(){ window.__thermalFetching=false; }); }";
  inner += "}catch(e){ console.warn('[Sensors][B1b-p1] init error', e); }";
  inner += "</script>";

  // B1b-p2: Thermal render + interval (very small)
  inner += "<script>";
  inner += "try{";
  inner += "window.__s_markers=(window.__s_markers||[]); window.__s_markers.push('B1b-p2');";
  inner += "if(typeof window.tempToColor!=='function'){ window.tempToColor=function(tn){ tn=Math.max(0,Math.min(1,Number(tn)||0)); var gray=Math.round(255*tn); return 'rgb('+gray+','+gray+','+gray+')'; }; }";
  inner += "function __renderThermal(){ var data=window.__thermalData; var grid=document.getElementById('thermalGrid'); if(!grid){ return; } if(grid.children.length!==768){ for(var i=0;i<768;i++){ var c=document.createElement('div'); c.className='thermal-pixel'; c.style.backgroundColor='rgb(128,128,128)'; grid.appendChild(c);} } if(!data){ return; } var isValid=(data && (data.valid===true || data.v===1 || data.v===true)); var frame=(data && (data.frame||data.f))||null; if(!isValid){ var perf=document.getElementById('thermal-performance'); if(perf){ perf.textContent='No valid thermal data'; } for(var k=0;k<768;k++){ grid.children[k].style.backgroundColor='rgb(128,128,128)'; } return; } if(!frame || frame.length!==768){ var perf2=document.getElementById('thermal-performance'); if(perf2){ perf2.textContent='Invalid frame ('+(frame?frame.length:'null')+')'; } return; } var minT=(data.minTemp!==undefined?data.minTemp:(data.mn!==undefined?data.mn:null)); var maxT=(data.maxTemp!==undefined?data.maxTemp:(data.mx!==undefined?data.mx:null)); var avgProvided=(data.avgTemp!==undefined?data.avgTemp:(data.av!==undefined?data.av:null)); if(minT===null || maxT===null){ var tMin=frame[0], tMax=frame[0], sum=0; for(var i=0;i<768;i++){ var v=frame[i]; if(v<tMin) tMin=v; if(v>tMax) tMax=v; sum+=v; } if(minT===null) minT=tMin; if(maxT===null) maxT=tMax; var avg=sum/768; var avgEl=document.getElementById('thermalAvg'); if(avgEl){ avgEl.textContent=(Number(avg)||0).toFixed(1); } } else if(avgProvided!==null){ var avgEl2=document.getElementById('thermalAvg'); if(avgEl2){ avgEl2.textContent=Number(avgProvided).toFixed(1); } } if(minT===maxT){ minT=Number(minT)-0.1; maxT=Number(maxT)+0.1; } if(!window.__thermalSeeded){ window.gThermalMinTemp=Number(minT); window.gThermalMaxTemp=Number(maxT); window.__thermalSeeded=true; } var range=(window.gThermalMaxTemp-window.gThermalMinTemp)||1; var hottestVal=-Infinity, hottestIdx=-1; for(var j=0;j<768;j++){ var t=frame[j]; var tn=(t-window.gThermalMinTemp)/range; grid.children[j].style.backgroundColor=window.tempToColor(tn); if(t>hottestVal){ hottestVal=t; hottestIdx=j; } } if(typeof window.thermalEwmaAlpha==='number'){ window.gThermalMinTemp=(1-window.thermalEwmaAlpha)*window.gThermalMinTemp+window.thermalEwmaAlpha*Number(minT); window.gThermalMaxTemp=(1-window.thermalEwmaAlpha)*window.gThermalMaxTemp+window.thermalEwmaAlpha*Number(maxT); } var miEl=document.getElementById('thermalMin'); var mxEl=document.getElementById('thermalMax'); if(miEl){ miEl.textContent=Number(minT).toFixed(1); } if(mxEl){ mxEl.textContent=Number(maxT).toFixed(1); } var perf3=document.getElementById('thermal-performance'); if(perf3 && hottestIdx>=0){ var x=hottestIdx%32, y=Math.floor(hottestIdx/32); perf3.textContent='Capture: --ms, Hottest: ['+x+','+y+'] = '+Number(hottestVal).toFixed(1)+'°C'; } }";
  inner += "function updateThermalVisualization(){ return __fetchThermal().then(function(){ try{ __renderThermal(); }catch(_){ } }); }";
  inner += "function startThermalPolling(){ if(window.thermalPollingInterval) return; try{ updateThermalVisualization(); }catch(_e){} window.thermalPollingInterval=setInterval(function(){ updateThermalVisualization(); }, window.thermalPollingMs); }";
  inner += "function stopThermalPolling(){ if(window.thermalPollingInterval){ clearInterval(window.thermalPollingInterval); window.thermalPollingInterval=null; } }";
  inner += "}catch(e){ console.warn('[Sensors][B1b-p2] init error', e); }";
  inner += "</script>";

  // B1b-fix1: Patch tn calculation to avoid all-blue grid when original line truncates variable name
  inner += "<script>";
  inner += "try{ window.__s_markers=(window.__s_markers||[]); window.__s_markers.push('B1b-fix1');";
  inner += "(function(){ if(typeof tempToColor!=='function'){ window.tempToColor=function(tn){ tn=Math.max(0,Math.min(1,Number(tn)||0)); var gray=Math.round(255*tn); return 'rgb('+gray+','+gray+','+gray+')'; }; }";
  inner += "if(typeof __renderThermal!=='function'){ return; }";
  inner += "var __rt = __renderThermal; __renderThermal = function(){ try{ var data=window.__thermalData; var grid=document.getElementById('thermalGrid'); if(!grid){ return; } if(!data){ return; } var frame=(data && (data.frame||data.f))||null; if(!frame || frame.length!==768){ return; } var min=Number(window.gThermalMinTemp); var max=Number(window.gThermalMaxTemp); if(!(min<=max)){ min=frame[0]; max=frame[0]; for(var i=1;i<768;i++){ var v=frame[i]; if(v<min) min=v; if(v>max) max=v; } } var range=(max-min)||1; var hottestVal=-Infinity, hottestIdx=-1; for(var j=0;j<768;j++){ var t=Number(frame[j]); var tn=(t-min)/range; tn = tn<0?0:(tn>1?1:tn); var px=grid.children[j]; if(px){ px.style.backgroundColor = tempToColor(tn); } if(t>hottestVal){ hottestVal=t; hottestIdx=j; } } var perf=document.getElementById('thermal-performance'); if(perf && isFinite(hottestVal) && hottestIdx>=0){ var r=(hottestIdx/32)|0, c=(hottestIdx%32); perf.textContent='Capture: --ms, Hottest: ['+r+','+c+'] = '+hottestVal.toFixed(1)+'°C'; } }catch(_){ /* swallow */ } }; })();";
  inner += "}catch(e){ console.warn('[Sensors][B1b-fix1] init error', e); }";
  inner += "</script>";

  // Script B2a: ToF state + start/stop + transitions (small)
  inner += "<script>";
  inner += "try{";
  inner += "window.__s_markers=(window.__s_markers||[]); window.__s_markers.push('B2a');";
  inner += "var tofPollingInterval = null;";
  inner += "var tofObjectStates = [{}, {}, {}, {}];";
  inner += "var tofStabilityThreshold = 3;";
  inner += "var tofMaxDistance = 3400;";
  inner += "var tofPollingMs = 300;";
  inner += "function applyToFTransition(ms){ try{ var s=(ms/1000).toFixed(3)+'s'; for(var j=1;j<=4;j++){ var be=document.getElementById('distance-bar-'+j); if(be){ be.style.transition='width '+s+' ease'; } } }catch(e){ console.warn('[Sensors] applyToFTransition', e);} }";
  inner += "function startToFPolling(){ if(tofPollingInterval) return; var d=document.getElementById('tof-objects-display'); if(d){ d.style.display='block'; } if(typeof updateToFObjects==='function'){ updateToFObjects(); } tofPollingInterval = setInterval(function(){ if(typeof updateToFObjects==='function'){ updateToFObjects(); } }, tofPollingMs); }";
  inner += "function stopToFPolling(){ if(tofPollingInterval){ clearInterval(tofPollingInterval); tofPollingInterval=null; } var d=document.getElementById('tof-objects-display'); if(d){ d.style.display='none'; } }";
  inner += "}catch(e){ console.warn('[Sensors][B2a] init error', e); }";
  inner += "</script>";

  // Script B2b: ToF update implementation (small)
  inner += "<script>";
  inner += "try{";
  inner += "window.__s_markers=(window.__s_markers||[]); window.__s_markers.push('B2b');";
  inner += "function updateToFObjects(){";
  inner += "  var _uF='/api/sensors?sensor=tof&ts=' + Date.now();";
  inner += "  fetch(_uF, { cache: 'no-store' })";
  inner += "  .then(function(response){return response.json();})";
  inner += "  .then(function(data){";
  inner += "    if(data && data.objects){";
  inner += "      var validObjects=0;";
  inner += "      for(var i=0;i<4;i++){";
  inner += "        var obj=data.objects[i];";
  inner += "        var barElement=document.getElementById('distance-bar-'+(i+1));";
  inner += "        var infoElement=document.getElementById('object-info-'+(i+1));";
  inner += "        var state=tofObjectStates[i];";
  inner += "        if(obj && obj.detected && obj.valid){";
  inner += "          var distance_mm=obj.distance_mm||0;";
  inner += "          var distance_cm=obj.distance_cm||0;";
  inner += "          if(!state.lastDistance || Math.abs(state.lastDistance-distance_mm) < 100){";
  inner += "            state.stableCount=(state.stableCount||0)+1;";
  inner += "            state.lastDistance=distance_mm;";
  inner += "            if(state.stableCount>=tofStabilityThreshold){";
  inner += "              validObjects++;";
  inner += "              var percentage=Math.min(100,(distance_mm/tofMaxDistance)*100);";
  inner += "              barElement.style.width=percentage+'%';";
  inner += "              barElement.className='distance-bar';";
  inner += "              infoElement.textContent=distance_cm.toFixed(1)+' cm';";
  inner += "              state.displayed=true;";
  inner += "            }";
  inner += "          } else {";
  inner += "            state.stableCount=1;";
  inner += "            state.lastDistance=distance_mm;";
  inner += "          }";
  inner += "        } else {";
  inner += "          state.stableCount=0;";
  inner += "          if(state.displayed){";
  inner += "            state.missCount=(state.missCount||0)+1;";
  inner += "            if(state.missCount>=tofStabilityThreshold){";
  inner += "              barElement.style.width='0%';";
  inner += "              barElement.className='distance-bar invalid';";
  inner += "              infoElement.textContent='---';";
  inner += "              state.displayed=false;";
  inner += "              state.missCount=0;";
  inner += "            }";
  inner += "          } else {";
  inner += "            barElement.style.width='0%';";
  inner += "            barElement.className='distance-bar invalid';";
  inner += "            infoElement.textContent='---';";
  inner += "          }";
  inner += "        }";
  inner += "      }";
  inner += "      var summary=document.getElementById('tof-objects-summary');";
  inner += "      if(summary){ summary.textContent=validObjects+' object(s) detected of '+(data.total_objects||0)+' total'; }";
  inner += "    } else if(data && data.error){";
  inner += "      var summary2=document.getElementById('tof-objects-summary');";
  inner += "      if(summary2){ summary2.textContent='Error: '+data.error; }";
  inner += "    }";
  inner += "  })";
  inner += "  .catch(function(error){";
  inner += "    var summary3=document.getElementById('tof-objects-summary');";
  inner += "    if(summary3){ summary3.textContent='Connection error'; }";
  inner += "    for(var j=1;j<=4;j++){ var be=document.getElementById('distance-bar-'+j); var ie=document.getElementById('object-info-'+j); if(be){ be.style.width='0%'; be.className='distance-bar invalid'; } if(ie){ ie.textContent='---'; } }";
  inner += "  });";
  inner += "}";
  inner += "}catch(e){console.error('[Sensors][B2b] init error', e);}";
  inner += "</script>";

  // Script C: Bind events and initialize DOM pieces
  inner += "<script>";
  inner += "document.addEventListener('DOMContentLoaded', function(){";
  inner += "  try {";
  inner += "    console.debug('[Sensors][C] DOMContentLoaded');";
  inner += "    var bind = function(id, fn){ var el=document.getElementById(id); if(el){ el.addEventListener('click', fn); } else { console.warn('[Sensors] missing element', id); } };";
  inner += "    bind('btn-imu-start', function(){ controlSensor('imu','start'); });";
  inner += "    bind('btn-imu-stop', function(){ controlSensor('imu','stop'); });";
  inner += "    bind('btn-imu-read', function(){ readSensor('imu'); });";
  inner += "    bind('btn-apdscolor-start', function(){ controlSensor('apdscolor','start'); });";
  inner += "    bind('btn-apdscolor-stop', function(){ controlSensor('apdscolor','stop'); });";
  inner += "    bind('btn-apdscolor-read', function(){ readSensor('apdscolor'); });";
  inner += "    bind('btn-apdsproximity-start', function(){ controlSensor('apdsproximity','start'); });";
  inner += "    bind('btn-apdsproximity-stop', function(){ controlSensor('apdsproximity','stop'); });";
  inner += "    bind('btn-apdsproximity-read', function(){ readSensor('apdsproximity'); });";
  inner += "    bind('btn-apdsgesture-start', function(){ controlSensor('apdsgesture','start'); });";
  inner += "    bind('btn-apdsgesture-stop', function(){ controlSensor('apdsgesture','stop'); });";
  inner += "    bind('btn-apdsgesture-read', function(){ readSensor('apdsgesture'); });";
  inner += "    bind('btn-thermal-start', function(){ controlSensor('thermal','start').then(function(){ try{ setIndicator('thermal-status-indicator', true); window.__lastSensorStatus = Object.assign({}, window.__lastSensorStatus||{}, { thermalEnabled: true, thermal: true }); }catch(_){ } try{ if(typeof startSensorPolling==='function'){ startSensorPolling('thermal'); } }catch(_){ } try{ fetchInitialStatus(); }catch(_){ } }).catch(function(e){ console.warn('[Sensors] thermal start chain error', e); }); });";
  inner += "    bind('btn-thermal-stop', function(){ controlSensor('thermal','stop').then(function(){ try{ setIndicator('thermal-status-indicator', false); window.__lastSensorStatus = Object.assign({}, window.__lastSensorStatus||{}, { thermalEnabled: false, thermal: false }); }catch(_){ } try{ if(typeof stopSensorPolling==='function'){ stopSensorPolling('thermal'); } }catch(_){ } try{ fetchInitialStatus(); }catch(_){ } }).catch(function(e){ console.warn('[Sensors] thermal stop chain error', e); }); });";
  inner += "    var paletteSel = document.getElementById('thermal-palette-select'); if(paletteSel){ paletteSel.addEventListener('change', function(){ changeThermalPalette(paletteSel.value); }); }";
  inner += "    bind('btn-tof-start', function(){ controlSensor('tof','start').then(function(){ try{ if(typeof startSensorPolling==='function'){ startSensorPolling('tof'); } }catch(_){ } try{ fetchInitialStatus(); }catch(_){ } }).catch(function(e){ console.warn('[Sensors] tof start chain error', e); }); });";
  inner += "    bind('btn-tof-stop', function(){ controlSensor('tof','stop').then(function(){ try{ if(typeof stopSensorPolling==='function'){ stopSensorPolling('tof'); } }catch(_){ } try{ fetchInitialStatus(); }catch(_){ } }).catch(function(e){ console.warn('[Sensors] tof stop chain error', e); }); });";
  inner += "    bind('btn-gamepad-read', function(){ readSensor('gamepad'); });";
  inner += "    var grid = document.getElementById('thermalGrid'); if(grid && grid.children.length!==768){ for(var i=0;i<768;i++){ var cell=document.createElement('div'); cell.className='thermal-pixel'; cell.style.backgroundColor='rgb(128,128,128)'; grid.appendChild(cell);} }";
  inner += "    console.debug('[Sensors][C] init complete');";
  inner += "    try{ window.__s_markers.push('C'); }catch(_){ }";
  inner += "  } catch(e){ console.error('[Sensors][C] init error', e); }";
  inner += "});";
  inner += "</script>";

  // Script D: Fetch and apply settings (isolated)
  inner += "<script>";
  inner += "try{";
  inner += "window.__s_markers=(window.__s_markers||[]);";
  inner += "function __applySensorsSettingsFrom(s,paletteSel){ try {";
  inner += "  if(typeof s.thermalPollingMs==='number'){ thermalPollingMs = Math.max(50, Math.min(5000, s.thermalPollingMs)); }";
  inner += "  if(typeof s.tofPollingMs==='number'){ tofPollingMs = Math.max(50, Math.min(5000, s.tofPollingMs)); }";
  inner += "  if(typeof s.tofStabilityThreshold==='number'){ tofStabilityThreshold = Math.max(1, Math.min(20, s.tofStabilityThreshold)); }";
  inner += "  if(typeof s.thermalPaletteDefault==='string'){ window.thermalPalette = s.thermalPaletteDefault; if(paletteSel){ paletteSel.value = s.thermalPaletteDefault; } }";
  inner += "  if(typeof s.thermalEWMAFactor==='number'){ thermalEwmaAlpha = Math.max(0, Math.min(1, s.thermalEWMAFactor)); }";
  inner += "  if(typeof s.thermalTransitionMs==='number' && typeof applyThermalTransition==='function'){ applyThermalTransition(Math.max(0, Math.min(1000, s.thermalTransitionMs))); }";
  inner += "  if(typeof s.tofTransitionMs==='number' && typeof applyToFTransition==='function'){ applyToFTransition(Math.max(0, Math.min(1000, s.tofTransitionMs))); }";
  inner += "  if(typeof s.tofUiMaxDistanceMm==='number'){ tofMaxDistance = Math.max(100, Math.min(12000, s.tofUiMaxDistanceMm)); var rng=document.getElementById('tof-range-mm'); if(rng){ rng.textContent = String(tofMaxDistance); } }";
  inner += "} catch(e){ console.warn('[Sensors] apply settings error', e); } }";
  inner += "function __fetchAndApplySensorsSettings(){ var paletteSel = document.getElementById('thermal-palette-select'); fetch('/api/settings').then(function(r){return r.json();}).then(function(cfg){ var s = (cfg && cfg.settings) ? cfg.settings : {}; __applySensorsSettingsFrom(s, paletteSel); }).catch(function(e){ console.warn('[Sensors] settings fetch error', e); }); }";
  inner += "if(document.readyState==='loading'){ document.addEventListener('DOMContentLoaded', __fetchAndApplySensorsSettings); } else { __fetchAndApplySensorsSettings(); }";
  inner += "console.debug('[Sensors][D] settings apply scheduled'); try{ window.__s_markers.push('D'); }catch(_){ }";
  inner += "}catch(e){console.error('[Sensors][D] init error', e);}";
  inner += "</script>";

  // Script E1: Sensor status helpers/state
  inner += "<script>";
  inner += "try{";
  inner += "window.__s_markers=(window.__s_markers||[]); window.__s_markers.push('E1');";
  inner += "function isSensorsPageActive(){ try{ return (location.pathname==='/sensors' || location.pathname.indexOf('/sensors')===0) && !document.hidden; }catch(_){ return true; } }";
  inner += "function setIndicator(id, enabled){ var el=document.getElementById(id); if(!el) return; el.className = enabled ? 'status-indicator status-enabled' : 'status-indicator status-disabled'; }";
  inner += "function applySensorStatus(s){ if(!s) return; window.__lastSensorStatus = s; setIndicator('thermal-status-indicator', !!(s.thermalEnabled||s.thermal)); setIndicator('tof-status-indicator', !!(s.tofEnabled||s.tof)); var apdsOn = !!(s.apdsColorEnabled||s.apdsProximityEnabled||s.apdsGestureEnabled); setIndicator('apds-status-indicator', apdsOn); setIndicator('gyro-status-indicator', !!(s.imuEnabled||s.imu)); setIndicator('gamepad-status-indicator', !!(s.gamepadEnabled||s.gamepad)); if(isSensorsPageActive()){ if(typeof startSensorPolling==='function' && typeof stopSensorPolling==='function'){ if(s.thermalEnabled||s.thermal){ startSensorPolling('thermal'); } else { stopSensorPolling('thermal'); } if(s.tofEnabled||s.tof){ startSensorPolling('tof'); } else { stopSensorPolling('tof'); } if(s.imuEnabled||s.imu){ startSensorPolling('imu'); } else { stopSensorPolling('imu'); } if(s.apdsColorEnabled){ startSensorPolling('apdscolor'); } else { stopSensorPolling('apdscolor'); } if(s.apdsProximityEnabled){ startSensorPolling('apdsproximity'); } else { stopSensorPolling('apdsproximity'); } if(s.apdsGestureEnabled){ startSensorPolling('apdsgesture'); } else { stopSensorPolling('apdsgesture'); } if(s.gamepadEnabled||s.gamepad){ startSensorPolling('gamepad'); } else { stopSensorPolling('gamepad'); } } } }";
  inner += "function fetchInitialStatus(){ return fetch('/api/sensors/status', { credentials: 'include' }).then(function(r){ return r.json(); }).then(function(j){ applySensorStatus(j); window.__sensorStatusSeq = j.seq || 0; }).catch(function(e){ console.warn('[Sensors] status fetch failed', e); }); }";
  inner += "function attachSSE(){ try{ if(!window.__es) return false; if(window.__sensorStatusAttached) return true; var handler=function(e){ try{ var d=JSON.parse(e.data||'{}'); if(!d) return; if(window.__sensorStatusSeq && d.seq && d.seq<=window.__sensorStatusSeq) return; window.__sensorStatusSeq=d.seq||((window.__sensorStatusSeq||0)+1); applySensorStatus(d); }catch(err){ console.warn('[Sensors] SSE parse', err);} }; window.__es.addEventListener('sensor-status', handler); window.__sensorStatusAttached=true; window.addEventListener('beforeunload', function(){ try{ if(window.__es){ window.__es.removeEventListener('sensor-status', handler); } }catch(_){ } }); return true; }catch(_){ return false; } }";
  inner += "function ensureSSE(){ if(attachSSE()) return; var attempts=0; var t=setInterval(function(){ attempts++; if(attachSSE()||attempts>40){ clearInterval(t); } }, 250); }";
  inner += "console.debug('[Sensors][E1] helpers ready');";
  inner += "}catch(e){ console.warn('[Sensors][E1] init error', e); }";
  inner += "</script>";

  // Script E2: Attach + visibility + kickoff
  inner += "<script>";
  inner += "try{";
  inner += "window.__s_markers=(window.__s_markers||[]); window.__s_markers.push('E2');";
  inner += "function handleVisibility(){ if(!isSensorsPageActive()){ if(typeof stopSensorPolling==='function'){ stopSensorPolling('thermal'); stopSensorPolling('tof'); stopSensorPolling('imu'); stopSensorPolling('apdscolor'); stopSensorPolling('apdsproximity'); stopSensorPolling('apdsgesture'); stopSensorPolling('gamepad'); } } else { if(window.__lastSensorStatus){ applySensorStatus(window.__lastSensorStatus); } else { try{ fetchInitialStatus(); }catch(_){ } } } }";
  inner += "document.addEventListener('visibilitychange', handleVisibility);";
  inner += "if(document.readyState==='loading'){ document.addEventListener('DOMContentLoaded', function(){ fetchInitialStatus().then(ensureSSE); }); } else { fetchInitialStatus().then(ensureSSE); }";
  inner += "console.debug('[Sensors][E2] attached');";
  inner += "}catch(e){ console.warn('[Sensors][E2] init error', e); }";
  inner += "</script>";

  // Final EOF marker script (debugging delivery completeness)
  inner += "<script>try{ window.__s_markers=(window.__s_markers||[]); window.__s_markers.push('EOF'); console.debug('[Sensors][EOF]'); }catch(_){ }</script>";
  
  return inner;
}

String getSensorsPage(const String& username) {
  return htmlShellWithNav(username, "sensors", getSensorsContent());
}

#endif // WEB_SENSORS_H
