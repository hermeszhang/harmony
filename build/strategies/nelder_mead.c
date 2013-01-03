/*
 * Copyright 2003-2013 Jeffrey K. Hollingsworth
 *
 * This file is part of Active Harmony.
 *
 * Active Harmony is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Active Harmony is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with Active Harmony.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "strategy.h"
#include "session-core.h"
#include "hsession.h"
#include "hutil.h"
#include "hcfg.h"
#include "defaults.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <math.h>
#include <tcl.h>

hpoint_t best;
double best_perf;

/* Be sure all remaining definitions are declared static to avoid
 * possible namspace conflicts in the GOT due to PIC behavior.
 */
static Tcl_Interp *tcl;
static hpoint_t curr;
static int client_count;

static char *buf;
static int buflen;

static int simulate_application_file(char **buf, int *cap, int id);

/*
 * Invoked once on strategy load.
 */
int strategy_init(hmesg_t *mesg)
{
    const char *cfgval;
    int i, retval;

    best = HPOINT_INITIALIZER;
    if (hpoint_init(&curr, sess->sig.range_len) < 0)
        return -1;
    best_perf = INFINITY;

    tcl = Tcl_CreateInterp();
    if (!tcl || Tcl_Init(tcl) != TCL_OK) {
        mesg->data.string = tcl->result;
        return -1;
    }

    buf = NULL;
    buflen = 0;
    cfgval = hcfg_get(sess->cfg, CFGKEY_HARMONY_ROOT);
    if (!cfgval) {
        mesg->data.string = "Could not determine Harmony root directory";
        return -1;
    }
    if (Tcl_SetVar(tcl, "harmony_root", cfgval, TCL_GLOBAL_ONLY) == NULL) {
        mesg->data.string = tcl->result;
        return -1;
    }

    retval = snprintf_grow(&buf, &buflen, "%s/libexec/%s", cfgval, 
                           hcfg_get(sess->cfg, CFGKEY_NM_INITFILE));
    if (retval < 0) {
        mesg->data.string = "Could not allocate memory for Tcl init file";
        return -1;
    }

    if (Tcl_EvalFile(tcl, buf) != TCL_OK) {
        mesg->data.string = tcl->result;
        return -1;
    }

    client_count = 0;
    cfgval = hcfg_get(sess->cfg, CFGKEY_CLIENT_COUNT);
    if (cfgval)
        client_count = atoi(cfgval);
    if (client_count <= 0)
        client_count = 1;

    for (i = 1; i <= client_count; ++i) {
        if (simulate_application_file(&buf, &buflen, i) < 0) {
            mesg->data.string = strerror(errno);
            return -1;
        }

        if (Tcl_Eval(tcl, buf) != TCL_OK) {
            mesg->data.string = tcl->result;
            return -1;
        }
    }

    if (Tcl_SetVar(tcl, "code_generation_params(enabled)", "0",
                   TCL_GLOBAL_ONLY) == NULL)
    {
        mesg->data.string = tcl->result;
        return -1;
    }

    if (hcfg_set(sess->cfg, CFGKEY_STRATEGY_CONVERGED, "0") < 0) {
        mesg->data.string = "Could not set search convergence status";        
        return -1;
    }

    return 0;
}

/*
 * Generate a new candidate configuration point in the space provided
 * by the hpoint_t parameter.
 */
