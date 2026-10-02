#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "knightshift.h"

// ======== pieces, locations, moves

piece *piece_new(color color, piece_type type)
{
  piece *p = malloc(sizeof(piece));
  p->color = color;
  p->type = type;
  return p;
}

piece *piece_dup(piece *p)
{
  return piece_new(p->color, p->type);
}

loc loc_new(char file, char rank)
{
  loc k;
  k.file = file;
  k.rank = rank;
  return k;
}

loc *loc_dup(loc k)
{
  loc *d = malloc(sizeof(loc));
  *d = k;
  return d;
}

char *loc_tos(loc k)
{
  char buf[8] = {0};
  snprintf(buf, sizeof(buf), "%c%d", k.file, k.rank);
  return strdup(buf);
}

int loc_eq(loc k1, loc k2)
{
  return k1.file == k2.file && k1.rank == k2.rank;
}

int on_board(loc k)
{
  return 'A' <= k.file && k.file <= 'E' && 1 <= k.rank && k.rank <= 8;
}

move move_new(loc src, loc dst)
{
  move m;
  m.src = src;
  m.dst = dst;
  return m;
}

move *move_dup(move m)
{
  move *d = malloc(sizeof(move));
  *d = m;
  return d;
}

char *move_tos(move m)
{
  char buf[16] = {0};
  snprintf(buf, sizeof(buf), "[%c%d -> %c%d]",
           m.src.file, m.src.rank, m.dst.file, m.dst.rank);
  return strdup(buf);
}

int move_eq(move m1, move m2)
{
  return loc_eq(m1.src, m2.src) && loc_eq(m1.dst, m2.dst);
}

// ======== lists

loc_list *lcons(loc k, loc_list *locs)
{
  loc_list *new_list = malloc(sizeof(loc_list));
  new_list->k = k;
  new_list->next = locs;
  return new_list;
}

void lfree(loc_list *locs)
{
  while (locs) {
    loc_list *next = locs->next;
    free(locs);
    locs = next;
  }
}

unsigned int llen(loc_list *locs)
{
  unsigned int n = 0;
  for (; locs; locs = locs->next)
    ++n;
  return n;
}

move_list *mcons(move m, move_list *moves)
{
  move_list *new_list = malloc(sizeof(move_list));
  new_list->m = m;
  new_list->next = moves;
  return new_list;
}

void mfree(move_list *moves)
{
  while (moves) {
    move_list *next = moves->next;
    free(moves);
    moves = next;
  }
}

unsigned int mlen(move_list *moves)
{
  unsigned int n = 0;
  for (; moves; moves = moves->next)
    ++n;
  return n;
}

// ======== board

color opponent(color c)
{
  return c == WHITE ? BLACK : WHITE;
}

static int loc_index(loc k)
{
  return (k.rank - 1) * BOARD_FILES + (k.file - 'A');
}

void put_piece(board *b, piece *p, loc k)
{
  b->squares[loc_index(k)] = p;
}

piece *piece_at(board *b, loc k)
{
  return b->squares[loc_index(k)];
}

board *board_empty()
{
  board *b = malloc(sizeof(board));
  for (int i = 0; i < BOARD_SQUARES; ++i)
    b->squares[i] = NULL;
  return b;
}

board *board_setup()
{
  board *b = board_empty();
  for (char file = 'A'; file <= 'E'; file++) {
    piece_type type = file == 'C' ? ROYAL_GUARD : KNIGHT;
    put_piece(b, piece_new(BLACK, type), loc_new(file, 8));
    put_piece(b, piece_new(WHITE, type), loc_new(file, 1));
  }
  return b;
}

board *board_from_strings(const char *ranks[])
{
  board *b = board_empty();
  for (int r = 0; r < BOARD_RANKS; ++r) {
    if (!ranks[r] || strlen(ranks[r]) != BOARD_FILES) {
      board_free(b);
      return NULL;
    }
    for (int f = 0; f < BOARD_FILES; ++f) {
      char c = ranks[r][f];
      if (c == '.')
        continue;
      color col = (c >= 'a' && c <= 'z') ? WHITE : BLACK;
      piece_type type;
      switch (c) {
      case 'k': case 'K': type = KNIGHT; break;
      case 'g': case 'G': type = ROYAL_GUARD; break;
      case 'f': case 'F': type = FORTRESS; break;
      default:
        board_free(b);
        return NULL;
      }
      put_piece(b, piece_new(col, type), loc_new('A' + f, BOARD_RANKS - r));
    }
  }
  return b;
}

board *board_dup(board *b)
{
  board *new_board = malloc(sizeof(board));
  for (int i = 0; i < BOARD_SQUARES; ++i) {
    piece *p = b->squares[i];
    new_board->squares[i] = p ? piece_dup(p) : NULL;
  }
  return new_board;
}

