#include "battle_scene.h"
#include "trainers.h"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace
{

    constexpr int battle_playback_capacity = 24;
    constexpr int battle_caption_max_chars = 96;
    constexpr float intro_transition_duration = 0.58f;
    constexpr float trainer_sprite_width = 32.0f;
    constexpr float trainer_sprite_height = 64.0f;

    enum class BattleVisualSide
    {
        None,
        Player,
        Opponent,
    };

    struct BattleBeat
    {
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

    bool has_opponent_trainer(TrainerId opponent_trainer_id)
    {
        return !battle_is_wild_encounter && opponent_trainer_id != TrainerId::Nil;
    }

    CocomonInstance &active_player_cocomon()
    {
        return player_party[player_active_party_slot];
    }

    CocomonInstance &active_opponent_cocomon()
    {
        return battle_opponent_party[battle_opponent_active_party_slot];
    }

    float move_towards_float(float current, float target, float max_delta)
    {
        if (current < target)
        {
            current += max_delta;
            if (current > target)
                current = target;
            return current;
        }

        if (current > target)
        {
            current -= max_delta;
            if (current < target)
                current = target;
        }

        return current;
    }

    void push_beat(const char *caption, float duration, Cocomon attacker = Cocomon::Nil, bool attacker_is_player = false, Cocomon defender = Cocomon::Nil, bool defender_is_player = false, int damage = 0, int defender_health_after = -1, bool trigger_attack = false, bool trigger_opponent_switch = false, int opponent_switch_slot = -1)
    {
        if (battle_beat_count >= battle_playback_capacity)
            return;

        BattleBeat &beat = battle_beats[battle_beat_count];
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

    void push_move_beat(const battle::MoveEvent &move_event)
    {
        if (!move_event.happened)
            return;

        const CocomonDef &attacker = cocomons[(size_t)move_event.attacker];
        const CocomonDef &defender = cocomons[(size_t)move_event.defender];
        const CocomonMoveDef &move = cocomons[(size_t)move_event.attacker].moves[move_event.move_slot];
        char caption[battle_caption_max_chars];

        std::snprintf(caption, sizeof(caption), "%s used %s!", attacker.name, move.name);
        push_beat(caption, 0.75f, move_event.attacker, move_event.attacker_is_player, move_event.defender, move_event.defender_is_player, move_event.damage, move_event.defender_health_after, true);

        switch (move_event.effectiveness)
        {
        case battle::Effectiveness::SuperEffective:
        {
            push_beat("It's super effective!", 0.55f);
            break;
        }
        case battle::Effectiveness::NotVeryEffective:
        {
            push_beat("It's not very effective.", 0.55f);
            break;
        }
        case battle::Effectiveness::Normal:
        {
            break;
        }
        }

        if (move_event.defender_fainted)
        {
            std::snprintf(caption, sizeof(caption), "%s fainted!", defender.name);
            push_beat(caption, 0.75f);
        }
    }

    void push_switch_beat(const battle::TurnResult &result)
    {
        if (!result.player_switched)
            return;

        char caption[battle_caption_max_chars];
        const CocomonDef &switched_to = cocomons[(size_t)result.switched_to];
        std::snprintf(caption, sizeof(caption), "Go, %s!", switched_to.name);
        push_beat(caption, 0.7f);
    }

    void push_opponent_switch_beat(const battle::TurnResult &result, TrainerId opponent_trainer_id)
    {
        if (!result.opponent_switched)
            return;

        char caption[battle_caption_max_chars];
        const CocomonDef &switched_to = cocomons[(size_t)result.opponent_switched_to];

        if (has_opponent_trainer(opponent_trainer_id))
        {
            std::snprintf(caption, sizeof(caption), "%s sent out %s!", trainer_name(opponent_trainer_id), switched_to.name);
        }
        else
        {
            std::snprintf(caption, sizeof(caption), "%s stepped in!", switched_to.name);
        }

        int switched_health = 0;
        if (result.opponent_switch_slot >= 0 && result.opponent_switch_slot < battle_opponent_party_count)
        {
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
            result.opponent_switch_slot);
    }

    void push_cocoball_beats(const battle::TurnResult &result)
    {
        if (result.action.type != battle::ActionType::ThrowCocoball)
            return;

        char caption[battle_caption_max_chars];
        const char *captured_name = cocomons[(size_t)result.captured_species].name;

        switch (result.capture_outcome)
        {
        case battle::CaptureOutcome::BlockedNotWild:
        {
            push_beat("You can only catch wild Cocomon.", 0.85f);
            break;
        }
        case battle::CaptureOutcome::BlockedPartyFull:
        {
            push_beat("Your party is full.", 0.75f);
            break;
        }
        case battle::CaptureOutcome::Failed:
        {
            push_beat("You threw a Cocoball!", 0.55f);
            std::snprintf(caption, sizeof(caption), "%s broke free!", captured_name);
            push_beat(caption, 0.8f);
            break;
        }
        case battle::CaptureOutcome::Caught:
        {
            push_beat("You threw a Cocoball!", 0.55f);
            std::snprintf(caption, sizeof(caption), "Gotcha! %s was caught!", captured_name);
            push_beat(caption, 0.9f);
            std::snprintf(caption, sizeof(caption), "%s joined your party!", captured_name);
            push_beat(caption, 0.85f);
            break;
        }
        case battle::CaptureOutcome::None:
        {
            break;
        }
        }
    }

    void award_victory_experience()
    {
        CocomonInstance &player = active_player_cocomon();
        const CocomonInstance &opponent = active_opponent_cocomon();
        char caption[battle_caption_max_chars];
        char previous_name[sizeof(player.battler.name)] = {};
        int experience_gained = 25 + opponent.level * 15;

        std::snprintf(caption, sizeof(caption), "%s gained %d XP!", player.battler.name, experience_gained);
        push_beat(caption, 0.9f);

        player.xp += experience_gained;
        while (player.xp >= experience_to_next_level(player.level))
        {
            player.xp -= experience_to_next_level(player.level);
            std::strncpy(previous_name, player.battler.name, sizeof(previous_name) - 1);
            previous_name[sizeof(previous_name) - 1] = '\0';
            Cocomon evolved_from = player.species;
            player.level += 1;
            refresh_cocomon_instance_stats(player);
            player_cocomon_idx = player.species;
            battle_player_health_target = (float)player.battler.health;
            battle_player_health_display = battle_player_health_target;

            std::snprintf(caption, sizeof(caption), "%s reached LV %d!", previous_name, player.level);
            push_beat(caption, 0.9f);

            if (player.species != evolved_from)
            {
                std::snprintf(caption, sizeof(caption), "%s evolved into %s!", previous_name, player.battler.name);
                push_beat(caption, 1.0f);
            }
        }
    }

    void resolve_playback_completion(const battle_scene::Callbacks &callbacks)
    {
        if (battle_pending_open_party_menu)
        {
            bool forced_selection = battle_pending_forced_party_menu;
            battle_pending_open_party_menu = false;
            battle_pending_forced_party_menu = false;
            if (callbacks.open_cocomon_list)
            {
                callbacks.open_cocomon_list(GameState::Battle, forced_selection);
            }
            return;
        }

        if (battle_pending_finish_reason != battle::FinishReason::None)
        {
            battle::FinishReason finish_reason = battle_pending_finish_reason;
            bool heal_player_party = battle_pending_party_heal_on_finish;
            battle_pending_finish_reason = battle::FinishReason::None;
            battle_pending_party_heal_on_finish = false;
            if (callbacks.finish_battle)
            {
                callbacks.finish_battle(finish_reason, heal_player_party);
            }
        }
    }

    void start_next_beat(const battle_scene::Callbacks &callbacks)
    {
        if (battle_beat_index >= battle_beat_count)
        {
            resolve_playback_completion(callbacks);
            return;
        }

        const BattleBeat &beat = battle_beats[battle_beat_index];
        battle_beat_index += 1;
        battle_beat_timer = beat.duration;

        std::strncpy(battle_active_caption, beat.caption, battle_caption_max_chars - 1);
        battle_active_caption[battle_caption_max_chars - 1] = '\0';
        battle_active_caption_timer = beat.duration;
        battle_active_caption_duration = beat.duration;

        if (beat.trigger_opponent_switch &&
            beat.opponent_switch_slot >= 0 &&
            beat.opponent_switch_slot < battle_opponent_party_count)
        {
            battle_opponent_active_party_slot = beat.opponent_switch_slot;
            opponent_cocomon_idx = battle_opponent_party[beat.opponent_switch_slot].species;
            battle_opponent_health_display = 0.0f;
        }

        if (beat.trigger_attack && beat.attacker != Cocomon::Nil)
        {
            battle_animating_attack_side = beat.attacker_is_player ? BattleVisualSide::Player : BattleVisualSide::Opponent;
            battle_attack_anim_duration = beat.duration < 0.35f ? beat.duration : 0.35f;
            battle_attack_anim_timer = battle_attack_anim_duration;
        }

        if (beat.damage > 0 && beat.defender != Cocomon::Nil)
        {
            battle_damage_popup_side = beat.defender_is_player ? BattleVisualSide::Player : BattleVisualSide::Opponent;
            battle_damage_popup_amount = beat.damage;
            battle_damage_popup_duration = beat.duration < 0.5f ? beat.duration : 0.5f;
            battle_damage_popup_timer = battle_damage_popup_duration;
        }

        if (beat.defender_health_after >= 0)
        {
            if (beat.defender_is_player)
            {
                battle_player_health_target = (float)beat.defender_health_after;
            }
            else
            {
                battle_opponent_health_target = (float)beat.defender_health_after;
            }
        }
    }

    bool has_active_caption()
    {
        return battle_active_caption_timer > 0.0f && battle_active_caption[0] != '\0';
    }

    float caption_alpha()
    {
        if (!has_active_caption() || battle_active_caption_duration <= 0.0f)
            return 0.0f;

        float elapsed = battle_active_caption_duration - battle_active_caption_timer;
        float fade_in = elapsed / 0.12f;
        float fade_out = battle_active_caption_timer / 0.15f;
        if (fade_in > 1.0f)
            fade_in = 1.0f;
        if (fade_out > 1.0f)
            fade_out = 1.0f;
        return fade_in < fade_out ? fade_in : fade_out;
    }

    float clamp01(float value)
    {
        if (value < 0.0f)
            return 0.0f;
        if (value > 1.0f)
            return 1.0f;
        return value;
    }

    float smoothstep01(float value)
    {
        value = clamp01(value);
        return value * value * (3.0f - 2.0f * value);
    }

    unsigned char alpha_byte(float value)
    {
        if (value < 0.0f)
            value = 0.0f;
        if (value > 255.0f)
            value = 255.0f;
        return (unsigned char)(value + 0.5f);
    }

    Color color_with_alpha(Color color, float alpha)
    {
        color.a = alpha_byte(255.0f * clamp01(alpha));
        return color;
    }

    Color color_lerp(Color from, Color to, float t)
    {
        t = clamp01(t);
        return Color{
            alpha_byte((float)from.r + ((float)to.r - (float)from.r) * t),
            alpha_byte((float)from.g + ((float)to.g - (float)from.g) * t),
            alpha_byte((float)from.b + ((float)to.b - (float)from.b) * t),
            alpha_byte((float)from.a + ((float)to.a - (float)from.a) * t),
        };
    }

    Color battle_element_color(CocomonElement element)
    {
        switch (element)
        {
        case CocomonElement::Grass:
            return Color{111, 214, 126, 255};
        case CocomonElement::Water:
            return Color{98, 191, 255, 255};
        case CocomonElement::Fire:
            return Color{255, 155, 94, 255};
        case CocomonElement::Nil:
        case CocomonElement::COUNT:
            return Color{184, 201, 218, 255};
        }

        return Color{184, 201, 218, 255};
    }

    Color battle_element_panel_color(CocomonElement element)
    {
        return color_lerp(battle_element_color(element), Color{15, 24, 36, 255}, 0.62f);
    }

    Color battle_health_color(float ratio)
    {
        if (ratio <= 0.20f)
            return Color{255, 95, 95, 255};
        if (ratio <= 0.50f)
            return Color{255, 184, 82, 255};
        return Color{95, 220, 126, 255};
    }

    int fit_font_size(const char *text, int max_font_size, int min_font_size, int max_width)
    {
        if (text == nullptr || text[0] == '\0')
            return max_font_size;
        if (max_width <= 0)
            return min_font_size;

        for (int font_size = max_font_size; font_size >= min_font_size; font_size--)
        {
            if (MeasureText(text, font_size) <= max_width)
                return font_size;
        }

        return min_font_size;
    }

    int battle_status_box_width()
    {
        int width = int(screen_width * 0.30f);
        if (width < 236)
            width = 236;
        if (width > 304)
            width = 304;
        return width;
    }

    int battle_status_box_height()
    {
        int height = int(screen_height * 0.085f);
        if (height < 62)
            height = 62;
        if (height > 84)
            height = 84;
        return height;
    }

    Rectangle sprite_bounds(Texture2D texture, Vector2 position, float scale)
    {
        return Rectangle{position.x, position.y, texture.width * scale, texture.height * scale};
    }

    Vector2 rect_center(Rectangle rect)
    {
        return Vector2{rect.x + rect.width * 0.5f, rect.y + rect.height * 0.5f};
    }

    float attack_pulse(BattleVisualSide side)
    {
        if (side != battle_animating_attack_side || battle_attack_anim_duration <= 0.0f)
            return 0.0f;

        float progress = 1.0f - (battle_attack_anim_timer / battle_attack_anim_duration);
        if (progress < 0.0f)
            progress = 0.0f;
        if (progress > 1.0f)
            progress = 1.0f;
        return std::sinf(progress * 3.14159265f);
    }

    Vector2 attack_offset(BattleVisualSide side)
    {
        if (side != battle_animating_attack_side || battle_attack_anim_duration <= 0.0f)
            return {};

        float progress = 1.0f - (battle_attack_anim_timer / battle_attack_anim_duration);
        float swing = std::sinf(progress * 3.14159265f);

        if (side == BattleVisualSide::Player)
        {
            return Vector2{swing * 28.0f, -swing * 12.0f};
        }

        if (side == BattleVisualSide::Opponent)
        {
            return Vector2{-swing * 28.0f, swing * 12.0f};
        }

        return {};
    }

    float battle_idle_wobble()
    {
        if (bobbing_interval <= 0.0f)
            return 0.0f;

        float current_sign = bobbing >= 0 ? 1.0f : -1.0f;
        float progress = 1.0f - (bobbing_timer / bobbing_interval);
        float swing = current_sign * (smoothstep01(progress) * 2.0f - 1.0f);
        return swing * 1.35f;
    }

    float intro_transition_progress()
    {
        if (battle_intro_transition_duration <= 0.0f)
            return 1.0f;

        float progress = 1.0f - (battle_intro_transition_timer / battle_intro_transition_duration);
        if (progress < 0.0f)
            progress = 0.0f;
        if (progress > 1.0f)
            progress = 1.0f;
        return progress;
    }

    bool intro_transition_active()
    {
        return battle_intro_transition_timer > 0.0f && battle_intro_transition_duration > 0.0f;
    }

    void draw_text_with_shadow(const char *text, int x, int y, int font_size, Color color, Color shadow = Color{7, 14, 22, 190})
    {
        DrawText(text, x + 2, y + 2, font_size, shadow);
        DrawText(text, x, y, font_size, color);
    }

    void draw_panel(Rectangle rect, Color fill, Color border, float /*roundness*/ = 0.16f)
    {
        Rectangle shadow = rect;
        shadow.x += 3.0f;
        shadow.y += 3.0f;

        DrawRectangle((int)shadow.x, (int)shadow.y, (int)shadow.width, (int)shadow.height, Color{5, 10, 16, 72});
        DrawRectangle((int)rect.x, (int)rect.y, (int)rect.width, (int)rect.height, fill);
        DrawRectangleLinesEx(rect, 2.0f, border);

        Rectangle inner = Rectangle{rect.x + 2.0f, rect.y + 2.0f, rect.width - 4.0f, rect.height - 4.0f};
        if (inner.width > 0.0f && inner.height > 0.0f)
        {
            DrawRectangleLinesEx(inner, 1.0f, color_with_alpha(border, 0.32f));
        }
    }

    void draw_chip(int x, int y, int padding_x, int height, Color fill, Color border, const char *text, int font_size, Color fg = WHITE)
    {
        int width = MeasureText(text, font_size) + padding_x * 2;
        Rectangle chip = Rectangle{(float)x, (float)y, (float)width, (float)height};
        draw_panel(chip, fill, border, 0.5f);
        draw_text_with_shadow(text, x + padding_x, y + (height - font_size) / 2 - 1, font_size, fg, Color{5, 10, 18, 120});
    }

    void draw_intro_transition()
    {
        if (!intro_transition_active())
            return;

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

        DrawRectangle(0, 0, screen_width, screen_height, Color{9, 16, 28, (unsigned char)(220.0f * veil_alpha)});

        for (int stripe_idx = 0; stripe_idx < 7; stripe_idx++)
        {
            float stripe_progress = progress * 1.15f - stripe_idx * 0.08f;
            if (stripe_progress <= 0.0f)
                continue;

            float stripe_width = width * (0.18f + stripe_idx * 0.018f);
            float stripe_height = 58.0f + stripe_idx * 18.0f;
            float stripe_x = -stripe_width + stripe_progress * (width + stripe_width * 1.6f);
            float stripe_y = height * (0.12f + stripe_idx * 0.11f);
            unsigned char alpha = (unsigned char)(110.0f * (1.0f - progress) * (1.0f - stripe_idx * 0.09f));
            Color stripe_color = (stripe_idx % 2 == 0)
                                     ? Color{84, 214, 255, alpha}
                                     : Color{255, 203, 112, alpha};

            DrawRectanglePro(
                Rectangle{stripe_x, stripe_y, stripe_width, stripe_height},
                Vector2{0.0f, stripe_height * 0.5f},
                -23.0f,
                stripe_color);
        }

        DrawRing(
            Vector2{center_x, center_y},
            shock_radius - 26.0f,
            shock_radius,
            0.0f,
            360.0f,
            96,
            Color{255, 244, 214, (unsigned char)(150.0f * (1.0f - progress))});

        DrawCircleGradient(
            (int)center_x,
            (int)center_y,
            reveal_radius,
            Color{255, 255, 255, (unsigned char)(120.0f * flash)},
            Color{255, 255, 255, 0});

        if (reveal_radius < max_radius)
        {
            DrawRing(
                Vector2{center_x, center_y},
                reveal_radius,
                max_radius + 4.0f,
                0.0f,
                360.0f,
                128,
                Color{9, 16, 28, (unsigned char)(255.0f * (1.0f - progress * 0.45f))});
        }
    }

    void draw_battle_backdrop(const battle_scene::DrawAssets &assets, int action_box_y)
    {
        if (assets.background.id != 0)
        {
            DrawTexturePro(
                assets.background,
                Rectangle{0.0f, 0.0f, (float)assets.background.width, (float)assets.background.height},
                Rectangle{0.0f, 0.0f, (float)screen_width, (float)screen_height},
                Vector2{0.0f, 0.0f},
                0.0f,
                WHITE);
        }
        else
        {
            ClearBackground(Color{223, 239, 233, 255});
        }

        DrawRectangleGradientV(
            0,
            0,
            screen_width,
            action_box_y,
            Color{24, 72, 108, 44},
            Color{255, 255, 255, 0});

        DrawCircleGradient(
            (int)(screen_width * 0.16f),
            (int)(screen_height * 0.18f),
            screen_width * 0.22f,
            Color{255, 242, 184, 118},
            Color{255, 242, 184, 0});

        DrawCircleGradient(
            (int)(screen_width * 0.76f),
            (int)(screen_height * 0.16f),
            screen_width * 0.28f,
            Color{109, 192, 255, 72},
            Color{109, 192, 255, 0});

        DrawRectangleGradientV(
            0,
            action_box_y - 150,
            screen_width,
            220,
            Color{255, 255, 255, 0},
            Color{255, 255, 255, 96});

        DrawRectangleGradientV(
            0,
            action_box_y - 18,
            screen_width,
            screen_height - action_box_y + 18,
            Color{8, 15, 24, 0},
            Color{8, 15, 24, 108});

        for (int band_idx = 0; band_idx < 5; band_idx++)
        {
            int band_y = (int)(screen_height * (0.22f + band_idx * 0.10f));
            int band_height = 20 + band_idx * 8;
            Color band_color = band_idx % 2 == 0
                                   ? Color{255, 255, 255, 14}
                                   : Color{150, 188, 218, 10};

            DrawRectangleGradientH(
                0,
                band_y,
                screen_width,
                band_height,
                band_color,
                Color{band_color.r, band_color.g, band_color.b, 0});
        }
    }

    void draw_ground_platform(Vector2 center, float width, float height, Color accent)
    {
        DrawEllipse((int)center.x, (int)(center.y + 12.0f), width * 0.46f, height * 0.32f, Color{0, 0, 0, 44});
        DrawEllipse((int)center.x, (int)center.y, width * 0.38f, height * 0.24f, Color{255, 255, 255, 28});
        DrawEllipse((int)center.x, (int)center.y, width * 0.34f, height * 0.20f, color_with_alpha(accent, 0.26f));
        DrawEllipseLines((int)center.x, (int)center.y, width * 0.34f, height * 0.20f, color_with_alpha(accent, 0.66f));
        DrawEllipseLines((int)center.x, (int)center.y, width * 0.24f, height * 0.12f, Color{255, 255, 255, 112});
    }

    void draw_attack_pulse(Vector2 center, float radius, Color accent, BattleVisualSide side)
    {
        float pulse = attack_pulse(side);
        if (pulse <= 0.0f)
            return;

        DrawRing(
            center,
            radius * 0.86f,
            radius * (1.02f + pulse * 0.25f),
            0.0f,
            360.0f,
            64,
            color_with_alpha(accent, 0.30f * pulse));

        DrawCircleGradient(
            (int)center.x,
            (int)center.y,
            radius * (0.84f + pulse * 0.12f),
            color_with_alpha(accent, 0.22f * pulse),
            color_with_alpha(accent, 0.0f));
    }

    void draw_sprite_with_outline(Texture2D texture, Vector2 position, float scale, Color tint, Color outline, Color glow, BattleVisualSide side)
    {
        if (texture.id == 0)
            return;

        Rectangle bounds = sprite_bounds(texture, position, scale);
        Vector2 center = rect_center(bounds);
        float glow_radius = (bounds.width > bounds.height ? bounds.width : bounds.height) * 0.48f;

        DrawCircleGradient(
            (int)center.x,
            (int)(center.y - bounds.height * 0.08f),
            glow_radius,
            color_with_alpha(glow, 0.20f),
            color_with_alpha(glow, 0.0f));

        draw_attack_pulse(center, glow_radius * 0.82f, glow, side);

        const Vector2 outline_offsets[] = {
            Vector2{-2.0f, 0.0f},
            Vector2{2.0f, 0.0f},
            Vector2{0.0f, -2.0f},
            Vector2{0.0f, 2.0f},
            Vector2{-1.5f, -1.5f},
            Vector2{1.5f, -1.5f},
            Vector2{-1.5f, 1.5f},
            Vector2{1.5f, 1.5f},
        };

        for (const Vector2 &offset : outline_offsets)
        {
            DrawTextureEx(texture, position + offset, 0.0f, scale, outline);
        }

        DrawTextureEx(texture, position, 0.0f, scale, tint);
    }

    void draw_opponent_trainer(const battle_scene::DrawAssets &assets, TrainerId opponent_trainer_id, int action_box_y)
    {
        if (!has_opponent_trainer(opponent_trainer_id) || assets.trainer_sprites == nullptr)
            return;

        Texture2D trainer_tex = assets.trainer_sprites[(size_t)trainer_def(opponent_trainer_id).sprite];
        if (trainer_tex.id == 0)
            return;

        float progress = intro_transition_active() ? intro_transition_progress() : 1.0f;
        float scale = 3.2f;
        float trainer_width = trainer_sprite_width * scale;
        float trainer_height = trainer_sprite_height * scale;
        float slide_in = (1.0f - progress) * 72.0f;
        float trainer_x = screen_width - trainer_width + slide_in;
        float trainer_y = (float)action_box_y - trainer_height * 1.98f;
        unsigned char alpha = (unsigned char)(205.0f * (0.45f + progress * 0.55f));
        Color tint = Color{255, 255, 255, alpha};

        DrawEllipse(
            (int)(trainer_x + trainer_width * 0.5f),
            (int)(trainer_y + trainer_height - 6.0f),
            trainer_width * 0.26f,
            13.0f,
            Color{0, 0, 0, (unsigned char)(52.0f * progress)});

        DrawCircleGradient(
            (int)(trainer_x + trainer_width * 0.45f),
            (int)(trainer_y + trainer_height * 0.38f),
            trainer_width * 0.46f,
            Color{210, 228, 240, (unsigned char)(55.0f * progress)},
            Color{210, 228, 240, 0});

        const Vector2 outline_offsets[] = {
            Vector2{-2.0f, 0.0f},
            Vector2{2.0f, 0.0f},
            Vector2{0.0f, -2.0f},
            Vector2{0.0f, 2.0f},
        };

        for (const Vector2 &offset : outline_offsets)
        {
            DrawTexturePro(
                trainer_tex,
                Rectangle{(float)EntityDirection::Left * trainer_sprite_width, 0.0f, trainer_sprite_width, trainer_sprite_height},
                Rectangle{trainer_x + offset.x, trainer_y + offset.y, trainer_width, trainer_height},
                Vector2{0.0f, 0.0f},
                0.0f,
                Color{10, 18, 28, (unsigned char)(110.0f * progress)});
        }

        DrawTexturePro(
            trainer_tex,
            Rectangle{(float)EntityDirection::Left * trainer_sprite_width, 0.0f, trainer_sprite_width, trainer_sprite_height},
            Rectangle{trainer_x, trainer_y, trainer_width, trainer_height},
            Vector2{0.0f, 0.0f},
            0.0f,
            tint);

        DrawRectangleGradientV(
            (int)(trainer_x - 8.0f),
            (int)(trainer_y + trainer_height * 0.16f),
            (int)(trainer_width + 16.0f),
            (int)(trainer_height * 0.92f),
            Color{180, 198, 214, 0},
            Color{180, 198, 214, (unsigned char)(90.0f * progress)});

        draw_chip(
            (int)(trainer_x - 6.0f),
            (int)(trainer_y + trainer_height - 22.0f),
            12,
            28,
            Color{12, 20, 32, (unsigned char)(210.0f * progress)},
            Color{162, 182, 204, (unsigned char)(220.0f * progress)},
            trainer_name(opponent_trainer_id),
            16,
            Color{240, 246, 252, (unsigned char)(255.0f * progress)});
    }

    // Draw the active battler card with only the essentials: name, level, type, and HP.
    void ui_draw_cocomon_box(int x, int y, const CocomonInstance &instance, float displayed_health = -1.0f)
    {
        const CocomonDef &cocomon = instance.battler;
        int width = battle_status_box_width();
        int height = battle_status_box_height();
        int padding = 12;
        int header_y = y - 20;
        int shown_health = displayed_health < 0.0f ? cocomon.health : (int)std::ceil(displayed_health);
        float health_ratio = cocomon.max_health > 0 ? (float)shown_health / (float)cocomon.max_health : 0.0f;
        if (health_ratio < 0.0f)
            health_ratio = 0.0f;
        if (health_ratio > 1.0f)
            health_ratio = 1.0f;
        int health_box_width = width - padding * 2;
        int health_box_height = 10;
        int health_bar_y = y + height - 18;
        int health_fill_width = (int)(health_box_width * health_ratio);
        if (health_ratio > 0.0f && health_fill_width < health_box_height)
        {
            health_fill_width = health_box_height;
        }

        Color accent = battle_element_color(cocomon.element);
        Rectangle panel = Rectangle{(float)x, (float)y, (float)width, (float)height};
        draw_panel(panel, Color{12, 21, 33, 178}, Color{132, 148, 170, 206}, 0.15f);

        char level_text[16];
        std::snprintf(level_text, sizeof(level_text), "LV %d", instance.level);
        int level_font_size = 15;
        int level_text_width = MeasureText(level_text, level_font_size);
        int name_font_size = fit_font_size(cocomon.name, width > 280 ? 24 : 20, 14, width - level_text_width - 10);

        draw_text_with_shadow(cocomon.name, x, header_y, name_font_size, WHITE);
        draw_text_with_shadow(level_text, x + width - level_text_width, header_y + 3, level_font_size, color_with_alpha(accent, 0.98f), Color{5, 10, 18, 128});

        const char *element_name = cocomon.element == CocomonElement::Nil
                                       ? "UNKNOWN"
                                       : cocomon_element_names[(size_t)cocomon.element];
        int element_font_size = fit_font_size(element_name, 13, 10, width / 3);
        int element_chip_width = MeasureText(element_name, element_font_size) + 18;

        char text_hp[32];
        std::snprintf(text_hp, sizeof(text_hp), "HP %d / %d", shown_health, cocomon.max_health);
        int hp_font_size = fit_font_size(text_hp, 16, 11, width - padding * 3 - element_chip_width);

        draw_chip(
            x + padding,
            y + 9,
            9,
            18,
            color_with_alpha(battle_element_panel_color(cocomon.element), 0.62f),
            color_with_alpha(accent, 0.80f),
            element_name,
            element_font_size);

        int hp_text_width = MeasureText(text_hp, hp_font_size);
        draw_text_with_shadow(text_hp, x + width - padding - hp_text_width, y + 10, hp_font_size, WHITE);

        DrawRectangle(x + padding, health_bar_y, health_box_width, health_box_height, Color{30, 43, 58, 220});
        if (health_fill_width > 0)
        {
            DrawRectangle(x + padding, health_bar_y, health_fill_width, health_box_height, battle_health_color(health_ratio));
        }
        DrawRectangleLinesEx(
            Rectangle{(float)(x + padding), (float)health_bar_y, (float)health_box_width, (float)health_box_height},
            1.0f,
            Color{162, 178, 196, 198});
    }

    // Draw the opponent's remaining roster as a compact retro HUD strip.
    void ui_draw_trainer_party_indicator(int x, int y, TrainerId opponent_trainer_id)
    {
        if (!has_opponent_trainer(opponent_trainer_id) || battle_opponent_party_count <= 0)
            return;

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

        draw_panel(Rectangle{(float)x, (float)y, (float)width, (float)height}, Color{13, 22, 35, 212}, Color{112, 128, 148, 236}, 0.42f);
        draw_text_with_shadow("TEAM", x + padding_x, label_y, label_font_size, Color{236, 242, 248, 255});

        for (int slot = battle_opponent_party_count - 1; slot >= 0; slot--)
        {
            const CocomonInstance &member = battle_opponent_party[slot];
            bool fainted = member.species == Cocomon::Nil || member.battler.health <= 0;
            bool active = slot == battle_opponent_active_party_slot && !fainted;
            Color fill = fainted ? Color{64, 72, 88, 255} : (active ? Color{255, 214, 102, 255} : Color{126, 214, 255, 255});
            Color outline = fainted ? Color{102, 110, 126, 255} : Color{245, 248, 255, 255};
            int center_x = pip_x - (battle_opponent_party_count - 1 - slot) * (pip_diameter + pip_gap);

            DrawCircle(center_x, pip_center_y, (float)pip_radius, fill);
            DrawCircleLines(center_x, pip_center_y, (float)pip_radius, outline);

            if (active)
            {
                DrawCircleLines(center_x, pip_center_y, (float)pip_radius + 3.0f, Color{255, 244, 214, 220});
            }
        }
    }

    // Draw one move tile, tinting the full card by element so it reads quickly in the grid.
    void ui_draw_action_bar_move(int x, int y, int cell_width, int cell_height, bool selected, const CocomonMoveDef &move)
    {
        bool has_move = move.flags > 0;
        bool out_of_pp = has_move && move.pp == 0;
        int margin = 12;
        bool compact = cell_height < 74 || cell_width < 184;
        Color accent = has_move ? battle_element_color(move.element) : Color{112, 125, 142, 255};
        Color fill = has_move
                         ? color_lerp(accent, Color{10, 17, 28, 232}, 0.42f)
                         : Color{10, 17, 28, 210};
        Color border = selected
                           ? Color{245, 248, 255, 245}
                           : (has_move ? color_with_alpha(accent, 0.96f) : Color{88, 104, 124, 220});

        if (selected)
        {
            fill = color_lerp(fill, accent, 0.22f);
        }
        if (out_of_pp)
        {
            accent = Color{255, 129, 129, 255};
            fill = color_lerp(accent, Color{34, 10, 14, 232}, 0.58f);
            border = selected ? Color{255, 234, 234, 245} : color_with_alpha(accent, 0.96f);
        }

        draw_panel(Rectangle{(float)x, (float)y, (float)cell_width, (float)cell_height}, fill, border, 0.14f);

        const char *title = has_move ? move.name : "Empty";
        int title_font_size = fit_font_size(title, compact ? 19 : (cell_height >= 86 ? 24 : 21), 14, cell_width - margin * 2);

        if (!has_move)
        {
            draw_text_with_shadow(title, x + margin, y + 18, title_font_size, Color{199, 210, 224, 255});
            return;
        }
        else
        {
            draw_text_with_shadow(move.name, x + margin, y + 18, title_font_size, WHITE);
        }

        const char *element_name = move.element == CocomonElement::Nil
                                       ? "UNKNOWN"
                                       : cocomon_element_names[(size_t)move.element];
        int element_font_size = fit_font_size(element_name, compact ? 13 : 14, 11, cell_width - margin * 2);
        draw_text_with_shadow(
            element_name,
            x + margin,
            y + (compact ? 40 : 44),
            element_font_size,
            color_with_alpha(Color{241, 245, 251, 255}, 0.92f));

        char text_pp[32];
        std::snprintf(text_pp, sizeof(text_pp), "PP %d/%d", move.pp, move.pp_max);
        char text_dmg[32];
        std::snprintf(text_dmg, sizeof(text_dmg), "DMG %d", move.dmg);
        int footer_max_width = (cell_width - margin * 2 - 10) / 2;
        int meta_font_size = fit_font_size(text_pp, compact ? 14 : 16, 11, footer_max_width);
        int dmg_font_size = fit_font_size(text_dmg, compact ? 14 : 16, 11, footer_max_width);
        if (dmg_font_size < meta_font_size)
            meta_font_size = dmg_font_size;
        int footer_y = y + cell_height - (compact ? 26 : 30);
        draw_text_with_shadow(text_pp, x + margin, footer_y, meta_font_size, Color{228, 235, 242, 255});

        int dmg_text_width = MeasureText(text_dmg, meta_font_size);
        draw_text_with_shadow(text_dmg, x + cell_width - margin - dmg_text_width, footer_y, meta_font_size, Color{228, 235, 242, 255});

        int pp_bar_width = cell_width - margin * 2;
        int pp_bar_y = y + cell_height - 10;
        DrawRectangle(x + margin, pp_bar_y, pp_bar_width, 6, Color{18, 26, 37, 255});
        float pp_ratio = move.pp_max > 0 ? (float)move.pp / (float)move.pp_max : 0.0f;
        int pp_fill_width = (int)(pp_bar_width * pp_ratio);
        if (pp_fill_width > 0)
        {
            DrawRectangle(x + margin, pp_bar_y, pp_fill_width, 6, Color{245, 248, 255, 220});
        }
        DrawRectangleLinesEx(Rectangle{(float)(x + margin), (float)pp_bar_y, (float)pp_bar_width, 6.0f}, 1.0f, color_with_alpha(border, 0.70f));
    }

    // Draw the four-move command grid for the player's active Cocomon.
    void ui_draw_action_bar_moves(int x, int y, int width, int height)
    {
        draw_panel(Rectangle{(float)x, (float)y, (float)width, (float)height}, Color{11, 19, 30, 20}, Color{78, 94, 114, 188}, 0.10f);

        const CocomonDef &cocomon = active_player_cocomon().battler;
        int gap = 12;
        int padding = 14;
        int cell_width = (width - padding * 2 - gap) / 2;
        int cell_height = (height - padding * 2 - gap) / 2;

        int top_left_x = x + padding;
        int top_left_y = y + padding;
        int top_right_x = top_left_x + cell_width + gap;
        int top_right_y = top_left_y;
        int bot_left_x = top_left_x;
        int bot_left_y = top_left_y + cell_height + gap;
        int bot_right_x = top_right_x;
        int bot_right_y = bot_left_y;

        ui_draw_action_bar_move(top_left_x, top_left_y, cell_width, cell_height, ui_cursor == (uint32_t)BattleUIIndex::AbilityOne, cocomon.moves[0]);
        ui_draw_action_bar_move(top_right_x, top_right_y, cell_width, cell_height, ui_cursor == (uint32_t)BattleUIIndex::AbilityTwo, cocomon.moves[1]);
        ui_draw_action_bar_move(bot_left_x, bot_left_y, cell_width, cell_height, ui_cursor == (uint32_t)BattleUIIndex::AbilityThree, cocomon.moves[2]);
        ui_draw_action_bar_move(bot_right_x, bot_right_y, cell_width, cell_height, ui_cursor == (uint32_t)BattleUIIndex::AbilityFour, cocomon.moves[3]);
    }

    // Draw one tactical command tile for party, ball, or run actions.
    void ui_draw_action_bar_menu_item(int x, int y, int width, int height, bool selected, const char *title, const char *subtitle, Color accent, bool interactive = true)
    {
        int available_width = width - 32;
        int title_font_size = fit_font_size(title, 24, 14, available_width);
        int subtitle_font_size = fit_font_size(subtitle, 16, 10, available_width);
        Color fill = interactive ? Color{15, 24, 37, 232} : Color{11, 17, 28, 205};
        Color border = selected ? accent : Color{88, 104, 124, 220};
        int title_y = y + 18;
        int subtitle_y = title_y + title_font_size + 6;
        if (subtitle_y + subtitle_font_size > y + height - 14)
        {
            subtitle_y = y + height - 14 - subtitle_font_size;
        }

        if (selected)
        {
            fill = color_lerp(fill, accent, 0.18f);
        }

        draw_panel(Rectangle{(float)x, (float)y, (float)width, (float)height}, fill, border, 0.14f);
        DrawRectangle(x + 12, y + 10, width - 24, 4, color_with_alpha(accent, interactive ? 0.95f : 0.26f));

        if (title[0] != '\0')
        {
            draw_text_with_shadow(title, x + 16, title_y, title_font_size, interactive ? WHITE : Color{198, 208, 220, 255});
        }
        if (subtitle[0] != '\0')
        {
            DrawText(subtitle, x + 16, subtitle_y, subtitle_font_size, interactive ? Color{184, 197, 212, 244} : Color{145, 158, 174, 220});
        }
    }

    // Draw the non-move command grid that sits beside the move list.
    void ui_draw_action_bar_menu(int x, int y, int width, int height)
    {
        draw_panel(Rectangle{(float)x, (float)y, (float)width, (float)height}, Color{11, 19, 30, 20}, Color{78, 94, 114, 188}, 0.10f);

        int gap = 12;
        int padding = 14;
        int cell_width = (width - padding * 2 - gap) / 2;
        int cell_height = (height - padding * 2 - gap) / 2;

        int top_left_x = x + padding;
        int top_left_y = y + padding;
        int top_right_x = top_left_x + cell_width + gap;
        int top_right_y = top_left_y;
        int bot_left_x = top_left_x;
        int bot_left_y = top_left_y + cell_height + gap;
        int bot_right_x = top_right_x;
        int bot_right_y = bot_left_y;

        ui_draw_action_bar_menu_item(top_left_x, top_left_y, cell_width, cell_height, ui_cursor == (uint32_t)BattleUIIndex::Cocomon, "COCOMON", "Switch party", Color{118, 216, 194, 255});
        ui_draw_action_bar_menu_item(top_right_x, top_right_y, cell_width, cell_height, ui_cursor == (uint32_t)BattleUIIndex::Cocoball, "COCOBALL", battle_is_wild_encounter ? "Catch wild Cocomon" : "Wild battles only", Color{255, 214, 102, 255});
        ui_draw_action_bar_menu_item(bot_left_x, bot_left_y, cell_width, cell_height, ui_cursor == (uint32_t)BattleUIIndex::Nil, "EMPTY", "No command", Color{110, 122, 138, 255}, false);
        ui_draw_action_bar_menu_item(bot_right_x, bot_right_y, cell_width, cell_height, ui_cursor == (uint32_t)BattleUIIndex::Run, "RUN", battle_is_wild_encounter ? "Escape this battle" : "Blocked by trainer", Color{255, 128, 128, 255});
    }

    // Draw the full battle command footer, split between moves and tactical actions.
    void ui_draw_action_bar(int action_box_height, int action_box_y, bool input_enabled)
    {
        Rectangle bar = Rectangle{12.0f, (float)(action_box_y + 10), (float)(screen_width - 24), (float)(action_box_height - 16)};
        int inner_x = (int)bar.x + 16;
        int header_y = (int)bar.y + 12;
        int section_y = header_y + 36;
        int section_height = (int)(bar.height - 54);
        int section_gap = 12;
        int inner_width = (int)bar.width - 32;
        int moves_width = (int)(inner_width * 0.55f);
        int menu_x = inner_x + moves_width + section_gap;
        int menu_width = inner_width - moves_width - section_gap;
        Color status_color = input_enabled ? Color{206, 223, 240, 255} : Color{255, 214, 102, 255};

        //DrawRectangleGradientV(0, action_box_y - 30, screen_width, 40, Color{8, 15, 24, 0}, Color{8, 15, 24, 122});
        //draw_panel(bar, Color{8, 15, 24, 222}, Color{102, 118, 140, 214}, 0.10f);

        ui_draw_action_bar_moves(inner_x, section_y, moves_width, section_height);
        ui_draw_action_bar_menu(menu_x, section_y, menu_width, section_height);
    }

    // Draw the battle log banner that announces turns, switches, and results.
    void ui_draw_caption_banner(int y, const char *message, float alpha)
    {
        int font_size = fit_font_size(message, 28, 18, screen_width - 96);
        int label_font_size = 16;
        int padding_x = 28;
        int padding_y = 16;
        int text_width = MeasureText(message, font_size);
        int width = text_width + padding_x * 2;
        int height = font_size + padding_y * 2;
        int x = (screen_width - width) / 2;
        Color fg = color_with_alpha(WHITE, alpha);

        draw_panel(
            Rectangle{(float)x, (float)y, (float)width, (float)height},
            Color{10, 18, 28, alpha_byte(230.0f * alpha)},
            Color{174, 195, 217, alpha_byte(255.0f * alpha)},
            0.18f);
        DrawRectangle(x + 16, y + 12, width - 32, 4, Color{255, 214, 102, alpha_byte(255.0f * alpha)});

        draw_text_with_shadow(message, x + padding_x, y + 21, font_size, fg, Color{6, 10, 18, alpha_byte(180.0f * alpha)});
    }

    // Float combat feedback near the target so damage reads immediately.
    void ui_draw_damage_popup(Vector2 player_anchor, Vector2 opponent_anchor)
    {
        if (battle_damage_popup_timer <= 0.0f || battle_damage_popup_side == BattleVisualSide::None || battle_damage_popup_amount <= 0)
            return;

        float progress = 1.0f - (battle_damage_popup_timer / battle_damage_popup_duration);
        float alpha = 1.0f - progress * 0.88f;
        int font_size = 30 + (int)(std::sinf(progress * 3.14159265f) * 6.0f);
        char damage_text[16];
        std::snprintf(damage_text, sizeof(damage_text), "-%d", battle_damage_popup_amount);

        Vector2 anchor = battle_damage_popup_side == BattleVisualSide::Player ? player_anchor : opponent_anchor;
        float rise = 24.0f + progress * 48.0f;
        int damage_width = MeasureText(damage_text, font_size);
        Vector2 position = Vector2{anchor.x - damage_width * 0.5f, anchor.y - rise};

        draw_text_with_shadow(
            damage_text,
            (int)position.x,
            (int)position.y,
            font_size,
            Color{255, 224, 120, alpha_byte(255.0f * alpha)},
            Color{96, 38, 12, alpha_byte(220.0f * alpha)});
    }

} // namespace

namespace battle_scene
{

    void clear_playback()
    {
        for (int beat_idx = 0; beat_idx < battle_playback_capacity; beat_idx++)
        {
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

    void reset_health_display()
    {
        battle_player_health_display = (float)active_player_cocomon().battler.health;
        battle_player_health_target = battle_player_health_display;
        battle_opponent_health_display = (float)active_opponent_cocomon().battler.health;
        battle_opponent_health_target = battle_opponent_health_display;
    }

    void start_intro_transition()
    {
        battle_intro_transition_duration = intro_transition_duration;
        battle_intro_transition_timer = battle_intro_transition_duration;
    }

    void queue_intro_playback(TrainerId opponent_trainer_id)
    {
        char caption[battle_caption_max_chars];

        if (battle_is_wild_encounter)
        {
            std::snprintf(caption, sizeof(caption), "A wild %s appeared!", active_opponent_cocomon().battler.name);
            push_beat(caption, 0.9f);
        }
        else if (has_opponent_trainer(opponent_trainer_id))
        {
            std::snprintf(caption, sizeof(caption), "%s wants to battle!", trainer_name(opponent_trainer_id));
            push_beat(caption, 0.9f);

            std::snprintf(caption, sizeof(caption), "%s sent out %s!", trainer_name(opponent_trainer_id), active_opponent_cocomon().battler.name);
            push_beat(caption, 0.8f);
        }
        else
        {
            std::snprintf(caption, sizeof(caption), "%s wants to battle!", active_opponent_cocomon().battler.name);
            push_beat(caption, 0.9f);
        }

        std::snprintf(caption, sizeof(caption), "Go, %s!", active_player_cocomon().battler.name);
        push_beat(caption, 0.75f);
    }

    void queue_turn_result_playback(const battle::TurnResult &result, TrainerId opponent_trainer_id)
    {
        clear_playback();
        battle_pending_finish_reason = result.finish_reason;
        battle_pending_open_party_menu = result.party_switch_required;
        battle_pending_forced_party_menu = result.party_switch_required;
        battle_pending_party_heal_on_finish = result.finish_reason == battle::FinishReason::OpponentWon;

        if (result.player_switched)
        {
            battle_player_health_display = (float)active_player_cocomon().battler.health;
            battle_player_health_target = battle_player_health_display;
        }

        if (result.action.type == battle::ActionType::ThrowCocoball)
        {
            push_cocoball_beats(result);
        }
        else if (!result.action_resolved)
        {
            switch (result.action.type)
            {
            case battle::ActionType::UseMove:
            {
                push_beat("That move can't be used.", 0.8f);
                break;
            }
            case battle::ActionType::OpenCocomonMenu:
            {
                push_beat("No other Cocomon can battle.", 0.85f);
                break;
            }
            case battle::ActionType::RunAway:
            {
                push_beat("You can't run from a trainer battle.", 0.95f);
                break;
            }
            case battle::ActionType::ThrowCocoball:
            case battle::ActionType::None:
            {
                break;
            }
            }
        }

        push_switch_beat(result);
        push_move_beat(result.first_move);
        push_move_beat(result.second_move);
        push_opponent_switch_beat(result, opponent_trainer_id);

        switch (result.finish_reason)
        {
        case battle::FinishReason::PlayerWon:
        {
            push_beat("You won the battle!", 0.9f);
            award_victory_experience();
            break;
        }
        case battle::FinishReason::OpponentWon:
        {
            push_beat("You lost the battle!", 0.9f);
            break;
        }
        case battle::FinishReason::PlayerRanAway:
        {
            push_beat("You ran away!", 0.7f);
            break;
        }
        case battle::FinishReason::PlayerCapturedOpponent:
        case battle::FinishReason::None:
        {
            break;
        }
        }
    }

    void begin_queued_playback(const Callbacks &callbacks)
    {
        if (battle_beat_count > 0)
        {
            start_next_beat(callbacks);
            return;
        }

        resolve_playback_completion(callbacks);
    }

    void update(float delta, const Callbacks &callbacks)
    {
        float health_delta = 140.0f * delta;
        battle_player_health_display = move_towards_float(battle_player_health_display, battle_player_health_target, health_delta);
        battle_opponent_health_display = move_towards_float(battle_opponent_health_display, battle_opponent_health_target, health_delta);

        if (battle_active_caption_timer > 0.0f)
        {
            battle_active_caption_timer -= delta;
            if (battle_active_caption_timer < 0.0f)
                battle_active_caption_timer = 0.0f;
        }

        if (battle_attack_anim_timer > 0.0f)
        {
            battle_attack_anim_timer -= delta;
            if (battle_attack_anim_timer <= 0.0f)
            {
                battle_attack_anim_timer = 0.0f;
                battle_animating_attack_side = BattleVisualSide::None;
            }
        }

        if (battle_damage_popup_timer > 0.0f)
        {
            battle_damage_popup_timer -= delta;
            if (battle_damage_popup_timer <= 0.0f)
            {
                battle_damage_popup_timer = 0.0f;
                battle_damage_popup_side = BattleVisualSide::None;
                battle_damage_popup_amount = 0;
            }
        }

        if (intro_transition_active())
        {
            battle_intro_transition_timer -= delta;
            if (battle_intro_transition_timer < 0.0f)
            {
                battle_intro_transition_timer = 0.0f;
            }
        }

        if (battle_beat_timer > 0.0f)
        {
            battle_beat_timer -= delta;
            if (battle_beat_timer > 0.0f)
                return;
        }

        if (battle_beat_index < battle_beat_count)
        {
            start_next_beat(callbacks);
            return;
        }

        resolve_playback_completion(callbacks);
    }

    bool is_busy()
    {
        return battle_beat_timer > 0.0f || battle_beat_index < battle_beat_count;
    }

    void draw(const DrawAssets &assets, TrainerId opponent_trainer_id)
    {
        int cocomon_status_box_width = battle_status_box_width();
        int cocomon_status_box_height = battle_status_box_height();
        int action_box_height = 250;

        int action_box_y = screen_height - action_box_height;
        float opponent_x = has_opponent_trainer(opponent_trainer_id) ? screen_width * 0.35f : screen_width * 0.48f;
        float opponent_scale = 7.0f;
        float player_scale = 8.0f;

        Texture2D opponent_texture = tex_cocomon_fronts[(size_t)opponent_cocomon_idx];
        Texture2D player_texture = tex_cocomon_backs[(size_t)player_cocomon_idx];

        Vector2 opponent_attack = attack_offset(BattleVisualSide::Opponent);
        Vector2 player_attack = attack_offset(BattleVisualSide::Player);
        float wobble = battle_idle_wobble();

        Vector2 opponent_pos = Vector2{
            opponent_x + opponent_attack.x,
            34.0f + wobble + opponent_attack.y};
        Vector2 player_pos = Vector2{
            screen_width * 0.08f + player_attack.x,
            (float)action_box_y - (player_texture.height * player_scale) * 0.78f - wobble + player_attack.y};

        Rectangle opponent_bounds = sprite_bounds(opponent_texture, opponent_pos, opponent_scale);
        Rectangle player_bounds = sprite_bounds(player_texture, player_pos, player_scale);
        Vector2 opponent_center = rect_center(opponent_bounds);
        Vector2 player_center = rect_center(player_bounds);

        draw_battle_backdrop(assets, action_box_y);

        Color opponent_accent = battle_element_color(active_opponent_cocomon().battler.element);
        Color player_accent = battle_element_color(active_player_cocomon().battler.element);

        draw_ground_platform(
            Vector2{opponent_center.x, opponent_bounds.y + opponent_bounds.height * 0.96f},
            opponent_bounds.width * 0.82f,
            82.0f,
            opponent_accent);
        draw_ground_platform(
            Vector2{player_center.x, player_bounds.y + player_bounds.height * 0.92f},
            player_bounds.width * 0.78f,
            94.0f,
            player_accent);

        int opponent_cocomon_box_x = 18;
        int opponent_cocomon_box_y = 48;
        ui_draw_cocomon_box(opponent_cocomon_box_x, opponent_cocomon_box_y, active_opponent_cocomon(), battle_opponent_health_display);
        ui_draw_trainer_party_indicator(opponent_cocomon_box_x + 12, opponent_cocomon_box_y + cocomon_status_box_height + 12, opponent_trainer_id);

        draw_opponent_trainer(assets, opponent_trainer_id, action_box_y);

        draw_sprite_with_outline(
            opponent_texture,
            opponent_pos,
            opponent_scale,
            WHITE,
            Color{6, 12, 20, 185},
            opponent_accent,
            BattleVisualSide::Opponent);

        int player_cocomon_box_x = screen_width - cocomon_status_box_width - 12;
        int player_cocomon_box_y = action_box_y - cocomon_status_box_height + 40;
        ui_draw_cocomon_box(player_cocomon_box_x, player_cocomon_box_y, active_player_cocomon(), battle_player_health_display);

        draw_sprite_with_outline(
            player_texture,
            player_pos,
            player_scale,
            WHITE,
            Color{6, 12, 20, 185},
            player_accent,
            BattleVisualSide::Player);

        ui_draw_action_bar(action_box_height, action_box_y, !is_busy());
        ui_draw_damage_popup(
            Vector2{player_center.x, player_bounds.y + player_bounds.height * 0.18f},
            Vector2{opponent_center.x, opponent_bounds.y + opponent_bounds.height * 0.10f});
        if (has_active_caption())
        {
            ui_draw_caption_banner(opponent_cocomon_box_y + cocomon_status_box_height + 26, battle_active_caption, caption_alpha());
        }
        draw_intro_transition();
    }

} // namespace battle_scene
