/*
 * Copyright (C) 1992 Clarendon Hill Software.
 *
 * Permission is granted to any individual or institution to use, copy,
 * or redistribute this software, provided this copyright notice is retained. 
 *
 * This software is provided "as is" without any expressed or implied
 * warranty.  If this software brings on any sort of damage -- physical,
 * monetary, emotional, or brain -- too bad.  You've got no one to blame
 * but yourself. 
 *
 * The software may be modified for your own purposes, but modified versions
 * must retain this notice.
 */

/*
   Modified by Timothy Mann, 1996
   Last modified on Mon Dec  1 15:13:27 PST 1997 by mann
*/

/*
 * trs_xinterface.c
 *
 * X Windows interface for TRS-80 simulator
 */

#include <stdio.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/time.h>
#include <malloc.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/keysymdef.h>
#include <X11/Xresource.h>

#include "trs_iodefs.h"
#include "trs.h"
#include "z80.h"
#include "trs_disk.h"

#define DEF_FONT1 \
    "-trs80-model1-medium-r-normal--24-*-64-64-c-80-trs80-model1"
#define DEF_WIDEFONT1 \
    "-trs80-model1-medium-r-normal--24-*-64-64-c-160-trs80-model1"
#define DEF_FONT3 \
    "-trs80-model3-medium-r-normal--24-*-64-64-c-80-trs80-model3"
#define DEF_WIDEFONT3 \
    "-trs80-model3-medium-r-normal--24-*-64-64-c-160-trs80-model3"
#define DEF_USEFONT 0

extern char trs_char_data[2][MAXCHARS][TRS_CHAR_HEIGHT];
extern char trs_widechar_data[2][MAXCHARS][TRS_CHAR_HEIGHT][2];

#define EVENT_MASK \
  ExposureMask | KeyPressMask | MapRequest | KeyReleaseMask | \
  StructureNotifyMask | LeaveWindowMask

/* Private data */
static unsigned char trs_screen[2048];
static int screen_chars = 1024;
static int row_chars = 64;
static int col_chars = 16;
static int resize = 0;
static int top_margin = 0;
static int left_margin = 0;
static Pixmap trs_char[2][MAXCHARS];
static Display *display;
static int screen;
static Window window;
static GC gc;
static GC gc_inv;
static int currentmode = NORMAL;
static int OrigHeight,OrigWidth;
static int usefont = DEF_USEFONT;
static int trsfont;
static int cur_char_width = TRS_CHAR_WIDTH;
static int cur_char_height = TRS_CHAR_HEIGHT;
static XFontStruct *myfont, *mywidefont, *curfont;

static XrmOptionDescRec opts[] = {
{"-background",	"*background",	XrmoptionSepArg,	(caddr_t)NULL},
{"-bg",		"*background",	XrmoptionSepArg,	(caddr_t)NULL},
{"-foreground",	"*foreground",	XrmoptionSepArg,	(caddr_t)NULL},
{"-fg",		"*foreground",	XrmoptionSepArg,	(caddr_t)NULL},
{"-usefont",	"*usefont",	XrmoptionNoArg,		(caddr_t)"on"},
{"-nofont",	"*usefont",	XrmoptionNoArg,		(caddr_t)"off"},
{"-font",	"*font",	XrmoptionSepArg,	(caddr_t)NULL},
{"-widefont",	"*widefont",	XrmoptionSepArg,	(caddr_t)NULL},
{"-trsfont",	"*trsfont",	XrmoptionNoArg,		(caddr_t)"on"},
{"-notrsfont",	"*trsfont",	XrmoptionNoArg,		(caddr_t)"off"},
{"-display",	"*display",	XrmoptionSepArg,	(caddr_t)NULL},
{"-debug",	"*debug",	XrmoptionNoArg,		(caddr_t)"on"},
{"-nodebug",	"*debug",	XrmoptionNoArg,		(caddr_t)"off"},
{"-romfile",	"*romfile",	XrmoptionSepArg,	(caddr_t)NULL},
{"-romfile3",	"*romfile3",	XrmoptionSepArg,	(caddr_t)NULL},
{"-resize",	"*resize",	XrmoptionNoArg,		(caddr_t)"on"},
{"-noresize",	"*resize",	XrmoptionNoArg,		(caddr_t)"off"},
{"-spinfast",   "*spinfast",    XrmoptionNoArg,         (caddr_t)"on"},
{"-nospinfast", "*spinfast",    XrmoptionNoArg,         (caddr_t)"off"},
{"-doublestep", "*doublestep",  XrmoptionNoArg,         (caddr_t)"on"},
{"-nodoublestep","*doublestep", XrmoptionNoArg,         (caddr_t)"off"},
{"-model",      "*model",       XrmoptionSepArg,	(caddr_t)NULL},
{"-model1",     "*model",       XrmoptionNoArg,		(caddr_t)"1"},
{"-model3",     "*model",       XrmoptionNoArg,		(caddr_t)"3"},
{"-model4",     "*model",       XrmoptionNoArg,		(caddr_t)"4"},
{"-diskdir",    "*diskdir",     XrmoptionSepArg,	(caddr_t)NULL},
};

