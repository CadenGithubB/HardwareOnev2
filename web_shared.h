#ifndef WEB_SHARED_H
#define WEB_SHARED_H

// Forward declarations
String getCommonCSS();
String generateNavigation(const String& activePage, const String& username);
String generatePublicNavigation();
String htmlPublicShellWithNav(const String& inner);

String htmlPage(const String& body) {
  return String(
    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width, initial-scale=1'>"
    "<title>HardwareOne - Minimal</title><style>") + getCommonCSS() + 
    "</style></head><body>" + body + "</body></html>";
}

String getCommonCSS() {
  return
    "*{margin:0;padding:0;box-sizing:border-box}"
    "body{font-family:'Segoe UI',Tahoma,Geneva,Verdana,sans-serif;"
    "background:linear-gradient(135deg,#667eea 0%,#764ba2 100%);"
    "min-height:100vh;color:#fff;line-height:1.6}"
    ".content{padding:1rem;max-width:1200px;margin:0 auto}"
    ".card{background:rgba(255,255,255,.1);backdrop-filter:blur(10px);"
    "border-radius:15px;padding:2rem;margin:1rem 0;border:1px solid rgba(255,255,255,.2);"
    "box-shadow:0 8px 32px rgba(0,0,0,.1)}"
    ".top-menu{background:rgba(0,0,0,.2);padding:1rem;display:flex;"
    "justify-content:space-between;align-items:center;flex-wrap:wrap}"
    ".menu-left{display:flex;gap:1rem;flex-wrap:wrap}"
    ".menu-item,button.menu-item{color:#333;text-decoration:none;font-weight:500;padding:8px 16px;border-radius:8px;"
    "transition:all .3s;border:1px solid rgba(0,0,0,.2);background:rgba(255,255,255,.8);"
    "box-shadow:0 2px 4px rgba(0,0,0,.1);display:inline-block;font-size:1rem;line-height:1.2}"
    "button.menu-item{cursor:pointer}"
    ".menu-item:hover,button.menu-item:hover{color:#222;background:rgba(255,255,255,.9);border-color:rgba(0,0,0,.3);"
    "transform:translateY(-1px);box-shadow:0 4px 8px rgba(0,0,0,.15)}"
    ".menu-item.active{color:#fff;background:rgba(255,255,255,.2);border-color:rgba(255,255,255,.4);font-weight:600}"
    ".user-info{display:flex;align-items:center;gap:1rem;flex-wrap:wrap}"
    ".username{font-weight:bold;color:#fff}"
    ".login-btn{background:rgba(255,255,255,.85);color:#0f5132;text-decoration:none;"
    "padding:.4rem .8rem;border-radius:8px;font-size:.85rem;transition:all .3s ease;"
    "border:1px solid rgba(25,135,84,.4);box-shadow:0 2px 4px rgba(0,0,0,.1)}"
    ".login-btn:hover{background:rgba(255,255,255,.95);border-color:rgba(25,135,84,.6);"
    "transform:translateY(-1px);box-shadow:0 4px 8px rgba(0,0,0,.15)}"
    ".logout-btn{background:rgba(255,255,255,.85);color:#b02a37;text-decoration:none;"
    "padding:.4rem .8rem;border-radius:8px;font-size:.85rem;transition:all .3s ease;"
    "border:1px solid rgba(176,42,55,.4);box-shadow:0 2px 4px rgba(0,0,0,.1)}"
    ".logout-btn:hover{background:rgba(255,255,255,.95);border-color:rgba(176,42,55,.6);"
    "transform:translateY(-1px);box-shadow:0 4px 8px rgba(0,0,0,.15)}"
    "h1,h2,h3{margin-bottom:1rem;color:#fff}"
    "p{margin-bottom:.5rem}"
    "a{color:#87ceeb;text-decoration:none}"
    "a:hover{text-decoration:underline}"
    "input,select,textarea{width:100%;padding:.5rem;border:1px solid #ddd;"
    "border-radius:4px;margin-bottom:.5rem}"
    /* taller input utility to visually match .btn height */
    ".input-tall{min-height:40px;padding:.5rem .6rem}"
    /* generic native buttons, but do NOT override our .btn components */
    "button:not(.menu-item):not(.btn){background:#007bff;color:#fff;border:none;padding:.5rem 1rem;"
    "border-radius:4px;cursor:pointer}"
    "button:not(.menu-item):not(.btn):hover{background:#0056b3}"
    "table{width:100%;border-collapse:collapse;margin:1rem 0}"
    "th,td{padding:.5rem;text-align:left;border-bottom:1px solid rgba(255,255,255,.1)}"
    "th{background:rgba(255,255,255,.1);font-weight:bold}"
    "@media(max-width:768px){"
    ".top-menu{flex-direction:column;gap:1rem}"
    ".menu-left{justify-content:center}"
    ".user-info{justify-content:center}"
    ".content{padding:.5rem}"
    ".card{padding:1rem}"
    "}"
    /* --- Utilities: Text --- */
    ".text-center{text-align:center}"
    ".text-muted{color:#6c757d}"
    ".text-danger{color:#b00020}"
    ".text-primary{color:#0d6efd}"
    ".text-sm{font-size:.9rem}"
    ".link-primary{color:#0d6efd}"
    /* --- Utilities: Visibility --- */
    ".vis-hidden{visibility:hidden!important}"
    ".vis-gone{display:none!important}"
    /* --- Utilities: Spacing --- */
    ".space-top-sm{margin-top:8px}"
    ".space-top-md{margin-top:16px}"
    ".space-top-lg{margin-top:24px}"
    ".space-bottom-sm{margin-bottom:8px}"
    ".space-bottom-md{margin-bottom:16px}"
    ".space-bottom-lg{margin-bottom:24px}"
    ".space-left-sm{margin-left:8px}"
    ".space-left-md{margin-left:16px}"
    ".space-left-lg{margin-left:24px}"
    ".space-right-sm{margin-right:8px}"
    ".space-right-md{margin-right:16px}"
    ".space-right-lg{margin-right:24px}"
    /* --- Containers & Panels --- */
    ".panel{background:rgba(255,255,255,.95);color:#333;border-radius:12px;padding:1.25rem;"
    "box-shadow:0 6px 20px rgba(0,0,0,.08);border:1px solid rgba(0,0,0,.08)}"
    /* headings inside light panels should be dark for readability */
    ".panel h1,.panel h2,.panel h3{color:#333}"
    ".panel-light{background:#f8f9fa;color:#333;border-radius:8px;padding:1rem;border:1px solid #e9ecef}"
    ".container-narrow{max-width:520px;margin:0 auto}"
    ".pad-xl{padding:2rem}"
    /* --- Forms --- */
    ".form-field{margin-bottom:12px}"
    ".form-field label{display:block;margin-bottom:6px}"
    ".form-input{width:100%;padding:.6rem;border:1px solid #ced4da;border-radius:6px;background:#fff;color:#333}"
    ".form-error{margin-bottom:.5rem}"
    /* --- Buttons (unified) --- */
    ".btn{display:inline-flex;align-items:center;justify-content:center;min-height:40px;"
    "padding:.5rem 1rem;border-radius:8px;border:1px solid rgba(0,0,0,.2);"
    "background:rgba(255,255,255,.9);color:#333;text-decoration:none;cursor:pointer;transition:all .2s;"
    "font-size:1rem;line-height:1.2;font-weight:500;box-sizing:border-box}"
    "button.btn,a.btn{display:inline-flex;align-items:center;justify-content:center;min-height:40px;"
    "font-size:1rem;line-height:1.2;font-weight:500}"
    ".btn:hover{transform:translateY(-1px);box-shadow:0 2px 6px rgba(0,0,0,.12);background:rgba(255,255,255,.95)}"
    /* semantic aliases share the same base look */
    ".btn-primary,.btn-secondary{ }"
    ".btn-small{padding:.25rem .5rem;border-radius:6px}"
    ".btn-row{display:flex;gap:.5rem;align-items:center;flex-wrap:wrap}"
    /* --- Modal (for future reuse) --- */
    ".modal-overlay{display:none;position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(0,0,0,0.5);z-index:1000}"
    ".modal-dialog{position:absolute;top:50%;left:50%;transform:translate(-50%,-50%);background:#fff;padding:1.25rem;border-radius:8px;min-width:320px}"
    /* --- Tables (for future reuse) --- */
    ".table{width:100%;border-collapse:collapse}"
    ".table th,.table td{padding:.5rem;text-align:left;border-bottom:1px solid #ddd;color:#333}"
    ".table-striped tr:nth-child(odd){background:#fafafa}"
    ;
}

