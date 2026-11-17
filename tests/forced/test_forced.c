/* forcedsubs: which names and answers count as forced (a film's title must not) */
#include "core/forcedsubs.h"
#include "cJSON.h"
#include <stdio.h>
static int bad;
static void t(int got, int want, const char *what) { if (got != want) { bad++; printf("FAIL %s: %d, wanted %d\n", what, got, want); } }
int main(void) {
  static const struct { const char *n; int f; } NAMES[] = {
    { "English Forced", 1 }, { "English (Forced)", 1 }, { "Forced", 1 }, { "FORCED", 1 }, { "Français forcés", 1 }, { "Français (Forcé)", 1 },
    { "Francais force", 1 }, { "Español Forzados", 1 }, { "Italiano forzati", 1 }, { "Português forçadas", 1 }, { "Deutsch erzwungen", 1 },
    { "Nederlands geforceerd", 1 }, { "Polski wymuszone", 1 }, { "Türkçe zorunlu", 1 }, { "Română forțat", 1 }, { "Magyar kényszerített", 1 },
    { "Русские форсированные", 1 }, { "Русские Принудительные", 1 }, { "العربية إجباري", 1 }, { "日本語 強制字幕", 1 }, { "中文强制", 1 },
    { "Signs & Songs", 1 }, { "Signs", 1 }, { "English [Foreign parts only]", 1 },
    { "English", 0 }, { "English SDH", 0 }, { "English Full", 0 }, { "Not forced", 0 }, { "English (non-forced)", 0 },
    { "Unforced", 0 }, { "", 0 }, { "Commentary", 0 }, { "Forcefield", 0 } };
  for (unsigned i = 0; i < sizeof NAMES / sizeof *NAMES; i++) t(forcedsubs_name(NAMES[i].n), NAMES[i].f, NAMES[i].n);
  static const struct { const char *n; int f; } FILES[] = {
    { "Film.2015.1080p.BluRay.forced.srt", 1 }, { "Film.2015.FORCED.en.srt", 1 }, { "Film.2015.fr.forces.srt", 0 },
    { "The.Force.Awakens.2015.1080p.srt", 0 }, { "Delta.Force.1986.srt", 0 }, { "Special.Forces.2011.srt", 0 }, { "Signs.2002.720p.srt", 0 },
    { "Full.Metal.Jacket.1987.forced.srt", 1 }, { "Film.2015.Signs.and.Songs.ass", 1 } };
  for (unsigned i = 0; i < sizeof FILES / sizeof *FILES; i++) t(forcedsubs_file(FILES[i].n), FILES[i].f, FILES[i].n);
  static const struct { const char *j, *label; int f; } ENTRIES[] = {
    { "{\"forced\":true}", "", 1 }, { "{\"forced\":1}", "", 1 }, { "{\"forced\":\"true\"}", "", 1 }, { "{\"isForced\":true}", "", 1 },
    { "{\"disposition\":{\"forced\":1}}", "", 1 }, { "{\"flags\":[\"forced\"]}", "", 1 }, { "{\"forced\":false}", "", 0 },
    { "{\"lang\":\"eng\"}", "English forced", 1 }, { "{\"subtitleFileName\":\"Film.2015.forced.srt\"}", "", 1 },
    { "{\"subtitleFileName\":\"Film.2015.srt\",\"releaseName\":\"Film.2015.forced\"}", "", 0 }, /* (the file's own name wins over the release's) */
    { "{\"releaseName\":\"Film.2015.Forced.Subs\"}", "", 1 }, { "{\"releaseName\":\"The.Force.Awakens.2015\"}", "", 0 }, { "{}", "", 0 } };
  for (unsigned i = 0; i < sizeof ENTRIES / sizeof *ENTRIES; i++) { cJSON *j = cJSON_Parse(ENTRIES[i].j); t(forcedsubs_entry(j, ENTRIES[i].label), ENTRIES[i].f, ENTRIES[i].j); cJSON_Delete(j); }
  t(forcedsubs_entry(NULL, "Forced"), 0, "no entry");
  printf(bad ? "%d failed\n" : "all ok\n", bad); return bad != 0;
}
