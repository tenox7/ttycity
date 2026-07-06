/* nc_main.c  --  ncurses Micropolis: program entry, engine bring-up, main loop.
 *
 * Replaces sim.c's main/tk_main and the Tk timer-driven sim_loop.  The game
 * runs a single fixed-rate tick: every SIM_TICK_MS we call SimFrame() (which
 * self-gates on SimSpeed) and MoveObjects(); input and rendering happen every
 * loop iteration.
 */

#include "sim.h"
#include <curses.h>
#include <locale.h>
#include "nc.h"

/* ---- globals that used to live in the (discarded) sim.c driver ----------- */

char *MicropolisVersion = "4.0";
Sim *sim = NULL;
int sim_loops = 0;
int sim_delay = 50;
int sim_skips = 0;
int sim_skip = 0;
int sim_paused = 0;
int sim_paused_speed = 3;
int sim_tty = 0;
int heat_steps = 0;
int heat_flow = -7;
int heat_rule = 0;
int heat_wrap = 3;
struct timeval start_time, now_time, beat_time, last_now_time;
char *CityFileName = NULL;
int Startup = 0;
int StartupGameLevel = 0;
char *StartupName = NULL;
int WireMode = 0;
int MultiPlayerMode = 0;
int TilesAnimated = 0;
int DoAnimation = 1;
int DoMessages = 1;
int DoNotices = 1;
char *Displays = NULL;
char *FirstDisplay = NULL;
int ExitReturn = 0;

/* HomeDir / ResourceDir / HostName are defined (as globals) by w_resrc.c;
 * env_init() below fills them in from $SIMHOME. */

/* ---- shared front-end state (declared extern in nc.h) -------------------- */

SimView *EditorView = NULL;
int CursorX = WORLD_X / 2;
int CursorY = WORLD_Y / 2;
int ViewPanX = 0;
int ViewPanY = 0;
int Quitting = 0;

#define SIM_TICK_MS 50

#ifdef NCURSES_MOUSE_VERSION
static int MouseMotionOn = 0;

/* xterm button-motion tracking (mode 1002): ncurses often only requests
 * click reporting (1000), so the terminal never sends drag motion and a
 * right-drag would look like a tap.  Ask for it ourselves; unknown private
 * modes are ignored by terminals that lack them.  Turned off before every
 * endwin so the terminal is left clean. */
static void
mouse_motion(int on)
{
  if (on) {
    fputs("\033[?1002h", stdout);
    MouseMotionOn = 1;
  } else if (MouseMotionOn) {
    fputs("\033[?1002l", stdout);
    MouseMotionOn = 0;
  }
  fflush(stdout);
}
#endif

sim_exit(int val)
{
  tkMustExit = 1;
  Quitting = 1;
  ExitReturn = val;
}

/* Does <dir> contain a "cities/" folder? (the bundled example cities) */
static int
dir_has_cities(char *dir)
{
  char p[600];
  struct stat st;
  sprintf(p, "%s/cities", dir);
  return (stat(p, &st) == 0) && S_ISDIR(st.st_mode);
}

static char HomeDirBuf[600];

/* Resolve HomeDir (replaces sim.c's env_init).  String and scenario resources
 * are baked into the binary (res_data.h), so nothing is loaded from a res/
 * directory at runtime.  HomeDir only seeds the Load-City browser's starting
 * folder ($HomeDir/cities), so honor $SIMHOME first, then probe the dev-tree
 * and install locations for a cities/ dir; failing that, the current dir. */
