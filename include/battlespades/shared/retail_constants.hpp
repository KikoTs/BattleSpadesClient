// GENERATED FILE - DO NOT HAND-EDIT.
//
// Produced from retail_constants.json by tools/generate_retail_constants.mjs.
// The JSON is authoritative: it was extracted by executing the retail
// Ace of Spades: Battle Builder shared/constants.py and aoslib/weapons/*.py
// with real Python 2.7. Regenerate with:
//   node tools/generate_retail_constants.mjs <retail_constants.json> <output.hpp>
// with <output.hpp> = include/battlespades/shared/retail_constants.hpp
#pragma once

#include <array>
#include <cstdint>

// NOLINTBEGIN
// clang-format off

namespace battlespades::retail {

// -------------------------------------------------------------------------
// Named scalar constants (retail shared/constants.py). Non-numeric values
// (strings, lists, dicts, booleans) are intentionally omitted; structured
// data lives in the classes/tools/weapons/teams sections below.
// -------------------------------------------------------------------------

inline constexpr std::int64_t ACH_BLOCK_DESTROY_REGION = 2;
inline constexpr std::int64_t ACH_CENTRE = 2;
inline constexpr std::int64_t ACH_CUBES = 5;
inline constexpr std::int64_t ACH_DEBUG = 0;
inline constexpr std::int64_t ACH_ID = 0;
inline constexpr std::int64_t ACH_JUMP_REGION = 3;
inline constexpr std::int64_t ACH_KILLS = 6;
inline constexpr std::int64_t ACH_KILL_REGION = 1;
inline constexpr std::int64_t ACH_REGIONS = 1;
inline constexpr std::int64_t ACH_TEAM = 4;
inline constexpr std::int64_t ACH_TYPE = 1;
inline constexpr std::int64_t ACH_WEAPON = 7;
inline constexpr std::int64_t ACH_W_H_D = 3;
inline constexpr std::int64_t ACTION_BUILD = 0;
inline constexpr std::int64_t ACTION_DESTROY = 1;
inline constexpr std::int64_t ACTION_GRENADE = 3;
inline constexpr std::int64_t ACTION_SPADE = 2;
inline constexpr std::int64_t AIRSTRIKE_DAMAGE = 18;
inline constexpr std::int64_t AIRSTRIKE_ENTITY = 17;
inline constexpr std::int64_t AIRSTRIKE_EXPLOSION_BLOCK_DAMAGE = 15;
inline constexpr std::int64_t AIRSTRIKE_EXPLOSION_DAMAGE = 400;
inline constexpr std::int64_t AIRSTRIKE_EXPLOSION_KNOCKBACK_MAX = 2;
inline constexpr std::int64_t AIRSTRIKE_EXPLOSION_KNOCKBACK_MIN = 1;
inline constexpr std::int64_t AIRSTRIKE_EXPLOSION_RADIUS = 6;
inline constexpr std::int64_t AIRSTRIKE_FLYBY_SOUND_ID = 10;
inline constexpr std::int64_t AIRSTRIKE_FLYBY_SPACE_SOUND_ID = 11;
inline constexpr std::int64_t AIRSTRIKE_GRAVITY_MULTIPLIER = 100;
inline constexpr std::int64_t AIRSTRIKE_KILL = 16;
inline constexpr std::int64_t AIRSTRIKE_SHELL_SPEED = 100;
inline constexpr std::int64_t AIRSTRIKE_SIREN_ONESHOT_SOUND_ID = 9;
inline constexpr std::int64_t ALCATRAZ_TIME_SCORE = 181;
inline constexpr std::int64_t ALPHA_BLEND_MODE_ADDITIVE = 2;
inline constexpr std::int64_t ALPHA_BLEND_MODE_BLEND = 1;
inline constexpr std::int64_t ALPHA_BLEND_MODE_NONE = 0;
inline constexpr std::int64_t ALPHA_BLEND_MODE_SUBTRACTIVE = 3;
inline constexpr std::int64_t ALWAYS_CROSSHAIR = 3;
inline constexpr double AMBIENCE_FADE_AMOUNT = 0.03;
inline constexpr std::int64_t AMMO_CRATE = 3;
inline constexpr std::int64_t AMMO_DROP_POINT_ENTITY = 18;
inline constexpr std::int64_t ANCIENT_EGYPT_TIME_SCORE = 162;
inline constexpr std::int64_t ANTIPERSONNEL_GRENADE_ACCURACY_SPREAD_INCREASE_PER_SHOT = 0;
inline constexpr std::int64_t ANTIPERSONNEL_GRENADE_ACCURACY_SPREAD_INITIAL = 6;
inline constexpr std::int64_t ANTIPERSONNEL_GRENADE_ACCURACY_SPREAD_RANGE = 0;
inline constexpr std::int64_t ANTIPERSONNEL_GRENADE_ACCURACY_SPREAD_REDUCTION_SPEED = 0;
inline constexpr std::int64_t ANTIPERSONNEL_GRENADE_DAMAGE = 23;
inline constexpr std::int64_t ANTIPERSONNEL_GRENADE_EXPLOSION_BLAST_WAVE_RADIUS = 6;
inline constexpr double ANTIPERSONNEL_GRENADE_EXPLOSION_BLOCK_DAMAGE = 0.5;
inline constexpr std::int64_t ANTIPERSONNEL_GRENADE_EXPLOSION_DAMAGE = 500;
inline constexpr double ANTIPERSONNEL_GRENADE_EXPLOSION_FUSE = 2.5;
inline constexpr double ANTIPERSONNEL_GRENADE_EXPLOSION_KNOCKBACK_MAX = 0.5;
inline constexpr double ANTIPERSONNEL_GRENADE_EXPLOSION_KNOCKBACK_MIN = 0.25;
inline constexpr std::int64_t ANTIPERSONNEL_GRENADE_EXPLOSION_RADIUS = 2;
inline constexpr std::int64_t ANTIPERSONNEL_GRENADE_INITIAL_STOCK = 2;
inline constexpr std::int64_t ANTIPERSONNEL_GRENADE_KILL = 23;
inline constexpr std::int64_t ANTIPERSONNEL_GRENADE_RESTOCK_AMOUNT = 4;
inline constexpr double ANTIPERSONNEL_GRENADE_SHOOT_INTERVAL = 0.5;
inline constexpr std::int64_t ANTIPERSONNEL_GRENADE_STOCK = 4;
inline constexpr std::int64_t ANTIPERSONNEL_GRENADE_THROW_MIN_SPEED = 25;
inline constexpr std::int64_t ANTIPERSONNEL_GRENADE_THROW_SPEED = 50;
inline constexpr std::int64_t ANTIPERSONNEL_GRENADE_TOOL = 32;
inline constexpr std::int64_t ARCTIC_BASE_TIME_SCORE = 163;
inline constexpr std::int64_t ARMS_PITCH_MAXIMUM = 70;
inline constexpr std::int64_t ARMS_PITCH_MINIMUM = -90;
inline constexpr std::int64_t ASSAULT_RIFLE_TOOL = 60;
inline constexpr std::int64_t ATTEMPT_PLAYER_PICKUP_RATE = 10;
inline constexpr std::int64_t AUTOMATIC_PISTOL_TOOL = 53;
inline constexpr std::int64_t AUTO_SHOTGUN_TOOL = 62;
inline constexpr std::int64_t BACK_ORIENTATION_PITCH = 2;
inline constexpr std::int64_t BAN_NONE = 0;
inline constexpr std::int64_t BAN_PERMANENT = 2;
inline constexpr std::int64_t BAN_TEMPORARY = 1;
inline constexpr std::int64_t BASE = 1;
inline constexpr double BASE_PLAYER_ZONE_DISTANCE_TOLERANCE_XY = 0.3;
inline constexpr std::int64_t BASE_PLAYER_ZONE_DISTANCE_TOLERANCE_ZEYES = 0;
inline constexpr double BASE_PLAYER_ZONE_DISTANCE_TOLERANCE_ZFEET = -0.1;
inline constexpr double BASE_ZONE_DISTANCE_TOLERANCE = 0.5;
inline constexpr std::int64_t BASE_ZONE_TINT_ALPHA = 150;
inline constexpr std::int64_t BIGGEST_COLLAPSING_OBJECT = 12;
inline constexpr std::int64_t BIGGEST_KILL_STREAK = 11;
inline constexpr std::int64_t BLOCKFIRE = 28;
inline constexpr std::int64_t BLOCKFIRE_BLOCKS_TO_ATTEMPT_TO_LIGHT = -1;
inline constexpr double BLOCKFIRE_BLOCK_DAMAGE = 0.7;
inline constexpr double BLOCKFIRE_BLOCK_DAMAGE_TIMER = 0.4;
inline constexpr double BLOCKFIRE_CHARACTER_DAMAGE = 2.5;
inline constexpr double BLOCKFIRE_CHARACTER_DAMAGE_TIMER = 0.3;
inline constexpr std::int64_t BLOCKFIRE_CHARACTER_DURATION = 10;
inline constexpr std::int64_t BLOCKFIRE_CHARACTER_SPREAD_RANGE = 3;
inline constexpr std::int64_t BLOCKFIRE_DAMAGE = 25;
inline constexpr std::int64_t BLOCKFIRE_INITIAL_SPREAD_RADIUS = 2;
inline constexpr std::int64_t BLOCKFIRE_KILL = 25;
inline constexpr std::int64_t BLOCKFIRE_LIGHT_RADIUS = 3;
inline constexpr std::int64_t BLOCKFIRE_MAX_FALLING_DISTANCE = 1;
inline constexpr std::int64_t BLOCKFIRE_MAX_LIFESPAN = 4;
inline constexpr double BLOCKFIRE_MAX_RANDOM_CHANCE = 0.3;
inline constexpr std::int64_t BLOCKFIRE_SMOKE_GENERATION_MAX_RATE = 2;
inline constexpr double BLOCKFIRE_SMOKE_GENERATION_MAX_VELOCITY = 0.1;
inline constexpr std::int64_t BLOCKFIRE_SMOKE_GENERATION_MIN_RATE = 1;
inline constexpr std::int64_t BLOCKFIRE_SMOKE_GENERATION_MIN_VELOCITY = 0;
inline constexpr std::int64_t BLOCKFIRE_SMOKE_GENERATION_PARTICLE_DECAY = -1;
inline constexpr std::int64_t BLOCKFIRE_SMOKE_GENERATION_PARTICLE_LIFESPAN = 3;
inline constexpr std::int64_t BLOCKFIRE_SMOKE_GENERATION_PARTICLE_MAX_SIZE = 8;
inline constexpr std::int64_t BLOCKFIRE_SMOKE_GENERATION_PARTICLE_MIN_SIZE = 4;
inline constexpr std::int64_t BLOCKFIRE_SPREAD_COUNT = 5;
inline constexpr std::int64_t BLOCKFIRE_SPREAD_RADIUS = 2;
inline constexpr double BLOCKFIRE_SPREAD_TIMER = 0.5;
inline constexpr std::int64_t BLOCK_CRATE = 5;
inline constexpr std::int64_t BLOCK_CRATE_DROP_POINT_ENTITY = 20;
inline constexpr std::int64_t BLOCK_NESS_TIME_SCORE = 172;
inline constexpr std::int64_t BLOCK_SMOKE_TRAIL_INITIAL_ROTATION_RANDOM_MAX = 200;
inline constexpr std::int64_t BLOCK_SMOKE_TRAIL_INITIAL_ROTATION_RANDOM_MIN = 160;
inline constexpr std::int64_t BLOCK_SMOKE_TRAIL_INITIAL_SIZE_RANDOM_MAX = 6;
inline constexpr std::int64_t BLOCK_SMOKE_TRAIL_INITIAL_SIZE_RANDOM_MIN = 3;
inline constexpr std::int64_t BLOCK_SMOKE_TRAIL_LIFETIME = 1;
inline constexpr std::int64_t BLOCK_SUCKER_DAMAGE = 42;
inline constexpr std::int64_t BLOCK_SUCKER_TOOL = 63;
inline constexpr std::int64_t BLOCK_TOOL = 5;
inline constexpr std::int64_t BLOCK_TYPE_PREFAB = 0;
inline constexpr std::int64_t BLOCK_TYPE_SNOW = 1;
inline constexpr double BODY_PARTS_SIZE = 0.05;
inline constexpr double BODY_PART_ARMS_CROUCH_Z = 0.4;
inline constexpr double BODY_PART_LEG_CROUCH_Y = -0.3;
inline constexpr std::int64_t BOMB_DAMAGE = 19;
inline constexpr std::int64_t BOMB_DROP_SOUND_ID = 22;
inline constexpr std::int64_t BOMB_EXPLODE_SOUND_ID = 18;
inline constexpr std::int64_t BOMB_EXPLODE_WATER_SOUND_ID = 17;
inline constexpr std::int64_t BOMB_EXPLOSION_BLOCK_DAMAGE = 20;
inline constexpr std::int64_t BOMB_EXPLOSION_DAMAGE = 500;
inline constexpr std::int64_t BOMB_EXPLOSION_FUSE = 10;
inline constexpr std::int64_t BOMB_EXPLOSION_KNOCKBACK_MAX = 3;
inline constexpr std::int64_t BOMB_EXPLOSION_KNOCKBACK_MIN = 2;
inline constexpr std::int64_t BOMB_EXPLOSION_RADIUS = 7;
inline constexpr std::int64_t BOMB_KILL = 17;
inline constexpr std::int64_t BOMB_PICKUP = 14;
inline constexpr std::int64_t BOMB_PICKUP_SOUND_ID = 16;
inline constexpr double BOMB_SMOKE_GENERATION_MAX_VELOCITY = 0.05;
inline constexpr double BOMB_SMOKE_GENERATION_MIN_VELOCITY = 0.05;
inline constexpr std::int64_t BOMB_SMOKE_GENERATION_PARTICLE_DECAY = -1;
inline constexpr std::int64_t BOMB_SMOKE_GENERATION_PARTICLE_LIFESPAN = 1;
inline constexpr std::int64_t BOMB_SMOKE_GENERATION_PARTICLE_MAX_SIZE = 1;
inline constexpr double BOMB_SMOKE_GENERATION_PARTICLE_MIN_SIZE = 0.5;
inline constexpr std::int64_t BOMB_SMOKE_GENERATION_RATE = 25;
inline constexpr double BOMB_SMOKE_X_OFFSET = -0.05;
inline constexpr double BOMB_SMOKE_Y_OFFSET = -0.05;
inline constexpr double BOMB_SMOKE_Z_OFFSET = -1.2;
inline constexpr std::int64_t BOMB_THROW_SPEED = 10;
inline constexpr std::int64_t BOMB_TOOL = 25;
inline constexpr std::int64_t BOMB_TOOL_SMOKE_FP_FORWARD_OFFSET = 0;
inline constexpr std::int64_t BOMB_TOOL_SMOKE_FP_RIGHT_OFFSET = 0;
inline constexpr double BOMB_TOOL_SMOKE_FP_UP_OFFSET = 0.45;
inline constexpr double BOMB_TOOL_SMOKE_TP_FORWARD_OFFSET = 0.82;
inline constexpr double BOMB_TOOL_SMOKE_TP_RIGHT_OFFSET = 0.37;
inline constexpr double BOMB_TOOL_SMOKE_TP_UP_OFFSET = 0.45;
inline constexpr std::int64_t BRAN_CASTLE_TIME_SCORE = 164;
inline constexpr std::int64_t BUILD_DYNAMITE_SOUND_ID = 21;
inline constexpr std::int64_t BUILD_LANDMINE_SOUND_ID = 31;
inline constexpr std::int64_t BUILD_SOUND_ID = 46;
inline constexpr std::int64_t BUILD_UGC_SOUND_ID = 45;
inline constexpr double BURN_INDICATOR_TIME = 1.2;
inline constexpr std::int64_t C4_DAMAGE = 41;
inline constexpr std::int64_t C4_KILL = 36;
inline constexpr std::int64_t C4_TOOL = 59;
inline constexpr std::int64_t CAPTURE_POINT_DISTANCE = 3;
inline constexpr std::int64_t CAPTURE_POINT_ENTITY = 25;
inline constexpr std::int64_t CAPTURE_POINT_REFILL_TIME = 10;
inline constexpr std::int64_t CASTLE_WARS_TIME_SCORE = 173;
inline constexpr std::int64_t CCTF_MODE_SCORE_REASON = 191;
inline constexpr std::int64_t CHARACTER_DRAW_RANGE = 192;
inline constexpr double CHARACTER_MOLOTOV_SMOKE_GENERATION_MAX_VELOCITY = 0.1;
inline constexpr std::int64_t CHARACTER_MOLOTOV_SMOKE_GENERATION_MIN_VELOCITY = 0;
inline constexpr std::int64_t CHARACTER_MOLOTOV_SMOKE_GENERATION_PARTICLE_DECAY = -1;
inline constexpr std::int64_t CHARACTER_MOLOTOV_SMOKE_GENERATION_PARTICLE_LIFESPAN = 1;
inline constexpr std::int64_t CHARACTER_MOLOTOV_SMOKE_GENERATION_PARTICLE_MAX_SIZE = 5;
inline constexpr std::int64_t CHARACTER_MOLOTOV_SMOKE_GENERATION_PARTICLE_MIN_SIZE = 2;
inline constexpr double CHARACTER_MOLOTOV_SMOKE_GENERATION_RATE = 0.02;
inline constexpr std::int64_t CHASE_CAMERA = 1;
inline constexpr std::int64_t CHAT_ALL = 0;
inline constexpr std::int64_t CHAT_BIG = 3;
inline constexpr std::int64_t CHAT_BUFFER_SIZE = 50;
inline constexpr std::int64_t CHAT_SHOWN_LINES = 10;
inline constexpr std::int64_t CHAT_SYSTEM = 2;
inline constexpr std::int64_t CHAT_TEAM = 1;
inline constexpr std::int64_t CHEMICALBOMB_KILL = 31;
inline constexpr std::int64_t CHEMICALBOMB_TOOL = 54;
inline constexpr std::int64_t CITY_OF_CHICAGO_TIME_SCORE = 180;
inline constexpr std::int64_t CLASSIC = 1;
inline constexpr std::int64_t CLASSIC_CTF_BASE_CAPTURE_DISTANCE = 5;
inline constexpr std::int64_t CLASSIC_CTF_INTEL_MIN_RADIUS_FROM_BASE = 3;
inline constexpr std::int64_t CLASSIC_GAME_LENGTH = 2700;
inline constexpr std::int64_t CLASSIC_GRENADE_ACCURACY_SPREAD_INCREASE_PER_SHOT = 0;
inline constexpr std::int64_t CLASSIC_GRENADE_ACCURACY_SPREAD_INITIAL = 6;
inline constexpr std::int64_t CLASSIC_GRENADE_ACCURACY_SPREAD_RANGE = 0;
inline constexpr std::int64_t CLASSIC_GRENADE_ACCURACY_SPREAD_REDUCTION_SPEED = 0;
inline constexpr std::int64_t CLASSIC_GRENADE_DAMAGE = 22;
inline constexpr std::int64_t CLASSIC_GRENADE_EXPLOSION_BLAST_WAVE_RADIUS = 9;
inline constexpr std::int64_t CLASSIC_GRENADE_EXPLOSION_BLOCK_DAMAGE = 15;
inline constexpr std::int64_t CLASSIC_GRENADE_EXPLOSION_DAMAGE = 130;
inline constexpr std::int64_t CLASSIC_GRENADE_EXPLOSION_FUSE = 3;
inline constexpr double CLASSIC_GRENADE_EXPLOSION_KNOCKBACK_MAX = 0.1;
inline constexpr double CLASSIC_GRENADE_EXPLOSION_KNOCKBACK_MIN = 0.1;
inline constexpr std::int64_t CLASSIC_GRENADE_EXPLOSION_RADIUS = 2;
inline constexpr std::int64_t CLASSIC_GRENADE_INITIAL_STOCK = 2;
inline constexpr std::int64_t CLASSIC_GRENADE_KILL = 22;
inline constexpr std::int64_t CLASSIC_GRENADE_RESTOCK_AMOUNT = 4;
inline constexpr double CLASSIC_GRENADE_SHOOT_INTERVAL = 0.5;
inline constexpr std::int64_t CLASSIC_GRENADE_STOCK = 4;
inline constexpr std::int64_t CLASSIC_GRENADE_THROW_SPEED = 35;
inline constexpr std::int64_t CLASSIC_GRENADE_TOOL = 31;
inline constexpr std::int64_t CLASSIC_MAX_BLOCK_DISTANCE = 5;
inline constexpr std::int64_t CLASSIC_PICKUP_SOUND_ID = 12;
inline constexpr std::int64_t CLASSIC_SCORE_TEABAG = 2;
inline constexpr double CLASSIC_SHOTGUN_ACCURACY = 0.04;
inline constexpr double CLASSIC_SHOTGUN_ACCURACY_RANGE = 0.04;
inline constexpr double CLASSIC_SHOTGUN_ACCURACY_SPREAD_INCREASE_PER_SHOT = 0.5;
inline constexpr std::int64_t CLASSIC_SHOTGUN_ACCURACY_SPREAD_INITIAL = 4;
inline constexpr std::int64_t CLASSIC_SHOTGUN_ACCURACY_SPREAD_RANGE = 3;
inline constexpr std::int64_t CLASSIC_SHOTGUN_ACCURACY_SPREAD_REDUCTION_SPEED = 1;
inline constexpr std::int64_t CLASSIC_SHOTGUN_AMMO_CLIP_SIZE = 5;
inline constexpr std::int64_t CLASSIC_SHOTGUN_AMMO_INITIAL_STOCK = 20;
inline constexpr std::int64_t CLASSIC_SHOTGUN_AMMO_MAX = 45;
inline constexpr std::int64_t CLASSIC_SHOTGUN_AMMO_RESTOCK_AMOUNT = 20;
inline constexpr std::int64_t CLASSIC_SHOTGUN_DAMAGE_ARMS = 12;
inline constexpr std::int64_t CLASSIC_SHOTGUN_DAMAGE_BLOCK = 1;
inline constexpr std::int64_t CLASSIC_SHOTGUN_DAMAGE_ENTITY = 25;
inline constexpr std::int64_t CLASSIC_SHOTGUN_DAMAGE_HEAD = 30;
inline constexpr std::int64_t CLASSIC_SHOTGUN_DAMAGE_LEGS = 12;
inline constexpr std::int64_t CLASSIC_SHOTGUN_DAMAGE_TORSO = 20;
inline constexpr std::int64_t CLASSIC_SHOTGUN_DELAY = 1;
inline constexpr std::int64_t CLASSIC_SHOTGUN_NUMBER_PELLETS = 12;
inline constexpr std::int64_t CLASSIC_SHOTGUN_RANGE = 75;
inline constexpr double CLASSIC_SHOTGUN_RECOIL_SIDE = 0.0002;
inline constexpr double CLASSIC_SHOTGUN_RECOIL_UP = -0.1;
inline constexpr double CLASSIC_SHOTGUN_RELOAD_TIME = 0.5;
inline constexpr std::int64_t CLASSIC_SHOTGUN_SHOOT_INTERVAL = 1;
inline constexpr std::int64_t CLASSIC_SHOTGUN_TOOL = 37;
inline constexpr double CLASSIC_SMG_ACCURACY = 0.01;
inline constexpr double CLASSIC_SMG_ACCURACY_RANGE = 0.05;
inline constexpr double CLASSIC_SMG_ACCURACY_SPREAD_INCREASE_PER_SHOT = 0.2;
inline constexpr std::int64_t CLASSIC_SMG_ACCURACY_SPREAD_INITIAL = 1;
inline constexpr std::int64_t CLASSIC_SMG_ACCURACY_SPREAD_RANGE = 5;
inline constexpr double CLASSIC_SMG_ACCURACY_SPREAD_REDUCTION_SPEED = 0.6;
inline constexpr std::int64_t CLASSIC_SMG_AMMO_CLIP_SIZE = 25;
inline constexpr std::int64_t CLASSIC_SMG_AMMO_INITIAL_STOCK = 100;
inline constexpr std::int64_t CLASSIC_SMG_AMMO_MAX = 100;
inline constexpr std::int64_t CLASSIC_SMG_AMMO_RESTOCK_AMOUNT = 100;
inline constexpr std::int64_t CLASSIC_SMG_DAMAGE_ARMS = 20;
inline constexpr std::int64_t CLASSIC_SMG_DAMAGE_BLOCK = 2;
inline constexpr std::int64_t CLASSIC_SMG_DAMAGE_ENTITY = 20;
inline constexpr std::int64_t CLASSIC_SMG_DAMAGE_HEAD = 20;
inline constexpr std::int64_t CLASSIC_SMG_DAMAGE_LEGS = 20;
inline constexpr std::int64_t CLASSIC_SMG_DAMAGE_TORSO = 20;
inline constexpr double CLASSIC_SMG_DELAY = 0.11;
inline constexpr std::int64_t CLASSIC_SMG_RANGE = 100;
inline constexpr std::int64_t CLASSIC_SMG_RECOIL_SIDE = 0;
inline constexpr double CLASSIC_SMG_RECOIL_UP = -0.007;
inline constexpr double CLASSIC_SMG_RELOAD_TIME = 1.25;
inline constexpr double CLASSIC_SMG_SHOOT_INTERVAL = 0.1;
inline constexpr std::int64_t CLASSIC_SMG_TOOL = 38;
inline constexpr std::int64_t CLASSIC_SOLDIER_ACCEL_MULTIPLIER = 1;
inline constexpr double CLASSIC_SOLDIER_CROUCH_SNEAK_MULTIPLIER = 0.5;
inline constexpr std::int64_t CLASSIC_SOLDIER_DAMAGE_MULTIPLIER = 1;
inline constexpr std::int64_t CLASSIC_SOLDIER_FALLING_DAMAGE_MAX_DAMAGE = 100;
inline constexpr std::int64_t CLASSIC_SOLDIER_FALLING_DAMAGE_MAX_DISTANCE = 26;
inline constexpr std::int64_t CLASSIC_SOLDIER_FALLING_DAMAGE_MIN_DISTANCE = 6;
inline constexpr std::int64_t CLASSIC_SOLDIER_FALL_ON_WATER_DAMAGE_MULTIPLIER = 1;
inline constexpr std::int64_t CLASSIC_SOLDIER_GRENADE_KILLS = 135;
inline constexpr std::int64_t CLASSIC_SOLDIER_HEADSHOT_DAMAGE_MULTIPLIER = 1;
inline constexpr std::int64_t CLASSIC_SOLDIER_INTEL_KILLS = 138;
inline constexpr std::int64_t CLASSIC_SOLDIER_JUMP_MULTIPLIER = 1;
inline constexpr std::int64_t CLASSIC_SOLDIER_MAX_BLOCKS = 100;
inline constexpr std::int64_t CLASSIC_SOLDIER_RIFLE_HEADSHOT_TOTAL = 137;
inline constexpr std::int64_t CLASSIC_SOLDIER_RIFLE_KILLS = 134;
inline constexpr std::int64_t CLASSIC_SOLDIER_SPADE_KILLS = 136;
inline constexpr double CLASSIC_SOLDIER_SPRINT_MULTIPLIER = 1.33;
inline constexpr std::int64_t CLASSIC_SOLDIER_STARTING_BLOCKS = 25;
inline constexpr std::int64_t CLASSIC_SOLDIER_WATER_FRICTION = 8;
inline constexpr std::int64_t CLASSIC_SPADE_DAMAGE = 4;
inline constexpr std::int64_t CLASSIC_SPADE_DAMAGE_AMOUNT = 3;
inline constexpr std::int64_t CLASSIC_SPADE_HITPLAYER_DAMAGE_AMOUNT = 50;
inline constexpr std::int64_t CLASSIC_SPADE_SECONDARY_DAMAGE = 5;
inline constexpr std::int64_t CLASSIC_SPADE_SECONDARY_DAMAGE_AMOUNT = 5;
inline constexpr double CLASSIC_SPADE_SHOOT_INTERVAL = 0.3;
inline constexpr std::int64_t CLASSIC_SPADE_TOOL = 4;
inline constexpr std::int64_t CLASS_CHANGE_KILL = 10;
inline constexpr std::int64_t CLASS_CLASSIC_SOLDIER = 5;
inline constexpr std::int64_t CLASS_COMMON = 5;
inline constexpr std::int64_t CLASS_ENGINEER = 12;
inline constexpr std::int64_t CLASS_EQUIPMENT = 3;
inline constexpr std::int64_t CLASS_FAST_ZOMBIE = 14;
inline constexpr std::int64_t CLASS_GANGSTER_1 = 6;
inline constexpr std::int64_t CLASS_GANGSTER_2 = 7;
inline constexpr std::int64_t CLASS_GANGSTER_3 = 8;
inline constexpr std::int64_t CLASS_GANGSTER_4 = 9;
inline constexpr std::int64_t CLASS_GANGSTER_VIP_1 = 10;
inline constexpr std::int64_t CLASS_GANGSTER_VIP_2 = 11;
inline constexpr std::int64_t CLASS_JUMP_ZOMBIE = 15;
inline constexpr std::int64_t CLASS_MEDIC = 17;
inline constexpr std::int64_t CLASS_MELEE = 0;
inline constexpr std::int64_t CLASS_MINER = 3;
inline constexpr std::int64_t CLASS_NOOF = 18;
inline constexpr std::int64_t CLASS_NOOF_ITEMS = 7;
inline constexpr std::int64_t CLASS_NOOF_SELECTABLE_ITEMS = 5;
inline constexpr std::int64_t CLASS_PREFABS = 4;
inline constexpr std::int64_t CLASS_PREFABS_CLASSIC_SOLDIER = 5;
inline constexpr std::int64_t CLASS_PREFABS_ENGINEER = 9;
inline constexpr std::int64_t CLASS_PREFABS_FAST_ZOMBIE = 11;
inline constexpr std::int64_t CLASS_PREFABS_GANGSTER = 6;
inline constexpr std::int64_t CLASS_PREFABS_JUMP_ZOMBIE = 12;
inline constexpr std::int64_t CLASS_PREFABS_MEDIC = 14;
inline constexpr std::int64_t CLASS_PREFABS_MINER = 3;
inline constexpr std::int64_t CLASS_PREFABS_ROCKETEER = 2;
inline constexpr std::int64_t CLASS_PREFABS_SCOUT = 1;
inline constexpr std::int64_t CLASS_PREFABS_SOLDIER = 0;
inline constexpr std::int64_t CLASS_PREFABS_SPECIALIST = 13;
inline constexpr std::int64_t CLASS_PREFABS_UGCBUILDER = 10;
inline constexpr std::int64_t CLASS_PREFABS_ZOMBIE = 4;
inline constexpr std::int64_t CLASS_PRIMARY_WEAPONS = 1;
inline constexpr std::int64_t CLASS_ROCKETEER = 2;
inline constexpr std::int64_t CLASS_SCOUT = 1;
inline constexpr std::int64_t CLASS_SECONDARY_WEAPONS = 2;
inline constexpr std::int64_t CLASS_SOLDIER = 0;
inline constexpr std::int64_t CLASS_SPECIALIST = 16;
inline constexpr std::int64_t CLASS_UGCBUILDER = 13;
inline constexpr std::int64_t CLASS_UGC_TOOLS = 6;
inline constexpr std::int64_t CLASS_ZOMBIE = 4;
inline constexpr std::int64_t CLIMB_SLOW_DOWN = 1;
inline constexpr double CLIMB_SLOW_DOWN_CLASSIC = 0.92;
inline constexpr std::int64_t CLOCK_SYNC_RATE = 60;
inline constexpr std::int64_t COLOUR_PALETTE_DISABLED = 0;
inline constexpr std::int64_t COLOUR_PALETTE_ENABLED = 1;
inline constexpr std::int64_t COMBAT_10INAROW_TOTAL = 140;
inline constexpr std::int64_t COMBAT_15INAROW_TOTAL = 141;
inline constexpr std::int64_t COMBAT_5INAROW_TOTAL = 139;
inline constexpr std::int64_t COMBAT_AMMO_DROP_TOTAL = 143;
inline constexpr std::int64_t COMBAT_BLOCK_DROP_TOTAL = 145;
inline constexpr std::int64_t COMBAT_DISTANCE_RAN_TOTAL = 142;
inline constexpr std::int64_t COMBAT_GRENADE_DEMOLISH_TOTAL = 146;
inline constexpr std::int64_t COMBAT_HEALTH_DROP_TOTAL = 144;
inline constexpr std::int64_t COMBAT_KILLSATLOWHEALTH_TOTAL = 154;
inline constexpr std::int64_t COMBAT_KILL_JETPACK_TOTAL = 147;
inline constexpr std::int64_t COMBAT_PICKAXE_KILLS = 148;
inline constexpr std::int64_t COMBAT_PISTOL_KILLS = 149;
inline constexpr std::int64_t COMBAT_SPADE_KILLS = 150;
inline constexpr std::int64_t COMBAT_TEABAG_CLASSIC_TOTAL = 153;
inline constexpr std::int64_t COMBAT_TEABAG_TOTAL = 152;
inline constexpr std::int64_t COMBAT_TIMEINAIR_TOTAL = 155;
inline constexpr std::int64_t COMBAT_TURRET_EVASION_TOTAL = 151;
inline constexpr std::int64_t COMPLETE_TOTAL_SCORE = 201;
inline constexpr std::int64_t COM_CTF_ASSAULT = 218;
inline constexpr std::int64_t COM_CTF_ASSIST = 216;
inline constexpr std::int64_t COM_CTF_DEFEND = 217;
inline constexpr std::int64_t COM_DIA_ASSAULT = 214;
inline constexpr std::int64_t COM_DIA_ASSIST = 213;
inline constexpr std::int64_t COM_DIA_STEAL = 215;
inline constexpr std::int64_t COM_MH_CONTROL = 219;
inline constexpr std::int64_t COM_OCC_ASSIST = 210;
inline constexpr std::int64_t COM_OCC_CARRY = 209;
inline constexpr std::int64_t COM_OCC_DEFEND = 211;
inline constexpr std::int64_t COM_OCC_SURVIVAL = 212;
inline constexpr std::int64_t COM_TC_CONTEND = 208;
inline constexpr std::int64_t COM_TDM_ASSIST = 202;
inline constexpr std::int64_t COM_TDM_RETRIBUTION = 203;
inline constexpr std::int64_t COM_VIP_ASSAULT = 205;
inline constexpr std::int64_t COM_VIP_DEFEND = 206;
inline constexpr std::int64_t COM_VIP_ESCORT = 207;
inline constexpr std::int64_t COM_VIP_SURVIVE = 204;
inline constexpr double CORPSE_BOUNCE = 0.1;
inline constexpr std::int64_t CORPSE_BOUNCE_SOUND_THRESHOLD = 2;
inline constexpr std::int64_t CORPSE_DAMAGE = 13;
inline constexpr std::int64_t CORPSE_ENTITY = 12;
inline constexpr std::int64_t CORPSE_EXPLOSION_BLOCK_DAMAGE = 1;
inline constexpr std::int64_t CORPSE_EXPLOSION_DAMAGE = 0;
inline constexpr std::int64_t CORPSE_EXPLOSION_FUSE = 0;
inline constexpr std::int64_t CORPSE_EXPLOSION_JETPACK_FUSE = 1;
inline constexpr double CORPSE_EXPLOSION_KNOCKBACK_MAX = 0.1;
inline constexpr double CORPSE_EXPLOSION_KNOCKBACK_MIN = 0.05;
inline constexpr std::int64_t CORPSE_EXPLOSION_RADIUS = 3;
inline constexpr std::int64_t CORPSE_KILL = 12;
inline constexpr double CORPSE_MOVE_THRESHOLD = 0.5;
inline constexpr std::int64_t CRATEDROP_FLYBY_POS_SOUND_ID = 25;
inline constexpr std::int64_t CRATEDROP_FLYBY_POS_WW_SOUND_ID = 24;
inline constexpr std::int64_t CRATEDROP_FLYBY_SPACE_POS_SOUND_ID = 26;
inline constexpr std::int64_t CRATE_BLOCKS_SOUND_ID = 15;
inline constexpr double CRATE_DISTANCE = 2.5;
inline constexpr std::int64_t CRATE_PARACHUTE_DEPLOYMENT_HEIGHT = 10;
inline constexpr std::int64_t CRATE_PARACHUTE_REMOVAL_HEIGHT = 2;
inline constexpr double CRATE_PARACHUTE_SLOWDOWN = 0.75;
inline constexpr std::int64_t CRATE_PICKUP_FX_ALPHA_BLEND_MODE = 2;
inline constexpr std::int64_t CRATE_PICKUP_FX_DECAY_RATE = 1;
inline constexpr double CRATE_PICKUP_FX_EXPLOSION_SPEED = 0.05;
inline constexpr std::int64_t CRATE_PICKUP_FX_FRAMERATE = 30;
inline constexpr std::int64_t CRATE_PICKUP_FX_INITIAL_ROTATION = 0;
inline constexpr std::int64_t CRATE_PICKUP_FX_LIFETIME = 2;
inline constexpr std::int64_t CRATE_PICKUP_FX_LOOP = 0;
inline constexpr std::int64_t CRATE_PICKUP_FX_NOOF = 25;
inline constexpr std::int64_t CRATE_PICKUP_FX_NUM_FRAMES_X = 4;
inline constexpr std::int64_t CRATE_PICKUP_FX_NUM_FRAMES_Y = 4;
inline constexpr std::int64_t CRATE_PICKUP_FX_PARTICLE_SIZE = 4;
inline constexpr std::int64_t CRATE_PICKUP_FX_ROTATION_SPEED = 180;
inline constexpr std::int64_t CRATE_PICKUP_FX_START_FRAME = 0;
inline constexpr double CRATE_PICKUP_FX_VERTICAL_SPEED = 0.05;
inline constexpr std::int64_t CRATE_SOUND_ID = 13;
inline constexpr std::int64_t CRATE_SPAWN_DELAY = 25;
inline constexpr std::int64_t CROSSROADS_TIME_SCORE = 177;
inline constexpr double CROUCHING_PLAYER_CENTER_VERTICAL_OFFSET = 1.25;
inline constexpr std::int64_t CROWBAR_DAMAGE = 26;
inline constexpr std::int64_t CROWBAR_DAMAGE_AMOUNT = 5;
inline constexpr std::int64_t CROWBAR_HITPLAYER_DAMAGE_AMOUNT = 80;
inline constexpr std::int64_t CROWBAR_HIT_BLOCK_SOUND_ID = 34;
inline constexpr std::int64_t CROWBAR_HIT_WATER_SOUND_ID = 41;
inline constexpr double CROWBAR_SHOOT_INTERVAL = 0.6;
inline constexpr std::int64_t CROWBAR_TOOL = 34;
inline constexpr std::int64_t CTF_ASSAULT_ENEMY_SCORE_REASON = 56;
inline constexpr std::int64_t CTF_ASSAULT_SCORE_REASON = 55;
inline constexpr std::int64_t CTF_CAPTURE_SCORE_REASON = 49;
inline constexpr std::int64_t CTF_CARRIER_DEFEND_SCORE_REASON = 57;
inline constexpr std::int64_t CTF_CARRIER_THREAT_RADIUS = 10;
inline constexpr std::int64_t CTF_CARRY_SCORE_REASON = 50;
inline constexpr std::int64_t CTF_CLAIM_SCORE_REASON = 52;
inline constexpr std::int64_t CTF_CLASSIC_GAME_LENGTH = 5400;
inline constexpr std::int64_t CTF_DEFEND_SCORE_REASON = 54;
inline constexpr std::int64_t CTF_DISTRACT_SCORE_REASON = 53;
inline constexpr std::int64_t CTF_ESCORT_HYSTERESIS = 1;
inline constexpr std::int64_t CTF_ESCORT_RADIUS = 20;
inline constexpr std::int64_t CTF_ESCORT_SCORE_REASON = 51;
inline constexpr std::int64_t CTF_GAME_LENGTH = 1800;
inline constexpr std::int64_t CTF_INDIVIDUAL_SCORE_FOR_CAPTURED_INTEL = 10;
inline constexpr std::int64_t CTF_INDIVIDUAL_SCORE_FOR_RETURNING_INTEL = 1;
inline constexpr std::int64_t CTF_INTEL_RETURN_TIME = 60;
inline constexpr std::int64_t CTF_INTERCEPT_SCORE_REASON = 58;
inline constexpr std::int64_t CTF_MODE_SCORE_REASON = 189;
inline constexpr std::int64_t CTF_SCORE_ASSAULT = 50;
inline constexpr std::int64_t CTF_SCORE_ASSAULT_ENEMY = 50;
inline constexpr std::int64_t CTF_SCORE_CARRIER_DEFEND = 100;
inline constexpr std::int64_t CTF_SCORE_CARRY_INTERVAL = 5;
inline constexpr std::int64_t CTF_SCORE_CARRY_SCORE = 50;
inline constexpr std::int64_t CTF_SCORE_CLAIM = 100;
inline constexpr std::int64_t CTF_SCORE_DEFEND = 50;
inline constexpr std::int64_t CTF_SCORE_DISTRACT = 100;
inline constexpr std::int64_t CTF_SCORE_ESCORT_INTERVAL = 5;
inline constexpr std::int64_t CTF_SCORE_ESCORT_SCORE = 10;
inline constexpr std::int64_t CTF_SCORE_INTERCEPT = 50;
inline constexpr std::int64_t CTF_TEAM_SCORE_FOR_CAPTURED_INTEL = 1;
inline constexpr std::int64_t CTF_THREAT_RADIUS = 20;
inline constexpr std::int64_t CTF_TOTAL_SCORE = 197;
inline constexpr std::int64_t DAMAGE_HEAL = 2;
inline constexpr std::int64_t DAMAGE_OTHER = 1;
inline constexpr std::int64_t DAMAGE_SELF = 0;
inline constexpr std::int64_t DEATHCAM_ANGLE_LERP_SPEED = 10;
inline constexpr std::int64_t DEATHCAM_POSITION_LERP_SPEED = 20;
inline constexpr std::int64_t DEATHCAM_RANGE_FROM_KILLER = 5;
inline constexpr std::int64_t DEATHCAM_STREAK_FOR_ORIENTATION = 2;
inline constexpr std::int64_t DEATHCAM_STREAK_FOR_POSITION = 3;
inline constexpr double DEATHCAM_TIME_TILL_CHASE_CAM_AVAIL = 1.5;
inline constexpr std::int64_t DEATHCAM_TIME_TILL_CHASE_CAM_FORCED = 5;
inline constexpr double DEATHCAM_TIME_TILL_POSITION_CHANGE = 0.25;
inline constexpr std::int64_t DEATH_CAMERA = 5;
inline constexpr std::int64_t DEATH_SCORE_REASON = 220;
inline constexpr std::int64_t DEATH_SOUND = 8;
inline constexpr std::int64_t DEBRIS_DRAW_RANGE = 128;
inline constexpr std::int64_t DEBUGDRAW_CUBE = 1;
inline constexpr std::int64_t DEBUGDRAW_LINE = 0;
inline constexpr double DEFAULT_ATTENUATION = 0.15;
inline constexpr std::int64_t DEFAULT_BLOCK_HEALTH = 5;
inline constexpr std::int64_t DEFAULT_CLASS = 5;
inline constexpr std::int64_t DEFAULT_GAME_LENGTH = 900;
inline constexpr double DEFAULT_MUSIC_FADE_TIME = 6.5;
inline constexpr std::int64_t DEFAULT_PREFABS = 7;
inline constexpr std::int64_t DEFAULT_PREFAB_HEALTH = 9;
inline constexpr std::int64_t DEFAULT_RESPAWN_TIME = 10;
inline constexpr std::int64_t DEFAULT_SNOW_HEALTH = 3;
inline constexpr std::int64_t DEMOLITION_END_MESSAGE = 2;
inline constexpr std::int64_t DEM_ASSAULT_SCORE_REASON = 72;
inline constexpr std::int64_t DEM_BUILD_SPACE_PERCENT = 0;
inline constexpr std::int64_t DEM_DAMAGE_TOTAL = 74;
inline constexpr std::int64_t DEM_DEFAULT_BUILD_TIME = 30;
inline constexpr std::int64_t DEM_DEFEND_SCORE_REASON = 71;
inline constexpr std::int64_t DEM_DESTROY_SCORE_REASON = 69;
inline constexpr std::int64_t DEM_FINAL_DAMAGE_TOBASE_TOTAL = 75;
inline constexpr std::int64_t DEM_GAME_LENGTH = 900;
inline constexpr std::int64_t DEM_MODE_SCORE_REASON = 186;
inline constexpr std::int64_t DEM_REPAIR_SCORE_REASON = 70;
inline constexpr std::int64_t DEM_REPAIR_TOTAL = 73;
inline constexpr std::int64_t DEM_REPAIR_WARNING_PERCENT = 75;
inline constexpr std::int64_t DEM_SCORE_ASSAULT = 50;
inline constexpr std::int64_t DEM_SCORE_DEFEND = 100;
inline constexpr std::int64_t DEM_SCORE_DESTROY_INTERVAL = 50;
inline constexpr std::int64_t DEM_SCORE_DESTROY_SCORE = 25;
inline constexpr std::int64_t DEM_SCORE_REPAIR_INTERVAL = 50;
inline constexpr std::int64_t DEM_SCORE_REPAIR_SCORE = 50;
inline constexpr std::int64_t DEM_TIME_TO_WAIT_FOR_AIRSTRIKE = 5;
inline constexpr std::int64_t DEM_TOTAL_SCORE = 199;
inline constexpr std::int64_t DIAMOND_APPEAR_SOUND_ID = 4;
inline constexpr std::int64_t DIAMOND_DISAPPEAR_SOUND_ID = 5;
inline constexpr std::int64_t DIAMOND_DROPINBASE_SOUND_ID = 6;
inline constexpr std::int64_t DIAMOND_DROP_SOUND_ID = 23;
inline constexpr std::int64_t DIAMOND_LIFETIME = 60;
inline constexpr std::int64_t DIAMOND_PICKUP = 15;
inline constexpr std::int64_t DIAMOND_PICKUP_FX_ALPHA_BLEND_MODE = 2;
inline constexpr std::int64_t DIAMOND_PICKUP_FX_DECAY_RATE = 1;
inline constexpr double DIAMOND_PICKUP_FX_EXPLOSION_SPEED = 0.07;
inline constexpr std::int64_t DIAMOND_PICKUP_FX_FRAMERATE = 30;
inline constexpr std::int64_t DIAMOND_PICKUP_FX_INITIAL_ROTATION = 0;
inline constexpr std::int64_t DIAMOND_PICKUP_FX_LIFETIME = 2;
inline constexpr std::int64_t DIAMOND_PICKUP_FX_LOOP = 0;
inline constexpr std::int64_t DIAMOND_PICKUP_FX_NOOF = 50;
inline constexpr std::int64_t DIAMOND_PICKUP_FX_NUM_FRAMES_X = 4;
inline constexpr std::int64_t DIAMOND_PICKUP_FX_NUM_FRAMES_Y = 4;
inline constexpr std::int64_t DIAMOND_PICKUP_FX_PARTICLE_SIZE = 5;
inline constexpr std::int64_t DIAMOND_PICKUP_FX_ROTATION_SPEED = 180;
inline constexpr std::int64_t DIAMOND_PICKUP_FX_START_FRAME = 0;
inline constexpr double DIAMOND_PICKUP_FX_VERTICAL_SPEED = 0.08;
inline constexpr std::int64_t DIAMOND_PICKUP_SOUND_ID = 19;
inline constexpr std::int64_t DIAMOND_THROW_SPEED = 15;
inline constexpr std::int64_t DIAMOND_TOOL = 26;
inline constexpr std::int64_t DIA_ASSAULT_SCORE_REASON = 45;
inline constexpr std::int64_t DIA_CAPTURE_SCORE_REASON = 38;
inline constexpr std::int64_t DIA_CARRIER_DEFEND_SCORE_REASON = 43;
inline constexpr std::int64_t DIA_CARRIER_THREAT_RADIUS = 10;
inline constexpr std::int64_t DIA_CARRY_SCORE_REASON = 40;
inline constexpr std::int64_t DIA_DEFAULT_ACTIVE_BASES_AT_ONCE = 1;
inline constexpr std::int64_t DIA_DEFAULT_MAX_ACTIVE_DIAMONDS = 2;
inline constexpr std::int64_t DIA_DEFEND_SCORE_REASON = 44;
inline constexpr std::int64_t DIA_DIAMONDS_TO_GET_FOR_MAP_ROTATION = 15;
inline constexpr std::int64_t DIA_DIAMONDS_TO_TRIGGER_MAP_VOTE = 12;
inline constexpr std::int64_t DIA_DISTRACT_SCORE_REASON = 42;
inline constexpr std::int64_t DIA_ESCORT_HYSTERESIS = 10;
inline constexpr std::int64_t DIA_ESCORT_RADIUS = 15;
inline constexpr std::int64_t DIA_ESCORT_SCORE_REASON = 41;
inline constexpr std::int64_t DIA_FINDANDCASHIN_TOTAL = 48;
inline constexpr std::int64_t DIA_GAME_LENGTH = 900;
inline constexpr double DIA_HIGHEST_DIAMOND_CHANCE = 0.01;
inline constexpr std::int64_t DIA_INDIVIDUAL_SCORE_FOR_CASHED_IN_DIAMOND = 100;
inline constexpr std::int64_t DIA_INDIVIDUAL_SCORE_FOR_MINED_DIAMOND = 10;
inline constexpr std::int64_t DIA_INTERCEPT_SCORE_REASON = 46;
inline constexpr double DIA_LOWEST_DIAMOND_CHANCE = 0.002;
inline constexpr std::int64_t DIA_MODE_SCORE_REASON = 184;
inline constexpr std::int64_t DIA_SCORE_ASSAULT = 50;
inline constexpr std::int64_t DIA_SCORE_CARRIER_DEFEND = 100;
inline constexpr std::int64_t DIA_SCORE_CARRY_INTERVAL = 5;
inline constexpr std::int64_t DIA_SCORE_CARRY_SCORE = 50;
inline constexpr std::int64_t DIA_SCORE_DEFEND = 50;
inline constexpr std::int64_t DIA_SCORE_DISTRACT = 100;
inline constexpr std::int64_t DIA_SCORE_ESCORT_INTERVAL = 5;
inline constexpr std::int64_t DIA_SCORE_ESCORT_SCORE = 10;
inline constexpr std::int64_t DIA_SCORE_INTERCEPT = 50;
inline constexpr std::int64_t DIA_STEAL_TOTAL = 47;
inline constexpr std::int64_t DIA_THREAT_RADIUS = 20;
inline constexpr std::int64_t DIA_TIME_BETWEEN_DIAMOND_SPAWN = 15;
inline constexpr std::int64_t DIA_TOTAL_SCORE = 196;
inline constexpr std::int64_t DIA_UNCOVER_SCORE_REASON = 39;
inline constexpr std::int64_t DIG_HIT_BLOCK_SOUND_ID = 33;
inline constexpr std::int64_t DIG_HIT_WATER_SOUND_ID = 39;
inline constexpr std::int64_t DISABLE_MINIMAP_HEIGHT_ICONS = 0;
inline constexpr std::int64_t DISABLE_NUMERIC_HP = 0;
inline constexpr std::int64_t DISGUISE_TOOL = 64;
inline constexpr std::int64_t DLC_AppID01 = 1;
inline constexpr std::int64_t DLC_AppID02 = 420650;
inline constexpr std::int64_t DOUBLE_DRAGON_TIME_SCORE = 174;
inline constexpr std::int64_t DOWN_ORIENTATION_PITCH = 3;
inline constexpr std::int64_t DRAGON_ISLAND_TIME_SCORE = 165;
inline constexpr std::int64_t DRAW_DISTANCE_HIGH = 2;
inline constexpr std::int64_t DRAW_DISTANCE_LOW = 0;
inline constexpr std::int64_t DRAW_DISTANCE_MED = 1;
inline constexpr std::int64_t DRAW_LIMIT_FRAMERATE = 5;
inline constexpr double DRAW_LIMIT_INTERVAL = 0.2;
inline constexpr double DRILLGUN_ACCURACY = 0.04;
inline constexpr std::int64_t DRILLGUN_AMMO_CLIP_SIZE = 1;
inline constexpr std::int64_t DRILLGUN_AMMO_INITIAL_STOCK = 1;
inline constexpr std::int64_t DRILLGUN_AMMO_MAX = 3;
inline constexpr std::int64_t DRILLGUN_AMMO_RESTOCK_AMOUNT = 2;
inline constexpr std::int64_t DRILLGUN_DELAY = 1;
inline constexpr double DRILLGUN_RECOIL_SIDE = 0.0002;
inline constexpr double DRILLGUN_RECOIL_UP = -0.1;
inline constexpr std::int64_t DRILLGUN_RELOAD_TIME = 4;
inline constexpr double DRILLGUN_SHOOT_INTERVAL = 0.2;
inline constexpr std::int64_t DRILLGUN_TOOL = 14;
inline constexpr double DRILL_COLLISION_RANGE = 0.5;
inline constexpr std::int64_t DRILL_DAMAGE = 10;
inline constexpr std::int64_t DRILL_DESTROYED_DAMAGE = 11;
inline constexpr std::int64_t DRILL_DESTROYED_EXPLOSION_BLOCK_DAMAGE = 10;
inline constexpr std::int64_t DRILL_DESTROYED_EXPLOSION_DAMAGE = 95;
inline constexpr double DRILL_DESTROYED_EXPLOSION_KNOCKBACK_MAX = 0.2;
inline constexpr double DRILL_DESTROYED_EXPLOSION_KNOCKBACK_MIN = 0.1;
inline constexpr double DRILL_DESTROYED_EXPLOSION_RADIUS = 3.5;
inline constexpr std::int64_t DRILL_DIGGING_SPEED = 20;
inline constexpr double DRILL_DIG_SLOWDOWN_DURATION = 0.5;
inline constexpr std::int64_t DRILL_ENTITY = 23;
inline constexpr std::int64_t DRILL_EXPLOSION_BLOCK_DAMAGE = 5;
inline constexpr std::int64_t DRILL_EXPLOSION_DAMAGE = 50;
inline constexpr double DRILL_EXPLOSION_KNOCKBACK_MAX = 0.1;
inline constexpr double DRILL_EXPLOSION_KNOCKBACK_MIN = 0.01;
inline constexpr std::int64_t DRILL_EXPLOSION_RADIUS = 3;
inline constexpr std::int64_t DRILL_FLYING_SPEED = 40;
inline constexpr double DRILL_GRAVITY_MULTIPLIER = 1.5;
inline constexpr std::int64_t DRILL_HEALTH = 10;
inline constexpr std::int64_t DRILL_KILL = 6;
inline constexpr std::int64_t DRILL_LIFESPAN = 3;
inline constexpr double DRILL_MODEL_SIZE = 0.06;
inline constexpr std::int64_t DRILL_MODEL_Z_OFFSET = 0;
inline constexpr std::int64_t DRILL_OWNER_EXPLOSION_PROTECTION_TIME = 1;
inline constexpr std::int64_t DRILL_SPEED = 20;
inline constexpr std::int64_t DYNAMITE_DAMAGE = 16;
inline constexpr std::int64_t DYNAMITE_ENTITY = 10;
inline constexpr std::int64_t DYNAMITE_EXPLOSION_BLOCK_DAMAGE = 7;
inline constexpr std::int64_t DYNAMITE_EXPLOSION_DAMAGE = 300;
inline constexpr std::int64_t DYNAMITE_EXPLOSION_FUSE = 7;
inline constexpr double DYNAMITE_EXPLOSION_KNOCKBACK_MAX = 0.15;
inline constexpr double DYNAMITE_EXPLOSION_KNOCKBACK_MIN = 0.1;
inline constexpr std::int64_t DYNAMITE_EXPLOSION_RADIUS = 5;
inline constexpr std::int64_t DYNAMITE_FAR_RADIUS = 5;
inline constexpr std::int64_t DYNAMITE_HEALTH = 1;
inline constexpr std::int64_t DYNAMITE_INITIAL_STOCK = 1;
inline constexpr std::int64_t DYNAMITE_KILL = 15;
inline constexpr double DYNAMITE_MODEL_SIZE = 0.06;
inline constexpr double DYNAMITE_MODEL_Z_OFFSET = -0.2;
inline constexpr std::int64_t DYNAMITE_RESTOCK_AMOUNT = 3;
inline constexpr std::int64_t DYNAMITE_SHOOT_INTERVAL = 1;
inline constexpr std::int64_t DYNAMITE_STOCK = 3;
inline constexpr std::int64_t DYNAMITE_TOOL = 21;
inline constexpr std::int64_t EAST = 1;
inline constexpr std::int64_t ENABLE_MINIMAP_HEIGHT_ICONS = 1;
inline constexpr std::int64_t ENABLE_NUMERIC_HP = 1;
inline constexpr double ENGINEER_ACCEL_MULTIPLIER = 0.7;
inline constexpr double ENGINEER_CROUCH_SNEAK_MULTIPLIER = 0.5;
inline constexpr double ENGINEER_DAMAGE_MULTIPLIER = 1.1765;
inline constexpr std::int64_t ENGINEER_FALLING_DAMAGE_MAX_DAMAGE = 100;
inline constexpr std::int64_t ENGINEER_FALLING_DAMAGE_MAX_DISTANCE = 40;
inline constexpr std::int64_t ENGINEER_FALLING_DAMAGE_MIN_DISTANCE = 10;
inline constexpr double ENGINEER_FALL_ON_WATER_DAMAGE_MULTIPLIER = 0.5;
inline constexpr std::int64_t ENGINEER_GRENADE_KILLS = 227;
inline constexpr double ENGINEER_HEADSHOT_DAMAGE_MULTIPLIER = 1.5;
inline constexpr std::int64_t ENGINEER_JETPACK_GRENADE_KILLS = 231;
inline constexpr std::int64_t ENGINEER_JETPACK_KILLS = 230;
inline constexpr std::int64_t ENGINEER_JETPACK_SMG_KILLS = 232;
inline constexpr std::int64_t ENGINEER_JUMP_MULTIPLIER = 1;
inline constexpr std::int64_t ENGINEER_MAX_BLOCKS = 3000;
inline constexpr std::int64_t ENGINEER_MINELAUNCHER_KILLS = 248;
inline constexpr std::int64_t ENGINEER_PICKAXE_KILLS = 229;
inline constexpr std::int64_t ENGINEER_SMG_KILLS = 225;
inline constexpr std::int64_t ENGINEER_SNOWBLOWER_KILLS = 233;
inline constexpr std::int64_t ENGINEER_SPADE_KILLS = 228;
inline constexpr double ENGINEER_SPRINT_MULTIPLIER = 1.25;
inline constexpr std::int64_t ENGINEER_STARTING_BLOCKS = 2000;
inline constexpr std::int64_t ENGINEER_TURRET_KILLS = 226;
inline constexpr std::int64_t ENGINEER_WATER_FRICTION = 8;
inline constexpr std::int64_t ENTITY_BILLBOARD_Z_OFFSET = 1;
inline constexpr std::int64_t ENTITY_KILL = 11;
inline constexpr std::int64_t ENTITY_RADIUS = 5;
inline constexpr std::int64_t ERROR_AFK_TIMEOUT = 16;
inline constexpr std::int64_t ERROR_BANNED = 1;
inline constexpr std::int64_t ERROR_CLIENT_OUT_OF_DATE = 10;
inline constexpr std::int64_t ERROR_CONTENT_LOCKED = 15;
inline constexpr std::int64_t ERROR_CONTROL_ALREADY_BOUND = 20;
inline constexpr std::int64_t ERROR_CUSTOM_SERVER = 17;
inline constexpr std::int64_t ERROR_DATA = 13;
inline constexpr std::int64_t ERROR_DEMO_VERSION = 21;
inline constexpr std::int64_t ERROR_DLC_LOCKED = 14;
inline constexpr std::int64_t ERROR_FULL = 4;
inline constexpr std::int64_t ERROR_HOST_HAS_LEFT = 22;
inline constexpr std::int64_t ERROR_KICKED = 2;
inline constexpr std::int64_t ERROR_KICK_ABUSE = 25;
inline constexpr std::int64_t ERROR_KICK_GRIEFING = 23;
inline constexpr std::int64_t ERROR_KICK_HACKING = 24;
inline constexpr std::int64_t ERROR_LOBBY_CLOSED = 26;
inline constexpr std::int64_t ERROR_LOBBY_CONNECTION_FAILED = 29;
inline constexpr std::int64_t ERROR_LOBBY_FULL = 27;
inline constexpr std::int64_t ERROR_LOBBY_UNKNOWN = 28;
inline constexpr std::int64_t ERROR_MATCH_ENDED = 18;
inline constexpr std::int64_t ERROR_NOLICENSE = 7;
inline constexpr std::int64_t ERROR_NOSTEAM = 5;
inline constexpr std::int64_t ERROR_NOTICKET = 8;
inline constexpr std::int64_t ERROR_NOVAC = 6;
inline constexpr std::int64_t ERROR_PUBLISHING_MAP_NO_JOIN = 30;
inline constexpr std::int64_t ERROR_RANKED_SERVER = 12;
inline constexpr std::int64_t ERROR_SERVER_OUT_OF_DATE = 3;
inline constexpr std::int64_t ERROR_TEMP_BANNED = 19;
inline constexpr std::int64_t ERROR_TIMEOUT = 11;
inline constexpr std::int64_t ERROR_UNDEFINED = 0;
inline constexpr std::int64_t ERROR_UNKNOWN = 31;
inline constexpr std::int64_t ERROR_VACBANNED = 9;
inline constexpr std::int64_t EVENT_NEGATIVE_SOUND_ID = 3;
inline constexpr std::int64_t EVENT_POSITIVE_SOUND_ID = 2;
inline constexpr std::int64_t EXPOSED_TEAMS_ALWAYS_ON_MINIMAP = 1;
inline constexpr std::int64_t EXPOSED_TEAMS_NOT_ALWAYS_ON_MINIMAP = 0;
inline constexpr std::int64_t FACE_BACK = 2;
inline constexpr std::int64_t FACE_BOTTOM = 5;
inline constexpr std::int64_t FACE_FRONT = 3;
inline constexpr std::int64_t FACE_LEFT = 1;
inline constexpr std::int64_t FACE_RIGHT = 0;
inline constexpr std::int64_t FACE_TOP = 4;
inline constexpr std::int64_t FADE_IN = 0;
inline constexpr std::int64_t FADE_OUT = 1;
inline constexpr std::int64_t FAKE_PISTOL_TOOL = 40;
inline constexpr std::int64_t FALLING_BLOCKS_MAX_SIZE = 8000;
inline constexpr std::int64_t FALLING_BLOCKS_PARTICLE_MOD_MAX = 15;
inline constexpr std::int64_t FALLING_BLOCKS_PARTICLE_MOD_MIN = 5;
inline constexpr std::int64_t FALLING_BLOCK_SOUND_LARGE = 80;
inline constexpr std::int64_t FALLING_BLOCK_SOUND_MED = 15;
inline constexpr std::int64_t FALL_HURT_SOUND = 5;
inline constexpr std::int64_t FALL_HURT_VO = 15;
inline constexpr std::int64_t FALL_KILL = 7;
inline constexpr std::int64_t FAST_ZOMBIE_MAX_BLOCKS = 1000;
inline constexpr std::int64_t FAST_ZOMBIE_STARTING_BLOCKS = 500;
inline constexpr std::int64_t FEWEST_SHOTS_FIRED = 24;
inline constexpr double FIRST_ZOMBIE_SPAWN_PROTECTION_TIME = 0.5;
inline constexpr std::int64_t FLAG = 0;
inline constexpr std::int64_t FLAG_RETURNED_SOUND_ID = 20;
inline constexpr std::int64_t FLAREBLOCK_COST = 10;
inline constexpr std::int64_t FLAREBLOCK_LIGHT_RADIUS = 5;
inline constexpr std::int64_t FLAREBLOCK_TOOL = 22;
inline constexpr std::int64_t FLARE_BLOCK = 13;
inline constexpr std::int64_t FLY_CAMERA = 0;
inline constexpr std::int64_t FOOTSTEP_SOUND = 6;
inline constexpr std::int64_t FORCED_TEAM_CHANGE_KILL = 8;
inline constexpr std::int64_t FORWARD_ORIENTATION_PITCH = 0;
inline constexpr std::int64_t FULLHURT_VO_CHANCE = 100;
inline constexpr std::int64_t GAME_DRAWS_TOTAL = 161;
inline constexpr std::int64_t GAME_LOSSES_TOTAL = 160;
inline constexpr std::int64_t GAME_MODE_CALLBACK_ADVANCE_DELAY = 8;
inline constexpr std::int64_t GAME_MODE_CALLBACK_BASE_PICK = 18;
inline constexpr std::int64_t GAME_MODE_CALLBACK_BUILDING_MODE = 1;
inline constexpr std::int64_t GAME_MODE_CALLBACK_EMPTY_SERVER_TIMEOUT = 0;
inline constexpr std::int64_t GAME_MODE_CALLBACK_GAME_ENDED_SCORES = 5;
inline constexpr std::int64_t GAME_MODE_CALLBACK_MAP_VOTE_CLOSE = 19;
inline constexpr std::int64_t GAME_MODE_CALLBACK_SPAWNING_BOMB = 9;
inline constexpr std::int64_t GAME_MODE_CALLBACK_SURVIVOR_WIN = 14;
inline constexpr std::int64_t GAME_MODE_CALLBACK_SURVIVOR_WIN_END = 16;
inline constexpr std::int64_t GAME_MODE_CALLBACK_SURVIVOR_WIN_MUSIC = 15;
inline constexpr std::int64_t GAME_MODE_CALLBACK_TIMEOUT = 6;
inline constexpr std::int64_t GAME_MODE_CALLBACK_TIMEOUT_MUSIC = 7;
inline constexpr std::int64_t GAME_MODE_CALLBACK_TIMER = 3;
inline constexpr std::int64_t GAME_MODE_CALLBACK_TIMER_MUSIC = 4;
inline constexpr std::int64_t GAME_MODE_CALLBACK_VIP_SUDDEN_DEATH_DELAY = 10;
inline constexpr std::int64_t GAME_MODE_CALLBACK_WATCH_EXPLOSION = 2;
inline constexpr std::int64_t GAME_MODE_CALLBACK_WIN_START = 17;
inline constexpr std::int64_t GAME_MODE_CALLBACK_ZOMBIE_PICK = 11;
inline constexpr std::int64_t GAME_MODE_CALLBACK_ZOMBIE_PICK_SOUND = 12;
inline constexpr std::int64_t GAME_MODE_CALLBACK_ZOMBIE_WIN_END = 13;
inline constexpr std::int64_t GAME_WINS_TOTAL = 159;
inline constexpr double GANGSTER_ACCEL_MULTIPLIER = 0.7;
inline constexpr double GANGSTER_CROUCH_SNEAK_MULTIPLIER = 0.5;
inline constexpr std::int64_t GANGSTER_CROWBAR_KILLS = 133;
inline constexpr std::int64_t GANGSTER_DAMAGE_MULTIPLIER = 1;
inline constexpr std::int64_t GANGSTER_FALLING_DAMAGE_MAX_DAMAGE = 100;
inline constexpr std::int64_t GANGSTER_FALLING_DAMAGE_MAX_DISTANCE = 40;
inline constexpr std::int64_t GANGSTER_FALLING_DAMAGE_MIN_DISTANCE = 10;
inline constexpr double GANGSTER_FALL_ON_WATER_DAMAGE_MULTIPLIER = 0.5;
inline constexpr double GANGSTER_HEADSHOT_DAMAGE_MULTIPLIER = 1.2;
inline constexpr double GANGSTER_JUMP_MULTIPLIER = 1.2;
inline constexpr std::int64_t GANGSTER_MAX_BLOCKS = 1200;
inline constexpr std::int64_t GANGSTER_MOLOTOV_KILLS = 132;
inline constexpr std::int64_t GANGSTER_PISTOL_KILLS = 131;
inline constexpr double GANGSTER_SPRINT_MULTIPLIER = 1.5;
inline constexpr std::int64_t GANGSTER_STARTING_BLOCKS = 500;
inline constexpr std::int64_t GANGSTER_TOMMYGUN_KILLS = 130;
inline constexpr std::int64_t GANGSTER_WATER_FRICTION = 8;
inline constexpr std::int64_t GENERIC_ASSIST_PERCENTAGE = 50;
inline constexpr std::int64_t GENERIC_SCORE_ASSIST = 50;
inline constexpr std::int64_t GENERIC_SCORE_DEFEND = 50;
inline constexpr std::int64_t GENERIC_SCORE_HEADSHOT = 150;
inline constexpr std::int64_t GENERIC_SCORE_KILL = 100;
inline constexpr std::int64_t GENERIC_SCORE_MELEE = 150;
inline constexpr std::int64_t GENERIC_SCORE_PAYBACK = 50;
inline constexpr std::int64_t GENERIC_SCORE_RELOAD = 50;
inline constexpr std::int64_t GENERIC_SCORE_REVENGE = 50;
inline constexpr std::int64_t GENERIC_SCORE_SUICIDE = -100;
inline constexpr std::int64_t GENERIC_SCORE_TEAMKILL = -100;
inline constexpr std::int64_t GENERIC_UPDATE_VOTE_COUNT = 2;
inline constexpr std::int64_t GENERIC_VOTE_CAST = 1;
inline constexpr std::int64_t GENERIC_VOTE_CLOSED = 3;
inline constexpr std::int64_t GENERIC_VOTE_START = 0;
inline constexpr std::int64_t GRAVE_DAMAGE = 14;
inline constexpr std::int64_t GRAVE_ENTITY = 11;
inline constexpr std::int64_t GRAVE_EXPLOSION_BLOCK_DAMAGE = 3;
inline constexpr std::int64_t GRAVE_EXPLOSION_DAMAGE = 25;
inline constexpr std::int64_t GRAVE_EXPLOSION_FUSE = 7;
inline constexpr std::int64_t GRAVE_EXPLOSION_KNOCKBACK_MAX = 1;
inline constexpr double GRAVE_EXPLOSION_KNOCKBACK_MIN = 0.5;
inline constexpr std::int64_t GRAVE_EXPLOSION_RADIUS = 3;
inline constexpr std::int64_t GRAVE_KILL = 13;
inline constexpr std::int64_t GRENADE_ACCURACY_SPREAD_INCREASE_PER_SHOT = 0;
inline constexpr std::int64_t GRENADE_ACCURACY_SPREAD_INITIAL = 6;
inline constexpr std::int64_t GRENADE_ACCURACY_SPREAD_RANGE = 0;
inline constexpr std::int64_t GRENADE_ACCURACY_SPREAD_REDUCTION_SPEED = 0;
inline constexpr std::int64_t GRENADE_DAMAGE = 7;
inline constexpr std::int64_t GRENADE_EXPLOSION_BLOCK_DAMAGE = 4;
inline constexpr std::int64_t GRENADE_EXPLOSION_DAMAGE = 230;
inline constexpr double GRENADE_EXPLOSION_FUSE = 2.5;
inline constexpr std::int64_t GRENADE_EXPLOSION_KNOCKBACK_MAX = 1;
inline constexpr double GRENADE_EXPLOSION_KNOCKBACK_MIN = 0.5;
inline constexpr std::int64_t GRENADE_EXPLOSION_RADIUS = 4;
inline constexpr std::int64_t GRENADE_INITIAL_STOCK = 2;
inline constexpr std::int64_t GRENADE_KILL = 3;
inline constexpr std::int64_t GRENADE_LAUNCHER_DAMAGE = 37;
inline constexpr std::int64_t GRENADE_LAUNCHER_KILL = 32;
inline constexpr std::int64_t GRENADE_LAUNCHER_WEAPON_TOOL = 55;
inline constexpr std::int64_t GRENADE_RESTOCK_AMOUNT = 4;
inline constexpr double GRENADE_SHOOT_INTERVAL = 0.5;
inline constexpr std::int64_t GRENADE_SHRAPNEL_DAMAGE_ARMS = 50;
inline constexpr std::int64_t GRENADE_SHRAPNEL_DAMAGE_ENTITY = 50;
inline constexpr std::int64_t GRENADE_SHRAPNEL_DAMAGE_HEAD = 50;
inline constexpr std::int64_t GRENADE_SHRAPNEL_DAMAGE_LEGS = 50;
inline constexpr std::int64_t GRENADE_SHRAPNEL_DAMAGE_TORSO = 50;
inline constexpr std::int64_t GRENADE_STOCK = 4;
inline constexpr std::int64_t GRENADE_THROW_MIN_SPEED = 25;
inline constexpr std::int64_t GRENADE_THROW_SPEED = 50;
inline constexpr std::int64_t GRENADE_TOOL = 11;
inline constexpr std::int64_t HAS_AMMO_CROSSHAIR = 4;
inline constexpr std::int64_t HEADSHOT_KILL = 1;
inline constexpr std::int64_t HEALTHCRATE_HP = 20;
inline constexpr std::int64_t HEALTHCRATE_SOUND_ID = 14;
inline constexpr std::int64_t HEALTH_CRATE = 4;
inline constexpr std::int64_t HEALTH_DROP_POINT_ENTITY = 19;
inline constexpr std::int64_t HEARING_DISTANCE = 50;
inline constexpr std::int64_t HELICOPTER = 2;
inline constexpr std::int64_t HIESVILLE_TIME_SCORE = 171;
inline constexpr std::int64_t HIGHEST_BLOCK = 21;
inline constexpr double HIT_CROSSHAIR_TIME = 0.25;
inline constexpr double HIT_INDICATOR_TIME = 1.2;
inline constexpr std::int64_t HIT_TOLERANCE = 5;
inline constexpr std::int64_t IMAGE_CENTRE = 0;
inline constexpr std::int64_t IMAGE_LEFT = 1;
inline constexpr std::int64_t IMAGE_RIGHT = 2;
inline constexpr std::int64_t INGAME_MUSIC_ENDING = 1;
inline constexpr std::int64_t INGAME_MUSIC_LAST_MAN = 0;
inline constexpr std::int64_t INGAME_MUSIC_TUTORIAL = 2;
inline constexpr std::int64_t INITIAL_HEALTH = 100;
inline constexpr std::int64_t INTEL_MINIMAP_EXPOSURE_TIME = 30;
inline constexpr std::int64_t INTEL_PICKUP = 16;
inline constexpr std::int64_t INTEL_THROW_SPEED = 15;
inline constexpr std::int64_t INTEL_TOOL = 30;
inline constexpr std::int64_t JETPACK2 = 67;
inline constexpr std::int64_t JETPACK_BURDENED_SLOW_DOWN = 5;
inline constexpr std::int64_t JETPACK_CRATE = 6;
inline constexpr std::int64_t JETPACK_DAMAGE_MULTIPLIER = 7;
inline constexpr std::int64_t JETPACK_DEATH_ACCELERATION = 8;
inline constexpr std::int64_t JETPACK_ENGINEER = 68;
inline constexpr std::int64_t JETPACK_FUEL_ACTIVATION_COST = 2;
inline constexpr std::int64_t JETPACK_FUEL_FLYING_CONSUMPTION = 4;
inline constexpr std::int64_t JETPACK_FUEL_REFILL_DELAY_DUE_DAMAGE = 6;
inline constexpr std::int64_t JETPACK_FUEL_REFILL_RATE = 3;
inline constexpr std::int64_t JETPACK_LAND_SOUND = 2;
inline constexpr std::int64_t JETPACK_MAX_FUEL = 1;
inline constexpr std::int64_t JETPACK_NORMAL = 66;
inline constexpr std::int64_t JETPACK_SMOKE_GENERATION_NUMBER_OF_PARTICLES = 2;
inline constexpr std::int64_t JETPACK_SMOKE_GENERATION_PARTICLE_DECAY = -1;
inline constexpr std::int64_t JETPACK_SMOKE_GENERATION_PARTICLE_LIFESPAN = 5;
inline constexpr std::int64_t JETPACK_SMOKE_GENERATION_PARTICLE_SIZE = 2;
inline constexpr double JETPACK_SMOKE_GENERATION_PARTICLE_SPREAD = 0.01;
inline constexpr double JETPACK_SMOKE_GENERATION_RATE = 0.02;
inline constexpr std::int64_t JETPACK_SPRINT_SMOKE_GENERATION_NUMBER_OF_PARTICLES = 2;
inline constexpr std::int64_t JETPACK_SPRINT_SMOKE_GENERATION_PARTICLE_DECAY = -1;
inline constexpr std::int64_t JETPACK_SPRINT_SMOKE_GENERATION_PARTICLE_LIFESPAN = 2;
inline constexpr std::int64_t JETPACK_SPRINT_SMOKE_GENERATION_PARTICLE_MAX_ROT = 200;
inline constexpr std::int64_t JETPACK_SPRINT_SMOKE_GENERATION_PARTICLE_MAX_SIZE = 4;
inline constexpr std::int64_t JETPACK_SPRINT_SMOKE_GENERATION_PARTICLE_MIN_ROT = 160;
inline constexpr std::int64_t JETPACK_SPRINT_SMOKE_GENERATION_PARTICLE_MIN_SIZE = 2;
inline constexpr double JETPACK_SPRINT_SMOKE_GENERATION_PARTICLE_SPREAD = 0.01;
inline constexpr double JETPACK_SPRINT_SMOKE_GENERATION_RATE = 0.01;
inline constexpr std::int64_t JETPACK_START_DELAY = 0;
inline constexpr std::int64_t JETPACK_UGCBUILDER = 69;
inline constexpr std::int64_t JUMP_SOUND = 0;
inline constexpr double JUMP_SOUND_REPEAT_DELAY = 0.1;
inline constexpr std::int64_t JUMP_VO = 11;
inline constexpr std::int64_t JUMP_VO_CHANCE = -33;
inline constexpr std::int64_t JUMP_ZOMBIE_MAX_BLOCKS = 1000;
inline constexpr std::int64_t JUMP_ZOMBIE_STARTING_BLOCKS = 500;
inline constexpr std::int64_t KICKED_SCORE_REASON = 224;
inline constexpr std::int64_t KICK_ABUSE = 2;
inline constexpr std::int64_t KICK_CANCEL = 3;
inline constexpr std::int64_t KICK_GRIEFING = 0;
inline constexpr std::int64_t KICK_HACKING = 1;
inline constexpr std::int64_t KILL_SCORE_ASSIST_REASON = 5;
inline constexpr std::int64_t KILL_SCORE_DEFEND_REASON = 11;
inline constexpr std::int64_t KILL_SCORE_DISTRACT_REASON = 8;
inline constexpr std::int64_t KILL_SCORE_HEADSHOT_REASON = 3;
inline constexpr std::int64_t KILL_SCORE_MELEE_REASON = 4;
inline constexpr std::int64_t KILL_SCORE_PAYBACK_REASON = 9;
inline constexpr std::int64_t KILL_SCORE_REASON = 1;
inline constexpr std::int64_t KILL_SCORE_RELOAD_REASON = 10;
inline constexpr std::int64_t KILL_SCORE_REVENGE_REASON = 7;
inline constexpr std::int64_t KILL_SCORE_TEAMKILL_REASON = 6;
inline constexpr std::int64_t KNIFE_DAMAGE = 1;
inline constexpr std::int64_t KNIFE_DAMAGE_AMOUNT = 1;
inline constexpr std::int64_t KNIFE_HITPLAYER_DAMAGE_AMOUNT = 20;
inline constexpr std::int64_t KNIFE_HIT_BLOCK_SOUND_ID = 35;
inline constexpr std::int64_t KNIFE_HIT_WATER_SOUND_ID = 42;
inline constexpr double KNIFE_SHOOT_INTERVAL = 0.25;
inline constexpr std::int64_t KNIFE_TOOL = 1;
inline constexpr std::int64_t LANDMINE_ACTIVATION_TIMER = 4;
inline constexpr std::int64_t LANDMINE_DAMAGE = 15;
inline constexpr std::int64_t LANDMINE_DETECTION_LAYERS = 3;
inline constexpr double LANDMINE_DETECTION_RANGE = 2.5;
inline constexpr std::int64_t LANDMINE_ENTITY = 9;
inline constexpr double LANDMINE_EXPLOSION_AND_DETECTION_VERTICAL_OFFSET = -0.5;
inline constexpr std::int64_t LANDMINE_EXPLOSION_BLAST_WAVE_RADIUS = 6;
inline constexpr std::int64_t LANDMINE_EXPLOSION_BLOCK_DAMAGE = 15;
inline constexpr std::int64_t LANDMINE_EXPLOSION_DAMAGE = 100;
inline constexpr double LANDMINE_EXPLOSION_KNOCKBACK_MAX = 0.75;
inline constexpr double LANDMINE_EXPLOSION_KNOCKBACK_MIN = 0.75;
inline constexpr std::int64_t LANDMINE_EXPLOSION_RADIUS = 3;
inline constexpr std::int64_t LANDMINE_FAR_RADIUS = 5;
inline constexpr std::int64_t LANDMINE_HEALTH = 1;
inline constexpr std::int64_t LANDMINE_INITIAL_STOCK = 3;
inline constexpr std::int64_t LANDMINE_KILL = 14;
inline constexpr double LANDMINE_MODEL_SIZE = 0.05;
inline constexpr std::int64_t LANDMINE_MODEL_Z_OFFSET = 0;
inline constexpr std::int64_t LANDMINE_RESTOCK_AMOUNT = 5;
inline constexpr std::int64_t LANDMINE_SHOOT_INTERVAL = 1;
inline constexpr std::int64_t LANDMINE_STOCK = 5;
inline constexpr std::int64_t LANDMINE_TOOL = 20;
inline constexpr std::int64_t LAND_SOUND = 3;
inline constexpr std::int64_t LAND_VO = 13;
inline constexpr std::int64_t LAND_VO_CHANCE = -33;
inline constexpr std::int64_t LEADERBOARD_ENTRY_FILTER = 0;
inline constexpr std::int64_t LEADERBOARD_ENTRY_GLOBAL_STRING = 1;
inline constexpr std::int64_t LEADERBOARD_ENTRY_LOCAL_STRING = 2;
inline constexpr std::int64_t LEADERBOARD_ENTRY_SORT_STAT = 3;
inline constexpr std::int64_t LEADERBOARD_ENTRY_STAT_LIST = 4;
inline constexpr std::int64_t LEADERBOARD_TYPE_CTF = 6;
inline constexpr std::int64_t LEADERBOARD_TYPE_DEM = 8;
inline constexpr std::int64_t LEADERBOARD_TYPE_DIA = 5;
inline constexpr std::int64_t LEADERBOARD_TYPE_GENERAL = 0;
inline constexpr std::int64_t LEADERBOARD_TYPE_MH = 9;
inline constexpr std::int64_t LEADERBOARD_TYPE_OCC = 4;
inline constexpr std::int64_t LEADERBOARD_TYPE_TC = 3;
inline constexpr std::int64_t LEADERBOARD_TYPE_TDM = 1;
inline constexpr std::int64_t LEADERBOARD_TYPE_TITLE = 10;
inline constexpr std::int64_t LEADERBOARD_TYPE_VIP = 2;
inline constexpr std::int64_t LEADERBOARD_TYPE_ZOM = 7;
inline constexpr std::int64_t LIGHT_MACHINE_GUN_TOOL = 61;
inline constexpr std::int64_t LINE_OF_SIGHT_HEAD = 0;
inline constexpr std::int64_t LINE_OF_SIGHT_LEGS = 2;
inline constexpr std::int64_t LINE_OF_SIGHT_TORSO = 1;
inline constexpr std::int64_t LOADING_MENU_NO_PROGRESS_TIMEOUT = 30;
inline constexpr std::int64_t LOADING_MENU_TAB_INTERVAL = 3;
inline constexpr std::int64_t LONDON_TIME_SCORE = 166;
inline constexpr std::int64_t LONGEST_RANGED_KILL = 13;
inline constexpr std::int64_t LOOKAT_CAMERA = 4;
inline constexpr std::int64_t LOWER_ARM = 1;
inline constexpr std::int64_t LUNAR_BASE_TIME_SCORE = 167;
inline constexpr std::int64_t MACHETE_DAMAGE = 35;
inline constexpr std::int64_t MACHETE_TOOL = 50;
inline constexpr std::int64_t MACHINE_GUN = 7;
inline constexpr std::int64_t MAP_BLOCKS_DESTROYED_TOTAL = 158;
inline constexpr std::int64_t MAP_COMPRESSION_LEVEL = 9;
inline constexpr std::int64_t MAP_IS_NOT_UGC = 0;
inline constexpr std::int64_t MAP_IS_UGC_CLIENT = 2;
inline constexpr std::int64_t MAP_IS_UGC_HOST = 1;
inline constexpr std::int64_t MAP_MASK_X = 511;
inline constexpr std::int64_t MAP_MASK_Y = 261632;
inline constexpr std::int64_t MAP_MASK_Z = 133955584;
inline constexpr std::int64_t MAP_PREFABS = 8;
inline constexpr std::int64_t MAP_PREFAB_ADDED_TOTAL = 157;
inline constexpr std::int64_t MAP_SHIFT_Y = 9;
inline constexpr std::int64_t MAP_SHIFT_Z = 18;
inline constexpr std::int64_t MAP_SINGLEBLOCKS_ADDED_TOTAL = 156;
inline constexpr std::int64_t MAP_X = 512;
inline constexpr std::int64_t MAP_Y = 512;
inline constexpr std::int64_t MAP_Z = 240;
inline constexpr std::int64_t MASTER_VERSION = 32;
inline constexpr std::int64_t MAX_BLOCK_DISTANCE = 10;
inline constexpr std::int64_t MAX_CHAT_MESSAGE_LENGTH = 200;
inline constexpr std::int64_t MAX_CHAT_SIZE = 90;
inline constexpr std::int64_t MAX_DAMAGE = 2;
inline constexpr std::int64_t MAX_DISPLAY_NAME_DISTANCE = 100;
inline constexpr std::int64_t MAX_DISTANCE_SHOOT_DISCREPANCY = 4;
inline constexpr double MAX_FALL_DAMAGE_AIR_TIME = 0.06666666666666667;
inline constexpr std::int64_t MAX_GAME_MODE_SIZE = 7;
inline constexpr std::int64_t MAX_LOADOUT_PREFABS = 3;
inline constexpr std::int64_t MAX_MAP_NAME_SIZE = 20;
inline constexpr std::int64_t MAX_NOOF_SCORE_REASONS = 251;
inline constexpr std::int64_t MAX_PACKET_DECOMPRESSION_SIZE = 1000000;
inline constexpr std::int64_t MAX_RAPID_SPEED = 60;
inline constexpr std::int64_t MAX_SERVER_NAME_SIZE = 31;
inline constexpr std::int64_t MAX_TIMER_SPEED = 2000;
inline constexpr std::int64_t MAX_UPDATES_PER_FRAME = 4;
inline constexpr std::int64_t MAX_VELOCITY_DISCREPANCY = 4;
inline constexpr std::int64_t MAYAN_JUNGLE_TIME_SCORE = 168;
inline constexpr std::int64_t MEDIC_LIGHTMACHINEGUN_KILLS = 236;
inline constexpr std::int64_t MEDIC_MAX_BLOCKS = 2000;
inline constexpr std::int64_t MEDIC_PICKAXE_KILLS = 245;
inline constexpr std::int64_t MEDIC_RIOTSHIELD_KILLS = 235;
inline constexpr std::int64_t MEDIC_RIOTSTICK_KILLS = 234;
inline constexpr std::int64_t MEDIC_SHOTGUN2_KILLS = 246;
inline constexpr std::int64_t MEDIC_STARTING_BLOCKS = 900;
inline constexpr std::int64_t MEDPACK_TOOL = 51;
inline constexpr std::int64_t MELEE_KILL = 2;
inline constexpr std::int64_t MELEE_RANGE = 3;
inline constexpr std::int64_t MELEE_TOLERANCE = 1;
inline constexpr std::int64_t MELEE_WORLD_RANGE = 4;
inline constexpr std::int64_t MENU_WINDOW_HEIGHT = 600;
inline constexpr std::int64_t MENU_WINDOW_WIDTH = 800;
inline constexpr double MG_ACCURACY = 0.01;
inline constexpr double MG_ACCURACY_RANGE = 0.05;
inline constexpr double MG_ACCURACY_SPREAD_INCREASE_PER_SHOT = 0.2;
inline constexpr std::int64_t MG_ACCURACY_SPREAD_INITIAL = 1;
inline constexpr std::int64_t MG_ACCURACY_SPREAD_RANGE = 5;
inline constexpr double MG_ACCURACY_SPREAD_REDUCTION_SPEED = 0.6;
inline constexpr std::int64_t MG_AMMO = 999;
inline constexpr std::int64_t MG_AMMO_CLIP_SIZE = 100;
inline constexpr std::int64_t MG_AMMO_INITIAL_STOCK = 400;
inline constexpr std::int64_t MG_AMMO_MAX = 400;
inline constexpr std::int64_t MG_AMMO_RESTOCK_AMOUNT = 400;
inline constexpr double MG_BASE_MODEL_OFFSET_Z = -1.5;
inline constexpr std::int64_t MG_DAMAGE = 27;
inline constexpr std::int64_t MG_DAMAGE_ARMS = 20;
inline constexpr std::int64_t MG_DAMAGE_BLOCK = 2;
inline constexpr std::int64_t MG_DAMAGE_ENTITY = 20;
inline constexpr std::int64_t MG_DAMAGE_HEAD = 20;
inline constexpr std::int64_t MG_DAMAGE_LEGS = 20;
inline constexpr std::int64_t MG_DAMAGE_TORSO = 30;
inline constexpr double MG_DELAY = 0.11;
inline constexpr double MG_DEPLOYED_ACCURACY = 0.01;
inline constexpr double MG_DEPLOYED_ACCURACY_RANGE = 0.005;
inline constexpr double MG_DEPLOYED_ACCURACY_SPREAD_INCREASE_PER_SHOT = 0.2;
inline constexpr std::int64_t MG_DEPLOYED_ACCURACY_SPREAD_INITIAL = 1;
inline constexpr std::int64_t MG_DEPLOYED_ACCURACY_SPREAD_RANGE = 5;
inline constexpr double MG_DEPLOYED_ACCURACY_SPREAD_REDUCTION_SPEED = 0.6;
inline constexpr std::int64_t MG_DEPLOYED_DAMAGE_ARMS = 20;
inline constexpr std::int64_t MG_DEPLOYED_DAMAGE_BLOCK = 2;
inline constexpr std::int64_t MG_DEPLOYED_DAMAGE_ENTITY = 20;
inline constexpr std::int64_t MG_DEPLOYED_DAMAGE_HEAD = 20;
inline constexpr std::int64_t MG_DEPLOYED_DAMAGE_LEGS = 20;
inline constexpr std::int64_t MG_DEPLOYED_DAMAGE_TORSO = 30;
inline constexpr std::int64_t MG_DEPLOYED_RANGE = 300;
inline constexpr std::int64_t MG_DEPLOYED_RECOIL_SIDE = 0;
inline constexpr double MG_DEPLOYED_RECOIL_UP = -0.007;
inline constexpr std::int64_t MG_DEPLOYED_RELOAD_TIME = 4;
inline constexpr double MG_DEPLOYED_SHOOT_INTERVAL = 0.1;
inline constexpr std::int64_t MG_DEPLOYMENT_TIME = 3;
inline constexpr std::int64_t MG_EXPLOSION_BLOCK_DAMAGE = 5;
inline constexpr std::int64_t MG_EXPLOSION_DAMAGE = 100;
inline constexpr std::int64_t MG_EXPLOSION_KNOCKBACK_MAX = 1;
inline constexpr double MG_EXPLOSION_KNOCKBACK_MIN = 0.2;
inline constexpr std::int64_t MG_EXPLOSION_RADIUS = 3;
inline constexpr std::int64_t MG_FAR_RADIUS = 5;
inline constexpr std::int64_t MG_HEALTH = 100;
inline constexpr std::int64_t MG_HORIZONTAL_ANGLE_RANGE = 45;
inline constexpr double MG_MODEL_SIZE = 0.06;
inline constexpr std::int64_t MG_RANGE = 300;
inline constexpr std::int64_t MG_RECOIL_SIDE = 0;
inline constexpr double MG_RECOIL_UP = -0.007;
inline constexpr std::int64_t MG_RELOAD_TIME = 4;
inline constexpr double MG_SHOOT_INTERVAL = 0.5;
inline constexpr std::int64_t MG_TOOL = 15;
inline constexpr double MG_TOP_MODEL_OFFSET_Z = -1.5;
inline constexpr std::int64_t MG_VERTICAL_ANGLE_RANGE = 45;
inline constexpr double MG_WITHDRAWAL_TIME = 0.75;
inline constexpr std::int64_t MH_ASSAULT_SCORE_REASON = 81;
inline constexpr std::int64_t MH_CLAIM_SCORE_REASON = 78;
inline constexpr std::int64_t MH_CONTEST_SCORE_REASON = 82;
inline constexpr std::int64_t MH_CONTROL_SCORE_REASON = 79;
inline constexpr std::int64_t MH_DEFAULT_BASE_AUTO_TIMEOUT = 240;
inline constexpr std::int64_t MH_DEFAULT_NUMBER_OF_BASE_TO_ACTIVATE_AT_ONCE = 1;
inline constexpr std::int64_t MH_DEFEND_SCORE_REASON = 80;
inline constexpr std::int64_t MH_FIRST_SCORE_REASON = 77;
inline constexpr std::int64_t MH_GAME_LENGTH = 1500;
inline constexpr std::int64_t MH_MODE_SCORE_REASON = 187;
inline constexpr std::int64_t MH_OCCUPY_SCORE_REASON = 76;
inline constexpr std::int64_t MH_SCORE_ASSAULT = 50;
inline constexpr std::int64_t MH_SCORE_CLAIM = 150;
inline constexpr std::int64_t MH_SCORE_CONTEST = 50;
inline constexpr std::int64_t MH_SCORE_CONTROL = 100;
inline constexpr std::int64_t MH_SCORE_DEFEND = 100;
inline constexpr std::int64_t MH_SCORE_FIRST = 250;
inline constexpr std::int64_t MH_SCORE_OCCUPY = 150;
inline constexpr std::int64_t MH_SCORE_OCCUPY_INTERVAL = 5;
inline constexpr std::int64_t MH_SURVIVE_AIRSTRIKE_TOTAL = 83;
inline constexpr std::int64_t MH_TEAM_SCORE_PER_TICK = 1;
inline constexpr std::int64_t MH_TEAM_SCORE_TICK_RATE = 1;
inline constexpr std::int64_t MH_TIME_BETWEEN_BASE_ACTIVATIONS = 10;
inline constexpr std::int64_t MH_TOTAL_SCORE = 200;
inline constexpr std::int64_t MH_TRIGGER_AIRSTRIKE_TOTAL = 84;
inline constexpr double MINER_ACCEL_MULTIPLIER = 0.7;
inline constexpr std::int64_t MINER_C4_KILLS = 247;
inline constexpr double MINER_CROUCH_SNEAK_MULTIPLIER = 0.5;
inline constexpr double MINER_DAMAGE_MULTIPLIER = 1.1765;
inline constexpr std::int64_t MINER_DRILL_DEMOLISH_TOTAL = 126;
inline constexpr std::int64_t MINER_DYNAMITE_BELOW_KILLS = 127;
inline constexpr std::int64_t MINER_DYNAMITE_KILLS = 122;
inline constexpr std::int64_t MINER_FALLING_DAMAGE_MAX_DAMAGE = 100;
inline constexpr std::int64_t MINER_FALLING_DAMAGE_MAX_DISTANCE = 40;
inline constexpr std::int64_t MINER_FALLING_DAMAGE_MIN_DISTANCE = 10;
inline constexpr double MINER_FALL_ON_WATER_DAMAGE_MULTIPLIER = 0.5;
inline constexpr double MINER_HEADSHOT_DAMAGE_MULTIPLIER = 0.5;
inline constexpr double MINER_JUMP_MULTIPLIER = 1.2;
inline constexpr std::int64_t MINER_MAX_BLOCKS = 1000;
inline constexpr std::int64_t MINER_PICKAXE_KILLS = 125;
inline constexpr std::int64_t MINER_SHOTGUN2_KILLS = 121;
inline constexpr std::int64_t MINER_SHOTGUN_HEADSHOT_TOTAL = 128;
inline constexpr std::int64_t MINER_SHOTGUN_KILLS = 120;
inline constexpr std::int64_t MINER_SHOTGUN_ZOMBIE_KILLS = 129;
inline constexpr std::int64_t MINER_SNOWBLOWER_KILLS = 123;
inline constexpr double MINER_SPRINT_MULTIPLIER = 1.4;
inline constexpr std::int64_t MINER_STARTING_BLOCKS = 0;
inline constexpr std::int64_t MINER_SUPERSPADE_KILLS = 124;
inline constexpr std::int64_t MINER_WATER_FRICTION = 8;
inline constexpr std::int64_t MINE_KILL = 35;
inline constexpr std::int64_t MINE_LAUNCHER_DAMAGE = 40;
inline constexpr std::int64_t MINE_LAUNCHER_TOOL = 58;
inline constexpr double MINIGUN_ACCURACY = 0.015;
inline constexpr double MINIGUN_ACCURACY_RANGE = 0.015;
inline constexpr double MINIGUN_ACCURACY_SPREAD_INCREASE_PER_SHOT = 0.3;
inline constexpr std::int64_t MINIGUN_ACCURACY_SPREAD_INITIAL = 2;
inline constexpr std::int64_t MINIGUN_ACCURACY_SPREAD_RANGE = 5;
inline constexpr std::int64_t MINIGUN_ACCURACY_SPREAD_REDUCTION_SPEED = 2;
inline constexpr std::int64_t MINIGUN_AMMO_CLIP_SIZE = 100;
inline constexpr std::int64_t MINIGUN_AMMO_INITIAL_STOCK = 300;
inline constexpr std::int64_t MINIGUN_AMMO_MAX = 300;
inline constexpr std::int64_t MINIGUN_AMMO_RESTOCK_AMOUNT = 300;
inline constexpr double MINIGUN_BARREL_SPIN_SOUND_FADE_THRESHOLD = 0.03;
inline constexpr std::int64_t MINIGUN_BARREL_SPIN_SPEED_MAX = 5;
inline constexpr double MINIGUN_BARREL_SPIN_SPEED_MIN_TO_ALLOW_SHOOTING = 0.5;
inline constexpr std::int64_t MINIGUN_DAMAGE_ARMS = 15;
inline constexpr double MINIGUN_DAMAGE_BLOCK = 2.5;
inline constexpr std::int64_t MINIGUN_DAMAGE_ENTITY = 20;
inline constexpr std::int64_t MINIGUN_DAMAGE_HEAD = 30;
inline constexpr std::int64_t MINIGUN_DAMAGE_LEGS = 15;
inline constexpr std::int64_t MINIGUN_DAMAGE_TORSO = 15;
inline constexpr double MINIGUN_DELAY = 0.11;
inline constexpr std::int64_t MINIGUN_RANGE = 100;
inline constexpr double MINIGUN_RECOIL_SIDE = 0.00002;
inline constexpr double MINIGUN_RECOIL_UP = -0.003;
inline constexpr std::int64_t MINIGUN_RELOAD_TIME = 2;
inline constexpr double MINIGUN_SHOOT_INTERVAL = 0.3;
inline constexpr double MINIGUN_SHOOT_INTERVAL_ACTIVE_ALTERATION_PER_SECOND = -0.15;
inline constexpr double MINIGUN_SHOOT_INTERVAL_INACTIVE_ALTERATION_PER_SECOND = 0.075;
inline constexpr double MINIGUN_SHOOT_INTERVAL_RANGE = -0.2;
inline constexpr double MINIGUN_SHOOT_SOUND_LENGTH = 0.1;
inline constexpr std::int64_t MINIGUN_TOOL = 8;
inline constexpr std::int64_t MINIMAP_HEIGHT_ICON_THRESHOLD = 4;
inline constexpr double MIN_BLOCK_INTERVAL = 0.1;
inline constexpr std::int64_t MIN_TIME_BETWEEN_CANCELLED_KICK_VOTES = 45;
inline constexpr std::int64_t MIN_TIME_BETWEEN_KICK_VOTES = 300;
inline constexpr std::int64_t MODE_CCTF = 11;
inline constexpr std::int64_t MODE_CTF = 8;
inline constexpr std::int64_t MODE_DEMOLITION = 1;
inline constexpr std::int64_t MODE_DIAMONDMINE = 5;
inline constexpr std::int64_t MODE_MULTIHILL = 3;
inline constexpr std::int64_t MODE_NORMAL = 0;
inline constexpr std::int64_t MODE_OCCUPATION = 4;
inline constexpr std::int64_t MODE_TDM = 6;
inline constexpr std::int64_t MODE_TERRITORY = 9;
inline constexpr std::int64_t MODE_TUTORIAL = 10;
inline constexpr std::int64_t MODE_UGC = 12;
inline constexpr std::int64_t MODE_VIP = 7;
inline constexpr std::int64_t MODE_ZOMBIE = 2;
inline constexpr std::int64_t MOLOTOV_ACCURACY_SPREAD_INCREASE_PER_SHOT = 0;
inline constexpr std::int64_t MOLOTOV_ACCURACY_SPREAD_INITIAL = 6;
inline constexpr std::int64_t MOLOTOV_ACCURACY_SPREAD_RANGE = 0;
inline constexpr std::int64_t MOLOTOV_ACCURACY_SPREAD_REDUCTION_SPEED = 0;
inline constexpr std::int64_t MOLOTOV_DAMAGE = 24;
inline constexpr std::int64_t MOLOTOV_ENTITY = 27;
inline constexpr std::int64_t MOLOTOV_EXPLOSION_BLOCK_DAMAGE = 3;
inline constexpr std::int64_t MOLOTOV_EXPLOSION_DAMAGE = 50;
inline constexpr double MOLOTOV_EXPLOSION_KNOCKBACK_MAX = 0.1;
inline constexpr std::int64_t MOLOTOV_EXPLOSION_KNOCKBACK_MIN = 0;
inline constexpr std::int64_t MOLOTOV_EXPLOSION_RADIUS = 4;
inline constexpr std::int64_t MOLOTOV_GRAVITY_MULTIPLIER = 1;
inline constexpr std::int64_t MOLOTOV_INITIAL_STOCK = 3;
inline constexpr std::int64_t MOLOTOV_KILL = 24;
inline constexpr std::int64_t MOLOTOV_RESTOCK_AMOUNT = 3;
inline constexpr std::int64_t MOLOTOV_SHOOT_INTERVAL = 1;
inline constexpr std::int64_t MOLOTOV_SMOKE_GENERATION_PARTICLE_DECAY = -1;
inline constexpr std::int64_t MOLOTOV_SMOKE_GENERATION_PARTICLE_LIFESPAN = 2;
inline constexpr std::int64_t MOLOTOV_SMOKE_GENERATION_PARTICLE_MAX_SIZE = 5;
inline constexpr std::int64_t MOLOTOV_SMOKE_GENERATION_PARTICLE_MIN_SIZE = 3;
inline constexpr std::int64_t MOLOTOV_STOCK = 3;
inline constexpr std::int64_t MOLOTOV_THROW_MAX_CHARGE = 3;
inline constexpr std::int64_t MOLOTOV_THROW_MIN_SPEED = 35;
inline constexpr std::int64_t MOLOTOV_THROW_SPEED = 40;
inline constexpr std::int64_t MOLOTOV_TOOL = 33;
inline constexpr std::int64_t MONITOR_RETURN_INVALID_PARAMETER = 1;
inline constexpr std::int64_t MONITOR_RETURN_MONITOR_FULL = 3;
inline constexpr std::int64_t MONITOR_RETURN_REQUEST_TOO_FAST = 2;
inline constexpr std::int64_t MONITOR_RETURN_STEAM_AUTH_ERROR = 4;
inline constexpr std::int64_t MONITOR_RETURN_SUCCESS = 0;
inline constexpr std::int64_t MOST_AIRSTRIKES_SURVIVED = 19;
inline constexpr std::int64_t MOST_AMMO_CRATES_COLLECTED = 3;
inline constexpr std::int64_t MOST_ASSISTS = 18;
inline constexpr std::int64_t MOST_BLOCKS_DESTROYED = 10;
inline constexpr std::int64_t MOST_BLOCKS_PLACED = 9;
inline constexpr std::int64_t MOST_BLOCK_CRATES_COLLECTED = 4;
inline constexpr std::int64_t MOST_BRAINS_EATEN = 15;
inline constexpr std::int64_t MOST_DAMAGE_TAKEN = 20;
inline constexpr std::int64_t MOST_DEFENDS = 17;
inline constexpr std::int64_t MOST_DISTANCE_RAN = 0;
inline constexpr std::int64_t MOST_DISTRACTIONS = 16;
inline constexpr std::int64_t MOST_DOMINATED = 28;
inline constexpr std::int64_t MOST_DOMINATIONS = 29;
inline constexpr std::int64_t MOST_HEADSHOTS = 8;
inline constexpr std::int64_t MOST_HEADSHOTS_RECEIVED = 22;
inline constexpr std::int64_t MOST_HEALTH_CRATES_COLLECTED = 2;
inline constexpr std::int64_t MOST_KILLS = 5;
inline constexpr std::int64_t MOST_KILLS_AT_LOW_HEALTH = 6;
inline constexpr std::int64_t MOST_KILL_STEALS = 26;
inline constexpr std::int64_t MOST_MELEE_KILLS = 14;
inline constexpr std::int64_t MOST_SNIPERS_KILLED = 23;
inline constexpr std::int64_t MOST_SUICIDES = 25;
inline constexpr std::int64_t MOST_TEABAGS = 7;
inline constexpr std::int64_t MOST_TIME_IN_AIR = 1;
inline constexpr std::int64_t MOST_TIME_ON_FIRE = 27;
inline constexpr std::int64_t MOUNTABLE_DISTANCE = 3;
inline constexpr std::int64_t MULTIKILLMAXTIMEGAP = 6;
inline constexpr std::int64_t NETWORK_FPS = 30;
inline constexpr double NETWORK_RATE = 0.03333333333333333;
inline constexpr std::int64_t NEVER_CROSSHAIR = 0;
inline constexpr std::int64_t NEVER_RESPAWN_TIME = 255;
inline constexpr std::int64_t NEXT_MAP_MESSAGE = 0;
inline constexpr std::int64_t NOOF_DIRECTIONS = 4;
inline constexpr std::int64_t NOOF_GAME_STATS_TO_SHOW = 3;
inline constexpr std::int64_t NOOF_ORIENTATION_PITCHES = 4;
inline constexpr std::int64_t NOOF_ORIENTATION_ROLLS = 4;
inline constexpr std::int64_t NOOF_SELECTABLE_TOOLS = 65;
inline constexpr std::int64_t NORMAL_SCORE_TEABAG = 2;
inline constexpr std::int64_t NORTH = 0;
inline constexpr std::int64_t NOT_CLASSIC = 0;
inline constexpr std::int64_t NOT_SORTING = 0;
inline constexpr std::int64_t NO_JETPACK = 65;
inline constexpr double NO_PICKUP_AFTER_DROP_TIME = 2.5;
inline constexpr std::int64_t NO_SCORE_REASON = 0;
inline constexpr std::int64_t NULL_TOOL = 39;
inline constexpr std::int64_t NUMBER_OF_WEAPONS = 66;
inline constexpr std::int64_t NUM_JETPACK = 5;
inline constexpr std::int64_t OCCUPATION_WIN_MESSAGE = 5;
inline constexpr std::int64_t OCC_ASSAULT_SCORE_REASON = 32;
inline constexpr std::int64_t OCC_BOOM_SCORE_REASON = 28;
inline constexpr std::int64_t OCC_CARRIER_DEFEND_SCORE_REASON = 30;
inline constexpr std::int64_t OCC_CARRY_SCORE_REASON = 27;
inline constexpr std::int64_t OCC_DEFEND_SCORE_REASON = 31;
inline constexpr std::int64_t OCC_DISPOSAL_SCORE_REASON = 36;
inline constexpr std::int64_t OCC_DISTRACT_SCORE_REASON = 29;
inline constexpr std::int64_t OCC_INTERCEPT_DISPOSAL_SCORE_REASON = 37;
inline constexpr std::int64_t OCC_INTERCEPT_SCORE_REASON = 34;
inline constexpr std::int64_t OCC_LASTMAN_TOTAL = 35;
inline constexpr std::int64_t OCC_OCCUPY_SCORE_REASON = 26;
inline constexpr std::int64_t OCC_SURVIVE_SCORE_REASON = 33;
inline constexpr std::int64_t OCC_TOTAL_SCORE = 195;
inline constexpr std::int64_t OC_BOMB_RESPAWN_TIME_ON_EXPLOSION = 10;
inline constexpr std::int64_t OC_CARRIER_THREAT_RADIUS = 10;
inline constexpr std::int64_t OC_EXTRA_INDIVIDUAL_SCORE_FOR_KILL_CARRIER = 0;
inline constexpr std::int64_t OC_GAME_LENGTH = 900;
inline constexpr std::int64_t OC_MODE_SCORE_REASON = 185;
inline constexpr std::int64_t OC_SCORE_ASSAULT = 100;
inline constexpr std::int64_t OC_SCORE_CARRIER_DEFEND = 100;
inline constexpr std::int64_t OC_SCORE_CARRY_INTERVAL = 5;
inline constexpr std::int64_t OC_SCORE_CARRY_SCORE = 50;
inline constexpr std::int64_t OC_SCORE_DEFEND = 50;
inline constexpr std::int64_t OC_SCORE_DISTRACT = 100;
inline constexpr std::int64_t OC_SCORE_FOR_BOMB_EXPLOSION_IN_BASE = 50;
inline constexpr std::int64_t OC_SCORE_FOR_DISPOSAL = 25;
inline constexpr std::int64_t OC_SCORE_FOR_DISPOSAL_INTERCEPT = 25;
inline constexpr std::int64_t OC_SCORE_INTERCEPT = 50;
inline constexpr std::int64_t OC_SCORE_OCCUPY_INTERVAL = 5;
inline constexpr std::int64_t OC_SCORE_OCCUPY_SCORE = 50;
inline constexpr std::int64_t OC_SCORE_SURVIVE = 50;
inline constexpr std::int64_t OC_TEAM_SCORE_FOR_BOMB_EXPLOSION_IN_BASE = 3;
inline constexpr std::int64_t OC_TEAM_SCORE_FOR_KILLING_CARRIER = 1;
inline constexpr std::int64_t OC_THREAT_RADIUS = 20;
inline constexpr std::int64_t PACKET_COMPRESSION = 1;
inline constexpr std::int64_t PACKET_RECEIVED = 1;
inline constexpr std::int64_t PACKET_SENT = 0;
inline constexpr std::int64_t PACKET_SEQUENCED = 0;
inline constexpr std::int64_t PACKET_UNSEQUENCED = 1;
inline constexpr std::int64_t PAINTBRUSH_RANGE = 15;
inline constexpr std::int64_t PAINTBRUSH_SECONDARY_RADIUS = 3;
inline constexpr double PAINTBRUSH_SECONDARY_RADIUS_SEPARATION = 0.5;
inline constexpr double PAINTBRUSH_SECONDARY_RANDOM_VARIATION = 0.5;
inline constexpr double PAINTBRUSH_SHOOT_INTERVAL = 0.03;
inline constexpr std::int64_t PAINTBRUSH_TOOL = 43;
inline constexpr std::int64_t PAINT_PRIMARY_SOUND_ID = 47;
inline constexpr double PAINT_SECONDARY_FADE_IN_DURATION = 0.3;
inline constexpr double PAINT_SECONDARY_FADE_OUT_DURATION = 0.5;
inline constexpr std::int64_t PAN_CAMERA = 3;
inline constexpr std::int64_t PART_ARMS = 2;
inline constexpr std::int64_t PART_ENTITY1 = 7;
inline constexpr std::int64_t PART_ENTITY2 = 8;
inline constexpr std::int64_t PART_HEAD = 0;
inline constexpr std::int64_t PART_LEFT_LEG = 3;
inline constexpr std::int64_t PART_LEG_CROUCH = 6;
inline constexpr std::int64_t PART_RIGHT_LEG = 4;
inline constexpr std::int64_t PART_TORSO = 1;
inline constexpr std::int64_t PART_TORSO_CROUCH = 5;
inline constexpr std::int64_t PERIODIC_SOUND = 9;
inline constexpr std::int64_t PICKAXE_DAMAGE = 0;
inline constexpr std::int64_t PICKAXE_DAMAGE_AMOUNT = 9;
inline constexpr std::int64_t PICKAXE_HITPLAYER_DAMAGE_AMOUNT = 50;
inline constexpr std::int64_t PICKAXE_HIT_BLOCK_SOUND_ID = 36;
inline constexpr std::int64_t PICKAXE_HIT_WATER_SOUND_ID = 43;
inline constexpr double PICKAXE_SHOOT_INTERVAL = 0.4;
inline constexpr std::int64_t PICKAXE_TOOL = 0;
inline constexpr std::int64_t PICKUP_DISTANCE = 3;
inline constexpr double PISTOL_ACCURACY = 0.015;
inline constexpr std::int64_t PISTOL_AMMO_CLIP_SIZE = 6;
inline constexpr std::int64_t PISTOL_AMMO_INITIAL_STOCK = 30;
inline constexpr std::int64_t PISTOL_AMMO_MAX = 30;
inline constexpr std::int64_t PISTOL_AMMO_RESTOCK_AMOUNT = 30;
inline constexpr std::int64_t PISTOL_DAMAGE_ARMS = 20;
inline constexpr std::int64_t PISTOL_DAMAGE_BLOCK = 3;
inline constexpr std::int64_t PISTOL_DAMAGE_ENTITY = 20;
inline constexpr std::int64_t PISTOL_DAMAGE_HEAD = 50;
inline constexpr std::int64_t PISTOL_DAMAGE_LEGS = 20;
inline constexpr std::int64_t PISTOL_DAMAGE_TORSO = 20;
inline constexpr std::int64_t PISTOL_RANGE = 800;
inline constexpr std::int64_t PISTOL_RECOIL_SIDE = 0;
inline constexpr double PISTOL_RECOIL_UP = -0.005;
inline constexpr double PISTOL_RELOAD_TIME = 0.5;
inline constexpr double PISTOL_SHOOT_INTERVAL = 0.3;
inline constexpr std::int64_t PISTOL_TOOL = 17;
inline constexpr double PLAYER_CENTER_VERTICAL_OFFSET = 0.75;
inline constexpr double PLAYER_CROUCHING_HEIGHT = 1.8;
inline constexpr double PLAYER_CROUCHING_POS_ABOVE_GROUND = 1.35;
inline constexpr std::int64_t PLAYER_INTERACTION_EXPIRY_SECONDS = 5;
inline constexpr double PLAYER_NAME_SCALE = 0.0075;
inline constexpr double PLAYER_RADIUS = 0.45;
inline constexpr std::int64_t PLAYER_SAFE_BUILD_RADIUS = 5;
inline constexpr std::int64_t PLAYER_SCORE_TICKER_SPEED = 2;
inline constexpr double PLAYER_STANDING_HEIGHT = 2.7;
inline constexpr double PLAYER_STANDING_POS_ABOVE_GROUND = 2.25;
inline constexpr std::int64_t PREFABBUILD_SOUND_ID = 32;
inline constexpr double PREFAB_BUILDING_FRAME_TIME = 0.001;
inline constexpr std::int64_t PREFAB_CONSTANT_DISTANCE_FROM_PLAYER = 0;
inline constexpr std::int64_t PREFAB_FAR_RADIUS = 20;
inline constexpr double PREFAB_INITIAL_VERTICAL_OFFSET = -0.8;
inline constexpr std::int64_t PREFAB_MAX_DISTANCE_FROM_PLAYER = 3;
inline constexpr std::int64_t PREFAB_MIN_DISTANCE_FROM_PLAYER = 2;
inline constexpr std::int64_t PREFAB_SCALED_DISTANCE_FROM_PLAYER = 1;
inline constexpr std::int64_t PREFAB_SERVER_TOLERANCE = 5;
inline constexpr std::int64_t PREFAB_SPRINT_SPEED = 10;
inline constexpr std::int64_t PREFAB_TOOL = 23;
inline constexpr std::int64_t PROGRESSBAR_ICON_BASE = 0;
inline constexpr std::int64_t PROGRESSBAR_ICON_DIAMOND = 1;
inline constexpr std::int64_t PROTOCOL_VERSION = 168;
inline constexpr double QUANTIZED_INTERVAL_BLOCK_DAMAGE = 0.25;
inline constexpr std::int64_t QUANTIZED_INVERSE_BLOCK_DAMAGE = 4;
inline constexpr std::int64_t RADAR_STATION_KILL = 33;
inline constexpr std::int64_t RADAR_STATION_TOOL = 56;
inline constexpr std::int64_t RADIUS_BLOCK_DAMAGE_RANDOM_EXTRA = 2;
inline constexpr double RANKUP_BAR_BEFORE_RANKUP_DELAY = 0.75;
inline constexpr std::int64_t RANKUP_BAR_CONTINUE_DELAY = 2;
inline constexpr double RANKUP_BAR_RANKUP_TIME = 1.5;
inline constexpr double RANKUP_BAR_TOTAL_TIME = 4.25;
inline constexpr double RANKUP_FADEIN_TIME = 0.5;
inline constexpr double RANKUP_FADEOUT_TIME = 0.5;
inline constexpr double RANKUP_LEVEL_UP_BOX_FADE_DOWN_TIMER = 0.3;
inline constexpr double RANKUP_LEVEL_UP_BOX_FADE_UP_TIMER = 0.3;
inline constexpr double RANKUP_LEVEL_UP_TEXT_SCALE_DOWN_TIMER = 0.4;
inline constexpr double RANKUP_LEVEL_UP_TEXT_SCALE_FROM = 0.3;
inline constexpr std::int64_t RANKUP_LEVEL_UP_TEXT_SCALE_TO = 3;
inline constexpr std::int64_t RANKUP_LEVEL_UP_TEXT_SCALE_UP_TIMER = 0;
inline constexpr double RANKUP_STATS_INITIAL_DELAY = 5.5;
inline constexpr std::int64_t RANKUP_TOLERANCE = 10;
inline constexpr std::int64_t RAPID_WINDOW_ENTRIES = 10;
inline constexpr std::int64_t RECENT_KILLS_EXPIRY_SECONDS = 30;
inline constexpr std::int64_t REGION_AUSTRALIA = 3;
inline constexpr std::int64_t REGION_EUROPE = 2;
inline constexpr std::int64_t REGION_US_EAST = 1;
inline constexpr std::int64_t REGION_US_WEST = 0;
inline constexpr double REVERB_AMBIENCE_DUCKING_RANGE = 0.8;
inline constexpr double REVERB_DECAY_TIME_SCALE = 0.1;
inline constexpr double REVERB_FADE_AMOUNT = 0.03;
inline constexpr double REVERB_GAINHF = 0.89;
inline constexpr double REVERB_MAX_DECAY_TIME = 2.2;
inline constexpr double REVERB_MAX_GAIN = 0.1;
inline constexpr std::int64_t REVERB_MAX_WALL_DISTANCE = 40;
inline constexpr std::int64_t REVERB_MIN_WALLS = 6;
inline constexpr double RIFLE_ACCURACY = 0.003;
inline constexpr std::int64_t RIFLE_AMMO_CLIP_SIZE = 10;
inline constexpr std::int64_t RIFLE_AMMO_INITIAL_STOCK = 30;
inline constexpr std::int64_t RIFLE_AMMO_MAX = 50;
inline constexpr std::int64_t RIFLE_AMMO_RESTOCK_AMOUNT = 50;
inline constexpr std::int64_t RIFLE_DAMAGE_ARMS = 35;
inline constexpr std::int64_t RIFLE_DAMAGE_BLOCK = 2;
inline constexpr std::int64_t RIFLE_DAMAGE_ENTITY = 25;
inline constexpr std::int64_t RIFLE_DAMAGE_HEAD = 150;
inline constexpr std::int64_t RIFLE_DAMAGE_LEGS = 35;
inline constexpr std::int64_t RIFLE_DAMAGE_TORSO = 70;
inline constexpr double RIFLE_DELAY = 0.5;
inline constexpr std::int64_t RIFLE_RANGE = 10000;
inline constexpr double RIFLE_RECOIL_SIDE = 0.0001;
inline constexpr double RIFLE_RECOIL_UP = -0.05;
inline constexpr double RIFLE_RELOAD_TIME = 2.5;
inline constexpr double RIFLE_SHOOT_INTERVAL = 0.5;
inline constexpr std::int64_t RIFLE_TOOL = 6;
inline constexpr std::int64_t RIOTSHIELD_DAMAGE = 36;
inline constexpr std::int64_t RIOTSHIELD_TOOL = 52;
inline constexpr std::int64_t RIOTSTICK_DAMAGE = 34;
inline constexpr std::int64_t RIOTSTICK_TOOL = 49;
inline constexpr double ROCKET2_COLLISION_RANGE = 0.5;
inline constexpr std::int64_t ROCKET2_DAMAGE = 9;
inline constexpr std::int64_t ROCKET2_ENTITY = 22;
inline constexpr std::int64_t ROCKET2_EXPLOSION_BLAST_WAVE_RADIUS = 4;
inline constexpr std::int64_t ROCKET2_EXPLOSION_BLOCK_DAMAGE = 2;
inline constexpr std::int64_t ROCKET2_EXPLOSION_DAMAGE = 50;
inline constexpr double ROCKET2_EXPLOSION_KNOCKBACK_MAX = 0.25;
inline constexpr std::int64_t ROCKET2_EXPLOSION_KNOCKBACK_MIN = 0;
inline constexpr std::int64_t ROCKET2_EXPLOSION_RADIUS = 4;
inline constexpr double ROCKET2_EXPLOSION_SELF_KNOCKBACK_MAX = 1.5;
inline constexpr std::int64_t ROCKET2_EXPLOSION_SELF_KNOCKBACK_MIN = 1;
inline constexpr double ROCKET2_GRAVITY_MULTIPLIER = 0.025;
inline constexpr std::int64_t ROCKET2_HEALTH = 1;
inline constexpr std::int64_t ROCKET2_KILL = 5;
inline constexpr double ROCKET2_MODEL_SIZE = 0.06;
inline constexpr std::int64_t ROCKET2_MODEL_Z_OFFSET = 0;
inline constexpr std::int64_t ROCKET2_SPEED = 150;
inline constexpr double ROCKETEER_ACCEL_MULTIPLIER = 0.7;
inline constexpr double ROCKETEER_CROUCH_SNEAK_MULTIPLIER = 0.5;
inline constexpr double ROCKETEER_DAMAGE_MULTIPLIER = 1.43;
inline constexpr std::int64_t ROCKETEER_FALLING_DAMAGE_MAX_DAMAGE = 10;
inline constexpr std::int64_t ROCKETEER_FALLING_DAMAGE_MAX_DISTANCE = 10;
inline constexpr std::int64_t ROCKETEER_FALLING_DAMAGE_MIN_DISTANCE = 10;
inline constexpr double ROCKETEER_FALL_ON_WATER_DAMAGE_MULTIPLIER = 0.5;
inline constexpr std::int64_t ROCKETEER_GRENADE_KILLS = 114;
inline constexpr double ROCKETEER_HEADSHOT_DAMAGE_MULTIPLIER = 1.5;
inline constexpr std::int64_t ROCKETEER_JETPACK_GRENADE_KILLS = 118;
inline constexpr std::int64_t ROCKETEER_JETPACK_KILLS = 117;
inline constexpr std::int64_t ROCKETEER_JETPACK_SMG_KILLS = 119;
inline constexpr std::int64_t ROCKETEER_JUMP_MULTIPLIER = 1;
inline constexpr std::int64_t ROCKETEER_MAX_BLOCKS = 1500;
inline constexpr std::int64_t ROCKETEER_PICKAXE_KILLS = 116;
inline constexpr std::int64_t ROCKETEER_SMG_KILLS = 112;
inline constexpr std::int64_t ROCKETEER_SPADE_KILLS = 115;
inline constexpr double ROCKETEER_SPRINT_MULTIPLIER = 1.1;
inline constexpr std::int64_t ROCKETEER_STARTING_BLOCKS = 500;
inline constexpr std::int64_t ROCKETEER_TURRET_KILLS = 113;
inline constexpr std::int64_t ROCKETEER_WATER_FRICTION = 12;
inline constexpr double ROCKET_COLLISION_RANGE = 0.5;
inline constexpr std::int64_t ROCKET_DAMAGE = 8;
inline constexpr std::int64_t ROCKET_ENTITY = 21;
inline constexpr std::int64_t ROCKET_EXPLOSION_BLAST_WAVE_RADIUS = 6;
inline constexpr std::int64_t ROCKET_EXPLOSION_BLOCK_DAMAGE = 5;
inline constexpr std::int64_t ROCKET_EXPLOSION_DAMAGE = 140;
inline constexpr double ROCKET_EXPLOSION_KNOCKBACK_MAX = 0.25;
inline constexpr std::int64_t ROCKET_EXPLOSION_KNOCKBACK_MIN = 0;
inline constexpr std::int64_t ROCKET_EXPLOSION_RADIUS = 4;
inline constexpr std::int64_t ROCKET_FALLOFF = 25;
inline constexpr double ROCKET_GRAVITY_MULTIPLIER = 0.05;
inline constexpr std::int64_t ROCKET_HEALTH = 1;
inline constexpr double ROCKET_JUMP_FALL_DAMAGE_MULTIPLIER = 0.2;
inline constexpr std::int64_t ROCKET_KILL = 4;
inline constexpr double ROCKET_MODEL_SIZE = 0.06;
inline constexpr std::int64_t ROCKET_MODEL_Z_OFFSET = 0;
inline constexpr std::int64_t ROCKET_SMOKE_DECAY_RATE_DEFAULT = -1;
inline constexpr std::int64_t ROCKET_SMOKE_DECAY_RATE_MAX = 1;
inline constexpr std::int64_t ROCKET_SMOKE_DECAY_RATE_MIN = 1;
inline constexpr std::int64_t ROCKET_SMOKE_INITIAL_ROTATION_RANDOM_MAX = 200;
inline constexpr std::int64_t ROCKET_SMOKE_INITIAL_ROTATION_RANDOM_MIN = 160;
inline constexpr std::int64_t ROCKET_SMOKE_INITIAL_SIZE_RANDOM_MAX = 6;
inline constexpr std::int64_t ROCKET_SMOKE_INITIAL_SIZE_RANDOM_MIN = 3;
inline constexpr double ROCKET_SMOKE_LIFETIME = 2.5;
inline constexpr double ROCKET_SMOKE_VELOCITY_MULTIPLIER = 0.7;
inline constexpr double ROCKET_SMOKE_VELOCITY_RANDOM_MAX_MULTIPLIER = 0.05;
inline constexpr std::int64_t ROCKET_SMOKE_VELOCITY_RANDOM_MIN_MULTIPLIER = 0;
inline constexpr std::int64_t ROCKET_SPEED = 75;
inline constexpr std::int64_t ROCKET_TURRET_AIMING_SPEED = 180;
inline constexpr std::int64_t ROCKET_TURRET_AMMO = 10;
inline constexpr std::int64_t ROCKET_TURRET_AMMO_TEXT_RADIUS = 20;
inline constexpr double ROCKET_TURRET_BALL_MODEL_OFFSET_Z = -1.02;
inline constexpr double ROCKET_TURRET_BASE_MODEL_OFFSET_Z = -0.18;
inline constexpr std::int64_t ROCKET_TURRET_DAMAGE = 12;
inline constexpr std::int64_t ROCKET_TURRET_DETECTION_RANGE = 30;
inline constexpr std::int64_t ROCKET_TURRET_ENTITY = 8;
inline constexpr std::int64_t ROCKET_TURRET_EXPLOSION_BLOCK_DAMAGE = 15;
inline constexpr std::int64_t ROCKET_TURRET_EXPLOSION_DAMAGE = 100;
inline constexpr std::int64_t ROCKET_TURRET_EXPLOSION_KNOCKBACK_MAX = 1;
inline constexpr double ROCKET_TURRET_EXPLOSION_KNOCKBACK_MIN = 0.2;
inline constexpr std::int64_t ROCKET_TURRET_EXPLOSION_RADIUS = 3;
inline constexpr std::int64_t ROCKET_TURRET_FAR_RADIUS = 10;
inline constexpr double ROCKET_TURRET_GUN_MODEL_OFFSET_Z = -0.6599999999999999;
inline constexpr std::int64_t ROCKET_TURRET_HEALTH = 100;
inline constexpr std::int64_t ROCKET_TURRET_INITIAL_STOCK = 2;
inline constexpr std::int64_t ROCKET_TURRET_KILL = 18;
inline constexpr std::int64_t ROCKET_TURRET_LOWER_PITCH_LIMIT = 30;
inline constexpr double ROCKET_TURRET_MODEL_SIZE = 0.06;
inline constexpr std::int64_t ROCKET_TURRET_RESTOCK_AMOUNT = 2;
inline constexpr std::int64_t ROCKET_TURRET_ROCKET_DAMAGE = 21;
inline constexpr std::int64_t ROCKET_TURRET_ROCKET_EXPLOSION_BLOCK_DAMAGE = 10;
inline constexpr std::int64_t ROCKET_TURRET_ROCKET_EXPLOSION_DAMAGE = 50;
inline constexpr double ROCKET_TURRET_ROCKET_EXPLOSION_KNOCKBACK_MAX = 0.3;
inline constexpr double ROCKET_TURRET_ROCKET_EXPLOSION_KNOCKBACK_MIN = 0.1;
inline constexpr std::int64_t ROCKET_TURRET_ROCKET_EXPLOSION_RADIUS = 3;
inline constexpr double ROCKET_TURRET_SHOOT_INTERVAL = 1.5;
inline constexpr std::int64_t ROCKET_TURRET_STOCK = 4;
inline constexpr double ROCKET_TURRET_TOLERANCE = 0.1;
inline constexpr std::int64_t ROCKET_TURRET_TOOL = 16;
inline constexpr std::int64_t ROCKET_TURRET_TRACKING_RANGE = 50;
inline constexpr double RPG2_ACCURACY = 0.02;
inline constexpr std::int64_t RPG2_AMMO_CLIP_SIZE = 3;
inline constexpr std::int64_t RPG2_AMMO_INITIAL_STOCK = 6;
inline constexpr std::int64_t RPG2_AMMO_MAX = 6;
inline constexpr std::int64_t RPG2_AMMO_RESTOCK_AMOUNT = 6;
inline constexpr double RPG2_DELAY = 0.75;
inline constexpr std::int64_t RPG2_RECOIL_SIDE = 0;
inline constexpr double RPG2_RECOIL_UP = -0.05;
inline constexpr std::int64_t RPG2_RELOAD_TIME = 1;
inline constexpr double RPG2_SHOOT_INTERVAL = 0.75;
inline constexpr std::int64_t RPG2_TOOL = 13;
inline constexpr double RPG_ACCURACY = 0.01;
inline constexpr std::int64_t RPG_AMMO_CLIP_SIZE = 1;
inline constexpr std::int64_t RPG_AMMO_INITIAL_STOCK = 3;
inline constexpr std::int64_t RPG_AMMO_MAX = 3;
inline constexpr std::int64_t RPG_AMMO_RESTOCK_AMOUNT = 3;
inline constexpr std::int64_t RPG_DELAY = 1;
inline constexpr double RPG_RECOIL_SIDE = 0.0001;
inline constexpr double RPG_RECOIL_UP = -0.1;
inline constexpr double RPG_RELOAD_TIME = 1.5;
inline constexpr double RPG_SHOOT_INTERVAL = 0.7;
inline constexpr std::int64_t RPG_TOOL = 12;
inline constexpr std::int64_t RUBBERBAND_DISTANCE = 99999;
inline constexpr std::int64_t RULE_DISABLED = 0;
inline constexpr std::int64_t RULE_ENABLED = 1;
inline constexpr std::int64_t SCORE_PLAYER = 1;
inline constexpr std::int64_t SCORE_RETURN_FAILED = 2;
inline constexpr std::int64_t SCORE_RETURN_INTERNAL_ERROR = 4;
inline constexpr std::int64_t SCORE_RETURN_INVALID_PARAMETER = 3;
inline constexpr std::int64_t SCORE_RETURN_SUCCESS = 1;
inline constexpr std::int64_t SCORE_TEAM = 0;
inline constexpr double SCOUT_ACCEL_MULTIPLIER = 0.7;
inline constexpr double SCOUT_CROUCH_SNEAK_MULTIPLIER = 0.5;
inline constexpr double SCOUT_DAMAGE_MULTIPLIER = 1.43;
inline constexpr std::int64_t SCOUT_FALLING_DAMAGE_MAX_DAMAGE = 100;
inline constexpr std::int64_t SCOUT_FALLING_DAMAGE_MAX_DISTANCE = 30;
inline constexpr std::int64_t SCOUT_FALLING_DAMAGE_MIN_DISTANCE = 10;
inline constexpr double SCOUT_FALL_ON_WATER_DAMAGE_MULTIPLIER = 0.5;
inline constexpr double SCOUT_HEADSHOT_DAMAGE_MULTIPLIER = 1.5;
inline constexpr double SCOUT_JUMP_MULTIPLIER = 1.5;
inline constexpr std::int64_t SCOUT_KNIFE_KILLS = 107;
inline constexpr std::int64_t SCOUT_LANDMINE_KILLS = 104;
inline constexpr std::int64_t SCOUT_MAX_BLOCKS = 1000;
inline constexpr std::int64_t SCOUT_PICKAXE_KILLS = 106;
inline constexpr std::int64_t SCOUT_SNIPER2_KILLS = 103;
inline constexpr std::int64_t SCOUT_SNIPER2_SPEED_TOTAL = 111;
inline constexpr std::int64_t SCOUT_SNIPER_HEADSHOT_TOTAL = 110;
inline constexpr std::int64_t SCOUT_SNIPER_KILLS = 102;
inline constexpr std::int64_t SCOUT_SNIPER_STREAK3_TOTAL = 108;
inline constexpr std::int64_t SCOUT_SNIPER_STREAK6_TOTAL = 109;
inline constexpr std::int64_t SCOUT_SNOWBLOWER_KILLS = 105;
inline constexpr double SCOUT_SPRINT_MULTIPLIER = 1.45;
inline constexpr std::int64_t SCOUT_STARTING_BLOCKS = 400;
inline constexpr std::int64_t SCOUT_WATER_FRICTION = 8;
inline constexpr std::int64_t SCREENSHOT_CAMERA_GAME_LENGTH = 60;
inline constexpr std::int64_t SCREENSHOT_CAMERA_PAN_DISTANCE = -5;
inline constexpr std::int64_t SCREENSHOT_CAMERA_PAN_TIME = 5;
inline constexpr std::int64_t SCREENSHOT_CAMERA_SWITCH_INTERVAL = 5;
inline constexpr double SECONDARY_MUSIC_BED_FADE_TIME = 1.5;
inline constexpr double SELF_EXPLOSION_DAMAGE_REDUCTION = 0.5;
inline constexpr std::int64_t SERVERMODE_ANY = 15;
inline constexpr std::int64_t SERVERMODE_CUSTOM = 8;
inline constexpr std::int64_t SERVERMODE_MONITOR = 16;
inline constexpr std::int64_t SERVERMODE_NOOF = 5;
inline constexpr std::int64_t SERVERMODE_PUBLIC = 1;
inline constexpr std::int64_t SERVERMODE_RANKED = 2;
inline constexpr std::int64_t SERVERMODE_TUTORIAL = 4;
inline constexpr std::int64_t SERVER_SHOOT_INTERVAL_TOLERANCE = 0;
inline constexpr std::int64_t SERVER_TIMEOUT_AFTER_FIRST_RESPONSE = 30;
inline constexpr std::int64_t SERVER_TIMEOUT_AFTER_FIRST_RESPONSE_IN_UGC_MODE = 60;
inline constexpr std::int64_t SERVER_TIMEOUT_BEFORE_FIRST_RESPONSE = 20;
inline constexpr std::int64_t SET_AMMO = 7;
inline constexpr std::int64_t SET_CHASE_CAM = 9;
inline constexpr std::int64_t SET_FORWARD_VECTOR = 4;
inline constexpr std::int64_t SET_FUSE = 6;
inline constexpr std::int64_t SET_HIGH_MINIMAP_VISIBILITY = 8;
inline constexpr std::int64_t SET_PLAYER = 3;
inline constexpr std::int64_t SET_PLAYER_SCORE = 1;
inline constexpr std::int64_t SET_POSITION = 1;
inline constexpr std::int64_t SET_STATE = 0;
inline constexpr std::int64_t SET_TARGET = 5;
inline constexpr std::int64_t SET_TEAM_SCORE = 0;
inline constexpr std::int64_t SET_VELOCITY = 2;
inline constexpr std::int64_t SHOPITEM_INVALID = 0;
inline constexpr std::int64_t SHOPITEM_MAFIA_PACK = 1;
inline constexpr std::int64_t SHOP_CATEGORY_ALL = 1;
inline constexpr std::int64_t SHOP_CATEGORY_EQUIPMENT = 6;
inline constexpr std::int64_t SHOP_CATEGORY_INVALID = 0;
inline constexpr std::int64_t SHOP_CATEGORY_MAPS = 8;
inline constexpr std::int64_t SHOP_CATEGORY_MODES = 5;
inline constexpr std::int64_t SHOP_CATEGORY_NEW = 4;
inline constexpr std::int64_t SHOP_CATEGORY_PACKS = 7;
inline constexpr std::int64_t SHOP_CATEGORY_SALE = 3;
inline constexpr std::int64_t SHOP_CATEGORY_TOPSELLERS = 2;
inline constexpr std::int64_t SHOP_CONNECTION_TIMEOUT = 5;
inline constexpr std::int64_t SHOP_MANAGER_CANCEL = 1;
inline constexpr std::int64_t SHOP_MANAGER_FINALIZE = 0;
inline constexpr std::int64_t SHOP_MANAGER_LIST_REQUEST = 0;
inline constexpr std::int64_t SHOP_MANAGER_PURCHASE_FINALIZE = 2;
inline constexpr std::int64_t SHOP_MANAGER_PURCHASE_REQUEST = 1;
inline constexpr std::int64_t SHOP_RETURN_ACCOUNT_DISABLED = 102;
inline constexpr std::int64_t SHOP_RETURN_ACCOUNT_DOESNT_EXIST = 9;
inline constexpr std::int64_t SHOP_RETURN_ACCOUNT_NOT_ALLOWED_TO_PURCHASE = 103;
inline constexpr std::int64_t SHOP_RETURN_CURRENCY_DOES_NOT_MATCH_USERS = 8;
inline constexpr std::int64_t SHOP_RETURN_FAILED = 2;
inline constexpr std::int64_t SHOP_RETURN_INSUFFICIENT_FUNDS = 100;
inline constexpr std::int64_t SHOP_RETURN_INTERNAL_ERROR = 4;
inline constexpr std::int64_t SHOP_RETURN_INVALID_PARAMETER = 3;
inline constexpr std::int64_t SHOP_RETURN_NO_ITEMS_SENT_OR_ALREADY_OWN_THEM = 200;
inline constexpr std::int64_t SHOP_RETURN_SUCCESS = 1;
inline constexpr std::int64_t SHOP_RETURN_TIME_LIMIT_TO_FINALIZE_EXCEEEDED = 101;
inline constexpr std::int64_t SHOP_RETURN_TRANSACTION_ALREADY_COMMITTED = 6;
inline constexpr std::int64_t SHOP_RETURN_TRANSACTION_ALREADY_REFUNDED = 201;
inline constexpr std::int64_t SHOP_RETURN_TRANSACTION_DENIED_FRAUD_DETECTION = 104;
inline constexpr std::int64_t SHOP_RETURN_TRANSACTION_DENIED_USER_IN_RESTRICTED_COUNTRY = 11;
inline constexpr std::int64_t SHOP_RETURN_USER_DENIED_TRANSACTION = 10;
inline constexpr std::int64_t SHOP_RETURN_USER_HAS_NOT_APPROVED_TRANSACTION = 5;
inline constexpr std::int64_t SHOP_RETURN_USER_NOT_LOGGED_IN = 7;
inline constexpr double SHOTGUN2_ACCURACY = 0.05;
inline constexpr double SHOTGUN2_ACCURACY_RANGE = 0.05;
inline constexpr double SHOTGUN2_ACCURACY_SPREAD_INCREASE_PER_SHOT = 0.5;
inline constexpr std::int64_t SHOTGUN2_ACCURACY_SPREAD_INITIAL = 4;
inline constexpr std::int64_t SHOTGUN2_ACCURACY_SPREAD_RANGE = 3;
inline constexpr std::int64_t SHOTGUN2_ACCURACY_SPREAD_REDUCTION_SPEED = 1;
inline constexpr std::int64_t SHOTGUN2_AMMO_CLIP_SIZE = 2;
inline constexpr std::int64_t SHOTGUN2_AMMO_INITIAL_STOCK = 14;
inline constexpr std::int64_t SHOTGUN2_AMMO_MAX = 14;
inline constexpr std::int64_t SHOTGUN2_AMMO_RESTOCK_AMOUNT = 14;
inline constexpr std::int64_t SHOTGUN2_DAMAGE_ARMS = 50;
inline constexpr double SHOTGUN2_DAMAGE_BLOCK = 2.5;
inline constexpr std::int64_t SHOTGUN2_DAMAGE_ENTITY = 25;
inline constexpr std::int64_t SHOTGUN2_DAMAGE_HEAD = 50;
inline constexpr std::int64_t SHOTGUN2_DAMAGE_LEGS = 50;
inline constexpr std::int64_t SHOTGUN2_DAMAGE_TORSO = 40;
inline constexpr std::int64_t SHOTGUN2_DELAY = 1;
inline constexpr std::int64_t SHOTGUN2_NUMBER_PELLETS = 10;
inline constexpr std::int64_t SHOTGUN2_RANGE = 20;
inline constexpr double SHOTGUN2_RECOIL_SIDE = 0.001;
inline constexpr double SHOTGUN2_RECOIL_UP = -0.25;
inline constexpr std::int64_t SHOTGUN2_RELOAD_TIME = 1;
inline constexpr std::int64_t SHOTGUN2_SHOOT_INTERVAL = 1;
inline constexpr std::int64_t SHOTGUN2_TOOL = 10;
inline constexpr double SHOTGUN_ACCURACY = 0.04;
inline constexpr double SHOTGUN_ACCURACY_RANGE = 0.04;
inline constexpr double SHOTGUN_ACCURACY_SPREAD_INCREASE_PER_SHOT = 0.5;
inline constexpr std::int64_t SHOTGUN_ACCURACY_SPREAD_INITIAL = 4;
inline constexpr std::int64_t SHOTGUN_ACCURACY_SPREAD_RANGE = 3;
inline constexpr std::int64_t SHOTGUN_ACCURACY_SPREAD_REDUCTION_SPEED = 1;
inline constexpr std::int64_t SHOTGUN_AMMO_CLIP_SIZE = 5;
inline constexpr std::int64_t SHOTGUN_AMMO_INITIAL_STOCK = 20;
inline constexpr std::int64_t SHOTGUN_AMMO_MAX = 20;
inline constexpr std::int64_t SHOTGUN_AMMO_RESTOCK_AMOUNT = 20;
inline constexpr std::int64_t SHOTGUN_DAMAGE_ARMS = 12;
inline constexpr std::int64_t SHOTGUN_DAMAGE_BLOCK = 1;
inline constexpr std::int64_t SHOTGUN_DAMAGE_ENTITY = 25;
inline constexpr std::int64_t SHOTGUN_DAMAGE_HEAD = 30;
inline constexpr std::int64_t SHOTGUN_DAMAGE_LEGS = 12;
inline constexpr std::int64_t SHOTGUN_DAMAGE_TORSO = 20;
inline constexpr std::int64_t SHOTGUN_DELAY = 1;
inline constexpr std::int64_t SHOTGUN_NUMBER_PELLETS = 10;
inline constexpr std::int64_t SHOTGUN_RANGE = 60;
inline constexpr double SHOTGUN_RECOIL_SIDE = 0.0002;
inline constexpr double SHOTGUN_RECOIL_UP = -0.1;
inline constexpr double SHOTGUN_RELOAD_TIME = 0.5;
inline constexpr std::int64_t SHOTGUN_SHOOT_INTERVAL = 1;
inline constexpr std::int64_t SHOTGUN_TOOL = 9;
inline constexpr std::int64_t SHRAPNEL_KILL = 19;
inline constexpr std::int64_t SHRAPNEL_TOOL = 27;
inline constexpr std::int64_t SHRAPNEL_TRACER_SPEED = 200;
inline constexpr std::int64_t SIMULATE_LAG_RECEIVE = 1;
inline constexpr std::int64_t SIMULATE_LAG_TRANSMIT = 0;
inline constexpr double SIMULATE_LATENCY = 0.05;
inline constexpr double SMG_ACCURACY = 0.02;
inline constexpr double SMG_ACCURACY_RANGE = 0.02;
inline constexpr double SMG_ACCURACY_SPREAD_INCREASE_PER_SHOT = 0.2;
inline constexpr std::int64_t SMG_ACCURACY_SPREAD_INITIAL = 1;
inline constexpr std::int64_t SMG_ACCURACY_SPREAD_RANGE = 5;
inline constexpr std::int64_t SMG_ACCURACY_SPREAD_REDUCTION_SPEED = 1;
inline constexpr std::int64_t SMG_AMMO_CLIP_SIZE = 25;
inline constexpr std::int64_t SMG_AMMO_INITIAL_STOCK = 100;
inline constexpr std::int64_t SMG_AMMO_MAX = 100;
inline constexpr std::int64_t SMG_AMMO_RESTOCK_AMOUNT = 100;
inline constexpr std::int64_t SMG_DAMAGE_ARMS = 10;
inline constexpr std::int64_t SMG_DAMAGE_BLOCK = 1;
inline constexpr std::int64_t SMG_DAMAGE_ENTITY = 15;
inline constexpr std::int64_t SMG_DAMAGE_HEAD = 15;
inline constexpr std::int64_t SMG_DAMAGE_LEGS = 10;
inline constexpr std::int64_t SMG_DAMAGE_TORSO = 10;
inline constexpr double SMG_DELAY = 0.11;
inline constexpr std::int64_t SMG_RANGE = 250;
inline constexpr double SMG_RECOIL_SIDE = 0.00002;
inline constexpr double SMG_RECOIL_UP = -0.01;
inline constexpr double SMG_RELOAD_TIME = 1.25;
inline constexpr double SMG_SHOOT_INTERVAL = 0.1;
inline constexpr std::int64_t SMG_TOOL = 7;
inline constexpr double SMOKE_INTERVAL = 1.2;
inline constexpr double SMOKE_RING_DECAY_RATE_MAX = 0.5;
inline constexpr std::int64_t SMOKE_RING_DECAY_RATE_MIN = 0;
inline constexpr std::int64_t SMOKE_RING_LIFETIME = 1;
inline constexpr std::int64_t SMOKE_RING_NOOF = 8;
inline constexpr std::int64_t SMOKE_RING_PARTICLE_SIZE_MAX = 10;
inline constexpr std::int64_t SMOKE_RING_PARTICLE_SIZE_MIN = 3;
inline constexpr std::int64_t SMOKE_RING_SIZE = 1;
inline constexpr double SMOKE_RING_VELOCITY = 0.5;
inline constexpr double SNIPER2_ACCURACY = 0.025;
inline constexpr std::int64_t SNIPER2_ACCURACY_ZOOM = 0;
inline constexpr std::int64_t SNIPER2_AMMO_CLIP_SIZE = 5;
inline constexpr std::int64_t SNIPER2_AMMO_INITIAL_STOCK = 25;
inline constexpr std::int64_t SNIPER2_AMMO_MAX = 25;
inline constexpr std::int64_t SNIPER2_AMMO_RESTOCK_AMOUNT = 25;
inline constexpr std::int64_t SNIPER2_DAMAGE_ARMS = 35;
inline constexpr std::int64_t SNIPER2_DAMAGE_BLOCK = 3;
inline constexpr std::int64_t SNIPER2_DAMAGE_ENTITY = 100;
inline constexpr std::int64_t SNIPER2_DAMAGE_HEAD = 95;
inline constexpr std::int64_t SNIPER2_DAMAGE_LEGS = 35;
inline constexpr std::int64_t SNIPER2_DAMAGE_TORSO = 70;
inline constexpr std::int64_t SNIPER2_DELAY = 1;
inline constexpr std::int64_t SNIPER2_RANGE = 10000;
inline constexpr std::int64_t SNIPER2_RAPID_KILL_ACHIEVE_COUNT = 3;
inline constexpr std::int64_t SNIPER2_RAPID_KILL_ACHIEVE_TIME = 20;
inline constexpr std::int64_t SNIPER2_RECOIL_SIDE = 0;
inline constexpr double SNIPER2_RECOIL_UP = -0.03;
inline constexpr std::int64_t SNIPER2_RELOAD_TIME = 3;
inline constexpr std::int64_t SNIPER2_SHOOT_INTERVAL = 1;
inline constexpr std::int64_t SNIPER2_TOOL = 19;
inline constexpr double SNIPER2_ZOOM_FACTOR = 1.2;
inline constexpr double SNIPER2_ZOOM_SENSITIVITY_FACTOR = 0.5;
inline constexpr double SNIPER_ACCURACY = 0.025;
inline constexpr std::int64_t SNIPER_ACCURACY_ZOOM = 0;
inline constexpr std::int64_t SNIPER_AMMO_CLIP_SIZE = 1;
inline constexpr std::int64_t SNIPER_AMMO_INITIAL_STOCK = 15;
inline constexpr std::int64_t SNIPER_AMMO_MAX = 15;
inline constexpr std::int64_t SNIPER_AMMO_RESTOCK_AMOUNT = 15;
inline constexpr std::int64_t SNIPER_DAMAGE_ARMS = 50;
inline constexpr std::int64_t SNIPER_DAMAGE_BLOCK = 5;
inline constexpr std::int64_t SNIPER_DAMAGE_ENTITY = 100;
inline constexpr std::int64_t SNIPER_DAMAGE_HEAD = 175;
inline constexpr std::int64_t SNIPER_DAMAGE_LEGS = 50;
inline constexpr std::int64_t SNIPER_DAMAGE_TORSO = 50;
inline constexpr std::int64_t SNIPER_DELAY = 1;
inline constexpr double SNIPER_LASER_DISTANCE_TO_PLAYER_OPAQUE = 0.45;
inline constexpr std::int64_t SNIPER_LASER_DISTANCE_TO_PLAYER_TRANSPARENT = 12;
inline constexpr double SNIPER_LASER_FADE_IN_DISTANCE = 1.5;
inline constexpr std::int64_t SNIPER_LASER_FADE_OUT_DISTANCE = 3;
inline constexpr double SNIPER_LASER_MAX_DOT_SIZE = 0.015;
inline constexpr double SNIPER_LASER_MIN_DOT_SIZE = 0.01;
inline constexpr double SNIPER_LASER_START_DISTANCE = 2.5;
inline constexpr double SNIPER_LASER_THIRD_PERSON_SIZE = 0.07;
inline constexpr std::int64_t SNIPER_LASER_TILING = 2;
inline constexpr std::int64_t SNIPER_RANGE = 10000;
inline constexpr std::int64_t SNIPER_RECOIL_SIDE = 0;
inline constexpr double SNIPER_RECOIL_UP = -0.06;
inline constexpr std::int64_t SNIPER_RELOAD_TIME = 2;
inline constexpr std::int64_t SNIPER_SHOOT_INTERVAL = 1;
inline constexpr std::int64_t SNIPER_TOOL = 18;
inline constexpr double SNIPER_ZOOM_FACTOR = 1.5;
inline constexpr double SNIPER_ZOOM_SENSITIVITY_FACTOR = 0.4;
inline constexpr std::int64_t SNOWBALL_DAMAGE = 20;
inline constexpr std::int64_t SNOWBALL_ENTITY = 24;
inline constexpr std::int64_t SNOWBALL_EXPLOSION_BLOCK_DAMAGE = 0;
inline constexpr std::int64_t SNOWBALL_EXPLOSION_DAMAGE = 10;
inline constexpr double SNOWBALL_EXPLOSION_KNOCKBACK_MAX = 0.3;
inline constexpr double SNOWBALL_EXPLOSION_KNOCKBACK_MIN = 0.3;
inline constexpr std::int64_t SNOWBALL_EXPLOSION_RADIUS = 5;
inline constexpr double SNOWBALL_GRAVITY_MULTIPLIER = 0.5;
inline constexpr std::int64_t SNOWBALL_KILL = 21;
inline constexpr std::int64_t SNOWBALL_SMOKE_DECAY_RATE_DEFAULT = -1;
inline constexpr std::int64_t SNOWBALL_SMOKE_DECAY_RATE_MAX = 1;
inline constexpr std::int64_t SNOWBALL_SMOKE_DECAY_RATE_MIN = 1;
inline constexpr std::int64_t SNOWBALL_SMOKE_INITIAL_ROTATION_RANDOM_MAX = 200;
inline constexpr std::int64_t SNOWBALL_SMOKE_INITIAL_ROTATION_RANDOM_MIN = 160;
inline constexpr double SNOWBALL_SMOKE_INITIAL_SIZE_RANDOM_MAX = 0.5;
inline constexpr double SNOWBALL_SMOKE_INITIAL_SIZE_RANDOM_MIN = 0.2;
inline constexpr double SNOWBALL_SMOKE_LIFETIME = 0.5;
inline constexpr double SNOWBALL_SMOKE_VELOCITY_MULTIPLIER = 0.7;
inline constexpr double SNOWBALL_SMOKE_VELOCITY_RANDOM_MAX_MULTIPLIER = 0.05;
inline constexpr std::int64_t SNOWBALL_SMOKE_VELOCITY_RANDOM_MIN_MULTIPLIER = 0;
inline constexpr std::int64_t SNOWBALL_SPEED = 50;
inline constexpr double SNOWBLOWER_ACCURACY = 0.02;
inline constexpr std::int64_t SNOWBLOWER_AMMO_CLIP_SIZE = 3;
inline constexpr std::int64_t SNOWBLOWER_AMMO_MAX = 9;
inline constexpr std::int64_t SNOWBLOWER_DELAY = 1;
inline constexpr std::int64_t SNOWBLOWER_RECOIL_SIDE = 0;
inline constexpr std::int64_t SNOWBLOWER_RECOIL_UP = 0;
inline constexpr std::int64_t SNOWBLOWER_RELOAD_TIME = 3;
inline constexpr double SNOWBLOWER_SHOOT_INTERVAL = 0.2;
inline constexpr std::int64_t SNOWBLOWER_TOOL = 29;
inline constexpr std::int64_t SNOWCAN_BUILD_SOUND_ID = 1;
inline constexpr std::int64_t SNOWCAN_IMPACT_SOUND_ID = 0;
inline constexpr double SNUB_PISTOL_ACCURACY = 0.01;
inline constexpr std::int64_t SNUB_PISTOL_AMMO_CLIP_SIZE = 6;
inline constexpr std::int64_t SNUB_PISTOL_AMMO_INITIAL_STOCK = 30;
inline constexpr std::int64_t SNUB_PISTOL_AMMO_MAX = 30;
inline constexpr std::int64_t SNUB_PISTOL_AMMO_RESTOCK_AMOUNT = 30;
inline constexpr std::int64_t SNUB_PISTOL_DAMAGE_ARMS = 30;
inline constexpr std::int64_t SNUB_PISTOL_DAMAGE_BLOCK = 1;
inline constexpr std::int64_t SNUB_PISTOL_DAMAGE_ENTITY = 20;
inline constexpr std::int64_t SNUB_PISTOL_DAMAGE_HEAD = 70;
inline constexpr std::int64_t SNUB_PISTOL_DAMAGE_LEGS = 30;
inline constexpr std::int64_t SNUB_PISTOL_DAMAGE_TORSO = 40;
inline constexpr std::int64_t SNUB_PISTOL_DELAY = 1;
inline constexpr std::int64_t SNUB_PISTOL_RANGE = 500;
inline constexpr std::int64_t SNUB_PISTOL_RECOIL_SIDE = 0;
inline constexpr double SNUB_PISTOL_RECOIL_UP = -0.05;
inline constexpr double SNUB_PISTOL_RELOAD_TIME = 0.75;
inline constexpr double SNUB_PISTOL_SHOOT_INTERVAL = 0.5;
inline constexpr std::int64_t SNUB_PISTOL_TOOL = 36;
inline constexpr double SOLDIER_ACCEL_MULTIPLIER = 0.7;
inline constexpr std::int64_t SOLDIER_AIRBORNE_ROCKET_KILLS = 101;
inline constexpr std::int64_t SOLDIER_APG_KILLS = 95;
inline constexpr std::int64_t SOLDIER_ASSAULTRIFLE_KILLS = 249;
inline constexpr double SOLDIER_CROUCH_SNEAK_MULTIPLIER = 0.5;
inline constexpr std::int64_t SOLDIER_DAMAGE_MULTIPLIER = 1;
inline constexpr std::int64_t SOLDIER_FALLING_DAMAGE_MAX_DAMAGE = 100;
inline constexpr std::int64_t SOLDIER_FALLING_DAMAGE_MAX_DISTANCE = 40;
inline constexpr std::int64_t SOLDIER_FALLING_DAMAGE_MIN_DISTANCE = 10;
inline constexpr double SOLDIER_FALL_ON_WATER_DAMAGE_MULTIPLIER = 0.5;
inline constexpr std::int64_t SOLDIER_GRENADE_KILLS = 94;
inline constexpr std::int64_t SOLDIER_HEADSHOT_DAMAGE_MULTIPLIER = 1;
inline constexpr double SOLDIER_JUMP_MULTIPLIER = 1.2;
inline constexpr std::int64_t SOLDIER_KNIFE_KILLS = 98;
inline constexpr std::int64_t SOLDIER_MAX_BLOCKS = 1000;
inline constexpr std::int64_t SOLDIER_MINIGUN_DEMOLISH_TOTAL = 99;
inline constexpr std::int64_t SOLDIER_MINIGUN_KILLS = 90;
inline constexpr std::int64_t SOLDIER_PISTOL_KILLS = 93;
inline constexpr std::int64_t SOLDIER_RPG2_KILLS = 92;
inline constexpr std::int64_t SOLDIER_RPG_FALL_TOTAL = 100;
inline constexpr std::int64_t SOLDIER_RPG_KILLS = 91;
inline constexpr std::int64_t SOLDIER_SNOWBLOWER_KILLS = 96;
inline constexpr std::int64_t SOLDIER_SPADE_KILLS = 97;
inline constexpr double SOLDIER_SPRINT_MULTIPLIER = 1.4;
inline constexpr std::int64_t SOLDIER_STARTING_BLOCKS = 200;
inline constexpr std::int64_t SOLDIER_WATER_FRICTION = 8;
inline constexpr std::int64_t SOME_DAMAGE = 38;
inline constexpr std::int64_t SOME_KILL = 30;
inline constexpr std::int64_t SORTING_DOWN = 1;
inline constexpr std::int64_t SORTING_UP = 2;
inline constexpr std::int64_t SOUTH = 2;
inline constexpr std::int64_t SPADES_GAME_APP_ID = 480;
inline constexpr std::int64_t SPADE_DAMAGE = 2;
inline constexpr std::int64_t SPADE_DAMAGE_AMOUNT = 5;
inline constexpr std::int64_t SPADE_HITPLAYER_DAMAGE_AMOUNT = 35;
inline constexpr double SPADE_SHOOT_INTERVAL = 0.4;
inline constexpr std::int64_t SPADE_TOOL = 2;
inline constexpr std::int64_t SPAWN_COLOR_MULTIPLIER = 2;
inline constexpr double SPAWN_OFFSET_X = 0.5;
inline constexpr double SPAWN_OFFSET_Y = 0.5;
inline constexpr double SPAWN_OFFSET_Z = -2.4;
inline constexpr std::int64_t SPAWN_VO = 10;
inline constexpr std::int64_t SPAWN_VO_CHANCE = 25;
inline constexpr std::int64_t SPECIALISM_DIG_SPEED = 2;
inline constexpr std::int64_t SPECIALISM_EXTRA_HEALTH = 0;
inline constexpr std::int64_t SPECIALISM_NOOF = 3;
inline constexpr std::int64_t SPECIALISM_SPEED = 1;
inline constexpr std::int64_t SPECIALIST_AUTOPISTOL_KILLS = 250;
inline constexpr std::int64_t SPECIALIST_AUTOSHOTGUN_KILLS = 242;
inline constexpr std::int64_t SPECIALIST_CHEMICALBOMB_KILLS = 239;
inline constexpr std::int64_t SPECIALIST_GRENADELAUNCHER_KILLS = 240;
inline constexpr std::int64_t SPECIALIST_MACHETE_KILLS = 237;
inline constexpr std::int64_t SPECIALIST_MAX_BLOCKS = 1000;
inline constexpr std::int64_t SPECIALIST_SMG_KILLS = 244;
inline constexpr std::int64_t SPECIALIST_SPADE_KILLS = 243;
inline constexpr std::int64_t SPECIALIST_STARTING_BLOCKS = 400;
inline constexpr std::int64_t SPECIALIST_STICKYGRENADE_KILLS = 241;
inline constexpr std::int64_t SPOOKY_MANSION_TIME_SCORE = 169;
inline constexpr std::int64_t SPOT_SHADOW_RAY_CAST_CHARACTER_HEIGHT = 3;
inline constexpr std::int64_t STATS_MAX_WEAPONS = 66;
inline constexpr std::int64_t STICKY_GRENADE_DAMAGE = 39;
inline constexpr std::int64_t STICKY_GRENADE_KILL = 34;
inline constexpr std::int64_t STICKY_GRENADE_TOOL = 57;
inline constexpr double SUDDEN_DEATH_INDICATOR_TIME = 1.2;
inline constexpr std::int64_t SUICIDE_SCORE_REASON = 2;
inline constexpr std::int64_t SUPERSPADE_DAMAGE = 3;
inline constexpr double SUPERSPADE_DAMAGE_AMOUNT = 7.5;
inline constexpr std::int64_t SUPERSPADE_HITPLAYER_DAMAGE_AMOUNT = 50;
inline constexpr double SUPERSPADE_SHOOT_INTERVAL = 0.6;
inline constexpr std::int64_t SUPERSPADE_TOOL = 3;
inline constexpr std::int64_t SUPER_MARKSMAN_SCORE_REASON = 222;
inline constexpr std::int64_t SUPER_SPADE_BLOCK_DAMAGE_RANDOM_EXTRA = 5;
inline constexpr std::int64_t SUPER_SPADE_HIT_BLOCK_SOUND_ID = 37;
inline constexpr std::int64_t SUPER_SPADE_HIT_WATER_SOUND_ID = 40;
inline constexpr std::int64_t SUPER_SPRINTER_SCORE_REASON = 223;
inline constexpr std::int64_t SURVIVOR_WIN_MESSAGE = 4;
inline constexpr std::int64_t TANK_CAMERA = 2;
inline constexpr std::int64_t TANK_CAMERA_DISTANCE = 12;
inline constexpr std::int64_t TANK_CONTACT_POINT_COUNT = 4;
inline constexpr std::int64_t TANK_ENTITY = 26;
inline constexpr std::int64_t TANK_GRAVITY_MULTIPLIER = 1;
inline constexpr std::int64_t TANK_RIDER_VERTICAL_OFFSET = 5;
inline constexpr std::int64_t TANK_SPEED_BACK = 50;
inline constexpr std::int64_t TANK_SPEED_FORWARD = 100;
inline constexpr std::int64_t TANK_SPEED_LEFT = 1;
inline constexpr std::int64_t TANK_SPEED_RIGHT = 1;
inline constexpr std::int64_t TANK_TRACK_LENGTH = 3;
inline constexpr std::int64_t TANK_TRACK_SEPARATION = 3;
inline constexpr std::int64_t TC_ASSAULT_SCORE_REASON = 24;
inline constexpr std::int64_t TC_BASE_ACTIVATE = 1;
inline constexpr std::int64_t TC_BASE_CAPTURE_UPDATE = 5;
inline constexpr std::int64_t TC_BASE_CONTENDED = 6;
inline constexpr std::int64_t TC_BASE_DEACTIVATE = 2;
inline constexpr std::int64_t TC_BASE_ENTERING = 3;
inline constexpr std::int64_t TC_BASE_LEAVING = 4;
inline constexpr std::int64_t TC_BASE_UNCONTENDED = 7;
inline constexpr double TC_CAPTURE_TICK_RATE = 0.5;
inline constexpr std::int64_t TC_CLAIM_SCORE_REASON = 21;
inline constexpr std::int64_t TC_CONTEND_SCORE_REASON = 25;
inline constexpr std::int64_t TC_CONTROL_SCORE_REASON = 22;
inline constexpr std::int64_t TC_DEFAULT_BASE_COUNT_TO_USE = 5;
inline constexpr std::int64_t TC_DEFEND_SCORE_REASON = 23;
inline constexpr std::int64_t TC_GAME_LENGTH = 1500;
inline constexpr std::int64_t TC_INITIAL_INFO = 0;
inline constexpr std::int64_t TC_MODE_SCORE_REASON = 190;
inline constexpr std::int64_t TC_NEW_TEAM_ENTERS_SHOUT_COOLDOWN = 5;
inline constexpr std::int64_t TC_OCCUPY_SCORE_REASON = 20;
inline constexpr std::int64_t TC_SCORE_CLAIM = 150;
inline constexpr std::int64_t TC_SCORE_CONTEND_HILL = 25;
inline constexpr std::int64_t TC_SCORE_CONTEND_INTERVAL = 5;
inline constexpr std::int64_t TC_SCORE_CONTROL = 100;
inline constexpr std::int64_t TC_SCORE_KILL_KILLERINHILL = 100;
inline constexpr std::int64_t TC_SCORE_KILL_VICTIMINHILL = 50;
inline constexpr std::int64_t TC_SCORE_OCCUPY_INTERVAL = 5;
inline constexpr std::int64_t TC_SCORE_OCCUPY_PERHILL = 35;
inline constexpr std::int64_t TC_TOTAL_SCORE = 194;
inline constexpr std::int64_t TDM_GAME_LENGTH = 900;
inline constexpr std::int64_t TDM_MODE_SCORE_REASON = 183;
inline constexpr std::int64_t TDM_SCORE_DISTRACT = 50;
inline constexpr std::int64_t TDM_TEAM_SCORE_FOR_KILL = 1;
inline constexpr std::int64_t TDM_TOTAL_SCORE = 192;
inline constexpr std::int64_t TEABAG_CROUCH_COUNT = 3;
inline constexpr double TEABAG_MAX_DISTANCE = 1.75;
inline constexpr std::int64_t TEABAG_SCORE_REASON = 221;
inline constexpr double TEABAG_TIME_THRESHOLD = 0.5;
inline constexpr std::int64_t TEAM1 = 2;
inline constexpr std::int64_t TEAM2 = 3;
inline constexpr std::int64_t TEAM_4DIGIT_SCORE_VALUE = 2;
inline constexpr std::int64_t TEAM_CHANGE_KILL = 9;
inline constexpr double TEAM_EXPLOSION_DAMAGE_REDUCTION = 0.5;
inline constexpr std::int64_t TEAM_LOCK_DIFFERENCE_COUNT_TOLERANCE = 2;
inline constexpr std::int64_t TEAM_NEUTRAL = 1;
inline constexpr std::int64_t TEAM_PLAYERS_COUNT_VALUE = 0;
inline constexpr std::int64_t TEAM_SCORES_DRAW = 8;
inline constexpr std::int64_t TEAM_SCORES_MESSAGE = 1;
inline constexpr std::int64_t TEAM_SCORE_INACTIVE = 3;
inline constexpr std::int64_t TEAM_SCORE_VALUE = 1;
inline constexpr std::int64_t TEAM_SPECTATOR = 0;
inline constexpr std::int64_t TEXT_DISPLAY_RANGE = 80;
inline constexpr std::int64_t TIMEOUT_MUSIC_LENGTH = 61;
inline constexpr std::int64_t TIMER_WINDOW_ENTRIES = 40;
inline constexpr std::int64_t TIME_AFTER_MAP_VOTE_START_BEFORE_END = 10;
inline constexpr std::int64_t TIME_AFTER_WIN_BEFORE_SCORES = 5;
inline constexpr std::int64_t TOKYO_NEON_TIME_SCORE = 170;
inline constexpr double TOMMYGUN_ACCURACY = 0.01;
inline constexpr double TOMMYGUN_ACCURACY_RANGE = 0.04;
inline constexpr double TOMMYGUN_ACCURACY_SPREAD_INCREASE_PER_SHOT = 0.1;
inline constexpr std::int64_t TOMMYGUN_ACCURACY_SPREAD_INITIAL = 1;
inline constexpr std::int64_t TOMMYGUN_ACCURACY_SPREAD_RANGE = 4;
inline constexpr double TOMMYGUN_ACCURACY_SPREAD_REDUCTION_SPEED = 0.5;
inline constexpr std::int64_t TOMMYGUN_AMMO_CLIP_SIZE = 30;
inline constexpr std::int64_t TOMMYGUN_AMMO_INITIAL_STOCK = 120;
inline constexpr std::int64_t TOMMYGUN_AMMO_MAX = 120;
inline constexpr std::int64_t TOMMYGUN_AMMO_RESTOCK_AMOUNT = 120;
inline constexpr std::int64_t TOMMYGUN_DAMAGE_ARMS = 30;
inline constexpr std::int64_t TOMMYGUN_DAMAGE_BLOCK = 1;
inline constexpr std::int64_t TOMMYGUN_DAMAGE_ENTITY = 30;
inline constexpr std::int64_t TOMMYGUN_DAMAGE_HEAD = 35;
inline constexpr std::int64_t TOMMYGUN_DAMAGE_LEGS = 30;
inline constexpr std::int64_t TOMMYGUN_DAMAGE_TORSO = 30;
inline constexpr double TOMMYGUN_DELAY = 0.11;
inline constexpr std::int64_t TOMMYGUN_RANGE = 500;
inline constexpr std::int64_t TOMMYGUN_RECOIL_SIDE = 0;
inline constexpr double TOMMYGUN_RECOIL_UP = -0.01;
inline constexpr std::int64_t TOMMYGUN_RELOAD_TIME = 2;
inline constexpr double TOMMYGUN_SHOOT_INTERVAL = 0.12;
inline constexpr std::int64_t TOMMYGUN_TOOL = 35;
inline constexpr std::int64_t TOP_FACES_BOTTOM_ORIENTATION_ROLL = 2;
inline constexpr std::int64_t TOP_FACES_LEFT_ORIENTATION_ROLL = 1;
inline constexpr std::int64_t TOP_FACES_RIGHT_ORIENTATION_ROLL = 3;
inline constexpr std::int64_t TOP_FACES_UP_ORIENTATION_ROLL = 0;
inline constexpr std::int64_t TO_THE_BRIDGE_TIME_SCORE = 176;
inline constexpr std::int64_t TRACKING_ENTITY = 1;
inline constexpr std::int64_t TRACKING_NOTHING = 0;
inline constexpr std::int64_t TRACKING_PLAYER = 2;
inline constexpr std::int64_t TRENCHES_TIME_SCORE = 179;
inline constexpr std::int64_t TURRET_PLACE_SOUND_ID = 30;
inline constexpr std::int64_t TUTORIAL_COMPLETE_SOUND_ID = 27;
inline constexpr std::int64_t UGCBUILDER_ACCEL_MULTIPLIER = 1;
inline constexpr double UGCBUILDER_CROUCH_SNEAK_MULTIPLIER = 0.5;
inline constexpr std::int64_t UGCBUILDER_DAMAGE_MULTIPLIER = 0;
inline constexpr std::int64_t UGCBUILDER_FALLING_DAMAGE_MAX_DAMAGE = 0;
inline constexpr std::int64_t UGCBUILDER_FALLING_DAMAGE_MAX_DISTANCE = 255;
inline constexpr std::int64_t UGCBUILDER_FALLING_DAMAGE_MIN_DISTANCE = 255;
inline constexpr std::int64_t UGCBUILDER_FALL_ON_WATER_DAMAGE_MULTIPLIER = 1;
inline constexpr std::int64_t UGCBUILDER_HEADSHOT_DAMAGE_MULTIPLIER = 0;
inline constexpr std::int64_t UGCBUILDER_JUMP_MULTIPLIER = 1;
inline constexpr std::int64_t UGCBUILDER_MAX_BLOCKS = 1;
inline constexpr std::int64_t UGCBUILDER_SPRINT_MULTIPLIER = 3;
inline constexpr std::int64_t UGCBUILDER_STARTING_BLOCKS = 1;
inline constexpr std::int64_t UGCBUILDER_WATER_FRICTION = 1;
inline constexpr std::int64_t UGCPREFAB_CAMERA = 6;
inline constexpr std::int64_t UGCTOOL_FAR_RADIUS = 10;
inline constexpr double UGCTOOL_SHOOT_INTERVAL = 0.5;
inline constexpr std::int64_t UGC_CONVERT_TO_GAME = 0;
inline constexpr double UGC_DRILLGUN_ACCURACY = 0.04;
inline constexpr std::int64_t UGC_DRILLGUN_AMMO_CLIP_SIZE = 1;
inline constexpr std::int64_t UGC_DRILLGUN_AMMO_INITIAL_STOCK = 1;
inline constexpr std::int64_t UGC_DRILLGUN_AMMO_MAX = 1;
inline constexpr std::int64_t UGC_DRILLGUN_AMMO_RESTOCK_AMOUNT = 1;
inline constexpr std::int64_t UGC_DRILLGUN_DELAY = 1;
inline constexpr std::int64_t UGC_DRILLGUN_RECOIL_SIDE = 0;
inline constexpr double UGC_DRILLGUN_RECOIL_UP = -0.00001;
inline constexpr std::int64_t UGC_DRILLGUN_RELOAD_TIME = 4;
inline constexpr double UGC_DRILLGUN_SHOOT_INTERVAL = 0.2;
inline constexpr std::int64_t UGC_DRILLGUN_TOOL = 47;
inline constexpr double UGC_DRILL_COLLISION_RANGE = 0.5;
inline constexpr std::int64_t UGC_DRILL_DAMAGE = 33;
inline constexpr std::int64_t UGC_DRILL_DESTROYED_EXPLOSION_BLOCK_DAMAGE = 10;
inline constexpr std::int64_t UGC_DRILL_DESTROYED_EXPLOSION_DAMAGE = 95;
inline constexpr double UGC_DRILL_DESTROYED_EXPLOSION_KNOCKBACK_MAX = 0.2;
inline constexpr double UGC_DRILL_DESTROYED_EXPLOSION_KNOCKBACK_MIN = 0.1;
inline constexpr double UGC_DRILL_DESTROYED_EXPLOSION_RADIUS = 3.5;
inline constexpr std::int64_t UGC_DRILL_DIGGING_SPEED = 20;
inline constexpr double UGC_DRILL_DIG_SLOWDOWN_DURATION = 0.5;
inline constexpr std::int64_t UGC_DRILL_EXPLOSION_BLOCK_DAMAGE = 5;
inline constexpr std::int64_t UGC_DRILL_EXPLOSION_DAMAGE = 50;
inline constexpr double UGC_DRILL_EXPLOSION_KNOCKBACK_MAX = 0.1;
inline constexpr double UGC_DRILL_EXPLOSION_KNOCKBACK_MIN = 0.01;
inline constexpr std::int64_t UGC_DRILL_EXPLOSION_RADIUS = 3;
inline constexpr std::int64_t UGC_DRILL_FLYING_SPEED = 40;
inline constexpr double UGC_DRILL_GRAVITY_MULTIPLIER = 1.5;
inline constexpr std::int64_t UGC_DRILL_HEALTH = 10;
inline constexpr std::int64_t UGC_DRILL_KILL = 28;
inline constexpr std::int64_t UGC_DRILL_LIFESPAN = 3;
inline constexpr double UGC_DRILL_MODEL_SIZE = 0.06;
inline constexpr std::int64_t UGC_DRILL_MODEL_Z_OFFSET = 0;
inline constexpr std::int64_t UGC_DRILL_OWNER_EXPLOSION_PROTECTION_TIME = 1;
inline constexpr std::int64_t UGC_DRILL_SPEED = 20;
inline constexpr std::int64_t UGC_ENTITY = 29;
inline constexpr std::int64_t UGC_GROUP_BLUE_BASE = 4;
inline constexpr std::int64_t UGC_GROUP_BLUE_SPAWN_ZONE = 2;
inline constexpr std::int64_t UGC_GROUP_DROP_POINT = 0;
inline constexpr std::int64_t UGC_GROUP_GREEN_BASE = 3;
inline constexpr std::int64_t UGC_GROUP_GREEN_SPAWN_ZONE = 1;
inline constexpr std::int64_t UGC_GROUP_NEUTRAL_BASE = 5;
inline constexpr std::uint64_t UGC_INVALID_STEAM_PUBLISHED_FILE_HANDLE = 18446744073709551615ULL;
inline constexpr std::int64_t UGC_ITEM_AMMO_DROP_POINT = 1;
inline constexpr std::int64_t UGC_ITEM_BLOCK_DROP_POINT = 2;
inline constexpr std::int64_t UGC_ITEM_BLUE_BASE_ZONE_LARGE = 15;
inline constexpr std::int64_t UGC_ITEM_BLUE_BASE_ZONE_MEDIUM = 14;
inline constexpr std::int64_t UGC_ITEM_BLUE_BASE_ZONE_SMALL = 13;
inline constexpr std::int64_t UGC_ITEM_BLUE_SPAWN_ZONE_LARGE = 9;
inline constexpr std::int64_t UGC_ITEM_BLUE_SPAWN_ZONE_MEDIUM = 8;
inline constexpr std::int64_t UGC_ITEM_BLUE_SPAWN_ZONE_SMALL = 7;
inline constexpr std::int64_t UGC_ITEM_GREEN_BASE_ZONE_LARGE = 12;
inline constexpr std::int64_t UGC_ITEM_GREEN_BASE_ZONE_MEDIUM = 11;
inline constexpr std::int64_t UGC_ITEM_GREEN_BASE_ZONE_SMALL = 10;
inline constexpr std::int64_t UGC_ITEM_GREEN_SPAWN_ZONE_LARGE = 6;
inline constexpr std::int64_t UGC_ITEM_GREEN_SPAWN_ZONE_MEDIUM = 5;
inline constexpr std::int64_t UGC_ITEM_GREEN_SPAWN_ZONE_SMALL = 4;
inline constexpr std::int64_t UGC_ITEM_HEALTH_DROP_POINT = 0;
inline constexpr std::int64_t UGC_ITEM_NEUTRAL_BASE_ZONE_LARGE = 18;
inline constexpr std::int64_t UGC_ITEM_NEUTRAL_BASE_ZONE_MEDIUM = 17;
inline constexpr std::int64_t UGC_ITEM_NEUTRAL_BASE_ZONE_SMALL = 16;
inline constexpr std::int64_t UGC_ITEM_OCC_BOMB_POINT = 3;
inline constexpr std::int64_t UGC_MAX_LOADOUT_PREFABS = 5;
inline constexpr std::int64_t UGC_NO_VXL_USE_BASEPLATE = 3;
inline constexpr std::int64_t UGC_PICKAXE_DAMAGE = 28;
inline constexpr std::int64_t UGC_PICKAXE_DAMAGE_AMOUNT = 9;
inline constexpr std::int64_t UGC_PICKAXE_HITPLAYER_DAMAGE_AMOUNT = 0;
inline constexpr double UGC_PICKAXE_SHOOT_INTERVAL = 0.2;
inline constexpr std::int64_t UGC_PICKAXE_TOOL = 44;
inline constexpr std::int64_t UGC_PREFAB_MAX_ZOOM = 9999999;
inline constexpr std::int64_t UGC_PREFAB_TOOL = 42;
inline constexpr std::int64_t UGC_RANDOM_COLOUR_JITTER_AMOUNT = 8;
inline constexpr std::int64_t UGC_REQUEST_MAPINFO = 4;
inline constexpr std::int64_t UGC_REQUEST_MAP_VALIDATION = 1;
inline constexpr std::int64_t UGC_REQUEST_VXL = 2;
inline constexpr std::int64_t UGC_ROCKET2_DAMAGE = 30;
inline constexpr std::int64_t UGC_ROCKET2_EXPLOSION_BLAST_WAVE_RADIUS = 4;
inline constexpr std::int64_t UGC_ROCKET2_EXPLOSION_BLOCK_DAMAGE = 2;
inline constexpr std::int64_t UGC_ROCKET2_EXPLOSION_DAMAGE = 50;
inline constexpr double UGC_ROCKET2_EXPLOSION_KNOCKBACK_MAX = 0.25;
inline constexpr std::int64_t UGC_ROCKET2_EXPLOSION_KNOCKBACK_MIN = 0;
inline constexpr std::int64_t UGC_ROCKET2_EXPLOSION_RADIUS = 4;
inline constexpr double UGC_ROCKET2_EXPLOSION_SELF_KNOCKBACK_MAX = 1.5;
inline constexpr std::int64_t UGC_ROCKET2_EXPLOSION_SELF_KNOCKBACK_MIN = 1;
inline constexpr std::int64_t UGC_ROCKET2_KILL = 27;
inline constexpr std::int64_t UGC_ROCKET2_SPEED = 150;
inline constexpr double UGC_RPG2_ACCURACY = 0.02;
inline constexpr std::int64_t UGC_RPG2_AMMO_CLIP_SIZE = 1;
inline constexpr std::int64_t UGC_RPG2_AMMO_INITIAL_STOCK = 6;
inline constexpr std::int64_t UGC_RPG2_AMMO_MAX = 1;
inline constexpr std::int64_t UGC_RPG2_AMMO_RESTOCK_AMOUNT = 6;
inline constexpr std::int64_t UGC_RPG2_DELAY = 1;
inline constexpr std::int64_t UGC_RPG2_RECOIL_SIDE = 0;
inline constexpr std::int64_t UGC_RPG2_RECOIL_UP = 0;
inline constexpr std::int64_t UGC_RPG2_RELOAD_TIME = 1;
inline constexpr double UGC_RPG2_SHOOT_INTERVAL = 0.5;
inline constexpr std::int64_t UGC_RPG2_TOOL = 46;
inline constexpr std::int64_t UGC_SNOWBALL_DAMAGE = 32;
inline constexpr std::int64_t UGC_SNOWBALL_EXPLOSION_BLOCK_DAMAGE = 0;
inline constexpr std::int64_t UGC_SNOWBALL_EXPLOSION_DAMAGE = 10;
inline constexpr double UGC_SNOWBALL_EXPLOSION_KNOCKBACK_MAX = 0.3;
inline constexpr double UGC_SNOWBALL_EXPLOSION_KNOCKBACK_MIN = 0.3;
inline constexpr std::int64_t UGC_SNOWBALL_EXPLOSION_RADIUS = 5;
inline constexpr double UGC_SNOWBALL_GRAVITY_MULTIPLIER = 0.5;
inline constexpr std::int64_t UGC_SNOWBALL_KILL = 29;
inline constexpr std::int64_t UGC_SNOWBALL_SPEED = 50;
inline constexpr double UGC_SNOWBLOWER_ACCURACY = 0.02;
inline constexpr std::int64_t UGC_SNOWBLOWER_AMMO_CLIP_SIZE = 3;
inline constexpr std::int64_t UGC_SNOWBLOWER_AMMO_MAX = 9;
inline constexpr std::int64_t UGC_SNOWBLOWER_DELAY = 1;
inline constexpr std::int64_t UGC_SNOWBLOWER_RECOIL_SIDE = 0;
inline constexpr std::int64_t UGC_SNOWBLOWER_RECOIL_UP = 0;
inline constexpr std::int64_t UGC_SNOWBLOWER_RELOAD_TIME = 3;
inline constexpr double UGC_SNOWBLOWER_SHOOT_INTERVAL = 0.2;
inline constexpr std::int64_t UGC_SNOWBLOWER_TOOL = 48;
inline constexpr std::int64_t UGC_SUPERSPADE_DAMAGE = 29;
inline constexpr double UGC_SUPERSPADE_DAMAGE_AMOUNT = 7.5;
inline constexpr std::int64_t UGC_SUPERSPADE_HITPLAYER_DAMAGE_AMOUNT = 0;
inline constexpr std::int64_t UGC_SUPERSPADE_SECONDARY_DAMAGE = 31;
inline constexpr double UGC_SUPERSPADE_SECONDARY_DAMAGE_AMOUNT = 7.5;
inline constexpr double UGC_SUPERSPADE_SHOOT_INTERVAL = 0.2;
inline constexpr std::int64_t UGC_SUPERSPADE_TOOL = 45;
inline constexpr std::int64_t UGC_TOOL = 41;
inline constexpr std::int64_t UGC_ZONE_TYPE_BASE = 4;
inline constexpr std::int64_t UGC_ZONE_TYPE_BASE1 = 2;
inline constexpr std::int64_t UGC_ZONE_TYPE_BASE2 = 3;
inline constexpr std::int64_t UGC_ZONE_TYPE_SPAWN1 = 0;
inline constexpr std::int64_t UGC_ZONE_TYPE_SPAWN2 = 1;
inline constexpr std::int64_t UNKNOWN_DAMAGE = 43;
inline constexpr std::int64_t UNKNOWN_ENTITY1 = 30;
inline constexpr std::int64_t UNKNOWN_ENTITY10 = 39;
inline constexpr std::int64_t UNKNOWN_ENTITY2 = 31;
inline constexpr std::int64_t UNKNOWN_ENTITY3 = 32;
inline constexpr std::int64_t UNKNOWN_ENTITY4 = 33;
inline constexpr std::int64_t UNKNOWN_ENTITY5 = 34;
inline constexpr std::int64_t UNKNOWN_ENTITY6 = 35;
inline constexpr std::int64_t UNKNOWN_ENTITY7 = 36;
inline constexpr std::int64_t UNKNOWN_ENTITY8 = 37;
inline constexpr std::int64_t UNKNOWN_ENTITY9 = 38;
inline constexpr std::int64_t UNZOOMED_CROSSHAIR = 2;
inline constexpr std::int64_t UPDATE_ENTITIES_RATE = 10;
inline constexpr std::int64_t UPDATE_FRAMERATE = 60;
inline constexpr double UPDATE_INTERVAL = 0.016666666666666666;
inline constexpr std::int64_t UPDATE_LIMIT_FRAMERATE = 20;
inline constexpr double UPDATE_LIMIT_INTERVAL = 0.05;
inline constexpr std::int64_t UPPER_ARM = 0;
inline constexpr std::int64_t UP_ORIENTATION_PITCH = 1;
inline constexpr std::int64_t VALID_PACKET_AGE_ALLOWANCE_FUTURE = 30;
inline constexpr std::int64_t VALID_PACKET_AGE_ALLOWANCE_PAST = 60;
inline constexpr std::int64_t VIP_ASSAULT_ENEMY_SCORE_REASON = 18;
inline constexpr std::int64_t VIP_ASSAULT_SCORE_REASON = 17;
inline constexpr std::int64_t VIP_CORPSE_EXPLOSION_BLOCK_DAMAGE = 20;
inline constexpr std::int64_t VIP_CORPSE_EXPLOSION_DAMAGE = 75;
inline constexpr std::int64_t VIP_CORPSE_EXPLOSION_KNOCKBACK_MAX = 3;
inline constexpr std::int64_t VIP_CORPSE_EXPLOSION_KNOCKBACK_MIN = 2;
inline constexpr std::int64_t VIP_CORPSE_EXPLOSION_RADIUS = 10;
inline constexpr double VIP_DAMAGE_MULTIPLIER = 0.5;
inline constexpr std::int64_t VIP_DEFEND_SCORE_REASON = 19;
inline constexpr std::int64_t VIP_DISTRACT_SCORE_REASON = 15;
inline constexpr std::int64_t VIP_ESCORT_HYSTERESIS = 10;
inline constexpr std::int64_t VIP_ESCORT_RADIUS = 15;
inline constexpr std::int64_t VIP_ESCORT_SCORE_REASON = 13;
inline constexpr std::int64_t VIP_GAME_LENGTH = 900;
inline constexpr std::int64_t VIP_KILLEDTHEIRS_SOUND_ID = 8;
inline constexpr std::int64_t VIP_KILLENEMYVIP_SCORE_REASON = 14;
inline constexpr std::int64_t VIP_KILL_SCORE_REASON = 16;
inline constexpr std::int64_t VIP_MINIMUM_TEAM_SIZE_TO_START = 1;
inline constexpr std::int64_t VIP_MODE_KILL = 26;
inline constexpr std::int64_t VIP_MODE_SCORE_REASON = 188;
inline constexpr std::int64_t VIP_NOOF_ROUNDS_BEFORE_NEXT_MAP = 3;
inline constexpr std::int64_t VIP_SCORE_DEFEND = 150;
inline constexpr std::int64_t VIP_SCORE_DISTRACT = 50;
inline constexpr std::int64_t VIP_SCORE_ESCORT_INTERVAL = 5;
inline constexpr std::int64_t VIP_SCORE_ESCORT_SCORE = 10;
inline constexpr std::int64_t VIP_SCORE_KILL_AS_VIP = 100;
inline constexpr std::int64_t VIP_SCORE_LIVEVIP_INTERVAL = 10;
inline constexpr std::int64_t VIP_SCORE_LIVEVIP_SCORE = 50;
inline constexpr std::int64_t VIP_SCORE_OWN_VIP_KILL = 0;
inline constexpr std::int64_t VIP_SCORE_VIP_KILL_CONSTANT = 0;
inline constexpr std::int64_t VIP_SCORE_VIP_KILL_PERCENT = 10;
inline constexpr std::int64_t VIP_SELECTION_DELAY = 10;
inline constexpr std::int64_t VIP_SUDDEN_DEATH_DAMAGE = 1;
inline constexpr std::int64_t VIP_SUDDEN_DEATH_DAMAGE_FREQUENCY = 1;
inline constexpr std::int64_t VIP_SUDDEN_DEATH_DELAY_AFTER_VIP_KILL = 5;
inline constexpr std::int64_t VIP_SUDDEN_DEATH_TIME = 60;
inline constexpr std::int64_t VIP_SURVIVE_SCORE_REASON = 12;
inline constexpr std::int64_t VIP_TEAM1_WIN_MESSAGE = 6;
inline constexpr std::int64_t VIP_TEAM2_WIN_MESSAGE = 7;
inline constexpr std::int64_t VIP_TEAM_SCORE_FOR_KILL = 10;
inline constexpr std::int64_t VIP_THREAT_RADIUS = 20;
inline constexpr std::int64_t VIP_TOTAL_SCORE = 193;
inline constexpr std::int64_t VIP_YOURSISDEAD_SOUND_ID = 7;
inline constexpr std::int64_t WADE_SOUND = 7;
inline constexpr std::int64_t WATER_JUMP_SOUND = 1;
inline constexpr std::int64_t WATER_JUMP_VO = 12;
inline constexpr std::int64_t WATER_JUMP_VO_CHANCE = -66;
inline constexpr std::int64_t WATER_LAND_SOUND = 4;
inline constexpr std::int64_t WATER_LAND_VO = 14;
inline constexpr std::int64_t WATER_LAND_VO_CHANCE = -66;
inline constexpr std::int64_t WEAPON_DAMAGE = 6;
inline constexpr double WEAPON_DAMAGE_MULTIPLIER_THRESHOLD = 0.3;
inline constexpr std::int64_t WEAPON_KILL = 0;
inline constexpr std::int64_t WEAPON_RANGE = 10000;
inline constexpr std::int64_t WEAPON_TRACER_SPEED = 200;
inline constexpr std::int64_t WEAPON_WORLD_RANGE = 10000;
inline constexpr std::int64_t WEAPON_ZOOM = 1;
inline constexpr std::int64_t WEST = 3;
inline constexpr std::int64_t WINTER_VALLEY_TIME_SCORE = 178;
inline constexpr std::int64_t WW1_TIME_SCORE = 175;
inline constexpr double ZERO_FALL_DAMAGE_AIR_TIME = 0.016666666666666666;
inline constexpr std::int64_t ZOMBIEHAND_DAMAGE_AMOUNT = 2;
inline constexpr std::int64_t ZOMBIEHAND_HITPLAYER_DAMAGE_AMOUNT = 70;
inline constexpr double ZOMBIEHAND_SHOOT_INTERVAL = 0.4;
inline constexpr std::int64_t ZOMBIEHAND_TOOL = 24;
inline constexpr double ZOMBIE_ACCEL_MULTIPLIER = 0.5;
inline constexpr std::int64_t ZOMBIE_BECOME_SOUND_ID = 28;
inline constexpr std::int64_t ZOMBIE_BLOCKS_DESTROYED_TOTAL = 88;
inline constexpr std::int64_t ZOMBIE_BLOCK_DAMAGE_RANDOM_EXTRA = 8;
inline constexpr double ZOMBIE_CROUCH_SNEAK_MULTIPLIER = 0.5;
inline constexpr std::int64_t ZOMBIE_DAMAGE = 17;
inline constexpr double ZOMBIE_DAMAGE_MULTIPLIER = 0.6;
inline constexpr std::int64_t ZOMBIE_FALLING_DAMAGE_MAX_DAMAGE = 100;
inline constexpr std::int64_t ZOMBIE_FALLING_DAMAGE_MAX_DISTANCE = 60;
inline constexpr std::int64_t ZOMBIE_FALLING_DAMAGE_MIN_DISTANCE = 10;
inline constexpr double ZOMBIE_FALL_ON_WATER_DAMAGE_MULTIPLIER = 0.25;
inline constexpr std::int64_t ZOMBIE_HANDS_KILLS_TOTAL = 89;
inline constexpr std::int64_t ZOMBIE_HAND_HIT_BLOCK_SOUND_ID = 38;
inline constexpr std::int64_t ZOMBIE_HAND_HIT_WATER_SOUND_ID = 44;
inline constexpr std::int64_t ZOMBIE_HEADSHOT_DAMAGE_MULTIPLIER = 1;
inline constexpr std::int64_t ZOMBIE_HUMANS_KILLED_AS_PATIENTZERO_TOTAL = 87;
inline constexpr std::int64_t ZOMBIE_HUMANS_KILLED_IN_WATER_TOTAL = 86;
inline constexpr std::int64_t ZOMBIE_HUMANS_KILLED_TOTAL = 85;
inline constexpr double ZOMBIE_JUMP_MULTIPLIER = 1.5;
inline constexpr std::int64_t ZOMBIE_MAX_BLOCKS = 2000;
inline constexpr std::int64_t ZOMBIE_PREFAB_TOOL = 28;
inline constexpr double ZOMBIE_SPRINT_MULTIPLIER = 1.65;
inline constexpr std::int64_t ZOMBIE_STARTING_BLOCKS = 1000;
inline constexpr std::int64_t ZOMBIE_TIMER_COUNTDOWN_SOUND_ID = 29;
inline constexpr std::int64_t ZOMBIE_WATER_FRICTION = 4;
inline constexpr std::int64_t ZOMBIE_WIN_MESSAGE = 3;
inline constexpr std::int64_t ZOM_EXTRA_INDIVIDUAL_SCORE_FOR_SURVIVAL = 200;
inline constexpr std::int64_t ZOM_GAMEMODE_TIMEOUT = 600;
inline constexpr std::int64_t ZOM_KILLSURVIVOR_SCORE_REASON = 61;
inline constexpr std::int64_t ZOM_LASTMANSTANDING_TOTAL = 68;
inline constexpr std::int64_t ZOM_LASTMAN_KILLS_TOTAL = 67;
inline constexpr std::int64_t ZOM_LASTMAN_SCORE_REASON = 60;
inline constexpr std::int64_t ZOM_LASTMAN_TIME_TOTAL = 65;
inline constexpr std::int64_t ZOM_LASTMAN_ZOMBIEKILL_SCORE_REASON = 62;
inline constexpr std::int64_t ZOM_MODE_SCORE_REASON = 182;
inline constexpr std::int64_t ZOM_NOOF_FIRST_INFECTION_ZOMBIES = 2;
inline constexpr std::int64_t ZOM_NOOF_ROUNDS_BEFORE_NEXT_MAP = 3;
inline constexpr std::int64_t ZOM_PISTOL_ZOMBIEKILL_TOTAL = 66;
inline constexpr std::int64_t ZOM_RESPAWN_AS_ZOMBIE_TIME = 0;
inline constexpr std::int64_t ZOM_ROUND_TIME = 600;
inline constexpr std::int64_t ZOM_SCORE_KILL_SURVIVOR = 100;
inline constexpr std::int64_t ZOM_SCORE_LASTMAN = 150;
inline constexpr std::int64_t ZOM_SCORE_LASTMAN_INTERVAL = 5;
inline constexpr std::int64_t ZOM_SCORE_LASTMAN_ZOMBIEKILL = 50;
inline constexpr std::int64_t ZOM_SCORE_SURVIVE = 50;
inline constexpr std::int64_t ZOM_SCORE_SURVIVE_INTERVAL = 10;
inline constexpr std::int64_t ZOM_SURVIVE_SCORE_REASON = 59;
inline constexpr std::int64_t ZOM_TIME_AFTER_SURVIVOR_WIN_BEFORE_SCORES = 5;
inline constexpr std::int64_t ZOM_TIME_AFTER_ZOMBIE_WIN_BEFORE_SCORES = 5;
inline constexpr std::int64_t ZOM_TIME_BEFORE_FIRST_INFECTION = 60;
inline constexpr std::int64_t ZOM_TIME_SURVIVED_TOTAL = 64;
inline constexpr std::int64_t ZOM_TOTAL_SCORE = 198;
inline constexpr std::int64_t ZOM_ZOMBIES_KILLED_TOTAL = 63;
inline constexpr std::int64_t ZONE_ICON_CTF = 6;
inline constexpr std::int64_t ZONE_ICON_DEMOLITION = 1;
inline constexpr std::int64_t ZONE_ICON_DIAMONDMINE = 4;
inline constexpr std::int64_t ZONE_ICON_MULTIHILL = 2;
inline constexpr std::int64_t ZONE_ICON_NONE = 0;
inline constexpr std::int64_t ZONE_ICON_OCCUPATION = 3;
inline constexpr std::int64_t ZONE_ICON_SPAWN = 17;
inline constexpr std::int64_t ZONE_ICON_TERRITORY_A = 7;
inline constexpr std::int64_t ZONE_ICON_TERRITORY_B = 8;
inline constexpr std::int64_t ZONE_ICON_TERRITORY_C = 9;
inline constexpr std::int64_t ZONE_ICON_TERRITORY_D = 10;
inline constexpr std::int64_t ZONE_ICON_TERRITORY_E = 11;
inline constexpr std::int64_t ZONE_ICON_TERRITORY_F = 12;
inline constexpr std::int64_t ZONE_ICON_TERRITORY_G = 13;
inline constexpr std::int64_t ZONE_ICON_TERRITORY_H = 14;
inline constexpr std::int64_t ZONE_ICON_TERRITORY_I = 15;
inline constexpr std::int64_t ZONE_ICON_TERRITORY_J = 16;
inline constexpr std::int64_t ZONE_ICON_VIP = 5;
inline constexpr std::int64_t ZOOMED_CROSSHAIR = 1;
inline constexpr std::int64_t Z_ABOVE_WATERPLANE = 238;
inline constexpr std::int64_t ugc_zone_key = 18;

// -------------------------------------------------------------------------
// Teams (retail shared/constants.py:213-245). Colors are RGB 0-255.
// -------------------------------------------------------------------------

inline constexpr int SPECTATOR_ID = 0;
inline constexpr std::array<int, 3> SPECTATOR_COLOR{255, 255, 255};
inline constexpr int NEUTRAL_ID = 1;
inline constexpr std::array<int, 3> NEUTRAL_COLOR{128, 128, 128};
inline constexpr int TEAM1_ID = 2;
inline constexpr std::array<int, 3> TEAM1_COLOR{44, 117, 179};
inline constexpr int TEAM2_ID = 3;
inline constexpr std::array<int, 3> TEAM2_COLOR{137, 179, 44};
inline constexpr std::array<int, 3> BASE_CONTESTED_COLOUR{255, 100, 0};

namespace classes {

// Per-class stats/multipliers (retail shared/constants.py:5333-5620).
// Loadout/model lists are omitted (non-numeric).
struct ClassStats final {
    int id;
    const char* name; // retail display_name key
    bool can_sprint_uphill;
    double accel_multiplier;
    double crouch_sneak_multiplier;
    double damage_multiplier;
    double fall_max_damage;
    double fall_max_distance;
    double fall_min_distance;
    double fall_on_water_multiplier;
    double headshot_damage_multiplier;
    double jump_multiplier;
    double sprint_multiplier;
    double water_friction;
    int max_blocks;
    int starting_blocks;
};

inline constexpr ClassStats CLASS_CLASSIC_SOLDIER{
    .id = 5,
    .name = "CLASSIC_SOLDIER",
    .can_sprint_uphill = false,
    .accel_multiplier = 1.0,
    .crouch_sneak_multiplier = 0.5,
    .damage_multiplier = 1.0,
    .fall_max_damage = 100.0,
    .fall_max_distance = 26.0,
    .fall_min_distance = 6.0,
    .fall_on_water_multiplier = 1.0,
    .headshot_damage_multiplier = 1.0,
    .jump_multiplier = 1.0,
    .sprint_multiplier = 1.33,
    .water_friction = 8.0,
    .max_blocks = 100,
    .starting_blocks = 25,
};
inline constexpr ClassStats CLASS_ENGINEER{
    .id = 12,
    .name = "ENGINEER2",
    .can_sprint_uphill = true,
    .accel_multiplier = 0.7,
    .crouch_sneak_multiplier = 0.5,
    .damage_multiplier = 1.1765,
    .fall_max_damage = 100.0,
    .fall_max_distance = 40.0,
    .fall_min_distance = 10.0,
    .fall_on_water_multiplier = 0.5,
    .headshot_damage_multiplier = 1.5,
    .jump_multiplier = 1.0,
    .sprint_multiplier = 1.25,
    .water_friction = 8.0,
    .max_blocks = 3000,
    .starting_blocks = 2000,
};
inline constexpr ClassStats CLASS_FAST_ZOMBIE{
    .id = 14,
    .name = "FAST_ZOMBIE",
    .can_sprint_uphill = true,
    .accel_multiplier = 1.1,
    .crouch_sneak_multiplier = 0.25,
    .damage_multiplier = 0.5,
    .fall_max_damage = 500.0,
    .fall_max_distance = 20.0,
    .fall_min_distance = 6.0,
    .fall_on_water_multiplier = 2.0,
    .headshot_damage_multiplier = 2.5,
    .jump_multiplier = 2.5,
    .sprint_multiplier = 3.0,
    .water_friction = 12.0,
    .max_blocks = 1000,
    .starting_blocks = 500,
};
inline constexpr ClassStats CLASS_GANGSTER_1{
    .id = 6,
    .name = "GANGSTER_1_NAME",
    .can_sprint_uphill = true,
    .accel_multiplier = 0.7,
    .crouch_sneak_multiplier = 0.5,
    .damage_multiplier = 1.0,
    .fall_max_damage = 100.0,
    .fall_max_distance = 40.0,
    .fall_min_distance = 10.0,
    .fall_on_water_multiplier = 0.5,
    .headshot_damage_multiplier = 1.2,
    .jump_multiplier = 1.2,
    .sprint_multiplier = 1.5,
    .water_friction = 8.0,
    .max_blocks = 1200,
    .starting_blocks = 500,
};
inline constexpr ClassStats CLASS_GANGSTER_2{
    .id = 7,
    .name = "GANGSTER_2_NAME",
    .can_sprint_uphill = true,
    .accel_multiplier = 0.7,
    .crouch_sneak_multiplier = 0.5,
    .damage_multiplier = 1.0,
    .fall_max_damage = 100.0,
    .fall_max_distance = 40.0,
    .fall_min_distance = 10.0,
    .fall_on_water_multiplier = 0.5,
    .headshot_damage_multiplier = 1.2,
    .jump_multiplier = 1.2,
    .sprint_multiplier = 1.5,
    .water_friction = 8.0,
    .max_blocks = 1200,
    .starting_blocks = 500,
};
inline constexpr ClassStats CLASS_GANGSTER_3{
    .id = 8,
    .name = "GANGSTER_3_NAME",
    .can_sprint_uphill = true,
    .accel_multiplier = 0.7,
    .crouch_sneak_multiplier = 0.5,
    .damage_multiplier = 1.0,
    .fall_max_damage = 100.0,
    .fall_max_distance = 40.0,
    .fall_min_distance = 10.0,
    .fall_on_water_multiplier = 0.5,
    .headshot_damage_multiplier = 1.2,
    .jump_multiplier = 1.2,
    .sprint_multiplier = 1.5,
    .water_friction = 8.0,
    .max_blocks = 1200,
    .starting_blocks = 500,
};
inline constexpr ClassStats CLASS_GANGSTER_4{
    .id = 9,
    .name = "GANGSTER_4_NAME",
    .can_sprint_uphill = true,
    .accel_multiplier = 0.7,
    .crouch_sneak_multiplier = 0.5,
    .damage_multiplier = 1.0,
    .fall_max_damage = 100.0,
    .fall_max_distance = 40.0,
    .fall_min_distance = 10.0,
    .fall_on_water_multiplier = 0.5,
    .headshot_damage_multiplier = 1.2,
    .jump_multiplier = 1.2,
    .sprint_multiplier = 1.5,
    .water_friction = 8.0,
    .max_blocks = 1200,
    .starting_blocks = 500,
};
inline constexpr ClassStats CLASS_GANGSTER_VIP_1{
    .id = 10,
    .name = "GANGSTER_VIP_1_NAME",
    .can_sprint_uphill = true,
    .accel_multiplier = 0.7,
    .crouch_sneak_multiplier = 0.5,
    .damage_multiplier = 1.0,
    .fall_max_damage = 100.0,
    .fall_max_distance = 40.0,
    .fall_min_distance = 10.0,
    .fall_on_water_multiplier = 0.5,
    .headshot_damage_multiplier = 1.2,
    .jump_multiplier = 1.2,
    .sprint_multiplier = 1.5,
    .water_friction = 8.0,
    .max_blocks = 1200,
    .starting_blocks = 500,
};
inline constexpr ClassStats CLASS_GANGSTER_VIP_2{
    .id = 11,
    .name = "GANGSTER_VIP_2_NAME",
    .can_sprint_uphill = true,
    .accel_multiplier = 0.7,
    .crouch_sneak_multiplier = 0.5,
    .damage_multiplier = 1.0,
    .fall_max_damage = 100.0,
    .fall_max_distance = 40.0,
    .fall_min_distance = 10.0,
    .fall_on_water_multiplier = 0.5,
    .headshot_damage_multiplier = 1.2,
    .jump_multiplier = 1.2,
    .sprint_multiplier = 1.5,
    .water_friction = 8.0,
    .max_blocks = 1200,
    .starting_blocks = 500,
};
inline constexpr ClassStats CLASS_JUMP_ZOMBIE{
    .id = 15,
    .name = "JUMP_ZOMBIE",
    .can_sprint_uphill = false,
    .accel_multiplier = 0.5,
    .crouch_sneak_multiplier = 0.5,
    .damage_multiplier = 0.5,
    .fall_max_damage = 100.0,
    .fall_max_distance = 25.0,
    .fall_min_distance = 15.0,
    .fall_on_water_multiplier = 0.5,
    .headshot_damage_multiplier = 1.5,
    .jump_multiplier = 3.0,
    .sprint_multiplier = 1.0,
    .water_friction = 8.0,
    .max_blocks = 1000,
    .starting_blocks = 500,
};
inline constexpr ClassStats CLASS_MEDIC{
    .id = 17,
    .name = "MEDIC",
    .can_sprint_uphill = true,
    .accel_multiplier = 0.6,
    .crouch_sneak_multiplier = 0.5,
    .damage_multiplier = 1.0,
    .fall_max_damage = 100.0,
    .fall_max_distance = 40.0,
    .fall_min_distance = 10.0,
    .fall_on_water_multiplier = 0.5,
    .headshot_damage_multiplier = 1.0,
    .jump_multiplier = 1.2,
    .sprint_multiplier = 1.35,
    .water_friction = 8.0,
    .max_blocks = 2000,
    .starting_blocks = 900,
};
inline constexpr ClassStats CLASS_MINER{
    .id = 3,
    .name = "MINER",
    .can_sprint_uphill = true,
    .accel_multiplier = 0.7,
    .crouch_sneak_multiplier = 0.5,
    .damage_multiplier = 1.1765,
    .fall_max_damage = 100.0,
    .fall_max_distance = 40.0,
    .fall_min_distance = 10.0,
    .fall_on_water_multiplier = 0.5,
    .headshot_damage_multiplier = 0.5,
    .jump_multiplier = 1.2,
    .sprint_multiplier = 1.4,
    .water_friction = 8.0,
    .max_blocks = 1000,
    .starting_blocks = 0,
};
inline constexpr ClassStats CLASS_ROCKETEER{
    .id = 2,
    .name = "ENGINEER",
    .can_sprint_uphill = true,
    .accel_multiplier = 0.7,
    .crouch_sneak_multiplier = 0.5,
    .damage_multiplier = 1.43,
    .fall_max_damage = 10.0,
    .fall_max_distance = 10.0,
    .fall_min_distance = 10.0,
    .fall_on_water_multiplier = 0.5,
    .headshot_damage_multiplier = 1.5,
    .jump_multiplier = 1.0,
    .sprint_multiplier = 1.1,
    .water_friction = 12.0,
    .max_blocks = 1500,
    .starting_blocks = 500,
};
inline constexpr ClassStats CLASS_SCOUT{
    .id = 1,
    .name = "SCOUT",
    .can_sprint_uphill = true,
    .accel_multiplier = 0.7,
    .crouch_sneak_multiplier = 0.5,
    .damage_multiplier = 1.43,
    .fall_max_damage = 100.0,
    .fall_max_distance = 30.0,
    .fall_min_distance = 10.0,
    .fall_on_water_multiplier = 0.5,
    .headshot_damage_multiplier = 1.5,
    .jump_multiplier = 1.5,
    .sprint_multiplier = 1.45,
    .water_friction = 8.0,
    .max_blocks = 1000,
    .starting_blocks = 400,
};
inline constexpr ClassStats CLASS_SOLDIER{
    .id = 0,
    .name = "SOLDIER",
    .can_sprint_uphill = true,
    .accel_multiplier = 0.7,
    .crouch_sneak_multiplier = 0.5,
    .damage_multiplier = 1.0,
    .fall_max_damage = 100.0,
    .fall_max_distance = 40.0,
    .fall_min_distance = 10.0,
    .fall_on_water_multiplier = 0.5,
    .headshot_damage_multiplier = 1.0,
    .jump_multiplier = 1.2,
    .sprint_multiplier = 1.4,
    .water_friction = 8.0,
    .max_blocks = 1000,
    .starting_blocks = 200,
};
inline constexpr ClassStats CLASS_SPECIALIST{
    .id = 16,
    .name = "SPECIALIST",
    .can_sprint_uphill = true,
    .accel_multiplier = 0.85,
    .crouch_sneak_multiplier = 0.5,
    .damage_multiplier = 1.1765,
    .fall_max_damage = 100.0,
    .fall_max_distance = 40.0,
    .fall_min_distance = 10.0,
    .fall_on_water_multiplier = 0.5,
    .headshot_damage_multiplier = 1.5,
    .jump_multiplier = 1.5,
    .sprint_multiplier = 1.55,
    .water_friction = 8.0,
    .max_blocks = 1000,
    .starting_blocks = 400,
};
inline constexpr ClassStats CLASS_UGCBUILDER{
    .id = 13,
    .name = "UGCBUILDER",
    .can_sprint_uphill = true,
    .accel_multiplier = 1.0,
    .crouch_sneak_multiplier = 0.5,
    .damage_multiplier = 0.0,
    .fall_max_damage = 0.0,
    .fall_max_distance = 255.0,
    .fall_min_distance = 255.0,
    .fall_on_water_multiplier = 1.0,
    .headshot_damage_multiplier = 0.0,
    .jump_multiplier = 1.0,
    .sprint_multiplier = 3.0,
    .water_friction = 1.0,
    .max_blocks = 1,
    .starting_blocks = 1,
};
inline constexpr ClassStats CLASS_ZOMBIE{
    .id = 4,
    .name = "ZOMBIE",
    .can_sprint_uphill = true,
    .accel_multiplier = 0.5,
    .crouch_sneak_multiplier = 0.5,
    .damage_multiplier = 0.6,
    .fall_max_damage = 100.0,
    .fall_max_distance = 60.0,
    .fall_min_distance = 10.0,
    .fall_on_water_multiplier = 0.25,
    .headshot_damage_multiplier = 1.0,
    .jump_multiplier = 1.5,
    .sprint_multiplier = 1.65,
    .water_friction = 4.0,
    .max_blocks = 2000,
    .starting_blocks = 1000,
};

inline constexpr std::array<ClassStats, 18> ALL_CLASSES{
    CLASS_CLASSIC_SOLDIER,
    CLASS_ENGINEER,
    CLASS_FAST_ZOMBIE,
    CLASS_GANGSTER_1,
    CLASS_GANGSTER_2,
    CLASS_GANGSTER_3,
    CLASS_GANGSTER_4,
    CLASS_GANGSTER_VIP_1,
    CLASS_GANGSTER_VIP_2,
    CLASS_JUMP_ZOMBIE,
    CLASS_MEDIC,
    CLASS_MINER,
    CLASS_ROCKETEER,
    CLASS_SCOUT,
    CLASS_SOLDIER,
    CLASS_SPECIALIST,
    CLASS_UGCBUILDER,
    CLASS_ZOMBIE,
};

} // namespace classes

namespace tools {

// Tool registry (retail shared/constants.py:858-942 ids, :1691-1836 kv6/names).
// show_crosshair uses the *_CROSSHAIR enum constants above; -1 means the
// retail data had no value (0 is a real value, NEVER_CROSSHAIR).
// SNIPER_TOOL has per-team icons in retail; the first (blue) is kept here.
struct ToolInfo final {
    int id;
    const char* name;
    const char* kv6;
    const char* icon;
    const char* kind;
    int show_crosshair;
};

inline constexpr ToolInfo A369_UNKNOWN{
    .id = 71,
    .name = "A369_UNKNOWN",
    .kv6 = "",
    .icon = "",
    .kind = "weapon",
    .show_crosshair = -1,
};
inline constexpr ToolInfo ANTIPERSONNEL_GRENADE_TOOL{
    .id = 32,
    .name = "ANTIPERSONNEL_GRENADE_TOOL",
    .kv6 = "antipersonnel_grenade",
    .icon = "antipersonnel_grenade",
    .kind = "equipment",
    .show_crosshair = 4,
};
inline constexpr ToolInfo ASSAULT_RIFLE_TOOL{
    .id = 60,
    .name = "ASSAULT_RIFLE_TOOL",
    .kv6 = "assaultRifle",
    .icon = "assaultRifle",
    .kind = "weapon",
    .show_crosshair = 3,
};
inline constexpr ToolInfo AUTOMATIC_PISTOL_TOOL{
    .id = 53,
    .name = "AUTOMATIC_PISTOL_TOOL",
    .kv6 = "autoPistol",
    .icon = "autoPistol",
    .kind = "weapon",
    .show_crosshair = 3,
};
inline constexpr ToolInfo AUTO_SHOTGUN_TOOL{
    .id = 62,
    .name = "AUTO_SHOTGUN_TOOL",
    .kv6 = "autoShotgun",
    .icon = "autoShotgun",
    .kind = "weapon",
    .show_crosshair = 3,
};
inline constexpr ToolInfo BLOCK_SUCKER_TOOL{
    .id = 63,
    .name = "BLOCK_SUCKER_TOOL",
    .kv6 = "blocksucker",
    .icon = "blocksucker",
    .kind = "weapon",
    .show_crosshair = 4,
};
inline constexpr ToolInfo BLOCK_TOOL{
    .id = 5,
    .name = "BLOCK_TOOL",
    .kv6 = "block",
    .icon = "block",
    .kind = "block",
    .show_crosshair = 3,
};
inline constexpr ToolInfo BOMB_TOOL{
    .id = 25,
    .name = "BOMB_TOOL",
    .kv6 = "bomb",
    .icon = "bomb",
    .kind = "weapon",
    .show_crosshair = 0,
};
inline constexpr ToolInfo C4_TOOL{
    .id = 59,
    .name = "C4_TOOL",
    .kv6 = "c4",
    .icon = "c4",
    .kind = "equipment",
    .show_crosshair = 4,
};
inline constexpr ToolInfo CHEMICALBOMB_TOOL{
    .id = 54,
    .name = "CHEMICALBOMB_TOOL",
    .kv6 = "chemicalbomb",
    .icon = "chemicalbomb",
    .kind = "equipment",
    .show_crosshair = 4,
};
inline constexpr ToolInfo CLASSIC_GRENADE_TOOL{
    .id = 31,
    .name = "CLASSIC_GRENADE_TOOL",
    .kv6 = "grenade",
    .icon = "grenade",
    .kind = "equipment",
    .show_crosshair = 4,
};
inline constexpr ToolInfo CLASSIC_SHOTGUN_TOOL{
    .id = 37,
    .name = "CLASSIC_SHOTGUN_TOOL",
    .kv6 = "classic_shotgun",
    .icon = "classic_shotgun",
    .kind = "weapon",
    .show_crosshair = 3,
};
inline constexpr ToolInfo CLASSIC_SMG_TOOL{
    .id = 38,
    .name = "CLASSIC_SMG_TOOL",
    .kv6 = "classic_smg",
    .icon = "classic_smg",
    .kind = "weapon",
    .show_crosshair = 3,
};
inline constexpr ToolInfo CLASSIC_SPADE_TOOL{
    .id = 4,
    .name = "CLASSIC_SPADE_TOOL",
    .kv6 = "spade",
    .icon = "spade",
    .kind = "melee",
    .show_crosshair = 3,
};
inline constexpr ToolInfo CROWBAR_TOOL{
    .id = 34,
    .name = "CROWBAR_TOOL",
    .kv6 = "Weapon_Crowbar",
    .icon = "Weapon_Crowbar",
    .kind = "melee",
    .show_crosshair = 3,
};
inline constexpr ToolInfo DIAMOND_TOOL{
    .id = 26,
    .name = "DIAMOND_TOOL",
    .kv6 = "diamond",
    .icon = "diamond",
    .kind = "weapon",
    .show_crosshair = 0,
};
inline constexpr ToolInfo DISGUISE_TOOL{
    .id = 64,
    .name = "DISGUISE_TOOL",
    .kv6 = "disguise",
    .icon = "disguise",
    .kind = "equipment",
    .show_crosshair = 3,
};
inline constexpr ToolInfo DRILLGUN_TOOL{
    .id = 14,
    .name = "DRILLGUN_TOOL",
    .kv6 = "drillgun",
    .icon = "drillgun",
    .kind = "weapon",
    .show_crosshair = 4,
};
inline constexpr ToolInfo DYNAMITE_TOOL{
    .id = 21,
    .name = "DYNAMITE_TOOL",
    .kv6 = "dynamite",
    .icon = "dynamite",
    .kind = "equipment",
    .show_crosshair = 4,
};
inline constexpr ToolInfo FAKE_PISTOL_TOOL{
    .id = 40,
    .name = "FAKE_PISTOL_TOOL",
    .kv6 = "pistol",
    .icon = "",
    .kind = "weapon",
    .show_crosshair = 0,
};
inline constexpr ToolInfo FLAREBLOCK_TOOL{
    .id = 22,
    .name = "FLAREBLOCK_TOOL",
    .kv6 = "glowblock",
    .icon = "glowblock",
    .kind = "block",
    .show_crosshair = 3,
};
inline constexpr ToolInfo GRENADE_LAUNCHER_WEAPON_TOOL{
    .id = 55,
    .name = "GRENADE_LAUNCHER_WEAPON_TOOL",
    .kv6 = "grenadelauncher",
    .icon = "grenadelauncher",
    .kind = "weapon",
    .show_crosshair = 4,
};
inline constexpr ToolInfo GRENADE_TOOL{
    .id = 11,
    .name = "GRENADE_TOOL",
    .kv6 = "grenade",
    .icon = "grenade",
    .kind = "equipment",
    .show_crosshair = 4,
};
inline constexpr ToolInfo INTEL_TOOL{
    .id = 30,
    .name = "INTEL_TOOL",
    .kv6 = "intel",
    .icon = "intel",
    .kind = "weapon",
    .show_crosshair = 0,
};
inline constexpr ToolInfo JETPACK2{
    .id = 67,
    .name = "JETPACK2",
    .kv6 = "jetpack2",
    .icon = "jetpack2",
    .kind = "equipment",
    .show_crosshair = -1,
};
inline constexpr ToolInfo JETPACK_ENGINEER{
    .id = 68,
    .name = "JETPACK_ENGINEER",
    .kv6 = "jetpack_engineer",
    .icon = "jetpack_engineer",
    .kind = "equipment",
    .show_crosshair = -1,
};
inline constexpr ToolInfo JETPACK_NORMAL{
    .id = 66,
    .name = "JETPACK_NORMAL",
    .kv6 = "jetpack",
    .icon = "jetpack",
    .kind = "equipment",
    .show_crosshair = -1,
};
inline constexpr ToolInfo JETPACK_UGCBUILDER{
    .id = 69,
    .name = "JETPACK_UGCBUILDER",
    .kv6 = "jetpack_ugcbuilder",
    .icon = "jetpack_ugcbuilder",
    .kind = "equipment",
    .show_crosshair = -1,
};
inline constexpr ToolInfo KNIFE_TOOL{
    .id = 1,
    .name = "KNIFE_TOOL",
    .kv6 = "knife",
    .icon = "knife",
    .kind = "melee",
    .show_crosshair = 3,
};
inline constexpr ToolInfo LANDMINE_TOOL{
    .id = 20,
    .name = "LANDMINE_TOOL",
    .kv6 = "land_mine",
    .icon = "land_mine",
    .kind = "equipment",
    .show_crosshair = 4,
};
inline constexpr ToolInfo LIGHT_MACHINE_GUN_TOOL{
    .id = 61,
    .name = "LIGHT_MACHINE_GUN_TOOL",
    .kv6 = "lightMachineGun",
    .icon = "lightMachineGun",
    .kind = "weapon",
    .show_crosshair = 3,
};
inline constexpr ToolInfo MACHETE_TOOL{
    .id = 50,
    .name = "MACHETE_TOOL",
    .kv6 = "machete",
    .icon = "machete",
    .kind = "melee",
    .show_crosshair = 3,
};
inline constexpr ToolInfo MEDPACK_TOOL{
    .id = 51,
    .name = "MEDPACK_TOOL",
    .kv6 = "medpack",
    .icon = "medpack",
    .kind = "equipment",
    .show_crosshair = 4,
};
inline constexpr ToolInfo MG_TOOL{
    .id = 15,
    .name = "MG_TOOL",
    .kv6 = "mg",
    .icon = "",
    .kind = "weapon",
    .show_crosshair = 3,
};
inline constexpr ToolInfo MINE_LAUNCHER_TOOL{
    .id = 58,
    .name = "MINE_LAUNCHER_TOOL",
    .kv6 = "minelauncher",
    .icon = "minelauncher",
    .kind = "weapon",
    .show_crosshair = 4,
};
inline constexpr ToolInfo MINIGUN_TOOL{
    .id = 8,
    .name = "MINIGUN_TOOL",
    .kv6 = "minigun",
    .icon = "minigun",
    .kind = "weapon",
    .show_crosshair = 3,
};
inline constexpr ToolInfo MOLOTOV_TOOL{
    .id = 33,
    .name = "MOLOTOV_TOOL",
    .kv6 = "Weapon_Molotov",
    .icon = "Weapon_Molotov",
    .kind = "equipment",
    .show_crosshair = 4,
};
inline constexpr ToolInfo NO_JETPACK{
    .id = 65,
    .name = "NO_JETPACK",
    .kv6 = "",
    .icon = "",
    .kind = "equipment",
    .show_crosshair = -1,
};
inline constexpr ToolInfo NULL_TOOL{
    .id = 39,
    .name = "NULL_TOOL",
    .kv6 = "null_tool",
    .icon = "",
    .kind = "weapon",
    .show_crosshair = 0,
};
inline constexpr ToolInfo PAINTBRUSH_TOOL{
    .id = 43,
    .name = "PAINTBRUSH_TOOL",
    .kv6 = "paintbrush",
    .icon = "paintbrush",
    .kind = "block",
    .show_crosshair = 3,
};
inline constexpr ToolInfo PARACHUTE_TOOL_A370{
    .id = 72,
    .name = "PARACHUTE_TOOL_A370",
    .kv6 = "parachute",
    .icon = "parachute",
    .kind = "equipment",
    .show_crosshair = -1,
};
inline constexpr ToolInfo PICKAXE_TOOL{
    .id = 0,
    .name = "PICKAXE_TOOL",
    .kv6 = "pickaxe",
    .icon = "pickaxe",
    .kind = "melee",
    .show_crosshair = 3,
};
inline constexpr ToolInfo PISTOL_TOOL{
    .id = 17,
    .name = "PISTOL_TOOL",
    .kv6 = "pistol",
    .icon = "pistol",
    .kind = "weapon",
    .show_crosshair = 3,
};
inline constexpr ToolInfo PREFAB_TOOL{
    .id = 23,
    .name = "PREFAB_TOOL",
    .kv6 = "prefab",
    .icon = "prefab",
    .kind = "prefab",
    .show_crosshair = 3,
};
inline constexpr ToolInfo RADAR_STATION_TOOL{
    .id = 56,
    .name = "RADAR_STATION_TOOL",
    .kv6 = "radar_station",
    .icon = "radar_station",
    .kind = "equipment",
    .show_crosshair = 4,
};
inline constexpr ToolInfo RIFLE_TOOL{
    .id = 6,
    .name = "RIFLE_TOOL",
    .kv6 = "semi",
    .icon = "semi",
    .kind = "weapon",
    .show_crosshair = 2,
};
inline constexpr ToolInfo RIOTSHIELD_TOOL{
    .id = 52,
    .name = "RIOTSHIELD_TOOL",
    .kv6 = "riotshield",
    .icon = "riotshield",
    .kind = "melee",
    .show_crosshair = 3,
};
inline constexpr ToolInfo RIOTSTICK_TOOL{
    .id = 49,
    .name = "RIOTSTICK_TOOL",
    .kv6 = "riotstick",
    .icon = "riotstick",
    .kind = "melee",
    .show_crosshair = 3,
};
inline constexpr ToolInfo ROCKET_TURRET_TOOL{
    .id = 16,
    .name = "ROCKET_TURRET_TOOL",
    .kv6 = "rocket_turret",
    .icon = "rocket_turret",
    .kind = "weapon",
    .show_crosshair = 4,
};
inline constexpr ToolInfo RPG2_TOOL{
    .id = 13,
    .name = "RPG2_TOOL",
    .kv6 = "rpg2",
    .icon = "rpg2",
    .kind = "weapon",
    .show_crosshair = 4,
};
inline constexpr ToolInfo RPG_TOOL{
    .id = 12,
    .name = "RPG_TOOL",
    .kv6 = "rpg",
    .icon = "rpg",
    .kind = "weapon",
    .show_crosshair = 4,
};
inline constexpr ToolInfo SHOTGUN2_TOOL{
    .id = 10,
    .name = "SHOTGUN2_TOOL",
    .kv6 = "shotgun2",
    .icon = "shotgun2",
    .kind = "weapon",
    .show_crosshair = 3,
};
inline constexpr ToolInfo SHOTGUN_TOOL{
    .id = 9,
    .name = "SHOTGUN_TOOL",
    .kv6 = "shotgun",
    .icon = "shotgun",
    .kind = "weapon",
    .show_crosshair = 3,
};
inline constexpr ToolInfo SHRAPNEL_TOOL{
    .id = 27,
    .name = "SHRAPNEL_TOOL",
    .kv6 = "shrapnel",
    .icon = "",
    .kind = "weapon",
    .show_crosshair = -1,
};
inline constexpr ToolInfo SMG_TOOL{
    .id = 7,
    .name = "SMG_TOOL",
    .kv6 = "smg",
    .icon = "smg",
    .kind = "weapon",
    .show_crosshair = 3,
};
inline constexpr ToolInfo SNIPER2_TOOL{
    .id = 19,
    .name = "SNIPER2_TOOL",
    .kv6 = "sniper2",
    .icon = "sniper2",
    .kind = "weapon",
    .show_crosshair = 4,
};
inline constexpr ToolInfo SNIPER_TOOL{
    .id = 18,
    .name = "SNIPER_TOOL",
    .kv6 = "sniper",
    .icon = "sniper_blue",
    .kind = "weapon",
    .show_crosshair = 4,
};
inline constexpr ToolInfo SNOWBLOWER_TOOL{
    .id = 29,
    .name = "SNOWBLOWER_TOOL",
    .kv6 = "snowblower",
    .icon = "snowblower",
    .kind = "weapon",
    .show_crosshair = 4,
};
inline constexpr ToolInfo SNUB_PISTOL_TOOL{
    .id = 36,
    .name = "SNUB_PISTOL_TOOL",
    .kv6 = "Weapon_SnubNosePistol",
    .icon = "Weapon_SnubNosePistol",
    .kind = "weapon",
    .show_crosshair = 3,
};
inline constexpr ToolInfo SPADE_TOOL{
    .id = 2,
    .name = "SPADE_TOOL",
    .kv6 = "spade",
    .icon = "spade",
    .kind = "melee",
    .show_crosshair = 3,
};
inline constexpr ToolInfo STICKY_GRENADE_TOOL{
    .id = 57,
    .name = "STICKY_GRENADE_TOOL",
    .kv6 = "stickygrenade",
    .icon = "stickygrenade",
    .kind = "equipment",
    .show_crosshair = 4,
};
inline constexpr ToolInfo SUPERSPADE_TOOL{
    .id = 3,
    .name = "SUPERSPADE_TOOL",
    .kv6 = "superspade",
    .icon = "superspade",
    .kind = "melee",
    .show_crosshair = 3,
};
inline constexpr ToolInfo TOMMYGUN_TOOL{
    .id = 35,
    .name = "TOMMYGUN_TOOL",
    .kv6 = "Weapon_TommyGun",
    .icon = "Weapon_TommyGun",
    .kind = "weapon",
    .show_crosshair = 3,
};
inline constexpr ToolInfo UGC_DRILLGUN_TOOL{
    .id = 47,
    .name = "UGC_DRILLGUN_TOOL",
    .kv6 = "drillgun",
    .icon = "drillgun",
    .kind = "weapon",
    .show_crosshair = 4,
};
inline constexpr ToolInfo UGC_PICKAXE_TOOL{
    .id = 44,
    .name = "UGC_PICKAXE_TOOL",
    .kv6 = "ugc_pickaxe",
    .icon = "ugc_pickaxe",
    .kind = "melee",
    .show_crosshair = 3,
};
inline constexpr ToolInfo UGC_PREFAB_TOOL{
    .id = 42,
    .name = "UGC_PREFAB_TOOL",
    .kv6 = "ugc_prefab",
    .icon = "",
    .kind = "prefab",
    .show_crosshair = 3,
};
inline constexpr ToolInfo UGC_RPG2_TOOL{
    .id = 46,
    .name = "UGC_RPG2_TOOL",
    .kv6 = "ugc_rpg2",
    .icon = "ugc_rpg2",
    .kind = "weapon",
    .show_crosshair = 4,
};
inline constexpr ToolInfo UGC_SNOWBLOWER_TOOL{
    .id = 48,
    .name = "UGC_SNOWBLOWER_TOOL",
    .kv6 = "snowblower",
    .icon = "snowblower",
    .kind = "weapon",
    .show_crosshair = 4,
};
inline constexpr ToolInfo UGC_SUPERSPADE_TOOL{
    .id = 45,
    .name = "UGC_SUPERSPADE_TOOL",
    .kv6 = "ugc_superspade",
    .icon = "ugc_superspade",
    .kind = "melee",
    .show_crosshair = 3,
};
inline constexpr ToolInfo UGC_TOOL{
    .id = 41,
    .name = "UGC_TOOL",
    .kv6 = "ugc_tool",
    .icon = "ugc_tool",
    .kind = "weapon",
    .show_crosshair = 4,
};
inline constexpr ToolInfo ZOMBIEHAND_TOOL{
    .id = 24,
    .name = "ZOMBIEHAND_TOOL",
    .kv6 = "zombie_hands",
    .icon = "zombie_hands",
    .kind = "melee",
    .show_crosshair = 3,
};
inline constexpr ToolInfo ZOMBIE_PREFAB_TOOL{
    .id = 28,
    .name = "ZOMBIE_PREFAB_TOOL",
    .kv6 = "prefab",
    .icon = "prefab",
    .kind = "prefab",
    .show_crosshair = 3,
};

inline constexpr std::array<ToolInfo, 72> ALL_TOOLS{
    A369_UNKNOWN,
    ANTIPERSONNEL_GRENADE_TOOL,
    ASSAULT_RIFLE_TOOL,
    AUTOMATIC_PISTOL_TOOL,
    AUTO_SHOTGUN_TOOL,
    BLOCK_SUCKER_TOOL,
    BLOCK_TOOL,
    BOMB_TOOL,
    C4_TOOL,
    CHEMICALBOMB_TOOL,
    CLASSIC_GRENADE_TOOL,
    CLASSIC_SHOTGUN_TOOL,
    CLASSIC_SMG_TOOL,
    CLASSIC_SPADE_TOOL,
    CROWBAR_TOOL,
    DIAMOND_TOOL,
    DISGUISE_TOOL,
    DRILLGUN_TOOL,
    DYNAMITE_TOOL,
    FAKE_PISTOL_TOOL,
    FLAREBLOCK_TOOL,
    GRENADE_LAUNCHER_WEAPON_TOOL,
    GRENADE_TOOL,
    INTEL_TOOL,
    JETPACK2,
    JETPACK_ENGINEER,
    JETPACK_NORMAL,
    JETPACK_UGCBUILDER,
    KNIFE_TOOL,
    LANDMINE_TOOL,
    LIGHT_MACHINE_GUN_TOOL,
    MACHETE_TOOL,
    MEDPACK_TOOL,
    MG_TOOL,
    MINE_LAUNCHER_TOOL,
    MINIGUN_TOOL,
    MOLOTOV_TOOL,
    NO_JETPACK,
    NULL_TOOL,
    PAINTBRUSH_TOOL,
    PARACHUTE_TOOL_A370,
    PICKAXE_TOOL,
    PISTOL_TOOL,
    PREFAB_TOOL,
    RADAR_STATION_TOOL,
    RIFLE_TOOL,
    RIOTSHIELD_TOOL,
    RIOTSTICK_TOOL,
    ROCKET_TURRET_TOOL,
    RPG2_TOOL,
    RPG_TOOL,
    SHOTGUN2_TOOL,
    SHOTGUN_TOOL,
    SHRAPNEL_TOOL,
    SMG_TOOL,
    SNIPER2_TOOL,
    SNIPER_TOOL,
    SNOWBLOWER_TOOL,
    SNUB_PISTOL_TOOL,
    SPADE_TOOL,
    STICKY_GRENADE_TOOL,
    SUPERSPADE_TOOL,
    TOMMYGUN_TOOL,
    UGC_DRILLGUN_TOOL,
    UGC_PICKAXE_TOOL,
    UGC_PREFAB_TOOL,
    UGC_RPG2_TOOL,
    UGC_SNOWBLOWER_TOOL,
    UGC_SUPERSPADE_TOOL,
    UGC_TOOL,
    ZOMBIEHAND_TOOL,
    ZOMBIE_PREFAB_TOOL,
};

} // namespace tools

namespace weapons {

// Weapon stat blocks (aoslib/weapons/*.py class attributes with retail
// A-constants resolved). Missing/null numeric fields are 0, except
// secondary_shoot_interval where retail null is kept as -1.0.
struct WeaponStats final {
    int tool_id;
    const char* name;
    double shoot_interval;
    double secondary_shoot_interval; // -1.0 when retail value is None
    double reload_time;
    double range;
    double block_damage;
    double block_penetration;
    // (torso, head, arms, left_leg, right_leg); scalar retail damage is
    // broadcast to all five entries.
    std::array<double, 5> damage;
    // (max_ammo, initial_ammo, max_clip, initial_stock) per
    // aoslib/weapons/weapon.py:78.
    std::array<int, 4> ammo;
    double recoil_up;
    double recoil_side;
    double accuracy;
};

inline constexpr WeaponStats ANTIPERSONNEL_GRENADE_TOOL{
    .tool_id = 32,
    .name = "ANTIPERSONNEL_GRENADE_TOOL",
    .shoot_interval = 0.5,
    .secondary_shoot_interval = -1.0,
    .reload_time = 0.0,
    .range = 0.0,
    .block_damage = 0.0,
    .block_penetration = 0.0,
    .damage = {0.0, 0.0, 0.0, 0.0, 0.0},
    .ammo = {0, 0, 0, 0},
    .recoil_up = 0.0,
    .recoil_side = 0.0,
    .accuracy = 0.0,
};
inline constexpr WeaponStats ASSAULT_RIFLE_TOOL{
    .tool_id = 60,
    .name = "ASSAULT_RIFLE_TOOL",
    .shoot_interval = 0.5,
    .secondary_shoot_interval = -1.0,
    .reload_time = 0.9,
    .range = 400.0,
    .block_damage = 2.5,
    .block_penetration = 2.5,
    .damage = {20.0, 40.0, 20.0, 20.0, 20.0},
    .ammo = {15, 15, 60, 60},
    .recoil_up = -0.0075,
    .recoil_side = 0.00001,
    .accuracy = 0.01,
};
inline constexpr WeaponStats AUTOMATIC_PISTOL_TOOL{
    .tool_id = 53,
    .name = "AUTOMATIC_PISTOL_TOOL",
    .shoot_interval = 0.175,
    .secondary_shoot_interval = -1.0,
    .reload_time = 1.0,
    .range = 300.0,
    .block_damage = 2.5,
    .block_penetration = 2.5,
    .damage = {15.0, 30.0, 15.0, 15.0, 15.0},
    .ammo = {15, 15, 50, 50},
    .recoil_up = -0.0175,
    .recoil_side = 0.0001,
    .accuracy = 0.02,
};
inline constexpr WeaponStats AUTO_SHOTGUN_TOOL{
    .tool_id = 62,
    .name = "AUTO_SHOTGUN_TOOL",
    .shoot_interval = 0.35,
    .secondary_shoot_interval = -1.0,
    .reload_time = 2.5,
    .range = 60.0,
    .block_damage = 2.0,
    .block_penetration = 2.5,
    .damage = {20.0, 25.0, 10.0, 10.0, 10.0},
    .ammo = {8, 8, 40, 40},
    .recoil_up = -0.05,
    .recoil_side = 0.0005,
    .accuracy = 0.05,
};
inline constexpr WeaponStats BLOCK_SUCKER_TOOL{
    .tool_id = 63,
    .name = "BLOCK_SUCKER_TOOL",
    .shoot_interval = 0.2,
    .secondary_shoot_interval = -1.0,
    .reload_time = 1.0,
    .range = 10000.0,
    .block_damage = 1.0,
    .block_penetration = 2.5,
    .damage = {0.0, 0.0, 0.0, 0.0, 0.0},
    .ammo = {0, 0, 0, 0},
    .recoil_up = 0.0,
    .recoil_side = 0.0,
    .accuracy = 0.03,
};
inline constexpr WeaponStats BLOCK_TOOL{
    .tool_id = 5,
    .name = "BLOCK_TOOL",
    .shoot_interval = 0.5,
    .secondary_shoot_interval = -1.0,
    .reload_time = 0.0,
    .range = 0.0,
    .block_damage = 0.0,
    .block_penetration = 0.0,
    .damage = {0.0, 0.0, 0.0, 0.0, 0.0},
    .ammo = {0, 0, 0, 0},
    .recoil_up = 0.0,
    .recoil_side = 0.0,
    .accuracy = 0.0,
};
inline constexpr WeaponStats BOMB_TOOL{
    .tool_id = 25,
    .name = "BOMB_TOOL",
    .shoot_interval = 0.0,
    .secondary_shoot_interval = -1.0,
    .reload_time = 0.0,
    .range = 0.0,
    .block_damage = 0.0,
    .block_penetration = 0.0,
    .damage = {0.0, 0.0, 0.0, 0.0, 0.0},
    .ammo = {0, 0, 0, 0},
    .recoil_up = 0.0,
    .recoil_side = 0.0,
    .accuracy = 0.0,
};
inline constexpr WeaponStats C4_TOOL{
    .tool_id = 59,
    .name = "C4_TOOL",
    .shoot_interval = 1.0,
    .secondary_shoot_interval = -1.0,
    .reload_time = 0.0,
    .range = 10000.0,
    .block_damage = 1.0,
    .block_penetration = 2.5,
    .damage = {0.0, 0.0, 0.0, 0.0, 0.0},
    .ammo = {2, 2, 0, 0},
    .recoil_up = 0.0,
    .recoil_side = 0.0,
    .accuracy = 0.0,
};
inline constexpr WeaponStats CHEMICALBOMB_TOOL{
    .tool_id = 54,
    .name = "CHEMICALBOMB_TOOL",
    .shoot_interval = 1.0,
    .secondary_shoot_interval = -1.0,
    .reload_time = 0.0,
    .range = 10000.0,
    .block_damage = 1.0,
    .block_penetration = 2.5,
    .damage = {0.0, 0.0, 0.0, 0.0, 0.0},
    .ammo = {4, 2, 0, 0},
    .recoil_up = 0.0,
    .recoil_side = 0.0,
    .accuracy = 0.0,
};
inline constexpr WeaponStats CLASSIC_GRENADE_TOOL{
    .tool_id = 31,
    .name = "CLASSIC_GRENADE_TOOL",
    .shoot_interval = 0.5,
    .secondary_shoot_interval = -1.0,
    .reload_time = 0.0,
    .range = 0.0,
    .block_damage = 0.0,
    .block_penetration = 0.0,
    .damage = {0.0, 0.0, 0.0, 0.0, 0.0},
    .ammo = {0, 0, 0, 0},
    .recoil_up = 0.0,
    .recoil_side = 0.0,
    .accuracy = 0.0,
};
inline constexpr WeaponStats CLASSIC_SHOTGUN_TOOL{
    .tool_id = 37,
    .name = "CLASSIC_SHOTGUN_TOOL",
    .shoot_interval = 1.0,
    .secondary_shoot_interval = -1.0,
    .reload_time = 0.5,
    .range = 75.0,
    .block_damage = 1.0,
    .block_penetration = 2.5,
    .damage = {20.0, 30.0, 12.0, 12.0, 12.0},
    .ammo = {5, 5, 45, 20},
    .recoil_up = -0.1,
    .recoil_side = 0.0002,
    .accuracy = 0.04,
};
inline constexpr WeaponStats CLASSIC_SMG_TOOL{
    .tool_id = 38,
    .name = "CLASSIC_SMG_TOOL",
    .shoot_interval = 0.1,
    .secondary_shoot_interval = -1.0,
    .reload_time = 1.25,
    .range = 100.0,
    .block_damage = 2.0,
    .block_penetration = 2.5,
    .damage = {20.0, 20.0, 20.0, 20.0, 20.0},
    .ammo = {25, 25, 100, 100},
    .recoil_up = -0.007,
    .recoil_side = 0.0,
    .accuracy = 0.01,
};
inline constexpr WeaponStats CLASSIC_SPADE_TOOL{
    .tool_id = 4,
    .name = "CLASSIC_SPADE_TOOL",
    .shoot_interval = 0.3,
    .secondary_shoot_interval = 0.8,
    .reload_time = 0.0,
    .range = 0.0,
    .block_damage = 0.0,
    .block_penetration = 0.0,
    .damage = {3.0, 3.0, 3.0, 3.0, 3.0},
    .ammo = {0, 0, 0, 0},
    .recoil_up = 0.0,
    .recoil_side = 0.0,
    .accuracy = 0.0,
};
inline constexpr WeaponStats CROWBAR_TOOL{
    .tool_id = 34,
    .name = "CROWBAR_TOOL",
    .shoot_interval = 0.6,
    .secondary_shoot_interval = -1.0,
    .reload_time = 0.0,
    .range = 0.0,
    .block_damage = 0.0,
    .block_penetration = 0.0,
    .damage = {5.0, 5.0, 5.0, 5.0, 5.0},
    .ammo = {0, 0, 0, 0},
    .recoil_up = 0.0,
    .recoil_side = 0.0,
    .accuracy = 0.0,
};
inline constexpr WeaponStats DIAMOND_TOOL{
    .tool_id = 26,
    .name = "DIAMOND_TOOL",
    .shoot_interval = 0.0,
    .secondary_shoot_interval = -1.0,
    .reload_time = 0.0,
    .range = 0.0,
    .block_damage = 0.0,
    .block_penetration = 0.0,
    .damage = {0.0, 0.0, 0.0, 0.0, 0.0},
    .ammo = {0, 0, 0, 0},
    .recoil_up = 0.0,
    .recoil_side = 0.0,
    .accuracy = 0.0,
};
inline constexpr WeaponStats DISGUISE_TOOL{
    .tool_id = 64,
    .name = "DISGUISE_TOOL",
    .shoot_interval = 0.5,
    .secondary_shoot_interval = -1.0,
    .reload_time = 0.0,
    .range = 0.0,
    .block_damage = 0.0,
    .block_penetration = 0.0,
    .damage = {0.0, 0.0, 0.0, 0.0, 0.0},
    .ammo = {0, 0, 0, 0},
    .recoil_up = 0.0,
    .recoil_side = 0.0,
    .accuracy = 0.0,
};
inline constexpr WeaponStats DRILLGUN_TOOL{
    .tool_id = 14,
    .name = "DRILLGUN_TOOL",
    .shoot_interval = 0.2,
    .secondary_shoot_interval = -1.0,
    .reload_time = 4.0,
    .range = 10000.0,
    .block_damage = 1.0,
    .block_penetration = 2.5,
    .damage = {0.0, 0.0, 0.0, 0.0, 0.0},
    .ammo = {1, 1, 3, 1},
    .recoil_up = -0.1,
    .recoil_side = 0.0002,
    .accuracy = 0.04,
};
inline constexpr WeaponStats DYNAMITE_TOOL{
    .tool_id = 21,
    .name = "DYNAMITE_TOOL",
    .shoot_interval = 1.0,
    .secondary_shoot_interval = -1.0,
    .reload_time = 0.0,
    .range = 10000.0,
    .block_damage = 1.0,
    .block_penetration = 2.5,
    .damage = {0.0, 0.0, 0.0, 0.0, 0.0},
    .ammo = {1, 1, 0, 0},
    .recoil_up = 0.0,
    .recoil_side = 0.0,
    .accuracy = 0.0,
};
inline constexpr WeaponStats FAKE_PISTOL_TOOL{
    .tool_id = 40,
    .name = "FAKE_PISTOL_TOOL",
    .shoot_interval = 0.0,
    .secondary_shoot_interval = -1.0,
    .reload_time = 0.0,
    .range = 0.0,
    .block_damage = 0.0,
    .block_penetration = 0.0,
    .damage = {0.0, 0.0, 0.0, 0.0, 0.0},
    .ammo = {0, 0, 0, 0},
    .recoil_up = 0.0,
    .recoil_side = 0.0,
    .accuracy = 0.0,
};
inline constexpr WeaponStats FLAREBLOCK_TOOL{
    .tool_id = 22,
    .name = "FLAREBLOCK_TOOL",
    .shoot_interval = 0.5,
    .secondary_shoot_interval = -1.0,
    .reload_time = 0.0,
    .range = 0.0,
    .block_damage = 0.0,
    .block_penetration = 0.0,
    .damage = {0.0, 0.0, 0.0, 0.0, 0.0},
    .ammo = {0, 0, 0, 0},
    .recoil_up = 0.0,
    .recoil_side = 0.0,
    .accuracy = 0.0,
};
inline constexpr WeaponStats GRENADE_LAUNCHER_WEAPON_TOOL{
    .tool_id = 55,
    .name = "GRENADE_LAUNCHER_WEAPON_TOOL",
    .shoot_interval = 0.35,
    .secondary_shoot_interval = -1.0,
    .reload_time = 2.0,
    .range = 10000.0,
    .block_damage = 1.0,
    .block_penetration = 2.5,
    .damage = {0.0, 0.0, 0.0, 0.0, 0.0},
    .ammo = {1, 1, 5, 3},
    .recoil_up = -0.15,
    .recoil_side = 0.0,
    .accuracy = 0.01,
};
inline constexpr WeaponStats GRENADE_TOOL{
    .tool_id = 11,
    .name = "GRENADE_TOOL",
    .shoot_interval = 0.5,
    .secondary_shoot_interval = -1.0,
    .reload_time = 0.0,
    .range = 0.0,
    .block_damage = 0.0,
    .block_penetration = 0.0,
    .damage = {0.0, 0.0, 0.0, 0.0, 0.0},
    .ammo = {0, 0, 0, 0},
    .recoil_up = 0.0,
    .recoil_side = 0.0,
    .accuracy = 0.0,
};
inline constexpr WeaponStats INTEL_TOOL{
    .tool_id = 30,
    .name = "INTEL_TOOL",
    .shoot_interval = 0.0,
    .secondary_shoot_interval = -1.0,
    .reload_time = 0.0,
    .range = 0.0,
    .block_damage = 0.0,
    .block_penetration = 0.0,
    .damage = {0.0, 0.0, 0.0, 0.0, 0.0},
    .ammo = {0, 0, 0, 0},
    .recoil_up = 0.0,
    .recoil_side = 0.0,
    .accuracy = 0.0,
};
inline constexpr WeaponStats KNIFE_TOOL{
    .tool_id = 1,
    .name = "KNIFE_TOOL",
    .shoot_interval = 0.25,
    .secondary_shoot_interval = -1.0,
    .reload_time = 0.0,
    .range = 0.0,
    .block_damage = 0.0,
    .block_penetration = 0.0,
    .damage = {1.0, 1.0, 1.0, 1.0, 1.0},
    .ammo = {0, 0, 0, 0},
    .recoil_up = 0.0,
    .recoil_side = 0.0,
    .accuracy = 0.0,
};
inline constexpr WeaponStats LANDMINE_TOOL{
    .tool_id = 20,
    .name = "LANDMINE_TOOL",
    .shoot_interval = 1.0,
    .secondary_shoot_interval = -1.0,
    .reload_time = 0.0,
    .range = 10000.0,
    .block_damage = 1.0,
    .block_penetration = 2.5,
    .damage = {0.0, 0.0, 0.0, 0.0, 0.0},
    .ammo = {5, 3, 0, 0},
    .recoil_up = 0.0,
    .recoil_side = 0.0,
    .accuracy = 0.0,
};
inline constexpr WeaponStats LIGHT_MACHINE_GUN_TOOL{
    .tool_id = 61,
    .name = "LIGHT_MACHINE_GUN_TOOL",
    .shoot_interval = 0.15,
    .secondary_shoot_interval = -1.0,
    .reload_time = 2.0,
    .range = 175.0,
    .block_damage = 2.5,
    .block_penetration = 2.5,
    .damage = {20.0, 37.0, 20.0, 20.0, 20.0},
    .ammo = {50, 50, 250, 250},
    .recoil_up = -0.02,
    .recoil_side = 0.00002,
    .accuracy = 0.02,
};
inline constexpr WeaponStats MACHETE_TOOL{
    .tool_id = 50,
    .name = "MACHETE_TOOL",
    .shoot_interval = 0.7,
    .secondary_shoot_interval = -1.0,
    .reload_time = 0.0,
    .range = 0.0,
    .block_damage = 0.0,
    .block_penetration = 0.0,
    .damage = {2.0, 2.0, 2.0, 2.0, 2.0},
    .ammo = {0, 0, 0, 0},
    .recoil_up = 0.0,
    .recoil_side = 0.0,
    .accuracy = 0.0,
};
inline constexpr WeaponStats MEDPACK_TOOL{
    .tool_id = 51,
    .name = "MEDPACK_TOOL",
    .shoot_interval = 1.0,
    .secondary_shoot_interval = -1.0,
    .reload_time = 1.5,
    .range = 10000.0,
    .block_damage = 1.0,
    .block_penetration = 2.5,
    .damage = {0.0, 0.0, 0.0, 0.0, 0.0},
    .ammo = {2, 2, 0, 0},
    .recoil_up = 0.0,
    .recoil_side = 0.0,
    .accuracy = 0.0,
};
inline constexpr WeaponStats MG_TOOL{
    .tool_id = 15,
    .name = "MG_TOOL",
    .shoot_interval = 0.5,
    .secondary_shoot_interval = -1.0,
    .reload_time = 4.0,
    .range = 300.0,
    .block_damage = 2.0,
    .block_penetration = 2.5,
    .damage = {0.0, 0.0, 0.0, 0.0, 0.0},
    .ammo = {100, 100, 400, 400},
    .recoil_up = -0.007,
    .recoil_side = 0.0,
    .accuracy = 0.01,
};
inline constexpr WeaponStats MINE_LAUNCHER_TOOL{
    .tool_id = 58,
    .name = "MINE_LAUNCHER_TOOL",
    .shoot_interval = 0.35,
    .secondary_shoot_interval = -1.0,
    .reload_time = 2.0,
    .range = 10000.0,
    .block_damage = 1.0,
    .block_penetration = 2.5,
    .damage = {0.0, 0.0, 0.0, 0.0, 0.0},
    .ammo = {1, 1, 5, 3},
    .recoil_up = -0.15,
    .recoil_side = 0.0,
    .accuracy = 0.01,
};
inline constexpr WeaponStats MINIGUN_TOOL{
    .tool_id = 8,
    .name = "MINIGUN_TOOL",
    .shoot_interval = 0.3,
    .secondary_shoot_interval = -1.0,
    .reload_time = 2.0,
    .range = 100.0,
    .block_damage = 2.5,
    .block_penetration = 2.5,
    .damage = {15.0, 30.0, 15.0, 15.0, 15.0},
    .ammo = {100, 100, 300, 300},
    .recoil_up = -0.003,
    .recoil_side = 0.00002,
    .accuracy = 0.015,
};
inline constexpr WeaponStats MOLOTOV_TOOL{
    .tool_id = 33,
    .name = "MOLOTOV_TOOL",
    .shoot_interval = 1.0,
    .secondary_shoot_interval = -1.0,
    .reload_time = 0.0,
    .range = 10000.0,
    .block_damage = 1.0,
    .block_penetration = 2.5,
    .damage = {0.0, 0.0, 0.0, 0.0, 0.0},
    .ammo = {3, 3, 0, 0},
    .recoil_up = 0.0,
    .recoil_side = 0.0,
    .accuracy = 0.0,
};
inline constexpr WeaponStats NULL_TOOL{
    .tool_id = 39,
    .name = "NULL_TOOL",
    .shoot_interval = 0.0,
    .secondary_shoot_interval = -1.0,
    .reload_time = 0.0,
    .range = 0.0,
    .block_damage = 0.0,
    .block_penetration = 0.0,
    .damage = {0.0, 0.0, 0.0, 0.0, 0.0},
    .ammo = {0, 0, 0, 0},
    .recoil_up = 0.0,
    .recoil_side = 0.0,
    .accuracy = 0.0,
};
inline constexpr WeaponStats PAINTBRUSH_TOOL{
    .tool_id = 43,
    .name = "PAINTBRUSH_TOOL",
    .shoot_interval = 0.05,
    .secondary_shoot_interval = -1.0,
    .reload_time = 0.0,
    .range = 0.0,
    .block_damage = 0.0,
    .block_penetration = 0.0,
    .damage = {0.0, 0.0, 0.0, 0.0, 0.0},
    .ammo = {0, 0, 0, 0},
    .recoil_up = 0.0,
    .recoil_side = 0.0,
    .accuracy = 0.0,
};
inline constexpr WeaponStats PICKAXE_TOOL{
    .tool_id = 0,
    .name = "PICKAXE_TOOL",
    .shoot_interval = 0.4,
    .secondary_shoot_interval = -1.0,
    .reload_time = 0.0,
    .range = 0.0,
    .block_damage = 0.0,
    .block_penetration = 0.0,
    .damage = {9.0, 9.0, 9.0, 9.0, 9.0},
    .ammo = {0, 0, 0, 0},
    .recoil_up = 0.0,
    .recoil_side = 0.0,
    .accuracy = 0.0,
};
inline constexpr WeaponStats PISTOL_TOOL{
    .tool_id = 17,
    .name = "PISTOL_TOOL",
    .shoot_interval = 0.3,
    .secondary_shoot_interval = -1.0,
    .reload_time = 0.5,
    .range = 800.0,
    .block_damage = 3.0,
    .block_penetration = 2.5,
    .damage = {20.0, 50.0, 20.0, 20.0, 20.0},
    .ammo = {6, 6, 30, 30},
    .recoil_up = -0.005,
    .recoil_side = 0.0,
    .accuracy = 0.015,
};
inline constexpr WeaponStats PREFAB_TOOL{
    .tool_id = 23,
    .name = "PREFAB_TOOL",
    .shoot_interval = 0.5,
    .secondary_shoot_interval = 0.5,
    .reload_time = 0.0,
    .range = 0.0,
    .block_damage = 0.0,
    .block_penetration = 0.0,
    .damage = {0.0, 0.0, 0.0, 0.0, 0.0},
    .ammo = {0, 0, 0, 0},
    .recoil_up = 0.0,
    .recoil_side = 0.0,
    .accuracy = 0.0,
};
inline constexpr WeaponStats RADAR_STATION_TOOL{
    .tool_id = 56,
    .name = "RADAR_STATION_TOOL",
    .shoot_interval = 1.5,
    .secondary_shoot_interval = -1.0,
    .reload_time = 0.0,
    .range = 10000.0,
    .block_damage = 1.0,
    .block_penetration = 2.5,
    .damage = {0.0, 0.0, 0.0, 0.0, 0.0},
    .ammo = {1, 1, 0, 0},
    .recoil_up = 0.0,
    .recoil_side = 0.0,
    .accuracy = 0.0,
};
inline constexpr WeaponStats RIFLE_TOOL{
    .tool_id = 6,
    .name = "RIFLE_TOOL",
    .shoot_interval = 0.5,
    .secondary_shoot_interval = -1.0,
    .reload_time = 2.5,
    .range = 10000.0,
    .block_damage = 2.0,
    .block_penetration = 2.5,
    .damage = {70.0, 150.0, 35.0, 35.0, 35.0},
    .ammo = {10, 10, 50, 30},
    .recoil_up = -0.05,
    .recoil_side = 0.0001,
    .accuracy = 0.003,
};
inline constexpr WeaponStats RIOTSHIELD_TOOL{
    .tool_id = 52,
    .name = "RIOTSHIELD_TOOL",
    .shoot_interval = 1.0,
    .secondary_shoot_interval = -1.0,
    .reload_time = 0.0,
    .range = 0.0,
    .block_damage = 0.0,
    .block_penetration = 0.0,
    .damage = {2.0, 2.0, 2.0, 2.0, 2.0},
    .ammo = {0, 0, 0, 0},
    .recoil_up = 0.0,
    .recoil_side = 0.0,
    .accuracy = 0.0,
};
inline constexpr WeaponStats RIOTSTICK_TOOL{
    .tool_id = 49,
    .name = "RIOTSTICK_TOOL",
    .shoot_interval = 0.5,
    .secondary_shoot_interval = -1.0,
    .reload_time = 0.0,
    .range = 0.0,
    .block_damage = 0.0,
    .block_penetration = 0.0,
    .damage = {1.75, 1.75, 1.75, 1.75, 1.75},
    .ammo = {0, 0, 0, 0},
    .recoil_up = 0.0,
    .recoil_side = 0.0,
    .accuracy = 0.0,
};
inline constexpr WeaponStats ROCKET_TURRET_TOOL{
    .tool_id = 16,
    .name = "ROCKET_TURRET_TOOL",
    .shoot_interval = 1.5,
    .secondary_shoot_interval = -1.0,
    .reload_time = 0.0,
    .range = 10000.0,
    .block_damage = 1.0,
    .block_penetration = 2.5,
    .damage = {0.0, 0.0, 0.0, 0.0, 0.0},
    .ammo = {4, 2, 0, 0},
    .recoil_up = 0.0,
    .recoil_side = 0.0,
    .accuracy = 0.0,
};
inline constexpr WeaponStats RPG2_TOOL{
    .tool_id = 13,
    .name = "RPG2_TOOL",
    .shoot_interval = 0.75,
    .secondary_shoot_interval = -1.0,
    .reload_time = 1.0,
    .range = 10000.0,
    .block_damage = 1.0,
    .block_penetration = 2.5,
    .damage = {0.0, 0.0, 0.0, 0.0, 0.0},
    .ammo = {3, 3, 3, 3},
    .recoil_up = -0.05,
    .recoil_side = 0.0,
    .accuracy = 0.02,
};
inline constexpr WeaponStats RPG_TOOL{
    .tool_id = 12,
    .name = "RPG_TOOL",
    .shoot_interval = 0.7,
    .secondary_shoot_interval = -1.0,
    .reload_time = 1.5,
    .range = 10000.0,
    .block_damage = 1.0,
    .block_penetration = 2.5,
    .damage = {0.0, 0.0, 0.0, 0.0, 0.0},
    .ammo = {1, 1, 3, 3},
    .recoil_up = -0.1,
    .recoil_side = 0.0001,
    .accuracy = 0.01,
};
inline constexpr WeaponStats SHOTGUN2_TOOL{
    .tool_id = 10,
    .name = "SHOTGUN2_TOOL",
    .shoot_interval = 1.0,
    .secondary_shoot_interval = -1.0,
    .reload_time = 1.0,
    .range = 20.0,
    .block_damage = 2.5,
    .block_penetration = 2.5,
    .damage = {40.0, 50.0, 50.0, 50.0, 50.0},
    .ammo = {2, 2, 14, 14},
    .recoil_up = -0.25,
    .recoil_side = 0.001,
    .accuracy = 0.05,
};
inline constexpr WeaponStats SHOTGUN_TOOL{
    .tool_id = 9,
    .name = "SHOTGUN_TOOL",
    .shoot_interval = 1.0,
    .secondary_shoot_interval = -1.0,
    .reload_time = 0.5,
    .range = 60.0,
    .block_damage = 1.0,
    .block_penetration = 2.5,
    .damage = {20.0, 30.0, 12.0, 12.0, 12.0},
    .ammo = {5, 5, 20, 20},
    .recoil_up = -0.1,
    .recoil_side = 0.0002,
    .accuracy = 0.04,
};
inline constexpr WeaponStats SMG_TOOL{
    .tool_id = 7,
    .name = "SMG_TOOL",
    .shoot_interval = 0.1,
    .secondary_shoot_interval = -1.0,
    .reload_time = 1.25,
    .range = 250.0,
    .block_damage = 1.0,
    .block_penetration = 2.5,
    .damage = {10.0, 15.0, 10.0, 10.0, 10.0},
    .ammo = {25, 25, 100, 100},
    .recoil_up = -0.01,
    .recoil_side = 0.00002,
    .accuracy = 0.02,
};
inline constexpr WeaponStats SNIPER2_TOOL{
    .tool_id = 19,
    .name = "SNIPER2_TOOL",
    .shoot_interval = 1.1,
    .secondary_shoot_interval = -1.0,
    .reload_time = 3.0,
    .range = 10000.0,
    .block_damage = 3.0,
    .block_penetration = 2.5,
    .damage = {34.0, 85.0, 34.0, 34.0, 34.0},
    .ammo = {5, 5, 15, 15},
    .recoil_up = -0.03,
    .recoil_side = 0.0,
    .accuracy = 0.025,
};
inline constexpr WeaponStats SNIPER_TOOL{
    .tool_id = 18,
    .name = "SNIPER_TOOL",
    .shoot_interval = 1.0,
    .secondary_shoot_interval = -1.0,
    .reload_time = 2.0,
    .range = 10000.0,
    .block_damage = 5.0,
    .block_penetration = 2.5,
    .damage = {50.0, 175.0, 50.0, 50.0, 50.0},
    .ammo = {1, 1, 7, 7},
    .recoil_up = -0.06,
    .recoil_side = 0.0,
    .accuracy = 0.025,
};
inline constexpr WeaponStats SNOWBLOWER_TOOL{
    .tool_id = 29,
    .name = "SNOWBLOWER_TOOL",
    .shoot_interval = 0.2,
    .secondary_shoot_interval = -1.0,
    .reload_time = 3.0,
    .range = 10000.0,
    .block_damage = 1.0,
    .block_penetration = 2.5,
    .damage = {0.0, 0.0, 0.0, 0.0, 0.0},
    .ammo = {0, 0, 0, 0},
    .recoil_up = 0.0,
    .recoil_side = 0.0,
    .accuracy = 0.02,
};
inline constexpr WeaponStats SNUB_PISTOL_TOOL{
    .tool_id = 36,
    .name = "SNUB_PISTOL_TOOL",
    .shoot_interval = 0.5,
    .secondary_shoot_interval = -1.0,
    .reload_time = 0.75,
    .range = 500.0,
    .block_damage = 1.0,
    .block_penetration = 2.5,
    .damage = {40.0, 70.0, 30.0, 30.0, 30.0},
    .ammo = {6, 6, 30, 30},
    .recoil_up = -0.05,
    .recoil_side = 0.0,
    .accuracy = 0.01,
};
inline constexpr WeaponStats SPADE_TOOL{
    .tool_id = 2,
    .name = "SPADE_TOOL",
    .shoot_interval = 0.4,
    .secondary_shoot_interval = 1.0,
    .reload_time = 0.0,
    .range = 0.0,
    .block_damage = 0.0,
    .block_penetration = 0.0,
    .damage = {5.0, 5.0, 5.0, 5.0, 5.0},
    .ammo = {0, 0, 0, 0},
    .recoil_up = 0.0,
    .recoil_side = 0.0,
    .accuracy = 0.0,
};
inline constexpr WeaponStats STICKY_GRENADE_TOOL{
    .tool_id = 57,
    .name = "STICKY_GRENADE_TOOL",
    .shoot_interval = 1.0,
    .secondary_shoot_interval = -1.0,
    .reload_time = 0.0,
    .range = 10000.0,
    .block_damage = 1.0,
    .block_penetration = 2.5,
    .damage = {0.0, 0.0, 0.0, 0.0, 0.0},
    .ammo = {4, 2, 0, 0},
    .recoil_up = 0.0,
    .recoil_side = 0.0,
    .accuracy = 0.0,
};
inline constexpr WeaponStats SUPERSPADE_TOOL{
    .tool_id = 3,
    .name = "SUPERSPADE_TOOL",
    .shoot_interval = 0.6,
    .secondary_shoot_interval = -1.0,
    .reload_time = 0.0,
    .range = 0.0,
    .block_damage = 0.0,
    .block_penetration = 0.0,
    .damage = {7.5, 7.5, 7.5, 7.5, 7.5},
    .ammo = {0, 0, 0, 0},
    .recoil_up = 0.0,
    .recoil_side = 0.0,
    .accuracy = 0.0,
};
inline constexpr WeaponStats TOMMYGUN_TOOL{
    .tool_id = 35,
    .name = "TOMMYGUN_TOOL",
    .shoot_interval = 0.12,
    .secondary_shoot_interval = -1.0,
    .reload_time = 2.0,
    .range = 500.0,
    .block_damage = 1.0,
    .block_penetration = 2.5,
    .damage = {30.0, 35.0, 30.0, 30.0, 30.0},
    .ammo = {30, 30, 120, 120},
    .recoil_up = -0.01,
    .recoil_side = 0.0,
    .accuracy = 0.01,
};
inline constexpr WeaponStats UGC_DRILLGUN_TOOL{
    .tool_id = 47,
    .name = "UGC_DRILLGUN_TOOL",
    .shoot_interval = 0.2,
    .secondary_shoot_interval = -1.0,
    .reload_time = 4.0,
    .range = 10000.0,
    .block_damage = 1.0,
    .block_penetration = 2.5,
    .damage = {0.0, 0.0, 0.0, 0.0, 0.0},
    .ammo = {1, 1, 3, 1},
    .recoil_up = -0.00001,
    .recoil_side = 0.0,
    .accuracy = 0.04,
};
inline constexpr WeaponStats UGC_PICKAXE_TOOL{
    .tool_id = 44,
    .name = "UGC_PICKAXE_TOOL",
    .shoot_interval = 0.2,
    .secondary_shoot_interval = -1.0,
    .reload_time = 0.0,
    .range = 0.0,
    .block_damage = 0.0,
    .block_penetration = 0.0,
    .damage = {9.0, 9.0, 9.0, 9.0, 9.0},
    .ammo = {0, 0, 0, 0},
    .recoil_up = 0.0,
    .recoil_side = 0.0,
    .accuracy = 0.0,
};
inline constexpr WeaponStats UGC_PREFAB_TOOL{
    .tool_id = 42,
    .name = "UGC_PREFAB_TOOL",
    .shoot_interval = 0.1,
    .secondary_shoot_interval = 0.5,
    .reload_time = 0.0,
    .range = 0.0,
    .block_damage = 0.0,
    .block_penetration = 0.0,
    .damage = {0.0, 0.0, 0.0, 0.0, 0.0},
    .ammo = {0, 0, 0, 0},
    .recoil_up = 0.0,
    .recoil_side = 0.0,
    .accuracy = 0.0,
};
inline constexpr WeaponStats UGC_RPG2_TOOL{
    .tool_id = 46,
    .name = "UGC_RPG2_TOOL",
    .shoot_interval = 0.5,
    .secondary_shoot_interval = -1.0,
    .reload_time = 1.0,
    .range = 10000.0,
    .block_damage = 1.0,
    .block_penetration = 2.5,
    .damage = {0.0, 0.0, 0.0, 0.0, 0.0},
    .ammo = {1, 1, 1, 3},
    .recoil_up = 0.0,
    .recoil_side = 0.0,
    .accuracy = 0.02,
};
inline constexpr WeaponStats UGC_SNOWBLOWER_TOOL{
    .tool_id = 48,
    .name = "UGC_SNOWBLOWER_TOOL",
    .shoot_interval = 0.2,
    .secondary_shoot_interval = -1.0,
    .reload_time = 3.0,
    .range = 10000.0,
    .block_damage = 1.0,
    .block_penetration = 2.5,
    .damage = {0.0, 0.0, 0.0, 0.0, 0.0},
    .ammo = {0, 0, 0, 0},
    .recoil_up = 0.0,
    .recoil_side = 0.0,
    .accuracy = 0.02,
};
inline constexpr WeaponStats UGC_SUPERSPADE_TOOL{
    .tool_id = 45,
    .name = "UGC_SUPERSPADE_TOOL",
    .shoot_interval = 0.2,
    .secondary_shoot_interval = 0.2,
    .reload_time = 0.0,
    .range = 0.0,
    .block_damage = 0.0,
    .block_penetration = 0.0,
    .damage = {7.5, 7.5, 7.5, 7.5, 7.5},
    .ammo = {0, 0, 0, 0},
    .recoil_up = 0.0,
    .recoil_side = 0.0,
    .accuracy = 0.0,
};
inline constexpr WeaponStats UGC_TOOL{
    .tool_id = 41,
    .name = "UGC_TOOL",
    .shoot_interval = 0.5,
    .secondary_shoot_interval = 0.5,
    .reload_time = 0.0,
    .range = 0.0,
    .block_damage = 0.0,
    .block_penetration = 0.0,
    .damage = {0.0, 0.0, 0.0, 0.0, 0.0},
    .ammo = {0, 0, 0, 0},
    .recoil_up = 0.0,
    .recoil_side = 0.0,
    .accuracy = 0.0,
};
inline constexpr WeaponStats ZOMBIEHAND_TOOL{
    .tool_id = 24,
    .name = "ZOMBIEHAND_TOOL",
    .shoot_interval = 0.4,
    .secondary_shoot_interval = -1.0,
    .reload_time = 0.0,
    .range = 0.0,
    .block_damage = 0.0,
    .block_penetration = 0.0,
    .damage = {2.0, 2.0, 2.0, 2.0, 2.0},
    .ammo = {0, 0, 0, 0},
    .recoil_up = 0.0,
    .recoil_side = 0.0,
    .accuracy = 0.0,
};
inline constexpr WeaponStats ZOMBIE_PREFAB_TOOL{
    .tool_id = 28,
    .name = "ZOMBIE_PREFAB_TOOL",
    .shoot_interval = 0.5,
    .secondary_shoot_interval = 0.5,
    .reload_time = 0.0,
    .range = 0.0,
    .block_damage = 0.0,
    .block_penetration = 0.0,
    .damage = {0.0, 0.0, 0.0, 0.0, 0.0},
    .ammo = {0, 0, 0, 0},
    .recoil_up = 0.0,
    .recoil_side = 0.0,
    .accuracy = 0.0,
};

inline constexpr std::array<WeaponStats, 64> ALL_WEAPONS{
    ANTIPERSONNEL_GRENADE_TOOL,
    ASSAULT_RIFLE_TOOL,
    AUTOMATIC_PISTOL_TOOL,
    AUTO_SHOTGUN_TOOL,
    BLOCK_SUCKER_TOOL,
    BLOCK_TOOL,
    BOMB_TOOL,
    C4_TOOL,
    CHEMICALBOMB_TOOL,
    CLASSIC_GRENADE_TOOL,
    CLASSIC_SHOTGUN_TOOL,
    CLASSIC_SMG_TOOL,
    CLASSIC_SPADE_TOOL,
    CROWBAR_TOOL,
    DIAMOND_TOOL,
    DISGUISE_TOOL,
    DRILLGUN_TOOL,
    DYNAMITE_TOOL,
    FAKE_PISTOL_TOOL,
    FLAREBLOCK_TOOL,
    GRENADE_LAUNCHER_WEAPON_TOOL,
    GRENADE_TOOL,
    INTEL_TOOL,
    KNIFE_TOOL,
    LANDMINE_TOOL,
    LIGHT_MACHINE_GUN_TOOL,
    MACHETE_TOOL,
    MEDPACK_TOOL,
    MG_TOOL,
    MINE_LAUNCHER_TOOL,
    MINIGUN_TOOL,
    MOLOTOV_TOOL,
    NULL_TOOL,
    PAINTBRUSH_TOOL,
    PICKAXE_TOOL,
    PISTOL_TOOL,
    PREFAB_TOOL,
    RADAR_STATION_TOOL,
    RIFLE_TOOL,
    RIOTSHIELD_TOOL,
    RIOTSTICK_TOOL,
    ROCKET_TURRET_TOOL,
    RPG2_TOOL,
    RPG_TOOL,
    SHOTGUN2_TOOL,
    SHOTGUN_TOOL,
    SMG_TOOL,
    SNIPER2_TOOL,
    SNIPER_TOOL,
    SNOWBLOWER_TOOL,
    SNUB_PISTOL_TOOL,
    SPADE_TOOL,
    STICKY_GRENADE_TOOL,
    SUPERSPADE_TOOL,
    TOMMYGUN_TOOL,
    UGC_DRILLGUN_TOOL,
    UGC_PICKAXE_TOOL,
    UGC_PREFAB_TOOL,
    UGC_RPG2_TOOL,
    UGC_SNOWBLOWER_TOOL,
    UGC_SUPERSPADE_TOOL,
    UGC_TOOL,
    ZOMBIEHAND_TOOL,
    ZOMBIE_PREFAB_TOOL,
};

} // namespace weapons

} // namespace battlespades::retail

// clang-format on
// NOLINTEND
