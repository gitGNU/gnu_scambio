/* Use chn API to allow for a persistent connection to a file server,
 * able to fetch/upload several files in parallel, and offering the possibility
 * for the user to register an incomming callback for some realtime stream.
 */

/*
 * Data Definitions
 */

static struct chn_cnx ccnx;
static pth_t reader_thread;

/*
 * Init
 */

void filec_begin(void)
{
}

void filec_end(void)
{
}

/*
 * Create
 */

void chn_create(char *name, size_t len, bool rt)
{
}

/*
 * Read
 */

int chn_get_file(char *localfile, size_t len, char const *name)
{
}

/*
 * Write
 */

void chn_send_file(char const *name, int fd)
{
}

