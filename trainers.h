#pragma once

#include "game.h"

const TrainerDef& trainer_def(TrainerId trainer_id);
const char* trainer_name(TrainerId trainer_id);
NpcDef make_world_trainer(Vector2 pos, EntityDirection dir, TrainerId trainer_id);
