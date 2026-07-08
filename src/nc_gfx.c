/* nc_gfx.c  --  pluggable tile-graphics modes for the editor viewport.
 *
 * A mode = one GfxOps: how many screen columns a map tile takes (tilew) and
 * how to draw a tile / sprite / cursor there.  nc_render.c owns layout and
 * panning and dispatches through Gfx, so modes never deal with scrolling.
 *
 *   color    1 col/tile, plain ASCII + ACS + 8 colors (nc_cell, nc_render.c).
 *            Works on any curses ever made -- this is the default mode.
 *   unicode  2 cols/tile, UTF-8: emoji buildings/vehicles, box-drawing roads
 *            and rails, quadrant-block density fills, braille terrain
 *            texture, legacy-computing checker.  Needs a UTF-8 locale and a
 *            wide-capable curses (macOS -lcurses is; Linux: -lncursesw).
 *   ascii    1 col/tile, strict 7-bit ASCII and no color anywhere -- only
 *            bold and reverse video, the vt100-era look.  Poor man's line
 *            drawing (| - + =), buildings as reverse-video blocks.  The
 *            mono flag makes NC_CP (nc.h) yield pair 0 everywhere, so the
 *            whole program draws in the terminal's own default colors;
 *            color pairs are never touched or remapped.
 *   aa       2 cols/tile, rendered through the real aalib, in color.  Lives
 *            in nc_aa.c and is compiled in only by `make aalib` (libaa is
 *            rare); otherwise its avail() fails and the mode is skipped.
 *   acs      1 col/tile, ascii's mono rules (bold/reverse only, no color)
 *            but real ACS box-drawing for roads/rails/wires instead of the
 *            +/-/| approximation.
 *
 * Future slots: braille 2x4-dot microtiles.  Add a GfxOps and list it in
 * modes[].
 *
 * Portability: this file stays C89 and uses no wide-char APIs -- the UTF-8
 * glyphs are plain byte strings pushed through addstr(), so it still COMPILES
 * on old curses; the mode is refused at runtime (avail()) when the locale is
 * not multibyte.  Every emoji used has Emoji_Presentation=Yes and East-Asian
 * width W (2 columns) so cells stay aligned; the one narrow pictograph we
 * could not resist (radiation, U+2622) is padded to "N " by hand.
 */

#include "sim.h"
#include <curses.h>
#include <stdlib.h>			/* MB_CUR_MAX */
#include "nc.h"

/* ========================= color mode (1 col/tile) ======================== */

static void
df_tile(int sy, int sx, int mx, int my)
{
  mvaddch(sy, sx, nc_cell(mx, my));
}

static void
df_sprite(int sy, int sx, int type)
{
  mvaddch(sy, sx, nc_sprite_glyph(type));
}

static void
df_cursor(int sy, int sx, int mx, int my)
{
  mvaddch(sy, sx, nc_cell(mx, my) | A_REVERSE);
}

/* ======================== unicode mode (2 cols/tile) ====================== */

/* Needs a UTF-8 locale; setlocale(LC_ALL,"") ran at startup.  MB_CUR_MAX > 1
 * alone would also pass EUC/Big5 locales, where our UTF-8 byte strings are
 * mojibake, so additionally ask for "utf" in the locale name, taken from the
 * environment in POSIX precedence order. */
static int
uni_avail(void)
{
  char *s;

  if (MB_CUR_MAX <= 1) return 0;
  s = getenv("LC_ALL");
  if (!s || !*s) s = getenv("LC_CTYPE");
  if (!s || !*s) s = getenv("LANG");
  for (; s && *s; s++) {
    if ((s[0] == 'u' || s[0] == 'U') &&
	(s[1] == 't' || s[1] == 'T') &&
	(s[2] == 'f' || s[2] == 'F')) return 1;
  }
  return 0;
}

/* put one 2-column tile: colors around/behind the glyph, then the glyph */
static void
uput(int sy, int sx, char *s, int fg, int bg, int attr)
{
  attrset(NC_CP(fg, bg) | attr);
  mvaddstr(sy, sx, s);
  attrset(A_NORMAL);
}

/* Box-drawing auto-tiling, indexed by nc_transit_mask (up<<3|dn<<2|lf<<1|rt).
 * Column 0 carries the connector; column 1 continues east when the neighbor
 * does, so adjacent tiles join seamlessly.  Roads are heavy, rails double,
 * power lines light. */
static char *road_gl[16] = {
  "╋ ", "━━", "━ ", "━━", "┃ ", "┏━", "┓ ", "┳━",
  "┃ ", "┗━", "┛ ", "┻━", "┃ ", "┣━", "┫ ", "╋━"
};
static char *rail_gl[16] = {
  "╬ ", "══", "═ ", "══", "║ ", "╔═", "╗ ", "╦═",
  "║ ", "╚═", "╝ ", "╩═", "║ ", "╠═", "╣ ", "╬═"
};
static char *wire_gl[16] = {
  "┼ ", "──", "─ ", "──", "│ ", "┌─", "┐ ", "┬─",
  "│ ", "└─", "┘ ", "┴─", "│ ", "├─", "┤ ", "┼─"
};