static int num_opts = (sizeof opts / sizeof opts[0]);

/*
 * Key event queueing routines
 */
#define KEY_QUEUE_SIZE	(32)
static int key_queue[KEY_QUEUE_SIZE];
static int key_queue_head;
static int key_queue_entries;

static void clear_key_queue()
{
    key_queue_head = 0;
    key_queue_entries = 0;
}

void queue_key(state)
    int state;
{
    key_queue[(key_queue_head + key_queue_entries) % KEY_QUEUE_SIZE] = state;
#ifdef KBDEBUG
	fprintf(stderr, "queue_key 0x%x", state);
#endif
    if (key_queue_entries < KEY_QUEUE_SIZE) {
	key_queue_entries++;
#ifdef KBDEBUG
	fprintf(stderr, "\n");
    } else {
	fprintf(stderr, " (overflow)\n");
#endif
    }
}

int dequeue_key()
{
    int rval = -1;

    if(key_queue_entries > 0)
    {
	rval = key_queue[key_queue_head];
	key_queue_head = (key_queue_head + 1) % KEY_QUEUE_SIZE;
	key_queue_entries--;
#ifdef KBDEBUG
	fprintf(stderr, "dequeue_key 0x%x\n", rval);
#endif
    }
    return rval;
}

/* Private routines */
void bitmap_init();
void screen_init();
void trs_event_init();
void trs_event();

static XrmDatabase x_db = NULL;
static XrmDatabase command_db = NULL;
static char *program_name;

