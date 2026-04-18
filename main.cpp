#include "game.h"
#include "battle.h"
#include <assert.h>
#include <csignal>
#include <stdio.h>
#include <string.h>
#include <cmath>
#include <ctime>

static inline void debug_break() {
#if defined(_MSC_VER)
    __debugbreak();
#elif defined(__has_builtin)
    #if __has_builtin(__builtin_debugtrap)
        __builtin_debugtrap();
    #else
        raise(SIGTRAP);
    #endif
#else
    raise(SIGTRAP);
#endif
}

// =====================================================================================================================
// FORWARD DECLARATIONS
// =====================================================================================================================

float world_from_tile(int tile);
Vector2 world_from_tile(Vector2i tile);
void open_cocomon_list(GameState return_state, bool forced_selection);

// =====================================================================================================================
// STATE
// =====================================================================================================================

const Color color_surface_0 = Color{ 130, 130, 130, 250 };
const Color color_surface_1 = Color{ 150, 150, 150, 250 };
const Color color_surface_2 = Color{ 170, 170, 170, 250 };
const Color color_surface_3 = Color{ 190, 190, 190, 250 };
const Color color_primary   = Color{ 104, 185, 199, 250 };
int screen_width = default_screen_width;
int screen_height = default_screen_height;
MoveKey key_stack[4];
int key_count = 0;
char cocomon_element_names[(size_t)CocomonElement::COUNT][32];
CocomonMoveDef cocomon_moves[(size_t)CocomonMove::COUNT];
CocomonDef cocomons[max_cocomons];
CocomonDef cocomon_defaults[max_cocomons];
Texture2D tex_cocomon_fronts[max_cocomons];
Texture2D tex_cocomon_backs[max_cocomons];
Texture2D tex_npc[(size_t)Npc::COUNT];
Texture2D tex_world_entities[(size_t)WorldEntity::COUNT];
CocomonInstance player_party[max_player_party];
int player_party_count = 0;
int player_active_party_slot = 0;
CocomonInstance battle_opponent_cocomon = {};
Cocomon player_cocomon_idx = Cocomon::LocoMoco;
Cocomon opponent_cocomon_idx = Cocomon::FrickaFlow;
bool battle_is_wild_encounter = false;
GameState game_state = GameState::Overworld;
GameState game_state_next = GameState::Overworld;
uint32_t ui_cursor = 0; // Each scene understands what this means.
Music current_music_stream = {};
bool music_loaded = false;
WorldEntityDef world[world_height][world_width];
const NpcDef npcs[] = {
    { Npc::Yamenko, world_from_tile({25, 25}), 1 },
    { Npc::Yamenko, world_from_tile({50, 33}), 2 },
    { Npc::Yamenko, world_from_tile({33, 50}), 3 },
    { Npc::Yamenko, world_from_tile({5, 22}), 4 },
};
const uint32_t npc_count = sizeof(npcs) / sizeof(npcs[0]);

// --- PLAYER ---
Texture2D tex_player;
Vector2 player_pos = { world_width * tile_size_f * 0.5f, world_height * tile_size_f * 0.5f };
float player_speed = 300.0f; // pixels per second
int player_frame = 0;
float player_anim_timer = 0.0f;
int player_frames_per_row = 4;
float player_frame_interval = 0.2f;
PlayerAnimState last_player_animation_row = PlayerAnimState::IdleDown;
PlayerAnimState player_animation_row = PlayerAnimState::IdleDown;
float person_width = 32.0f;
float person_height = 64.0f;

// --- BOBBING ---
int bobbing = 6;
float bobbing_timer = 0.0f;
float bobbing_interval = 0.4f;

// --- ENOUNTER ---
extern const float chance_encounter = 0.005f;
float encounter_timer = 0.0f;
float encounter_interval = 1.0f;

// --- BATTLE PRESENTATION ---
constexpr int battle_playback_capacity = 24;
constexpr int battle_caption_max_chars = 96;
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
};

BattleBeat battle_beats[battle_playback_capacity];
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
GameState cocomon_list_return_state = GameState::Overworld;
bool cocomon_list_forced_selection = false;
// --- TILE ANIM ---
float tile_anim_timer = 0.0f;
float tile_anime_interval = 0.25f;

// =====================================================================================================================
// METHODS
// =====================================================================================================================

void play_music(const char* path) {
    Music music_battle_anthem = LoadMusicStream(path);
    SetMusicVolume(music_battle_anthem, 0.15f);
    assert(music_battle_anthem.frameCount > 0);
    PlayMusicStream(music_battle_anthem);

    current_music_stream = music_battle_anthem;
    music_loaded = true;
}

void stop_current_music() {
    StopMusicStream(current_music_stream);
    UnloadMusicStream(current_music_stream);
    current_music_stream = {};
    music_loaded = false;
}

bool chance(float probability) {
    return ((float)rand() / ((float)RAND_MAX + 1.0f)) < probability;
}

int tile_from_world(float world) {
    return (int)floorf(world / tile_size_f);
}

Vector2i tile_from_world(Vector2 world) {
    Vector2i result;

    result.x = (int)floorf(world.x / tile_size_f);
    result.y = (int)floorf(world.y / tile_size_f);

    return result;
}

float world_from_tile(int tile) {
    return tile * tile_size_f;
}

Vector2 world_from_tile(Vector2i tile) {
    Vector2 result;

    result.x = tile.x * tile_size_f;
    result.y = tile.y * tile_size_f;

    return result;
}

bool cocomon_instance_is_valid(const CocomonInstance& instance) {
    return instance.species != Cocomon::Nil;
}

bool cocomon_instance_can_battle(const CocomonInstance& instance) {
    return cocomon_instance_is_valid(instance) && instance.battler.health > 0;
}

CocomonDef scaled_cocomon_def(Cocomon species, int level) {
    CocomonDef result = cocomon_defaults[(size_t)species];
    int level_offset = level - 1;
    if (level_offset < 0) level_offset = 0;

    result.max_health += level_offset * 8;
    result.health = result.max_health;
    result.attack += level_offset * 2;
    result.defense += level_offset * 2;
    result.speed += level_offset;

    for (int move_slot = 0; move_slot < max_cocomon_moves; move_slot++) {
        if (result.moves[move_slot].flags > 0) {
            result.moves[move_slot].pp = result.moves[move_slot].pp_max;
        }
    }

    return result;
}

void refresh_cocomon_instance_stats(CocomonInstance& instance, bool full_heal = false) {
    if (!cocomon_instance_is_valid(instance)) return;

    int previous_max_health = instance.battler.max_health;
    int previous_health = instance.battler.health;
    CocomonMoveDef previous_moves[max_cocomon_moves] = {};
    for (int move_slot = 0; move_slot < max_cocomon_moves; move_slot++) {
        previous_moves[move_slot] = instance.battler.moves[move_slot];
    }

    instance.battler = scaled_cocomon_def(instance.species, instance.level);

    if (!full_heal && previous_max_health > 0) {
        int health_gain = instance.battler.max_health - previous_max_health;
        instance.battler.health = previous_health + health_gain;
        if (instance.battler.health < 0) instance.battler.health = 0;
        if (instance.battler.health > instance.battler.max_health) instance.battler.health = instance.battler.max_health;
    }

    for (int move_slot = 0; move_slot < max_cocomon_moves; move_slot++) {
        if (instance.battler.moves[move_slot].flags == 0) continue;
        if (full_heal) continue;

        instance.battler.moves[move_slot].pp = previous_moves[move_slot].pp;
        if (instance.battler.moves[move_slot].pp > instance.battler.moves[move_slot].pp_max) {
            instance.battler.moves[move_slot].pp = instance.battler.moves[move_slot].pp_max;
        }
        if ((int)instance.battler.moves[move_slot].pp < 0) {
            instance.battler.moves[move_slot].pp = 0;
        }
    }
}

