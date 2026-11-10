/* Recognise forced/foreign-only subtitle track names without depending on the UI language.
   The Matroska/metadata forced flag remains authoritative when provided. */
#ifndef SB_FORCEDSUBS_H
#define SB_FORCEDSUBS_H
int forcedsubs_name(const char *name);
struct cJSON;
/* Stremio subtitle JSON: metadata flag, track label or subtitle filename. */
int forcedsubs_metadata(const struct cJSON *entry, const char *label, const char *name);
#endif
