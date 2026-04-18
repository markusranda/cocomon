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

int screen_width = default_screen_width;
int screen_height = default_screen_height;
extern const Color color_surface_0 = Color{ 130, 130, 130, 250 };
extern const Color color_surface_1 = Color{ 150, 150, 150, 250 };
extern const Color color_surface_2 = Color{ 170, 170, 170, 250 };
extern const Color color_surface_3 = Color{ 190, 190, 190, 250 };
extern const Color color_primary   = Color{ 104, 185, 199, 250 };

// =====================================================================================================================
// STATE
// =====================================================================================================================

MoveKey key_stack[4];
int key_count = 0;
char cocomon_element_names[(size_t)CocomonElement::COUNT][32];
CocomonMoveDef cocomon_moves[(size_t)CocomonMove::COUNT];
CocomonDef cocomons[max_cocomons];
CocomonDef cocomon_defaults[max_cocomons];
Texture2D tex_cocomon_fronts[max_cocomons];
Texture2D tex_cocomon_backs[max_cocomons];
Texture2D tex_world_entities[(size_t)WorldEntity::COUNT];
Cocomon player_cocomon_idx = Cocomon::LocoMoco;
Cocomon opponent_cocomon_idx = Cocomon::FrickaFlow;
GameState game_state = GameState::Overworld;
GameState game_state_next = GameState::Overworld;
uint32_t ui_cursor = 0; // Each scene understands what this means.
Music current_music_stream = {};
bool music_loaded = false;
WorldEntity world[world_height][world_width] = { };

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
float player_width = 32.0f;
float player_height = 64.0f;

// --- BOBBING ---
int bobbing = 6;
float bobbing_timer = 0.0f;
float bobbing_interval = 0.4f;

// --- ENOUNTER ---
extern const float chance_encounter = 0.005f;
float encounter_timer = 0.0f;
float encounter_interval = 1.0f;

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

Vector2i tile_from_world(Vector2 world) {
    Vector2i result;

    result.x = (int)floorf(world.x / tile_size_f);
    result.y = (int)floorf(world.y / tile_size_f);

    return result;
}

Vector2 world_from_tile(Vector2i tile) {
    Vector2 result;

    result.x = tile.x * tile_size_f;
    result.y = tile.y * tile_size_f;

    return result;
}

