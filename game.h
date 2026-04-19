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
inline Vector2i operator+(Vector2i a, Vector2i b) {
    return {
        a.x + b.x,
        a.y + b.y
    };
}
inline Vector2i operator*(Vector2i v, int scalar) {
    return {
        v.x * scalar,
        v.y * scalar
    };
}
inline Vector2i operator*(int scalar, Vector2i v) {
    return {
        v.x * scalar,
        v.y * scalar
    };
}

enum class Npc {
    Nil,
    Yamenko,
    Ippip,
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
    Jokko,
    WakaCaca,
    COUNT,
};

constexpr int max_cocomon_moves = 4;

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
    CocomonElement element;
    int health;
    int max_health;
    int attack;
    int defense;
    int speed;
    CocomonMoveDef moves[max_cocomon_moves];
};

struct CocomonInstance {
    Cocomon species;
    int level;
    int xp;
    CocomonDef battler;
};

struct FloatingHeart {
    Vector2 pos;      // World units
    float radius;     // World units
    float anim_timer; // Seconds
    float lifetime;   // Seconds
    bool go_left;
};

enum class EntityDirection : uint32_t {
    Down,
    Up,
    Right,
    Left
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
    EntityDirection dir; // Notice: All npcs are assumed to have all four directions
    bool battled; 
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

struct Building {
    Texture2D tex;
    Vector2 pos;
    Vector2 size;
};

struct Renderable {
    Texture2D tex;
    Rectangle src;
    Rectangle dst;
    Vector2 origin;
    float rotation;
    float sort_y;
};

enum class Interactable {
    Nurse,
};

struct InteractableDef {
    Interactable type;  
    Rectangle hitbox;
};

struct SweepHit {
    bool hit;
    float time;
    Vector2 normal;
};

constexpr int default_screen_width = 800;
constexpr int default_screen_height = 800;
constexpr int font_size_move = 32;
constexpr int max_player_party = 6;
constexpr int max_cocomons = 32;
constexpr int max_npcs = 32;
constexpr int max_renderables = 4096;
constexpr int max_collidables = 4096;
constexpr int max_interactables = 4096;
constexpr int max_floating_hearts = 4096;
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
extern CocomonInstance player_party[max_player_party];
extern int player_party_count;
extern int player_active_party_slot;
extern CocomonInstance battle_opponent_cocomon;
extern Cocomon player_cocomon_idx;
extern Cocomon opponent_cocomon_idx;
extern bool battle_is_wild_encounter;
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

extern int bobbing;
extern float bobbing_timer;
extern float bobbing_interval;

extern const float chance_encounter;
extern float encounter_timer;
extern float encounter_interval;

void play_music(const char* path);
void state_transition_overworld();
