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
#include <string.h>
#include <time.h>
#include "scambio.h"

char const *sc_tm2gmfield(struct tm *tm, bool with_hour)
{
	static char field[] = "XXXX XX XX XX XX XX";
	if (with_hour) {
		// convert to UTC
		time_t ts = mktime(tm);
		struct tm *gm = gmtime(&ts); // now we have the GM timestruct
		snprintf(field, sizeof(field), "%04u %02u %02u %02u %02u %02u",
			(unsigned)gm->tm_year+1900, (unsigned)gm->tm_mon+1, (unsigned)gm->tm_mday,
			(unsigned)gm->tm_hour, (unsigned)gm->tm_min, (unsigned)gm->tm_sec);
	} else {
		snprintf(field, sizeof(field), "%04u %02u %02u",
			(unsigned)tm->tm_year+1900, (unsigned)tm->tm_mon+1, (unsigned)tm->tm_mday);
	}
	return field;
}

// Yngvar Folling's function appeared in comp.lang.c.moderated on 2000/04/16
static time_t mkgmtime(struct tm *utc_tm)
{
   time_t timeval;
   struct tm timestruct;

   timestruct = *utc_tm;
   timestruct.tm_isdst = 0;
   timeval = mktime(&timestruct);
   timestruct = *gmtime(&timeval);

   /* timestruct now contains time in the "negative" of your time zone. */

   timestruct.tm_sec   = utc_tm->tm_sec  * 2 - timestruct.tm_sec;
   timestruct.tm_min   = utc_tm->tm_min  * 2 - timestruct.tm_min;
   timestruct.tm_hour  = utc_tm->tm_hour * 2 - timestruct.tm_hour;
   timestruct.tm_mday  = utc_tm->tm_mday * 2 - timestruct.tm_mday;
   timestruct.tm_mon   = utc_tm->tm_mon  * 2 - timestruct.tm_mon;
   timestruct.tm_year  = utc_tm->tm_year * 2 - timestruct.tm_year;
   timestruct.tm_isdst = 0;

   /* timestruct now contains local time, but without normalized fields. */

   timeval = mktime(&timestruct);
   *utc_tm = *gmtime(&timeval);
   return timeval;
}

void sc_gmfield2uint(char const *str, unsigned *year, unsigned *month, unsigned *day, unsigned *hour, unsigned *min, unsigned *sec, bool *hour_set)
{
	int ret = sscanf(str, "%u %u %u %u %u %u", year, month, day, hour, min, sec);
	if (ret == 3) {	// OK, make it a date only (no timezone adjustement)
		*hour_set = false;
	} else if (ret == 5 || ret == 6) {	// Adjust timezone (we were given UTC timestamp
		*hour_set = true;
		if (ret == 5) *sec = 0;
		struct tm tm;
		memset(&tm, 0, sizeof(tm));
		tm.tm_year = *year - 1900;
		tm.tm_mon  = *month - 1;
		tm.tm_mday = *day;
		tm.tm_hour = *hour;
		tm.tm_min  = *min;
		tm.tm_sec  = *sec;
		time_t t = mkgmtime(&tm);
		struct tm *now = localtime(&t);
		*year  = now->tm_year + 1900;
		*month = now->tm_mon + 1;
		*day   = now->tm_mday;
		*hour  = now->tm_hour;
		*min   = now->tm_min;
		*sec   = now->tm_sec;
		debug("UTC '%s' converted to local '%u %u %u %u %u %u'", str, *year, *month, *day, *hour, *min, *sec);
	} else with_error(0, "Cannot convert string '%s' to date", str) return;
}

time_t sc_gmfield2ts(char const *str, bool *hour_set)
{
	unsigned year, month, day, hour, min, sec;
	if_fail (sc_gmfield2uint(str, &year, &month, &day, &hour, &min, &sec, hour_set)) return 0;
	struct tm tm;
	memset(&tm, 0, sizeof(tm));
	tm.tm_year = year - 1900;
	tm.tm_mon  = month - 1;
	tm.tm_mday = day;
	if (*hour_set) {
		tm.tm_hour = hour;
		tm.tm_min  = min;
		tm.tm_sec  = sec;
	}
	time_t ts = mktime(&tm);
	if ((time_t)-1 == ts) with_error(0, "mktime") return 0;
	return ts;
}

