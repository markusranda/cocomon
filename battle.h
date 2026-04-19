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
    PlayerCapturedOpponent,
};

enum class CaptureOutcome {
    None,
    BlockedNotWild,
    BlockedPartyFull,
    Failed,
    Caught,
};

enum class Effectiveness {
    Normal,
    SuperEffective,
    NotVeryEffective,
};

struct MoveEvent {
    bool happened = false;
    Cocomon attacker = Cocomon::Nil;
    bool attacker_is_player = false;
    Cocomon defender = Cocomon::Nil;
    bool defender_is_player = false;
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
    bool player_switched = false;
    bool opponent_switched = false;
    bool party_switch_required = false;
    CaptureOutcome capture_outcome = CaptureOutcome::None;
    Cocomon captured_species = Cocomon::Nil;
    int captured_party_slot = -1;
    int opponent_switch_slot = -1;
    Cocomon switched_from = Cocomon::Nil;
    Cocomon switched_to = Cocomon::Nil;
    Cocomon opponent_switched_from = Cocomon::Nil;
    Cocomon opponent_switched_to = Cocomon::Nil;
    MoveEvent first_move = {};
    MoveEvent second_move = {};
    FinishReason finish_reason = FinishReason::None;
};

void start();
Action action_from_ui(BattleUIIndex selected_index);
TurnResult resolve_player_action(Action action);
TurnResult resolve_player_switch(int party_slot, bool forced_switch);

} // namespace battle
