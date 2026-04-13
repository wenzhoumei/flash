/* See LICENSE file for copyright and license details. */
#include <errno.h>
#include <locale.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <X11/Xatom.h>
#include <X11/Xft/Xft.h>
#include <X11/XKBlib.h>
#include <X11/Xlib.h>

#define LEN(a) (sizeof(a) / sizeof((a)[0]))
#define LIMIT(x, a, b) ((x) = (x) < (a) ? (a) : (x) > (b) ? (b) : (x))

typedef struct Card Card;
typedef struct Deck Deck;

struct Card {
	char *q;
	char *a;
	Deck *deck;
	int state;
};

struct Deck {
	char *path;
	char **lines;
	Card **cards;
	size_t nlines, linecap;
	size_t ncards, cardcap;
	size_t actstart, actend;
	int succ, att;
};

typedef struct {
	Display *dpy;
	Window win;
	Visual *vis;
	Colormap cmap;
	Atom wmdeletewin;
	int scr, w, h, uw, uh;
} XWindow;

enum {
	SAVE_PARTIAL,
	SAVE_FAILED,
};

enum {
	CARD_NEW,
	CARD_OK,
	CARD_FAIL,
	CARD_SKIP,
};

#include "config.h"
#include "prompt.h"

static void die(const char *fmt, ...);
static void *ecalloc(size_t n, size_t size);
static void *erealloc(void *p, size_t size);
static char *estrdup(const char *s);
static void addcard(Deck *d, const char *s);
static void addline(Deck *d, const char *s);
static void cleanup(void);
static void draw(void);
static void back(void);
static XftFont *fitfont(const char *s, char ***lines, size_t *nlines, int *maxw);
static void freelines(char **lines, size_t nlines);
static int issep(const char *s);
static int savecard(const Card *c);
static void loaddeck(const char *path, int reset);
static void next(int ok);
static void skip(void);
static void resetdeck(Deck *d);
static void run(void);
static void save(void);
static void shuffle(void);
static int startswith(const char *s, const char *prefix);
static int textwidth(XftFont *font, const char *s);
static void timestamp(char *buf, size_t len);
static void usage(int status);
static void wraptext(const char *s, XftFont *font, char ***lines, size_t *nlines, int *maxw);
static void xinit(void);
static void xloadfonts(void);

static char *argv0;
static Card **cards;
static Deck *decks;
static size_t cardcount, cardcap, cardidx;
static size_t deckcount, deckcap;
static XWindow xw;
static GC gc;
static XftDraw *drawctx;
static XftColor fg, bg;
static XftFont *fonts[NUMFONTSCALES];
static int running = 1, flipped = 0, dosave = 1, seenanswer = 0, savemode;

void
die(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	if (argv0)
		fprintf(stderr, "%s: ", argv0);
	vfprintf(stderr, fmt, ap);
	if (fmt[0] && fmt[strlen(fmt) - 1] == ':')
		fprintf(stderr, " %s", strerror(errno));
	fputc('\n', stderr);
	va_end(ap);
	exit(1);
}

void *
ecalloc(size_t n, size_t size)
{
	void *p;

	if (!(p = calloc(n, size)))
		die("calloc:");
	return p;
}

void *
erealloc(void *p, size_t size)
{
	if (!(p = realloc(p, size)))
		die("realloc:");
	return p;
}

char *
estrdup(const char *s)
{
	char *p;

	if (!(p = strdup(s)))
		die("strdup:");
	return p;
}

int
startswith(const char *s, const char *prefix)
{
	return !strncmp(s, prefix, strlen(prefix));
}

void
usage(int status)
{
	FILE *fp = status ? stderr : stdout;

	fprintf(fp,
	        "usage: %s -h\n"
	        "       %s -p\n"
	        "       %s [-o] [-r] deck ...\n",
	        argv0, argv0, argv0);
	exit(status);
}

void
addline(Deck *d, const char *s)
{
	if (d->nlines == d->linecap) {
		d->linecap = d->linecap ? 2 * d->linecap : 128;
		d->lines = erealloc(d->lines, d->linecap * sizeof(*d->lines));
	}
	d->lines[d->nlines++] = estrdup(s);
}

