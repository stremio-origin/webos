/* Trakt's rows on Home, from the account connected on this TV: Trakt History, Trakt Watchlist and Trakt
   Recommendations, each for films and series (the official Trakt addon's six). One set only: when the Stremio account
   has the official Trakt addon, its own rows show and these never do. They are asked like an addon's catalog (an
   address "trakt:<list>/<type>", answered with {"metas": [...]}), so Home keeps, shows and refreshes them as any row.
   Settings > Trakt > Trakt rows on Home turns them off. Nothing here writes to Trakt or to the Stremio library */
#ifndef SB_TRAKTROWS_H
#define SB_TRAKTROWS_H
#include "core/net.h"
#include "core/stremio.h"
/* the official Trakt addon is in the Stremio account (on or off on this TV) */
int  traktrows_official(void);
/* the rows to add to Home's (none when not connected, switched off, or the official addon has them); returns how many */
int  traktrows_defs(CatalogDef *out, int max);
/* an address of these rows ("trakt:...") */
int  traktrows_is(const char *url);
/* asks Trakt; cb gets 200 and {"metas": [...]} as an addon answers, or Trakt's status and no body (0: not connected) */
void traktrows_fetch(const char *url, NetCb cb, void *ud);
/* a new connection: the rows' kept copies go (another Trakt account's must never show first) */
void traktrows_forget(void);
#endif
