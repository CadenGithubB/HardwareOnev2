#ifndef WEB_AUTH_REQUIRED_H
#define WEB_AUTH_REQUIRED_H

// Forward declaration - implemented in main file
String getLogoutReasonForAuthPage(httpd_req_t* req);

esp_err_t sendAuthRequiredResponse(httpd_req_t* req) {
  httpd_resp_set_status(req, "401 Unauthorized");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate");
  httpd_resp_set_hdr(req, "Pragma", "no-cache");
  httpd_resp_set_type(req, "text/html");
  
  // Get logout reason from main file function
  String logoutReason = getLogoutReasonForAuthPage(req);
  
  String inner;
  inner += "<div class='text-center pad-xl'>";
  inner += "  <h2>Authentication Required</h2>";
  
  // Show logout reason if present
  if (logoutReason.length() > 0) {
    inner += "  <div class='alert alert-warning mb-3' style='background:#fff3cd;border:1px solid #ffeaa7;color:#856404;padding:12px;border-radius:4px;'>";
    inner += "    <strong>Session Terminated:</strong> " + logoutReason;
    inner += "  </div>";
  }
  
  inner += "  <p>You need to sign in to access this page.</p>";
  inner += "  <p class='text-sm' style='color:#fff'>Don't have an account? <a class='link-primary' href='/register' style='text-decoration:none'>Request Access</a></p>";
  inner += "</div>";
  inner += "</div>";
  
  // Add script to check for revoked message in sessionStorage and show popup
  inner += "<script>window.addEventListener('load', function(){ setTimeout(function(){ try{ var msg = sessionStorage.getItem('revokeMsg'); if(msg){ sessionStorage.removeItem('revokeMsg'); alert(msg); } }catch(_){} }, 500); });</script>";
  
  String page = htmlPublicShellWithNav(inner);
  httpd_resp_send(req, page.c_str(), HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
} 

#endif