/* Zone lot decode (see zone_glyph, nc_render.c): a built zone is a 3x3 image
 * of consecutive tiles with the density baked into the index.  Returns the
 * density 0..dmod-1, or -1 if t is outside the built range; *vac = 1 when
 * this lot cell is still undeveloped at that density.  Shared with the
 * minimap's unicode renderer (nc_minimap.c). */
int
nc_zone_den(int t, int builtBase, int builtHi, int dmod, int *vac)
{
  static int rank[9] = { 5, 1, 6,  3, 0, 4,  7, 2, 8 };
  int off, den, thr;

  if (t < builtBase || t > builtHi) return -1;
  off = (t - builtBase) % 9;
  den = ((t - builtBase) / 9) % dmod;
  thr = ((den + 1) * 8) / dmod;
  *vac = (off != 4 && rank[off] > thr);
  return den;
}

/* companion decode: land-value class 0..3 (industrial 0..1) baked into the
 * same tile index as ZonePlop's base = ((val * dmod) + den) * 9 + ZB - 4 */
static int
nc_zone_val(int t, int builtBase, int dmod)
{
  return ((t - builtBase) / 9) / dmod;
}

/* zone-center icon by land value (GetCRVal 0..3); the top glyph (🌇/🏦) is
 * reserved for zones at max value AND max density; industrial is always 🏭 */
static char *res_val[4] = { "🛖", "🏠", "🏡", "🏡" };
static char *com_val[4] = { "🏪", "🏬", "🏢", "🏢" };

/* braille terrain textures */
static char *dirt_tex[4]  = { "⠐⠄", "⠠⠂", "⠄⠐", "⠂⠠" };
static char *water_tex[4] = { "⣀⣀", "⠤⠤", "⣀⠤", "⠤⣀" };

/* quadrant-block debris */
static char *rubble_tex[4] = { "▚▞", "▞▖", "▙▘", "▗▚" };

/* stadium (4x4 lot): chalk-line oval on the green pitch, ball/game at center.
 * Indexed by tile offset row*4+col; the same layout serves the empty (779+)
 * and full (795+) plots since they are 16 tiles apart. */
static char *stad_gl[16] = {
  "◜◠", "◠◠", "◠◠", "◠◝",
  "( ", "  ", "  ", " )",
  "( ", "  ", "  ", " )",
  "◟◡", "◡◡", "◡◡", "◡◞"
};

/* police station (3x3 lot): dashed fence around the yard, officer at center */
static char *fence_gl[9] = {
  "┏┅", "┅┅", "┅┓",
  "┇ ", "  ", " ┇",
  "┗┅", "┅┅", "┅┛"
};

