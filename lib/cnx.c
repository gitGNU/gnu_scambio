#include <netdb.h>
#include "scambio/cnx.h"

/*
 * Data Definitions
 */

struct mdir_cnx {
	int fd;
	struct user *user;
	bool outbound;	// ie server, ie seqnums are <0
	// TODO : a list of sent queries, and a list of recently sent data boxes
};

/*
 * Constructors
 */

static struct mdir_cnx *cnx_alloc(void)
{
	struct mdir_cnx *cnx = malloc(sizeof(*cnx));
	if (! cnx) with_error(ENOMEM, "malloc(mdir_cnx)") return NULL;
	cnx->fd = -1;
	cnx->user = NULL;
	return cnx;
}

void mdir_cnx_dtor(struct mdir_cnx *cnx)
{
	if (cnx->fd != -1) {
		(void)close(cnx->fd);
		cnx->fd = -1;
	}
}

void mdir_cnx_del(struct mdir_cnx *cnx)
{
	mdir_cnx_dtor(cnx);
	(void)free(cnx);
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

struct mdir_cnx *mdir_cnx_new_outbound(char const *host, char const *service, char const *username)
{
	struct mdir_cnx *cnx = cnx_alloc();
	on_error return NULL;
	if_fail (mdir_cnx_ctor_outbound(cnx, host, service, username)) {
		free(cnx);
		cnx = NULL;
	}
	return cnx;
}

void mdir_cnx_ctor_inbound(struct mdir_cnx *cnx, int fd)
{
	cnx->outbound = false;
	cnx->user = NULL;
	cnx->fd = fd;
}

struct mdir_cnx *mdir_cnx_new_inbound(int fd)
{
	struct mdir_cnx *cnx = cnx_alloc();
	if_fail (cnx_ctor_inbound(cnx, fd)) {
		free(cnx);
		cnx = NULL;
	}
	return cnx;
}

void mdir_cnx_set_user(struct mdir_cnx *cnx, char const *username)
{
	cnx->user = user_load(username);
}

/*
 * Read
 */

static bool is_my_seqnum(struct mdir_cnx *cnx, long long seqnum)
{
	// TODO
}

void mdir_cnx_read(struct mdir_cnx *cnx, struct mdir_cmd *cmd, struct mdir_parser *parser)
{
	// TODO: add to the parser the parser for sent queries (so that we got the response)
	// so we have a parser in any case.
	// Then use cmd.seqnum sign to find out if its a command or an answer
	// oubien réécrire cmd_read pour qu'il renvoit les commandes inconnues (sans check de syntax
	// dans ce cas). Et aussi, il faut que parser soit un paramètre optionnel. On peut alors
	// faire ceci :
	assert(cmd);
	if_fail (mdir_cmd_read(parser, cmd, cnx->fd)) return;
	if (is_my_seqnum(cnx, cmd.seqnum)) {	// handle as a query
		// TODO : find the query with this seqnum and keyword, and call the CB
	} else {	// handle as a response
		if (! cmd.checked) with_error(0, "Unknown command '%s' or invalid syntax", cmd.keyword) return;
	}
}