CocomonInstance make_cocomon_instance(Cocomon species, int level) {
    CocomonInstance instance = {};
    instance.species = species;
    instance.level = level;
    instance.xp = 0;
    instance.battler = scaled_cocomon_def(species, level);
    return instance;
}

void restore_cocomon_instance(CocomonInstance& instance) {
    refresh_cocomon_instance_stats(instance, true);
}

int experience_to_next_level(int level) {
    return 50 + (level - 1) * 25;
}

bool player_party_slot_is_valid(int slot) {
    return slot >= 0 && slot < player_party_count && cocomon_instance_is_valid(player_party[slot]);
}

bool player_party_slot_can_battle(int slot) {
    return player_party_slot_is_valid(slot) && cocomon_instance_can_battle(player_party[slot]);
}

int first_valid_player_party_slot() {
    for (int slot = 0; slot < player_party_count; slot++) {
        if (player_party_slot_is_valid(slot)) return slot;
    }

    return 0;
}

int first_usable_player_party_slot() {
    for (int slot = 0; slot < player_party_count; slot++) {
        if (player_party_slot_can_battle(slot)) return slot;
    }

    return first_valid_player_party_slot();
}

int first_switchable_player_party_slot() {
    for (int slot = 0; slot < player_party_count; slot++) {
        if (slot == player_active_party_slot) continue;
        if (player_party_slot_can_battle(slot)) return slot;
    }

    return player_active_party_slot;
}

void sync_player_active_cocomon_from_party() {
    if (!player_party_slot_can_battle(player_active_party_slot)) {
        player_active_party_slot = first_usable_player_party_slot();
    } else if (!player_party_slot_is_valid(player_active_party_slot)) {
        player_active_party_slot = first_valid_player_party_slot();
    }

    if (player_party_slot_is_valid(player_active_party_slot)) {
        player_cocomon_idx = player_party[player_active_party_slot].species;
    }
}

void set_player_active_party_slot(int slot) {
    if (!player_party_slot_is_valid(slot)) return;

    player_active_party_slot = slot;
    player_cocomon_idx = player_party[slot].species;
}

void heal_player_party_full() {
    for (int slot = 0; slot < player_party_count; slot++) {
        if (!player_party_slot_is_valid(slot)) continue;
        restore_cocomon_instance(player_party[slot]);
    }
}

Cocomon random_wild_encounter_cocomon() {
    static const Cocomon encounters[] = {
        Cocomon::FrickaFlow,
        Cocomon::Molly,
        Cocomon::LocoMoco,
        Cocomon::Jokko,
        Cocomon::WakaCaca,
    };
    int encounter_count = (int)(sizeof(encounters) / sizeof(encounters[0]));
    return encounters[rand() % encounter_count];
}

int random_wild_encounter_level() {
    int base_level = 3;
    if (player_party_slot_is_valid(player_active_party_slot)) {
        base_level = player_party[player_active_party_slot].level;
    }

    int level = base_level - 1 + (rand() % 3);
    if (level < 1) level = 1;
    return level;
}

void prepare_random_encounter() {
    sync_player_active_cocomon_from_party();
    battle_opponent_cocomon = make_cocomon_instance(random_wild_encounter_cocomon(), random_wild_encounter_level());
    opponent_cocomon_idx = battle_opponent_cocomon.species;
}

CocomonInstance& active_player_cocomon() {
    return player_party[player_active_party_slot];
}

CocomonInstance& active_opponent_cocomon() {
    return battle_opponent_cocomon;
}

Rectangle ui_draw_cocomon_box(int x, int y, const CocomonDef& cocomon, float displayed_health = -1.0f) {
    int width = int(screen_width * 0.35f);
    int height = int(screen_height * 0.13f);
    int health_box_width = width - 30;
    int health_box_height = (height * 0.15f);
    int name_font_size = 34;
    int stats_font_size = 18;
    int shown_health = displayed_health < 0.0f ? cocomon.health : (int)ceilf(displayed_health);
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
    snprintf(text_hp, sizeof(text_hp), "HP %d/%d", shown_health, cocomon.max_health);
    DrawText(text_hp, x + 15, stats_y, stats_font_size, WHITE);

    char text_stats[64];
    snprintf(text_stats, sizeof(text_stats), "ATK %d  DEF %d  SPD %d", cocomon.attack, cocomon.defense, cocomon.speed);
    DrawText(text_stats, x + 15, stats_y + stats_font_size + 4, stats_font_size, WHITE);

    return { (float)x, (float)y, (float)width, (float)height };
}

void ui_draw_action_bar_move(int x, int y, int cell_width, int cell_height, bool selected, CocomonMoveDef move) {
    int margin = 10;
    Color color = selected ? color_primary : color_surface_3;

    // Draw container
    DrawRectangle(x, y, cell_width, cell_height, color);
    
    // Draw name
    DrawText(move.name, x + margin, y + margin, font_size_move, WHITE);

    // Draw PP
    char text_pp[32];
    snprintf(text_pp, 32, "%d / %d", move.pp, move.pp_max);
    DrawText(text_pp, x + margin, y + margin + font_size_move + margin, font_size_move, WHITE);

    // Draw dmg
    char text_dmg[32];
    snprintf(text_dmg, 32, "%s %d", cocomon_element_names[(size_t)move.element], move.dmg);
    DrawText(text_dmg, x + margin, y + margin + font_size_move + margin + font_size_move + margin, font_size_move, WHITE);
}