void board_free(board *b)
{
  for (int i = 0; i < BOARD_SQUARES; ++i)
    free(b->squares[i]);
  free(b);
}

int occupied(board *b, loc k)
{
  return piece_at(b, k) ? 1 : 0;
}

int unoccupied(board *b, loc k)
{
  return !piece_at(b, k);
}

loc_list *pieces_of(board *b, color curr)
{
  loc_list *locs = NULL;
  for (char f = 'A'; f <= 'E'; ++f) {
    for (char r = 1; r <= 8; ++r) {
      loc k = loc_new(f, r);
      piece *p = piece_at(b, k);
      if (p && p->color == curr)
        locs = lcons(k, locs);
    }
  }
  return locs;
}

int count_pieces(board *b, color curr, piece_type type)
{
  int n = 0;
  for (int i = 0; i < BOARD_SQUARES; ++i) {
    piece *p = b->squares[i];
    if (p && p->color == curr && p->type == type)
      ++n;
  }
  return n;
}

// ======== geometry

static loc loc_nudge(loc k, char df, char dr)
{
  return loc_new(k.file + df, k.rank + dr);
}

static loc_list *lcons_if(loc k, loc_list *locs)
{
  return on_board(k) ? lcons(k, locs) : locs;
}

loc_list *adjacent_knight(loc k)
{
  loc_list *locs = NULL;
  char dfs[] = {2, 2, -2, -2, 1, 1, -1, -1};
  char drs[] = {1, -1, 1, -1, 2, -2, 2, -2};
  for (int i = 0; i < 8; ++i)
    locs = lcons_if(loc_nudge(k, dfs[i], drs[i]), locs);
  return locs;
}

loc_list *adjacent_guard(loc k)
{
  loc_list *locs = NULL;
  char dfs[] = {1, 1, 1, -1, -1, -1, 0, 0};
  char drs[] = {1, -1, 0, 1, -1, 0, 1, -1};
  for (int i = 0; i < 8; ++i)
    locs = lcons_if(loc_nudge(k, dfs[i], drs[i]), locs);
  return locs;
}

loc_list *adjacent_ortho(loc k)
{
  loc_list *locs = NULL;
  char dfs[] = {1, -1, 0, 0};
  char drs[] = {0, 0, 1, -1};
  for (int i = 0; i < 4; ++i)
    locs = lcons_if(loc_nudge(k, dfs[i], drs[i]), locs);
  return locs;
}

// ======== rules

// true if a guard of color `guard_color` is orthogonally adjacent to k
static int guard_adjacent(board *b, loc k, color guard_color)
{
  loc_list *orth = adjacent_ortho(k);
  int found = 0;
  for (loc_list *it = orth; it && !found; it = it->next) {
    piece *g = piece_at(b, it->k);
    if (g && g->type == ROYAL_GUARD && g->color == guard_color)
      found = 1;
  }
  lfree(orth);
  return found;
}

int protected_knight_at(board *b, loc k)
{
  piece *p = piece_at(b, k);
  if (!p || p->type != KNIGHT)
    return 0;
  return guard_adjacent(b, k, p->color);
}

static int knight_shape(move m)
{
  int df = abs(m.src.file - m.dst.file);
  int dr = abs(m.src.rank - m.dst.rank);
  return (df == 1 && dr == 2) || (df == 2 && dr == 1);
}

static int guard_shape(move m)
{
  int df = abs(m.src.file - m.dst.file);
  int dr = abs(m.src.rank - m.dst.rank);
  if (df == 0 && dr == 0)
    return 0;
  return df <= 1 && dr <= 1;
}

static int opp_knight_at(board *b, loc k, color curr)
{
  piece *p = piece_at(b, k);
  return p && p->type == KNIGHT && p->color == opponent(curr);
}

int legal_knight(board *b, move m, color curr)
{
  if (!on_board(m.src) || !on_board(m.dst))
    return 0;
  piece *p = piece_at(b, m.src);
  if (!p || p->type != KNIGHT || p->color != curr)
    return 0;
  if (!knight_shape(m))
    return 0;
  if (protected_knight_at(b, m.dst))
    return 0;
  return unoccupied(b, m.dst) || opp_knight_at(b, m.dst, curr);
}

int legal_guard(board *b, move m, color curr)
{
  if (!on_board(m.src) || !on_board(m.dst))
    return 0;
  piece *p = piece_at(b, m.src);
  if (!p || p->type != ROYAL_GUARD || p->color != curr)
    return 0;
  if (!guard_shape(m))
    return 0;
  // a guard may not step into the enemy guard's zone of protection
  if (guard_adjacent(b, m.dst, opponent(curr)))
    return 0;
  return unoccupied(b, m.dst) || opp_knight_at(b, m.dst, curr);
}

