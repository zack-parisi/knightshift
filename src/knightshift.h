#ifndef KNIGHTSHIFT_H
#define KNIGHTSHIFT_H

// Knightshift rules engine.
//
// The board is 5 files (A-E) by 8 ranks (1-8). Each side starts with four
// knights and a royal guard on its home rank. See README.md for the rules.

// ======== constants

#define BOARD_FILES 5
#define BOARD_RANKS 8
#define BOARD_SQUARES (BOARD_FILES * BOARD_RANKS)

// error outcomes reported by apply_move and fortify
#define ERR_NO_PIECE 64
#define ERR_PIECE_TYPE 65
#define ERR_BAD_MOVE 66
#define ERR_FRIENDLY 67
#define ERR_BAD_LOCATION 68

// game_over results
#define VICTORY_WHITE_TWO_FORTRESSES 32
#define VICTORY_BLACK_TWO_FORTRESSES 33
#define VICTORY_WHITE_THREE_CAPTURES 34
#define VICTORY_BLACK_THREE_CAPTURES 35

#define UNICODE_KING_WHITE    "♔"
#define UNICODE_ROOK_WHITE    "♖"
#define UNICODE_KNIGHT_WHITE  "♘"
#define UNICODE_KING_BLACK    "♚"
#define UNICODE_ROOK_BLACK    "♜"
#define UNICODE_KNIGHT_BLACK  "♞"

// ======== data definitions

typedef enum {
  BLACK,
  WHITE
} color;

typedef enum {
  KNIGHT,
  ROYAL_GUARD,
  FORTRESS
} piece_type;

typedef struct piece {
  color color;
  piece_type type;
} piece;

typedef struct board {
  // array of piece *pointers*, NULL means no piece
  // index is (rank-1)*5 + (file-'A')
  piece *squares[BOARD_SQUARES];
} board;

typedef struct loc {
  char file;
  char rank;
} loc;

typedef struct move {
  loc src;
  loc dst;
} move;

typedef struct loc_list {
  loc k;
  struct loc_list *next;
} loc_list;

typedef struct move_list {
  move m;
  struct move_list *next;
} move_list;

// ======== operations

// -------- piece operations

piece *piece_new(color color, piece_type type);

piece *piece_dup(piece *p);

// -------- loc operations

loc loc_new(char file, char rank);

loc *loc_dup(loc k);

// caller frees the returned string
char *loc_tos(loc k);

int loc_eq(loc k1, loc k2);

int on_board(loc k);

// -------- move operations

move move_new(loc src, loc dst);

move *move_dup(move m);

// caller frees the returned string
char *move_tos(move m);

int move_eq(move m1, move m2);

// -------- list operations

loc_list *lcons(loc k, loc_list *locs);

void lfree(loc_list *locs);

unsigned int llen(loc_list *locs);

move_list *mcons(move m, move_list *moves);

void mfree(move_list *moves);

unsigned int mlen(move_list *moves);

// --------- board operations

color opponent(color c);

// return a freshly-constructed empty board
board *board_empty();

// return a freshly-constructed board, ready to play the game
board *board_setup();

// build a board from 8 strings of 5 characters, rank 8 first
// k, g, f for white knight, guard, fortress; K, G, F for black; . for empty
// returns NULL if the input is malformed
board *board_from_strings(const char *ranks[]);

// return a deep copy of the board
board *board_dup(board *b);

// free board and all pieces pointed to
void board_free(board *b);

// return the piece at the location, or NULL for an empty square
// GIGO for bad location
piece *piece_at(board *b, loc k);

// put piece (or NULL) at given location on board uncritically
// (no legality check)
void put_piece(board *b, piece *p, loc k);

// return true if space is not empty, false otherwise
int occupied(board *b, loc k);

// return true if space is empty, false otherwise
int unoccupied(board *b, loc k);

// curr means "current player's color"
// return the locations of the current player's pieces
loc_list *pieces_of(board *b, color curr);

// count the given color's pieces of the given type
int count_pieces(board *b, color curr, piece_type type);

// --------- geometry

// return the (as many as) eight locations that are
// a knight's move away from the argument location
loc_list *adjacent_knight(loc k);

// return the (as many as) eight locations that are
// a guard's move away from the argument location
loc_list *adjacent_guard(loc k);

// return the (as many as) four locations that are
// orthogonally adjacent to the argument location
loc_list *adjacent_ortho(loc k);

// --------- rules

// return true if there is a *protected knight* at the location
// a knight is protected when orthogonally adjacent to its own guard
int protected_knight_at(board *b, loc k);

// return true if proposed move is a legal knight's move for curr color
// for a move to be legal, it must either end on an open space
//   or capture an unprotected opponent's knight
int legal_knight(board *b, move m, color curr);

// return true if proposed move is a legal guard's move for curr color
// for a move to be legal, it must either end on an open space
//   or capture an unprotected opponent's knight
// it also may not end in the opponent's guard's zone of protection
int legal_guard(board *b, move m, color curr);

// return a list of all legal moves for the current player
move_list *available_moves(board *b, color curr);

// return a list of all legal fortifications for the current player
loc_list *available_fortifications(board *b, color curr);

// apply the given move if possible
// set *outcome to 0 for successful move
// return pointer to captured piece (caller frees), or NULL for no capture
piece *apply_move(board *b, move m, color curr, int *outcome);

// fortify the given location if possible
// set *outcome to 0 for successful fortification
void fortify(board *b, loc k, color curr, int *outcome);

// return 0 if game is not over
// otherwise return one of the VICTORY constants (see above)
int game_over(board *b);

// return the winning color of a VICTORY constant
color victory_winner(int outcome);

// return a new board that is the result of fortifying
// GIGO in case of illegitimate fortification
board *do_fortify_new(board *b, loc k, color curr);

// return a new board that is the result of applying move
// GIGO in case of bad move
board *do_move_new(board *b, move m);

#endif
