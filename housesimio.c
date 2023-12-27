/* housesimio - A simple home web server for simulating the real world
 *
 * Copyright 2020, Pascal Martin
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
 * housesimio.c - Simulation I/O controls.
 *
 * SYNOPSYS:
 *
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "echttp.h"
#include "echttp_cors.h"
#include "echttp_json.h"
#include "echttp_static.h"
#include "houseportalclient.h"
#include "houselog.h"
#include "houseconfig.h"

static int  UseHousePortal = 0;
static char HostName[256];

struct SimIoMap {
    const char *name;
    const char *gear;
    char *status;
    int value;
    int commanded;
    time_t deadline;
};

static struct SimIoMap *SimIoDb = 0;

static int SimIoCount = 0;

const char *simio_refresh (void) {
    int i;
    int oldcount = SimIoCount;
    struct SimIoMap *old = SimIoDb;

    int points = houseconfig_array (0, ".simio.points");
    if (points < 0) return "cannot find points array";

    SimIoCount = houseconfig_array_length (points);
    if (SimIoCount < 0) return "no point found";
    if (echttp_isdebug()) fprintf (stderr, "found %d points\n", SimIoCount);

    SimIoDb = calloc (sizeof(struct SimIoMap), SimIoCount);

    for (i = 0; i < SimIoCount; ++i) {
        int point;
        char path[128];
        snprintf (path, sizeof(path), "[%d]", i);
        point = houseconfig_object (points, path);
        if (point > 0) {
            SimIoDb[i].name = houseconfig_string (point, ".name");
            SimIoDb[i].gear = houseconfig_string (point, ".gear");
            SimIoDb[i].commanded = 0;
            SimIoDb[i].deadline = 0;
            SimIoDb[i].value = 0;
        }
    }

    if (old) {
        // Maintain the pre-existing state for those points still present.
        for (i = 0; i < oldcount; ++i) {
            int j;
            for (j = 0; j < SimIoCount; ++j) {
               if (strcmp (old[i].name, SimIoDb[j].name) == 0) {
                   SimIoDb[j].value = old[i].value;
                   SimIoDb[j].commanded = old[i].commanded;
                   SimIoDb[j].deadline = old[i].deadline;
                   break;
               }
            }
        }
        free(old);
    }
}

static const char *simio_status (const char *method, const char *uri,
                                 const char *data, int length) {
    static char buffer[65537];
    ParserToken token[1024];
    char pool[65537];
    int i;

    ParserContext context = echttp_json_start (token, 1024, pool, 65537);

    int root = echttp_json_add_object (context, 0, 0);
    echttp_json_add_string (context, root, "host", HostName);
    echttp_json_add_string (context, root, "proxy", houseportal_server());
    echttp_json_add_integer (context, root, "timestamp", (long)time(0));
    int top = echttp_json_add_object (context, root, "control");
    int container = echttp_json_add_object (context, top, "status");

    for (i = 0; i < SimIoCount; ++i) {
        time_t pulsed = SimIoDb[i].deadline;
        const char *name = SimIoDb[i].name;
        const char *status = SimIoDb[i].status;
        if (!status) status = SimIoDb[i].value?"on":"off";
        const char *commanded = SimIoDb[i].commanded?"on":"off";
        const char *gear = SimIoDb[i].gear;

        int point = echttp_json_add_object (context, container, name);
        echttp_json_add_string (context, point, "state", status);
        echttp_json_add_string (context, point, "command", commanded);
        if (pulsed)
            echttp_json_add_integer (context, point, "pulse", (int)pulsed);
        if (gear && gear[0] != 0)
            echttp_json_add_string (context, point, "gear", gear);
    }
    const char *error = echttp_json_export (context, buffer, 65537);
    if (error) {
        echttp_error (500, error);
        return "";
    }
    echttp_content_type_json ();
    return buffer;
}

static const char *simio_set (const char *method, const char *uri,
                                const char *data, int length) {

    const char *point = echttp_parameter_get("point");
    const char *statep = echttp_parameter_get("state");
    const char *pulsep = echttp_parameter_get("pulse");
    const char *cause = echttp_parameter_get("cause");
    int state;
    int pulse;
    int i;
    int found = 0;
    char comment[256];

    if (!point) {
        echttp_error (404, "missing point name");
        return "";
    }
    if (!statep) {
        echttp_error (400, "missing state value");
        return "";
    }
    if ((strcmp(statep, "on") == 0) || (strcmp(statep, "1") == 0)) {
        state = 1;
    } else if ((strcmp(statep, "off") == 0) || (strcmp(statep, "0") == 0)) {
        state = 0;
    } else {
        echttp_error (400, "invalid state value");
        houselog_event ("SIMIO", point, statep, "INVALID STATE");
        return "";
    }

    pulse = pulsep ? atoi(pulsep) : 0;
    if (pulse < 0) {
        echttp_error (400, "invalid pulse value");
        houselog_event ("SIMIO", point, statep, "INVALID PULSE %s", pulsep);
        return "";
    }

    int is_all = (strcmp (point, "all") == 0);
    if (cause)
        snprintf (comment, sizeof(comment), " (%s)", cause);
    else
        comment[0] = 0;

    for (i = 0; i < SimIoCount; ++i) {
       if (is_all || (strcmp (point, SimIoDb[i].name) == 0)) {
           found = 1;
           SimIoDb[i].value = state;
           SimIoDb[i].commanded = state;
           if (pulse) {
               SimIoDb[i].deadline = time(0) + pulse;
               houselog_event ("SIMIO", point, statep,
                               "FOR %d SECONDS%s", pulse, comment);
           } else {
               SimIoDb[i].deadline = 0;
               houselog_event ("SIMIO", point, statep, "LATCHED%s", comment);
           }
       }
    }

    if (! found) {
        echttp_error (404, "invalid point name");
        houselog_event ("SIMIO", point, statep, "INVALID POINT");
        return "";
    }
    return simio_status (method, uri, data, length);
}

static const char *simio_config (const char *method, const char *uri,
                                   const char *data, int length) {

    if (strcmp ("GET", method) == 0) {
        echttp_transfer (houseconfig_open(), houseconfig_size());
        echttp_content_type_json ();
    } else if (strcmp ("POST", method) == 0) {
        const char *error = houseconfig_update (data);
        if (error) echttp_error (400, error);
        simio_refresh ();
    } else {
        echttp_error (400, "invalid state value");
    }
    return "";
}

static void simio_background (int fd, int mode) {

    static time_t LastRenewal = 0;
    time_t now = time(0);
    int i;

    if (UseHousePortal) {
        static const char *path[] = {"control:/simio"};
        if (now >= LastRenewal + 60) {
            if (LastRenewal > 0)
                houseportal_renew();
            else
                houseportal_register (echttp_port(4), path, 1);
            LastRenewal = now;
        }
    }
    for (i = 0; i < SimIoCount; ++i) {
        if ((SimIoDb[i].deadline > 0) && (SimIoDb[i].deadline < now)) {
            SimIoDb[i].value = 1 - SimIoDb[i].commanded;
            SimIoDb[i].commanded = SimIoDb[i].value;
            SimIoDb[i].deadline = 0;
            const char *state = SimIoDb[i].value?"ON":"OFF";
            houselog_event ("SIMIO", SimIoDb[i].name, state, "END OF PULSE");
        }
    }
    houselog_background (now);
}

static void simio_protect (const char *method, const char *uri) {
    echttp_cors_protect(method, uri);
}

int main (int argc, const char **argv) {

    char path[256];
    char cfgoption[512];

    // These strange statements are to make sure that fds 0 to 2 are
    // reserved, since this application might output some errors.
    // 3 descriptors are wasted if 0, 1 and 2 are already open. No big deal.
    //
    open ("/dev/null", O_RDONLY);
    dup(open ("/dev/null", O_WRONLY));

    gethostname (HostName, sizeof(HostName));

    echttp_default ("-http-service=dynamic");

    argc = echttp_open (argc, argv);
    if (echttp_dynamic_port()) {
        houseportal_initialize (argc, argv);
        UseHousePortal = 1;
    }
    houselog_initialize ("simio", argc, argv);

    getcwd(path, sizeof(path));
    snprintf (cfgoption, sizeof(cfgoption), "--config=%s/simio.json", path);
    houseconfig_default (cfgoption);
    const char *error = houseconfig_load (argc, argv);
    if (error) {
        houselog_trace
            (HOUSE_FAILURE, "CONFIG", "Cannot load configuration: %s\n", error);
    }
    simio_refresh();

    echttp_cors_allow_method("GET");
    echttp_protect (0, simio_protect);

    echttp_route_uri ("/simio/status", simio_status);
    echttp_route_uri ("/simio/set",    simio_set);

    echttp_route_uri ("/simio/config", simio_config);

    echttp_static_route ("/", "/usr/local/share/house/public");
    echttp_background (&simio_background);
    houselog_event ("SERVICE", "simio", "STARTED", "ON %s", houselog_host());
    echttp_loop();
}

