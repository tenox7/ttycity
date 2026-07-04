/* nc_render.c  --  editor viewport rendering for the ncurses port.
 *
 * Replaces the X11 tile blitter (g_bigmap.c / w_editor.c).  The per-cell tile
 * decode is the same as the original MemDrawBeegMapRect (g_bigmap.c:60-68),
 * after which the tile index is mapped to a character + curses color instead
 * of a 16x16 pixmap.  nc_cell() here is the DEFAULT graphics mode (one plain
 * chtype per tile); the editor loop dispatches through Gfx (nc_gfx.c) so
 * alternate modes (unicode/emoji, braille, ...) can draw tiles differently.
 */

#include "sim.h"
#include <curses.h>
#include "nc.h"

void nc_draw_sprites(void);		/* defined below; called by nc_draw_editor */

/* Earthquake screen-shake (engine callbacks; ShakeNow counts frames down). */
void DoEarthQuake(void)   { ShakeNow = 40; }
void StopEarthquake(void) { ShakeNow = 0; }

/* Land background color for the "light" theme (like the original game): the
 * bare-ground surface everything sits on.  COLOR_YELLOW = tan (default),
 * COLOR_GREEN = grass, COLOR_BLACK = dark.  Set via -theme / Options menu. */
int ThemeLand = COLOR_YELLOW;

static char *
nc_theme_name(void)
{
  if (ThemeLand == COLOR_GREEN) return "grass";
  if (ThemeLand == COLOR_BLACK) return "dark";
  return "tan";
}

char *
nc_cycle_theme(void)
{
  if (ThemeLand == COLOR_YELLOW)     ThemeLand = COLOR_GREEN;
  else if (ThemeLand == COLOR_GREEN) ThemeLand = COLOR_BLACK;
  else                               ThemeLand = COLOR_YELLOW;
  return nc_theme_name();
}

void
nc_set_theme(char *name)
{
  if (!name) return;
  if (!strcmp(name, "grass") || !strcmp(name, "green")) ThemeLand = COLOR_GREEN;
  else if (!strcmp(name, "dark") || !strcmp(name, "black")) ThemeLand = COLOR_BLACK;
  else ThemeLand = COLOR_YELLOW;	/* tan */
}

/* Transit class of a tile for line-drawing: 0 none, 1 road, 2 rail, 3 power. */
int
nc_transit_class(int t)
{
  t &= LOMASK;
  if (t >= ROADBASE && t <= LASTROAD) return 1;
  if (t >= RAILBASE && t <= LASTRAIL) return 2;
  if (t >= POWERBASE && t <= LASTPOWER) return 3;
  return 0;
}

/* Four same-class neighbor bits: (up<<3)|(dn<<2)|(lf<<1)|rt.  Shared by the
 * ACS auto-tiler below and the per-mode line tables in nc_gfx.c. */
int
nc_transit_mask(int x, int y, int cls)
{
  int up, dn, lf, rt;

  up = (y > 0)           && (nc_transit_class(Map[x][y - 1]) == cls);
  dn = (y < WORLD_Y - 1) && (nc_transit_class(Map[x][y + 1]) == cls);
  lf = (x > 0)           && (nc_transit_class(Map[x - 1][y]) == cls);
  rt = (x < WORLD_X - 1) && (nc_transit_class(Map[x + 1][y]) == cls);

  return (up << 3) | (dn << 2) | (lf << 1) | rt;
}

/* Line-drawing glyph chosen from the four same-class neighbors. */
static chtype
line_glyph(int x, int y, int cls)
{
  int m = nc_transit_mask(x, y, cls);

  switch (m) {
  case 0:  return ACS_PLUS;		/* isolated segment */
  case 1:  case 2:  case 3:  return ACS_HLINE;
  case 4:  case 8:  case 12: return ACS_VLINE;
  case 5:  return ACS_ULCORNER;		/* down + right */
  case 6:  return ACS_URCORNER;		/* down + left  */
  case 9:  return ACS_LLCORNER;		/* up + right   */
  case 10: return ACS_LRCORNER;		/* up + left    */
  case 7:  return ACS_TTEE;		/* down + left + right */
  case 11: return ACS_BTEE;		/* up + left + right   */
  case 13: return ACS_LTEE;		/* up + down + right   */
  case 14: return ACS_RTEE;		/* up + down + left    */
  case 15: return ACS_PLUS;
  }
  return ACS_HLINE;
}