static void
uni_tile(int sy, int sx, int mx, int my)
{
  int raw = Map[mx][my];
  int blink = (flagBlink <= 0);
  int land = ThemeLand;
  int t, cls, center, h, den, vac, val;
  char *ico;

  /* same decode as nc_cell / MemDrawBeegMapRect */
  t = raw;
  if ((t & LOMASK) >= TILE_COUNT) t -= TILE_COUNT;
  if (blink && (t & ZONEBIT) && !(t & PWRBIT)) {
    t = LIGHTNINGBOLT;
  } else {
    t &= LOMASK;
  }

  center = (raw & ZONEBIT) != 0;	/* ZonePlop marks each lot's center tile */
  h = mx * 73 + my * 179;		/* stable per-cell hash for variety */

  if (t == LIGHTNINGBOLT) {		/* unpowered zone blink */
    uput(sy, sx, "⚡", COLOR_YELLOW, land, A_BOLD);
    return;
  }

  /* ---- transit: box-drawing auto-tiling + emoji traffic ---- */
  cls = nc_transit_class(t);
  if (cls == 1) {
    if (t >= HTRFBASE) {				/* heavy traffic jam */
      uput(sy, sx, ((mx + my) & 1) ? "🚗" : "🚕", COLOR_WHITE, COLOR_BLACK, 0);
      return;
    }
    uput(sy, sx, road_gl[nc_transit_mask(mx, my, 1)],
	 (t >= LTRFBASE) ? COLOR_YELLOW : COLOR_WHITE, COLOR_BLACK,
	 (t >= LTRFBASE) ? A_BOLD : 0);			/* light traffic glows */
    return;
  }
  if (cls == 2) {
    uput(sy, sx, rail_gl[nc_transit_mask(mx, my, 2)],
	 COLOR_BLACK, COLOR_WHITE, 0);
    return;
  }
  if (cls == 3) {
    uput(sy, sx, wire_gl[nc_transit_mask(mx, my, 3)],
	 COLOR_RED, land, A_BOLD);
    return;
  }

  /* ---- terrain ---- */
  if (t == DIRT) {
    uput(sy, sx, dirt_tex[h & 3], COLOR_BLACK, land, 0);
    return;
  }
  if (t >= RIVER && t <= LASTRIVEDGE) {
    if (t >= FIRSTRIVEDGE && (h % 13) == 0) {		/* a crab on the shore */
      uput(sy, sx, "🦀", COLOR_RED, COLOR_BLUE, A_BOLD);
      return;
    }
    if ((h % 37) == 0) { uput(sy, sx, "⛵", COLOR_WHITE, COLOR_BLUE, 0); return; }
    if ((h % 41) == 0) { uput(sy, sx, "🐟", COLOR_CYAN,  COLOR_BLUE, 0); return; }
    uput(sy, sx, water_tex[h & 3], COLOR_CYAN, COLOR_BLUE, A_BOLD);
    return;
  }
  if (t >= TREEBASE && t < WOODS2) {
    uput(sy, sx, ((h % 7) == 0) ? "🌳" : "🌲", COLOR_BLACK, COLOR_GREEN, 0);
    return;
  }
  if (t >= WOODS2 && t <= WOODS5) {			/* user-placed park */
    uput(sy, sx, "🌳", COLOR_BLACK, COLOR_GREEN, 0);
    return;
  }
  if (t >= RUBBLE && t <= LASTRUBBLE) {
    if ((h % 5) == 0) uput(sy, sx, "🧱", COLOR_BLACK, land, 0);
    else              uput(sy, sx, rubble_tex[h & 3], COLOR_BLACK, land, 0);
    return;
  }
  if (t >= FLOOD && t <= LASTFLOOD) {
    uput(sy, sx, "🌊", COLOR_WHITE, COLOR_CYAN, A_BOLD);
    return;
  }
  if (t == RADTILE) {
    uput(sy, sx, "☢ ", COLOR_YELLOW, COLOR_MAGENTA, A_BOLD);
    return;
  }
  if (t >= FIREBASE && t <= LASTFIRE) {
    uput(sy, sx, "🔥", COLOR_YELLOW, COLOR_RED, A_BOLD);
    return;
  }

  /* ---- city services (3x3 lots; the exact center tile is the landmark) ---- */
  if (t >= HOSPITAL - 4 && t <= HOSPITAL + 4) {
    if (t == HOSPITAL) uput(sy, sx, "🏥", COLOR_WHITE, COLOR_RED, A_BOLD);
    else               uput(sy, sx, "▒▒", COLOR_WHITE, COLOR_RED, 0);
    return;
  }
  if (t >= CHURCH - 4 && t <= CHURCH + 4) {
    if (t == CHURCH) uput(sy, sx, "⛪", COLOR_BLACK, COLOR_CYAN, 0);
    else             uput(sy, sx, "▒▒", COLOR_BLACK, COLOR_CYAN, 0);
    return;
  }

  /* ---- R/C/I zones: lot fills in with copies of the center icon as the
   * zone develops (same rank/threshold progression as text-mode zone_glyph);
   * still-vacant lot cells stay ░░ ---- */
  if (t >= RESBASE && t < COMBASE) {
    if (t >= LHTHR && t <= HHTHR) {			/* single family house */
      uput(sy, sx, res_val[(t - LHTHR) / 3], COLOR_BLACK, COLOR_CYAN, 0);
      return;
    }
    den = nc_zone_den(t, RZB - 4, HOSPITAL - 5, 4, &vac);
    if (den >= 0) {
      val = nc_zone_val(t, RZB - 4, 4);
      ico = (den == 3 && val == 3) ? "🌇" : res_val[val];
      if (vac) uput(sy, sx, "░░", COLOR_BLACK, COLOR_CYAN, 0);
      else     uput(sy, sx, ico, COLOR_BLACK, COLOR_CYAN, 0);
      return;
    }
    if (t <= RESBASE + 8) {				/* empty designated lot */
      if (center) uput(sy, sx, "🏗", COLOR_YELLOW, COLOR_CYAN, A_BOLD);
      else        uput(sy, sx, "░░", COLOR_BLACK, COLOR_CYAN, 0);
      return;
    }
    uput(sy, sx, "▚ ", COLOR_BLACK, COLOR_CYAN, 0);	/* stray tile */
    return;
  }
  if (t >= COMBASE && t < INDBASE) {
    den = nc_zone_den(t, CZB - 4, INDBASE - 1, 5, &vac);
    if (den >= 0) {
      val = nc_zone_val(t, CZB - 4, 5);
      ico = (den == 4 && val == 3) ? "🏦" : com_val[val];
      if (vac) uput(sy, sx, "░░", COLOR_WHITE, COLOR_BLUE, 0);
      else     uput(sy, sx, ico, COLOR_WHITE, COLOR_BLUE, A_BOLD);
      return;
    }
    if (t <= COMBASE + 8) {
      if (center) uput(sy, sx, "🏗", COLOR_YELLOW, COLOR_BLUE, A_BOLD);
      else        uput(sy, sx, "░░", COLOR_WHITE, COLOR_BLUE, 0);
      return;
    }
    uput(sy, sx, "▚ ", COLOR_WHITE, COLOR_BLUE, A_BOLD);
    return;
  }
  if (t >= INDBASE && t < PORTBASE) {
    den = nc_zone_den(t, IZB - 4, PORTBASE - 1, 4, &vac);
    if (den >= 0) {
      if (vac) uput(sy, sx, "░░", COLOR_YELLOW, COLOR_MAGENTA, 0);
      else     uput(sy, sx, "🏭", COLOR_YELLOW, COLOR_MAGENTA, A_BOLD);
      return;
    }
    if (t <= INDBASE + 8) {
      if (center) uput(sy, sx, "🏗", COLOR_YELLOW, COLOR_MAGENTA, A_BOLD);
      else        uput(sy, sx, "░░", COLOR_YELLOW, COLOR_MAGENTA, 0);
      return;
    }
    uput(sy, sx, "▚ ", COLOR_YELLOW, COLOR_MAGENTA, A_BOLD);
    return;
  }

  /* ---- big buildings (center tile carries ZONEBIT) ---- */
  if (t >= PORTBASE && t <= LASTPORT) {			/* seaport */
    if (center) uput(sy, sx, "⚓", COLOR_WHITE, COLOR_BLUE, A_BOLD);
    else        uput(sy, sx, "🮕🮕", COLOR_WHITE, COLOR_BLUE, 0);	/* containers */
    return;
  }
  if (t >= RADAR0 && t <= RADAR7) {			/* airport tower (anim) */
    uput(sy, sx, "📡", COLOR_BLACK, COLOR_WHITE, 0);
    return;
  }
  if (t >= AIRPORTBASE && t < COALBASE) {		/* airport (6x6) */
    if (center) uput(sy, sx, "🛫", COLOR_BLACK, COLOR_WHITE, 0);
    else        uput(sy, sx, "░░", COLOR_BLACK, COLOR_WHITE, 0);   /* tarmac */
    return;
  }
  if (t >= COALBASE && t <= LASTPOWERPLANT) {		/* coal power */
    if (center) uput(sy, sx, "🔌", COLOR_WHITE, COLOR_RED, A_BOLD);
    else        uput(sy, sx, "▒▒", COLOR_WHITE, COLOR_RED, 0);
    return;
  }
  if (t >= FIRESTBASE && t < POLICESTBASE) {		/* fire station */
    if (center) uput(sy, sx, "🚒", COLOR_WHITE, COLOR_RED, A_BOLD);
    else        uput(sy, sx, "▒▒", COLOR_WHITE, COLOR_RED, 0);
    return;
  }
  if (t >= POLICESTBASE && t < STADIUMBASE) {		/* police station */
    if (center) uput(sy, sx, "👮", COLOR_WHITE, COLOR_BLUE, A_BOLD);
    else        uput(sy, sx, fence_gl[(t - POLICESTBASE) % 9],
		     COLOR_WHITE, COLOR_BLUE, A_BOLD);
    return;
  }
  if (t >= FOOTBALLGAME1 && t <= FOOTBALLGAME2 + 7) {	/* game day (anim) */
    uput(sy, sx, "🏈", COLOR_WHITE, COLOR_GREEN, 0);
    return;
  }
  if (t >= STADIUMBASE && t < NUCLEARBASE) {		/* stadium */
    if (center) uput(sy, sx, (t >= FULLSTADIUM) ? "🏈" : "⚽",
		     COLOR_WHITE, COLOR_GREEN, A_BOLD);
    else        uput(sy, sx, stad_gl[(t - STADIUMBASE) % 16],
		     COLOR_WHITE, COLOR_GREEN, A_BOLD);
    return;
  }
  if (t >= NUCLEARBASE && t <= LASTZONE) {		/* nuclear power */
    if (center) uput(sy, sx, "☢ ", COLOR_WHITE, COLOR_GREEN, A_BOLD);
    else        uput(sy, sx, "▒▒", COLOR_BLACK, COLOR_GREEN, 0);
    return;
  }

  /* ---- animation / oddball tiles ---- */
  if ((t >= HBRDG0 && t <= HBRDG3) || (t >= VBRDG0 && t <= VBRDG3)) {
    uput(sy, sx, "🌉", COLOR_WHITE, COLOR_BLUE, 0);	/* open drawbridge */
    return;
  }
  if (t >= FOUNTAIN && t <= INDBASE2) {
    uput(sy, sx, "⛲", COLOR_BLACK, COLOR_GREEN, 0);
    return;
  }
  if (t >= TINYEXP && t <= LASTTINYEXP) {
    uput(sy, sx, "💥", COLOR_YELLOW, COLOR_RED, A_BOLD);
    return;
  }
  if (t >= SMOKEBASE && t < TINYEXP) {
    uput(sy, sx, "💨", COLOR_WHITE, land, 0);
    return;
  }
  if (t >= COALSMOKE1 && t <= COALSMOKE4 + 3) {
    uput(sy, sx, "💨", COLOR_WHITE, COLOR_RED, 0);
    return;
  }

  uput(sy, sx, "▒▒", COLOR_WHITE, land, 0);		/* unknown tile */
}

