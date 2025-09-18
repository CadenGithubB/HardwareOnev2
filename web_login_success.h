#ifndef WEB_LOGIN_SUCCESS_H
#define WEB_LOGIN_SUCCESS_H

String getLoginSuccessPage(const String& sessionId) {
  String inner = "<div class='text-center'>";
  inner += "<div class='card container-narrow'>";
  inner += "<h2 style='color:#fff;margin-bottom:1.5rem'>Login Successful</h2>";
  inner += "<div style='background:rgba(40,167,69,0.1);border:1px solid rgba(40,167,69,0.3);border-radius:8px;padding:1.5rem;margin:1rem 0'>";
  inner += "<p style='color:#fff;margin-bottom:1rem;font-size:1.1rem'>Welcome! You are being redirected to the dashboard...</p>";
  inner += "<div style='display:flex;align-items:center;justify-content:center;gap:0.5rem;color:#87ceeb'>";
  inner += "<div style='width:20px;height:20px;border:2px solid #87ceeb;border-top:2px solid transparent;border-radius:50%;animation:spin 1s linear infinite'></div>";
  inner += "<span>Loading dashboard</span>";
  inner += "</div>";
  inner += "</div>";
  inner += "<p style='font-size:0.9rem;color:#87ceeb;margin-top:1rem'>If you are not redirected automatically, <a href='/dashboard' style='color:#fff;text-decoration:underline'>click here</a>.</p>";
  inner += "</div>";
  inner += "</div>";
  
  String page = "<!DOCTYPE html><html><head>";
  page += "<meta charset='utf-8'>";
  page += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  page += "<title>Login Successful - HardwareOne</title>";
  page += "<style>" + getCommonCSS();
  page += "@keyframes spin{0%{transform:rotate(0deg)}100%{transform:rotate(360deg)}}";
  page += "</style>";
  
  // Meta refresh for primary redirect
  page += "<meta http-equiv='refresh' content='2;url=/dashboard'>";
  page += "</head><body>";
  
  // Add the styled content
  page += "<div class='content'>" + inner + "</div>";
  
  // JavaScript for cookie handling and fallback
  page += "<script>";
  page += "console.log('Login success page loaded');";
  // Set cookie explicitly for Safari compatibility
  page += "try { document.cookie = 'session=" + sessionId + "; Path=/'; } catch(e) { console.warn('cookie set error', e); }";
  // Poll for cookie and redirect when detected
  page += "(function(){";
  page += "  var checks = 0; var maxChecks = 10; var timer = setInterval(function(){";
  page += "    checks++;";
  page += "    if (document.cookie && document.cookie.indexOf('session=') >= 0) {";
  page += "      console.log('Session cookie detected; redirecting to /dashboard');";
  page += "      clearInterval(timer); window.location.href = '/dashboard'; return;";
  page += "    }";
  page += "    if (checks >= maxChecks) {";
  page += "      console.log('Session cookie not detected after wait; navigating to /login (GET)');";
  page += "      clearInterval(timer); window.location.href = '/login'; return;";
  page += "    }";
  page += "  }, 300);";
  page += "})();";
  page += "</script>";
  
  page += "</body></html>";
  
  return page;
}

#endif
