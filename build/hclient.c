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

#include "hclient.h"
#include "hmesg.h"
#include "hutil.h"
#include "hsockutil.h"
#include "defaults.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <assert.h>

typedef enum harmony_state_t {
    HARMONY_STATE_UNKNOWN,
    HARMONY_STATE_INIT,
    HARMONY_STATE_CONNECTED,
    HARMONY_STATE_READY,
    HARMONY_STATE_TESTING,
    HARMONY_STATE_CONVERGED,

    HARMONY_STATE_MAX
} harmony_state_t;

struct hdesc_t {
    harmony_state_t state;
    int socket;
    char id[MAX_ID_LEN];

    hsession_t sess;
    hmesg_t mesg;

    void **ptr;
    int ptr_len;
    int ptr_cap;

    hpoint_t curr, best;
    const char *errstr;
};

/* -------------------------------------------------------------------
 * Forward declarations for internal helper functions
 */
int ptr_bind(hdesc_t *hdesc, const char *name, hval_type type, void *ptr);
int mesg_send_and_recv(hdesc_t *hdesc);
int set_values(hdesc_t *hdesc, const hpoint_t *pt);

static int debug_mode = 0;

/* -------------------------------------------------------------------
 * Public Client API Implementations
 */
hdesc_t *harmony_init(void)
{
    hdesc_t *hdesc = (hdesc_t *) malloc(sizeof(hdesc_t));
    if (!hdesc)
        return NULL;

    if (hsession_init(&hdesc->sess) != 0) {
        free(hdesc);
        return NULL;
    }

    hdesc->mesg = HMESG_INITIALIZER;
    hdesc->state = HARMONY_STATE_INIT;

    hdesc->ptr = NULL;
    hdesc->ptr_len = hdesc->ptr_cap = 0;

    hdesc->curr = HPOINT_INITIALIZER;
    hdesc->best = HPOINT_INITIALIZER;
    hdesc->errstr = NULL;

    gethostname(hdesc->id, MAX_HOSTNAME_STRLEN);
    hdesc->id[MAX_HOSTNAME_STRLEN] = '\0';
    sprintf(hdesc->id + strlen(hdesc->id), "_%d_%d", getpid(), hdesc->socket);

    return hdesc;
}

void harmony_fini(hdesc_t *hdesc)
{
    hmesg_fini(&hdesc->mesg);
    hsession_fini(&hdesc->sess);
    hpoint_fini(&hdesc->curr);
    hpoint_fini(&hdesc->best);
    free(hdesc->ptr);
    free(hdesc);
}

/* -------------------------------------------------------------------
 * Session Establishment API Implementations
 */
int harmony_session_name(hdesc_t *hdesc, const char *name)
{
    return hsignature_name(&hdesc->sess.sig, name);
}

int harmony_int(hdesc_t *hdesc, const char *name,
                long min, long max, long step)
{
    return hsignature_int(&hdesc->sess.sig, name, min, max, step);
}

int harmony_real(hdesc_t *hdesc, const char *name,
                 double min, double max, double step)
{
    return hsignature_real(&hdesc->sess.sig, name, min, max, step);
}

int harmony_enum(hdesc_t *hdesc, const char *name, const char *value)
{
    return hsignature_enum(&hdesc->sess.sig, name, value);
}

int harmony_strategy(hdesc_t *hdesc, const char *strategy)
{
    return hcfg_set(hdesc->sess.cfg, CFGKEY_SESSION_STRATEGY, strategy);
}

int harmony_layer_list(hdesc_t *hdesc, const char *list)
{
    return hcfg_set(hdesc->sess.cfg, CFGKEY_SESSION_LAYERS, list);
}

int harmony_launch(hdesc_t *hdesc, const char *host, int port)
{
    /* Sanity check input */
    if (hdesc->sess.sig.range_len < 1) {
        hdesc->errstr = "No tuning variables defined";
        errno = EINVAL;
        return -1;
    }

    if (!host && !getenv("HARMONY_S_HOST")) {
        char *path;
        const char *home;

        /* Provide a default name, if one isn't defined. */
        if (!hdesc->sess.sig.name) {
            if (hsignature_name(&hdesc->sess.sig, "NONAME"))
                return -1;
        }

        /* Find the Active Harmony installation. */
        home = getenv("HARMONY_HOME");
        if (!home) {
            hdesc->errstr = "No host or HARMONY_HOME specified";
            return -1;
        }

        if (hcfg_set(hdesc->sess.cfg, CFGKEY_HARMONY_HOME, home) != 0) {
            hdesc->errstr = "Could not set " CFGKEY_HARMONY_HOME;
            return -1;
        }

        /* Fork a local tuning session. */
        path = sprintf_alloc("%s/libexec/session-core", home);
        hdesc->socket = socket_launch(path, NULL, NULL);
        free(path);
    }
    else {
        hdesc->socket = tcp_connect(host, port);
    }

    if (hdesc->socket < 0) {
        hdesc->errstr = strerror(errno);
        return -1;
    }
    hdesc->state = HARMONY_STATE_CONNECTED;

    /* Prepare a Harmony message. */
    hmesg_scrub(&hdesc->mesg);
    hdesc->mesg.type = HMESG_SESSION;
    hdesc->mesg.status = HMESG_STATUS_REQ;
    strncpy(hdesc->mesg.src_id, hdesc->id, MAX_ID_LEN);
    hsession_init(&hdesc->mesg.data.session);
    hsession_copy(&hdesc->mesg.data.session, &hdesc->sess);

    return mesg_send_and_recv(hdesc);
}

