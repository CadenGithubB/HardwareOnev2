#ifndef WEB_AUTH_REQUIRED_H
#define WEB_AUTH_REQUIRED_H

esp_err_t sendAuthRequiredResponse(httpd_req_t* req) {
  httpd_resp_set_status(req, "401 Unauthorized");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate");
  httpd_resp_set_hdr(req, "Pragma", "no-cache");
  httpd_resp_set_type(req, "text/html");
  
  String inner;
  inner += "<div class='text-center pad-xl'>";
  inner += "  <h2>Authentication Required</h2>";
  inner += "  <p>You need to sign in to access this page.</p>";
  inner += "  <p class='text-muted text-sm'>Don't have an account? <a class='link-primary' href='/register' style='text-decoration:none'>Request Access</a></p>";
  inner += "</div>";
  inner += "</div>";
  
  String page = htmlShellWithNav("guest", "login", inner);
  httpd_resp_send(req, page.c_str(), HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
} 

#endif
