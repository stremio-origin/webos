/* "Original" audio against real files' tracks (read with ffprobe from debrid copies, 2026-11-11): the original
   language's track, not the file's default (often a dub), never a commentary, one whose sound plays here first */
#include "core/audorig.h"
#include <stdio.h>
#include <string.h>
static const char *AL[][2] = { { "ja", "jpn" }, { "es", "spa" }, { "en", "eng" }, { "uk", "ukr" }, { "ru", "rus" }, { "pt", "por" }, { "fr", "fre" }, { NULL, NULL } };
static const char *canon(const char *c) { for (int i = 0; AL[i][0]; i++) if (!strcmp(c, AL[i][0]) || !strcmp(c, AL[i][1])) return AL[i][1]; return c; }
static int match(const char *a, const char *b) { return !strcmp(canon(a), canon(b)); }
static int fails;
#define T(name, orig, want, ...) do { AudInfo t[] = { __VA_ARGS__ }; int n = sizeof t / sizeof *t, got = aud_pick_original(orig, t, n, match); if (got != want) { fails++; printf("FAIL: %s: track %d, wanted %d\n", name, got + 1, want + 1); } else printf("ok: %s -> #%d %s\n", name, got + 1, got >= 0 ? t[got].lang : "(the file's own)"); } while (0)
int main(void) {
  T("Spirited Away", "ja", 0, { "jpn", "FLAC 2.0", 1, 0 }, { "jpn", "DTS-HD MA 6.1", 0, 1 }, { "eng", "DTS-HD MA 5.1", 0, 1 }, { "fre", "", 0, 1 });
  T("Your Name (Ukrainian dub first; DTS not played here: the FLAC one)", "ja", 4, { "ukr", "MVO Robota Holosom | DTS 5.1", 1, 1 }, { "ukr", "MVO | DTS 5", 0, 1 }, { "jpn", "Original | DTS-HD MA 5.1 Ch", 0, 1 }, { "jpn", "Original | DTS-HD MA 2.0 Ch", 0, 1 }, { "jpn", "Original | FLAC 5.1 Ch", 0, 0 }, { "jpn", "Original | OPUS 2.0 Ch", 0, 0 });
  T("Your Name, DTS played here", "ja", 2, { "ukr", "MVO | DTS 5.1", 1, 0 }, { "ukr", "MVO | DTS 5", 0, 0 }, { "jpn", "Original | DTS-HD MA 5.1 Ch", 0, 0 }, { "jpn", "Original | FLAC 5.1 Ch", 0, 0 });
  T("Attack on Titan (English dub first; TrueHD not played here: the AC-3 one)", "ja", 3, { "eng", "English TrueHD 5.1", 1, 1 }, { "eng", "English AC-3 5.1", 0, 0 }, { "jpn", "Japanese TrueHD 2.0", 0, 1 }, { "jpn", "Japanese AC-3 2.0", 0, 0 }, { "jpn", "Japanese PCM 2.0", 0, 0 });
  T("Your Name, 5.3 GB copy (English default, AAC)", "ja", 1, { "eng", "English 5.1 channel AAC", 1, 0 }, { "jpn", "Japanese 5.1 channel AAC", 0, 0 }, { "jpn", "Japanese 5.1 channel AAC (English theme song version)", 0, 0 }, { "jpn", "Japanese 2.0 channel AAC", 0, 0 }, { "chi", "Chinese 5.1 channel AAC", 0, 0 });
  T("Attack on Titan, a TV that plays TrueHD", "ja", 2, { "eng", "English TrueHD 5.1", 1, 0 }, { "eng", "English AC-3 5.1", 0, 0 }, { "jpn", "Japanese TrueHD 2.0", 0, 0 }, { "jpn", "Japanese AC-3 2.0", 0, 0 });
  T("Demon Slayer (English first)", "ja", 1, { "eng", "", 1, 0 }, { "jpn", "", 0, 0 });
  T("Demon Slayer (commentary in Japanese too)", "ja", 0, { "jpn", "FLAC 2.0", 1, 0 }, { "eng", "FLAC 2.0", 0, 0 }, { "jpn", "Commentary by Voice Actors", 0, 0 });
  T("Pan's Labyrinth", "es", 0, { "spa", "", 1, 1 }, { "spa", "", 0, 1 }, { "eng", "", 0, 1 });
  T("The Secret in Their Eyes (Russian first)", "es", 2, { "rus", "MVO, 448kbps", 1, 0 }, { "rus", "DTS-HD", 0, 1 }, { "spa", "", 0, 0 }, { "spa", "Commentary", 0, 0 });
  T("Money Heist (one track)", "es", 0, { "spa", "FLAC 2.0", 1, 0 });
  T("The Orphanage (Portuguese default)", "es", 0, { "spa", "DTS-HD MA 7.1", 0, 1 }, { "por", "AC3 2.0", 1, 0 });
  T("The Orphanage (Russian dub first)", "es", 2, { "rus", "DTS (Dubbed)", 1, 1 }, { "rus", "AC3 (Nemahov)", 0, 0 }, { "spa", "DTS", 0, 1 });
  T("no track in the original language", "ko", -1, { "eng", "", 1, 0 }, { "fre", "", 0, 0 });
  T("original unknown", "", -1, { "eng", "", 1, 0 });
  printf("audorig: %s\n", fails ? "FAILED" : "all passed"); return fails ? 1 : 0;
}
