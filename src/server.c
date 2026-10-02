// Local web server for the Knightshift browser UI.
//
// Serves web/index.html and a small JSON API backed by the C engine and bots.
// It is single-threaded and listens on 127.0.0.1 only.
//
// usage: knightshift-web [--port N]
//
// API (all responses are the full game state as JSON):
//   GET  /api/state
//   POST /api/new
//   POST /api/move?from=B1&to=C3
//   POST /api/fortify?at=A8
//   POST /api/bot?algo=minimax|positional|tree|random&depth=5
//   POST /api/undo?plies=2

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include "knightshift.h"
#include "bot.h"

#define MAX_PLIES 1024
#define REQUEST_MAX 8192

// ======== growable string buffer

typedef struct {
  char *data;
  size_t len, cap;
} strbuf;

static void sb_printf(strbuf *sb, const char *fmt, ...)
{
  va_list ap;
  for (;;) {
    size_t avail = sb->cap - sb->len;
    va_start(ap, fmt);
    int n = vsnprintf(sb->data ? sb->data + sb->len : NULL, avail, fmt, ap);
    va_end(ap);
    if (n < 0)
      return;
    if ((size_t)n < avail) {
      sb->len += n;
      return;
    }
    sb->cap = (sb->cap + n + 1) * 2;
    sb->data = realloc(sb->data, sb->cap);
  }
}

// append a JSON string literal, escaping quotes and backslashes
static void sb_json_str(strbuf *sb, const char *s)
{
  sb_printf(sb, "\"");
  for (; *s; ++s) {
    if (*s == '"' || *s == '\\')
      sb_printf(sb, "\\%c", *s);
    else if ((unsigned char)*s >= 0x20)
      sb_printf(sb, "%c", *s);
  }
  sb_printf(sb, "\"");
}

// ======== game state

typedef struct {
  action a;
  color mover;
  int captured;
} ply_record;

static board *positions[MAX_PLIES + 1];   // positions[0] is the start
static ply_record plies[MAX_PLIES];
static int ply_count = 0;
static strbuf bot_info;                     // JSON for the last bot decision

static board *current() { return positions[ply_count]; }
static color side_to_move() { return ply_count % 2 == 0 ? WHITE : BLACK; }

static void game_reset()
{
  for (int i = 0; i <= ply_count; ++i)
    board_free(positions[i]);
  ply_count = 0;
  positions[0] = board_setup();
  bot_info.len = 0;
}

// play an action for the side to move; returns 0 or an ERR_ outcome
static int game_play(action a)
{
  if (ply_count >= MAX_PLIES)
    return ERR_BAD_MOVE;
  if (game_over(current()))
    return ERR_BAD_MOVE;
  board *next = board_dup(current());
  int captured = 0;
  int o = action_apply(next, a, side_to_move(), &captured);
  if (o) {
    board_free(next);
    return o;
  }
  plies[ply_count] = (ply_record){a, side_to_move(), captured};
  positions[++ply_count] = next;
  return 0;
}

static void game_undo(int n)
{
  while (n-- > 0 && ply_count > 0)
    board_free(positions[ply_count--]);
  bot_info.len = 0;
}

// ======== JSON

static const char *color_name(color c) { return c == WHITE ? "white" : "black"; }

static char piece_char(piece *p)
{
  if (!p)
    return '.';
  char c = p->type == KNIGHT ? 'k' : p->type == ROYAL_GUARD ? 'g' : 'f';
  return p->color == WHITE ? c : c - 'a' + 'A';
}

static const char *error_name(int o)
{
  switch (o) {
  case ERR_NO_PIECE:     return "There is no piece on that square.";
  case ERR_PIECE_TYPE:   return "Only knights can be fortified.";
  case ERR_FRIENDLY:     return "That square holds one of your own pieces.";
  case ERR_BAD_LOCATION: return "That square isn't on the enemy back rank.";
  default:               return "That move isn't legal.";
  }
}