/* moving objects: THE toys */
static void
uni_sprite(int sy, int sx, int type)
{
  char *s;

  switch (type) {
  case TRA: s = "🚂"; break;		/* train      */
  case COP: s = "🚁"; break;		/* helicopter */
  case AIR: s = "🛫"; break;		/* airplane   */
  case SHI: s = "🚢"; break;		/* ship       */
  case GOD: s = "🦖"; break;		/* monster    */
  case TOR: s = "🌀"; break;		/* tornado    */
  case EXP: s = "💥"; break;		/* explosion  */
  case BUS: s = "🚌"; break;		/* bus        */
  default:  s = "❓"; break;
  }
  uput(sy, sx, s, COLOR_WHITE, COLOR_BLACK, 0);
}

/* emoji ignore A_REVERSE in most terminals, so bracket the tile instead.
 * Each bracket overwrites BOTH columns of its neighbor tile: a lone write
 * onto half of a 2-col emoji mangles the row on old ncursesw. */
static void
uni_cursor(int sy, int sx, int mx, int my)
{
  attrset(NC_CP(COLOR_YELLOW, COLOR_BLACK) | A_BOLD);
  if (sx - 2 >= EdLeft)       { mvaddch(sy, sx - 2, ' '); addch('['); }
  if (sx + 3 < EdLeft + EdW)  { mvaddch(sy, sx + 2, ']'); addch(' '); }
  attrset(A_NORMAL);
}

