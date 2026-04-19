#pragma once

#include "battle.h"

namespace battle_scene {

struct DrawAssets {
    Texture2D background = {};
    const Texture2D* trainer_sprites = nullptr;
};

struct Callbacks {
    void (*open_cocomon_list)(GameState return_state, bool forced_selection) = nullptr;
    void (*finish_battle)(battle::FinishReason reason, bool heal_player_party) = nullptr;
};

void clear_playback();
void reset_health_display();
void start_intro_transition();
void queue_intro_playback(TrainerId opponent_trainer_id);
void queue_turn_result_playback(const battle::TurnResult& result, TrainerId opponent_trainer_id);
void begin_queued_playback(const Callbacks& callbacks);
void update(float delta, const Callbacks& callbacks);
bool is_busy();
void draw(const DrawAssets& assets, TrainerId opponent_trainer_id);

} // namespace battle_scene
