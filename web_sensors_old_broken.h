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
    "#gyro-data { color: #111; }"
    ".imu-grid { display: grid; grid-template-columns: 160px 1fr; column-gap: 8px; row-gap: 6px; align-items: baseline; }"
    ".imu-label { color: #333; font-weight: 600; font-family: system-ui, -apple-system, Segoe UI, Roboto, Helvetica, Arial, sans-serif; }"
    ".imu-val { color: #111; font-family: 'Courier New', monospace; }"
    ".status-indicator { display: inline-block; width: 12px; height: 12px; border-radius: 50%; margin-right: 8px; }"
    ".status-enabled { background: #28a745; animation: pulse 2s infinite; }"
    ".status-disabled { background: #dc3545; }"
    "@keyframes pulse { 0% { opacity: 1; } 50% { opacity: 0.5; } 100% { opacity: 1; } }"
    ".thermal-pixel { width: 100%; height: 100%; background-clip: padding-box; border-radius: 0; transition: background-color 0.15s ease-out; }"
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
    "<div id='sensors-loading' style='text-align:center;padding:3rem;color:#666'>"
    "<div style='font-size:1.2rem;margin-bottom:0.5rem'>Loading sensors...</div>"
    "<div style='font-size:0.9rem;opacity:0.7'>Checking available sensors and their status</div>"
    "</div>"
    "<div class='sensor-grid' id='sensors-grid' style='display:none'>"
    "</div>"
    "</div>" 
    "</div>"; 
    
  // Script A-pre: tiniest early marker
  inner += "<script>console.log('A-PRE SCRIPT START'); try{ (window.__s_markers=window.__s_markers||[]).push('A-pre'); }catch(_){ };</script>";

  // Script A1: markers + global error handler (split into parts)
  inner += "<script>";
  inner += "console.log('A1-PART1 START');";
  inner += "try{";
  inner += "console.log('A1-PART2 TRY BLOCK');";
  inner += "window.__s_markers=(window.__s_markers||[]);";
  inner += "console.log('A1-PART3 MARKERS SETUP');";
  inner += "window.__s_markers.push('A1');";
  inner += "console.log('A1-PART4 BEFORE ERROR HANDLER');";
  inner += "}catch(e){console.error('[Sensors][A1] init error', e);}";
  inner += "console.log('A1-PART5 END');";
  inner += "</script>";
  
  // Split the error handler into separate script
  inner += "<script>";
  inner += "console.log('A1-ERROR-HANDLER START');";
  inner += "try{";
  inner += "window.addEventListener('error', function(e){try{console.error('[Sensors][GlobalError]', e.message, e.filename, e.lineno, e.colno);}catch(_){}});";
  inner += "console.log('A1-ERROR-HANDLER COMPLETE');";
  inner += "}catch(e){console.error('[Sensors][A1-ERROR-HANDLER] error', e);}";
  inner += "</script>";

  // Script A2: minimal fallbacks (ultra-granular debug)
  inner += "<script>";
  inner += "</script>";
  
  // Test if basic script works
  inner += "<script>";
  inner += "console.log('A2-TEST1: Basic script works');";
  inner += "</script>";
  
  // Test console.log
  inner += "<script>";
  inner += "console.log('A2-TEST2: Console.log works');";
  inner += "console.log('A2-TEST3: Multiple console.log works');";
  inner += "</script>";
  
  // Test try block
  inner += "<script>";
  inner += "console.log('A2-TEST4: About to test try block');";
  inner += "try{";
  inner += "  console.log('A2-TEST5: Inside try block');";
  inner += "}catch(e){";
  inner += "  console.error('A2-TEST-ERROR:', e);";
  inner += "}";
  inner += "console.log('A2-TEST6: After try block');";
  inner += "</script>";
  
  // Now test if we can add the actual A2 content
  inner += "<script>";
  inner += "console.log('A2-REAL-START: Adding real A2 content');";
  inner += "try{";
  inner += "  console.log('A2-REAL: Setting up markers');";
  inner += "  window.__s_markers=(window.__s_markers||[]); window.__s_markers.push('A2');";
  inner += "  console.log('A2-REAL: Markers complete');";
  inner += "  console.log('A2-REAL: Adding controlSensor function');";
  inner += "  window.controlSensor = function(sensor, action) {";
  inner += "    console.log('[Sensors] controlSensor called:', sensor, action);";
  inner += "    var command = String(sensor||'') + String(action||'');";
  inner += "    console.log('[Sensors] sending command:', command);";
  inner += "    return fetch('/api/cli', {";
  inner += "      method: 'POST',";
  inner += "      headers: {'Content-Type': 'application/x-www-form-urlencoded'},";
  inner += "      body: 'cmd=' + encodeURIComponent(command)";
  inner += "    }).then(function(r){ return r.text(); })";
  inner += "    .then(function(t){";
  inner += "      console.log('[Sensors] control result:', t);";
  inner += "      return t;";
  inner += "    }).catch(function(e){";
  inner += "      console.error('[Sensors] control error:', e);";
  inner += "      throw e;";
  inner += "    });";
  inner += "  };";
  inner += "  console.log('A2-REAL: controlSensor function added');";
  inner += "}catch(e){";
  inner += "  console.error('A2-REAL-ERROR:', e);";
  inner += "}";
  inner += "</script>";

  // Script B1a-1: Core helpers (control/read)
  inner += "<script>";
  inner += "console.log('B1a-1 START: Core helpers');";
  inner += "try{";
  inner += "window.__s_markers=(window.__s_markers||[]);";
  inner += "var sensorIntervals = {};";
  inner += "// controlSensor function now defined in A2 section - removed duplicate";
  inner += "function readSensor(sensor){";
  inner += "  if(String(sensor)==='imu'){";
  inner += "    var url='/api/sensors?sensor=imu&ts=' + Date.now();";
  inner += "    return fetch(url, { cache: 'no-store' })";
  inner += "      .then(function(r){ return r.json(); })";
  inner += "      .then(function(j){";
  inner += "        var el = document.getElementById(getSensorDataId(sensor));";
  inner += "        if(el){";
  inner += "          if(j && j.valid){";
  inner += "            var ax=j.accel.x.toFixed(2), ay=j.accel.y.toFixed(2), az=j.accel.z.toFixed(2);";
  inner += "            var gx=j.gyro.x.toFixed(2), gy=j.gyro.y.toFixed(2), gz=j.gyro.z.toFixed(2);";
  inner += "            var yw=j.ori.yaw.toFixed(1), pt=j.ori.pitch.toFixed(1), rl=j.ori.roll.toFixed(1);";
  inner += "            var tc=Number(j.temp).toFixed(0);";
  inner += "            el.innerHTML = '<div class=\\'imu-grid\\'>' + ";
  inner += "              '<div class=\\'imu-label\\'>Acceleration (m/s&sup2;)</div><div class=\\'imu-val\\'>' + ax + ', ' + ay + ', ' + az + '</div>' + ";
  inner += "              '<div class=\\'imu-label\\'>Gyroscope (rad/s)</div><div class=\\'imu-val\\'>' + gx + ', ' + gy + ', ' + gz + '</div>' + ";
  inner += "              '<div class=\\'imu-label\\'>Orientation YPR (deg)</div><div class=\\'imu-val\\'>' + yw + ', ' + pt + ', ' + rl + '</div>' + ";
  inner += "              '<div class=\\'imu-label\\'>Temperature</div><div class=\\'imu-val\\'>' + tc + '&deg;C</div>' + ";
  inner += "              '</div>';";
  inner += "          } else {";
  inner += "            el.textContent = 'IMU not ready';";
  inner += "          }";
  inner += "        }";
  inner += "        return j;";
  inner += "      })";
  inner += "      .catch(function(e){ console.error('[Sensors] IMU read (JSON) error', e); throw e; });";
  inner += "  }";
  inner += "  return fetch('/api/cli', { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body: 'cmd=' + encodeURIComponent(sensor) })";
  inner += "    .then(function(response){ return response.text(); })";
  inner += "    .then(function(result){ var dataElement = document.getElementById(getSensorDataId(sensor)); if(dataElement){ var timestamp = new Date().toLocaleTimeString(); dataElement.textContent = '[' + timestamp + '] ' + result; } return result; })";
  inner += "    .catch(function(error){ console.error('Sensor read error:', error); throw error; });";
  inner += "}";
  // inner += "console.debug('SNSR - Core helpers loaded');"; 
  inner += "window.__s_markers.push('B1a-1');";
  inner += "}catch(e){console.error('[Sensors][B1a-1] init error', e);}";
  inner += "</script>";

  // Script B1a-2: Core helpers (ids + polling helpers)
  inner += "<script>";
  inner += "console.log('B1a-2 START: Core helpers (ids + polling)');";
  inner += "try{";
  inner += "window.__s_markers=(window.__s_markers||[]);";
  inner += "function getSensorDataId(sensor){ if(sensor.indexOf('imu')!==-1) return 'gyro-data'; if(sensor.indexOf('tof')!==-1) return 'tof-data'; if(sensor.indexOf('apds')!==-1) return 'apds-data'; if(sensor.indexOf('thermal')!==-1) return 'thermal-data'; if(sensor.indexOf('gamepad')!==-1) return 'gamepad-data'; return sensor + '-data'; }";
  inner += "function getSensorStatusId(sensor){ if(sensor.indexOf('imu')!==-1) return 'gyro-status-indicator'; if(sensor.indexOf('tof')!==-1) return 'tof-status-indicator'; if(sensor.indexOf('apds')!==-1) return 'apds-status-indicator'; if(sensor.indexOf('thermal')!==-1) return 'thermal-status-indicator'; if(sensor.indexOf('gamepad')!==-1) return 'gamepad-status-indicator'; return sensor + '-status-indicator'; }";
  inner += "function updateSensorStatus(sensor, action, result){ var statusElement = document.getElementById(getSensorStatusId(sensor)); if(statusElement){ if(action==='start' && result.indexOf('ERROR')===-1){ statusElement.className='status-indicator status-enabled'; } else if(action==='stop'){ statusElement.className='status-indicator status-disabled'; } } }";
  inner += "function startSensorPolling(sensor){ if(sensorIntervals[sensor]) return; if(sensor==='thermal'){ if(typeof startThermalPolling==='function'){ startThermalPolling(); } return; } else if(sensor==='tof'){ if(typeof startToFPolling==='function'){ startToFPolling(); } return; } else { readSensor(sensor); sensorIntervals[sensor] = setInterval(function(){ readSensor(sensor); }, sensor==='imu'?200:500); } }";
  inner += "function stopSensorPolling(sensor){ if(sensorIntervals[sensor]){ clearInterval(sensorIntervals[sensor]); delete sensorIntervals[sensor]; } if(sensor==='thermal'){ if(typeof stopThermalPolling==='function'){ stopThermalPolling(); } } else if(sensor==='tof'){ if(typeof stopToFPolling==='function'){ stopToFPolling(); } } }";
  // inner += "console.debug('SNSR - Core helpers (ids/polling) loaded');"; 
  inner += "window.__s_markers.push('B1a-2');";
  inner += "}catch(e){console.error('[Sensors][B1a-2] init error', e);}";
  inner += "</script>";

  // B1b-pre: tiny prelude marker
  inner += "<script>try{ (window.__s_markers=window.__s_markers||[]).push('B1b-pre'); }catch(_){ };</script>";

  // B1b-h1: Thermal vars (tiny)
  inner += "<script>";
  // inner += "console.debug('SNSR - Thermal vars begin');";
  inner += "try{ window.__s_markers=(window.__s_markers||[]); window.__s_markers.push('B1b-h1');";
  inner += "window.thermalPollingInterval = (typeof window.thermalPollingInterval==='undefined')?null:window.thermalPollingInterval;";
  inner += "window.thermalPollingMs = (typeof window.thermalPollingMs==='undefined')?200:window.thermalPollingMs;";
  inner += "if(typeof thermalPollingInterval==='undefined'){ var thermalPollingInterval = window.thermalPollingInterval; }";
  inner += "if(typeof thermalPollingMs==='undefined'){ var thermalPollingMs = window.thermalPollingMs; }";
  inner += "window.gThermalMinTemp = (typeof window.gThermalMinTemp==='undefined')?20.0:window.gThermalMinTemp;";
  inner += "window.gThermalMaxTemp = (typeof window.gThermalMaxTemp==='undefined')?30.0:window.gThermalMaxTemp;";
  inner += "window.thermalSmoothedMin = (typeof window.thermalSmoothedMin==='undefined')?null:window.thermalSmoothedMin;";
  inner += "window.thermalSmoothedMax = (typeof window.thermalSmoothedMax==='undefined')?null:window.thermalSmoothedMax;";
  inner += "window.thermalPalette = (typeof window.thermalPalette==='undefined')?'grayscale':window.thermalPalette;";
  inner += "window.thermalEwmaAlpha = (typeof window.thermalEwmaAlpha==='undefined')?0.1:window.thermalEwmaAlpha;";
  inner += "window.thermalFpsClampMax = (typeof window.thermalFpsClampMax==='undefined')?20:window.thermalFpsClampMax;";
  inner += "window.thermalInterpolationEnabled = (typeof window.thermalInterpolationEnabled==='undefined')?true:window.thermalInterpolationEnabled;";
  inner += "window.thermalPrevFrame = (typeof window.thermalPrevFrame==='undefined')?null:window.thermalPrevFrame;";
  inner += "window.thermalInterpolationSteps = (typeof window.thermalInterpolationSteps==='undefined')?3:window.thermalInterpolationSteps;";
  inner += "window.thermalInterpolationInterval = (typeof window.thermalInterpolationInterval==='undefined')?null:window.thermalInterpolationInterval;";
  inner += "window.thermalFrameBuffer = (typeof window.thermalFrameBuffer==='undefined')?[]:window.thermalFrameBuffer;";
  inner += "window.thermalBufferSize = (typeof window.thermalBufferSize==='undefined')?3:window.thermalBufferSize;";
  inner += "window.thermalStableMin = (typeof window.thermalStableMin==='undefined')?null:window.thermalStableMin;";
  inner += "window.thermalStableMax = (typeof window.thermalStableMax==='undefined')?null:window.thermalStableMax;";
  inner += "window.thermalRangeSmoothing = (typeof window.thermalRangeSmoothing==='undefined')?0.05:window.thermalRangeSmoothing;";
  inner += "window.thermalOutlierLowPct = (typeof window.thermalOutlierLowPct==='undefined')?0.02:window.thermalOutlierLowPct;";
  inner += "window.thermalOutlierHighPct = (typeof window.thermalOutlierHighPct==='undefined')?0.98:window.thermalOutlierHighPct;";
  inner += "window.thermalMaxRangeStepC = (typeof window.thermalMaxRangeStepC==='undefined')?1.5:window.thermalMaxRangeStepC;";
  inner += "window.thermalMaxJumpFactor = (typeof window.thermalMaxJumpFactor==='undefined')?2.5:window.thermalMaxJumpFactor;";
  inner += "window.thermalWebClientQuality = (typeof window.thermalWebClientQuality==='undefined')?1:window.thermalWebClientQuality;";
  // inner += "console.debug('SNSR - Thermal vars ready');";
  inner += "}catch(e){ console.warn('[Sensors][B1b-h1] init error', e); }";
  inner += "</script>";

  // B1b-h2: Thermal transition (tiny)
  inner += "<script>";
  // inner += "console.debug('SNSR - Thermal transition begin');";
  inner += "try{ window.__s_markers=(window.__s_markers||[]); window.__s_markers.push('B1b-h2');";
  inner += "function applyThermalTransition(ms){ try{ var grid=document.getElementById('thermalGrid'); if(grid && grid.children.length===768){ var s=(ms/1000).toFixed(3)+'s'; for(var i=0;i<768;i++){ grid.children[i].style.transition='background-color '+s+' linear'; } } }catch(e){console.warn('[Sensors] applyThermalTransition', e);} }";
  // inner += "console.debug('SNSR - Thermal transition ready');";
  inner += "}catch(e){ console.warn('[Sensors][B1b-h2] init error', e); }";
  inner += "</script>";

  // B1b-h3: Thermal palette function (tiny)
  inner += "<script>";
  // inner += "console.debug('SNSR - Thermal palette begin');";
  inner += "try{ window.__s_markers=(window.__s_markers||[]); window.__s_markers.push('B1b-h3');";
  inner += "function tempToColor(tn){ tn=Math.max(0,Math.min(1,Number(tn)||0)); var p=window.thermalPalette||'grayscale'; if(p==='grayscale'){ var gamma=(typeof window.thermalGrayGamma==='number')?window.thermalGrayGamma:0.85; var g=Math.round(255*Math.pow(tn,gamma)); return 'rgb('+g+','+g+','+g+')'; } // refined Moreland-style coolwarm: blue->neutral gray->red\nvar cBlue=[59,76,192], cNeutral=[221,221,221], cRed=[180,4,38]; var r,g,b; if(tn<0.5){ var t=tn*2; r=Math.round(cBlue[0]+(cNeutral[0]-cBlue[0])*t); g=Math.round(cBlue[1]+(cNeutral[1]-cBlue[1])*t); b=Math.round(cBlue[2]+(cNeutral[2]-cBlue[2])*t); } else { var t=(tn-0.5)*2; r=Math.round(cNeutral[0]+(cRed[0]-cNeutral[0])*t); g=Math.round(cNeutral[1]+(cRed[1]-cNeutral[1])*t); b=Math.round(cNeutral[2]+(cRed[2]-cNeutral[2])*t); } return 'rgb('+r+','+g+','+b+')'; }";
  inner += "function changeThermalPalette(palette){ window.thermalPalette = palette; }";
  // inner += "console.debug('SNSR - Thermal palette ready');";
  inner += "}catch(e){ console.warn('[Sensors][B1b-h3] init error', e); }";
  inner += "</script>";

  // B1b-p1: Thermal fetch + store (very small)
  inner += "<script>";
  // inner += "console.debug('SNSR - Thermal fetch/store begin');";
  inner += "try{";
  inner += "window.__s_markers=(window.__s_markers||[]); window.__s_markers.push('B1b-p1');";
  inner += "if(typeof window.__thermalSeeded==='undefined'){ window.__thermalSeeded=false; }";
  inner += "if(typeof window.__thermalFetching==='undefined'){ window.__thermalFetching=false; }";
  inner += "if(typeof window.__thermalData==='undefined'){ window.__thermalData=null; }";
  inner += "if(typeof window.__thermalLastCaptureMs==='undefined'){ window.__thermalLastCaptureMs=null; }";
  inner += "if(typeof window.__thermalLastFrameAt==='undefined'){ window.__thermalLastFrameAt=null; }";
  inner += "if(typeof window.__thermalFpsEwma==='undefined'){ window.__thermalFpsEwma=null; }";
  inner += "function __fetchThermal(){ if(window.__thermalFetching){ return Promise.resolve(false); } window.__thermalFetching=true; var _uT='/api/sensors?sensor=thermal&ts='+Date.now(); return fetch(_uT,{cache:'no-store'}).then(function(r){ if(!r.ok){ var errMsg='HTTP '+r.status+' '+r.statusText; if(r.status===401||r.status===403){ errMsg+=' (Authentication/Session expired)'; } else if(r.status===404){ errMsg+=' (Thermal API endpoint not found)'; } else if(r.status>=500){ errMsg+=' (Server error - ESP32 may be overloaded)'; } if(window.logSensorsFrame){ console.warn('[Thermal] Fetch failed: '+errMsg+', URL: '+_uT); } throw new Error(errMsg); } return r.text(); }).then(function(txt){ if(!txt||txt.trim()===''){if(window.logSensorsFrame){ console.warn('[Thermal] Fetch failed: Empty response from server'); } throw new Error('Empty response');} var d=null; try{ d=JSON.parse(txt); }catch(parseErr){ if(window.logSensorsFrame){ console.warn('[Thermal] Fetch failed: Invalid JSON response, first 100 chars: '+txt.slice(0,100)); } throw new Error('JSON parse error: '+parseErr.message); } if(!d){ if(window.logSensorsFrame){ console.warn('[Thermal] Fetch failed: Null data after JSON parse'); } throw new Error('Null data'); } if(d.error){ if(window.logSensorsFrame){ console.warn('[Thermal] Fetch failed: Server returned error: '+d.error); } throw new Error('Server error: '+d.error); } window.__thermalData=d; return true; }).catch(function(err){ var errType='Unknown'; var errDetail=err.message||'No details'; if(err.name==='TypeError'&&errDetail.includes('Failed to fetch')){ errType='Network/CORS'; errDetail='Network unreachable or CORS policy blocking request'; } else if(err.name==='AbortError'){ errType='Request timeout'; errDetail='Request was aborted or timed out'; } else if(errDetail.includes('JSON parse')){ errType='Response format'; } else if(errDetail.includes('HTTP ')){ errType='HTTP error'; } else if(errDetail.includes('Empty response')){ errType='Empty response'; } if(window.logSensorsFrame){ console.warn('[Thermal] Fetch failed ('+errType+'): '+errDetail+', URL: '+_uT); } return false; }).finally(function(){ window.__thermalFetching=false; }); }";
  // inner += "console.debug('SNSR - Thermal fetch/store ready');";
  inner += "}catch(e){ console.warn('[Sensors][B1b-p1] init error', e); }";
  inner += "</script>";

  // B1b-p2: Thermal render + interval (very small)
  inner += "<script>";
  // inner += "console.debug('SNSR - Thermal render/polling begin');";
  inner += "try{";
  inner += "window.__s_markers=(window.__s_markers||[]); window.__s_markers.push('B1b-p2');";
  inner += "if(typeof tempToColor!=='function'){ window.tempToColor=function(tn){ tn=Math.max(0,Math.min(1,Number(tn)||0)); var palette=window.thermalPalette||'grayscale'; if(palette==='coolwarm'){ var r,g,b; if(tn<0.5){ var t=tn*2; r=Math.round(59+(180-59)*t); g=Math.round(76+(180-76)*t); b=Math.round(192+(255-192)*t); }else{ var t=(tn-0.5)*2; r=Math.round(180+(221-180)*t); g=Math.round(180+(130-180)*t); b=Math.round(255+(107-255)*t); } return 'rgb('+r+','+g+','+b+')'; }else{ var g=Math.round(255*tn); return 'rgb('+g+','+g+','+g+')'; } }; }";
  inner += "function __bilinearInterpolate(data, srcW, srcH, dstW, dstH){ var result=new Array(dstW*dstH); var xRatio=(srcW-1)/(dstW-1); var yRatio=(srcH-1)/(dstH-1); for(var y=0;y<dstH;y++){ for(var x=0;x<dstW;x++){ var srcX=x*xRatio; var srcY=y*yRatio; var x1=Math.floor(srcX); var y1=Math.floor(srcY); var x2=Math.min(x1+1,srcW-1); var y2=Math.min(y1+1,srcH-1); var dx=srcX-x1; var dy=srcY-y1; var tl=data[y1*srcW+x1]||0; var tr=data[y1*srcW+x2]||0; var bl=data[y2*srcW+x1]||0; var br=data[y2*srcW+x2]||0; var top=tl+(tr-tl)*dx; var bot=bl+(br-bl)*dx; var val=top+(bot-top)*dy; result[y*dstW+x]=val; } } return result; }";
  inner += "function __computeRobustMinMax(arr){ try{ if(!arr||!arr.length) return {min:0,max:1}; var filtered=[]; for(var i=0;i<arr.length;i++){ var v=Number(arr[i]); if(isFinite(v)) filtered.push(v); } if(filtered.length===0) return {min:0,max:1}; filtered.sort(function(a,b){return a-b;}); var loPct=Math.max(0,Math.min(1, window.thermalOutlierLowPct||0)); var hiPct=Math.max(0,Math.min(1, window.thermalOutlierHighPct||1)); if(hiPct<=loPct){ loPct=0.02; hiPct=0.98; } var loIdx=Math.floor(loPct*(filtered.length-1)); var hiIdx=Math.floor(hiPct*(filtered.length-1)); var lo=filtered[loIdx]; var hi=filtered[hiIdx]; if(!isFinite(lo)||!isFinite(hi)||hi<=lo){ lo=filtered[0]; hi=filtered[filtered.length-1]; if(hi<=lo){ hi=lo+1; } } return {min:lo, max:hi}; }catch(e){ return {min:0, max:1}; } }";
  inner += "function __updateThermalGrid(quality){ var grid=document.getElementById('thermalGrid'); if(!grid) return; var baseW=32; var baseH=24; var newW=baseW*quality; var newH=baseH*quality; var totalPixels=newW*newH; if(window.logSensorsFrame){ console.debug('[Thermal] Grid update: quality='+quality+'x, size='+newW+'×'+newH+', pixels='+totalPixels+', current='+grid.children.length); } if(grid.children.length!==totalPixels){ if(window.logSensorsFrame){ console.debug('[Thermal] Rebuilding grid from '+grid.children.length+' to '+totalPixels+' pixels'); } grid.innerHTML=''; for(var i=0;i<totalPixels;i++){ var c=document.createElement('div'); c.className='thermal-pixel'; c.style.backgroundColor='rgb(128,128,128)'; grid.appendChild(c); } grid.style.gridTemplateColumns='repeat('+newW+', 1fr)'; if(window.logSensorsFrame){ console.debug('[Thermal] Grid rebuilt: '+grid.children.length+' pixels, columns='+newW); } } }";
  inner += "function __renderThermal(){ try{ var data=window.__thermalData; var grid=document.getElementById('thermalGrid'); if(!grid) return; var quality=window.thermalWebClientQuality||1; if(window.logSensorsFrame){ console.debug('[Thermal] Render: quality='+quality+'x, data='+(data?'present':'null')); } __updateThermalGrid(quality); if(!data) return; var frame=(data.frame||data.f)||null; var valid=(data.valid===true||data.v===1||data.v===true); if(!valid||!frame||frame.length!==768){ if(window.logSensorsFrame){ console.debug('[Thermal] Invalid frame: valid='+valid+', frameLen='+(frame?frame.length:'null')); } return; } window.thermalFrameBuffer.push(frame.slice()); if(window.thermalFrameBuffer.length>window.thermalBufferSize){ window.thermalFrameBuffer.shift(); } var smoothedFrame=__smoothThermalFrame(window.thermalFrameBuffer); var baseW=32; var baseH=24; var displayW=baseW*quality; var displayH=baseH*quality; var upscaledFrame=smoothedFrame; if(quality>1){ if(window.logSensorsFrame){ console.debug('[Thermal] Upscaling '+baseW+'×'+baseH+' to '+displayW+'×'+displayH+' ('+quality+'x)'); } upscaledFrame=__bilinearInterpolate(smoothedFrame, baseW, baseH, displayW, displayH); if(window.logSensorsFrame){ console.debug('[Thermal] Upscaled frame: '+smoothedFrame.length+' -> '+upscaledFrame.length+' pixels'); } } var min=Infinity,max=-Infinity,sum=0,maxIdx=-1; for(var j=0;j<upscaledFrame.length;j++){ var v=Number(upscaledFrame[j]); if(v<min)min=v; if(v>max){ max=v; maxIdx=j; } sum+=v; } if(!isFinite(min)||!isFinite(max)||min===max){ min=0; max=1; } if(window.thermalStableMin===null){ window.thermalStableMin=min; window.thermalStableMax=max; } else { var alpha=window.thermalRangeSmoothing||0.05; window.thermalStableMin=alpha*min+(1-alpha)*window.thermalStableMin; window.thermalStableMax=alpha*max+(1-alpha)*window.thermalStableMax; } var stableRange=(window.thermalStableMax-window.thermalStableMin)||1; if(window.thermalInterpolationEnabled && window.thermalPrevUpscaled && window.thermalPrevUpscaled.length===upscaledFrame.length){ __startThermalInterpolationUpscaled(window.thermalPrevUpscaled, upscaledFrame, window.thermalStableMin, window.thermalStableMax, stableRange); } else { for(var k=0;k<upscaledFrame.length;k++){ var t=Number(upscaledFrame[k]); var tn=(t-window.thermalStableMin)/stableRange; tn = tn<0?0:(tn>1?1:tn); grid.children[k].style.backgroundColor = tempToColor(tn); } } window.thermalPrevUpscaled = upscaledFrame.slice(); var avg = sum/upscaledFrame.length; try{ var elMin=document.getElementById('thermalMin'); if(elMin){ elMin.textContent = (isFinite(min)?min.toFixed(1):'--'); } var elMax=document.getElementById('thermalMax'); if(elMax){ elMax.textContent = (isFinite(max)?max.toFixed(1):'--'); } var elAvg=document.getElementById('thermalAvg'); if(elAvg){ elAvg.textContent = (isFinite(avg)?avg.toFixed(1):'--'); } }catch(_){} }catch(e){ if(window.logSensorsFrame){ console.warn('[Thermal] render error:', e); } } }";
  inner += "function __smoothThermalFrame(buffer){ if(!buffer || buffer.length===0) return null; if(buffer.length===1) return buffer[0].slice(); var smoothed=new Array(768); for(var i=0;i<768;i++){ var sum=0; for(var j=0;j<buffer.length;j++){ sum+=Number(buffer[j][i])||0; } smoothed[i]=sum/buffer.length; } return smoothed; }";
  inner += "function __startThermalInterpolationUpscaled(prevFrame, newFrame, min, max, range){ if(window.thermalInterpolationInterval){ clearInterval(window.thermalInterpolationInterval); } var grid=document.getElementById('thermalGrid'); if(!grid || grid.children.length!==newFrame.length) return; var steps=window.thermalInterpolationSteps||3; var stepMs=Math.round(window.thermalPollingMs/(steps+1)); var currentStep=0; window.thermalInterpolationInterval=setInterval(function(){ currentStep++; var alpha=currentStep/(steps+1); if(alpha>=1){ clearInterval(window.thermalInterpolationInterval); window.thermalInterpolationInterval=null; for(var k=0;k<newFrame.length;k++){ var t=Number(newFrame[k]); var tn=(t-min)/range; tn=tn<0?0:(tn>1?1:tn); grid.children[k].style.backgroundColor=tempToColor(tn); } return; } for(var j=0;j<newFrame.length;j++){ var prevT=Number(prevFrame[j]); var newT=Number(newFrame[j]); var interpT=prevT+(newT-prevT)*alpha; var tn=(interpT-min)/range; tn=tn<0?0:(tn>1?1:tn); grid.children[j].style.backgroundColor=tempToColor(tn); } }, stepMs); }";
  inner += "function updateThermalVisualization(){ var t0=(window.performance&&performance.now)?performance.now():Date.now(); return __fetchThermal().then(function(success){ if(!success){ window.__thermalFailCount=(window.__thermalFailCount||0)+1; if(window.__thermalFailCount>=5 && window.logSensorsFrame){ console.warn('[Thermal] Multiple failures (#'+window.__thermalFailCount+') - Consider checking: 1) WiFi connection, 2) ESP32 thermal sensor status, 3) Browser console for detailed error messages'); } return; } window.__thermalFailCount=0; var t1=(window.performance&&performance.now)?performance.now():Date.now(); __renderThermal(); var t2=(window.performance&&performance.now)?performance.now():Date.now(); var dt=Math.round(t2 - t0); window.__thermalLastCaptureMs = dt; var perfEl=document.getElementById('thermal-performance'); if(perfEl){ perfEl.textContent = 'Capture: '+dt+'ms'; } var now=t2; if(window.__thermalLastFrameAt){ var frameDt = now - window.__thermalLastFrameAt; if(frameDt > 5){ var instFps = 1000 / frameDt; var clampMax = (typeof window.thermalFpsClampMax==='number')?window.thermalFpsClampMax:30; if(instFps > clampMax) instFps = clampMax; var a = (typeof window.thermalEwmaAlpha==='number')?window.thermalEwmaAlpha:0.1; window.__thermalFpsEwma = (window.__thermalFpsEwma==null)?instFps:(a*instFps + (1-a)*window.__thermalFpsEwma); var fpsEl=document.getElementById('thermalFps'); if(fpsEl && window.__thermalFpsEwma!=null){ fpsEl.textContent = (Math.max(0, Math.min(99.9, window.__thermalFpsEwma))).toFixed(1); } } } window.__thermalLastFrameAt = now; }).catch(function(err){ if(window.logSensorsFrame){ console.warn('[Thermal] Visualization error: '+(err.message||err)+', continuing polling'); } }); }";
  inner += "function startThermalPolling(){ if(window.thermalPollingInterval) return; try{ updateThermalVisualization(); }catch(_){} window.thermalPollingInterval=setInterval(function(){ updateThermalVisualization(); }, window.thermalPollingMs); }";
  inner += "function stopThermalPolling(){ if(window.thermalPollingInterval){ clearInterval(window.thermalPollingInterval); window.thermalPollingInterval=null; } }";
  // inner += "console.debug('SNSR - Thermal render/polling ready');";
  inner += "}catch(e){ console.warn('[Sensors][B1b-p2] init error', e); }";
  inner += "</script>";

  // Script B2a: ToF state + start/stop + transitions (small)
  inner += "<script>";
  inner += "console.debug('[SENSORS] ToF state begin');";
  inner += "try{";
  inner += "window.__s_markers=(window.__s_markers||[]); window.__s_markers.push('B2a');";
  inner += "var tofPollingInterval = null;";
  inner += "var tofObjectStates = [{}, {}, {}, {}];";
  inner += "var tofStabilityThreshold = 2;";
  inner += "var tofMaxDistance = 3400;";
  inner += "var tofPollingMs = 300;";
  inner += "function applyToFTransition(ms){ try{ var s=(ms/1000).toFixed(3)+'s'; for(var j=1;j<=4;j++){ var be=document.getElementById('distance-bar-'+j); if(be){ be.style.transition='width '+s+' ease'; } } }catch(e){ console.warn('[Sensors] applyToFTransition', e);} }";
  inner += "function startToFPolling(){ if(tofPollingInterval) return; var d=document.getElementById('tof-objects-display'); if(d){ d.style.display='block'; } if(typeof updateToFObjects==='function'){ updateToFObjects(); } tofPollingInterval = setInterval(function(){ if(typeof updateToFObjects==='function'){ updateToFObjects(); } }, tofPollingMs); }";
  inner += "function stopToFPolling(){ if(tofPollingInterval){ clearInterval(tofPollingInterval); tofPollingInterval=null; } var d=document.getElementById('tof-objects-display'); if(d){ d.style.display='none'; } }";
  inner += "console.debug('[SENSORS] ToF state ready');";
  inner += "}catch(e){ console.warn('[Sensors][B2a] init error', e); }";
  inner += "</script>";

  // Script B2b: ToF update implementation (small)
  inner += "<script>";
  inner += "console.debug('[SENSORS] ToF update begin');";
  inner += "try{";
  inner += "window.__s_markers=(window.__s_markers||[]); window.__s_markers.push('B2b');";
  inner += "function updateToFObjects(){";
  inner += "  var _uF='/api/sensors?sensor=tof&ts=' + Date.now();";
  inner += "  fetch(_uF, { cache: 'no-store', credentials: 'same-origin' })";
  inner += "  .then(function(response){ if(!response.ok){ var err='HTTP '+response.status; throw new Error(err); } return response.json(); })";
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
  inner += "          if(!state.lastDistance || Math.abs(state.lastDistance-distance_mm) < 200){";
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
  inner += "      if(summary){ var seq=(typeof data.seq!=='undefined'?data.seq:'?'); summary.textContent=validObjects+' object(s) detected of '+(data.total_objects||0)+' total | seq='+seq; }";
  inner += "    } else if(data && data.error){";
  inner += "      var summary2=document.getElementById('tof-objects-summary');";
  inner += "      if(summary2){ summary2.textContent='Error: '+data.error; }";
  inner += "    }";
  inner += "  })";
  inner += "  .catch(function(error){";
  inner += "    var summary3=document.getElementById('tof-objects-summary');";
  inner += "    if(summary3){ summary3.textContent='Connection error: '+(error&&error.message?error.message:''); }";
  inner += "    for(var j=1;j<=4;j++){ var be=document.getElementById('distance-bar-'+j); var ie=document.getElementById('object-info-'+j); if(be){ be.style.width='0%'; be.className='distance-bar invalid'; } if(ie){ ie.textContent='---'; } }";
  inner += "  });";
  inner += "}";
  inner += "console.debug('[SENSORS] ToF update ready');";
  inner += "}catch(e){console.error('[Sensors][B2b] init error', e);}";
  inner += "</script>";

  // Script C1: Bindings and setup
  inner += "<script>";
  inner += "console.debug('[SENSORS] UI bindings open');";
  inner += "document.addEventListener('DOMContentLoaded', function(){";
  inner += "  try {";
  inner += "    console.debug('[SENSORS] C1 DOMContentLoaded');";
  // TEMP: Comment out showPair function to test for syntax error
  inner += "    window.showPair=function(startId, stopId, running){ console.log('TEMP: showPair called but disabled for debugging'); };";
  // TEMP: Comment out bind function to test for syntax error
  inner += "    var bind = function(id, fn){ console.log('TEMP: bind called but disabled for debugging'); };";
  // TEMP: Comment out entire rebindSensorButtonsIfPresent function to test for syntax error
  inner += "    window.rebindSensorButtonsIfPresent = function(){";
  inner += "      console.log('TEMP: rebindSensorButtonsIfPresent called but bindings disabled for debugging');";
  inner += "    };";
  inner += "    // Initial attempt; subsequent calls after dynamic render will bind as elements appear";
  inner += "    try{ window.rebindSensorButtonsIfPresent(); }catch(_){ }";
  // Duplicate binding code removed - handled by rebindSensorButtonsIfPresent()
  // TEMP: Comment out palette selector to test for syntax error
  // inner += "    var paletteSel = document.getElementById('thermal-palette-select'); if(paletteSel){ paletteSel.addEventListener('change', function(){ changeThermalPalette(paletteSel.value); }); }";
  // TEMP: Comment out thermal grid creation to test for syntax error
  // inner += "    var grid = document.getElementById('thermalGrid'); if(grid && grid.children.length!==768){ for(var i=0;i<768;i++){ var cell=document.createElement('div'); cell.className='thermal-pixel'; cell.style.backgroundColor='rgb(128,128,128)'; grid.appendChild(cell);} }";
  inner += "    console.debug('[SENSORS] UI bindings init complete');";
  inner += "    try{ window.__s_markers.push('C1'); window.__s_markers.push('C'); }catch(_){ }";
  inner += "  } catch(e){ console.error('[Sensors][C1] init error', e); }";
  inner += "});";
  inner += "console.debug('[SENSORS] UI bindings close');";
  inner += "</script>";

  // Script C2: Initial button visibility (pre-hide)
  inner += "<script>";
  inner += "console.debug('[SENSORS] Pre-hide open');";
  inner += "document.addEventListener('DOMContentLoaded', function(){";
  inner += "  try {";
  inner += "    console.debug('[SENSORS] C2 DOMContentLoaded');";
  inner += "    try{ setHidden('btn-imu-stop', true); }catch(_){ }";
  inner += "    try{ setHidden('btn-apdscolor-stop', true); }catch(_){ }";
  inner += "    try{ setHidden('btn-apdsproximity-stop', true); }catch(_){ }";
  inner += "    try{ setHidden('btn-apdsgesture-stop', true); }catch(_){ }";
  inner += "    try{ setHidden('btn-thermal-stop', true); }catch(_){ }";
  inner += "    try{ setHidden('btn-tof-stop', true); }catch(_){ }";
  inner += "    console.debug('[SENSORS] Pre-hide init complete');";
  inner += "    try{ window.__s_markers.push('C2'); }catch(_){ }";
  inner += "  } catch(e){ console.error('[Sensors][C2] init error', e); }";
  inner += "});";
  inner += "console.debug('[SENSORS] Pre-hide close');";
  inner += "</script>";

  // Script D: Fetch and apply settings (isolated)
  inner += "<script>";
  inner += "try{";
  inner += "window.__s_markers=(window.__s_markers||[]);";
  inner += "function __applySensorsSettingsFrom(s,paletteSel){ try {";
  inner += "  var th=(s&&s.thermal)||{}; var thUI=th.ui||{}; var thDev=th.device||{}; var tof=(s&&s.tof)||{}; var tofUI=tof.ui||{}; var tofDev=tof.device||{};";
  inner += "  var webFps = (typeof thUI.thermalWebMaxFps==='number')?thUI.thermalWebMaxFps:s.thermalWebMaxFps; if(typeof webFps==='number'){ webFps=Math.max(1, Math.min(20, webFps)); window.thermalFpsClampMax = webFps; var derivedMs = Math.max(50, Math.round(1000 / webFps)); thermalPollingMs = derivedMs; }";
  inner += "  var tpoll = (typeof thUI.thermalPollingMs==='number')?thUI.thermalPollingMs:s.thermalPollingMs; if(typeof tpoll==='number'){ thermalPollingMs = Math.max(50, Math.min(5000, tpoll)); }";
  inner += "  var tofpoll = (typeof tofUI.tofPollingMs==='number')?tofUI.tofPollingMs:s.tofPollingMs; if(typeof tofpoll==='number'){ tofPollingMs = Math.max(50, Math.min(5000, tofpoll)); }";
  inner += "  if(typeof s.tofStabilityThreshold==='number'){ tofStabilityThreshold = Math.max(1, Math.min(20, s.tofStabilityThreshold)); }";
  inner += "  if(typeof s.thermalPaletteDefault==='string'){ window.thermalPalette = s.thermalPaletteDefault; if(paletteSel){ paletteSel.value = s.thermalPaletteDefault; } }";
  inner += "  if(typeof s.thermalEWMAFactor==='number'){ thermalEwmaAlpha = Math.max(0, Math.min(1, s.thermalEWMAFactor)); }";
  inner += "if(typeof s.thermalTransitionMs==='number' && typeof applyThermalTransition==='function'){ applyThermalTransition(Math.max(0, Math.min(1000, s.thermalTransitionMs))); }";
  inner += "  if(typeof thUI.thermalInterpolationEnabled==='boolean'){ window.thermalInterpolationEnabled = thUI.thermalInterpolationEnabled; } else if(typeof s.thermalInterpolationEnabled==='boolean'){ window.thermalInterpolationEnabled = s.thermalInterpolationEnabled; }";
  inner += "  var tsteps=(typeof thUI.thermalInterpolationSteps==='number')?thUI.thermalInterpolationSteps:s.thermalInterpolationSteps; if(typeof tsteps==='number'){ window.thermalInterpolationSteps = Math.max(1, Math.min(8, tsteps)); }";
  inner += "  var tbuf=(typeof thUI.thermalInterpolationBufferSize==='number')?thUI.thermalInterpolationBufferSize:s.thermalInterpolationBufferSize; if(typeof tbuf==='number'){ window.thermalBufferSize = Math.max(1, Math.min(10, tbuf)); }";
  inner += "  var q=(typeof thUI.thermalWebClientQuality==='number')?thUI.thermalWebClientQuality:s.thermalWebClientQuality; if(typeof q==='number'){ q=Math.max(1, Math.min(4, q)); window.thermalWebClientQuality=q; }";
  inner += "if(typeof s.debugSensorsFrame==='boolean'){ window.debugSensorsFrame = s.debugSensorsFrame; console.debug('[SENSORS] Quality check: type='+typeof q+', value='+q); }";
  inner += "if(typeof s.thermalWebClientQuality==='number'){ window.thermalWebClientQuality = Math.max(1, Math.min(16, s.thermalWebClientQuality)); console.debug('[SENSORS] Quality set to: '+window.thermalWebClientQuality); if(typeof __updateThermalGrid==='function'){ __updateThermalGrid(window.thermalWebClientQuality); console.debug('[SENSORS] Grid updated for quality: '+window.thermalWebClientQuality); } }";
  inner += "if(typeof s.debugSensorsFrame==='boolean'){ window.debugSensorsFrame = s.debugSensorsFrame; if(window.debugSensorsFrame && window.thermalWebClientQuality){ console.debug('[Thermal] Settings loaded: quality='+window.thermalWebClientQuality+'x'); } }";
  inner += "  if(typeof s.tofTransitionMs==='number' && typeof applyToFTransition==='function'){ applyToFTransition(Math.max(0, Math.min(1000, s.tofTransitionMs))); }";
  inner += "  if(typeof s.tofUiMaxDistanceMm==='number'){ tofMaxDistance = Math.max(100, Math.min(12000, s.tofUiMaxDistanceMm)); var rng=document.getElementById('tof-range-mm'); if(rng){ rng.textContent = String(tofMaxDistance); } }";
  inner += "} catch(e){ console.warn('[Sensors] apply settings error', e); } }";
  inner += "function __fetchAndApplySensorsSettings(){ console.debug('[SENSORS] Fetching settings...'); var paletteSel = document.getElementById('thermal-palette-select'); fetch('/api/settings').then(function(r){return r.json();}).then(function(cfg){ console.debug('[SENSORS] Settings received:', cfg); var s = (cfg && cfg.settings) ? cfg.settings : {}; console.debug('[SENSORS] Applying settings:', s); __applySensorsSettingsFrom(s, paletteSel); }).catch(function(e){ console.warn('[Sensors] settings fetch error', e); }); }";
  inner += "if(document.readyState==='loading'){ document.addEventListener('DOMContentLoaded', __fetchAndApplySensorsSettings); } else { __fetchAndApplySensorsSettings(); }";
  inner += "console.debug('[SENSORS] Settings apply scheduled'); try{ window.__s_markers.push('D'); }catch(_){ }";
  inner += "}catch(e){console.error('[Sensors][D] init error', e);}";
  inner += "</script>";

  // Device Registry Integration for Dynamic Sensor Cards
  inner += "<script>";
  inner += "console.log('[Sensors] Device registry integration');";
  inner += "window.fetchDeviceRegistry = function(){";
  inner += "  console.log('[Sensors] Fetching device registry...');";
  inner += "  return fetch('/api/devices', { credentials: 'include', cache: 'no-store' })";
  inner += "    .then(function(r){ ";
  inner += "      console.log('[Sensors] Device registry response:', r.status); ";
  inner += "      if (!r.ok) throw new Error('HTTP ' + r.status);";
  inner += "      return r.json(); ";
  inner += "    })";
  inner += "    .then(function(j){ ";
  inner += "      console.log('[Sensors] Device registry data:', j); ";
  inner += "      if (window.updateSensorCardVisibility) window.updateSensorCardVisibility(j);";
  inner += "    })";
  inner += "    .catch(function(e){ console.warn('[Sensors] Device registry fetch failed:', e); });";
  inner += "};";
  inner += "window.updateSensorCardVisibility = function(registry){";
  inner += "  if(!registry || !registry.devices) return;";
  inner += "  try{";
  inner += "    var devices = registry.devices;";
  inner += "    var hasIMU = devices.some(function(d){ return d.name === 'BNO055'; });";
  inner += "    var hasThermal = devices.some(function(d){ return d.name === 'MLX90640'; });";
  inner += "    var hasToF = devices.some(function(d){ return d.name === 'VL53L4CX'; });";
  inner += "    var hasAPDS = devices.some(function(d){ return d.name === 'APDS9960'; });";
  inner += "    var hasGamepad = devices.some(function(d){ return d.name === 'Seesaw'; });";
  inner += "    var hasDRV = devices.some(function(d){ return d.name === 'DRV2605'; });";
  inner += "    window.showHideSensorCard = window.showHideSensorCard || function(cardId, show){";
  inner += "      var card = document.getElementById(cardId);";
  inner += "      if(card) card.style.display = show ? 'block' : 'none';";
  inner += "    };";
  inner += "    window.showHideSensorCard('sensor-card-imu', hasIMU);";
  inner += "    window.showHideSensorCard('sensor-card-thermal', hasThermal);";
  inner += "    window.showHideSensorCard('sensor-card-tof', hasToF);";
  inner += "    window.showHideSensorCard('sensor-card-apds', hasAPDS);";
  inner += "    window.showHideSensorCard('sensor-card-gamepad', hasGamepad);";
  inner += "    console.log('[Sensors] Card visibility updated - IMU:'+hasIMU+' Thermal:'+hasThermal+' ToF:'+hasToF+' APDS:'+hasAPDS+' Gamepad:'+hasGamepad+' Haptic:'+hasDRV);";
  inner += "  }catch(e){ console.warn('[Sensors] Card visibility update error', e); }";
  inner += "};";
  inner += "console.log('[Sensors] Device registry functions added');";
  inner += "</script>";
  // E1-pre boundary marker
  inner += "<script>try{ console.debug('[SENSORS] E1-pre boundary'); (window.__s_markers=window.__s_markers||[]).push('E1-pre'); }catch(_){ }</script>";

  // Script E1a: Page activity + indicators
  inner += "<script>";
  inner += ";try{ console.debug('[SENSORS] E1a begin');";
  inner += "window.__s_markers=(window.__s_markers||[]); window.__s_markers.push('E1a');";
  inner += "window.isSensorsPageActive = function(){";
  inner += "  try{ return (location.pathname==='/sensors' || location.pathname.indexOf('/sensors')===0) && !document.hidden; }";
  inner += "  catch(_){ return true; }";
  inner += "};";
  inner += "window.setIndicator = function(id, enabled){";
  inner += "  var el = document.getElementById(id);";
  inner += "  if(!el) return;";
  inner += "  el.className = enabled ? 'status-indicator status-enabled' : 'status-indicator status-disabled';";
  inner += "};";
  inner += "window.__initialStatusApplied = false;";
  inner += "console.debug('[SENSORS] E1a ready');";
  inner += "}catch(e){ console.warn('[Sensors][E1a] init error', e); }";
  inner += "</script>";
  // E1a-after boundary marker
  inner += "<script>try{ console.debug('[SENSORS] E1a-after boundary'); (window.__s_markers=window.__s_markers||[]).push('E1a-after'); }catch(_){ }</script>";

  // Script E1b-1: Basic functions
  inner += "<script>";
  inner += ";try{ console.debug('[SENSORS] E1b-1 begin');";
  inner += "window.__s_markers=(window.__s_markers||[]); window.__s_markers.push('E1b-1');";
  inner += "if(typeof window.setIndicator!=='function'){ window.setIndicator=function(id, enabled){ var el=document.getElementById(id); if(!el) return; el.className = enabled ? 'status-indicator status-enabled' : 'status-indicator status-disabled'; }; }";
  inner += "if(typeof setIndicator!=='function'){ try{ setIndicator = window.setIndicator; }catch(_){ } }";
  inner += "if(typeof window.isSensorsPageActive!=='function'){ window.isSensorsPageActive=function(){ try{ return !document.hidden; }catch(_){ return true; } }; }";
  inner += "console.log('[Sensors] E1b-1 basic functions complete');";
  inner += "}catch(e){ console.warn('[Sensors][E1b-1] init error', e); }";
  inner += "</script>";
  
  // Script E1b-2: Device registry setup
  inner += "<script>";
  inner += ";try{ console.debug('[SENSORS] E1b-2 begin');";
  inner += "console.log('[Sensors] Setting up device registry support');";
  inner += "window.__deviceRegistry = null;";
  inner += "window.fetchDeviceRegistry = function(){";
  inner += "  console.log('[Sensors] fetchDeviceRegistry called');";
  inner += "  return fetch('/api/devices', { credentials: 'include', cache: 'no-store' })";
  inner += "    .then(function(r){ console.log('[Sensors] Device registry fetch response:', r.status); return r.json(); })";
  inner += "    .then(function(d){ ";
  inner += "      console.log('[Sensors] Setting window.__deviceRegistry to:', d);";
  inner += "      window.__deviceRegistry = d; ";
  inner += "      console.log('[Sensors] Device registry loaded and stored:', window.__deviceRegistry);";
  inner += "      return d;";
  inner += "    })";
  inner += "    .catch(function(e){ console.warn('[Sensors] Device registry fetch failed:', e); return null; });";
  inner += "};";
  inner += "console.log('[Sensors] E1b-2 fetchDeviceRegistry complete');";
  inner += "}catch(e){ console.warn('[Sensors][E1b-2] init error', e); }";
  inner += "</script>";
  
  // Script E1b-3: getAvailableSensorsForPage function
  inner += "<script>";
  inner += ";try{ console.debug('[SENSORS] E1b-3 begin');";
  inner += "window.getAvailableSensorsForPage = function(deviceRegistry, sensorStatus){";
  inner += "  var sensors = [];";
  inner += "  if(!deviceRegistry || !deviceRegistry.devices) return sensors;";
  inner += "  deviceRegistry.devices.forEach(function(device){";
  inner += "    console.log('[Sensors] Checking device:', device.name);";
  inner += "    if(device.name === 'BNO055'){";
  inner += "      sensors.push({key:'imu', name:'IMU', desc:'IMU sensor', enabled:!!sensorStatus.imuEnabled, dataId:'gyro-data', dataText:'IMU data here'});";
  inner += "    } else if(device.name === 'MLX90640'){";
  inner += "      sensors.push({key:'thermal', name:'Thermal', desc:'Thermal sensor', enabled:!!sensorStatus.thermalEnabled, dataId:'thermal-data', dataText:'Thermal data here'});";
  inner += "    } else if(device.name === 'VL53L4CX'){";
  inner += "      sensors.push({key:'tof', name:'ToF', desc:'ToF sensor', enabled:!!sensorStatus.tofEnabled, dataId:'tof-data', dataText:'ToF data here'});";
  inner += "    } else if(device.name === 'DRV2605'){";
  inner += "      sensors.push({key:'haptic', name:'Haptic', desc:'Haptic sensor', enabled:!!sensorStatus.hapticEnabled, dataId:'haptic-data', dataText:'Haptic data here'});";
  inner += "    }";
  inner += "  });";
  inner += "  console.log('[Sensors] getAvailableSensorsForPage returning', sensors.length, 'sensors:', sensors);";
  inner += "  return sensors;";
  inner += "};";
  inner += "console.log('[Sensors] E1b-3 getAvailableSensorsForPage complete');";
  inner += "}catch(e){ console.warn('[Sensors][E1b-3] init error', e); }";
  inner += "</script>";
  
  // Script E1b-4: createSensorCards function
  inner += "<script>";
  inner += "console.log('E1b-4 STEP 1: Starting');";
  inner += "window.applySensorStatus = function(s){";
  inner += "  if(window.__applyingSensorStatus){ console.warn('[Sensors] Preventing recursive applySensorStatus call'); return; }";
  inner += "  window.__applyingSensorStatus = true;";
  inner += "  console.log('[Sensors] NEW applySensorStatus called with:', s);";
  inner += "  if(!s) return;";
  inner += "  console.log('[Sensors] Step A: Basic setup');";
  inner += "  window.__lastSensorStatus = s;";
  inner += "  window.__initialStatusApplied = true;";
  inner += "  console.log('[Sensors] Step B: Getting elements');";
  inner += "  var loading = document.getElementById('sensors-loading');";
  inner += "  var grid = document.getElementById('sensors-grid');";
  inner += "  console.log('[Sensors] Step C: Hiding loading');";
  inner += "  if(loading) loading.style.display = 'none';";
  inner += "  if(grid) { grid.style.display = 'grid'; grid.innerHTML = ''; }";
  inner += "  console.log('[Sensors] Step D: Creating sensor cards');";
  inner += "  if(window.__deviceRegistry && window.__deviceRegistry.devices){";
  inner += "    console.log('[Sensors] Device registry available with', window.__deviceRegistry.devices.length, 'devices');";
  inner += "    var availableSensors = window.getAvailableSensorsForPage(window.__deviceRegistry, s);";
  inner += "    console.log('[Sensors] Available sensors:', availableSensors.length);";
  inner += "    for(var i = 0; i < availableSensors.length; i++){";
  inner += "      var sensor = availableSensors[i];";
  inner += "      var card = document.createElement('div');";
  inner += "      card.className = 'sensor-card';";
  inner += "      card.id = 'sensor-card-' + sensor.key;";
  inner += "      var statusClass = sensor.enabled ? 'status-enabled' : 'status-disabled';";
  inner += "      var cardHTML = '<div class=\"sensor-title\"><span>' + sensor.name + '</span><span class=\"status-indicator ' + statusClass + '\" id=\"' + sensor.key + '-status-indicator\"></span></div>';";
  inner += "      cardHTML += '<div class=\"sensor-description\">' + sensor.desc + '</div>';";
  inner += "      cardHTML += '<div class=\"sensor-controls\">';";
  inner += "      if(sensor.key==='imu'){";
  inner += "        cardHTML += '<button class=\"btn\" id=\"btn-imu-start\">Start</button>';";
  inner += "        cardHTML += '<button class=\"btn\" id=\"btn-imu-stop\">Stop</button>';";
  inner += "      } else if(sensor.key==='thermal'){";
  inner += "        cardHTML += '<button class=\"btn\" id=\"btn-thermal-start\">Start</button>';";
  inner += "        cardHTML += '<button class=\"btn\" id=\"btn-thermal-stop\">Stop</button>';";
  inner += "      } else if(sensor.key==='tof'){";
  inner += "        cardHTML += '<button class=\"btn\" id=\"btn-tof-start\">Start</button>';";
  inner += "        cardHTML += '<button class=\"btn\" id=\"btn-tof-stop\">Stop</button>';";
  inner += "      }";
  inner += "      cardHTML += '</div>';";
  inner += "      cardHTML += '<div class=\"sensor-data\" id=\"' + sensor.dataId + '\">' + sensor.dataText + '</div>';";
  inner += "      card.innerHTML = cardHTML;";
  inner += "      if(grid) grid.appendChild(card);";
  inner += "    }";
  inner += "    try{ if(window.rebindSensorButtonsIfPresent) window.rebindSensorButtonsIfPresent(); }catch(_){ }";
  inner += "    try{ if(window.showPair){";
  inner += "      try{ showPair('btn-imu-start','btn-imu-stop', !!s.imuEnabled); }catch(_){ }";
  inner += "      try{ showPair('btn-thermal-start','btn-thermal-stop', !!s.thermalEnabled); }catch(_){ }";
  inner += "      try{ showPair('btn-tof-start','btn-tof-stop', !!s.tofEnabled); }catch(_){ }";
  inner += "    } }catch(_){ }";
  inner += "    try{ if(window.setIndicator){";
  inner += "      try{ setIndicator('imu-status-indicator', !!s.imuEnabled); }catch(_){ }";
  inner += "      try{ setIndicator('thermal-status-indicator', !!s.thermalEnabled); }catch(_){ }";
  inner += "      try{ setIndicator('tof-status-indicator', !!s.tofEnabled); }catch(_){ }";
  inner += "    } }catch(_){ }";
  inner += "  } else {";
  inner += "    console.log('[Sensors] No device registry available yet');";
  inner += "  }";
  inner += "  console.log('[Sensors] applySensorStatus complete');";
  inner += "  window.__applyingSensorStatus = false;";
  inner += "};";
  inner += "console.log('E1b-4 STEP 2: Function defined');";
  inner += "console.log('E1b-4 applySensorStatus DEFINED');";
  inner += "</script>";
  
  // E1b-after boundary marker
  inner += "<script>try{ console.debug('[SENSORS] E1b-after boundary'); (window.__s_markers=window.__s_markers||[]).push('E1b-after'); }catch(_){ }</script>";

  // Script E1c: status fetch + helpers
  inner += "<script>";
  inner += ";try{ console.debug('[SENSORS] E1c begin');";
  inner += "window.__s_markers=(window.__s_markers||[]); window.__s_markers.push('E1c');";
  inner += "window.fetchInitialStatus = function(){ if(window.debugSensorsGeneral){ console.log('[DEBUG-FETCH] === fetchInitialStatus called ==='); }";
  inner += "  if(window.debugSensorsGeneral){ console.log('[DEBUG] fetchInitialStatus called'); }";
  inner += "  return fetch('/api/sensors/status', { credentials: 'include', cache: 'no-store' })";
  inner += "    .then(function(r){ if(window.debugSensorsGeneral){ console.log('[DEBUG] status fetch response:', r.status); } return r.json(); })";
  inner += "    .then(function(j){ if(window.debugSensorsGeneral){ console.log('[DEBUG-FETCH] status data received:', j); console.log('[DEBUG-FETCH] About to call applySensorStatus...'); } applySensorStatus(j); window.__sensorStatusSeq = j.seq || 0; })";
  inner += "    .catch(function(e){ console.warn('[Sensors] status fetch failed', e); });";
  inner += "};";
  inner += "try{ console.debug('[SENSORS] E1c-a after fetchInitialStatus def'); (window.__s_markers=window.__s_markers||[]).push('E1c-a'); }catch(_){ }";
  inner += "try{ console.debug('[SENSORS] E1c-b after setTimeout'); (window.__s_markers=window.__s_markers||[]).push('E1c-b'); }catch(_){ }";
  inner += "try{ console.debug('SNSR - E1c-c after DOMContentLoaded bind'); (window.__s_markers=window.__s_markers||[]).push('E1c-c'); }catch(_){ }";
  inner += "window.fetchStatusDelayed = function(delayMs){";
  inner += "  delayMs = delayMs || 100;";
  // inner += "  console.log('[DEBUG] fetchStatusDelayed called with delay:', delayMs)";
  inner += "  setTimeout(function(){";
  // inner += "    console.log('[DEBUG] fetchStatusDelayed executing after delay')";
  inner += "    try{ if(window.fetchInitialStatus) window.fetchInitialStatus(); }catch(_){ }";
  inner += "  }, delayMs);";
  inner += "};";
  inner += "try{ console.debug('SNSR - E1c-d after fetchStatusDelayed def'); (window.__s_markers=window.__s_markers||[]).push('E1c-d'); }catch(_){ }";
  inner += "console.debug('SNSR - E1c ready');";
  inner += "}catch(e){ console.warn('[Sensors][E1c] init error', e); }";
  inner += "</script>";
  // E1c-after boundary marker
  inner += "<script>try{ console.debug('SNSR - E1c-after boundary'); (window.__s_markers=window.__s_markers||[]).push('E1c-after'); }catch(_){ }</script>";

  // Script E1d-1: attachSSE
  inner += "<script>";
  inner += ";try{ console.debug('SNSR - E1d-1 attachSSE begin'); window.__s_markers=(window.__s_markers||[]); window.__s_markers.push('E1d-1');";
  inner += "window.attachSSE = function(){";
  inner += "  try{";
  // inner += "    console.log('[DEBUG-SSE] attachSSE called, window.__es:', !!window.__es, 'already attached:', !!window.__sensorStatusAttached);";
  inner += "    if(!window.__es){ return false; }";
  inner += "    if(window.__sensorStatusAttached){ return true; }";
  inner += "    var handler = function(e){";
  inner += "      try{ var dj = JSON.parse(e.data||'{}'); var seq = (dj && dj.seq) ? dj.seq : 0;";
  // inner += "        console.log('[DEBUG-SSE] SSE sensor-status event received:', e.data);";
  inner += "        var cur = window.__sensorStatusSeq || 0; if(seq<=cur){ return; }";
  inner += "        window.__sensorStatusSeq = seq; if(window.applySensorStatus) window.applySensorStatus(dj); window.__lastSSEEventAt = Date.now();";
  inner += "      }catch(err){ console.warn('[DEBUG-SSE] SSE status parse error', err); }";
  inner += "    };";
  inner += "    window.__es.addEventListener('sensor-status', handler);";
  inner += "    var pingHandler = function(e){";
  inner += "      try{ var dj = JSON.parse(e.data||'{}'); var srvSeq = (dj && dj.seq) ? dj.seq : 0; var cur = window.__sensorStatusSeq || 0;";
  inner += "        var willFetch = srvSeq>cur; window.__lastSSEEventAt = Date.now();";
  inner += "        if(willFetch && window.fetchInitialStatus){ window.fetchInitialStatus(); }";
  inner += "      }catch(err){ console.warn('[DEBUG-SSE] SSE ping parse error', err); }";
  inner += "    };";
  inner += "    window.__es.addEventListener('sensor-status-ping', pingHandler);";
  inner += "    window.__sensorStatusAttached = true;";
  // inner += "    console.log('[DEBUG-SSE] SSE sensor-status handlers attached');";
  inner += "    window.addEventListener('beforeunload', function(){ try{ if(window.__es){ window.__es.removeEventListener('sensor-status', handler); window.__es.removeEventListener('sensor-status-ping', pingHandler); } window.__sensorStatusAttached=false; }catch(_){ } });";
  inner += "    return true;";
  inner += "  }catch(e){ console.error('[DEBUG-SSE] attachSSE error:', e); return false; }";
  inner += "};";
  inner += "console.debug('SNSR - E1d-1 ready');";
  inner += "}catch(e){ console.warn('[Sensors][E1d-1] init error', e); }";
  inner += "</script>";
  inner += "<script>try{ console.debug('SNSR - E1d-1-after'); (window.__s_markers=window.__s_markers||[]).push('E1d-1-after'); }catch(_){ }</script>";

  // Script E1d-2a: ensureSSE function start
  inner += "<script>";
  inner += "try{";
  inner += "console.debug('SNSR - E1d-2a ensureSSE begin');";
  inner += "window.__s_markers=(window.__s_markers||[]);";
  inner += "window.__s_markers.push('E1d-2a');";
  inner += "window.ensureSSE = function(){";
  // inner += "  console.log('[DEBUG] ensureSSE called');";
  inner += "  function createESIfNeeded(){";
  inner += "    try{";
  inner += "      if(!window.ENABLE_SSE){ return false; }";
  inner += "      if(!window.EventSource){ return false; }";
  inner += "      if(window.__es){";
  inner += "        var rs = -1;";
  inner += "        try{ if(typeof window.__es.readyState !== 'undefined'){ rs = window.__es.readyState; } }catch(_){}";
  inner += "        if(rs === 2){ try{ window.__es.close(); }catch(_){} window.__es = null; }";
  inner += "      }";
  inner += "      if(window.__es){ return true; }";
  inner += "      var es = new EventSource('/api/events');";
  inner += "      es.onerror = function(){ try{ es.close(); }catch(_){} window.__es = null; };";
  inner += "      window.__es = es;";
  // inner += "      console.log('[DEBUG-SSE] EventSource reopened');";
  inner += "      return true;";
  inner += "    }catch(_){ return false; }";
  inner += "  };";
  inner += "  createESIfNeeded();";
  inner += "  if(window.attachSSE && window.attachSSE()){ return; }";
  // inner += "  console.log('[DEBUG] SSE retrying');";
  inner += "  var attempts = 0;";
  inner += "  var t = setInterval(function(){";
  inner += "    attempts = attempts + 1;";
  // inner += "    console.log('[DEBUG] SSE retry attempt:', attempts);";
  inner += "    createESIfNeeded();";
  inner += "    var attached = window.attachSSE ? window.attachSSE() : false;";
  inner += "    if(attached || attempts > 40){";
  // inner += "      console.log('[DEBUG] SSE retry done, attempts:', attempts);";
  inner += "      clearInterval(t);";
  inner += "    }";
  inner += "  }, 250);";
  inner += "};";
  inner += "console.debug('SNSR - E1d-2a ready');";
  inner += "}catch(e){ console.warn('[Sensors][E1d-2a] error', e); }";
  inner += "</script>";
  inner += "<script>try{ console.debug('SNSR - E1d-2-after'); (window.__s_markers=window.__s_markers||[]).push('E1d-2-after'); }catch(_){ }</script>";

  // Script E1d-3: triggerSSEBurst
  inner += "<script>";
  inner += ";try{ console.debug('SNSR - E1d-3 triggerSSEBurst begin'); window.__s_markers=(window.__s_markers||[]); window.__s_markers.push('E1d-3');";
  inner += "window.triggerSSEBurst = function(){";
  inner += "  var ts = new Date().toISOString();";
  inner += "  console.log('[DEBUG-SSE] triggerSSEBurst called @', ts, '- triggering status refresh and quick probes');";
  inner += "  try{ fetchStatusDelayed(100); }catch(_){ };";
  inner += "  try{ if(window.triggerSSECheck) window.triggerSSECheck(); }catch(_){ }";
  inner += "};";
  inner += "console.debug('SNSR - E1d-3 ready');";
  inner += "}catch(e){ console.warn('[Sensors][E1d-3] init error', e); }";
  inner += "</script>";
  inner += "<script>try{ console.debug('SNSR - E1d-3-after'); (window.__s_markers=window.__s_markers||[]).push('E1d-3-after'); }catch(_){ }</script>";

  // Script E1d-4: notifyOtherSessions
  inner += "<script>";
  inner += ";try{ console.debug('SNSR - E1d-4 notifyOtherSessions begin'); window.__s_markers=(window.__s_markers||[]); window.__s_markers.push('E1d-4');";
  inner += "window.notifyOtherSessions = function(statusData){";
  inner += "  /* no-op: prevent self-triggering SSE on initial/status fetch */";
  inner += "  console.log('[DEBUG-SSE] notifyOtherSessions no-op');";
  inner += "};";
  inner += "console.debug('SNSR - E1d-4 ready');";
  inner += "}catch(e){ console.warn('[Sensors][E1d-4] init error', e); }";
  inner += "</script>";
  inner += "<script>try{ console.debug('SNSR - E1d-4-after'); (window.__s_markers=window.__s_markers||[]).push('E1d-4-after'); }catch(_){ }</script>";

  // Script E1d-5: probe cooldown + triggerSSECheck (compact)
  inner += "<script>";
  inner += ";(function(){ try{ (window.__s_markers=window.__s_markers||[]).push('E1d-5'); }catch(_){ }";
  inner += "window.__probeCooldownMs=10000; window.__lastProbeAt=0;";
  inner += "window.setSSEProbeCooldownMs=function(ms){ window.__probeCooldownMs=Math.max(0,(ms|0)); };";
  inner += "window.triggerSSECheck=function(){ var n=Date.now(),l=window.__lastProbeAt||0,cd=window.__probeCooldownMs||0; if(n-l<cd){ return; } window.__lastProbeAt=n; if(window.fetchStatusDelayed) window.fetchStatusDelayed(0); };";
  inner += "})();";
  inner += "</script>";
  inner += "<script>try{ (window.__s_markers=window.__s_markers||[]).push('E1d-5-after'); }catch(_){ }</script>";

  // E1d-after boundary marker
  inner += "<script>try{ console.debug('SNSR - E1d-after boundary'); (window.__s_markers=window.__s_markers||[]).push('E1d-after'); }catch(_){ }</script>";

  // Script E2a-1: basic setup
  inner += "<script>";
  inner += "try{";
  inner += "  window.ENABLE_SSE = true;";
  inner += "  console.log('[DEBUG-SSE] SSE enabled for pure event-driven updates');";
  inner += "  window.__s_markers = window.__s_markers || [];";
  inner += "  window.__s_markers.push('E2a-1');";
  inner += "  console.debug('SNSR - E2a-1 start');";
  inner += "  function handleVisibility(){";
  inner += "    try{";
  inner += "      var isActive = true;";
  inner += "      if(window.isSensorsPageActive){ isActive = window.isSensorsPageActive(); }";
  inner += "      if(!isActive){";
  inner += "        if(typeof stopSensorPolling === 'function'){";
  inner += "          stopSensorPolling('thermal');";
  inner += "          stopSensorPolling('tof');";
  inner += "          stopSensorPolling('imu');";
  inner += "          stopSensorPolling('apdscolor');";
  inner += "          stopSensorPolling('apdsproximity');";
  inner += "          stopSensorPolling('apdsgesture');";
  inner += "          stopSensorPolling('gamepad');";
  inner += "        }";
  inner += "      } else {";
  inner += "        if(window.fetchInitialStatus) window.fetchInitialStatus();";
  inner += "        if(window.triggerSSECheck) window.triggerSSECheck();";
  inner += "      }";
  inner += "    }catch(_){}";
  inner += "  }";
  inner += "  window.handleVisibility = handleVisibility;";
  inner += "  console.debug('SNSR - E2a-1 ready');";
  inner += "}catch(e){ console.warn('[Sensors][E2a-1] error', e); }";
  inner += "</script>";
  inner += "<script>try{ window.__s_markers.push('E2a-1-after'); }catch(_){}</script>";

  // Script E2a-2: kickoff function
  inner += "<script>";
  inner += "try{";
  inner += "  window.__s_markers.push('E2a-2');";
  inner += "  function kickoff(){";
  inner += "    try{";
  inner += "      if(window.fetchDeviceRegistry) window.fetchDeviceRegistry();";
  inner += "      if(window.ensureSSE) window.ensureSSE();";
  inner += "      if(window.fetchInitialStatus && !window.__initialStatusApplied){";
  inner += "        window.fetchInitialStatus();";
  inner += "      }";
  inner += "      if(window.triggerSSECheck) window.triggerSSECheck();";
  inner += "    }catch(_){}";
  inner += "  }";
  inner += "  window.kickoff = kickoff;";
  inner += "  console.debug('SNSR - E2a-2 ready');";
  inner += "}catch(e){ console.warn('[Sensors][E2a-2] error', e); }";
  inner += "</script>";
  inner += "<script>try{ window.__s_markers.push('E2a-2-after'); }catch(_){}</script>";

  // Script E2a-3: periodic trigger only (10s)
  inner += "<script>";
  inner += "try{";
  inner += "  window.__s_markers.push('E2a-3');";
  inner += "  window.__lastSSEEventAt = Date.now();";
  inner += "  if(window.__ssePeriodic){ clearInterval(window.__ssePeriodic); }";
  inner += "  window.__ssePeriodic = setInterval(function(){";
  inner += "    try{";
  inner += "      if(document.visibilityState && document.visibilityState !== 'visible'){ return; }";
  // inner += "      console.log('[DEBUG-SSE] 60s periodic trigger');";
  inner += "      if(window.triggerSSECheck) window.triggerSSECheck();";
  inner += "    }catch(_){}";
  inner += "  }, 10000);";
  inner += "  console.debug('SNSR - E2a-3 ready');";
  inner += "}catch(e){ console.warn('[Sensors][E2a-3] error', e); }";
  inner += "</script>";
  inner += "<script>try{ window.__s_markers.push('E2a-3-after'); console.debug('SNSR - E2a ready'); }catch(_){}</script>";

  // E2b: bind visibility + kickoff
  inner += "<script>";
  inner += "try{";
  inner += "  (window.__s_markers=window.__s_markers||[]).push('E2b'); console.debug('SNSR - E2b start');";
  inner += "  try{ document.addEventListener('visibilitychange', handleVisibility); }catch(_){ }";
  inner += "  try{ if(document.readyState==='loading'){ document.addEventListener('DOMContentLoaded', kickoff); } else { kickoff(); } }catch(_){ }";
  inner += "  console.debug('SNSR - E2b after listeners');";
  inner += "}catch(e){ console.warn('[Sensors][E2b] init error', e); }";
  inner += "</script>";

  // E2c: interaction-driven probes
  inner += "<script>";
  inner += "try{";
  inner += "  (window.__s_markers=window.__s_markers||[]).push('E2c'); console.debug('SNSR - E2c start');";
  inner += "  try{ document.addEventListener('pointerdown', function(){ try{ if(window.triggerSSECheck) window.triggerSSECheck(); }catch(_){ } }); }catch(_){ }";
  inner += "  try{ window.addEventListener('keydown', function(){ try{ if(window.triggerSSECheck) window.triggerSSECheck(); }catch(_){ } }); }catch(_){ }";
  // Removed mousemove-triggered probe to reduce unnecessary triggers
  inner += "  console.debug('SNSR - E2c after interaction binds');";
  inner += "}catch(e){ console.warn('[Sensors][E2c] init error', e); }";
  inner += "</script>";

  // E2d: final marker
  inner += "<script>try{ (window.__s_markers=window.__s_markers||[]).push('E2d'); console.debug('SNSR - E2d ready'); console.debug('SNSR - Attach/SSE ready'); }catch(e){ console.warn('[Sensors][E2d] init error', e); }</script>";
  // E2-after boundary marker
  inner += "<script>try{ console.debug('SNSR - E2-after boundary'); (window.__s_markers=window.__s_markers||[]).push('E2-after'); }catch(_){ }</script>";

  // EOF marker
  inner += "<script>try{ console.debug('SNSR - EOF'); window.__s_markers.push('EOF'); }catch(_){ };</script>";

  return inner;
}

// getSensorsPage function moved to web_sensors_clean.h

#endif // WEB_SENSORS_H
