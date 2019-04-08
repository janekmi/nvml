/*
 * Copyright 2019, Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *
 *     * Neither the name of the copyright holder nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * pmseti2.c -- Search for Extraterrestrial Intelligence in pminvaders2
 *
 * snooping the pminvaders2 game state
 */

#include <stddef.h>
#ifdef __FreeBSD__
#include <ncurses/ncurses.h>	/* Need pkg, not system, version */
#else
#include <ncurses.h>
#endif
#include <unistd.h>
#include <time.h>
#include <stdlib.h>
#include <sys/stat.h>

#include <libpmem.h>
#include <libpmemobj.h>


/*
 * Layout definition
 */
POBJ_LAYOUT_BEGIN(pminvaders2);
POBJ_LAYOUT_ROOT(pminvaders2, struct root);
POBJ_LAYOUT_TOID(pminvaders2, struct state);
POBJ_LAYOUT_TOID(pminvaders2, struct alien);
POBJ_LAYOUT_TOID(pminvaders2, struct player);
POBJ_LAYOUT_TOID(pminvaders2, struct bullet);
POBJ_LAYOUT_TOID(pminvaders2, struct star);
POBJ_LAYOUT_END(pminvaders2);


#define POOL_SIZE	(100 * 1024 * 1024) /* 100 megabytes */

#define GAME_WIDTH	50
#define GAME_HEIGHT	25

#define ALIENS_ROW	4
#define ALIENS_COL	18

#define RRAND(min, max)	(rand() % ((max) - (min) + 1) + (min))

#define STEP 50

#define PLAYER_Y (GAME_HEIGHT - 1)

#define MAX_GSTATE_TIMER 10000
#define MIN_GSTATE_TIMER 5000

#define MAX_ALIEN_TIMER	1000
#define MAX_PLAYER_TIMER 1000
#define MAX_BULLET_TIMER 500
#define MAX_STAR1_TIMER 200
#define MAX_STAR2_TIMER 100


enum game_event {
	EVENT_NOP,
	EVENT_BOUNCE,
	EVENT_PLAYER_KILLED,
	EVENT_ALIENS_KILLED
};

enum colors {
	C_UNKNOWN,
	C_PLAYER,
	C_ALIEN,
	C_BULLET,
	C_STAR,
	C_INTRO
};

struct state {
	unsigned timer;
	int score;
	unsigned high_score;
	int level;
	int new_level;
	int dx;
	int dy;
};

struct player {
	unsigned x;
	unsigned timer;
};

struct alien {
	unsigned x;
	unsigned y;
	TOID(struct alien) prev;
	TOID(struct alien) next;
};

struct star {
	unsigned x;
	unsigned y;
	int c;
	unsigned timer;
	TOID(struct star) prev;
	TOID(struct star) next;
};

struct bullet {
	unsigned x;
	unsigned y;
	unsigned timer;
	TOID(struct bullet) prev;
	TOID(struct bullet) next;
};

struct root {
	TOID(struct state) state;
	TOID(struct player) player;
	TOID(struct alien) aliens;
	TOID(struct bullet) bullets;
	TOID(struct star) stars;
};

/*
 * draw_alien -- draw an alien
 */
static int
draw_alien(const struct alien *a)
{
	if (a == NULL)
		return 1;
	mvaddch(a->y, a->x, ACS_DIAMOND | COLOR_PAIR(C_ALIEN));
	return 0;
}

/*
 * draw_player -- draw a player
 */
static int
draw_player(const struct player *p)
{
	if (p == NULL)
		return 1;
	mvaddch(PLAYER_Y, p->x, ACS_DIAMOND | COLOR_PAIR(C_PLAYER));
	return 0;
}

/*
 * draw_bullet -- draw a bullet
 */
static int
draw_bullet(const struct bullet *b)
{
	if (b == NULL)
		return 1;
	mvaddch(b->y, b->x, ACS_BULLET | COLOR_PAIR(C_BULLET));
	return 0;
}

/*
 * draw_score -- draw the game score and the global highest score
 */
static int
draw_score(const struct state *s)
{
	if (s == NULL)
		return 1;
	mvprintw(1, 1, "Level: %u    Score: %u | %u\n",
		s->level, s->score, s->high_score);
	return 0;
}

/*
 * draw_border -- draw a frame around the map
 */