static void
env_init(void)
{
  char *s = getenv("SIMHOME");
  char *cands[4];
  int n = 0, i;

  HostName = "localhost";
  ResourceDir = NULL;			/* resources are embedded, not on disk */

  if (s && *s && dir_has_cities(s)) {
    strcpy(HomeDirBuf, s);
    HomeDir = HomeDirBuf; return;
  }

  cands[n++] = ".";			/* run from the repo / install root   */
  cands[n++] = "..";			/* run from src/ (the build dir)      */
  cands[n++] = "../..";
  cands[n++] = "/usr/local/share/ttycity";	/* classic install path       */
  for (i = 0; i < n; i++) {
    if (dir_has_cities(cands[i])) {
      strcpy(HomeDirBuf, cands[i]);
      HomeDir = HomeDirBuf; return;
    }
  }

  strcpy(HomeDirBuf, (s && *s) ? s : ".");
  HomeDir = HomeDirBuf;
}

/* ---- engine bring-up (subset of sim.c's sim_init) ------------------------ */

static void
engine_init(void)
{
  gettimeofday(&start_time, NULL);
  gettimeofday(&beat_time, NULL);
  gettimeofday(&now_time, NULL);
  last_now_time = now_time;

  UserSoundOn = 0;
  MustUpdateOptions = 1;
  HaveLastMessage = 0;
  ScenarioID = 0;
  StartingYear = 1900;
  tileSynch = 0x01;
  sim_skips = sim_skip = 0;
  autoGo = 1;
  CityTax = 7;
  CityTime = 50;
  NoDisasters = 0;
  PunishCnt = 0;
  autoBulldoze = 1;
  autoBudget = 1;
  MesNum = 0;
  LastMesTime = 0;
  flagBlink = 1;
  SimSpeed = 3;
  ChangeEval();
  MessagePort = 0;
  MesX = MesY = 0;
  sim_paused = 0;
  sim_loops = 0;
  InitSimLoad = 2;
  tkMustExit = 0;
  ExitReturn = 0;

  InitializeSound();
  initMapArrays();
  initGraphs();
  InitFundingLevel();
  setUpMapProcs();
  StopEarthquake();

  sim = MakeNewSim();
  EditorView = MakeNewView();
  EditorView->type = Editor_Class;
  EditorView->tool_state = residentialState;
  EditorView->tool_state_save = -1;
  EditorView->map_state = ALMAP;
  EditorView->super_user = 1;
  sim->editor = EditorView;
  sim->editors = 1;

  ResetMapState();
  ResetEditorState();
  ClearMap();
  InitWillStuff();
  SetFunds(5000);
  SetGameLevelFunds(StartupGameLevel);
  setSpeed(0);
  setSkips(0);
}

static void
start_new_city(int seed)
{
  GenerateSomeCity(seed);
  setCityName("Micropolis");
  SetGameLevelFunds(StartupGameLevel);
  setSpeed(1);
}

/* called from the File menu */
void
start_new_city_menu(void)
{
  start_new_city((int)Rand16());
  EditorView->tool_state = residentialState;
  CursorX = WORLD_X / 2;
  CursorY = WORLD_Y / 2;
}

static int
start_load_city(char *fname)
{
  if (!LoadCity(fname)) return 0;
  DoSimInit();
  Kick();
  setSpeed(1);
  return 1;
}

/* The status HUD lives in nc_status.c (nc_draw_status). */

/* ---- input --------------------------------------------------------------- */

