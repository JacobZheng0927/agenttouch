// Copyright (c) 2026 yuwentong
// Licensed under PolyForm Noncommercial 1.0.0 (see LICENSE)
// Orientation pages: clock / calendar / todo. Flat widget style: pure black,
// Logisoso type (u8g2, see pet_fonts.h), white primary / grey secondary /
// agent-accent details, one white rounded card with a flat analog dial.
// Time comes from the host ({"t":"time"}); a soft clock runs on millis().
#include "pages.h"
#include "config.h"
#include "pet_fonts.h"
#include "i18n.h"
#include <time.h>

uint8_t g_lang = LANG_ZH;   // i18n.h: set from NVS at boot and by host cfg

static const uint16_t BG      = 0x0000;
static const uint16_t WHITE   = 0xFFFF;
static const uint16_t GREYTXT = 0x7BEF;
static const uint16_t GREYDIM = 0x39E7;

// ---------------------------------------------------------------- soft clock
static uint32_t epochBase = 0;   // local seconds at baseMs
static uint32_t baseMs    = 0;

void clockSet(uint32_t localEpoch) { epochBase = localEpoch; baseMs = millis(); }
bool clockValid() { return epochBase != 0; }

static uint32_t clockNow() {
  return epochBase + (millis() - baseMs) / 1000;   // both wrap-safe unsigned
}

uint32_t clockEpoch() { return clockValid() ? clockNow() : 0; }

static void localTm(struct tm& out) {
  time_t tt = (time_t)clockNow();     // tz already applied -> use gmtime
  gmtime_r(&tt, &out);
}

// ---------------------------------------------------------------- helpers
// classic-font centered text (todo / no-clock states)
static void tc(Arduino_Canvas* c, int cx, int y, const char* s,
               uint8_t size, uint16_t col) {
  c->setTextSize(size);
  c->setTextColor(col);
  c->setCursor(cx - (int)strlen(s) * 3 * size, y);
  c->print(s);
}

// Small companion eyes so the todo page still reads as the pet.
static void miniEyes(Arduino_Canvas* c, uint32_t t, int cy) {
  float blink = 1.0f;
  uint32_t bp = t % 4600;
  if (bp > 4400) blink = fabsf((bp - 4400) / 200.0f - 0.5f) * 2.0f;
  int h = max(4, (int)(30 * blink));
  for (int s = -1; s <= 1; s += 2)
    c->fillRoundRect(240 + s * 38 - 15, cy + (30 - h) / 2, 30, h,
                     min(9, h / 2), WHITE);
}

static const char* MO3[12]  = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                               "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
static const char* WD3[7]   = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};

// Print s with extra tracking between glyphs (letterspacing the u8g2 fonts
// don't do natively); returns the width drawn.
static int trackPrint(Arduino_Canvas* c, int x, int y, const char* s,
                      int track, uint16_t col) {
  c->setTextColor(col);
  int x0 = x;
  for (const char* p = s; *p; ++p) {
    c->setCursor(x, y);
    c->write((uint8_t)*p);
    x = c->getCursorX() + track;
  }
  return x - track - x0;
}

// Width of s at the current font: invisible draw above the canvas.
static int trackWidth(Arduino_Canvas* c, const char* s, int track) {
  return trackPrint(c, 0, -200, s, track, BG);
}

// ---------------------------------------------------------------- clock
// 圆环座舱 clock (user-picked 蓝环+青字 2026-08-29):
// implemented at the end of this file where the AA font machinery is visible.

// ---------------------------------------------------------------- calendar
static int daysInMonth(int year, int mon0) {
  static const int D[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (mon0 == 1 && (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)))
    return 29;
  return D[mon0];
}

void drawCalendarPage(Arduino_Canvas* c, uint32_t t, uint16_t accent) {
  c->fillScreen(BG);
  c->setTextSize(1);
  if (!clockValid()) {
    miniEyes(c, t, 180);
    tc(c, 240, 260, "WAITING FOR HOST", 2, GREYDIM);
    return;
  }
  struct tm tmv;
  localTm(tmv);

  c->setFont(u8g2_font_fur25_tr);
  char ds[16];
  snprintf(ds, sizeof(ds), "%04d%02d%02d", tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday);
  trackPrint(c, (480 - trackWidth(c, ds, 3)) / 2, 66, ds, 3, GREYTXT);

  // Monday-first weekday row, columns centered
  c->setFont(u8g2_font_fur17_tr);
  static const char WD[8] = "MTWTFSS";
  for (int i = 0; i < 7; i++) {
    char s[2] = {WD[i], 0};
    int cw = trackWidth(c, s, 0);
    trackPrint(c, 65 + i * 58 - cw / 2, 122, s, 0, GREYDIM);
  }

  int wcolToday = (tmv.tm_wday + 6) % 7;                    // 0 = Monday
  int wcolFirst = (wcolToday + 7 - (tmv.tm_mday - 1) % 7) % 7;
  int days = daysInMonth(1900 + tmv.tm_year, tmv.tm_mon);
  int col = wcolFirst, row = 0;
  for (int d = 1; d <= days; d++) {
    int cx = 65 + col * 58, base = 180 + row * 50;
    char s[4];
    snprintf(s, sizeof(s), "%d", d);
    int w = trackWidth(c, s, 0);
    uint16_t txt;
    if (d == tmv.tm_mday) {
      c->fillCircle(cx, base - 8, 23, accent);              // true circle
      txt = BG;
    } else {
      txt = col >= 5 ? GREYTXT : WHITE;                     // weekends dimmer
    }
    trackPrint(c, cx - w / 2, base, s, 0, txt);
    if (++col == 7) { col = 0; row++; }
  }
  c->setFont((const GFXfont*)nullptr);
}

// ---------------------------------------------------------------- cyber almanac
// 赛博黄历, 方向一「朱砂符纸」(design canvas artifact 29d853d2): gold
// sexagenary header, vermilion 宜 seal / indigo 忌 seal, signal+color param
// row, cyan omen line. Data arrives daily from the host ({"t":"almanac"});
// CJK text renders with Arduino_GFX's bundled cubic11 pixel font (俐方體,
// 10167 glyphs — the ONLY translation unit referencing it, keep it that way:
// the font header defines the array per-TU).
static const uint16_t AL_GOLD    = C565(0xE8, 0xC5, 0x6A);
static const uint16_t AL_GOLDDIM = C565(0x54, 0x47, 0x26);
static const uint16_t AL_RED     = C565(0xC1, 0x35, 0x2A);
static const uint16_t AL_SEALTXT = C565(0xF6, 0xEF, 0xE4);
static const uint16_t AL_INK     = C565(0x1C, 0x22, 0x30);
static const uint16_t AL_INKBRD  = C565(0x3A, 0x42, 0x56);
static const uint16_t AL_INKTXT  = C565(0x8F, 0xA0, 0xC0);
static const uint16_t AL_CYAN    = C565(0x3E, 0xE6, 0xD2);
static const uint16_t AL_WHITE   = C565(0xED, 0xEF, 0xF2);
static const uint16_t AL_GREY    = C565(0x9A, 0xA3, 0xB2);
static const uint16_t AL_DIM     = C565(0x6A, 0x74, 0x80);

static char alGz[16], alSx[16], alJc[16], alDate[8];
static char alYi[2][48], alJi[2][48], alQian[96], alDir[12], alCn[20];
static uint8_t  alSig   = 3;
static uint16_t alColor = AL_CYAN;
static bool     alValid = false;
// alternates + pick (pages.h 五份黄历); alHash fingerprints the whole text so
// commit can tell "same words again" from "new words".
struct AlAlt { char yi[2][48], ji[2][48], qian[96]; };
static AlAlt    alAlt[ALMANAC_ALT_MAX];
static uint8_t  alAltN = 0, alAltFill = 0;
static uint8_t  alPick = 0;
static uint32_t alHash = 0;
static const char* const AL_PICK_TAG[ALMANAC_ALT_MAX] = {"签二", "签三", "签四", "签五"};
static const char* pkYi(int i)  { return alPick ? alAlt[alPick - 1].yi[i] : alYi[i]; }
static const char* pkJi(int i)  { return alPick ? alAlt[alPick - 1].ji[i] : alJi[i]; }
static const char* pkQian()     { return alPick ? alAlt[alPick - 1].qian : alQian; }

void almanacSet(const char* gz, const char* sx, const char* jc, const char* date,
                const char* yi0, const char* yi1, const char* ji0, const char* ji1,
                const char* qian, const char* dir, int sig,
                const char* cn, uint16_t color) {
  strlcpy(alGz, gz, sizeof(alGz));
  strlcpy(alSx, sx, sizeof(alSx));
  strlcpy(alJc, jc, sizeof(alJc));
  strlcpy(alDate, date, sizeof(alDate));
  strlcpy(alYi[0], yi0, sizeof(alYi[0]));
  strlcpy(alYi[1], yi1, sizeof(alYi[1]));
  strlcpy(alJi[0], ji0, sizeof(alJi[0]));
  strlcpy(alJi[1], ji1, sizeof(alJi[1]));
  strlcpy(alQian, qian, sizeof(alQian));
  strlcpy(alDir, dir, sizeof(alDir));
  strlcpy(alCn, cn, sizeof(alCn));
  alSig = (uint8_t)((sig < 0) ? 0 : (sig > 4 ? 4 : sig));
  alColor = color;
  alValid = true;
}
bool almanacValid() { return alValid; }

