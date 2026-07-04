/* view.h  --  ncurses port (lean, no X11/Tcl/Tk)
 *
 * Micropolis, Unix Version.  Copyright (C) 1989 - 2007 Electronic Arts Inc.
 * Licensed under the GNU GPL v3 or later; see the LICENSE file for the full
 * text and the additional EA terms.  No trademark or publicity rights granted.
 *
 * This is the terminal-port replacement for the original X11/Tk view.h: the
 * SimView / SimGraph / SimDate structures have had all Xlib/Tk/Tcl-typed fields
 * removed (they only served the discarded X11 renderer).  SimSprite is kept
 * verbatim -- it never held any X type.  The engine (.c files) touches only the
 * plain scalar / pointer fields retained here.
 */

#define X_Mem_View 1
#define X_Wire_View 2

#define Editor_Class 0
#define Map_Class 1

#define Button_Press 0
#define Button_Move 1
#define Button_Release 2

#define VIEW_REDRAW_PENDING 1


typedef struct SimPoint {		/* was XPoint */
  int x, y;
} SimPoint;


typedef struct Ink {
  struct Ink *next;
  int x, y;
  int color;
  int length;
  int maxlength;
  SimPoint *points;			/* was XPoint *points */
  int left, top, right, bottom;
  int last_x, last_y;
} Ink;


typedef struct SimView {
  struct SimView *next;
  char *title;
  int type;
  int class;

/* status */
  int visible;
  int invalid;
  int skips;
  int skip;
  int update;

/* map stuff */
  short map_state;
  int show_editors;

/* editor / tool stuff */
  short power_type;
  short tool_showing;
  short tool_mode;
  short tool_x, tool_y;
  short tool_x_const, tool_y_const;
  short tool_state;
  short tool_state_save;
  short super_user;
  short show_me;
  short dynamic_filter;
  unsigned long tool_event_time;		/* was Time */
  unsigned long tool_last_event_time;		/* was Time */

/* scrolling */
  int w_x, w_y;					/* view window position */
  int w_width, w_height;			/* view window size */
  int m_width, m_height;			/* memory buffer size */
  int i_width, i_height;			/* ideal whole size */
  int pan_x, pan_y;				/* centered in window */
  int tile_x, tile_y, tile_width, tile_height;	/* visible tiles */
  int screen_x, screen_y, screen_width, screen_height; /* visible pixels */

/* tracking */
  int orig_pan_x, orig_pan_y;
  int last_x, last_y;
  int last_button;
  char *track_info;
  char *message_var;

  int flags;

/* timing */
  int updates;
  double update_real;
  double update_user;
  double update_system;
  int update_context;

/* auto goto */
  int auto_goto;
  int auto_going;
  int auto_x_goal, auto_y_goal;
  int auto_speed;
  struct SimSprite *follow;

/* sound */
  int sound;

/* configuration */
  int width, height;

/* overlay */
  int show_overlay;
  int overlay_mode;
  struct timeval overlay_time;
} SimView;


/* History graph window -- X/Tk fields dropped; history data lives in globals
 * (ResHis, History10[] ...).  Kept as a minimal stub referenced by Sim.graph. */
typedef struct SimGraph {
  struct SimGraph *next;
  int range;
  int mask;
  int visible;
  int w_x, w_y;
  int w_width, w_height;
} SimGraph;


/* Date display window -- X/Tk fields dropped.  Kept minimal for Sim.date. */
typedef struct SimDate {
  struct SimDate *next;
  int reset;
  int month;
  int year;
  int lastmonth;
  int lastyear;
  int visible;
  int w_x, w_y;
  int w_width, w_height;
} SimDate;


/* Sprites -- kept verbatim from the original (contained no X types). */
typedef struct SimSprite {
  struct SimSprite *next;
  char *name;
  int type;
  int frame;
  int x, y;
  int width, height;
  int x_offset, y_offset;
  int x_hot, y_hot;
  int orig_x, orig_y;
  int dest_x, dest_y;
  int count, sound_count;
  int dir, new_dir;
  int step, flag, control;
  int turn;
  int accel;
  int speed;
} SimSprite;


typedef struct Person {
  int id;
  char *name;
} Person;


typedef struct Sim {
  int editors;
  SimView *editor;
  int maps;
  SimView *map;
  int graphs;
  SimGraph *graph;
  int dates;
  SimDate *date;
  int sprites;
  SimSprite *sprite;
  Ink *overlay;
} Sim;


typedef struct Cmd {
  char *name;
  int (*cmd)();
} Cmd;