static void
handle_key(int ch)
{
  int tool;

  if (ch == '`') {			/* screenshot always allowed */
    nc_screenshot("/tmp/ttycity_shot.txt");
    nc_set_status("screenshot -> /tmp/ttycity_shot.txt");
    return;
  }
#ifdef KEY_RESIZE
  if (ch == KEY_RESIZE) {		/* terminal resized: repaint from scratch */
    clear();
    return;
  }
#endif
#if defined(KEY_MOUSE) && defined(NCURSES_MOUSE_VERSION)
  if (ch == KEY_MOUSE) {		/* before the menu: clicks drive it too */
    nc_mouse(EditorView);
    return;
  }
#endif

  /* menu bar takes priority while open; open it on Esc / F10 */
  if (nc_menu_active()) {
    nc_menu_key(ch);
    return;
  }
  if (ch == 27
#ifdef KEY_F
      || ch == KEY_F(10)
#endif
      ) {
    nc_menu_enter();
    return;
  }

  /* a popup swallows the next keypress to dismiss itself */
  if (NoticeActive) {
    nc_notice_dismiss();
    return;
  }
  if (QueryActive) {
    nc_query_dismiss();
    return;
  }

  /* single-letter tool hotkeys (the toolbar buttons) */
  tool = nc_tool_from_key(ch);
  if (tool >= 0) {
    nc_tool_select(EditorView, tool);
    return;
  }

  switch (ch) {
  case 'q':
    Quitting = 1;
    break;
  case KEY_UP: case 'k':
    if (CursorY > 0) CursorY--;
    break;
  case KEY_DOWN: case 'j':
    if (CursorY < WORLD_Y - 1) CursorY++;
    break;
  case KEY_LEFT: case 'h':
    if (CursorX > 0) CursorX--;
    break;
  case KEY_RIGHT: case 'l':
    if (CursorX < WORLD_X - 1) CursorX++;
    break;

  /* tool selection: uppercase letters (handled above) or cycle */
  case ']': case '\t':
    nc_tool_next(EditorView);
    break;
  case '[':
    nc_tool_prev(EditorView);
    break;

  /* apply the current tool at the cursor */
  case '\n': case '\r': case KEY_ENTER: case 'x':
    nc_clear_status();
    nc_apply_tool(EditorView);
    break;

  /* speed / pause */
  case ' ':
    if (SimSpeed) setSpeed(0); else setSpeed(3);
    break;
  case '0': setSpeed(0); break;
  case '1': setSpeed(1); break;
  case '2': setSpeed(2); break;
  case '3': setSpeed(3); break;

  case 'm':				/* cycle the overview minimap + overlays */
    nc_minimap_cycle();
    clear();
    break;

  case 'u':				/* cycle graphics mode (default/unicode/...) */
    {
      char msg[48];
      sprintf(msg, "Graphics: %s", nc_gfx_cycle());
      nc_set_status(msg);
      clear();				/* tile width may have changed */
    }
    break;

  case 'g': case 'G':
    start_new_city(Rand16());
    break;
  }
}

/* ---- pacing -------------------------------------------------------------- */

static long
ms_since(struct timeval *t0)
{
  struct timeval now;
  gettimeofday(&now, NULL);
  return (now.tv_sec - t0->tv_sec) * 1000L +
	 (now.tv_usec - t0->tv_usec) / 1000L;
}

int
main(int argc, char *argv[])
{
  struct timeval last_tick;
  char *loadfile = NULL;
  char *shot_path = NULL;
  char *gfxname = NULL;
  int shot_frames = 60;
  int seed = 0, i, have_seed = 0;

  for (i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "-shot") && i + 1 < argc) {
      shot_path = argv[++i];			/* headless render test */
    } else if (!strcmp(argv[i], "-frames") && i + 1 < argc) {
      shot_frames = atoi(argv[++i]);
    } else if (!strcmp(argv[i], "-theme") && i + 1 < argc) {
      nc_set_theme(argv[++i]);			/* tan | grass | dark */
    } else if (!strcmp(argv[i], "-gfx") && i + 1 < argc) {
      gfxname = argv[++i];			/* standard | unicode | ascii | aa */
    } else if (argv[i][0] == '-') {
      /* other options handled in later phases; ignore for now */
    } else if (!loadfile && (strstr(argv[i], ".cty") || strstr(argv[i], ".scn"))) {
      loadfile = argv[i];
    } else {
      seed = atoi(argv[i]);
      have_seed = 1;
    }
  }

  setlocale(LC_ALL, "");	/* multibyte output for the unicode gfx mode */
  if (gfxname && !nc_gfx_set(gfxname)) {
    fprintf(stderr,
	    "ttycity: unknown or unavailable -gfx mode '%s'\n"
	    "         (modes: standard, unicode, ascii, aa; unicode needs"
	    " a UTF-8 locale,\n         aa a `make aalib` build)\n",
	    gfxname);
    return 1;
  }

  /* ncurses init */
  initscr();
  cbreak();
  noecho();
  nonl();
  keypad(stdscr, TRUE);
  curs_set(0);
