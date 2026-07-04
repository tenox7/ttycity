#include "sim.h"
#include <string.h>
#include "res_data.h"		/* baked-in stri.* / snro.* resources */


char *HomeDir, *ResourceDir, *KeyDir, *HostName;

struct Resource *Resources = NULL;

/* Find an embedded resource by exact basename ("stri.301", "snro.111"). */
const unsigned char *
res_find(const char *name, unsigned int *size)
{
  int i;
  for (i = 0; i < RES_COUNT; i++) {
    if (strcmp(res_table[i].name, name) == 0) {
      if (size) *size = res_table[i].size;
      return res_table[i].data;
    }
  }
  if (size) *size = 0;
  return NULL;
}

/* Find an embedded example city by exact basename ("about.cty"). */
const unsigned char *
city_find(const char *name, unsigned int *size)
{
  int i;
  for (i = 0; i < CITY_COUNT; i++) {
    if (strcmp(city_table[i].name, name) == 0) {
      if (size) *size = city_table[i].size;
      return city_table[i].data;
    }
  }
  if (size) *size = 0;
  return NULL;
}

/* Enumerate the embedded cities (for the built-in load picker). */
int
EmbeddedCityCount(void)
{
  return CITY_COUNT;
}

const char *
EmbeddedCityName(int i)
{
  return (i >= 0 && i < CITY_COUNT) ? city_table[i].name : NULL;
}

struct StringTable {
  QUAD id;
  int lines;
  char **strings;
  struct StringTable *next;
} *StringTables;


Handle GetResource(char *name, QUAD id)
{
  struct Resource *r = Resources;
  char key[16];
  const unsigned char *data;
  unsigned int size;

  while (r != NULL) {
    if ((r->id == id) &&
	(strncmp(r->name, name, 4) == 0)) {
      return ((Handle)&r->buf);
    }
    r = r->next;
  }

  /* resources are baked into the binary (res_data.h); look up by basename */
  sprintf(key, "%c%c%c%c.%d", name[0], name[1], name[2], name[3], (int)id);
  data = res_find(key, &size);
  if ((data == NULL) || (size == 0))
    return (NULL);		/* missing: caller (GetIndString) degrades */

  r = (struct Resource *)ckalloc(sizeof(struct Resource));
  r->name[0] = name[0];
  r->name[1] = name[1];
  r->name[2] = name[2];
  r->name[3] = name[3];
  r->id = id;
  r->size = size;

  /* hand back a MUTABLE copy: GetIndString rewrites '\n' -> '\0' in place,
   * so we must never expose the const baked-in bytes directly. */
  r->buf = (char *)ckalloc(size);
  memcpy(r->buf, data, size);
  r->next = Resources; Resources = r;
  return ((Handle)&r->buf);
}


void
ReleaseResource(Handle r)
{
}


QUAD
ResourceSize(Handle h)
{
  struct Resource *r = (struct Resource *)h;

  return (r->size);
}


char *
ResourceName(Handle h)
{
  struct Resource *r = (struct Resource *)h;

  return (r->name);
}


QUAD
ResourceID(Handle h)
{
  struct Resource *r = (struct Resource *)h;

  return (r->id);
}


GetIndString(char *str, int id, short num)
{
  struct StringTable **tp, *st = NULL;
  Handle h;

  tp = &StringTables;

  while (*tp) {
    if ((*tp)->id == id) {
      st = *tp;
      break;
    }
    tp = &((*tp)->next);
  }
  if (!st) {
    QUAD i, lines, size;
    char *buf;

    st = (struct StringTable *)ckalloc(sizeof (struct StringTable));
    st->id = id;
    h = GetResource("stri", id);
    if (h == NULL) {		/* resource file missing: degrade, don't crash */
      ckfree((char *)st);
      strcpy(str, "");
      return;
    }
    size = ResourceSize(h);
    buf = (char *)*h;
    for (i=0, lines=0; i<size; i++)
      if (buf[i] == '\n') {
	buf[i] = 0;
	lines++;
      }
    st->lines = lines;
    st->strings = (char **)ckalloc(size * sizeof(char *));
    for (i=0; i<lines; i++) {
      st->strings[i] = buf;
      buf += strlen(buf) + 1;
    }
    st->next = StringTables;
    StringTables = st;
  }
  if ((num < 1) || (num > st->lines)) {
    strcpy(str, "");		/* ncurses port: silent (was a stderr print) */
  } {
    strcpy(str, st->strings[num-1]);
  }
}
