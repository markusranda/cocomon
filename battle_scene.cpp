#include "battle_scene.h"
#include "trainers.h"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

constexpr int battle_playback_capacity = 24;
constexpr int battle_caption_max_chars = 96;
constexpr float intro_transition_duration = 0.58f;
constexpr float trainer_sprite_width = 32.0f;
constexpr float trainer_sprite_height = 64.0f;

enum class BattleVisualSide {
    None,
    Player,
    Opponent,
};

struct BattleBeat {
    char caption[battle_caption_max_chars];
    float duration;
    Cocomon attacker;
    bool attacker_is_player;
    Cocomon defender;
    bool defender_is_player;
    int damage;
    int defender_health_after;
    bool trigger_attack;
    bool trigger_opponent_switch;
    int opponent_switch_slot;
};

BattleBeat battle_beats[battle_playback_capacity] = {};
int battle_beat_count = 0;
int battle_beat_index = 0;
float battle_beat_timer = 0.0f;
char battle_active_caption[battle_caption_max_chars] = {};
float battle_active_caption_timer = 0.0f;
float battle_active_caption_duration = 0.0f;
battle::FinishReason battle_pending_finish_reason = battle::FinishReason::None;
float battle_player_health_display = 0.0f;
float battle_player_health_target = 0.0f;
float battle_opponent_health_display = 0.0f;
float battle_opponent_health_target = 0.0f;
BattleVisualSide battle_animating_attack_side = BattleVisualSide::None;
float battle_attack_anim_timer = 0.0f;
float battle_attack_anim_duration = 0.0f;
BattleVisualSide battle_damage_popup_side = BattleVisualSide::None;
int battle_damage_popup_amount = 0;
float battle_damage_popup_timer = 0.0f;
float battle_damage_popup_duration = 0.0f;
bool battle_pending_open_party_menu = false;
bool battle_pending_forced_party_menu = false;
bool battle_pending_party_heal_on_finish = false;
float battle_intro_transition_timer = 0.0f;
float battle_intro_transition_duration = 0.0f;

bool has_opponent_trainer(TrainerId opponent_trainer_id) {
    return !battle_is_wild_encounter && opponent_trainer_id != TrainerId::Nil;
}

CocomonInstance& active_player_cocomon() {
    return player_party[player_active_party_slot];
}

CocomonInstance& active_opponent_cocomon() {
    return battle_opponent_party[battle_opponent_active_party_slot];
}

float move_towards_float(float current, float target, float max_delta) {
    if (current < target) {
        current += max_delta;
        if (current > target) current = target;
        return current;
    }

    if (current > target) {
        current -= max_delta;
        if (current < target) current = target;
    }

    return current;
}

void push_beat(const char* caption, float duration, Cocomon attacker = Cocomon::Nil, bool attacker_is_player = false, Cocomon defender = Cocomon::Nil, bool defender_is_player = false, int damage = 0, int defender_health_after = -1, bool trigger_attack = false, bool trigger_opponent_switch = false, int opponent_switch_slot = -1) {
    if (battle_beat_count >= battle_playback_capacity) return;

    BattleBeat& beat = battle_beats[battle_beat_count];
    beat = {};
    std::strncpy(beat.caption, caption, battle_caption_max_chars - 1);
    beat.caption[battle_caption_max_chars - 1] = '\0';
    beat.duration = duration;
    beat.attacker = attacker;
    beat.attacker_is_player = attacker_is_player;
    beat.defender = defender;
    beat.defender_is_player = defender_is_player;
    beat.damage = damage;
    beat.defender_health_after = defender_health_after;
    beat.trigger_attack = trigger_attack;
    beat.trigger_opponent_switch = trigger_opponent_switch;
    beat.opponent_switch_slot = opponent_switch_slot;
    battle_beat_count += 1;
}

void push_move_beat(const battle::MoveEvent& move_event) {
    if (!move_event.happened) return;

    const CocomonDef& attacker = cocomons[(size_t)move_event.attacker];
    const CocomonDef& defender = cocomons[(size_t)move_event.defender];
    const CocomonMoveDef& move = cocomons[(size_t)move_event.attacker].moves[move_event.move_slot];
    char caption[battle_caption_max_chars];

    std::snprintf(caption, sizeof(caption), "%s used %s!", attacker.name, move.name);
    push_beat(caption, 0.75f, move_event.attacker, move_event.attacker_is_player, move_event.defender, move_event.defender_is_player, move_event.damage, move_event.defender_health_after, true);

    switch (move_event.effectiveness) {
        case battle::Effectiveness::SuperEffective: {
            push_beat("It's super effective!", 0.55f);
            break;
        }
        case battle::Effectiveness::NotVeryEffective: {
            push_beat("It's not very effective.", 0.55f);
            break;
        }
        case battle::Effectiveness::Normal: {
            break;
        }
    }

    if (move_event.defender_fainted) {
        std::snprintf(caption, sizeof(caption), "%s fainted!", defender.name);
        push_beat(caption, 0.75f);
    }
}

