/* See LICENSE file for license details. */
#define _XOPEN_SOURCE 500

#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/extensions/scrnsaver.h>

typedef struct {
	int screen;
	Window root, win;
	Pixmap pmap;
	unsigned long color;
} Lock;

static Lock **locks;
static int nscreens;
static Bool running = True;

static void
die(const char *errstr, ...)
{
	va_list ap;

	va_start(ap, errstr);
	vfprintf(stderr, errstr, ap);
	va_end(ap);
	exit(1);
}

static void
unlockscreen(Display *dpy, Lock *lock)
{
	if(dpy == NULL || lock == NULL)
		return;

	XUngrabPointer(dpy, CurrentTime);
	XFreeColors(dpy, DefaultColormap(dpy, lock->screen), &lock->color, 1, 0);
	XFreePixmap(dpy, lock->pmap);
	XDestroyWindow(dpy, lock->win);

	free(lock);
}

static Lock *
lockscreen(Display *dpy, int screen)
{
	char curs[] = {0, 0, 0, 0, 0, 0, 0, 0};
	unsigned int len;
	Lock *lock;
	XColor color, dummy;
	XSetWindowAttributes wa;
	Cursor invisible;

	if (!running || dpy == NULL || screen < 0 || !(lock = malloc(sizeof(Lock))))
		return NULL;

	lock->screen = screen;
	lock->root = RootWindow(dpy, lock->screen);

	XAllocNamedColor(dpy, DefaultColormap(dpy, lock->screen), "black", &color, &dummy);
	lock->color = color.pixel;

	/* init */
	wa.override_redirect = 1;
	wa.background_pixel = lock->color;
	lock->win = XCreateWindow(dpy, lock->root, 0, 0, DisplayWidth(dpy, lock->screen), DisplayHeight(dpy, lock->screen),
	                          0, DefaultDepth(dpy, lock->screen), CopyFromParent,
	                          DefaultVisual(dpy, lock->screen), CWOverrideRedirect | CWBackPixel, &wa);
	lock->pmap = XCreateBitmapFromData(dpy, lock->win, curs, 8, 8);
	invisible = XCreatePixmapCursor(dpy, lock->pmap, lock->pmap, &color, &color, 0, 0);
	XDefineCursor(dpy, lock->win, invisible);
	XMapRaised(dpy, lock->win);

	/* Try to grab mouse pointer else fail the lock */
	for (len = 1000; len; len--) {
		if (XGrabPointer(dpy, lock->root, False, ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
		    GrabModeAsync, GrabModeAsync, None, invisible, CurrentTime) == GrabSuccess)
			return lock;
		usleep(1000);
	}
	fprintf(stderr, "clicklock: unable to grab mouse pointer for screen %d\n", screen);
	running = 0;
	unlockscreen(dpy, lock);
	return NULL;
}

static void
usage(void)
{
	fprintf(stderr, "usage: clicklock [-v|POST_LOCK_CMD]\n");
	exit(1);
}

int
main(int argc, char **argv) {
	Display *dpy;
	XEvent ev;
	int screen, ssevbase, sserrbase;

	if ((argc == 2) && !strcmp("-v", argv[1]))
		die("clicklock based on: slock-%s, © 2006-2016 slock engineers\n", VERSION);

	if ((argc == 2) && !strcmp("-h", argv[1]))
		usage();

	if (!(dpy = XOpenDisplay(0)))
		die("clicklock: cannot open display\n");
	/* Get the number of screens in display "dpy" and blank them all. */
	nscreens = ScreenCount(dpy);
	if (!(locks = malloc(sizeof(Lock*) * nscreens)))
		die("clicklock: malloc: %s\n", strerror(errno));
	int nlocks = 0;
	for (screen = 0; screen < nscreens; screen++) {
		if ((locks[screen] = lockscreen(dpy, screen)) != NULL)
			nlocks++;
	}
	XSync(dpy, False);

	/* Did we actually manage to lock something? */
	if (nlocks == 0) { /* nothing to protect */
		free(locks);
		XCloseDisplay(dpy);
		return 1;
	}

	if (XScreenSaverQueryExtension(dpy, &ssevbase, &sserrbase)) {
		XScreenSaverSelectInput(dpy, DefaultRootWindow(dpy), ScreenSaverNotifyMask);
	}

	if (argc >= 2 && fork() == 0) {
		if (dpy)
			close(ConnectionNumber(dpy));
		execvp(argv[1], argv+1);
		die("clicklock: execvp %s failed: %s\n", argv[1], strerror(errno));
	}

	running = True;
	while (running && !XNextEvent(dpy, &ev)) {
		if (ev.type == ButtonPress || (ev.type == ssevbase && ((XScreenSaverNotifyEvent *)&ev)->state == ScreenSaverOff)) {
			running = False;
		}
	}

	for (screen = 0; screen < nscreens; screen++)
		unlockscreen(dpy, locks[screen]);

	free(locks);
	XCloseDisplay(dpy);

	return 0;
}
