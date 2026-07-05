/* nc_gfx.c  --  pluggable tile-graphics modes for the editor viewport.
 *
 * A mode = one GfxOps: how many screen columns a map tile takes (tilew) and
 * how to draw a tile / sprite / cursor there.  nc_render.c owns layout and
 * panning and dispatches through Gfx, so modes never deal with scrolling.
 *
 *   default  1 col/tile, plain ASCII + ACS + 8 colors (nc_cell, nc_render.c).
 *            Works on any curses ever made -- this is the old-Unix mode.
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
 *
 * Future slots (see TODO.md): braille 2x4-dot microtiles, aalib-style
 * shading.  Add a GfxOps and list it in modes[].
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

/* ======================== default mode (1 col/tile) ====================== */

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

/* needs a multibyte (UTF-8) locale; setlocale(LC_ALL,"") ran at startup */
static int
uni_avail(void)
{
  return MB_CUR_MAX > 1;
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

/* quadrant-block density fills: lots visibly fill in as the zone grows */
static char *fill4[4] = { "▗ ", "▚ ", "▚▞", "▙▟" };
static char *fill5[5] = { "▗ ", "▚ ", "▞▖", "▛▞", "▟▛" };

/* zone-center skylines, low to high density */
static char *res_ctr[4] = { "🏡", "🏠", "🏨", "🌆" };
static char *com_ctr[5] = { "🏪", "🏬", "🏦", "🏢", "🌃" };
static char *ind_ctr[4] = { "🔨", "🔧", "🏭", "🏭" };

/* braille terrain textures */
static char *dirt_tex[4]  = { "⠐⠄", "⠠⠂", "⠄⠐", "⠂⠠" };
static char *water_tex[4] = { "⣀⣀", "⠤⠤", "⣀⠤", "⠤⣀" };

/* quadrant-block debris */
static char *rubble_tex[4] = { "▚▞", "▞▖", "▙▘", "▗▚" };

static void
uni_tile(int sy, int sx, int mx, int my)
{
  int raw = Map[mx][my];
  int blink = (flagBlink <= 0);
  int land = ThemeLand;
  int t, cls, center, h, den, vac;

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

  /* ---- R/C/I zones: emoji skyline center, quadrant fills around it ---- */
  if (t >= RESBASE && t < COMBASE) {
    if (t >= LHTHR && t <= HHTHR) {			/* single family house */
      uput(sy, sx, "🏡", COLOR_BLACK, COLOR_CYAN, 0);
      return;
    }
    den = nc_zone_den(t, RZB - 4, HOSPITAL - 5, 4, &vac);
    if (den >= 0) {
      if (center)   uput(sy, sx, res_ctr[den], COLOR_BLACK, COLOR_CYAN, 0);
      else if (vac) uput(sy, sx, "░░", COLOR_BLACK, COLOR_CYAN, 0);
      else          uput(sy, sx, fill4[den], COLOR_BLACK, COLOR_CYAN, 0);
      return;
    }
    if (t <= RESBASE + 8) {				/* empty designated lot */
      if (center) uput(sy, sx, "🚧", COLOR_YELLOW, COLOR_CYAN, A_BOLD);
      else        uput(sy, sx, "░░", COLOR_BLACK, COLOR_CYAN, 0);
      return;
    }
    uput(sy, sx, "▚ ", COLOR_BLACK, COLOR_CYAN, 0);	/* stray tile */
    return;
  }
  if (t >= COMBASE && t < INDBASE) {
    den = nc_zone_den(t, CZB - 4, INDBASE - 1, 5, &vac);
    if (den >= 0) {
      if (center)   uput(sy, sx, com_ctr[den], COLOR_WHITE, COLOR_BLUE, A_BOLD);
      else if (vac) uput(sy, sx, "░░", COLOR_WHITE, COLOR_BLUE, 0);
      else          uput(sy, sx, fill5[den], COLOR_WHITE, COLOR_BLUE, A_BOLD);
      return;
    }
    if (t <= COMBASE + 8) {
      if (center) uput(sy, sx, "🚧", COLOR_YELLOW, COLOR_BLUE, A_BOLD);
      else        uput(sy, sx, "░░", COLOR_WHITE, COLOR_BLUE, 0);
      return;
    }
    uput(sy, sx, "▚ ", COLOR_WHITE, COLOR_BLUE, A_BOLD);
    return;
  }
  if (t >= INDBASE && t < PORTBASE) {
    den = nc_zone_den(t, IZB - 4, PORTBASE - 1, 4, &vac);
    if (den >= 0) {
      if (center)   uput(sy, sx, ind_ctr[den], COLOR_YELLOW, COLOR_MAGENTA, A_BOLD);
      else if (vac) uput(sy, sx, "░░", COLOR_YELLOW, COLOR_MAGENTA, 0);
      else          uput(sy, sx, fill4[den], COLOR_YELLOW, COLOR_MAGENTA, A_BOLD);
      return;
    }
    if (t <= INDBASE + 8) {
      if (center) uput(sy, sx, "🚧", COLOR_YELLOW, COLOR_MAGENTA, A_BOLD);
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
    if (center) uput(sy, sx, "🚨", COLOR_WHITE, COLOR_BLUE, A_BOLD);
    else        uput(sy, sx, "▒▒", COLOR_WHITE, COLOR_BLUE, 0);
    return;
  }
  if (t >= FOOTBALLGAME1 && t <= FOOTBALLGAME2) {	/* game day (anim) */
    uput(sy, sx, "🏈", COLOR_WHITE, COLOR_GREEN, 0);
    return;
  }
  if (t >= STADIUMBASE && t < NUCLEARBASE) {		/* stadium */
    if (center) uput(sy, sx, (t >= FULLSTADIUM) ? "🏈" : "⚽",
		     COLOR_BLACK, COLOR_WHITE, A_BOLD);
    else        uput(sy, sx, "▒▒", COLOR_BLACK, COLOR_WHITE, 0);
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

/* emoji ignore A_REVERSE in most terminals, so bracket the tile instead */
static void
uni_cursor(int sy, int sx, int mx, int my)
{
  attrset(NC_CP(COLOR_YELLOW, COLOR_BLACK) | A_BOLD);
  if (sx - 1 >= EdLeft)       mvaddch(sy, sx - 1, '[');
  if (sx + 2 < EdLeft + EdW)  mvaddch(sy, sx + 2, ']');
  attrset(A_NORMAL);
}

/* ==================== ascii mode (1 col/tile, monochrome) ================= */

/* Poor man's line drawing indexed by nc_transit_mask (up<<3|dn<<2|lf<<1|rt).
 * Roads and wires share the | - + set (wires drawn bold); rails get '='
 * horizontals and are drawn in reverse video (the ballast strip, like the
 * default mode's black-on-white). */
static char as_road[17] = "+---|+++|+++|+++";
static char as_rail[17] = "+===|+++|+++|+++";

/* One zone lot cell (mirrors zone_glyph, nc_render.c, minus the colors):
 * reverse-video bold letter = built center, UPPER/lower letter = dense/sparse
 * lot, '.' = still-vacant lot, bare letter = empty designated zone. */
static chtype
as_zone(int t, int raw, int letter, int emptyBase, int builtBase,
	int builtHi, int dmod)
{
  int den, vac;

  den = nc_zone_den(t, builtBase, builtHi, dmod, &vac);
  if (den >= 0) {					/* built zone */
    if (raw & ZONEBIT) return ((chtype)letter) | A_BOLD | A_REVERSE;
    if (vac) return (chtype)'.';
    if (den * 2 >= dmod) return ((chtype)letter) | A_BOLD;
    return (chtype)(letter + 32);
  }
  if (t >= emptyBase && t <= emptyBase + 8)		/* empty designated lot */
    return (t - emptyBase == 4) ? (chtype)letter : (chtype)'.';
  return (chtype)(letter + 32);				/* stray tile */
}

/* Big civic buildings: a reverse-video block with a bold letter at the
 * center tile, so a 3x3 station reads as one solid building. */
static chtype
as_bldg(int raw, int letter)
{
  if (raw & ZONEBIT) return ((chtype)letter) | A_BOLD | A_REVERSE;
  return ((chtype)' ') | A_REVERSE;
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
    return (chtype)as_road[nc_transit_mask(mx, my, 1)];
  }
  if (cls == 2)
    return ((chtype)as_rail[nc_transit_mask(mx, my, 2)]) | A_REVERSE;
  if (cls == 3)
    return ((chtype)as_road[nc_transit_mask(mx, my, 3)]) | A_BOLD;

  if (t == DIRT) return (chtype)'.';
  if (t >= RIVER && t <= LASTRIVEDGE) return (chtype)'~';
  if (t >= TREEBASE && t < WOODS2) return (chtype)'^';
  if (t >= WOODS2 && t <= WOODS5) return (chtype)'"';	/* user-placed park */
  if (t >= RUBBLE && t <= LASTRUBBLE) return (chtype)'%';
  if (t >= FLOOD && t <= LASTFLOOD) return ((chtype)'~') | A_BOLD;
  if (t == RADTILE) return ((chtype)'*') | A_BOLD;
  if (t >= FIREBASE && t <= LASTFIRE) return ((chtype)'!') | A_BOLD | A_REVERSE;

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

  if (t >= PORTBASE && t <= LASTPORT) return as_bldg(raw, 'W');
  if (t >= RADAR0 && t <= RADAR7)			/* airport tower (anim) */
    return ((chtype)'A') | A_REVERSE;
  if (t >= AIRPORTBASE && t < COALBASE) return as_bldg(raw, 'A');
  if (t >= COALBASE && t <= LASTPOWERPLANT) return as_bldg(raw, 'E');
  if (t >= FIRESTBASE && t < POLICESTBASE) return as_bldg(raw, 'F');
  if (t >= POLICESTBASE && t < STADIUMBASE) return as_bldg(raw, 'P');
  if (t >= FOOTBALLGAME1 && t <= FOOTBALLGAME2)		/* game day (anim) */
    return ((chtype)'S') | A_REVERSE;
  if (t >= STADIUMBASE && t < NUCLEARBASE) return as_bldg(raw, 'S');
  if (t >= NUCLEARBASE && t <= LASTZONE) return as_bldg(raw, 'N');

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

/* ======================== mode registry ================================== */

static struct GfxOps GfxDefault =
  { "default", 1, 0, 0, NULL,      df_tile,  df_sprite,  df_cursor  };
static struct GfxOps GfxUnicode =
  { "unicode", 2, 1, 0, uni_avail, uni_tile, uni_sprite, uni_cursor };
static struct GfxOps GfxAscii =
  { "ascii",   1, 0, 1, NULL,      as_tile,  as_sprite,  as_cursor  };

/* future: braille microtiles, aalib shading */
static struct GfxOps *modes[] = { &GfxDefault, &GfxUnicode, &GfxAscii };
#define NMODES ((int)(sizeof(modes) / sizeof(modes[0])))

struct GfxOps *Gfx = &GfxDefault;

/* select by name (-gfx flag); returns 0 if unknown or unavailable */
int
nc_gfx_set(char *name)
{
  int i;

  if (!name) return 0;
  if (!strcmp(name, "emoji")) name = "unicode";
  if (!strcmp(name, "vt100") || !strcmp(name, "mono") ||
      !strcmp(name, "bw") || !strcmp(name, "7bit")) name = "ascii";
  for (i = 0; i < NMODES; i++) {
    if (strcmp(modes[i]->name, name)) continue;
    if (modes[i]->avail && !modes[i]->avail()) return 0;
    Gfx = modes[i];
    return 1;
  }
  return 0;
}

/* step to the next available mode; returns its name (for the status line) */
char *
nc_gfx_cycle(void)
{
  int i, k;

  for (i = 0; i < NMODES; i++)
    if (modes[i] == Gfx) break;
  for (k = 1; k <= NMODES; k++) {
    struct GfxOps *m = modes[(i + k) % NMODES];
    if (m->avail && !m->avail()) continue;
    Gfx = m;
    break;
  }
  nc_colors_init();	/* colors start lazily on first switch to a color mode */
  return Gfx->name;
}
