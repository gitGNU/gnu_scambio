#ifndef TIMETOOLS_H_081119
#define TIMETOOLS_H_081119

char const *sc_ts2gmfield(time_t ts, bool with_hour);
void sc_gmfield2uint(char const *str, unsigned *year, unsigned *month, unsigned *day, unsigned *hour, unsigned *min, unsigned *sec, bool *hour_set);

#endif