/*
 * One zone cell, drawn as a lot that fills in as the zone develops.  A zone is
 * a 3x3 image of consecutive tiles (ZonePlop: base+0..base+8, center = base+4
 * carries ZONEBIT), and its density is baked into the tile index -- so every
 * cell independently recovers its position in the lot and the zone's level.
 * Empty/houses stages render straight from real tiles; the built-zone gradient
 * (how many of the 9 cells show the type letter vs a vacant '.') is synthesized
 * from the density so growth and decline are visible at a glance.
 *   emptyBase  first tile of the empty designated lot (center = emptyBase+4)
 *   builtBase  ZB-4 (first tile of the lowest built image)
 *   dmod       number of density levels (Res 4, Com 5, Ind 4)
 *   houseLo/Hi single-family house tiles (Res only; -1 for none)
 */
static chtype
zone_glyph(int t, int letter, int fg, int bg, int baseAttr,
	   int emptyBase, int builtBase, int builtHi, int dmod,
	   int houseLo, int houseHi)
{
  static int rank[9] = { 5, 1, 6,  3, 0, 4,  7, 2, 8 };  /* fill order, center first */
  int off, den, thr, up, ch;

  if (houseLo >= 0 && t >= houseLo && t <= houseHi)	/* a single house */
    return ((chtype)(letter + 32)) | NC_CP(fg, bg) | baseAttr;

  if (t >= builtBase && t <= builtHi) {			/* built zone */
    off = (t - builtBase) % 9;
    den = ((t - builtBase) / 9) % dmod;			/* 0..dmod-1 */
    thr = ((den + 1) * 8) / dmod;			/* cells revealed: 0..8 */
    if (off != 4 && rank[off] > thr)			/* lot still vacant here */
      return ((chtype)'.') | NC_CP(fg, bg) | baseAttr;
    up = (off == 4) || (den * 2 >= dmod);		/* UPPER = center or dense */
    ch = up ? letter : (letter + 32);
    return ((chtype)ch) | NC_CP(fg, bg) | baseAttr | (off == 4 ? A_BOLD : 0);
  }

  if (t >= emptyBase && t <= emptyBase + 8) {		/* empty designated lot */
    if (t - emptyBase == 4)				/* center: zone marker */
      return ((chtype)letter) | NC_CP(fg, bg) | baseAttr;
    return ((chtype)'.') | NC_CP(COLOR_BLACK, bg);
  }

  return ((chtype)(letter + 32)) | NC_CP(fg, bg) | baseAttr;  /* stray tile */
}

