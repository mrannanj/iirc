#pragma once

#define MAX_EVENTS 10
#define MAX_CONN 20
#define MAX_TYPE 5

#include "conn.h"

struct epoll_cont {
  int epfd;
  int nconn;
  struct conn conns[MAX_CONN];
};

void epoll_cont_init(struct epoll_cont*);
void epoll_cont_serve(struct epoll_cont*);
void epoll_cont_walk(struct epoll_cont*, struct event*);
void epoll_cont_destroy(struct epoll_cont*);
