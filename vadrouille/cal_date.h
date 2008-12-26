/* Copyright 2008 Cedric Cellier.
 *
 * This file is part of Scambio.
 *
 * Scambio is free software: you can redistribute it and/or modify it under the
 * terms of the GNU Affero General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * Scambio is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Affero General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with Scambio.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef CAL_DATE_H_081226
#define CAL_DATE_H_081226
#include "merelib.h"

struct cal_date {
	guint year;	// 0 means the whole structure is undefined (ending dates are optional)
	guint month;	// from 0 to 11
	guint day;	// from 1 to 31
	guint hour;	// from 0 to 23, >23 if hour and min are not defined (hour+min are optional)
	guint min;	// from 0 to 59
	char str[4+1+2+1+2+1+5+1];
};

static inline bool cal_date_is_set(struct cal_date const *cd)
{
	return cd->year != 0;
}

static inline bool cal_date_has_time(struct cal_date const *cd)
{
	return cal_date_is_set(cd) && cd->hour <= 23;
}

static inline void cal_date_dtor(struct cal_date *cd)
{
	(void)cd;
}

void cal_date_ctor(struct cal_date *, guint y, guint M, guint d, guint h, guint m);
void cal_date_ctor_from_input(struct cal_date *, char const *input, struct cal_date *);
void cal_date_ctor_from_tm(struct cal_date *, struct tm const *);
void cal_date_ctor_from_str(struct cal_date *, char const *);
int cal_date_compare(struct cal_date const *, struct cal_date const *);
void cal_date_to_str(struct cal_date *, char *, size_t);

#endif
