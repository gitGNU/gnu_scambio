#ifndef MAIN_H_080731
#define MAIN_H_080731

#include "cnx.h"

extern struct cnx_client cnx;
typedef void *thread_entry(void *);
thread_entry connecter_thread, reader_thread, writer_thread;

int reader_begin(void);
void reader_end(void);
int writer_begin(void);
void writer_end(void);

#endif
