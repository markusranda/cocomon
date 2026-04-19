#include "trainers.h"
#include <array>
#include <cstring>
#include <initializer_list>

namespace {

const char* npc_sprite_name(Npc npc) {
    switch (npc) {
        case Npc::Yamenko: return "YAMENKO";
        case Npc::Ippip: return "IPPIP";
        case Npc::Nil:
        case Npc::COUNT:
        default: return "TRAINER";
    }
}

TrainerDef make_trainer(const char* name, Npc sprite, int sight_tiles, std::initializer_list<TrainerPartyMemberDef> party_members) {
    TrainerDef trainer = {};
    trainer.sprite = sprite;
    trainer.sight_tiles = sight_tiles;

    const char* resolved_name = (name != nullptr && name[0] != '\0') ? name : npc_sprite_name(sprite);
    std::strncpy(trainer.name, resolved_name, sizeof(trainer.name) - 1);
    trainer.name[sizeof(trainer.name) - 1] = '\0';

    for (const TrainerPartyMemberDef& party_member : party_members) {
        if (trainer.party_count >= max_party_size) break;
        if (party_member.species == Cocomon::Nil) continue;
        trainer.party[trainer.party_count++] = party_member;
    }

    return trainer;
}

const std::array<TrainerDef, (size_t)TrainerId::COUNT> trainer_defs = []() {
    std::array<TrainerDef, (size_t)TrainerId::COUNT> defs = {};

    defs[(size_t)TrainerId::YamenkoScout] = make_trainer("YAMENKO SCOUT", Npc::Yamenko, 8, {
        { Cocomon::Jokko, 4 },
        { Cocomon::Molly, 4 },
    });
    defs[(size_t)TrainerId::YamenkoMixer] = make_trainer("YAMENKO MIXER", Npc::Yamenko, 8, {
        { Cocomon::Caca, 5 },
        { Cocomon::FrickaFlow, 4 },
    });
    defs[(size_t)TrainerId::YamenkoAce] = make_trainer("YAMENKO ACE", Npc::Yamenko, 8, {
        { Cocomon::FrickaFlow, 6 },
        { Cocomon::Jokko, 5 },
        { Cocomon::Molly, 5 },
    });
    defs[(size_t)TrainerId::YamenkoLookout] = make_trainer("YAMENKO LOOKOUT", Npc::Yamenko, 8, {
        { Cocomon::Molly, 4 },
        { Cocomon::Caca, 4 },
    });
    defs[(size_t)TrainerId::IppipPyro] = make_trainer("IPPIP PYRO", Npc::Ippip, 6, {
        { Cocomon::LocoMoco, 6 },
        { Cocomon::Jokko, 5 },
        { Cocomon::Caca, 5 },
    });

    return defs;
}();

} // namespace

const TrainerDef& trainer_def(TrainerId trainer_id) {
    size_t index = (size_t)trainer_id;
    if (index >= trainer_defs.size()) {
        index = (size_t)TrainerId::Nil;
    }

    return trainer_defs[index];
}

const char* trainer_name(TrainerId trainer_id) {
    const TrainerDef& trainer = trainer_def(trainer_id);
    if (trainer.name[0] != '\0') return trainer.name;
    return npc_sprite_name(trainer.sprite);
}

NpcDef make_world_trainer(Vector2 pos, EntityDirection dir, TrainerId trainer_id) {
    return NpcDef{
        .pos = pos,
        .dir = dir,
        .trainer_id = trainer_id,
        .battled = false,
    };
}