void almanacAltClear() { alAltFill = 0; }
void almanacAltAdd(const char* yi0, const char* yi1, const char* ji0, const char* ji1,
                   const char* qian) {
  if (alAltFill >= ALMANAC_ALT_MAX) return;
  AlAlt& a = alAlt[alAltFill++];
  strlcpy(a.yi[0], yi0, sizeof(a.yi[0]));
  strlcpy(a.yi[1], yi1, sizeof(a.yi[1]));
  strlcpy(a.ji[0], ji0, sizeof(a.ji[0]));
  strlcpy(a.ji[1], ji1, sizeof(a.ji[1]));
  strlcpy(a.qian, qian, sizeof(a.qian));
}
static uint32_t alFnv(uint32_t h, const char* s) {
  for (; *s; s++) { h ^= (uint8_t)*s; h *= 16777619u; }
  return h ^ 0xFF;                                   // field separator
}
void almanacCommit() {
  alAltN = alAltFill;
  uint32_t h = 2166136261u;
  h = alFnv(h, alDate); h = alFnv(h, alYi[0]); h = alFnv(h, alYi[1]);
  h = alFnv(h, alJi[0]); h = alFnv(h, alJi[1]); h = alFnv(h, alQian);
  for (int i = 0; i < alAltN; i++) {
    h = alFnv(h, alAlt[i].yi[0]); h = alFnv(h, alAlt[i].yi[1]);
    h = alFnv(h, alAlt[i].ji[0]); h = alFnv(h, alAlt[i].ji[1]); h = alFnv(h, alAlt[i].qian);
  }
  h = alFnv(h, "n") + alAltN;
  if (h != alHash) alPick = 0;                       // new day / new words: back to the own draw
  alHash = h;
  if (alPick > alAltN) alPick = 0;
}
int almanacAltCount() { return alAltN; }
int almanacPick()     { return alPick; }
int almanacPickStep(int dir) {
  int n = alAltN + 1;
  if (n <= 1) return 0;
  alPick = (uint8_t)(((int)alPick + (dir >= 0 ? 1 : -1) + n) % n);
  return alPick;
}

// Anti-aliased CJK text: 4bpp glyphs baked offline from macOS fonts by
// host/gen_almanac_font.py (Songti SC Bold header/seals, Hiragino W6 body)
// and alpha-blended per pixel. Replaced the 11px cubic11 bitmap font, whose
// upscaled blocks read as "low-res" on this 314-PPI panel (user, 2026-08-29).
#include "almanac_font.h"
#include "sdfont.h"

// sd: which card font (sdfont.h) backs this face for glyphs missing in flash
struct AFont { const uint32_t* cp; const uint32_t* off; const uint8_t* data;
               int n; int ascent; int8_t sd; };
static const AFont AF_HEAD = {AFH_CP, AFH_OFF, AFH_DATA, AFH_COUNT, AFH_ASCENT, SDF_HEAD};
static const AFont AF_SEAL = {AFS_CP, AFS_OFF, AFS_DATA, AFS_COUNT, AFS_ASCENT, -1};
static const AFont AF_BODY = {AFB_CP, AFB_OFF, AFB_DATA, AFB_COUNT, AFB_ASCENT, SDF_BODY};
static const AFont AF_QUOT = {AFQ_CP, AFQ_OFF, AFQ_DATA, AFQ_COUNT, AFQ_ASCENT, SDF_QUOT};

static uint16_t blend565(uint16_t bg, uint16_t fg, uint8_t a) {   // a: 0..15
  int br = (bg >> 11) & 31, bgr = (bg >> 5) & 63, bb = bg & 31;
  int fr = (fg >> 11) & 31, fgr = (fg >> 5) & 63, fb = fg & 31;
  return (uint16_t)(((br + ((fr - br) * a) / 15) << 11) |
                    ((bgr + ((fgr - bgr) * a) / 15) << 5) |
                    (bb + ((fb - bb) * a) / 15));
}

static const uint8_t* afGlyph(const AFont& f, uint32_t cp) {
  int lo = 0, hi = f.n - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    if (f.cp[mid] == cp) return f.data + f.off[mid];
    if (f.cp[mid] < cp) lo = mid + 1; else hi = mid - 1;
  }
  return f.sd >= 0 ? sdFontGlyph((uint8_t)f.sd, cp) : nullptr;   // card fallback
}

static uint32_t afDecode(const char*& p) {   // tiny UTF-8 decoder (BMP only)
  uint8_t c = (uint8_t)*p++;
  if (c < 0x80) return c;
  if ((c >> 5) == 6) {
    uint32_t v = (uint32_t)(c & 31) << 6;
    return v | ((uint8_t)*p++ & 63);
  }
  if ((c >> 4) == 14) {
    uint32_t v = (uint32_t)(c & 15) << 12;
    v |= ((uint8_t)*p++ & 63) << 6;
    return v | ((uint8_t)*p++ & 63);
  }
  return '?';
}

// glyph record: w, h, adv, int8 dx, int8 dyTop, then ceil(w/2)*h 4bpp bytes.
// bg must match what the glyph sits on (page black / seal fill).
static int afChar(Arduino_Canvas* c, const AFont& f, int x, int base,
                  uint32_t cp, uint16_t fg, uint16_t bg) {
  const uint8_t* g = afGlyph(f, cp);
  if (!g) { c->drawRect(x + 2, base - 16, 15, 16, fg); return 19; }   // tofu
  int w = g[0], h = g[1], adv = g[2];
  int dx = (int8_t)g[3], dy = (int8_t)g[4];
  const uint8_t* d = g + 5;
  int top = base - f.ascent + dy, stride = (w + 1) / 2;
  for (int yy = 0; yy < h; yy++)
    for (int xx = 0; xx < w; xx++) {
      uint8_t b = d[yy * stride + xx / 2];
      uint8_t a = (xx & 1) ? (b & 15) : (b >> 4);
      if (a) c->drawPixel(x + dx + xx, top + yy,
                          a == 15 ? fg : blend565(bg, fg, a));
    }
  return adv;
}

static int afPrint(Arduino_Canvas* c, const AFont& f, int x, int base,
                   const char* s, uint16_t fg, uint16_t bg = BG) {
  const char* p = s;
  while (*p) x += afChar(c, f, x, base, afDecode(p), fg, bg);
  return x;
}

static int afWidth(const AFont& f, const char* s) {
  int w = 0;
  const char* p = s;
  while (*p) {
    const uint8_t* g = afGlyph(f, afDecode(p));
    w += g ? g[2] : 19;
  }
  return w;
}

// Center a string's ink box (not its advance/ascent box) on (cx, cy): the
// seal characters sat ~7 px low with a hand-picked baseline (user, 2026-09-03).
static void afPrintCenter(Arduino_Canvas* c, const AFont& f, int cx, int cy,
                          const char* s, uint16_t fg, uint16_t bg) {
  int x = 0, left = 1 << 20, right = -(1 << 20), top = 1 << 20, bottom = -(1 << 20);
  for (const char* p = s; *p;) {
    const uint8_t* g = afGlyph(f, afDecode(p));
    if (!g) { x += 19; continue; }
    int w = g[0], h = g[1], dx = (int8_t)g[3], dy = (int8_t)g[4];
    if (w && h) {
      left = min(left, x + dx); right = max(right, x + dx + w);
      top = min(top, dy - f.ascent); bottom = max(bottom, dy - f.ascent + h);
    }
    x += g[2];
  }
  if (right < left) return;
  int ox = cx - (left + right) / 2, base = cy - (top + bottom) / 2;
  afPrint(c, f, ox, base, s, fg, bg);
}

int almanacPrint(Arduino_Canvas* c, int x, int baseline, const char* s,
                 uint16_t fg, uint16_t bg) {
  return afPrint(c, AF_BODY, x, baseline, s, fg, bg);
}
int almanacTextWidth(const char* s) { return afWidth(AF_BODY, s); }

