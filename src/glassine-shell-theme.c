/* glassine-shell-theme.c — the three themes of the "E-ink Shell" design (claude.ai
 * design project, EinkShell.dc.html themes): white, editorial, stationery.
 * One layout for all three; a theme only sets fonts, fills, edges, radii,
 * rules and stitching. Values are the design's (logical px). Hard edges only:
 * every "shadow" is an unblurred ring or offset, see Ring.
 * the design: every colour is an Interleaf token; only the E-ink scheme is
 * used here (16-grey ladder: paper #fff, sheet #eee, ink #111, ink-soft #444,
 * rule #bbb, shade #b4b4b4, highlight #ccc; stationery-eink adds leather-dark
 * #111, gilt #bbb, gold #555, accent #222, on-accent #ccc). */
#include <string.h>
#include "glassine-shell.h"

#define R4(a) { a, a, a, a }
#define OUT(x_, y_, s_, c_) { x_, y_, s_, RGB(c_), 0 }
#define IN(x_, y_, s_, c_) { x_, y_, s_, RGB(c_), 1 }

#define PLEX "IBM Plex Sans"
#define MONO "IBM Plex Mono"
#define SERIF "Source Serif 4"
#define GARA "EB Garamond"

/* e-ink friendly (design iteration 3): white grounds and ink edges in all
 * three; grey only as shading (a lip under pressable things, recesses, emboss) */
static const Theme white = {
    .name = "white",
    .screenBg = RGB(0xffffff), .ink = RGB(0x111111), .sub = RGB(0x444444),
    .lbl = { MONO, 500, 0, 10.5, 0, .08, CASE_UPPER }, .lblC = RGB(0x444444),
    .barBg = RGB(0xffffff), .barFg = RGB(0x111111),
    .barBorderW = 1.5, .barBorderC = RGB(0x111111),
    .stitchColor = RGB(0x111111),
    .plateOn = { .bg = RGB(0xcccccc), .r = R4(8) }, .plateOff = { .r = R4(8) }, .plateOnFg = RGB(0x111111),
    .wsFont = { MONO, 500, 0, 13 }, .titleFont = { PLEX, 500, 0, 14 }, .numFont = { MONO, 500, 0, 13 },
    .card = { .bg = RGB(0xffffff), .bw = 1.5, .bc = RGB(0x111111), .r = R4(16) },
    .sepW = 1, .sepC = RGB(0x111111), .sepDashed = 2, .sepSoftW = 1, .sepSoftC = RGB(0x111111),
    .headRuleW = 1.5, .headRuleC = RGB(0x111111),
    .nameFont = { PLEX, 600, 0, 17 }, .headFont = { PLEX, 600, 0, 18 }, .dateFont = { PLEX, 400, 0, 13 },
    .btn = { .bg = RGB(0xffffff), .r = R4(12), .ring = { IN(0, 0, 1.5, 0x111111), OUT(0, 3, 0, 0xb4b4b4) }, .nring = 2 },
    .sel = { .bg = RGB(0xcccccc), .r = R4(12) }, .selFg = RGB(0x111111),
    .tog = { .bg = RGB(0xffffff), .r = R4(18), .ring = { IN(0, 0, 1.5, 0x111111), OUT(0, 3, 0, 0xb4b4b4) }, .nring = 2 },
    .togOn = { .bg = RGB(0x111111), .r = R4(18), .ring = { OUT(0, 3, 0, 0xb4b4b4) }, .nring = 1 }, .togOnFg = RGB(0xffffff),
    .grooveTop = 20, .grooveH = 6, .grooveR = 3, .grooveBg = RGB(0xffffff),
    .grooveRing = { IN(0, 0, 1.5, 0x111111) }, .ngrooveRing = 1, .fillTop = 20, .fillH = 6,
    .knobTop = 10, .knobML = -13, .knobW = 26, .knobH = 26, .knobR = HALF, .knobBg = RGB(0xffffff),
    .knobBw = 2, .knobBc = RGB(0x111111),
    .tray = { .bg = RGB(0xffffff), .r = R4(12), .ring = { IN(0, 0, 1.5, 0x111111), IN(0, 3, 0, 0xbbbbbb) }, .nring = 2 },
    .segR = 9, .segFont = { PLEX, 400, 0, 14 },
    .segOn = { .bg = RGB(0x111111), .r = R4(9), .ring = { OUT(0, 3, 0, 0xb4b4b4) }, .nring = 1 }, .segOnFg = RGB(0xffffff),
    .pwr = { .bg = RGB(0x111111), .r = R4(21), .ring = { OUT(0, 3, 0, 0xb4b4b4) }, .nring = 1 }, .pwrFg = RGB(0xffffff),
    .pad = { .bg = RGB(0xffffff), .bw = 1.5, .bc = RGB(0x111111), .r = R4(18) }, .padFg = RGB(0x111111),
    .folder = { .bg = RGB(0xffffff), .bw = 1.5, .bc = RGB(0x111111), .r = R4(10), .ring = { OUT(0, 4, 0, 0xb4b4b4) }, .nring = 1 },
    .folderTab = { .bg = RGB(0xffffff), .r = { 6, 6, 0, 0 }, .ring = { IN(0, 0, 1.5, 0x111111) }, .nring = 1 },
    .folderFont = { PLEX, 400, 0, 13 },
    .app = { .bg = RGB(0xffffff), .r = R4(12), .ring = { IN(0, 0, 1.5, 0x111111) }, .nring = 1 },
    .appNameFont = { PLEX, 500, 0, 14 }, .appCatFont = { PLEX, 400, 0, 11.5 },
    .nTitleFont = { PLEX, 600, 0, 15 }, .nBodyFont = { PLEX, 400, 0, 13.5 }, .nBodyLh = 1.45,
    .searchFont = { PLEX, 400, 0, 14 }, .pageFont = { MONO, 500, 0, 12 },
    .tabOn = { .bg = RGB(0xcccccc), .r = { 0, 10, 10, 0 },
               .ring = { IN(-1.5, 0, 0, 0x111111), IN(0, 1.5, 0, 0x111111), IN(0, -1.5, 0, 0x111111) }, .nring = 3 },
    .tabOff = { .bg = RGB(0xffffff), .r = { 0, 10, 10, 0 }, .ring = { IN(0, 0, 1.5, 0x111111), IN(-6, 0, 0, 0xbbbbbb) }, .nring = 2 },
    .tabFont = { PLEX, 500, 0, 13 },
    .ratioFont = { MONO, 600, 0, 15 }, .snapFont = { MONO, 500, 0, 12 },
    .qsTitle = "Quick settings", .lTitle = "Apps", .nTitle = "Notifications", .oTitle = "Overview", .curTag = "Showing",
};

