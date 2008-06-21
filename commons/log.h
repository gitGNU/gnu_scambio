#ifndef LOG_H_080527
#define LOG_H_080527

#include <stdio.h>

extern FILE *log_file;
extern int log_level;
int log_begin(char const *dirname, char const *filename);
void log_end(void);

#define log_print(fmt, ...) do if (log_file) fprintf(log_file, fmt "\n", ##__VA_ARGS__); while(0)

#define error(...)     do if (log_level > 0) log_print("ERR: " __VA_ARGS__); while(0)
#define warning(...)   do if (log_level > 1) log_print("WRN: " __VA_ARGS__); while(0)
#define info(...)      do if (log_level > 2) log_print("NFO: " __VA_ARGS__); while(0)
#define debug(...)     do if (log_level > 3) log_print("DBG: " __FUNCTION__ ": " __VA_ARGS__); while(0)
#define fatal(...)     do { log_print("FATAL: " __VA_ARGS__); abort(); } while(0)

#endif