void drawAlmanacPage(Arduino_Canvas* c, uint32_t t) {
  (void)t;
  c->fillScreen(BG);
  if (!alValid) {
    afPrint(c, AF_BODY, (480 - afWidth(AF_BODY, "黄历同步中...")) / 2, 250,
            "黄历同步中...", AL_DIM);
    return;
  }

  // gold Songti header: 乙亥日 · 属马 · 平日, gregorian date right-aligned
  char head[64];
  snprintf(head, sizeof(head), "%s · %s · %s", alGz, alSx, alJc);
  afPrint(c, AF_HEAD, 34, 56, head, AL_GOLD);
  int dw = afWidth(AF_HEAD, alDate);
  afPrint(c, AF_HEAD, 446 - dw, 56, alDate, AL_DIM);
  if (alPick > 0) {   // 签二..签五: which reading this is (up/down swipe cycles them)
    const char* tag = AL_PICK_TAG[alPick - 1];
    afPrint(c, AF_HEAD, 446 - dw - 16 - afWidth(AF_HEAD, tag), 56, tag, AL_CYAN);
  }
  c->fillRect(34, 72, 180, 2, AL_GOLD);
  c->fillRect(214, 73, 232, 1, AL_GOLDDIM);

  // 宜 seal + entries
  c->fillRoundRect(34, 96, 62, 62, 10, AL_RED);
  afPrintCenter(c, AF_SEAL, 65, 127, "宜", AL_SEALTXT, AL_RED);
  afPrint(c, AF_BODY, 108, 124, pkYi(0), AL_WHITE);
  afPrint(c, AF_BODY, 108, 160, pkYi(1), AL_WHITE);

  // 忌 seal + entries
  c->fillRoundRect(34, 186, 62, 62, 10, AL_INK);
  c->drawRoundRect(34, 186, 62, 62, 10, AL_INKBRD);
  afPrintCenter(c, AF_SEAL, 65, 217, "忌", AL_INKTXT, AL_INK);
  afPrint(c, AF_BODY, 108, 214, pkJi(0), AL_GREY);
  afPrint(c, AF_BODY, 108, 250, pkJi(1), AL_GREY);

  // param row: "信号 东南 ▂▄▆█" left, color swatch + name right-aligned
  char sigTxt[32];
  snprintf(sigTxt, sizeof(sigTxt), "信号 %s", alDir);
  afPrint(c, AF_BODY, 34, 338, sigTxt, AL_DIM);
  int bx = 34 + afWidth(AF_BODY, sigTxt) + 14;
  for (int i = 0; i < 4; i++) {
    int h = 9 + 4 * i;
    c->fillRect(bx + i * 11, 338 - h, 7, h, i < alSig ? AL_CYAN : AL_INKBRD);
  }
  int nw = afWidth(AF_BODY, alCn);
  c->fillRoundRect(446 - nw - 28, 319, 20, 20, 4, alColor);
  afPrint(c, AF_BODY, 446 - nw, 338, alCn, AL_DIM);

  // omen line, centered, cyan (24 px: the longest omen + brackets must
  // still clear the margins — 26 px would kiss the bezel)
  char qbuf[112];
  snprintf(qbuf, sizeof(qbuf), "「%s」", pkQian());
  afPrint(c, AF_QUOT, (480 - afWidth(AF_QUOT, qbuf)) / 2, 410, qbuf, AL_CYAN);
}

// ---------------------------------------------------------------- clock v2
// 圆环座舱 (2026-08-29 定稿「蓝环+青字」): breathing
// minute ring (accent blue) + orbiting white second dot, wrapped around
// C-direction cockpit content — gold Songti sexagenary/weekday header,
// Menlo Bold time with cyan seconds, and the true-computed 时辰 ink panel.
static const AFont AF_TINY = {AFT_CP, AFT_OFF, AFT_DATA, AFT_COUNT, AFT_ASCENT, SDF_TINY};
static const AFont AF_MBIG = {AFM_CP, AFM_OFF, AFM_DATA, AFM_COUNT, AFM_ASCENT, -1};
static const AFont AF_MSEC = {AFN_CP, AFN_OFF, AFN_DATA, AFN_COUNT, AFN_ASCENT, -1};
static const AFont AF_MDAT = {AFO_CP, AFO_OFF, AFO_DATA, AFO_COUNT, AFO_ASCENT, -1};

static const uint16_t CK_TRACK  = C565(0x14, 0x17, 0x1C);   // ring groove

static const char* const CK_WEEKDAY[7] = {"日", "一", "二", "三", "四", "五", "六"};
// afPrint with per-glyph tracking (the 96 px Menlo digits sit too loose at
// their natural advance; the mock uses about -6 px).
static int afPrintTrack(Arduino_Canvas* c, const AFont& f, int x, int base,
                        const char* s, uint16_t fg, int track) {
  const char* p = s;
  while (*p) x += afChar(c, f, x, base, afDecode(p), fg, BG) + track;
  return x - track;
}

static int afWidthTrack(const AFont& f, const char* s, int track) {
  int w = 0, n = 0;
  const char* p = s;
  while (*p) {
    const uint8_t* g = afGlyph(f, afDecode(p));
    w += g ? g[2] : 19;
    n++;
  }
  return n > 1 ? w + track * (n - 1) : w;
}

void drawClockPage(Arduino_Canvas* c, uint32_t t, uint16_t accent) {
  (void)t;
  c->fillScreen(BG);
  bool ok = clockValid();

  // ring groove is always on; progress + dot only once the clock is set
  c->fillArc(240, 240, 218, 214, 0, 360, CK_TRACK);

  if (!ok) {
    afPrintTrack(c, AF_MBIG, (480 - afWidthTrack(AF_MBIG, "--:--", -6)) / 2,
                 262, "--:--", GREYDIM, -6);
    c->setTextSize(1);
    c->setFont(u8g2_font_fur17_tr);
    int w = trackWidth(c, "WAITING FOR HOST", 3);
    trackPrint(c, (480 - w) / 2, 330, "WAITING FOR HOST", 3, GREYDIM);
    c->setFont((const GFXfont*)nullptr);
    return;
  }

  struct tm tmv;
  localTm(tmv);
  float msFrac = ((millis() - baseMs) % 1000) / 1000.0f;

  // minute-progress arc from 12 o'clock (angles clockwise, 0 deg = east)
  float sweep = ((tmv.tm_min * 60 + tmv.tm_sec) + msFrac) * 360.0f / 3600.0f;
  if (sweep > 1.0f) {
    if (sweep <= 90.0f) {
      c->fillArc(240, 240, 218, 214, 270, 270 + sweep, accent);
    } else {
      c->fillArc(240, 240, 218, 214, 270, 360, accent);
      c->fillArc(240, 240, 218, 214, 0, sweep - 90.0f, accent);
    }
  }
  // orbiting second dot rides on the ring
  float sa = (270.0f + (tmv.tm_sec + msFrac) * 6.0f) * 0.0174533f;
  c->fillCircle(240 + (int)(216 * cosf(sa)), 240 + (int)(216 * sinf(sa)), 7,
                WHITE);

  char ds[16];
  snprintf(ds, sizeof(ds), "%04d%02d%02d", tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday);
  afPrintTrack(c, AF_MDAT, (480 - afWidthTrack(AF_MDAT, ds, 2)) / 2, 100, ds, AL_DIM, 2);
  c->fillRect(240 - 32, 112, 64, 2, AL_GOLD);

  // big Menlo time + cyan seconds on the shared baseline
  char ts[8], ss[4];
  snprintf(ts, sizeof(ts), "%02d:%02d", tmv.tm_hour, tmv.tm_min);
  snprintf(ss, sizeof(ss), "%02d", tmv.tm_sec);
  int tw = afWidthTrack(AF_MBIG, ts, -6);
  int sw = afWidthTrack(AF_MSEC, ss, 0);
  int tx = 240 - (tw + 12 + sw) / 2;
  afPrintTrack(c, AF_MBIG, tx, 262, ts, AL_WHITE, -6);
  afPrintTrack(c, AF_MSEC, tx + tw + 12, 262, ss, AL_CYAN, 0);

  char weekday[32];
  if (langEn()) snprintf(weekday, sizeof(weekday), "%s", EN_WEEKDAY[tmv.tm_wday]);
  else snprintf(weekday, sizeof(weekday), "星期%s", CK_WEEKDAY[tmv.tm_wday]);
  afPrint(c, AF_HEAD, (480 - afWidth(AF_HEAD, weekday)) / 2, 356, weekday, AL_DIM);
}

// ---------------------------------------------------------------- Codex usage
#include "usage_font.h"
static const AFont UF_HERO = {AFU_CP, AFU_OFF, AFU_DATA, AFU_COUNT, AFU_ASCENT, -1};
static const AFont UF_STAT = {AFV_CP, AFV_OFF, AFV_DATA, AFV_COUNT, AFV_ASCENT, -1};
static const AFont UF_TEXT = {AFW_CP, AFW_OFF, AFW_DATA, AFW_COUNT, AFW_ASCENT, -1};
static const AFont UF_SMALL = {AFX_CP, AFX_OFF, AFX_DATA, AFX_COUNT, AFX_ASCENT, -1};
static const uint16_t US_PURPLE = C565(0x6F, 0x78, 0xFF);
static const uint16_t US_MUTED = C565(0x92, 0x98, 0xA2);
static const uint16_t US_TRACK = C565(0x30, 0x32, 0x38);
static int64_t usageToday = -1, usageWeek = -1;
static char usageDate[16] = "", usageStatus[16] = "UNAVAILABLE";
static uint32_t usageAt = 0;
static bool usageTokensStale = true;
static bool usageTodayStale = true;
struct UsageWindow { char label[12], reset[24]; float left; };
static UsageWindow usageWindows[2];
static uint8_t usageN = 0;

