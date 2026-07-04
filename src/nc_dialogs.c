/* nc_dialogs.c  --  modal dialogs (budget; evaluation and graphs follow).
 *
 * A modal dialog runs its own nested getch loop, so the simulation is frozen
 * while it is open (the main loop -- and thus SimFrame -- is suspended).  It
 * reads/writes the engine's budget globals directly.
 */

#include "sim.h"
#include <curses.h>
#include <dirent.h>
#include "nc.h"

/* engine budget globals (defined in w_budget.c) */
extern float roadPercent, policePercent, firePercent;
extern QUAD roadValue, policeValue, fireValue;
extern QUAD roadMaxValue, policeMaxValue, fireMaxValue;
extern int makeDollarDecimalStr(char *numStr, char *dollarStr);

static void
dollar(long v, char *out)
{
  char num[64];
  sprintf(num, "%ld", v);
  makeDollarDecimalStr(num, out);
}

/* one funding row: label, want (100%), got (funded), percent, selected */
static void
fund_row(int row, int col, int w, char *label, long want, float pct, int sel)
{
  char got[64], wnt[64];
  int i, n = (int)(pct * 10 + 0.5);	/* 0..10 bar */

  dollar((long)(want * pct), got);
  dollar(want, wnt);
  attrset(sel ? (NC_CP(COLOR_BLACK, COLOR_CYAN) | A_BOLD) : NC_CP(COLOR_WHITE, COLOR_BLUE));
  move(row, col);
  printw(" %-7s [", label);
  for (i = 0; i < 10; i++) addch(i < n ? '=' : ' ');
  printw("] %3d%%  %10s / %-10s", (int)(pct * 100 + 0.5), got, wnt);
  { int x = getcurx(stdscr); while (x++ < col + w) addch(' '); }
  attrset(A_NORMAL);
}

/*
 * Modal budget dialog.  Called from w_budget.c (ShowBudgetWindowAndStartWaiting).
 * sel: 0=tax 1=road 2=fire 3=police.  Arrows/hjkl move; left/right or -/+ adjust;
 * Enter/Esc close.  Adjusting a percent updates *Value so the caller spends it.
 */
