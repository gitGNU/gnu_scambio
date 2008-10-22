#ifndef FILED_H_081022
#define FILED_H_081022

#include <stdbool.h>
#include "scambio/channel.h"

struct cnx_env {
	struct chn_cnx cnx;
	bool filed;
};

#endif