void push_switch_beat(const battle::TurnResult& result) {
    if (!result.player_switched) return;

    char caption[battle_caption_max_chars];
    const CocomonDef& switched_to = cocomons[(size_t)result.switched_to];
    std::snprintf(caption, sizeof(caption), "Go, %s!", switched_to.name);
    push_beat(caption, 0.7f);
}

void push_opponent_switch_beat(const battle::TurnResult& result, TrainerId opponent_trainer_id) {
    if (!result.opponent_switched) return;

    char caption[battle_caption_max_chars];
    const CocomonDef& switched_to = cocomons[(size_t)result.opponent_switched_to];

    if (has_opponent_trainer(opponent_trainer_id)) {
        std::snprintf(caption, sizeof(caption), "%s sent out %s!", trainer_name(opponent_trainer_id), switched_to.name);
    } else {
        std::snprintf(caption, sizeof(caption), "%s stepped in!", switched_to.name);
    }

    int switched_health = 0;
    if (result.opponent_switch_slot >= 0 && result.opponent_switch_slot < battle_opponent_party_count) {
        switched_health = battle_opponent_party[result.opponent_switch_slot].battler.health;
    }

    push_beat(
        caption,
        0.8f,
        Cocomon::Nil,
        false,
        result.opponent_switched_to,
        false,
        0,
        switched_health,
        false,
        true,
        result.opponent_switch_slot
    );
}

void push_cocoball_beats(const battle::TurnResult& result) {
    if (result.action.type != battle::ActionType::ThrowCocoball) return;

    char caption[battle_caption_max_chars];
    const char* captured_name = cocomons[(size_t)result.captured_species].name;

    switch (result.capture_outcome) {
        case battle::CaptureOutcome::BlockedNotWild: {
            push_beat("You can only catch wild Cocomon.", 0.85f);
            break;
        }
        case battle::CaptureOutcome::BlockedPartyFull: {
            push_beat("Your party is full.", 0.75f);
            break;
        }
        case battle::CaptureOutcome::Failed: {
            push_beat("You threw a Cocoball!", 0.55f);
            std::snprintf(caption, sizeof(caption), "%s broke free!", captured_name);
            push_beat(caption, 0.8f);
            break;
        }
        case battle::CaptureOutcome::Caught: {
            push_beat("You threw a Cocoball!", 0.55f);
            std::snprintf(caption, sizeof(caption), "Gotcha! %s was caught!", captured_name);
            push_beat(caption, 0.9f);
            std::snprintf(caption, sizeof(caption), "%s joined your party!", captured_name);
            push_beat(caption, 0.85f);
            break;
        }
        case battle::CaptureOutcome::None: {
            break;
        }
    }
}

void award_victory_experience() {
    CocomonInstance& player = active_player_cocomon();
    const CocomonInstance& opponent = active_opponent_cocomon();
    char caption[battle_caption_max_chars];
    int experience_gained = 25 + opponent.level * 15;

    std::snprintf(caption, sizeof(caption), "%s gained %d XP!", player.battler.name, experience_gained);
    push_beat(caption, 0.9f);

    player.xp += experience_gained;
    while (player.xp >= experience_to_next_level(player.level)) {
        player.xp -= experience_to_next_level(player.level);
        player.level += 1;
        refresh_cocomon_instance_stats(player);
        player_cocomon_idx = player.species;
        battle_player_health_target = (float)player.battler.health;
        battle_player_health_display = battle_player_health_target;

        std::snprintf(caption, sizeof(caption), "%s reached LV %d!", player.battler.name, player.level);
        push_beat(caption, 0.9f);
    }
}

void resolve_playback_completion(const battle_scene::Callbacks& callbacks) {
    if (battle_pending_open_party_menu) {
        bool forced_selection = battle_pending_forced_party_menu;
        battle_pending_open_party_menu = false;
        battle_pending_forced_party_menu = false;
        if (callbacks.open_cocomon_list) {
            callbacks.open_cocomon_list(GameState::Battle, forced_selection);
        }
        return;
    }

    if (battle_pending_finish_reason != battle::FinishReason::None) {
        battle::FinishReason finish_reason = battle_pending_finish_reason;
        bool heal_player_party = battle_pending_party_heal_on_finish;
        battle_pending_finish_reason = battle::FinishReason::None;
        battle_pending_party_heal_on_finish = false;
        if (callbacks.finish_battle) {
            callbacks.finish_battle(finish_reason, heal_player_party);
        }
    }
}