/* ==================== ascii mode (1 col/tile, monochrome) ================= */

/* Poor man's line drawing indexed by nc_transit_mask (up<<3|dn<<2|lf<<1|rt).
 * Roads and wires share the | - + set (wires drawn bold); roads stay plain
 * (no fill, like the color mode's white-on-black).  Rails collapse to a
 * single reverse-video '#' -- a solid track bed instead of line art. */
static char as_road[17] = "+---|+++|+++|+++";

/* One zone lot cell (mirrors zone_glyph, nc_render.c, minus the colors):
 * reverse-video bold letter = built center, '.' = still-vacant lot, bare
 * letter = empty designated zone.  A built cell's density (0..dmod-1) is
 * split into quarters and shown along two independent axes -- UPPER/lower
 * case for the top/bottom half, bold for the upper half of *that* half --
 * so ".rRrR" bottom-to-top reads as: vacant, sparse, sparse+bold, dense,
 * dense+bold. */
static chtype
as_zone(int t, int raw, int letter, int emptyBase, int builtBase,
	int builtHi, int dmod)
{
  int den, vac, q;

  den = nc_zone_den(t, builtBase, builtHi, dmod, &vac);
  if (den >= 0) {					/* built zone */
    if (raw & ZONEBIT) return ((chtype)letter) | A_BOLD | A_REVERSE;
    if (vac) return (chtype)'.';
    q = ((den + 1) * 4 - 1) / dmod;			/* 0..3: density quarter */
    if (q >= 2) return ((chtype)letter) | (q & 1 ? A_BOLD : 0);
    return ((chtype)(letter + 32)) | (q & 1 ? A_BOLD : 0);
  }
  if (t >= emptyBase && t <= emptyBase + 8)		/* empty designated lot */
    return (t - emptyBase == 4) ? (chtype)letter : (chtype)'.';
  return (chtype)(letter + 32);				/* stray tile */
}

/* Big civic buildings: a reverse-video block with a bold letter at the
 * center tile, so a 3x3 station reads as one solid building.  fill frames
 * the letter (' ' for a plain block; police uses '=', fire uses '*', so
 * e.g. a fire station's 3x3 lot reads as a ring of stars around F). */
static chtype
as_bldg(int raw, int letter, int fill)
{
  if (raw & ZONEBIT) return ((chtype)letter) | A_BOLD | A_REVERSE;
  return ((chtype)fill) | A_REVERSE;
}

/* Decode Map[mx][my] -> 7-bit glyph + mono attributes (same tile decode as
 * nc_cell; the art maps the default mode's palette onto A_BOLD/A_REVERSE). */
