// Dependency-free tests for the Knightshift engine and bots.
// Run with `make test`.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "knightshift.h"
#include "bot.h"

static int failures = 0;
static int checks = 0;
static const char *current_test = "";

#define CHECK(cond)                                                       \
  do {                                                                    \
    ++checks;                                                             \
    if (!(cond)) {                                                        \
      ++failures;                                                         \
      fprintf(stderr, "  FAIL %s (%s:%d): %s\n", current_test, __FILE__, \
              __LINE__, #cond);                                           \
    }                                                                     \
  } while (0)

#define TEST(name) static void name(void)
#define RUN(name)          \
  do {                     \
    current_test = #name;  \
    name();                \
  } while (0)

// boards are written rank 8 first, files A-E left to right
static board *B(const char *r8, const char *r7, const char *r6, const char *r5,
                const char *r4, const char *r3, const char *r2, const char *r1)
{
  const char *ranks[] = {r8, r7, r6, r5, r4, r3, r2, r1};
  board *b = board_from_strings(ranks);
  if (!b) {
    fprintf(stderr, "bad test board\n");
    exit(2);
  }
  return b;
}

static move M(const char *src, const char *dst)
{
  return move_new(loc_new(src[0], src[1] - '0'), loc_new(dst[0], dst[1] - '0'));
}

static loc L(const char *s)
{
  return loc_new(s[0], s[1] - '0');
}

// ======== setup and geometry

TEST(setup_has_both_armies)
{
  board *b = board_setup();
  CHECK(count_pieces(b, WHITE, KNIGHT) == 4);
  CHECK(count_pieces(b, BLACK, KNIGHT) == 4);
  CHECK(piece_at(b, L("C1"))->type == ROYAL_GUARD);
  CHECK(piece_at(b, L("C8"))->color == BLACK);
  CHECK(!game_over(b));
  board_free(b);
}

TEST(board_from_strings_rejects_bad_input)
{
  const char *bad[] = {".....", ".....", ".....", ".....",
                       ".....", ".....", ".....", "..x.."};
  CHECK(board_from_strings(bad) == NULL);
}

TEST(knight_neighbors_stay_on_board)
{
  loc_list *corner = adjacent_knight(L("A1"));
  CHECK(llen(corner) == 2);
  lfree(corner);
  loc_list *center = adjacent_knight(L("C4"));
  CHECK(llen(center) == 8);
  lfree(center);
}

TEST(opening_move_count)
{
  board *b = board_setup();
  move_list *ms = available_moves(b, WHITE);
  // knights: A1 2, B1 3, D1 3, E1 2; guard: B2, C2, D2
  CHECK(mlen(ms) == 13);
  mfree(ms);
  board_free(b);
}

// ======== capture and protection rules

TEST(knight_captures_unprotected_knight)
{
  board *b = B(".....", ".....", ".....", ".....",
               ".....", "...K.", ".....", "..k..");
  int o = -1;
  piece *p = apply_move(b, M("C1", "D3"), WHITE, &o);
  CHECK(o == 0);
  CHECK(p && p->type == KNIGHT && p->color == BLACK);
  free(p);
  board_free(b);
}

TEST(protected_knight_cannot_be_captured)
{
  // black guard on E3 sits beside the black knight on D3
  board *b = B(".....", ".....", ".....", ".....",
               ".....", "...KG", ".....", "..k..");
  CHECK(protected_knight_at(b, L("D3")));
  CHECK(!legal_knight(b, M("C1", "D3"), WHITE));
  board_free(b);
}

TEST(diagonal_guard_does_not_protect)
{
  board *b = B(".....", ".....", ".....", ".....",
               "....G", "...K.", ".....", "..k..");
  CHECK(!protected_knight_at(b, L("D3")));
  CHECK(legal_knight(b, M("C1", "D3"), WHITE));
  board_free(b);
}

TEST(guards_and_fortresses_cannot_be_captured)
{
  board *b = B(".....", ".....", ".....", ".....",
               ".....", ".F.G.", ".....", "..k..");
  CHECK(!legal_knight(b, M("C1", "B3"), WHITE));
  CHECK(!legal_knight(b, M("C1", "D3"), WHITE));
  board_free(b);
}

TEST(guard_cannot_enter_enemy_guard_zone)
{
  board *b = B(".....", ".....", ".....", ".....",
               ".....", "..G..", ".....", "..g..");
  CHECK(!legal_guard(b, M("C1", "C2"), WHITE));   // orthogonally next to C3
  CHECK(legal_guard(b, M("C1", "B2"), WHITE));    // diagonal is fine
  board_free(b);
}

TEST(cannot_move_onto_friendly_piece)
{
  board *b = board_setup();
  int o = 0;
  apply_move(b, M("C1", "B1"), WHITE, &o);
  CHECK(o == ERR_FRIENDLY);
  board_free(b);
}

TEST(cannot_move_opponents_piece)
{
  board *b = board_setup();
  int o = 0;
  apply_move(b, M("A8", "B6"), WHITE, &o);
  CHECK(o == ERR_BAD_MOVE);
  board_free(b);
}

// ======== fortifying

TEST(fortify_on_enemy_back_rank)
{
  board *b = B("k....", ".....", ".....", ".....",
               ".....", ".....", ".....", "K....");
  loc_list *fs = available_fortifications(b, WHITE);
  CHECK(llen(fs) == 1 && loc_eq(fs->k, L("A8")));
  lfree(fs);
  int o = -1;
  fortify(b, L("A8"), WHITE, &o);
  CHECK(o == 0);
  CHECK(piece_at(b, L("A8"))->type == FORTRESS);
  board_free(b);
}

TEST(fortify_errors)
{
  board *b = B("..g..", ".....", ".....", ".....",
               ".....", ".....", "k....", ".....");
  int o = 0;
  fortify(b, L("A2"), WHITE, &o);
  CHECK(o == ERR_BAD_LOCATION);
  fortify(b, L("B8"), WHITE, &o);
  CHECK(o == ERR_NO_PIECE);
  fortify(b, L("C8"), WHITE, &o);
  CHECK(o == ERR_PIECE_TYPE);
  board_free(b);
}

// ======== victory

TEST(two_fortresses_win)
{
  board *b = B("ff...", ".....", ".....", ".....",
               ".....", ".....", ".....", "KK...");
  CHECK(game_over(b) == VICTORY_WHITE_TWO_FORTRESSES);
  CHECK(victory_winner(game_over(b)) == WHITE);
  board_free(b);
}

TEST(one_knight_left_loses)
{
  board *b = B("KK...", ".....", ".....", ".....",
               ".....", ".....", ".....", "k....");
  CHECK(game_over(b) == VICTORY_BLACK_THREE_CAPTURES);
  board_free(b);
}

TEST(empty_board_is_not_over)
{
  board *b = board_empty();
  CHECK(game_over(b) == 0);
  board_free(b);
}

// ======== evaluation

TEST(material_eval)
{
  board *b = board_setup();
  CHECK(eval_material(b) == 0);
  board_free(b);

  b = B("....F", "...K.", ".....", ".....",
        ".....", ".....", ".....", "k....");
  CHECK(eval_material(b) == 3 - 13);
  board_free(b);
}

TEST(positional_eval_rewards_protection)
{
  board *alone = B(".....", ".....", ".....", ".....",
                   "..k..", ".....", ".....", ".....");
  board *guarded = B(".....", ".....", ".....", ".....",
                     "..k..", "..g..", ".....", ".....");
  CHECK(eval_positional(guarded) > eval_positional(alone));
  CHECK(eval_positional(alone) == 3 + 8);   // knight + 8 moves from C4
  board_free(alone);
  board_free(guarded);
}

TEST(evals_are_symmetric_at_start)
{
  board *b = board_setup();
  CHECK(eval_positional(b) == 0);
  board_free(b);
}

// ======== minimax

// plain minimax without pruning, used to check alpha-beta gives the same value
static int reference_minimax(board *b, int depth, color curr)
{
  int outcome = game_over(b);
  if (outcome)
    return victory_winner(outcome) == WHITE ? WIN_SCORE : -WIN_SCORE;
  if (depth == 0)
    return eval_material(b);
  int best = curr == WHITE ? -2 * WIN_SCORE : 2 * WIN_SCORE;
  int any = 0;
  move_list *ms = available_moves(b, curr);
  for (move_list *it = ms; it; it = it->next) {
    board *c = do_move_new(b, it->m);
    int s = reference_minimax(c, depth - 1, opponent(curr));
    board_free(c);
    any = 1;
    if (curr == WHITE ? s > best : s < best)
      best = s;
  }
  mfree(ms);
  loc_list *fs = available_fortifications(b, curr);
  for (loc_list *it = fs; it; it = it->next) {
    board *c = do_fortify_new(b, it->k, curr);
    int s = reference_minimax(c, depth - 1, opponent(curr));
    board_free(c);
    any = 1;
    if (curr == WHITE ? s > best : s < best)
      best = s;
  }
  lfree(fs);
  return any ? best : eval_material(b);
}

// the reference ignores the "win sooner" adjustment, so compare signs of wins
static int same_value(int a, int b)
{
  if (abs(a) > WIN_SCORE - 100 || abs(b) > WIN_SCORE - 100)
    return (a > 0) == (b > 0) && abs(a) > WIN_SCORE - 100 && abs(b) > WIN_SCORE - 100;
  return a == b;
}

TEST(alpha_beta_matches_plain_minimax)
{
  board *positions[] = {
    board_setup(),
    B("K.G.K", ".....", "..K..", ".k...", ".....", "....k", ".....", "k.g.."),
    B(".k..K", "..G..", ".....", "...K.", "..k..", ".....", "K....", "..g.k"),
  };
  for (int i = 0; i < 3; ++i) {
    for (int depth = 1; depth <= 3; ++depth) {
      action a;
      int ab = minimax(eval_material, positions[i], depth, WHITE, &a, NULL);
      int ref = reference_minimax(positions[i], depth, WHITE);
      CHECK(same_value(ab, ref));
      CHECK(a.kind != ACTION_NONE);
    }
    board_free(positions[i]);
  }
}

TEST(minimax_depth_zero_returns_eval)
{
  board *b = board_setup();
  action a;
  CHECK(minimax(eval_material, b, 0, WHITE, &a, NULL) == 0);
  CHECK(a.kind == ACTION_NONE);
  board_free(b);
}

TEST(minimax_takes_winning_fortification)
{
  // white already has one fortress; fortifying B8 wins
  board *b = B("fk..K", ".....", ".....", ".....",
               "k...k", "..G..", ".....", "K.K.K");
  action a;
  search_stats st;
  int score = minimax(eval_material, b, 3, WHITE, &a, &st);
  CHECK(a.kind == ACTION_FORTIFY && loc_eq(a.fort, L("B8")));
  CHECK(score == WIN_SCORE - 1);
  CHECK(st.nodes > 0);
  board_free(b);
}

TEST(minimax_works_for_black)
{
  // black's knight on B5 can capture the unprotected knight on C3
  board *b = B("K.G.K", "....K", ".....", ".K...",
               ".....", "..k..", ".....", "k.g.k");
  action a;
  minimax(eval_material, b, 1, BLACK, &a, NULL);
  CHECK(a.kind == ACTION_MOVE && loc_eq(a.m.dst, L("C3")));
  board_free(b);
}

// ======== decision tree

TEST(tree_wins_immediately)
{
  board *b = B("fk..K", ".....", ".....", ".....",
               "k...k", "..G..", ".....", "K.K.K");
  action a;
  tree_trace t;
  decision_tree(b, WHITE, &a, &t);
  CHECK(t.rule == RULE_WIN_NOW);
  CHECK(a.kind == ACTION_FORTIFY && loc_eq(a.fort, L("B8")));
  board_free(b);
}

TEST(tree_blocks_opponent_win)
{
  // black has a fortress on A1 and a knight on E1 ready to fortify;
  // white's knight on C2 can capture it
  board *b = B("..G..", ".....", ".....", ".....",
               ".....", ".....", "..k..", "F...K");
  // give both sides enough knights that captures don't end the game
  put_piece(b, piece_new(WHITE, KNIGHT), L("A5"));
  put_piece(b, piece_new(WHITE, KNIGHT), L("E5"));
  put_piece(b, piece_new(BLACK, KNIGHT), L("A8"));
  put_piece(b, piece_new(BLACK, KNIGHT), L("E8"));
  action a;
  tree_trace t;
  decision_tree(b, WHITE, &a, &t);
  CHECK(t.rule == RULE_BLOCK_THREAT);
  CHECK(a.kind == ACTION_MOVE && loc_eq(a.m.dst, L("E1")));
  board_free(b);
}

TEST(tree_fortifies_when_possible)
{
  board *b = B("k...K", ".....", ".....", "..G..",
               ".....", ".....", "K....", "k.g.k");
  put_piece(b, piece_new(WHITE, KNIGHT), L("E3"));
  put_piece(b, piece_new(BLACK, KNIGHT), L("E6"));
  action a;
  tree_trace t;
  decision_tree(b, WHITE, &a, &t);
  CHECK(t.rule == RULE_FORTIFY);
  CHECK(a.kind == ACTION_FORTIFY && loc_eq(a.fort, L("A8")));
  board_free(b);
}

TEST(tree_captures_safely)
{
  // the guard (or the B1 knight) can take the loose knight on D2
  board *b = B("K.G.K", ".....", "K....", ".....",
               ".....", ".....", "...K.", "kkg.k");
  action a;
  tree_trace t;
  decision_tree(b, WHITE, &a, &t);
  CHECK(t.rule == RULE_SAFE_CAPTURE);
  CHECK(a.kind == ACTION_MOVE && occupied(b, a.m.dst));
  board_free(b);
}

TEST(tree_opening_advances)
{
  board *b = board_setup();
  action a;
  tree_trace t;
  decision_tree(b, WHITE, &a, &t);
  CHECK(t.rule == RULE_ADVANCE);
  CHECK(a.kind == ACTION_MOVE && a.m.dst.rank == 3);
  CHECK(strlen(t.reason) > 0);
  board_free(b);
}

TEST(tree_always_returns_legal_action)
{
  // play tree against tree for a while; every action must be legal
  board *b = board_setup();
  color turn = WHITE;
  for (int ply = 0; ply < 120 && !game_over(b); ++ply) {
    action a;
    tree_trace t;
    decision_tree(b, turn, &a, &t);
    CHECK(a.kind != ACTION_NONE);
    if (a.kind == ACTION_NONE)
      break;
    CHECK(action_apply(b, a, turn, NULL) == 0);
    turn = opponent(turn);
  }
  board_free(b);
}

TEST(random_action_is_legal)
{
  board *b = board_setup();
  for (int i = 0; i < 20; ++i) {
    action a;
    random_action(b, WHITE, &a);
    board *c = board_dup(b);
    CHECK(action_apply(c, a, WHITE, NULL) == 0);
    board_free(c);
  }
  board_free(b);
}

int main(void)
{
  RUN(setup_has_both_armies);
  RUN(board_from_strings_rejects_bad_input);
  RUN(knight_neighbors_stay_on_board);
  RUN(opening_move_count);
  RUN(knight_captures_unprotected_knight);
  RUN(protected_knight_cannot_be_captured);
  RUN(diagonal_guard_does_not_protect);
  RUN(guards_and_fortresses_cannot_be_captured);
  RUN(guard_cannot_enter_enemy_guard_zone);
  RUN(cannot_move_onto_friendly_piece);
  RUN(cannot_move_opponents_piece);
  RUN(fortify_on_enemy_back_rank);
  RUN(fortify_errors);
  RUN(two_fortresses_win);
  RUN(one_knight_left_loses);
  RUN(empty_board_is_not_over);
  RUN(material_eval);
  RUN(positional_eval_rewards_protection);
  RUN(evals_are_symmetric_at_start);
  RUN(alpha_beta_matches_plain_minimax);
  RUN(minimax_depth_zero_returns_eval);
  RUN(minimax_takes_winning_fortification);
  RUN(minimax_works_for_black);
  RUN(tree_wins_immediately);
  RUN(tree_blocks_opponent_win);
  RUN(tree_fortifies_when_possible);
  RUN(tree_captures_safely);
  RUN(tree_opening_advances);
  RUN(tree_always_returns_legal_action);
  RUN(random_action_is_legal);

  if (failures) {
    printf("%d of %d checks failed\n", failures, checks);
    return 1;
  }
  printf("all %d checks passed\n", checks);
  return 0;
}