void
nc_budget_modal(void)
{
  int sel = 0, done = 0, rows, cols, top, left, w = 56, h = 13;
  char buf[80], d1[64], d2[64];

  while (!done) {
    long cashflow, spend;

    /* keep funded amounts in sync with the sliders */
    roadValue = (QUAD)(roadMaxValue * roadPercent);
    fireValue = (QUAD)(fireMaxValue * firePercent);
    policeValue = (QUAD)(policeMaxValue * policePercent);
    spend = (long)roadValue + (long)fireValue + (long)policeValue;
    cashflow = (long)TaxFund - spend;

    getmaxyx(stdscr, rows, cols);
    top = (rows - h) / 2; if (top < 0) top = 0;
    left = (cols - w) / 2; if (left < 0) left = 0;

    { int r, c;
      attrset(NC_CP(COLOR_WHITE, COLOR_BLUE) | A_BOLD);
      for (r = 0; r < h; r++) { move(top + r, left); for (c = 0; c < w; c++) addch(' '); }
      mvaddnstr(top, left + 2, " CITY BUDGET ", w - 4);
    }

    attrset(sel == 0 ? (NC_CP(COLOR_BLACK, COLOR_CYAN) | A_BOLD) : NC_CP(COLOR_WHITE, COLOR_BLUE));
    sprintf(buf, " Tax rate: %2d%%", CityTax);
    mvaddnstr(top + 2, left + 2, buf, w - 4);

    attrset(NC_CP(COLOR_WHITE, COLOR_BLUE));
    dollar((long)TaxFund, d1);
    sprintf(buf, " Taxes collected: %s", d1);
    mvaddnstr(top + 3, left + 2, buf, w - 4);

    fund_row(top + 5, left + 2, w - 4, "Road",   (long)roadMaxValue,   roadPercent,   sel == 1);
    fund_row(top + 6, left + 2, w - 4, "Fire",   (long)fireMaxValue,   firePercent,   sel == 2);
    fund_row(top + 7, left + 2, w - 4, "Police", (long)policeMaxValue, policePercent, sel == 3);

    attrset(NC_CP(COLOR_WHITE, COLOR_BLUE));
    dollar(cashflow < 0 ? -cashflow : cashflow, d1);
    dollar((long)TotalFunds + cashflow, d2);
    sprintf(buf, " Cash flow: %c%s    Funds after: %s",
	    cashflow < 0 ? '-' : '+', d1, d2);
    mvaddnstr(top + 9, left + 2, buf, w - 4);

    attrset(NC_CP(COLOR_YELLOW, COLOR_BLUE) | A_BOLD);
    mvaddnstr(top + h - 1, left + 2,
	      " up/down select  left/right adjust  Enter=OK ", w - 4);
    attrset(A_NORMAL);
    refresh();

    switch (getch()) {
    case ERR: break;
    case '`': nc_screenshot("/tmp/ttycity_shot.txt"); break;
    case KEY_UP: case 'k':   sel = (sel + 3) % 4; break;
    case KEY_DOWN: case 'j': sel = (sel + 1) % 4; break;
    case KEY_LEFT: case 'h': case '-':
      if (sel == 0) { if (CityTax > 0) CityTax--; }
      else if (sel == 1) { roadPercent -= 0.05f; if (roadPercent < 0) roadPercent = 0; }
      else if (sel == 2) { firePercent -= 0.05f; if (firePercent < 0) firePercent = 0; }
      else { policePercent -= 0.05f; if (policePercent < 0) policePercent = 0; }
      break;
    case KEY_RIGHT: case 'l': case '+': case '=':
      if (sel == 0) { if (CityTax < 20) CityTax++; }
      else if (sel == 1) { roadPercent += 0.05f; if (roadPercent > 1) roadPercent = 1; }
      else if (sel == 2) { firePercent += 0.05f; if (firePercent > 1) firePercent = 1; }
      else { policePercent += 0.05f; if (policePercent > 1) policePercent = 1; }
      break;
    case '\n': case '\r': case KEY_ENTER: case 27: case 'q':
      done = 1; break;
    }
  }

  roadValue = (QUAD)(roadMaxValue * roadPercent);
  fireValue = (QUAD)(fireMaxValue * firePercent);
  policeValue = (QUAD)(policeMaxValue * policePercent);
  clear();
}

/* ---- evaluation window --------------------------------------------------- */

extern char *probStr[10], *cityClassStr[6], *cityLevelStr[3];
extern int CurrentYear(void);

