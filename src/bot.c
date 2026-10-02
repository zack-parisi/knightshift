#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bot.h"

// upper bound on actions in one position: 4 knights * 8 + guard 8 + 5 forts
#define MAX_ACTIONS 64
#define INF (WIN_SCORE * 2)

// ======== actions

int action_apply(board *b, action a, color curr, int *captured)
{
  int o = 0;
  if (captured)
    *captured = 0;
  if (a.kind == ACTION_MOVE) {
    piece *q = apply_move(b, a.m, curr, &o);
    if (q) {
      if (captured)
        *captured = 1;
      free(q);
    }
  } else if (a.kind == ACTION_FORTIFY) {
    fortify(b, a.fort, curr, &o);
  } else {
    o = ERR_BAD_MOVE;
  }
  return o;
}

void action_str(action a, char *buf, int size)
{
  if (a.kind == ACTION_MOVE)
    snprintf(buf, size, "%c%d-%c%d", a.m.src.file, a.m.src.rank,
             a.m.dst.file, a.m.dst.rank);
  else if (a.kind == ACTION_FORTIFY)
    snprintf(buf, size, "F %c%d", a.fort.file, a.fort.rank);
  else
    snprintf(buf, size, "--");
}

// collect every legal action for curr into acts, ordered
// fortifications, then captures, then quiet moves (helps alpha-beta prune)
static int gen_actions(board *b, color curr, action acts[MAX_ACTIONS])
{
  int n = 0;
  loc_list *forts = available_fortifications(b, curr);
  for (loc_list *it = forts; it; it = it->next) {
    acts[n].kind = ACTION_FORTIFY;
    acts[n].fort = it->k;
    ++n;
  }
  lfree(forts);

  move_list *moves = available_moves(b, curr);
  for (int pass = 0; pass < 2; ++pass) {
    for (move_list *it = moves; it; it = it->next) {
      int capture = occupied(b, it->m.dst);
      if (capture == (pass == 0)) {
        acts[n].kind = ACTION_MOVE;
        acts[n].m = it->m;
        ++n;
      }
    }
  }
  mfree(moves);
  return n;
}

// ======== evaluation

static int count_mobility(board *b, color curr)
{
  move_list *ms = available_moves(b, curr);
  int n = mlen(ms);
  mfree(ms);
  return n;
}

static int count_fortifiable(board *b, color curr)
{
  loc_list *ls = available_fortifications(b, curr);
  int n = llen(ls);
  lfree(ls);
  return n;
}

static int material(board *b, color curr)
{
  return 3 * count_pieces(b, curr, KNIGHT) + 10 * count_pieces(b, curr, FORTRESS);
}

int eval_material(board *b)
{
  return material(b, WHITE) - material(b, BLACK);
}

static int positional(board *b, color curr)
{
  int score = material(b, curr);
  loc_list *mine = pieces_of(b, curr);
  for (loc_list *it = mine; it; it = it->next)
    if (protected_knight_at(b, it->k))
      score += 2;
  lfree(mine);
  score += count_mobility(b, curr);
  score += 5 * count_fortifiable(b, curr);
  return score;
}

int eval_positional(board *b)
{
  return positional(b, WHITE) - positional(b, BLACK);
}

// ======== minimax with alpha-beta pruning

// wins found sooner score higher, so the bot takes the fastest win
// and delays a loss as long as possible
static int terminal_score(int outcome, int ply)
{
  int s = WIN_SCORE - ply;
  return victory_winner(outcome) == WHITE ? s : -s;
}

static int search(eval_fn eval, board *b, int depth, int ply, color curr,
                  int alpha, int beta, action *best, long *nodes)
{
  ++*nodes;
  int outcome = game_over(b);
  if (outcome)
    return terminal_score(outcome, ply);
  if (depth == 0)
    return eval(b);

  action acts[MAX_ACTIONS];
  int n = gen_actions(b, curr, acts);
  if (n == 0)
    return eval(b);

  int maximizing = curr == WHITE;
  int best_score = maximizing ? -INF : INF;
  for (int i = 0; i < n; ++i) {
    board *child = board_dup(b);
    action_apply(child, acts[i], curr, NULL);
    int s = search(eval, child, depth - 1, ply + 1, opponent(curr),
                   alpha, beta, NULL, nodes);
    board_free(child);

    if (maximizing ? s > best_score : s < best_score) {
      best_score = s;
      if (best)
        *best = acts[i];
    }
    if (maximizing && s > alpha)
      alpha = s;
    if (!maximizing && s < beta)
      beta = s;
    if (alpha >= beta)
      break;
  }
  return best_score;
}

int minimax(eval_fn eval, board *b, unsigned int depth, color curr,
            action *out, search_stats *stats)
{
  long nodes = 0;
  out->kind = ACTION_NONE;
  int score = search(eval, b, depth, 0, curr, -INF, INF, out, &nodes);
  if (stats) {
    stats->nodes = nodes;
    stats->score = score;
  }
  return score;
}

// ======== decision tree