#define SERIF_LBL(f) { f, 400, 0, 14, 1, .1, CASE_LOWER }

static const Theme editorial = {
    .name = "editorial",
    .screenBg = RGB(0xffffff), .ink = RGB(0x111111), .sub = RGB(0x444444),
    .lbl = SERIF_LBL(SERIF), .lblC = RGB(0x111111),
    .barBg = RGB(0xffffff), .barFg = RGB(0x111111),
    .barBorderW = 3, .barBorderC = RGB(0x111111), .barDouble = 1,
    .stitchColor = RGB(0x111111),
    .plateOn = { .ring = { IN(0, -2, 0, 0x111111) }, .nring = 1 }, .plateOff = { 0 }, .plateOnFg = RGB(0x111111),
    .wsFont = { SERIF, 400, 1, 16 }, .titleFont = { SERIF, 400, 1, 16 }, .numFont = { SERIF, 400, 0, 15 },
    .card = { .bg = RGB(0xffffff), .bw = 1, .bc = RGB(0x111111),
              .ring = { OUT(0, 0, 5, 0xffffff), OUT(0, 0, 6, 0x111111) }, .nring = 2 },
    .sepW = 1, .sepC = RGB(0x111111), .sepSoftW = 1, .sepSoftC = RGB(0x111111),
    .headRuleW = 1, .headRuleC = RGB(0x111111),
    .nameFont = { SERIF, 600, 0, 20 }, .headFont = { SERIF, 600, 0, 22 }, .dateFont = { SERIF, 400, 1, 14 },
    /* square buttons with a hard offset (the "offset" token) */
    .btn = { .bg = RGB(0xffffff), .ring = { IN(0, 0, 1, 0x111111), OUT(3, 3, 0, 0x111111) }, .nring = 2 },    /* filled: some sit on the apps (dividers, the move) */
    .sel = { .bg = RGB(0x111111) }, .selFg = RGB(0xffffff),
    .tog = { .r = R4(HALF), .ring = { IN(0, 0, 1, 0x111111), OUT(3, 3, 0, 0x111111) }, .nring = 2 },
    .togOn = { .bg = RGB(0x111111), .r = R4(HALF), .ring = { IN(0, 0, 4, 0x111111), IN(0, 0, 5, 0xffffff) }, .nring = 2 },
    .togOnFg = RGB(0xffffff),
    .grooveTop = 22, .grooveH = 1, .grooveR = 0, .grooveBg = RGB(0x111111), .fillTop = 21, .fillH = 3,
    .knobTop = 13, .knobML = -10, .knobW = 20, .knobH = 20, .knobR = HALF, .knobBg = RGB(0xffffff),
    .knobBw = 1.5, .knobBc = RGB(0x111111),
    .tray = { .ring = { IN(0, -1, 0, 0x111111) }, .nring = 1 }, .segR = 0, .segFont = { SERIF, 400, 0, 15 },
    .segOn = { .ring = { IN(0, -2, 0, 0x111111) }, .nring = 1 }, .segOnFg = RGB(0x111111),
    .pwr = { .ring = { IN(0, 0, 1, 0x111111), OUT(3, 3, 0, 0x111111) }, .nring = 2 }, .pwrFg = RGB(0x111111),
    .pad = { .bg = RGB(0xffffff), .bw = 1, .bc = RGB(0x111111),
             .ring = { OUT(0, 0, 5, 0xffffff), OUT(0, 0, 6, 0x111111) }, .nring = 2 }, .padFg = RGB(0x111111),
    .folder = { .bg = RGB(0xffffff), .bw = 1, .bc = RGB(0x111111) },
    .folderTab = { .bg = RGB(0xffffff), .ring = { IN(0, 0, 1, 0x111111) }, .nring = 1 },
    .folderFont = { SERIF, 400, 0, 14 },
    .app = { .ring = { IN(0, -1, 0, 0x111111) }, .nring = 1 },
    .appNameFont = { SERIF, 400, 0, 16 }, .appCatFont = { SERIF, 400, 1, 13 },
    .nTitleFont = { SERIF, 600, 0, 17 }, .nBodyFont = { SERIF, 400, 0, 15 }, .nBodyLh = 1.45,
    .searchFont = { SERIF, 400, 1, 15 }, .pageFont = { SERIF, 400, 1, 15 },
    .tabOn = { .bg = RGB(0xffffff),
               .ring = { IN(-1, 0, 0, 0x111111), IN(0, 1, 0, 0x111111), IN(0, -1, 0, 0x111111), OUT(3, 3, 0, 0x111111) }, .nring = 4 },
    .tabOff = { .bg = RGB(0xffffff), .ring = { IN(0, 0, 1, 0x111111), OUT(3, 3, 0, 0xbbbbbb) }, .nring = 2 },
    .tabFont = { SERIF, 400, 0, 15, 1, .06 },
    .ratioFont = { SERIF, 400, 1, 22 }, .snapFont = { SERIF, 400, 1, 16 },
    .frac = 1, .roman = 1,
    .qsTitle = "Controls", .lTitle = "Applications", .nTitle = "Notices", .oTitle = "Windows", .curTag = "on screen",
};