void
addcard(Deck *d, const char *s)
{
	Card *c;
	char *p, *sep;

	p = estrdup(s);
	if (!(sep = strstr(p, ":::")))
		die("bad card in '%s': '%s'", d->path, s);
	*sep = '\0';
	c = ecalloc(1, sizeof(*c));
	c->q = p;
	c->a = estrdup(sep + 3);
	c->deck = d;

	if (cardcount == cardcap) {
		cardcap = cardcap ? 2 * cardcap : 128;
		cards = erealloc(cards, cardcap * sizeof(*cards));
	}
	if (d->ncards == d->cardcap) {
		d->cardcap = d->cardcap ? 2 * d->cardcap : 128;
		d->cards = erealloc(d->cards, d->cardcap * sizeof(*d->cards));
	}
	cards[cardcount++] = c;
	d->cards[d->ncards++] = c;
}

void
resetdeck(Deck *d)
{
	size_t cut, i;

	for (cut = 0; cut < d->nlines; cut++)
		if (issep(d->lines[cut]))
			break;
	if (cut == d->nlines)
		return;
	for (i = cut + 1; i < d->nlines; i++)
		free(d->lines[i]);
	d->nlines = cut + 1;
}

int
issep(const char *s)
{
	return !strcmp(s, "# SEP") || startswith(s, "# SEP ");
}

int
savecard(const Card *c)
{
	if (savemode == SAVE_PARTIAL)
		return c->state != CARD_OK;
	return c->state == CARD_FAIL;
}

void
loaddeck(const char *path, int reset)
{
	Deck *d;
	FILE *fp;
	char *line = NULL;
	size_t len = 0, i, prevsep, lastsep;
	ssize_t n;

	if (deckcount == deckcap) {
		deckcap = deckcap ? 2 * deckcap : 8;
		decks = erealloc(decks, deckcap * sizeof(*decks));
	}
	d = &decks[deckcount++];
	memset(d, 0, sizeof(*d));
	d->path = estrdup(path);

	if (!(fp = fopen(path, "r")))
		die("unable to open '%s' for reading:", path);

	while ((n = getline(&line, &len, fp)) >= 0) {
		while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
			line[--n] = '\0';
		if (!n)
			continue;
		addline(d, line);
	}

	free(line);
	fclose(fp);

	if (reset)
		resetdeck(d);
	if (!d->nlines)
		die("no active cards in '%s'", d->path);

	d->actstart = 0;
	d->actend = d->nlines;
	prevsep = lastsep = (size_t)-1;
	for (i = 0; i < d->nlines; i++) {
		if (!issep(d->lines[i]))
			continue;
		prevsep = lastsep;
		lastsep = i;
	}
	if (lastsep != (size_t)-1) {
		if (lastsep == d->nlines - 1) {
			d->actstart = prevsep == (size_t)-1 ? 0 : prevsep + 1;
			d->actend = lastsep;
		} else {
			d->actstart = lastsep + 1;
		}
	}
	if (d->actstart >= d->actend)
		die("no active cards in '%s'", d->path);

	for (i = d->actstart; i < d->actend; i++) {
		if (issep(d->lines[i]))
			die("malformed stack in '%s'", d->path);
		addcard(d, d->lines[i]);
	}
}

void
shuffle(void)
{
	size_t i, j;
	Card *tmp;

	srand((unsigned)time(NULL) ^ (unsigned)getpid());
	for (i = cardcount - 1; i > 0; i--) {
		j = (size_t)(rand() % (int)(i + 1));
		tmp = cards[i];
		cards[i] = cards[j];
		cards[j] = tmp;
	}
}

XftFont *
fitfont(const char *s, char ***lines, size_t *nlines, int *maxw)
{
	int i;

	for (i = NUMFONTSCALES - 1; i >= 0; i--) {
		wraptext(s, fonts[i], lines, nlines, maxw);
		if (*maxw <= xw.uw &&
		    (int)(*nlines * (fonts[i]->ascent + fonts[i]->descent)) <= xw.uh)
			return fonts[i];
		freelines(*lines, *nlines);
	}
	wraptext(s, fonts[0], lines, nlines, maxw);
	return fonts[0];
}