void
nc_eval_modal(void)
{
  int rows, cols, top, left, w = 52, h = 16, i, done = 0;
  char buf[80], d[64];

  while (!done) {
    getmaxyx(stdscr, rows, cols);
    top = (rows - h) / 2; if (top < 0) top = 0;
    left = (cols - w) / 2; if (left < 0) left = 0;

    { int r, c;
      attrset(NC_CP(COLOR_WHITE, COLOR_BLUE) | A_BOLD);
      for (r = 0; r < h; r++) { move(top + r, left); for (c = 0; c < w; c++) addch(' '); }
    }
    sprintf(buf, " CITY EVALUATION  %d", CurrentYear());
    mvaddnstr(top, left + 2, buf, w - 4);

    attrset(NC_CP(COLOR_WHITE, COLOR_BLUE));
    sprintf(buf, " Public Opinion:   Positive %d%%   Negative %d%%",
	    (int)CityYes, (int)CityNo);
    mvaddnstr(top + 2, left + 2, buf, w - 4);

    mvaddnstr(top + 4, left + 2, " Worst Problems:", w - 4);
    for (i = 0; i < 4; i++) {
      int p = ProblemOrder[i], v = (p >= 0 && p < 10) ? ProblemVotes[p] : 0;
      if (v > 0)
	sprintf(buf, "   %d. %-14s %d%%", i + 1, probStr[p], v);
      else
	sprintf(buf, "   %d. --", i + 1);
      mvaddnstr(top + 5 + i, left + 2, buf, w - 4);
    }

    mvaddnstr(top + 10, left + 2, " Statistics:", w - 4);
    sprintf(d, "%ld", (long)CityPop);
    sprintf(buf, "   Population:   %s  (%+d)", d, (int)deltaCityPop);
    mvaddnstr(top + 11, left + 2, buf, w - 4);
    { char num[64]; sprintf(num, "%ld", (long)CityAssValue); makeDollarDecimalStr(num, d); }
    sprintf(buf, "   Assessed Value: %s", d);
    mvaddnstr(top + 12, left + 2, buf, w - 4);
    sprintf(buf, "   City Class: %s     Level: %s",
	    cityClassStr[(CityClass >= 0 && CityClass < 6) ? CityClass : 0],
	    cityLevelStr[(GameLevel >= 0 && GameLevel < 3) ? GameLevel : 0]);
    mvaddnstr(top + 13, left + 2, buf, w - 4);

    attrset(NC_CP(COLOR_YELLOW, COLOR_BLUE) | A_BOLD);
    sprintf(buf, " Overall Score: %d   (annual change %+d)   -- any key --",
	    (int)CityScore, (int)deltaCityScore);
    mvaddnstr(top + h - 1, left + 2, buf, w - 4);
    attrset(A_NORMAL);
    refresh();

    switch (getch()) {
    case ERR: break;
    case '`': nc_screenshot("/tmp/ttycity_shot.txt"); break;
    default: done = 1; break;
    }
  }
  clear();
}

/* ---- history graphs ------------------------------------------------------ */

extern short *ResHis, *ComHis, *IndHis, *MoneyHis, *CrimeHis, *PollutionHis;

static void
sparkline(int row, int col, int width, char *label, short *arr, int off, int color)
{
  static char ramp[] = " .:-=+*#@";
  int i, hi = 1, v;

  for (i = 0; i < width; i++) { v = arr[off + i]; if (v > hi) hi = v; }
  attrset(NC_CP(color, COLOR_BLACK) | A_BOLD);
  mvaddstr(row, col, label);
  for (i = 0; i < width; i++) {
    v = arr[off + (width - 1 - i)];		/* newest on the right */
    if (v < 0) v = 0;
    mvaddch(row, col + 6 + i, ramp[(v * 8) / hi]);
  }
  attrset(A_NORMAL);
}

void
nc_graph_modal(void)
{
  int rows, cols, top, left, w, h = 12, done = 0, range = 0, width, off;
  char buf[80];

  while (!done) {
    getmaxyx(stdscr, rows, cols);
    w = cols - 6; if (w > 90) w = 90; if (w < 30) w = 30;
    top = (rows - h) / 2; if (top < 0) top = 0;
    left = (cols - w) / 2; if (left < 0) left = 0;
    width = w - 8; if (width > 120) width = 120;
    off = range ? 120 : 0;

    { int r, c;
      attrset(NC_CP(COLOR_WHITE, COLOR_BLACK));
      for (r = 0; r < h; r++) { move(top + r, left); for (c = 0; c < w; c++) addch(' '); }
    }
    attrset(NC_CP(COLOR_WHITE, COLOR_BLACK) | A_BOLD);
    sprintf(buf, " CITY HISTORY  (%s)   r=toggle range   any other key=close",
	    range ? "120 years" : "10 years");
    mvaddnstr(top, left + 2, buf, w - 4);

    sparkline(top + 3, left + 2, width, "Res  ", ResHis,       off, COLOR_GREEN);
    sparkline(top + 4, left + 2, width, "Com  ", ComHis,       off, COLOR_BLUE);
    sparkline(top + 5, left + 2, width, "Ind  ", IndHis,       off, COLOR_YELLOW);
    sparkline(top + 6, left + 2, width, "Cash ", MoneyHis,     off, COLOR_CYAN);
    sparkline(top + 7, left + 2, width, "Crime", CrimeHis,     off, COLOR_RED);
    sparkline(top + 8, left + 2, width, "Poll ", PollutionHis, off, COLOR_MAGENTA);
    attrset(NC_CP(COLOR_WHITE, COLOR_BLACK));
    mvaddnstr(top + 10, left + 2, " oldest <-------------------- newest ", w - 4);
    attrset(A_NORMAL);
    refresh();

    switch (getch()) {
    case ERR: break;
    case '`': nc_screenshot("/tmp/ttycity_shot.txt"); break;
    case 'r': case 'R': range = !range; break;
    default: done = 1; break;
    }
  }
  clear();
}

