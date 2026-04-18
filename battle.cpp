#include "battle.h"
#include <cstdlib>

namespace {

float move_effectiveness_multiplier(CocomonElement attack_element, CocomonElement defender_element) {
    if (attack_element == CocomonElement::Nil || defender_element == CocomonElement::Nil) return 1.0f;

    if ((attack_element == CocomonElement::Grass && defender_element == CocomonElement::Water) ||
        (attack_element == CocomonElement::Water && defender_element == CocomonElement::Fire) ||
        (attack_element == CocomonElement::Fire && defender_element == CocomonElement::Grass)) {
        return 1.5f;
    }

    if ((attack_element == CocomonElement::Grass && defender_element == CocomonElement::Fire) ||
        (attack_element == CocomonElement::Water && defender_element == CocomonElement::Grass) ||
        (attack_element == CocomonElement::Fire && defender_element == CocomonElement::Water)) {
        return 0.65f;
    }

    return 1.0f;
}

battle::Effectiveness effectiveness_from_multiplier(float multiplier) {
    if (multiplier > 1.0f) return battle::Effectiveness::SuperEffective;
    if (multiplier < 1.0f) return battle::Effectiveness::NotVeryEffective;
    return battle::Effectiveness::Normal;
}

bool is_valid_move_slot(int move_slot) {
    return move_slot >= 0 && move_slot < max_cocomon_moves;
}

CocomonInstance& active_player_instance() {
    return player_party[player_active_party_slot];
}

CocomonInstance& active_opponent_instance() {
    return battle_opponent_cocomon;
}

bool player_party_slot_is_usable_for_switch(int slot, bool allow_current_slot) {
    if (slot < 0 || slot >= player_party_count) return false;
    if (player_party[slot].species == Cocomon::Nil) return false;
    if (player_party[slot].battler.health <= 0) return false;
    if (!allow_current_slot && slot == player_active_party_slot) return false;
    return true;
}

bool player_has_usable_reserve() {
    for (int slot = 0; slot < player_party_count; slot++) {
        if (player_party_slot_is_usable_for_switch(slot, false)) return true;
    }

    return false;
}

bool player_has_open_party_slot() {
    return player_party_count < max_player_party;
}

int append_player_party_member(CocomonInstance instance) {
    if (!player_has_open_party_slot()) return -1;

    player_party[player_party_count] = instance;
    player_party_count += 1;
    return player_party_count - 1;
}

void set_active_player_slot(int slot) {
    player_active_party_slot = slot;
    player_cocomon_idx = player_party[slot].species;
}

bool can_use_move(const CocomonInstance& attacker, int move_slot) {
    if (!is_valid_move_slot(move_slot)) return false;

    const CocomonMoveDef& move = attacker.battler.moves[move_slot];
    return move.flags > 0 && move.pp > 0;
}

battle::Action move_action(int move_slot) {
    return battle::Action{ .type = battle::ActionType::UseMove, .move_slot = move_slot };
}

battle::MoveEvent apply_move(CocomonInstance& attacker, bool attacker_is_player, CocomonInstance& defender, bool defender_is_player, int move_slot) {
    if (!can_use_move(attacker, move_slot)) return {};

    CocomonMoveDef& move = attacker.battler.moves[move_slot];
    battle::MoveEvent result;
    float effectiveness = move_effectiveness_multiplier(move.element, defender.battler.element);

    int total_damage = (int)move.dmg + attacker.battler.attack - (defender.battler.defense / 2);
    float damage_variance = 0.95f + ((float)rand() / (float)RAND_MAX) * 0.10f;
    total_damage = (int)(total_damage * effectiveness * damage_variance);
    if (total_damage < 1) total_damage = 1;

    move.pp -= 1;
    defender.battler.health -= total_damage;

    if (defender.battler.health < 0) {
        defender.battler.health = 0;
    }

    result.happened = true;
    result.attacker = attacker.species;
    result.attacker_is_player = attacker_is_player;
    result.defender = defender.species;
    result.defender_is_player = defender_is_player;
    result.move_slot = move_slot;
    result.damage = total_damage;
    result.defender_health_after = defender.battler.health;
    result.effectiveness = effectiveness_from_multiplier(effectiveness);
    result.defender_fainted = defender.battler.health == 0;
    return result;
}

int first_usable_move_slot(const CocomonInstance& cocomon) {
    for (int move_idx = 0; move_idx < max_cocomon_moves; move_idx++) {
        const CocomonMoveDef& move = cocomon.battler.moves[move_idx];
        if (move.flags > 0 && move.pp > 0) return move_idx;
    }

    return -1;
}

float cocoball_capture_chance(const CocomonInstance& player, const CocomonInstance& opponent) {
    float health_ratio = (float)opponent.battler.health / (float)opponent.battler.max_health;
    float missing_health_ratio = 1.0f - health_ratio;
    float chance = 0.20f + missing_health_ratio * 0.55f;

    if (opponent.battler.health * 4 <= opponent.battler.max_health) {
        chance += 0.12f;
    }

    if (player.level >= opponent.level) {
        chance += 0.08f;
    }

    if (chance < 0.20f) chance = 0.20f;
    if (chance > 0.92f) chance = 0.92f;
    return chance;
}

battle::TurnResult resolve_player_move(battle::Action action) {
    battle::TurnResult result;
    result.action = action;

    CocomonInstance& player = active_player_instance();
    CocomonInstance& opponent = active_opponent_instance();

    if (!can_use_move(player, action.move_slot)) return result;

    int enemy_move_slot = first_usable_move_slot(opponent);
    result.action_resolved = true;

    if (player.battler.speed >= opponent.battler.speed) {
        result.first_move = apply_move(player, true, opponent, false, action.move_slot);
        if (result.first_move.defender_fainted) {
            result.finish_reason = battle::FinishReason::PlayerWon;
            return result;
        }

        if (enemy_move_slot >= 0) {
            result.second_move = apply_move(opponent, false, player, true, enemy_move_slot);
            if (result.second_move.defender_fainted) {
                if (player_has_usable_reserve()) {
                    result.party_switch_required = true;
                } else {
                    result.finish_reason = battle::FinishReason::OpponentWon;
                }
            }
        }
        return result;
    }

    if (enemy_move_slot >= 0) {
        result.first_move = apply_move(opponent, false, player, true, enemy_move_slot);
        if (result.first_move.defender_fainted) {
            if (player_has_usable_reserve()) {
                result.party_switch_required = true;
            } else {
                result.finish_reason = battle::FinishReason::OpponentWon;
            }
            return result;
        }
    }

    result.second_move = apply_move(player, true, opponent, false, action.move_slot);
    if (result.second_move.defender_fainted) {
        result.finish_reason = battle::FinishReason::PlayerWon;
    }

    return result;
}

battle::TurnResult resolve_throw_cocoball(battle::Action action) {
    battle::TurnResult result;
    result.action = action;

    CocomonInstance& player = active_player_instance();
    CocomonInstance& opponent = active_opponent_instance();
    result.captured_species = opponent.species;

    if (!battle_is_wild_encounter) {
        result.capture_outcome = battle::CaptureOutcome::BlockedNotWild;
        return result;
    }

    if (!player_has_open_party_slot()) {
        result.capture_outcome = battle::CaptureOutcome::BlockedPartyFull;
        return result;
    }

    result.action_resolved = true;

    float capture_chance = cocoball_capture_chance(player, opponent);
    float roll = (float)rand() / ((float)RAND_MAX + 1.0f);
    if (roll <= capture_chance) {
        CocomonInstance captured = opponent;
        captured.xp = 0;
        captured.battler.health = captured.battler.max_health;
        for (int move_slot = 0; move_slot < max_cocomon_moves; move_slot++) {
            if (captured.battler.moves[move_slot].flags == 0) continue;
            captured.battler.moves[move_slot].pp = captured.battler.moves[move_slot].pp_max;
        }

        int party_slot = append_player_party_member(captured);
        if (party_slot < 0) {
            result.action_resolved = false;
            result.capture_outcome = battle::CaptureOutcome::BlockedPartyFull;
            return result;
        }

        result.capture_outcome = battle::CaptureOutcome::Caught;
        result.captured_party_slot = party_slot;
        result.finish_reason = battle::FinishReason::PlayerCapturedOpponent;
        return result;
    }

    result.capture_outcome = battle::CaptureOutcome::Failed;

    int enemy_move_slot = first_usable_move_slot(opponent);
    if (enemy_move_slot < 0) return result;

    result.first_move = apply_move(opponent, false, player, true, enemy_move_slot);
    if (result.first_move.defender_fainted) {
        if (player_has_usable_reserve()) {
            result.party_switch_required = true;
        } else {
            result.finish_reason = battle::FinishReason::OpponentWon;
        }
    }

    return result;
}

} // namespace

