/* nc_aa.c  --  the "aa" graphics mode: aalib rendering, in color.
 *
 * Renders tiles through the real aalib: every map tile is drawn as a tiny
 * grayscale picture (4x2 virtual pixels, aalib's native 2x2 per character
 * cell) into an aalib memory-driver context, aa_render() picks the ASCII
 * glyphs, and the result is blitted to curses.  Unlike stock aalib the
 * picture is in color: each tile class supplies (fg,bg) from the palette
 * the other modes already use, through the existing NC_CP pairs (never
 * remapped); aalib's brightness attribute maps to A_BOLD.
 *
 * libaa is a rare bird, so the mode is OFF by default: build it in with
 * `make aalib` (see Makefile).  Without HAVE_AALIB this file compiles to a
 * stub whose avail() always fails -- the mode stays in the registry, but
 * `-gfx aa` is refused and the 'u' cycle skips it, and the binary keeps
 * linking against curses alone.
 */

#include "sim.h"
#include <curses.h>
#include "nc.h"

#ifdef HAVE_AALIB

#include <aalib.h>
#include <locale.h>

/* ----------------------- the aalib context ------------------------------- */

/* One persistent tile-sized memory-driver scratch context: every tile is
 * painted at the origin and rendered one at a time.  (aalib only renders
 * regions anchored at the origin -- non-zero x1/y1 silently render nothing
 * in 1.4 -- and a 2x1-character context sidesteps terminal resizes too.) */
static aa_context *Ctx = NULL;
static struct aa_renderparams Rp;

static aa_context *
aa_ctx(void)
{
  static unsigned char prime[8] = { 0, 255, 0, 255, 255, 0, 255, 0 };
  struct aa_hardware_params hp;
  unsigned char *img;
  char save[64];
  char *loc;
  int iw, r, k;

  if (Ctx) return Ctx;
  hp = aa_defparams;
  hp.width = 4;
  hp.height = 2;
  hp.supported = AA_NORMAL_MASK | AA_BOLD_MASK;	/* color carries the rest */
  Rp = aa_defrenderparams;
  Rp.dither = AA_NONE;	/* the art is flat 5-level ink; dithering just adds
			 * noise at tile scale */
  /* aalib builds its glyph tables with locale-dependent isprint(): under a
   * UTF-8 locale it admits bytes above 127 (IBM-PC shade blocks) that no
   * terminal can print raw.  Create the context and prime the tables (built
   * lazily on the first render) under the C ctype locale, then restore. */
  save[0] = '\0';
  loc = setlocale(LC_CTYPE, NULL);
  if (loc) {
    strncpy(save, loc, sizeof(save) - 1);
    save[sizeof(save) - 1] = '\0';
  }
  setlocale(LC_CTYPE, "C");
  Ctx = aa_init(&mem_d, &hp, NULL);
  if (Ctx) {
    img = (unsigned char *)aa_image(Ctx);
    iw = aa_imgwidth(Ctx);
    for (r = 0; r < 2; r++)
      for (k = 0; k < 4; k++)
	img[r * iw + k] = prime[r * 4 + k];
    aa_render(Ctx, &Rp, 0, 0, 2, 1);
  }
  if (save[0]) setlocale(LC_CTYPE, save);
  return Ctx;
}

static int
aa_avail(void)
{
  return aa_ctx() != NULL;
}

/* Render the 4x2-pixel canvas as two characters at (sy,sx): paint it into
 * the aalib framebuffer, aa_render one tile's worth of cells, blit the
 * text+attrs out with the tile class color on top. */
