/* Trakt (the rating people give a title, its spread, their comments): with the app's own client id, built in from the
   private keys file, or a user's own one added from the phone (kept in the data dir as trakt.key, used first). With
   neither every Trakt call is skipped and the reviews show the other sources alone. Main thread, never blocks. */
#ifndef SB_TRAKT_H
#define SB_TRAKT_H
#include "core/net.h"
void trakt_init(void);                 /* after store_init; otherwise done on first use */
int  trakt_has_key(void);           /* the built-in id or the user's */
int  trakt_builtin(void);           /* this build carries the app's own id: nobody needs to add one */
void trakt_key_clear(void);            /* "Remove the Trakt id" */
unsigned trakt_generation(void);
const char *trakt_client_id(void);     /* the id in use: the user's own, else the app's; "" with neither */       /* bumps when the id comes or goes: what was fetched without it is asked again */
/* a GET of api.trakt.tv<path> with the id's headers, the answer on the main thread; 0 when there is no id */
unsigned trakt_request(const char *path, NetCb cb, void *ud);
#endif