int trs_parse_command_line(argc, argv, debug)
     int argc;
     char **argv;
     int *debug;
{
    char option[512];
    char *type;
    XrmValue value;
    char *xrms;

    program_name = strrchr(argv[0], '/');
    if (program_name == NULL) {
      program_name = argv[0];
    } else {
      program_name++;
    }
    
    XrmInitialize();
    /* parse command line options */
    XrmParseCommand(&command_db,opts,num_opts,program_name,&argc,argv);

    (void) sprintf(option, "%s%s", program_name, ".display");
    (void) XrmGetResource(command_db, option, "Xtrs.Display", &type, &value);
    /* open display */
    if ( (display = XOpenDisplay (value.addr)) == NULL) {
	printf("Unable to open display.");
	exit(-1);
    }

    /* get defaults from server */
    xrms = XResourceManagerString(display);
    if (xrms != NULL) {
      x_db = XrmGetStringDatabase(xrms);
      XrmMergeDatabases(command_db,&x_db);
    } else {
      x_db = command_db;
    }

    (void) sprintf(option, "%s%s", program_name, ".debug");
    if (XrmGetResource(x_db, option, "Xtrs.Debug", &type, &value))
    {
	if (strcmp(value.addr,"on") == 0) {
	    *debug = True;
	} else if (strcmp(value.addr,"off") == 0) {
	    *debug = False;
	}
    }

    (void) sprintf(option, "%s%s", program_name, ".spinfast");
    if (XrmGetResource(x_db, option, "Xtrs.Spinfast", &type, &value))
    {
	if (strcmp(value.addr,"on") == 0) {
	    trs_disk_spinfast = True;
	} else if (strcmp(value.addr,"off") == 0) {
	    trs_disk_spinfast = False;
	}
    }

    (void) sprintf(option, "%s%s", program_name, ".doublestep");
    if (XrmGetResource(x_db, option, "Xtrs.doublestep", &type, &value))
    {
	if (strcmp(value.addr,"on") == 0) {
	    trs_disk_doublestep = True;
	} else if (strcmp(value.addr,"off") == 0) {
	    trs_disk_doublestep = False;
	}
    }

    (void) sprintf(option, "%s%s", program_name, ".model");
    if (XrmGetResource(x_db, option, "Xtrs.Model", &type, &value))
    {
      if (strcmp(value.addr, "1") == 0 ||
	  strcasecmp(value.addr, "I") == 0) {
	trs_model = 1;
      } else if (strcmp(value.addr, "3") == 0 ||
		 strcasecmp(value.addr, "III") == 0) {
	trs_model = 3;
      } else if (strcmp(value.addr, "4") == 0 ||
		 strcasecmp(value.addr, "IV") == 0) {
	trs_model = 4;
      } else {
	  fprintf(stderr, "%s: TRS-80 Model %s not supported\n",
		  program_name, value.addr);
	  exit(1);
      }
    }

    (void) sprintf(option, "%s%s", program_name, ".diskdir");
    if (XrmGetResource(x_db, option, "Xtrs.Diskdir", &type, &value))
    {
        trs_disk_dir = strdup(value.addr);
    }

    return argc;
}

