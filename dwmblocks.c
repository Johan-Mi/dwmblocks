#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifndef NO_X
#include <X11/Xlib.h>
#endif
#ifdef __OpenBSD__
#define SIGPLUS SIGUSR1 + 1
#define SIGMINUS SIGUSR1 - 1
#else
#define SIGPLUS SIGRTMIN
#define SIGMINUS SIGRTMIN
#endif
#define LENGTH(X) (sizeof(X) / sizeof(X[0]))
#define MIN(a, b) ((a < b) ? a : b)

typedef struct {
    char *icon;
    char *command;
    unsigned int interval;
    int signal;
} Block;
#ifndef __OpenBSD__
static void dummysighandler(int num);
#endif
static void sighandler(int num);
static void getcmds(int time);
static void getsigcmds(int signal);
static void setupsignals(void);
static void sighandler(int signum);
static int getstatus(char *str, char *last);
static void statusloop(void);
static void termhandler(int signum);
static void pstdout(void);
#ifndef NO_X
static void setroot(void);
static void (*writestatus)(void) = setroot;
static int setupX(void);
static Display *dpy;
static int screen;
static Window root;
#else
static void (*writestatus)() = pstdout;
#endif

#include "blocks.h"

enum {
    CMDLENGTH = 50,
    STATUSLENGTH = LENGTH(blocks) * CMDLENGTH,
};

static char statusbar[LENGTH(blocks)][CMDLENGTH + 1] = {0};
static char statusstr[2][STATUSLENGTH + 1];
static int statusContinue = 1;

// opens process *cmd and stores output in *output
static void getcmd(const Block *block, char output[static CMDLENGTH + 1]) {
    strlcpy(output, block->icon, CMDLENGTH + 1);
    FILE *cmdf = popen(block->command, "r");
    if (!cmdf) {
        return;
    }
    size_t i = strlen(block->icon);
    fgets(output + i, (int)(CMDLENGTH + 1 - i), cmdf);
    i = strlen(output);
    if (i == 0) {
        // return if block and command output are both empty
        pclose(cmdf);
        return;
    }

    // only chop off newline if one is present at the end
    if (output[i - 1] == '\n') {
        output[i - 1] = '\0';
    }

    pclose(cmdf);
}

static void getcmds(int time) {
    const Block *current;
    for (unsigned int i = 0; i < LENGTH(blocks); i++) {
        current = blocks + i;
        if (time < 0 || (current->interval != 0 &&
                         (unsigned int)time % current->interval == 0)) {
            getcmd(current, statusbar[i]);
        }
    }
}

static void getsigcmds(int signal) {
    const Block *current;
    for (unsigned int i = 0; i < LENGTH(blocks); i++) {
        current = blocks + i;
        if (current->signal == signal) {
            getcmd(current, statusbar[i]);
        }
    }
}

static void setupsignals(void) {
#ifndef __OpenBSD__
    /* initialize all real time signals with dummy handler */
    for (int i = SIGRTMIN; i <= SIGRTMAX; i++) {
        signal(i, dummysighandler);
    }
#endif

    for (unsigned int i = 0; i < LENGTH(blocks); i++) {
        if (blocks[i].signal > 0) {
            signal(SIGMINUS + blocks[i].signal, sighandler);
        }
    }
}

static int getstatus(
    char str[static STATUSLENGTH + 1], char last[static STATUSLENGTH + 1]
) {
    strlcpy(last, str, STATUSLENGTH + 1);

    char *p = str;
    size_t size = STATUSLENGTH + 1;
    for (unsigned int i = 0; i < LENGTH(blocks); i++) {
        int const n = snprintf(
            p, size, "%s%s", statusbar[i], i + 1 == LENGTH(blocks) ? "" : delim
        );
        p += n;
        size -= (size_t)n;
    }

    return strcmp(str, last); // 0 if they are the same
}

#ifndef NO_X
static void setroot(void) {
    if (!getstatus(
            statusstr[0], statusstr[1]
        )) { // Only set root if text has changed.
        return;
    }
    XStoreName(dpy, root, statusstr[0]);
    XFlush(dpy);
}

static int setupX(void) {
    dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "dwmblocks: Failed to open display\n");
        return 0;
    }
    screen = DefaultScreen(dpy);
    root = RootWindow(dpy, screen);
    return 1;
}
#endif

static void pstdout(void) {
    if (!getstatus(
            statusstr[0], statusstr[1]
        )) { // Only write out if text has changed.
        return;
    }
    printf("%s\n", statusstr[0]);
    fflush(stdout);
}

static void statusloop(void) {
    setupsignals();
    int i = 0;
    getcmds(-1);
    while (1) {
        getcmds(i++);
        writestatus();
        if (!statusContinue) {
            break;
        }
        sleep(1.0);
    }
}

#ifndef __OpenBSD__
/* this signal handler should do nothing */
static void dummysighandler(int signum) {
    (void)signum;
}
#endif

static void sighandler(int signum) {
    getsigcmds(signum - SIGPLUS);
    writestatus();
}

static void termhandler(int signum) {
    (void)signum;
    statusContinue = 0;
}

int main(int argc, char **argv) {
    for (int i = 0; i < argc; i++) { // Handle command line arguments
        if (!strcmp("-p", argv[i])) {
            writestatus = pstdout;
        }
    }
#ifndef NO_X
    if (!setupX()) {
        return 1;
    }
#endif
    signal(SIGTERM, termhandler);
    signal(SIGINT, termhandler);
    statusloop();
#ifndef NO_X
    XCloseDisplay(dpy);
#endif
    return 0;
}
