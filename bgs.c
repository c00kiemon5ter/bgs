/* See LICENSE file for copyright and license details.
 *
 * To understand bgs , start reading main().
 */
#include <stdlib.h>
#include <stdio.h>
#include <X11/Xlib.h>
#include <Imlib2.h>
#ifdef XINERAMA
#include <X11/extensions/Xinerama.h>
#endif

/* macros */
#define MIN(a, b)       ((a) < (b) ? (a) : (b))
#define MAX(a, b)       ((a) > (b) ? (a) : (b))

/* typedefs */
typedef struct {
	int x, y, w, h;
} Monitor;

/**
 * background modes
 *
 * SCALE:   scale image to fit screen
 * CENTER:  center image instead of rescale
 * STRETCH: stretch image to fill screen losing aspect ratio
 */
enum { SCALE, CENTER, STRETCH, };

/* function declarations */
static void cleanup(void);		/* frees images before exit. */
static void die(const char *errstr);	/* prints errstr to strerr and exits. */
static void drawbg(void);		/* draws background to root. */
static void run(void);			/* main loop */
static void setup(char *paths[], int c);/* sets up imlib and X */
static void updategeom(void);		/* updates screen and/or Xinerama
					   dimensions */

/**
 * global variables
 *
 * N         max number of monitors/background images
 * scrn      geometry of the screen
 * mode      the background mode
 * nmonitor  amount of monitors
 * nimage    available background images
 */
#define N 8
static Monitor monitors[N];
static Imlib_Image images[N];
static Monitor scrn;
static Bool running = False;
static Display *dpy;
static unsigned int mode = SCALE;
static int nmonitor, nimage;

/* function implementations */
void
cleanup(void) {
	for(int i = 0; i < nimage; i++) {
		imlib_context_set_image(images[i]);
		imlib_free_image_and_decache();
	}
}

void
die(const char *errstr) {
	fprintf(stderr, "bgs: %s\n", errstr);
	exit(EXIT_FAILURE);
}

void
drawbg(void) {
	int w, h, nx, ny, nh, nw;
	double factor;
	Imlib_Image tmpimg, buffer;

	const int screen = DefaultScreen(dpy);
	const Window root = RootWindow(dpy, screen);
	const Pixmap pm = XCreatePixmap(dpy, root, scrn.w, scrn.h,
			DefaultDepth(dpy, DefaultScreen(dpy)));

	if(!(buffer = imlib_create_image(scrn.w, scrn.h)))
		die("error: cannot allocate buffer.");

	for(int i = 0; i < nmonitor; i++) {
		imlib_context_set_image(images[i % nimage]);

		/* get a copy to work on */
		if(!(tmpimg = imlib_clone_image()))
			die("error: cannot clone image.");

		/* if necessery rotate the image */
		imlib_context_set_image(tmpimg);
		w = imlib_image_get_width();
		h = imlib_image_get_height();
		if((monitors[i].w > monitors[i].h && w < h) ||
				(monitors[i].w < monitors[i].h && w > h)) {
			imlib_image_orientate(1);
			if (w != h) {
				int tmp = w;
				w = h;
				h = tmp;
			}
		}

		imlib_context_set_image(buffer);
		switch (mode) {
			case CENTER:
				nx = monitors[i].x + (monitors[i].w - w) / 2;
				ny = monitors[i].y + (monitors[i].h - h) / 2;
				imlib_blend_image_onto_image(tmpimg, 0,
							0, 0, w, h,
							nx, ny, w, h);
				break;
			case STRETCH:
				imlib_blend_image_onto_image_skewed(tmpimg, 0,
						0, 0, w, h, monitors[i].x,
						monitors[i].y, monitors[i].w,
						0, 0, monitors[i].h);
				break;
			case SCALE:
				factor = MAX((double)w / monitors[i].w,
						(double)h / monitors[i].h);
				nw = w / factor;
				nh = h / factor;
				nx = monitors[i].x + (monitors[i].w - nw) / 2;
				ny = monitors[i].y + (monitors[i].h - nh) / 2;
				imlib_blend_image_onto_image(tmpimg, 0, 0, 0,
							w, h, nx, ny, nw, nh);
			default:
				break;
		}

		imlib_context_set_image(tmpimg);
		imlib_free_image();
	}
	imlib_context_set_image(buffer);
	imlib_context_set_drawable(root);
	imlib_render_image_on_drawable(0, 0);
	imlib_context_set_drawable(pm);
	imlib_render_image_on_drawable(0, 0);
	XSetWindowBackgroundPixmap(dpy, root, pm);
	imlib_context_set_image(buffer);
	imlib_free_image_and_decache();
	XFreePixmap(dpy, pm);
}