/* ---- text-input prompt (filenames) --------------------------------------- */

int
nc_prompt(char *title, char *buf, int buflen)
{
  int rows, cols, top, left, w = 56, done = 0, ok = 0, pos, ch;

  pos = (int)strlen(buf);
  while (!done) {
    getmaxyx(stdscr, rows, cols);
    top = rows / 2 - 2; if (top < 0) top = 0;
    left = (cols - w) / 2; if (left < 0) left = 0;

    { int r, c;
      attrset(NC_CP(COLOR_WHITE, COLOR_BLUE) | A_BOLD);
      for (r = 0; r < 4; r++) { move(top + r, left); for (c = 0; c < w; c++) addch(' '); }
    }
    mvaddnstr(top, left + 2, title, w - 4);
    attrset(NC_CP(COLOR_BLACK, COLOR_WHITE));
    { int c; move(top + 1, left + 2); for (c = 0; c < w - 4; c++) addch(' '); }
    mvaddnstr(top + 1, left + 2, buf, w - 4);
    attrset(NC_CP(COLOR_YELLOW, COLOR_BLUE) | A_BOLD);
    mvaddnstr(top + 3, left + 2, " Enter=OK  Esc=cancel ", w - 4);
    attrset(A_NORMAL);
    refresh();

    ch = getch();
    if (ch == ERR) continue;
    if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) { ok = 1; done = 1; }
    else if (ch == 27) { done = 1; }
    else if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
      if (pos > 0) buf[--pos] = 0;
    } else if (ch >= 32 && ch < 127 && pos < buflen - 1) {
      buf[pos++] = ch; buf[pos] = 0;
    }
  }
  clear();
  return ok;
}

/* ---- load / save --------------------------------------------------------- */

extern int LoadCity(char *filename);
extern int SaveCityAs(char *filename);
extern int SaveCity(void);
extern char *CityFileName;

extern char *HomeDir;

#define FB_MAX 512

typedef struct { char name[256]; int isdir; } FBEnt;

static int
fb_cmp(const void *a, const void *b)
{
  const FBEnt *x = (const FBEnt *)a, *y = (const FBEnt *)b;
  if (x->isdir != y->isdir) return y->isdir - x->isdir;	/* directories first */
  return strcmp(x->name, y->name);
}

static int
fb_is_city(char *name)
{
  int n = (int)strlen(name);
  return (n > 4) && (!strcmp(name + n - 4, ".cty") || !strcmp(name + n - 4, ".scn"));
}

/* read <dir> into ents[] (".." first, then dirs, then .cty/.scn); returns count */
static int
fb_read(char *dir, FBEnt *ents, int max)
{
  DIR *d;
  struct dirent *e;
  struct stat st;
  char path[1200];
  int n = 0;

  strcpy(ents[n].name, ".."); ents[n].isdir = 1; n++;
  d = opendir(dir);
  if (d) {
    while ((e = readdir(d)) != NULL && n < max) {
      if (e->d_name[0] == '.') continue;		/* skip dotfiles + . .. */
      sprintf(path, "%s/%s", dir, e->d_name);
      if (stat(path, &st) != 0) continue;
      if (S_ISDIR(st.st_mode)) {
	strncpy(ents[n].name, e->d_name, 255); ents[n].name[255] = 0;
	ents[n].isdir = 1; n++;
      } else if (fb_is_city(e->d_name)) {
	strncpy(ents[n].name, e->d_name, 255); ents[n].name[255] = 0;
	ents[n].isdir = 0; n++;
      }
    }
    closedir(d);
  }
  if (n > 1) qsort(ents + 1, n - 1, sizeof(FBEnt), fb_cmp);	/* keep ".." first */
  return n;
}