void start_next_beat(const battle_scene::Callbacks& callbacks) {
    if (battle_beat_index >= battle_beat_count) {
        resolve_playback_completion(callbacks);
        return;
    }

    const BattleBeat& beat = battle_beats[battle_beat_index];
    battle_beat_index += 1;
    battle_beat_timer = beat.duration;

    std::strncpy(battle_active_caption, beat.caption, battle_caption_max_chars - 1);
    battle_active_caption[battle_caption_max_chars - 1] = '\0';
    battle_active_caption_timer = beat.duration;
    battle_active_caption_duration = beat.duration;

    if (beat.trigger_opponent_switch &&
        beat.opponent_switch_slot >= 0 &&
        beat.opponent_switch_slot < battle_opponent_party_count) {
        battle_opponent_active_party_slot = beat.opponent_switch_slot;
        opponent_cocomon_idx = battle_opponent_party[beat.opponent_switch_slot].species;
        battle_opponent_health_display = 0.0f;
    }

    if (beat.trigger_attack && beat.attacker != Cocomon::Nil) {
        battle_animating_attack_side = beat.attacker_is_player ? BattleVisualSide::Player : BattleVisualSide::Opponent;
        battle_attack_anim_duration = beat.duration < 0.35f ? beat.duration : 0.35f;
        battle_attack_anim_timer = battle_attack_anim_duration;
    }

    if (beat.damage > 0 && beat.defender != Cocomon::Nil) {
        battle_damage_popup_side = beat.defender_is_player ? BattleVisualSide::Player : BattleVisualSide::Opponent;
        battle_damage_popup_amount = beat.damage;
        battle_damage_popup_duration = beat.duration < 0.5f ? beat.duration : 0.5f;
        battle_damage_popup_timer = battle_damage_popup_duration;
    }

    if (beat.defender_health_after >= 0) {
        if (beat.defender_is_player) {
            battle_player_health_target = (float)beat.defender_health_after;
        } else {
            battle_opponent_health_target = (float)beat.defender_health_after;
        }
    }
}

bool has_active_caption() {
    return battle_active_caption_timer > 0.0f && battle_active_caption[0] != '\0';
}

float caption_alpha() {
    if (!has_active_caption() || battle_active_caption_duration <= 0.0f) return 0.0f;

    float elapsed = battle_active_caption_duration - battle_active_caption_timer;
    float fade_in = elapsed / 0.12f;
    float fade_out = battle_active_caption_timer / 0.15f;
    if (fade_in > 1.0f) fade_in = 1.0f;
    if (fade_out > 1.0f) fade_out = 1.0f;
    return fade_in < fade_out ? fade_in : fade_out;
}

Vector2 attack_offset(BattleVisualSide side) {
    if (side != battle_animating_attack_side || battle_attack_anim_duration <= 0.0f) return {};

    float progress = 1.0f - (battle_attack_anim_timer / battle_attack_anim_duration);
    float swing = std::sinf(progress * 3.14159265f);

    if (side == BattleVisualSide::Player) {
        return Vector2{ swing * 28.0f, -swing * 12.0f };
    }

    if (side == BattleVisualSide::Opponent) {
        return Vector2{ -swing * 28.0f, swing * 12.0f };
    }

    return {};
}

float intro_transition_progress() {
    if (battle_intro_transition_duration <= 0.0f) return 1.0f;

    float progress = 1.0f - (battle_intro_transition_timer / battle_intro_transition_duration);
    if (progress < 0.0f) progress = 0.0f;
    if (progress > 1.0f) progress = 1.0f;
    return progress;
}

bool intro_transition_active() {
    return battle_intro_transition_timer > 0.0f && battle_intro_transition_duration > 0.0f;
}

