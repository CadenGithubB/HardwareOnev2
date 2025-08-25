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
  return htmlShellWithNav("guest", "login", inner);
}

#endif