void
run(void) {
	XEvent ev;

	updategeom();
	drawbg();

	while(running) {
		XNextEvent(dpy, &ev);
		if(ev.type == ConfigureNotify) {
			scrn.w = ev.xconfigure.width;
			scrn.h = ev.xconfigure.height;
			imlib_flush_loaders();
		}
		updategeom();
		drawbg();
		imlib_flush_loaders();
	}
}

void
setup(char *paths[], int c) {
	/* Loading images */
	for(int i = nimage = 0; i < c && i < N; i++) {
		if((images[nimage] = imlib_load_image_without_cache(paths[i])))
			nimage++;
		else
			fprintf(stderr, "warning: cannot load file `%s`."
					"ignoring.\n", paths[nimage]);
	}
	if(nimage == 0)
		die("error: no image to draw.");

	/* set up X */
	const int screen = DefaultScreen(dpy);
	const Window root = RootWindow(dpy, screen);

	Visual * const vis = DefaultVisual(dpy, screen);
	const Colormap cm = DefaultColormap(dpy, screen);
	XSelectInput(dpy, root, StructureNotifyMask);

	scrn.x = scrn.y = 0;
	scrn.w = DisplayWidth(dpy, screen);
	scrn.h = DisplayHeight(dpy, screen);

	/* set up Imlib */
	imlib_context_set_display(dpy);
	imlib_context_set_visual(vis);
	imlib_context_set_colormap(cm);
}

void
updategeom(void) {
#ifdef XINERAMA
	XineramaScreenInfo *info = XineramaQueryScreens(dpy, &nmonitor);
#else
	struct { int x_org, y_org, width, height; } info[(nmonitor = 1)];
	info[0].x_org  = scrn.x;
	info[0].y_org  = scrn.y;
	info[0].width  = scrn.w;
	info[0].height = scrn.h;
#endif

	nmonitor = MIN(nmonitor, N);
	for(int i = 0; i < nmonitor; i++) {
		monitors[i].x = info[i].x_org;
		monitors[i].y = info[i].y_org;
		monitors[i].w = info[i].width;
		monitors[i].h = info[i].height;
	}

#ifdef XINERAMA
	XFree(info);
#endif

	if (!nmonitor)
		die("error: no monitors to configure");
}

int
main(int argc, char *argv[]) {
	int i;

	for(i = 1; i < argc && argv[i][0] == '-' && argv[i][1] != '\0' &&
			argv[i][1] != '-' && argv[i][2] == '\0'; i++)
		switch(argv[i][1]) {
		case 'c':
			mode = CENTER; break;
		case 's':
			mode = STRETCH; break;
		case 'x':
			running = True; break;
		case 'v':
			die("bgs-"VERSION", © 2010 bgs engineers, see"
					"LICENSE for details");
		default:
			die("usage: bgs [-v] [-c] [-s] [-x] IMAGE(S)...");
		}
	if(!(dpy = XOpenDisplay(NULL)))
		die("error: cannot open display");
	setup(&argv[i], argc - i);
	run();
	cleanup();
	XCloseDisplay(dpy);
	return EXIT_SUCCESS;
}

/* vim: set noexpandtab ts=8 sts=8 sw=8 tw=80: */