void
freelines(char **lines, size_t nlines)
{
	size_t i;

	for (i = 0; i < nlines; i++)
		free(lines[i]);
	free(lines);
}

int
textwidth(XftFont *font, const char *s)
{
	XGlyphInfo ext;

	XftTextExtentsUtf8(xw.dpy, font, (FcChar8 *)s, strlen(s), &ext);
	return ext.xOff;
}

void
wraptext(const char *s, XftFont *font, char ***lines, size_t *nlines, int *maxw)
{
	char *buf, *cur, *next, *tok, **out = NULL;
	char *saveptr = NULL;
	size_t cap = 0, len;

	*lines = NULL;
	*nlines = 0;
	*maxw = 0;
	if (!*s)
		return;

	len = strlen(s) + 1;
	buf = estrdup(s);
	cur = ecalloc(len, 1);
	next = ecalloc(len, 1);

	for (tok = strtok_r(buf, " \t", &saveptr); tok; tok = strtok_r(NULL, " \t", &saveptr)) {
		if (*cur)
			snprintf(next, len, "%s %s", cur, tok);
		else
			snprintf(next, len, "%s", tok);
		if (*cur && textwidth(font, next) > xw.uw) {
			if (*nlines == cap) {
				cap = cap ? 2 * cap : 8;
				out = erealloc(out, cap * sizeof(*out));
			}
			out[(*nlines)++] = estrdup(cur);
			if (textwidth(font, cur) > *maxw)
				*maxw = textwidth(font, cur);
			snprintf(cur, len, "%s", tok);
		} else {
			snprintf(cur, len, "%s", next);
		}
	}
	if (*cur) {
		if (*nlines == cap) {
			cap = cap ? 2 * cap : 8;
			out = erealloc(out, cap * sizeof(*out));
		}
		out[(*nlines)++] = estrdup(cur);
		if (textwidth(font, cur) > *maxw)
			*maxw = textwidth(font, cur);
	}

	free(next);
	free(cur);
	free(buf);
	*lines = out;
}

void
draw(void)
{
	char title[256];
	const char *s = flipped ? cards[cardidx]->a : cards[cardidx]->q;
	char **lines = NULL;
	size_t i, nlines = 0;
	int blockh, fh, maxw = 0, x, y;
	XftFont *font = fitfont(s, &lines, &nlines, &maxw);
	fh = font->ascent + font->descent;

	snprintf(title, sizeof(title), "flash [%zu/%zu]%s",
	         cardidx + 1, cardcount, flipped ? " answer" : "");
	XStoreName(xw.dpy, xw.win, title);

	XSetForeground(xw.dpy, gc, bg.pixel);
	XFillRectangle(xw.dpy, xw.win, gc, 0, 0, xw.w, xw.h);
	blockh = nlines * fh;
	y = (xw.h - blockh) / 2 + font->ascent;
	for (i = 0; i < nlines; i++) {
		x = (xw.w - textwidth(font, lines[i])) / 2;
		XftDrawStringUtf8(drawctx, &fg, font, x, y,
		                  (FcChar8 *)lines[i], strlen(lines[i]));
		y += fh;
	}
	freelines(lines, nlines);
	XFlush(xw.dpy);
}

void
back(void)
{
	Card *c;

	if (!cardidx)
		return;
	c = cards[cardidx - 1];
	if (c->state == CARD_OK) {
		c->deck->succ--;
		c->deck->att--;
	} else if (c->state == CARD_FAIL) {
		c->deck->att--;
	} else if (c->state != CARD_SKIP) {
		return;
	}
	c->state = CARD_NEW;
	cardidx--;
	flipped = seenanswer = 0;
	draw();
}

void
next(int ok)
{
	cards[cardidx]->state = ok ? CARD_OK : CARD_FAIL;
	cards[cardidx]->deck->att++;
	if (ok)
		cards[cardidx]->deck->succ++;
	flipped = seenanswer = 0;
	if (++cardidx >= cardcount)
		running = 0;
	else
		draw();
}

