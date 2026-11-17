/* Persistent state: settings, the sign-in, the library cache and small caches, kept as one JSON
   document in the data folder and written back a moment after a change (never on every key). */
#ifndef SB_STORE_H
#define SB_STORE_H
#include "cJSON.h"

void store_init(const char *dataDir);
/* string / number values under the top-level object (settings and the like) */
const char *store_get(const char *key, const char *def);
void        store_set(const char *key, const char *value);
/* whole JSON branches (library, addons, user): the returned node belongs to the store */
cJSON      *store_node(const char *key);
void        store_set_node(const char *key, cJSON *node);  /* takes ownership */
void        store_remove(const char *key);
unsigned    store_serial(void);    /* bumps on every change: a value read once stays good while it holds */
void        store_flush(void);     /* writes now if dirty */
void        store_pump(void);      /* writes a little after the last change; call per frame */

/* file caches: bytes under a name (hashed from the key), with an age in seconds */
unsigned char *cache_read(const char *key, long maxAgeSec, size_t *len);
int            cache_file(const char *key, long maxAgeSec, char *path, size_t cap);  /* 1: a copy exists (and is young enough); its path */
void           cache_prune_start(int capMb);  /* the cache folder kept under capMb, least recently used first (a thread) */
long           cache_cap_bytes(void);         /* the cap given to cache_prune_start (0 before) */
long           cache_kept_bytes(void);        /* the cache folder's size after the prune at start (-1 until it ran) */
void           cache_write(const char *key, const unsigned char *data, size_t len);
void           cache_basename(const char *key, char *out, size_t cap);   /* the file a key is kept in (its name only) */
void           cache_protect_set(const char (*names)[40], int n);     /* these files are never pruned (the pictures on screen) */
const char    *store_data_dir(void);
#endif
