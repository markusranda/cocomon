#include "battle.h"
#include <cstdlib>

void state_transition_battle() {
    game_state_next = GameState::Battle;
    ui_cursor = 0;
    cocomons[(size_t)player_cocomon_idx] = cocomon_defaults[(size_t)player_cocomon_idx];
    cocomons[(size_t)opponent_cocomon_idx] = cocomon_defaults[(size_t)opponent_cocomon_idx];

    play_music("songs/battle_anthem.mp3");
}

int battle_move_slot_from_cursor(BattleUIIndex index) {
    switch (index) {
        case BattleUIIndex::AbilityOne: return 0;
        case BattleUIIndex::AbilityTwo: return 1;
        case BattleUIIndex::AbilityThree: return 2;
        case BattleUIIndex::AbilityFour: return 3;
        default: return -1;
    }
}

bool battle_use_move(Cocomon attacker_idx, Cocomon defender_idx, int move_slot) {
    if (move_slot < 0 || move_slot >= 4) return false;

    CocomonDef& attacker = cocomons[(size_t)attacker_idx];
    CocomonDef& defender = cocomons[(size_t)defender_idx];
    CocomonMoveDef& move = attacker.moves[move_slot];

    if (move.flags == 0 || move.pp == 0) return false;

    int total_damage = (int)move.dmg + attacker.attack - (defender.defense / 2);
    float damage_variance = 0.95f + ((float)rand() / (float)RAND_MAX) * 0.10f;
    total_damage = (int)(total_damage * damage_variance);
    if (total_damage < 1) total_damage = 1;

    move.pp -= 1;
    defender.health -= total_damage;

    if (defender.health < 0) {
        defender.health = 0;
    }

    return true;
}

int battle_first_usable_move_slot(Cocomon cocomon_idx) {
    CocomonDef& cocomon = cocomons[(size_t)cocomon_idx];

    for (int move_idx = 0; move_idx < 4; move_idx++) {
        CocomonMoveDef& move = cocomon.moves[move_idx];
        if (move.flags > 0 && move.pp > 0) return move_idx;
    }

    return -1;
}

static void battle_take_turn(Cocomon attacker_idx, Cocomon defender_idx, int move_slot) {
    if (!battle_use_move(attacker_idx, defender_idx, move_slot)) return;

    if (cocomons[(size_t)defender_idx].health == 0) {
        state_transition_overworld();
    }
}

void battle_player_attack(int move_slot) {
    CocomonDef& player = cocomons[(size_t)player_cocomon_idx];
    CocomonDef& opponent = cocomons[(size_t)opponent_cocomon_idx];
    int enemy_move_slot = battle_first_usable_move_slot(opponent_cocomon_idx);

    if (player.speed >= opponent.speed) {
        battle_take_turn(player_cocomon_idx, opponent_cocomon_idx, move_slot);
        if (game_state_next != GameState::Battle) return;

        if (enemy_move_slot >= 0) {
            battle_take_turn(opponent_cocomon_idx, player_cocomon_idx, enemy_move_slot);
        }
        return;
    }

    if (enemy_move_slot >= 0) {
        battle_take_turn(opponent_cocomon_idx, player_cocomon_idx, enemy_move_slot);
        if (game_state_next != GameState::Battle) return;
    }

    battle_take_turn(player_cocomon_idx, opponent_cocomon_idx, move_slot);
}