void draw_intro_transition() {
    if (!intro_transition_active()) return;

    float progress = intro_transition_progress();
    float width = (float)screen_width;
    float height = (float)screen_height;
    float center_x = width * 0.5f;
    float center_y = height * 0.5f;
    float max_radius = std::sqrtf(width * width + height * height) * 0.65f;
    float reveal_radius = max_radius * progress * progress;
    float shock_radius = max_radius * (0.24f + progress * 0.82f);
    float flash = 1.0f - progress;
    flash *= flash;
    float veil_alpha = 1.0f - progress;

    DrawRectangle(0, 0, screen_width, screen_height, Color{ 9, 16, 28, (unsigned char)(220.0f * veil_alpha) });

    for (int stripe_idx = 0; stripe_idx < 7; stripe_idx++) {
        float stripe_progress = progress * 1.15f - stripe_idx * 0.08f;
        if (stripe_progress <= 0.0f) continue;

        float stripe_width = width * (0.18f + stripe_idx * 0.018f);
        float stripe_height = 58.0f + stripe_idx * 18.0f;
        float stripe_x = -stripe_width + stripe_progress * (width + stripe_width * 1.6f);
        float stripe_y = height * (0.12f + stripe_idx * 0.11f);
        unsigned char alpha = (unsigned char)(110.0f * (1.0f - progress) * (1.0f - stripe_idx * 0.09f));
        Color stripe_color = (stripe_idx % 2 == 0)
            ? Color{ 84, 214, 255, alpha }
            : Color{ 255, 203, 112, alpha };

        DrawRectanglePro(
            Rectangle{ stripe_x, stripe_y, stripe_width, stripe_height },
            Vector2{ 0.0f, stripe_height * 0.5f },
            -23.0f,
            stripe_color
        );
    }

    DrawRing(
        Vector2{ center_x, center_y },
        shock_radius - 26.0f,
        shock_radius,
        0.0f,
        360.0f,
        96,
        Color{ 255, 244, 214, (unsigned char)(150.0f * (1.0f - progress)) }
    );

    DrawCircleGradient(
        (int)center_x,
        (int)center_y,
        reveal_radius,
        Color{ 255, 255, 255, (unsigned char)(120.0f * flash) },
        Color{ 255, 255, 255, 0 }
    );

    if (reveal_radius < max_radius) {
        DrawRing(
            Vector2{ center_x, center_y },
            reveal_radius,
            max_radius + 4.0f,
            0.0f,
            360.0f,
            128,
            Color{ 9, 16, 28, (unsigned char)(255.0f * (1.0f - progress * 0.45f)) }
        );
    }
}

void draw_opponent_trainer(const battle_scene::DrawAssets& assets, TrainerId opponent_trainer_id, int action_box_y) {
    if (!has_opponent_trainer(opponent_trainer_id) || assets.trainer_sprites == nullptr) return;

    Texture2D trainer_tex = assets.trainer_sprites[(size_t)trainer_def(opponent_trainer_id).sprite];
    if (trainer_tex.id == 0) return;

    float progress = intro_transition_active() ? intro_transition_progress() : 1.0f;
    float scale = 3.2f;
    float trainer_width = trainer_sprite_width * scale;
    float trainer_height = trainer_sprite_height * scale;
    float slide_in = (1.0f - progress) * 72.0f;
    float trainer_x = screen_width - trainer_width + slide_in;
    float trainer_y = (float)action_box_y - trainer_height * 1.98f;
    unsigned char alpha = (unsigned char)(205.0f * (0.45f + progress * 0.55f));
    Color tint = Color{ 255, 255, 255, alpha };

    DrawEllipse(
        (int)(trainer_x + trainer_width * 0.5f),
        (int)(trainer_y + trainer_height - 4.0f),
        trainer_width * 0.22f,
        11.0f,
        Color{ 0, 0, 0, (unsigned char)(45.0f * progress) }
    );

    DrawCircleGradient(
        (int)(trainer_x + trainer_width * 0.45f),
        (int)(trainer_y + trainer_height * 0.38f),
        trainer_width * 0.46f,
        Color{ 210, 228, 240, (unsigned char)(55.0f * progress) },
        Color{ 210, 228, 240, 0 }
    );

    DrawTexturePro(
        trainer_tex,
        Rectangle{ (float)EntityDirection::Left * trainer_sprite_width, 0.0f, trainer_sprite_width, trainer_sprite_height },
        Rectangle{ trainer_x, trainer_y, trainer_width, trainer_height },
        Vector2{ 0.0f, 0.0f },
        0.0f,
        tint
    );

    DrawRectangleGradientV(
        (int)(trainer_x - 8.0f),
        (int)(trainer_y + trainer_height * 0.16f),
        (int)(trainer_width + 16.0f),
        (int)(trainer_height * 0.92f),
        Color{ 180, 198, 214, 0 },
        Color{ 180, 198, 214, (unsigned char)(90.0f * progress) }
    );
}

void ui_draw_cocomon_box(int x, int y, const CocomonDef& cocomon, float displayed_health = -1.0f) {
    int width = int(screen_width * 0.35f);
    int height = int(screen_height * 0.13f);
    int health_box_width = width - 30;
    int health_box_height = (height * 0.15f);
    int name_font_size = 34;
    int stats_font_size = 18;
    int shown_health = displayed_health < 0.0f ? cocomon.health : (int)std::ceil(displayed_health);
    float health_ratio = (float)shown_health / (float)cocomon.max_health;
    if (health_ratio < 0.0f) health_ratio = 0.0f;
    if (health_ratio > 1.0f) health_ratio = 1.0f;
    int health_fill_width = (int)(health_box_width * health_ratio);
    int health_bar_y = y + 10 + name_font_size;
    int stats_y = health_bar_y + health_box_height + 8;

    DrawRectangle(x, y, width, height, GRAY);
    DrawText(cocomon.name, x + 5, y + 5, name_font_size, WHITE);
    DrawRectangle(x + 15, health_bar_y, health_box_width, health_box_height, DARKGRAY);
    DrawRectangle(x + 15, health_bar_y, health_fill_width, health_box_height, GREEN);

    char text_hp[32];
    std::snprintf(text_hp, sizeof(text_hp), "HP %d/%d", shown_health, cocomon.max_health);
    DrawText(text_hp, x + 15, stats_y, stats_font_size, WHITE);

    char text_stats[64];
    std::snprintf(text_stats, sizeof(text_stats), "ATK %d  DEF %d  SPD %d", cocomon.attack, cocomon.defense, cocomon.speed);
    DrawText(text_stats, x + 15, stats_y + stats_font_size + 4, stats_font_size, WHITE);
}

