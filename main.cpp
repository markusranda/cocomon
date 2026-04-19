#include "game.h"
#include "battle.h"
#include "battle_scene.h"
#include "trainers.h"
#include <assert.h>
#include <csignal>
#include <stdio.h>
#include <string.h>
#include <cmath>
#include <ctime>
#include <float.h>

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
void clear_battle_opponent_party();
void setup_battle_opponent_party(CocomonInstance opponent);
void setup_battle_opponent_party(const TrainerDef& trainer);
void prepare_trainer_encounter(int npc_index);
Vector2 nurse_tent_respawn_position();
void respawn_player_at_nurse_tent();
void finish_battle_and_transition_overworld(battle::FinishReason finish_reason, bool heal_player_party);

// =====================================================================================================================
// STATE
// =====================================================================================================================

namespace {

struct RuntimeAssets {
    Texture2D tex_npc[(size_t)Npc::COUNT] = {};
    Texture2D tex_background_battle_1 = {};
    Texture2D tex_heart = {};
    Building nurse_tent = {};
    Sound sound_kiss = {};
};

struct WorldNpcState {
    NpcDef npcs[max_npcs] = {};
    uint32_t count = 0;
};

struct TrainerEncounterState {
    TrainerId opponent_trainer_id = TrainerId::Nil;
    int opponent_trainer_npc_index = -1;
};

struct CocomonListState {
    GameState return_state = GameState::Overworld;
    bool forced_selection = false;
};

RuntimeAssets runtime_assets = {};
WorldNpcState world_npc_state = {};
TrainerEncounterState trainer_encounter_state = {};
CocomonListState cocomon_list_state = {};

} // namespace

Camera2D camera = {};
Rectangle debug_rect = {};
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
Texture2D tex_world_entities[(size_t)WorldEntity::COUNT];
CocomonInstance player_party[max_player_party];
int player_party_count = 0;
int player_active_party_slot = 0;
CocomonInstance battle_opponent_party[max_party_size] = {};
int battle_opponent_party_count = 0;
int battle_opponent_active_party_slot = 0;
Cocomon player_cocomon_idx = Cocomon::LocoMoco;
Cocomon opponent_cocomon_idx = Cocomon::FrickaFlow;
bool battle_is_wild_encounter = false;
GameState game_state = GameState::Overworld;
GameState game_state_next = GameState::Overworld;
uint32_t ui_cursor = 0; // Each scene understands what this means.
Music current_music_stream = {};
bool music_loaded = false;
WorldEntityDef world[world_height][world_width];

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

// Renderables
Renderable renderables[max_renderables];
uint32_t renderables_count = 0;

// Collidables
Rectangle collidables[max_collidables];
uint32_t collidables_count = 0;

// Interactables
InteractableDef interactables[max_interactables];
uint32_t interactables_count = 0;
bool interacting = false;

// Floating hearts
FloatingHeart floating_hearts[max_floating_hearts];
uint32_t floating_hearts_count = 0;
const float floating_heart_interval = 0.4f;
const float heart_speed = 100.0f;

// --- TILE ANIM ---
float tile_anim_timer = 0.0f;
float tile_anime_interval = 0.25f;

// =====================================================================================================================
// METHODS
// =====================================================================================================================

bool floating_hearts_push(FloatingHeart heart) {
    if (floating_hearts_count >= max_floating_hearts) {
        debug_break();
        return false;
    }

    floating_hearts[floating_hearts_count++] = heart;

    return true;
}

bool interactables_push(InteractableDef interactable) {
    if (interactables_count >= max_interactables) {
        debug_break();
        return false;
    }

    interactables[interactables_count++] = interactable;

    return true;
}

bool renderables_push(Renderable renderable) {
    if (renderables_count == max_renderables) {
        debug_break();
        return false;
    }

    renderables[renderables_count++] = renderable;
    
    return true;
}

bool collidables_push(Rectangle collidable) {
    if (collidables_count >= max_collidables) {
        debug_break();
        return false;
    }

    collidables[collidables_count++] = collidable;
    return true;
}

