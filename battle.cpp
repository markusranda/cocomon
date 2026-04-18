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

bool can_use_move(Cocomon attacker_idx, int move_slot) {
    if (!is_valid_move_slot(move_slot)) return false;

    const CocomonDef& attacker = cocomons[(size_t)attacker_idx];
    const CocomonMoveDef& move = attacker.moves[move_slot];
    return move.flags > 0 && move.pp > 0;
}

battle::Action move_action(int move_slot) {
    return battle::Action{ .type = battle::ActionType::UseMove, .move_slot = move_slot };
}

battle::MoveEvent apply_move(Cocomon attacker_idx, Cocomon defender_idx, int move_slot) {
    if (!can_use_move(attacker_idx, move_slot)) return {};

    CocomonDef& attacker = cocomons[(size_t)attacker_idx];
    CocomonDef& defender = cocomons[(size_t)defender_idx];
    CocomonMoveDef& move = attacker.moves[move_slot];
    battle::MoveEvent result;
    float effectiveness = move_effectiveness_multiplier(move.element, defender.element);

    int total_damage = (int)move.dmg + attacker.attack - (defender.defense / 2);
    float damage_variance = 0.95f + ((float)rand() / (float)RAND_MAX) * 0.10f;
    total_damage = (int)(total_damage * effectiveness * damage_variance);
    if (total_damage < 1) total_damage = 1;

    move.pp -= 1;
    defender.health -= total_damage;

    if (defender.health < 0) {
        defender.health = 0;
    }

    result.happened = true;
    result.attacker = attacker_idx;
    result.defender = defender_idx;
    result.move_slot = move_slot;
    result.damage = total_damage;
    result.defender_health_after = defender.health;
    result.effectiveness = effectiveness_from_multiplier(effectiveness);
    result.defender_fainted = defender.health == 0;
    return result;
}

int first_usable_move_slot(Cocomon cocomon_idx) {
    const CocomonDef& cocomon = cocomons[(size_t)cocomon_idx];

    for (int move_idx = 0; move_idx < max_cocomon_moves; move_idx++) {
        const CocomonMoveDef& move = cocomon.moves[move_idx];
        if (move.flags > 0 && move.pp > 0) return move_idx;
    }

    return -1;
}

battle::FinishReason finish_reason_for_defender(Cocomon defender_idx) {
    if (defender_idx == opponent_cocomon_idx) return battle::FinishReason::PlayerWon;
    if (defender_idx == player_cocomon_idx) return battle::FinishReason::OpponentWon;
    return battle::FinishReason::None;
}

battle::TurnResult resolve_player_move(battle::Action action) {
    battle::TurnResult result;
    result.action = action;

    if (!can_use_move(player_cocomon_idx, action.move_slot)) return result;

    const CocomonDef& player = cocomons[(size_t)player_cocomon_idx];
    const CocomonDef& opponent = cocomons[(size_t)opponent_cocomon_idx];
    int enemy_move_slot = first_usable_move_slot(opponent_cocomon_idx);
    result.action_resolved = true;

    if (player.speed >= opponent.speed) {
        result.first_move = apply_move(player_cocomon_idx, opponent_cocomon_idx, action.move_slot);
        if (result.first_move.defender_fainted) {
            result.finish_reason = finish_reason_for_defender(result.first_move.defender);
            return result;
        }

        if (enemy_move_slot >= 0) {
            result.second_move = apply_move(opponent_cocomon_idx, player_cocomon_idx, enemy_move_slot);
            if (result.second_move.defender_fainted) {
                result.finish_reason = finish_reason_for_defender(result.second_move.defender);
            }
        }
        return result;
    }

    if (enemy_move_slot >= 0) {
        result.first_move = apply_move(opponent_cocomon_idx, player_cocomon_idx, enemy_move_slot);
        if (result.first_move.defender_fainted) {
            result.finish_reason = finish_reason_for_defender(result.first_move.defender);
            return result;
        }
    }

    result.second_move = apply_move(player_cocomon_idx, opponent_cocomon_idx, action.move_slot);
    if (result.second_move.defender_fainted) {
        result.finish_reason = finish_reason_for_defender(result.second_move.defender);
    }

    return result;
}

} // namespace

namespace battle {

void start() {
    game_state_next = GameState::Battle;
    ui_cursor = 0;
    cocomons[(size_t)player_cocomon_idx] = cocomon_defaults[(size_t)player_cocomon_idx];
    cocomons[(size_t)opponent_cocomon_idx] = cocomon_defaults[(size_t)opponent_cocomon_idx];

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
        case ActionType::OpenCocomonMenu:
        case ActionType::ThrowCocoball:
        case ActionType::None: {
            return result;
        }
    }

    return result;
}

} // namespace battle