int strategy_fetch(hmesg_t *mesg)
{
    const char *retval;
    int i, count, client_id;

    client_id = (curr.id % client_count) + 1;

    for (i = 0; i < sess->sig.range_len; ++i) {
        /* constructing the Tcl variable name: the way variables are
         * represented in the backend is as follows:
         * <appname>_<client socket>_bundle_<name of the variable>
         *
         * In this case, appname will always be "nm".
         */
        count = snprintf_grow(&buf, &buflen, "nm_%d_bundle_%s(value)",
                              client_id, sess->sig.range[i].name);
        if (count < 0) {
            mesg->data.string = "Could not allocate space for Tcl command";
            goto error;
        }

        retval = Tcl_GetVar(tcl, buf, 0);
        if (retval == NULL) {
            mesg->data.string = tcl->result;
            goto error;
        }

        curr.idx[i] = atoi(retval);
    }

    count = snprintf_grow(&buf, &buflen, "nm_%d_simplex_time", client_id);
    if (count < 0) {
        mesg->data.string = "Could not allocate space for Tcl command";
        goto error;
    }

    retval = Tcl_GetVar(tcl, buf, 0);
    if (retval == NULL) {
        mesg->data.string = tcl->result;
        goto error;
    }
    curr.step = atoi(retval);

    mesg->data.fetch.cand = HPOINT_INITIALIZER;
    if (hpoint_copy(&mesg->data.fetch.cand, &curr) < 0)
        goto error;

    /* Send best point information, if needed. */
    if (mesg->data.fetch.best.id < best.id) {
        mesg->data.fetch.best = HPOINT_INITIALIZER;
        if (hpoint_copy(&mesg->data.fetch.best, &best) < 0)
            goto error;
    }
    else {
        mesg->data.fetch.best = HPOINT_INITIALIZER;
    }

    mesg->status = HMESG_STATUS_OK;
    ++curr.id;
    return 0;

  error:
    mesg->status = HMESG_STATUS_FAIL;
    return -1;
}

/*
 * Inform the search strategy of an observed performance associated with
 * a configuration point.
 */
int strategy_report(hmesg_t *mesg)
{
    int retval, count, client_id;
    const char *tclval;

    /* Update the best performing point, if necessary. */
    if (best_perf > mesg->data.report.perf) {
        best_perf = mesg->data.report.perf;
        if (hpoint_copy(&best, &mesg->data.report.cand) < 0) {
            mesg->status = HMESG_STATUS_FAIL;
            mesg->data.string = strerror(errno);
            return -1;
        }
    }

    client_id = (mesg->data.report.cand.id % client_count) + 1;
    count = snprintf_grow(&buf, &buflen, "updateObsGoodness nm_%d %.17lg %d",
                          client_id, mesg->data.report.perf,
                          mesg->data.report.cand.step);
    if (count < 0) {
        mesg->data.string = "Could not allocate space for Tcl command";
        return -1;
    }

    retval = Tcl_Eval(tcl, buf);
    if (retval != TCL_OK) {
        mesg->data.string = tcl->result;
        return -1;
    }

    /* Check Tcl backend to see if search has converged. */
    count = snprintf_grow(&buf, &buflen, "nm_%d_search_done", client_id);
    if (count < 0) {
        mesg->data.string = "Could not allocate space for Tcl command";
        return -1;
    }

    tclval = Tcl_GetVar(tcl, buf, 0);
    if (tclval == NULL) {
        mesg->data.string = "Could not retrieve search convergence status";
        return -1;
    }

    if (atoi(tclval) == 1) {
        if (hcfg_set(sess->cfg, CFGKEY_STRATEGY_CONVERGED, "1") < 0) {
            mesg->data.string = "Could not set search convergence status";
            return -1;
        }
    }

    mesg->status = HMESG_STATUS_OK;
    return 0;
}

/*
 * Create a harmony application file from the registered bundles.
 * This should be removed in favor of a "signature" connection message.
 * Such a signature message might contain the following info:
 *
 *	[appName, bundles, constraints]
 */
int simulate_application_file(char **buf, int *cap, int id)
{
    int i, count, total, len;
    char *ptr;

  top:
    ptr = *buf;
    len = *cap;
    count = snprintf_serial(&ptr, &len, "harmonyApp nm {\n");
    if (count < 0)
        return -1;
    total = count;

    for (i = 0; i < sess->sig.range_len; ++i) {
        /* Internally, all ranges are reduced to the integer domain. */
        count = snprintf_serial(&ptr, &len,
                                "{harmonyBundle %s {int {0 %d 1} global}}\n",
                                sess->sig.range[i].name,
                                sess->sig.range[i].max_idx - 1);
        if (count < 0)
            return -1;
        total += count;
    }

    count = snprintf_serial(&ptr, &len,
                            " { obsGoodness 0 0 global }"
                            " { predGoodness 0 0 } } %d", id);
    if (count < 0)
        return -1;
    total += count;

    if (*cap <= total) {
        ptr = (char *) realloc(*buf, total + 1);
        if (!ptr) return -1;

        *buf = ptr;
        *cap = total + 1;
        goto top;
    }

    return 0;
}