// Public page shell - minimal navigation with only login
String htmlPublicShellWithNav(const String& inner) {
  String h;
  h += "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
  h += "<title>HardwareOne - Minimal</title><style>"; h += getCommonCSS(); h += "</style></head><body>";
  h += generatePublicNavigation();
  h += "<div class='content'><div class='card'>"; h += inner; h += "</div></div>";
  h += "</body></html>";
  return h;
}

// Authenticated page shell - full navigation
String htmlShellWithNav(const String& username, const String& activePage, const String& inner) {
  String h;
  h += "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
  h += "<title>HardwareOne - Minimal</title><style>"; h += getCommonCSS(); h += "</style></head><body>";
  h += generateNavigation(activePage, username);
  h += "<div class='content'><div class='card'>"; h += inner; h += "</div></div>";
  // Unified SSE + Session Management System
  h += "<script>(function(){try{ if(typeof window.ENABLE_SSE==='undefined'){ window.ENABLE_SSE=true; } var pollInterval=null; var checkRevoke=function(){ try{ fetch('/api/notice',{cache:'no-store',credentials:'same-origin'}).then(function(r){ if(r.status===401){ fetch('/login',{cache:'no-store',credentials:'same-origin'}).then(function(lr){return lr.text();}).then(function(loginHtml){ var startMarker='alert alert-warning'; var alertStart=loginHtml.indexOf(startMarker); var reason=''; if(alertStart>=0){ var strongStart=loginHtml.indexOf('<strong>Session Terminated:</strong>',alertStart); if(strongStart>=0){ var reasonStart=strongStart+37; var reasonEnd=loginHtml.indexOf('<',reasonStart); if(reasonEnd<0) reasonEnd=loginHtml.length; reason=loginHtml.substring(reasonStart,reasonEnd).trim(); } } if(reason.length>0){ sessionStorage.setItem('revokeMsg','[revoke] '+reason); }else{ sessionStorage.setItem('revokeMsg','[revoke] Your session has expired. Please log in again.'); } document.body.innerHTML='<div style=\"text-align:center;padding:50px;font-family:Arial,sans-serif\"><h2>Session Expired</h2><p>Redirecting to login...</p></div>'; setTimeout(function(){ window.location.replace('/login'); }, 1000); }).catch(function(){ sessionStorage.setItem('revokeMsg','[revoke] Your session has expired. Please log in again.'); document.body.innerHTML='<div style=\"text-align:center;padding:50px;font-family:Arial,sans-serif\"><h2>Session Expired</h2><p>Redirecting to login...</p></div>'; setTimeout(function(){ window.location.replace('/login'); }, 1000); }); return; } return r.json(); }).then(function(d){ if(d&&d.notice&&d.notice.indexOf('[revoke]')===0){ try{ if(window.__es){ window.__es.close(); window.__es=null; } sessionStorage.setItem('revokeMsg', d.notice); document.body.innerHTML='<div style=\"text-align:center;padding:50px;font-family:Arial,sans-serif\"><h2>Session Revoked</h2><p>Redirecting to login...</p></div>'; setTimeout(function(){ window.location.replace('/login'); }, 1000); }catch(_){ window.location.href='/login'; } } }).catch(function(){}); }catch(_){} }; var startPolling=function(){ if(!pollInterval){ pollInterval=setInterval(checkRevoke, 8000); } }; var stopPolling=function(){ if(pollInterval){ clearInterval(pollInterval); pollInterval=null; } }; if(!window.ENABLE_SSE||!window.EventSource){ startPolling(); }else{ var attachNotice=function(es){ try{ es.addEventListener('notice', function(e){ try{ var d=null; try{ d=JSON.parse(e.data||'{}'); }catch(_){ d=null; } if(d&&d.msg){ if(d.msg.indexOf('[revoke]')===0){ try{ if(window.__es){ window.__es.close(); window.__es=null; } sessionStorage.setItem('revokeMsg', d.msg); document.body.innerHTML='<div style=\"text-align:center;padding:50px;font-family:Arial,sans-serif\"><h2>Session Revoked</h2><p>Redirecting to login...</p></div>'; setTimeout(function(){ window.location.replace('/login'); }, 1000); }catch(_){ window.location.href='/login'; } } else { try{ alert(d.msg); }catch(_){ console.log('[NOTICE]', d.msg); } } } } catch(_){ } }); }catch(_){ } }; window.ensureES=function(){ try{ var es=window.__es; if(es && es.readyState===1){ stopPolling(); attachNotice(es); return; } if(es){ try{ es.close(); }catch(_){ } } es = new EventSource('/api/events', { withCredentials: true }); window.__es = es; attachNotice(es); es.onopen=function(){ stopPolling(); if(window.DEBUG_SSE){ console.log('[SSE] connected'); } }; es.onerror=function(){ if(window.DEBUG_SSE){ console.log('[SSE] error (auto-reconnect)'); } startPolling(); }; }catch(_){ startPolling(); } }; window.ensureES(); setInterval(window.ensureES, 10000); } }catch(_){ startPolling(); } })();</script>";
  // Proactively close SSE on navigation/unload to free server resources quickly
  h += "<script>(function(){ try{ function closeES(){ try{ if(window.__es){ window.__es.close(); window.__es=null; } }catch(_){ } } window.addEventListener('beforeunload', closeES, {capture:true}); window.addEventListener('pagehide', closeES, {capture:true}); document.addEventListener('click', function(ev){ try{ var a=ev.target; while(a && a.tagName!=='A'){ a=a.parentElement; } if(!a) return; var href=a.getAttribute('href'); if(!href) return; if(href.indexOf('#')===0) return; /* ignore in-page anchors */ closeES(); }catch(_){ } }, {capture:true}); }catch(_){ } })();</script>";
  // One-shot status hydration on page load (no persistent SSE needed)
  h += "<script>(function(){ try{ fetch('/api/sensors/status',{cache:'no-store', credentials:'same-origin'}).then(function(r){return r.json();}).then(function(j){ if(j && typeof window.applySensorStatus==='function'){ try{ window.applySensorStatus(j); }catch(_){ } } }).catch(function(_){ }); }catch(_){ } })();</script>";
  h += "</body></html>";
  return h;
}

