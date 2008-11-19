#include <string.h>
#include <time.h>
#include "scambio.h"

char const *sc_ts2gmfield(time_t ts, bool with_hour)
{
	static char field[] = "XXXX XX XX XX XX XX";
	struct tm *tm = gmtime(&ts); // now we have the GM timestruct
	if (with_hour) {
		snprintf(field, sizeof(field), "%04u %02u %02u %02u %02u %02u",
			(unsigned)tm->tm_year+1900, (unsigned)tm->tm_mon+1, (unsigned)tm->tm_mday,
			(unsigned)tm->tm_hour, (unsigned)tm->tm_min, (unsigned)tm->tm_sec);
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