int cmp_renderable(const void* a, const void* b) {
    float y1 = ((Renderable*)a)->sort_y;
    float y2 = ((Renderable*)b)->sort_y;

    if (y1 < y2) return -1;
    if (y1 > y2) return 1;
    return 0;
}

inline float distance(Vector2i a, Vector2i b) {
    int dx = b.x - a.x;
    int dy = b.y - a.y;

    return sqrtf((float)(dx * dx + dy * dy));
}

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

bool rnd_chance(float probability) {
    return ((float)rand() / ((float)RAND_MAX + 1.0f)) < probability;
}

float rnd_range(float min, float max) {
    float t = (float)rand() / ((float)RAND_MAX + 1.0f);
    return min + t * (max - min);
}

// ------ COORDINATE CONVERTERS ------

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

Vector2i vector2i_from_direction(EntityDirection dir) {
    switch (dir) {
        case EntityDirection::Up:    return { 0, -1 };
        case EntityDirection::Right: return { 1,  0 };
        case EntityDirection::Down:  return { 0,  1 };
        case EntityDirection::Left:  return { -1, 0 };
    }

    return { 0, 0 }; // fallback
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

void refresh_cocomon_instance_stats(CocomonInstance& instance, bool full_heal) {
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
    setup_battle_opponent_party(make_cocomon_instance(random_wild_encounter_cocomon(), random_wild_encounter_level()));
}

void clear_battle_opponent_party() {
    for (int slot = 0; slot < max_party_size; slot++) {
        battle_opponent_party[slot] = {};
    }

    battle_opponent_party_count = 0;
    battle_opponent_active_party_slot = 0;
    opponent_cocomon_idx = Cocomon::Nil;
}

void setup_battle_opponent_party(CocomonInstance opponent) {
    clear_battle_opponent_party();
    battle_opponent_party[0] = opponent;
    battle_opponent_party_count = 1;
    battle_opponent_active_party_slot = 0;
    opponent_cocomon_idx = opponent.species;
}

void setup_battle_opponent_party(const TrainerDef& trainer) {
    clear_battle_opponent_party();

    for (int slot = 0; slot < trainer.party_count && slot < max_party_size; slot++) {
        const TrainerPartyMemberDef& party_member = trainer.party[slot];
        if (party_member.species == Cocomon::Nil) continue;
        battle_opponent_party[battle_opponent_party_count++] = make_cocomon_instance(party_member.species, party_member.level);
    }

    if (battle_opponent_party_count <= 0) {
        setup_battle_opponent_party(make_cocomon_instance(Cocomon::Jokko, 4));
        return;
    }

    battle_opponent_active_party_slot = 0;
    opponent_cocomon_idx = battle_opponent_party[0].species;
}

void prepare_trainer_encounter(int npc_index) {
    if (npc_index < 0 || npc_index >= (int)world_npc_state.count) {
        trainer_encounter_state.opponent_trainer_id = TrainerId::Nil;
        trainer_encounter_state.opponent_trainer_npc_index = -1;
        return;
    }

    const NpcDef& npc = world_npc_state.npcs[npc_index];
    trainer_encounter_state.opponent_trainer_id = npc.trainer_id;
    trainer_encounter_state.opponent_trainer_npc_index = npc_index;
    setup_battle_opponent_party(trainer_def(npc.trainer_id));
}

Vector2 nurse_tent_respawn_position() {
    return Vector2{
        runtime_assets.nurse_tent.pos.x,
        runtime_assets.nurse_tent.pos.y + runtime_assets.nurse_tent.size.y * 0.5f + tile_size_f
    };
}

void respawn_player_at_nurse_tent() {
    player_pos = nurse_tent_respawn_position();
    camera.target = player_pos;
    player_animation_row = PlayerAnimState::IdleDown;
    last_player_animation_row = PlayerAnimState::IdleDown;
    player_frame = 0;
}

// ------ KEY HANDLING ------

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
    battle_scene::clear_playback();
    battle_is_wild_encounter = false;
    trainer_encounter_state.opponent_trainer_id = TrainerId::Nil;
    trainer_encounter_state.opponent_trainer_npc_index = -1;
    reset_keys();
    stop_current_music();
}