/* Decode Map[mapx][mapy] into a ready-to-draw chtype (glyph + color + attr). */
chtype
nc_cell(int mapx, int mapy)
{
  int raw = Map[mapx][mapy];
  int blink = (flagBlink <= 0);
  int t, ch, fg, bg, bold, cls;
  int land = ThemeLand;

  /* same decode as MemDrawBeegMapRect (g_bigmap.c) */
  t = raw;
  if ((t & LOMASK) >= TILE_COUNT) t -= TILE_COUNT;
  if (blink && (t & ZONEBIT) && !(t & PWRBIT)) {
    t = LIGHTNINGBOLT;
  } else {
    t &= LOMASK;
  }

  /* Transit lines all reuse the ACS auto-tiling glyph but stay distinct by
   * (fg,bg): road = white-on-black, rail = black-on-white (inverse ballast
   * strip), power = red-on-land.  Heavy-traffic road tiles (index >= HTRFBASE)
   * show alternating `,` car glyphs. */
  cls = nc_transit_class(t);
  if (cls == 1) {
    if (t >= HTRFBASE && t <= LASTROAD) {		/* heavy traffic */
      ch = ((mapx + mapy) & 1) ? ',' : '`';
      return ((chtype)ch) | NC_CP(COLOR_YELLOW, COLOR_BLACK) | A_BOLD;
    }
    return line_glyph(mapx, mapy, 1) | NC_CP(COLOR_WHITE, COLOR_BLACK);
  }
  if (cls == 2) return line_glyph(mapx, mapy, 2) | NC_CP(COLOR_BLACK, COLOR_WHITE);
  if (cls == 3) return line_glyph(mapx, mapy, 3) | NC_CP(COLOR_RED, land) | A_BOLD;

  /* Light theme (modeled on the original game): the surface is a background
   * color, glyphs/letters are drawn on top. */
  ch = '?'; fg = COLOR_WHITE; bg = COLOR_BLACK; bold = 0;

  if (t == DIRT) {
    ch = '.'; fg = COLOR_BLACK; bg = land;		/* bare land */
  } else if (t >= RIVER && t <= LASTRIVEDGE) {
    ch = '~'; fg = COLOR_WHITE; bg = COLOR_BLUE;		/* water */
  } else if (t >= TREEBASE && t < WOODS2) {
    ch = '^'; fg = COLOR_BLACK; bg = COLOR_GREEN;	/* natural forest */
  } else if (t >= WOODS2 && t <= WOODS5) {
    ch = '"'; fg = COLOR_BLACK; bg = COLOR_GREEN;	/* user-placed park */
  } else if (t >= RUBBLE && t <= LASTRUBBLE) {
    ch = '%'; fg = COLOR_BLACK; bg = land;		/* rubble */
  } else if (t >= FLOOD && t <= LASTFLOOD) {
    ch = '~'; fg = COLOR_WHITE; bg = COLOR_CYAN; bold = 1;
  } else if (t == RADTILE) {
    ch = '*'; fg = COLOR_WHITE; bg = COLOR_MAGENTA; bold = 1;
  } else if (t >= FIREBASE && t <= LASTFIRE) {
    ch = '!'; fg = COLOR_YELLOW; bg = COLOR_RED; bold = 1;
  } else if (t == LIGHTNINGBOLT) {
    ch = '!'; fg = COLOR_RED; bg = land; bold = 1;	/* unpowered blink */
  } else if (t == HOSPITAL) {
    ch = '+'; fg = COLOR_WHITE; bg = COLOR_RED; bold = 1;
  } else if (t == CHURCH) {
    ch = 'X'; fg = COLOR_BLACK; bg = COLOR_CYAN;
  } else if (t >= RESBASE && t < COMBASE) {
    return zone_glyph(t, 'R', COLOR_BLACK, COLOR_CYAN, 0,
		      RESBASE, RZB - 4, HOSPITAL - 1, 4, LHTHR, HHTHR);
  } else if (t >= COMBASE && t < INDBASE) {
    return zone_glyph(t, 'C', COLOR_WHITE, COLOR_BLUE, A_BOLD,
		      COMBASE, CZB - 4, INDBASE - 1, 5, -1, -1);
  } else if (t >= INDBASE && t < PORTBASE) {
    return zone_glyph(t, 'I', COLOR_YELLOW, COLOR_MAGENTA, A_BOLD,
		      INDBASE, IZB - 4, PORTBASE - 1, 4, -1, -1);
  } else if (t >= PORTBASE && t <= LASTPORT) {
    ch = 'W'; fg = COLOR_WHITE; bg = COLOR_BLUE; bold = 1;
  } else if ((t >= AIRPORTBASE && t <= AIRPORT) || (t >= RADAR0 && t <= RADAR7)) {
    ch = 'A'; fg = COLOR_BLACK; bg = COLOR_WHITE;
  } else if (t >= COALBASE && t <= LASTPOWERPLANT) {
    ch = 'E'; fg = COLOR_WHITE; bg = COLOR_RED; bold = 1;
  } else if (t >= FIRESTBASE && t <= FIRESTATION) {
    ch = 'F'; fg = COLOR_WHITE; bg = COLOR_RED; bold = 1;
  } else if (t >= POLICESTBASE && t <= POLICESTATION) {
    ch = 'P'; fg = COLOR_WHITE; bg = COLOR_RED; bold = 1;
  } else if ((t >= STADIUMBASE && t <= FULLSTADIUM) ||
	     (t >= FOOTBALLGAME1 && t <= FOOTBALLGAME2)) {
    ch = 'S'; fg = COLOR_BLACK; bg = COLOR_WHITE; bold = 1;
  } else if (t >= NUCLEARBASE && t <= NUCLEAR) {
    ch = 'N'; fg = COLOR_BLACK; bg = COLOR_GREEN; bold = 1;
  } else if ((t >= HBRDG0 && t <= HBRDG3) || (t >= VBRDG0 && t <= VBRDG3)) {
    ch = '='; fg = COLOR_WHITE; bg = COLOR_BLACK;	/* draw bridge */
  } else if (t >= FOUNTAIN && t <= INDBASE2) {
    ch = '"'; fg = COLOR_BLACK; bg = COLOR_GREEN;	/* fountain / park */
  } else if ((t >= SMOKEBASE && t <= LASTTINYEXP) ||
	     (t >= COALSMOKE1 && t <= COALSMOKE4 + 3)) {
    ch = '*'; fg = COLOR_WHITE; bg = land;
  } else {
    ch = '*'; fg = COLOR_WHITE; bg = land;
  }

  return ((chtype)ch) | NC_CP(fg, bg) | (bold ? A_BOLD : 0);
}