/* exits if something really bad happens */
void trs_screen_init()
{
    Window root_window;
    unsigned long fore_pixel, back_pixel, foreground, background;
    char option[512];
    char *type;
    XrmValue value;
    Colormap color_map;
    XColor cdef;
    XGCValues gcvals;
    char *fontname = NULL;
    char *widefontname = NULL;
    int len;

    screen = DefaultScreen(display);
    color_map = DefaultColormap(display,screen);

    (void) sprintf(option, "%s%s", program_name, ".foreground");
    if (XrmGetResource(x_db, option, "Xtrs.Foreground", &type, &value))
    {
	/* printf("foreground is %s\n",value.addr); */
	XParseColor(display, color_map, value.addr, &cdef);
	XAllocColor(display, color_map, &cdef);
	fore_pixel = cdef.pixel;
    } else {
	fore_pixel = WhitePixel(display, screen);
    }

    (void) sprintf(option, "%s%s", program_name, ".background");
    if (XrmGetResource(x_db, option, "Xtrs.Background", &type, &value))
    {
	XParseColor(display, color_map, value.addr, &cdef);
	XAllocColor(display, color_map, &cdef);
	back_pixel = cdef.pixel;
    } else {
	back_pixel = BlackPixel(display, screen);
    }

    (void) sprintf(option, "%s%s", program_name, ".usefont");
    if (XrmGetResource(x_db, option, "Xtrs.Usefont", &type, &value))
    {
	if (strcmp(value.addr,"on") == 0) {
	    usefont = 1;
	} else if (strcmp(value.addr,"off") == 0) {
	    usefont = 0;
	}
    }

    if (usefont) {
	(void) sprintf(option, "%s%s", program_name, ".font");
	if (XrmGetResource(x_db, option, "Xtrs.Font", &type, &value))
	{
	    len = strlen(value.addr);
	    fontname = malloc(len + 1);
	    strcpy(fontname,value.addr);
	} else {
	    char *def_font = (trs_model == 1 ? DEF_FONT1 : DEF_FONT3);
	    len = strlen(def_font);
	    fontname = malloc(len+1);
	    strcpy(fontname, def_font);
	}
	(void) sprintf(option, "%s%s", program_name, ".widefont");
	if (XrmGetResource(x_db, option, "Xtrs.Widefont", &type, &value))
	{
	    len = strlen(value.addr);
	    widefontname = malloc(len + 1);
	    strcpy(widefontname,value.addr);
	} else {
	    char *def_widefont =
	      (trs_model == 1 ? DEF_WIDEFONT1 : DEF_WIDEFONT3);
	    len = strlen(def_widefont);
	    widefontname = malloc(len+1);
	    strcpy(widefontname, def_widefont);
	}
	/* Set default (kludge) */
	trsfont = (strncasecmp(fontname, "-trs80", 6) == 0);
    }

    (void) sprintf(option, "%s%s", program_name, ".trsfont");
    if (XrmGetResource(x_db, option, "Xtrs.Trsfont", &type, &value))
    {
	if (strcmp(value.addr,"on") == 0) {
	    trsfont = 1;
	} else if (strcmp(value.addr,"off") == 0) {
	    trsfont = 0;
	}
    }

    (void) sprintf(option, "%s%s", program_name, ".resize");
    if (XrmGetResource(x_db, option, "Xtrs.Resize", &type, &value))
    {
	if (strcmp(value.addr,"on") == 0) {
	    resize = 1;
	} else if (strcmp(value.addr,"off") == 0) {
	    resize = 0;
	}
    }

    if (trs_model == 1) {
	/* try resources first */
	(void) sprintf(option, "%s%s", program_name, ".romfile");
	if (XrmGetResource(x_db, option, "Xtrs.Romfile", &type, &value)) {
	    trs_load_rom(value.addr);
	} else if (trs_rom1_size > 0) {
	    trs_load_compiled_rom(trs_rom1_size, trs_rom1);
	} else {
#ifdef DEFAULT_ROM
	    trs_load_rom(DEFAULT_ROM);
#else
	    fprintf(stderr,"%s: rom file not specified!\n",program_name);
	    exit(-1);
#endif
	}
    } else {
	(void) sprintf(option, "%s%s", program_name, ".romfile3");
	if (XrmGetResource(x_db, option, "Xtrs.Romfile3", &type, &value)) {
	    trs_load_rom(value.addr);
	} else if (trs_rom3_size > 0) {
	    trs_load_compiled_rom(trs_rom3_size, trs_rom3);
	} else {
#ifdef DEFAULT_ROM3
	    trs_load_rom(DEFAULT_ROM3);
#else
	    fprintf(stderr,"%s: rom file not specified!\n",program_name);
	    exit(-1);
#endif
	}
    }

    clear_key_queue(); /* init the key queue */

    /* setup root window, and gc */
    root_window = DefaultRootWindow(display);

    gcvals.graphics_exposures = False;
    gc = XCreateGC(display, root_window, GCGraphicsExposures, &gcvals);

    XSetForeground(display, gc, fore_pixel);
    XSetBackground(display, gc, back_pixel);
    foreground = fore_pixel;
    background = back_pixel;

    gc_inv = XCreateGC(display, root_window, GCGraphicsExposures, &gcvals);
    XSetForeground(display, gc_inv, back_pixel);
    XSetBackground(display, gc_inv, fore_pixel);

    if (usefont) {
	if ((myfont = XLoadQueryFont(display,fontname)) == NULL) {
	    fprintf(stderr,"%s: Can't open font %s!\n",program_name,fontname);
	    exit(-1);
	}
	if ((mywidefont = XLoadQueryFont(display,widefontname)) == NULL) {
	    fprintf(stderr,"%s: Can't open font %s!\n",
		    program_name,widefontname);
	    exit(-1);
	}
	curfont = myfont;
	XSetFont(display,gc,myfont->fid);
	cur_char_width =  myfont->max_bounds.width;
	cur_char_height = myfont->ascent + myfont->descent;
    }

    if (trs_model == 4 && !resize) {
      OrigWidth = cur_char_width * 80;
      left_margin = cur_char_width * (80 - row_chars)/2;
      OrigHeight = cur_char_height * 24;
      top_margin = cur_char_height * (24 - col_chars)/2;
    } else {
      OrigWidth = cur_char_width * row_chars;
      OrigHeight = cur_char_height * col_chars;
    }
    window = XCreateSimpleWindow(display, root_window, 400, 400,
				 OrigWidth, OrigHeight, 1, foreground,
				 background);
    XStoreName(display,window,program_name);
    XSelectInput(display, window, EVENT_MASK | ResizeRedirectMask);
    XMapWindow(display, window);

    bitmap_init(foreground, background);

    screen_init();
    XClearWindow(display,window);

#ifdef HAVE_SIGIO
    trs_event_init();
#endif
}

