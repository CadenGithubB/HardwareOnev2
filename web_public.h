#ifndef WEB_PUBLIC_H
#define WEB_PUBLIC_H

String getPublicPage() {
  String body;
  body += "<h1>Public Page</h1>";
  body += "<p>This endpoint does not require authentication.</p>";
  body += "<p>Protected page: <a href='/'>Dashboard</a> (will prompt for credentials)</p>";
  body += "<p><a href='/login' class='menu-item' style='display:inline-block;margin-top:1rem'>Sign In</a></p>";
  
  return htmlPage(body);
}

#endif