void usageSet(JsonDocument& d) {
  usageTokensStale = d["tokens_stale"] | true;
  usageTodayStale = d["today_stale"] | true;
  usageToday = d["today"].isNull() ? -1 : d["today"].as<int64_t>();
  usageWeek = d["week"].isNull() ? -1 : d["week"].as<int64_t>();
  strlcpy(usageDate, d["date"] | "", sizeof(usageDate));
  strlcpy(usageStatus, d["status"] | "UNAVAILABLE", sizeof(usageStatus));
  usageN = 0;
  for (JsonObject w : d["windows"].as<JsonArray>()) {
    if (usageN == 2) break;
    UsageWindow& dst = usageWindows[usageN++];
    strlcpy(dst.label, w["label"] | "?", sizeof(dst.label));
    strlcpy(dst.reset, w["reset"] | "--", sizeof(dst.reset));
    dst.left = constrain(w["left"] | 0.0f, 0.0f, 100.0f);
  }
  usageAt = millis();
}

static void tokenText(char* out, size_t n, int64_t value) {
  if (value < 0) strlcpy(out, "--", n);
  else if (value >= 1000000000LL) snprintf(out, n, "%.2fB", value / 1e9);
  else if (value >= 1000000) snprintf(out, n, "%.2fM", value / 1e6);
  else if (value >= 1000) snprintf(out, n, "%.1fK", value / 1e3);
  else snprintf(out, n, "%lld", (long long)value);
}

void drawUsagePage(Arduino_Canvas* c, uint32_t t, uint16_t accent) {
  (void)accent;
  c->fillScreen(BG);
  c->fillCircle(28, 36, 6, US_PURPLE);
  afPrintTrack(c, UF_TEXT, 43, 44, "CODEX", WHITE, 2);
  afPrint(c, UF_SMALL, 334 - afWidth(UF_SMALL, usageDate), 41, usageDate, US_MUTED);
  c->fillRect(22, 63, 436, 1, C565(0x20, 0x23, 0x28));
  char text[64];
  if (usageN && !strcmp(usageWindows[0].label, "7d")) strlcpy(text, "WEEKLY USED", sizeof(text));
  else if (usageN) snprintf(text, sizeof(text), "%s USED", usageWindows[0].label);
  else strlcpy(text, "QUOTA USED", sizeof(text));
  afPrintTrack(c, UF_TEXT, 22, 95, text, US_MUTED, 1);
  if (usageN) snprintf(text, sizeof(text), "%.0f%% LEFT", usageWindows[0].left);
  else strlcpy(text, "UNAVAILABLE", sizeof(text));
  afPrint(c, UF_SMALL, 458 - afWidth(UF_SMALL, text), 92, text, US_PURPLE);
  if (usageN) snprintf(text, sizeof(text), "%.0f%%", 100 - usageWindows[0].left);
  else strlcpy(text, "--", sizeof(text));
  afPrintTrack(c, UF_HERO, 16, 270, text, WHITE, -9);
  c->fillRoundRect(22, 304, 436, 20, 10, US_TRACK);
  int width = usageN ? (int)(436 * (100 - usageWindows[0].left) / 100) : 0;
  if (width) c->fillRoundRect(22, 304, width, 20, min(10, width / 2), US_PURPLE);
  if (usageN) snprintf(text, sizeof(text), "RESET %s UTC+8", usageWindows[0].reset);
  else strlcpy(text, "WAITING FOR ACCOUNT", sizeof(text));
  afPrint(c, UF_SMALL, 22, 350, text, US_MUTED);
  for (int i = 0; i < 2; i++) {
    int x = 22 + i * 240;
    tokenText(text, sizeof(text), i ? usageWeek : usageToday);
    afPrint(c, UF_STAT, x, 402, text, US_PURPLE);
    afPrintTrack(c, UF_SMALL, x, 426, i ? "WEEK / ACCOUNT" : "TODAY / THIS MAC", US_MUTED, 1);
  }
  bool stale = !usageAt || (int32_t)(t - usageAt) > 90000;
  const char* status = stale ? "HOST OFFLINE / STALE" : usageStatus;
  afPrint(c, UF_SMALL, 22, 459, status, US_MUTED);
  if (usageN > 1) {
    snprintf(text, sizeof(text), "%s %.0f%% LEFT", usageWindows[1].label, usageWindows[1].left);
  } else {
    strlcpy(text, usageTodayStale ? "LOCAL STALE" : usageTokensStale ? "ACCOUNT STALE" : usageWeek < 0 ? "WEEK PENDING" : "LOCAL + ACCOUNT", sizeof(text));
  }
  afPrint(c, UF_SMALL, 458 - afWidth(UF_SMALL, text), 459, text, US_MUTED);
}

// ---------------------------------------------------------------- tasks (calendar placement)
static const AFont TF_TITLE = {AFY_CP, AFY_OFF, AFY_DATA, AFY_COUNT, AFY_ASCENT, -1};
static const uint16_t TD_CARD = C565(0x12, 0x14, 0x19);
struct TodoItem { char id[13], title[145]; bool done, important; };
static TodoItem todoItems[16];
static uint8_t todoN = 0, todoOrder[16], todoOffset = 0, todoView = 0;
static uint32_t todoRev = 0, todoAt = 0, todoUndoUntil = 0, todoPendingAt = 0, todoErrorUntil = 0;
static uint32_t todoRequestAt = 0;
static bool todoBusy = false, todoLoaded = false, todoPendingDone = false;
static char todoDate[12] = "", todoCurrent[13] = "", todoPending[13] = "";
static uint32_t focusEnd = 0, focusLeft = 25 * 60000;
static bool focusRunning = false;

static void todoSort() {
  for (int i = 0; i < todoN; i++) todoOrder[i] = i;
  for (int i = 1; i < todoN; i++) {
    uint8_t v = todoOrder[i]; int j = i;
    int score = todoItems[v].done * 2 + !todoItems[v].important;
    while (j > 0) {
      TodoItem& p = todoItems[todoOrder[j - 1]];
      if (p.done * 2 + !p.important <= score) break;
      todoOrder[j] = todoOrder[j - 1]; j--;
    }
    todoOrder[j] = v;
  }
}

void todoSet(JsonDocument& d) {
  uint32_t rev = d["rev"] | 0U;
  if (rev != todoRev || d["ok"].is<bool>()) todoBusy = false;
  todoRev = rev;
  if (strlen(d["error"] | "")) { todoErrorUntil = millis() + 4000; todoPendingAt = 0; }
  uint8_t oldN = todoN;
  todoN = 0;
  for (JsonObject item : d["items"].as<JsonArray>()) {
    if (todoN == 16) break;
    TodoItem& dst = todoItems[todoN++];
    strlcpy(dst.id, item["id"] | "", sizeof(dst.id));
    strlcpy(dst.title, item["title"] | "", sizeof(dst.title));
    dst.done = item["done"] | false;
    dst.important = item["important"] | false;
  }
  strlcpy(todoDate, d["date"] | "", sizeof(todoDate));
  todoUndoUntil = millis() + min(10000U, d["undo_ms"] | 0U);
  todoAt = millis(); todoLoaded = true;
  if (oldN != todoN || !todoPendingAt) todoSort();
  todoOffset = min((int)todoOffset, todoN ? ((todoN - 1) / 3) * 3 : 0);
}

static TodoItem* todoChosen() {
  for (int i = 0; i < todoN; i++) if (!strcmp(todoItems[i].id, todoCurrent)) return &todoItems[i];
  return nullptr;
}

// Wrap by glyph, with an ellipsis on the final line. Consume unsupported emoji as one '?'.
static void todoText(Arduino_Canvas* c, const char* text, int x, int y, int width,
                     int lines, uint16_t color, uint16_t bg = BG) {
  const char* p = text; int pen = x, line = 0;
  while (*p && line < lines) {
    uint32_t cp;
    if (((uint8_t)*p & 0xF8) == 0xF0) { cp = '?'; p += 4; }
    else cp = afDecode(p);
    const uint8_t* g = afGlyph(TF_TITLE, cp);
    if (!g) { cp = '?'; g = afGlyph(TF_TITLE, cp); }
    int advance = g ? g[2] : 19;
    if (pen + advance > x + width) { line++; pen = x; }
    if (line >= lines) break;
    if (line == lines - 1 && *p && pen + advance + 26 > x + width) {
      afChar(c, TF_TITLE, pen, y + line * 34, 0x2026, color, bg); break;
    }
    pen += afChar(c, TF_TITLE, pen, y + line * 34, cp, color, bg);
  }
}

static uint32_t focusRemaining(uint32_t now) {
  if (!focusRunning) return focusLeft;
  int32_t left = (int32_t)(focusEnd - now);
  if (left <= 0) { focusRunning = false; focusLeft = 0; return 0; }
  return (uint32_t)left;
}

static void todoButton(Arduino_Canvas* c, int x, int y, int w, const char* text, bool primary) {
  uint16_t bg = primary ? US_PURPLE : TD_CARD;
  c->fillRoundRect(x, y, w, 60, 18, bg);
  afPrintCenter(c, TF_TITLE, x + w / 2, y + 30, text, WHITE, bg);
}

