/* nc_menu.c  --  top menu bar with dropdown menus.
 *
 * A classic menu bar (row 0): File / Options / Disasters / Speed / Views.
 * Open with F10 or Esc; Left/Right switch menus, Up/Down move, Enter selects,
 * Esc closes.  Actions call the engine directly (toggles, speed, disasters,
 * new/quit) or open a dialog (budget/eval/graph -- added in their phases).
 */

#include "sim.h"
#include <curses.h>
#include "nc.h"

/* engine entry points */
extern int  setSpeed(short);
extern void Pause(void);
extern void Resume(void);
extern void MakeFire(void);
extern void MakeFlood(void);
extern void MakeEarthquake(void);
extern void MakeMeltdown(void);
extern void MakeMonster(void);
extern void MakeTornado(void);
extern void DoBudgetFromMenu(void);

/* action ids */
enum {
  A_NONE = 0, A_SEP,
  A_NEW, A_LOAD, A_LOAD_RES, A_SAVE, A_SAVEAS, A_QUIT,
  A_T_BUDGET, A_T_BULLDOZE, A_T_GOTO, A_T_DISAST, A_T_SOUND,
  A_T_ANIM, A_T_MSG, A_T_NOTICE,
  A_DIS_MONSTER, A_DIS_FIRE, A_DIS_FLOOD, A_DIS_TORNADO, A_DIS_QUAKE, A_DIS_MELT,
  A_SPD_PAUSE, A_SPD_SLOW, A_SPD_MED, A_SPD_FAST,
  A_BUDGET, A_EVAL, A_GRAPH, A_MAP, A_THEME, A_GFX
};

typedef struct { char *label; int id; } Item;
typedef struct { char *title; Item *items; int n; } Menu;

static Item file_items[] = {
  { "New City",          A_NEW },
  { "Load Built-in...",  A_LOAD_RES },
  { "Load from Disk...", A_LOAD },
  { "Save City",         A_SAVE },
  { "Save As...",        A_SAVEAS },
  { "",                  A_SEP },
  { "Quit",              A_QUIT }
};
static Item opt_items[] = {
  { "Auto Budget",   A_T_BUDGET },
  { "Auto Bulldoze", A_T_BULLDOZE },
  { "Auto Goto",     A_T_GOTO },
  { "Disasters",     A_T_DISAST },
  { "Sound",         A_T_SOUND },
  { "Animation",     A_T_ANIM },
  { "Messages",      A_T_MSG },
  { "Notices",       A_T_NOTICE },
  { "Land Color >",  A_THEME },
  { "Graphics >",    A_GFX }
};
static Item dis_items[] = {
  { "Monster",    A_DIS_MONSTER },
  { "Fire",       A_DIS_FIRE },
  { "Flood",      A_DIS_FLOOD },
  { "Tornado",    A_DIS_TORNADO },
  { "Earthquake", A_DIS_QUAKE },
  { "Meltdown",   A_DIS_MELT }
};
static Item spd_items[] = {
  { "Pause",  A_SPD_PAUSE },
  { "Slow",   A_SPD_SLOW },
  { "Medium", A_SPD_MED },
  { "Fast",   A_SPD_FAST }
};
static Item view_items[] = {
  { "Budget",     A_BUDGET },
  { "Evaluation", A_EVAL },
  { "Graph",      A_GRAPH },
  { "Map/Overlay",A_MAP }
};

static Menu menus[] = {
  { "File",      file_items, 7 },
  { "Options",   opt_items,  10 },
  { "Disasters", dis_items,  6 },
  { "Speed",     spd_items,  4 },
  { "Views",     view_items, 4 }
};
#define NMENUS ((int)(sizeof(menus) / sizeof(menus[0])))

static int MenuOpen = -1;	/* -1 = closed, else menu index */
static int MenuSel = 0;		/* highlighted item in the open menu */

int
nc_menu_active(void)
{
  return MenuOpen >= 0;
}

void
nc_menu_enter(void)
{
  MenuOpen = 0;
  MenuSel = 0;
}