/*
 * File browser: navigate directories with the arrow keys, Enter opens a folder
 * (".." goes up) or loads a .cty/.scn file, Esc cancels.
 */
void
nc_load_modal(void)
{
  char cwd[1200], tmp[1300];
  FBEnt ents[FB_MAX];
  int n, sel = 0, off = 0, done = 0;
  int rows, cols, top, left, w, h, listh, i;

  sprintf(tmp, "%s/cities", HomeDir ? HomeDir : ".");	/* start near the cities */
  if (!realpath(tmp, cwd))
    if (!getcwd(cwd, sizeof(cwd))) strcpy(cwd, ".");

  n = fb_read(cwd, ents, FB_MAX);

  while (!done) {
    getmaxyx(stdscr, rows, cols);
    w = cols - 8; if (w > 64) w = 64; if (w < 24) w = 24;
    /* fixed height (don't shrink to fit the entry count) so a smaller directory
     * fully overwrites a larger one -- no leftover rows below the box */
    h = rows - 4; if (h > 26) h = 26; if (h < 8) h = 8;
    listh = h - 4;
    top = (rows - h) / 2; if (top < 0) top = 0;
    left = (cols - w) / 2; if (left < 0) left = 0;

    if (sel < 0) sel = 0;
    if (sel >= n) sel = n - 1;
    if (sel < off) off = sel;
    if (sel >= off + listh) off = sel - listh + 1;

    { int r, c;
      attrset(NC_CP(COLOR_WHITE, COLOR_BLUE) | A_BOLD);
      for (r = 0; r < h; r++) { move(top + r, left); for (c = 0; c < w; c++) addch(' '); }
    }
    mvaddnstr(top, left + 2, " Load City ", w - 4);
    attrset(NC_CP(COLOR_YELLOW, COLOR_BLUE));
    { int l = (int)strlen(cwd); char *p = cwd + (l > w - 4 ? l - (w - 4) : 0);
      mvaddnstr(top + 1, left + 2, p, w - 4); }

    for (i = 0; i < listh && off + i < n; i++) {
      FBEnt *en = &ents[off + i];
      char line[300];
      int selrow = (off + i == sel);
      attrset(selrow ? (NC_CP(COLOR_BLACK, COLOR_CYAN) | A_BOLD)
		     : (en->isdir ? (NC_CP(COLOR_WHITE, COLOR_BLUE) | A_BOLD)
				  : NC_CP(COLOR_WHITE, COLOR_BLUE)));
      sprintf(line, " %s%s", en->name, en->isdir ? "/" : "");
      { int x; move(top + 2 + i, left + 1); for (x = 0; x < w - 2; x++) addch(' '); }
      mvaddnstr(top + 2 + i, left + 1, line, w - 2);
    }

    attrset(NC_CP(COLOR_YELLOW, COLOR_BLUE) | A_BOLD);
    mvaddnstr(top + h - 1, left + 2, " up/down  Enter=open  Esc=cancel ", w - 4);
    attrset(A_NORMAL);
    refresh();

    switch (getch()) {
    case ERR: break;
    case '`': nc_screenshot("/tmp/ttycity_shot.txt"); break;
    case KEY_UP: case 'k':   sel--; break;
    case KEY_DOWN: case 'j': sel++; break;
    case KEY_NPAGE:          sel += listh; break;
    case KEY_PPAGE:          sel -= listh; break;
    case '\n': case '\r': case KEY_ENTER:
      if (ents[sel].isdir) {
	sprintf(tmp, "%s/%s", cwd, ents[sel].name);
	if (realpath(tmp, cwd)) { n = fb_read(cwd, ents, FB_MAX); sel = 0; off = 0; }
      } else {
	sprintf(tmp, "%s/%s", cwd, ents[sel].name);	/* mutable buffer for LoadCity */
	if (LoadCity(tmp)) {
	  CursorX = WORLD_X / 2; CursorY = WORLD_Y / 2;
	  nc_set_status("City loaded.");
	} else {
	  nc_set_status("Could not load that file.");
	}
	done = 1;
      }
      break;
    case 27: case 'q':
      done = 1; break;
    }
  }
  clear();
}

