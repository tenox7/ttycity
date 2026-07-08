/* nc.h  --  shared declarations for the ncurses Micropolis front end.
 *
 * Included by the nc_*.c files after <curses.h>.  Only the terminal UI uses
 * this; the simulation engine never sees it.
 */

#ifndef NC_H
#define NC_H

/* --- color pairs ---------------------------------------------------------
 * We allocate one curses pair per (fg,bg) combination: pair = fg*8 + bg + 1,
 * giving 1..64 (SysV curses guarantees at least 64 pairs).  A_BOLD supplies
 * the 8 "bright" tones on top of the 8 base colors.
 *
 * NC_MONO is true when no color must be emitted: a mono gfx mode (Gfx->mono,
 * e.g. "ascii") or a terminal without color support.  NC_CP then yields
 * pair 0 -- the terminal's own default colors -- so nothing on the screen is
 * ever colored or remapped; bold and reverse video still apply.  NC_MSEL(x)
 * is for selection/highlight bars: the colored attribute normally, plain
 * reverse video when there is no color to highlight with.
 */
#define NC_PAIR(fg, bg) (((fg) * 8 + (bg)) + 1)
#define NC_MONO         (Gfx->mono || !has_colors())
#define NC_CP(fg, bg)   (NC_MONO ? 0 : COLOR_PAIR(NC_PAIR((fg), (bg))))
#define NC_MSEL(colored) (NC_MONO ? A_REVERSE : (colored))

/* --- shared editor/view state (owned by nc_main.c) ----------------------- */
extern SimView *EditorView;		/* the single editor view */
extern int CursorX, CursorY;		/* cursor position in tile coords */
extern int ViewPanX, ViewPanY;		/* top-left visible tile */
extern int Quitting;

/* --- nc_gfx.c: pluggable tile-graphics modes ------------------------------
 * Each mode renders one map tile as `tilew` screen columns.  The editor loop
 * (nc_render.c) only does layout/panning and dispatches through Gfx; adding a
 * mode (braille, aalib shading, 7-bit ASCII, B&W...) = one new GfxOps entry.
 */
struct GfxOps {
  char *name;					/* "standard", "unicode", ... */
  int tilew;					/* screen columns per map tile */
  int emojiui;					/* emoji faces on the tool palette */
  int mono;					/* emit no color anywhere (see NC_MONO) */
  int (*avail)(void);				/* runtime check (NULL = always) */
  void (*tile)(int sy, int sx, int mapx, int mapy);
  void (*sprite)(int sy, int sx, int type);
  void (*cursor)(int sy, int sx, int mapx, int mapy);
};
extern struct GfxOps *Gfx;			/* current mode */
extern struct GfxOps GfxAA;			/* nc_aa.c: aalib-style shading */
int   nc_gfx_set(char *name);			/* -gfx <name>; 0 = unknown/unavail */
void  nc_gfx_auto(void);			/* no -gfx: pick from TERM/locale */
char *nc_gfx_cycle(void);			/* 'u' key / Options menu */
void  nc_gfx_list(FILE *f);			/* -gfx help/list, or bad -gfx arg */
void  nc_popup_snap(int *x, int *w);		/* align popup to the tile grid */
int   nc_zone_den(int t, int builtBase, int builtHi, int dmod, int *vac);

/* --- nc_render.c --------------------------------------------------------- */
extern int EdTop, EdLeft, EdW, EdH;		/* editor region (screen coords) */
extern int MinimapW;				/* right minimap panel width (0=off) */
extern int ToolbarW;				/* left tool-palette width */
extern int ThemeLand;				/* land background color */
char *nc_cycle_theme(void);			/* Options menu: cycle land color */
void  nc_set_theme(char *name);			/* -theme tan|grass|dark */
int  nc_toolbar_hit(int y, int x);		/* click -> tool state, or -1 */
void nc_colors_init(void);
int  nc_transit_class(int t);			/* 0 none, 1 road, 2 rail, 3 wire */
int  nc_transit_mask(int x, int y, int cls);	/* up/dn/lf/rt neighbor bits */
chtype nc_line_glyph(int x, int y, int cls);	/* ACS glyph for road/rail/wire */
chtype nc_sprite_glyph(int type);		/* default-mode sprite chtype */
chtype nc_cell(int mapx, int mapy);		/* decode Map[x][y] -> glyph */
void nc_draw_editor(SimView *view);		/* render viewport to stdscr */
void nc_draw_toolbar(SimView *view);		/* vertical left tool palette */
void nc_screenshot(char *path);			/* dump stdscr as ASCII (testing) */

/* --- nc_dialogs.c -------------------------------------------------------- */
void nc_budget_modal(void);			/* engine callback (w_budget.c) */
void nc_eval_modal(void);
void nc_graph_modal(void);
void nc_newgame_modal(void);
void nc_load_modal(void);			/* load .cty from disk (browser) */
void nc_load_embedded_modal(void);		/* load a baked-in example city */
void nc_save_modal(int saveas);
int  nc_prompt(char *title, char *buf, int buflen);

/* --- nc_menu.c ----------------------------------------------------------- */
int  nc_menu_active(void);
void nc_menu_enter(void);
int  nc_menu_key(int ch);			/* returns 1 if consumed */
int  nc_menu_mouse(int y, int x);		/* click; returns 1 if consumed */
void nc_menu_draw(int cols);

/* --- nc_minimap.c -------------------------------------------------------- */
void nc_draw_minimap(void);			/* sets MinimapW, draws side panel */
void nc_draw_minimap_late(void);		/* narrow overlay, after the editor */
void nc_minimap_cycle(void);			/* 'm' key: cycle overlay/off */
void nc_minimap_close(void);			/* close button */
int  nc_minimap_on(void);
/* click: 0 = not ours, 1 = map grid (*tx,*ty), 2 = close button, 3 = chrome */
int  nc_minimap_hit(int y, int x, int *tx, int *ty);

/* --- nc_status.c --------------------------------------------------------- */
extern int NoticeActive;
void nc_set_status(char *msg);			/* transient status-line message */
void nc_clear_status(void);
void nc_draw_status(SimView *view);		/* the bottom status line */
void nc_auto_goto(int x, int y);		/* engine callback */
void nc_show_notice(int id);			/* engine callback */
void nc_draw_notice(void);
void nc_notice_dismiss(void);

/* --- nc_input.c ---------------------------------------------------------- */
extern int QueryActive;
char *nc_tool_name(int state);
int  nc_tool_from_key(int ch);			/* letter -> tool state, or -1 */
int  nc_toolbar_count(void);
int  nc_toolbar_state(int i);
int  nc_toolbar_color(int i);
int  nc_toolbar_bg(int i);
char nc_toolbar_key(int i);
char *nc_toolbar_code(int i);
char *nc_toolbar_lcode(int i);
char *nc_toolbar_emoji(int i);
int  nc_toolbar_gridrows(void);
int  nc_toolbar_rowlen(int r);
void nc_tool_next(SimView *view);
void nc_tool_prev(SimView *view);
void nc_tool_select(SimView *view, int state);
void nc_apply_tool(SimView *view);
int  nc_tool_cost(int state);
void nc_draw_query(void);
void nc_query_dismiss(void);
/* engine callbacks (called from w_tool.c) */
void nc_show_zone_status(char *zone, char *s0, char *s1, char *s2,
			 char *s3, char *s4, int x, int y);
void nc_did_tool(char *name, int x, int y);
#ifdef NCURSES_MOUSE_VERSION
void nc_mouse(SimView *view);			/* KEY_MOUSE dispatcher */
#endif

#endif /* NC_H */
