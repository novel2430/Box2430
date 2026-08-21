#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>

int main(void)
{
    struct sigaction action;
    if (sigaction(SIGCHLD, NULL, &action) < 0 || action.sa_handler != SIG_DFL) {
        fprintf(stderr, "x11-sigchld-client: SIGCHLD is not SIG_DFL\n");
        return 1;
    }

    Display *display = XOpenDisplay(NULL);
    if (!display) return 2;
    int screen = DefaultScreen(display);
    Window window = XCreateSimpleWindow(
        display, RootWindow(display, screen), 20, 20, 160, 80, 0,
        BlackPixel(display, screen), WhitePixel(display, screen));
    XStoreName(display, window, "SpawnSigchldDefault");
    Atom pid_atom = XInternAtom(display, "_NET_WM_PID", False);
    unsigned long pid = (unsigned long)getpid();
    XChangeProperty(display, window, pid_atom, XA_CARDINAL, 32,
                    PropModeReplace, (unsigned char *)&pid, 1);
    XMapWindow(display, window);
    XFlush(display);
    for (;;) pause();
}