/* -------------------------------------------------------------------
 * Tuning Client Establishment API Implementations
 */
int harmony_bind_int(hdesc_t *hdesc, const char *name, long *ptr)
{
    return ptr_bind(hdesc, name, HVAL_INT, ptr);
}

int harmony_bind_real(hdesc_t *hdesc, const char *name, double *ptr)
{
    return ptr_bind(hdesc, name, HVAL_REAL, ptr);
}

int harmony_bind_enum(hdesc_t *hdesc, const char *name, const char **ptr)
{
    return ptr_bind(hdesc, name, HVAL_STR, (void *)ptr);
}

int ptr_bind(hdesc_t *hdesc, const char *name, hval_type type, void *ptr)
{
    int i;
    hsignature_t *sig;

    sig = &hdesc->sess.sig;
    for (i = 0; i < sig->range_len; ++i) {
        if (strcmp(sig->range[i].name, name) == 0)
            break;
    }
    if (i == sig->range_cap) {
        if (array_grow(&sig->range, &sig->range_cap, sizeof(hrange_t)) != 0)
            return -1;
    }

    if (i == sig->range_len) {
        sig->range[i].name = stralloc(name);
        if (!sig->range[i].name)
            return -1;

        sig->range[i].type = type;
        ++sig->range_len;
    }

    if (hdesc->ptr_cap < sig->range_cap) {
        void **tmpbuf = (void **) realloc(hdesc->ptr,
                                          sig->range_cap * sizeof(void *));
        if (!tmpbuf)
            return -1;

        hdesc->ptr = tmpbuf;
        hdesc->ptr_cap = sig->range_cap;
    }

    hdesc->ptr[i] = ptr;
    ++hdesc->ptr_len;

    return 0;
}

int harmony_join(hdesc_t *hdesc, const char *host, int port, const char *name)
{
    /* Verify that we have *at least* one variable bound, and that
     * this descriptor isn't already associated with a tuning
     * session.
     */
    if (hdesc->ptr_len == 0) {
        hdesc->errstr = "No variables bound to Harmony session";
        errno = EINVAL;
        return -1;
    }

    if (hdesc->state >= HARMONY_STATE_READY) {
        hdesc->errstr = "Descriptor already joined with an existing session";
        errno = EINVAL;
        return -1;
    }

    if (hdesc->state == HARMONY_STATE_INIT) {
        hdesc->socket = tcp_connect(host, port);
        if (hdesc->socket < 0) {
            hdesc->errstr = "Error establishing TCP connection with server";
            return -1;
        }
        hdesc->state = HARMONY_STATE_CONNECTED;

        hdesc->sess.sig.name = stralloc(name);
        if (!hdesc->sess.sig.name) {
            hdesc->errstr = "Error allocating memory for signature name";
            return -1;
        }
    }

    /* Prepare a Harmony message. */
    hmesg_scrub(&hdesc->mesg);
    hdesc->mesg.type = HMESG_JOIN;
    hdesc->mesg.status = HMESG_STATUS_REQ;
    strncpy(hdesc->mesg.src_id, hdesc->id, MAX_ID_LEN);

    hdesc->mesg.data.join = HSIGNATURE_INITIALIZER;
    if (hsignature_copy(&hdesc->mesg.data.join, &hdesc->sess.sig) < 0) {
        hdesc->errstr = "Internal error copying signature data";
        return -1;
    }

    /* send the client registration message */
    if (mesg_send_and_recv(hdesc) < 0)
        return -1;

    if (hdesc->mesg.status != HMESG_STATUS_OK) {
        hdesc->errstr = "Invalid message received";
        errno = EINVAL;
        return -1;
    }

    hdesc->state = HARMONY_STATE_READY;
    hsignature_fini(&hdesc->sess.sig);
    if (hsignature_copy(&hdesc->sess.sig, &hdesc->mesg.data.join) < 0) {
        hdesc->errstr = "Error copying received signature structure";
        return -1;
    }

    return 0;
}

