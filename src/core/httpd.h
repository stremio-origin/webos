/* The phone pages: a tiny HTTP server on the TV's network address, port 8099, only while a screen has
   asked for it (15 min after the last ask it goes away). It serves only a few pages behind a random token:
   the search page (what is typed reaches the app), the TMDB key page and the Trakt id page. It runs on its own thread; everything here is for the main
   thread. */
#ifndef SB_HTTPD_H
#define SB_HTTPD_H
#include <stddef.h>

#define HTTPD_PORT 8099

/* on (or kept on) for 15 more minutes; the pages take the app's current language. 0 = the port is taken */
int  httpd_start(void);
/* the address to put in a QR: "http://<LAN IP>:8099/t/<token><suffix>" (suffix "", "/tmdb", "/trakt");
   0 (and "") when the TV has no network address. Starts the server. */
int  httpd_url(const char *suffix, char *out, size_t cap);
/* the address as shown under a code, the token hidden ("/t/••••") */
void httpd_url_shown(const char *url, char *out, size_t cap);
/* text typed on the phone: the latest sequence number; when it is past `since`, the text (at most 80
   characters) is copied out */
int  httpd_typed(int since, char *out, size_t cap);
/* how many times a code has been opened on a phone (the pages, not what they send): a
   screen keeps the count from when it showed its code, a higher one = scanned */
unsigned httpd_hits(void);
/* the key page's "Save": called on the server thread with the key as typed; 1 = TMDB accepted it and it is
   kept. Set by the TMDB module. */
void httpd_set_key_handler(int (*fn)(const char *key));
/* the same for the Trakt page's Save (the Trakt module): 1 = Trakt accepted the client id */
void httpd_set_trakt_handler(int (*fn)(const char *key));
/* (Stremio Claude) an addon link sent from the phone page ("/addon"): as httpd_typed */
int  httpd_addon_link(int since, char *out, size_t cap);
#endif