void
nc_colors_init(void)
{
  int fg, bg, n;

  if (!has_colors()) return;
  start_color();
  for (fg = 0; fg < 8; fg++) {
    for (bg = 0; bg < 8; bg++) {
      n = NC_PAIR(fg, bg);
      if (n < COLOR_PAIRS) init_pair(n, fg, bg);
    }
  }
}

/* Editor region on screen (computed each frame; shared with nc_minimap etc). */
int EdTop = 1, EdLeft = 0, EdW = 0, EdH = 0;
int MinimapW = 0;		/* width of the right-side minimap panel (0 = off) */
int ToolbarW = 3;		/* width of the left-side tool palette */

/* one vertical RCI demand bar row: "R[++++++]" (demand) / "C[------]" (surplus) */
static void
demand_row(int y, int x, char lbl, int valve, int color, int w)
{
  int v = valve / 100, n = (v < 0) ? -v : v, k;

  if (n > 6) n = 6;
  if (x + 9 > w) return;
  attrset(NC_CP(color, COLOR_WHITE));		/* on the white panel; non-bold reads darker */
  move(y, x);
  addch(lbl);
  addch('[');
  for (k = 0; k < 6; k++) addch(k < n ? (v >= 0 ? '+' : '-') : ' ');
  addch(']');
  attrset(A_NORMAL);
}

/* panel text color for a tool: its dominant map color, kept legible on the
 * white panel (white/black map colors fall back to the letter color). */
static int
tool_panel_color(int i)
{
  int c = nc_toolbar_bg(i);
  if (c == COLOR_WHITE || c == COLOR_BLACK) c = nc_toolbar_color(i);
  if (c == COLOR_WHITE) c = COLOR_BLACK;
  return c;
}

/* one info-block line, clipped to the panel (bot = first row below it) */
static void
info_line(int y, int x, char *s, int w, int bot, int attr)
{
  if (y >= bot) return;
  attrset(attr);
  mvaddnstr(y, x, s, w);
  attrset(A_NORMAL);
}

extern char *cityClassStr[6];		/* w_eval.c */
extern int makeDollarDecimalStr(char *numStr, char *dollarStr);

/*
 * The left panel: a 3-column tool GRID (grouped rows, short codes dense /
 * "[RE]" expanded) followed by an info block -- current tool + cost, the RCI
 * demand bars (stacked vertically), the cursor position, the sim speed, and
 * the city stats (name, date, funds, pop, score, class), one per row.
 * Sets ToolbarW so the editor shrinks to make room; selected tool in reverse.
 */