/* current on/off state of a toggle action (for the checkbox) */
static int
toggle_state(int id)
{
  switch (id) {
  case A_T_BUDGET:   return autoBudget;
  case A_T_BULLDOZE: return autoBulldoze;
  case A_T_GOTO:     return autoGo;
  case A_T_DISAST:   return !NoDisasters;
  case A_T_SOUND:    return UserSoundOn;
  case A_T_ANIM:     return DoAnimation;
  case A_T_MSG:      return DoMessages;
  case A_T_NOTICE:   return DoNotices;
  }
  return -1;			/* not a toggle */
}

extern void start_new_city_menu(void);	/* provided by nc_main.c */

static void
do_action(int id)
{
  switch (id) {
  case A_NEW:      nc_newgame_modal(); break;
  case A_LOAD_RES: nc_load_embedded_modal(); break;
  case A_LOAD:     nc_load_modal(); break;
  case A_SAVE:   nc_save_modal(0); break;
  case A_SAVEAS: nc_save_modal(1); break;
  case A_QUIT:   Quitting = 1; break;

  case A_T_BUDGET:   autoBudget = !autoBudget; MustUpdateOptions = 1; break;
  case A_T_BULLDOZE: autoBulldoze = !autoBulldoze; MustUpdateOptions = 1; break;
  case A_T_GOTO:     autoGo = !autoGo; MustUpdateOptions = 1; break;
  case A_T_DISAST:   NoDisasters = !NoDisasters; MustUpdateOptions = 1; break;
  case A_T_SOUND:    UserSoundOn = !UserSoundOn; MustUpdateOptions = 1; break;
  case A_T_ANIM:     DoAnimation = !DoAnimation; MustUpdateOptions = 1; break;
  case A_T_MSG:      DoMessages = !DoMessages; MustUpdateOptions = 1; break;
  case A_T_NOTICE:   DoNotices = !DoNotices; MustUpdateOptions = 1; break;

  case A_DIS_MONSTER: MakeMonster(); nc_set_status("A monster attacks!"); break;
  case A_DIS_FIRE:    MakeFire(); nc_set_status("Fire reported!"); break;
  case A_DIS_FLOOD:   MakeFlood(); nc_set_status("Flooding reported!"); break;
  case A_DIS_TORNADO: MakeTornado(); nc_set_status("Tornado sighted!"); break;
  case A_DIS_QUAKE:   MakeEarthquake(); nc_set_status("Earthquake!"); break;
  case A_DIS_MELT:    MakeMeltdown(); nc_set_status("Nuclear meltdown!"); break;

  case A_SPD_PAUSE: setSpeed(0); break;
  case A_SPD_SLOW:  setSpeed(1); break;
  case A_SPD_MED:   setSpeed(2); break;
  case A_SPD_FAST:  setSpeed(3); break;

  case A_BUDGET: DoBudgetFromMenu(); break;
  case A_EVAL:   nc_eval_modal(); break;
  case A_GRAPH:  nc_graph_modal(); break;
  case A_MAP:    nc_minimap_cycle(); break;
  case A_THEME:
    { char msg[64]; sprintf(msg, "Land color: %s", nc_cycle_theme());
      nc_set_status(msg); }
    break;
  case A_GFX:
    { char msg[64]; sprintf(msg, "Graphics: %s ('u' cycles)", nc_gfx_cycle());
      nc_set_status(msg); }
    break;
  }
}

static int
next_item(int m, int sel, int dir)
{
  int n = menus[m].n, i = sel, guard = 0;
  do {
    i = (i + dir + n) % n;
    if (menus[m].items[i].id != A_SEP) return i;
  } while (++guard < n);
  return sel;
}

/* handle a key while a menu is open; returns 1 if consumed */
int
nc_menu_key(int ch)
{
  if (MenuOpen < 0) return 0;

  switch (ch) {
  case KEY_LEFT: case 'h':
    MenuOpen = (MenuOpen - 1 + NMENUS) % NMENUS; MenuSel = 0; break;
  case KEY_RIGHT: case 'l':
    MenuOpen = (MenuOpen + 1) % NMENUS; MenuSel = 0; break;
  case KEY_UP: case 'k':
    MenuSel = next_item(MenuOpen, MenuSel, -1); break;
  case KEY_DOWN: case 'j':
    MenuSel = next_item(MenuOpen, MenuSel, 1); break;
  case '\n': case '\r': case KEY_ENTER:
    { int id = menus[MenuOpen].items[MenuSel].id;
      MenuOpen = -1;
      if (id != A_SEP) do_action(id); }
    clear();
    break;
  case 27:			/* Esc */
#ifdef KEY_F
  case KEY_F(10):
#endif
    MenuOpen = -1; clear(); break;
  default:
    return 1;			/* swallow other keys while menu open */
  }
  return 1;
}

