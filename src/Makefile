# Makefile -- ncurses Micropolis (terminal port), self-contained.
#
# Portability: the engine is K&R / ANSI-C89 with implicit-int returns.  Modern
# compilers need the MODERN flags below; on a real old Unix cc build with:
#     make MODERN=
MODERN = -std=gnu89 -Wno-implicit-int -Wno-implicit-function-declaration \
         -Wno-return-type -Wno-return-mismatch -Wno-int-conversion \
         -Wno-incompatible-pointer-types
CC = cc
CFLAGS = -O $(MODERN) -Iheaders $(AA_CFLAGS) -w
LIBS = -lm
# The unicode/emoji gfx mode (-gfx unicode) needs a wide-capable curses.
# macOS system -lcurses already is; on Linux build with: make CURSES=-lncursesw
CURSES = -lcurses
# The aalib gfx mode (-gfx aa) is OFF by default -- libaa is a rare bird.
# Build it in with:
#     make aalib                        (libaa in the default search path)
#     make aalib AA_PREFIX=/opt/homebrew   (or wherever libaa lives)
# or set the raw knobs yourself: make AA_CFLAGS=-DHAVE_AALIB AA_LIBS=-laa
AA_CFLAGS =
AA_LIBS =
AA_PREFIX = /usr/local

ENGINE_OBJS = \
	s_alloc.o s_disast.o s_eval.o s_fileio.o s_gen.o s_init.o s_msg.o \
	s_power.o s_scan.o s_sim.o s_traf.o s_zone.o \
	g_ani.o w_con.o w_tool.o w_sprite.o w_budget.o w_eval.o w_keys.o \
	w_resrc.o w_update.o w_stubs.o w_util.o w_sound.o rand.o random.o

NC_OBJS = nc_main.o nc_render.o nc_gfx.o nc_aa.o nc_input.o nc_status.o \
	nc_minimap.o nc_menu.o nc_dialogs.o nc_stubs.o

all: ttycity

ttycity: $(NC_OBJS) $(ENGINE_OBJS)
	$(CC) $(CFLAGS) $(NC_OBJS) $(ENGINE_OBJS) $(CURSES) $(AA_LIBS) $(LIBS) -o ttycity

# aalib-enabled build (recompiles nc_aa.o with the mode switched on)
aalib:
	rm -f nc_aa.o ttycity
	$(MAKE) AA_CFLAGS="-DHAVE_AALIB -I$(AA_PREFIX)/include" \
		AA_LIBS="-L$(AA_PREFIX)/lib -laa"

.c.o:
	$(CC) $(CFLAGS) -c $< -o $@

# GetResource() pulls the baked-in resources from this generated header.
w_resrc.o: headers/res_data.h

# Regenerate headers/res_data.h from ../res and ../cities (run only when those
# change; the generated header is committed so a normal build never invokes it).
genres: genres.c
	$(CC) -O -o genres genres.c

resdata: genres
	./genres headers/res_data.h res=../res city=../cities

clean:
	rm -f *.o ttycity genres core

.PHONY: all clean resdata aalib