void
nc_draw_toolbar(SimView *view)
{
  static char *spd[4] = { "Paused", "Slow", "Medium", "Fast" };
  int rows, cols, top, height, expanded, cellw, gridw, contentw, lpad, rpad, tw;
  int idx, r, c, x, iy, ts, bot, mon, wcp;
  int ngr = nc_toolbar_gridrows();
  char cell[16], buf[32], num[32], dollars[32];

  getmaxyx(stdscr, rows, cols);
  top = 1;			/* below the menu bar */
  height = rows - 2;		/* menu (1) + status line (1) */
  if (height < 1) height = 1;

  expanded = (cols >= 90);
  cellw = expanded ? 5 : 3;
  gridw = 3 * cellw;
  contentw = gridw > 12 ? gridw : 12;	/* wide enough for the info labels */
  if (cols >= 108)     { lpad = 2; rpad = 2; }
  else if (cols >= 68) { lpad = 1; rpad = 1; }
  else                 { lpad = 0; rpad = 0; }
  tw = lpad + contentw + rpad;
  ToolbarW = tw;

  attrset(NC_CP(COLOR_BLACK, COLOR_WHITE));	/* white panel, matching menu/status bars */
  for (r = 0; r < height; r++) {		/* clear the whole toolbar column */
    move(top + r, 0);
    for (x = 0; x < tw; x++) addch(' ');
  }

  /* the tool grid (one blank row below the menu bar) */
  idx = 0;
  for (r = 0; r < ngr && top + 1 + r < top + height; r++) {
    int rl = nc_toolbar_rowlen(r);
    for (c = 0; c < rl; c++, idx++) {
      int st = nc_toolbar_state(idx);
      int attr = NC_CP(tool_panel_color(idx), COLOR_WHITE);	/* colored letters */

      if (view->tool_state == st) attr |= A_REVERSE | A_BOLD;	/* selected tool */
      if (Gfx->emojiui) {		/* emoji button faces (unicode mode) */
	if (expanded) sprintf(cell, "[%s]", nc_toolbar_emoji(idx));
	else          sprintf(cell, "%s",   nc_toolbar_emoji(idx));
	attrset(attr);
	mvaddstr(top + 1 + r, lpad + c * cellw, cell);
	if (!expanded)			/* no brackets to flip: mark with '<' */
	  addch(view->tool_state == st ? '<' : ' ');
	continue;
      }
      if (expanded) sprintf(cell, "[%s]", nc_toolbar_lcode(idx));
      else          sprintf(cell, "%-2s", nc_toolbar_code(idx));
      attrset(attr);
      mvaddnstr(top + 1 + r, lpad + c * cellw, cell, (int)strlen(cell));
    }
  }

  /* info block under the grid: tool + cost, RCI demand, cursor, speed,
   * then the city stats that used to be a full-width HUD line, stacked
   * vertically.  On short terminals the tail is clipped, never the grid. */
  iy = top + ngr + 2;
  if (iy + 15 >= top + height) iy = top + height - 16;
  if (iy < top + ngr + 1) iy = top + ngr + 1;
  bot = top + height;
  ts = view->tool_state;
  wcp = NC_CP(COLOR_BLACK, COLOR_WHITE);

  info_line(iy, lpad, nc_tool_name(ts), contentw, bot, wcp | A_BOLD);
  sprintf(buf, "$%d", nc_tool_cost(ts));
  info_line(iy + 1, lpad, buf, contentw, bot, wcp);

  if (iy + 3 < bot) demand_row(iy + 3, lpad, 'R', RValve, COLOR_GREEN, tw);
  if (iy + 4 < bot) demand_row(iy + 4, lpad, 'C', CValve, COLOR_BLUE, tw);
  if (iy + 5 < bot) demand_row(iy + 5, lpad, 'I', IValve, COLOR_MAGENTA, tw);

  sprintf(buf, "@(%d,%d)", CursorX, CursorY);
  info_line(iy + 7, lpad, buf, contentw, bot, wcp);
  info_line(iy + 8, lpad, spd[SimSpeed & 3], contentw, bot, wcp);

  mon = (int)((CityTime % 48) >> 2);
  if (mon < 0 || mon > 11) mon = 0;
  sprintf(num, "%ld", (long)TotalFunds);
  makeDollarDecimalStr(num, dollars);	/* adds the '$' */

  info_line(iy + 10, lpad, CityName ? CityName : "NowHere", contentw, bot,
	    wcp | A_BOLD);
  sprintf(buf, "%s %d", dateStr[mon],
	  (int)StartingYear + (int)(CityTime / 48));
  info_line(iy + 11, lpad, buf, contentw, bot, wcp);
  info_line(iy + 12, lpad, dollars, contentw, bot, wcp);
  sprintf(buf, "Pop:%ld", (long)CityPop);
  info_line(iy + 13, lpad, buf, contentw, bot, wcp);
  sprintf(buf, "Score:%d", (int)CityScore);
  info_line(iy + 14, lpad, buf, contentw, bot, wcp);
  info_line(iy + 15, lpad,
	    cityClassStr[(CityClass >= 0 && CityClass < 6) ? CityClass : 0],
	    contentw, bot, wcp);
}