int harmony_leave(hdesc_t *hdesc)
{
    if (hdesc->state <= HARMONY_STATE_INIT) {
        hdesc->errstr = "Descriptor not currently joined to any session.";
        errno = EINVAL;
        return -1;
    }

    hdesc->state = HARMONY_STATE_INIT;
    if (close(hdesc->socket) < 0 && debug_mode)
        perror("Error closing socket during harmony_leave()");

    /* Reset the hsession_t to prepare for hdesc reuse. */
    hsession_fini(&hdesc->sess);
    if (hsession_init(&hdesc->sess) != 0) {
        hdesc->errstr = "Internal memory allocation error.";
        return -1;
    }

    return 0;
}

char *harmony_getcfg(hdesc_t *hdesc, const char *key)
{
    if (hdesc->state < HARMONY_STATE_CONNECTED) {
        hdesc->errstr = "Descriptor not currently joined to any session.";
        errno = EINVAL;
        return NULL;
    }

    /* Prepare a Harmony message. */
    hmesg_scrub(&hdesc->mesg);
    hdesc->mesg.type = HMESG_GETCFG;
    hdesc->mesg.status = HMESG_STATUS_REQ;
    strncpy(hdesc->mesg.src_id, hdesc->id, MAX_ID_LEN);
    hdesc->mesg.data.string = key;

    if (mesg_send_and_recv(hdesc) < 0)
        return NULL;

    if (hdesc->mesg.status != HMESG_STATUS_OK) {
        hdesc->errstr = "Invalid message received from server.";
        errno = EINVAL;
        return NULL;
    }

    /* It is the user's responsibility to free this memory. */
    return stralloc(hdesc->mesg.data.string);
}

char *harmony_setcfg(hdesc_t *hdesc, const char *key, const char *val)
{
    char *buf = NULL;
    int retval;

    if (hdesc->state < HARMONY_STATE_CONNECTED) {
        /* User must be preparing for a new session since we're not
         * connected yet.  Store the key/value pair in a local cache.
         */
        buf = stralloc( hcfg_get(hdesc->sess.cfg, key) );
        hcfg_set(hdesc->sess.cfg, key, val);
        return buf;
    }

    if (!key) {
        hdesc->errstr = "Invalid key string.";
        errno = EINVAL;
        return NULL;
    }

    if (val) {
        buf = sprintf_alloc("%s=%s", key, val);
        if (!buf) {
            hdesc->errstr = "Internal memory allocation error.";
            return NULL;
        }
        hdesc->mesg.data.string = buf;
    }
    else {
        hdesc->mesg.data.string = key;
    }

    /* Prepare a Harmony message. */
    hmesg_scrub(&hdesc->mesg);
    hdesc->mesg.type = HMESG_SETCFG;
    hdesc->mesg.status = HMESG_STATUS_REQ;
    strncpy(hdesc->mesg.src_id, hdesc->id, MAX_ID_LEN);
    retval = mesg_send_and_recv(hdesc);
    free(buf);

    if (retval < 0)
        return NULL;

    if (hdesc->mesg.status != HMESG_STATUS_OK) {
        hdesc->errstr = "Invalid message received from server.";
        errno = EINVAL;
        return NULL;
    }

    /* It is the user's responsibility to free this memory. */
    return stralloc(hdesc->mesg.data.string);
}

int harmony_fetch(hdesc_t *hdesc)
{
    int retval;

    if (hdesc->state < HARMONY_STATE_CONNECTED) {
        hdesc->errstr = "Descriptor not currently joined to any session.";
        errno = EINVAL;
        return -1;
    }

    /* Prepare a Harmony message. */
    hmesg_scrub(&hdesc->mesg);
    hdesc->mesg.type = HMESG_FETCH;
    hdesc->mesg.status = HMESG_STATUS_REQ;
    strncpy(hdesc->mesg.src_id, hdesc->id, MAX_ID_LEN);
    hdesc->mesg.data.fetch.best.id = hdesc->best.id;

    if (mesg_send_and_recv(hdesc) < 0)
        return -1;

    if (hdesc->mesg.status == HMESG_STATUS_BUSY) {
        if (hdesc->best.id >= 0) {
            if (hpoint_copy(&hdesc->curr, &hdesc->best) < 0) {
                hdesc->errstr = "Internal error copying point data.";
                return -1;
            }
        }
        else {
            /* Server cannot provide a candidate point, and we have no
             * prior points to fall back upon.  Inform user by returning 0.
             */
            return 0;
        }
    }
    else if (hdesc->mesg.status == HMESG_STATUS_OK) {
        hdesc->state = HARMONY_STATE_TESTING;
        retval = hpoint_copy(&hdesc->curr, &hdesc->mesg.data.fetch.cand);
        if (retval < 0) {
            hdesc->errstr = "Internal error copying point data.";
            errno = EINVAL;
            return -1;
        }

        if (hdesc->best.id < hdesc->mesg.data.fetch.best.id) {
            retval = hpoint_copy(&hdesc->best, &hdesc->mesg.data.fetch.best);
            if (retval < 0) {
                hdesc->errstr = "Internal error copying point data.";
                errno = EINVAL;
                return -1;
            }
        }
    }
    else {
        hdesc->errstr = "Invalid message received from server.";
        errno = EINVAL;
        return -1;
    }

    /* Updating the variables from the content of the message */
    if (set_values(hdesc, &hdesc->curr) < 0) {
        hdesc->errstr = "Internal error writing values to local memory.";
        return -1;
    }
    return 1;
}