const char *tree_rule_names[RULE_COUNT] = {
  "Win immediately",
  "Block the opponent's winning threat",
  "Fortify a knight on the back rank",
  "Capture a knight safely",
  "Rescue a threatened knight",
  "Advance a knight safely",
  "Escort with the royal guard",
  "Make a safe move",
  "Make any legal move",
};

// how far a location is toward curr's goal rank, from 1 (home) to 8 (goal)
static int progress(color curr, loc k)
{
  return curr == WHITE ? k.rank : 9 - k.rank;
}

static int is_winner(board *b, color curr)
{
  int outcome = game_over(b);
  return outcome && victory_winner(outcome) == curr;
}

// can `attacker` capture whatever stands on k with one move?
static int attacked(board *b, loc k, color attacker)
{
  move_list *ms = available_moves(b, attacker);
  int hit = 0;
  for (move_list *it = ms; it && !hit; it = it->next)
    if (loc_eq(it->m.dst, k))
      hit = 1;
  mfree(ms);
  return hit;
}

// how many of curr's knights the opponent can capture next turn
static int threatened_knights(board *b, color curr)
{
  move_list *ms = available_moves(b, opponent(curr));
  int seen[BOARD_SQUARES] = {0};
  int n = 0;
  for (move_list *it = ms; it; it = it->next) {
    piece *p = piece_at(b, it->m.dst);
    int idx = (it->m.dst.rank - 1) * BOARD_FILES + (it->m.dst.file - 'A');
    if (p && p->color == curr && p->type == KNIGHT && !seen[idx]) {
      seen[idx] = 1;
      ++n;
    }
  }
  mfree(ms);
  return n;
}

// does the opponent have a move that wins the game right now?
static int opponent_can_win(board *b, color curr)
{
  color opp = opponent(curr);
  action acts[MAX_ACTIONS];
  int n = gen_actions(b, opp, acts);
  for (int i = 0; i < n; ++i) {
    board *child = board_dup(b);
    action_apply(child, acts[i], opp, NULL);
    int win = is_winner(child, opp);
    board_free(child);
    if (win)
      return 1;
  }
  return 0;
}

// what we know about one candidate action
typedef struct {
  action a;
  int capture;        // takes an enemy knight
  int wins;           // ends the game in our favor
  int allows_loss;    // opponent can win on the reply
  int threats;        // our knights capturable after this action
  int piece_safe;     // the moved piece cannot be captured afterwards
  piece_type mover;   // type of the moved piece (moves only)
} candidate;

static loc best_knight(board *b, color curr, int *found)
{
  loc best = loc_new('A', 1);
  int best_p = -1;
  loc_list *mine = pieces_of(b, curr);
  for (loc_list *it = mine; it; it = it->next) {
    piece *p = piece_at(b, it->k);
    if (p->type == KNIGHT && progress(curr, it->k) > best_p) {
      best_p = progress(curr, it->k);
      best = it->k;
    }
  }
  lfree(mine);
  *found = best_p >= 0;
  return best;
}

static int chebyshev(loc a, loc b)
{
  int df = abs(a.file - b.file);
  int dr = abs(a.rank - b.rank);
  return df > dr ? df : dr;
}

static void choose(candidate *c, tree_rule rule, action *out, tree_trace *t,
                   const char *fmt, const char *detail)
{
  char act[16];
  action_str(c->a, act, sizeof(act));
  *out = c->a;
  t->rule = rule;
  snprintf(t->reason, sizeof(t->reason), fmt, act, detail ? detail : "");
}