/*
 * Draw the editor viewport.  Layout: row 0 = menu bar, bottom row = status
 * line, the editor fills the middle and (when a minimap panel is shown) the
 * left part of the width.  Panned so the cursor stays visible; cursor cell drawn in reverse.
 * Recomputes from getmaxyx every frame, so it adapts to terminal resizes.
 */
void
nc_draw_editor(SimView *view)
{
  int rows, cols, sy, tx, sx, mx, my, jx = 0, jy = 0;
  int tw, vt, k;

  getmaxyx(stdscr, rows, cols);
  if (ShakeNow > 0) { jx = Rand(3) - 1; jy = Rand(3) - 1; }  /* earthquake shake */
  EdTop = 1;				/* row 0 = menu bar; toolbar is a left column */
  EdLeft = ToolbarW;
  EdH = rows - 2;			/* menu (1) + status line (1) */
  EdW = cols - ToolbarW - MinimapW;
  if (EdH < 1) EdH = 1;
  if (EdW < 1) EdW = cols;

  tw = Gfx->tilew;			/* screen columns per map tile */
  vt = EdW / tw;			/* visible tiles per row */
  if (vt < 1) vt = 1;

  /* keep cursor visible: scroll the pan window to contain it */
  if (CursorX < ViewPanX) ViewPanX = CursorX;
  if (CursorX >= ViewPanX + vt) ViewPanX = CursorX - vt + 1;
  if (CursorY < ViewPanY) ViewPanY = CursorY;
  if (CursorY >= ViewPanY + EdH) ViewPanY = CursorY - EdH + 1;

  if (ViewPanX > WORLD_X - vt) ViewPanX = WORLD_X - vt;
  if (ViewPanY > WORLD_Y - EdH) ViewPanY = WORLD_Y - EdH;
  if (ViewPanX < 0) ViewPanX = 0;
  if (ViewPanY < 0) ViewPanY = 0;

  for (sy = 0; sy < EdH; sy++) {
    my = ViewPanY + sy + jy;
    for (tx = 0; tx < vt; tx++) {
      mx = ViewPanX + tx + jx;
      sx = EdLeft + tx * tw;
      if (mx >= 0 && mx < WORLD_X && my >= 0 && my < WORLD_Y) {
	Gfx->tile(EdTop + sy, sx, mx, my);
      } else {
	for (k = 0; k < tw; k++) mvaddch(EdTop + sy, sx + k, ' ');
      }
    }
    for (sx = EdLeft + vt * tw; sx < EdLeft + EdW; sx++)
      mvaddch(EdTop + sy, sx, ' ');	/* ragged right edge (EdW % tw) */
  }

  /* cursor marker (drawn by the mode: reverse cell / brackets / ...) */
  tx = CursorX - ViewPanX - jx;
  sy = CursorY - ViewPanY - jy;
  if (tx >= 0 && tx < vt && sy >= 0 && sy < EdH)
    Gfx->cursor(EdTop + sy, EdLeft + tx * tw, CursorX, CursorY);

  nc_draw_sprites();			/* moving objects over the tile layer */
}

