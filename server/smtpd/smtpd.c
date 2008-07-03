#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <pth.h>
#include <string.h>
#include "scambio.h"
#include "daemon.h"
#include "cnx.h"
#include "varbuf.h"
#include "cmd.h"

/*
 * Data Definitions
 */

static struct cnx_server server;
static sig_atomic_t terminate = 0;


