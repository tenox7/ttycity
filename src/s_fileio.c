#include "sim.h"
#include <string.h>


/*
 * The .cty / scenario files are stored big-endian.  The original decided at
 * compile time (IS_INTEL etc.) whether to byte-swap.  That is fragile across
 * hosts, so the port detects host endianness at RUNTIME: swap on little-endian
 * hosts (Intel/ARM), leave alone on big-endian hosts (the classic Unix targets
 * -- SPARC, PA-RISC, MIPS, POWER).  Works everywhere with no build flag.
 */

static int
_host_little_endian(void)
{
  unsigned short s = 1;
  return (int)(*(unsigned char *)&s);	/* 1 => little-endian host */
}

static void
_swap_shorts(short *buf, int len)
{
  int i;

  /* Flip bytes in each short! */
  for (i = 0; i < len; i++) {
    *buf = ((*buf & 0xFF) <<8) | ((*buf &0xFF00) >>8);
    buf++;
  }
}

static void
_swap_longs(long *buf, int len)
{
  int i;

  /* Flip bytes in each long! */
  for (i = 0; i < len; i++) {
    long l = *buf;
    *buf =
      ((l & 0x000000ff) << 24) |
      ((l & 0x0000ff00) << 8) |
      ((l & 0x00ff0000) >> 8) |
      ((l & 0xff000000) >> 24);
    buf++;
  }
}

static void
_half_swap_longs(long *buf, int len)
{
  int i;

  /* Flip bytes in each long! */
  for (i = 0; i < len; i++) {
    long l = *buf;
    *buf =
      ((l & 0x0000ffff) << 16) |
      ((l & 0xffff0000) >> 16);
    buf++;
  }
}

#define SWAP_SHORTS(a, b)	do { if (_host_little_endian()) _swap_shorts(a, b); } while (0)
#define SWAP_LONGS(a, b)	do { if (_host_little_endian()) _swap_longs(a, b); } while (0)
#define HALF_SWAP_LONGS(a, b)	do { if (_host_little_endian()) _half_swap_longs(a, b); } while (0)


static int
_load_short(short *buf, int len, FILE *f)
{
  if (fread(buf, sizeof(short), len, f) != len)
     return 0;

  SWAP_SHORTS(buf, len);	/* to intel */

  return 1;
}


static int
_load_long(long *buf, int len, FILE *f)
{
  if (fread(buf, sizeof(long), len, f) != len)
     return 0;

  SWAP_LONGS(buf, len);	/* to intel */

  return 1;
}


static int
_save_short(short *buf, int len, FILE *f)
{

  SWAP_SHORTS(buf, len);	/* to MAC */

  if (fwrite(buf, sizeof(short), len, f) != len)
     return 0;

  SWAP_SHORTS(buf, len);	/* back to intel */

  return 1;
}


static int
_save_long(long *buf, int len, FILE *f)
{

  SWAP_LONGS(buf, len);	/* to MAC */

  if (fwrite(buf, sizeof(long), len, f) != len)
     return 0;

  SWAP_LONGS(buf, len);	/* back to intel */

  return 1;
}


static
int
_load_file(char *filename, char *dir)
{
  FILE *f;
  char path[512];
  QUAD size;

#ifdef MSDOS
  if (dir != NULL) {
    sprintf(path, "%s\\%s", dir, filename);
    filename = path;
  }
  if ((f = fopen(filename, "rb")) == NULL) {
    return 0;
  }
#else
  if (dir != NULL) {
    sprintf(path, "%s/%s", dir, filename);
    filename = path;
  }
  if ((f = fopen(filename, "r")) == NULL) {
    return (0);
  }
#endif

  fseek(f, 0L, SEEK_END);
  size = ftell(f);
  fseek(f, 0L, SEEK_SET);

  switch (size) {
  case 27120: /* Normal city */
    break;

  case 99120: /* 2x2 city */
    break;

  case 219120: /* 3x3 city */
    break;

  default:
    return (0);
  }

  if ((_load_short(ResHis, HISTLEN / 2, f) == 0) ||
      (_load_short(ComHis, HISTLEN / 2, f) == 0) ||
      (_load_short(IndHis, HISTLEN / 2, f) == 0) ||
      (_load_short(CrimeHis, HISTLEN / 2, f) == 0) ||
      (_load_short(PollutionHis, HISTLEN / 2, f) == 0) ||
      (_load_short(MoneyHis, HISTLEN / 2, f) == 0) ||
      (_load_short(MiscHis, MISCHISTLEN / 2, f) == 0) ||
      (_load_short((&Map[0][0]), WORLD_X * WORLD_Y, f) == 0)) {

    /* TODO:  report error */
    fclose(f);
    return(0);
  }

  fclose(f);
  return(1);
}