static void
aa_put(int sy, int sx, unsigned char px[2][4], int fg, int bg, int attr)
{
  static unsigned char lum[5] = { 0, 64, 128, 192, 255 };
  aa_context *c = aa_ctx();
  unsigned char *img, *txt, *atr;
  int iw, r, k;
  chtype ch;

  if (!c) return;
  img = (unsigned char *)aa_image(c);
  iw = aa_imgwidth(c);
  for (r = 0; r < 2; r++)
    for (k = 0; k < 4; k++)
      img[r * iw + k] = lum[px[r][k]];
  aa_render(c, &Rp, 0, 0, 2, 1);
  txt = (unsigned char *)aa_text(c);
  atr = (unsigned char *)aa_attrs(c);
  for (k = 0; k < 2; k++) {
    ch = (chtype)txt[k];
    if (ch < 32 || ch > 126) ch = ' ';		/* stay 7-bit printable */
    if (atr[k] == AA_BOLD) ch |= A_BOLD;
    mvaddch(sy, sx + k, ch | NC_CP(fg, bg) | attr);
  }
}

/* ---- the 4x2 pixel canvas one tile is drawn on (ink levels 0..4) ---- */

static void
aa_clear(unsigned char px[2][4], int v)
{
  int r, c;

  for (r = 0; r < 2; r++)
    for (c = 0; c < 4; c++)
      px[r][c] = (unsigned char)v;
}

static void
aa_set(unsigned char px[2][4], int r, int c, int v)
{
  if (v < 0) v = 0;
  if (v > 4) v = 4;
  px[r][c] = (unsigned char)v;
}

static void
aa_hline(unsigned char px[2][4], int row, int c0, int c1, int v)
{
  int c;

  for (c = c0; c <= c1; c++) aa_set(px, row, c, v);
}

static void
aa_vline(unsigned char px[2][4], int col, int r0, int r1, int v)
{
  int r;

  for (r = r0; r <= r1; r++) aa_set(px, r, col, v);
}

static void
aa_checker(unsigned char px[2][4], int hi, int lo, int phase)
{
  int r, c;

  for (r = 0; r < 2; r++)
    for (c = 0; c < 4; c++)
      px[r][c] = (unsigned char)(((r + c + phase) & 1) ? lo : hi);
}

/* ------------------------------ tile art --------------------------------- */

/* Paint Map[mx][my] onto the canvas and flush it; xattr lets the cursor
 * re-render the tile in reverse video.  Same decode and branch order as
 * uni_tile (nc_gfx.c); the art maps each class to pixels + a color. */
