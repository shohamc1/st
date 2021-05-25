#define _XOPEN_SOURCE 600
#include <ctype.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>
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

bool pt_pair(struct PTY *pty)
{
    // open pseudoterminal master
    char *slave_name;

    pty->master = posix_openpt(O_RDWR | O_NOCTTY); // master can read/write and do not make controlling terminal for process
    if (pty->master == -1)
    {
        perror("posix_openpt");
        return false;
    }

    // grant master access to slave pseudoterminal
    if (grantpt(pty->master) == -1)
    {
        perror("grantpt");
        return false;
    }

    // unlock slave to be used by master
    if (unlockpt(pty->master) == -1)
    {
        perror("unlockpt");
        return false;
    }

    slave_name = ptsname(pty->master); // get name of slave pseudoterminal
    if (slave_name == NULL)
    {
        perror("slave_name");
        return false;
    }

    pty->slave = open(slave_name, O_RDWR | O_NOCTTY); // create slave file descriptor
    if (pty->slave == -1)
    {
        perror("open(slave)");
        return false;
    }

    return true;
}

bool term_set_size(struct PTY *pty, struct X11 *x11)
{
    // update window size
    struct winsize ws =
        {
            .ws_col = x11->buf_w,
            .ws_row = x11->buf_h,
        };

    if (ioctl(pty->master, TIOCSWINSZ, &ws) == -1)
    {
        perror("ioctl(TIOCSWINSZ)");
        return false;
    }

    return true;
}

bool spawn(struct PTY *pty)
{
    // spawn slave terminal
    pid_t pid;
    char *env[] = {"TERM=dumb", NULL};

    pid = fork();
    if (pid == 0)
    {
        close(pty->master);

        // create new session and this the controlling terminal
        setsid();

        if (ioctl(pty->slave, TIOCSCTTY, NULL) == -1)
        {
            perror("ioctl(TIOCSCTTY)");
            return false;
        }

        dup2(pty->slave, 0);
        dup2(pty->slave, 1);
        dup2(pty->slave, 2);
        close(pty->slave);

        execle(SHELL, "-" SHELL, (char *)NULL, env);
        return false;
    }
    else if (pid > 0)
    {
        close(pty->slave);
        return true;
    }
    perror("fork()");
    return false;
}

void x11_redraw(struct X11 *x11)
{
    int x, y;
    char buf[1];

    XSetForeground(x11->display, x11->termgc, x11->col_bg);
    XFillRectangle(x11->display, x11->terminal_window, x11->termgc, 0, 0, x11->w, x11->h);

    XSetForeground(x11->display, x11->termgc, x11->col_fg);
    for (y = 0; y < x11->buf_h; y++)
    {
        for (x = 0; x < x11->buf_w; x++)
        {
            buf[0] = x11->buf[y * x11->buf_w + x];
            if (!iscntrl(buf[0]))
            {
                XDrawString(x11->display, x11->terminal_window,
                            x11->termgc, x * x11->font_width,
                            y * x11->font_height + x11->xfont->ascent,
                            buf, 1);
            }
        }
    }

    XSetForeground(x11->display, x11->termgc, x11->col_fg);
    XFillRectangle(x11->display, x11->terminal_window, x11->termgc,
                   x11->buf_x * x11->font_width,
                   x11->buf_y * x11->font_height,
                   x11->font_width, x11->font_height);

    XSync(x11->display, False);
}

void x11_key(XKeyEvent *ev, struct PTY *pty)
{
    char buf[32];
    int i, num;
    KeySym keysym;

    num = XLookupString(ev, buf, sizeof buf, &keysym, 0);
    for (i = 0; i < num; i++)
        write(pty->master, &buf[i], 1);
}

int run(struct PTY *pty, struct X11 *x11)
{
    int i, maxfd;
    fd_set readable;
    XEvent xevent;
    char buf[1];
    bool just_wrapped = false;

    maxfd = pty->master > x11->fd ? pty->master : x11->fd;

    for (;;)
    {
        FD_ZERO(&readable);
        FD_SET(pty->master, &readable);
        FD_SET(x11->fd, &readable);

        if (select(maxfd + 1, &readable, NULL, NULL, NULL) == -1)
        {
            perror("select");
            return 1;
        }

        if (FD_ISSET(pty->master, &readable))
        {
            if (read(pty->master, buf, 1) <= 0)
            {
                // child is exiting
                fprintf(stderr, "Nothing to read from child\n");
                perror(NULL);
                return 1;
            }

            switch (buf[0])
            {
            case '\r':
                // carriage return, move cursor to first column
                x11->buf_x = 0;
                break;
            case '\b':
                x11->buf_x = x11->buf_x - 1;
                break;
            case '\n':
                if (!just_wrapped)
                {
                    // new line is read (not from overflow)
                    x11->buf_y++;
                    just_wrapped = false;
                }
                break;
            default:
                // regular byte, store and move to the right
                x11->buf[x11->buf_y * x11->buf_w + x11->buf_x] = buf[0];
                x11->buf_x++;

                if (x11->buf_x >= x11->buf_w)
                {
                    // overflow to next line
                    x11->buf_x = 0;
                    x11->buf_y++;
                    just_wrapped = true;
                }
                else
                    just_wrapped = false;
            }

            if (x11->buf_y >= x11->buf_h)
            {
                // we have hit bottom of the window
                // scroll down one line to fit content

                memmove(x11->buf, &x11->buf[x11->buf_w], x11->buf_w * (x11->buf_h - 1));
                x11->buf_y = x11->buf_h - 1;

                for (i = 0; i < x11->buf_w; i++)
                    x11->buf[x11->buf_y * x11->buf_w + i] = 0; // clear new line
            }

            x11_redraw(x11);
        }

        if (FD_ISSET(x11->fd, &readable))
        {
            while (XPending(x11->display))
            {
                XNextEvent(x11->display, &xevent);
                switch (xevent.type)
                {
                case Expose:
                    x11_redraw(x11);
                    break;
                case KeyPress:
                    x11_key(&xevent.xkey, pty);
                    break;
                }
            }
        }
    }

    return 0;
}

int main()
{
    struct PTY pty;
    struct X11 x11;

    if (!x11_setup(&x11))
        return 1;

    if (!pt_pair(&pty))
        return 1;

    if (!term_set_size(&pty, &x11))
        return 1;

    if (!spawn(&pty))
        return 1;

    return run(&pty, &x11);
}
