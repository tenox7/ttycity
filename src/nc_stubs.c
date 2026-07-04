/* nc_stubs.c -- Phase 0 UI-callback stubs.
 *
 * The simulation engine (kept from ../sim) calls into a small set of UI
 * functions that used to live in the discarded X11/Tcl files (w_x.c, w_tk.c,
 * w_editor.c, w_map.c, ...) plus the extract-later files not yet ported
 * (w_sprite.c, w_tool.c, g_map.c, g_smmaps.c, g_bigmap.c).
 *
 * For Phase 0 (prove the engine compiles and links X-free and that SimFrame
 * runs) these are no-ops / minimal.  Real ncurses implementations replace them
 * phase by phase (nc_shim.c, nc_render.c, nc_minimap.c, ...).
 */

#include "sim.h"

/* ---- globals owned by not-yet-ported files (w_graph.c / w_tool.c /
 *      w_sprite.c) that kept engine code references ------------------------- */

short Graph10Max, Graph120Max;		/* w_graph.c */
short NewGraph;				/* w_graph.c */
int UpdateDelayed;			/* w_update/w_tool */
/* OverRide / PendingTool are defined by w_tool.c; CrashX/CrashY by w_sprite.c */

/* ---- lifecycle / run-loop flags (were in w_tk.c / sim.c) ------------------ */

int tkMustExit = 0;

void Kick(void) { }
void UpdateFlush(void) { }
void DoTimeoutListen(void) { }
void DoStopMicropolis(void) { }
void StopToolkit(void) { }
/* ResetLastKeys is provided by kept w_keys.c */

/* ---- redraw / invalidate (were in w_tk.c / w_x.c) ------------------------- */

void InvalidateMaps(void) { }
void InvalidateEditors(void) { }
void RedrawMaps(void) { }
void RedrawEditors(void) { }
void EventuallyRedrawView(SimView *view) { }
void CancelRedrawView(SimView *view) { }
/* DoUpdateEditor is provided by nc_render.c */
int  DoUpdateMap(SimView *view) { return 0; }
void DoNewEditor(SimView *view) { }
void DoNewMap(SimView *view) { }
void ViewToTileCoords(SimView *view, int x, int y, int *tx, int *ty) { *tx = 0; *ty = 0; }
void ViewToPixelCoords(SimView *view, int x, int y, int *px, int *py) { *px = 0; *py = 0; }
void DidStopPan(SimView *view) { }

/* ---- allocators (were ckalloc in w_x.c) ---------------------------------- */

Sim *MakeNewSim(void)
{
  return (Sim *)calloc(1, sizeof(Sim));
}

SimView *MakeNewView(void)
{
  return (SimView *)calloc(1, sizeof(SimView));
}

/* ---- timer / earthquake (were Tk timers in w_tk.c) ----------------------- */

void StartMicropolisTimer(void) { }
void StopMicropolisTimer(void) { }
void FixMicropolisTimer(void) { }
/* DoEarthQuake / StopEarthquake are provided by nc_render.c (screen shake) */

/* ---- chalk / ink overlay (discarded feature) ----------------------------- */

Ink *NewInk(void) { return (Ink *)0; }
void FreeInk(Ink *ink) { }
void StartInk(int x, int y) { }
void AddInk(int x, int y) { }
void EraseOverlay(void) { }

/* ---- graphs (history data is engine state; drawing not yet ported) ------- */

void ChangeCensus(void) { }
void doAllGraphs(void) { }
void graphDoer(void) { }
void initGraphs(void) { }
void InitGraphMax(void) { }
void graph_command_init(void) { }
void drawGraph(void) { }

/* DoShowPicture is provided by kept s_msg.c; DoUpdateHeads and UpdateFunds by
 * kept w_update.c (their UI-notify bodies call Eval / other stubs here). */

/* ---- minimap decode (g_map.c / g_smmaps.c not yet ported) ---------------- */

void setUpMapProcs(void) { }
void drawAll(SimView *view) { }

/* ---- sprites (w_sprite.c not yet ported): spawners referenced by
 *      s_disast.c / s_traf.c / s_sim.c, plus MoveObjects from the main loop.
 *      No-op for Phase 0 (no sprites move, no crash-fires start).           */

/* Sprite AI (MoveObjects, Make*, Generate*, GetSprite, StartFire, ...) is now
 * provided by the compiled w_sprite.c.  Only the X-drawing DrawObjects/DrawSprite
 * are dropped there, so DrawObjects keeps its no-op here (nc_render draws sprites). */
void DrawObjects(SimView *view) { }

/* Tool logic (DoTool/setWandState/ToolDown/ToolUp/ToolDrag/DoPendTool) is now
 * provided by the compiled w_tool.c. */

/* ---- sound (w_sound.c is kept, but MakeSound may be here for now) --------
 *      MakeSound itself is provided by kept w_sound.c; nothing needed.      */

/* ---- Eval: the engine->UI string bridge (was Tcl_Eval in w_tk.c) ---------
 *
 * Kept engine files emit a handful of fixed "UIxxx" command literals.  For
 * Phase 0 we recognize them and no-op; nc_shim.c will route them to real
 * ncurses handlers later.  Unknown strings are ignored silently.
 */

int Eval(char *buf)
{
  return 0;
}