move_list *available_moves(board *b, color curr)
{
  loc_list *locs = pieces_of(b, curr);
  move_list *moves = NULL;
  for (loc_list *it = locs; it; it = it->next) {
    piece *p = piece_at(b, it->k);
    loc_list *dsts = NULL;
    if (p->type == KNIGHT)
      dsts = adjacent_knight(it->k);
    else if (p->type == ROYAL_GUARD)
      dsts = adjacent_guard(it->k);
    for (loc_list *d = dsts; d; d = d->next) {
      move m = move_new(it->k, d->k);
      int legal = p->type == KNIGHT ? legal_knight(b, m, curr)
                                    : legal_guard(b, m, curr);
      if (legal)
        moves = mcons(m, moves);
    }
    lfree(dsts);
  }
  lfree(locs);
  return moves;
}

loc_list *available_fortifications(board *b, color curr)
{
  loc_list *locs = NULL;
  char rank = curr == WHITE ? 8 : 1;
  for (char file = 'A'; file <= 'E'; ++file) {
    loc k = loc_new(file, rank);
    piece *p = piece_at(b, k);
    if (p && p->type == KNIGHT && p->color == curr)
      locs = lcons(k, locs);
  }
  return locs;
}

// move a piece without legality checks; returns the displaced piece
static piece *do_move(board *b, move m)
{
  piece *p = piece_at(b, m.src);
  piece *q = piece_at(b, m.dst);
  if (p) {
    put_piece(b, p, m.dst);
    put_piece(b, NULL, m.src);
  }
  return q;
}

board *do_move_new(board *b, move m)
{
  board *b2 = board_dup(b);
  free(do_move(b2, m));
  return b2;
}

board *do_fortify_new(board *b, loc k, color curr)
{
  int o = 0;
  board *b2 = board_dup(b);
  fortify(b2, k, curr, &o);
  if (o) {
    fprintf(stderr, "(do_fortify_new) outcome %d\n", o);
    exit(1);
  }
  return b2;
}

piece *apply_move(board *b, move m, color curr, int *outcome)
{
  if (!on_board(m.src) || !on_board(m.dst)) {
    *outcome = ERR_BAD_LOCATION;
    return NULL;
  }
  piece *p = piece_at(b, m.src);
  if (!p) {
    *outcome = ERR_NO_PIECE;
    return NULL;
  }
  if (p->color != curr) {
    *outcome = ERR_BAD_MOVE;
    return NULL;
  }
  piece *q = piece_at(b, m.dst);
  if (q && q->color == curr) {
    *outcome = ERR_FRIENDLY;
    return NULL;
  }
  if ((p->type == KNIGHT && legal_knight(b, m, curr)) ||
      (p->type == ROYAL_GUARD && legal_guard(b, m, curr))) {
    *outcome = 0;
    return do_move(b, m);
  }
  *outcome = ERR_BAD_MOVE;
  return NULL;
}

void fortify(board *b, loc k, color curr, int *outcome)
{
  if (!on_board(k)) {
    *outcome = ERR_BAD_LOCATION;
    return;
  }
  if ((curr == WHITE && k.rank != 8) || (curr == BLACK && k.rank != 1)) {
    *outcome = ERR_BAD_LOCATION;
    return;
  }
  piece *p = piece_at(b, k);
  if (!p) {
    *outcome = ERR_NO_PIECE;
    return;
  }
  if (p->type != KNIGHT) {
    *outcome = ERR_PIECE_TYPE;
    return;
  }
  if (p->color != curr) {
    *outcome = ERR_BAD_MOVE;
    return;
  }
  p->type = FORTRESS;
  *outcome = 0;
}

int game_over(board *b)
{
  if (count_pieces(b, WHITE, FORTRESS) >= 2)
    return VICTORY_WHITE_TWO_FORTRESSES;
  if (count_pieces(b, WHITE, KNIGHT) == 1)
    return VICTORY_BLACK_THREE_CAPTURES;
  if (count_pieces(b, BLACK, FORTRESS) >= 2)
    return VICTORY_BLACK_TWO_FORTRESSES;
  if (count_pieces(b, BLACK, KNIGHT) == 1)
    return VICTORY_WHITE_THREE_CAPTURES;
  return 0;
}

color victory_winner(int outcome)
{
  return (outcome == VICTORY_WHITE_TWO_FORTRESSES ||
          outcome == VICTORY_WHITE_THREE_CAPTURES) ? WHITE : BLACK;
}