static chtype
as_cell(int mx, int my)
{
  int raw = Map[mx][my];
  int blink = (flagBlink <= 0);
  int t, cls;

  t = raw;
  if ((t & LOMASK) >= TILE_COUNT) t -= TILE_COUNT;
  if (blink && (t & ZONEBIT) && !(t & PWRBIT)) {
    t = LIGHTNINGBOLT;
  } else {
    t &= LOMASK;
  }

  if (t == LIGHTNINGBOLT)				/* unpowered zone blink */
    return ((chtype)'!') | A_BOLD;

  cls = nc_transit_class(t);
  if (cls == 1) {
    if (t >= HTRFBASE && t <= LASTROAD)			/* heavy traffic */
      return ((chtype)(((mx + my) & 1) ? ',' : '`')) | A_BOLD;
    return (chtype)as_road[nc_transit_mask(mx, my, 1)];	/* plain: road has no fill */
  }
  if (cls == 2)
    return ((chtype)'#') | A_REVERSE;
  if (cls == 3)
    return ((chtype)as_road[nc_transit_mask(mx, my, 3)]) | A_BOLD;

  if (t == DIRT) return (chtype)' ';
  if (t >= RIVER && t <= LASTRIVEDGE) return ((chtype)'~') | A_REVERSE;
  if (t >= TREEBASE && t < WOODS2) return (chtype)'^';
  if (t >= WOODS2 && t <= WOODS5) return (chtype)'"';	/* user-placed park */
  if (t >= RUBBLE && t <= LASTRUBBLE) return (chtype)'%';
  if (t >= FLOOD && t <= LASTFLOOD) return ((chtype)'~') | A_BOLD;
  if (t == RADTILE) return ((chtype)'*') | A_BOLD;
  if (t >= FIREBASE && t <= LASTFIRE) return ((chtype)'*') | A_BOLD | A_REVERSE;

  if (t >= HOSPITAL - 4 && t <= HOSPITAL + 4)
    return (t == HOSPITAL) ? (((chtype)'H') | A_BOLD | A_REVERSE)
			   : (((chtype)' ') | A_REVERSE);
  if (t >= CHURCH - 4 && t <= CHURCH + 4)
    return (t == CHURCH) ? (((chtype)'X') | A_BOLD | A_REVERSE)
			 : (((chtype)' ') | A_REVERSE);

  if (t >= RESBASE && t < COMBASE) {
    if (t >= LHTHR && t <= HHTHR) return (chtype)'r';	/* single family house */
    return as_zone(t, raw, 'R', RESBASE, RZB - 4, HOSPITAL - 5, 4);
  }
  if (t >= COMBASE && t < INDBASE)
    return as_zone(t, raw, 'C', COMBASE, CZB - 4, INDBASE - 1, 5);
  if (t >= INDBASE && t < PORTBASE)
    return as_zone(t, raw, 'I', INDBASE, IZB - 4, PORTBASE - 1, 4);

  if (t >= PORTBASE && t <= LASTPORT) return as_bldg(raw, 'W', ' ');
  if (t >= RADAR0 && t <= RADAR7)			/* airport tower (anim) */
    return ((chtype)'A') | A_REVERSE;
  if (t >= AIRPORTBASE && t < COALBASE) return as_bldg(raw, 'A', ' ');
  if (t >= COALBASE && t <= LASTPOWERPLANT) return as_bldg(raw, 'E', ' ');
  if (t >= FIRESTBASE && t < POLICESTBASE) return as_bldg(raw, 'F', '*');
  if (t >= POLICESTBASE && t < STADIUMBASE) return as_bldg(raw, 'P', '=');
  if (t >= FOOTBALLGAME1 && t <= FOOTBALLGAME2)		/* game day (anim) */
    return ((chtype)'S') | A_REVERSE;
  if (t >= STADIUMBASE && t < NUCLEARBASE) return as_bldg(raw, 'S', ' ');
  if (t >= NUCLEARBASE && t <= LASTZONE) return as_bldg(raw, 'N', ' ');

  if ((t >= HBRDG0 && t <= HBRDG3) || (t >= VBRDG0 && t <= VBRDG3))
    return ((chtype)'=') | A_BOLD;			/* open drawbridge */
  if (t >= FOUNTAIN && t <= INDBASE2) return (chtype)'"';
  if (t >= TINYEXP && t <= LASTTINYEXP)
    return ((chtype)'*') | A_BOLD | A_REVERSE;		/* explosion */
  if ((t >= SMOKEBASE && t < TINYEXP) ||
      (t >= COALSMOKE1 && t <= COALSMOKE4 + 3))
    return ((chtype)'*') | A_BOLD;

  return (chtype)'*';					/* unknown tile */
}

static void
as_tile(int sy, int sx, int mx, int my)
{
  mvaddch(sy, sx, as_cell(mx, my));
}

/* same glyphs as the default mode's sprites, colors dropped */
static void
as_sprite(int sy, int sx, int type)
{
  chtype c;

  switch (type) {
  case TRA: c = ((chtype)'e') | A_BOLD; break;		/* train      */
  case COP: c = ((chtype)'H') | A_BOLD; break;		/* helicopter */
  case AIR: c = ((chtype)'>') | A_BOLD; break;		/* airplane   */
  case SHI: c = ((chtype)'b') | A_BOLD; break;		/* ship       */
  case GOD: c = ((chtype)'M') | A_BOLD | A_REVERSE; break;  /* monster   */
  case TOR: c = ((chtype)'@') | A_BOLD | A_REVERSE; break;  /* tornado   */
  case EXP: c = ((chtype)'*') | A_BOLD | A_REVERSE; break;  /* explosion */
  case BUS: c = ((chtype)'u') | A_BOLD; break;
  default:  c = (chtype)'?'; break;
  }
  mvaddch(sy, sx, c);
}

/* XOR, not OR: the cursor must stay visible on already-reversed tiles
 * (rails, buildings), where OR-ing A_REVERSE would be a no-op.  The XOR of
 * a zone center (letter|BOLD|REVERSE) is letter|BOLD -- identical to the
 * dense lot cells around it -- so underline too: no tile uses underline,
 * making the cursor cell unique everywhere (and it is a native vt100 attr). */
static void
as_cursor(int sy, int sx, int mx, int my)
{
  mvaddch(sy, sx, (as_cell(mx, my) ^ A_REVERSE) | A_UNDERLINE);
}

/* ==================== acs mode (1 col/tile, monochrome ACS lines) ========= */

/* Same as as_cell, but roads/rails/wires draw real ACS box-drawing
 * (nc_line_glyph, the same auto-tiler the "color" mode uses)
 * instead of the 7-bit +/-/| approximation.  Plain 8-bit curses ACS has no
 * double-line glyphs (that's a wide-ncurses cchar_t/WACS_D_* or Unicode-only
 * thing), so road/rail stay visually distinct the same way the default color
 * mode does it: road is a plain line (no fill), rail is reverse video (the
 * ballast strip).  Same bold/reverse everywhere else -- still no color set
 * anywhere. */