void
skip(void)
{
	cards[cardidx]->state = CARD_SKIP;
	flipped = seenanswer = 0;
	if (++cardidx >= cardcount)
		running = 0;
	else
		draw();
}

void
timestamp(char *buf, size_t len)
{
	time_t now;
	struct tm tm;

	now = time(NULL);
	if (!localtime_r(&now, &tm))
		die("localtime_r:");
	if (!strftime(buf, len, "%Y-%m-%dT%H:%M:%S", &tm))
		die("strftime:");
}

void
save(void)
{
	char stamp[64];
	char meta[96];
	FILE *fp;
	Deck *d;
	size_t i, j, left;

	timestamp(stamp, sizeof(stamp));
	for (i = 0; i < deckcount; i++) {
		d = &decks[i];
		if (!d->att)
			continue;
		left = 0;
		for (j = 0; j < d->ncards; j++)
			if (savecard(d->cards[j]))
				left++;
		if (d->att == (int)d->ncards)
			snprintf(meta, sizeof(meta), "%d/%d", d->succ, d->att);
		else
			snprintf(meta, sizeof(meta), "%d/%d/%zu", d->succ, d->att, d->ncards);

		if (!(fp = fopen(d->path, "w")))
			die("unable to open '%s' for writing:", d->path);
		if (!left) {
			if (!d->actstart) {
				for (j = 0; j < d->actend; j++)
					fprintf(fp, "%s\n", d->lines[j]);
				fprintf(fp, "# SEP %s %s\n", stamp, meta);
			} else {
				for (j = 0; j < d->actstart; j++)
					fprintf(fp, "%s\n", d->lines[j]);
			}
			fclose(fp);
			continue;
		}
		for (j = 0; j < d->actend; j++)
			fprintf(fp, "%s\n", d->lines[j]);
		fprintf(fp, "# SEP %s %s\n", stamp, meta);
		for (j = 0; j < d->ncards; j++) {
			if (!savecard(d->cards[j]))
				continue;
			fprintf(fp, "%s:::%s\n", d->cards[j]->q, d->cards[j]->a);
		}
		fprintf(fp, "# SEP\n");
		fclose(fp);
	}
}

void
xloadfonts(void)
{
	int i, j;
	char spec[256];

	for (i = 0; i < NUMFONTSCALES; i++) {
		for (j = 0; j < (int)LEN(fontfallbacks); j++) {
			snprintf(spec, sizeof(spec), "%s:size=%d", fontfallbacks[j], FONTSZ(i));
			if ((fonts[i] = XftFontOpenName(xw.dpy, xw.scr, spec)))
				break;
		}
		if (!fonts[i])
			die("unable to load font size %d", FONTSZ(i));
	}
}

void
xinit(void)
{
	if (!(xw.dpy = XOpenDisplay(NULL)))
		die("unable to open display");
	xw.scr = DefaultScreen(xw.dpy);
	xw.vis = DefaultVisual(xw.dpy, xw.scr);
	xw.cmap = DefaultColormap(xw.dpy, xw.scr);
	xw.w = DisplayWidth(xw.dpy, xw.scr);
	xw.h = DisplayHeight(xw.dpy, xw.scr);
	xw.uw = usablewidth * xw.w;
	xw.uh = usableheight * xw.h;
	xw.win = XCreateSimpleWindow(xw.dpy, RootWindow(xw.dpy, xw.scr),
	                             0, 0, xw.w, xw.h, 0,
	                             BlackPixel(xw.dpy, xw.scr),
	                             WhitePixel(xw.dpy, xw.scr));
	XSelectInput(xw.dpy, xw.win, KeyPressMask | ExposureMask | StructureNotifyMask);
	xw.wmdeletewin = XInternAtom(xw.dpy, "WM_DELETE_WINDOW", False);
	XSetWMProtocols(xw.dpy, xw.win, &xw.wmdeletewin, 1);
	if (!XftColorAllocName(xw.dpy, xw.vis, xw.cmap, fgcolor, &fg) ||
	    !XftColorAllocName(xw.dpy, xw.vis, xw.cmap, bgcolor, &bg))
		die("unable to allocate colors");
	gc = XCreateGC(xw.dpy, xw.win, 0, NULL);
	drawctx = XftDrawCreate(xw.dpy, xw.win, xw.vis, xw.cmap);
	xloadfonts();
	XMapWindow(xw.dpy, xw.win);
	XSync(xw.dpy, False);
}