void
nc_save_modal(int saveas)
{
  char path[256];

  if (!saveas && CityFileName && CityFileName[0]) {
    if (SaveCity()) nc_set_status("City saved.");
    else nc_set_status("Save failed.");
    return;
  }
  path[0] = 0;
  if (CityFileName && CityFileName[0])
    strcpy(path, CityFileName);			/* loaded/saved from disk */
  else if (CityName && CityName[0])
    sprintf(path, "%s.cty", CityName);		/* built-in city: suggest its name */
  else
    strcpy(path, "mycity.cty");
  if (nc_prompt("Save city as -- enter path:", path, sizeof(path))) {
    if (SaveCityAs(path)) nc_set_status("City saved.");
    else nc_set_status("Save failed.");
  }
}

/* ---- new game / scenario picker ------------------------------------------ */

extern int LoadScenario(short s);
extern int GenerateSomeCity(int r);
extern int SetGameLevelFunds(short);
extern int setCityName(char *);
extern int setSpeed(short);
extern short Rand16(void);

static char *newgame_items[] = {
  "New City  -  Easy    ($20,000)",
  "New City  -  Medium  ($10,000)",
  "New City  -  Hard    ($5,000)",
  "Scenario: Dullsville      1900",
  "Scenario: San Francisco   1906",
  "Scenario: Hamburg         1944",
  "Scenario: Bern            1965",
  "Scenario: Tokyo           1957",
  "Scenario: Detroit         1972",
  "Scenario: Boston          2010",
  "Scenario: Rio de Janeiro  2047"
};
#define NG_N 11

void
nc_newgame_modal(void)
{
  int rows, cols, top, left, w = 40, h = NG_N + 5, sel = 0, done = 0, i;

  while (!done) {
    getmaxyx(stdscr, rows, cols);
    top = (rows - h) / 2; if (top < 0) top = 0;
    left = (cols - w) / 2; if (left < 0) left = 0;

    { int r, c;
      attrset(NC_CP(COLOR_WHITE, COLOR_BLUE) | A_BOLD);
      for (r = 0; r < h; r++) { move(top + r, left); for (c = 0; c < w; c++) addch(' '); }
    }
    mvaddnstr(top, left + 2, " NEW GAME ", w - 4);
    for (i = 0; i < NG_N; i++) {
      attrset(i == sel ? (NC_CP(COLOR_BLACK, COLOR_CYAN) | A_BOLD)
			: NC_CP(COLOR_WHITE, COLOR_BLUE));
      mvaddch(top + 2 + i, left + 2, ' ');
      mvaddnstr(top + 2 + i, left + 3, newgame_items[i], w - 5);
    }
    attrset(NC_CP(COLOR_YELLOW, COLOR_BLUE) | A_BOLD);
    mvaddnstr(top + h - 1, left + 2, " Enter=start  Esc=cancel ", w - 4);
    attrset(A_NORMAL);
    refresh();

    switch (getch()) {
    case ERR: break;
    case '`': nc_screenshot("/tmp/ttycity_shot.txt"); break;
    case KEY_UP: case 'k':   sel = (sel + NG_N - 1) % NG_N; break;
    case KEY_DOWN: case 'j': sel = (sel + 1) % NG_N; break;
    case '\n': case '\r': case KEY_ENTER:
      if (sel < 3) {
	StartupGameLevel = sel;
	GenerateSomeCity((int)Rand16());
	setCityName("Micropolis");
	SetGameLevelFunds((short)sel);
      } else {
	LoadScenario((short)(sel - 2));
      }
      EditorView->tool_state = residentialState;
      CursorX = WORLD_X / 2; CursorY = WORLD_Y / 2;
      setSpeed(3);
      done = 1;
      break;
    case 27: case 'q':
      done = 1; break;
    }
  }
  clear();
}

