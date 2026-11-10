/* Host-side test without the webOS SDK. */
#include "core/forcedsubs.h"
#include "cJSON.h"
#include <stdio.h>
typedef struct { const char *s; int forced; } Test;
int main(void) {
  static const Test tests[] = {
    {"Movie.fr.forced.srt",1},{"Movie.en.FORCED.ass",1},
    {"French Forced",1},{"Français forcés",1},{"French FORCÉE",1},
    {"Castellano forzadas",1},{"Español forzado",1},
    {"Português forçados",1},{"Italiano forzati",1},
    {"Deutsch erzwungen",1},{"Nederlands geforceerd",1},
    {"Polski wymuszone",1},{"Türkçe zorunlu",1},
    {"Русские ФОРСИРОВАННЫЕ",1},{"العربية إجباري",1},
    {"日本語強制字幕",1},{"中文强制字幕",1},{"한국어 강제 자막",1},
    {"हिन्दी अनिवार्य",1},{"English Foreign Only",1},
    {"English Foreign Dialogue",1},{"English signs & songs",1},
    {"English normal",0},{"French",0},{"French SDH",0},
    {"English unforced",0},{"English not-forced",0},
    {"English non-forced",0},{"French Non-Forcés",0},{"English no forced",0},
    {"reinforced dialogue",0},{"notforced",0},{"",0},{NULL,0}
  };
  for (unsigned i=0; i<sizeof tests/sizeof tests[0]; ++i) {
    int actual = !!forcedsubs_name(tests[i].s);
    if (actual != tests[i].forced) {
      fprintf(stderr,"FAIL %u: '%s': got %d expected %d\n",i,tests[i].s?tests[i].s:"(null)",actual,tests[i].forced);
      return 1;
    }
  }
  static const struct { const char *json, *label; int forced; } metadata[] = {
    {"{\"forced\":true}", "", 1},
    {"{\"forced\":1}", "", 1},
    {"{\"forced\":\"true\"}", "", 1},
    {"{\"isForced\":true}", "", 1},
    {"{\"is_forced\":true}", "", 1},
    {"{\"forcedSubtitle\":true}", "", 1},
    {"{\"disposition\":{\"forced\":1}}", "", 1},
    {"{\"flags\":[\"default\",\"forced\"]}", "", 1},
    {"{\"subtitleFileName\":\"Film.fr.forced.srt\"}", "", 1},
    {"{\"fileName\":\"Film.de.erzwungen.ass\"}", "", 1},
    {"{\"releaseName\":\"Film.2025.en.forced\"}", "", 1},
    {"{\"subtitleFileName\":\"Film.fr.srt\",\"releaseName\":\"Film.forced\"}", "", 0},
    {"{\"forced\":false,\"subtitleFileName\":\"Film.fr.forced.srt\"}", "", 1},
    {"{\"flags\":[\"default\",\"sdh\"]}", "", 0},
    {"{\"filename\":\"Film.fr.srt\"}", "French Forced", 1},
    {"{\"filename\":\"Film.fr.srt\"}", "French Full", 0},
    {"{\"forced\":false}", "", 0}
  };
  for (unsigned i=0; i<sizeof metadata/sizeof metadata[0]; ++i) {
    cJSON *entry = cJSON_Parse(metadata[i].json);
    if (!entry) return 2;
    int actual = forcedsubs_metadata(entry, metadata[i].label, "");
    if (actual != metadata[i].forced) {
      fprintf(stderr, "METADATA FAIL %u: %s (got %d expected %d)\\n",
              i, metadata[i].json, actual, metadata[i].forced);
      cJSON_Delete(entry);
      return 3;
    }
    cJSON_Delete(entry);
  }
  printf("PASS: %zu filename/label cases and %zu metadata cases\n",sizeof tests/sizeof tests[0],sizeof metadata/sizeof metadata[0]);
  return 0;
}