static const Theme stationery = {
    .name = "stationery",
    .screenBg = RGB(0xffffff), .ink = RGB(0x111111), .sub = RGB(0x444444),
    .lbl = SERIF_LBL(GARA), .lblC = RGB(0x555555),         /* gold */
    .barBg = RGB(0x111111), .barFg = RGB(0xcccccc),     /* leather-dark, on-accent */
    .stitch = 1, .stitchColor = RGB(0xbbbbbb),         /* gilt */
    .plateOn = { .bg = RGB(0xffffff), .r = R4(3),
                 .ring = { IN(0, 0, 1, 0x111111), IN(0, 0, 3, 0xffffff), IN(0, 0, 4, 0x111111), IN(0, -3, 4, 0xbbbbbb) }, .nring = 4 },
    .plateOff = { .r = R4(3) },
    .plateOnFg = RGB(0x111111),
    .wsFont = { GARA, 400, 1, 16 }, .titleFont = { GARA, 400, 1, 17 }, .numFont = { GARA, 400, 0, 16 },
    /* stacked cardstock (card-stack): two ink-edged sheets, no shadow */
    .card = { .bg = RGB(0xffffff), .r = R4(3),
              .ring = { OUT(0, 0, 1.5, 0x111111), OUT(0, 3, 0, 0xffffff), OUT(0, 3, 1.5, 0x111111) }, .nring = 3 },
    .emboss = 1,
    .sepW = 1.5, .sepC = RGB(0x111111), .sepDashed = 1, .sepSoftW = 1, .sepSoftC = RGB(0x111111),
    .headRuleW = 1, .headRuleC = RGB(0x111111),
    .nameFont = { GARA, 500, 1, 22 }, .headFont = { GARA, 500, 0, 22, 1, .1 }, .dateFont = { GARA, 400, 1, 16 },
    .btn = { .bg = RGB(0xffffff), .r = R4(6), .ring = { IN(0, 0, 1.5, 0x111111), IN(0, 4, 0, 0xbbbbbb) }, .nring = 2 },
    .sel = { .bg = RGB(0x222222), .r = R4(6) }, .selFg = RGB(0xcccccc),      /* accent, on-accent */
    .tog = { .bg = RGB(0xffffff), .r = R4(HALF), .ring = { IN(0, 0, 1.5, 0x111111), IN(0, 4, 0, 0xbbbbbb) }, .nring = 2 },
    .togOn = { .bg = RGB(0x222222), .r = R4(HALF),
               .ring = { IN(0, 0, 5, 0x222222), IN(0, 0, 6.5, 0xbbbbbb) }, .nring = 2 },      /* a gilt ring */
    .togOnFg = RGB(0xcccccc),
    .grooveTop = 19, .grooveH = 8, .grooveR = 4, .grooveBg = RGB(0xffffff),
    .grooveRing = { IN(0, 0, 1.5, 0x111111), IN(0, 3, 0, 0xbbbbbb) }, .ngrooveRing = 2,
    .fillTop = 21.5, .fillH = 3,
    .knobTop = 9, .knobML = -3, .knobW = 6, .knobH = 28, .knobR = 3, .knobBg = RGB(0x111111),
    .tray = { .bg = RGB(0xffffff), .r = R4(9), .ring = { IN(0, 0, 1.5, 0x111111), IN(0, 4, 0, 0xbbbbbb) }, .nring = 2 },
    .segR = 6, .segFont = { GARA, 400, 0, 17 },
    .segOn = { .bg = RGB(0xffffff), .r = R4(6), .ring = { OUT(0, 0, 1.5, 0x111111), OUT(0, 3, 0, 0xb4b4b4) }, .nring = 2 },
    .segOnFg = RGB(0x111111),
    .pwr = { .bg = RGB(0x222222), .r = R4(21), .ring = { IN(0, 0, 1.5, 0xbbbbbb) }, .nring = 1 },
    .pwrFg = RGB(0xcccccc),
    .pad = { .bg = RGB(0x222222), .r = R4(8) }, .padFg = RGB(0xcccccc),
    .padStitchInset = 7,
    .folder = { .bg = RGB(0xffffff), .r = { 2, 4, 4, 4 },
                .ring = { OUT(0, 0, 1, 0x111111), OUT(0, 3, 0, 0xffffff), OUT(0, 3, 1, 0x111111) }, .nring = 3 },
    .folderTab = { .r = { 6, 6, 0, 0 } },
    .folderFont = { GARA, 400, 0, 14 },
    .app = { .bg = RGB(0xffffff), .r = R4(3), .ring = { OUT(0, 0, 1, 0x111111), OUT(0, 3, 0, 0xbbbbbb) }, .nring = 2 },
    .appNameFont = { GARA, 500, 0, 15, 1, .1 }, .appCatFont = { GARA, 400, 1, 13 },
    .nTitleFont = { GARA, 500, 0, 19 }, .nBodyFont = { GARA, 400, 0, 16 }, .nBodyLh = 1.4,
    .searchFont = { GARA, 400, 1, 17 }, .pageFont = { GARA, 400, 1, 17 },
    .tabOn = { .bg = RGB(0xffffff), .r = { 0, 5, 5, 0 },
               .ring = { IN(-1.5, 0, 0, 0x111111), IN(0, 1.5, 0, 0x111111), IN(0, -1.5, 0, 0x111111), OUT(0, 2, 0, 0x111111) }, .nring = 4 },
    .tabOff = { .bg = RGB(0xffffff), .r = { 0, 5, 5, 0 }, .ring = { IN(0, 0, 1.5, 0x111111), OUT(0, 2, 0, 0xb4b4b4) }, .nring = 2 },
    .tabFont = { GARA, 400, 0, 15, 1, .1 },
    .ratioFont = { GARA, 400, 1, 22 }, .snapFont = { GARA, 400, 1, 16 },
    .frac = 1, .roman = 1,
    .qsTitle = "The desk", .lTitle = "Index", .nTitle = "The post", .oTitle = "The spread", .curTag = "on top",
};

const Theme *themes[] = { &white, &editorial, &stationery };
const int nthemes = 3;

const Theme *theme_find(const char *name)
{
    for (int i = 0; i < nthemes; i++)
        if (name && !strcmp(name, themes[i]->name)) return themes[i];
    return themes[0];
}