#ifdef HAVE_SIGIO
void trs_event_init()
{
    int fd, rc;
    struct sigaction sa;

    /* set up event handler */
    sa.sa_handler = trs_event;
    sigemptyset(&sa.sa_mask);
    sigaddset(&sa.sa_mask, SIGIO);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGIO, &sa, NULL);

    fd = ConnectionNumber(display);
    if (fcntl(fd,F_SETOWN,getpid()) < 0)
	perror("fcntl F_SETOWN");
    rc = fcntl(fd,F_SETFL,FASYNC);
    if (rc != 0)
	perror("fcntl F_SETFL async error");
}

/* ARGSUSED */
void trs_event(signo)
    int signo;
{
    x_poll_count = 0;
}
#endif /*HAVE_SIGIO*/

KeySym last_key[256];

/* 
 * Get and process X event(s).
 *   If wait is true, process one event, blocking until one is available.
 *   If wait is false, process as many events as are available, returning
 *     when none are left.
 */ 
void trs_get_event(wait)
     int wait;
{
    XEvent event;
    KeySym key;
    char buf[10];
    XComposeStatus status;
    XWindowChanges xwc;

    do {
	if (wait) {
	    XNextEvent(display, &event);
	} else {
	    if (!XCheckMaskEvent(display, ~0, &event)) return;
	}

	switch(event.type) {
	  case Expose:
#ifdef XDEBUG
	    fprintf(stderr,"Expose\n");
#endif
	    trs_screen_refresh();
	    break;

	  case ResizeRequest:
#ifdef XDEBUG
	    fprintf(stderr,"ResizeRequest w %d-->%d, h %d-->%d\n",
		    OrigWidth, event.xresizerequest.width,
		    OrigHeight, event.xresizerequest.height);
#endif
            if (event.xresizerequest.width != OrigWidth ||
		event.xresizerequest.height != OrigHeight) {
	      xwc.width = OrigWidth;
	      xwc.height = OrigHeight;
	      XConfigureWindow(display,event.xresizerequest.window,
			       (CWWidth | CWHeight),&xwc);
	    }
#ifdef DO_XFLUSH
	    XFlush(display);
#endif
	    break;

	  case ConfigureNotify:
#ifdef XDEBUG
	    fprintf(stderr,"ConfigureNotify\n");
#endif
	    break;

	  case MapNotify:
#ifdef XDEBUG
	    fprintf(stderr,"MapNotify\n");
#endif
	    trs_screen_refresh();
	    break;

	  case EnterNotify:
#ifdef XDEBUG
	    fprintf(stderr,"EnterNotify\n");
#endif
	    trs_xlate_keycode(0x10000); /* all keys up */
	    break;

	  case LeaveNotify:
#ifdef XDEBUG
	    fprintf(stderr,"LeaveNotify\n");
#endif
	    trs_xlate_keycode(0x10000); /* all keys up */
	    break;

	  case KeyPress:
	    (void) XLookupString((XKeyEvent *)&event,buf,10,&key,&status);
#ifdef XDEBUG
	    fprintf(stderr,"KeyPress: state 0x%x, keycode 0x%x, key 0x%x\n",
		    event.xkey.state, event.xkey.keycode, key);
#endif
	    switch (key) {
	      /* Trap some function keys here */
	      case XK_F10:
		trs_reset();
		key = 0;
		break;
	      case XK_F9:
		trs_debug();
		key = 0;
		break;
	      case XK_F8:
		trs_exit();
		key = 0;
		break;
	      case XK_F7:
		trs_disk_change_all();
		key = 0;
		break;
	      default:
		break;
	    }
	    if ( ((event.xkey.state & (ShiftMask|LockMask))
		  == (ShiftMask|LockMask))
		   && key >= 'A' && key <= 'Z' ) {
		/* Make Shift + CapsLock give lower case */
		key = (int) key + 0x20;
	    }
	    if (key == XK_Shift_R && trs_model == 1) {
		key = XK_Shift_L;
	    }
	    if (last_key[event.xkey.keycode] != 0) {
		trs_xlate_keycode(0x10000 | last_key[event.xkey.keycode]);
	    }
	    last_key[event.xkey.keycode] = key;
	    if (key != 0) {
		trs_xlate_keycode(key);
	    }
	    break;

	  case KeyRelease:
	    key = last_key[event.xkey.keycode];
	    last_key[event.xkey.keycode] = 0;
	    if (key != 0) {
		trs_xlate_keycode(0x10000 | key);
	    }
#ifdef XDEBUG
	    fprintf(stderr,"KeyRelease: keycode 0x%x, last_key 0x%x\n",
		    event.xkey.state, event.xkey.keycode, key);
#endif
	    break;

	  default:
#ifdef XDEBUG	    
	    fprintf(stderr,"Unhandled event: type %d\n", event.type);
#endif
	    break;
	}
    } while (!wait);
}

