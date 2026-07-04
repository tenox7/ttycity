/* nc_status.c  --  the bottom message line (transient status / scrolling
 * advisory) and the disaster/milestone notice popup.  The city stats (name,
 * date, funds, pop, score, class) live in the left panel (nc_render.c).
 */

#include "sim.h"
#include <curses.h>
#include "nc.h"

extern char LastMessage[256];		/* s_msg.c: current advisory text */
extern short HaveLastMessage;

/* ---- transient status message (tool feedback etc.) ----------------------- */

static char StatusMsg[128] = "";

void
nc_set_status(char *msg)
{
  strncpy(StatusMsg, msg ? msg : "", sizeof(StatusMsg) - 1);
  StatusMsg[sizeof(StatusMsg) - 1] = 0;
}

void
nc_clear_status(void)
{
  StatusMsg[0] = 0;
}

/* ---- the status line ------------------------------------------------------ */

void
nc_draw_status(SimView *view)
{
  int rows, cols;
  char buf[300];

  getmaxyx(stdscr, rows, cols);
  if (rows < 3 || cols < 20) return;

  /* transient status OR the scrolling advisory */
  move(rows - 1, 0); clrtoeol();
  if (StatusMsg[0]) {
    attrset(A_BOLD);
    sprintf(buf, " * %s", StatusMsg);
    mvaddnstr(rows - 1, 0, buf, cols - 1);
    attrset(A_NORMAL);
  } else if (HaveLastMessage && LastMessage[0]) {
    sprintf(buf, " > %s", LastMessage);
    mvaddnstr(rows - 1, 0, buf, cols - 1);
  } else {
    mvaddnstr(rows - 1, 0,
      " SHIFT+letter=tool  arrows=move  enter=build  m=map  space=pause  0-3=speed  q=quit",
      cols - 1);
  }
}

/* ---- auto-goto (engine callback from DoAutoGoto) ------------------------- */

void
nc_auto_goto(int x, int y)
{
  if (x < 0) x = 0; if (x > WORLD_X - 1) x = WORLD_X - 1;
  if (y < 0) y = 0; if (y > WORLD_Y - 1) y = WORLD_Y - 1;
  CursorX = x;
  CursorY = y;
}

/* ---- notice popup (engine callback from DoShowPicture) ------------------- */

int NoticeActive = 0;
static char NoticeText[256];
static int NoticeId;

void
nc_show_notice(int id)
{
  if (!DoNotices) return;		/* Options > Notices disables popups */
  NoticeId = id;
  NoticeText[0] = 0;
  if (id > 0 && id < 43)
    GetIndString(NoticeText, 301, id);	/* same advisory text table */
  if (NoticeText[0] == 0)
    sprintf(NoticeText, "Event #%d", id);
  NoticeActive = 1;
}

void
nc_notice_dismiss(void)
{
  NoticeActive = 0;
}

void
nc_draw_notice(void)
{
  int rows, cols, w, h, top, left, i, len, x;

  if (!NoticeActive) return;
  getmaxyx(stdscr, rows, cols);

  len = (int)strlen(NoticeText);
  w = len + 6;
  if (w > cols - 4) w = cols - 4;
  if (w < 20) w = 20;
  h = 5;
  top = rows / 3;
  left = (cols - w) / 2;
  if (left < 0) left = 0;

  attrset(NC_CP(COLOR_WHITE, COLOR_RED) | A_BOLD);
  for (i = 0; i < h; i++) {
    move(top + i, left);
    for (x = 0; x < w; x++) addch(' ');
  }
  mvaddnstr(top + 1, left + 2, "! NOTICE !", w - 4);
  mvaddnstr(top + 2, left + 2, NoticeText, w - 4);
  mvaddnstr(top + h - 1, left + 2, " press any key ", w - 4);
  attrset(A_NORMAL);
}
