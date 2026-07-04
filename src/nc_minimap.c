/* nc_minimap.c  --  the overview map panel + data overlays.
 *
 * Reuses the engine's overlay classification (g_map.c: GetCI thresholds, the
 * VAL_* -> color ramp, and the reduced-resolution arrays PopDensity/CrimeMem/
 * PollutionMem/LandValueMem/TrfDensity/RateOGMem/PowerMap), rendered as a
 * compressed grid of colored blocks instead of a pixmap.
 *
 * The panel appears on the right when the terminal is wide enough; on a narrow
 * terminal it is drawn as a centered full overlay.  Cycled with the 'm' key:
 * off -> All -> PopDensity -> Growth -> Traffic -> Pollution -> Crime ->
 * LandValue -> Power -> Fire -> Police -> off.
 */

#include "sim.h"
#include <curses.h>
#include "nc.h"

/* VAL_* levels (from g_map.c) */
#define VAL_NONE 0
#define VAL_LOW 1
#define VAL_MEDIUM 2
#define VAL_HIGH 3
#define VAL_VERYHIGH 4
#define VAL_PLUS 5
#define VAL_VERYPLUS 6
#define VAL_MINUS 7
#define VAL_VERYMINUS 8

/* VAL_* -> curses color (bg fill); -1 means "transparent, show base tile" */
static int val_color[9] = {
  -1, COLOR_WHITE, COLOR_YELLOW, COLOR_RED, COLOR_RED,
  COLOR_GREEN, COLOR_GREEN, COLOR_RED, COLOR_YELLOW
};
static int val_bold[9] = { 0, 0, 0, 0, 1, 0, 1, 0, 1 };

static short
get_ci(short x)
{
  if (x < 50)  return VAL_NONE;
  if (x < 100) return VAL_LOW;
  if (x < 150) return VAL_MEDIUM;
  if (x < 200) return VAL_HIGH;
  return VAL_VERYHIGH;
}

/* the overlay modes we cycle through (engine map_state constants) */
static struct { int state; char *label; } modes[] = {
  { ALMAP, "All" },
  { PDMAP, "Population" },
  { RGMAP, "Growth Rate" },
  { TDMAP, "Traffic" },
  { PLMAP, "Pollution" },
  { CRMAP, "Crime" },
  { LVMAP, "Land Value" },
  { PRMAP, "Power Grid" },
  { FIMAP, "Fire Cover" },
  { POMAP, "Police Cover" }
};
#define NMODES ((int)(sizeof(modes) / sizeof(modes[0])))

static int MinimapMode = -1;		/* -1 = off, else index into modes[] */

int
nc_minimap_on(void)
{
  return MinimapMode >= 0;
}

/* 'm' key: advance off -> mode0 -> ... -> last -> off */
void
nc_minimap_cycle(void)
{
  MinimapMode++;
  if (MinimapMode >= NMODES) MinimapMode = -1;
}

/* base tile color for the overview (curses bg color) */
static int
base_color(int tx, int ty)
{
  int t = Map[tx][ty] & LOMASK;

  if (t == DIRT) return COLOR_BLACK;
  if (t >= RIVER && t <= LASTRIVEDGE) return COLOR_BLUE;
  if (t >= TREEBASE && t <= WOODS5) return COLOR_GREEN;
  if (t >= ROADBASE && t <= LASTROAD) return COLOR_WHITE;
  if (t >= POWERBASE && t <= LASTPOWER) return COLOR_YELLOW;
  if (t >= RAILBASE && t <= LASTRAIL) return COLOR_WHITE;
  if (t >= RESBASE && t < COMBASE) return COLOR_GREEN;
  if (t >= COMBASE && t < INDBASE) return COLOR_BLUE;
  if (t >= INDBASE && t < PORTBASE) return COLOR_YELLOW;
  if (t >= PORTBASE && t <= LASTPOWERPLANT) return COLOR_CYAN;
  if (t >= FIRESTBASE && t <= POLICESTATION) return COLOR_RED;
  if (t >= FIREBASE && t <= LASTFIRE) return COLOR_RED;
  return COLOR_BLACK;
}

