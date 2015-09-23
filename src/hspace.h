/*
 * Copyright 2003-2015 Jeffrey K. Hollingsworth
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

#ifndef __HSPACE_H__
#define __HSPACE_H__

#include "hrange.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Harmony search space: Multi-dimensional input space to be searched
 * for optimal performance values.  Each dimension is defined by a
 * range of possible values.
 */
typedef struct hspace {
    unsigned  id;
    char*     name;
    hrange_t* dim;
    int       len;
    int       cap;

    void*     owner;
} hspace_t;
#define HSPACE_INITIALIZER {0}
extern const hspace_t hspace_zero;

/* Harmony search space functions */
int  hspace_copy(hspace_t* dst, const hspace_t* src);
void hspace_scrub(hspace_t* space);
void hspace_fini(hspace_t* space);

int  hspace_equal(const hspace_t* space_a, const hspace_t* space_b);
int  hspace_name(hspace_t* space, const char* name);
int  hspace_int(hspace_t* space, const char* name,
                long min, long max, long step);
int  hspace_real(hspace_t* space, const char* name,
                 double min, double max, double step);
int  hspace_enum(hspace_t* space, const char* name, const char* value);

int  hspace_pack(char** buf, int* buflen, const hspace_t* space);
int  hspace_unpack(hspace_t* space, char* buf);
int  hspace_parse(hspace_t* space, const char* buf, const char** errptr);

#ifdef __cplusplus
}
#endif

#endif /* __HSPACE_H__ */
