#ifndef WEB_SETTINGS_H
#define WEB_SETTINGS_H

String getSettingsPage(const String& username) {
  String inner;
  inner += "<h2>System Settings</h2>";
  inner += "<p>Configure your HardwareOne device settings</p>";
  // Page sections below render the live settings; top summary removed.
  
  // WiFi Network Section
  inner += "<div style='background:#f8f9fa;border-radius:8px;padding:1.5rem;margin:1rem 0;border-left:4px solid #667eea;color:#333'>";
  inner += "  <div style='font-size:1.2rem;font-weight:bold;margin-bottom:0.5rem;color:#333'>WiFi Network</div>";
  inner += "  <div style='color:#666;margin-bottom:1rem;font-size:0.9rem'>Current WiFi network and connection settings.</div>";
  inner += "  <div style='margin-bottom:1rem'>";
  inner += "    <span style='color:#333'>SSID: <span style='font-weight:bold;color:#667eea' id='wifi-ssid'>-</span></span>";
  inner += "  </div>";
  inner += "  <div style='display:flex;align-items:center;gap:1rem;margin-bottom:1rem;flex-wrap:wrap'>";
  inner += "    <span style='color:#333'>Auto-Reconnect: <span style='font-weight:bold;color:#667eea' id='wifi-value'>-</span></span>";
  inner += "    <button class='btn' onclick='toggleWifi()' id='wifi-btn'>Toggle</button>";
  inner += "  </div>";
  inner += "  <div style='display:flex;align-items:center;gap:1rem;flex-wrap:wrap'>";
  inner += "    <button class='btn' onclick='disconnectWifi()'>Disconnect WiFi</button>";
  inner += "    <button class='btn' onclick='scanNetworks()'>Scan Networks</button>";
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
  inner += "</div>";

  // Output Channels Section
  inner += "<div style='background:#f8f9fa;border-radius:8px;padding:1.5rem;margin:1rem 0;border-left:4px solid #667eea;color:#333'>";
  inner += "  <div style='font-size:1.2rem;font-weight:bold;margin-bottom:0.5rem;color:#333'>Output Channels</div>";
  inner += "  <div style='color:#666;margin-bottom:1rem;font-size:0.9rem'>Configure which output channels are enabled for system messages.</div>";
  inner += "  <div style='display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,1fr));gap:1rem'>";
  inner += "    <div style='display:flex;align-items:center;gap:0.5rem'>";
  inner += "      <span style='color:#333'>Serial: <span style='font-weight:bold;color:#667eea' id='serial-value'>-</span></span>";
  inner += "      <button class='btn' onclick='toggleOutput(\"outSerial\")' id='serial-btn'>Toggle</button>";
  inner += "    </div>";
  inner += "    <div style='display:flex;align-items:center;gap:0.5rem'>";
  inner += "      <span style='color:#333'>Web: <span style='font-weight:bold;color:#667eea' id='web-value'>-</span></span>";
  inner += "      <button class='btn' onclick='toggleOutput(\"outWeb\")' id='web-btn'>Toggle</button>";
  inner += "    </div>";
  inner += "    <div style='display:flex;align-items:center;gap:0.5rem'>";
  inner += "      <span style='color:#333'>TFT: <span style='font-weight:bold;color:#667eea' id='tft-value'>-</span></span>";
  inner += "      <button class='btn' onclick='toggleOutput(\"outTft\")' id='tft-btn'>Toggle</button>";
  inner += "    </div>";
  inner += "  </div>";
  inner += "</div>";
  
  // CLI History Section
  inner += "<div style='background:#f8f9fa;border-radius:8px;padding:1.5rem;margin:1rem 0;border-left:4px solid #667eea;color:#333'>";
  inner += "  <div style='font-size:1.2rem;font-weight:bold;margin-bottom:0.5rem;color:#333'>CLI History Size</div>";
  inner += "  <div style='color:#666;margin-bottom:1rem;font-size:0.9rem'>Number of commands to keep in CLI history buffer.</div>";
  inner += "  <div style='display:flex;align-items:center;gap:1rem;flex-wrap:wrap'>";
  inner += "    <span style='color:#333'>Current: <span style='font-weight:bold;color:#667eea' id='cli-value'>-</span></span>";
  inner += "    <input type='number' id='cli-input' min='1' max='100' value='10' style='padding:0.5rem;border:1px solid #ddd;border-radius:4px;width:80px'>";
  inner += "    <button class='btn' onclick='updateCliHistory()'>Update</button>";
  inner += "    <button class='btn' onclick='clearCliHistory()'>Clear History</button>";
  inner += "  </div>";
  inner += "</div>";

  // Sensors UI Settings (non-advanced)
  inner += "<div style='background:#f8f9fa;border-radius:8px;padding:1.5rem;margin:1rem 0;border-left:4px solid #667eea;color:#333'>";
  inner += "  <div style='font-size:1.2rem;font-weight:bold;margin-bottom:0.5rem;color:#333'>Sensors UI Settings</div>";
  inner += "  <div style='color:#666;margin-bottom:1rem;font-size:0.9rem'>Client-side visualization behavior. Applies without reboot (may require page reload).</div>";
  inner += "  <div style='display:grid;grid-template-columns:repeat(auto-fit,minmax(260px,1fr));gap:1rem'>";
  inner += "    <label title=\"How often the thermal camera UI fetches frames. Higher = less CPU/network, lower = smoother updates.\">Thermal Polling (ms)<br><input type='number' id='thermalPollingMs' min='100' max='2000' step='50' value='200' style='padding:0.5rem;border:1px solid #ddd;border-radius:4px;width:140px' title='Polling interval for thermal UI'></label>";
  inner += "    <label title=\"How often the ToF UI fetches distance data.\">ToF Polling (ms)<br><input type='number' id='tofPollingMs' min='100' max='2000' step='50' value='300' style='padding:0.5rem;border:1px solid #ddd;border-radius:4px;width:140px' title='Polling interval for ToF UI'></label>";
  inner += "    <label title=\"Number of consecutive stable ToF readings required before updating the displayed value.\">ToF Stability Threshold<br><input type='number' id='tofStabilityThreshold' min='1' max='10' step='1' value='3' style='padding:0.5rem;border:1px solid #ddd;border-radius:4px;width:140px' title='Stability filter for ToF display'></label>";
  inner += "    <label title=\"Default color palette for thermal visualization.\">Thermal Default Palette<br><select id='thermalPaletteDefault' class='menu-item' style='padding:0.4rem' title='Default thermal palette'><option value='turbo'>Turbo</option><option value='ironbow'>Ironbow</option><option value='grayscale'>Grayscale</option><option value='rainbow'>Rainbow</option></select></label>";
  inner += "  </div>";
  inner += "  <div style='margin-top:1rem'><button class='btn' onclick=\"updateNonAdvancedSensorsUI()\">Save Sensors UI</button></div>";
  inner += "</div>";

  // Advanced Settings (hidden by default)
  inner += "<div style='background:#fff;border-radius:8px;padding:1.5rem;margin:1rem 0;border-left:4px solid #6c757d;color:#333'>";
  inner += "  <div style='display:flex;align-items:center;justify-content:space-between'>";
  inner += "    <div style='font-size:1.2rem;font-weight:bold;color:#333'>Advanced Settings</div>";
  inner += "    <button class='btn' id='btn-advanced-toggle' onclick=\"toggleAdvanced()\">Show Advanced</button>";
  inner += "  </div>";
  inner += "  <div id='advanced-section' style='display:none;margin-top:1rem'>";
  inner += "    <div style='color:#856404;background:#fff3cd;border:1px solid #ffeeba;border-radius:6px;padding:0.75rem;margin-bottom:1rem'>Caution: changing these may impact stability. Values validated on save.</div>";
  inner += "    <div style='display:grid;grid-template-columns:repeat(auto-fit,minmax(260px,1fr));gap:1rem'>";
  inner += "      <label title=\"Smoothing factor for thermal values (0 = no smoothing, 1 = very slow changes).\">Thermal EWMA Factor (0..1)<br><input type='number' id='thermalEWMAFactor' min='0' max='1' step='0.05' value='0.2' style='padding:0.5rem;border:1px solid #ddd;border-radius:4px;width:140px' title='EWMA smoothing factor'></label>";
  inner += "      <label title=\"Animation duration for thermal color updates.\">Thermal Transition (ms)<br><input type='number' id='thermalTransitionMs' min='0' max='500' step='10' value='120' style='padding:0.5rem;border:1px solid #ddd;border-radius:4px;width:140px' title='Thermal transition duration'></label>";
  inner += "      <label title=\"Animation duration for ToF UI updates.\">ToF Transition (ms)<br><input type='number' id='tofTransitionMs' min='0' max='500' step='10' value='200' style='padding:0.5rem;border:1px solid #ddd;border-radius:4px;width:140px' title='ToF transition duration'></label>";
  inner += "      <label title=\"Maximum distance shown in ToF UI bar/graph.\">ToF UI Max Distance (mm)<br><input type='number' id='tofUiMaxDistanceMm' min='500' max='6000' step='50' value='3400' style='padding:0.5rem;border:1px solid #ddd;border-radius:4px;width:160px' title='ToF UI maximum distance'></label>";
  inner += "      <label title=\"I2C bus speed for thermal sensor. Higher may reduce frame time but can affect stability.\">I2C Clock Thermal (Hz)<br><input type='number' id='i2cClockThermalHz' min='400000' max='1000000' step='50000' value='800000' style='padding:0.5rem;border:1px solid #ddd;border-radius:4px;width:180px' title='I2C clock for thermal sensor'></label>";
  inner += "      <label title=\"I2C bus speed for ToF sensor.\">I2C Clock ToF (Hz)<br><input type='number' id='i2cClockToFHz' min='50000' max='400000' step='50000' value='100000' style='padding:0.5rem;border:1px solid #ddd;border-radius:4px;width:180px' title='I2C clock for ToF sensor'></label>";
  inner += "      <label title=\"Target frames per second for thermal capture.\">Thermal Target FPS<br><input type='number' id='thermalTargetFps' min='1' max='8' step='1' value='5' style='padding:0.5rem;border:1px solid #ddd;border-radius:4px;width:120px' title='Thermal frames per second'></label>";
  inner += "    </div>";
  inner += "    <div style='margin-top:1rem'><button class='btn' onclick=\"saveAdvancedSettings()\">Save Advanced</button></div>";
  inner += "  </div>";
  inner += "</div>";
  
  // Admin section (grouped panes: Active Sessions + Pending User Approvals)
  inner += "<div id='admin-section' style='display:none;background:#fff;border-radius:8px;padding:1.5rem;margin:1rem 0;border-left:4px solid #ffc107;color:#333'>";
  inner += "  <div style='font-size:1.2rem;font-weight:bold;margin-bottom:0.75rem;color:#333'>Admin Controls</div>";
  inner += "  <div style='display:grid;grid-template-columns:repeat(auto-fit,minmax(300px,1fr));gap:1rem'>";
  inner += "    <div style='background:#f8f9fa;border:1px solid #e5e7eb;border-radius:8px;padding:1rem'>";
  inner += "      <div style='font-weight:bold;margin-bottom:0.5rem;color:#333'>Active Sessions</div>";
  inner += "      <div style='color:#666;margin-bottom:0.75rem;font-size:0.9rem'>View all active sessions on this device. You can revoke any session.</div>";
  inner += "      <div id='sessions-list' style='min-height:24px;color:#333'>Loading...</div>";
  inner += "      <div style='margin-top:0.75rem'><button class='btn' onclick='refreshSessions()'>Refresh Sessions</button></div>";
  inner += "    </div>";
  inner += "    <div id='pending-panel' style='background:#f8f9fa;border:1px solid #e5e7eb;border-radius:8px;padding:1rem'>";
  inner += "      <div id='pending-title' style='font-weight:bold;margin-bottom:0.5rem;color:#333'>Pending User Approvals</div>";
  inner += "      <div id='pending-desc' style='color:#666;margin-bottom:0.75rem;font-size:0.9rem'>Users waiting for admin approval to access the system.</div>";
  inner += "      <div id='pending-users' style='margin-bottom:0.75rem'>Loading...</div>";
  inner += "      <button class='btn' onclick='refreshPendingUsers()'>Refresh Pending</button>";
  inner += "    </div>";
  inner += "  </div>";
  inner += "</div>";
  // Page controls
  inner += "<div style='text-align:center;margin-top:2rem'>";
  inner += "  <button class='btn' onclick='refreshSettings()'>Refresh Settings</button>";
  inner += "</div>";
  // Script chunks for reliability on ESP32
  // Chunk 1: Build tag and error handlers
  inner += "<script>(function(){ try {";
  inner += "window.settingsBuildTag='settings-minimal-chunked-v2';";
  inner += "console.log('[SETTINGS BUILD]', window.settingsBuildTag, new Date().toISOString());";
  inner += "window.__S=window.__S||{};";
  inner += "window.addEventListener('error',function(e){console.error('[SETTINGS ERROR]',e.message,'at',e.filename+':'+e.lineno+':'+e.colno);});";
  inner += "window.addEventListener('unhandledrejection',function(e){console.error('[SETTINGS PROMISE REJECTION]',e.reason);});";
  inner += "console.debug('[SETTINGS] Chunk1 ready');";
  inner += "} catch(err){console.error('[SETTINGS CHUNK1 FAIL]',err);} })();</script>";

  // Chunk 2: State and DOM helpers
  inner += "<script>(function(){ try {";
  inner += "window.$=function(id){return document.getElementById(id);};";
  inner += "__S.state={savedSSIDs:[],currentSSID:''};";
  inner += "__S.renderSettings=function(s){ try {";
  inner += "console.debug('[SETTINGS] render settings', s && typeof s=== 'object' ? Object.assign({}, s, {wifiNetworks: undefined}) : s);";
  inner += "__S.state.currentSSID=(s.wifiPrimarySSID||s.wifiSSID||''); $('wifi-ssid').textContent=__S.state.currentSSID;";
  inner += "var primary=__S.state.currentSSID||''; var list=Array.isArray(s.wifiNetworks)?s.wifiNetworks.map(function(n){return n&&n.ssid;}).filter(function(x){return !!x;}):[]; __S.state.savedSSIDs=[]; if(primary) __S.state.savedSSIDs.push(primary); if(list&&list.length) __S.state.savedSSIDs=__S.state.savedSSIDs.concat(list);";
  inner += "$('wifi-value').textContent = s.wifiAutoReconnect ? 'Enabled':'Disabled';";
  inner += "$('cli-value').textContent = s.cliHistorySize; $('cli-input').value = s.cliHistorySize;";
  inner += "$('wifi-btn').textContent = s.wifiAutoReconnect ? 'Disable':'Enable';";
  inner += "$('serial-value').textContent = s.outSerial ? 'Enabled':'Disabled'; $('serial-btn').textContent = s.outSerial ? 'Disable':'Enable';";
  inner += "$('web-value').textContent = s.outWeb ? 'Enabled':'Disabled'; $('web-btn').textContent = s.outWeb ? 'Disable':'Enable';";
  inner += "$('tft-value').textContent = s.outTft ? 'Enabled':'Disabled'; $('tft-btn').textContent = s.outTft ? 'Disable':'Enable';";
  inner += "if(s.thermalPollingMs!==undefined) $('thermalPollingMs').value=s.thermalPollingMs;";
  inner += "if(s.tofPollingMs!==undefined) $('tofPollingMs').value=s.tofPollingMs;";
  inner += "if(s.tofStabilityThreshold!==undefined) $('tofStabilityThreshold').value=s.tofStabilityThreshold;";
  inner += "if(s.thermalPaletteDefault) $('thermalPaletteDefault').value=s.thermalPaletteDefault;";
  inner += "if(s.thermalEWMAFactor!==undefined) $('thermalEWMAFactor').value=s.thermalEWMAFactor;";
  inner += "if(s.thermalTransitionMs!==undefined) $('thermalTransitionMs').value=s.thermalTransitionMs;";
  inner += "if(s.tofTransitionMs!==undefined) $('tofTransitionMs').value=s.tofTransitionMs;";
  inner += "if(s.tofUiMaxDistanceMm!==undefined) $('tofUiMaxDistanceMm').value=s.tofUiMaxDistanceMm;";
  inner += "if(s.i2cClockThermalHz!==undefined) $('i2cClockThermalHz').value=s.i2cClockThermalHz;";
  inner += "if(s.i2cClockToFHz!==undefined) $('i2cClockToFHz').value=s.i2cClockToFHz;";
  inner += "if(s.thermalTargetFps!==undefined) $('thermalTargetFps').value=s.thermalTargetFps;";
  inner += "var isAdm = (s && s.user && (s.user.isAdmin===true)) || (__S && __S.user && (__S.user.isAdmin===true)); var hasFeat = (__S && __S.features && __S.features.adminSessions===true); var admin = isAdm && hasFeat; var sec=document.getElementById('admin-section'); if(sec){ sec.style.display = admin ? 'block' : 'none'; } if(admin){ try{ if(typeof window.refreshSessions==='function'){ refreshSessions(); } if(typeof window.refreshPendingUsers==='function'){ refreshPendingUsers(); } }catch(e){} }";
  inner += "} catch(e){ console.error('[SETTINGS RENDER FAIL]',e); alert('Render error: '+e.message); } };";
  inner += "console.debug('[SETTINGS] Chunk2 ready');";
  inner += "} catch(err){console.error('[SETTINGS CHUNK2 FAIL]',err);} })();</script>";

  // Chunk 3: API helpers
  inner += "<script>(function(){ try {";
  inner += "window.refreshSettings=function(){ console.debug('[SETTINGS] refreshSettings'); fetch('/api/settings',{credentials:'same-origin'})";
  inner += ".then(function(r){ return r.text(); })";
  inner += ".then(function(t){ var d=null; try{ d=JSON.parse(t||'{}'); } catch(e){ console.error('[SETTINGS] settings JSON parse fail; first 120 chars:', (t||'').slice(0,120)); alert('Error fetching settings (not JSON) — possibly logged out.'); return; } if(d&&d.success){ try{ window.__S=window.__S||{}; __S.user=d.user||null; __S.features=d.features||null; }catch(_){ } __S.renderSettings(d.settings||{}); } else { alert('Error: '+(d&&d.error||'Unknown')); } })";
  inner += ".catch(function(e){ alert('Error: '+e.message); }); };";
  inner += "window.refreshSessions=function(){ var cont=$('sessions-list'); if(!cont){return;} cont.textContent='Loading...'; fetch('/admin/sessions',{credentials:'same-origin'})";
  inner += ".then(function(r){ return r.text(); })";
  inner += ".then(function(t){ var d=null; try{ d=JSON.parse(t||'{}'); } catch(e){ cont.textContent='Failed to parse sessions'; return; } if(!d||!d.success){ cont.textContent='Failed to load sessions'; return; } var arr=d.sessions||[]; if(arr.length===0){ cont.textContent='No active sessions'; return; } var html='<div style=\\'display:grid;grid-template-columns:repeat(auto-fit,minmax(260px,1fr));gap:0.5rem\\'>'; arr.forEach(function(s){ var badge=s.current?' (This device)':''; var encSid=encodeURIComponent(s.sid||''); html+='<div style=\\'background:#fff;border:1px solid #ddd;border-radius:6px;padding:0.5rem;display:flex;align-items:center;justify-content:space-between\\'>' + '<div><div style=\\'font-weight:bold\\'>'+ (s.ip||'-') + badge + '</div>' + '<div style=\\'color:#666;font-size:0.85rem\\'>Last seen '+ (s.lastSeen||0) +' ms; Expires '+ (s.expiresAt||0) +' ms</div></div>' + '<button class=\\'btn btn-revoke\\' data-sid=\\''+ encSid +'\\'>Revoke</button>' + '</div>'; }); html+='</div>'; cont.innerHTML=html; try{ var btns=cont.querySelectorAll('.btn-revoke'); btns.forEach(function(b){ b.addEventListener('click', function(){ try{ var sid=this && this.dataset ? this.dataset.sid : ''; if(sid){ revokeSession(decodeURIComponent(sid)); } }catch(_){ } }); }); }catch(_){ } })";
  inner += ".catch(function(e){ if(cont) cont.textContent='Error: '+e.message; }); };";
  inner += "window.revokeSession=function(sid){ if(!sid) return; if(!confirm('Revoke this session?')) return; fetch('/admin/sessions/revoke',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},credentials:'same-origin',body:'sid='+encodeURIComponent(sid)})";
  inner += ".then(function(r){ return r.text(); })";
  inner += ".then(function(t){ var d=null; try{ d=JSON.parse(t||'{}'); } catch(e){ alert('Revoke failed (not JSON)'); return; } if(!d||!d.success){ alert('Revoke failed: '+(d&&d.error||'Unknown')); return; } refreshSessions(); })";
  inner += ".catch(function(e){ alert('Error: '+e.message); }); };";
  // Pending users: fetch and render list
  inner += "window.refreshPendingUsers=function(){ var cont=$('pending-users'); if(!cont){return;} cont.textContent='Loading...'; fetch('/admin/pending',{credentials:'same-origin'})";
  inner += ".then(function(r){ return r.text(); })";
  inner += ".then(function(t){ var d=null; try{ d=JSON.parse(t||'{}'); } catch(e){ cont.textContent='Failed to parse pending list'; return; } if(!d||!d.success){ cont.textContent='Failed to load pending users'; return; } var arr=d.users||[]; if(arr.length===0){ cont.textContent='No pending users'; return; } var html='<div style=\\'display:grid;grid-template-columns:repeat(auto-fit,minmax(260px,1fr));gap:0.5rem\\'>'; arr.forEach(function(u){ var name=(u&&u.username)||''; var esc=name.replace(/&/g,'&amp;').replace(/</g,'&lt;'); var enc=encodeURIComponent(name); html+='<div style=\\'background:#fff;border:1px solid #ddd;border-radius:6px;padding:0.5rem;display:flex;align-items:center;justify-content:space-between\\'>' + '<div><div style=\\'font-weight:bold\\'>'+ esc + '</div><div style=\\'color:#666;font-size:0.85rem\\'>Awaiting approval</div></div>' + '<div><button class=\\'btn btn-approve\\' style=\\'margin-right:6px\\' data-user=\\''+ enc +'\\'>Approve</button>' + '<button class=\\'btn btn-deny\\' data-user=\\''+ enc +'\\'>Deny</button></div>' + '</div>'; }); html+='</div>'; cont.innerHTML=html; try{ var a=cont.querySelectorAll('.btn-approve'); a.forEach(function(b){ b.addEventListener('click', function(){ try{ var u=this&&this.dataset?this.dataset.user:''; if(u){ approveUser(decodeURIComponent(u)); } }catch(_){ } }); }); var dBtns=cont.querySelectorAll('.btn-deny'); dBtns.forEach(function(b){ b.addEventListener('click', function(){ try{ var u=this&&this.dataset?this.dataset.user:''; if(u){ denyUser(decodeURIComponent(u)); } }catch(_){ } }); }); }catch(_){ } })";
  inner += ".catch(function(e){ if(cont) cont.textContent='Error: '+e.message; }); };";
  // Approve/Deny actions
  inner += "window.approveUser=function(username){ if(!username) return; if(!confirm('Approve '+username+'?')) return; fetch('/admin/approve',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},credentials:'same-origin',body:'username='+encodeURIComponent(username)})";
  inner += ".then(function(r){ return r.text(); })";
  inner += ".then(function(t){ var d=null; try{ d=JSON.parse(t||'{}'); } catch(e){ alert('Approve failed (not JSON)'); return; } if(!d||!d.success){ alert('Approve failed: '+(d&&d.error||'Unknown')); return; } refreshPendingUsers(); })";
  inner += ".catch(function(e){ alert('Error: '+e.message); }); };";
  inner += "window.denyUser=function(username){ if(!username) return; if(!confirm('Deny '+username+'?')) return; fetch('/admin/deny',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},credentials:'same-origin',body:'username='+encodeURIComponent(username)})";
  inner += ".then(function(r){ return r.text(); })";
  inner += ".then(function(t){ var d=null; try{ d=JSON.parse(t||'{}'); } catch(e){ alert('Deny failed (not JSON)'); return; } if(!d||!d.success){ alert('Deny failed: '+(d&&d.error||'Unknown')); return; } refreshPendingUsers(); })";
  inner += ".catch(function(e){ alert('Error: '+e.message); }); };";
  inner += "window.updateSetting=function(name,val){ fetch('/api/settings/update',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},credentials:'same-origin',body:'setting='+encodeURIComponent(name)+'&value='+encodeURIComponent(val)})";
  inner += ".then(function(r){ return r.text(); })";
  inner += ".then(function(t){ var d=null; try{ d=JSON.parse(t||'{}'); } catch(e){ console.error('[SETTINGS] updateSetting JSON parse fail; first 120 chars:', (t||'').slice(0,120)); alert('Update failed (not JSON)'); return; } if(d&&d.success){ alert('Updated!'); refreshSettings(); } else { alert('Error: '+(d&&d.error||'Unknown')); } })";
  inner += ".catch(function(e){ alert('Error: '+e.message); }); };";
  inner += "window.updateSettingReturn=function(name,val){ return fetch('/api/settings/update',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},credentials:'same-origin',body:'setting='+encodeURIComponent(name)+'&value='+encodeURIComponent(val)})";
  inner += ".then(function(r){ return r.text(); })";
  inner += ".then(function(t){ var d=null; try{ d=JSON.parse(t||'{}'); } catch(e){ throw new Error('not-json'); } if(!d.success){ throw new Error(d.error||'fail'); } return d; }); };";
  inner += "console.debug('[SETTINGS] Chunk3 ready');";
  inner += "} catch(err){console.error('[SETTINGS CHUNK3 FAIL]',err);} })();</script>";

  // Chunk 4: UI actions
  inner += "<script>(function(){ try {";
  inner += "window.updateNonAdvancedSensorsUI=function(){ var p1=updateSettingReturn('thermalPollingMs',$('thermalPollingMs').value); var p2=updateSettingReturn('tofPollingMs',$('tofPollingMs').value); var p3=updateSettingReturn('tofStabilityThreshold',$('tofStabilityThreshold').value); var p4=updateSettingReturn('thermalPaletteDefault',$('thermalPaletteDefault').value); Promise.all([p1,p2,p3,p4]).then(function(){ alert('Sensors UI settings saved. Reload Sensors page to apply.'); }).catch(function(){ alert('One or more settings failed to save.'); }); };";
  inner += "window.toggleWifi=function(){ var cur = ($('wifi-value').textContent==='Enabled')?1:0; updateSetting('wifiAutoReconnect',cur?0:1); };";
  inner += "window.updateCliHistory=function(){ var v=parseInt($('cli-input').value); if(v<1){ alert('Must be at least 1'); return; } updateSetting('cliHistorySize',v); };";
  inner += "window.toggleOutput=function(setting){ var valueId=setting.replace('out','').toLowerCase()+'-value'; var cur = (document.getElementById(valueId).textContent==='Enabled')?1:0; updateSetting(setting,cur?0:1); };";
  inner += "window.disconnectWifi=function(){ if(confirm('Are you sure you want to disconnect from WiFi? You may lose connection to this device.')){ fetch('/api/cli',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},credentials:'same-origin',body:'cmd='+encodeURIComponent('wifidisconnect')}).then(function(r){return r.text();}).then(function(t){ alert(t||'Disconnected'); }).catch(function(e){ alert('Error: '+e.message); }); } };";
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
  inner += "          html+='<div style=\\'background:#fff;border:1px solid '+border+';border-radius:6px;padding:0.5rem;display:flex;align-items:center;justify-content:space-between\\'>' +";
  inner += "               '<div><div style=\\'font-weight:bold\\'>'+ssid+' '+badge+'</div><div style=\\'color:#666;font-size:0.85rem\\'>RSSI '+rssi+' | CH '+ch+'</div></div>' +";
  inner += "               '<button class=\\'btn\\' data-ssid=\\''+esc+'\\' data-locked=\\''+needsPass+'\\' onclick=\\'(function(b){selectSsid(decodeURIComponent(b.dataset.ssid), b.dataset.locked===\"true\");})(this)\\'>Select '+lock+'</button>' +";
  inner += "               '</div>';";
  inner += "        });";
  inner += "        html+='</div>';";
  inner += "      }";
  inner += "      html+='<div style=\\'margin-top:0.5rem\\'><button class=\\'btn\\' onclick=\\'toggleManualConnect()\\'>Hidden network...</button></div>';";
  inner += "      container.innerHTML=html;";
  inner += "    }).catch(function(e){ container.textContent='Scan failed: '+e.message; });";
  inner += "};";
  // Helper to show selection panel for a scanned SSID
  inner += "window.selectSsid=function(ssid, needsPass){ try{ var p=$('wifi-connect-panel'); if(p){ p.style.display='block'; } var s=$('sel-ssid'); if(s){ s.textContent=ssid||''; } var inp=$('sel-pass'); if(inp){ inp.value=''; if(needsPass){ inp.placeholder='WiFi password'; inp.disabled=false; } else { inp.placeholder='(open network)'; inp.disabled=false; } } }catch(e){ console.error('[SETTINGS] selectSsid fail', e); } };";
  // Connect to selected scanned SSID: wifiadd then wificonnect
  inner += "window.connectSelected=function(){ try{ var ssidEl=$('sel-ssid'); var passEl=$('sel-pass'); var ssid=(ssidEl&&ssidEl.textContent)||''; var pass=(passEl&&passEl.value)||''; if(!ssid){ alert('No SSID selected'); return; } var cmd1='wifiadd '+ssid+' '+pass+' 1 0'; fetch('/api/cli',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},credentials:'same-origin',body:'cmd='+encodeURIComponent(cmd1)})";
  inner += ".then(function(r){ return r.text(); })";
  inner += ".then(function(t1){ return fetch('/api/cli',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},credentials:'same-origin',body:'cmd='+encodeURIComponent('wificonnect')}); })";
  inner += ".then(function(r){ return r.text(); })";
  inner += ".then(function(t2){ try{ alert(t2||'Connect attempted'); }catch(_){ } try{ refreshSettings(); }catch(_){ } })";
  inner += ".catch(function(e){ alert('Connect failed: '+e.message); }); }catch(e){ alert('Connect error: '+e.message); } };";
  // Manual connect for hidden networks
  inner += "window.connectManual=function(){ try{ var ssid=($('manual-ssid')&&$('manual-ssid').value)||''; var pass=($('manual-pass')&&$('manual-pass').value)||''; if(!ssid){ alert('Enter SSID'); return; } var cmd1='wifiadd '+ssid+' '+pass+' 1 1'; fetch('/api/cli',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},credentials:'same-origin',body:'cmd='+encodeURIComponent(cmd1)})";
  inner += ".then(function(r){ return r.text(); })";
  inner += ".then(function(t1){ return fetch('/api/cli',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},credentials:'same-origin',body:'cmd='+encodeURIComponent('wificonnect')}); })";
  inner += ".then(function(r){ return r.text(); })";
  inner += ".then(function(t2){ try{ alert(t2||'Connect attempted'); }catch(_){ } try{ refreshSettings(); }catch(_){ } })";
  inner += ".catch(function(e){ alert('Connect failed: '+e.message); }); }catch(e){ alert('Connect error: '+e.message); } };";
  inner += "window.toggleManualConnect=function(){ var p=$('wifi-manual-panel'); if(!p) return; p.style.display=(p.style.display==='none'||!p.style.display)?'block':'none'; };";
  inner += "window.toggleAdvanced=function(){ try{ var sec=$('advanced-section'); var btn=$('btn-advanced-toggle'); if(!sec||!btn) return; var show=(sec.style.display==='none'||!sec.style.display); sec.style.display=show?'block':'none'; btn.textContent=show?'Hide Advanced':'Show Advanced'; }catch(e){ console.error('[SETTINGS] toggleAdvanced fail', e); } };";
  // Advanced settings saver
  inner += "window.saveAdvancedSettings=function(){ try{";
  inner += "  var ewma=parseFloat(($('thermalEWMAFactor')||{}).value||''); if(isNaN(ewma)||ewma<0||ewma>1){ alert('EWMA must be 0..1'); return; }";
  inner += "  var tTrans=parseInt(($('thermalTransitionMs')||{}).value||'0'); if(isNaN(tTrans)||tTrans<0||tTrans>500){ alert('Thermal transition must be 0..500'); return; }";
  inner += "  var tofTrans=parseInt(($('tofTransitionMs')||{}).value||'0'); if(isNaN(tofTrans)||tofTrans<0||tofTrans>500){ alert('ToF transition must be 0..500'); return; }";
  inner += "  var tofMax=parseInt(($('tofUiMaxDistanceMm')||{}).value||'0'); if(isNaN(tofMax)||tofMax<500||tofMax>6000){ alert('ToF UI max distance must be 500..6000'); return; }";
  inner += "  var i2cTherm=parseInt(($('i2cClockThermalHz')||{}).value||'0'); if(isNaN(i2cTherm)||i2cTherm<400000||i2cTherm>1000000){ alert('I2C Clock Thermal must be 400k..1,000k'); return; }";
  inner += "  var i2cTof=parseInt(($('i2cClockToFHz')||{}).value||'0'); if(isNaN(i2cTof)||i2cTof<50000||i2cTof>400000){ alert('I2C Clock ToF must be 50k..400k'); return; }";
  inner += "  var fps=parseInt(($('thermalTargetFps')||{}).value||'0'); if(isNaN(fps)||fps<1||fps>8){ alert('Thermal FPS must be 1..8'); return; }";
  inner += "  var p=[];";
  inner += "  p.push(updateSettingReturn('thermalEWMAFactor', ewma));";
  inner += "  p.push(updateSettingReturn('thermalTransitionMs', tTrans));";
  inner += "  p.push(updateSettingReturn('tofTransitionMs', tofTrans));";
  inner += "  p.push(updateSettingReturn('tofUiMaxDistanceMm', tofMax));";
  inner += "  p.push(updateSettingReturn('i2cClockThermalHz', i2cTherm));";
  inner += "  p.push(updateSettingReturn('i2cClockToFHz', i2cTof));";
  inner += "  p.push(updateSettingReturn('thermalTargetFps', fps));";
  inner += "  Promise.all(p).then(function(){ alert('Advanced settings saved. Some changes may apply on next sensor start.'); try{ refreshSettings(); }catch(_){ } }).catch(function(e){ alert('Failed to save one or more advanced settings'); console.error('[SETTINGS] saveAdvancedSettings fail', e); });";
  inner += "}catch(e){ alert('Save error: '+e.message); } };";
  inner += "window.onload=function(){ console.debug('[SETTINGS] onload'); try{ refreshSettings(); }catch(e){ console.error('[SETTINGS onload refresh fail]',e);} };";
  inner += "console.debug('[SETTINGS] Chunk5 ready');";
  inner += "} catch(err){console.error('[SETTINGS CHUNK5 FAIL]',err);} })();</script>";
  
  return htmlShellWithNav(username, "settings", inner);
}
 
#endif