/* overlay color for a tile; returns curses color + sets *bold, or -1 = base */
static int
overlay_color(int state, int tx, int ty, int *bold)
{
  int hx = tx >> 1, hy = ty >> 1;	/* half-res arrays */
  int sx = tx >> 3, sy = ty >> 3;	/* eighth-res arrays */
  int val = VAL_NONE, z;

  *bold = 0;
  switch (state) {
  case PDMAP: val = get_ci(PopDensity[hx][hy]); break;
  case TDMAP: val = get_ci(TrfDensity[hx][hy]); break;
  case PLMAP: val = get_ci(10 + PollutionMem[hx][hy]); break;
  case CRMAP: val = get_ci(CrimeMem[hx][hy]); break;
  case LVMAP: val = get_ci(LandValueMem[hx][hy]); break;
  case FIMAP: val = get_ci(FireRate[sx][sy]); break;
  case POMAP: val = get_ci(PoliceMapEffect[sx][sy]); break;
  case RGMAP:
    z = RateOGMem[sx][sy];
    if (z > 100) val = VAL_VERYPLUS;
    else if (z > 20) val = VAL_PLUS;
    else if (z < -100) val = VAL_VERYMINUS;
    else if (z < -20) val = VAL_MINUS;
    else val = VAL_NONE;
    break;
  case PRMAP: {
    int t = Map[tx][ty];
    if (!(t & ZONEBIT) && !(t & CONDBIT)) return -1;
    if (t & PWRBIT) { *bold = 1; return COLOR_RED; }		/* powered */
    if (t & CONDBIT) return COLOR_WHITE;			/* conductive */
    *bold = 1; return COLOR_CYAN;				/* unpowered */
  }
  default: return -1;			/* ALMAP: base only */
  }
  *bold = val_bold[val];
  return val_color[val];
}

/*
 * Draw the overview into a screen rectangle, compressing 120x100 tiles into
 * (w-2) x (h-2) cells (leaving a border).  Each cell samples the tile at its
 * center; the active overlay recolors it.
 */
static void
draw_grid(int top, int left, int w, int h)
{
  int iw = w - 2, ih = h - 2;
  int sx, sy, tx, ty, color, bold, i;
  int state = (MinimapMode >= 0) ? modes[MinimapMode].state : ALMAP;
  char title[64];

  if (iw < 4 || ih < 3) return;

  /* border + title bar */
  attrset(NC_CP(COLOR_WHITE, COLOR_BLACK) | A_BOLD);
  for (i = 0; i < w; i++) { mvaddch(top, left + i, ' '); mvaddch(top + h - 1, left + i, ' '); }
  sprintf(title, " Map: %s ", modes[MinimapMode].label);
  mvaddnstr(top, left + 1, title, w - 2);

  for (sy = 0; sy < ih; sy++) {
    ty = sy * WORLD_Y / ih;
    if (ty >= WORLD_Y) ty = WORLD_Y - 1;
    for (sx = 0; sx < iw; sx++) {
      tx = sx * WORLD_X / iw;
      if (tx >= WORLD_X) tx = WORLD_X - 1;

      color = overlay_color(state, tx, ty, &bold);
      if (color < 0) { color = base_color(tx, ty); bold = 0; }

      attrset(NC_CP(COLOR_BLACK, color) | (bold ? A_BOLD : 0));
      mvaddch(top + 1 + sy, left + 1 + sx, ' ');
    }
  }

  /* mark the editor's current viewport with a box corner cue */
  attrset(A_NORMAL);
}

/*
 * Public entry: lay out and draw the minimap if active.  Sets MinimapW so the
 * editor viewport shrinks to make room (side panel) when the terminal is wide;
 * otherwise draws a centered overlay and leaves the editor full width.
 */
void
nc_draw_minimap(void)
{
  int rows, cols, pw, ph, top, left;

  MinimapW = 0;
  if (MinimapMode < 0) return;

  getmaxyx(stdscr, rows, cols);

  if (cols >= 70) {
    /* side panel on the right */
    pw = cols / 3;
    if (pw > 44) pw = 44;
    if (pw < 22) pw = 22;
    MinimapW = pw;
    ph = rows - 2;			/* menu (1) + status line (1) */
    top = 1;
    left = cols - pw;
    draw_grid(top, left, pw, ph);
  } else {
    /* narrow terminal: centered overlay */
    pw = cols - 2;
    ph = rows - 5;
    if (ph < 5) ph = 5;
    top = 1;
    left = 1;
    draw_grid(top, left, pw, ph);
  }
}