static void
aa_tile_at(int sy, int sx, int mx, int my, int xattr)
{
  int raw = Map[mx][my];
  int blink = (flagBlink <= 0);
  int land = ThemeLand;
  int t, cls, center, h, den, vac, m, r0, r1;
  int fg, bg, attr;
  unsigned char px[2][4];

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
  fg = COLOR_WHITE; bg = land; attr = 0;
  aa_clear(px, 0);
  cls = nc_transit_class(t);

  if (t == LIGHTNINGBOLT) {		/* unpowered zone blink: a bolt */
    aa_set(px, 0, 1, 2); aa_set(px, 0, 2, 2);
    aa_set(px, 1, 0, 2); aa_set(px, 1, 3, 2);
    fg = COLOR_YELLOW; attr = A_BOLD;

  /* ---- transit: strokes drawn to the connecting edges ---- */
  } else if (cls == 1) {		/* road: ground-level stroke, row 1 */
    m = nc_transit_mask(mx, my, 1);
    fg = COLOR_WHITE; bg = COLOR_BLACK;
    if (m == 0) aa_hline(px, 1, 1, 2, 3);		/* isolated stub */
    if (m & 3) aa_hline(px, 1, (m & 2) ? 0 : 1, (m & 1) ? 3 : 1, 3);
    r0 = (m & 8) ? 0 : 1;
    r1 = (m & 4) ? 1 : 0;
    if (m & 12) aa_vline(px, 1, r0, r1, 3);
    if (t >= HTRFBASE) {				/* heavy traffic jam */
      aa_set(px, 0, h & 3, 4); aa_set(px, 0, (h >> 3) & 3, 3);
      fg = COLOR_YELLOW; attr = A_BOLD;
    } else if (t >= LTRFBASE) {				/* the odd car */
      aa_set(px, 0, (h >> 2) & 3, 2);
    }
  } else if (cls == 2) {		/* rail: double stroke on ballast */
    m = nc_transit_mask(mx, my, 2);
    fg = COLOR_BLACK; bg = COLOR_WHITE;
    if (m == 0) { aa_hline(px, 0, 1, 2, 2); aa_hline(px, 1, 1, 2, 2); }
    if (m & 3) {
      aa_hline(px, 0, (m & 2) ? 0 : 1, (m & 1) ? 3 : 1, 2);
      aa_hline(px, 1, (m & 2) ? 0 : 1, (m & 1) ? 3 : 1, 2);
    }
    r0 = (m & 8) ? 0 : 1;
    r1 = (m & 4) ? 1 : 0;
    if (m & 12) { aa_vline(px, 1, r0, r1, 3); aa_vline(px, 2, r0, r1, 3); }
  } else if (cls == 3) {		/* power line: overhead wire, row 0 */
    m = nc_transit_mask(mx, my, 3);
    fg = COLOR_RED; attr = A_BOLD;
    if (m == 0) aa_hline(px, 0, 1, 2, 2);
    if (m & 3) aa_hline(px, 0, (m & 2) ? 0 : 1, (m & 1) ? 3 : 1, 2);
    r0 = (m & 8) ? 0 : 1;
    r1 = (m & 4) ? 1 : 0;
    if (m & 12) aa_vline(px, 1, r0, r1, 2);
    if ((m & 3) && (h & 3) == 0) aa_set(px, 1, 1, 2);	/* a pole */

  /* ---- terrain ---- */
  } else if (t == DIRT) {
    fg = COLOR_BLACK;
    aa_set(px, (h >> 2) & 1, h & 3, 1);			/* a grain of dirt */
  } else if (t >= RIVER && t <= LASTRIVEDGE) {
    fg = COLOR_CYAN; bg = COLOR_BLUE;
    if ((h % 37) == 0) {				/* a sail on the water */
      aa_set(px, 0, 1, 4);
      aa_hline(px, 1, 0, 3, 2); aa_set(px, 1, 1, 4);
      fg = COLOR_WHITE;
    } else if ((h & 3) == 0) {
      aa_hline(px, 1, 0, 3, 2);
    } else if ((h & 3) == 1) {
      aa_hline(px, 0, 0, 3, 2);
    } else if ((h & 3) == 2) {
      aa_hline(px, 0, 0, 1, 2); aa_hline(px, 1, 2, 3, 2);
    } else {
      aa_hline(px, 1, 0, 1, 2); aa_hline(px, 0, 2, 3, 2);
    }
  } else if (t >= TREEBASE && t < WOODS2) {
    fg = COLOR_BLACK; bg = COLOR_GREEN;
    aa_checker(px, 3, 1, h & 1);			/* canopy texture */
    aa_set(px, 0, (h >> 1) & 3, 4);
  } else if (t >= WOODS2 && t <= WOODS5) {		/* user-placed park */
    fg = COLOR_BLACK; bg = COLOR_GREEN;
    aa_checker(px, 2, 0, h & 1);
  } else if (t >= RUBBLE && t <= LASTRUBBLE) {
    fg = COLOR_BLACK;
    aa_set(px, 0, h & 3, 3);				/* strewn debris */
    aa_set(px, 1, (h >> 2) & 3, 2);
    aa_set(px, (h >> 4) & 1, (h >> 5) & 3, 3);
  } else if (t >= FLOOD && t <= LASTFLOOD) {
    fg = COLOR_WHITE; bg = COLOR_CYAN;
    aa_hline(px, h & 1, 0, 3, 3);
    aa_set(px, (~h) & 1, (h >> 1) & 3, 2);
  } else if (t == RADTILE) {
    fg = COLOR_WHITE; bg = COLOR_MAGENTA; attr = A_BOLD;
    aa_set(px, 0, 0, 3); aa_set(px, 1, 1, 3);		/* crossed-out ground */
    aa_set(px, 0, 3, 3); aa_set(px, 1, 2, 3);
  } else if (t >= FIREBASE && t <= LASTFIRE) {
    fg = COLOR_YELLOW; bg = COLOR_RED; attr = A_BOLD;
    aa_hline(px, 1, 0, 3, 4);				/* dancing flames */
    aa_set(px, 0, t & 3, 4);
    aa_set(px, 0, (h >> 2) & 3, 3);

  /* ---- city services (3x3 lots; the center tile is the landmark) ---- */
  } else if (t >= HOSPITAL - 4 && t <= HOSPITAL + 4) {
    fg = COLOR_WHITE; bg = COLOR_RED;
    if (t == HOSPITAL) aa_clear(px, 4);
    else aa_checker(px, 2, 1, (mx ^ my) & 1);
  } else if (t >= CHURCH - 4 && t <= CHURCH + 4) {
    fg = COLOR_BLACK; bg = COLOR_CYAN;
    if (t == CHURCH) {					/* gable and spire */
      aa_set(px, 0, 1, 4); aa_set(px, 0, 2, 3);
      aa_hline(px, 1, 0, 3, 3);
    } else {
      aa_checker(px, 2, 1, (mx ^ my) & 1);
    }

  /* ---- R/C/I zones: window texture ramps with density ---- */
  } else if (t >= RESBASE && t < COMBASE) {
    fg = COLOR_BLACK; bg = COLOR_CYAN;
    if (t >= LHTHR && t <= HHTHR) {			/* single family house */
      aa_set(px, 0, 0, 1); aa_set(px, 0, 1, 3);		/* two cottages */
      aa_set(px, 0, 2, 3); aa_set(px, 0, 3, 1);
      aa_hline(px, 1, 0, 3, 3);
    } else {
      den = nc_zone_den(t, RZB - 4, HOSPITAL - 5, 4, &vac);
      if (den >= 0) {
	if (center)   aa_clear(px, 2 + den > 4 ? 4 : 2 + den);
	else if (vac) { aa_set(px, 1, h & 3, 1); aa_set(px, 0, (h >> 2) & 3, 1); }
	else          aa_checker(px, 1 + den, den / 2, h & 1);
      } else if (t <= RESBASE + 8) {			/* empty designated lot */
	if (center) { aa_hline(px, 0, 1, 2, 2); aa_hline(px, 1, 1, 2, 2); }
	else        aa_hline(px, 1, 0, 1, 1);
      } else {
	aa_clear(px, 1);				/* stray tile */
      }
    }
  } else if (t >= COMBASE && t < INDBASE) {
    fg = COLOR_WHITE; bg = COLOR_BLUE;
    den = nc_zone_den(t, CZB - 4, INDBASE - 1, 5, &vac);
    if (den >= 0) {
      if (center)   aa_clear(px, 2 + den > 4 ? 4 : 2 + den);
      else if (vac) { aa_set(px, 1, h & 3, 1); aa_set(px, 0, (h >> 2) & 3, 1); }
      else          aa_checker(px, 1 + den > 4 ? 4 : 1 + den, den / 2, h & 1);
    } else if (t <= COMBASE + 8) {
      if (center) { aa_hline(px, 0, 1, 2, 2); aa_hline(px, 1, 1, 2, 2); }
      else        aa_hline(px, 1, 0, 1, 1);
    } else {
      aa_clear(px, 1);
    }
  } else if (t >= INDBASE && t < PORTBASE) {
    fg = COLOR_YELLOW; bg = COLOR_MAGENTA;
    den = nc_zone_den(t, IZB - 4, PORTBASE - 1, 4, &vac);
    if (den >= 0) {
      if (center)   aa_clear(px, 2 + den > 4 ? 4 : 2 + den);
      else if (vac) { aa_set(px, 1, h & 3, 1); aa_set(px, 0, (h >> 2) & 3, 1); }
      else          aa_checker(px, 1 + den, den / 2, h & 1);
    } else if (t <= INDBASE + 8) {
      if (center) { aa_hline(px, 0, 1, 2, 2); aa_hline(px, 1, 1, 2, 2); }
      else        aa_hline(px, 1, 0, 1, 1);
    } else {
      aa_clear(px, 1);
    }

  /* ---- big buildings (center tile carries ZONEBIT) ---- */
  } else if (t >= PORTBASE && t <= LASTPORT) {		/* seaport */
    fg = COLOR_WHITE; bg = COLOR_BLUE;
    if (center) aa_clear(px, 4);
    else aa_checker(px, 3, 2, (mx + my) & 1);		/* container stacks */
  } else if (t >= RADAR0 && t <= RADAR7) {		/* radar sweep (anim) */
    fg = COLOR_BLACK; bg = COLOR_WHITE;
    m = (t - RADAR0) & 3;
    if (m == 0)      { aa_vline(px, 1, 0, 1, 4); }
    else if (m == 1) { aa_set(px, 0, 2, 4); aa_set(px, 1, 1, 4); }
    else if (m == 2) { aa_hline(px, 0, 1, 2, 4); }
    else             { aa_set(px, 0, 1, 4); aa_set(px, 1, 2, 4); }
  } else if (t >= AIRPORTBASE && t < COALBASE) {	/* airport */
    fg = COLOR_BLACK; bg = COLOR_WHITE;
    if (center) aa_clear(px, 4);
    else aa_hline(px, 1, 0, 3, 1);			/* tarmac */
  } else if (t >= COALBASE && t <= LASTPOWERPLANT) {	/* coal power */
    fg = COLOR_WHITE; bg = COLOR_RED;
    if (center) aa_clear(px, 4);
    else { aa_checker(px, 2, 1, (mx ^ my) & 1); aa_set(px, 0, 1, 4); }
  } else if (t >= FIRESTBASE && t < POLICESTBASE) {	/* fire station */
    fg = COLOR_WHITE; bg = COLOR_RED;
    if (center) aa_clear(px, 4);
    else aa_checker(px, 2, 1, (mx ^ my) & 1);
  } else if (t >= POLICESTBASE && t < STADIUMBASE) {	/* police station */
    fg = COLOR_WHITE; bg = COLOR_BLUE;
    if (center) aa_clear(px, 4);
    else aa_checker(px, 2, 1, (mx ^ my) & 1);
  } else if (t >= FOOTBALLGAME1 && t <= FOOTBALLGAME2) { /* game day (anim) */
    fg = COLOR_WHITE; bg = COLOR_GREEN;
    aa_checker(px, 3, 1, h & 1);			/* the crowd */
  } else if (t >= STADIUMBASE && t < NUCLEARBASE) {	/* stadium */
    fg = COLOR_BLACK; bg = COLOR_WHITE;
    if (center) { aa_hline(px, 0, 0, 3, 4); aa_hline(px, 1, 0, 3, 2); }
    else aa_checker(px, 3, 1, (mx ^ my) & 1);
  } else if (t >= NUCLEARBASE && t <= LASTZONE) {	/* nuclear power */
    fg = COLOR_WHITE; bg = COLOR_GREEN;
    if (center) aa_clear(px, 4);
    else aa_checker(px, 2, 1, (mx ^ my) & 1);

  /* ---- animation / oddball tiles ---- */
  } else if ((t >= HBRDG0 && t <= HBRDG3) || (t >= VBRDG0 && t <= VBRDG3)) {
    fg = COLOR_WHITE; bg = COLOR_BLUE;			/* open drawbridge */
    aa_hline(px, 0, 0, 3, 4);
  } else if (t >= FOUNTAIN && t <= INDBASE2) {
    fg = COLOR_BLACK; bg = COLOR_GREEN;			/* fountain / park */
    aa_set(px, 0, 1, 2); aa_set(px, 1, 0, 1); aa_set(px, 1, 3, 1);
  } else if (t >= TINYEXP && t <= LASTTINYEXP) {
    fg = COLOR_YELLOW; bg = COLOR_RED; attr = A_BOLD;
    aa_checker(px, 4, 2, h & 1);			/* explosion */
  } else if ((t >= SMOKEBASE && t < TINYEXP) ||
	     (t >= COALSMOKE1 && t <= COALSMOKE4 + 3)) {
    aa_set(px, 0, h & 3, 2);				/* drifting smoke */
    aa_set(px, 0, (h >> 2) & 3, 1);
  } else {
    aa_clear(px, 2);					/* unknown tile */
  }

  aa_put(sy, sx, px, fg, bg, attr | xattr);
}

