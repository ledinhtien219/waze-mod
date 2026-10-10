// Lunar calendar checks: known dates + structural invariants over 1990-2060.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstddef>
#include "build/lunar_block.cpp"

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static int daysIn(int y, int m) {
  static const int d[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  return (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) ? 29 : d[m - 1];
}

int main() {
  struct K { int d, m, y; int ld, lm, ly; bool leap; const char *name; } known[] = {
    {10, 2, 2024, 1, 1, 2024, false, "Tet 2024"},   {29, 1, 2025, 1, 1, 2025, false, "Tet 2025"},
    {17, 2, 2026, 1, 1, 2026, false, "Tet 2026"},   {22, 3, 2023, 1, 2, 2023, true,  "leap month 2 / 2023"},
    {25, 7, 2025, 1, 6, 2025, true,  "leap month 6 / 2025"}, {1, 1, 2023, 10, 12, 2022, false, "1 Jan 2023"},
    {1, 1, 2000, 25, 11, 1999, false, "1 Jan 2000"}, {25, 9, 2026, 15, 8, 2026, false, "Trung thu 2026"},
  };
  for (auto &k : known) {
    int ld, lm, ly; bool leap;
    solarToLunar(k.d, k.m, k.y, ld, lm, ly, leap);
    CHECK(ld == k.ld && lm == k.lm && ly == k.ly && leap == k.leap, "%s -> %d/%d/%d%s", k.name, ld, lm, ly, leap ? " (leap)" : "");
  }
  char nm[32]; lunarYearName(2026, nm, sizeof nm);
  CHECK(strcmp(nm, "Bính Ngọ") == 0, "year name 2026 = %s", nm);

  // Invariants: day counter advances by 1 or restarts after day 29/30; exactly one Tet per solar year.
  int pld = 0, plm = 0, ply = 0; bool pleap = false, first = true;
  int tetCount[2100] = {0};
  long total = 0;
  for (int y = 1990; y <= 2060; y++)
    for (int m = 1; m <= 12; m++)
      for (int d = 1; d <= daysIn(y, m); d++) {
        int ld, lm, ly; bool leap;
        solarToLunar(d, m, y, ld, lm, ly, leap);
        CHECK(ld >= 1 && ld <= 30 && lm >= 1 && lm <= 12, "range %d-%d-%d -> %d/%d", y, m, d, ld, lm);
        if (!first) {
          if (ld == pld + 1) CHECK(lm == plm && ly == ply && leap == pleap, "same month broke at %d-%d-%d", y, m, d);
          else CHECK(ld == 1 && (pld == 29 || pld == 30), "bad day step at %d-%d-%d (%d -> %d)", y, m, d, pld, ld);
        }
        if (ld == 1 && lm == 1 && !leap) tetCount[y]++;
        pld = ld; plm = lm; ply = ly; pleap = leap; first = false; total++;
      }
  for (int y = 1991; y <= 2060; y++) CHECK(tetCount[y] == 1, "expected exactly one Tet in %d, got %d", y, tetCount[y]);
  printf("%s: %ld days checked, %d failure(s)\n", fails ? "LUNAR TESTS FAILED" : "lunar tests passed", total, fails);
  return fails ? 1 : 0;
}