void drawTodoPage(Arduino_Canvas* c, uint32_t t) {
  c->fillScreen(BG);
  if (todoBusy && (int32_t)(t - todoRequestAt) > 5000) {
    todoBusy = false; todoErrorUntil = t + 4000;
  }
  if (todoPendingAt && !todoBusy && (int32_t)(t - todoPendingAt) > 650) { todoSort(); todoPendingAt = 0; }
  TodoItem* chosen = todoChosen();
  if (todoView && !chosen) todoView = 0;
  if (todoView) {
    afPrint(c, UF_TEXT, 26, 48, "<", US_MUTED);
    afPrintTrack(c, UF_SMALL, 70, 45, todoView == 2 ? "FOCUS SESSION" : "ONE THING AT A TIME", US_MUTED, 1);
    todoText(c, chosen->title, 26, 108, 428, todoView == 2 ? 2 : 5, WHITE);
    if (todoView == 2) {
      uint32_t seconds = (focusRemaining(t) + 999) / 1000;
      char text[12]; snprintf(text, sizeof(text), "%02u:%02u", (unsigned)(seconds / 60), (unsigned)(seconds % 60));
      c->setFont(u8g2_font_logisoso78_tn); c->setTextSize(1);
      int w = trackWidth(c, text, 0);
      trackPrint(c, (480 - w) / 2, 278, text, 0, WHITE);
      c->fillRoundRect(26, 307, 428, 4, 2, US_TRACK);
      int done = 428 * (1.0f - focusRemaining(t) / 1500000.0f);
      if (done > 0) c->fillRoundRect(26, 307, min(done, 428), 4, 2, US_PURPLE);
      todoButton(c, 26, 359, 200, focusRunning ? "暂停" : focusLeft ? "继续" : "再来一次", false);
      todoButton(c, 240, 359, 214, "完成任务", true);
    } else {
      todoButton(c, 26, 285, 428, chosen->important ? "取消置顶" : "置顶任务", false);
      todoButton(c, 26, 359, 200, chosen->done ? "恢复待办" : "完成任务", false);
      todoButton(c, 240, 359, 214, "开始专注", true);
    }
  } else {
    afPrint(c, TF_TITLE, 26, 48, "今日待办", WHITE);
    afPrint(c, UF_SMALL, 334 - afWidth(UF_SMALL, todoDate), 43, todoDate, US_MUTED);
    int left = 0; for (int i = 0; i < todoN; i++) if (!todoItems[i].done) left++;
    char text[24]; snprintf(text, sizeof(text), "%02d", left);
    afPrint(c, UF_STAT, 26, 101, text, WHITE);
    afPrintTrack(c, UF_SMALL, 91, 98, "TASKS LEFT", US_MUTED, 1);
    snprintf(text, sizeof(text), "%d / %d DONE", todoN - left, todoN);
    afPrint(c, UF_SMALL, 454 - afWidth(UF_SMALL, text), 98, text, US_PURPLE);
    c->fillRoundRect(26, 117, 428, 3, 1, US_TRACK);
    if (todoN > left) c->fillRoundRect(26, 117, 428 * (todoN - left) / todoN, 3, 1, US_PURPLE);
    if (!todoN) {
      miniEyes(c, t, 216);
      afPrintCenter(c, TF_TITLE, 240, 304, todoLoaded ? "给今天留一点空间" : "等待 Mac 连接", WHITE, BG);
      afPrintCenter(c, UF_SMALL, 240, 345, "ADD A TASK ON YOUR MAC", US_MUTED, BG);
    }
    for (int row = 0; row < 3 && todoOffset + row < todoN; row++) {
      TodoItem& item = todoItems[todoOrder[todoOffset + row]];
      int y = 136 + row * 90;
      bool checked = todoPendingAt && !strcmp(item.id, todoPending) ? todoPendingDone : item.done;
      c->fillRoundRect(22, y, 436, 80, 16, TD_CARD);
      c->drawCircle(47, y + 40, 13, checked ? US_PURPLE : US_MUTED);
      if (checked) {
        c->fillCircle(47, y + 40, 13, US_PURPLE);
        c->drawLine(41, y + 40, 46, y + 45, WHITE); c->drawLine(46, y + 45, 54, y + 35, WHITE);
      }
      todoText(c, item.title, 78, y + 31, 342, 2, checked ? US_MUTED : WHITE, TD_CARD);
      if (checked) c->drawLine(78, y + 26, 410, y + 26, US_MUTED);
      if (item.important) c->fillCircle(437, y + 17, 3, US_PURPLE);
    }
    if (todoBusy) {
      c->fillRoundRect(149, 419, 182, 44, 22, TD_CARD);
      afPrintCenter(c, TF_TITLE, 240, 441, "正在保存…", WHITE, TD_CARD);
    } else if ((int32_t)(todoUndoUntil - t) > 0) {
      c->fillRoundRect(149, 419, 182, 44, 22, TD_CARD);
      afPrintCenter(c, TF_TITLE, 240, 441, "撤销操作", WHITE, TD_CARD);
    } else {
      afPrint(c, UF_SMALL, 26, 453, "SWIPE TO BROWSE", US_MUTED);
      snprintf(text, sizeof(text), "%d / %d", todoN ? todoOffset / 3 + 1 : 1, max(1, (todoN + 2) / 3));
      afPrint(c, UF_SMALL, 454 - afWidth(UF_SMALL, text), 453, text, US_MUTED);
    }
  }
  if (todoView && todoBusy) afPrintCenter(c, TF_TITLE, 240, 455, "正在保存…", US_PURPLE, BG);
  if ((int32_t)(todoErrorUntil - t) > 0) {
    c->fillRoundRect(85, 303, 310, 48, 24, US_TRACK);
    afPrintCenter(c, TF_TITLE, 240, 327, "同步失败，请重试", WHITE, US_TRACK);
  }
  else if (!todoAt || (int32_t)(t - todoAt) > 90000)
    afPrint(c, UF_SMALL, 26, 473, "MAC OFFLINE - READ ONLY", US_MUTED);
}

bool todoTouch(JsonDocument& command, int x, int y, int dx, int dy) {
  uint32_t now = millis();
  if (todoBusy) return false;  // the saving indicator already explains the pending action
  TodoItem* item = todoChosen();
  bool swipe = abs(dx) > 40 || abs(dy) > 40;
  if (todoView) {
    if ((swipe && dx > 60) || (!swipe && x < 90 && y < 80)) { todoView = 0; return false; }
    if (swipe || !item) return false;
    if (y >= 359 && y < 419 && ((todoView == 1 && x >= 240) || (todoView == 2 && x < 230))) {
      if (todoView == 1) {
        todoView = 2; focusLeft = 1500000; focusRunning = true; focusEnd = now + focusLeft;
      } else if (focusRunning) {
        focusLeft = focusRemaining(now); focusRunning = false;
      } else {
        if (!focusLeft) focusLeft = 1500000;
        focusEnd = now + focusLeft; focusRunning = true;
      }
      return false;
    }
    if (todoView == 1 && y >= 285 && y < 345) command["important"] = !item->important;
    else if (y >= 359 && y < 419) {
      command["done"] = todoView == 2 ? true : !item->done;
      focusRunning = false; todoView = 0;
    } else return false;
    command["op"] = "set"; command["id"] = item->id;
  } else {
    if (swipe) {
      if (abs(dy) > abs(dx)) todoOffset = constrain((int)todoOffset + (dy < 0 ? 3 : -3), 0, todoN ? ((todoN - 1) / 3) * 3 : 0);
      return false;
    }
    if (y >= 419 && y < 465 && x >= 149 && x <= 331 && (int32_t)(todoUndoUntil - now) > 0) {
      command["op"] = "undo";
    } else if (y >= 136 && y < 396 && (y - 136) % 90 < 80) {
      int pos = todoOffset + (y - 136) / 90;
      if (pos >= todoN) return false;
      item = &todoItems[todoOrder[pos]];
      if (x >= 78) { strlcpy(todoCurrent, item->id, sizeof(todoCurrent)); todoView = 1; return false; }
      command["op"] = "set"; command["id"] = item->id; command["done"] = !item->done;
    } else return false;
  }
  if (!todoAt || (int32_t)(now - todoAt) > 90000) { todoErrorUntil = now + 4000; return false; }
  todoPending[0] = 0;
  if (command["done"].is<bool>()) {
    strlcpy(todoPending, command["id"] | "", sizeof(todoPending));
    todoPendingDone = command["done"].as<bool>();
  }
  command["t"] = "todo_action"; command["rev"] = todoRev;
  todoPendingAt = todoRequestAt = now; todoBusy = true;
  return true;
}

// ---------------------------------------------------------------- play page
// 播放页 ×2: the clock
// orientation's two flip sides share one compact media card. 当前播放 (left
// swipe) shows the Mac's track ({"t":"np"}); 播客 (right swipe) the board's
// own player (player.h). Square 480×480 layout, design px = device px:
//   card 448×224 at (16,20) r26  — label 18 px / 116 px artwork / two-line
//   26 px title / 18 px subtitle / 4 px slider with elapsed and -remaining
//   band y 264..424 — three 160×160 hit zones (prev|toggle|next; ±15 s on the
//   podcast page), glyphs at y=344, the middle one in an 88 px disc
//   bottom 56 px — the volume toast after an up/down swipe
#include "player.h"
#include "jpegrom.h"

