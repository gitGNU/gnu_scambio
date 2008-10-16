#include <netdb.h>
#include "scambio/cnx.h"

/*
 * Data Definitions
 */

struct sent_query {
	LIST_ENTRY(sent_query) def_entry;	// list head is in the definition of the query
	LIST_ENTRY(sent_query) cnx_entry;	// list head is in the cnx
	long long seqnum;
	void *user_data;
};

// When we register a query, we add a definition for the answer,
// with a special callback that looks for the very query that was sent, and call its callback.
struct query_def {
	struct mdir_cmd_def def;
	LIST_HEAD(sent_queries, sent_query) sent_queries;
	mdir_cnx_answ_cb *cb;
};

/*
 * Constructors for sent_query
 */

static void sent_query_ctor(struct sent_query *sq, struct mdir_cnx *cnx, struct query_def *def, long long seq, void *user_data)
{
	sq->seqnum = seq;
	sq->user_data = user_data;
	LIST_INSERT_HEAD(&def->sent_queries, sq, def_entry);
	LIST_INSERT_HEAD(&cnx->sent_queries, sq, cnx_entry);
}

static void sent_query_dtor(struct sent_query *sq)
{
	LIST_REMOVE(sq, def_entry);
	LIST_REMOVE(sq, cnx_entry);
}

static struct sent_query *sent_query_new(struct mdir_cnx *cnx, struct query_def *def, long long seq, void *user_data)
{
	struct sent_query *sq = malloc(sizeof(*sq));
	if (! sq) with_error(ENOMEM, "malloc(sent_query)") return NULL;
	if_fail (sent_query_ctor(sq, cnx, def, seq, user_data)) {
		free(sq);
		sq = NULL;
	}
	return sq;
}

static void sent_query_del(struct sent_query *sq)
{
	sent_query_dtor(sq);
	free(sq);
}

/*
 * Constructors for mdir_cnx
 */

static struct mdir_cnx *cnx_alloc(void)
{
	struct mdir_cnx *cnx = malloc(sizeof(*cnx));
	if (! cnx) with_error(ENOMEM, "malloc(mdir_cnx)") return NULL;
	cnx->fd = -1;
	cnx->user = NULL;
	mdir_syntax_ctor(&cnx->syntax);
	LIST_INIT(&cnx->sent_queries);
	return cnx;
}

static int gaierr2errno(int err)
{
	switch (err) {
		case EAI_SYSTEM: return errno;
		case EAI_MEMORY: return ENOMEM;
	}
	return -1;	// FIXME
}

static void cnx_connect(struct mdir_cnx *cnx, char const host, char const *service)
{
	// Resolve hostname into sockaddr
	struct addrinfo *info_head, *ainfo;
	int err;
	if (0 != (err = getaddrinfo(host, service, NULL, &info_head))) {
		// TODO: check that freeaddrinfo is not required in this case
		with_error(gaierr2errno(err), "Cannot getaddrinfo") return;
	}
	err = ENOENT;
	for (ainfo = info_head; ainfo; ainfo = ainfo->ai_next) {
		cnx->fd = socket(ainfo->ai_family, ainfo->ai_socktype, ainfo->ai_protocol);
		if (cnx->fd == -1) continue;
		if (0 == connect(cnx->fd, ainfo->ai_addr, ainfo->ai_addrlen)) {
			info("Connected to %s:%s", host, service);
			break;
		}
		err = errno;
		(void)close(cnx->fd);
		cnx->fd = -1;
	}
	if (! ainfo) error_push(err, "No suitable address found for host %s:%s", host, service);
	freeaddrinfo(info_head);
}

static void auth_answ(char const *kw, int status, char const *compl, void *data)
{
	bool *done = (bool *)data;
	assert(kw == kw_auth);
	(void)compl;
	if (status == 200) *done = true;
}

void mdir_cnx_ctor_outbound(struct mdir_cnx *cnx, char const host, char const *service, char const *username)
{
	cnx->outbound = true;
	if_fail (cnx_connect(cnx, host, service)) return;
	do {
		bool done = false;
		if_fail (mdir_cnx_query(cnx, auth_answ, username, kw_auth, &done)) break;
		if_fail (mdir_cnx_read(cnx, NULL, NULL)) break;
		if (! done) with_error (0, "no answer to auth") break;
		cnx->user = user_load(username);
	} while (0);
	on_error {
		(void)close(cnx->fd);
		cnx->fd = -1;
	}
}

void mdir_cnx_ctor_inbound(struct mdir_cnx *cnx, int fd)
{
	cnx->outbound = false;
	cnx->user = NULL;
	cnx->fd = fd;
	// TODO: wait for user auth and set user or return error
}

void mdir_cnx_dtor(struct mdir_cnx *cnx)
{
	if (cnx->fd != -1) {
		(void)close(cnx->fd);
		cnx->fd = -1;
	}
	mdir_syntax_dtor(&cnx->syntax);
	struct sent_query *sq;
	while (NULL != (sq = LIST_FIRT(&cnx->sent_queries, cnx_entry))) {
		sent_query_del(sq);
	}
}

/*
 * Read
 */

void mdir_cnx_read(struct mdir_cnx *cnx)
{
	// pour les query, le callback query sera appelé, donc au saura qu'on n'a pas a faire
	// a une vulgaire mdir_cmd_def mais a une query_def, et de la on pourra lire la liste des
	// query envoyées, etc...
}