void trs_screen_expanded(flag)
    int flag;
{
    int bit = flag ? EXPANDED : 0;
    if ((currentmode ^ bit) & EXPANDED) {
	currentmode ^= EXPANDED;
	if (usefont) {
	    curfont = (flag ? mywidefont : myfont);
	    XSetFont(display,gc,curfont->fid);
	}
	XClearWindow(display,window);
	trs_screen_refresh();
    }
}

void trs_screen_inverse(flag)
     int flag;
{
    int bit = flag ? INVERSE : 0;
    int i;
    if ((currentmode ^ bit) & INVERSE) {
	currentmode ^= INVERSE;
	for (i = 0; i < screen_chars; i++) {
	    if (trs_screen[i] & 0x80)
	      trs_screen_write_char(i,trs_screen[i],False);
	}
    }
}

void trs_screen_80x24(flag)
     int flag;
{
    /* Called only on an actual change */
    if (flag) {
	row_chars = 80;
	col_chars = 24;
    } else {
	row_chars = 64;
	col_chars = 16;
    }
    screen_chars = row_chars * col_chars;
    if (resize) {
      OrigWidth = cur_char_width * row_chars;
      OrigHeight = cur_char_height * col_chars;;
      XSelectInput(display, window, EVENT_MASK);
      XResizeWindow(display, window, OrigWidth, OrigHeight);
      XFlush(display);
      XSelectInput(display, window, EVENT_MASK | ResizeRedirectMask);
    } else {
      left_margin = cur_char_width * (80 - row_chars)/2;
      top_margin = cur_char_height * (24 - col_chars)/2;
      if (left_margin || top_margin) XClearWindow(display,window);
    }
    trs_screen_refresh();
}

void screen_init()
{
    int i;

    /* initially, screen is blank (i.e. full of spaces) */
    for (i = 0; i < sizeof(trs_screen); i++)
	trs_screen[i] = ' ';
}