// Render a generic two-field form with two buttons using shared classes
// title: heading for the form
// subtitle: small helper text under the title (optional)
// action, method: form target and HTTP method
// Field 1: label1, name1, value1, type1 (e.g., text, email)
// Field 2: label2, name2, value2, type2 (e.g., password)
// primaryText: primary button text
// secondaryText, secondaryHref: secondary action link
// errorMsg: optional error message to display above the form
String renderTwoFieldForm(
  const String& title,
  const String& subtitle,
  const String& action,
  const String& method,
  const String& label1,
  const String& name1,
  const String& value1,
  const String& type1,
  const String& label2,
  const String& name2,
  const String& value2,
  const String& type2,
  const String& primaryText,
  const String& secondaryText,
  const String& secondaryHref,
  const String& errorMsg
) {
  String html;
  html += "<div class='panel container-narrow space-top-md'>";
  html += "  <div class='text-center space-bottom-sm'>";
  html += "    <h2>" + title + "</h2>";
  if (subtitle.length()) {
    html += "    <p class='text-muted' style='margin:0'>" + subtitle + "</p>";
  }
  html += "  </div>";

  if (errorMsg.length()) {
    html += "  <div id='err' class='form-error text-danger'>" + errorMsg + "</div>";
  } else {
    html += "  <div id='err' class='form-error' style='display:none'></div>";
  }

  html += "  <form method='" + method + "' action='" + action + "'>";
  html += "    <div class='form-field'><label>" + label1 + "</label>";
  html += "      <input class='form-input' name='" + name1 + "' value='" + value1 + "' type='" + type1 + "'></div>";
  html += "    <div class='form-field'><label>" + label2 + "</label>";
  html += "      <input class='form-input' name='" + name2 + "' value='" + value2 + "' type='" + type2 + "'></div>";
  html += "    <div class='btn-row space-top-md'>";
  html += "      <button class='btn btn-primary' type='submit'>" + primaryText + "</button>";
  if (secondaryText.length()) {
    html += "      <a class='btn btn-secondary' href='" + secondaryHref + "'>" + secondaryText + "</a>";
  }
  html += "    </div>";
  html += "  </form>";
  html += "</div>";
  return html;
}

#endif
