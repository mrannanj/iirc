#include "unix_listen.h"
#include "epoll_cont.h"
#include "event.h"
#include "common/common.h"

#include <assert.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdlib.h>
#include <string.h>

static const size_t UNIX_LISTEN_BACKLOG = 10;

int unix_listen_read(struct epoll_cont* e, uint32_t p, struct event* ev) {
  assert(e->nconn < MAX_CONN);
  struct conn* c = &e->conns[p];
  unix_conn_init(e, unix_listen_accept(c->fd));
  return 1;
}

int unix_listen_init() {
  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) die("socket");
  size_t path_len = strlen(UNIX_ADDR_PREFIX);
  struct sockaddr_un sa = { .sun_family = AF_UNIX };
  memcpy(sa.sun_path, UNIX_ADDR_PREFIX, path_len);
  sa.sun_path[0] = '\0';
  if (bind(fd, (struct sockaddr*)&sa, sizeof(sa.sun_family) + path_len) < 0)
    die("bind");
  if (listen(fd, UNIX_LISTEN_BACKLOG) < 0) die("listen");
  return fd;
}

int unix_listen_accept(int s) {
  struct sockaddr_un client;
  socklen_t addrlen = sizeof(client);
  int acc = accept(s, (struct sockaddr*)&client, &addrlen);
  if (acc < 0) die("accept");
  return acc;
}
