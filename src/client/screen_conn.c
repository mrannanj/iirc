#include <ncurses.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

#include "client/screen_conn.h"
#include "common/common.h"
#include "common/epoll_cont.h"

void screen_init(struct screen* s) {
  memset(s, 0, sizeof(*s));

  initscr();
  curs_set(1);
  raw();
  noecho();

  int max_row, max_col;
  getmaxyx(stdscr, max_row, max_col);

  s->text = newwin(max_row-2, max_col, 0, 0);
  scrollok(s->text, TRUE);
  idlok(s->text, TRUE);
  s->input = newwin(2, max_col, max_row-2, 0);

  s->cn = 0;
  s->cpos = 0;

  s->cursor_pos = 0;
  s->line_len = 0;
  s->mode = MODE_NORMAL;
  s->attach_conn = NULL;
}

void screen_draw(struct screen* s) {
  erase();
  werase(s->text);
  werase(s->input);
  setsyx(0, 0);

  int max_row, max_col;
  getmaxyx(stdscr, max_row, max_col);

  wresize(s->text, max_row-2, max_col);
  mvwin(s->text, 0, 0);

  wresize(s->input, 2, max_col);
  mvwin(s->text, max_row-2, 0);

  screen_draw_buf(s);
  wrefresh(s->text);

  setsyx(max_row-2, 0);
  screen_status_bar(s, max_col);

  for (int c = 0; c < max_col && s->line[c]; ++c)
    waddch(s->input, s->line[c]);
  wmove(s->input, 1, s->cursor_pos);
  wrefresh(s->input);
}

void screen_status_bar(struct screen* s, int max_col) {
  char status[SCR_NCOL];
  time_t t = time(NULL);
  struct tm local;
  localtime_r(&t, &local);
  ssize_t n = strftime(status, sizeof(status), "%H:%M:%S", &local);
  memmove(&status[max_col-n], status, n);
  memset(status, ' ', max_col-n);
  if (s->mode == MODE_INSERT) {
    n = snprintf(status, sizeof(status), "INSERT");
  } else if (s->mode == MODE_NORMAL) {
    n = snprintf(status, sizeof(status), "NORMAL");
  } else if (s->mode == MODE_REPLACE) {
    n = snprintf(status, sizeof(status), "REPLACE");
  } else {
    die2("invalid mode in screen");
  }
  status[n] = ' ';
  for (int i = 0; i < max_col; ++i) {
    waddch(s->input, status[i]);
  }
}

void screen_add_line(struct screen* s, char* row) {
  size_t len = strlen(row);
  memcpy(s->cbuf[s->cpos], row, len);
  s->cbuf[s->cpos][len] = '\0';
  s->cn = min(s->cn + 1, IRC_NLINES);
  s->cpos = (IRC_NLINES + s->cpos + 1) % IRC_NLINES;
  screen_draw(s);
}

void screen_draw_buf(struct screen* s) {
  for (ssize_t i = 0, p = (IRC_NLINES + s->cpos - s->cn) % IRC_NLINES;
      i < s->cn;
      ++i, p = (IRC_NLINES + p + 1) % IRC_NLINES)
  {
    waddch(s->text, '\n');
    for (ssize_t j = 0; isprint(s->cbuf[p][j]); ++j) {
      waddch(s->text, s->cbuf[p][j]);
    }
  }
}

void screen_destroy(struct screen* s) {
  erase();
  endwin();
}

struct conn* screen_conn_add(struct epoll_cont* e) {
  struct conn* c = epoll_cont_find_free(e);
  if (!c) die2("no connection slot for stdin");
  memset(c, 0, sizeof(*c));
  c->rfd = STDIN_FILENO;
  c->wfd = -1;
  c->cbs[EV_READ] = screen_conn_read;
  c->cbs[EV_CLOSE] = conn_close_fatal;
  struct epoll_event ee = { .events = EPOLLIN, .data.ptr = c };
  if (epoll_ctl(e->epfd, EPOLL_CTL_ADD, c->rfd, &ee) < 0)
    die("epoll_ctl");
  return c;
}

void screen_conn_push_line(struct epoll_cont* e, struct conn* c) {
  struct screen* s = e->ptr;
  char* line = s->line;
  if (s->attach_conn != NULL) {
    line[s->line_len] = '\n';
    line[s->line_len+1] = '\0';
    conn_write_buf2(e, s->attach_conn, line, s->line_len+1);
  }
  s->cursor_pos = s->line_len = 0;
  memset(s->line, 0, SCR_NCOL);
}

int screen_conn_read(struct epoll_cont* e, struct conn* c, struct event* ev) {
  char ch;
  ssize_t nread = read(c->rfd, &ch, 1);
  if (nread < 0) {
    log_errno("read");
    return 0;
  } else if (nread == 0) {
    log("EOF from stdin");
    return 0;
  }
  struct screen* s = e->ptr;
  if (!s) die2("screen is NULL");

  int cont = 1;
  if (s->mode == MODE_REPLACE) {
    if (isprint(ch)) {
      s->line[s->cursor_pos] = ch;
      s->cursor_pos = min(s->cursor_pos + 1, SCR_NCOL - 1);
      s->line_len = max(s->cursor_pos, s->line_len);
    } else if (ch == ASCII_ESC) {
      s->mode = MODE_NORMAL;
    }
  } else if (s->mode == MODE_INSERT) {
    if (isprint(ch)) {
      s->line_len = min(s->line_len + 1, SCR_NCOL - 1);
      if (s->cursor_pos < s->line_len) {
        memmove(&s->line[s->cursor_pos+1], &s->line[s->cursor_pos],
            s->line_len-(s->cursor_pos+1));
      }
      s->line[s->cursor_pos] = ch;
      s->cursor_pos = min(s->cursor_pos + 1, s->line_len);
    } else if (ch == ASCII_ESC) {
      s->cursor_pos = max(0, s->cursor_pos - 1);
      s->mode = MODE_NORMAL;
    } else if (ch == '\r') {
      screen_conn_push_line(e, c);
    }
  } else if (s->mode == MODE_NORMAL) {
    switch (ch) {
      case '\r':
        screen_conn_push_line(e, c);
        break;
      case 'x':
        if (s->cursor_pos < s->line_len) {
          memmove(&s->line[s->cursor_pos], &s->line[s->cursor_pos+1],
              s->line_len - s->cursor_pos);
          s->line_len = max(0, s->line_len-1);
          s->cursor_pos = min(max(0, s->line_len-1), s->cursor_pos);
        }
        break;
      case 'I':
        s->cursor_pos = 0;
      case 'i':
        s->mode = MODE_INSERT;
        break;
      case 'R':
        s->mode = MODE_REPLACE;
        break;
      case 'l':
        s->cursor_pos = min(max(s->line_len-1, 0), s->cursor_pos + 1);
        break;
      case 'h':
        s->cursor_pos = max(0, s->cursor_pos - 1);
        break;
      case 'A':
        s->cursor_pos = s->line_len;
        s->mode = MODE_INSERT;
      case 'a':
        s->cursor_pos = min(s->line_len, s->cursor_pos + 1);
        s->mode = MODE_INSERT;
        break;
      case 'q':
        cont = 0;
        break;
    }
  }
  screen_draw(s);
  return cont;
}
