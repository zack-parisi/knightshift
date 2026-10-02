// Terminal version of Knightshift: you play white against a bot.
//
// usage: knightshift [--bot minimax|positional|tree|random] [--depth N]
//                    [--watch]

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include "knightshift.h"
#include "bot.h"

typedef enum { MOVE, FORTIFY, HELP, QUIT, UNKNOWN } command_kind;

typedef struct {
  command_kind kind;
  loc src;
  loc dst;
} command;

typedef enum { BOT_MINIMAX, BOT_POSITIONAL, BOT_TREE, BOT_RANDOM } bot_kind;

// ======== display

static const char *square_str(piece *p, loc k)
{
  if (!p)
    return (k.file + k.rank) % 2 == 0 ? "~" : " ";
  if (p->color == WHITE) {
    if (p->type == KNIGHT) return UNICODE_KNIGHT_WHITE;
    if (p->type == ROYAL_GUARD) return UNICODE_KING_WHITE;
    return UNICODE_ROOK_WHITE;
  }
  if (p->type == KNIGHT) return UNICODE_KNIGHT_BLACK;
  if (p->type == ROYAL_GUARD) return UNICODE_KING_BLACK;
  return UNICODE_ROOK_BLACK;
}

static void board_show(board *b)
{
  for (int rank = 8; rank >= 1; --rank) {
    printf("-----------\n");
    for (char file = 'A'; file <= 'E'; ++file) {
      loc l = loc_new(file, rank);
      printf("|%s", square_str(piece_at(b, l), l));
    }
    printf("| %d\n", rank);
  }
  printf("-----------\n");
  printf(" A B C D E \n");
}

static void help_show()
{
  printf("\n");
  printf("-------- HELP --------\n");
  printf("M: move; ex: M A1 B3\n");
  printf("F: fortify, ex: F A8\n");
  printf("H: display (these) help messages\n");
  printf("Q: quit\n");
  printf("(commands are case insensitive and ignore spaces)\n");
  printf("(the M/m can be omitted from a move)\n");
  printf("\n");
}

static void error_show(int outcome)
{
  printf("ERROR: ");
  switch (outcome) {
  case ERR_NO_PIECE:     printf("no piece at the source location\n"); break;
  case ERR_PIECE_TYPE:   printf("wrong piece type for selection\n"); break;
  case ERR_BAD_MOVE:     printf("illegal move\n"); break;
  case ERR_FRIENDLY:     printf("destination space contains friendly piece\n"); break;
  case ERR_BAD_LOCATION: printf("bad location\n"); break;
  default:               printf("unknown problem (outcome=%d)\n", outcome); break;
  }
}

// ======== parsing

static int isfile(char c) { return 'A' <= c && c <= 'E'; }
static int isrank(char c) { return '1' <= c && c <= '8'; }

// remove all spaces and make all characters uppercase
// ex: turns "M b1   C3  " into "MB1C3"
static void canonicalize(const char *line, char *canon, size_t size)
{
  size_t j = 0;
  for (size_t i = 0; line[i] && j + 1 < size; ++i) {
    if (isdigit((unsigned char)line[i]))
      canon[j++] = line[i];
    else if (isalpha((unsigned char)line[i]))
      canon[j++] = toupper((unsigned char)line[i]);
  }
  canon[j] = '\0';
}

static command parse(const char *s)
{
  command cmd = {.kind = UNKNOWN};
  char canon[64];
  canonicalize(s, canon, sizeof(canon));
  int len = strlen(canon);
  const char *c = canon;

  if ((len == 5 && c[0] == 'M') || len == 4) {
    if (len == 5)
      ++c;
    if (isfile(c[0]) && isrank(c[1]) && isfile(c[2]) && isrank(c[3])) {
      cmd.kind = MOVE;
      cmd.src = loc_new(c[0], c[1] - '0');
      cmd.dst = loc_new(c[2], c[3] - '0');
    }
  } else if (len == 3 && c[0] == 'F' && isfile(c[1]) && isrank(c[2])) {
    cmd.kind = FORTIFY;
    cmd.src = loc_new(c[1], c[2] - '0');
  } else if (len > 0 && c[0] == 'H') {
    cmd.kind = HELP;
  } else if (len > 0 && c[0] == 'Q') {
    cmd.kind = QUIT;
  }
  return cmd;
}

// ======== game flow

static int handle_victory(board *b)
{
  int g = game_over(b);
  if (!g)
    return 0;
  printf(">>>VICTORY<<<\n");
  switch (g) {
  case VICTORY_WHITE_TWO_FORTRESSES: printf("White wins with two fortresses!\n"); break;
  case VICTORY_WHITE_THREE_CAPTURES: printf("White wins with three captures!\n"); break;
  case VICTORY_BLACK_TWO_FORTRESSES: printf("Black wins with two fortresses!\n"); break;
  case VICTORY_BLACK_THREE_CAPTURES: printf("Black wins with three captures!\n"); break;
  }
  return g;
}