Rectangle ui_draw_cocomon_box(int x, int y, const CocomonDef& cocomon) {
    int width = int(screen_width * 0.35f);
    int height = int(screen_height * 0.13f);
    int health_box_width = width - 30;
    int health_box_height = (height * 0.15f);
    int name_font_size = 34;
    int stats_font_size = 18;
    float health_ratio = (float)cocomon.health / (float)cocomon.max_health;
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
    snprintf(text_hp, sizeof(text_hp), "HP %d/%d", cocomon.health, cocomon.max_health);
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

    CocomonDef cocomon = cocomons[(size_t)player_cocomon_idx];
    
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
    reset_keys();
    stop_current_music();
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

    tex_cocomon_backs[(size_t)Cocomon::LocoMoco] = LoadTexture("sprites/locomoco_back.png");
    tex_cocomon_backs[(size_t)Cocomon::FrickaFlow] = LoadTexture("sprites/fricka_flow_back.png");
    tex_cocomon_backs[(size_t)Cocomon::Molly] = LoadTexture("sprites/molly_back.png");

    tex_world_entities[(size_t)WorldEntity::Grass] = LoadTexture("sprites/grass_tile.png");
    tex_world_entities[(size_t)WorldEntity::GrassTall] = LoadTexture("sprites/grass_tile_tall.png");

    strcpy(cocomon_element_names[(size_t)CocomonElement::Grass], "GRASS");
    strcpy(cocomon_element_names[(size_t)CocomonElement::Fire], "FIRE");
    strcpy(cocomon_element_names[(size_t)CocomonElement::Water], "WATER");

    cocomon_moves[(size_t)CocomonMove::Ember] = { "EMBER", 30, 30, 30, CocomonElement::Fire, 1 };
    cocomon_moves[(size_t)CocomonMove::WaterGun] = { "WATER GUN", 30, 30, 30, CocomonElement::Water, 1 };
    cocomon_moves[(size_t)CocomonMove::LeafBlade] = { "LEAF BLADE", 30, 30, 30, CocomonElement::Grass, 1 };

    cocomon_defaults[(size_t)Cocomon::LocoMoco] = {
        .name = "LOCOMOCO",
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
        .health = 90,
        .max_health = 90,
        .attack = 15,
        .defense = 8,
        .speed = 20,
        .moves = { cocomon_moves[(size_t)CocomonMove::LeafBlade] }
    };
    cocomon_defaults[(size_t)Cocomon::Molly] = {
        .name = "MOLLY",
        .health = 110,
        .max_health = 110,
        .attack = 16,
        .defense = 14,
        .speed = 10,
        .moves = { cocomon_moves[(size_t)CocomonMove::WaterGun] }
    };

    cocomons[(size_t)Cocomon::LocoMoco] = cocomon_defaults[(size_t)Cocomon::LocoMoco];
    cocomons[(size_t)Cocomon::FrickaFlow] = cocomon_defaults[(size_t)Cocomon::FrickaFlow];
    cocomons[(size_t)Cocomon::Molly] = cocomon_defaults[(size_t)Cocomon::Molly];

    // --- WORLD SETUP ---
    for(int y = 0; y < 8; y++) {
        for(int x = 0; x < 8; x++) {
            world[y + 10][x + 10] = WorldEntity::GrassTall;
        }
    }

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
        if (IsKeyPressed(KEY_F2)) state_transition_battle();
        if (IsKeyPressed(KEY_ONE)) player_speed -= 100.0f;
        if (IsKeyPressed(KEY_TWO)) player_speed += 100.0f;

        // Game state update
        switch (game_state) {
            case GameState::Overworld: {
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
                Rectangle col_box = player_collision_box(); 
                float half_width  = col_box.width * 0.5f;
                float max_x = world_width * tile_size_f;
                float max_y = world_height * tile_size_f;
                if (col_box.x < 0.0f)
                    player_pos.x = 0.0f + half_width;

                if (col_box.y < 0.0f)
                    player_pos.y = 0.0f + col_box.height;

                if (col_box.x + col_box.width > max_x)
                    player_pos.x = max_x - half_width;

                if (col_box.y + col_box.height > max_y)
                    player_pos.y = max_y;

                bool moving = move_x != 0.0f || move_y != 0.0f;
                bool standing_in_tall_grass = false;
                for(int y = 0; y < world_height; y++) {
                    for(int x = 0; x < world_width; x++) {
                        Vector2i tile_tile = { x, y };
                        Vector2 tile_world = world_from_tile(tile_tile);
                        WorldEntity tile_entity = world[y][x];
                        Rectangle tile_rect = { tile_world.x, tile_world.y, tile_size_i, tile_size_i };
                        if (rect_intersects(tile_rect, col_box) && tile_entity == WorldEntity::GrassTall) {
                            standing_in_tall_grass = true;
                        }
                    }
                }

                // --- ENCOUNTER ---
                if (standing_in_tall_grass && moving && encounter_timer <= 0.0f && chance(chance_encounter)) {
                    state_transition_battle();
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

                break;
            }
            case GameState::Battle: {
                uint32_t grid_width = 4;
                uint32_t grid_height = 2;

                // Right
                if (IsKeyPressed(KEY_RIGHT) && (ui_cursor % grid_width) < grid_width - 1) ui_cursor += 1;
                // Left
                if (IsKeyPressed(KEY_LEFT) && (ui_cursor % grid_width) > 0) ui_cursor -= 1;
                // Down
                if (IsKeyPressed(KEY_DOWN) && (ui_cursor / grid_width) < grid_height - 1) ui_cursor += grid_width;
                // Up
                if (IsKeyPressed(KEY_UP) && (ui_cursor / grid_width) > 0) ui_cursor -= grid_width;

                if (IsKeyPressed(KEY_ENTER)) {
                    BattleUIIndex selected_index = (BattleUIIndex)ui_cursor;
                    int move_slot = battle_move_slot_from_cursor(selected_index);

                    if (move_slot >= 0) {
                        battle_player_attack(move_slot);
                    } else {
                        switch (selected_index) {
                            case BattleUIIndex::Cocomon: {
                                break;
                            }
                            case BattleUIIndex::Cocoball: {
                                break;
                            }
                            case BattleUIIndex::Nil: {
                                break;
                            }
                            case BattleUIIndex::Run: {
                                state_transition_overworld();
                                break;
                            }
                            default: {
                                debug_break();
                            }
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

                        WorldEntity world_entity = world[y][x];
                        Texture2D tex = tex_world_entities[(size_t)world_entity];
                        Rectangle src = { 0.0f, 0.0f, tile_size_f, tile_size_f };
                        Rectangle dst = { (float)world_x, (float)world_y, tile_size_f, tile_size_f };
                        Vector2 origin = { 0.0f, 0.0f };

                        DrawTexturePro(tex, src, dst, origin, 0.0f, WHITE);
                    }
                }

                // Draw player
                Rectangle src = { 
                    player_frame * player_width, 
                    (int)player_animation_row * player_height, 
                    player_width, 
                    player_height,
                };
                Rectangle dst = { player_pos.x, player_pos.y, player_width, player_height };
                Vector2 origin = { player_width * 0.5f, player_height };

                // Rectangle col_box = player_collision_box(); 
                // DrawRectangle(col_box.x, col_box.y, col_box.width, col_box.height, BLUE);
                DrawTexturePro(tex_player, src, dst, origin, 0.0f, WHITE);

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
                ui_draw_cocomon_box(15, 15, cocomons[(size_t)opponent_cocomon_idx]);
        
                // Opponent cocomon
                DrawTextureEx(tex_cocomon_fronts[(size_t)opponent_cocomon_idx], Vector2{screen_width * 0.5f, float(opponent_cocomon_box_y - -bobbing)}, 0.0f, 12.0f, WHITE);
        
                // Player box
                int player_cocomon_box_x = screen_width - cocomon_status_box_width - 15;
                int player_cocomon_box_y = action_box_y - cocomon_status_box_height - 15;
                ui_draw_cocomon_box(player_cocomon_box_x, player_cocomon_box_y, cocomons[(size_t)player_cocomon_idx]);
                
                // Player cocomon
                Texture2D tex_player_cocomon_back = tex_cocomon_backs[(size_t)player_cocomon_idx];
                DrawTextureEx(tex_player_cocomon_back, Vector2{(float)50, float(action_box_y - (tex_player_cocomon_back.height * 14.0f) * 0.75f - bobbing)}, 0.0f, 14.0f, WHITE);

                // Action bar
                ui_draw_action_bar(action_box_height, action_box_y);
                break;
            }
            case GameState::CocomonList: {

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
