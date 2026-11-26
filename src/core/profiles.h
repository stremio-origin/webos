#ifndef SB_PROFILES_H
#define SB_PROFILES_H

#include <stddef.h>

enum { PROFILE_DEFAULT = 1, PROFILE_MAX = 12, PROFILE_NAME_CAP = 64, PROFILE_AVATAR_COUNT = 18 };

void profiles_init(void);
int profiles_count(void);
int profiles_id(int index);
const char *profiles_name(int id);
int profiles_add(const char *name);
int profiles_avatar(int id);
int profiles_set_avatar(int id, int avatar);
int profiles_int(int id, const char *key, int def);   /* lock (0/1), limit (minutes), bed (minutes after midnight) */
int profiles_set_int(int id, const char *key, int v);
int profiles_kids(int id);            /* a child's profile */
int profiles_set_kids(int id, int on);
int profiles_rename(int id, const char *name);
int profiles_delete(int id);
int profiles_active(void);
int profiles_select(int id);
void profiles_deselect(void);
unsigned profiles_session(void);
void profiles_clear_data(int id);
void profiles_file(int id, const char *file, char *path, size_t cap);

#endif
