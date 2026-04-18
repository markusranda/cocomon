#include "raylib.h"
#include <assert.h>
#include <cstdint>
#include <stdio.h>
#include <string.h>
#include <cmath>

// =====================================================================================================================
// DATASTRUCTURES
// =====================================================================================================================

enum MoveKey {
    MOVE_NONE,
    MOVE_W,
    MOVE_A,
    MOVE_S,
    MOVE_D
};

enum class GameState {
    Overworld,
    Battle,
};

enum class CocomonElement {
    Nil,
    Grass, // Beats water
    Water, // Beats fire
    Fire,  // Beats grass
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

// =====================================================================================================================
// CONSTS
// =====================================================================================================================

const int screen_width = 1024;
const int screen_height = 1024;
const Color color_surface_0 = Color{ 130, 130, 130, 250 };
const Color color_surface_1 = Color{ 150, 150, 150, 250 };
const Color color_surface_2 = Color{ 170, 170, 170, 250 };
const Color color_surface_3 = Color{ 190, 190, 190, 250 };
const Color color_primary   = Color{ 104, 185, 199, 250 };
const int font_size_move = 32;
const int max_cocomons = 32;
const int world_width = 64;
const int world_height = 64;

// These two should match
const int   tile_size_i = 32;
const float tile_size_f = 32.0f;

// =====================================================================================================================
// STATE
// =====================================================================================================================

MoveKey key_stack[4];
int key_count = 0;

char cocomon_element_names[(size_t)CocomonElement::COUNT][32];
CocomonMoveDef cocomon_moves[(size_t)CocomonMove::COUNT];
CocomonDef cocomons[max_cocomons];
Texture2D tex_grass_tile;

Texture2D tex_cocomon_fronts[max_cocomons];
Texture2D tex_cocomon_backs[max_cocomons];
Cocomon player_cocomon_idx = Cocomon::LocoMoco;
Cocomon opponent_cocomon_idx = Cocomon::FrickaFlow;
GameState game_state = GameState::Overworld;
uint32_t ui_cursor = 0; // Each scene understands what this means.
Music current_music_stream;
int world[world_height][world_width] = { 0 };

// --- PLAYER ---
Texture2D tex_player;
Vector2 player_pos = { world_width * tile_size_f * 0.5f, world_height * tile_size_f * 0.5f };
float player_speed = 200.0f; // pixels per second
int player_frame = 0;
float player_anim_timer = 0.0f;
int player_frames_per_row = 4;
float player_frame_interval = 0.2f;
PlayerAnimState last_player_animation_row = PlayerAnimState::IdleDown;
PlayerAnimState player_animation_row = PlayerAnimState::IdleDown;

// =====================================================================================================================
// METHODS
// =====================================================================================================================

Rectangle ui_draw_cocomon_box(int x, int y, const char* cocomon_name) {
    int width = int(screen_width * 0.35f);
    int height = int(screen_height * 0.1f);
    int health_box_width = width - 30;
    int health_box_height = (height * 0.15f);
    int font_size = 34;

    DrawRectangle(x, y, width, height, GRAY);
    DrawText(cocomon_name, x + 5, y + 5, font_size, WHITE);
    DrawRectangle(x + 15, y + 5 + font_size, health_box_width, health_box_height, GREEN);

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
        ui_draw_action_bar_move(top_left_x, top_left_y, cell_width, cell_height, ui_cursor == 0, cocomon.moves[0]);
    }
    // Top-right
    if (cocomon.moves[1].flags > 0) {
        ui_draw_action_bar_move(top_right_x, top_right_y, cell_width, cell_height, ui_cursor == 1, cocomon.moves[1]);
    }
    // Bottom-left
    if (cocomon.moves[2].flags > 0) {
        ui_draw_action_bar_move(bot_left_x, bot_left_y, cell_width, cell_height, ui_cursor == 4, cocomon.moves[2]);
    }
    // Bottom-right
    if (cocomon.moves[3].flags > 0) {
        ui_draw_action_bar_move(bot_right_x, bot_right_y, cell_width, cell_height, ui_cursor == 5, cocomon.moves[3]);
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

    ui_draw_action_bar_menu_item(top_left_x, top_left_y, cell_width, cell_height, ui_cursor == 2, "COCOMON");
    ui_draw_action_bar_menu_item(top_right_x, top_right_y, cell_width, cell_height, ui_cursor == 3, "COCOBALL");
    ui_draw_action_bar_menu_item(bot_left_x, bot_left_y, cell_width, cell_height, ui_cursor == 6, "");
    ui_draw_action_bar_menu_item(bot_right_x, bot_right_y, cell_width, cell_height, ui_cursor == 7, "RUN");
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

void state_transition_overworld() {
    game_state = GameState::Overworld;

    current_music_stream = Music{};
}

void state_transition_battle() {
    game_state = GameState::Battle;
    ui_cursor = 0;

    Music music_battle_anthem = LoadMusicStream("songs/battle_anthem.mp3");
    SetMusicVolume(music_battle_anthem, 0.15f);
    assert(music_battle_anthem.frameCount > 0);
    PlayMusicStream(music_battle_anthem);

    current_music_stream = music_battle_anthem;
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

// =====================================================================================================================
// MAIN
// =====================================================================================================================

int main(void) {
    InitWindow(screen_width, screen_height, "raylib 2D");
    InitAudioDevice();

    tex_grass_tile = LoadTexture("sprites/grass_tile.png");
    tex_player = LoadTexture("sprites/player_sprites.png");

    tex_cocomon_fronts[(size_t)Cocomon::LocoMoco] = LoadTexture("sprites/locomoco_front.png");
    tex_cocomon_fronts[(size_t)Cocomon::FrickaFlow] = LoadTexture("sprites/fricka_flow_front.png");
    tex_cocomon_fronts[(size_t)Cocomon::Molly] = LoadTexture("sprites/molly_front.png");

    tex_cocomon_backs[(size_t)Cocomon::LocoMoco] = LoadTexture("sprites/locomoco_back.png");
    tex_cocomon_backs[(size_t)Cocomon::FrickaFlow] = LoadTexture("sprites/fricka_flow_back.png");
    tex_cocomon_backs[(size_t)Cocomon::Molly] = LoadTexture("sprites/molly_back.png");

    strcpy(cocomon_element_names[(size_t)CocomonElement::Grass], "GRASS");
    strcpy(cocomon_element_names[(size_t)CocomonElement::Fire], "FIRE");
    strcpy(cocomon_element_names[(size_t)CocomonElement::Water], "WATER");

    cocomon_moves[(size_t)CocomonMove::Ember] = { "EMBER", 30, 30, 30, CocomonElement::Fire, 1 };
    cocomon_moves[(size_t)CocomonMove::WaterGun] = { "WATER GUN", 30, 30, 30, CocomonElement::Water, 1 };
    cocomon_moves[(size_t)CocomonMove::LeafBlade] = { "LEAF BLADE", 30, 30, 30, CocomonElement::Grass, 1 };

    cocomons[(size_t)Cocomon::LocoMoco] = { .name = "LOCOMOCO", .health = 100, .moves = { cocomon_moves[(size_t)CocomonMove::Ember], cocomon_moves[(size_t)CocomonMove::Ember], cocomon_moves[(size_t)CocomonMove::Ember], cocomon_moves[(size_t)CocomonMove::Ember] }};
    cocomons[(size_t)Cocomon::FrickaFlow] = { .name = "FRICKA FLOW", .health = 100, .moves = { cocomon_moves[(size_t)CocomonMove::LeafBlade] }};
    cocomons[(size_t)Cocomon::Molly] = { .name = "MOLLY", .health = 100, .moves = { cocomon_moves[(size_t)CocomonMove::WaterGun] }};

    // --- CAMERA SETUP ---
    Camera2D camera = { 0 };
    camera.target = player_pos;
    camera.offset = Vector2{ screen_width * 0.5f, screen_height * 0.5f };
    camera.rotation = 0.0f;
    camera.zoom = 3.0f;

    int bobbing = 6;
    float bobbing_timer = 0.0f;
    float bobbing_interval = 0.4f;

    SetTargetFPS(60);

    // Main loop
    while (!WindowShouldClose()) {
        double delta = GetFrameTime();
        
        // DEBUG ONLY
        if (IsKeyPressed(KEY_F1)) state_transition_overworld();
        if (IsKeyPressed(KEY_F2)) state_transition_battle();
        
        // Game state update
        switch (game_state) {
            case GameState::Overworld: {
                // --- PLAYER MOVEMENT ---
                float move_x = 0.0f;
                float move_y = 0.0f;

                if (IsKeyPressed(KEY_W)) push_move_key(MOVE_W);
                if (IsKeyPressed(KEY_A)) push_move_key(MOVE_A);
                if (IsKeyPressed(KEY_S)) push_move_key(MOVE_S);
                if (IsKeyPressed(KEY_D)) push_move_key(MOVE_D);

                if (IsKeyReleased(KEY_W)) remove_move_key(MOVE_W);
                if (IsKeyReleased(KEY_A)) remove_move_key(MOVE_A);
                if (IsKeyReleased(KEY_S)) remove_move_key(MOVE_S);
                if (IsKeyReleased(KEY_D)) remove_move_key(MOVE_D);

                if (key_count > 0) {
                    MoveKey key = key_stack[key_count - 1];

                    if (key == MOVE_W) move_y = -1.0f;
                    if (key == MOVE_S) move_y =  1.0f;
                    if (key == MOVE_A) move_x = -1.0f;
                    if (key == MOVE_D) move_x =  1.0f;
                }

                player_pos.x += float(move_x * player_speed) * delta;
                player_pos.y += float(move_y * player_speed) * delta;

                // --- PLAYER COLLISION ---
                if (player_pos.x < 0) player_pos.x = 0;
                if (player_pos.y < 0) player_pos.y = 0;
                float max_x = world_width * tile_size_f;
                float max_y = world_height * tile_size_f;
                if (player_pos.x > max_x) player_pos.x = max_x;
                if (player_pos.y > max_y) player_pos.y = max_y;

                // --- PLAYER CAMERA ---
                float lerp_factor = 10.0f * delta;
                camera.target.x += (player_pos.x - camera.target.x) * lerp_factor;
                camera.target.y += (player_pos.y - camera.target.y) * lerp_factor;

                // --- PLAYER ANIM ---
                // idle rows
                last_player_animation_row = player_animation_row;
                if (move_x != 0.0f || move_y != 0.0f) {
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
                

                if (bobbing_timer <= 0) {
                    bobbing *= -1;
                    bobbing_timer = bobbing_interval;
                }
                bobbing_timer -= delta;

                break;
            }
            default: {
                __debugbreak();
            }
        };
        
        // Common update
        UpdateMusicStream(current_music_stream);

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

                        DrawTexturePro(
                            tex_grass_tile, 
                            Rectangle{(float)world_x, (float)world_y, tile_size_f, tile_size_f}, 
                            Rectangle{(float)world_x, (float)world_y, tile_size_f, tile_size_f}, 
                            { 0 }, 
                            0.0f, 
                            WHITE
                        );
                    }
                }

                // Draw player
                float player_frame_width = 32.0f;
                float player_frame_height = 64.0f;
                Rectangle src = { 
                    player_frame * player_frame_width, 
                    (int)player_animation_row * player_frame_height, 
                    player_frame_width, 
                    player_frame_height,
                };
                Rectangle dst = { player_pos.x, player_pos.y, player_frame_width, player_frame_height };
                Vector2 origin = { player_frame_width * 0.5f, player_frame_height * 0.5f };
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
                ui_draw_cocomon_box(15, 15, cocomons[(size_t)opponent_cocomon_idx].name);
        
                // Opponent cocomon
                DrawTextureEx(tex_cocomon_fronts[(size_t)opponent_cocomon_idx], Vector2{screen_width * 0.5f, float(opponent_cocomon_box_y - -bobbing)}, 0.0f, 12.0f, WHITE);
        
                // Player box
                int player_cocomon_box_x = screen_width - cocomon_status_box_width - 15;
                int player_cocomon_box_y = action_box_y - cocomon_status_box_height - 15;
                ui_draw_cocomon_box(player_cocomon_box_x, player_cocomon_box_y, cocomons[(size_t)player_cocomon_idx].name);
                
                // Player cocomon
                Texture2D tex_player_cocomon_back = tex_cocomon_backs[(size_t)player_cocomon_idx];
                DrawTextureEx(tex_player_cocomon_back, Vector2{(float)50, float(action_box_y - (tex_player_cocomon_back.height * 14.0f) * 0.75f - bobbing)}, 0.0f, 14.0f, WHITE);

                // Action bar
                ui_draw_action_bar(action_box_height, action_box_y);

                break;
            }
            default: {
                __debugbreak();
            }
        }

        EndDrawing();


        // --- FPS IN WINDOW TITLE ---
        int fps = (int)(1.0f / delta);
        if (fps > 1000) fps = 1000;
        char title[64];
        sprintf(title, "cocomon | FPS: %d", fps);

        SetWindowTitle(title);
    }

    // De-initialization
    CloseWindow();

    return 0;
}