void ui_draw_trainer_party_indicator(int x, int y, TrainerId opponent_trainer_id) {
    if (!has_opponent_trainer(opponent_trainer_id) || battle_opponent_party_count <= 0) return;

    int label_font_size = 16;
    int pip_radius = 7;
    int pip_gap = 10;
    int padding_x = 12;
    int padding_y = 8;
    int label_gap = 10;
    int label_width = MeasureText("TEAM", label_font_size);
    int pip_diameter = pip_radius * 2;
    int pip_area_width = battle_opponent_party_count * pip_diameter + (battle_opponent_party_count - 1) * pip_gap;
    int width = padding_x * 2 + label_width + label_gap + pip_area_width;
    int height = padding_y * 2 + pip_diameter;
    int label_y = y + (height - label_font_size) / 2;
    int pip_center_y = y + height / 2;
    int pip_x = x + width - padding_x - pip_radius;

    DrawRectangle(x, y, width, height, Color{ 30, 38, 53, 210 });
    DrawRectangleLinesEx(Rectangle{ (float)x, (float)y, (float)width, (float)height }, 2.0f, Color{ 120, 132, 152, 230 });
    DrawText("TEAM", x + padding_x, label_y, label_font_size, Color{ 230, 236, 243, 255 });

    for (int slot = battle_opponent_party_count - 1; slot >= 0; slot--) {
        const CocomonInstance& member = battle_opponent_party[slot];
        bool fainted = member.species == Cocomon::Nil || member.battler.health <= 0;
        bool active = slot == battle_opponent_active_party_slot && !fainted;
        Color fill = fainted ? Color{ 64, 72, 88, 255 } : (active ? Color{ 255, 214, 102, 255 } : Color{ 126, 214, 255, 255 });
        Color outline = fainted ? Color{ 102, 110, 126, 255 } : Color{ 245, 248, 255, 255 };
        int center_x = pip_x - (battle_opponent_party_count - 1 - slot) * (pip_diameter + pip_gap);

        DrawCircle(center_x, pip_center_y, (float)pip_radius, fill);
        DrawCircleLines(center_x, pip_center_y, (float)pip_radius, outline);

        if (active) {
            DrawCircleLines(center_x, pip_center_y, (float)pip_radius + 3.0f, Color{ 255, 244, 214, 220 });
        }
    }
}

void ui_draw_action_bar_move(int x, int y, int cell_width, int cell_height, bool selected, CocomonMoveDef move) {
    int margin = 10;
    Color color = selected ? color_primary : color_surface_3;

    DrawRectangle(x, y, cell_width, cell_height, color);
    DrawText(move.name, x + margin, y + margin, font_size_move, WHITE);

    char text_pp[32];
    std::snprintf(text_pp, sizeof(text_pp), "%d / %d", move.pp, move.pp_max);
    DrawText(text_pp, x + margin, y + margin + font_size_move + margin, font_size_move, WHITE);

    char text_dmg[32];
    std::snprintf(text_dmg, sizeof(text_dmg), "%s %d", cocomon_element_names[(size_t)move.element], move.dmg);
    DrawText(text_dmg, x + margin, y + margin + font_size_move + margin + font_size_move + margin, font_size_move, WHITE);
}

