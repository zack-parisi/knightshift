#ifndef BOT_H
#define BOT_H

#include "knightshift.h"

// A single turn: either move a piece or fortify a knight.
typedef enum { ACTION_NONE, ACTION_MOVE, ACTION_FORTIFY } action_kind;

typedef struct {
  action_kind kind;
  move m;     // valid when kind == ACTION_MOVE
  loc fort;   // valid when kind == ACTION_FORTIFY
} action;

// apply an action to the board; returns 0 on success or an ERR_ outcome
// sets *captured (if non-NULL) to whether a piece was captured
int action_apply(board *b, action a, color curr, int *captured);

// write a short human-readable description such as "B1-C3" or "F A8"
void action_str(action a, char *buf, int size);

// -------- evaluation functions
// positive scores favor white, negative scores favor black

typedef int (*eval_fn)(board *);

// material only: knight 3, fortress 10
int eval_material(board *b);

// material plus protected knights, mobility and fortification readiness
int eval_positional(board *b);

// -------- minimax with alpha-beta pruning

#define WIN_SCORE 100000

typedef struct {
  long nodes;     // positions visited
  int score;      // evaluation of the chosen line
} search_stats;

// choose the best action for `curr` by searching `depth` plies ahead
// returns the score; *out is ACTION_NONE if curr has nothing to play
int minimax(eval_fn eval, board *b, unsigned int depth, color curr,
            action *out, search_stats *stats);

// -------- rule-based decision tree

// the branches of the tree, checked in order; the first one that
// produces an action wins
typedef enum {
  RULE_WIN_NOW,
  RULE_BLOCK_THREAT,
  RULE_FORTIFY,
  RULE_SAFE_CAPTURE,
  RULE_RESCUE,
  RULE_ADVANCE,
  RULE_ESCORT,
  RULE_SAFE_MOVE,
  RULE_ANY_MOVE,
  RULE_COUNT
} tree_rule;

extern const char *tree_rule_names[RULE_COUNT];

typedef struct {
  tree_rule rule;     // branch that produced the action
  char reason[160];   // explanation of the choice
} tree_trace;

// choose an action by walking the decision tree
void decision_tree(board *b, color curr, action *out, tree_trace *trace);

// -------- random

// choose a uniformly random legal action
void random_action(board *b, color curr, action *out);

#endif