int harmony_report(hdesc_t *hdesc, double value)
{
    if (hdesc->state < HARMONY_STATE_CONNECTED) {
        hdesc->errstr = "Descriptor not currently joined to any session.";
        errno = EINVAL;
        return -1;
    }

    if (hdesc->state < HARMONY_STATE_TESTING)
        return 0;

    /* Prepare a Harmony message. */
    hmesg_scrub(&hdesc->mesg);
    hdesc->mesg.type = HMESG_REPORT;
    hdesc->mesg.status = HMESG_STATUS_REQ;
    strncpy(hdesc->mesg.src_id, hdesc->id, MAX_ID_LEN);

    hdesc->mesg.data.report.cand = HPOINT_INITIALIZER;
    hpoint_copy(&hdesc->mesg.data.report.cand, &hdesc->curr);
    hdesc->mesg.data.report.perf = value;

    hdesc->state = HARMONY_STATE_READY;
    if (mesg_send_and_recv(hdesc) < 0)
        return -1;

    if (hdesc->mesg.status != HMESG_STATUS_OK) {
        hdesc->errstr = "Invalid message received from server.";
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int harmony_best(hdesc_t *hdesc)
{
    if (hdesc->best.id < 0 || set_values(hdesc, &hdesc->best) < 0)
        return -1;

    return 0;
}

int harmony_converged(hdesc_t *hdesc)
{
    int retval;
    char *str;

    retval = 0;
    str = harmony_getcfg(hdesc, CFGKEY_STRATEGY_CONVERGED);
    if (str && str[0] == '1') {
        hdesc->state = HARMONY_STATE_CONVERGED;
        retval = 1;
    }
    free(str);
    return retval;
}

const char *harmony_error_string(hdesc_t *hdesc)
{
    return hdesc->errstr;
}

void harmony_error_clear(hdesc_t *hdesc)
{
    hdesc->errstr = NULL;
}

int mesg_send_and_recv(hdesc_t *hdesc)
{
    hmesg_type orig_type;
    orig_type = hdesc->mesg.type;

    if (mesg_send(hdesc->socket, &hdesc->mesg) < 1) {
        hdesc->errstr = "Error sending Harmony message to server.";
        errno = ECOMM;
        return -1;
    }

    if (mesg_recv(hdesc->socket, &hdesc->mesg) < 1) {
        hdesc->errstr = "Error retrieving Harmony message from server.";
        errno = ECOMM;
        return -1;
    }

    if (orig_type != hdesc->mesg.type) {
        hdesc->mesg.status = HMESG_STATUS_FAIL;
        hdesc->errstr = "Internal error: Server response message mismatch.";
        return -1;
    }

    if (hdesc->mesg.status == HMESG_STATUS_FAIL) {
        hdesc->errstr = hdesc->mesg.data.string;
        return -1;
    }
    return 0;
}

int set_values(hdesc_t *hdesc, const hpoint_t *pt)
{
    int i;

    if (hdesc->ptr_len != pt->n) {
        hdesc->errstr = "Invalid internal point structure.";
        errno = EINVAL;
        return -1;
    }

    for (i = 0; i < pt->n; ++i) {
        switch (pt->val[i].type) {
        case HVAL_INT:
            (*((       long *)hdesc->ptr[i])) = pt->val[i].value.i; break;
        case HVAL_REAL:
            (*((     double *)hdesc->ptr[i])) = pt->val[i].value.r; break;
        case HVAL_STR:
            (*((const char **)hdesc->ptr[i])) = pt->val[i].value.s; break;
        default:
            hdesc->errstr = "Invalid internal point structure.";
            errno = EINVAL;
            return -1;
        }
    }
    return 0;
}