static int _load_mem(const unsigned char *src, QUAD size);	/* defined below */

/* Shared tail of loadFile/loadMem: unpack funds/date/flags from MiscHis and
 * kick the engine.  The raw map+history arrays are already populated. */
static void
_finish_load(void)
{
  long l;

  /* total funds is a long.....    MiscHis is array of shorts */
  /* total funds is being put in the 50th & 51th word of MiscHis */
  /* find the address, cast the ptr to a lontPtr, take contents */

  l = *(QUAD *)(MiscHis + 50);
  HALF_SWAP_LONGS(&l, 1);
  SetFunds(l);

  l = *(QUAD *)(MiscHis + 8);
  HALF_SWAP_LONGS(&l, 1);
  CityTime = l;

  autoBulldoze = MiscHis[52];	/* flag for autoBulldoze */
  autoBudget = MiscHis[53];	/* flag for autoBudget */
  autoGo = MiscHis[54];		/* flag for autoGo */
  UserSoundOn = MiscHis[55];	/* flag for the sound on/off */
  CityTax = MiscHis[56];
  SimSpeed = MiscHis[57];
  //  sim_skips = sim_skip = 0;
  ChangeCensus();
  MustUpdateOptions = 1;

  /* yayaya */

  l = *(QUAD *)(MiscHis + 58);
  HALF_SWAP_LONGS(&l, 1);
  policePercent = l / 65536.0;

  l = *(QUAD *)(MiscHis + 60);
  HALF_SWAP_LONGS(&l, 1);
  firePercent = l / 65536.0;

  l = *(QUAD *)(MiscHis + 62);
  HALF_SWAP_LONGS(&l, 1);
  roadPercent = l / 65536.0;

  policePercent = (*(QUAD*)(MiscHis + 58)) / 65536.0;	/* and 59 */
  firePercent = (*(QUAD*)(MiscHis + 60)) / 65536.0;	/* and 61 */
  roadPercent =(*(QUAD*)(MiscHis + 62)) / 65536.0;	/* and 63 */

  if (CityTime < 0)
    CityTime = 0;
  if ((CityTax > 20) || (CityTax < 0))
    CityTax = 7;
  if ((SimSpeed < 0) || (SimSpeed > 3))
    SimSpeed = 3;

  setSpeed(SimSpeed);
  setSkips(0);

  InitFundingLevel();

  /* set the scenario id to 0 */
  InitWillStuff();
  ScenarioID = 0;
  InitSimLoad = 1;
  DoInitialEval = 0;
  DoSimInit();
  InvalidateEditors();
  InvalidateMaps();
}


int loadFile(char *filename)
{
  if (_load_file(filename, NULL) == 0)
    return (0);
  _finish_load();
  return (1);
}


/* Load a city from an in-memory blob (embedded resource) rather than disk. */
int loadMem(const unsigned char *buf, QUAD size)
{
  if (_load_mem(buf, size) == 0)
    return (0);
  _finish_load();
  return (1);
}