/*
 * Mouse click at (y,x): row 0 opens/toggles a menu title; a click inside an
 * open dropdown runs that item; any other click while open just closes it.
 * Returns 1 when the click was consumed.  Geometry mirrors nc_menu_draw.
 */
int
nc_menu_mouse(int y, int x)
{
  int i, j, w, mx;
  Menu *m;

  if (y == 0) {				/* the menu bar itself */
    mx = 1;
    for (i = 0; i < NMENUS; i++) {
      int tw = (int)strlen(menus[i].title) + 3;
      if (x >= mx && x < mx + tw - 1) {
	if (MenuOpen == i) MenuOpen = -1;
	else { MenuOpen = i; MenuSel = 0; }
	clear();
	return 1;
      }
      mx += tw;
    }
    if (MenuOpen >= 0) { MenuOpen = -1; clear(); }
    return 1;				/* bare bar: swallow the click */
  }

  if (MenuOpen < 0) return 0;

  m = &menus[MenuOpen];
  mx = 1;
  for (i = 0; i < MenuOpen; i++) mx += (int)strlen(menus[i].title) + 3;
  w = 0;
  for (j = 0; j < m->n; j++) {
    int l = (int)strlen(m->items[j].label) + 4;
    if (l > w) w = l;
  }
  if (w < 12) w = 12;

  j = y - 1;				/* dropdown rows start under the bar */
  if (j >= 0 && j < m->n && x >= mx && x < mx + w) {
    int id = m->items[j].id;
    MenuOpen = -1;
    if (id != A_SEP) do_action(id);
    clear();
    return 1;
  }
  MenuOpen = -1;			/* clicked elsewhere: close */
  clear();
  return 1;
}

void
nc_menu_draw(int cols)
{
  int i, x = 0;

  /* menu bar */
  attrset(NC_CP(COLOR_BLACK, COLOR_WHITE));
  for (i = 0; i < cols; i++) mvaddch(0, i, ' ');
  x = 1;
  for (i = 0; i < NMENUS; i++) {
    int hot = (MenuOpen == i);
    attrset(hot ? NC_MSEL(NC_CP(COLOR_WHITE, COLOR_BLUE) | A_BOLD)
		: NC_CP(COLOR_BLACK, COLOR_WHITE));
    mvaddch(0, x, ' ');
    mvaddstr(0, x + 1, menus[i].title);
    addch(' ');
    x += (int)strlen(menus[i].title) + 3;
  }
  attrset(A_NORMAL);

  /* open dropdown */
  if (MenuOpen >= 0) {
    Menu *m = &menus[MenuOpen];
    int w = 0, mx = 1, j;
    for (i = 0; i < MenuOpen; i++) mx += (int)strlen(menus[i].title) + 3;
    for (j = 0; j < m->n; j++) {
      int l = (int)strlen(m->items[j].label) + 4;
      if (l > w) w = l;
    }
    if (w < 12) w = 12;
    for (j = 0; j < m->n; j++) {
      char line[64];
      int st, sel = (j == MenuSel);
      Item *it = &m->items[j];
      attrset(sel ? NC_MSEL(NC_CP(COLOR_BLACK, COLOR_CYAN) | A_BOLD)
		  : NC_CP(COLOR_WHITE, COLOR_BLUE));
      if (it->id == A_SEP) {
	int k; move(1 + j, mx); for (k = 0; k < w; k++) addch('-');
	continue;
      }
      st = toggle_state(it->id);
      if (st >= 0)
	sprintf(line, " [%c] %-*s", st ? 'x' : ' ', w - 6, it->label);
      else
	sprintf(line, " %-*s", w - 2, it->label);
      mvaddnstr(1 + j, mx, line, w);
    }
    attrset(A_NORMAL);
  }
}