void ui_draw_action_bar_moves(int x, int y, int width, int height) {
    DrawRectangle(x, y, width, height, color_surface_1);

    CocomonDef cocomon = active_player_cocomon().battler;
    
    // Grid setup
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

    // Top-left
    if (cocomon.moves[0].flags > 0) {
        ui_draw_action_bar_move(top_left_x, top_left_y, cell_width, cell_height, ui_cursor == (uint32_t)BattleUIIndex::AbilityOne, cocomon.moves[0]);
    }
    // Top-right
    if (cocomon.moves[1].flags > 0) {
        ui_draw_action_bar_move(top_right_x, top_right_y, cell_width, cell_height, ui_cursor == (uint32_t)BattleUIIndex::AbilityTwo, cocomon.moves[1]);
    }
    // Bottom-left
    if (cocomon.moves[2].flags > 0) {
        ui_draw_action_bar_move(bot_left_x, bot_left_y, cell_width, cell_height, ui_cursor == (uint32_t)BattleUIIndex::AbilityThree, cocomon.moves[2]);
    }
    // Bottom-right
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

Rectangle ui_draw_action_bar(int action_box_height, int action_box_y) {    
    int x = 0;
    int y = action_box_y;
    int width = screen_width;
    int height = action_box_height;

    int moves_width = (int)(width * 0.6f);

    DrawRectangle(x, y, width, height, color_surface_1);

    ui_draw_action_bar_moves(x, y, moves_width, height);
    ui_draw_action_bar_menu(x + moves_width, y, screen_width - moves_width, height);

    return { (float)0, (float)y, (float)width, (float)height };
}

Rectangle ui_draw_battle_caption_banner(int y, const char* message, float alpha) {
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

    return { (float)x, (float)y, (float)width, (float)height };
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

void battle_clear_playback() {
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
}

void battle_reset_health_display() {
    battle_player_health_display = (float)active_player_cocomon().battler.health;
    battle_player_health_target = battle_player_health_display;
    battle_opponent_health_display = (float)active_opponent_cocomon().battler.health;
    battle_opponent_health_target = battle_opponent_health_display;
}

bool battle_has_pending_beats() {
    return battle_beat_timer > 0.0f || battle_beat_index < battle_beat_count;
}

bool battle_has_active_caption() {
    return battle_active_caption_timer > 0.0f && battle_active_caption[0] != '\0';
}

void battle_push_beat(const char* caption, float duration, Cocomon attacker = Cocomon::Nil, bool attacker_is_player = false, Cocomon defender = Cocomon::Nil, bool defender_is_player = false, int damage = 0, int defender_health_after = -1, bool trigger_attack = false) {
    if (battle_beat_count >= battle_playback_capacity) return;

    BattleBeat& beat = battle_beats[battle_beat_count];
    beat = {};
    strncpy(beat.caption, caption, battle_caption_max_chars - 1);
    beat.caption[battle_caption_max_chars - 1] = '\0';
    beat.duration = duration;
    beat.attacker = attacker;
    beat.attacker_is_player = attacker_is_player;
    beat.defender = defender;
    beat.defender_is_player = defender_is_player;
    beat.damage = damage;
    beat.defender_health_after = defender_health_after;
    beat.trigger_attack = trigger_attack;
    battle_beat_count += 1;
}

void battle_push_move_beat(const battle::MoveEvent& move_event) {
    if (!move_event.happened) return;

    const CocomonDef& attacker = cocomons[(size_t)move_event.attacker];
    const CocomonDef& defender = cocomons[(size_t)move_event.defender];
    const CocomonMoveDef& move = cocomons[(size_t)move_event.attacker].moves[move_event.move_slot];
    char caption[battle_caption_max_chars];

    snprintf(caption, sizeof(caption), "%s used %s!", attacker.name, move.name);
    battle_push_beat(caption, 0.75f, move_event.attacker, move_event.attacker_is_player, move_event.defender, move_event.defender_is_player, move_event.damage, move_event.defender_health_after, true);

    switch (move_event.effectiveness) {
        case battle::Effectiveness::SuperEffective: {
            battle_push_beat("It's super effective!", 0.55f);
            break;
        }
        case battle::Effectiveness::NotVeryEffective: {
            battle_push_beat("It's not very effective.", 0.55f);
            break;
        }
        case battle::Effectiveness::Normal: {
            break;
        }
    }

    if (move_event.defender_fainted) {
        snprintf(caption, sizeof(caption), "%s fainted!", defender.name);
        battle_push_beat(caption, 0.75f);
    }
}

void battle_push_switch_beat(const battle::TurnResult& result) {
    if (!result.player_switched) return;

    char caption[battle_caption_max_chars];
    const CocomonDef& switched_to = cocomons[(size_t)result.switched_to];
    snprintf(caption, sizeof(caption), "Go, %s!", switched_to.name);
    battle_push_beat(caption, 0.7f);
}

void battle_push_cocoball_beats(const battle::TurnResult& result) {
    if (result.action.type != battle::ActionType::ThrowCocoball) return;

    char caption[battle_caption_max_chars];
    const char* captured_name = cocomons[(size_t)result.captured_species].name;

    switch (result.capture_outcome) {
        case battle::CaptureOutcome::BlockedNotWild: {
            battle_push_beat("You can only catch wild Cocomon.", 0.85f);
            break;
        }
        case battle::CaptureOutcome::BlockedPartyFull: {
            battle_push_beat("Your party is full.", 0.75f);
            break;
        }
        case battle::CaptureOutcome::Failed: {
            battle_push_beat("You threw a Cocoball!", 0.55f);
            snprintf(caption, sizeof(caption), "%s broke free!", captured_name);
            battle_push_beat(caption, 0.8f);
            break;
        }
        case battle::CaptureOutcome::Caught: {
            battle_push_beat("You threw a Cocoball!", 0.55f);
            snprintf(caption, sizeof(caption), "Gotcha! %s was caught!", captured_name);
            battle_push_beat(caption, 0.9f);
            snprintf(caption, sizeof(caption), "%s joined your party!", captured_name);
            battle_push_beat(caption, 0.85f);
            break;
        }
        case battle::CaptureOutcome::None: {
            break;
        }
    }
}

void battle_award_victory_experience() {
    CocomonInstance& player = active_player_cocomon();
    const CocomonInstance& opponent = active_opponent_cocomon();
    char caption[battle_caption_max_chars];
    int experience_gained = 25 + opponent.level * 15;

    snprintf(caption, sizeof(caption), "%s gained %d XP!", player.battler.name, experience_gained);
    battle_push_beat(caption, 0.9f);

    player.xp += experience_gained;
    while (player.xp >= experience_to_next_level(player.level)) {
        player.xp -= experience_to_next_level(player.level);
        player.level += 1;
        refresh_cocomon_instance_stats(player);
        player_cocomon_idx = player.species;
        battle_player_health_target = (float)player.battler.health;
        battle_player_health_display = battle_player_health_target;

        snprintf(caption, sizeof(caption), "%s reached LV %d!", player.battler.name, player.level);
        battle_push_beat(caption, 0.9f);
    }
}

void battle_queue_turn_result_playback(const battle::TurnResult& result) {
    battle_clear_playback();
    battle_pending_finish_reason = result.finish_reason;
    battle_pending_open_party_menu = result.party_switch_required;
    battle_pending_forced_party_menu = result.party_switch_required;
    battle_pending_party_heal_on_finish = result.finish_reason == battle::FinishReason::OpponentWon;

    if (result.player_switched) {
        battle_player_health_display = (float)active_player_cocomon().battler.health;
        battle_player_health_target = battle_player_health_display;
    }

    if (result.action.type == battle::ActionType::ThrowCocoball) {
        battle_push_cocoball_beats(result);
    } else if (!result.action_resolved) {
        switch (result.action.type) {
            case battle::ActionType::UseMove: {
                battle_push_beat("That move can't be used.", 0.8f);
                break;
            }
            case battle::ActionType::OpenCocomonMenu: {
                battle_push_beat("No other Cocomon can battle.", 0.85f);
                break;
            }
            case battle::ActionType::RunAway:
            case battle::ActionType::ThrowCocoball:
            case battle::ActionType::None: {
                break;
            }
        }
    }

    battle_push_switch_beat(result);
    battle_push_move_beat(result.first_move);
    battle_push_move_beat(result.second_move);

    switch (result.finish_reason) {
        case battle::FinishReason::PlayerWon: {
            battle_push_beat("You won the battle!", 0.9f);
            battle_award_victory_experience();
            break;
        }
        case battle::FinishReason::OpponentWon: {
            battle_push_beat("You lost the battle!", 0.9f);
            break;
        }
        case battle::FinishReason::PlayerRanAway: {
            battle_push_beat("You ran away!", 0.7f);
            break;
        }
        case battle::FinishReason::PlayerCapturedOpponent: {
            break;
        }
        case battle::FinishReason::None: {
            break;
        }
    }
}

void reset_keys() {
    for (int i = 0; i < key_count; i++) {
        key_stack[i] = MoveKey::Nil;
    }
    key_count = 0;
}

void push_move_key(MoveKey key) {
    // prevent duplicates
    for (int i = 0; i < key_count; i++) {
        if (key_stack[i] == key) {
            return;
        }
    }

    key_stack[key_count++] = key;
}

void remove_move_key(MoveKey key) {
    for (int i = 0; i < key_count; i++) {
        if (key_stack[i] == key) {
            // shift left
            for (int j = i; j < key_count - 1; j++) {
                key_stack[j] = key_stack[j + 1];
            }
            key_count--;
            return;
        }
    }
}

void state_transition_overworld() {
    game_state_next = GameState::Overworld;
    battle_clear_playback();
    battle_pending_open_party_menu = false;
    battle_pending_forced_party_menu = false;
    battle_pending_party_heal_on_finish = false;
    battle_is_wild_encounter = false;
    reset_keys();
    stop_current_music();
}

void open_cocomon_list(GameState return_state, bool forced_selection) {
    cocomon_list_return_state = return_state;
    cocomon_list_forced_selection = forced_selection;
    int selected_slot = player_active_party_slot;
    if (return_state == GameState::Battle) {
        selected_slot = first_switchable_player_party_slot();
    }
    if (forced_selection && !player_party_slot_can_battle(selected_slot)) {
        selected_slot = first_usable_player_party_slot();
    }
    ui_cursor = (uint32_t)selected_slot;
    game_state_next = GameState::CocomonList;
}

void battle_start_next_beat() {
    if (battle_beat_index >= battle_beat_count) {
        if (battle_pending_open_party_menu) {
            bool forced_selection = battle_pending_forced_party_menu;
            battle_pending_open_party_menu = false;
            battle_pending_forced_party_menu = false;
            open_cocomon_list(GameState::Battle, forced_selection);
            return;
        }

        if (battle_pending_finish_reason != battle::FinishReason::None) {
            if (battle_pending_party_heal_on_finish) {
                heal_player_party_full();
            }
            state_transition_overworld();
        }
        return;
    }

    const BattleBeat& beat = battle_beats[battle_beat_index];
    battle_beat_index += 1;
    battle_beat_timer = beat.duration;

    strncpy(battle_active_caption, beat.caption, battle_caption_max_chars - 1);
    battle_active_caption[battle_caption_max_chars - 1] = '\0';
    battle_active_caption_timer = beat.duration;
    battle_active_caption_duration = beat.duration;

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

void battle_update_playback(float delta) {
    float health_delta = 140.0f * (float)delta;
    battle_player_health_display = move_towards_float(battle_player_health_display, battle_player_health_target, health_delta);
    battle_opponent_health_display = move_towards_float(battle_opponent_health_display, battle_opponent_health_target, health_delta);

    if (battle_active_caption_timer > 0.0f) {
        battle_active_caption_timer -= (float)delta;
        if (battle_active_caption_timer < 0.0f) battle_active_caption_timer = 0.0f;
    }

    if (battle_attack_anim_timer > 0.0f) {
        battle_attack_anim_timer -= (float)delta;
        if (battle_attack_anim_timer <= 0.0f) {
            battle_attack_anim_timer = 0.0f;
            battle_animating_attack_side = BattleVisualSide::None;
        }
    }

    if (battle_damage_popup_timer > 0.0f) {
        battle_damage_popup_timer -= (float)delta;
        if (battle_damage_popup_timer <= 0.0f) {
            battle_damage_popup_timer = 0.0f;
            battle_damage_popup_side = BattleVisualSide::None;
            battle_damage_popup_amount = 0;
        }
    }

    if (battle_beat_timer > 0.0f) {
        battle_beat_timer -= (float)delta;
        if (battle_beat_timer > 0.0f) return;
    }

    if (battle_beat_index < battle_beat_count) {
        battle_start_next_beat();
        return;
    }

    if (battle_pending_open_party_menu) {
        bool forced_selection = battle_pending_forced_party_menu;
        battle_pending_open_party_menu = false;
        battle_pending_forced_party_menu = false;
        open_cocomon_list(GameState::Battle, forced_selection);
        return;
    }

    if (battle_pending_finish_reason != battle::FinishReason::None) {
        if (battle_pending_party_heal_on_finish) {
            heal_player_party_full();
        }
        state_transition_overworld();
    }
}

void battle_queue_intro_playback() {
    char caption[battle_caption_max_chars];

    if (battle_is_wild_encounter) {
        snprintf(caption, sizeof(caption), "A wild %s appeared!", active_opponent_cocomon().battler.name);
    } else {
        snprintf(caption, sizeof(caption), "%s wants to battle!", active_opponent_cocomon().battler.name);
    }
    battle_push_beat(caption, 0.9f);

    snprintf(caption, sizeof(caption), "Go, %s!", active_player_cocomon().battler.name);
    battle_push_beat(caption, 0.75f);
}

void enter_battle(bool random_wild_encounter = false) {
    battle_is_wild_encounter = random_wild_encounter;
    if (random_wild_encounter) {
        prepare_random_encounter();
    } else {
        sync_player_active_cocomon_from_party();
        if (!cocomon_instance_can_battle(battle_opponent_cocomon)) {
            battle_opponent_cocomon = make_cocomon_instance(opponent_cocomon_idx, active_player_cocomon().level);
        }
    }

    battle::start();
    battle_clear_playback();
    battle_reset_health_display();
    battle_queue_intro_playback();
    if (battle_beat_count > 0) {
        battle_start_next_beat();
    }
}

float battle_caption_alpha() {
    if (!battle_has_active_caption() || battle_active_caption_duration <= 0.0f) return 0.0f;

    float elapsed = battle_active_caption_duration - battle_active_caption_timer;
    float fade_in = elapsed / 0.12f;
    float fade_out = battle_active_caption_timer / 0.15f;
    if (fade_in > 1.0f) fade_in = 1.0f;
    if (fade_out > 1.0f) fade_out = 1.0f;
    return fade_in < fade_out ? fade_in : fade_out;
}

Vector2 battle_attack_offset(BattleVisualSide side) {
    if (side != battle_animating_attack_side || battle_attack_anim_duration <= 0.0f) return {};

    float progress = 1.0f - (battle_attack_anim_timer / battle_attack_anim_duration);
    float swing = sinf(progress * 3.14159265f);

    if (side == BattleVisualSide::Player) {
        return Vector2{ swing * 28.0f, -swing * 12.0f };
    }

    if (side == BattleVisualSide::Opponent) {
        return Vector2{ -swing * 28.0f, swing * 12.0f };
    }

    return {};
}

void ui_draw_battle_damage_popup(int action_box_y) {
    if (battle_damage_popup_timer <= 0.0f || battle_damage_popup_side == BattleVisualSide::None || battle_damage_popup_amount <= 0) return;

    float progress = 1.0f - (battle_damage_popup_timer / battle_damage_popup_duration);
    float alpha = 1.0f - progress;
    int font_size = 34;
    char damage_text[16];
    snprintf(damage_text, sizeof(damage_text), "-%d", battle_damage_popup_amount);

    Vector2 position;
    if (battle_damage_popup_side == BattleVisualSide::Player) {
        position = Vector2{ 160.0f, (float)action_box_y - 220.0f - progress * 28.0f };
    } else {
        position = Vector2{ screen_width * 0.62f, 170.0f - progress * 28.0f };
    }

    Color color = { 255, 214, 102, (unsigned char)(255.0f * alpha) };
    DrawText(damage_text, (int)position.x, (int)position.y, font_size, color);
}

void ui_draw_cocomon_list_screen() {
    int title_font_size = 40;
    int row_height = 82;
    int row_gap = 10;
    int start_y = 102;
    int x = 80;
    int width = screen_width - 160;

    ClearBackground(color_surface_0);
    DrawText("YOUR COCOMON", x, 40, title_font_size, WHITE);

    for (int slot = 0; slot < player_party_count; slot++) {
        const CocomonInstance& instance = player_party[slot];
        bool selected = (int)ui_cursor == slot;
        bool can_battle = cocomon_instance_can_battle(instance);
        bool active = slot == player_active_party_slot;
        int y = start_y + slot * (row_height + row_gap);
        Color background = selected ? color_primary : color_surface_2;
        Color text_color = can_battle ? WHITE : Color{ 210, 210, 210, 255 };
        char summary[128];
        char xp_text[64];
        char tag[64];
        int xp_needed = experience_to_next_level(instance.level);
        float xp_ratio = xp_needed > 0 ? (float)instance.xp / (float)xp_needed : 0.0f;
        int xp_bar_x = x + 18;
        int xp_bar_y = y + row_height - 14;
        int xp_bar_width = width - 36;
        if (xp_ratio < 0.0f) xp_ratio = 0.0f;
        if (xp_ratio > 1.0f) xp_ratio = 1.0f;

        DrawRectangle(x, y, width, row_height, background);

        snprintf(summary, sizeof(summary), "LV %d   HP %d/%d", instance.level, instance.battler.health, instance.battler.max_health);
        snprintf(xp_text, sizeof(xp_text), "XP %d/%d", instance.xp, xp_needed);
        DrawText(instance.battler.name, x + 18, y + 10, 30, text_color);
        DrawText(summary, x + 18, y + 44, 22, text_color);
        DrawText(xp_text, x + width - MeasureText(xp_text, 22) - 18, y + 44, 22, text_color);
        DrawRectangle(xp_bar_x, xp_bar_y, xp_bar_width, 8, color_surface_1);
        DrawRectangle(xp_bar_x, xp_bar_y, (int)(xp_bar_width * xp_ratio), 8, WHITE);

        tag[0] = '\0';
        if (active) strncpy(tag, "ACTIVE", sizeof(tag) - 1);
        if (!can_battle) strncpy(tag, "FAINTED", sizeof(tag) - 1);
        tag[sizeof(tag) - 1] = '\0';

        if (tag[0] != '\0') {
            int tag_width = MeasureText(tag, 24);
            DrawText(tag, x + width - tag_width - 18, y + 12, 24, text_color);
        }
    }

    if (cocomon_list_forced_selection) {
        DrawText("Choose a Cocomon that can still fight.", x, screen_height - 60, 28, WHITE);
    } else {
        DrawText("ENTER to confirm   ESC to close", x, screen_height - 60, 28, WHITE);
    }
}

Rectangle player_collision_box() {
    float foot_width = 20.0f;
    float foot_height = 8.0f;

    Rectangle result = {
        player_pos.x - foot_width * 0.5f,
        player_pos.y - foot_height,
        foot_width,
        foot_height,
    };

    return result;
}

bool rect_intersects(Rectangle a, Rectangle b) {
    return (a.x < b.x + b.width)  &&
           (a.y < b.y + b.height) &&
           (a.x + a.width > b.x)  &&
           (a.y + a.height > b.y);
}

bool world_entity_blocks_movement(WorldEntity entity) {
    switch(entity) {
        case WorldEntity::Grass:
        case WorldEntity::GrassTall:
            return false;

        case WorldEntity::WallGrassEnd:
        case WorldEntity::WallGrassLine:
        case WorldEntity::WallGrassBend:
        case WorldEntity::WallGrassT:
        case WorldEntity::WallGrassX:
        case WorldEntity::Water:
            return true;

        default:
            return false;
    }
}

void move_and_resolve_player(Vector2 delta) {
    // -------------------------------------------------------------------- 
    // X AXIS 
    // ----------------- --------------------------------------------------
    player_pos.x += delta.x;

    Rectangle box = player_collision_box();

    float epsilon = 0.001f;
    int left      = tile_from_world(box.x);
    int right     = tile_from_world(box.x + box.width  - epsilon);
    int top       = tile_from_world(box.y);
    int bottom    = tile_from_world(box.y + box.height - epsilon);

    if(top < 0) top = 0;
    if(bottom >= world_height) bottom = world_height - 1;

    if(delta.x > 0.0f) {
        // moving right
        if(right >= world_width) {
            player_pos.x = world_from_tile(world_width) - box.width * 0.5f;
        } else {
            for(int y = top; y <= bottom; y++) {
                if(world_entity_blocks_movement(world[y][right].entity)) {
                    player_pos.x = world_from_tile(right) - box.width * 0.5f;
                    break;
                }
            }
        }
    }
    else if(delta.x < 0.0f) {
        // moving left
        if(left < 0) {
            player_pos.x = box.width * 0.5f;
        } else {
            for(int y = top; y <= bottom; y++) {
                if(world_entity_blocks_movement(world[y][left].entity)) {
                    player_pos.x = world_from_tile(left + 1) + box.width * 0.5f;
                    break;
                }
            }
        }
    }

    // -------------------------------------------------------------------- 
    // Y AXIS
    // ----------------- --------------------------------------------------
    player_pos.y += delta.y;

    box = player_collision_box();

    left   = tile_from_world(box.x);
    right  = tile_from_world(box.x + box.width  - epsilon);
    top    = tile_from_world(box.y);
    bottom = tile_from_world(box.y + box.height - epsilon);

    if(left < 0) left = 0;
    if(right >= world_width) right = world_width - 1;

    if(delta.y > 0.0f) {
        // moving down
        if(bottom >= world_height) {
            player_pos.y = world_from_tile(world_height);
        } else {
            for(int x = left; x <= right; x++) {
                if(world_entity_blocks_movement(world[bottom][x].entity)) {
                    player_pos.y = world_from_tile(bottom);
                    break;
                }
            }
        }
    }
    else if(delta.y < 0.0f) {
        // moving up
        if(top < 0) {
            player_pos.y = box.height;
        } else {
            for(int x = left; x <= right; x++) {
                if(world_entity_blocks_movement(world[top][x].entity)) {
                    player_pos.y = world_from_tile(top + 1) + box.height;
                    break;
                }
            }
        }
    }
}

// =====================================================================================================================
// MAIN
// =====================================================================================================================

int main(void) {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(screen_width, screen_height, "raylib 2D");
    InitAudioDevice();
    SetWindowMinSize(640, 640);

    srand((unsigned int)time(NULL));

    tex_player = LoadTexture("sprites/player_sprites.png");

    tex_cocomon_fronts[(size_t)Cocomon::LocoMoco] = LoadTexture("sprites/locomoco_front.png");
    tex_cocomon_fronts[(size_t)Cocomon::FrickaFlow] = LoadTexture("sprites/fricka_flow_front.png");
    tex_cocomon_fronts[(size_t)Cocomon::Molly] = LoadTexture("sprites/molly_front.png");
    tex_cocomon_fronts[(size_t)Cocomon::Jokko] = LoadTexture("sprites/jokko_front.png");
    tex_cocomon_fronts[(size_t)Cocomon::WakaCaca] = LoadTexture("sprites/waka-caca_front.png");

    tex_cocomon_backs[(size_t)Cocomon::LocoMoco] = LoadTexture("sprites/locomoco_back.png");
    tex_cocomon_backs[(size_t)Cocomon::FrickaFlow] = LoadTexture("sprites/fricka_flow_back.png");
    tex_cocomon_backs[(size_t)Cocomon::Molly] = LoadTexture("sprites/molly_back.png");
    tex_cocomon_backs[(size_t)Cocomon::Jokko] = LoadTexture("sprites/jokko_back.png");
    tex_cocomon_backs[(size_t)Cocomon::WakaCaca] = LoadTexture("sprites/waka-caca_back.png");

    tex_npc[(size_t)Npc::Yamenko] = LoadTexture("sprites/npc_yamenko.png");

    tex_world_entities[(size_t)WorldEntity::Grass]         = LoadTexture("sprites/grass_tile.png");
    tex_world_entities[(size_t)WorldEntity::GrassTall]     = LoadTexture("sprites/grass_tile_tall.png");
    tex_world_entities[(size_t)WorldEntity::WallGrassEnd]  = LoadTexture("sprites/wall_grass_end.png");
    tex_world_entities[(size_t)WorldEntity::WallGrassLine] = LoadTexture("sprites/wall_grass_line.png");
    tex_world_entities[(size_t)WorldEntity::WallGrassBend] = LoadTexture("sprites/wall_grass_bend.png");
    tex_world_entities[(size_t)WorldEntity::WallGrassT]    = LoadTexture("sprites/wall_grass_t.png");
    tex_world_entities[(size_t)WorldEntity::WallGrassX]    = LoadTexture("sprites/wall_grass_x.png");
    tex_world_entities[(size_t)WorldEntity::Water]         = LoadTexture("sprites/water_tile.png");

    strcpy(cocomon_element_names[(size_t)CocomonElement::Grass], "GRASS");
    strcpy(cocomon_element_names[(size_t)CocomonElement::Fire], "FIRE");
    strcpy(cocomon_element_names[(size_t)CocomonElement::Water], "WATER");

    cocomon_moves[(size_t)CocomonMove::Ember] = { "EMBER", 30, 30, 30, CocomonElement::Fire, 1 };
    cocomon_moves[(size_t)CocomonMove::WaterGun] = { "WATER GUN", 30, 30, 30, CocomonElement::Water, 1 };
    cocomon_moves[(size_t)CocomonMove::LeafBlade] = { "LEAF BLADE", 30, 30, 30, CocomonElement::Grass, 1 };

    cocomon_defaults[(size_t)Cocomon::LocoMoco] = {
        .name = "LOCOMOCO",
        .element = CocomonElement::Fire,
        .health = 120,
        .max_health = 120,
        .attack = 18,
        .defense = 10,
        .speed = 14,
        .moves = {
            cocomon_moves[(size_t)CocomonMove::Ember],
            cocomon_moves[(size_t)CocomonMove::Ember],
            cocomon_moves[(size_t)CocomonMove::Ember],
            cocomon_moves[(size_t)CocomonMove::Ember]
        }
    };
    cocomon_defaults[(size_t)Cocomon::FrickaFlow] = {
        .name = "FRICKA FLOW",
        .element = CocomonElement::Grass,
        .health = 90,
        .max_health = 90,
        .attack = 15,
        .defense = 8,
        .speed = 20,
        .moves = { cocomon_moves[(size_t)CocomonMove::LeafBlade] }
    };
    cocomon_defaults[(size_t)Cocomon::Molly] = {
        .name = "MOLLY",
        .element = CocomonElement::Water,
        .health = 110,
        .max_health = 110,
        .attack = 16,
        .defense = 14,
        .speed = 10,
        .moves = { cocomon_moves[(size_t)CocomonMove::WaterGun] }
    };
    cocomon_defaults[(size_t)Cocomon::Jokko] = {
        .name = "JOKKO",
        .element = CocomonElement::Fire,
        .health = 100,
        .max_health = 100,
        .attack = 17,
        .defense = 9,
        .speed = 18,
        .moves = {
            cocomon_moves[(size_t)CocomonMove::Ember],
            cocomon_moves[(size_t)CocomonMove::LeafBlade]
        }
    };
    cocomon_defaults[(size_t)Cocomon::WakaCaca] = {
        .name = "WAKA CACA",
        .element = CocomonElement::Water,
        .health = 95,
        .max_health = 95,
        .attack = 18,
        .defense = 11,
        .speed = 16,
        .moves = {
            cocomon_moves[(size_t)CocomonMove::WaterGun],
            cocomon_moves[(size_t)CocomonMove::LeafBlade]
        }
    };

    cocomons[(size_t)Cocomon::LocoMoco] = cocomon_defaults[(size_t)Cocomon::LocoMoco];
    cocomons[(size_t)Cocomon::FrickaFlow] = cocomon_defaults[(size_t)Cocomon::FrickaFlow];
    cocomons[(size_t)Cocomon::Molly] = cocomon_defaults[(size_t)Cocomon::Molly];
    cocomons[(size_t)Cocomon::Jokko] = cocomon_defaults[(size_t)Cocomon::Jokko];
    cocomons[(size_t)Cocomon::WakaCaca] = cocomon_defaults[(size_t)Cocomon::WakaCaca];

    player_party[0] = make_cocomon_instance(Cocomon::LocoMoco, 5);
    player_party[1] = make_cocomon_instance(Cocomon::Molly, 4);
    player_party[2] = make_cocomon_instance(Cocomon::FrickaFlow, 4);
    player_party_count = 3;
    player_active_party_slot = 0;
    sync_player_active_cocomon_from_party();
    battle_opponent_cocomon = make_cocomon_instance(Cocomon::Jokko, 4);
    opponent_cocomon_idx = battle_opponent_cocomon.species;

    // --- WORLD SETUP ---
    for(int y = 0; y < 16; y++) {
        for(int x = 0; x < 16; x++) {
            world[y + 10][x + 10] = { WorldEntity::GrassTall, 0.0f };
        }
    }

    int lake_y = 18;
    int lake_x = 36;
    for(int x = 9;  x <= 14; x++) world[lake_y + 0][lake_x + x] = { WorldEntity::Water, 0.0f, 4, 0 };
    for(int x = 6;  x <= 17; x++) world[lake_y + 1][lake_x + x] = { WorldEntity::Water, 0.0f, 4, 0 };
    for(int x = 4;  x <= 19; x++) world[lake_y + 2][lake_x + x] = { WorldEntity::Water, 0.0f, 4, 0 };
    for(int x = 3;  x <= 20; x++) world[lake_y + 3][lake_x + x] = { WorldEntity::Water, 0.0f, 4, 0 };
    for(int x = 2;  x <= 21; x++) world[lake_y + 4][lake_x + x] = { WorldEntity::Water, 0.0f, 4, 0 };
    for(int x = 1;  x <= 22; x++) world[lake_y + 5][lake_x + x] = { WorldEntity::Water, 0.0f, 4, 0 };
    for(int x = 1;  x <= 22; x++) world[lake_y + 6][lake_x + x] = { WorldEntity::Water, 0.0f, 4, 0 };
    for(int x = 0;  x <= 23; x++) world[lake_y + 7][lake_x + x] = { WorldEntity::Water, 0.0f, 4, 0 };
    for(int x = 0;  x <= 23; x++) world[lake_y + 8][lake_x + x] = { WorldEntity::Water, 0.0f, 4, 0 };
    for(int x = 1;  x <= 22; x++) world[lake_y + 9][lake_x + x] = { WorldEntity::Water, 0.0f, 4, 0 };
    for(int x = 1;  x <= 21; x++) world[lake_y +10][lake_x + x] = { WorldEntity::Water, 0.0f, 4, 0 };
    for(int x = 2;  x <= 20; x++) world[lake_y +11][lake_x + x] = { WorldEntity::Water, 0.0f, 4, 0 };
    for(int x = 3;  x <= 18; x++) world[lake_y +12][lake_x + x] = { WorldEntity::Water, 0.0f, 4, 0 };
    for(int x = 4;  x <= 16; x++) world[lake_y +13][lake_x + x] = { WorldEntity::Water, 0.0f, 4, 0 };
    for(int x = 6;  x <= 13; x++) world[lake_y +14][lake_x + x] = { WorldEntity::Water, 0.0f, 4, 0 };
    for(int x = 8;  x <= 11; x++) world[lake_y +15][lake_x + x] = { WorldEntity::Water, 0.0f, 4, 0 };
    
    for (int x = 1; x < world_width - 1; x++) world[0][x] = { WorldEntity::WallGrassLine, 90.0f };
    for (int x = 1; x < world_width - 1; x++) world[world_height - 1][x] = { WorldEntity::WallGrassLine, 90.0f };

    for (int y = 1; y < world_height; y++) world[y][0] = { WorldEntity::WallGrassLine, 0.0f };
    for (int y = 1; y < world_height; y++) world[y][world_width - 1] = { WorldEntity::WallGrassLine, 0.0f };

    world[0][0] = { WorldEntity::WallGrassBend, 0.0f };
    world[0][world_width - 1] = { WorldEntity::WallGrassBend, 90.0f };
    world[world_height - 1][world_width - 1] = { WorldEntity::WallGrassBend, 180.0f };
    world[world_height - 1][0] = { WorldEntity::WallGrassBend, 270.0f };

    // --- CAMERA SETUP ---
    Camera2D camera = { 0 };
    camera.target = player_pos;
    camera.offset = Vector2{ screen_width * 0.5f, screen_height * 0.5f };
    camera.rotation = 0.0f;
    camera.zoom = 2.0f;

    SetTargetFPS(60);

    // Main loop
    while (!WindowShouldClose()) {
        double delta = GetFrameTime();
        screen_width = GetScreenWidth();
        screen_height = GetScreenHeight();
        game_state = game_state_next;
        
        // DEBUG ONLY
        if (IsKeyPressed(KEY_F1)) state_transition_overworld();
        if (IsKeyPressed(KEY_F2)) enter_battle();
        if (IsKeyPressed(KEY_ONE)) player_speed -= 100.0f;
        if (IsKeyPressed(KEY_TWO)) player_speed += 100.0f;

        // Game state update
        switch (game_state) {
            case GameState::Overworld: {
                if (IsKeyPressed(KEY_TAB)) {
                    open_cocomon_list(GameState::Overworld, false);
                    break;
                }

                // --- PLAYER MOVEMENT ---
                float move_x = 0.0f;
                float move_y = 0.0f;

                if (IsKeyPressed(KEY_W)) push_move_key(MoveKey::W);
                if (IsKeyPressed(KEY_A)) push_move_key(MoveKey::A);
                if (IsKeyPressed(KEY_S)) push_move_key(MoveKey::S);
                if (IsKeyPressed(KEY_D)) push_move_key(MoveKey::D);

                if (IsKeyReleased(KEY_W)) remove_move_key(MoveKey::W);
                if (IsKeyReleased(KEY_A)) remove_move_key(MoveKey::A);
                if (IsKeyReleased(KEY_S)) remove_move_key(MoveKey::S);
                if (IsKeyReleased(KEY_D)) remove_move_key(MoveKey::D);

                if (key_count > 0) {
                    MoveKey key = key_stack[key_count - 1];

                    if (key == MoveKey::W) move_y = -1.0f;
                    if (key == MoveKey::S) move_y =  1.0f;
                    if (key == MoveKey::A) move_x = -1.0f;
                    if (key == MoveKey::D) move_x =  1.0f;
                }

                Vector2 player_prev_pos = player_pos;
                player_pos.x += float(move_x * player_speed) * delta;
                player_pos.y += float(move_y * player_speed) * delta;
                
                // --- PLAYER COLLISION ---
                Vector2 move_delta = player_pos - player_prev_pos;
                move_and_resolve_player(move_delta);

                Rectangle col_box = player_collision_box();
                bool moving = move_x != 0.0f || move_y != 0.0f;
                bool standing_in_tall_grass = false;
                for(int y = 0; y < world_height; y++) {
                    for(int x = 0; x < world_width; x++) {
                        Vector2i tile_tile = { x, y };
                        Vector2 tile_world = world_from_tile(tile_tile);
                        WorldEntityDef tile_entity = world[y][x];
                        Rectangle tile_rect = { tile_world.x, tile_world.y, tile_size_i, tile_size_i };
                        if (rect_intersects(tile_rect, col_box) && tile_entity.entity == WorldEntity::GrassTall) {
                            standing_in_tall_grass = true;
                        }
                    }
                }

                // --- ENCOUNTER ---
                if (standing_in_tall_grass && moving && encounter_timer <= 0.0f && chance(chance_encounter)) {
                    enter_battle(true);
                    encounter_timer = encounter_interval;
                    break;
                }
                encounter_timer -= delta;

                // --- PLAYER CAMERA ---
                float lerp_factor = 10.0f * delta;
                camera.target.x += (player_pos.x - camera.target.x) * lerp_factor;
                camera.target.y += (player_pos.y - camera.target.y) * lerp_factor;
                camera.offset = Vector2{ screen_width * 0.5f, screen_height * 0.5f };

                // --- PLAYER ANIM ---
                // idle rows
                last_player_animation_row = player_animation_row;
                if (moving) {
                    if (move_y > 0) player_animation_row = PlayerAnimState::RunDown; 
                    if (move_y < 0) player_animation_row = PlayerAnimState::RunUp; 
                    if (move_x > 0) player_animation_row = PlayerAnimState::RunRight; 
                    if (move_x < 0) player_animation_row = PlayerAnimState::RunLeft; 
                } else {
                    if (last_player_animation_row == PlayerAnimState::RunDown) player_animation_row = PlayerAnimState::IdleDown; 
                    if (last_player_animation_row == PlayerAnimState::RunUp) player_animation_row = PlayerAnimState::IdleUp; 
                    if (last_player_animation_row == PlayerAnimState::RunRight) player_animation_row = PlayerAnimState::IdleRight; 
                    if (last_player_animation_row == PlayerAnimState::RunLeft) player_animation_row = PlayerAnimState::IdleLeft; 
                }

                player_anim_timer += delta;
                if (player_anim_timer >= player_frame_interval) {
                    player_anim_timer = 0.0f;
                    player_frame = (player_frame + 1) % player_frames_per_row;
                }

                // ANIMATE TILES
                if(tile_anim_timer <= 0.0f) {
                    tile_anim_timer = tile_anime_interval;
                    
                    for(int y = 0; y < world_height; y++) {
                        for(int x = 0; x < world_width; x++) {
                            WorldEntityDef* world_entity = &world[y][x];
                            
                            if(world_entity->frame_count > 1) {
                                world_entity->frame_index++;
                                if(world_entity->frame_index >= world_entity->frame_count) {
                                    world_entity->frame_index = 0;
                                }
                            }
                        }
                    }
                }
                tile_anim_timer -= delta;

                break;
            }
            case GameState::Battle: {
                uint32_t grid_width = 4;
                uint32_t grid_height = 2;

                battle_update_playback(delta);
                if (game_state_next != GameState::Battle) {
                    break;
                }

                bool battle_busy = battle_has_pending_beats();

                if (!battle_busy) {
                    // Right
                    if (IsKeyPressed(KEY_RIGHT) && (ui_cursor % grid_width) < grid_width - 1) ui_cursor += 1;
                    // Left
                    if (IsKeyPressed(KEY_LEFT) && (ui_cursor % grid_width) > 0) ui_cursor -= 1;
                    // Down
                    if (IsKeyPressed(KEY_DOWN) && (ui_cursor / grid_width) < grid_height - 1) ui_cursor += grid_width;
                    // Up
                    if (IsKeyPressed(KEY_UP) && (ui_cursor / grid_width) > 0) ui_cursor -= grid_width;
                }

                if (!battle_busy && IsKeyPressed(KEY_ENTER)) {
                    battle::Action action = battle::action_from_ui((BattleUIIndex)ui_cursor);
                    if (action.type == battle::ActionType::OpenCocomonMenu) {
                        battle::TurnResult result = battle::resolve_player_action(action);
                        if (result.action_resolved) {
                            open_cocomon_list(GameState::Battle, false);
                        } else {
                            battle_queue_turn_result_playback(result);
                            if (battle_beat_count > 0) {
                                battle_start_next_beat();
                            }
                        }
                    } else {
                        battle::TurnResult result = battle::resolve_player_action(action);
                        battle_queue_turn_result_playback(result);
                        if (battle_beat_count > 0) {
                            battle_start_next_beat();
                        } else if (battle_pending_finish_reason != battle::FinishReason::None) {
                            state_transition_overworld();
                        }
                    }
                }

                if (bobbing_timer <= 0) {
                    bobbing *= -1;
                    bobbing_timer = bobbing_interval;
                }
                bobbing_timer -= delta;

                break;
            }
            case GameState::CocomonList: {
                if (IsKeyPressed(KEY_DOWN) && player_party_count > 0) {
                    ui_cursor = (ui_cursor + 1) % (uint32_t)player_party_count;
                }
                if (IsKeyPressed(KEY_UP) && player_party_count > 0) {
                    ui_cursor = (ui_cursor + (uint32_t)player_party_count - 1) % (uint32_t)player_party_count;
                }

                if (!cocomon_list_forced_selection && IsKeyPressed(KEY_ESCAPE)) {
                    game_state_next = cocomon_list_return_state;
                    break;
                }

                if (IsKeyPressed(KEY_ENTER) && player_party_count > 0) {
                    int selected_slot = (int)ui_cursor;
                    if (!player_party_slot_can_battle(selected_slot)) {
                        break;
                    }

                    if (cocomon_list_return_state == GameState::Battle) {
                        battle::TurnResult result = battle::resolve_player_switch(selected_slot, cocomon_list_forced_selection);
                        if (!result.action_resolved) {
                            break;
                        }

                        game_state_next = GameState::Battle;
                        battle_queue_turn_result_playback(result);
                        if (battle_beat_count > 0) {
                            battle_start_next_beat();
                        }
                    } else {
                        set_player_active_party_slot(selected_slot);
                        game_state_next = cocomon_list_return_state;
                    }
                }

                break;
            }
            default: {
                debug_break();
            }
        };

        // Skip draw if we changed state
        if (game_state_next != game_state) {
            continue;
        }
        
        // Common update
        if (music_loaded) UpdateMusicStream(current_music_stream);

        // Draw
        BeginDrawing();
        ClearBackground(BLACK);
        
        switch (game_state) {
            case GameState::Overworld: {
                BeginMode2D(camera);
                
                // Draw world tiles
                for (int y = 0; y < world_height; y++) {
                    for (int x = 0; x < world_width; x++) {
                        int world_x = x * tile_size_i;
                        int world_y = y * tile_size_i;

                        WorldEntityDef world_entity = world[y][x];
                        Texture2D tex = tex_world_entities[(size_t)world_entity.entity];
                        Rectangle src = { world_entity.frame_index * tile_size_f, 0.0f, tile_size_f, tile_size_f };
                        Rectangle dst = { (float)world_x + tile_size_f * 0.5f, (float)world_y + tile_size_f * 0.5f, tile_size_f, tile_size_f };
                        Vector2 origin = { tile_size_f * 0.5f, tile_size_f * 0.5f };

                        DrawTexturePro(tex, src, dst, origin, world_entity.rot, WHITE);
                    }
                }

                // Draw player
                {
                    Rectangle src = { 
                        player_frame * person_width, 
                        (int)player_animation_row * person_height, 
                        person_width, 
                        person_height,
                    };
                    Rectangle dst = { player_pos.x, player_pos.y, person_width, person_height };
                    Vector2 origin = { person_width * 0.5f, person_height };

                    // Rectangle col_box = player_collision_box(); 
                    // DrawRectangle(col_box.x, col_box.y, col_box.width, col_box.height, BLUE);
                    DrawTexturePro(tex_player, src, dst, origin, 0.0f, WHITE);
                }

                // Draw NPCS
                for (int idx = 0; idx < npc_count; idx++) {
                    NpcDef npc = npcs[idx];
                    Texture2D tex = tex_npc[(size_t)npc.npc];
                    Rectangle src = { 
                        npc.dir * person_width, 
                        0.0f, 
                        person_width, 
                        person_height,
                    };
                    Rectangle dst = { npc.pos.x, npc.pos.y, person_width, person_height };
                    Vector2 origin = { person_width * 0.5f, person_height };

                    DrawTexturePro(tex, src, dst, origin, 0.0f, WHITE);
                }

                EndMode2D();

                break;
            }
            case GameState::Battle: {
                int cocomon_status_box_width = int(screen_width * 0.35f);
                int cocomon_status_box_height = int(screen_height * 0.1f);
                int action_box_height = int(screen_height * 0.3f);
                int action_box_y = screen_height - action_box_height;
                
                // Opponent box
                int opponent_cocomon_box_x = 15;
                int opponent_cocomon_box_y = 15;
                ui_draw_cocomon_box(15, 15, active_opponent_cocomon().battler, battle_opponent_health_display);
        
                // Opponent cocomon
                Vector2 opponent_attack_offset = battle_attack_offset(BattleVisualSide::Opponent);
                DrawTextureEx(tex_cocomon_fronts[(size_t)opponent_cocomon_idx], Vector2{screen_width * 0.5f + opponent_attack_offset.x, float(opponent_cocomon_box_y - -bobbing) + opponent_attack_offset.y}, 0.0f, 12.0f, WHITE);
        
                // Player box
                int player_cocomon_box_x = screen_width - cocomon_status_box_width - 15;
                int player_cocomon_box_y = action_box_y - cocomon_status_box_height - 15;
                ui_draw_cocomon_box(player_cocomon_box_x, player_cocomon_box_y, active_player_cocomon().battler, battle_player_health_display);
                
                // Player cocomon
                Texture2D tex_player_cocomon_back = tex_cocomon_backs[(size_t)player_cocomon_idx];
                Vector2 player_attack_offset = battle_attack_offset(BattleVisualSide::Player);
                DrawTextureEx(tex_player_cocomon_back, Vector2{50.0f + player_attack_offset.x, float(action_box_y - (tex_player_cocomon_back.height * 14.0f) * 0.75f - bobbing) + player_attack_offset.y}, 0.0f, 14.0f, WHITE);

                // Action bar / battle playback overlays
                ui_draw_action_bar(action_box_height, action_box_y);
                ui_draw_battle_damage_popup(action_box_y);
                if (battle_has_active_caption()) {
                    ui_draw_battle_caption_banner(140, battle_active_caption, battle_caption_alpha());
                }
                break;
            }
            case GameState::CocomonList: {
                ui_draw_cocomon_list_screen();
                break;
            }
            default: {
                debug_break();
            }
        }

        EndDrawing();


        // --- FPS IN WINDOW TITLE ---
        int fps = (int)(1.0f / delta);
        if (fps > 1000) fps = 1000;
        char title[64];
        snprintf(title, sizeof(title), "cocomon | FPS: %d", fps);

        SetWindowTitle(title);
    }

    // De-initialization
    CloseWindow();

    return 0;
}