int saveFile(char *filename)
{
  long l;
  FILE *f;

#ifdef MSDOS
  if ((f = fopen(filename, "wb")) == NULL) {
#else
  if ((f = fopen(filename, "w")) == NULL) {
#endif
    /* TODO: report error */
    return(0);
  }

  /* total funds is a long.....    MiscHis is array of ints */
  /* total funds is bien put in the 50th & 51th word of MiscHis */
  /* find the address, cast the ptr to a lontPtr, take contents */

  l = TotalFunds;
  HALF_SWAP_LONGS(&l, 1);
  (*(QUAD *)(MiscHis + 50)) = l;

  l = CityTime;
  HALF_SWAP_LONGS(&l, 1);
  (*(QUAD *)(MiscHis + 8)) = l;

  MiscHis[52] = autoBulldoze;	/* flag for autoBulldoze */
  MiscHis[53] = autoBudget;	/* flag for autoBudget */
  MiscHis[54] = autoGo;		/* flag for autoGo */
  MiscHis[55] = UserSoundOn;	/* flag for the sound on/off */
  MiscHis[57] = SimSpeed;
  MiscHis[56] = CityTax;	/* post release */

  /* yayaya */

  l = (int)(policePercent * 65536);
  HALF_SWAP_LONGS(&l, 1);
  (*(QUAD *)(MiscHis + 58)) = l;

  l = (int)(firePercent * 65536);
  HALF_SWAP_LONGS(&l, 1);
  (*(QUAD *)(MiscHis + 60)) = l;

  l = (int)(roadPercent * 65536);
  HALF_SWAP_LONGS(&l, 1);
  (*(QUAD *)(MiscHis + 62)) = l;

  if ((_save_short(ResHis, HISTLEN / 2, f) == 0) ||
      (_save_short(ComHis, HISTLEN / 2, f) == 0) ||
      (_save_short(IndHis, HISTLEN / 2, f) == 0) ||
      (_save_short(CrimeHis, HISTLEN / 2, f) == 0) ||
      (_save_short(PollutionHis, HISTLEN / 2, f) == 0) ||
      (_save_short(MoneyHis, HISTLEN / 2, f) == 0) ||
      (_save_short(MiscHis, MISCHISTLEN / 2, f) == 0) ||
      (_save_short((&Map[0][0]), WORLD_X * WORLD_Y, f) < 0)) {

    /* TODO:  report error */
    fclose(f);
    return(0);
  }

  fclose(f);
  return(1);
}


/* Load a city straight from an embedded resource blob (res_data.h): the
 * baked-in scenarios (snro.*) and example cities (cities/*.cty).  Reads the
 * same fixed-length field prefix as _load_file (histories + WORLD_X*WORLD_Y
 * map), accepting the same three city sizes -- just from memory. */
static int
_load_mem(const unsigned char *src, QUAD size)
{
  const unsigned char *p = src;

  switch (size) {
  case 27120:	/* normal city */
  case 99120:	/* 2x2 city    */
  case 219120:	/* 3x3 city    */
    break;
  default:
    return 0;
  }

#define RD(arr, len) do { \
    memcpy((arr), p, (len) * sizeof(short)); \
    SWAP_SHORTS((short *)(arr), (len)); \
    p += (len) * sizeof(short); \
  } while (0)

  RD(ResHis,       HISTLEN / 2);
  RD(ComHis,       HISTLEN / 2);
  RD(IndHis,       HISTLEN / 2);
  RD(CrimeHis,     HISTLEN / 2);
  RD(PollutionHis, HISTLEN / 2);
  RD(MoneyHis,     HISTLEN / 2);
  RD(MiscHis,      MISCHISTLEN / 2);
  RD(&Map[0][0],   WORLD_X * WORLD_Y);

#undef RD
  return 1;
}


LoadScenario(short s)
{
  char *name, *fname;

  if (CityFileName != NULL) {
    ckfree(CityFileName);
    CityFileName = NULL;
  }

  SetGameLevel(0);

  if ((s < 1) || (s > 8)) s = 1;

  switch (s) {
  case 1:
    name = "Dullsville";
    fname = "snro.111";
    ScenarioID = 1;
    CityTime = ((1900 - 1900) * 48) + 2;
    SetFunds(5000);
    break;
  case 2:
    name = "San Francisco";
    fname = "snro.222";
    ScenarioID = 2;
    CityTime = ((1906 - 1900) * 48) + 2;
    SetFunds(20000);
    break;
  case 3:
    name = "Hamburg";
    fname = "snro.333";
    ScenarioID = 3;
    CityTime = ((1944 - 1900) * 48) + 2;
    SetFunds(20000);
    break;
  case 4:
    name = "Bern";
    fname = "snro.444";
    ScenarioID = 4;
    CityTime = ((1965 - 1900) * 48) + 2;
    SetFunds(20000);
    break;
  case 5:
    name = "Tokyo";
    fname = "snro.555";
    ScenarioID = 5;
    CityTime = ((1957 - 1900) * 48) + 2;
    SetFunds(20000);
    break;
  case 6:
    name = "Detroit";
    fname = "snro.666";
    ScenarioID = 6;
    CityTime = ((1972 - 1900) * 48) + 2;
    SetFunds(20000);
    break;
  case 7:
    name = "Boston";
    fname = "snro.777";
    ScenarioID = 7;
    CityTime = ((2010 - 1900) * 48) + 2;
    SetFunds(20000);
    break;
  case 8:
    name = "Rio de Janeiro";
    fname = "snro.888";
    ScenarioID = 8;
    CityTime = ((2047 - 1900) * 48) + 2;
    SetFunds(20000);
    break;
  }

  setAnyCityName(name);
  //  sim_skips = sim_skip = 0;
  InvalidateMaps();
  InvalidateEditors();
  setSpeed(3);
  CityTax = 7;
  gettimeofday(&start_time, NULL);

  {
    unsigned int sz;
    const unsigned char *d = res_find(fname, &sz);
    if (d != NULL)
      _load_mem(d, sz);
  }

  InitWillStuff();
  InitFundingLevel();
  UpdateFunds();
  InvalidateEditors();
  InvalidateMaps();
  InitSimLoad = 1;
  DoInitialEval = 0;
  DoSimInit();
  DidLoadScenario();
  Kick();
}