void finish_battle_and_transition_overworld(battle::FinishReason finish_reason, bool heal_player_party) {
    if (finish_reason == battle::FinishReason::PlayerWon &&
        trainer_encounter_state.opponent_trainer_npc_index >= 0 &&
        trainer_encounter_state.opponent_trainer_npc_index < (int)world_npc_state.count) {
        world_npc_state.npcs[trainer_encounter_state.opponent_trainer_npc_index].battled = true;
    }

    if (heal_player_party) {
        heal_player_party_full();
        sync_player_active_cocomon_from_party();
    }

    if (finish_reason == battle::FinishReason::OpponentWon) {
        respawn_player_at_nurse_tent();
    }

    state_transition_overworld();
}

void open_cocomon_list(GameState return_state, bool forced_selection) {
    cocomon_list_state.return_state = return_state;
    cocomon_list_state.forced_selection = forced_selection;
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

void enter_battle(bool random_wild_encounter = false, int trainer_index = -1) {
    battle_is_wild_encounter = random_wild_encounter;
    trainer_encounter_state.opponent_trainer_id = TrainerId::Nil;
    trainer_encounter_state.opponent_trainer_npc_index = -1;
    if (random_wild_encounter) {
        prepare_random_encounter();
    } else if (trainer_index >= 0) {
        sync_player_active_cocomon_from_party();
        prepare_trainer_encounter(trainer_index);
    } else {
        sync_player_active_cocomon_from_party();
        if (battle_opponent_party_count <= 0 ||
            battle_opponent_active_party_slot < 0 ||
            battle_opponent_active_party_slot >= battle_opponent_party_count ||
            !cocomon_instance_can_battle(battle_opponent_party[battle_opponent_active_party_slot])) {
            setup_battle_opponent_party(make_cocomon_instance(opponent_cocomon_idx, player_party[player_active_party_slot].level));
        }
    }

    battle::start();
    battle_scene::clear_playback();
    battle_scene::reset_health_display();
    battle_scene::start_intro_transition();
    battle_scene::queue_intro_playback(trainer_encounter_state.opponent_trainer_id);
    battle_scene::begin_queued_playback({
        .open_cocomon_list = open_cocomon_list,
        .finish_battle = finish_battle_and_transition_overworld,
    });
}

void draw_state_cocomon_list() {
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

    if (cocomon_list_state.forced_selection) {
        DrawText("Choose a Cocomon that can still fight.", x, screen_height - 60, 28, WHITE);
    } else {
        DrawText("ENTER to confirm   ESC to close", x, screen_height - 60, 28, WHITE);
    }
}

void draw_state_overworld() {
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
        Renderable r = {
            .tex = tex_player,
            .src = { 
                player_frame * person_width,
                (int)player_animation_row * person_height,
                person_width,
                person_height
            },
            .dst = { player_pos.x, player_pos.y, person_width, person_height },
            .origin = { person_width * 0.5f, person_height },
            .rotation = 0.0f,
            .sort_y = player_pos.y
        };

        renderables_push(r);
    }

    // Draw NPCS
    for (int idx = 0; idx < (int)world_npc_state.count; idx++) {
        NpcDef npc = world_npc_state.npcs[idx];
        const TrainerDef& trainer = trainer_def(npc.trainer_id);

        Renderable r = {
            .tex = runtime_assets.tex_npc[(size_t)trainer.sprite],
            .src = { 
                (uint32_t)npc.dir * person_width,
                0.0f,
                person_width,
                person_height
            },
            .dst = { npc.pos.x, npc.pos.y, person_width, person_height },
            .origin = { person_width * 0.5f, person_height },
            .rotation = 0.0f,
            .sort_y = npc.pos.y
        };

        renderables_push(r);
    }

    // Buildings
    {
        Renderable r = {
            .tex = runtime_assets.nurse_tent.tex,
            .src = {0.0f, 0.0f, 192.0f, 192.0f},
            .dst = { runtime_assets.nurse_tent.pos.x, runtime_assets.nurse_tent.pos.y, 192.0f, 192.0f },
            .origin = {192.0f * 0.5f, 192.0f * 0.5f},
            .rotation = 0.0f,
            .sort_y = runtime_assets.nurse_tent.pos.y + 192.0f * 0.5f,
        };

        renderables_push(r);
    }

    qsort(renderables, renderables_count, sizeof(Renderable), cmp_renderable);

    // You gotta draw 'em all
    for (int i = 0; i < renderables_count; i++) {
        Renderable* r = &renderables[i];

        DrawTexturePro(
            r->tex,
            r->src,
            r->dst,
            r->origin,
            r->rotation,
            WHITE
        );
    }

    // Draw text
    if (interacting) {
        const char* text = "PRESS 'E' TO INTERACT";
        float font_size = 12.0f;
        float text_width = (float)MeasureText(text, font_size);
        float container_padding = 5.0f;
        Vector2 pos = { player_pos.x - text_width * 0.5f, player_pos.y - person_height - font_size };
        DrawRectangleRec({ pos.x - container_padding, pos.y - container_padding, text_width + 2.0f * container_padding, font_size + 2.0f * container_padding }, { 100, 100, 100, 150 });
        DrawTextPro(GetFontDefault(), text, pos, {0}, 0.0f, font_size, 1.0f, WHITE);
    }

    // Draw floating hearts
    for (int idx = 0; idx < floating_hearts_count; idx++) {
        FloatingHeart &heart = floating_hearts[idx];
        DrawTexturePro(
            runtime_assets.tex_heart,
            {0.0f, 0.0f, 32.0f, 32.0f},
            {heart.pos.x, heart.pos.y, 32.0f, 32.0f},
            {16.0f, 16.0f}, // center origin
            0.0f,
            WHITE
        );    
    }
    
    DrawRectangleRec(debug_rect, RED);
    
    // Reset state
    renderables_count = 0;
    interacting = false;

    EndMode2D();
}

