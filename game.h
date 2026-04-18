#pragma once

#include "raylib.h"
#include <cstddef>
#include <cstdint>

// OVERLOADS
inline Vector2 operator*(float scalar, Vector2 v) { return { v.x * scalar, v.y * scalar }; }
inline Vector2 operator*(Vector2 v, float scalar) { return { v.x * scalar, v.y * scalar }; }
inline Vector2 operator/(Vector2 v, float scalar) { return { v.x / scalar, v.y / scalar }; }
inline Vector2 operator+(Vector2 a, Vector2 b) { return { a.x + b.x, a.y + b.y }; }
inline Vector2 operator+(Vector2 a, float b) { return { a.x + b, a.y + b }; }
inline Vector2 operator-(Vector2 a, float b) { return { a.x - b, a.y - b }; }
inline Vector2 operator-(Vector2 a, Vector2 b) { return { a.x - b.x, a.y - b.y }; }
inline Vector2& operator+=(Vector2& a, Vector2 b) { 
    a.x += b.x;
    a.y += b.y;
    return a;
}
inline Vector2& operator-=(Vector2& a, Vector2 b) {
    a.x -= b.x;
    a.y -= b.y;
    return a;
}

struct Vector2i {
    int x;
    int y;
};

static inline bool operator==(Vector2i a, Vector2i b) { return (a.x == b.x) && (a.y == b.y); }
static inline bool operator!=(Vector2i a, Vector2i b) { return !(a == b); }

enum class Npc {
    Nil,
    Yamenko,
    COUNT,
};

enum class MoveKey {
    Nil,
    W,
    A,
    S,
    D
};

enum class GameState {
    Overworld,
    Battle,
    CocomonList,
};

enum class CocomonElement {
    Nil,
    Grass,
    Water,
    Fire,
    COUNT,
};

enum class CocomonMove {
    Nil,
    LeafBlade,
    WaterGun,
    Ember,
    COUNT,
};

enum class Cocomon {
    Nil,
    LocoMoco,
    FrickaFlow,
    Molly,
    COUNT,
};

struct CocomonMoveDef {
    char name[32];
    uint32_t pp;
    uint32_t pp_max;
    uint32_t dmg;
    CocomonElement element;
    uint32_t flags;
};

struct CocomonDef {
    char name[32];
    int health;
    int max_health;
    int attack;
    int defense;
    int speed;
    CocomonMoveDef moves[4];
};

enum class PlayerAnimState {
    IdleDown,
    IdleUp,
    IdleRight,
    IdleLeft,
    RunDown,
    RunUp,
    RunRight,
    RunLeft,
};

enum class WorldEntity {
    Grass,
    GrassTall,
    WallGrassEnd,
    WallGrassLine,
    WallGrassBend,
    WallGrassT,
    WallGrassX,
    Water,
    COUNT,
};

struct WorldEntityDef {
    WorldEntity entity;
    float       rot;
    int         frame_count;
    int         frame_index;
};

struct NpcDef {
    Npc npc;
    Vector2 pos;
    int dir; // Notice: All npcs are assumed to have all four directions
};

enum class BattleUIIndex : uint32_t {
    AbilityOne   = 0,
    AbilityTwo   = 1,
    Cocomon      = 2,
    Cocoball     = 3,
    AbilityThree = 4,
    AbilityFour  = 5,
    Nil          = 6,
    Run          = 7,
};

constexpr int default_screen_width = 800;
constexpr int default_screen_height = 800;
constexpr int font_size_move = 32;
constexpr int max_cocomons = 32;
constexpr int world_width = 64;
constexpr int world_height = 64;
constexpr int tile_size_i = 32;
constexpr float tile_size_f = 32.0f;

extern int screen_width;
extern int screen_height;
extern const Color color_surface_0;
extern const Color color_surface_1;
extern const Color color_surface_2;
extern const Color color_surface_3;
extern const Color color_primary;

extern MoveKey key_stack[4];
extern int key_count;
extern char cocomon_element_names[(size_t)CocomonElement::COUNT][32];
extern CocomonMoveDef cocomon_moves[(size_t)CocomonMove::COUNT];
extern CocomonDef cocomons[max_cocomons];
extern CocomonDef cocomon_defaults[max_cocomons];
extern Texture2D tex_cocomon_fronts[max_cocomons];
extern Texture2D tex_cocomon_backs[max_cocomons];
extern Texture2D tex_world_entities[(size_t)WorldEntity::COUNT];
extern Cocomon player_cocomon_idx;
extern Cocomon opponent_cocomon_idx;
extern GameState game_state;
extern GameState game_state_next;
extern uint32_t ui_cursor;
extern Music current_music_stream;
extern bool music_loaded;
extern WorldEntityDef world[world_height][world_width];

extern Texture2D tex_player;
extern Vector2 player_pos;
extern float player_speed;
extern int player_frame;
extern float player_anim_timer;
extern int player_frames_per_row;
extern float player_frame_interval;
extern PlayerAnimState last_player_animation_row;
extern PlayerAnimState player_animation_row;
extern float player_width;
extern float player_height;

extern int bobbing;
extern float bobbing_timer;
extern float bobbing_interval;

extern const float chance_encounter;
extern float encounter_timer;
extern float encounter_interval;

void play_music(const char* path);
void state_transition_overworld();
