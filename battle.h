#pragma once

#include "game.h"

void state_transition_battle();
int battle_move_slot_from_cursor(BattleUIIndex index);
bool battle_use_move(Cocomon attacker_idx, Cocomon defender_idx, int move_slot);
int battle_first_usable_move_slot(Cocomon cocomon_idx);
void battle_player_attack(int move_slot);