// ------ COLLISION HANDLING ------

Rectangle entity_collision_box(Vector2 position) {
    float foot_width = 20.0f;
    float foot_height = 8.0f;

    Rectangle result = {
        position.x - foot_width * 0.5f,
        position.y - foot_height,
        foot_width,
        foot_height,
    };

    return result;
}

Rectangle player_collision_box() {
    return entity_collision_box(player_pos);
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

bool ranges_overlap(float min_a, float max_a, float min_b, float max_b) {
    return (max_a > min_b) && (min_a < max_b);
}

SweepHit ray_vs_rect(Vector2 ray_origin, Vector2 ray_delta, Rectangle box) {
    SweepHit result = {};
    result.hit = false;
    result.time = 1.0f;
    result.normal = { 0.0f, 0.0f };

    float inv_dx = 0.0f;
    float inv_dy = 0.0f;

    if(ray_delta.x != 0.0f) inv_dx = 1.0f / ray_delta.x;
    if(ray_delta.y != 0.0f) inv_dy = 1.0f / ray_delta.y;

    float tx1, tx2;
    if(ray_delta.x == 0.0f) {
        if(ray_origin.x < box.x || ray_origin.x > box.x + box.width) return result;
        tx1 = -FLT_MAX;
        tx2 = FLT_MAX;
    } else {
        tx1 = (box.x - ray_origin.x) * inv_dx;
        tx2 = (box.x + box.width - ray_origin.x) * inv_dx;
    }

    float ty1, ty2;
    if(ray_delta.y == 0.0f) {
        if(ray_origin.y < box.y || ray_origin.y > box.y + box.height) return result;
        ty1 = -FLT_MAX;
        ty2 = FLT_MAX;
    } else {
        ty1 = (box.y - ray_origin.y) * inv_dy;
        ty2 = (box.y + box.height - ray_origin.y) * inv_dy;
    }

    float t_near_x = tx1 < tx2 ? tx1 : tx2;
    float t_far_x  = tx1 > tx2 ? tx1 : tx2;
    float t_near_y = ty1 < ty2 ? ty1 : ty2;
    float t_far_y  = ty1 > ty2 ? ty1 : ty2;

    float t_entry = t_near_x > t_near_y ? t_near_x : t_near_y;
    float t_exit  = t_far_x < t_far_y ? t_far_x : t_far_y;

    if(t_entry > t_exit) return result;
    if(t_exit < 0.0f) return result;
    if(t_entry > 1.0f) return result;

    result.hit = true;
    result.time = t_entry < 0.0f ? 0.0f : t_entry;

    if(t_near_x > t_near_y) {
        result.normal.x = ray_delta.x > 0.0f ? -1.0f : 1.0f;
        result.normal.y = 0.0f;
    } else {
        result.normal.x = 0.0f;
        result.normal.y = ray_delta.y > 0.0f ? -1.0f : 1.0f;
    }

    return result;
}

SweepHit sweep_player_against_rect(Vector2 player_center, Vector2 move_delta, Rectangle player_box, Rectangle target_box) {
    float player_half_width = player_box.width * 0.5f;
    float player_half_height = player_box.height * 0.5f;

    Rectangle expanded = {};
    expanded.x = target_box.x - player_half_width;
    expanded.y = target_box.y - player_half_height;
    expanded.width = target_box.width + player_box.width;
    expanded.height = target_box.height + player_box.height;

    return ray_vs_rect(player_center, move_delta, expanded);
}

void gather_all_collidables(Vector2 move_delta) {
    // Npcs
    for(int entity_index = 0; entity_index < (int)world_npc_state.count; entity_index++) {
        NpcDef &npc = world_npc_state.npcs[entity_index];
        collidables_push(entity_collision_box(npc.pos));
    }

    // Buildings
    {
        Rectangle box = {
            runtime_assets.nurse_tent.pos.x - runtime_assets.nurse_tent.size.x * 0.5f,
            runtime_assets.nurse_tent.pos.y - runtime_assets.nurse_tent.size.y * 0.5f,
            runtime_assets.nurse_tent.size.x,
            runtime_assets.nurse_tent.size.y
        };
        collidables_push(box);
    }

    // Tiles intersecting the full swept area from start box to end box
    {
        Rectangle start_box = entity_collision_box(player_pos);

        Vector2 end_pos = player_pos;
        end_pos.x += move_delta.x;
        end_pos.y += move_delta.y;
        Rectangle end_box = entity_collision_box(end_pos);

        float start_right = start_box.x + start_box.width;
        float end_right = end_box.x + end_box.width;
        float start_bottom = start_box.y + start_box.height;
        float end_bottom = end_box.y + end_box.height;

        Rectangle gather_box = {};
        gather_box.x = start_box.x < end_box.x ? start_box.x : end_box.x;
        gather_box.y = start_box.y < end_box.y ? start_box.y : end_box.y;

        float gather_right = start_right > end_right ? start_right : end_right;
        float gather_bottom = start_bottom > end_bottom ? start_bottom : end_bottom;

        gather_box.width = gather_right - gather_box.x;
        gather_box.height = gather_bottom - gather_box.y;

        float epsilon = 0.001f;

        int left   = tile_from_world(gather_box.x);
        int right  = tile_from_world(gather_box.x + gather_box.width - epsilon);
        int top    = tile_from_world(gather_box.y);
        int bottom = tile_from_world(gather_box.y + gather_box.height - epsilon);

        if(left < 0) left = 0;
        if(right >= world_width) right = world_width - 1;
        if(top < 0) top = 0;
        if(bottom >= world_height) bottom = world_height - 1;

        for(int y = top; y <= bottom; y++) {
            for(int x = left; x <= right; x++) {
                WorldEntityDef world_entity = world[y][x];
                if(!world_entity_blocks_movement(world_entity.entity)) continue;

                Rectangle box = {
                    world_from_tile(x),
                    world_from_tile(y),
                    tile_size_f,
                    tile_size_f
                };
                collidables_push(box);
            }
        }
    }
}

void move_and_resolve_player_step(Vector2 move_delta) {
    gather_all_collidables(move_delta);

    Vector2 remaining_move = move_delta;
    for(int iteration = 0; iteration < 2; iteration++) {
        if(remaining_move.x == 0.0f && remaining_move.y == 0.0f) break;

        Rectangle player_box = entity_collision_box(player_pos);

        bool found_hit = false;
        SweepHit best_hit = {};
        int best_index = -1;

        best_hit.time = 1.0f;

        for(int idx = 0; idx < collidables_count; idx++) {
            Rectangle &entity_box = collidables[idx];

            SweepHit hit = sweep_player_against_rect(player_pos, remaining_move, player_box, entity_box);
            if(!hit.hit) continue;

            if(!found_hit || hit.time < best_hit.time) {
                found_hit = true;
                best_hit = hit;
                best_index = idx;
            }
        }

        if(!found_hit) {
            player_pos.x += remaining_move.x;
            player_pos.y += remaining_move.y;
            break;
        }

        float epsilon = 0.001f;
        float move_time = best_hit.time > epsilon ? best_hit.time - epsilon : 0.0f;

        player_pos.x += remaining_move.x * move_time;
        player_pos.y += remaining_move.y * move_time;

        float dot = remaining_move.x * best_hit.normal.x + remaining_move.y * best_hit.normal.y;

        Vector2 slide_move = remaining_move;
        slide_move.x = remaining_move.x * (1.0f - move_time);
        slide_move.y = remaining_move.y * (1.0f - move_time);

        float slide_dot = slide_move.x * best_hit.normal.x + slide_move.y * best_hit.normal.y;
        slide_move.x -= best_hit.normal.x * slide_dot;
        slide_move.y -= best_hit.normal.y * slide_dot;

        remaining_move = slide_move;
    }

    collidables_count = 0;
}

void move_and_resolve_player(float move_x, float move_y, float delta) {
    Vector2 total_move = {};
    total_move.x = float(move_x * player_speed) * delta;
    total_move.y = float(move_y * player_speed) * delta;

    float abs_x = total_move.x >= 0.0f ? total_move.x : -total_move.x;
    float abs_y = total_move.y >= 0.0f ? total_move.y : -total_move.y;
    float max_dist = abs_x > abs_y ? abs_x : abs_y;

    float max_step = tile_size_f * 0.25f;
    int step_count = (int)ceilf(max_dist / max_step);
    if(step_count < 1) step_count = 1;

    Vector2 step_move = {};
    step_move.x = total_move.x / float(step_count);
    step_move.y = total_move.y / float(step_count);

    for(int step_index = 0; step_index < step_count; step_index++) {
        move_and_resolve_player_step(step_move);
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
    SetExitKey(0);

    srand((unsigned int)time(NULL));

    runtime_assets.sound_kiss = LoadSound("sounds/kiss-sound-effect.mp3");
    SetSoundVolume(runtime_assets.sound_kiss, 0.1f);

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

    runtime_assets.tex_npc[(size_t)Npc::Yamenko] = LoadTexture("sprites/npc_yamenko.png");
    runtime_assets.tex_npc[(size_t)Npc::Ippip] = LoadTexture("sprites/npc_ippip.png");

    tex_world_entities[(size_t)WorldEntity::Grass]         = LoadTexture("sprites/grass_tile.png");
    tex_world_entities[(size_t)WorldEntity::GrassTall]     = LoadTexture("sprites/grass_tile_tall.png");
    tex_world_entities[(size_t)WorldEntity::WallGrassEnd]  = LoadTexture("sprites/wall_grass_end.png");
    tex_world_entities[(size_t)WorldEntity::WallGrassLine] = LoadTexture("sprites/wall_grass_line.png");
    tex_world_entities[(size_t)WorldEntity::WallGrassBend] = LoadTexture("sprites/wall_grass_bend.png");
    tex_world_entities[(size_t)WorldEntity::WallGrassT]    = LoadTexture("sprites/wall_grass_t.png");
    tex_world_entities[(size_t)WorldEntity::WallGrassX]    = LoadTexture("sprites/wall_grass_x.png");
    tex_world_entities[(size_t)WorldEntity::Water]         = LoadTexture("sprites/water_tile.png");

    runtime_assets.tex_background_battle_1 = LoadTexture("sprites/background_battle_1.png");
    runtime_assets.tex_heart = LoadTexture("sprites/heart.png");

    runtime_assets.nurse_tent = { LoadTexture("sprites/building_nurse_tent.png"), world_from_tile({10, 50}), { 192.0f, 192.0f } };

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

    setup_battle_opponent_party(make_cocomon_instance(Cocomon::Jokko, 4));

    world_npc_state.npcs[world_npc_state.count++] = make_world_trainer(world_from_tile({25, 25}), EntityDirection::Down, TrainerId::YamenkoScout);
    world_npc_state.npcs[world_npc_state.count++] = make_world_trainer(world_from_tile({50, 33}), EntityDirection::Up, TrainerId::YamenkoMixer);
    world_npc_state.npcs[world_npc_state.count++] = make_world_trainer(world_from_tile({33, 50}), EntityDirection::Right, TrainerId::YamenkoAce);
    world_npc_state.npcs[world_npc_state.count++] = make_world_trainer(world_from_tile({5, 22}), EntityDirection::Left, TrainerId::YamenkoLookout);
    world_npc_state.npcs[world_npc_state.count++] = make_world_trainer(world_from_tile({20, 30}), EntityDirection::Down, TrainerId::IppipPyro);

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

        // SANITIY CHECKS
        assert(collidables_count == 0);
        assert(interactables_count == 0);
        assert(!interacting);

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

                // --- GATHER INTERACTABLES ---
                {
                    float offset = 40.0f;
                    Rectangle rect = { runtime_assets.nurse_tent.pos.x - runtime_assets.nurse_tent.size.x * 0.5f + offset, runtime_assets.nurse_tent.pos.y + runtime_assets.nurse_tent.size.y * 0.5f, runtime_assets.nurse_tent.size.x - offset * 2.0f, 40.0f };
                    interactables_push({ Interactable::Nurse, rect });
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
                
                // --- PLAYER COLLISION ---
                move_and_resolve_player(move_x, move_y, delta);
                
                // --- ENCOUNTER ---
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
                if (standing_in_tall_grass && moving && encounter_timer <= 0.0f && rnd_chance(chance_encounter)) {
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

                // --- TILE ANIM ---
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

                // --- TRAINER BATTLE ---
                col_box = player_collision_box();
                for (int idx = 0; idx < (int)world_npc_state.count; idx++) {
                    NpcDef &npc = world_npc_state.npcs[idx];
                    const TrainerDef& trainer = trainer_def(npc.trainer_id);
                    
                    if (npc.battled) continue;

                    Vector2i tile_start = tile_from_world(npc.pos);
                    Vector2i dir = vector2i_from_direction(npc.dir);
                    Vector2i tile_end = tile_start + dir * trainer.sight_tiles;

                    float dist = world_from_tile(distance(tile_start, tile_end));
                    float half_thickness = tile_size_f * 0.5f;
                    Rectangle trainer_hit_box = {
                        npc.pos.x + (dir.x < 0 ? -dist : 0) - (dir.y != 0 ? half_thickness : 0),
                        npc.pos.y + (dir.y < 0 ? -dist : 0) - (dir.x != 0 ? half_thickness : 0),
                        (dir.x != 0) ? dist : tile_size_f,
                        (dir.y != 0) ? dist : tile_size_f
                    };

                    if (rect_intersects(trainer_hit_box, col_box)) {
                        enter_battle(false, idx);
                        break;
                    }
                }

                // --- INTERACTABLES ---
                // Let's figure out if the player is ready for interaction
                // Notice: This only handles all interactions, so be careful.
                Rectangle player_col_box = player_collision_box();
                for (int idx = 0; idx < interactables_count; idx++) {
                    InteractableDef interactable = interactables[idx];
                    bool intersects = rect_intersects(player_col_box, interactable.hitbox);

                    if (intersects) interacting = true;

                    if (intersects && IsKeyPressed(KEY_E)) {
                        switch(interactable.type) {
                            case Interactable::Nurse: {
                                for (int cocomon_idx = 0; cocomon_idx < player_party_count; cocomon_idx++) {
                                    CocomonInstance &cocomon_instance = player_party[cocomon_idx];
                                    cocomon_instance.battler.health = cocomon_instance.battler.max_health;
                                }

                                PlaySound(runtime_assets.sound_kiss);

                                Vector2 start_pos = { player_pos.x, player_pos.y - person_height - 5.0f };
                                for (int i = 0; i < 3; i++) {
                                    float offset_x = rnd_range(-5.0f, 5.0f);
                                    float offset_y = rnd_range(0.0f, 5.0f);
                                    Vector2 pos = { start_pos.x + offset_x, start_pos.y + offset_y };
                                    FloatingHeart heart = { .pos = pos, .radius = 5.0f, .anim_timer = floating_heart_interval, .lifetime = 2.0f, .go_left = rnd_chance(0.5f) };
                                    floating_hearts_push(heart);
                                }
                                
                                break;
                            }
                            default: {
                                debug_break();
                            }
                        }
                    }
                }
                interactables_count = 0;

                // --- FLOATING HEARTS ---
                {
                    int read_idx = 0;
                    int write_idx = 0;
                    for (; read_idx < floating_hearts_count; read_idx++) {
                        FloatingHeart &heart = floating_hearts[read_idx];
                        
                        if ((heart.lifetime -= delta) <= 0.0f) {
                            continue;
                        }
                        
                        heart.pos.y -= heart_speed * delta;
                        heart.pos.x += heart_speed * delta * (heart.go_left ? -1.0f : 1.0f);

                        if ((heart.anim_timer -= delta) <= 0.0f) {
                            heart.anim_timer = floating_heart_interval;
                            heart.go_left = !heart.go_left;
                        }

                        floating_hearts[write_idx++] = heart;
                    }

                    // Update count with what remained
                    floating_hearts_count = write_idx;
                }

                break;
            }
            case GameState::Battle: {
                uint32_t grid_width = 4;
                uint32_t grid_height = 2;
                battle_scene::Callbacks battle_callbacks = {
                    .open_cocomon_list = open_cocomon_list,
                    .finish_battle = finish_battle_and_transition_overworld,
                };

                battle_scene::update((float)delta, battle_callbacks);
                if (game_state_next != GameState::Battle) {
                    break;
                }

                bool battle_busy = battle_scene::is_busy();

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
                            battle_scene::queue_turn_result_playback(result, trainer_encounter_state.opponent_trainer_id);
                            battle_scene::begin_queued_playback(battle_callbacks);
                        }
                    } else {
                        battle::TurnResult result = battle::resolve_player_action(action);
                        battle_scene::queue_turn_result_playback(result, trainer_encounter_state.opponent_trainer_id);
                        battle_scene::begin_queued_playback(battle_callbacks);
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

                if (!cocomon_list_state.forced_selection && IsKeyPressed(KEY_ESCAPE)) {
                    game_state_next = cocomon_list_state.return_state;
                    break;
                }

                if (IsKeyPressed(KEY_ENTER) && player_party_count > 0) {
                    int selected_slot = (int)ui_cursor;
                    if (!player_party_slot_can_battle(selected_slot)) {
                        break;
                    }

                    if (cocomon_list_state.return_state == GameState::Battle) {
                        battle::TurnResult result = battle::resolve_player_switch(selected_slot, cocomon_list_state.forced_selection);
                        if (!result.action_resolved) {
                            break;
                        }

                        game_state_next = GameState::Battle;
                        battle_scene::queue_turn_result_playback(result, trainer_encounter_state.opponent_trainer_id);
                        battle_scene::begin_queued_playback({
                            .open_cocomon_list = open_cocomon_list,
                            .finish_battle = finish_battle_and_transition_overworld,
                        });
                    } else {
                        set_player_active_party_slot(selected_slot);
                        game_state_next = cocomon_list_state.return_state;
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
                draw_state_overworld();
                break;
            }
            case GameState::Battle: {
                battle_scene::draw(
                    {
                        .background = runtime_assets.tex_background_battle_1,
                        .trainer_sprites = runtime_assets.tex_npc,
                    },
                    trainer_encounter_state.opponent_trainer_id
                );
                break;
            }
            case GameState::CocomonList: {
                draw_state_cocomon_list();
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
