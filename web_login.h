#ifndef WEB_LOGIN_H
#define WEB_LOGIN_H

String getLoginPage(const String& username = "", const String& errorMsg = "") {
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
    errorMsg
  );
  
  // Add script to check for revoked message in sessionStorage and show popup
  inner += "<script>window.addEventListener('load', function(){ setTimeout(function(){ try{ var msg = sessionStorage.getItem('revokeMsg'); if(msg){ sessionStorage.removeItem('revokeMsg'); alert(msg); } }catch(_){} }, 500); });</script>";
  
  return htmlPublicShellWithNav(inner);
}

#endif
