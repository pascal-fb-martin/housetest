/* HouseTest - A test client for web-based services.
 *
 * Copyright 2022, Pascal Martin
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 *
 * housetest.c - Main loop of the housetest program.
 *
 * SYNOPSYS:
 *
 */

#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <signal.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

#include "echttp.h"
#include "echttp_cors.h"
#include "echttp_json.h"
#include "echttp_static.h"

#define HOUSETEST_MAX 256
static int housetest_count = 0;
static int housetest_cursor = 0;
static FILE *housetest_current = 0;
static const char *housetest_files[HOUSETEST_MAX];

static struct timeval housetest_pending = {0, 0};

static char housetest_method[128];
static char housetest_data[1024];

static void housetest_open (void) {

    for (;;) {
        if (housetest_current) fclose (housetest_current);
        if (housetest_cursor >= housetest_count) {
            printf ("== End of tests\n");
            exit (0);
        }
        const char *file = housetest_files[housetest_cursor++];
        printf ("======== Test %s\n", file);
        housetest_current = fopen (file, "r");
        if (housetest_current) break;

        printf ("** %s: %s\n", file, strerror(errno));
    }
}

static void housetest_next (void);

static void housetest_response (void *origin, int status,
                                char *data, int length) {

    struct timeval now;

    gettimeofday (&now, 0);
    if (now.tv_usec < housetest_pending.tv_usec) {
        now.tv_usec += 1000000;
        now.tv_sec -= 1;
    }
    printf ("-- Response after %lld.%06lld seconds\n",
            (long long)(now.tv_sec - housetest_pending.tv_sec),
            (long long)(now.tv_usec - housetest_pending.tv_usec));

    const char *redirection = echttp_attribute_get("Location");
    status = echttp_redirected(housetest_method);
    if (!status) {
        printf ("-- Redirected %s to %s\n", housetest_method, redirection);
        echttp_submit (housetest_data, strlen(housetest_data),
                       housetest_response, 0);
        return;
    }

    if (status >= 200 && status <= 299) {
        printf ("-- HTTP status %d\n", status);
        if (data && length)
            printf ("%s\n", data);
    } else {
        printf ("** HTTP error %d\n", status);
    }
    fflush(stdout);
    housetest_next ();
}
                                
static void housetest_next (void) {

    for (;;) {
        int i;
        char command[1024];
        char data[1024];

        command[0] = 0;
        if (!fgets (command, sizeof(command), housetest_current)) {
            housetest_open();
            continue;
        }
        for (i = 0; command[i]; i++) if (command[i] == '\n') command[i] = 0;
        i = 0;
        while (isspace(command[i])) i += 1;
        if (command[i] == 0) continue;
        if (command[i] == '#') {
            while (isspace(command[++i]));
            printf ("## %s\n", command+i);
            continue;
        }

        const char *method = command+i;
        while (isalpha(command[i])) i += 1;
        command[i++] = 0; // Split.
        while (isspace(command[i])) i += 1;
        const char *url = command+i;

        printf ("== %s %s\n", method, url);
        data[0] = 0;
        if (!strcmp(method, "POST") || !strcmp(method, "PUT")) {
            if (!fgets (data, sizeof(data), housetest_current)) {
                housetest_open();
                continue;
            }
            if (data[0] != '+') {
                printf ("** Invalid test file (no data)\n");
                housetest_open();
                continue;
            }
            printf ("-- Data to send: %s", data+1);
        }
        const char *error = echttp_client (method, url);
        if (error) {
            printf ("** %s %s: %s\n", method, url, error);
            continue;
        }

        snprintf (housetest_method, sizeof(housetest_method), "%s", method);
        if (data[0])
            snprintf (housetest_data, sizeof(housetest_data), "%s", data+1);
        else
            housetest_data[0] = 0;

        echttp_submit (housetest_data, strlen(housetest_data),
                       housetest_response, 0);
        gettimeofday (&housetest_pending, 0);
        break;
    }
}

static void housetest_start (void) {
    if (housetest_count > 0) {
        housetest_open ();
    } else {
        housetest_current = stdin;
    }
    housetest_next ();
}

static void housetest_background (int fd, int mode) {

    if (housetest_pending.tv_sec) {
        if (time(0) > housetest_pending.tv_sec + 30) {
            printf ("** Test abort\n");
            exit (1);
        }
    } else {
        housetest_start();
    }
}

static void housetest_protect (const char *method, const char *uri) {
    echttp_cors_protect(method, uri);
}

int main (int argc, const char **argv) {

    int i;

    // These strange statements are to make sure that fds 0 to 2 are
    // reserved, since this application might output some errors.
    // 3 descriptors are wasted if 0, 1 and 2 are already open. No big deal.
    //
    open ("/dev/null", O_RDONLY);
    dup(open ("/dev/null", O_WRONLY));

    signal(SIGPIPE, SIG_IGN);

    echttp_default ("-http-service=dynamic");
    argc = echttp_open (argc, argv);

    echttp_cors_allow_method("GET");
    echttp_protect (0, housetest_protect);

    if (argc >= 256) argc = 256; // Avoid array overflow.
    for (i = 1; i < argc; ++i) {
        if (*(argv[i]) != '-') {
            housetest_files[housetest_count++] = argv[i];
        }
    }

    echttp_static_route ("/", "/usr/local/share/house/public");
    echttp_background (&housetest_background);
    echttp_loop();
}