void ui_draw_action_bar_moves(int x, int y, int width, int height) {
    DrawRectangle(x, y, width, height, color_surface_1);

    CocomonDef cocomon = active_player_cocomon().battler;
    int gap = 10;
    int cell_width = (width - gap * 3) / 2;
    int cell_height = (height - gap * 3) / 2;

    int top_left_x = x + gap;
    int top_left_y = y + gap;
    int top_right_x = top_left_x + cell_width + gap;
    int top_right_y = top_left_y;
    int bot_left_x = top_left_x;
    int bot_left_y = top_left_y + cell_height + gap;
    int bot_right_x = top_right_x;
    int bot_right_y = bot_left_y;

    if (cocomon.moves[0].flags > 0) {
        ui_draw_action_bar_move(top_left_x, top_left_y, cell_width, cell_height, ui_cursor == (uint32_t)BattleUIIndex::AbilityOne, cocomon.moves[0]);
    }
    if (cocomon.moves[1].flags > 0) {
        ui_draw_action_bar_move(top_right_x, top_right_y, cell_width, cell_height, ui_cursor == (uint32_t)BattleUIIndex::AbilityTwo, cocomon.moves[1]);
    }
    if (cocomon.moves[2].flags > 0) {
        ui_draw_action_bar_move(bot_left_x, bot_left_y, cell_width, cell_height, ui_cursor == (uint32_t)BattleUIIndex::AbilityThree, cocomon.moves[2]);
    }
    if (cocomon.moves[3].flags > 0) {
        ui_draw_action_bar_move(bot_right_x, bot_right_y, cell_width, cell_height, ui_cursor == (uint32_t)BattleUIIndex::AbilityFour, cocomon.moves[3]);
    }
}

void ui_draw_action_bar_menu_item(int x, int y, int width, int height, bool selected, const char* text) {
    int font_size = 24;
    int text_width = MeasureText(text, font_size);
    Color color = selected ? color_primary : color_surface_2;

    DrawRectangle(x, y, width, height, color);
    DrawText(text, x + (width - text_width) / 2, y + (height - font_size) / 2, font_size, WHITE);
}

void ui_draw_action_bar_menu(int x, int y, int width, int height) {
    DrawRectangle(x, y, width, height, color_surface_1);

    int gap = 10;
    int cell_width = (width - gap * 3) / 2;
    int cell_height = (height - gap * 3) / 2;

    int top_left_x = x + gap;
    int top_left_y = y + gap;
    int top_right_x = top_left_x + cell_width + gap;
    int top_right_y = top_left_y;
    int bot_left_x = top_left_x;
    int bot_left_y = top_left_y + cell_height + gap;
    int bot_right_x = top_right_x;
    int bot_right_y = bot_left_y;

    ui_draw_action_bar_menu_item(top_left_x, top_left_y, cell_width, cell_height, ui_cursor == (uint32_t)BattleUIIndex::Cocomon, "COCOMON");
    ui_draw_action_bar_menu_item(top_right_x, top_right_y, cell_width, cell_height, ui_cursor == (uint32_t)BattleUIIndex::Cocoball, "COCOBALL");
    ui_draw_action_bar_menu_item(bot_left_x, bot_left_y, cell_width, cell_height, ui_cursor == (uint32_t)BattleUIIndex::Nil, "");
    ui_draw_action_bar_menu_item(bot_right_x, bot_right_y, cell_width, cell_height, ui_cursor == (uint32_t)BattleUIIndex::Run, "RUN");
}

void ui_draw_action_bar(int action_box_height, int action_box_y) {
    int x = 0;
    int y = action_box_y;
    int width = screen_width;
    int height = action_box_height;
    int moves_width = (int)(width * 0.6f);

    DrawRectangle(x, y, width, height, color_surface_1);
    ui_draw_action_bar_moves(x, y, moves_width, height);
    ui_draw_action_bar_menu(x + moves_width, y, screen_width - moves_width, height);
}

void ui_draw_caption_banner(int y, const char* message, float alpha) {
    int font_size = 28;
    int padding_x = 26;
    int padding_y = 16;
    int text_width = MeasureText(message, font_size);
    int width = text_width + padding_x * 2;
    int height = font_size + padding_y * 2;
    int x = (screen_width - width) / 2;
    Color bg = color_surface_0;
    Color border = color_surface_3;
    Color fg = WHITE;
    bg.a = (unsigned char)(210.0f * alpha);
    border.a = (unsigned char)(255.0f * alpha);
    fg.a = (unsigned char)(255.0f * alpha);

    DrawRectangle(x, y, width, height, bg);
    DrawRectangleLinesEx(Rectangle{ (float)x, (float)y, (float)width, (float)height }, 4.0f, border);
    DrawText(message, x + padding_x, y + padding_y, font_size, fg);
}

void ui_draw_damage_popup(int action_box_y) {
    if (battle_damage_popup_timer <= 0.0f || battle_damage_popup_side == BattleVisualSide::None || battle_damage_popup_amount <= 0) return;

    float progress = 1.0f - (battle_damage_popup_timer / battle_damage_popup_duration);
    float alpha = 1.0f - progress;
    int font_size = 34;
    char damage_text[16];
    std::snprintf(damage_text, sizeof(damage_text), "-%d", battle_damage_popup_amount);

    Vector2 position;
    if (battle_damage_popup_side == BattleVisualSide::Player) {
        position = Vector2{ 160.0f, (float)action_box_y - 220.0f - progress * 28.0f };
    } else {
        position = Vector2{ screen_width * 0.62f, 170.0f - progress * 28.0f };
    }

    Color color = { 255, 214, 102, (unsigned char)(255.0f * alpha) };
    DrawText(damage_text, (int)position.x, (int)position.y, font_size, color);
}

} // namespace