// 18 px companion to almanacPrint, for English strings that overflow the
// 26 px face in a fixed box (face bubbles).
int almanacPrintSmall(Arduino_Canvas* c, int x, int baseline, const char* s,
                      uint16_t fg, uint16_t bg) {
  return afPrint(c, AF_TINY, x, baseline, s, fg, bg);
}
int almanacTextWidthSmall(const char* s) { return afWidth(AF_TINY, s); }

static const uint16_t PM_TRACK = C565(0x22, 0x26, 0x2E);   // slider groove
static const uint16_t PM_PLACE = C565(0x1A, 0x1F, 0x2A);   // artwork placeholder

enum { CARD_X = 16, CARD_Y = 20, CARD_W = 448, CARD_H = 224, CARD_R = 26,
       LABEL_BASE = 52, ART_X = 36, ART_Y = 66, ART_S = 116, ART_R = 18,
       TEXT_X = 168, TEXT_W = 276, SLIDER_X = 36, SLIDER_Y = 196, SLIDER_W = 408,
       TIME_BASE = 221, BAND_Y = 264, BAND_H = 160, GLYPH_CY = 344, PLAY_R = 44,
       VOL_Y = 435 };

// ---- Mac "now playing" state (host {"t":"np"}) ----------------------------
static bool  npHas = false, npIsPlaying = false;
static char  npTitle[128] = "", npArtist[128] = "", npAlbum[128] = "";
static char  npApp[32] = "", npCover[80] = "";
static float npPos = 0, npDur = 0, npRate = 1;
static uint32_t npAt = 0;          // millis() when pos was true

// Copy at most n-1 bytes, never splitting a UTF-8 sequence (host truncates
// by characters, but a BLE-shortened line must not leave half a glyph).
static void utf8Copy(char* dst, size_t n, const char* src) {
  if (!n) return;
  size_t len = strlen(src);
  if (len >= n) {
    len = n - 1;
    while (len && ((uint8_t)src[len] & 0xC0) == 0x80) len--;   // back to a lead byte
  }
  memcpy(dst, src, len);
  dst[len] = 0;
}
static void utf8Cat(char* dst, size_t n, const char* src) {
  size_t len = strlen(dst);
  if (len + 1 < n) utf8Copy(dst + len, n - len, src);
}

void npSet(bool on, const char* title, const char* artist, const char* album,
           float pos, float dur, bool play, float rate, const char* app,
           const char* cover) {
  npHas = on;
  if (!on) { npIsPlaying = false; npPos = npDur = 0; npCover[0] = 0; return; }
  utf8Copy(npTitle, sizeof(npTitle), title);
  utf8Copy(npArtist, sizeof(npArtist), artist);
  utf8Copy(npAlbum, sizeof(npAlbum), album);
  utf8Copy(npApp, sizeof(npApp), app);
  utf8Copy(npCover, sizeof(npCover), cover);
  npPos = pos < 0 ? 0 : pos;
  npDur = dur < 0 ? 0 : dur;
  npIsPlaying = play;
  npRate = (rate > 0.05f && rate < 8.0f) ? rate : 1.0f;
  npAt = millis();
}
bool npOn()      { return npHas; }
bool npPlaying() { return npHas && npIsPlaying; }

// Mac position between pushes: the host only speaks on change and every 15 s,
// so the slider runs locally from the last known point.
static float npPosNow() {
  float p = npPos;
  if (npIsPlaying) p += (float)(uint32_t)(millis() - npAt) / 1000.0f * npRate;
  if (npDur > 0 && p > npDur) p = npDur;
  return p < 0 ? 0 : p;
}
void npSnapshot(float* pos, float* dur, bool* play) {
  if (pos)  *pos  = npHas ? npPosNow() : 0.0f;
  if (dur)  *dur  = npHas ? npDur : 0.0f;
  if (play) *play = npHas && npIsPlaying;
}

// The app the Mac is playing in, in the label's language.
static const char* npAppName() {
  if (!strcmp(npApp, "netease"))  return tr(S_APP_NETEASE);
  if (!strcmp(npApp, "music"))    return tr(S_APP_MUSIC);
  if (!strcmp(npApp, "podcasts")) return tr(S_APP_PODCASTS);
  if (!strcmp(npApp, "chrome"))   return tr(S_APP_CHROME);
  if (!strcmp(npApp, "safari"))   return tr(S_APP_SAFARI);
  return npApp;
}

// ---- text fitting (cached: a card-backed glyph lookup is a microSD read) ---
// One line clipped to maxW with an ellipsis; single pass, remembers the last
// cut that still leaves room for the "…".
static void afFit(char* out, size_t n, const AFont& f, const char* s, int maxW) {
  if (n < 8) { if (n) out[0] = 0; return; }
  int ell = afWidth(f, "…"), w = 0;
  size_t cut = 0;
  const char* p = s;
  while (*p) {
    const uint8_t* g = afGlyph(f, afDecode(p));
    int adv = g ? g[2] : 19;
    if (w + adv > maxW) {
      if (cut > n - 4) cut = n - 4;
      memcpy(out, s, cut);
      out[cut] = 0;
      strlcat(out, "…", n);
      return;
    }
    w += adv;
    if (w + ell <= maxW) cut = (size_t)(p - s);
  }
  utf8Copy(out, n, s);
}

// Claim card (face.cpp): one line fitted to maxW in body26 / tiny18, and
// "can every glyph be drawn" (flash or card) for the ASCII-name fallback.
void almanacFit(char* out, size_t n, const char* s, int maxW, bool small) {
  afFit(out, n, small ? AF_TINY : AF_BODY, s, maxW);
}
bool almanacHasAll(const char* s) {
  for (const char* p = s; *p;) {
    uint32_t cp = afDecode(p);
    if (cp >= 0x80 && !afGlyph(AF_BODY, cp)) return false;
  }
  return true;
}

// Claude 多会话 top row (face.cpp): tiny18 with +track px between
// glyphs, the canvas's "+1 px 字距". Printed on pure black (the row's backing
// is #000), so afPrintTrack's BG blend is the right one.
int almanacPrintSmallTrack(Arduino_Canvas* c, int x, int baseline, const char* s,
                           uint16_t fg, int track) {
  return *s ? afPrintTrack(c, AF_TINY, x, baseline, s, fg, track) : x;
}
int almanacTextWidthSmallTrack(const char* s, int track) {
  return afWidthTrack(AF_TINY, s, track);
}
// The canvas's fit(): the whole string when it is ≤ maxW wide, else the
// longest prefix whose "prefix…" stays within maxW - 8. Tracking counted, so
// a 300 px fit really ends at 300 px (afFit measures without it).
void almanacFitSmallTrack(char* out, size_t n, const char* s, int maxW, int track) {
  if (n < 8) { if (n) out[0] = 0; return; }
  if (afWidthTrack(AF_TINY, s, track) <= maxW) { utf8Copy(out, n, s); return; }
  int budget = maxW - 8 - afWidth(AF_TINY, "…") - track;   // prefix + gap + "…"
  int w = 0, glyphs = 0;
  size_t cut = 0;
  for (const char* p = s; *p;) {
    const uint8_t* g = afGlyph(AF_TINY, afDecode(p));
    int nw = w + (glyphs ? track : 0) + (g ? g[2] : 19);
    if (nw > budget) break;
    w = nw; glyphs++;
    cut = (size_t)(p - s);
  }
  if (cut > n - 4) cut = n - 4;
  while (cut && ((uint8_t)s[cut] & 0xC0) == 0x80) cut--;   // never half a glyph
  memcpy(out, s, cut);
  out[cut] = 0;
  strlcat(out, "…", n);
}
// Every glyph drawable in tiny18 (flash or card), and no 4-byte UTF-8 (emoji):
// afDecode is BMP-only and would print one "?" per byte of it.
bool almanacHasAllSmall(const char* s) {
  for (const char* p = s; *p;) {
    uint8_t b = (uint8_t)*p;
    if (b >= 0x80 && (b >> 5) != 6 && (b >> 4) != 14) return false;
    uint32_t cp = afDecode(p);
    if (cp >= 0x80 && !afGlyph(AF_TINY, cp)) return false;
  }
  return true;
}

// Two lines: the first breaks where the next glyph would overflow (after the
// last space if the break falls inside a Latin word), the second is fitted
// with an ellipsis. Returns the number of lines used (1 or 2).
static int afWrap2(char* l1, char* l2, size_t n, const AFont& f, const char* s, int maxW) {
  int w = 0;
  const char* p = s;
  const char* brk = nullptr;          // where line 1 ends
  const char* lastSpace = nullptr;
  while (*p) {
    const char* q = p;
    uint32_t cp = afDecode(q);
    const uint8_t* g = afGlyph(f, cp);
    int adv = g ? g[2] : 19;
    if (w + adv > maxW) { brk = p; break; }
    w += adv;
    if (cp == ' ') lastSpace = q;      // break after the space
    p = q;
  }
  if (!brk) { utf8Copy(l1, n, s); l2[0] = 0; return 1; }
  if (lastSpace && (brk - s) > 8 && (brk - lastSpace) < 20) brk = lastSpace;
  size_t len = (size_t)(brk - s);
  if (len >= n) len = n - 1;
  memcpy(l1, s, len); l1[len] = 0;
  while (*brk == ' ') brk++;
  afFit(l2, n, f, brk, maxW);
  return 2;
}