/* Sprite glyph by type (TRA COP AIR SHI GOD TOR EXP BUS = 1..8). */
chtype
nc_sprite_glyph(int type)
{
  switch (type) {
  case TRA: return 'e' | NC_CP(COLOR_CYAN, COLOR_BLACK) | A_BOLD;	/* train  */
  case COP: return 'H' | NC_CP(COLOR_WHITE, COLOR_BLACK) | A_BOLD;	/* copter */
  case AIR: return '>' | NC_CP(COLOR_WHITE, COLOR_BLACK) | A_BOLD;	/* plane  */
  case SHI: return 'b' | NC_CP(COLOR_CYAN, COLOR_BLACK) | A_BOLD;	/* ship   */
  case GOD: return 'M' | NC_CP(COLOR_RED, COLOR_BLACK) | A_BOLD | A_REVERSE; /* monster */
  case TOR: return '@' | NC_CP(COLOR_WHITE, COLOR_BLACK) | A_BOLD | A_REVERSE; /* tornado */
  case EXP: return '*' | NC_CP(COLOR_YELLOW, COLOR_RED) | A_BOLD;	/* explosion */
  case BUS: return 'u' | NC_CP(COLOR_YELLOW, COLOR_BLACK) | A_BOLD;	/* bus */
  }
  return '?' | NC_CP(COLOR_WHITE, COLOR_BLACK);
}

/* Draw active sprites (moving objects) as glyphs over the tile layer. */
void
nc_draw_sprites(void)
{
  extern Sim *sim;
  SimSprite *s;
  int tx, ty, sx, sy, tw, vt;

  if (!sim) return;
  tw = Gfx->tilew;
  vt = EdW / tw;
  if (vt < 1) vt = 1;
  for (s = sim->sprite; s != NULL; s = s->next) {
    if (s->frame == 0) continue;		/* dead / inactive */
    tx = (s->x + s->x_hot) >> 4;		/* pixel -> tile coords */
    ty = (s->y + s->y_hot) >> 4;
    if (tx < ViewPanX || tx >= ViewPanX + vt ||
	ty < ViewPanY || ty >= ViewPanY + EdH)
      continue;
    sx = EdLeft + (tx - ViewPanX) * tw;
    sy = EdTop + (ty - ViewPanY);
    Gfx->sprite(sy, sx, s->type);
  }
}

/* Engine callback: an editor view needs redrawing.  In the ncurses port the
 * whole frame is redrawn from the main loop, so this just flags the view. */
void
DoUpdateEditor(SimView *view)
{
  if (view) view->invalid = 1;
}

/*
 * Dump the current stdscr contents to a plain-ASCII file (for headless
 * testing under a pty).  ACS line-drawing glyphs are translated back to
 * ASCII approximations so the result is readable in a normal text file.
 */
void
nc_screenshot(char *path)
{
  FILE *f, *cf;
  int rows, cols, y, x, ch, pair, bg;
  chtype c;
  char cpath[512];
  static char digit[9] = "krgybmcw ";	/* curses bg color -> letter */

  f = fopen(path, "w");
  if (!f) return;
  sprintf(cpath, "%s.col", path);	/* companion background-color grid */
  cf = fopen(cpath, "w");
  getmaxyx(stdscr, rows, cols);
  for (y = 0; y < rows; y++) {
    for (x = 0; x < cols; x++) {
      c = mvinch(y, x);
      ch = (int)(c & A_CHARTEXT);
      if (c & A_ALTCHARSET) {
	switch (ch) {
	case 'q': ch = '-'; break;
	case 'x': ch = '|'; break;
	case 'n': case 'l': case 'm': case 'k': case 'j':
	case 't': case 'u': case 'w': case 'v': ch = '+'; break;
	default: ch = '#'; break;
	}
      }
      if (ch < 32 || ch > 126) ch = '?';
      fputc(ch, f);
      if (cf) {
	pair = (int)(PAIR_NUMBER(c));
	bg = pair ? (pair - 1) % 8 : 8;
	fputc(digit[bg], cf);
      }
    }
    fputc('\n', f);
    if (cf) fputc('\n', cf);
  }
  fclose(f);
  if (cf) fclose(cf);
}
