#ifndef WEB_AUTOMATIONS_H
#define WEB_AUTOMATIONS_H

String getAutomationsPage(const String& username) {
  String inner;
  inner += "<h2>Automations</h2>";
  inner += "<p>Create schedules that run commands automatically. All authenticated users can view and interact with automations.</p>";

  // Download from GitHub section
  inner += "<div style='background:#f8f9fa;border:1px solid #dee2e6;border-radius:8px;padding:1rem;margin:1rem 0'>";
  inner += "<h3 style='margin-top:0;color:#000'>📥 Download from GitHub</h3>";
  inner += "<p style='margin:0.5rem 0;color:#666'>Download automation scripts from GitHub repositories:</p>";
  inner += "<div style='display:flex;gap:0.5rem;align-items:center;flex-wrap:wrap'>";
  inner += "<input id='github_url' placeholder='GitHub raw URL (e.g., https://raw.githubusercontent.com/user/repo/main/automation.json)' style='flex:1;min-width:300px;padding:0.5rem'>";
  inner += "<input id='github_name' placeholder='Optional: Custom name' style='width:150px;padding:0.5rem'>";
  inner += "<button onclick='downloadFromGitHub()' class='btn'>Download</button>";
  inner += "</div>";
  inner += "<div id='download_status' style='margin-top:0.5rem;font-size:0.9em'></div>";
  inner += "<details style='margin-top:0.5rem'>";
  inner += "<summary style='cursor:pointer;color:#007bff'>ℹ️ How to use</summary>";
  inner += "<div style='margin-top:0.5rem;padding:0.5rem;background:#fff;border-radius:4px;font-size:0.9em;color:#000'>";
  inner += "<p><strong>Steps:</strong></p>";
  inner += "<ol>";
  inner += "<li>Find an automation JSON file on GitHub</li>";
  inner += "<li>Click 'Raw' to get the raw file URL</li>";
  inner += "<li>Paste the URL above and click Download</li>";
  inner += "</ol>";
  inner += "<p><strong>Example automation format:</strong></p>";
  inner += "<pre style='background:#f8f9fa;padding:0.5rem;border-radius:4px;font-size:0.8em'>{";
  inner += "\\n  \"name\": \"Morning Lights\",";
  inner += "\\n  \"type\": \"atTime\",";
  inner += "\\n  \"atTime\": \"07:00\",";
  inner += "\\n  \"days\": \"1234567\",";
  inner += "\\n  \"command\": \"broadcast Good morning!\",";
  inner += "\\n  \"enabled\": true";
  inner += "\\n}</pre>";
  inner += "</div>";
  inner += "</details>";
  inner += "</div>";

  // Create form
  inner += "<div id='auto_form' style='background:#eef6ff;border:1px solid #cfe2ff;border-radius:8px;padding:1rem;margin:1rem 0'>";
  inner += "<style>\n"
           "#auto_form .row-inline{display:flex;align-items:center;gap:0.5rem;flex-wrap:wrap;}\n"
           "#auto_form .row-inline .input-tall{height:32px;line-height:32px;box-sizing:border-box;}\n"
           "#auto_form .row-inline .btn,#auto_form .row-inline .btn-small{height:32px;line-height:32px;padding:0 10px;display:inline-flex;align-items:center;margin:0;box-sizing:border-box;font-size:14px;}\n"
           "#auto_form input[type=time].input-tall{height:32px;line-height:32px;}\n"
           "#auto_form .row-inline input,#auto_form .row-inline select{margin:0;}\n"
           "</style>";
  inner += "<h3 style='margin-top:0;color:#000'>Create Automation</h3>";
  inner += "<div style='display:flex;flex-wrap:wrap;gap:0.5rem;align-items:center'>";
  inner += "<input id='a_name' class='input-tall' placeholder='Name' style='flex:1;min-width:160px'>";
  inner += "<select id='a_type' class='input-tall' onchange='autoTypeChanged()'>";
  inner += "  <option value='atTime'>At Time</option>";
  inner += "  <option value='afterDelay'>After Delay</option>";
  inner += "  <option value='interval'>Interval</option>";
  inner += "</select>";
  // atTime group
  inner += "<div id='grp_atTime'>";
  inner += "<div style='display:flex;flex-direction:column;gap:0.5rem'>";
  inner += "  <div class='row-inline'>";
  inner += "    <label style='font-size:0.9em;color:#000'>Repeat:</label>";
  inner += "    <select id='a_recur' class='input-tall' onchange='recurChanged()'>";
  inner += "      <option value='daily' selected>Daily</option>";
  inner += "      <option value='weekly'>Weekly</option>";
  inner += "      <option value='monthly'>Monthly</option>";
  inner += "      <option value='yearly'>Yearly</option>";
  inner += "    </select>";
  inner += "  </div>";
  inner += "  <div style='margin-top:0.5rem'>";
  inner += "    <label style='font-size:0.9em;color:#000;margin-bottom:0.25rem;display:block'>Times:</label>";
  inner += "    <div class='row-inline'>";
  inner += "      <input type='time' class='time-input input-tall' placeholder='HH:MM' style='width:120px;height:32px;line-height:32px'>";
  inner += "      <button id='btn_add_time' type='button' class='btn btn-small' onclick='addTimeField()' style='height:32px;line-height:32px;padding:0 10px;box-sizing:border-box;font-size:14px;display:inline-flex;align-items:center;margin:0'>+ Add Time</button>";
  inner += "      <button id='btn_remove_main_time' type='button' class='btn btn-small' onclick='removeMainTimeField()' style='height:32px;line-height:32px;padding:0 10px;box-sizing:border-box;font-size:14px;display:inline-flex;align-items:center;margin:0;visibility:hidden'>Remove</button>";
  inner += "    </div>";
  inner += "  </div>";
  inner += "  <div id='time_fields' style='margin-top:0.25rem'></div>";
  inner += "</div>";
  inner += "<div id='dow_wrap' style='display:none;flex-direction:column;gap:0.25rem;margin-top:0.5rem;color:#000;margin-left:0;padding-left:0'>";
  inner += "  <div style='display:flex;align-items:center;flex-wrap:wrap;margin:0'>";
  inner += "    <span style='font-size:0.9em;color:#000;margin:0;margin-right:1rem'>Days of week:</span>";
  inner += "    <label style='display:flex;align-items:center;gap:0;margin-right:2.5rem'><input type='checkbox' id='day_mon' value='mon' style='margin:0;padding:0;vertical-align:middle'><span style='display:inline-block;margin-left:-2px;font-kerning:none'>Mon</span></label>";
  inner += "    <label style='display:flex;align-items:center;gap:0;margin-right:2.5rem'><input type='checkbox' id='day_tue' value='tue' style='margin:0;padding:0;vertical-align:middle'><span style='display:inline-block;margin-left:-2px;font-kerning:none'>Tue</span></label>";
  inner += "    <label style='display:flex;align-items:center;gap:0;margin-right:2.5rem'><input type='checkbox' id='day_wed' value='wed' style='margin:0;padding:0;vertical-align:middle'><span style='display:inline-block;margin-left:-2px;font-kerning:none'>Wed</span></label>";
  inner += "    <label style='display:flex;align-items:center;gap:0;margin-right:2.5rem'><input type='checkbox' id='day_thu' value='thu' style='margin:0;padding:0;vertical-align:middle'><span style='display:inline-block;margin-left:-2px;font-kerning:none'>Thu</span></label>";
  inner += "    <label style='display:flex;align-items:center;gap:0;margin-right:2.5rem'><input type='checkbox' id='day_fri' value='fri' style='margin:0;padding:0;vertical-align:middle'><span style='display:inline-block;margin-left:-2px;font-kerning:none'>Fri</span></label>";
  inner += "    <label style='display:flex;align-items:center;gap:0;margin-right:2.5rem'><input type='checkbox' id='day_sat' value='sat' style='margin:0;padding:0;vertical-align:middle'><span style='display:inline-block;margin-left:-2px;font-kerning:none'>Sat</span></label>";
  inner += "    <label style='display:flex;align-items:center;gap:0;margin-right:0'><input type='checkbox' id='day_sun' value='sun' style='margin:0;padding:0;vertical-align:middle'><span style='display:inline-block;margin-left:-2px;font-kerning:none'>Sun</span></label>";
  inner += "  </div>";
  inner += "  </div>";
  inner += "</div>";
  inner += "</div>";

  // afterDelay group
  inner += "<div id='grp_afterDelay' class='vis-gone'>";
  inner += "<div class='row-inline' style='gap:0.3rem'>";
  inner += "  <input id='a_delay' class='input-tall' placeholder='Delay' style='width:160px'>";
  inner += "  <select id='a_delay_unit' class='input-tall'>";
  inner += "    <option value='ms' selected>ms</option>";
  inner += "    <option value='s'>seconds</option>";
  inner += "    <option value='min'>minutes</option>";
  inner += "    <option value='hr'>hours</option>";
  inner += "    <option value='day'>days</option>";
  inner += "  </select>";
  inner += "</div>";
  inner += "</div>";

  // interval group
  inner += "<div id='grp_interval' class='vis-gone row-inline' style='gap:0.3rem'>";
  inner += "  <input id='a_interval' class='input-tall' placeholder='Interval' style='width:160px'>";
  inner += "  <select id='a_interval_unit' class='input-tall'>";
  inner += "    <option value='ms' selected>ms</option>";
  inner += "    <option value='s'>seconds</option>";
  inner += "    <option value='min'>minutes</option>";
  inner += "    <option value='hr'>hours</option>";
  inner += "    <option value='day'>days</option>";
  inner += "  </select>";
  inner += "</div>";
  inner += "<div style='display:flex;flex-direction:column;gap:0.5rem'>";
  inner += "  <div style='display:flex;flex-direction:column;gap:0.5rem'>";
  inner += "    <div style='margin-top:0.5rem'>";
  inner += "      <label style='font-size:0.9em;color:#000;margin-bottom:0.25rem;display:block'>Commands:</label>";
  inner += "      <div class='row-inline'>";
  inner += "        <input type='text' class='cmd-input input-tall' placeholder='Command to run' style='flex:1;min-width:260px;height:32px;line-height:32px;padding:0 0.5rem;box-sizing:border-box'>";
  inner += "        <button id='btn_add_cmd' type='button' class='btn btn-small' onclick='addCommandField()'>+ Add Command</button>";
  inner += "        <button id='btn_remove_main_cmd' type='button' class='btn btn-small' onclick='removeMainCommandField()' style='visibility:hidden'>Remove</button>";
  inner += "      </div>";
  inner += "    </div>";
  inner += "    <div id='command_fields' style='margin-top:0.25rem'></div>";
  inner += "    </div>";
  inner += "    <div style='display:flex;align-items:center;gap:0.5rem;flex-wrap:wrap'>";
  inner += "      <label style='display:flex;align-items:center;gap:0;margin:0'><input id='a_enabled' type='checkbox' checked style='margin:0 -8px 0 6px;padding:0;vertical-align:middle;width:16px;height:16px'><span style='display:inline-block;margin-left:0;font-kerning:none;color:#000 !important;position:relative;left:16px'>Enabled</span></label>";
  inner += "    </div>";
  inner += "    <div style='margin-top:0.5rem'>";
  inner += "      <button class='btn' onclick='createAutomation()'>Add</button>";
  inner += "    </div>";
  inner += "  </div>";
  inner += "</div>";
  inner += "</div>";
  inner += "<div id='a_error' style='color:#b00;margin-top:0.5rem'></div>";
  inner += "</div>";

  // Listing
  inner += "<div id='autos' style='background:#f8f9fa;padding:1rem;border-radius:8px;margin:1rem auto;color:#333;max-width:1000px;overflow-x:auto'>Loading automations...</div>";

  inner += "<script>";
  inner += "window.onload = function() { try{ autoTypeChanged(); }catch(_){ } loadAutos(); };";
  inner += "function autoTypeChanged(){ try { var t=document.getElementById('a_type').value; var g1=document.getElementById('grp_atTime'); var g2=document.getElementById('grp_afterDelay'); var g3=document.getElementById('grp_interval'); if(t==='atTime'){ g1.classList.remove('vis-gone'); g2.classList.add('vis-gone'); g3.classList.add('vis-gone'); recurChanged(); } else if(t==='afterDelay'){ g1.classList.add('vis-gone'); g2.classList.remove('vis-gone'); g3.classList.add('vis-gone'); } else if(t==='interval'){ g1.classList.add('vis-gone'); g2.classList.add('vis-gone'); g3.classList.remove('vis-gone'); } }catch(_){ } }";
  inner += "function recurChanged(){ try { var r=document.getElementById('a_recur').value; var dw=document.getElementById('dow_wrap'); if(!dw) return; if(r==='weekly'){ dw.style.display='flex'; } else { dw.style.display='none'; } }catch(_){ } }";
  
  inner += "function addTimeField(){ const container=document.getElementById('time_fields'); const newField=document.createElement('div'); newField.className='time-field row-inline'; newField.style.cssText='gap:0.5rem;margin-bottom:0.3rem'; newField.innerHTML='<input type=\"time\" class=\"time-input input-tall\" placeholder=\"HH:MM\" style=\"width:120px;height:32px;line-height:32px\"><button type=\"button\" class=\"btn btn-small\" onclick=\"removeTimeField(this)\" style=\"height:32px;line-height:32px;padding:0 10px;box-sizing:border-box;font-size:14px;display:inline-flex;align-items:center;margin:0\">Remove</button>'; container.appendChild(newField); updateTimeRemoveButtons(); updateMainTimeRemove(); }";
  inner += "function removeTimeField(btn){ btn.parentElement.remove(); updateTimeRemoveButtons(); updateMainTimeRemove(); }";
  inner += "function removeMainTimeField(){ const mainInput=document.querySelector('#grp_atTime .time-input'); const additionalFields=document.querySelectorAll('.time-field'); if(additionalFields.length>0){ const firstAdditional=additionalFields[0]; const firstAdditionalInput=firstAdditional.querySelector('.time-input'); if(firstAdditionalInput){ mainInput.value=firstAdditionalInput.value; firstAdditional.remove(); } } else { mainInput.value=''; } updateTimeRemoveButtons(); updateMainTimeRemove(); }";
  inner += "function updateTimeRemoveButtons(){ const fields=document.querySelectorAll('.time-field'); const allTimeInputs=document.querySelectorAll('.time-input'); const totalTimeFields=allTimeInputs.length; fields.forEach((field,idx)=>{ const btn=field.querySelector('button'); if(totalTimeFields<=1){ btn.style.visibility='hidden'; } else { btn.style.visibility='visible'; } }); }";
  inner += "function updateMainTimeRemove(){ const allTimeInputs=document.querySelectorAll('.time-input'); const mainRemoveBtn=document.querySelector('#btn_remove_main_time'); if(mainRemoveBtn){ if(allTimeInputs.length<=1){ mainRemoveBtn.style.visibility='hidden'; } else { mainRemoveBtn.style.visibility='visible'; } } }";
  inner += "function updateCommandRemoveButtons(){ const cfields=document.querySelectorAll('.cmd-field'); const allCmdInputs=document.querySelectorAll('.cmd-input'); const totalCmdFields=allCmdInputs.length; cfields.forEach((field,idx)=>{ const btn=field.querySelector('button[onclick*=\"removeCommandField\"]'); if(totalCmdFields<=1){ btn.style.visibility='hidden'; } else { btn.style.visibility='visible'; } }); }";
  inner += "function updateMainCommandRemove(){ const allCmdInputs=document.querySelectorAll('.cmd-input'); const mainRemoveBtn=document.querySelector('#btn_remove_main_cmd'); if(mainRemoveBtn){ if(allCmdInputs.length<=1){ mainRemoveBtn.style.visibility='hidden'; } else { mainRemoveBtn.style.visibility='visible'; } } }";
  inner += "function addCommandField(){ const container=document.getElementById('command_fields'); const div=document.createElement('div'); div.className='cmd-field row-inline'; div.style.cssText='gap:0.5rem;margin-bottom:0.3rem'; div.innerHTML='<input type=\"text\" class=\"cmd-input input-tall\" placeholder=\"Command to run\" style=\"flex:1;min-width:260px;height:32px;line-height:32px;padding:0 0.5rem;box-sizing:border-box\"><button type=\"button\" class=\"btn btn-small\" onclick=\"removeCommandField(this)\" style=\"height:32px;line-height:32px;padding:0 10px;box-sizing:border-box;font-size:14px;display:inline-flex;align-items:center;margin:0\">Remove</button>'; container.appendChild(div); updateCommandRemoveButtons(); updateMainCommandRemove(); }";
  inner += "function removeCommandField(btn){ btn.parentElement.remove(); updateCommandRemoveButtons(); updateMainCommandRemove(); }";
  inner += "function removeMainCommandField(){ const mainInput=document.querySelector('.cmd-input'); const additionalFields=document.querySelectorAll('.cmd-field'); if(additionalFields.length>0){ const firstAdditional=additionalFields[0]; const firstAdditionalInput=firstAdditional.querySelector('.cmd-input'); if(firstAdditionalInput){ mainInput.value=firstAdditionalInput.value; firstAdditional.remove(); } } else { mainInput.value=''; } updateCommandRemoveButtons(); updateMainCommandRemove(); }";
  inner += "function human(v){ if(v===null||v===undefined) return '\\u2014'; if(typeof v==='boolean') return v?'Yes':'No'; return ''+v; }";
  inner += "function formatNextRun(nextAt){ if(!nextAt || nextAt === null) return '\u2014'; try { const now = Math.floor(Date.now()/1000); const next = parseInt(nextAt); if(isNaN(next) || next <= 0) return '\u2014'; const date = new Date(next * 1000); const timeStr = date.toLocaleString(); const diffSec = next - now; let relativeStr = ''; if(diffSec <= 0){ relativeStr = 'overdue'; } else if(diffSec < 60){ relativeStr = 'in ' + diffSec + 's'; } else if(diffSec < 3600){ relativeStr = 'in ' + Math.floor(diffSec/60) + 'm'; } else if(diffSec < 86400){ relativeStr = 'in ' + Math.floor(diffSec/3600) + 'h'; } else { relativeStr = 'in ' + Math.floor(diffSec/86400) + 'd'; } return timeStr + '<br><small style=\"color:#666\">' + relativeStr + '</small>'; } catch(e){ return '\u2014'; } }";
  inner += "function renderAutos(json){ try { let data = (typeof json === 'string') ? JSON.parse(json) : json; let autos = []; if(data && data.automations && Array.isArray(data.automations)) autos = data.automations; let html=''; if(autos.length===0){ html += 'No automations yet.'; } else { html += '<table style=\"width:100%;border-collapse:collapse\">'; html += '<tr style=\"background:#e9ecef\"><th style=\"padding:0.5rem;text-align:left\">ID</th><th style=\"padding:0.5rem;text-align:left\">Name</th><th style=\"padding:0.5rem;text-align:left\">Enabled</th><th style=\"padding:0.5rem;text-align:left\">Type</th><th style=\"padding:0.5rem;text-align:left\">Summary</th><th style=\"padding:0.5rem;text-align:left\">Next Run</th><th style=\"padding:0.5rem\">Actions</th></tr>'; autos.forEach(a=>{ let name = a.name || '(unnamed)'; let enabled = (a.enabled===true?'Yes':'No'); let t = (a.type||'').toLowerCase(); let type = a.type || human(a.type); let summary = ''; if(t==='attime'){ summary = 'At ' + (a.time || '?') + (a.days?' on '+a.days:''); } else if(t==='afterdelay'){ summary = 'After ' + (a.delayMs || '?') + ' ms'; } else if(t==='interval'){ summary = 'Every ' + (a.intervalMs || '?') + ' ms'; } else { summary = '\u2014'; } if(Array.isArray(a.commands) && a.commands.length){ summary += ' | cmds: ' + a.commands.join('; '); } else if(a.command){ summary += ' | cmd: ' + a.command; } let nextRun = formatNextRun(a.nextAt); let id = (typeof a.id !== 'undefined') ? a.id : ''; let btns = ''; if(id!==''){ if(a.enabled===true){ btns += '<button class=\"btn\" onclick=\"autoToggle('+id+',0)\" style=\"margin-right:0.3rem\">Disable</button>'; } else { btns += '<button class=\"btn\" onclick=\"autoToggle('+id+',1)\" style=\"margin-right:0.3rem\">Enable</button>'; } btns += '<button class=\"btn\" onclick=\"autoRun('+id+')\" style=\"margin-right:0.3rem\">Run</button>'; btns += '<button class=\"btn\" onclick=\"autoDelete('+id+')\" style=\"margin-right:0.3rem;color:#b00\">Delete</button>'; } html += '<tr style=\"border-bottom:1px solid #ddd\"><td style=\"padding:0.5rem\">'+id+'</td><td style=\"padding:0.5rem\">'+name+'</td><td style=\"padding:0.5rem\">'+enabled+'</td><td style=\"padding:0.5rem\">'+type+'</td><td style=\"padding:0.5rem\">'+summary+'</td><td style=\"padding:0.5rem\">'+nextRun+'</td><td style=\"padding:0.5rem\">'+btns+'</td></tr>'; }); html += '</table>'; } document.getElementById('autos').innerHTML = html; } catch(e) { document.getElementById('autos').innerHTML = 'Error parsing automations: ' + e.message; } }";
  inner += "function loadAutos(){ fetch('/api/automations').then(r => { if(r.ok) return r.text(); else throw new Error('HTTP '+r.status); }).then(txt => { renderAutos(txt); }).catch(e => { document.getElementById('autos').innerHTML = 'Error loading automations: ' + e.message; }); }";
  // CLI helpers via /api/cli
  inner += "function postCLI(cmd){ return fetch('/api/cli',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'cmd='+encodeURIComponent(cmd)}).then(r=>r.text()); }";
  inner += "function postCLIValidate(cmd){ return fetch('/api/cli',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'cmd='+encodeURIComponent(cmd)+'&validate=1'}).then(r=>r.text()); }";
  inner += "function createAutomation(){ const name=document.getElementById('a_name').value.trim(); const type=document.getElementById('a_type').value; const delayRaw=document.getElementById('a_delay').value.trim(); const delayUnit=(document.getElementById('a_delay_unit')?document.getElementById('a_delay_unit').value:'ms'); const intervalRaw=document.getElementById('a_interval').value.trim(); const intervalUnit=(document.getElementById('a_interval_unit')?document.getElementById('a_interval_unit').value:'ms'); const en=document.getElementById('a_enabled').checked; document.getElementById('a_error').textContent=''; const recur=(document.getElementById('a_recur')?document.getElementById('a_recur').value:'daily'); if(type==='atTime'&&(recur==='monthly'||recur==='yearly')){ document.getElementById('a_error').textContent='Monthly/Yearly repeats are not supported yet.'; return; } const selectedDays=[]; if(type==='atTime'&&recur==='weekly'){ ['mon','tue','wed','thu','fri','sat','sun'].forEach(day=>{ if(document.getElementById('day_'+day).checked) selectedDays.push(day); }); if(selectedDays.length===0){ document.getElementById('a_error').textContent='Please select at least one day for a weekly schedule.'; return; } } const days=selectedDays.join(','); const timeInputs=document.querySelectorAll('.time-input'); const times=[]; timeInputs.forEach(input=>{ const val=input.value.trim(); if(val) times.push(val); }); const cmdInputs=document.querySelectorAll('.cmd-input'); const cmds=[]; cmdInputs.forEach(inp=>{ const v=inp.value.trim(); if(v) cmds.push(v); }); const cmdsParam=cmds.join(';'); const buildParts=(time,idx)=>{ let parts=['automation add']; parts.push('name='+name+(time!==null && times.length>1?' #'+(idx+1):'')); parts.push('type='+type); if(time) parts.push('time='+time); if(type==='atTime'){ parts.push('recurrence='+recur); if(days) parts.push('days='+days); } if(delayRaw){ let n=parseFloat(delayRaw); if(!isNaN(n)&&n>=0){ let mult=1; if(delayUnit==='s') mult=1000; else if(delayUnit==='min') mult=60000; else if(delayUnit==='hr') mult=3600000; else if(delayUnit==='day') mult=86400000; const delayMs=Math.floor(n*mult); parts.push('delayms='+delayMs); } } if(intervalRaw){ let n=parseFloat(intervalRaw); if(!isNaN(n)&&n>=0){ let mult=1; if(intervalUnit==='s') mult=1000; else if(intervalUnit==='min') mult=60000; else if(intervalUnit==='hr') mult=3600000; else if(intervalUnit==='day') mult=86400000; const intervalMs=Math.floor(n*mult); parts.push('intervalms='+intervalMs); } } parts.push('enabled='+(en?1:0)); if(cmdsParam) parts.push('commands='+cmdsParam); return parts.join(' '); }; const fullCmds=(times.length?times:[null]).map((t,idx)=>buildParts(t,idx)); ";
  inner += "Promise.all(fullCmds.map(c=>postCLIValidate(c))).then(vals=>{ for(let i=0;i<vals.length;i++){ const v=(vals[i]||'').trim(); if(v!=='VALID'){ document.getElementById('a_error').textContent=v; throw new Error('Invalid'); } } return Promise.all(fullCmds.map(c=>postCLI(c))); }).then(results=>{ const err=results.find(t=>t.toLowerCase().indexOf('error:')>=0); if(err){ document.getElementById('a_error').textContent=err; return; } ";
  inner += "document.getElementById('a_name').value=''; document.querySelectorAll('.time-input').forEach(input=>input.value=''); ['mon','tue','wed','thu','fri','sat','sun'].forEach(day=>{ let el=document.getElementById('day_'+day); if(el) el.checked=false; }); document.getElementById('a_delay').value=''; document.getElementById('a_interval').value=''; const cwrap=document.getElementById('command_fields'); if(cwrap){ cwrap.innerHTML=''; addCommandField(); } loadAutos(); }).catch(e=>{ if(!document.getElementById('a_error').textContent){ document.getElementById('a_error').textContent='Validation error: '+e.message; } }); }";
  inner += "function autoToggle(id,en){ const cmd='automation ' + (en? 'enable':'disable') + ' id='+id; postCLI(cmd).then(()=>loadAutos()); }";
  inner += "function autoDelete(id){ if(!confirm('Delete automation '+id+'?')) return; postCLI('automation delete id='+id).then(()=>loadAutos()); }";
  inner += "function autoRun(id){ postCLI('automation run id='+id).then(r=>{ if(r.toLowerCase().indexOf('error:')>=0){ alert(r); } else { alert('Automation executed: '+r); loadAutos(); } }); }";
  inner += "function downloadFromGitHub(){ const url=document.getElementById('github_url').value.trim(); const name=document.getElementById('github_name').value.trim(); const status=document.getElementById('download_status'); if(!url){ status.innerHTML='<span style=\"color:#dc3545\">Please enter a GitHub URL</span>'; return; } status.innerHTML='<span style=\"color:#007bff\">Downloading...</span>'; let cmd='downloadautomation url='+encodeURIComponent(url); if(name) cmd+=' name='+encodeURIComponent(name); postCLI(cmd).then(r=>{ if(r.toLowerCase().indexOf('error:')>=0){ status.innerHTML='<span style=\"color:#dc3545\">'+r+'</span>'; } else { status.innerHTML='<span style=\"color:#28a745\">'+r+'</span>'; document.getElementById('github_url').value=''; document.getElementById('github_name').value=''; loadAutos(); } }).catch(e=>{ status.innerHTML='<span style=\"color:#dc3545\">Network error: '+e.message+'</span>'; }); }";
  inner += "</script>";

  return htmlShellWithNav(username, "automations", inner);
}

#endif