#ifdef NCURSES_VERSION
  set_escdelay(25);			/* make lone Esc responsive (menu key) */
#endif
#ifdef NCURSES_MOUSE_VERSION
  /* BUTTON3_RELEASED un-merges right press/release (drag bookkeeping);
   * REPORT_MOUSE_POSITION delivers motion while a button is held (pan). */
  if (mousemask(BUTTON1_PRESSED | BUTTON1_CLICKED | BUTTON1_DOUBLE_CLICKED |
		BUTTON3_PRESSED | BUTTON3_CLICKED | BUTTON3_RELEASED |
		BUTTON4_PRESSED | REPORT_MOUSE_POSITION
#if NCURSES_MOUSE_VERSION > 1
		| BUTTON5_PRESSED		/* wheel down */
#endif
		, NULL))
    mouse_motion(1);
  mouseinterval(0);			/* act on press; no click-resolve delay */
#endif
  if (!gfxname)
    nc_gfx_auto();		/* mono -> ascii, UTF-8 emulator -> unicode */
  nc_colors_init();

  env_init();
  engine_init();

  if (loadfile) {
    if (!start_load_city(loadfile)) {
#ifdef NCURSES_MOUSE_VERSION
      mouse_motion(0);
#endif
      endwin();
      fprintf(stderr, "Could not load city file '%s'\n", loadfile);
      return 1;
    }
  } else {
    start_new_city(have_seed ? seed : (int)Rand16());
  }

  CursorX = WORLD_X / 2;
  CursorY = WORLD_Y / 2;
  EditorView->tool_state = residentialState;	/* ResetEditorState defaults to doze */

  /* headless screenshot mode: advance a few frames, dump stdscr, exit */
  if (shot_path) {
    for (i = 0; i < shot_frames; i++) {
      SimFrame();
      MoveObjects();
      if (DoAnimation) animateTiles();
    }
    DoUpdateHeads();
    scoreDoer();
    nc_draw_toolbar(EditorView);
    nc_draw_minimap();
    nc_draw_editor(EditorView);
    nc_draw_status(EditorView);
    nc_draw_minimap_late();
    nc_menu_draw(COLS);
    refresh();
    nc_screenshot(shot_path);
#ifdef NCURSES_MOUSE_VERSION
    mouse_motion(0);
#endif
    endwin();
    return 0;
  }

  gettimeofday(&last_tick, NULL);
  timeout(SIM_TICK_MS);

  while (!Quitting && !tkMustExit) {
    int ch = getch();
    if (ch != ERR) handle_key(ch);

    if (ms_since(&last_tick) >= SIM_TICK_MS) {
      gettimeofday(&last_tick, NULL);
      gettimeofday(&now_time, NULL);
      flagBlink = (now_time.tv_usec < 500000) ? 1 : -1;
      if (SimSpeed) {
	SimFrame();
	MoveObjects();
	sim_loops++;
      }
      if (DoAnimation) animateTiles();
      if (ShakeNow > 0) ShakeNow--;	/* earthquake shake decays */
      DoUpdateHeads();		/* funds/date/demand + message ticker + notices */
      scoreDoer();		/* refresh CityScore/Class if EvalChanged */
    }

    nc_draw_toolbar(EditorView);
    nc_draw_minimap();		/* sets MinimapW before the editor sizes itself */
    nc_draw_editor(EditorView);
    nc_draw_status(EditorView);
    nc_draw_minimap_late();	/* narrow-terminal overlay, over the editor */
    nc_menu_draw(COLS);		/* bar + dropdown on top of the editor */
    nc_draw_query();
    nc_draw_notice();
    refresh();
  }

#ifdef NCURSES_MOUSE_VERSION
  mouse_motion(0);
#endif
  endwin();
  return ExitReturn;
}
