#ifndef WEB_LOGIN_H
#define WEB_LOGIN_H

// Forward declarations for functions defined in main file
static void getClientIP(httpd_req_t* req, String& ip);
static String getLogoutReason(const String& ip);

String getLoginPage(const String& username = "", const String& errorMsg = "", httpd_req_t* req = nullptr) {
  // Check for logout reason if request is provided
  String logoutReason = "";
  if (req) {
    String clientIP;
    getClientIP(req, clientIP);
    if (clientIP.length() > 0) {
      logoutReason = getLogoutReason(clientIP);
      Serial.printf("[LOGIN_PAGE_DEBUG] Direct login page access for IP '%s' - logout reason: '%s'\n", clientIP.c_str(), logoutReason.c_str());
    }
  }
  
  // Combine error message and logout reason
  String combinedError = errorMsg;
  if (logoutReason.length() > 0) {
    if (combinedError.length() > 0) combinedError += "<br>";
    combinedError += "<div class='alert alert-warning mb-3' style='background:#fff3cd;border:1px solid #ffeaa7;color:#856404;padding:12px;border-radius:4px;'>";
    combinedError += "<strong>Session Terminated:</strong> " + logoutReason;
    combinedError += "</div>";
  }
  
  String inner = renderTwoFieldForm(
    "Sign In",
    "Use your HardwareOne credentials",
    "/login",
    "POST",
    "Username",
    "username",
    username,
    "text",
    "Password",
    "password",
    "",
    "password",
    "Sign In",
    "Request Account",
    "/register",
    combinedError
  );
  
  // Add script to check for revoked message in sessionStorage and show popup
  inner += "<script>window.addEventListener('load', function(){ setTimeout(function(){ try{ var msg = sessionStorage.getItem('revokeMsg'); if(msg){ sessionStorage.removeItem('revokeMsg'); alert(msg); } }catch(_){} }, 500); });</script>";
  
  return htmlPublicShellWithNav(inner);
}

#endif