static chtype
ln_cell(int mx, int my)
{
  int raw = Map[mx][my];
  int blink = (flagBlink <= 0);
  int t, cls;

  t = raw;
  if ((t & LOMASK) >= TILE_COUNT) t -= TILE_COUNT;
  if (blink && (t & ZONEBIT) && !(t & PWRBIT)) {
    t = LIGHTNINGBOLT;
  } else {
    t &= LOMASK;
  }

  if (t == LIGHTNINGBOLT)				/* unpowered zone blink */
    return ((chtype)'!') | A_BOLD;

  cls = nc_transit_class(t);
  if (cls == 1) {
    if (t >= HTRFBASE && t <= LASTROAD)			/* heavy traffic */
      return ((chtype)(((mx + my) & 1) ? ',' : '`')) | A_BOLD;
    return nc_line_glyph(mx, my, 1);			/* plain: road has no fill */
  }
  if (cls == 2)
    return nc_line_glyph(mx, my, 2) | A_REVERSE;	/* rail: reverse ballast strip */
  if (cls == 3)
    return nc_line_glyph(mx, my, 3) | A_BOLD;

  if (t == DIRT) return (chtype)' ';
  if (t >= RIVER && t <= LASTRIVEDGE) return ((chtype)'~') | A_REVERSE;
  if (t >= TREEBASE && t < WOODS2) return (chtype)'^';
  if (t >= WOODS2 && t <= WOODS5) return (chtype)'"';	/* user-placed park */
  if (t >= RUBBLE && t <= LASTRUBBLE) return (chtype)'%';
  if (t >= FLOOD && t <= LASTFLOOD) return ((chtype)'~') | A_BOLD;
  if (t == RADTILE) return ((chtype)'*') | A_BOLD;
  if (t >= FIREBASE && t <= LASTFIRE) return ((chtype)'*') | A_BOLD | A_REVERSE;

  if (t >= HOSPITAL - 4 && t <= HOSPITAL + 4)
    return (t == HOSPITAL) ? (((chtype)'H') | A_BOLD | A_REVERSE)
			   : (((chtype)' ') | A_REVERSE);
  if (t >= CHURCH - 4 && t <= CHURCH + 4)
    return (t == CHURCH) ? (((chtype)'X') | A_BOLD | A_REVERSE)
			 : (((chtype)' ') | A_REVERSE);

  if (t >= RESBASE && t < COMBASE) {
    if (t >= LHTHR && t <= HHTHR) return (chtype)'r';	/* single family house */
    return as_zone(t, raw, 'R', RESBASE, RZB - 4, HOSPITAL - 5, 4);
  }
  if (t >= COMBASE && t < INDBASE)
    return as_zone(t, raw, 'C', COMBASE, CZB - 4, INDBASE - 1, 5);
  if (t >= INDBASE && t < PORTBASE)
    return as_zone(t, raw, 'I', INDBASE, IZB - 4, PORTBASE - 1, 4);

  if (t >= PORTBASE && t <= LASTPORT) return as_bldg(raw, 'W', ' ');
  if (t >= RADAR0 && t <= RADAR7)			/* airport tower (anim) */
    return ((chtype)'A') | A_REVERSE;
  if (t >= AIRPORTBASE && t < COALBASE) return as_bldg(raw, 'A', ' ');
  if (t >= COALBASE && t <= LASTPOWERPLANT) return as_bldg(raw, 'E', ' ');
  if (t >= FIRESTBASE && t < POLICESTBASE) return as_bldg(raw, 'F', '*');
  if (t >= POLICESTBASE && t < STADIUMBASE) return as_bldg(raw, 'P', '=');
  if (t >= FOOTBALLGAME1 && t <= FOOTBALLGAME2)		/* game day (anim) */
    return ((chtype)'S') | A_REVERSE;
  if (t >= STADIUMBASE && t < NUCLEARBASE) return as_bldg(raw, 'S', ' ');
  if (t >= NUCLEARBASE && t <= LASTZONE) return as_bldg(raw, 'N', ' ');

  if ((t >= HBRDG0 && t <= HBRDG3) || (t >= VBRDG0 && t <= VBRDG3))
    return ((chtype)'=') | A_BOLD;			/* open drawbridge */
  if (t >= FOUNTAIN && t <= INDBASE2) return (chtype)'"';
  if (t >= TINYEXP && t <= LASTTINYEXP)
    return ((chtype)'*') | A_BOLD | A_REVERSE;		/* explosion */
  if ((t >= SMOKEBASE && t < TINYEXP) ||
      (t >= COALSMOKE1 && t <= COALSMOKE4 + 3))
    return ((chtype)'*') | A_BOLD;

  return (chtype)'*';					/* unknown tile */
}

static void
ln_tile(int sy, int sx, int mx, int my)
{
  mvaddch(sy, sx, ln_cell(mx, my));
}

static void
ln_cursor(int sy, int sx, int mx, int my)
{
  mvaddch(sy, sx, (ln_cell(mx, my) ^ A_REVERSE) | A_UNDERLINE);
}

/* ======================== mode registry ================================== */

static struct GfxOps GfxDefault =
  { "color",    1, 0, 0, NULL,     df_tile,  df_sprite,  df_cursor  };
static struct GfxOps GfxUnicode =
  { "unicode", 2, 1, 0, uni_avail, uni_tile, uni_sprite, uni_cursor };
static struct GfxOps GfxAscii =
  { "ascii",   1, 0, 1, NULL,      as_tile,  as_sprite,  as_cursor  };