namespace battle_scene {

void clear_playback() {
    for (int beat_idx = 0; beat_idx < battle_playback_capacity; beat_idx++) {
        battle_beats[beat_idx].caption[0] = '\0';
    }

    battle_beat_count = 0;
    battle_beat_index = 0;
    battle_beat_timer = 0.0f;
    battle_active_caption[0] = '\0';
    battle_active_caption_timer = 0.0f;
    battle_active_caption_duration = 0.0f;
    battle_pending_finish_reason = battle::FinishReason::None;
    battle_animating_attack_side = BattleVisualSide::None;
    battle_attack_anim_timer = 0.0f;
    battle_attack_anim_duration = 0.0f;
    battle_damage_popup_side = BattleVisualSide::None;
    battle_damage_popup_amount = 0;
    battle_damage_popup_timer = 0.0f;
    battle_damage_popup_duration = 0.0f;
    battle_pending_open_party_menu = false;
    battle_pending_forced_party_menu = false;
    battle_pending_party_heal_on_finish = false;
    battle_intro_transition_timer = 0.0f;
    battle_intro_transition_duration = 0.0f;
}

void reset_health_display() {
    battle_player_health_display = (float)active_player_cocomon().battler.health;
    battle_player_health_target = battle_player_health_display;
    battle_opponent_health_display = (float)active_opponent_cocomon().battler.health;
    battle_opponent_health_target = battle_opponent_health_display;
}

void start_intro_transition() {
    battle_intro_transition_duration = intro_transition_duration;
    battle_intro_transition_timer = battle_intro_transition_duration;
}

void queue_intro_playback(TrainerId opponent_trainer_id) {
    char caption[battle_caption_max_chars];

    if (battle_is_wild_encounter) {
        std::snprintf(caption, sizeof(caption), "A wild %s appeared!", active_opponent_cocomon().battler.name);
        push_beat(caption, 0.9f);
    } else if (has_opponent_trainer(opponent_trainer_id)) {
        std::snprintf(caption, sizeof(caption), "%s wants to battle!", trainer_name(opponent_trainer_id));
        push_beat(caption, 0.9f);

        std::snprintf(caption, sizeof(caption), "%s sent out %s!", trainer_name(opponent_trainer_id), active_opponent_cocomon().battler.name);
        push_beat(caption, 0.8f);
    } else {
        std::snprintf(caption, sizeof(caption), "%s wants to battle!", active_opponent_cocomon().battler.name);
        push_beat(caption, 0.9f);
    }

    std::snprintf(caption, sizeof(caption), "Go, %s!", active_player_cocomon().battler.name);
    push_beat(caption, 0.75f);
}

void queue_turn_result_playback(const battle::TurnResult& result, TrainerId opponent_trainer_id) {
    clear_playback();
    battle_pending_finish_reason = result.finish_reason;
    battle_pending_open_party_menu = result.party_switch_required;
    battle_pending_forced_party_menu = result.party_switch_required;
    battle_pending_party_heal_on_finish = result.finish_reason == battle::FinishReason::OpponentWon;

    if (result.player_switched) {
        battle_player_health_display = (float)active_player_cocomon().battler.health;
        battle_player_health_target = battle_player_health_display;
    }

    if (result.action.type == battle::ActionType::ThrowCocoball) {
        push_cocoball_beats(result);
    } else if (!result.action_resolved) {
        switch (result.action.type) {
            case battle::ActionType::UseMove: {
                push_beat("That move can't be used.", 0.8f);
                break;
            }
            case battle::ActionType::OpenCocomonMenu: {
                push_beat("No other Cocomon can battle.", 0.85f);
                break;
            }
            case battle::ActionType::RunAway: {
                push_beat("You can't run from a trainer battle.", 0.95f);
                break;
            }
            case battle::ActionType::ThrowCocoball:
            case battle::ActionType::None: {
                break;
            }
        }
    }

    push_switch_beat(result);
    push_move_beat(result.first_move);
    push_move_beat(result.second_move);
    push_opponent_switch_beat(result, opponent_trainer_id);

    switch (result.finish_reason) {
        case battle::FinishReason::PlayerWon: {
            push_beat("You won the battle!", 0.9f);
            award_victory_experience();
            break;
        }
        case battle::FinishReason::OpponentWon: {
            push_beat("You lost the battle!", 0.9f);
            break;
        }
        case battle::FinishReason::PlayerRanAway: {
            push_beat("You ran away!", 0.7f);
            break;
        }
        case battle::FinishReason::PlayerCapturedOpponent:
        case battle::FinishReason::None: {
            break;
        }
    }
}

void begin_queued_playback(const Callbacks& callbacks) {
    if (battle_beat_count > 0) {
        start_next_beat(callbacks);
        return;
    }

    resolve_playback_completion(callbacks);
}

void update(float delta, const Callbacks& callbacks) {
    float health_delta = 140.0f * delta;
    battle_player_health_display = move_towards_float(battle_player_health_display, battle_player_health_target, health_delta);
    battle_opponent_health_display = move_towards_float(battle_opponent_health_display, battle_opponent_health_target, health_delta);

    if (battle_active_caption_timer > 0.0f) {
        battle_active_caption_timer -= delta;
        if (battle_active_caption_timer < 0.0f) battle_active_caption_timer = 0.0f;
    }

    if (battle_attack_anim_timer > 0.0f) {
        battle_attack_anim_timer -= delta;
        if (battle_attack_anim_timer <= 0.0f) {
            battle_attack_anim_timer = 0.0f;
            battle_animating_attack_side = BattleVisualSide::None;
        }
    }

    if (battle_damage_popup_timer > 0.0f) {
        battle_damage_popup_timer -= delta;
        if (battle_damage_popup_timer <= 0.0f) {
            battle_damage_popup_timer = 0.0f;
            battle_damage_popup_side = BattleVisualSide::None;
            battle_damage_popup_amount = 0;
        }
    }

    if (intro_transition_active()) {
        battle_intro_transition_timer -= delta;
        if (battle_intro_transition_timer < 0.0f) {
            battle_intro_transition_timer = 0.0f;
        }
    }

    if (battle_beat_timer > 0.0f) {
        battle_beat_timer -= delta;
        if (battle_beat_timer > 0.0f) return;
    }

    if (battle_beat_index < battle_beat_count) {
        start_next_beat(callbacks);
        return;
    }

    resolve_playback_completion(callbacks);
}

bool is_busy() {
    return battle_beat_timer > 0.0f || battle_beat_index < battle_beat_count;
}

void draw(const DrawAssets& assets, TrainerId opponent_trainer_id) {
    int cocomon_status_box_width = int(screen_width * 0.35f);
    int cocomon_status_box_height = int(screen_height * 0.1f);
    int action_box_height = int(screen_height * 0.3f);
    int action_box_y = screen_height - action_box_height;
    float opponent_x = has_opponent_trainer(opponent_trainer_id) ? screen_width * 0.36f : screen_width * 0.5f;

    if (assets.background.id != 0) {
        DrawTexturePro(assets.background, { 0.0f, 0.0f, 512.0f, 512.0f }, { 0.0f, 0.0f, (float)screen_width, (float)screen_height }, { 0.0f, 0.0f }, 0.0f, WHITE);
    } else {
        ClearBackground(color_surface_0);
    }

    int opponent_cocomon_box_x = 15;
    int opponent_cocomon_box_y = 15;
    ui_draw_cocomon_box(opponent_cocomon_box_x, opponent_cocomon_box_y, active_opponent_cocomon().battler, battle_opponent_health_display);
    ui_draw_trainer_party_indicator(opponent_cocomon_box_x + 12, opponent_cocomon_box_y + cocomon_status_box_height + 10, opponent_trainer_id);

    draw_opponent_trainer(assets, opponent_trainer_id, action_box_y);

    Vector2 opponent_attack = attack_offset(BattleVisualSide::Opponent);
    DrawTextureEx(tex_cocomon_fronts[(size_t)opponent_cocomon_idx], Vector2{ opponent_x + opponent_attack.x, float(opponent_cocomon_box_y - -bobbing) + opponent_attack.y }, 0.0f, 12.0f, WHITE);

    int player_cocomon_box_x = screen_width - cocomon_status_box_width - 15;
    int player_cocomon_box_y = action_box_y - cocomon_status_box_height - 15;
    ui_draw_cocomon_box(player_cocomon_box_x, player_cocomon_box_y, active_player_cocomon().battler, battle_player_health_display);

    Texture2D player_back_texture = tex_cocomon_backs[(size_t)player_cocomon_idx];
    Vector2 player_attack = attack_offset(BattleVisualSide::Player);
    DrawTextureEx(player_back_texture, Vector2{ 50.0f + player_attack.x, float(action_box_y - (player_back_texture.height * 14.0f) * 0.75f - bobbing) + player_attack.y }, 0.0f, 14.0f, WHITE);

    ui_draw_action_bar(action_box_height, action_box_y);
    ui_draw_damage_popup(action_box_y);
    if (has_active_caption()) {
        ui_draw_caption_banner(140, battle_active_caption, caption_alpha());
    }
    draw_intro_transition();
}

} // namespace battle_scene
