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

/* function declarations */
static void cleanup(void);		/* frees images before exit. */
static void die(const char *errstr);	/* prints errstr to strerr and exits. */
static void drawbg(void);		/* draws background to root. */
static void run(void);			/* main loop */
static void setup(char *paths[], int c);/* sets up imlib and X */
static void updategeom(void);		/* updates screen and/or Xinerama
					   dimensions */

/* variables */
#define N 8			/* max number of monitors/background images */
static Monitor monitors[N];
static Imlib_Image images[N];
static Monitor s;		/* geometry of the screen */
static Bool center  = False;	/* center image instead of rescale */
static Bool running = False;
static Display *dpy;
static Window root;
static int nmonitor, nimage;	/* Amount of monitors/available background
				   images */

/* function implementations */
void
cleanup(void) {
	int i;

	for(i = 0; i < nimage; i++) {
		imlib_context_set_image(images[i]);
		imlib_free_image_and_decache();
	}
}

void
die(const char *errstr) {
	fputs(errstr, stderr);
	exit(EXIT_FAILURE);
}

void
drawbg(void) {
	int i, w, h, nx, ny, nh, nw, tmp;
	double factor;
	Pixmap pm;
	Imlib_Image tmpimg, buffer;

	pm = XCreatePixmap(dpy, root, s.w, s.h, DefaultDepth(dpy,
				DefaultScreen(dpy)));
	if(!(buffer = imlib_create_image(s.w, s.h)))
		die("Error: Cannot allocate buffer.\n");
	imlib_context_set_image(buffer);
	imlib_context_set_color(0,0,0,0);
	imlib_image_fill_rectangle(0, 0, s.w, s.h);
	imlib_context_set_blend(1);
	for(i = 0; i < nmonitor; i++) {
		imlib_context_set_image(images[i % nimage]);
		w = imlib_image_get_width();
		h = imlib_image_get_height();
		if(!(tmpimg = imlib_clone_image()))
			die("Error: Cannot clone image.\n");
		imlib_context_set_image(tmpimg);
		if((monitors[i].w > monitors[i].h && w < h) ||
				(monitors[i].w < monitors[i].h && w > h)) {
			imlib_image_orientate(1);
			tmp = w;
			w = h;
			h = tmp;
		}
		imlib_context_set_image(buffer);
		if(center) {
			nw = (monitors[i].w - w) / 2;
			nh = (monitors[i].h - h) / 2;
		}
		else {
			factor = MAX((double)w / monitors[i].w,
					(double)h / monitors[i].h);
			nw = w / factor;
			nh = h / factor;
		}
		nx = monitors[i].x + (monitors[i].w - nw) / 2;
		ny = monitors[i].y + (monitors[i].h - nh) / 2;
		imlib_blend_image_onto_image(tmpimg, 0, 0, 0, w, h,
				nx, ny, nw, nh);
		imlib_context_set_image(tmpimg);
		imlib_free_image();
	}
	imlib_context_set_blend(0);
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


	for(;;) {
		updategeom();
		drawbg();
		if(!running)
			break;
		imlib_flush_loaders();
		XNextEvent(dpy, &ev);
		if(ev.type == ConfigureNotify) {
			s.w = ev.xconfigure.width;
			s.h = ev.xconfigure.height;
			imlib_flush_loaders();
		}
	}
}

void
setup(char *paths[], int c) {
	/* Loading images */
	for(int i = nimage = 0; i < c && i < N; i++) {
		if((images[nimage] = imlib_load_image_without_cache(paths[i])))
			nimage++;
		else {
			fprintf(stderr, "Warning: Cannot load file `%s`."
					"Ignoring.\n", paths[nimage]);
			continue;
		}
	}
	if(nimage == 0)
		die("Error: No image to draw.\n");

	/* set up X */
	const int screen = DefaultScreen(dpy);
	Visual * const vis = DefaultVisual(dpy, screen);
	const Colormap cm = DefaultColormap(dpy, screen);
	root = RootWindow(dpy, screen);
	XSelectInput(dpy, root, StructureNotifyMask);
	s.x = s.y = 0;
	s.w = DisplayWidth(dpy, screen);
	s.h = DisplayHeight(dpy, screen);

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
	info[0].x_org  = 0;
	info[0].y_org  = 0;
	info[0].width  = DisplayWidth(dpy, screen);
	info[0].height = DisplayHeight(dpy, screen);
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
		die("bgs: no monitors to configure");
}

int
main(int argc, char *argv[]) {
	int i;

	for(i = 1; i < argc && argv[i][0] == '-' && argv[i][1] != '\0' &&
			argv[i][1] != '-' && argv[i][2] == '\0'; i++)
		switch(argv[i][1]) {
		case 'c':
			center = True; break;
		case 'x':
			running = True; break;
		case 'v':
			die("bgs-"VERSION", © 2010 bgs engineers, see"
					"LICENSE for details\n");
		default:
			die("usage: bgs [-v] [-c] [-x] [IMAGE]...\n");
		}
	if(!(dpy = XOpenDisplay(NULL)))
		die("bgs: cannot open display\n");
	setup(&argv[i], argc - i);
	run();
	cleanup();
	XCloseDisplay(dpy);
	return EXIT_SUCCESS;
}

/* vim: set noexpandtab ts=8 sts=8 sw=8 tw=80: */