static struct GfxOps GfxAcs =
  { "acs",     1, 0, 1, NULL,      ln_tile,  as_sprite,  ln_cursor  };

/* future: braille microtiles */
static struct GfxOps *modes[] = { &GfxDefault, &GfxUnicode, &GfxAscii, &GfxAA, &GfxAcs };
#define NMODES ((int)(sizeof(modes) / sizeof(modes[0])))

struct GfxOps *Gfx = &GfxDefault;

/* select by name (-gfx flag); returns 0 if unknown or unavailable */
int
nc_gfx_set(char *name)
{
  int i;

  if (!name) return 0;
  if (!strcmp(name, "default") || !strcmp(name, "standard")) name = "color";
  if (!strcmp(name, "emoji")) name = "unicode";
  if (!strcmp(name, "vt100") || !strcmp(name, "mono") ||
      !strcmp(name, "bw") || !strcmp(name, "7bit")) name = "ascii";
  if (!strcmp(name, "aalib") || !strcmp(name, "aa-lib")) name = "aa";
  for (i = 0; i < NMODES; i++) {
    if (strcmp(modes[i]->name, name)) continue;
    if (modes[i]->avail && !modes[i]->avail()) return 0;
    Gfx = modes[i];
    return 1;
  }
  return 0;
}

/* List every registered mode with a one-line blurb and live availability;
 * used for -gfx with a missing/unknown argument, "-gfx help", "-gfx list". */
void
nc_gfx_list(FILE *f)
{
  static char *blurb[NMODES] = {
    "1 col/tile, ACS + 8 colors -- default, works on any curses",
    "2 cols/tile, UTF-8 emoji tiles -- needs a UTF-8 locale",
    "1 col/tile, strict 7-bit, monochrome -- vt100-safe",
    "2 cols/tile, aalib dithered shading, in color -- `make aalib` build",
    "1 col/tile, ACS lines, monochrome -- bold/reverse only, no color"
  };
  int i;

  fprintf(f, "available -gfx modes:\n");
  for (i = 0; i < NMODES; i++)
    fprintf(f, "  %-9s%s%s\n", modes[i]->name, blurb[i],
	    (modes[i]->avail && !modes[i]->avail()) ? "  [unavailable]" : "");
}

/* Pick a startup mode from the environment when no -gfx flag was given; runs
 * after initscr() so has_colors() reflects the real terminfo entry.
 *   no color      -> ascii    vt100/vt220/dumb/xterm-mono, real DEC iron, the
 *                             OpenBSD console: no color capability, and the
 *                             7-bit mode is the only safe bet there anyway.
 *   modern + UTF-8-> unicode  needs emoji fonts, not just a UTF-8 locale, so
 *                             ask for an emulator-class TERM ("256color") or
 *                             COLORTERM=truecolor|24bit (kitty, alacritty,
 *                             foot, ghostty advertise that way).  Raw consoles
 *                             (linux, cons25, wsvt25, FreeBSD vt's "xterm")
 *                             set neither -- their fonts have no emoji.
 *   anything else -> default  8 colors + ACS works on every color terminal:
 *                             plain xterm, screen/tmux, the consoles above,
 *                             regardless of locale (ACS is locale-blind). */
void
nc_gfx_auto(void)
{
  char *term = getenv("TERM");
  char *ct = getenv("COLORTERM");

  if (!has_colors()) {
    nc_gfx_set("ascii");
    return;
  }
  if ((term && strstr(term, "256color")) ||
      (ct && (!strcmp(ct, "truecolor") || !strcmp(ct, "24bit"))))
    nc_gfx_set("unicode");	/* still refused unless the locale is UTF-8 */
}

/* Snap a popup rectangle to the tile grid in 2-col modes.  Tiles and sprites
 * all start at columns of EdLeft's parity; if a popup edge lands on the
 * second column of a double-width glyph, old ncursesw mangles the row (half
 * the emoji survives or the text lands one cell off), which shows up as
 * alternate lines of a dropdown/dialog shifted sideways. */
void
nc_popup_snap(int *x, int *w)
{
  if (Gfx->tilew < 2) return;
  if ((*x - EdLeft) & 1) (*x)--;
  if (*x < 0) *x += 2;
  if (*w & 1) (*w)++;
}

/* ---- accessors for the picker (nc_gfx_modal, nc_dialogs.c) --------------- */

int
nc_gfx_count(void)
{
  return NMODES;
}

char *
nc_gfx_name_at(int i)
{
  return (i >= 0 && i < NMODES) ? modes[i]->name : NULL;
}

int
nc_gfx_avail_at(int i)
{
  if (i < 0 || i >= NMODES) return 0;
  return !modes[i]->avail || modes[i]->avail();
}

int
nc_gfx_current(void)
{
  int i;

  for (i = 0; i < NMODES; i++)
    if (modes[i] == Gfx) return i;
  return 0;
}

/* activate mode i ('u' key / Options menu picker); 0 and no-op if out of
 * range or currently unavailable */
int
nc_gfx_select_at(int i)
{
  if (!nc_gfx_avail_at(i)) return 0;
  Gfx = modes[i];
  nc_colors_init();	/* colors start lazily on first switch to a color mode */
  return 1;
}