DidLoadScenario()
{
  Eval("UIDidLoadScenario");
}


int LoadCity(char *filename)
{
  char *cp;
  char msg[256];

  if (loadFile(filename)) {
    if (CityFileName != NULL)
      ckfree(CityFileName);
    CityFileName = (char *)ckalloc(strlen(filename) + 1);
    strcpy(CityFileName, filename);

    if (cp = (char *)rindex(filename, '.'))
      *cp = 0;
#ifdef MSDOS
    if (cp = (char *)rindex(filename, '\\'))
#else
    if (cp = (char *)rindex(filename, '/'))
#endif
      cp++;
    else
      cp = filename;
    filename = (char *)ckalloc(strlen(cp) + 1);
    strcpy(filename, cp);
    setCityName(filename);
    gettimeofday(&start_time, NULL);

    InvalidateMaps();
    InvalidateEditors();
    DidLoadCity();
    return (1);
  } else {
    sprintf(msg, "Unable to load a city from the file named \"%s\". %s",
	    filename ? filename : "(null)",
	    errno ? strerror(errno) : "");
    DidntLoadCity(msg);
    return (0);
  }
}


/* Load one of the baked-in example cities (cities/*.cty) by basename, e.g.
 * "about.cty".  Mirrors LoadCity but reads from the embedded blob and leaves
 * CityFileName NULL, so Save / Save As prompt for a path -- an embedded city
 * is fully saveable to disk. */
int LoadEmbeddedCity(char *name)
{
  unsigned int sz;
  const unsigned char *d = city_find(name, &sz);
  char *nm, *cp;
  char msg[256];

  if ((d != NULL) && loadMem(d, sz)) {
    if (CityFileName != NULL) {
      ckfree(CityFileName);
      CityFileName = NULL;			/* force Save As (no disk path) */
    }

    /* persistent, writable copy of "<name>" minus its extension: setCityName
     * stores the pointer and rewrites it in place, so it must not be a stack
     * buffer nor the read-only table string. */
    nm = (char *)ckalloc(strlen(name) + 1);
    strcpy(nm, name);
    if (cp = (char *)rindex(nm, '.'))
      *cp = 0;
    setCityName(nm);
    gettimeofday(&start_time, NULL);

    InvalidateMaps();
    InvalidateEditors();
    DidLoadCity();
    return (1);
  } else {
    sprintf(msg, "Unable to load the built-in city \"%s\".",
	    name ? name : "(null)");
    DidntLoadCity(msg);
    return (0);
  }
}


DidLoadCity()
{
  Eval("UIDidLoadCity");
}


DidntLoadCity(char *msg)
{
  char buf[1024];
  sprintf(buf, "UIDidntLoadCity {%s}", msg);
  Eval(buf);
}


SaveCity()
{
  char msg[256];

  if (CityFileName == NULL) {
    DoSaveCityAs();
  } else {
    if (saveFile(CityFileName)) {
      DidSaveCity();
      return (1);			/* ncurses port: report success */
    } else {
      sprintf(msg, "Unable to save the city to the file named \"%s\". %s",
	      CityFileName ? CityFileName : "(null)",
	      errno ? strerror(errno) : "");
      DidntSaveCity(msg);
    }
  }
  return (0);
}


DoSaveCityAs()
{
  Eval("UISaveCityAs");
}


DidSaveCity()
{
  Eval("UIDidSaveCity");
}


DidntSaveCity(char *msg)
{
  char buf[1024];
  sprintf(buf, "UIDidntSaveCity {%s}", msg);
  Eval(buf);
}


SaveCityAs(char *filename)
{
  char msg[256];
  char *cp;

  if (CityFileName != NULL)
    ckfree(CityFileName);
  CityFileName = (char *)ckalloc(strlen(filename) + 1);
  strcpy(CityFileName, filename);

  if (saveFile(CityFileName)) {
    if (cp = (char *)rindex(filename, '.'))
      *cp = 0;
#ifdef MSDOS
    if (cp = (char *)rindex(filename, '\\'))
#else
    if (cp = (char *)rindex(filename, '/'))
#endif
      cp++;
    else
      cp = filename;
    filename = (char *)ckalloc(strlen(cp) + 1);
    strcpy(filename, cp);
    setCityName(cp);
    DidSaveCity();
    return (1);				/* ncurses port: report success */
  } else {
    sprintf(msg, "Unable to save the city to the file named \"%s\". %s",
	    CityFileName ? CityFileName : "(null)",
	    errno ? strerror(errno) : "");
    DidntSaveCity(msg);
  }
  return (0);
}


