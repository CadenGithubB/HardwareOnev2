#ifndef WEB_FILES_H
#define WEB_FILES_H

String getFilesPage(const String& username) {
  String inner;
  inner += "<h2>File Manager</h2>";
  inner += "<p>Browse and manage files on the device filesystem</p>";
  
  // File operations toolbar
  inner += "<div style='background:#f8f9fa;padding:1rem;border-radius:8px;margin:1rem 0;border-left:4px solid #667eea'>";
  inner += "  <div style='display:flex;gap:1rem;flex-wrap:wrap;align-items:center'>";
  inner += "    <button class='btn' onclick='createFolder()'>Create Folder</button>";
  inner += "    <button class='btn' onclick='createFile()'>Create File</button>";
  inner += "    <button class='btn' onclick='refreshFiles()'>Refresh</button>";
  inner += "  </div>";
  inner += "</div>";
  
  // File list container
  inner += "<div id='file-list' style='background:#f8f9fa;padding:1rem;border-radius:8px;margin:1rem 0;color:#333'>Loading files...</div>";
  
  // File creation modal
  inner += "<div id='file-modal' style='display:none;position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(0,0,0,0.5);z-index:1000'>";
  inner += "  <div style='position:absolute;top:50%;left:50%;transform:translate(-50%,-50%);background:white;padding:2rem;border-radius:8px;min-width:400px'>";
  inner += "    <h3 id='modal-title'>Create File</h3>";
  inner += "    <label>Name:<br><input id='file-name' style='width:100%;padding:0.5rem;margin:0.5rem 0'></label><br>";
  inner += "    <label>Type:<br><select id='file-type' style='width:100%;padding:0.5rem;margin:0.5rem 0'>";
  inner += "      <option value='txt'>Text File (.txt)</option>";
  inner += "      <option value='csv'>CSV File (.csv)</option>";
  inner += "      <option value='rtf'>Rich Text (.rtf)</option>";
  inner += "      <option value='pdf'>PDF File (.pdf)</option>";
  inner += "      <option value='docx'>Word Document (.docx)</option>";
  inner += "      <option value='xlsx'>Excel Spreadsheet (.xlsx)</option>";
  inner += "      <option value='pptx'>PowerPoint Presentation (.pptx)</option>";
  inner += "    </select></label><br>";
  inner += "    <div style='margin-top:1rem'>";
  inner += "      <button class='btn' onclick='submitFileCreate()'>Create</button>";
  inner += "      <button class='btn' onclick='closeModal()' style='margin-left:0.5rem'>Cancel</button>";
  inner += "    </div>";
  inner += "  </div>";
  inner += "</div>";
  
  inner += "<script>";
  inner += "window.onload = function() { refreshFiles(); };";
  inner += "let currentPath = '/';";
  inner += "function refreshFiles() {";
  inner += "  fetch('/api/files/list?path=' + encodeURIComponent(currentPath)).then(r => r.json()).then(d => {";
  inner += "    if (d.success) {";
  inner += "      let html = '<div style=\"margin-bottom:1rem\">';";
  inner += "      html += '<strong>Current Path:</strong> ' + currentPath;";
  inner += "      if (currentPath !== '/') {";
  inner += "        html += ' <button onclick=\"navigateUp()\" class=\"btn\" style=\"margin-left:1rem\">← Back</button>';";
  inner += "      }";
  inner += "      html += '</div>';";
  inner += "      html += '<table style=\"width:100%;border-collapse:collapse\">';";
  inner += "      html += '<tr style=\"background:#e9ecef\"><th style=\"padding:0.5rem;text-align:left\">Name</th><th style=\"padding:0.5rem;text-align:left\">Type</th><th style=\"padding:0.5rem;text-align:left\">Size</th><th style=\"padding:0.5rem;text-align:left\">Actions</th></tr>';";
  inner += "      d.files.forEach(f => {";
  inner += "        html += '<tr style=\"border-bottom:1px solid #ddd\">';";
  inner += "        if (f.type === 'folder') {";
  inner += "          html += '<td style=\"padding:0.5rem\"><a href=\"#\" onclick=\"enterFolder(\\'' + f.name + '\\')\" style=\"color:#007bff;text-decoration:none\">📁 ' + f.name + '</a></td>';";
  inner += "        } else {";
  inner += "          html += '<td style=\"padding:0.5rem\">📄 ' + f.name + '</td>';";
  inner += "        }";
  inner += "        html += '<td style=\"padding:0.5rem\">' + f.type + '</td>';";
  inner += "        if (f.type === 'folder') {";
  inner += "          const cnt = (typeof f.count === 'number') ? (f.count + ' items') : (f.size || '—');";
  inner += "          html += '<td style=\"padding:0.5rem\">' + cnt + '</td>';";
  inner += "        } else {";
  inner += "          html += '<td style=\"padding:0.5rem\">' + (f.size || '0 bytes') + '</td>';";
  inner += "        }";
  inner += "        html += '<td style=\"padding:0.5rem\">';";
  inner += "        if (f.type === 'folder') {";
  inner += "          html += '<button onclick=\"enterFolder(\\'' + f.name + '\\')\" class=\"btn\" style=\"margin-right:0.5rem\">Enter</button>';";
  inner += "        } else {";
  inner += "          html += '<button onclick=\"viewFile(\\'' + f.name + '\\')\" class=\"btn\" style=\"margin-right:0.5rem\">View</button>';";
  inner += "        }";
  inner += "        if (f.name !== 'settings.json' && f.name !== 'users.json') {";
  inner += "          html += '<button onclick=\"deleteFile(\\'' + f.name + '\\')\" class=\"btn\">Delete</button>';";
  inner += "        }";
  inner += "        html += '</td></tr>';";
  inner += "      });";
  inner += "      html += '</table>';";
  inner += "      document.getElementById('file-list').innerHTML = html;";
  inner += "    } else {";
  inner += "      document.getElementById('file-list').innerHTML = 'Error: ' + d.error;";
  inner += "    }";
  inner += "  }).catch(e => document.getElementById('file-list').innerHTML = 'Error: ' + e.message);";
  inner += "}";
  inner += "function createFolder() {";
  inner += "  let name = prompt('Folder name:');";
  inner += "  if (name) {";
  inner += "    let fullPath = currentPath === '/' ? '/' + name : currentPath + '/' + name;";
  inner += "    fetch('/api/files/create', { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body: 'name=' + encodeURIComponent(fullPath) + '&type=folder' })";
  inner += "    .then(r => r.json()).then(d => { if (d.success) { alert('Folder created!'); refreshFiles(); } else alert('Error: ' + d.error); });";
  inner += "  }";
  inner += "}";
  inner += "function createFile() {";
  inner += "  document.getElementById('file-modal').style.display = 'block';";
  inner += "}";
  inner += "function closeModal() {";
  inner += "  document.getElementById('file-modal').style.display = 'none';";
  inner += "}";
  inner += "function submitFileCreate() {";
  inner += "  let name = document.getElementById('file-name').value;";
  inner += "  let type = document.getElementById('file-type').value;";
  inner += "  if (name) {";
  inner += "    let fullPath = currentPath === '/' ? '/' + name : currentPath + '/' + name;";
  inner += "    fetch('/api/files/create', { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body: 'name=' + encodeURIComponent(fullPath) + '&type=' + type })";
  inner += "    .then(r => r.json()).then(d => { if (d.success) { alert('File created!'); closeModal(); refreshFiles(); } else alert('Error: ' + d.error); });";
  inner += "  }";
  inner += "}";
  inner += "function enterFolder(name) {";
  inner += "  if (currentPath === '/') {";
  inner += "    currentPath = '/' + name;";
  inner += "  } else {";
  inner += "    currentPath = currentPath.endsWith('/') ? currentPath + name : currentPath + '/' + name;";
  inner += "  }";
  inner += "  refreshFiles();";
  inner += "}";
  inner += "function navigateUp() {";
  inner += "  let lastSlash = currentPath.lastIndexOf('/');";
  inner += "  if (lastSlash > 0) {";
  inner += "    currentPath = currentPath.substring(0, lastSlash);";
  inner += "  } else {";
  inner += "    currentPath = '/';";
  inner += "  }";
  inner += "  refreshFiles();";
  inner += "}";
  inner += "function viewFile(name) {";
  inner += "  let fullPath = currentPath === '/' ? '/' + name : currentPath + '/' + name;";
  inner += "  window.open('/api/files/view?name=' + encodeURIComponent(fullPath), '_blank');";
  inner += "}";
  inner += "function deleteFile(name) {";
  inner += "  let fullPath = currentPath === '/' ? '/' + name : currentPath + '/' + name;";
  inner += "  if (confirm('Delete ' + name + '?')) {";
  inner += "    fetch('/api/files/delete', { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body: 'name=' + encodeURIComponent(fullPath) })";
  inner += "    .then(r => r.json()).then(d => { alert(d.message || d.error); refreshFiles(); });";
  inner += "  }";
  inner += "}";
  inner += "</script>";
  
  return htmlShellWithNav(username, "files", inner);
}

#endif