// Per-slot cache: 0 title (two lines), 1 subtitle, 2 label, 3 hint.
static char pmKey[4][320], pmL1[4][320], pmL2[4][320];
static int  pmLines[4];
static void pmText(int slot, const AFont& f, const char* s, int maxW, bool two) {
  if (strcmp(pmKey[slot], s) == 0) return;
  utf8Copy(pmKey[slot], sizeof(pmKey[slot]), s);
  if (two) pmLines[slot] = afWrap2(pmL1[slot], pmL2[slot], sizeof(pmL1[slot]), f, s, maxW);
  else   { afFit(pmL1[slot], sizeof(pmL1[slot]), f, s, maxW); pmL2[slot][0] = 0; pmLines[slot] = 1; }
}

// ---- artwork: the 200 px card cover scaled once to 116 px, corners rounded
// into the card colour so the blit is a single rectangle -------------------
static uint16_t* pmArt = nullptr;
static char      pmArtKey[80] = "";
static bool      pmArtOk = false;

static inline bool pmCornerOut(int x, int y, int s, int r) {
  int cx = x < r ? r : (x >= s - r ? s - 1 - r : -1);
  int cy = y < r ? r : (y >= s - r ? s - 1 - r : -1);
  if (cx < 0 || cy < 0) return false;                 // not in a corner square
  int dx = x - cx, dy = y - cy;
  return dx * dx + dy * dy > r * r;
}

static const uint16_t* pmArtGet(const char* path) {
  if (!path || !path[0]) return nullptr;
  if (pmArtOk && strcmp(path, pmArtKey) == 0) return pmArt;
  CoverImg img = coverGet(path);                      // cached decode / cached failure
  if (!img.px || img.w <= 0 || img.h <= 0) { pmArtOk = false; return nullptr; }
  if (!pmArt) pmArt = (uint16_t*)heap_caps_malloc((size_t)ART_S * ART_S * 2, MALLOC_CAP_SPIRAM);
  if (!pmArt) return nullptr;
  for (int y = 0; y < ART_S; y++) {
    int sy = y * img.h / ART_S;
    for (int x = 0; x < ART_S; x++) {
      int sx = x * img.w / ART_S;
      pmArt[y * ART_S + x] = pmCornerOut(x, y, ART_S, ART_R)
                               ? AL_INK : img.px[(size_t)sy * COVER_MAX + sx];
    }
  }
  snprintf(pmArtKey, sizeof(pmArtKey), "%s", path);
  pmArtOk = true;
  return pmArt;
}

static void pmPlaceholder(Arduino_Canvas* c) {
  c->fillRoundRect(ART_X, ART_Y, ART_S, ART_S, ART_R, PM_PLACE);
  c->drawRoundRect(ART_X, ART_Y, ART_S, ART_S, ART_R, AL_INKBRD);
  int cx = ART_X + ART_S / 2, cy = ART_Y + ART_S / 2;   // a beamed pair of notes
  c->fillCircle(cx - 13, cy + 13, 7, AL_DIM);
  c->fillCircle(cx + 11, cy + 8, 7, AL_DIM);
  c->fillRect(cx - 8, cy - 14, 3, 27, AL_DIM);
  c->fillRect(cx + 16, cy - 19, 3, 27, AL_DIM);
  c->fillTriangle(cx - 8, cy - 14, cx + 19, cy - 19, cx + 19, cy - 14, AL_DIM);
  c->fillTriangle(cx - 8, cy - 14, cx - 8, cy - 9, cx + 19, cy - 14, AL_DIM);
}

// ---- glyphs, SF-style, drawn (never fonts) --------------------------------
static void pmPlayPause(Arduino_Canvas* c, int cx, int cy, bool playing, uint16_t col) {
  if (playing) {                                       // ▮▮ = "pause me"
    c->fillRoundRect(cx - 14, cy - 14, 10, 28, 2, col);
    c->fillRoundRect(cx + 4,  cy - 14, 10, 28, 2, col);
  } else {                                             // ▶ = "play me"
    c->fillTriangle(cx - 11, cy - 15, cx - 11, cy + 15, cx + 17, cy, col);
  }
}
static void pmSkip(Arduino_Canvas* c, int cx, int cy, bool fwd, uint16_t col) {
  if (fwd) {
    c->fillTriangle(cx - 17, cy - 11, cx - 17, cy + 11, cx - 2,  cy, col);
    c->fillTriangle(cx - 2,  cy - 11, cx - 2,  cy + 11, cx + 13, cy, col);
    c->fillRect(cx + 13, cy - 11, 4, 22, col);
  } else {
    c->fillTriangle(cx + 17, cy - 11, cx + 17, cy + 11, cx + 2,  cy, col);
    c->fillTriangle(cx + 2,  cy - 11, cx + 2,  cy + 11, cx - 13, cy, col);
    c->fillRect(cx - 17, cy - 11, 4, 22, col);
  }
}
// ±15 s: an open ring with an arrowhead at its start and "15" inside.
static void pmSeek15(Arduino_Canvas* c, int cx, int cy, bool fwd, uint16_t col) {
  if (fwd) {                                           // gap top-right, arrow clockwise
    c->fillArc(cx, cy, 17, 14, 300, 360, col);
    c->fillArc(cx, cy, 17, 14, 0, 250, col);
    c->fillTriangle(cx + 6, cy - 20, cx + 14, cy - 12, cx + 4, cy - 10, col);
  } else {
    c->fillArc(cx, cy, 17, 14, 290, 360, col);
    c->fillArc(cx, cy, 17, 14, 0, 240, col);
    c->fillTriangle(cx - 6, cy - 20, cx - 14, cy - 12, cx - 4, cy - 10, col);
  }
  int w = afWidth(AF_MDAT, "15");
  afPrint(c, AF_MDAT, cx - w / 2, cy + 6, "15", col);
}

static void pmFmtTime(char* out, size_t n, uint32_t sec) {
  if (sec >= 3600) snprintf(out, n, "%lu:%02lu:%02lu", (unsigned long)(sec / 3600),
                            (unsigned long)(sec / 60 % 60), (unsigned long)(sec % 60));
  else             snprintf(out, n, "%lu:%02lu", (unsigned long)(sec / 60), (unsigned long)(sec % 60));
}

// Volume toast in the bottom band (y 424..480 is empty by design): speaker,
// 236 px bar, level. Horizontal readout for an up/down gesture is the
// keyboard-volume-HUD convention (Mac, iOS landscape); the user preferred it
// here over a vertical bar crowding the transport band (2026-09-09).
static void pmVolume(Arduino_Canvas* c, uint8_t vol, float k) {
  if (k <= 0.02f) return;
  uint16_t col = vol ? AL_WHITE : AL_DIM;
  int sx = 88, cy = VOL_Y;
  c->fillRect(sx, cy - 4, 6, 8, col);
  c->fillTriangle(sx + 5, cy, sx + 14, cy - 9, sx + 14, cy + 9, col);
  if (vol) { c->fillArc(sx + 12, cy, 9, 7, 300, 360, col); c->fillArc(sx + 12, cy, 9, 7, 0, 60, col); }
  c->fillRoundRect(116, cy - 3, 236, 6, 3, PM_TRACK);
  int fill = 236 * vol / 100;
  if (fill > 0) c->fillRoundRect(116, cy - 3, fill < 6 ? 6 : fill, 6, 3, AL_WHITE);
  char num[4];
  snprintf(num, sizeof(num), "%u", vol);
  afPrint(c, AF_MDAT, 364, cy + 6, num, AL_GREY);
}

