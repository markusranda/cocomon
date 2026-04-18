#pragma once

#include "game.h"

namespace battle {

enum class ActionType {
    None,
    UseMove,
    OpenCocomonMenu,
    ThrowCocoball,
    RunAway,
};

// The battle system consumes one typed action from the UI at a time.
// Add new player-facing battle verbs here so the game loop stays simple.
struct Action {
    ActionType type = ActionType::None;
    int move_slot = -1;
};

enum class FinishReason {
    None,
    PlayerWon,
    OpponentWon,
    PlayerRanAway,
};

enum class Effectiveness {
    Normal,
    SuperEffective,
    NotVeryEffective,
};

struct MoveEvent {
    bool happened = false;
    Cocomon attacker = Cocomon::Nil;
    Cocomon defender = Cocomon::Nil;
    int move_slot = -1;
    int damage = 0;
    int defender_health_after = 0;
    Effectiveness effectiveness = Effectiveness::Normal;
    bool defender_fainted = false;
};

// One player input can create up to two move events because either side may act first.
// Keeping them in order makes later battle text and animation playback straightforward.
struct TurnResult {
    Action action = {};
    bool action_resolved = false;
    MoveEvent first_move = {};
    MoveEvent second_move = {};
    FinishReason finish_reason = FinishReason::None;
};

void start();
Action action_from_ui(BattleUIIndex selected_index);
TurnResult resolve_player_action(Action action);

} // namespace battle
