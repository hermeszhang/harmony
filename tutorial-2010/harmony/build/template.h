/*
 * Copyright 2003-2011 Jeffrey K. Hollingsworth
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

/* Generated by libcreater automatically */

#ifndef _$1_H
#define _$1_H

/* user defined header */
$$

#define $1_OK 1
#define $1_NO_SUCH_METHOD -100
#define $1_NO_SUCH_METRIC -101
#define $1_NO_SUCH_TRANSFORM -102

#define $1_NOMETHOD $2
#define $1_NOMETRIC $3
#define $1_NOPREDICATE $4
#define $1_NOFUNC $5


struct $1_metric {

        char *name;
        char *datatype;

} ;

struct $1_cvtfrom{

	/*int fromid;*/
	char *cvtfuncname;
	char *cvtestfuncname;
};

struct $1_mntest {

	char *mntfuncname;
	char *estfuncname;

};

struct $1_interface {

	char *funcname;
};

struct $1_method{

        /*methodname name;*/
        char *name;
        char *libfilename;
	struct $1_cvtfrom *cvtfunc;
	struct $1_mntest *mntfunc;
	struct $1_interface *interfacefunc;

} ;


/* common interface */
void $1_init();
void $1_final();
int setmethod(char *method );
int methodquery(void **table);
int metricquery(void **table);
int estimate(char *metric,char *method, void ** result);
int estimateconvert(char *target_method, void **result);
int measurement(char *metric, void **result);
char *methodconversionquery(char *method);
int updateresult();

/* UpdateAttribute and library interfaces */
$$

#endif