void
run(void)
{
	XEvent ev;
	KeySym sym;

	for (;;) {
		XNextEvent(xw.dpy, &ev);
		if (ev.type == MapNotify)
			break;
		if (ev.type == ConfigureNotify) {
			xw.w = ev.xconfigure.width;
			xw.h = ev.xconfigure.height;
			xw.uw = usablewidth * xw.w;
			xw.uh = usableheight * xw.h;
		}
	}

	draw();
	while (running) {
		XNextEvent(xw.dpy, &ev);
		if (ev.type == Expose && ev.xexpose.count == 0) {
			draw();
		} else if (ev.type == ConfigureNotify) {
			xw.w = ev.xconfigure.width;
			xw.h = ev.xconfigure.height;
			xw.uw = usablewidth * xw.w;
			xw.uh = usableheight * xw.h;
			draw();
		} else if (ev.type == ClientMessage) {
			if ((Atom)ev.xclient.data.l[0] == xw.wmdeletewin)
				running = 0;
		} else if (ev.type == KeyPress) {
			sym = XkbKeycodeToKeysym(xw.dpy, (KeyCode)ev.xkey.keycode, 0, 0);
			if (sym == XK_Escape) {
				dosave = 0;
				running = 0;
			} else if (sym == XK_p) {
				savemode = SAVE_PARTIAL;
				running = 0;
			} else if (sym == XK_space) {
				if (!flipped)
					seenanswer = 1;
				flipped = !flipped;
				draw();
			} else if (sym == XK_j && seenanswer) {
				next(1);
			} else if (sym == XK_k && seenanswer) {
				next(0);
			} else if (sym == XK_b) {
				back();
			} else if (sym == XK_n) {
				skip();
			} else if (sym == XK_x) {
				savemode = SAVE_FAILED;
				running = 0;
			}
		}
	}
}

void
cleanup(void)
{
	size_t i, j;

	for (i = 0; i < cardcount; i++) {
		free(cards[i]->q);
		free(cards[i]->a);
		free(cards[i]);
	}
	for (i = 0; i < deckcount; i++) {
		for (j = 0; j < decks[i].nlines; j++)
			free(decks[i].lines[j]);
		free(decks[i].lines);
		free(decks[i].cards);
		free(decks[i].path);
	}
	free(cards);
	free(decks);

	for (i = 0; i < NUMFONTSCALES; i++)
		if (fonts[i])
			XftFontClose(xw.dpy, fonts[i]);
	if (drawctx)
		XftDrawDestroy(drawctx);
	if (gc)
		XFreeGC(xw.dpy, gc);
	if (xw.dpy) {
		XDestroyWindow(xw.dpy, xw.win);
		XCloseDisplay(xw.dpy);
	}
}

int
main(int argc, char *argv[])
{
	int argi, i, ndecks, ordered, reset;

	setlocale(LC_CTYPE, "");
	argv0 = argv[0];
	if (argc < 2)
		usage(1);

	reset = 0;
	ordered = 0;
	savemode = closemode;
	for (argi = 1; argi < argc && argv[argi][0] == '-' && argv[argi][1]; argi++) {
		if (!strcmp(argv[argi], "-h")) {
			usage(0);
		} else if (!strcmp(argv[argi], "-p")) {
			fputs(prompt, stdout);
			return 0;
		} else if (!strcmp(argv[argi], "-o")) {
			ordered = 1;
		} else if (!strcmp(argv[argi], "-r")) {
			reset = 1;
		} else {
			usage(1);
		}
	}
	if (argi >= argc)
		usage(1);

	ndecks = argc - argi;

	for (i = 0; i < ndecks; i++)
		loaddeck(argv[argi + i], reset);
	if (!cardcount)
		die("no cards loaded");

	if (!ordered)
		shuffle();
	xinit();
	run();
	if (dosave)
		save();
	cleanup();
	return 0;
}