void boxes_init(foreground, background, width, height, expanded)
{
    int graphics_char, bit, p;
    XRectangle bits[6];
    XRectangle cur_bits[6];

    /*
     * Calculate what the 2x3 boxes look like.
     */
    bits[0].x = bits[2].x = bits[4].x = 0;
    bits[0].width = bits[2].width = bits[4].width =
      bits[1].x = bits[3].x = bits[5].x =  width / 2;
    bits[1].width = bits[3].width = bits[5].width = width - bits[1].x;

    bits[0].y = bits[1].y = 0;
    bits[0].height = bits[1].height =
      bits[2].y = bits[3].y = cur_char_height / 3;
    bits[4].y = bits[5].y = (cur_char_height * 2) / 3;
    bits[2].height = bits[3].height = bits[4].y - bits[2].y;
    bits[4].height = bits[5].height = cur_char_height - bits[4].y;

    for (graphics_char = 128; graphics_char < 192; ++graphics_char) {
	trs_char[expanded][graphics_char] =
	  XCreatePixmap(display, window, width, height,
			DefaultDepth(display, screen));

	/* Clear everything */
	XSetForeground(display, gc, background);
	XFillRectangle(display, trs_char[expanded][graphics_char],
		       gc, 0, 0, width, height);

	/* Set the bits */
	XSetForeground(display, gc, foreground);

	for (bit = 0, p = 0; bit < 6; ++bit) {
	    if (graphics_char & (1 << bit)) {
		cur_bits[p++] = bits[bit];
	    }
	}
	XFillRectangles(display, trs_char[expanded][graphics_char],
			  gc, cur_bits, p);
    }
}

void bitmap_init(foreground, background)
    unsigned long foreground, background;
{
    if(!usefont) {
	/* Initialize from built-in font bitmaps. */
	int i;
	
	for (i = 0; i < MAXCHARS; i++) {
	    trs_char[0][i] =
		XCreateBitmapFromData(display,window,
				      trs_char_data[trs_model != 1][i],
				      TRS_CHAR_WIDTH,TRS_CHAR_HEIGHT);
	    trs_char[1][i] =
		XCreateBitmapFromData(display,window,
				     (char*)trs_widechar_data[trs_model!=1][i],
				      TRS_CHAR_WIDTH*2,TRS_CHAR_HEIGHT);
	}
    } else if (!trsfont) {
	int dwidth, dheight;

	boxes_init(foreground, background,
		   cur_char_width, cur_char_height, 0);
	dwidth = 2*cur_char_width;
	dheight = 2*cur_char_height;
	if (dwidth > mywidefont->max_bounds.width) {
	    dwidth = mywidefont->max_bounds.width;
	}
	if (dheight > mywidefont->ascent + mywidefont->descent) {
	    dheight = mywidefont->ascent + mywidefont->descent;
	}
	boxes_init(foreground, background, dwidth, dheight, 1);
    }
}

void trs_screen_refresh()
{
    int i;

    for (i = 0; i < screen_chars; i++) {
	trs_screen_write_char(i,trs_screen[i],False);
    }
#ifdef DO_XFLUSH
    XFlush(display);
#endif
}