static void
draw_border(void)
{
	for (int x = 0; x <= GAME_WIDTH; ++x) {
		mvaddch(0, x, ACS_HLINE);
		mvaddch(GAME_HEIGHT, x, ACS_HLINE);
	}

	for (int y = 0; y <= GAME_HEIGHT; ++y) {
		mvaddch(y, 0, ACS_VLINE);
		mvaddch(y, GAME_WIDTH, ACS_VLINE);
	}

	mvaddch(0, 0, ACS_ULCORNER);
	mvaddch(GAME_HEIGHT, 0, ACS_LLCORNER);
	mvaddch(0, GAME_WIDTH, ACS_URCORNER);
	mvaddch(GAME_HEIGHT, GAME_WIDTH, ACS_LRCORNER);
}

/*
 * process_aliens -- process movement of the aliens and game events
 */
static void
process_aliens(PMEMobjpool *pop, TOID(struct root) r)
{
	TOID(struct alien) a = D_RO(r)->aliens;
	while (!TOID_IS_NULL(a)) {
		const struct alien *ap = D_RO(a);
		if (draw_alien(ap))
			break;
		a = ap->next;
	}
}

/*
 * process_bullets -- process bullets movement and collision
 */
static void
process_bullets(PMEMobjpool *pop, TOID(struct root) r, const struct state *sp)
{
	TOID(struct bullet) b = D_RO(r)->bullets;
	while (!TOID_IS_NULL(b)) {
		const struct bullet *bptr = D_RO(b);
		if (!bptr)
			break;
		TOID(struct bullet) bn = bptr->next;
		if (draw_bullet(bptr))
			break;
		b = bn;
	}
}

/*
 * process_player -- handle player actions
 */
static void
process_player(PMEMobjpool *pop, TOID(struct root) r, int input)
{
	TOID(struct player) p = D_RO(r)->player;
	draw_player(D_RO(p));
}

/*
 * game_init -- create and initialize game state and the player
 */
static TOID(struct root)
game_init(PMEMobjpool *pop)
{
	TOID(struct root) r = POBJ_ROOT(pop, struct root);
	return r;
}

/*
 * game_loop -- process drawing and logic of the game
 */
static int
game_loop(PMEMobjpool *pop, TOID(struct root) r)
{
	int input = getch();

	TOID(struct state) s = D_RO(r)->state;
	const struct state *sp = D_RO(s);

	erase();
	draw_score(sp);
	draw_border();

	process_aliens(pop, r);
	process_bullets(pop, r, sp);
	process_player(pop, r, input);

	usleep(STEP);
	refresh();

	if (input == 'q')
		return -1;
	else
		return 0;
}

static PMEMobjpool*
do_open(const char *path)
{
	PMEMobjpool *pop = pmemobj_create(path, POBJ_LAYOUT_NAME(pminvaders2),
			0, S_IRUSR | S_IWUSR);

	if (pop)
		return pop;

	if (errno != EEXIST) {
		fprintf(stderr, "pmemobj_create: %s", pmemobj_errormsg());
		return NULL;
	}

	pop = pmemobj_open(path, POBJ_LAYOUT_NAME(pminvaders2));

	if (pop)
		return pop;

	fprintf(stderr, "pmemobj_open: %s", pmemobj_errormsg());
	return NULL;
}

int
main(int argc, char *argv[])
{
	static PMEMobjpool *pop;
	int in;

	if (argc != 2)
		exit(1);

	srand(time(NULL));

	pop = do_open(argv[1]);
	if (!pop)
		return 1;

	initscr();
	start_color();
	init_pair(C_PLAYER, COLOR_GREEN, COLOR_BLACK);
	init_pair(C_ALIEN, COLOR_RED, COLOR_BLACK);
	init_pair(C_BULLET, COLOR_YELLOW, COLOR_BLACK);
	init_pair(C_STAR, COLOR_WHITE, COLOR_BLACK);
	init_pair(C_INTRO, COLOR_BLUE, COLOR_BLACK);
	nodelay(stdscr, true);
	curs_set(0);
	keypad(stdscr, true);

	TOID(struct root) r = game_init(pop);

	while ((in = game_loop(pop, r)) == 0)
		;

	endwin();

	pmemobj_close(pop);

	return 0;
}
