/* nc_input.c  --  tool selection, placement, and the query panel.
 *
 * The engine's tool logic (w_tool.c / w_con.c) is reused unchanged: the clean
 * entry point is DoTool(view, tool_state, tileX, tileY), which takes TILE
 * coordinates and returns 1 ok / 0 silent / -1 must-bulldoze / -2 no-funds.
 */

#include "sim.h"
#include <curses.h>
#include "nc.h"

extern int DoTool(SimView *view, short tool, short x, short y);
extern int setWandState(SimView *view, short state);

/* Toolbar: a 3-column grid of tool buttons in grouped rows.  Each tool has a
 * short 1-2 char display code and an uppercase keyboard hotkey.  Order matches
 * the grid reading order; the grid_cols[] table gives how many tools are on
 * each row (so groups like RR/RO or the lone AP sit on their own lines). */
typedef struct {
  short state;
  char key;
  char *code;		/* short code (dense grid)  */
  char *lcode;		/* long code (expanded grid, always 2 chars) */
  char *emoji;		/* unicode gfx mode button face (2 columns) */
  short fg, bg;		/* button colors = how the tile looks on the map */
} ToolBtn;

static ToolBtn toolbar[] = {
  /* row 0: zones            */ { residentialState,'R', "R",  "RE", "🏠", COLOR_BLACK,  COLOR_CYAN    },
                                { commercialState, 'C', "C",  "CO", "🏬", COLOR_WHITE,  COLOR_BLUE    },
                                { industrialState, 'I', "I",  "IN", "🏭", COLOR_YELLOW, COLOR_MAGENTA },
  /* row 1: services + query */ { fireState,       'F', "F",  "FI", "🚒", COLOR_WHITE,  COLOR_RED     },
                                { queryState,      'Q', "?",  "QY", "🔍", COLOR_BLACK,  COLOR_WHITE   },
                                { policeState,     'P', "P",  "PD", "🚨", COLOR_WHITE,  COLOR_RED     },
  /* row 2: utilities        */ { wireState,       'W', "W",  "WI", "⚡",     COLOR_RED,    COLOR_WHITE   },
                                { dozeState,       'B', "B",  "BU", "🚜", COLOR_BLACK,  COLOR_YELLOW  },
                                { parkState,       'K', "\"", "PK", "🌳", COLOR_BLACK,  COLOR_GREEN   },
  /* row 3: transport        */ { rrState,         'L', "RR", "RR", "🚂", COLOR_BLACK,  COLOR_WHITE   },
                                { roadState,       'O', "RO", "RO", "🚗", COLOR_WHITE,  COLOR_BLACK   },
  /* row 4: big buildings    */ { stadiumState,    'S', "ST", "ST", "⚽",     COLOR_BLACK,  COLOR_WHITE   },
                                { seaportState,    'E', "PO", "PO", "⚓",     COLOR_WHITE,  COLOR_BLUE    },
  /* row 5: power plants      */ { powerState,     'G', "CP", "CP", "🔌", COLOR_WHITE,  COLOR_RED     },
                                { nuclearState,    'N', "NP", "NP", "☢ ",    COLOR_BLACK,  COLOR_GREEN   },
  /* row 6: airport           */ { airportState,   'A', "AP", "AP", "🛫", COLOR_BLACK,  COLOR_WHITE   }
};
#define NTOOLS ((int)(sizeof(toolbar) / sizeof(toolbar[0])))

static int grid_cols[] = { 3, 3, 3, 2, 2, 2, 1 };	/* tools per grid row */
#define NGRIDROWS ((int)(sizeof(grid_cols) / sizeof(grid_cols[0])))

static short tool_cycle_state(int i) { return toolbar[i].state; }

/* toolbar accessors for the renderer */
int    nc_toolbar_count(void)     { return NTOOLS; }
int    nc_toolbar_state(int i)    { return toolbar[i].state; }
int    nc_toolbar_color(int i)    { return toolbar[i].fg; }
int    nc_toolbar_bg(int i)       { return toolbar[i].bg; }
char   nc_toolbar_key(int i)      { return toolbar[i].key; }
char  *nc_toolbar_code(int i)     { return toolbar[i].code; }
char  *nc_toolbar_lcode(int i)    { return toolbar[i].lcode; }
char  *nc_toolbar_emoji(int i)    { return toolbar[i].emoji; }
int    nc_toolbar_gridrows(void)  { return NGRIDROWS; }
int    nc_toolbar_rowlen(int r)   { return grid_cols[r]; }