static void state_json(strbuf *out, const char *error)
{
  board *b = current();
  color turn = side_to_move();
  int outcome = game_over(b);

  sb_printf(out, "{\"board\":\"");
  for (int i = 0; i < BOARD_SQUARES; ++i)
    sb_printf(out, "%c", piece_char(b->squares[i]));
  sb_printf(out, "\",\"turn\":\"%s\",\"ply\":%d", color_name(turn), ply_count);

  if (outcome) {
    int forts = outcome == VICTORY_WHITE_TWO_FORTRESSES ||
                outcome == VICTORY_BLACK_TWO_FORTRESSES;
    sb_printf(out, ",\"winner\":\"%s\",\"reason\":\"%s\"",
              color_name(victory_winner(outcome)),
              forts ? "two fortresses" : "three captures");
  } else {
    sb_printf(out, ",\"winner\":null,\"reason\":null");
  }

  sb_printf(out, ",\"moves\":[");
  if (!outcome) {
    move_list *ms = available_moves(b, turn);
    for (move_list *it = ms; it; it = it->next)
      sb_printf(out, "%s[\"%c%d\",\"%c%d\"]", it == ms ? "" : ",",
                it->m.src.file, it->m.src.rank, it->m.dst.file, it->m.dst.rank);
    mfree(ms);
  }
  sb_printf(out, "],\"forts\":[");
  if (!outcome) {
    loc_list *fs = available_fortifications(b, turn);
    for (loc_list *it = fs; it; it = it->next)
      sb_printf(out, "%s\"%c%d\"", it == fs ? "" : ",", it->k.file, it->k.rank);
    lfree(fs);
  }

  sb_printf(out, "],\"history\":[");
  for (int i = 0; i < ply_count; ++i) {
    char text[16];
    action a = plies[i].a;
    action_str(a, text, sizeof(text));
    sb_printf(out, "%s{\"color\":\"%s\",\"text\":\"%s\",\"capture\":%s,",
              i ? "," : "", color_name(plies[i].mover), text,
              plies[i].captured ? "true" : "false");
    if (a.kind == ACTION_MOVE)
      sb_printf(out, "\"kind\":\"move\",\"from\":\"%c%d\",\"to\":\"%c%d\"}",
                a.m.src.file, a.m.src.rank, a.m.dst.file, a.m.dst.rank);
    else
      sb_printf(out, "\"kind\":\"fortify\",\"from\":\"%c%d\",\"to\":\"%c%d\"}",
                a.fort.file, a.fort.rank, a.fort.file, a.fort.rank);
  }

  sb_printf(out, "],\"counts\":{");
  for (int c = 0; c < 2; ++c) {
    color col = c == 0 ? WHITE : BLACK;
    sb_printf(out, "%s\"%s\":{\"knights\":%d,\"forts\":%d}", c ? "," : "",
              color_name(col), count_pieces(b, col, KNIGHT),
              count_pieces(b, col, FORTRESS));
  }

  sb_printf(out, "},\"bot\":");
  if (bot_info.len)
    sb_printf(out, "%.*s", (int)bot_info.len, bot_info.data);
  else
    sb_printf(out, "null");

  sb_printf(out, ",\"error\":");
  if (error)
    sb_json_str(out, error);
  else
    sb_printf(out, "null");
  sb_printf(out, "}");
}

// ======== bots