// ---- the page ----------------------------------------------------------------
void drawPlayPage(Arduino_Canvas* c, uint32_t t, uint16_t accent, uint8_t src,
                  uint8_t volume, float volK) {
  (void)t;
  c->fillScreen(BG);
  bool mac = src == PSRC_MAC;
  const PlayerInfo& pi = playerInfo();
  bool has = mac ? npHas : pi.hasItems;

  // card
  c->fillRoundRect(CARD_X, CARD_Y, CARD_W, CARD_H, CARD_R, AL_INK);
  c->drawRoundRect(CARD_X, CARD_Y, CARD_W, CARD_H, CARD_R, AL_INKBRD);

  // label
  char label[96];
  if (mac) {
    if (npHas && npApp[0]) snprintf(label, sizeof(label), tr(S_NP_LABEL_APP), npAppName());
    else                   snprintf(label, sizeof(label), "%s", tr(S_NP_LABEL));
  } else if (has) {
    snprintf(label, sizeof(label), tr(S_POD_LABEL_N), pi.index + 1, playerCount());
  } else {
    snprintf(label, sizeof(label), "%s", tr(S_POD_LABEL));
  }
  pmText(2, AF_TINY, label, CARD_W - 40, false);
  afPrint(c, AF_TINY, CARD_X + 20, LABEL_BASE, pmL1[2], AL_DIM, AL_INK);

  bool playing = false;
  if (!has) {
    // empty state: two lines in the middle of the card, glyphs dimmed
    const char* head = tr(mac ? S_NP_EMPTY : S_POD_EMPTY);
    const char* hint = tr(mac ? S_NP_HINT : S_POD_HINT);
    pmText(3, AF_TINY, hint, CARD_W - 56, true);
    int lines = pmLines[3];
    int base = lines == 2 ? 118 : 128;
    afPrint(c, AF_BODY, 240 - afWidth(AF_BODY, head) / 2, base, head, AL_GREY, AL_INK);
    afPrint(c, AF_TINY, 240 - afWidth(AF_TINY, pmL1[3]) / 2, base + 38, pmL1[3], AL_DIM, AL_INK);
    if (lines == 2)
      afPrint(c, AF_TINY, 240 - afWidth(AF_TINY, pmL2[3]) / 2, base + 62, pmL2[3], AL_DIM, AL_INK);
  } else {
    const char* title = mac ? npTitle : pi.title;
    const char* cover = mac ? npCover : pi.cover;
    playing = mac ? npIsPlaying : pi.playing;
    float pos = mac ? npPosNow() : (float)pi.posSec;
    float dur = mac ? npDur : (float)pi.durSec;

    char sub[288];
    sub[0] = 0;
    if (mac) {
      if (npArtist[0]) utf8Copy(sub, sizeof(sub), npArtist);
      if (npAlbum[0] && strcmp(npAlbum, npArtist) != 0) {
        if (sub[0]) utf8Cat(sub, sizeof(sub), " · ");
        utf8Cat(sub, sizeof(sub), npAlbum);
      }
      if (!sub[0]) utf8Copy(sub, sizeof(sub), npAppName());
    } else utf8Copy(sub, sizeof(sub), pi.show);

    // artwork (116, rounded) or the placeholder
    const uint16_t* art = cover[0] ? pmArtGet(cover) : nullptr;
    if (art) c->draw16bitRGBBitmap(ART_X, ART_Y, (uint16_t*)art, ART_S, ART_S);
    else     pmPlaceholder(c);

    // title (two lines) + subtitle, vertically centred beside the artwork
    pmText(0, AF_BODY, title, TEXT_W, true);
    pmText(1, AF_TINY, sub, TEXT_W, false);
    int lines = pmLines[0];
    int blockH = lines * 33 + (sub[0] ? 4 + 24 : 0);
    int top = ART_Y + (ART_S - blockH) / 2;
    afPrint(c, AF_BODY, TEXT_X, top + 26, pmL1[0], AL_WHITE, AL_INK);
    if (lines == 2) afPrint(c, AF_BODY, TEXT_X, top + 26 + 33, pmL2[0], AL_WHITE, AL_INK);
    if (sub[0]) afPrint(c, AF_TINY, TEXT_X, top + lines * 33 + 4 + 18, pmL1[1], AL_GREY, AL_INK);

    // slider + times
    c->fillRoundRect(SLIDER_X, SLIDER_Y, SLIDER_W, 4, 2, PM_TRACK);
    if (dur > 1.0f) {
      int fill = (int)(SLIDER_W * (pos / dur));
      if (fill > SLIDER_W) fill = SLIDER_W;
      if (fill >= 4) c->fillRoundRect(SLIDER_X, SLIDER_Y, fill, 4, 2, playing ? accent : AL_DIM);
    }
    char el[16], rm[18];
    uint32_t p = (uint32_t)(pos < 0 ? 0 : pos), d = (uint32_t)(dur < 0 ? 0 : dur);
    pmFmtTime(el, sizeof(el), p);
    rm[0] = '-';
    pmFmtTime(rm + 1, sizeof(rm) - 1, d > p ? d - p : 0);
    afPrint(c, AF_MDAT, SLIDER_X, TIME_BASE, el, AL_DIM, AL_INK);
    afPrint(c, AF_MDAT, SLIDER_X + SLIDER_W - afWidth(AF_MDAT, rm), TIME_BASE, rm, AL_DIM, AL_INK);
  }

  // transport band: three zones, glyph colour says whether they do anything
  uint16_t gcol = has ? AL_WHITE : AL_DIM;
  if (mac) { pmSkip(c, 80, GLYPH_CY, false, gcol); pmSkip(c, 400, GLYPH_CY, true, gcol); }
  else     { pmSeek15(c, 80, GLYPH_CY, false, gcol); pmSeek15(c, 400, GLYPH_CY, true, gcol); }
  c->fillCircle(240, GLYPH_CY, PLAY_R, AL_INK);
  c->drawCircle(240, GLYPH_CY, PLAY_R, AL_INKBRD);
  pmPlayPause(c, 240, GLYPH_CY, has && playing, gcol);

  pmVolume(c, volume, volK);
}

// ---- 钉住 chrome (pages.h) --------------------------------------------------
static uint16_t pgDim(uint16_t col, float f) {
  if (f >= 1.0f) return col;
  if (f <= 0.0f) return 0;
  int r = (int)(((col >> 11) & 0x1F) * f);
  int g = (int)(((col >> 5) & 0x3F) * f);
  int b = (int)((col & 0x1F) * f);
  return (uint16_t)((r << 11) | (g << 5) | b);
}

// Same geometry as face.cpp drawPin (bottom-right, ~20 px): head, collar,
// needle straight down. Grey = the AL_DIM the pages already use for
// secondary text, so it sits quietly on the clock and the almanac alike.
void drawPagePin(Arduino_Canvas* c, bool lit, uint16_t seatColor) {
  uint16_t col = lit ? seatColor : AL_DIM;
  const int cx = 442, cy = 440;
  c->fillCircle(cx, cy - 6, 7, col);
  c->fillRect(cx - 9, cy - 1, 18, 4, col);
  c->fillRoundRect(cx - 2, cy + 2, 4, 12, 2, col);
}

// Shared status on every common page; unknown battery level never becomes zero.
static const uint16_t BATT_RED = C565(0xFF, 0x5A, 0x4A);
void drawPageBatt(Arduino_Canvas* c, int pct, uint32_t t, bool charging, bool usb, int y) {
  const int x = 358;
  uint16_t color = charging ? US_PURPLE : pct >= 0 && pct < 20 ? BATT_RED : US_MUTED;
  c->fillRoundRect(350, y - 6, 116, 36, 10, BG);
  if (pct < 0 && usb) {
    c->drawLine(x + 8, y + 2, x + 8, y + 9, color);
    c->drawLine(x + 18, y + 2, x + 18, y + 9, color);
    c->drawRoundRect(x + 4, y + 8, 19, 10, 3, color);
    c->drawLine(x + 13, y + 18, x + 13, y + 24, color);
  } else {
    c->drawRoundRect(x, y + 5, 26, 13, 3, color);
    c->fillRect(x + 26, y + 9, 3, 6, color);
    if (pct > 0 && (pct >= 10 || charging || (t / 500) % 2 == 0))
      c->fillRoundRect(x + 3, y + 8, max(1, 20 * min(pct, 100) / 100), 7, 2, color);
  }
  if (charging) {
    c->drawLine(403, y + 3, 397, y + 12, color);
    c->drawLine(397, y + 12, 404, y + 12, color);
    c->drawLine(404, y + 12, 398, y + 21, color);
  }
  char text[8];
  if (pct >= 0) snprintf(text, sizeof(text), "%d%%", min(pct, 100));
  else strlcpy(text, usb ? "USB" : "--", sizeof(text));
  afPrint(c, UF_SMALL, 458 - afWidth(UF_SMALL, text), y + 17, text, color);
}

// Plain pill, centred on cy: each page hands in its own empty band (face and
// play pages 368 = above the volume bar / nameplate; clock 300 = between the
// digits and the 时辰 box; almanac 286 = between 忌 and the 信号 row), so the
// 1.5 s 名牌 never sits on a line of text.
void drawPillToast(Arduino_Canvas* c, const char* label, float k, int cy) {
  if (k <= 0.02f || !label || !*label) return;
  enum { PILL_H = 48 };
  int tw = almanacTextWidth(label);
  int w = tw + 64;
  int x = 240 - w / 2, y = cy - PILL_H / 2;
  uint16_t fill = pgDim(AL_INK, k);
  c->fillRoundRect(x, y, w, PILL_H, PILL_H / 2, fill);
  c->drawRoundRect(x, y, w, PILL_H, PILL_H / 2, pgDim(AL_INKBRD, k));
  almanacPrint(c, 240 - tw / 2, cy + 9, label, pgDim(AL_WHITE, k), fill);
}

// Which of the three zones a page-frame tap lands in: -1 left, +1 right,
// 0 middle or anywhere else on the page (the card is a toggle too).
int playZoneAt(int px, int py) {
  if (py < BAND_Y || py >= BAND_Y + BAND_H) return 0;
  if (px < 160) return -1;
  if (px >= 320) return +1;
  return 0;
}