void trs_screen_write_char(position, char_index, doflush)
int position;
int char_index;
Bool doflush;
{
    int row,col,destx,desty;
    int plane;
    char temp_char;

    trs_screen[position] = char_index;
    if (position >= screen_chars) {
      return;
    }
    if ((currentmode & EXPANDED) && (position & 1)) {
      return;
    }
    row = position / row_chars;
    col = position - (row * row_chars);
    destx = col * cur_char_width + left_margin;
    desty = row * cur_char_height + top_margin;
    if (usefont) {
	if (trs_model == 1 && !trsfont) {
#ifndef UPPERCASE
	    /* Emulate Radio Shack lowercase mod.  The replacement character
	       generator ROM had another copy of the uppercase characters in
	       the control character positions, to compensate for a bug in the
	       Level II ROM that stores such values instead of uppercase. */
	    if (char_index < 0x20) char_index += 0x40;
#endif
	    if (char_index >= 0x80) char_index &= 0xbf;
	}
	if(!trsfont && char_index >= 0x80 && char_index <= 0xbf) {
	    /* use graphics character bitmap instead of font */
	    plane = 1;
	    switch (currentmode) {
	      case NORMAL:
		XCopyArea(display,
			  trs_char[0][char_index],window,gc,0,0,
			  cur_char_width,cur_char_height,destx,desty);
		break;
	      case EXPANDED:
		XCopyArea(display,
			  trs_char[1][char_index],window,gc,0,0,
			  cur_char_width*2,cur_char_height,destx,desty);
		break;
	      case INVERSE:
		XCopyArea(display,
			  trs_char[0][char_index & 0x7f], window,
			  (char_index & 0x80) ? gc_inv : gc, 0, 0,
			  cur_char_width,cur_char_height,destx,desty);
		break;
	      case EXPANDED+INVERSE:
		XCopyArea(display,
			  trs_char[1][char_index & 0x7f], window,
			  (char_index & 0x80) ? gc_inv : gc, 0, 0,
			  cur_char_width*2,cur_char_height,destx,desty);
		break;
	    }
	} else {
	    desty += curfont->ascent;
	    if (currentmode & INVERSE) {
		temp_char = (char)(char_index & 0x7f);
		XDrawImageString(display, window,
				 (char_index & 0x80) ? gc_inv : gc,
				 destx, desty, &temp_char, 1);
	    } else {
		temp_char = (char)char_index;
		XDrawImageString(display, window, gc,
				 destx, desty, &temp_char, 1);
	    }
	}
    } else {
        plane = 1;
	switch (currentmode) {
	  case NORMAL:
	    XCopyPlane(display,trs_char[0][char_index],
		       window,gc,0,0,TRS_CHAR_WIDTH,
		       TRS_CHAR_HEIGHT,destx,desty,plane);
	    break;
	  case EXPANDED:
	    XCopyPlane(display,trs_char[1][char_index],
		       window,gc,0,0,TRS_CHAR_WIDTH*2,
		       TRS_CHAR_HEIGHT,destx,desty,plane);
	    break;
	  case INVERSE:
	    XCopyPlane(display,trs_char[0][char_index & 0x7f],window,
		       (char_index & 0x80) ? gc_inv : gc,
		       0,0,TRS_CHAR_WIDTH,TRS_CHAR_HEIGHT,destx,desty,plane);
	    break;
	  case EXPANDED+INVERSE:
	    XCopyPlane(display,trs_char[1][char_index & 0x7f],window,
		       (char_index & 0x80) ? gc_inv : gc,
		       0,0,TRS_CHAR_WIDTH*2,TRS_CHAR_HEIGHT,destx,desty,plane);
	    break;
	}
    }
#ifdef DO_XFLUSH
    if (doflush)
	XFlush(display);
#endif
}

 /* Copy lines 1 through col_chars-1 to lines 0 through col_chars-2.
    Doesn't need to clear line col_chars-1. */
void trs_screen_scroll()
{
    int i = 0;

    XCopyArea(display,window,window,gc,0,cur_char_height,
	      (cur_char_width*row_chars),(cur_char_height*col_chars),0,0);
    for (i = row_chars; i < screen_chars; i++)
	trs_screen[i-row_chars] = trs_screen[i];
}

void trs_screen_write_chars(locations, values, count)
int *locations;
int *values;
int count;
{
    while(count--)
    {
        trs_screen_write_char(*locations++, *values++ , False);
    }
#ifdef DO_XFLUSH
    XFlush(display);
#endif
}

int trs_next_key(wait)
     int wait;
{
#if KBWAIT
    if (wait) {
	int rval;
	for (;;) {
	    if ((rval = dequeue_key()) >= 0) break;
	    if ((z80_state.nmi && !z80_state.nmi_seen) ||
		(z80_state.irq && z80_state.iff1)) {
		rval = -1;
		break;
	    }
	    pause(); /* Wait for SIGALRM or SIGIO */
	    trs_get_event(0);
	}
	return rval;
    }
#endif
    return dequeue_key();

}
