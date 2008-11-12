#ifndef MEREFS_H_081112
#define MEREFS_H_081112

#include <stdbool.h>
#include <time.h>
#include "scambio.h"
#include "scambio/mdir.h"
#include "scambio/channel.h"
#include "file.h"

extern char *mdir_name;
extern char *local_path;
extern struct mdir *mdir;
extern bool quit;
extern struct chn_cnx ccnx;

time_t last_run_start(void);
void start_read_mdir(void);
void reread_mdir(void);
void traverse_local_path(void);
void create_unmatched_files(void);
void create_local_file(struct file *file);

#endif
