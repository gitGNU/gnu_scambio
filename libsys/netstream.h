#ifndef SOCKSTREAM_H_080616
#define SOCKSTREAM_H_080616

#include "stream.h"

struct stream *netstream_client_new(char const *host, unsigned short port);
void netstream_del(struct stream *);

// This obj listen to a port and creates netstream from incoming connections.
// Each created stream is passed to a callback that can be used to validate the
// client and to store the stream somewhere. The server keeps no refs of these streams.
struct netstream_server;
typedef netstram_connect(struct netstream_server *server, struct stream *stream, void *data);
struct netstream_server *netstream_server_new(unsigned short port, netstram_connect *cb, void *data);
// Notice that this wont delete any opened streams
void netstream_server_del(struct netstream_server *);

#endif
