/* $Id$ */
/*
 * Copyright (c) 2010 Dimitri Sokolyuk <demon@dim13.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <arpa/inet.h>			/* endian conversion */
#include <sys/time.h>			/* endian conversion */
#include <assert.h>
#include <err.h>
#include <signal.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "tekplot.h"

struct tri {				/* Format 0 (3D Coordinates) */
	int16_t	x;
	int16_t	y;
	int16_t	z;
	uint8_t	color;			/* Color Index */
	uint8_t	state;			/* State Byte */
} tri;

struct two {				/* Format 1 (2D Coordinates) */
	int16_t	x;
	int16_t	y;
	uint8_t	color;			/* Color Index */
	uint8_t	state;			/* State Byte */
} two;

struct color {				/* Format 2 (Color Index Palette) */
	uint8_t	r;
	uint8_t	g;
	uint8_t	b;
} color;

struct head {
	int8_t	magic[4];		/* Signature */
	int8_t	zero[3];		/* Not used, must be zero */
	uint8_t	format;			/* Format Type */
	int8_t	name[8];		/* Name */
	int8_t	company[8];		/* Company Name */
	uint16_t npoints;		/* Number of Entries in Data Section */
	uint16_t fnumner;		/* Current Frame Number */
	uint16_t nframes;		/* Total Number of Frames */
	uint8_t	shead;			/* Scanner Head */
	uint8_t	future;			/* Not used, must be zero */
} head;

struct coord {
	int	x;
	int	y;
	int	z;
	int	on;
	int	color;
	struct	coord *next;
};

struct ilda {
	int	format;
	struct	coord *coord;
	struct	ilda *next;
};

#ifndef __dead
#define __dead	__attribute__((noreturn))
#endif

int	dflag = 0;

struct ilda *grab(FILE *);
void drop(struct ilda *);
void settimer(int);
void usage(void);

struct ilda *
grab(FILE *fd)
{
	struct	ilda *ilda, **pilda, *il;
	struct	coord *coord, **pcoord, *c;
	int	i, n;
	int	maxx, maxy, maxz, minx, miny, minz, tmp;

	ilda = NULL;
	pilda = &ilda;

	maxx = maxy = maxz = 0;
	minx = miny = minz = 1<<12;

#define C(c)	((((htons(c)) + (1<<15)) % (1<<16)) >> 4)

/* find min and max */
#define R(p, c)							\
	do {							\
		if ((*p)->c > max##c)				\
			max##c = (*p)->c;			\
		if ((*p)->c < min##c)				\
			min##c = (*p)->c;			\
	} while (0)

	warnx("Read Vectors");

	while (fread(&head, sizeof(head), 1, fd) > 0) {
		if (memcmp(head.magic, "ILDA", sizeof(head.magic)))
			errx(-1, "bad magic");

		*pilda = calloc(1, sizeof(*ilda));
		assert(*pilda);
		(*pilda)->format = head.format;

		(*pilda)->coord = NULL;
		pcoord = &(*pilda)->coord;

		n = ntohs(head.npoints);

		switch (head.format) {
		case 0:		/* 3D Coordinates */
			for (i = 0; i < n; i++) {
				fread(&tri, sizeof(tri), 1, fd);
				*pcoord = calloc(1, sizeof(*coord));
				assert(*pcoord);
				(*pcoord)->x = C(tri.x);
				R(pcoord, x);
				(*pcoord)->y = C(tri.y);
				R(pcoord, y);
				(*pcoord)->z = C(tri.z);
				R(pcoord, z);
				(*pcoord)->on = tri.state;
				(*pcoord)->color = tri.color;
				pcoord = &(*pcoord)->next;
			}
			break;
		case 1:		/* 2D Coordinates */
			for (i = 0; i < n; i++) {
				fread(&two, sizeof(two), 1, fd);
				*pcoord = calloc(1, sizeof(*coord));
				assert(*pcoord);
				(*pcoord)->x = C(two.x);
				R(pcoord, x);
				(*pcoord)->y = C(two.y);
				R(pcoord, y);
				(*pcoord)->on = two.state;
				(*pcoord)->color = two.color;
				pcoord = &(*pcoord)->next;
			}
			break;
		case 2:		/* Color Index Palette */
			for (i = 0; i < head.npoints; i++) {
				fread(&color, sizeof(color), 1, fd);
				/* do nothing, just read ahead */
			}
			break;
		}

		pilda = &(*pilda)->next;
	}

#undef C
#undef R

/* normalize */
#define N(p, c, r, m)						\
	do {							\
		r = max##c - min##c;				\
		if (r)						\
			p->c = (p->c - min##c) * (m - 1) / r;	\
	} while (0)

	warnx("Normalize Points");

	for (il = ilda; il; il = il->next) {
		for (c = il->coord; c; c = c->next) {
			N(c, x, tmp, 4096);
			N(c, y, tmp, 3120);
			N(c, z, tmp, 1024);
		}
	}
#undef N
	warnx("Ready to Play");

	return ilda;
}

void
drop(struct ilda *ilda)
{
	struct	ilda *nilda;
	struct	coord *coord, *ncoord;

	for (; ilda; ilda = nilda) {
		nilda = ilda->next;
		for (coord = ilda->coord; coord; coord = ncoord) {
			ncoord = coord->next;
			free(coord);
		}
		free(ilda);
	}
}

void
catch(int signo)
{
	if (signo != SIGALRM)
		dflag = 1;
	return;
}

int
main(int argc, char **argv)
{
	struct	sigaction sa;
	struct	ilda *ilda, *i;
	struct	coord *c;
	int	on, ch;
	int	delay = 40000;		/* 25 fps */
	FILE	*input;

	while ((ch = getopt(argc, argv, "d:h")) != -1)
		switch (ch) {
		case 'd':
			delay = 1000 * atoi(optarg);
			break;
		case 'h':
		default:
			usage();
			/* NOTREACHED */
		}

	argc -= optind;
	argv += optind;

	if (!argc)
		usage();

	input = fopen(*argv, "r");
	if(!input)
		err(-1, "%s", *argv);

	ilda = grab(input);
	assert(ilda);

	fclose(input);

	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sa.sa_handler = catch;
	sigaction(SIGALRM, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGHUP, &sa, NULL);

	settimer(delay);

	inittek();
	for (i = ilda; !dflag && i; i = i->next) {
		page();
		/* first vector is always blank */
		for (on = 0, c = i->coord; c; c = c->next) {
			iplot(on, c->x, c->y);
			on = !c->color;
		}
		fflush(stdout);
		sigsuspend(&sa.sa_mask);
	}
	endtek();

	drop(ilda);

	return 0;
}

void
settimer(int usec)
{
	struct	itimerval itv;

	itv.it_value.tv_sec = 0;
	itv.it_value.tv_usec = usec;
	itv.it_interval = itv.it_value;

	setitimer(ITIMER_REAL, &itv, NULL);
}

__dead void
usage(void)
{
	extern	char *__progname;

	(void)fprintf(stderr, "usage: %s [-d msec] [ILDA]\n", __progname);

	exit(1);
}