namespace battle {

void start() {
    game_state_next = GameState::Battle;
    ui_cursor = 0;
    player_cocomon_idx = active_player_instance().species;
    opponent_cocomon_idx = active_opponent_instance().species;

    play_music("songs/battle_anthem.mp3");
}

Action action_from_ui(BattleUIIndex selected_index) {
    switch (selected_index) {
        case BattleUIIndex::AbilityOne: return move_action(0);
        case BattleUIIndex::AbilityTwo: return move_action(1);
        case BattleUIIndex::AbilityThree: return move_action(2);
        case BattleUIIndex::AbilityFour: return move_action(3);
        case BattleUIIndex::Cocomon: return Action{ .type = ActionType::OpenCocomonMenu };
        case BattleUIIndex::Cocoball: return Action{ .type = ActionType::ThrowCocoball };
        case BattleUIIndex::Run: return Action{ .type = ActionType::RunAway };
        case BattleUIIndex::Nil: return Action{};
        default: return Action{};
    }
}

TurnResult resolve_player_action(Action action) {
    TurnResult result;
    result.action = action;

    switch (action.type) {
        case ActionType::UseMove: {
            return resolve_player_move(action);
        }
        case ActionType::RunAway: {
            result.action_resolved = true;
            result.finish_reason = FinishReason::PlayerRanAway;
            return result;
        }
        case ActionType::OpenCocomonMenu: {
            result.action_resolved = player_has_usable_reserve();
            return result;
        }
        case ActionType::ThrowCocoball: {
            return resolve_throw_cocoball(action);
        }
        case ActionType::None: {
            return result;
        }
    }

    return result;
}

TurnResult resolve_player_switch(int party_slot, bool forced_switch) {
    TurnResult result;

    if (!player_party_slot_is_usable_for_switch(party_slot, false)) return result;

    Cocomon switched_from = player_cocomon_idx;
    set_active_player_slot(party_slot);

    result.action_resolved = true;
    result.player_switched = true;
    result.party_switch_required = false;
    result.switched_from = switched_from;
    result.switched_to = player_cocomon_idx;

    if (forced_switch) {
        return result;
    }

    CocomonInstance& opponent = active_opponent_instance();
    CocomonInstance& player = active_player_instance();
    int enemy_move_slot = first_usable_move_slot(opponent);
    if (enemy_move_slot < 0) return result;

    result.first_move = apply_move(opponent, false, player, true, enemy_move_slot);
    if (result.first_move.defender_fainted) {
        if (player_has_usable_reserve()) {
            result.party_switch_required = true;
        } else {
            result.finish_reason = FinishReason::OpponentWon;
        }
    }

    return result;
}

} // namespace battle
