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
#ifndef TIMETOOLS_H_081119
#define TIMETOOLS_H_081119

char const *sc_tm2gmfield(struct tm *tm, bool with_hour);
char const *sc_ts2gmfield(time_t ts, bool with_hour);
void sc_gmfield2uint(char const *str, unsigned *year, unsigned *month, unsigned *day, unsigned *hour, unsigned *min, unsigned *sec, bool *hour_set);
time_t sc_gmfield2ts(char const *str, bool *hour_set);
int month_days(unsigned year, unsigned month);	// month is from 0 to 11

#endif
