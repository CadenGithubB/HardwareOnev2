#ifndef WEB_DASHBOARD_H
#define WEB_DASHBOARD_H

String getDashboardPage(const String& username) {
  String inner;
  inner += "<h2>Dashboard</h2><p>Welcome, <strong>"+username+"</strong>.</p>";
  inner += "<p>WiFi IP: "+WiFi.localIP().toString()+"</p>";
  inner += "<p>Pages: <a href='/cli'>CLI</a> • <a href='/settings'>Settings</a> • <a href='/files'>Files</a> • <a href='/sensors'>Sensors</a></p>";
  
  return htmlShellWithNav(username, "dashboard", inner);
}

#endif