static double now_ms()
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static const char *run_bot(const char *algo, int depth)
{
  board *b = current();
  color turn = side_to_move();
  action a;
  double start = now_ms();

  bot_info.len = 0;
  if (game_over(b))
    return "The game is over.";

  if (!strcmp(algo, "tree")) {
    tree_trace t;
    decision_tree(b, turn, &a, &t);
    sb_printf(&bot_info, "{\"algo\":\"tree\",\"rule\":%d,\"rules\":[", t.rule);
    for (int i = 0; i < RULE_COUNT; ++i) {
      if (i)
        sb_printf(&bot_info, ",");
      sb_json_str(&bot_info, tree_rule_names[i]);
    }
    sb_printf(&bot_info, "],\"reason\":");
    sb_json_str(&bot_info, t.reason);
  } else if (!strcmp(algo, "random")) {
    random_action(b, turn, &a);
    sb_printf(&bot_info, "{\"algo\":\"random\"");
  } else {
    int positional = !strcmp(algo, "positional");
    search_stats stats;
    minimax(positional ? eval_positional : eval_material, b, depth, turn, &a, &stats);
    sb_printf(&bot_info,
              "{\"algo\":\"minimax\",\"eval\":\"%s\",\"depth\":%d,"
              "\"score\":%d,\"nodes\":%ld",
              positional ? "positional" : "material", depth, stats.score, stats.nodes);
  }

  char text[16];
  action_str(a, text, sizeof(text));
  sb_printf(&bot_info, ",\"color\":\"%s\",\"action\":\"%s\",\"ms\":%.1f}",
            color_name(turn), text, now_ms() - start);

  if (a.kind == ACTION_NONE)
    return "The bot has no legal moves.";
  if (game_play(a))
    return "The bot chose an illegal move.";
  return NULL;
}

// ======== HTTP

// copy the value of `key` from a query string like "from=B1&to=C3"
static int query_param(const char *query, const char *key, char *out, size_t size)
{
  size_t klen = strlen(key);
  const char *p = query;
  while (p && *p) {
    if (!strncmp(p, key, klen) && p[klen] == '=') {
      p += klen + 1;
      size_t i = 0;
      while (*p && *p != '&' && i + 1 < size)
        out[i++] = *p++;
      out[i] = '\0';
      return 1;
    }
    p = strchr(p, '&');
    if (p)
      ++p;
  }
  return 0;
}

static int parse_loc(const char *s, loc *k)
{
  if (strlen(s) != 2)
    return 0;
  char f = s[0] >= 'a' && s[0] <= 'z' ? s[0] - 'a' + 'A' : s[0];
  *k = loc_new(f, s[1] - '0');
  return on_board(*k);
}

static void send_response(int fd, int status, const char *type,
                          const char *body, size_t len)
{
  char header[256];
  const char *text = status == 200 ? "OK" : status == 404 ? "Not Found" : "Bad Request";
  int n = snprintf(header, sizeof(header),
                   "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
                   "Cache-Control: no-store\r\nConnection: close\r\n\r\n",
                   status, text, type, len);
  write(fd, header, n);
  size_t sent = 0;
  while (sent < len) {
    ssize_t w = write(fd, body + sent, len - sent);
    if (w <= 0)
      break;
    sent += w;
  }
}

static char *read_file(const char *path, size_t *len)
{
  FILE *f = fopen(path, "rb");
  if (!f)
    return NULL;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  char *data = malloc(n);
  *len = fread(data, 1, n, f);
  fclose(f);
  return data;
}

static char web_dir[1024] = "web";

static void serve_index(int fd)
{
  char path[1100];
  snprintf(path, sizeof(path), "%s/index.html", web_dir);
  size_t len;
  char *data = read_file(path, &len);
  if (!data) {
    const char *msg = "web/index.html not found";
    send_response(fd, 404, "text/plain", msg, strlen(msg));
    return;
  }
  send_response(fd, 200, "text/html; charset=utf-8", data, len);
  free(data);
}

