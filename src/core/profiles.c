#include "core/profiles.h"
#include "core/store.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static int active;
static unsigned session = 1;

static cJSON *list(void) {
  return cJSON_GetObjectItemCaseSensitive(store_node("profiles"), "list");
}

static cJSON *profile(int id) {
  cJSON *entry;
  cJSON_ArrayForEach(entry, list()) {
    if (cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(entry, "id")) == id) return entry;
  }
  return NULL;
}

static int clean_name(const char *input, char out[PROFILE_NAME_CAP]) {
  while (isspace((unsigned char)*input)) input++;
  size_t len = strlen(input);
  while (len && isspace((unsigned char)input[len - 1])) len--;
  if (!len || len >= PROFILE_NAME_CAP) return 0;
  memcpy(out, input, len);
  out[len] = 0;
  return 1;
}

static int append(cJSON *entries, int id, const char *name) {
  cJSON *entry = cJSON_CreateObject();
  if (!entry || !cJSON_AddNumberToObject(entry, "id", id) ||
      !cJSON_AddStringToObject(entry, "name", name) || !cJSON_AddItemToArray(entries, entry)) {
    cJSON_Delete(entry);
    return 0;
  }
  return 1;
}

static void save(void) {
  store_set_node("profiles", store_node("profiles"));
  store_flush();
}

void profiles_init(void) {
  if (store_node("profiles")) return;
  cJSON *state = cJSON_CreateObject();
  cJSON *entries = cJSON_AddArrayToObject(state, "list");
  if (!entries || !append(entries, PROFILE_DEFAULT, "") || !cJSON_AddNumberToObject(state, "next", 2)) {
    cJSON_Delete(state);
    return;
  }
  store_set_node("profiles", state);
  store_flush();
}

int profiles_count(void) { return cJSON_GetArraySize(list()); }

int profiles_id(int index) {
  const cJSON *id = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(list(), index), "id");
  return cJSON_IsNumber(id) ? id->valueint : 0;
}

const char *profiles_name(int id) {
  return cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(profile(id), "name"));
}

int profiles_add(const char *input) {
  char name[PROFILE_NAME_CAP];
  cJSON *next = cJSON_GetObjectItemCaseSensitive(store_node("profiles"), "next");
  if (!next || next->valueint >= INT_MAX || profiles_count() >= PROFILE_MAX || !clean_name(input, name)) return 0;
  int id = next->valueint;
  if (!append(list(), id, name)) return 0;
  cJSON_SetIntValue(next, id + 1);
  save();
  return id;
}

int profiles_avatar(int id) {
  const cJSON *value = cJSON_GetObjectItemCaseSensitive(profile(id), "avatar");
  if (cJSON_IsNumber(value) && value->valueint >= 0 && value->valueint < PROFILE_AVATAR_COUNT)
    return value->valueint;
  return id > 0 ? (id - 1) % PROFILE_AVATAR_COUNT : 0;
}

int profiles_set_avatar(int id, int avatar) {
  cJSON *entry = profile(id);
  if (!entry || avatar < 0 || avatar >= PROFILE_AVATAR_COUNT) return 0;
  cJSON *value = cJSON_GetObjectItemCaseSensitive(entry, "avatar");
  if (cJSON_IsNumber(value)) cJSON_SetIntValue(value, avatar);
  else {
    cJSON *choice = cJSON_CreateNumber(avatar);
    if (!choice) return 0;
    int stored = value ? cJSON_ReplaceItemInObjectCaseSensitive(entry, "avatar", choice)
                       : cJSON_AddItemToObject(entry, "avatar", choice);
    if (!stored) { cJSON_Delete(choice); return 0; }
  }
  save();
  return 1;
}

int profiles_int(int id, const char *key, int def) { const cJSON *v = cJSON_GetObjectItemCaseSensitive(profile(id), key); return cJSON_IsNumber(v) ? v->valueint : def; }
int profiles_set_int(int id, const char *key, int v) {
  cJSON *entry = profile(id); if (!entry) return 0;
  cJSON_DeleteItemFromObjectCaseSensitive(entry, key);
  if (v && !cJSON_AddItemToObject(entry, key, cJSON_CreateNumber(v))) return 0;
  save();
  return 1;
}
int profiles_kids(int id) { return cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(profile(id), "kids")); }
int profiles_set_kids(int id, int on) {
  cJSON *entry = profile(id); if (!entry) return 0;
  cJSON_DeleteItemFromObjectCaseSensitive(entry, "kids");
  if (on && !cJSON_AddItemToObject(entry, "kids", cJSON_CreateTrue())) return 0;
  save();
  return 1;
}

int profiles_rename(int id, const char *input) {
  char name[PROFILE_NAME_CAP];
  cJSON *entry = profile(id);
  if (!entry || !clean_name(input, name) ||
      !cJSON_SetValuestring(cJSON_GetObjectItemCaseSensitive(entry, "name"), name)) return 0;
  save();
  return 1;
}

int profiles_delete(int id) {
  cJSON *entry = profile(id);
  if (!entry || active == id || profiles_count() < 2) return 0;
  profiles_clear_data(id);
  cJSON_Delete(cJSON_DetachItemViaPointer(list(), entry));
  save();
  return 1;
}

int profiles_active(void) { return active; }

int profiles_select(int id) {
  if (!profile(id)) return 0;
  if (active == id) return 1;
  active = id;
  session++;
  return 1;
}

void profiles_file(int id, const char *file, char *path, size_t cap) {
  if (id <= PROFILE_DEFAULT) snprintf(path, cap, "%s/%s", store_data_dir(), file);
  else snprintf(path, cap, "%s/profile-%d-%s", store_data_dir(), id, file);
}

void profiles_clear_data(int id) {
  char path[1200];
  store_remove_profile(id);
  profiles_file(id, "trakt.auth", path, sizeof path);
  remove(path);
  profiles_file(id, "trakt-watched.json", path, sizeof path);
  remove(path);
}

void profiles_deselect(void) { active = 0; session++; }
unsigned profiles_session(void) { return session; }