/* ---- built-in (embedded) city picker ------------------------------------- */

extern int EmbeddedCityCount(void);
extern const char *EmbeddedCityName(int i);
extern int LoadEmbeddedCity(char *name);

/* Scrollable list of the cities baked into the binary (cities/*.cty).  Enter
 * loads the selected one from memory; it is then saveable to disk (Save As). */
void
nc_load_embedded_modal(void)
{
  int n = EmbeddedCityCount();
  int rows, cols, top, left, w, h, listh, i;
  int sel = 0, off = 0, done = 0;

  while (!done) {
    getmaxyx(stdscr, rows, cols);
    w = cols - 8; if (w > 48) w = 48; if (w < 24) w = 24;
    h = rows - 4; if (h > 26) h = 26; if (h < 8) h = 8;
    listh = h - 4;
    top = (rows - h) / 2; if (top < 0) top = 0;
    left = (cols - w) / 2; if (left < 0) left = 0;

    if (sel < 0) sel = 0;
    if (sel >= n) sel = n - 1;
    if (sel < off) off = sel;
    if (sel >= off + listh) off = sel - listh + 1;

    { int r, c;
      attrset(NC_CP(COLOR_WHITE, COLOR_BLUE) | A_BOLD);
      for (r = 0; r < h; r++) { move(top + r, left); for (c = 0; c < w; c++) addch(' '); }
    }
    mvaddnstr(top, left + 2, " Load Built-in City ", w - 4);

    for (i = 0; i < listh && off + i < n; i++) {
      const char *nm = EmbeddedCityName(off + i);
      char line[300];
      int selrow = (off + i == sel);
      attrset(selrow ? (NC_CP(COLOR_BLACK, COLOR_CYAN) | A_BOLD)
		     : NC_CP(COLOR_WHITE, COLOR_BLUE));
      sprintf(line, " %s", nm ? nm : "");
      { int x; move(top + 2 + i, left + 1); for (x = 0; x < w - 2; x++) addch(' '); }
      mvaddnstr(top + 2 + i, left + 1, line, w - 2);
    }

    attrset(NC_CP(COLOR_YELLOW, COLOR_BLUE) | A_BOLD);
    mvaddnstr(top + h - 1, left + 2, " up/down  Enter=load  Esc=cancel ", w - 4);
    attrset(A_NORMAL);
    refresh();

    switch (getch()) {
    case ERR: break;
    case '`': nc_screenshot("/tmp/ttycity_shot.txt"); break;
    case KEY_UP: case 'k':   sel--; break;
    case KEY_DOWN: case 'j': sel++; break;
    case KEY_NPAGE:          sel += listh; break;
    case KEY_PPAGE:          sel -= listh; break;
    case '\n': case '\r': case KEY_ENTER:
      { const char *nm = EmbeddedCityName(sel);
	char buf[256];
	if (nm) {
	  strncpy(buf, nm, sizeof(buf) - 1); buf[sizeof(buf) - 1] = 0;
	  if (LoadEmbeddedCity(buf)) {
	    CursorX = WORLD_X / 2; CursorY = WORLD_Y / 2;
	    nc_set_status("Built-in city loaded.");
	  } else {
	    nc_set_status("Could not load that city.");
	  }
	}
	done = 1;
      }
      break;
    case 27: case 'q':
      done = 1; break;
    }
  }
  clear();
}