void decision_tree(board *b, color curr, action *out, tree_trace *t)
{
  action acts[MAX_ACTIONS];
  candidate cs[MAX_ACTIONS];
  int n = gen_actions(b, curr, acts);

  out->kind = ACTION_NONE;
  t->rule = RULE_ANY_MOVE;
  snprintf(t->reason, sizeof(t->reason), "No legal moves available");
  if (n == 0)
    return;

  // look one move ahead for every candidate
  int any_safe_from_loss = 0;
  for (int i = 0; i < n; ++i) {
    candidate *c = &cs[i];
    c->a = acts[i];
    c->mover = KNIGHT;
    if (acts[i].kind == ACTION_MOVE)
      c->mover = piece_at(b, acts[i].m.src)->type;
    board *child = board_dup(b);
    action_apply(child, acts[i], curr, &c->capture);
    c->wins = is_winner(child, curr);
    c->allows_loss = !c->wins && opponent_can_win(child, curr);
    c->threats = threatened_knights(child, curr);
    c->piece_safe = acts[i].kind != ACTION_MOVE || c->mover != KNIGHT ||
                    !attacked(child, acts[i].m.dst, opponent(curr));
    board_free(child);
    if (!c->allows_loss)
      any_safe_from_loss = 1;
  }
  // if every action loses, stop filtering on it
  if (!any_safe_from_loss)
    for (int i = 0; i < n; ++i)
      cs[i].allows_loss = 0;

  int threats_now = threatened_knights(b, curr);

  // 1. win immediately
  for (int i = 0; i < n; ++i)
    if (cs[i].wins) {
      choose(&cs[i], RULE_WIN_NOW, out, t, "%s wins the game%s", NULL);
      return;
    }

  // 2. the opponent threatens to win: find an action that stops it
  if (any_safe_from_loss && opponent_can_win(b, curr)) {
    int pick = -1;
    for (int i = 0; i < n; ++i)
      if (!cs[i].allows_loss && (pick < 0 || (cs[i].capture && !cs[pick].capture)))
        pick = i;
    choose(&cs[pick], RULE_BLOCK_THREAT, out, t,
           "Opponent was one move from winning; %s stops it%s", NULL);
    return;
  }

  // 3. fortify: fortresses can never be captured
  for (int i = 0; i < n; ++i)
    if (cs[i].a.kind == ACTION_FORTIFY && !cs[i].allows_loss) {
      choose(&cs[i], RULE_FORTIFY, out, t,
             "%s turns a knight into a fortress%s", NULL);
      return;
    }

  // 4. capture a knight without the capturing piece being taken back
  for (int i = 0; i < n; ++i)
    if (cs[i].capture && cs[i].piece_safe && !cs[i].allows_loss) {
      choose(&cs[i], RULE_SAFE_CAPTURE, out, t,
             "%s captures an unprotected knight%s",
             cs[i].mover == ROYAL_GUARD ? " with the guard, which can't be captured"
                                        : " and the knight can't be taken back");
      return;
    }

  // 5. one of our knights is hanging: reduce the number under attack
  if (threats_now > 0) {
    int pick = -1;
    for (int i = 0; i < n; ++i)
      if (!cs[i].allows_loss && cs[i].threats < threats_now &&
          (pick < 0 || cs[i].threats < cs[pick].threats))
        pick = i;
    if (pick >= 0) {
      char detail[64];
      snprintf(detail, sizeof(detail), " (%d knight%s under attack -> %d)",
               threats_now, threats_now == 1 ? "" : "s", cs[pick].threats);
      choose(&cs[pick], RULE_RESCUE, out, t, "%s saves a threatened knight%s", detail);
      return;
    }
  }

  // 6. advance the knight that gets furthest toward the goal rank safely
  {
    int pick = -1;
    for (int i = 0; i < n; ++i) {
      candidate *c = &cs[i];
      if (c->a.kind != ACTION_MOVE || c->mover != KNIGHT || c->allows_loss ||
          !c->piece_safe || c->threats > threats_now)
        continue;
      if (progress(curr, c->a.m.dst) <= progress(curr, c->a.m.src))
        continue;
      if (pick < 0 || progress(curr, c->a.m.dst) > progress(curr, cs[pick].a.m.dst))
        pick = i;
    }
    if (pick >= 0) {
      char detail[64];
      int to_go = 8 - progress(curr, cs[pick].a.m.dst);
      if (to_go == 0)
        snprintf(detail, sizeof(detail), " and reaches the back rank");
      else
        snprintf(detail, sizeof(detail), " (%d rank%s from the back rank)",
                 to_go, to_go == 1 ? "" : "s");
      choose(&cs[pick], RULE_ADVANCE, out, t, "%s advances a knight safely%s", detail);
      return;
    }
  }

  // 7. bring the guard closer to the most advanced knight to protect it
  {
    int found;
    loc target = best_knight(b, curr, &found);
    int pick = -1;
    for (int i = 0; found && i < n; ++i) {
      candidate *c = &cs[i];
      if (c->a.kind != ACTION_MOVE || c->mover != ROYAL_GUARD || c->allows_loss ||
          c->threats > threats_now)
        continue;
      int before = chebyshev(c->a.m.src, target);
      int after = chebyshev(c->a.m.dst, target);
      if (before > 1 && after < before &&
          (pick < 0 || after < chebyshev(cs[pick].a.m.dst, target)))
        pick = i;
    }
    if (pick >= 0) {
      char detail[32];
      snprintf(detail, sizeof(detail), " toward the knight on %c%d",
               target.file, target.rank);
      choose(&cs[pick], RULE_ESCORT, out, t, "%s moves the guard%s", detail);
      return;
    }
  }

  // 8. anything that doesn't hang a piece
  for (int i = 0; i < n; ++i)
    if (!cs[i].allows_loss && cs[i].piece_safe && cs[i].threats <= threats_now) {
      choose(&cs[i], RULE_SAFE_MOVE, out, t, "%s keeps every piece safe%s", NULL);
      return;
    }

  // 9. fall back on the first legal action that doesn't lose outright
  for (int i = 0; i < n; ++i)
    if (!cs[i].allows_loss) {
      choose(&cs[i], RULE_ANY_MOVE, out, t, "%s is the only reasonable option left%s", NULL);
      return;
    }
}

// ======== random

void random_action(board *b, color curr, action *out)
{
  action acts[MAX_ACTIONS];
  int n = gen_actions(b, curr, acts);
  if (n == 0) {
    out->kind = ACTION_NONE;
    return;
  }
  *out = acts[random() % n];
}
