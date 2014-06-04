#include "common/epoll_cont.h"
#include "common/irc_conn.h"
#include "common/common.h"
#include "common/conn.h"

#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/un.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <stdio.h>
#include <signal.h>
#include <time.h>
#include <assert.h>

static void irc_send(struct epoll_cont*, int, const char *, ...);
static void irc_send_handshake(struct epoll_cont*, int);
static void irc_generate_nickname(size_t, char*);

#define ST_INIT 0
#define ST_AFTER_FIRST 1

int irc_conn_irc_msg(struct epoll_cont* e, uint32_t p, struct event* ev) {
  if (p != ev->source) return 1;
  struct conn* c = &e->conns[p];
  struct irc_data* irc = c->data.ptr;
  char* msg = ev->p;
  int n = 0;
  char server_addr[513];

  switch (irc->st) {
    case ST_INIT:
      irc_send_handshake(e, p);
      irc->st = ST_AFTER_FIRST;
      break;
    case ST_AFTER_FIRST:
      n = sscanf(msg, "PING :%s", server_addr);
      if (n != 1) break;
      irc_send(e, p, "PONG :%s\r\n", server_addr);
      goto out;
    default:
      log("unkown state in irc_conn");
  }
  printf("%s", msg);
  memcpy(irc->cbuf[irc->cpos], msg, strlen(msg)+1);
  irc->cpos = (irc->cpos + 1) % IRC_NLINES;
  irc->cn = (irc->cn < IRC_NLINES ? irc->cn + 1 : irc->cn);
out:
  return 1;
}

int irc_conn_unix_acc(struct epoll_cont* e, uint32_t p, struct event* ev) {
  struct irc_data* irc = e->conns[p].data.ptr;
  int k = IRC_NLINES + irc->cpos - irc->cn;
  for (int j = 0; j < irc->cn; ++j, k = (k+1) % IRC_NLINES) {
    ssize_t len = strlen(irc->cbuf[k]);
    conn_write_buf2(e, ev->source, irc->cbuf[k], len);
  }
  return 1;
}

int irc_conn_unix_msg(struct epoll_cont* e, uint32_t p, struct event* ev) {
  char buf[IRC_MAXLEN];
  ssize_t len = strlen(ev->p);
  memcpy(buf, ev->p, len);
  buf[len-1] = '\r';
  buf[len] = '\n';
  buf[len+1] = '\0';
  len += 1;
  conn_write_buf2(e, p, buf, len);
  return 1;
}

int irc_conn_close(struct epoll_cont* e, uint32_t p, struct event* ev) {
  struct conn* c = &e->conns[p];
  if (c->data.ptr) free(c->data.ptr);
  conn_close(e,p,ev);
  return 1;
}

void irc_conn_init(struct epoll_cont* e, const char* host, uint16_t port) {
  int rfd = tcp_conn_init(host, port);
  int wfd = dup(rfd);
  int slot = conn_init(e, rfd, wfd);
  if (slot < 0) die2("no slot for irc connection");
  struct conn* c = &e->conns[slot];
  c->data.ptr = malloc(sizeof(struct irc_data));
  assert(c->data.ptr);
  struct irc_data* irc = c->data.ptr;
  irc->st = ST_INIT;
  c->cbs[EV_CLOSE] = irc_conn_close;
  c->cbs[EV_READ] = conn_read;
  c->cbs[EV_AFTER_READ] = irc_conn_after_read;
  c->cbs[EV_WRITE] = conn_write;
  c->cbs[EV1_IRC_MESSAGE] = irc_conn_irc_msg;
  c->cbs[EV1_UNIX_MESSAGE] = irc_conn_unix_msg;
  c->cbs[EV1_UNIX_ACCEPTED] = irc_conn_unix_acc;
  irc->cpos = 0;
  irc->cn = 0;
}

int irc_conn_after_read(struct epoll_cont* e, uint32_t p, struct event* ev) {
  struct conn* c = &e->conns[p];
  char prev_ch = '\0';
  char msg[IRC_MAXLEN];

  for (ssize_t i = 0; i < c->in_pos;) {
    const char x = c->in_buf[i];
    switch (prev_ch) {
      case '\r':
        if (x != '\n') break;
        memcpy(msg, c->in_buf, i+1);
        msg[i+1] = '\0';
        struct event evt = { .type = EV1_IRC_MESSAGE, .source = p, .p = msg };
        epoll_cont_walk(e, &evt);
        c->in_pos -= i+1;
        memmove(c->in_buf, &c->in_buf[i+1], c->in_pos);
        i = 0;
        break;
      default:
        ++i;
        break;
    }
    prev_ch = x;
  }
  return 1;
}

void irc_send(struct epoll_cont* e, int slot, const char *message, ...) {
  va_list vl;
  va_start(vl, message);

  char buf[IRC_MAXLEN];
  int n = vsnprintf(buf, IRC_MAXLEN-3, message, vl);
  if (n < 0) {
    log("vsnprintf error");
    return;
  }
  va_end(vl);

  buf[n] = '\r';
  buf[n+1] = '\n';
  buf[n+2] = '\0';
  conn_write_buf2(e, slot, buf, n+1);
}

void irc_generate_nickname(size_t len, char* nick) {
  const char pot[] = "abcdefghijklmnopqrstuvwxyz";
  size_t pot_len = sizeof(pot);
  nick[0] = 'a';
  for (size_t i = 1; i < len; ++i)
    nick[i] = pot[rand()%pot_len];
  nick[len] = '\0';
}

void irc_send_handshake(struct epoll_cont* e, int slot) {
  char nick[7];
  irc_generate_nickname(sizeof(nick)-1, nick);
  irc_send(e, slot, "NICK %s", nick);
  irc_send(e, slot, "USER %s 8 * :%s", nick, nick);
  irc_send(e, slot, "MODE %s +i", nick);
}
