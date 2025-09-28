#ifndef WEB_CLI_H
#define WEB_CLI_H

String getCLIPage(const String& username) {
  String inner;
  inner.reserve(6000);
  inner += "<style>";
  // Constrain page to viewport on CLI page so only CLI output scrolls
  inner += "html, body { height: 100vh; overflow: hidden; }";
  inner += ".cli-container {";
  inner += "  background: rgba(0, 0, 0, 0.3);";
  inner += "  border-radius: 15px;";
  inner += "  padding: 12px;";
  inner += "  backdrop-filter: blur(10px);";
  inner += "  border: 1px solid rgba(255, 255, 255, 0.1);";
  inner += "  box-shadow: 0 20px 40px rgba(0, 0, 0, 0.2);";
  inner += "  font-family: 'Courier New', monospace;";
  inner += "  width: 95%;";
  inner += "  max-width: 1400px;";
  inner += "  margin: 0 auto;";
  inner += "  height: 62vh;"; /* ~25% taller */
  inner += "  max-height: 75vh;";
  inner += "  min-height: 45vh;";
  inner += "  overflow: hidden;"; /* prevent page scroll; inner will scroll */
  inner += "  display: flex;";
  inner += "  flex-direction: column;";
  inner += "}";
  inner += ".cli-header {";
  inner += "  text-align: center;";
  inner += "  font-size: 1.1em;";
  inner += "  margin-bottom: 6px;";
  inner += "  color: #4CAF50;";
  inner += "  font-weight: bold;";
  inner += "}";
  inner += ".cli-output {";
  inner += "  background: rgba(0, 0, 0, 0.5);";
  inner += "  border: 1px solid #333;";
  inner += "  border-radius: 5px;";
  inner += "  padding: 8px;";
  inner += "  flex: 1 1 auto;"; /* fill remaining space */
  inner += "  min-height: 60px;";
  inner += "  overflow-y: auto;";
  inner += "  margin-bottom: 6px;";
  inner += "  font-size: 14px;";
  inner += "  line-height: 1.4;";
  inner += "  white-space: pre-wrap;";
  inner += "  color: #fff;";
  inner += "  scroll-behavior: smooth;";
  inner += "}";
  inner += ".cli-input-container {";
  inner += "  display: flex;";
  inner += "  align-items: center;";
  inner += "  gap: 8px;";
  inner += "  flex: 0 0 auto;"; /* keep input row visible */
  inner += "  min-height: 30px;";
  inner += "  margin-top: 2px;";
  inner += "}";
  inner += ".cli-prompt {";
  inner += "  color: #4CAF50;";
  inner += "  font-weight: bold;";
  inner += "}";
  inner += ".cli-input {";
  inner += "  flex: 1 1 260px;";
  inner += "  min-width: 140px;";
  inner += "  width: auto;";
  inner += "  background: rgba(255,255,255,0.08);";
  inner += "  border: 1px solid rgba(255,255,255,0.25);";
  inner += "  color: #fff;";
  inner += "  font-family: 'Courier New', monospace;";
  inner += "  font-size: 14px;";
  inner += "  outline: none;";
  inner += "  padding: 6px 8px;";
  inner += "  height: 34px;";
  inner += "  margin-bottom: 0;";
  inner += "  display: block;";
  inner += "  position: relative;";
  inner += "  z-index: 2;";
  inner += "  pointer-events: auto;";
  inner += "  box-sizing: border-box;";
  inner += "}";
  inner += ".help-text { display:none; }";
  // Widen outer shell (the translucent white card) on larger screens for CLI
  inner += "@media (min-width: 1200px) { .content { max-width: 1600px; } }";
  inner += "@media (min-width: 1600px) { .content { max-width: 90vw; } }";
  inner += "@media (max-height: 820px) { .cli-container { height: 60vh; max-height: 68vh; padding: 8px; } .cli-header { font-size: 1.0em; margin-bottom: 4px; } .cli-output { padding: 6px; margin-bottom: 4px; min-height: 50px; } .cli-input-container { min-height: 28px; } }";
  inner += "@media (max-height: 700px) { .cli-container { height: 55vh; max-height: 60vh; padding: 6px; } .cli-header { font-size: 0.95em; margin-bottom: 4px; } .cli-output { padding: 4px; margin-bottom: 4px; min-height: 40px; } .cli-input-container { gap: 6px; min-height: 26px; } }";
  inner += "</style>";
  
  inner += "<div class='cli-container'>";
  inner += "  <div class='cli-header'>HardwareOne Command Line Interface</div>";
  inner += "  <script>try{console.log('[CLI] Section Header ready');}catch(_){}</script>";
  inner += "  <div id='cli-output' class='cli-output'></div>";
  inner += "  <script>try{console.log('[CLI] Section Output ready');}catch(_){}</script>";
  inner += "  <div class='cli-input-container'>";
  inner += "    <span class='cli-prompt'>$</span>";
  inner += "    <input type='text' id='cli-input' class='cli-input' placeholder='Enter command...' autocomplete='off'>";
  inner += "    <button id='cli-exec' class='btn'>Execute</button>";
  inner += "  </div>";
  inner += "  <script>try{console.log('[CLI] Section Input ready');}catch(_){}</script>";
  inner += "  <div class='help-text'>Press Enter to execute commands | Type 'help' for command list | Authenticated as: " + username + "</div>";
  inner += "  <script>try{console.log('[CLI] Section HelpText ready');}catch(_){}</script>";
  inner += "</div>";
  

  inner += "<script>";
  inner += "try{console.log('[CLI] Core init start');}catch(_){}";
  inner += "var cliInput = document.getElementById('cli-input');";
  inner += "var cliOutput = document.getElementById('cli-output');";
  inner += "var cliExecBtn = document.getElementById('cli-exec');";
  inner += "window.addEventListener('error', function(e){ try { if(cliOutput){ cliOutput.textContent += ('[JS Error] ' + e.message + '\\n'); } } catch(_){} });";
  inner += "var commandHistory = []; var historyIndex = -1; var currentCommand=''; var outputHistory=''; var inHelp=false; var outputBackup=''; var scrolledOnce=false;";
  inner += "if(cliExecBtn){ cliExecBtn.addEventListener('click', function(){ if(window.executeCommand) executeCommand(); }); }";
  inner += "if(cliInput){ cliInput.addEventListener('keydown', function(e){ if(e.key==='Enter' && window.executeCommand){ executeCommand(); } }); }";
  inner += "try{console.log('[CLI] Core init ready');}catch(_){}";
  inner += "</script>";

  inner += "<script>";
  inner += "try{console.log('[CLI] Session/init start');}catch(_){}";
  inner += "try{ commandHistory = JSON.parse(localStorage.getItem('cliHistory') || '[]'); }catch(_){ commandHistory = []; }";
  inner += "historyIndex = -1; currentCommand = '';";
  inner += "try{ inHelp = JSON.parse(localStorage.getItem('cliInHelp') || 'false'); }catch(_){ inHelp=false; }";
  inner += "try{ outputBackup = localStorage.getItem('cliOutputHistoryBackup') || ''; }catch(_){ outputBackup=''; }";
  // Helpers to process ESC clear and ANSI sequences from server output
  inner += "function __stripAnsi(s){ try{ return (s||'').replace(/\x1B\\[[0-9;]*[A-Za-z]/g, ''); }catch(_){ return s; } }";
  inner += "function __applyClear(s){ try{ var ESC=String.fromCharCode(27); var clearSeq=ESC+'[2J'+ESC+'[H'; var idx=(s||'').lastIndexOf(clearSeq); if(idx!==-1){ return s.substring(idx+clearSeq.length); } return s; }catch(_){ return s; } }";
  inner += "// Bootstrap logs on load (no SSE)\n";
  inner += "try{ fetch('/api/cli/logs', { credentials: 'same-origin', cache:'no-store' })\n";
  inner += ".then(function(r){ return r.text(); })\n";
  inner += ".then(function(text){ var t=__applyClear(text); t=__stripAnsi(t); if(cliOutput){ cliOutput.textContent = t || ''; try{ localStorage.setItem('cliOutputHistory', cliOutput.textContent); }catch(_){} try{ if(!scrolledOnce){ cliOutput.scrollTop = cliOutput.scrollHeight; scrolledOnce = true; } }catch(_){} } })\n";
  inner += ".catch(function(e){ try { console.debug('[CLI] logs fetch error: ' + e.message); } catch(_){} }); }catch(_){ }";
  inner += "// Periodic polling for CLI logs (no SSE).\n";
  inner += "try {\n";
  inner += "  if (window.__cliPoller) { try{ clearInterval(window.__cliPoller); }catch(_){} }\n";
  inner += "  window.__cliPoller = setInterval(function(){\n";
  inner += "    fetch('/api/cli/logs', { credentials: 'same-origin', cache: 'no-store' })\n";
  inner += "      .then(function(r){ if(r.status===401){ if(window.__cliPoller){ clearInterval(window.__cliPoller); window.__cliPoller=null; } return ''; } return r.text(); })\n";
  inner += "      .then(function(text){ if(text){ var t=__applyClear(text); t=__stripAnsi(t); if(cliOutput){ cliOutput.textContent = t || ''; try{ localStorage.setItem('cliOutputHistory', cliOutput.textContent); }catch(_){} } } })\n";
  inner += "      .catch(function(_){ });\n";
  inner += "  }, 500);\n"; // Changed from 1000 to 500
  inner += "} catch(e) { try{ console.debug('[CLI] polling init error: ' + e.message); }catch(_){} }";
  inner += "try{ window.addEventListener('beforeunload', function(){ try{ if(window.__cliPoller){ clearInterval(window.__cliPoller); window.__cliPoller=null; } }catch(_){ } }, {capture:true}); }catch(_){ }";
  inner += "if(cliInput){ cliInput.addEventListener('keydown', function(e){";
  inner += "  if (e.key === 'ArrowUp') { e.preventDefault(); if (historyIndex === -1) { currentCommand = cliInput.value; } if (historyIndex < commandHistory.length - 1) { historyIndex++; cliInput.value = commandHistory[commandHistory.length - 1 - historyIndex]; } }";
  inner += "  else if (e.key === 'ArrowDown') { e.preventDefault(); if (historyIndex > 0) { historyIndex--; cliInput.value = commandHistory[commandHistory.length - 1 - historyIndex]; } else if (historyIndex === 0) { historyIndex = -1; cliInput.value = currentCommand; } }";
  inner += "}); }";
  inner += "try{console.log('[CLI] Session/init ready');}catch(_){}";
  inner += "</script>";

  inner += "<script>";
  inner += "try{console.log('[CLI] Execute handler ready');}catch(_){}";
  inner += "function executeCommand(){";
  inner += "  try { console.debug('[CLI] execute start'); } catch(_){}";
  inner += "  var command = (cliInput && cliInput.value ? cliInput.value : '').trim();";
  inner += "  if (!command) return;";
  inner += "  var lower = command.toLowerCase();";
  inner += "  var exitingHelp = false;";
  inner += "  if (!inHelp && (lower === 'help' || lower === 'menu' || lower === 'cli help')) {";
  inner += "    outputBackup = cliOutput ? cliOutput.textContent : '';";
  inner += "    try{ localStorage.setItem('cliOutputHistoryBackup', outputBackup); }catch(_){}";
  inner += "    inHelp = true; try{ localStorage.setItem('cliInHelp', 'true'); }catch(_){}";
  inner += "  } else if (inHelp && (lower === 'exit' || lower === 'back' || lower === 'q' || lower === 'quit')) {";
  inner += "    exitingHelp = true;";
  inner += "  }";
  inner += "  if (commandHistory[commandHistory.length - 1] !== command) {";
  inner += "    commandHistory.push(command); if (commandHistory.length > 50) commandHistory.shift();";
  inner += "    try{ localStorage.setItem('cliHistory', JSON.stringify(commandHistory)); }catch(_){}";
  inner += "  }";
  inner += "  historyIndex = -1; currentCommand = '';";
  inner += "  if (cliOutput) { cliOutput.textContent += ('$ ' + command + '\\n'); }";
  inner += "  try { console.debug('[CLI] fetch start: ' + command); } catch(_){}";
  inner += "  fetch('/api/cli', { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, credentials: 'same-origin', body: 'cmd=' + encodeURIComponent(command) })";
  inner += "  .then(function(r){ try{ console.debug('[CLI] fetch status: ' + r.status); }catch(_){} return r.text(); })";
  inner += "  .then(function(result){ try { console.debug('[CLI] fetch ok, len=' + (result ? result.length : 0)); } catch(_){} var ESC=String.fromCharCode(27); var clearSeq = ESC+'[2J'+ESC+'[H'; if (result && result.indexOf(clearSeq) !== -1) { var cleanResult = result.split(clearSeq).join(''); if (exitingHelp && inHelp) { if (cliOutput) { cliOutput.textContent = outputBackup || ''; } inHelp = false; try{ localStorage.setItem('cliInHelp','false'); localStorage.removeItem('cliOutputHistoryBackup'); }catch(_){} if (cleanResult && cliOutput) { cliOutput.textContent += cleanResult; } try{ localStorage.setItem('cliOutputHistory', cliOutput ? cliOutput.textContent : ''); }catch(_){} } else { if (cliOutput) { cliOutput.textContent = cleanResult; try{ localStorage.setItem('cliOutputHistory', cliOutput.textContent); }catch(_){} } } } else { if (cliOutput) { cliOutput.textContent += result + '\\n'; try{ localStorage.setItem('cliOutputHistory', cliOutput.textContent); }catch(_){} } } if (cliInput) { cliInput.value=''; cliInput.focus(); } })";
  inner += "  .catch(function(e){ try { console.debug('[CLI] fetch error: ' + e.message); } catch(_){} var errorMsg='Error: ' + e.message + '\\n'; if (cliOutput) { cliOutput.textContent += errorMsg; try{ localStorage.setItem('cliOutputHistory', cliOutput.textContent); }catch(_){} } if (cliInput) { cliInput.value=''; cliInput.focus(); } });";
  inner += "}";
  inner += "try { console.debug('[CLI] EOF'); } catch(_){}";
  inner += "function clearHistory() {";
  inner += "  localStorage.removeItem('cliHistory');";
  inner += "  localStorage.removeItem('cliOutputHistory');";
  inner += "  commandHistory = [];";
  inner += "  cliOutput.textContent = '';";
  inner += "  historyIndex = -1;";
  inner += "}";
  inner += "</script>";
  
  return htmlShellWithNav(username, "cli", inner);
}


#endif