static void
aa_tile(int sy, int sx, int mx, int my)
{
  aa_tile_at(sy, sx, mx, my, 0);
}

/* moving objects: little pictures pushed through the same renderer */
static void
aa_sprite(int sy, int sx, int type)
{
  int fg = COLOR_WHITE, bg = COLOR_BLACK, attr = A_BOLD;
  unsigned char px[2][4];

  aa_clear(px, 0);
  switch (type) {
  case TRA:					/* train: low engine block */
    aa_hline(px, 0, 0, 3, 3); aa_hline(px, 1, 0, 3, 4);
    fg = COLOR_CYAN;
    break;
  case COP:					/* helicopter: rotor + body */
    aa_hline(px, 0, 0, 3, 4);
    aa_set(px, 1, 1, 3); aa_set(px, 1, 2, 3);
    break;
  case AIR:					/* airplane: nose + wings */
    aa_set(px, 0, 1, 4); aa_set(px, 0, 2, 4);
    aa_hline(px, 1, 0, 3, 4);
    break;
  case SHI:					/* ship: mast + hull */
    aa_set(px, 0, 1, 4); aa_set(px, 0, 2, 3);
    aa_hline(px, 1, 0, 3, 4);
    fg = COLOR_CYAN;
    break;
  case GOD:					/* monster */
    aa_clear(px, 4);
    fg = COLOR_RED; attr = A_BOLD | A_REVERSE;
    break;
  case TOR:					/* tornado */
    aa_clear(px, 4);
    attr = A_BOLD | A_REVERSE;
    break;
  case EXP:					/* explosion */
    aa_checker(px, 4, 2, 0);
    fg = COLOR_YELLOW; bg = COLOR_RED;
    break;
  case BUS:
    aa_hline(px, 0, 0, 3, 3);
    aa_set(px, 1, 0, 4); aa_set(px, 1, 3, 4);
    fg = COLOR_YELLOW;
    break;
  default:
    aa_clear(px, 2);
    break;
  }
  aa_put(sy, sx, px, fg, bg, attr);
}

/* the glyphs are plain ASCII, so reverse video works on every terminal */
static void
aa_cursor(int sy, int sx, int mx, int my)
{
  aa_tile_at(sy, sx, mx, my, A_REVERSE);
}

#else /* !HAVE_AALIB */

/* Stub build: the mode stays registered but is never available, so the
 * 'u' cycle skips it and `-gfx aa` is refused with the usual message. */

static int
aa_avail(void)
{
  return 0;
}

static void
aa_tile(int sy, int sx, int mx, int my)
{
}

static void
aa_sprite(int sy, int sx, int type)
{
}

static void
aa_cursor(int sy, int sx, int mx, int my)
{
}

#endif /* HAVE_AALIB */

struct GfxOps GfxAA =
  { "aa", 2, 0, 0, aa_avail, aa_tile, aa_sprite, aa_cursor };
