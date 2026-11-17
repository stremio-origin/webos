/* tests/ssreq: the TV's screen-saver request read as it really comes (pretty-printed) and in every other spacing;
   what is not a start is never answered as one. tests/ssreq/run.sh */
#include "platform/webos/ssreq.h"
#include <stdio.h>
static int fails;
static void t(const char *name, const char *p, int want, const char *wantTs) {
  char ts[64] = ""; int got = ssreq_active(p, ts, sizeof ts);
  if (got != want || (want && strcmp(ts, wantTs))) { fails++; printf("FAIL %s: got %d \"%s\"\n", name, got, ts); } }
int main(void) {
  t("pretty-printed, as the TV sends it", "{\n    \"returnValue\": true,\n    \"state\": \"Active\",\n    \"timestamp\": 1730000000123\n}", 1, "1730000000123");
  t("compact", "{\"returnValue\":true,\"state\":\"Active\",\"timestamp\":42}", 1, "42");
  t("spaces before the colon", "{ \"state\" : \"Active\" , \"timestamp\" : 7 }", 1, "7");
  t("tabs and CRLF", "{\r\n\t\"timestamp\":\t99,\r\n\t\"state\":\t\"Active\"\r\n}", 1, "99");
  t("a string timestamp kept with its quotes", "{\"state\": \"Active\", \"timestamp\": \"2026-11-02 10:00\"}", 1, "\"2026-11-02 10:00\"");
  t("a large number kept whole", "{\"state\": \"Active\", \"timestamp\": 98765432109876543210}", 1, "98765432109876543210");
  t("subscription answer (no state)", "{\"returnValue\": true, \"subscribed\": true}", 0, "");
  t("another state", "{\"state\": \"Inactive\", \"timestamp\": 5}", 0, "");
  t("Active only inside a nested object", "{\"x\": {\"state\": \"Active\", \"timestamp\": 1}, \"state\": \"Done\"}", 0, "");
  t("the word as a value of another key", "{\"note\": \"state\", \"state\": \"Active\", \"timestamp\": 3}", 1, "3");
  t("no timestamp", "{\"state\": \"Active\"}", 0, "");
  t("a timestamp that is not a number", "{\"state\": \"Active\", \"timestamp\": 12abc}", 0, "");
  t("refused", "{\"returnValue\": false, \"errorCode\": -1, \"errorText\": \"Denied\"}", 0, "");
  t("NULL", NULL, 0, "");
  printf(fails ? "%d failed\n" : "all ok\n", fails); return fails ? 1 : 0;
}
