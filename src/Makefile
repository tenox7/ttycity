# Makefile -- ncurses Micropolis (terminal port), self-contained.
#
# Portability: the engine is K&R / ANSI-C89 with implicit-int returns.  Modern
# compilers need the MODERN flags below; on a real old Unix cc build with:
#     make MODERN=
MODERN = -std=gnu89 -Wno-implicit-int -Wno-implicit-function-declaration \
         -Wno-return-type -Wno-return-mismatch -Wno-int-conversion \
         -Wno-incompatible-pointer-types
CC = cc
CFLAGS = -O $(MODERN) -Iheaders -w
LIBS = -lm
CURSES = -lcurses

ENGINE_OBJS = \
	s_alloc.o s_disast.o s_eval.o s_fileio.o s_gen.o s_init.o s_msg.o \
	s_power.o s_scan.o s_sim.o s_traf.o s_zone.o \
	g_ani.o w_con.o w_tool.o w_sprite.o w_budget.o w_eval.o w_keys.o \
	w_resrc.o w_update.o w_stubs.o w_util.o w_sound.o rand.o random.o

NC_OBJS = nc_main.o nc_render.o nc_input.o nc_status.o nc_minimap.o \
	nc_menu.o nc_dialogs.o nc_stubs.o

all: ttycity

ttycity: $(NC_OBJS) $(ENGINE_OBJS)
	$(CC) $(CFLAGS) $(NC_OBJS) $(ENGINE_OBJS) $(CURSES) $(LIBS) -o ttycity

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

.PHONY: all clean resdata