static void bot_turn(board *b, color curr, bot_kind bot, int depth)
{
  const char *name = curr == WHITE ? "White" : "Black";
  action a;
  char astr[16];

  if (bot == BOT_TREE) {
    tree_trace t;
    decision_tree(b, curr, &a, &t);
    printf("(decision tree: %s) %s\n", tree_rule_names[t.rule], t.reason);
  } else if (bot == BOT_RANDOM) {
    random_action(b, curr, &a);
  } else {
    printf("(thinking...)\n");
    search_stats stats;
    eval_fn eval = bot == BOT_POSITIONAL ? eval_positional : eval_material;
    minimax(eval, b, depth, curr, &a, &stats);
    printf("(minimax depth %d: score %d, %ld positions)\n",
           depth, stats.score, stats.nodes);
  }

  if (a.kind == ACTION_NONE) {
    printf("%s has no legal moves\n", name);
    return;
  }
  int captured = 0;
  action_apply(b, a, curr, &captured);
  action_str(a, astr, sizeof(astr));
  printf("%s plays %s\n", name, astr);
  if (captured)
    printf(">>>CAPTURE<<<\n");
  board_show(b);
}

static void usage(const char *prog)
{
  fprintf(stderr,
          "usage: %s [--bot minimax|positional|tree|random] [--depth N] [--watch]\n"
          "  --bot     black's algorithm (default: minimax)\n"
          "  --depth   minimax search depth (default: 5)\n"
          "  --watch   watch the decision tree (white) play the chosen bot (black)\n",
          prog);
}

static int parse_bot(const char *s, bot_kind *out)
{
  if (!strcmp(s, "minimax")) *out = BOT_MINIMAX;
  else if (!strcmp(s, "positional")) *out = BOT_POSITIONAL;
  else if (!strcmp(s, "tree")) *out = BOT_TREE;
  else if (!strcmp(s, "random")) *out = BOT_RANDOM;
  else return 0;
  return 1;
}

static void watch(bot_kind black_bot, int depth)
{
  board *b = board_setup();
  color turn = WHITE;
  board_show(b);
  for (int ply = 0; ply < 200 && !game_over(b); ++ply) {
    printf("\n");
    bot_turn(b, turn, turn == WHITE ? BOT_TREE : black_bot, depth);
    turn = opponent(turn);
  }
  if (!handle_victory(b))
    printf("No result after 200 moves.\n");
  board_free(b);
}

int main(int argc, char *argv[])
{
  bot_kind bot = BOT_MINIMAX;
  int depth = 5;
  int watch_mode = 0;

  for (int i = 1; i < argc; ++i) {
    if (!strcmp(argv[i], "--bot") && i + 1 < argc) {
      if (!parse_bot(argv[++i], &bot)) {
        usage(argv[0]);
        return 1;
      }
    } else if (!strcmp(argv[i], "--depth") && i + 1 < argc) {
      depth = atoi(argv[++i]);
      if (depth < 1 || depth > 8) {
        fprintf(stderr, "depth must be between 1 and 8\n");
        return 1;
      }
    } else if (!strcmp(argv[i], "--watch")) {
      watch_mode = 1;
    } else {
      usage(argv[0]);
      return 1;
    }
  }

  srandom(time(NULL));

  if (watch_mode) {
    watch(bot, depth);
    return 0;
  }

  char *line = NULL;
  size_t size = 0;
  board *b = board_setup();

  board_show(b);
  printf("-> White's turn ('H' for help)\n > ");

  while (getline(&line, &size, stdin) != -1) {
    command cmd = parse(line);
    int o = 0;

    if (cmd.kind == QUIT) {
      printf("Bye!\n");
      break;
    } else if (cmd.kind == HELP) {
      help_show();
      printf("-> White's turn ('H' for help)\n > ");
      continue;
    } else if (cmd.kind == FORTIFY) {
      fortify(b, cmd.src, WHITE, &o);
    } else if (cmd.kind == MOVE) {
      piece *p = apply_move(b, move_new(cmd.src, cmd.dst), WHITE, &o);
      if (p) {
        printf(">>>CAPTURE<<<\n");
        free(p);
      }
    } else {
      printf("unrecognized command\n > ");
      continue;
    }

    if (o) {
      error_show(o);
      printf(" > ");
      continue;
    }
    board_show(b);
    if (handle_victory(b))
      break;

    bot_turn(b, BLACK, bot, depth);
    if (handle_victory(b))
      break;

    printf("-> White's turn ('H' for help)\n > ");
  }

  free(line);
  board_free(b);
  return 0;
}
