#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <X11/Xutil.h>
#include <X11/Xlib.h>

#define SHELL "/bin/bash"

struct PTY // pseudoterminal, see `man pty`
{
    int master, slave;
};

struct X11
{
    int fd;           // file descriptor of X11 connection
    Display *display; // X11 display
    int screen;       // screen (monitor) number
    Window root;      // root window

    Window terminal_window;       // terminal window
    GC termgc;                    // graphics context, used to draw stuff on the window
    unsigned long col_fg, col_bg; // foreground and background colour variables
    int w, h;                     // width and height

    XFontStruct *xfont;          // font to use
    int font_width, font_height; // font width and height

    char *buf;        // window buffer
    int buf_w, buf_h; // width and height of window
    int buf_x, buf_y; // cursor position
};

bool x11_setup(struct X11 *x11)
{
    Colormap cmap;
    XColor color; // stores colour values

    XSetWindowAttributes wa = {
        .background_pixmap = ParentRelative,                        // use parent's pixmap if available, or use default
        .event_mask = KeyPressMask | KeyReleaseMask | ExposureMask, // allow window to read keypresses and expose events (alerts)
    };

    x11->display = XOpenDisplay(NULL); // connect to X11 server
    if (x11->display == NULL)
    {
        fprintf(stderr, "Cannot open display! \n");
        return false;
    }

    x11->screen = DefaultScreen(x11->display);         // select the monitor
    x11->root = RootWindow(x11->display, x11->screen); // root window on the monitor, this one covers the whole screen
    x11->fd = ConnectionNumber(x11->display);          // file descriptor of the connection

    x11->xfont = XLoadQueryFont(x11->display, "fixed"); // use core X11 fixed font
    if (x11->xfont == NULL)
    {
        fprintf(stderr, "Could not load font!\n");
        return false;
    }
    x11->font_width = XTextWidth(x11->xfont, "m", 1);            // returns the width of one m character
    x11->font_height = x11->xfont->ascent + x11->xfont->descent; // top edge of character - bottom edge of characater

    cmap = DefaultColormap(x11->display, x11->screen);

    if (!XAllocNamedColor(x11->display, cmap, "#000000", &color, &color))
    {
        fprintf(stderr, "Could not load background colour!\n");
        return false;
    } // after this colour contains the colour (RGB or as int/pixel) values for black
    x11->col_bg = color.pixel;

    // same for foreground colour
    if (!XAllocNamedColor(x11->display, cmap, "#aaaaaa", &color, &color))
    {
        fprintf(stderr, "Could not load foreground colour!\n");
        return false;
    }
    x11->col_fg = color.pixel;

    // use fixed size of 100x40 cells, no resizing
    // child proceses will be unable to ask for current size
    x11->buf_w = 100;
    x11->buf_h = 40;
    x11->buf_x = 0;
    x11->buf_y = 0;
    x11->buf = calloc(x11->buf_w * x11->buf_h, 1);
    if (x11->buf == NULL)
    {
        perror("calloc");
        return false;
    }

    x11->w = x11->buf_w * x11->font_width;  // raw width
    x11->h = x11->buf_h * x11->font_height; // raw height

    // create terminal window
    x11->terminal_window = XCreateWindow(x11->display, x11->root,                    // display and parent window to create window in
                                         0, 0,                                       // position of top left corner in parent window
                                         x11->w, x11->h,                             //width and height of new window
                                         0, DefaultDepth(x11->display, x11->screen), //border width and depth of window (depth = bits per pixel)
                                         CopyFromParent,                             // set window class (set same as parent)
                                         DefaultVisual(x11->display, x11->screen),   // set visual structure
                                         CWBackPixmap | CWEventMask, &wa);           // set value mask and window attributes
    XStoreName(x11->display, x11->terminal_window, "st");                            // set window title
    XMapWindow(x11->display, x11->terminal_window);                                  // map window to display
    x11->termgc = XCreateGC(x11->display, x11->terminal_window, 0, NULL);            // attach graphical context

    XSync(x11->display, False); // refresh display, do not discard all X events in queue
    return true;
}

int main()
{
    struct PTY pty;
    struct X11 x11;

    if (!x11_setup(&x11))
        return 1;

    sleep(10);
}
