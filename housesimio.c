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
#include "housediscover.h"
#include "houselog.h"
#include "houseconfig.h"
#include "housestate.h"
#include "housedepositor.h"

static char HostName[256];

#define DEBUG if (echttp_isdebug()) printf

struct SimIoMap {
    const char *name;
    const char *gear;
    char *state;
    time_t deadline;
};

static struct SimIoMap *SimIoDb = 0;

static int LiveState = -1;

static int SimIoCount = 0;

static void simio_refresh (void) {

    int i;
    int newcount;
    int oldcount = SimIoCount;
    struct SimIoMap *olddb = SimIoDb;

    int points = houseconfig_array (0, ".simio.points");
    if (points < 0) {
        DEBUG ("Cannot find points array\n");
        return;
    }

    newcount = houseconfig_array_length (points);
    if (newcount < 0) {
        DEBUG ("No point found\n");
        return;
    }
    DEBUG ("found %d points\n", newcount);

    SimIoDb = calloc (newcount, sizeof(struct SimIoMap));
    int *pointlist = calloc (newcount, sizeof(int));
    SimIoCount = houseconfig_enumerate (points, pointlist, newcount);

    for (i = 0; i < SimIoCount; ++i) {
        int point = pointlist[i];
        if (point > 0) {
            SimIoDb[i].name = houseconfig_string (point, ".name");
            SimIoDb[i].gear = houseconfig_string (point, ".gear");
            SimIoDb[i].deadline = 0;
            SimIoDb[i].state = 0;
        }
    }
    free (pointlist);

    if (olddb) {
        // Maintain the pre-existing state for those points still present.
        for (i = 0; i < oldcount; ++i) {
            int j;
            for (j = 0; j < SimIoCount; ++j) {
               if (strcmp (olddb[i].name, SimIoDb[j].name) == 0) {
                   SimIoDb[j].state = olddb[i].state;
                   SimIoDb[j].deadline = olddb[i].deadline;
                   olddb[i].state = 0; // Do not free: reused.
                   break;
               }
            }
            if (olddb[i].state) free (olddb[i].state);
        }
        free(olddb);
    }
    housestate_changed (LiveState);
}

static const char *simio_status (const char *method, const char *uri,
                                 const char *data, int length) {

    if (housestate_same (LiveState)) return "";

    static char buffer[65537];
    ParserToken token[1024];
    char pool[65537];
    int i;

    ParserContext context = echttp_json_start (token, 1024, pool, 65537);

    int root = echttp_json_add_object (context, 0, 0);
    echttp_json_add_string (context, root, "host", HostName);
    echttp_json_add_string (context, root, "proxy", houseportal_server());
    echttp_json_add_integer (context, root, "timestamp", (long long)time(0));
    echttp_json_add_integer (context, root, "latest", housestate_current(LiveState));
    int top = echttp_json_add_object (context, root, "control");
    int container = echttp_json_add_object (context, top, "status");

    for (i = 0; i < SimIoCount; ++i) {
        time_t pulsed = SimIoDb[i].deadline;
        const char *name = SimIoDb[i].name;
        const char *state = SimIoDb[i].state;
        if (!state) state = "off";
        const char *gear = SimIoDb[i].gear;

        int point = echttp_json_add_object (context, container, name);
        echttp_json_add_string (context, point, "state", state);
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
    int pulse;
    int i;
    int found = 0;
    char comment[256];

    if (!point) {
        echttp_error (404, "missing point name");
        return "";
    }
    if (!statep) {
        echttp_error (400, "missing state");
        return "";
    }

    // Optimization: use no storage for default state "off".
    const char *state = strcasecmp (statep, "off") ? statep : 0;

    // Special case: "clear" means a transition from "alert" to "on"
    if (!strcasecmp (statep, "clear")) state = "on";

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

           // Special case: "clear" means a transition from "alert" to "on" if
           // the point is in the "alert" state, silently ignored otherwise.
           if (!strcasecmp (statep, "clear")) {
              if (!SimIoDb[i].state) continue;
              if (strcasecmp (SimIoDb[i].state, "alert")) continue;
           }

           if (SimIoDb[i].state) free (SimIoDb[i].state);
           SimIoDb[i].state = state ? strdup (state) : 0;
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
    housestate_changed (LiveState);
    return simio_status (method, uri, data, length);
}

static const char *simio_config (const char *method, const char *uri,
                                   const char *data, int length) {

    if (strcmp ("GET", method) == 0) {
        echttp_content_type_json ();
        return houseconfig_current();
    } else if (strcmp ("POST", method) == 0) {
        const char *error = houseconfig_update (data);
        if (error) {
            echttp_error (400, error);
        } else {
            simio_refresh ();
            houselog_event ("SYSTEM", "CONFIG", "SAVE", "TO DEPOT %s", houseconfig_name());
            housedepositor_put ("config", houseconfig_name(), data, length);
        }
    } else {
        echttp_error (400, "invalid state value");
    }
    return "";
}

static void simio_background (int fd, int mode) {

    time_t now = time(0);
    int i;

    for (i = 0; i < SimIoCount; ++i) {
        if ((SimIoDb[i].deadline > 0) && (SimIoDb[i].deadline < now)) {
            if (SimIoDb[i].state) free (SimIoDb[i].state);
            SimIoDb[i].state = 0;
            SimIoDb[i].deadline = 0;
            houselog_event ("SIMIO", SimIoDb[i].name, "OFF", "END OF PULSE");
        }
    }
    houseportal_background (now);
    housediscover (now);
    houselog_background (now);
    housedepositor_periodic (now);
}

static void simio_protect (const char *method, const char *uri) {
    echttp_cors_protect(method, uri);
}

static void simio_config_listener (const char *name, time_t timestamp,
                                   const char *data, int length) {

    houselog_event ("SYSTEM", "CONFIG", "LOAD", "FROM DEPOT %s", name);
    if (!houseconfig_update (data)) simio_refresh ();
}

int main (int argc, const char **argv) {

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
        static const char *path[] = {"control:/simio"};
        houseportal_initialize (argc, argv);
        houseportal_declare (echttp_port(4), path, 1);
    }
    housediscover_initialize (argc, argv);
    houselog_initialize ("simio", argc, argv);
    housedepositor_initialize (argc, argv);

    houseconfig_default ("--config-name=simio");
    const char *error = houseconfig_load (argc, argv);
    if (error) {
        DEBUG ("Cannot load configuration: %s\n", error);
        houselog_trace
            (HOUSE_FAILURE, "CONFIG", "Cannot load configuration: %s\n", error);
    }

    LiveState = housestate_declare ("live");

    simio_refresh();
    housedepositor_subscribe ("config", houseconfig_name(), simio_config_listener);

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