static void handle_api(int fd, const char *method, const char *route, const char *query)
{
  const char *error = NULL;
  char a[16], b[16];

  if (!strcmp(route, "/api/state")) {
    // nothing to do
  } else if (strcmp(method, "POST")) {
    send_response(fd, 400, "text/plain", "use POST", 8);
    return;
  } else if (!strcmp(route, "/api/new")) {
    game_reset();
  } else if (!strcmp(route, "/api/move")) {
    loc src, dst;
    if (!query_param(query, "from", a, sizeof(a)) || !query_param(query, "to", b, sizeof(b)) ||
        !parse_loc(a, &src) || !parse_loc(b, &dst)) {
      error = "Bad move request.";
    } else {
      bot_info.len = 0;
      action act = {.kind = ACTION_MOVE, .m = move_new(src, dst)};
      int o = game_play(act);
      if (o)
        error = error_name(o);
    }
  } else if (!strcmp(route, "/api/fortify")) {
    loc k;
    if (!query_param(query, "at", a, sizeof(a)) || !parse_loc(a, &k)) {
      error = "Bad fortify request.";
    } else {
      bot_info.len = 0;
      action act = {.kind = ACTION_FORTIFY, .fort = k};
      int o = game_play(act);
      if (o)
        error = error_name(o);
    }
  } else if (!strcmp(route, "/api/bot")) {
    char algo[16] = "minimax";
    int depth = 5;
    query_param(query, "algo", algo, sizeof(algo));
    if (query_param(query, "depth", b, sizeof(b)))
      depth = atoi(b);
    if (depth < 1) depth = 1;
    if (depth > 8) depth = 8;
    error = run_bot(algo, depth);
  } else if (!strcmp(route, "/api/undo")) {
    int n = 1;
    if (query_param(query, "plies", b, sizeof(b)))
      n = atoi(b);
    game_undo(n);
  } else {
    send_response(fd, 404, "text/plain", "not found", 9);
    return;
  }

  strbuf out = {0};
  state_json(&out, error);
  send_response(fd, 200, "application/json", out.data, out.len);
  free(out.data);
}

static void handle_client(int fd)
{
  char req[REQUEST_MAX + 1];
  size_t len = 0;
  while (len < REQUEST_MAX) {
    ssize_t r = read(fd, req + len, REQUEST_MAX - len);
    if (r <= 0)
      break;
    len += r;
    req[len] = '\0';
    if (strstr(req, "\r\n\r\n"))
      break;
  }
  req[len] = '\0';

  char method[8], target[512];
  if (sscanf(req, "%7s %511s", method, target) != 2) {
    send_response(fd, 400, "text/plain", "bad request", 11);
    return;
  }

  char *query = strchr(target, '?');
  if (query)
    *query++ = '\0';
  else
    query = "";

  if (!strcmp(target, "/") || !strcmp(target, "/index.html"))
    serve_index(fd);
  else if (!strncmp(target, "/api/", 5))
    handle_api(fd, method, target, query);
  else
    send_response(fd, 404, "text/plain", "not found", 9);
}

// look for web/ next to the working directory or one level above the binary
static void find_web_dir(const char *argv0)
{
  if (access("web/index.html", R_OK) == 0)
    return;
  const char *slash = strrchr(argv0, '/');
  if (slash) {
    snprintf(web_dir, sizeof(web_dir), "%.*s/../web", (int)(slash - argv0), argv0);
  }
}

int main(int argc, char *argv[])
{
  int port = 8080;
  for (int i = 1; i < argc; ++i) {
    if (!strcmp(argv[i], "--port") && i + 1 < argc) {
      port = atoi(argv[++i]);
    } else {
      fprintf(stderr, "usage: %s [--port N]\n", argv[0]);
      return 1;
    }
  }

  signal(SIGPIPE, SIG_IGN);
  srandom(time(NULL));
  find_web_dir(argv[0]);
  positions[0] = board_setup();

  int server = socket(AF_INET, SOCK_STREAM, 0);
  int yes = 1;
  setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

  struct sockaddr_in addr = {0};
  addr.sin_family = AF_INET;
  inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
  addr.sin_port = htons(port);

  if (bind(server, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("bind");
    return 1;
  }
  if (listen(server, 16) < 0) {
    perror("listen");
    return 1;
  }

  printf("Knightshift is running at http://localhost:%d  (Ctrl+C to stop)\n", port);
  fflush(stdout);

  for (;;) {
    int client = accept(server, NULL, NULL);
    if (client < 0)
      continue;
    handle_client(client);
    close(client);
  }
}