/* Map a pressed key to a tool state, or -1.  Tool hotkeys are UPPERCASE (the
 * toolbar shows them that way), so lowercase keys stay free for movement (hjkl)
 * and quit (q). */
int
nc_tool_from_key(int ch)
{
  int i;
  if (ch < 'A' || ch > 'Z') return -1;
  for (i = 0; i < NTOOLS; i++)
    if (toolbar[i].key == ch) return toolbar[i].state;
  return -1;
}

static char *tool_names[] = {
  "Residential", "Commercial", "Industrial", "FireDept", "Query",
  "PoliceDept", "Wire", "Bulldozer", "Rail", "Road", "Chalk",
  "Eraser", "Stadium", "Park", "Seaport", "CoalPower", "Nuclear",
  "Airport", "Network"
};

char *
nc_tool_name(int state)
{
  if (state < 0 || state > networkState) return "?";
  return tool_names[state];
}

/* Per-tool build cost (mirrors CostOf[] in w_tool.c, for the status display). */
int
nc_tool_cost(int state)
{
  extern QUAD CostOf[];
  if (state < 0 || state > networkState) return 0;
  return (int)CostOf[state];
}

static int
cycle_index(int state)
{
  int i;
  for (i = 0; i < NTOOLS; i++)
    if (tool_cycle_state(i) == state) return i;
  return 0;
}

void
nc_tool_next(SimView *view)
{
  int i = cycle_index(view->tool_state);
  setWandState(view, tool_cycle_state((i + 1) % NTOOLS));
}

void
nc_tool_prev(SimView *view)
{
  int i = cycle_index(view->tool_state);
  setWandState(view, tool_cycle_state((i + NTOOLS - 1) % NTOOLS));
}

void
nc_tool_select(SimView *view, int state)
{
  setWandState(view, state);
}

/* ---- apply the current tool at the cursor -------------------------------- */

void
nc_apply_tool(SimView *view)
{
  int r = DoTool(view, view->tool_state, (short)CursorX, (short)CursorY);
  Kick();
  if (r == -1)
    nc_set_status("Area must be bulldozed first.");
  else if (r == -2)
    nc_set_status("Insufficient funds to build that.");
}

/* ---- query panel --------------------------------------------------------- */

int QueryActive = 0;
static char QStr[6][256];
static int QX, QY;

/* engine callback (from DoShowZoneStatus in w_tool.c) */
void
nc_show_zone_status(char *zone, char *s0, char *s1, char *s2, char *s3,
		    char *s4, int x, int y)
{
  strncpy(QStr[0], zone ? zone : "", 255); QStr[0][255] = 0;
  strncpy(QStr[1], s0 ? s0 : "", 255); QStr[1][255] = 0;
  strncpy(QStr[2], s1 ? s1 : "", 255); QStr[2][255] = 0;
  strncpy(QStr[3], s2 ? s2 : "", 255); QStr[3][255] = 0;
  strncpy(QStr[4], s3 ? s3 : "", 255); QStr[4][255] = 0;
  strncpy(QStr[5], s4 ? s4 : "", 255); QStr[5][255] = 0;
  QX = x; QY = y;
  QueryActive = 1;
}

/* engine callback (from DidTool in w_tool.c): tool-success feedback sound */
void
nc_did_tool(char *name, int x, int y)
{
  /* sound is optional in the terminal port (Phase 9); no-op for now */
}

void
nc_query_dismiss(void)
{
  QueryActive = 0;
}

static char *QLabel[6] = {
  "Zone:     ", "Density:  ", "Value:    ",
  "Crime:    ", "Pollution:", "Growth:   "
};

void
nc_draw_query(void)
{
  int rows, cols, w, h, top, left, i;

  if (!QueryActive) return;
  getmaxyx(stdscr, rows, cols);

  w = 40;
  h = 9;
  top = (rows - h) / 2;
  left = (cols - w) / 2;
  if (top < 0) top = 0;
  if (left < 0) left = 0;

  attrset(NC_CP(COLOR_WHITE, COLOR_BLUE) | A_BOLD);
  for (i = 0; i < h; i++) {
    int c;
    move(top + i, left);
    for (c = 0; c < w; c++) addch(' ');
  }
  mvaddnstr(top, left + 2, " Zone Query ", w - 4);
  for (i = 0; i < 6; i++) {
    char line[256];
    sprintf(line, "%s %.24s", QLabel[i], QStr[i]);
    mvaddnstr(top + 2 + i, left + 2, line, w - 4);
  }
  mvaddnstr(top + h - 1, left + 2, " any key to close ", w - 4);
  attrset(A_NORMAL);
}
