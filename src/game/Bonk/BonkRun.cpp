#include "BonkRun.h"
#include "BonkCoop.h"
#include "Group.h"
#include "BonkCodex.h"
#include "BonkLocale.h"
#include "BonkMobAI.h"
#include "BonkPlayer.h"
#include "BonkQuests.h"
#include "BonkRunStats.h"
#include "CellImpl.h"
#include "Chat.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "DBCStores.h"
#include "GridMap.h"
#include "DynamicObject.h"
#include "GameObject.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Map.h"
#include "MoveMap.h"
#include "MoveMapSharedDefines.h"
#include "MoveSpline.h"
#include "NullCreatureAI.h"
#include "ObjectMgr.h"
#include "Opcodes.h"
#include "PathFinder.h"
#include "Player.h"
#include "Spell.h"
#include "SpellAuras.h"
#include "SpellMgr.h"
#include "TemporarySummon.h"
#include "World.h"
#include "Utilities/Random.h"

#include <algorithm>
#include <iterator>
#include <cctype>
#include <cmath>
#include <limits>
#include <sstream>

namespace
{
    uint32 const BONK_ENTER_TIMEOUT_MS     = 30000;
    uint32 const BONK_END_DELAY_MS         = 4000;
    uint32 const BONK_STATE_INTERVAL_MS    = 1000;
    uint32 const BONK_BOUNDARY_INTERVAL_MS = 500;
    uint32 const BONK_FIRST_WAVE_MS        = 1500;
    float const BONK_BOSS_FIGHT_LINE_HEALTH_PCT = 50.0f;
    uint32 const BONK_ENEMY_FACTION        = 14;
    uint32 const BONK_SUMMON_LIMIT         = 1000;
    uint32 const BONK_OBJECT_LIFETIME_SEC  = 3600;
    uint32 const BONK_MARKER_LOOK_CHECK_MS = 250;
    float const BONK_DROP_SIGHT            = VISIBILITY_DISTANCE_GIGANTIC;

    float const BONK_SPAWN_MAX_HEIGHT_DIFF = 20.0f;
    float const BONK_SPAWN_MAX_DETOUR      = 2.5f;
    float const BONK_SPAWN_SNAP_DISTANCE   = 3.0f;

    uint32 const BONK_NATIVE_SWEEP_MS      = 500;
    float const BONK_NATIVE_SWEEP_RANGE    = 90.0f;
    uint32 const BONK_NATIVE_RESPAWN_SEC   = 30 * MINUTE;

    float const BONK_CHEST_MIN_DISTANCE    = 35.0f;
    float const BONK_CHEST_MAX_DISTANCE    = 70.0f;
    float const BONK_CHEST_MIN_SPACING     = 40.0f;
    uint32 const BONK_CHEST_PLACE_ATTEMPTS = 10;
    float const BONK_PORTAL_DISTANCE       = 6.0f;
    float const BONK_PORTAL_MAX_DETOUR     = 1.5f;
    float const BONK_PORTAL_DETOUR_SLACK   = 10.0f;
    float const BONK_POWERUP_MIN_DISTANCE  = 20.0f;
    float const BONK_POWERUP_MAX_DISTANCE  = 50.0f;
    float const BONK_POWERUP_PICKUP_RANGE  = 3.0f;
    uint32 const BONK_POWERUP_LIFETIME_SEC = 90;
    float const BONK_POWERUP_COMMAND_DISTANCE = 10.0f;
    uint32 const BONK_POWERUP_CHECK_MS     = 250;
    uint32 const BONK_MIN_SHOWN_COOLDOWN_GAIN_MS = 50;
    uint32 const BONK_DOT_RETARGET_MS      = 4000;
    float const BONK_LEECH_HEAL            = 1.0f;
    float const BONK_LEECH_CAP_PER_CHANCE  = 0.03f;
    float const BONK_SHARD_PICKUP_RANGE    = 3.0f;
    float const BONK_SHARD_SCATTER         = 2.0f;
    uint32 const BONK_SHARD_CHECK_MS       = 250;
    uint32 const BONK_SPAWN_TICK_MS        = 1000;
    uint32 const BONK_SPAWN_PER_TICK       = 12;
    uint32 const BONK_OPENING_REFILL_MIN   = 6;
    float const BONK_STAGE_CARRY_SHARE     = 0.65f;
    float const BONK_STAGE_CALM_SHARE      = 0.5f;
    float const BONK_STAGE_CROWD_GROWTH    = 0.15f;
    float const BONK_KILL_SHOT_PERCENT     = 100000.0f;
    uint32 const BONK_STAGE_CROWD_GROWTH_STAGES = 2;
    float const BONK_VETERAN_HEALTH        = 2.5f;
    float const BONK_MAX_MOB_HEALTH        = 2000000000.0f;
    uint32 const BONK_MOB_HEALTH_TABLE_LEVELS = 10;
    float const BONK_MOB_HEALTH_LEVEL_GROWTH  = 0.1f;
    float const BONK_GEAR_POWER_REFERENCE  = 100.0f;
    float const BONK_ARMOR_BASE            = 100.0f;
    float const BONK_ARMOR_SLOPE           = 4.0f / 3.0f;
    float const BONK_ARMOR_MAX_REDUCTION   = 0.6f;
    float const BONK_ARMOR_CAP_POINT       = BONK_ARMOR_MAX_REDUCTION * BONK_ARMOR_BASE / (1.0f - BONK_ARMOR_MAX_REDUCTION * BONK_ARMOR_SLOPE);
    float const BONK_ARMOR_OVERFLOW_HEALTH = 0.5f;
    float const BONK_VETERAN_DAMAGE        = 1.25f;
    float const BONK_VETERAN_REWARD        = 3.5f;
    float const BONK_VETERAN_SIZE          = 1.2f;
    uint32 const BONK_STRESS_SPAWN_PER_TICK = 40;
    float const BONK_BOSS_CROWD            = 0.5f;
    float const BONK_COOP_SPAWN_PER_PLAYER = 0.15f;
    float const BONK_COOP_HEALTH_PER_PLAYER = 0.15f;
    float const BONK_COOP_BOSS_HEALTH_PER_PLAYER = 0.75f;
    uint32 const BONK_MOB_SWING_MS         = 2000;
    uint32 const BONK_BEAT_WARN_SECONDS    = 10;
    uint32 const BONK_OVERTIME_AFTER_SEC   = 90;
    uint32 const BONK_OVERTIME_INTERVAL_MS = 15000;
    uint32 const BONK_OVERTIME_RING        = 12;
    uint32 const BONK_OVERTIME_RING_STEP   = 4;
    float const BONK_OVERTIME_DAMAGE_STEP  = 0.25f;
    uint32 const BONK_QUIET_SECONDS        = 30;
    uint32 const BONK_ELITE_RETRY_MS       = 5000;
    float const BONK_BRUISER_SIZE          = 1.3f;
    float const BONK_BRUISER_MAX_GROWTH    = 2.5f;
    float const BONK_BOSS_MIN_RADIUS       = 0.6f;
    float const BONK_MINIBOSS_MIN_RADIUS   = 0.48f;
    float const BONK_BOSS_MAX_GROWTH       = 1.5f;
    uint32 const BONK_MAX_PLATE_ID         = 65535;
    float const BONK_RING_DISTANCE         = 22.0f;
    float const BONK_RUSH_WIDTH            = 0.6f;

    uint32 const BONK_HEADING_SAMPLE_MS    = 250;
    float const BONK_HEADING_SMOOTH_SEC    = 1.5f;
    float const BONK_HEADING_MIN_SPEED     = 2.5f;
    float const BONK_HEADING_MAX_STEP      = 20.0f;
    float const BONK_FRONT_ARC             = M_PI_F / 3.0f;
    float const BONK_FRONT_RANGE           = 95.0f;
    float const BONK_FRONT_SHARE           = 0.45f;
    uint32 const BONK_FRONT_MIN            = 5;
    float const BONK_FAR_MIN_DISTANCE      = 62.0f;
    float const BONK_FAR_MAX_DISTANCE      = 78.0f;
    uint32 const BONK_FAR_TRIES            = 6;
    uint32 const BONK_STRAGGLER_TICK_MS    = 500;
    float const BONK_STRAY_DISTANCE        = 68.0f;
    uint32 const BONK_STRAY_MS             = 3000;
    uint32 const BONK_PACK_MIN             = 3;
    uint32 const BONK_PACK_MAX             = 5;
    float const BONK_PACK_RADIUS           = 3.0f;
    float const BONK_PACK_SPACING          = 21.0f;
    float const BONK_PACK_CLEARANCE        = 8.0f;
    uint32 const BONK_PACK_SPOT_MS         = 4000;
    uint32 const BONK_PACK_WAIT_MS         = 3000;
    uint32 const BONK_STRAGGLER_SIDE_MS    = 5000;
    float const BONK_SIDE_MIN_ANGLE        = M_PI_F / 3.0f;
    uint32 const BONK_SPAWN_SECTORS        = 12;
    float const BONK_SECTOR_RANGE          = 80.0f;
    uint32 const BONK_SECTOR_BLOCK_MS      = 6000;
    uint32 const BONK_SECTOR_TRIES         = 4;
    uint32 const BONK_SECTOR_ANGLE_TRIES   = 3;
    uint32 const BONK_LANE_MIN_GAP         = 4;
    uint32 const BONK_LANE_SHIFT_MS        = 8000;
    float const BONK_LANE_EDGE_WEIGHT      = 0.4f;
    float const BONK_FORWARD_WEIGHT        = 1.6f;
    float const BONK_BEHIND_WEIGHT         = 0.6f;
    float const BONK_GAP_RANGE             = 25.0f;
    float const BONK_RING_RANGE            = 15.0f;
    uint32 const BONK_RING_SAMPLE_MS       = 1000;
    uint32 const BONK_SPAWN_SINGLE_CHANCE  = 35;
    uint32 const BONK_SPAWN_PAIR_CHANCE    = 30;
    uint32 const BONK_SPAWN_GROUP_MIN      = 3;
    uint32 const BONK_SPAWN_GROUP_MAX      = 5;
    float const BONK_SPAWN_GROUP_MIN_GAP   = 2.0f;
    float const BONK_SPAWN_GROUP_RADIUS    = 7.0f;
    uint32 const BONK_EMERGE_HIDDEN_MS     = 2000;
    uint32 const BONK_EMERGE_MEMBER_STAGGER_MS = 600;
    uint32 const BONK_EMERGE_SETTLE_MS     = 400;
    uint32 const BONK_EMERGE_FACTION       = 35;
    uint32 const BONK_SPELL_BOSS_ADD_SMOKE = 10389;
    uint32 s_spawnKit                      = 92;
    uint32 s_spawnLookLeadMs               = 400;
    uint32 const BONK_SPAWN_LOOK_HOLD_MS   = 1600;
    float const BONK_SPAWN_LOOK_PREVIEW_DISTANCE = 8.0f;
    uint32 const BONK_PERCH_CHECK_MS       = 1000;
    uint32 const BONK_PERCH_CONFIRM_CHECKS = 2;
    uint32 const BONK_PERCH_CLEAR_CHECKS   = 3;
    float const BONK_PERCH_SENSE_RANGE     = 40.0f;
    uint32 const BONK_PERCH_PROBES         = 3;
    float const BONK_PERCH_REACH           = 3.0f;
    float const BONK_PERCH_RING_DISTANCE   = 8.0f;
    float const BONK_PERCH_STRANDED_RANGE  = 5.0f;
    uint32 const BONK_PERCH_STRANDED_MOBS  = 2;
    uint32 const BONK_PERCH_UNSTRUCK_MS    = 3000;
    uint32 const BONK_PERCH_RING_POINTS    = 6;
    uint32 const BONK_FLYER_FIRST_MS       = 1000;
    uint32 const BONK_FLYER_INTERVAL_MS    = 3000;
    uint32 const BONK_FLYER_PACK_MIN       = 2;
    uint32 const BONK_FLYER_PACK_MAX       = 8;
    uint32 const BONK_FLYER_RAMP_MS        = 6000;
    uint32 const BONK_FLYER_MAX_ALIVE      = 20;
    float const BONK_FLYER_MIN_DISTANCE    = 22.0f;
    float const BONK_FLYER_MAX_DISTANCE    = 32.0f;
    float const BONK_FLYER_HEIGHT          = 7.0f;
    uint32 const BONK_FLYER_TRIES          = 8;
    float const BONK_FLYER_PACK_RADIUS     = 3.0f;
    float const BONK_SIDE_MAX_ANGLE        = 2.0f * M_PI_F / 3.0f;

    float AngleBetween(float a, float b)
    {
        return std::fabs(std::remainder(a - b, 2.0f * M_PI_F));
    }

    size_t const BONK_MAX_ORBS             = 100;
    uint32 const BONK_ORB_CHECK_MS         = 100;
    float const BONK_ORB_PICKUP_RANGE      = 1.5f;
    float const BONK_ORB_MAGNET_RANGE      = 6.0f;
    float const BONK_MAX_MAGNET_RANGE      = 30.0f;
    float const BONK_ORB_FLY_SPEED         = 28.0f;
    float const BONK_ORB_REAIM_DISTANCE    = 1.0f;
    uint32 const BONK_ORB_FACTION          = 35;
    uint32 const BONK_ORB_LIFETIME_MS      = 30 * MINUTE * IN_MILLISECONDS;
    size_t const BONK_PERF_SECONDS_KEPT    = 10;
    uint32 const BONK_PERF_DIP_GAP_MS      = 5 * IN_MILLISECONDS;
    uint32 const BONK_ORB_MERGE_COUNT      = 5;
    float const BONK_ORB_MERGE_RANGE       = 8.0f;
    uint32 const BONK_ORB_MERGE_CHECK_MS   = 1000;
    uint32 const BONK_ORB_MERGE_MIN_AGE_MS = 1000;
    uint32 const BONK_ORB_MERGE_SLIDE_MS   = 400;
    float const BONK_ORB_MERGE_SLIDE_SPEED = 12.0f;
    float const BONK_ORB_LIFT              = 0.5f;
    float const BONK_ORB_CHEST_SHARE       = 0.6f;
    float const BONK_ORB_GROUND_SEARCH     = 6.0f;
    float const BONK_ORB_RISE_SPEED        = 2.0f;

    size_t const BONK_START_SPELL_SLOTS   = 3;
    size_t const BONK_MAX_SPELL_SLOTS     = 5;
    size_t const BONK_BENCH_SPELL_SLOTS   = 4;
    size_t const BONK_START_PASSIVE_SLOTS = 3;
    size_t const BONK_MAX_PASSIVE_SLOTS   = 6;
    float const BONK_DUNGEON_HEALTH = 1.3f;
    float const BONK_DUNGEON_DAMAGE = 1.2f;
    float const BONK_DUNGEON_GOLD   = 1.5f;
    float const BONK_FINALE_GOLD_PER_MINUTE  = 0.5f;
    float const BONK_FINALE_SCRAP_PER_MINUTE = 2.0f;
    float const BONK_INVASION_GOLD  = 1.25f;
    float const BONK_INVASION_CROWD = 1.15f;

    float const BONK_VAULT_SIDE     = 4.0f;

    size_t const BONK_MAX_BOLTS     = 12;

    float const BONK_PHASE_ADD_MIN  = 8.0f;
    float const BONK_PHASE_ADD_MAX  = 12.0f;
    uint32 const BONK_WARD_ENTRY    = 3560;
    float const BONK_WARD_SCALE     = 1.6f;
    float const BONK_WARD_MIN_DISTANCE = 10.0f;
    float const BONK_WARD_MAX_DISTANCE = 14.0f;

    uint32 const BONK_POWERUP_RESTORATION = 900004;

    uint32 const BONK_KIT_HOLY_NOVA  = 3154;
    uint32 const BONK_KIT_FROST_NOVA = 160;

    size_t const BONK_START_MIN_LEARNED = 4;
    size_t const BONK_START_MAX_LEARNED = 15;
    float const BONK_START_EDGE         = 60.0f;
    float const BONK_START_EDGE_ENCLOSED = 25.0f;
    float const BONK_START_SPREAD       = 80.0f;
    float const BONK_COOP_FOLLOW_MIN    = 3.0f;
    float const BONK_COOP_FOLLOW_MAX    = 7.0f;
    uint32 const BONK_ZONE_START_TRIES  = 40;
    float const BONK_ZONE_START_MARGIN  = 6.0f;
    float const BONK_ZONE_START_FLAT    = 12.0f;
    float const BONK_ZONE_START_CLEAR   = 1.5f;
    int32 const BONK_ZONE_START_SPAWN_CELLS = 2;
    size_t const BONK_ZONE_START_SPAWNS = 3;
int const BONK_START_PATH_POLYS     = 2048;
int const BONK_START_PATH_NODES     = 65535;
float const BONK_START_POLY_REACH   = 10.0f;
    float const BONK_CROWD_WAIT_RANGE   = 30.0f;
    float const BONK_CROWD_TOUCH_SLACK  = 1.0f;
    float const BONK_CROWD_MAX_BODY     = 3.0f;
    size_t const BONK_OFFER_SIZE   = 3;
    uint32 const BONK_RUN_XP_SEND_MS = 200;
    uint32 const BONK_PAUSE_AURA_MS  = 200;
    int32 const BONK_OVERCAP_XP_START_LEVEL = 60;
    uint32 const BONK_SPELL_FROST_TRAP_AURA = 13810;
    uint32 const BONK_KIT_FROST_TRAP_SOUND = 7503;
    float const BONK_OVERCAP_XP_DOUBLING_LEVELS = 60.0f;
    BonkRarity const BONK_LEVEL_MIN_RARITY = BONK_RARITY_UNCOMMON;
    BonkRarity const BONK_CHEST_MIN_RARITY = BONK_RARITY_RARE;
    float const BONK_SKIP_COINS = 0.25f;
    size_t const BONK_MAX_OFFER_SIZE = 6;
    float const BONK_MAX_HAGGLE = 50.0f;

    float const BONK_ECHO_AREA             = 1.5f;
    float const BONK_FREEZE_ARC            = 2.0f * M_PI_F / 3.0f;
    float const BONK_FROZEN_CRIT_BONUS     = 0.5f;
    uint32 const BONK_SPELL_WEAPON_ORB     = 22576;
    float const BONK_WEAPON_ORB_SCALE      = 1.2f;
    uint32 const BONK_DISPLAY_SPELL_ORB    = 17998;
    float const BONK_WEAPON_ORB_SPEED      = 0.35f;
    float const BONK_PLAYER_RUN_SPEED      = 7.0f;
    float const BONK_PROJECTILE_HEIGHT     = 1.2f;
    float const BONK_ENEMY_MISSILE_HEIGHT  = 1.5f;
    float const BONK_ENEMY_MISSILE_AIM     = 1.0f;
    uint32 const BONK_ENEMY_MISSILE_MIN_MS = 100;
    float const BONK_PROJECTILE_FAN        = 0.17f;
    float const BONK_PROJECTILE_Z_REACH    = 4.0f;
    uint32 const BONK_HERO_MAX_AGE_MS      = 1000;
    float const BONK_CONE_ARC              = M_PI_F / 2.0f;
    float const BONK_KNOCKBACK_DISTANCE    = 6.0f;
    float const BONK_KNOCKBACK_SPEED       = 18.0f;
    uint32 const BONK_KNOCKBACK_SETTLE_MS  = 300;
    uint32 const BONK_BURST_INTERVAL_MS    = 150;
    uint32 const BONK_COUNT_RECAST_MS      = 300;
    float const BONK_MAX_DEMORALIZE        = 60.0f;
    float const BONK_RICOCHET_RANGE        = 10.0f;
    float const BONK_RICOCHET_GROWTH       = 1.25f;
    int32 const BONK_RICOCHET_LEAD_MS      = 50;
    float const BONK_TRICK_SHOT_RANGE      = 15.0f;
    int32 const BONK_TRICK_SHOT_DELAY_MS   = 100;
    size_t const BONK_MAX_TRICK_SHOTS      = 12;
    size_t const BONK_SHOT_SOURCES         = 12;
    uint32 const BONK_SHOT_SOURCE_CHECK_MS = 1000;
    float const BONK_SHOT_SOURCE_GATHER    = 50.0f;
    size_t const BONK_SHOT_SOURCES_HELD_MAX = BONK_SHOT_SOURCES / 2;
    uint32 const BONK_SHOT_SOURCE_LIFETIME_MS = 60 * MINUTE * IN_MILLISECONDS;
    uint32 const BONK_SHOT_SOURCE_BOW      = 2504;
    uint32 const BONK_RATE_WINDOW_MS       = 1000;
    int32 const BONK_DETONATION_FIRST_MS   = 200;
    int32 const BONK_DETONATION_GAP_MS     = 100;
    size_t const BONK_MAX_DETONATIONS      = 20;
    uint32 const BONK_KIT_DETONATION       = 286;
    float const BONK_STEADY_RATE           = 2.0f;
    uint32 const BONK_STEADY_STEP_MS       = 1000;
    uint32 const BONK_STEADY_FIRST_PULSE_MS = 250;
    float const BONK_STEADY_DRIFT          = 0.1f;
    uint32 const BONK_KIT_STEADY_AIM_STEP  = 3029;
    size_t const BONK_MAX_ARROW_STORMS     = 12;
    int32 const BONK_STORM_AIM_MS          = 750;
    float const BONK_STORM_SEEK_RANGE      = 25.0f;
    size_t const BONK_STORM_SEEK_MOBS      = 40;
    int32 const BONK_STORM_RAIN_MS         = 500;
    int32 const BONK_STORM_RAIN_LIFE_MS    = 900;
    float const BONK_GRAVITY_PILE_RADIUS   = 1.5f;
    float const BONK_GRAVITY_PULL_SPEED    = 30.0f;
    uint32 const BONK_GRAVITY_SETTLE_MS    = 300;
    int32 const BONK_GRAVITY_RAIN_DELAY_MS = 400;
    uint32 const BONK_KIT_GRAVITY_PULL     = 1364;
    size_t const BONK_MAX_FLARES           = 12;
    size_t const BONK_MAX_FLARE_SPLITS     = 10;
    size_t const BONK_MAX_SHARD_BURSTS     = 10;
    char const* const BONK_BOSS_PIN_ICON     = "INV_Misc_Bone_HumanSkull_01";
    char const* const BONK_MINIBOSS_PIN_ICON = "INV_Misc_Bone_ElfSkull_01";
    int32 const BONK_SHARD_BURST_DELAY_MS  = 100;
    uint32 const BONK_KIT_SHARD_BURST      = 988;
    float const BONK_VENOM_BOLT_RANGE      = 12.0f;
    uint32 const BONK_VENOM_BOLT_STACKS    = 2;
    int32 const BONK_VENOM_BOLT_DELAY_MS   = 100;
    size_t const BONK_MAX_VENOM_VOLLEYS    = 10;
    uint32 const BONK_VENOM_CLOUD_MS       = 2000;
    int32 const BONK_VENOM_CLOUD_PULSE_MS  = 500;
    uint32 const BONK_VENOM_CLOUD_STACKS   = 1;
    size_t const BONK_MAX_VENOM_CLOUDS     = 16;
    size_t const BONK_MAX_NEW_VENOM_CLOUDS = 8;
    uint32 const BONK_SPELL_VENOM_CLOUD_LOOK = 21070;
    uint32 const BONK_KIT_VENOM_BURST      = 6377;
    uint32 const BONK_CLUSTER_BOMBLETS     = 5;
    float const BONK_BOMBLET_MIN_TOSS      = 3.0f;
    float const BONK_BOMBLET_MAX_TOSS      = 6.0f;
    int32 const BONK_BOMBLET_FIRST_MS      = 300;
    int32 const BONK_BOMBLET_GAP_MS        = 100;
    float const BONK_BOMBLET_SHARE         = 0.4f;
    float const BONK_BOMBLET_RADIUS        = 3.0f;
    uint32 const BONK_SPELL_BOMBLET_TOSS   = 4066;
    uint32 const BONK_KIT_BOMBLET          = 5009;
    uint32 const BONK_SNAKES_PER_TRAP      = 6;
    size_t const BONK_MAX_SNAKES           = 18;
    float const BONK_SNAKE_SPREAD          = 1.5f;
    uint32 const BONK_SNAKE_ATTACK_MS      = 1500;
    float const BONK_SNAKE_SCALE           = 1.5f;
    uint32 const BONK_SPELL_PLAGUE_LOOK    = 19502;
    uint32 const BONK_KIT_PLAGUE_RISE      = 219;
    int32 const BONK_PLAGUE_RISE_DELAY_MS  = 500;
    int32 const BONK_PLAGUE_PULSE_MS       = 1000;
    uint32 const BONK_PLAGUE_CLAW_STACKS   = 2;
    uint32 const BONK_STAMPEDE_MS          = 20000;
    int32 const BONK_STAMPEDE_FIRST_MS     = 2000;
    int32 const BONK_STAMPEDE_RETRY_MS     = 500;
    uint32 const BONK_STAMPEDE_BEASTS      = 3;
    float const BONK_STAMPEDE_LANE         = 2.5f;
    float const BONK_STAMPEDE_LEAD         = 12.0f;
    float const BONK_STAMPEDE_PAST         = 14.0f;
    float const BONK_STAMPEDE_SPEED        = 28.0f;
    uint32 const BONK_STAMPEDE_LIFE_MS     = 5000;
    size_t const BONK_MAX_SPECTRAL_BEASTS  = 6;
    float const BONK_SPECTRAL_SCALE        = 1.0f;
    size_t const BONK_GROUP_SEEK_MOBS      = 40;
    float const BONK_CHARGE_REACH          = 2.0f;
    int32 const BONK_CHARGE_DELAY_MS       = 300;
    int32 const BONK_CHARGE_GRACE_MS       = 500;
    uint32 const BONK_KIT_CHARGE_TRAIL     = 44;
    uint32 const BONK_HOWL_BEAST_MS        = 8000;
    size_t const BONK_MAX_HOWL_BEASTS      = 3;
    float const BONK_HOWL_BEAST_SPREAD     = 3.0f;
    float const BONK_BOAR_CHARGE_SHARE     = 1.5f;
    float const BONK_BOAR_CHARGE_PAST      = 10.0f;
    float const BONK_BOAR_CHARGE_SPEED     = 24.0f;
    float const BONK_BOAR_SCALE            = 1.0f;
    float const BONK_BEAR_SCALE            = 0.9f;
    float const BONK_WYVERN_SCALE          = 0.7f;
    int32 const BONK_BEAR_FIRST_MAUL_MS    = 500;
    uint32 const BONK_BEAR_MAUL_MS         = 1500;
    int32 const BONK_BEAR_IDLE_MS          = 250;
    float const BONK_BEAR_MAUL_RADIUS      = 5.0f;
    uint32 const BONK_BEAR_MAUL_STACKS     = 2;
    uint8 const BONK_SUMMON_HIT_KEYWORD_STACKS = 1;
    uint32 const BONK_SPELL_SWIPE          = 779;
    uint32 const BONK_KIT_MAUL             = 268;
    uint32 const BONK_BEAST_NO_SWING_MS    = HOUR * IN_MILLISECONDS;
    int32 const BONK_WYVERN_FIRST_SCREECH_MS = 400;
    int32 const BONK_WYVERN_SCREECH_MS     = 4000;
    uint32 const BONK_SPELL_SCREECH_BUFF   = 28419;
    uint32 const BONK_KIT_SCREECH          = 6394;
    uint32 const BONK_SCREECH_CHECK_MS     = 250;
    uint32 const BONK_FEEDING_FRENZY_MS    = 500;
    uint32 const BONK_FEEDING_FRENZY_MAX_MS = 10000;
    uint32 const BONK_FRENZY_GLOW_GAP_MS   = 1000;
    uint32 const BONK_KIT_FRENZY_GLOW      = 6385;
    uint32 const BONK_SPELL_ARCANE_SLOW    = 5116;
    int32 const BONK_ARCANE_SLOW_MS        = 2000;
    int32 const BONK_ICE_PATH_SLOW_PULSE_MS = 250;
    int32 const BONK_ICE_PATH_SLOW_MS      = 750;
    float const BONK_TRAP_TRIGGER_RADIUS   = 4.0f;
    int32 const BONK_TRAP_ARM_MS           = 500;
    float const BONK_TRAP_LIFETIME         = 4.0f;
    size_t const BONK_MAX_TRAPS            = 8;
    float const BONK_TRAP_AWARE_RANGE      = 25.0f;
    float const BONK_TRAP_SPACING          = 3.0f;
    float const BONK_TRAP_GROUND_SEARCH    = 30.0f;
    uint32 const BONK_TRAP_CHECK_MS        = 100;
    uint32 const BONK_THROWN_TRAP_ARM_MS   = 400;
    float const BONK_TRAP_THROW_RANGE      = 20.0f;
    float const BONK_LINE_WIDTH            = 3.0f;
    float const BONK_LINE_GROUP_RADIUS     = 8.0f;
    float const BONK_WOLF_SCALE            = 0.8f;
    float const BONK_SUMMON_LOOK_GROWTH    = 2.0f;
    float const BONK_MAX_SUMMON_LOOK_GROWTH = 60.0f;
    uint32 const BONK_WOLF_ATTACK_MS       = 1000;
    uint32 const BONK_SPELL_BESTIAL_WRATH  = 19574;
    float const BONK_FRENZY_DAMAGE         = 2.0f;
    float const BONK_FRENZY_REACH          = 5.0f;
    float const BONK_FRENZY_ARC            = 2.0f * M_PI_F / 3.0f;
    uint32 const BONK_SPELL_DETERRENCE     = 33467;
    float const BONK_DETERRENCE_HEALTH     = 35.0f;
    int32 const BONK_DETERRENCE_DODGE      = 50;
    uint32 const BONK_DETERRENCE_MS        = 8000;
    int32 const BONK_DETERRENCE_COOLDOWN_MS = 60000;
    float const BONK_PET_HUNT_RANGE        = 35.0f;
    float const BONK_CASTER_PET_DISTANCE   = 18.0f;
    float const BONK_PET_CONE_ARC          = 5.0f * M_PI_F / 6.0f;
    float const BONK_PET_LEASH_RANGE       = 50.0f;
    uint32 const BONK_PET_CHECK_MS         = 250;
    float const BONK_PET_SPEED             = 1.8f;
    float const BONK_PET_PICK_RANGE        = 10.0f;
    float const BONK_PET_DASH_RANGE        = 12.0f;
    float const BONK_POUNCE_SPEED          = 30.0f;
    float const BONK_POUNCE_GIVE           = 3.0f;

    float GetSummonSpeedRate(float moveSpeed)
    {
        return BONK_PET_SPEED * (1.0f + std::max(0.0f, moveSpeed) / 100.0f);
    }
    uint32 const BONK_SPELL_BLEED_HEAL     = 23881;

    struct BonkCastAnimation
    {
        uint32 spellId;
        uint32 kitId;
    };
    BonkCastAnimation const BONK_CAST_ANIMATIONS[] = {
        { 116, 202 },
        { 133, 38 },
        { 11366, 38 },
        { 7268, 4509 },
        { 16075, 1184 },
        { 403, 72 },
        { 686, 118 },
        { 33504, 118 },
        { 5176, 3291 },
        { 24239, 6409 },
        { 24099, 181 },
    };
    uint32 const BONK_CAST_ANIMATION_GAP_MS = 600;

    uint32 GetCastAnimationKit(uint32 spellId)
    {
        for (BonkCastAnimation const& animation : BONK_CAST_ANIMATIONS)
            if (animation.spellId == spellId)
                return animation.kitId;
        return 0;
    }
    uint32 const BONK_SPELL_ERUPTION_HIT   = 13339;
    uint32 const BONK_SPELL_PLAIN_HIT      = 7482;
    uint32 const BONK_KIT_FIRE_BURST       = 286;
    float const BONK_LINE_IMPACT_SPACING   = 6.0f;
    uint32 const BONK_ERUPTION_LOOK_MS     = 3000;
    int32 const BONK_VOID_LOOK_EVERY_MS    = 1000;
    uint32 const BONK_KIT_SAND_VORTEX      = 6472;
    uint32 const BONK_SPELL_SAND_VORTEX_LOOK = 33460;
    uint32 const BONK_SAND_VORTEX_LIFE_MS  = 8000;

    uint32 GetPoolBurstMs(uint32 kit)
    {
        switch (kit)
        {
            case 895:  return 3100;
            case 7453:
            case 7454: return 3000;
            case 286:  return 1600;
            case 160:  return 900;
            case 1004: return 800;
            case 809:  return 700;
            default:   return 1000;
        }
    }
    float const BONK_IMPACT_KIT_RADIUS     = 2.0f;
    float const BONK_IMPACT_MAX_STRETCH    = 6.0f;
    float const BONK_IMPACT_RING_FROM      = 5.0f;
    float const BONK_IMPACT_RING_SHARE     = 0.65f;
    float const BONK_IMPACT_RING_SPACING   = 4.5f;
    uint32 const BONK_IMPACT_RING_MIN      = 4;
    uint32 const BONK_IMPACT_RING_MAX      = 8;
    uint32 const BONK_HAZARD_MS            = 1000;
    uint32 const BONK_BOLT_TARGET_FREE_MS  = 500;
    size_t const BONK_MARKER_POOL_MAX      = 32;
    float const BONK_MOBINFO_STRIKE_RANGE  = 10.0f;

    float const BONK_ELITE_CHEST_CHANCE    = 15.0f;
    float const BONK_ELITE_CHEST_PER_AFFIX = 10.0f;
    float const BONK_ELITE_CHEST_PER_LUCK  = 0.1f;
    uint32 const BONK_ELITE_CHEST_PITY     = 5;

    uint32 const BONK_SPELL_NECROTIC       = 18951;
    float const BONK_NECROTIC_PER_STACK    = 5.0f;
    uint32 const BONK_NECROTIC_MAX_STACKS  = 15;
    uint32 const BONK_NECROTIC_CHECK_MS    = 250;
    float const BONK_BOLSTER_RANGE         = 12.0f;
    float const BONK_SANGUINE_RADIUS       = 5.0f;
    float const BONK_SANGUINE_HEAL         = 0.05f;
    uint32 const BONK_SANGUINE_DURATION_MS = 12000;

    std::string GetSanguineMarkKey(uint32 marker)
    {
        return "sanguine" + std::to_string(marker);
    }

    uint32 const BONK_SPELL_MOVE_SPEED = 17498;
    uint32 const BONK_SPELL_MAX_HEALTH = 19255;
    uint32 const BONK_SPELL_DODGE      = 13669;
    uint32 const BONK_SPELL_HEAL_TEXT  = 439;

    float const BONK_HEAL_CHANCE_NORMAL  = 0.5f;
    float const BONK_HEAL_CHANCE_ELITE   = 35.0f;
    float const BONK_HEAL_AMOUNT         = 0.20f;
    uint32 const BONK_HEAL_CHECK_MS      = 100;
    size_t const BONK_MAX_HEAL_DROPS     = 20;

    uint32 const BONK_SPELL_LOOT_SPARKLE = 33454;
    uint32 const BONK_HEAL_SPARKLE       = 900135;
    char const* const BONK_CHEST_PIN_ICON = "INV_Box_02";
    char const* const BONK_MAGNET_PIN_ICON = "INV_Stone_15";
    uint32 const BONK_SPELL_FEET_SPARKLE = 33455;
    uint32 const BONK_SPELL_TICKET_POOL  = 33640;
    uint32 const BONK_SPELL_GLYPH_POOL   = 33641;
    float const BONK_DROP_POOL_SCALE     = 0.3f;
    uint32 const BONK_DISPLAY_INVISIBLE  = 11686;
    uint32 const BONK_ORB_FADE_MS        = 1000;

    float const BONK_LEVEL_UP_HEAL       = 0.05f;
    uint32 const BONK_SPELL_REVIVE_SHIELD = 33468;
    float const BONK_REVIVE_CLEAR_RADIUS  = 15.0f;
    uint32 const BONK_SPELL_REVIVE_NOVA   = 15237;
    uint32 const BONK_KIT_REVIVE_SMITE    = 3153;
    float const BONK_REVIVE_OVERKILL      = 5.0f;
    float const BONK_DEMORALIZING_SHOUT_REDUCTION = 20.0f;
    int32 const BONK_PASSIVE_TRIGGER_POLL_MS = 100;

    uint32 const BONK_SPELL_SHIELD_VISUAL = 16051;
    uint32 const BONK_SHIELD_SEND_MS      = 250;
    uint32 const BONK_KIT_SHIELD_BLOCK    = 3053;
    int32 const BONK_PASSIVE_TRIGGER_IDLE_MS = 1000;

    float const BONK_MASTERY_OPENER_HEALTH  = 80.0f;
    float const BONK_MASTERY_EXECUTE_HEALTH = 30.0f;
    float const BONK_MASTERY_SIPHON_CAP     = 3.0f;
    int32 const BONK_CLEARCAST_DELAY_MS     = 300;
    int32 const BONK_WEAPON_RETRY_MS        = 250;
    float const BONK_PIE_DRIFT              = 0.05f;

    float GetKindHealth(BonkSpawnKind kind)
    {
        switch (kind)
        {
            case BONK_SPAWN_ELITE:    return 4.0f;
            case BONK_SPAWN_MINIBOSS: return 35.0f;
            case BONK_SPAWN_BOSS:     return 110.0f;
            default:                  return 1.0f;
        }
    }

    float GetKindDamage(BonkSpawnKind kind)
    {
        switch (kind)
        {
            case BONK_SPAWN_ELITE:    return 2.0f;
            case BONK_SPAWN_MINIBOSS: return 3.0f;
            case BONK_SPAWN_BOSS:     return 4.0f;
            default:                  return 1.0f;
        }
    }

    uint32 GetKindReward(BonkSpawnKind kind)
    {
        switch (kind)
        {
            case BONK_SPAWN_ELITE:    return 5;
            case BONK_SPAWN_MINIBOSS: return 20;
            case BONK_SPAWN_BOSS:     return 50;
            default:                  return 1;
        }
    }

    uint32 GetKindLevelBonus(BonkSpawnKind kind)
    {
        switch (kind)
        {
            case BONK_SPAWN_ELITE:    return 1;
            case BONK_SPAWN_MINIBOSS: return 2;
            case BONK_SPAWN_BOSS:     return 2;
            default:                  return 0;
        }
    }

    void SetMovementImmunity(Player* player, bool apply)
    {
        for (AuraType aura : { SPELL_AURA_MOD_DECREASE_SPEED, SPELL_AURA_MOD_ROOT, SPELL_AURA_MOD_STUN, SPELL_AURA_MOD_FEAR, SPELL_AURA_MOD_CONFUSE })
        {
            player->ApplySpellImmune(0, IMMUNITY_STATE, aura, apply);
            if (apply)
                player->RemoveSpellsCausingAura(aura);
        }
    }

    thread_local uint32 s_nextHelperLevel = 0;
    thread_local ObjectGuid t_missileLookSource;

    void ApplyHelperLevel(Creature* creature)
    {
        if (!s_nextHelperLevel)
            return;
        creature->SetLevel(s_nextHelperLevel);
        s_nextHelperLevel = 0;
    }

    void InstallOrbAI(Creature* creature)
    {
        creature->GetMotionMaster()->Initialize();
        creature->SetAI(new NullCreatureAI(creature));
        creature->SetFactionTemplateId(BONK_ORB_FACTION);
        creature->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE | UNIT_FLAG_IMMUNE_TO_PLAYER | UNIT_FLAG_IMMUNE_TO_NPC);
    }

    void InstallProjectileAI(Creature* creature)
    {
        ApplyHelperLevel(creature);
        creature->GetMotionMaster()->Initialize();
        creature->SetAI(new NullCreatureAI(creature));
        creature->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_PLAYER_CONTROLLED | UNIT_FLAG_NOT_SELECTABLE | UNIT_FLAG_IMMUNE_TO_PLAYER);
    }

    void InstallCaravanAI(Creature* creature)
    {
        creature->GetMotionMaster()->Initialize();
        creature->SetAI(new NullCreatureAI(creature));
        creature->SetFactionTemplateId(BONK_ORB_FACTION);
        creature->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IMMUNE_TO_PLAYER | UNIT_FLAG_IMMUNE_TO_NPC);
        creature->ClearCreatureState(CSTATE_REGEN_HEALTH);
    }

    void InstallBoltTargetAI(Creature* creature)
    {
        creature->GetMotionMaster()->Initialize();
        creature->SetAI(new NullCreatureAI(creature));
        creature->SetClientUnselectable(true);
    }

    uint32 GetOrbEntry(BonkOrb colour)
    {
        switch (colour)
        {
            case BONK_ORB_GREEN:  return BONK_ORB_LARGE;
            case BONK_ORB_RED:    return BONK_ORB_HUGE;
            case BONK_ORB_PURPLE: return BONK_ORB_GRAND;
            default:              return BONK_ORB_SMALL;
        }
    }

    void VanishOrb(Creature* orb)
    {
        orb->RemoveAllAuras();
        orb->SetDisplayId(BONK_DISPLAY_INVISIBLE);
        orb->DespawnOrUnsummon(BONK_ORB_FADE_MS);
    }

    bool IsPetInRunningReach(Creature const* pet, Unit const* victim)
    {
        return pet->IsMovingButNotWalking() && victim->IsMovingButNotWalking() && pet->CanReachWithMeleeAutoAttack(victim, LEEWAY_BONUS_RANGE) &&
            pet->HasInArc(victim, 2 * M_PI_F / 3);
    }

    class BonkPetAI : public CreatureAI
    {
        public:
            explicit BonkPetAI(Creature* creature) : CreatureAI(creature) {}

            void MoveInLineOfSight(Unit*) override {}
            void AttackedBy(Unit*) override {}
            void EnterEvadeMode() override {}
            void UpdateAI(uint32 const ) override
            {
                Unit* victim = m_creature->GetVictim();
                if (!victim || !victim->IsAlive() || !m_creature->HasUnitState(UNIT_STATE_MELEE_ATTACKING))
                    return;

                if (m_creature->IsAttackReady(BASE_ATTACK) && !m_creature->CanReachWithMeleeAutoAttack(victim) && IsPetInRunningReach(m_creature, victim))
                {
                    m_creature->AttackerStateUpdate(victim, BASE_ATTACK);
                    m_creature->ResetAttackTimer(BASE_ATTACK);
                    return;
                }
                DoMeleeAttackIfReady();
            }
    };

    struct TrapLook
    {
        uint32 object;
        uint32 set;
        uint32 sprung;
        uint32 sprungMs;
    };
    TrapLook const BONK_TRAP_LOOKS[] =
    {
        { 900009, 17986, 17987, 900 },
        { 900010, 17988, 17989, 1300 },
    };

    TrapLook const* GetTrapLook(uint32 object)
    {
        for (TrapLook const& look : BONK_TRAP_LOOKS)
            if (look.object == object)
                return &look;
        return nullptr;
    }

    void InstallPetAI(Creature* creature)
    {
        ApplyHelperLevel(creature);
        creature->GetMotionMaster()->Initialize();
        creature->SetAI(new BonkPetAI(creature));
    }

    void InstallChargerAI(Creature* creature)
    {
        ApplyHelperLevel(creature);
        creature->GetMotionMaster()->Initialize();
        creature->SetAI(new NullCreatureAI(creature));
        creature->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE | UNIT_FLAG_IMMUNE_TO_PLAYER | UNIT_FLAG_IMMUNE_TO_NPC);
    }

    float GetSegmentDistance(float x, float y, float fromX, float fromY, float toX, float toY)
    {
        float const dx = toX - fromX, dy = toY - fromY;
        float const length = dx * dx + dy * dy;
        float t = length > 0.0f ? ((x - fromX) * dx + (y - fromY) * dy) / length : 0.0f;
        t = std::max(0.0f, std::min(1.0f, t));
        float const px = fromX + t * dx - x, py = fromY + t * dy - y;
        return std::sqrt(px * px + py * py);
    }

    float GetSpellRadiusOf(SpellEntry const* spellInfo)
    {
        float radius = 0.0f;
        for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
            if (spellInfo->EffectRadiusIndex[i])
                radius = std::max(radius, Spells::GetSpellRadius(sSpellRadiusStore.LookupEntry(spellInfo->EffectRadiusIndex[i])));
        return radius;
    }

    uint32 GetKindRank(Creature const* mob)
    {
        BonkMobAI const* ai = dynamic_cast<BonkMobAI const*>(mob->AI());
        switch (ai ? ai->GetKind() : BONK_SPAWN_NORMAL)
        {
            case BONK_SPAWN_BOSS:     return 3;
            case BONK_SPAWN_MINIBOSS: return 2;
            case BONK_SPAWN_ELITE:    return 1;
            default:                  return 0;
        }
    }

    std::string Percent(float value)
    {
        char buffer[16];
        snprintf(buffer, sizeof(buffer), "%g%%", value);
        return buffer;
    }

    int32 FindDamageEffectOf(SpellEntry const* spellInfo)
    {
        for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
            if (spellInfo->Effect[i] == SPELL_EFFECT_SCHOOL_DAMAGE)
                return int32(i);
        for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
            if ((spellInfo->Effect[i] == SPELL_EFFECT_APPLY_AURA || spellInfo->Effect[i] == SPELL_EFFECT_PERSISTENT_AREA_AURA) &&
                spellInfo->EffectApplyAuraName[i] == SPELL_AURA_PERIODIC_DAMAGE)
                return int32(i);
        return -1;
    }

    void CastAtGround(Player* player, SpellEntry const* spellInfo, optional<int32> const* basePoints, float x, float y, float z)
    {
        Spell* spell = new Spell(player, spellInfo, true);
        for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
            if (basePoints[i])
                spell->m_currentBasePoints[i] = *basePoints[i];

        float const ground = player->GetMap()->GetHeight(x, y, z + 3.0f, true, 10.0f);
        SpellCastTargets targets;
        targets.setDestination(x, y, ground > INVALID_HEIGHT ? ground : z);
        spell->prepare(std::move(targets));
    }

    uint32 const BONK_SPELL_AMBUSH_PLAYER   = 8676;
    uint32 const BONK_SPELL_AMBUSH_NPC      = 24337;
    uint32 const BONK_SPELL_BACKSTAB_PLAYER = 25300;
    uint32 const BONK_SPELL_BACKSTAB_TEST   = 8627;
    uint32 const BONK_SPELL_JUDGEMENT_PLAYER = 20271;
    uint32 const BONK_SPELL_JUDGEMENT_NPC   = 23590;
    uint32 const BONK_SPELL_CHEAP_SHOT_PLAYER = 1833;
    uint32 const BONK_SPELL_CHEAP_SHOT_NPC  = 6409;
    uint32 const BONK_SPELL_SHRED_PLAYER    = 5221;
    uint32 const BONK_SPELL_SHRED_NPC       = 27555;
    uint32 const BONK_SPELL_MAUL_PLAYER     = 6807;
    uint32 const BONK_SPELL_MAUL_NPC        = 12161;
    uint32 const BONK_SPELL_CLAW_PLAYER     = 1082;
    uint32 const BONK_SPELL_CLAW_NPC        = 16827;
    uint32 const BONK_SPELL_RAKE_PLAYER     = 1822;
    uint32 const BONK_SPELL_RAKE_NPC        = 24331;

    uint32 GetQuietStrikeLook(uint32 spellId)
    {
        switch (spellId)
        {
            case BONK_SPELL_AMBUSH_PLAYER:   return BONK_SPELL_AMBUSH_NPC;
            case BONK_SPELL_BACKSTAB_PLAYER: return BONK_SPELL_BACKSTAB_TEST;
            case BONK_SPELL_JUDGEMENT_PLAYER: return BONK_SPELL_JUDGEMENT_NPC;
            case BONK_SPELL_CHEAP_SHOT_PLAYER: return BONK_SPELL_CHEAP_SHOT_NPC;
            case BONK_SPELL_SHRED_PLAYER:  return BONK_SPELL_SHRED_NPC;
            case BONK_SPELL_MAUL_PLAYER:   return BONK_SPELL_MAUL_NPC;
            case BONK_SPELL_CLAW_PLAYER:   return BONK_SPELL_CLAW_NPC;
            case BONK_SPELL_RAKE_PLAYER:   return BONK_SPELL_RAKE_NPC;
            default:                         return spellId;
        }
    }

    uint32 const BONK_SPELL_SHURIKEN_THROW   = 4165;
    uint32 const BONK_SHURIKEN_AMMO_DISPLAY  = 20771;

    void AddLookAmmo(WorldPackets::Spell::SpellGo& packet)
    {
        if (packet.spellId != BONK_SPELL_SHURIKEN_THROW)
            return;
        packet.castFlags |= CAST_FLAG_AMMO;
        packet.ammoDisplayId = BONK_SHURIKEN_AMMO_DISPLAY;
        packet.ammoInventoryType = INVTYPE_THROWN;
    }

    void ShowMissileAt(Unit* caster, SpellEntry const* spellInfo, float x, float y, float z)
    {
        auto packet = std::make_unique<WorldPackets::Spell::SpellGo>();
        packet->casterGuid = caster->GetObjectGuid();
        packet->unitCasterGuid = caster->GetObjectGuid();
        packet->spellId = GetQuietStrikeLook(spellInfo->Id);
        packet->castFlags = CAST_FLAG_UNKNOWN9;
        AddLookAmmo(*packet);
        packet->targets.setDestination(x, y, z);
        caster->SendObjectMessageToSet(std::move(packet), true);
    }

    void ShowSwingOn(Unit* caster, SpellEntry const* spellInfo, Unit* target)
    {
        auto packet = std::make_unique<WorldPackets::Spell::SpellGo>();
        packet->casterGuid = caster->GetObjectGuid();
        packet->unitCasterGuid = caster->GetObjectGuid();
        packet->spellId = GetQuietStrikeLook(spellInfo->Id);
        packet->castFlags = CAST_FLAG_UNKNOWN9;
        AddLookAmmo(*packet);
        packet->targets.setUnitTarget(target);
        caster->SendObjectMessageToSet(std::move(packet), true);
    }

    void ShowStrikeOn(Unit* caster, SpellEntry const* spellInfo, Unit* target)
    {
        auto packet = std::make_unique<WorldPackets::Spell::SpellGo>();
        packet->casterGuid = caster->GetObjectGuid();
        packet->unitCasterGuid = caster->GetObjectGuid();
        packet->spellId = GetQuietStrikeLook(spellInfo->Id);
        packet->castFlags = CAST_FLAG_UNKNOWN9;
        AddLookAmmo(*packet);
        packet->hitTargets.push_back(target->GetObjectGuid());
        packet->targets.setUnitTarget(target);
        caster->SendObjectMessageToSet(std::move(packet), true);
    }

    class BonkAreaVisual : public DynamicObject
    {
        public:
            void ApplyNothing() { m_radius = 0.0f; }
    };

    ObjectGuid PlaceAreaVisual(Player* player, uint32 spellId, float x, float y, float z, float radius, uint32 durationMs)
    {
        Map* map = player->GetMap();
        BonkAreaVisual* area = new BonkAreaVisual;
        if (!area->Create(map->GenerateLocalLowGuid(HIGHGUID_DYNAMICOBJECT), player, spellId, EFFECT_INDEX_0, x, y, z, int32(durationMs), radius,
            DYNAMIC_OBJECT_AREA_SPELL))
        {
            delete area;
            return ObjectGuid();
        }
        area->ApplyNothing();
        player->AddDynObject(area);
        map->Add(static_cast<DynamicObject*>(area));
        return area->GetObjectGuid();
    }

    void RemoveAreaVisual(Player* player, ObjectGuid const& guid)
    {
        if (guid.IsEmpty())
            return;
        if (Map* map = player->FindMap())
            if (DynamicObject* area = map->GetDynamicObject(guid))
            {
                player->RemoveDynObjectWithGUID(guid);
                area->Delete();
            }
    }

    void ApplyDashRunTravel(Player const* hero, uint32 age, float& x, float& y)
    {
        BonkPlayer const* bonk = hero->GetBonkPlayer();
        uint32 const now = WorldTimer::getMSTime();
        float travel;
        if (!bonk || !age || !bonk->GetDashRunTravel(now - age, now, travel))
            return;
        MovementInfo const& mi = hero->m_movementInfo;
        float const dx = x - mi.pos.x, dy = y - mi.pos.y;
        float const extrapolated = std::sqrt(dx * dx + dy * dy);
        if (extrapolated < 0.01f)
            return;
        float const scale = travel / extrapolated;
        x = mi.pos.x + dx * scale;
        y = mi.pos.y + dy * scale;
    }

    bool ExtrapolateHeroNow(Player const* hero, float& x, float& y, float& z)
    {
        if (!hero->IsMoving())
            return false;
        MovementInfo const& mi = hero->m_movementInfo;
        uint32 const age = std::min(WorldTimer::getMSTimeDiffToNow(mi.stime), BONK_HERO_MAX_AGE_MS);
        float o;
        if (!hero->ExtrapolateMovement(mi, age, x, y, z, o))
            return false;
        if (!mi.HasMovementFlag(MOVEFLAG_JUMPING))
            ApplyDashRunTravel(hero, age, x, y);
        return hero->IsWithinLOS(x, y, z);
    }

    void GetHeroPositionNow(Player const* hero, float& x, float& y, float& z)
    {
        hero->GetPosition(x, y, z);
        float px, py, pz;
        if (!ExtrapolateHeroNow(hero, px, py, pz))
            return;
        hero->UpdateAllowedPositionZ(px, py, pz);
        x = px;
        y = py;
        z = pz;
    }

    bool IsHeroWithin(Player const* hero, float x, float y, float radius)
    {
        float heroX, heroY, heroZ;
        GetHeroPositionNow(hero, heroX, heroY, heroZ);
        float const dx = heroX - x, dy = heroY - y;
        return dx * dx + dy * dy <= radius * radius;
    }

    void GetHeroBodyPositionNow(Player const* hero, float& x, float& y, float& z)
    {
        hero->GetPosition(x, y, z);
        float px, py, pz;
        if (!ExtrapolateHeroNow(hero, px, py, pz))
            return;
        float ground = pz;
        hero->UpdateAllowedPositionZ(px, py, ground);
        x = px;
        y = py;
        z = hero->m_movementInfo.HasMovementFlag(MOVEFLAG_JUMPING) ? std::max(pz, ground) : ground;
    }

    void GetHeroChestPositionNow(Player const* hero, float& x, float& y, float& z)
    {
        GetHeroBodyPositionNow(hero, x, y, z);
        z += std::max(BONK_ORB_LIFT, hero->GetCollisionHeight() * BONK_ORB_CHEST_SHARE);
    }

    float GetHeroDistance(Player const* hero, float x, float y, float z, WorldObject const* object)
    {
        return std::max(0.0f, object->GetDistance(x, y, z) - hero->GetObjectBoundingRadius());
    }

    float GetHeroSweepDistance(Player const* hero, Position const& from, float x, float y, float z, WorldObject const* object)
    {
        float const sx = x - from.x, sy = y - from.y, sz = z - from.z;
        float const lengthSq = sx * sx + sy * sy + sz * sz;
        float share = 0.0f;
        if (lengthSq > 0.0001f)
            share = std::min(1.0f, std::max(0.0f,
                ((object->GetPositionX() - from.x) * sx + (object->GetPositionY() - from.y) * sy + (object->GetPositionZ() - from.z) * sz) / lengthSq));
        return GetHeroDistance(hero, from.x + sx * share, from.y + sy * share, from.z + sz * share, object);
    }

    std::vector<Creature*> PickGroundSpots(std::vector<Creature*> const& mobs, float radius, uint32 count)
    {
        std::vector<Creature*> spots;
        std::vector<bool> covered(mobs.size(), false);
        while (spots.size() < count)
        {
            size_t best = mobs.size();
            uint32 bestCount = 0;
            for (size_t i = 0; i < mobs.size(); ++i)
            {
                if (covered[i])
                    continue;
                uint32 around = 0;
                for (size_t j = 0; j < mobs.size(); ++j)
                    if (!covered[j] && mobs[i]->GetDistance2d(mobs[j]) <= radius)
                        ++around;
                if (around > bestCount)
                {
                    best = i;
                    bestCount = around;
                }
            }
            if (best == mobs.size())
                break;

            for (size_t j = 0; j < mobs.size(); ++j)
                if (mobs[best]->GetDistance2d(mobs[j]) <= radius)
                    covered[j] = true;
            spots.push_back(mobs[best]);
        }
        return spots;
    }

    std::string FormatTime(uint32 seconds)
    {
        char buffer[16];
        snprintf(buffer, sizeof(buffer), "%u:%02u", seconds / 60, seconds % 60);
        return buffer;
    }
}

BonkRun::BonkRun(Player* player, BonkTier const& tier, BonkRunChoice const& choice) : m_player(player), m_tier(tier),
    m_bonuses(player->GetBonkPlayer()->GetRunBonuses()), m_mastery(player->GetBonkPlayer()->GetMasteryBonuses())
{
    BonkMap const* dungeon = choice.dungeon;
    m_dungeonId = dungeon && !choice.invade ? dungeon->id : 0;
    m_invaderId = dungeon && choice.invade ? dungeon->id : 0;
    m_startMapId = choice.startMap && !m_dungeonId ? choice.startMap->id : 0;
    m_difficultyLevel = tier.HasLevel(choice.level) ? choice.level : tier.firstLevel;
    m_levelRow = sBonkMgr.GetLevel(tier, GetBandLevel());
    uint32 const classMask = player->GetClassMask();
    for (bool const ownClass : { true, false })
        for (auto const& itr : sBonkMgr.GetWeapons())
            if (bool(itr.second.classMask & classMask) == ownClass)
                m_spellRoots.emplace(itr.second.spellId, BonkMastery::GetRootWeapon(itr.first));

    m_rerolls = m_bonuses.rerolls;
    m_banishes = m_bonuses.banishes;
    m_skips = m_bonuses.skips;
    m_revives = m_bonuses.revives;
    m_coins = m_bonuses.startCoins;
    m_valorEarned = m_coins;
    m_levelBestAtStart = player->GetBonkPlayer()->GetBestLevel();
}

std::string BonkRun::GetString(BonkString id, std::vector<std::string> const& args) const
{
    return m_player->GetBonkPlayer()->GetString(id, args);
}

std::string BonkRun::Localize(BonkTextField field, uint32 id, std::string const& english) const
{
    return m_player->GetBonkPlayer()->Localize(field, id, english);
}

LocaleConstant BonkRun::GetLocale() const
{
    return m_player->GetBonkPlayer()->GetLocale();
}

bool BonkRun::Begin(std::string& error)
{
    BonkStats::Reset(m_player->GetObjectGuid());

    BonkMap const* first = nullptr;
    if (IsCoopGuest())
    {
        Player* host = ObjectAccessor::FindPlayerNotInWorld(m_coopHost);
        BonkRun const* hostRun = host ? host->GetBonkPlayer()->GetRun() : nullptr;
        if (!hostRun || hostRun->IsOver())
        {
            error = m_player->GetBonkPlayer()->GetString(BONK_STRING_COOP_HOST_RUN_ENDED);
            return false;
        }
        first = &hostRun->GetStageMap();
    }
    else
        first = ChooseNextMap();
    if (!first)
    {
        error = GetString(BONK_STRING_NO_MAPS_ARE_SET_UP, { Localize(BONK_FIELD_TIER_NAME, m_tier.id, m_tier.name) });
        return false;
    }
    m_map = *first;
    ApplyInvader();
    BringBossEarlier();
    if (!m_sandbox)
        LogLoadout();

    if (m_player->IsGameMaster())
        m_player->SetGameMaster(false);
    m_player->SetCheatGod(m_player->GetBonkPlayer()->IsTestGod());
    SetMovementImmunity(m_player, true);

    m_player->GetBonkPlayer()->ResetCharacter();
    m_player->RemoveAurasDueToSpell(BONK_SPELL_MOVE_SPEED);
    m_player->RemoveAurasDueToSpell(BONK_SPELL_MAX_HEALTH);
    m_player->RemoveAurasDueToSpell(BONK_SPELL_DODGE);

    return m_sandbox ? TeleportToSandbox(error) : TeleportToStage(m_map, error);
}

BonkMap const* BonkRun::ChooseNextMap()
{
    if (m_sandbox)
        return GetSandboxMap();
    if (m_dungeonId)
        return sBonkMgr.GetMap(m_dungeonId);

    std::vector<BonkMap const*> pool;
    for (uint32 id : m_tier.mapIds)
        if (BonkMap const* map = sBonkMgr.GetMap(id))
            if (!map->creatures.empty())
                pool.push_back(map);

    if (pool.empty())
        return nullptr;

    if (m_startMapId && m_playedMaps.empty())
        if (BonkMap const* start = sBonkMgr.GetMap(m_startMapId))
        {
            m_playedMaps.insert(start->id);
            return start;
        }

    std::vector<BonkMap const*> candidates;
    for (BonkMap const* map : pool)
        if (!m_playedMaps.count(map->id))
            candidates.push_back(map);

    if (candidates.empty())
    {
        m_playedMaps.clear();
        for (BonkMap const* map : pool)
            if (pool.size() == 1 || map->id != m_map.id)
                candidates.push_back(map);
    }

    BonkMap const* map = candidates[urand(0, uint32(candidates.size() - 1))];
    m_playedMaps.insert(map->id);
    return map;
}

void BonkRun::ApplyInvader()
{
    BonkMap const* invader = m_invaderId ? sBonkMgr.GetMap(m_invaderId) : nullptr;
    if (!invader)
        return;
    m_map.creatures = invader->creatures;
    m_map.levelOffset = invader->levelOffset;
}

namespace
{
    bool CanWalkFromAnchor(BonkMap const& map, TerrainInfo* terrain, float x, float y, float z);
}

void BonkRun::LearnStarts(std::vector<BonkSpot> const& spots)
{
    if (spots.empty() || !m_player->IsInWorld())
        return;
    TerrainInfo* terrain = sTerrainMgr.LoadTerrain(m_map.mapId);
    float const edge = m_map.type == BONK_MAP_OPEN ? BONK_START_EDGE : BONK_START_EDGE_ENCLOSED;
    for (BonkSpot const& spot : spots)
    {
        if (m_map.type == BONK_MAP_OPEN && !terrain->IsOutdoors(spot.x, spot.y, spot.z + 1.0f))
            continue;
        bool inside = true;
        for (uint32 i = 0; i < 8 && inside; ++i)
        {
            float const angle = i * M_PI_F / 4.0f;
            float const x = spot.x + edge * std::cos(angle);
            float const y = spot.y + edge * std::sin(angle);
            float const z = m_player->GetMap()->GetHeight(x, y, spot.z + 20.0f, true, 60.0f);
            if (z <= INVALID_HEIGHT || (m_map.zoneId && terrain->GetZoneId(x, y, z) != m_map.zoneId) || terrain->IsInWater(x, y, z + 1.0f))
                inside = false;
        }
        if (!inside || (m_map.type == BONK_MAP_OPEN && !CanWalkFromAnchor(m_map, terrain, spot.x, spot.y, spot.z)))
            continue;
        BonkMapStart start;
        start.x = spot.x;
        start.y = spot.y;
        start.z = spot.z;
        sBonkMgr.AddMapStart(m_map.id, start, BONK_START_SPREAD, BONK_START_MAX_LEARNED);
    }
}

namespace
{
    bool IsOpenGround(TerrainInfo const* terrain, uint32 zoneId, float x, float y, float z)
    {
        if (terrain->GetZoneId(x, y, z) != zoneId || terrain->IsInWater(x, y, z + 1.0f) || !terrain->IsOutdoors(x, y, z + 1.0f))
            return false;
        float const top = terrain->GetHeightStatic(x, y, z + 50.0f, true, 60.0f);
        return top <= z + BONK_ZONE_START_CLEAR;
    }

    bool HasWorldSpawnsNear(uint32 mapId, float x, float y)
    {
        CellPair const center = MaNGOS::ComputeCellPair(x, y);
        int32 const last = int32(TOTAL_NUMBER_OF_CELLS_PER_MAP) - 1;
        size_t spawns = 0;
        std::shared_lock<std::shared_timed_mutex> lock(sObjectMgr.GetCellLoadingObjectsMutex());
        for (int32 cx = std::max(0, int32(center.x_coord) - BONK_ZONE_START_SPAWN_CELLS); cx <= std::min(last, int32(center.x_coord) + BONK_ZONE_START_SPAWN_CELLS); ++cx)
            for (int32 cy = std::max(0, int32(center.y_coord) - BONK_ZONE_START_SPAWN_CELLS); cy <= std::min(last, int32(center.y_coord) + BONK_ZONE_START_SPAWN_CELLS); ++cy)
                if (CellObjectGuids const* guids = sObjectMgr.GetCellObjectGuids(mapId, uint32(cy) * TOTAL_NUMBER_OF_CELLS_PER_MAP + uint32(cx)))
                    spawns += guids->creatures.size() + guids->gameobjects.size();
        return spawns >= BONK_ZONE_START_SPAWNS;
    }

    void LoadGridsBetween(TerrainInfo* terrain, float fromX, float fromY, float toX, float toY)
    {
        float const step = SIZE_OF_GRIDS / 2.0f;
        for (float gx = std::min(fromX, toX) - step; gx <= std::max(fromX, toX) + step; gx += step)
            for (float gy = std::min(fromY, toY) - step; gy <= std::max(fromY, toY) + step; gy += step)
                terrain->GetHeightStatic(gx, gy, MAX_HEIGHT, false);
    }

    struct StartQuery
    {
        dtNavMesh const* mesh = nullptr;
        dtNavMeshQuery* query = nullptr;
    };

    dtNavMeshQuery const* GetStartQuery(uint32 mapId)
    {
        dtNavMesh const* mesh = MMAP::MMapFactory::createOrGetMMapManager()->GetNavMesh(mapId);
        if (!mesh)
            return nullptr;
        thread_local std::map<uint32, StartQuery> queries;
        StartQuery& entry = queries[mapId];
        if (entry.mesh == mesh)
            return entry.query;
        if (!entry.query)
            entry.query = dtAllocNavMeshQuery();
        entry.mesh = nullptr;
        if (!entry.query || dtStatusFailed(entry.query->init(mesh, BONK_START_PATH_NODES)))
            return nullptr;
        entry.mesh = mesh;
        return entry.query;
    }

    bool CanWalkFromAnchor(BonkMap const& map, TerrainInfo* terrain, float x, float y, float z)
    {
        LoadGridsBetween(terrain, map.x, map.y, x, y);
        dtNavMeshQuery const* query = GetStartQuery(map.mapId);
        if (!query)
            return true;

        dtQueryFilter filter;
        filter.setIncludeFlags(NAV_GROUND | NAV_WATER);
        float const anchor[3] = { map.y, map.z, map.x };
        float const spot[3] = { y, z, x };
        float anchorNear[3] = { 0.0f, 0.0f, 0.0f };
        float spotNear[3] = { 0.0f, 0.0f, 0.0f };
        dtPolyRef const anchorRef = PathInfo::FindWalkPoly(query, anchor, filter, anchorNear, BONK_START_POLY_REACH);
        if (!anchorRef)
            return true;
        dtPolyRef const spotRef = PathInfo::FindWalkPoly(query, spot, filter, spotNear, BONK_START_POLY_REACH);
        if (!spotRef)
            return false;

        dtPolyRef path[BONK_START_PATH_POLYS];
        int count = 0;
        dtStatus const status = query->findPath(anchorRef, spotRef, anchorNear, spotNear, &filter, path, &count, BONK_START_PATH_POLYS);
        return dtStatusSucceed(status) && count > 0 && path[count - 1] == spotRef;
    }

    bool FindZoneStart(BonkMap const& map, float& x, float& y, float& z)
    {
        if (!map.zoneId || map.type != BONK_MAP_OPEN)
            return false;
        TerrainInfo* terrain = sTerrainMgr.LoadTerrain(map.mapId);
        for (uint32 attempt = 0; attempt < BONK_ZONE_START_TRIES; ++attempt)
        {
            float px = frand(BONK_ZONE_START_MARGIN, 100.0f - BONK_ZONE_START_MARGIN);
            float py = frand(BONK_ZONE_START_MARGIN, 100.0f - BONK_ZONE_START_MARGIN);
            if (!Zone2MapCoordinates(px, py, map.zoneId))
                return false;
            if (!HasWorldSpawnsNear(map.mapId, px, py))
                continue;
            float const pz = terrain->GetHeightStatic(px, py, MAX_HEIGHT, false);
            if (pz <= INVALID_HEIGHT || !IsOpenGround(terrain, map.zoneId, px, py, pz))
                continue;
            bool flat = true;
            for (uint32 i = 0; i < 8 && flat; ++i)
            {
                float const angle = i * M_PI_F / 4.0f;
                float const rx = px + BONK_START_EDGE * std::cos(angle);
                float const ry = py + BONK_START_EDGE * std::sin(angle);
                float const rz = terrain->GetHeightStatic(rx, ry, pz + 40.0f, false, 80.0f);
                flat = rz > INVALID_HEIGHT && std::fabs(rz - pz) <= BONK_ZONE_START_FLAT &&
                    terrain->GetZoneId(rx, ry, rz) == map.zoneId && !terrain->IsInWater(rx, ry, rz + 1.0f);
            }
            if (!flat || !CanWalkFromAnchor(map, terrain, px, py, pz))
                continue;
            x = px;
            y = py;
            z = pz;
            return true;
        }
        return false;
    }

    std::vector<BonkMapStart const*> GetReachableStarts(BonkMap const& map, std::vector<BonkMapStart> const& starts)
    {
        std::vector<BonkMapStart const*> reachable;
        TerrainInfo* terrain = sTerrainMgr.LoadTerrain(map.mapId);
        for (BonkMapStart const& start : starts)
            if (map.type != BONK_MAP_OPEN || (HasWorldSpawnsNear(map.mapId, start.x, start.y) && CanWalkFromAnchor(map, terrain, start.x, start.y, start.z)))
                reachable.push_back(&start);
        return reachable;
    }
}

bool BonkRun::PlaceNearHost(float& x, float& y, float& z, float& o) const
{
    Player* host = ObjectAccessor::FindPlayerNotInWorld(m_coopHost);
    BonkRun const* hostRun = host ? host->GetBonkPlayer()->GetRun() : nullptr;
    if (!hostRun)
        return false;
    float hostX, hostY, hostZ;
    hostRun->GetStageStart(hostX, hostY, hostZ);
    if (hostX == 0.0f && hostY == 0.0f)
        return false;
    float const angle = frand(0.0f, 2.0f * M_PI_F);
    float const distance = frand(BONK_COOP_FOLLOW_MIN, BONK_COOP_FOLLOW_MAX);
    x = hostX + std::cos(angle) * distance;
    y = hostY + std::sin(angle) * distance;
    z = hostZ;
    o = std::atan2(hostY - y, hostX - x);
    if (o < 0.0f)
        o += 2.0f * M_PI_F;
    return true;
}

bool BonkRun::TeleportToStage(BonkMap const& map, std::string& error)
{
    float x = map.x, y = map.y, z = map.z, o = map.o;
    std::vector<BonkMapStart> const starts = sBonkMgr.GetMapStarts(map.id);
    std::vector<BonkMapStart const*> const reachable = GetReachableStarts(map, starts);
    if (IsCoopGuest() && PlaceNearHost(x, y, z, o))
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "Co-op: %s placed near the host at %.1f %.1f", m_player->GetName(), x, y);
    else if (FindZoneStart(map, x, y, z))
    {
        z += 0.5f;
        o = std::atan2(map.y - y, map.x - x);
        if (o < 0.0f)
            o += 2.0f * M_PI_F;
    }
    else if (reachable.size() >= BONK_START_MIN_LEARNED)
    {
        std::vector<BonkMapStart const*> away;
        if (m_player->GetMapId() == map.mapId)
            for (BonkMapStart const* start : reachable)
                if (m_player->GetDistance2d(start->x, start->y) > BONK_START_SPREAD)
                    away.push_back(start);
        BonkMapStart const& start = away.empty() ? *reachable[urand(0, uint32(reachable.size()) - 1)] : *away[urand(0, uint32(away.size()) - 1)];
        x = start.x;
        y = start.y;
        z = start.z + 0.5f;
        o = std::atan2(map.y - y, map.x - x);
        if (o < 0.0f)
            o += 2.0f * M_PI_F;
    }

    if (!m_player->TeleportTo(map.mapId, x, y, z, o))
    {
        error = GetString(BONK_STRING_YOU_COULDNT_TRAVEL_TO, { Localize(BONK_FIELD_MAP_NAME, map.id, map.name) });
        return false;
    }

    m_stageX = x;
    m_stageY = y;
    m_stageZ = z;
    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "Co-op stage: %s goes to map %u at %.1f %.1f, guest %u", m_player->GetName(), map.mapId, x, y, uint32(IsCoopGuest()));
    m_state = STATE_ENTERING;
    m_enterTimer = 0;
    return true;
}

void BonkRun::Update(uint32 diff)
{
    StressTick const stressTick(*this, diff);
    RefreshGroupEnemies();
    UpdateSky();
    RefreshHeroPositionNow();
    if (m_runXpDirty)
        UpdateRunXp(false);

    if (m_paused)
        HoldAuraTimers(diff);

    switch (m_state)
    {
        case STATE_ENTERING:
        {
            if (m_player->GetMapId() == m_map.mapId && !m_player->IsBeingTeleported())
            {
                StartStage();
                return;
            }

            m_enterTimer += diff;
            if (m_enterTimer >= BONK_ENTER_TIMEOUT_MS)
            {
                m_player->GetBonkPlayer()->Notify(BONK_STRING_YOU_COULDNT_TRAVEL_TO, { Localize(BONK_FIELD_MAP_NAME, m_map.id, m_map.name) });
                Exit();
            }
            break;
        }
        case STATE_RUNNING:
            UpdateRunning(diff);
            break;
        case STATE_PORTAL:
        {
            if (!m_player->IsAlive() && !BonkCoop::IsDowned(m_player))
            {
                if (m_revives)
                    Revive();
                else if (!BonkCoop::Down(m_player))
                    Finish(false);
                return;
            }

            UpdateOffers();
            UpdateBoundary(diff);
            UpdateNatives(diff);
            UpdateShield(diff);
            UpdateNecrotic(diff);
            UpdatePools(diff);
            UpdatePowerups(diff, false);
            UpdateOrbs(diff);
            UpdateShards(diff);
            UpdateHeals(diff);

            m_stateTimer += diff;
            if (m_stateTimer >= BONK_STATE_INTERVAL_MS)
            {
                m_stateTimer = 0;
                SendState();

                if (!m_player->GetMap()->GetGameObject(m_portalGuid) && !m_player->IsBeingTeleported())
                {
                    float const angle = m_player->GetOrientation();
                    PlacePortal(m_player->GetPositionX() + BONK_PORTAL_DISTANCE * std::cos(angle),
                        m_player->GetPositionY() + BONK_PORTAL_DISTANCE * std::sin(angle), m_player->GetPositionZ());
                }
            }
            break;
        }
        case STATE_ENDING:
            m_endTimer -= int32(diff);
            if (m_endTimer <= 0)
                Exit();
            break;
        case STATE_FINISHED:
            break;
    }
}

void BonkRun::StartStage()
{
    sLog.Player(m_player->GetSession(), LOG_LEVELUP, LOG_LVL_BASIC,
        "Bonk stage: %s starts stage %u at run minute %.2f (stage %u, %s, %s, class %u): level %u, %u kills, took %u damage so far",
        m_player->GetName(), m_stage, GetRunMinutes(), m_stage, m_map.key.c_str(), m_tier.key.c_str(), uint32(m_player->GetClass()),
        m_player->GetLevel() + m_overcapLevel, m_kills, m_damageTaken);
    m_state = STATE_RUNNING;
    m_elapsedMs = 0;
    m_nextEvent = 0;
    m_menuOpen = false;
    m_miniEventActive = false;
    m_nextMiniEventMs = 0;
    m_lastMiniEvent = MAX_BONK_MINI_EVENT;
    m_spawnTimer = BONK_FIRST_WAVE_MS;
    m_eliteTimer = int32(BONK_ELITE_RETRY_MS);
    m_waveIndex = 0;
    m_waveEntered = false;
    m_waveWarned = 0;
    m_miniEventSlots = 0;
    m_overtimeWaves = 0;
    m_overtimeTimer = 0;
    m_spawnDamageScale = 1.0f;
    m_overtimeReported = 0;
    m_bossArrivals.clear();
    m_heldEvents.clear();
    RollHidden();
    m_gruntRadius = 0.0f;
    m_bossSpawned = false;
    m_stoneBoss = false;
    m_bossGuid.Clear();
    m_powerupSpawnTimer = m_map.powerupIntervalSeconds * IN_MILLISECONDS / 2;
    m_nativeTimer = BONK_NATIVE_SWEEP_MS;
    if (m_stage > 1)
        m_rerolls += uint32(GetGlyphValue(BONK_GLYPH_FORESIGHT, 0));
    m_glyphNextChestMs = uint32(GetGlyphValue(BONK_GLYPH_TREASURE, 0));
    m_lastLevel = m_player->GetLevel();
    m_safeX = m_player->GetPositionX();
    m_safeY = m_player->GetPositionY();
    m_safeZ = m_player->GetPositionZ();
    m_lastX = m_safeX;
    m_lastY = m_safeY;
    m_moveX = m_moveY = 0.0f;
    m_headingTimer = 0;
    m_farSince.clear();
    m_crowd.Clear();
    m_packSpots.clear();
    m_sectorBlockedUntil.assign(BONK_SPAWN_SECTORS, 0);
    m_lanes[0] = m_lanes[1] = -1;
    m_ringTimer = m_ringSamples = m_ringBlocked = m_ringClosed = 0;
    m_laneTimer = 0;
    m_perched = false;
    m_perchTimer = 0;
    m_perchChecks = 0;
    m_reachChecks = 0;
    m_perchedMs = 0;
    m_flyerTimer = 0;
    m_hasGround = true;
    m_groundX = m_player->GetPositionX();
    m_groundY = m_player->GetPositionY();
    m_groundZ = m_player->GetPositionZ();

    m_player->SetCreatureSummonLimit(BONK_SUMMON_LIMIT);

    if (m_weapons.empty())
    {
        uint32 const classMask = m_player->GetClassMask();
        if (BonkWeapon const* starter = m_player->GetBonkPlayer()->GetStarterWeapon())
        {
            AddWeapon(*starter);
            m_runSpec = starter->spec;
        }

        if (m_weapons.empty())
            for (auto const& itr : sBonkMgr.GetWeapons())
                if (!itr.second.evolvesFrom && (!itr.second.classMask || (itr.second.classMask & classMask)))
                {
                    AddWeapon(itr.second);
                    break;
                }

        SnapshotBroughtGear();

        ApplyPassives();

        if (!m_sandbox)
        {
            GiveStartCard();
            GiveStartLevels();
            if (GetUtility(BONK_UTILITY_START_GLYPH) > 0.0f)
                QueueGlyphPick();
        }
    }
    else
    {
        m_player->SetHealth(m_player->GetMaxHealth());
        m_player->SetPower(POWER_MANA, m_player->GetMaxPower(POWER_MANA));
    }

    UpdateGear();

    RefillShield(GetMaxShield());
    m_shieldIdleMs = 0;
    SetNecroticStacks(0);
    ClearBossPressure();
    DespawnPools();
    DespawnWeaponOrbs();
    DespawnProjectiles();
    DespawnShotSources();
    m_shotSourceTimer = BONK_SHOT_SOURCE_CHECK_MS;
    m_trickShotTimes.clear();
    ClearHunterEvolutions();
    m_pendingCasts.clear();
    m_heldShotSources.clear();
    m_telegraphs.Dismiss(m_player);
    m_objectives.Clear(m_player);
    m_spawnPoints.Reset(m_player->GetPositionX(), m_player->GetPositionY(), m_player->GetPositionZ());
    ResetShrines();
    ResetVendor();
    DespawnTraps();
    DespawnLandMines();
    DespawnPets();
    m_pendingKeywords.clear();
    m_pendingConsumes.clear();
    m_dyingKeywords.clear();
    m_dotStacks.clear();
    m_crowdControl.clear();
    m_buffs.clear();
    ResetWarriorState();
    ResetMageState();
    ResetShamanState();
    ResetPriestState();
    ResetRogueState();
    ResetPaladinState();
    ResetDruidState();
    m_bleedHeal = 0.0f;
    m_player->GetBonkPlayer()->SendAddon("mobclear");

    if (m_sandbox)
    {
        StartSandbox();
        return;
    }

    SendFullState();
    if (IsFinale())
    {
        m_player->GetBonkPlayer()->Notify(BONK_STRING_FINALE_BEGINS, { Localize(BONK_FIELD_MAP_NAME, m_map.id, m_map.name) });
        return;
    }
    uint32 const minutes = m_map.durationSeconds / 60;
    uint32 const seconds = m_map.durationSeconds % 60;
    std::string const minuteText = GetString(minutes == 1 ? BONK_STRING_DURATION_MINUTE : BONK_STRING_DURATION_MINUTES, { std::to_string(minutes) });
    std::string const secondText = GetString(seconds == 1 ? BONK_STRING_DURATION_SECOND : BONK_STRING_DURATION_SECONDS, { std::to_string(seconds) });
    std::string const bossIn = !minutes ? secondText : !seconds ? minuteText : GetString(BONK_STRING_DURATION_MINUTES_AND_SECONDS, { minuteText, secondText });
    m_player->GetBonkPlayer()->Notify(BONK_STRING_STAGE_BEGINS, { m_player->GetBonkPlayer()->GetLevelName(m_difficultyLevel), std::to_string(m_stage),
        std::to_string(m_tier.stages), Localize(BONK_FIELD_MAP_NAME, m_map.id, m_map.name), bossIn });
}

void BonkRun::SendFullState() const
{
    BonkPlayer* bonk = m_player->GetBonkPlayer();
    if (m_sandbox)
        bonk->SendAddon("start~" + Localize(BONK_FIELD_MAP_NAME, m_map.id, m_map.name) + "~" + GetString(BONK_STRING_SANDBOX) + "~0~" + std::to_string(m_stage) + "~0~");
    else
        bonk->SendAddon("start~" + Localize(BONK_FIELD_MAP_NAME, m_map.id, m_map.name) + "~" + Localize(BONK_FIELD_TIER_NAME, m_tier.id, m_tier.name) + "~" +
            std::to_string(m_map.durationSeconds) + "~" + std::to_string(m_stage) + "~" + std::to_string(GetFirstMinibossSecond()) + "~" + DescribeTimelineMarks() + "~" + (IsMythic() ? "1" : "0") + "~" +
            std::to_string(m_tier.stages) + "~" + bonk->GetLevelName(m_difficultyLevel) + "~" + (IsFinale() ? "1" : "0"));
    SendLoadout();
    SendState();
    SendRunXp();
    SendCards();
    SendLeyBlessings();
    if (!m_offer.empty())
        SendOffer();
    m_objectives.Resend(m_player);
    if (m_greedStacks)
        SendGreed();
    if (!m_leyOffer.empty())
        SendLeyOffer();
    if (m_vendorOpen)
        SendVendor();
    SendGlyphs();
    if (!m_glyphOffer.empty())
        SendGlyphOffer();
    if (m_miniEventActive)
        SendMiniEventTimer(true);
    m_player->GetBonkPlayer()->SendDash();
}

void BonkRun::UpdateRunning(uint32 diff)
{
    if (!m_player->IsAlive() && !BonkCoop::IsDowned(m_player))
    {
        SpeakBossLine(m_player->GetMap()->GetCreature(m_lastStrikerGuid), BONK_BOSS_LINE_KILL);
        if (m_sandbox)
        {
            ++m_sandboxDeaths;
            ReviveInSandbox();
            return;
        }
        if (IsDivineShieldReady())
            Revive(false, GetGlyph(BONK_GLYPH_DIVINE_SHIELD));
        else if (m_revives)
            Revive();
        else if (m_soulstones)
            Revive(true);
        else if (!BonkCoop::Down(m_player))
            Finish(false);
        return;
    }

    UpdateOffers();
    UpdateNatives(diff);
    if (m_paused)
        return;

    if (!m_sandbox)
        m_elapsedMs += diff;
    m_totalMs += diff;
    m_rewardMs += diff * GetBonkMapRewardPercent(m_map.type) / 100;

    UpdateHeading(diff);
    UpdateBoundary(diff);
    UpdateRecovery(diff);
    UpdateCards(diff);
    UpdateGlyphs(diff);
    UpdateFeel(diff);
    UpdateTrinkets(diff);
    UpdateShield(diff);
    UpdateNecrotic(diff);
    UpdatePools(diff);
    UpdateVoidZones(diff);
    UpdateHeroMark(diff);
    UpdateBossWounds(diff);
    UpdatePowerups(diff, !m_sandbox);
    UpdateObjectives(diff);
    UpdateOrbs(diff);
    UpdateShards(diff);
    UpdateHeals(diff);
    UpdateGearDrops(diff);
    UpdateHearth(diff);
    if (m_state != STATE_RUNNING)
        return;
    if (m_sandbox)
        UpdateSandbox(diff);
    else
    {
        UpdateEvents();
        if (m_state != STATE_RUNNING)
            return;
        UpdateMiniEvents(diff);
        UpdateShrines(diff);
        UpdateHidden(diff);
        UpdateVendor(diff);
        if (m_state != STATE_RUNNING)
            return;

        UpdateEmerging(diff);
        UpdateSpawns(diff);
        if (m_state != STATE_RUNNING)
            return;
        UpdateStragglers(diff);
    }
    if (m_state != STATE_RUNNING)
        return;

    UpdateCrowd(diff);
    UpdateSteadyAim();
    UpdateWeapons(diff);
    if (m_state != STATE_RUNNING)
        return;
    UpdatePendingCasts(diff);
    UpdateArrowStorms(diff);
    UpdateKeywordClouds(diff);
    UpdateWarrior(diff);
    UpdateMage(diff);
    UpdateTalents(diff);
    UpdateShaman(diff);
    UpdatePriest(diff);
    UpdateRogue(diff);
    UpdateWarlock(diff);
    UpdateDruid(diff);
    UpdateTalents();
    UpdateIcePath(diff);
    UpdateWeaponOrbs(diff);
    UpdateProjectiles(diff);
    UpdateEnemyMissiles(diff);
    UpdateShotSources(diff);
    UpdateTraps(diff);
    UpdatePetTargets(diff);
    UpdateBeasts(diff);
    ResolveCriticalStrikes();
    UpdateKeywords(diff);
    UpdateDefensive(diff);
    m_telegraphs.Update(m_player, diff);
    if (m_state != STATE_RUNNING)
        return;

    m_stateTimer += diff;
    if (m_stateTimer >= BONK_STATE_INTERVAL_MS)
    {
        m_stateTimer = 0;
        UpdateGear();
        SendState();
        RefreshChangedWeaponInfo();
    }
}

void BonkRun::DropOffers()
{
    BonkPlayer* bonk = m_player->GetBonkPlayer();
    if (!m_offer.empty())
        bonk->SendAddon("picked");
    if (!m_glyphOffer.empty())
        bonk->SendAddon("glyphclose");
    if (!m_leyOffer.empty())
        bonk->SendAddon("leyofferclose");
    CloseGearOffer();
    m_offer.clear();
    m_glyphOffer.clear();
    m_leyOffer.clear();
    m_pendingLevels = 0;
    m_pendingGlyphPicks = 0;
    m_pendingLeyOffers = 0;
    m_pendingGear.clear();
    m_pendingChestChoices.clear();
}

void BonkRun::UpdateOffers()
{
    if (m_state != STATE_RUNNING && m_state != STATE_PORTAL)
        return;

    uint32 const level = m_player->GetLevel();
    if (level > m_lastLevel)
    {
        m_pendingLevels += level - m_lastLevel;
        m_lastLevel = level;
    }

    if (!m_offer.empty() || !m_leyOffer.empty() || m_vendorOpen || m_vaultOpen || m_gearOfferEntry || !m_glyphOffer.empty())
        return;

    if (m_stress.on)
    {
        m_pendingLevels = 0;
        m_pendingGlyphPicks = 0;
        m_pendingGear.clear();
        m_pendingChestChoices.clear();
        m_pendingLeyOffers = 0;
        return;
    }

    if (m_pendingGlyphPicks)
        OfferGlyphs();
    else if (!m_pendingChestChoices.empty())
        OfferUpgrades(OFFER_CHEST);
    else if (!m_pendingGear.empty())
        OfferGear();
    else if (m_pendingLevels && m_sandbox && m_sandboxQuietLevels)
        m_pendingLevels = 0;
    else if (m_pendingLevels)
        OfferUpgrades(OFFER_LEVEL);
    else if (m_pendingLeyOffers)
        OfferLeyBuffs();
}

void BonkRun::UpdateBoundary(uint32 diff)
{
    m_boundaryTimer += diff;
    if (m_boundaryTimer < BONK_BOUNDARY_INTERVAL_MS)
        return;
    m_boundaryTimer = 0;

    if (!m_map.zoneId || m_player->IsBeingTeleported())
        return;

    if (m_player->GetZoneId() == m_map.zoneId)
    {
        m_safeX = m_player->GetPositionX();
        m_safeY = m_player->GetPositionY();
        m_safeZ = m_player->GetPositionZ();
        return;
    }

    m_player->NearTeleportTo(m_safeX, m_safeY, m_safeZ, m_player->GetOrientation());
    m_player->GetBonkPlayer()->Notify(BONK_STRING_YOU_CANT_LEAVE, { Localize(BONK_FIELD_MAP_NAME, m_map.id, m_map.name) });
}

void BonkRun::UpdateNatives(uint32 diff)
{
    m_nativeTimer += diff;
    if (m_nativeTimer < BONK_NATIVE_SWEEP_MS)
        return;
    m_nativeTimer = 0;

    if (m_player->IsBeingTeleported() || !m_player->IsInWorld())
        return;

    std::list<Creature*> creatures;
    MaNGOS::AnyUnitInObjectRangeCheck check(m_player, BONK_NATIVE_SWEEP_RANGE);
    MaNGOS::CreatureListSearcher<MaNGOS::AnyUnitInObjectRangeCheck> searcher(creatures, check);
    Cell::VisitGridObjects(m_player, searcher, BONK_NATIVE_SWEEP_RANGE);

    for (Creature* creature : creatures)
    {
        if (creature->IsTemporarySummon() && static_cast<TemporarySummon*>(creature)->GetSummonerGuid().IsPlayer())
            continue;
        if (creature->GetCharmerOrOwnerGuid().IsPlayer() || creature->IsFriendlyTo(m_player))
            continue;
        if (creature->HasCreatureState(CSTATE_DESPAWNING))
            continue;

        creature->DespawnOrUnsummon(0, BONK_NATIVE_RESPAWN_SEC);
    }
}

void BonkRun::UpdateRecovery(uint32 diff)
{
    m_recoveryTimer += diff;
    if (m_recoveryTimer < IN_MILLISECONDS)
        return;
    m_recoveryTimer -= IN_MILLISECONDS;
    ReportQuestCounts();
    SamplePerfSecond();

    uint32 const leech = uint32(m_leechCarry);
    m_leechCarry = 0.0f;
    m_leechUnreported += leech;
    if (leech && m_player->IsAlive())
    {
        SupportScope scope(*this, PassiveSupport(BONK_PASSIVE_LEECH));
        HealHero(ReduceHealing(leech));
    }

    float const percent = GetPassiveTotal(BONK_PASSIVE_RECOVERY);
    if (percent > 0.0f && m_player->IsAlive())
    {
        SupportScope scope(*this, PassiveSupport(BONK_PASSIVE_RECOVERY));
        RegenerateHero(ReduceHealing(std::max(1u, uint32(m_player->GetMaxHealth() * percent / 100.0f))));
    }

    uint32 const bleedHeal = uint32(m_bleedHeal);
    m_bleedHeal = 0.0f;
    if (bleedHeal && m_player->IsAlive() && m_player->GetHealth() < m_player->GetMaxHealth())
    {
        uint32 const amount = ReduceHealing(bleedHeal);
        SupportScope scope(*this, m_bleedHealSource);
        if (SpellEntry const* spellInfo = sSpellMgr.GetSpellEntry(BONK_SPELL_BLEED_HEAL))
            m_player->DealHeal(m_player, amount, spellInfo);
        else
            m_player->ModifyHealth(int32(amount));
    }
}

void BonkRun::ReportQuestCounts()
{
    if (m_crits != m_critsReported)
    {
        m_critsReported = m_crits;
        sBonkQuests.OnProgress(m_player, BONK_QUEST_CRITS, 0, m_crits, true);
    }
    if (m_healedUnreported)
    {
        sBonkQuests.OnProgress(m_player, BONK_QUEST_HEALED, 0, m_healedUnreported);
        m_healedUnreported = 0;
    }

    auto report = [this](BonkQuestType type, uint32& unreported)
    {
        if (unreported)
            sBonkQuests.OnProgress(m_player, type, 0, unreported);
        unreported = 0;
    };
    if (m_runXpDirty)
        UpdateRunXp(true);

    if (m_player->IsAlive())
        m_minuteLowestHealth = std::min(m_minuteLowestHealth, uint32(m_player->GetHealthPercent()));
    m_minuteClientObjectsPeak = std::max(m_minuteClientObjectsPeak, CountClientObjects());
    if (!m_sandbox && m_totalMs >= m_nextLogMinute * MINUTE * IN_MILLISECONDS)
        LogMinute();
    m_recentHits.erase(std::remove_if(m_recentHits.begin(), m_recentHits.end(),
        [this](std::pair<uint32, uint32> const& hit) { return hit.first + 10 * IN_MILLISECONDS < m_totalMs; }), m_recentHits.end());

    report(BONK_QUEST_LEECH_HEALED, m_leechUnreported);
    report(BONK_QUEST_DODGES, m_dodgesUnreported);
    report(BONK_QUEST_ARMOR_ABSORBED, m_armorUnreported);
    report(BONK_QUEST_PROJECTILES, m_projectilesUnreported);

    uint32 const maxHealth = m_player->GetMaxHealth();
    if (maxHealth > m_maxHealthReported)
    {
        m_maxHealthReported = maxHealth;
        sBonkQuests.OnProgress(m_player, BONK_QUEST_MAX_HEALTH, 0, maxHealth, true);
    }
    uint32 const unhurt = (m_totalMs - (m_hurt ? m_lastHurtMs : 0)) / IN_MILLISECONDS / 10 * 10;
    if (unhurt != m_unhurtReported)
    {
        m_unhurtReported = unhurt;
        if (unhurt)
            sBonkQuests.OnProgress(m_player, BONK_QUEST_UNHURT_TIME, 0, unhurt, true);
    }
    uint32 const overtimeStartMs = (m_map.durationSeconds + BONK_OVERTIME_AFTER_SEC) * IN_MILLISECONDS;
    uint32 const overtime = m_bossSpawned && m_elapsedMs > overtimeStartMs ? (m_elapsedMs - overtimeStartMs) / IN_MILLISECONDS / 10 * 10 : 0;
    if (overtime > m_overtimeReported)
    {
        m_overtimeReported = overtime;
        sBonkQuests.OnProgress(m_player, BONK_QUEST_OVERTIME, 0, overtime, true);
    }
    for (auto const& itr : m_passiveRanks)
    {
        uint32& reported = m_ranksReported[{ false, itr.first }];
        if (itr.second > reported)
        {
            reported = itr.second;
            sBonkQuests.OnProgress(m_player, BONK_QUEST_PASSIVE_RANK, itr.first, itr.second, true);
        }
    }
    for (OwnedWeapon const& owned : m_weapons)
    {
        uint32& reported = m_ranksReported[{ true, owned.weapon.id }];
        if (owned.rank > reported)
        {
            reported = owned.rank;
            sBonkQuests.OnProgress(m_player, BONK_QUEST_SPELL_RANK, owned.weapon.id, owned.rank, true);
        }
    }
}

void BonkRun::UpdateEvents()
{
    uint32 const elapsedSeconds = m_elapsedMs / IN_MILLISECONDS;
    bool const quiet = IsQuietTime();

    if (!quiet && !m_heldEvents.empty())
    {
        std::vector<BonkMapEvent> const held = std::move(m_heldEvents);
        m_heldEvents.clear();
        if (!m_bossSpawned)
            for (BonkMapEvent const& event : held)
            {
                RunEvent(event);
                if (m_state != STATE_RUNNING)
                    return;
            }
    }

    while (m_nextEvent < m_map.events.size() && m_map.events[m_nextEvent].second <= elapsedSeconds)
    {
        BonkMapEvent const& event = m_map.events[m_nextEvent++];
        if (quiet && IsQuietEvent(event.type))
        {
            m_heldEvents.push_back(event);
            continue;
        }
        RunEvent(event);
        if (m_state != STATE_RUNNING)
            return;
    }
}

bool BonkRun::IsQuietEvent(BonkEventType type)
{
    return type == BONK_EVENT_ELITES || type == BONK_EVENT_SWARM || type == BONK_EVENT_RING || type == BONK_EVENT_RUSH;
}

bool BonkRun::IsQuietTime() const
{
    uint32 const second = m_elapsedMs / IN_MILLISECONDS;
    BonkWaveBeat const beat = GetWave().beat;
    if (beat == BONK_BEAT_LULL || beat == BONK_BEAT_MINIBOSS)
        return true;
    return second + BONK_QUIET_SECONDS >= m_map.durationSeconds;
}

void BonkRun::RunEvent(BonkMapEvent const& event)
{
    BonkPlayer* bonk = m_player->GetBonkPlayer();

    switch (event.type)
    {
        case BONK_EVENT_MINIBOSS:
        {
            std::string name;
            uint32 const count = event.count + (event.count ? uint32(GetGlyphValue(BONK_GLYPH_AMBITION, 0)) : 0);
            for (uint32 i = 0; i < count; ++i)
            {
                BonkMapCreature const* creature = PickCreature(BONK_SPAWN_MINIBOSS);
                if (!creature)
                    creature = PickCreature(BONK_SPAWN_ELITE);
                if (creature)
                    if (Creature* mob = SpawnMob(*creature))
                        name = bonk->GetLocalName(mob);
            }
            if (!name.empty())
                bonk->Notify(BONK_STRING_ARRIVED, { name });
            break;
        }
        case BONK_EVENT_ELITES:
        {
            bool spawned = false;
            uint32 const count = uint32(event.count * GetEliteScale() + 0.5f);
            for (uint32 i = 0; i < count; ++i)
                if (BonkMapCreature const* creature = PickCreature(BONK_SPAWN_ELITE))
                    spawned |= SpawnMob(*creature) != nullptr;
            if (spawned)
                bonk->Notify(BONK_STRING_ELITES_ARE_CLOSING_IN);
            break;
        }
        case BONK_EVENT_SWARM:
        {
            PruneMobs();
            uint32 const alive = m_mobs.size();
            uint32 const count = std::min(event.count, m_tier.maxAlive > alive ? m_tier.maxAlive - alive : 0u);
            for (uint32 i = 0; i < count; ++i)
                if (BonkMapCreature const* creature = PickTrash())
                    SpawnMob(*creature);
            if (count)
                bonk->Notify(BONK_STRING_SWARM_IS_COMING);
            break;
        }
        case BONK_EVENT_RING:
        case BONK_EVENT_RUSH:
        {
            if (SpawnFormation(event))
                bonk->Notify(event.type == BONK_EVENT_RING ? BONK_STRING_YOU_ARE_SURROUNDED : BONK_STRING_ENEMIES_ARE_CHARGING_IN);
            break;
        }
        case BONK_EVENT_CHESTS:
        {
            if (IsCoopGuest())
                break;
            uint32 placed = 0;
            for (uint32 i = 0; i < event.count; ++i)
                placed += SpawnChestNearby() ? 1 : 0;
            if (placed)
                bonk->Notify(placed == 1 ? BONK_STRING_CHEST_APPEARED_NEARBY : BONK_STRING_CHESTS_APPEARED_NEARBY);
            break;
        }
        default:
            break;
    }
}

uint32 BonkRun::SpawnFormation(BonkMapEvent const& event)
{
    PruneMobs();
    uint32 const alive = m_mobs.size();
    uint32 const count = std::min(event.count, m_tier.maxAlive > alive ? m_tier.maxAlive - alive : 0u);

    float const side = frand(0.0f, 2.0f * M_PI_F);
    uint32 spawned = 0;
    for (uint32 i = 0; i < count; ++i)
    {
        BonkMapCreature const* creature = PickTrash();
        if (!creature)
            break;

        float angle, distance;
        if (event.type == BONK_EVENT_RING)
        {
            angle = side + 2.0f * M_PI_F * i / count;
            distance = BONK_RING_DISTANCE;
        }
        else
        {
            angle = side + frand(-BONK_RUSH_WIDTH, BONK_RUSH_WIDTH);
            distance = frand(m_map.spawnMinDistance, m_map.spawnMaxDistance);
        }

        float x, y, z;
        if (FindSpawnPositionAt(angle, distance, x, y, z) && SpawnMobAt(*creature, x, y, z))
            ++spawned;
    }
    return spawned;
}

void BonkRun::PruneMobs()
{
    Map* map = m_player->GetMap();
    m_mobs.erase(std::remove_if(m_mobs.begin(), m_mobs.end(), [map](ObjectGuid const& guid)
    {
        Creature* mob = map->GetCreature(guid);
        return !mob || !mob->IsAlive() || mob->HasCreatureState(CSTATE_DESPAWNING);
    }), m_mobs.end());
}

void BonkRun::UpdateSpawns(uint32 diff)
{
    uint32 const elapsedSeconds = m_elapsedMs / IN_MILLISECONDS;

    if (m_bossSpawned)
    {
        Creature* boss = m_player->GetMap()->GetCreature(m_bossGuid);
        if (!boss || (boss->IsAlive() && boss->HasCreatureState(CSTATE_DESPAWNING)))
        {
            m_bossSpawned = false;
            SendBossMusic(false);
        }
    }

    UpdateWaves();
    if (m_state != STATE_RUNNING)
        return;
    UpdateBossVoices();
    UpdateOvertime(diff);
    UpdateLanes(diff);
    UpdateRingSample(diff);
    UpdatePerch(diff);

    bool const bossDue = !IsFinale() && !m_bossSpawned && elapsedSeconds >= m_map.durationSeconds;
    if (bossDue && IsCoopGuest())
        m_bossSpawned = true;
    else if (bossDue)
    {
        BonkMapCreature const* boss = PickCreature(BONK_SPAWN_BOSS);
        if (!boss)
        {
            OpenPortal(nullptr);
            return;
        }

        if (Creature* mob = SpawnMob(*boss))
        {
            m_bossSpawned = true;
            m_player->GetBonkPlayer()->Notify(BONK_STRING_ARRIVED, { m_player->GetBonkPlayer()->GetLocalName(mob) });
        }
    }

    m_spawnTimer -= int32(diff);
    m_eliteTimer -= int32(diff);
    if (m_spawnTimer > 0 && m_eliteTimer > 0)
        return;

    PruneMobs();

    if (m_eliteTimer <= 0)
    {
        uint32 const interval = GetEliteIntervalMs();
        if (!interval || IsQuietTime() || m_bossSpawned || m_mobs.size() >= m_tier.maxAlive)
            m_eliteTimer = int32(BONK_ELITE_RETRY_MS);
        else
        {
            m_eliteTimer += int32(interval);
            float x, y, z;
            if (BonkMapCreature const* elite = PickCreature(BONK_SPAWN_ELITE))
                if (FindMobPosition(x, y, z, m_map.spawnMinDistance, m_map.spawnMaxDistance))
                    EmergeMobAt(*elite, x, y, z, false, urand(0, BONK_SPAWN_TICK_MS));
        }
    }

    if (m_spawnTimer <= 0)
    {
        m_spawnTimer += int32(BONK_SPAWN_TICK_MS);

        uint32 const target = GetSpawnTarget();
        uint32 const trash = (m_stress.on ? CountTrash() : CountRoster()) + m_emerging.size();
        uint32 const alive = m_mobs.size() + m_emerging.size();
        uint32 const fullPerTick = GetWave().spawnPerTick ? GetWave().spawnPerTick : BONK_SPAWN_PER_TICK;
        uint32 const perTick = fullPerTick > BONK_OPENING_REFILL_MIN ?
            BONK_OPENING_REFILL_MIN + uint32((fullPerTick - BONK_OPENING_REFILL_MIN) * GetOpeningProgress() + 0.5f) : fullPerTick;
        uint32 count = target > trash ? target - trash : 0;
        count = std::min(count, m_tier.maxAlive > alive ? m_tier.maxAlive - alive : 0u);
        count = std::min(count, m_stress.on ? BONK_STRESS_SPAWN_PER_TICK : perTick);
        SpawnTrashPacks(count);
    }
}

BonkWave const& BonkRun::GetWave() const
{
    std::vector<BonkWave> const& waves = sBonkMgr.GetWaves();
    return waves[std::min(m_waveIndex, waves.size() - 1)];
}

bool BonkRun::IsInRoster(BonkArchetype archetype) const
{
    if (m_stress.on)
        return true;
    if (archetype >= MAX_BONK_ARCHETYPE)
        return false;
    BonkWave const& wave = GetWave();
    if (m_stage > 1 && wave.beat == BONK_BEAT_BUILD && archetype != BONK_ARCHETYPE_SWARMER)
        return true;
    return wave.roster[archetype] > 0;
}

uint32 BonkRun::CountRoster() const
{
    Map* map = m_player->GetMap();
    uint32 count = 0;
    for (ObjectGuid const& guid : m_mobs)
        if (Creature const* mob = map->GetCreature(guid))
            if (BonkMobAI const* ai = dynamic_cast<BonkMobAI const*>(mob->AI()))
                if (ai->GetKind() == BONK_SPAWN_NORMAL && !ai->IsFlyer() && IsInRoster(ai->GetArchetype()))
                    ++count;
    return count;
}

float BonkRun::GetCurveMinAlive() const
{
    std::vector<BonkWave> const& waves = sBonkMgr.GetWaves();
    BonkWave const& wave = GetWave();
    float const second = m_elapsedMs / float(IN_MILLISECONDS);
    float const end = m_waveIndex + 1 < waves.size() ? float(waves[m_waveIndex + 1].startSecond) : float(m_map.durationSeconds);
    float const span = end - float(wave.startSecond);
    float minAlive = wave.crowdStart;
    if (span > 0.0f)
        minAlive += (wave.crowdEnd - wave.crowdStart) * std::min(1.0f, std::max(0.0f, (second - wave.startSecond) / span));
    minAlive = std::max(minAlive, GetStageCrowdFloor()) * GetStageCrowdScale();
    return minAlive * m_tier.minAliveMultiplier * (1.0f + m_bonuses.spawnRate / 100.0f) * (1.0f + GetGreedCrowdPercent() / 100.0f) *
        GetOpeningEase() * (m_invaderId ? BONK_INVASION_CROWD : 1.0f);
}

float BonkRun::GetStageCrowdFloor() const
{
    if (m_stage <= 1)
        return 0.0f;
    float peak = 0.0f;
    for (BonkWave const& wave : sBonkMgr.GetWaves())
        peak = std::max(peak, wave.crowdEnd);
    float const carry = peak * BONK_STAGE_CARRY_SHARE;
    switch (GetWave().beat)
    {
        case BONK_BEAT_BUILD:    return carry;
        case BONK_BEAT_LULL:
        case BONK_BEAT_MINIBOSS: return carry * BONK_STAGE_CALM_SHARE;
        default:                 return 0.0f;
    }
}

float BonkRun::GetStageCrowdScale() const
{
    if (IsMythic())
        return 1.0f;
    return 1.0f + BONK_STAGE_CROWD_GROWTH * float(std::min(m_stage > 1 ? m_stage - 1 : 0, BONK_STAGE_CROWD_GROWTH_STAGES));
}

float BonkRun::GetOpeningProgress() const
{
    if (m_stage != 1 || m_stress.on || m_elapsedMs >= m_tier.openingMs)
        return 1.0f;
    return m_elapsedMs / float(m_tier.openingMs);
}

float BonkRun::GetOpeningEase() const
{
    return m_tier.openingCrowd + (1.0f - m_tier.openingCrowd) * GetOpeningProgress();
}

void BonkRun::UpdateWaves()
{
    std::vector<BonkWave> const& waves = sBonkMgr.GetWaves();
    uint32 const second = m_elapsedMs / IN_MILLISECONDS;
    size_t index = m_waveIndex;
    while (index + 1 < waves.size() && waves[index + 1].startSecond <= second)
        ++index;
    if (!m_waveEntered || index != m_waveIndex)
    {
        m_waveEntered = true;
        EnterWave(index);
    }

    size_t const next = m_waveIndex + 1;
    if (next < waves.size() && m_waveWarned < next && second + BONK_BEAT_WARN_SECONDS >= waves[next].startSecond)
    {
        m_waveWarned = next;
        BonkWave const& coming = waves[next];
        if (coming.beat == BONK_BEAT_WALL || coming.beat == BONK_BEAT_MINIBOSS)
            m_player->GetBonkPlayer()->SendAddon("beat~" + Localize(BONK_FIELD_WAVE_NAME, coming.startSecond, coming.name) + "~" + std::to_string(coming.beat) + "~" +
                std::to_string(coming.startSecond > second ? coming.startSecond - second : 0));
    }
}

void BonkRun::EnterWave(size_t index)
{
    m_waveIndex = index;
    BonkWave const& wave = GetWave();
    BonkPlayer* bonk = m_player->GetBonkPlayer();

    m_miniEventSlots = wave.miniEvents;
    m_nextMiniEventMs = m_elapsedMs;
    if (wave.eliteIntervalSeconds)
        m_eliteTimer = int32(GetEliteIntervalMs());
    m_spawnTimer = std::min(m_spawnTimer, 0);

    if (m_bossSpawned || m_stress.on)
        return;

    std::string name;
    uint32 const minibosses = wave.minibosses + (wave.minibosses ? uint32(GetGlyphValue(BONK_GLYPH_AMBITION, 0)) : 0);
    for (uint32 i = 0; i < minibosses; ++i)
    {
        BonkMapCreature const* creature = PickCreature(BONK_SPAWN_MINIBOSS);
        if (!creature)
            creature = PickCreature(BONK_SPAWN_ELITE);
        if (creature)
            if (Creature* mob = SpawnMob(*creature))
                name = bonk->GetLocalName(mob);
    }
    if (!name.empty())
        bonk->Notify(BONK_STRING_ARRIVED, { name });

    bool elites = false;
    uint32 const eliteCount = uint32(wave.elites * GetEliteScale() + 0.5f);
    for (uint32 i = 0; i < eliteCount; ++i)
        if (BonkMapCreature const* creature = PickCreature(BONK_SPAWN_ELITE))
            elites |= SpawnMob(*creature) != nullptr;

    if (wave.beat == BONK_BEAT_WALL)
        bonk->Notify(BONK_STRING_EXCLAIM, { Localize(BONK_FIELD_WAVE_NAME, wave.startSecond, wave.name) });
    else if (elites)
        bonk->Notify(BONK_STRING_ELITES_ARE_CLOSING_IN);
    if (wave.beat == BONK_BEAT_WALL || wave.beat == BONK_BEAT_MINIBOSS)
        bonk->SendAddon("beat~" + Localize(BONK_FIELD_WAVE_NAME, wave.startSecond, wave.name) + "~" + std::to_string(wave.beat) + "~0");
}

std::string BonkRun::DescribeTimelineMarks() const
{
    std::string marks;
    for (BonkWave const& wave : sBonkMgr.GetWaves())
        if (wave.beat == BONK_BEAT_WALL || wave.beat == BONK_BEAT_MINIBOSS)
        {
            std::string name = Localize(BONK_FIELD_WAVE_NAME, wave.startSecond, wave.name);
            name.erase(std::remove_if(name.begin(), name.end(), [](char c) { return c == ':' || c == ',' || c == '~'; }), name.end());
            marks += (marks.empty() ? "" : ",") + std::to_string(wave.startSecond) + ":" + std::to_string(wave.beat) + ":" + name;
        }
    return marks;
}

void BonkRun::LogMinute()
{
    float const maxHealth = std::max(1.0f, float(m_player->GetMaxHealth()));
    sLog.Player(m_player->GetSession(), LOG_LEVELUP, LOG_LVL_BASIC,
        "Bonk minute: %s at run minute %u (stage %u, %s, %s, class %u): level %u, %u kills (+%u), took %u damage (%.0f%% of max health), "
        "healed %u (%.0f%%), lowest health %u%%, %u enemies alive, block \"%s\"; client objects peak %u, %s; packets %u/s (%u KB/s); gold earned %u; valor earned %u, on chests %u, in shops %u, on enchants %u, left %u, chests bought %u; %s",
        m_player->GetName(), m_nextLogMinute, m_stage, m_map.key.c_str(), m_tier.key.c_str(), uint32(m_player->GetClass()), m_player->GetLevel() + m_overcapLevel,
        m_kills, m_kills - m_minuteKills, m_minuteDamage, m_minuteDamage * 100.0f / maxHealth, m_minuteHealed, m_minuteHealed * 100.0f / maxHealth,
        m_minuteLowestHealth, uint32(m_mobs.size()), GetWave().name.c_str(), m_minuteClientObjectsPeak, DescribeClientObjects().c_str(),
        m_minutePackets / std::max(1u, m_minutePacketSeconds), m_minutePacketBytes / 1024 / std::max(1u, m_minutePacketSeconds), m_goldEarned,
        m_valorEarned, m_valorOnChests, m_valorInShops, m_valorOnEnchants, m_coins, m_chestsBought, DescribeDefense().c_str());
    m_minutePackets = 0;
    m_minutePacketBytes = 0;
    m_minutePacketSeconds = 0;
    LogBuild();
    LogRing();
    ++m_nextLogMinute;
    m_minuteClientObjectsPeak = 0;
    m_minuteDamage = 0;
    m_minuteHealed = 0;
    m_minuteKills = m_kills;
    m_minuteLowestHealth = m_player->IsAlive() ? uint32(m_player->GetHealthPercent()) : 0;
}

uint32 BonkRun::CountClientObjects() const
{
    std::shared_lock<std::shared_timed_mutex> lock(m_player->m_visibleGUIDs_lock);
    return uint32(m_player->m_visibleGUIDs.size());
}

void BonkRun::SamplePerfSecond()
{
    PerfSecond second;
    second.runSecond = m_totalMs / IN_MILLISECONDS;
    second.clientObjects = CountClientObjects();
    second.mobs = uint32(m_mobs.size());
    second.orbs = uint32(m_orbs.size());
    second.pulledOrbs = uint32(std::count_if(m_orbs.begin(), m_orbs.end(), [](Orb const& orb) { return orb.pulled; }));
    second.standIns = uint32(m_telegraphs.CountStandIns());
    second.busyStandIns = uint32(m_telegraphs.CountBusyStandIns());
    second.telegraphs = uint32(m_telegraphs.Count());
    second.hits = m_secondHits;
    second.casts = m_secondCasts;
    second.pendingCasts = uint32(m_pendingCasts.size());
    second.pets = uint32(GetAllPets(false).size());
    BonkPlayer::PacketCounts const packets = m_player->GetBonkPlayer()->TakePacketCounts();
    if (m_packetsPrimed)
    {
        second.packets = packets.packets;
        second.bytes = packets.bytes;
        for (auto const& kind : packets.top)
            second.topPackets += (second.topPackets.empty() ? "" : ", ") + std::string(LookupOpcodeName(kind.first)) + " x" + std::to_string(kind.second);
        m_minutePackets += packets.packets;
        m_minutePacketBytes += packets.bytes;
        ++m_minutePacketSeconds;
    }
    m_packetsPrimed = true;
    m_secondHits = 0;
    m_secondCasts = 0;
    m_perfSeconds.push_back(second);
    if (m_perfSeconds.size() > BONK_PERF_SECONDS_KEPT)
        m_perfSeconds.erase(m_perfSeconds.begin());
}

void BonkRun::LogPerfDip()
{
    if (m_perfDipLogged && m_totalMs - m_perfDipLoggedMs < BONK_PERF_DIP_GAP_MS)
        return;
    m_perfDipLogged = true;
    m_perfDipLoggedMs = m_totalMs;

    sLog.Out(LOG_PERFORMANCE, LOG_LVL_BASIC, "Client frame dip: %s at run minute %.2f (stage %u, %s, %s, class %u, level %u), newest second first:",
        m_player->GetName(), GetRunMinutes(), m_stage, m_map.key.c_str(), m_tier.key.c_str(), uint32(m_player->GetClass()),
        m_player->GetLevel() + m_overcapLevel);
    for (auto itr = m_perfSeconds.rbegin(); itr != m_perfSeconds.rend(); ++itr)
        sLog.Out(LOG_PERFORMANCE, LOG_LVL_BASIC, "  run %u:%02u: client objects %u, mobs %u, orbs %u (%u pulled), stand-ins %u (%u busy), "
            "telegraphs %u, hits %u, casts %u, pending casts %u, pets %u; packets %u (%u KB): %s",
            itr->runSecond / 60, itr->runSecond % 60, itr->clientObjects, itr->mobs, itr->orbs, itr->pulledOrbs, itr->standIns,
            itr->busyStandIns, itr->telegraphs, itr->hits, itr->casts, itr->pendingCasts, itr->pets, itr->packets, itr->bytes / 1024,
            itr->topPackets.c_str());
}

std::string BonkRun::DescribeClientObjects() const
{
    uint32 total = 0, enemies = 0, gameObjects = 0, dynamicObjects = 0, players = 0;
    std::map<uint32, uint32> helpers;
    {
        std::shared_lock<std::shared_timed_mutex> lock(m_player->m_visibleGUIDs_lock);
        total = uint32(m_player->m_visibleGUIDs.size());
        for (ObjectGuid const& guid : m_player->m_visibleGUIDs)
        {
            if (guid.IsCreatureOrPet())
            {
                if (std::find(m_mobs.begin(), m_mobs.end(), guid) != m_mobs.end())
                    ++enemies;
                else
                    ++helpers[guid.GetEntry()];
            }
            else if (guid.IsGameObject())
                ++gameObjects;
            else if (guid.IsDynamicObject())
                ++dynamicObjects;
            else if (guid.IsPlayer())
                ++players;
        }
    }

    std::vector<std::pair<uint32, uint32>> ranked(helpers.begin(), helpers.end());
    std::sort(ranked.begin(), ranked.end(), [](auto const& a, auto const& b) { return a.second > b.second; });
    uint32 helperCount = 0;
    for (auto const& helper : ranked)
        helperCount += helper.second;

    std::string top;
    for (size_t i = 0; i < ranked.size() && i < 5; ++i)
        top += (top.empty() ? "" : ", ") + std::to_string(ranked[i].first) + " x" + std::to_string(ranked[i].second);

    std::ostringstream text;
    text << "now " << total << " (" << enemies << " enemies, " << helperCount << " other creatures [" << top << "], "
        << gameObjects << " gameobjects, " << dynamicObjects << " dynamic objects, " << players << " players; "
        << m_telegraphs.CountStandIns() << " stand-ins, " << m_orbs.size() << " orbs, " << m_projectiles.size() << " projectiles)";
    return text.str();
}

void BonkRun::LogDeath()
{
    uint32 recent = 0;
    for (auto const& hit : m_recentHits)
        if (hit.first + 10 * IN_MILLISECONDS >= m_totalMs)
            recent += hit.second;
    sLog.Player(m_player->GetSession(), LOG_LEVELUP, LOG_LVL_BASIC,
        "Bonk death: %s at run minute %.2f (stage %u, %s, %s, class %u), level %u, %u kills: last hit by %s for %u; "
        "%u damage in the last 10 s from %u hits (%.0f%% of max health); %u enemies alive, block \"%s\"; %s",
        m_player->GetName(), GetRunMinutes(), m_stage, m_map.key.c_str(), m_tier.key.c_str(), uint32(m_player->GetClass()),
        m_player->GetLevel() + m_overcapLevel, m_kills, m_lastStriker.empty() ? "?" : m_lastStriker.c_str(), m_lastStrikeDamage, recent,
        uint32(m_recentHits.size()), recent * 100.0f / std::max(1.0f, float(m_player->GetMaxHealth())), uint32(m_mobs.size()), GetWave().name.c_str(), DescribeDefense().c_str());
}

std::string BonkRun::DescribeDefense() const
{
    float const maxHealth = std::max(1.0f, float(m_player->GetMaxHealth()));
    float const armor = std::max(0.0f, GetStat(BONK_PASSIVE_ARMOR));
    float const reduction = GetArmorReduction();
    float const damageScale = GetStageDamageMultiplier();
    float const gruntHit = GetScalingNow().hitShare * damageScale * GetHeroBaseHealth() * (1.0f - reduction);
    char text[256];
    snprintf(text, sizeof(text), "defense: max health %.0f, armor %.0f (%.1f%% reduction, +%.1f%% health), enemy damage x%.2f, grunt hit %.1f%% of max health",
        maxHealth, armor, reduction * 100.0f, GetArmorOverflowHealth(), damageScale, gruntHit * 100.0f / maxHealth);
    return text;
}

void BonkRun::LogLoadout() const
{
    sLog.Player(m_player->GetSession(), LOG_LEVELUP, LOG_LVL_BASIC,
        "Bonk loadout: %s starts a run at run minute 0 (stage %u, %s, %s, class %u): %s; difficulty level %u",
        m_player->GetName(), m_stage, m_map.key.c_str(), m_tier.key.c_str(), uint32(m_player->GetClass()),
        m_player->GetBonkPlayer()->DescribeLoadout().c_str(), m_difficultyLevel);
}

void BonkRun::LogBuild() const
{
    sLog.Player(m_player->GetSession(), LOG_LEVELUP, LOG_LVL_BASIC,
        "Bonk build: %s at run minute %.2f (stage %u, %s, %s, class %u): level %u; %s; power %.0f%%, gear power %.0f%% (x%.2f)",
        m_player->GetName(), GetRunMinutes(), m_stage, m_map.key.c_str(), m_tier.key.c_str(), uint32(m_player->GetClass()),
        m_player->GetLevel() + m_overcapLevel, DescribeBuildTotals().c_str(), GetRunPower() + GetGlyphPower() + GetTalentDamagePercent(),
        GetGearTotal(BONK_PASSIVE_POWER), GetGearDamageScale());
}

void BonkRun::LogRunEnd(bool wentHome, std::string const& build) const
{
    char const* outcome = m_victory ? "victory" : wentHome ? "home" : m_player->IsAlive() ? "stopped" : "died";
    sLog.Player(m_player->GetSession(), LOG_LEVELUP, LOG_LVL_BASIC,
        "Bonk end: %s at run minute %.2f (stage %u, %s, %s, class %u): %s, level %u, %u kills, gold earned %u, tickets earned %u, scrap earned %u; %s; valor earned %u, on chests %u, in shops %u, on enchants %u, chests bought %u",
        m_player->GetName(), GetRunMinutes(), m_stage, m_map.key.c_str(), m_tier.key.c_str(), uint32(m_player->GetClass()), outcome,
        m_player->GetLevel() + m_overcapLevel, m_kills, m_goldEarned, m_shardsEarned, m_scrapEarned, build.c_str(),
        m_valorEarned, m_valorOnChests, m_valorInShops, m_valorOnEnchants, m_chestsBought);
}

std::string BonkRun::DescribeBoardEntry() const
{
    char text[96];
    snprintf(text, sizeof(text), "coop %u, gm %u", BonkCoop::IsInCoopGroup(m_player) ? 1u : 0u, m_player->GetSession()->GetSecurity() > SEC_PLAYER ? 1u : 0u);
    return text;
}

void BonkRun::LogLevelClear() const
{
    sLog.Player(m_player->GetSession(), LOG_LEVELUP, LOG_LVL_BASIC,
        "Bonk clear: %s clears level %u at run minute %.2f in %u ms (stage %u, %s, %s, class %u): %s",
        m_player->GetName(), m_difficultyLevel, GetRunMinutes(), m_totalMs, m_stage, m_map.key.c_str(), m_tier.key.c_str(),
        uint32(m_player->GetClass()), DescribeBoardEntry().c_str());
}

void BonkRun::LogFinale(float minutes, uint32 score) const
{
    sLog.Player(m_player->GetSession(), LOG_LEVELUP, LOG_LVL_BASIC,
        "Bonk finale: %s holds out %.2f minutes on level %u at run minute %.2f, score %u (stage %u, %s, %s, class %u): %s",
        m_player->GetName(), minutes, m_difficultyLevel, GetRunMinutes(), score, m_stage, m_map.key.c_str(), m_tier.key.c_str(),
        uint32(m_player->GetClass()), DescribeBoardEntry().c_str());
}

void BonkRun::LogBossFight(Creature const* mob, BonkSpawnKind kind, BossArrival const& arrival)
{
    uint32 const taken = m_damageTaken - arrival.damageTaken;
    sLog.Player(m_player->GetSession(), LOG_LEVELUP, LOG_LVL_BASIC,
        "Bonk boss: %s kills %s (%s) at run minute %.2f (stage %u, %s, %s, class %u), level %u: %.1f s after it arrived; "
        "took %u damage meanwhile (%.0f%% of max health), health now %u%%; client objects %s",
        m_player->GetName(), mob->GetName(), kind == BONK_SPAWN_BOSS ? "boss" : "mini-boss", GetRunMinutes(), m_stage, m_map.key.c_str(),
        m_tier.key.c_str(), uint32(m_player->GetClass()), m_player->GetLevel() + m_overcapLevel, (m_totalMs - arrival.ms) / 1000.0f, taken,
        taken * 100.0f / std::max(1.0f, float(m_player->GetMaxHealth())), uint32(m_player->GetHealthPercent()), DescribeClientObjects().c_str());
}

uint32 BonkRun::GetFirstMinibossSecond() const
{
    for (BonkWave const& wave : sBonkMgr.GetWaves())
        if (wave.minibosses)
            return wave.startSecond;
    return 0;
}

void BonkRun::UpdateOvertime(uint32 diff)
{
    uint32 const startSecond = IsFinale() ? m_map.durationSeconds : m_map.durationSeconds + BONK_OVERTIME_AFTER_SEC;
    if ((!m_bossSpawned && !IsFinale()) || m_stress.on || m_elapsedMs < startSecond * IN_MILLISECONDS)
        return;
    m_overtimeTimer -= int32(diff);
    if (m_overtimeTimer > 0)
        return;
    m_overtimeTimer = int32(BONK_OVERTIME_INTERVAL_MS);

    BonkMapEvent ring;
    ring.second = m_elapsedMs / IN_MILLISECONDS;
    ring.type = BONK_EVENT_RING;
    ring.count = BONK_OVERTIME_RING + BONK_OVERTIME_RING_STEP * m_overtimeWaves;
    m_spawnDamageScale = 1.0f + BONK_OVERTIME_DAMAGE_STEP * (m_overtimeWaves + 1);
    uint32 const spawned = SpawnFormation(ring);
    m_spawnDamageScale = 1.0f;
    if (!spawned)
        return;
    if (!m_overtimeWaves)
        m_player->GetBonkPlayer()->Notify(BONK_STRING_OVERTIME_ENEMIES_KEEP_COMING_UNTIL);
    ++m_overtimeWaves;
}

BonkScalingPoint BonkRun::GetScalingNow() const
{
    std::vector<BonkScalingPoint> const& points = sBonkMgr.GetScaling();
    float const minute = m_scalingMinuteOverride >= 0.0f ? m_scalingMinuteOverride :
        m_sandbox && m_sandboxMinute >= 0.0f ? m_sandboxMinute : GetRunMinutes();
    size_t index = 0;
    while (index + 1 < points.size() && points[index + 1].minute <= minute)
        ++index;
    BonkScalingPoint const& from = points[index];
    BonkScalingPoint const* to = index + 1 < points.size() ? &points[index + 1] : nullptr;
    BonkScalingPoint now = from;
    now.minute = minute;
    if (!to && index > 0)
    {
        BonkScalingPoint const& before = points[index - 1];
        float const span = from.minute - before.minute;
        if (span > 0.0f)
        {
            float const t = (minute - from.minute) / span;
            now.hitShare += (from.hitShare - before.hitShare) * t;
            now.health += (from.health - before.health) * t;
            now.healthNormal += (from.healthNormal - before.healthNormal) * t;
        }
    }
    else if (to)
    {
        float const span = to->minute - from.minute;
        float const t = span > 0.0f ? std::min(1.0f, std::max(0.0f, (minute - from.minute) / span)) : 0.0f;
        now.hitShare = from.hitShare + (to->hitShare - from.hitShare) * t;
        now.health = from.health + (to->health - from.health) * t;
        now.healthNormal = from.healthNormal + (to->healthNormal - from.healthNormal) * t;
    }
    else
        now.minute = from.minute;
    if (m_tier.key == "normal")
        now.health = now.healthNormal;
    return now;
}

float BonkRun::GetHeroBaseHealth() const
{
    PlayerLevelInfo info;
    sObjectMgr.GetPlayerLevelInfo(m_player->GetRace(), m_player->GetClass(), m_player->GetLevel(), &info);
    float const stamina = float(info.stats[STAT_STAMINA]);
    float const fromStamina = std::min(stamina, 20.0f) + std::max(stamina - 20.0f, 0.0f) * 10.0f;
    return std::max(1.0f, float(m_player->GetCreateHealth()) + fromStamina);
}

uint32 BonkRun::GetSpawnTarget() const
{
    if (m_stress.on)
        return m_tier.maxAlive;
    float target = std::min(GetCurveMinAlive(), float(m_tier.maxAlive));
    if (m_bossSpawned)
        target *= BONK_BOSS_CROWD;
    target *= 1.0f + BONK_COOP_SPAWN_PER_PLAYER * float(m_coopPlayers - 1);
    return uint32(target + 0.5f);
}

float BonkRun::GetCrowdOverflow() const
{
    return std::max(1.0f, GetCurveMinAlive() / float(m_tier.maxAlive));
}

uint32 BonkRun::GetEliteIntervalMs() const
{
    uint32 const interval = GetWave().eliteIntervalSeconds;
    if (!interval)
        return 0;
    return uint32(interval * IN_MILLISECONDS / GetEliteScale());
}

float BonkRun::GetEliteScale() const
{
    return (1.0f + m_bonuses.eliteRate / 100.0f) * (1.0f + GetGlyphValue(BONK_GLYPH_HUNT, 0) / 100.0f);
}

BonkMapCreature const* BonkRun::PickTrash() const
{
    if (m_stress.on)
        return PickCreature(BONK_SPAWN_NORMAL);

    BonkWave const& wave = GetWave();
    uint32 weights[MAX_BONK_ARCHETYPE] = {};
    uint32 total = 0;
    bool const bruiserRoom = !m_bossSpawned && CountBruisers() < GetMaxBruisers();
    for (uint8 archetype = 0; archetype < MAX_BONK_ARCHETYPE; ++archetype)
    {
        uint32 weight = wave.roster[archetype];
        if (!weight && IsInRoster(BonkArchetype(archetype)))
            weight = 15;
        if (archetype == BONK_ARCHETYPE_BRUISER && !bruiserRoom)
            weight = 0;
        weights[archetype] = weight;
        total += weight;
    }

    for (uint32 attempt = 0; attempt < 3 && total; ++attempt)
    {
        uint32 roll = urand(1, total);
        for (uint8 archetype = 0; archetype < MAX_BONK_ARCHETYPE; ++archetype)
        {
            if (roll > weights[archetype])
            {
                roll -= weights[archetype];
                continue;
            }
            if (BonkMapCreature const* creature = PickCreature(BONK_SPAWN_NORMAL, archetype))
                return creature;
            total -= weights[archetype];
            weights[archetype] = 0;
            break;
        }
    }
    return nullptr;
}

uint32 BonkRun::GetMaxBruisers() const
{
    uint32 const wave = GetWave().maxBruisers;
    uint32 const cap = wave ? wave : m_tier.maxBruisers;
    return std::max(1u, uint32(cap * GetOpeningEase() + 0.5f));
}

uint32 BonkRun::CountTrash() const
{
    Map* map = m_player->GetMap();
    uint32 count = 0;
    for (ObjectGuid const& guid : m_mobs)
        if (Creature const* mob = map->GetCreature(guid))
            if (BonkMobAI const* ai = dynamic_cast<BonkMobAI const*>(mob->AI()))
                if (ai->GetKind() == BONK_SPAWN_NORMAL)
                    ++count;
    return count;
}

uint32 BonkRun::CountBruisers() const
{
    Map* map = m_player->GetMap();
    uint32 count = 0;
    for (ObjectGuid const& guid : m_mobs)
        if (Creature const* mob = map->GetCreature(guid))
            if (mob->IsAlive())
                if (BonkMobAI const* ai = dynamic_cast<BonkMobAI const*>(mob->AI()))
                    if (ai->GetKind() == BONK_SPAWN_NORMAL && ai->GetArchetype() == BONK_ARCHETYPE_BRUISER && !ai->IsFlyer())
                        ++count;
    return count;
}

BonkMapCreature const* BonkRun::PickCreature(BonkSpawnKind kind, int32 archetype) const
{
    uint32 const elapsedSeconds = m_elapsedMs / IN_MILLISECONDS;

    std::vector<BonkMapCreature const*> candidates;
    uint32 totalWeight = 0;
    for (bool windows : { true, false })
    {
        for (auto const& creature : m_map.creatures)
        {
            if (creature.kind != kind || creature.flyer)
                continue;
            if (windows && (elapsedSeconds < creature.minSecond || (creature.maxSecond && elapsedSeconds > creature.maxSecond)))
                continue;
            if (kind == BONK_SPAWN_NORMAL && archetype >= 0 && int32(creature.archetype) != archetype)
                continue;
            if (creature.maxAlive && CountAlive(creature.entry) >= creature.maxAlive)
                continue;
            candidates.push_back(&creature);
            totalWeight += creature.weight;
        }
        if (totalWeight || kind != BONK_SPAWN_NORMAL || archetype < 0)
            break;
    }

    if (!totalWeight)
        return nullptr;

    uint32 roll = urand(1, totalWeight);
    for (BonkMapCreature const* creature : candidates)
    {
        if (roll <= creature->weight)
            return creature;
        roll -= creature->weight;
    }
    return nullptr;
}

uint32 BonkRun::CountAlive(uint32 entry) const
{
    Map* map = m_player->GetMap();
    uint32 count = 0;
    for (ObjectGuid const& guid : m_mobs)
        if (guid.GetEntry() == entry)
            if (Creature const* mob = map->GetCreature(guid))
                if (mob->IsAlive() && !mob->HasCreatureState(CSTATE_DESPAWNING))
                    ++count;
    return count;
}

bool BonkRun::FindSpawnPosition(float& x, float& y, float& z, float minDistance, float maxDistance) const
{
    for (uint32 attempt = 0; attempt < 10; ++attempt)
        if (FindSpawnPositionAt(frand(0.0f, 2.0f * M_PI_F), frand(minDistance, maxDistance), x, y, z))
            return true;
    return false;
}

bool BonkRun::FindSpawnPositionAt(float angle, float distance, float& x, float& y, float& z) const
{
    Map const* map = m_player->GetMap();
    bool const fromGround = m_perched && m_hasGround;
    float const playerZ = fromGround ? m_groundZ : m_player->GetPositionZ();

    x = m_player->GetPositionX() + distance * std::cos(angle);
    y = m_player->GetPositionY() + distance * std::sin(angle);
    z = map->GetHeight(x, y, playerZ + BONK_SPAWN_MAX_HEIGHT_DIFF, true, 2.0f * BONK_SPAWN_MAX_HEIGHT_DIFF);

    if (z <= INVALID_HEIGHT || std::fabs(z - playerZ) > BONK_SPAWN_MAX_HEIGHT_DIFF)
        return false;

    if (m_map.zoneId && map->GetTerrain()->GetZoneId(x, y, z) != m_map.zoneId)
        return false;

    if (fromGround)
        return FindSpawnPathFrom(m_groundX, m_groundY, m_groundZ, distance, x, y, z);
    if (FindSpawnPathFrom(m_player->GetPositionX(), m_player->GetPositionY(), m_player->GetPositionZ(), distance, x, y, z))
        return true;
    return m_hasGround && FindSpawnPathFrom(m_groundX, m_groundY, m_groundZ, distance, x, y, z);
}

bool BonkRun::FindSpawnPathFrom(float fromX, float fromY, float fromZ, float distance, float& x, float& y, float& z) const
{
    PathFinder path(m_player);
    path.ExcludeSteepSlopes();
    path.calculate(Vector3(fromX, fromY, fromZ), Vector3(x, y, z));
    if (path.getPathType() & PATHFIND_NOT_USING_PATH)
        return true;
    if (!(path.getPathType() & PATHFIND_NORMAL) || path.Length() > distance * BONK_SPAWN_MAX_DETOUR)
        return false;

    Vector3 const end = path.getActualEndPosition();
    if ((end.x - x) * (end.x - x) + (end.y - y) * (end.y - y) > BONK_SPAWN_SNAP_DISTANCE * BONK_SPAWN_SNAP_DISTANCE)
        return false;

    x = end.x;
    y = end.y;
    z = end.z;
    return true;
}

void BonkRun::UpdateHeading(uint32 diff)
{
    m_headingTimer += diff;
    if (m_headingTimer < BONK_HEADING_SAMPLE_MS)
        return;
    float const seconds = m_headingTimer / float(IN_MILLISECONDS);
    m_headingTimer = 0;

    float const x = m_player->GetPositionX();
    float const y = m_player->GetPositionY();
    float const dx = x - m_lastX;
    float const dy = y - m_lastY;
    m_lastX = x;
    m_lastY = y;

    if (dx * dx + dy * dy > BONK_HEADING_MAX_STEP * BONK_HEADING_MAX_STEP)
    {
        m_moveX = m_moveY = 0.0f;
        return;
    }

    float const blend = std::min(1.0f, seconds / BONK_HEADING_SMOOTH_SEC);
    m_moveX += (dx / seconds - m_moveX) * blend;
    m_moveY += (dy / seconds - m_moveY) * blend;
}

bool BonkRun::GetHeading(float& angle) const
{
    if (m_moveX * m_moveX + m_moveY * m_moveY < BONK_HEADING_MIN_SPEED * BONK_HEADING_MIN_SPEED)
        return false;
    angle = std::atan2(m_moveY, m_moveX);
    return true;
}

float BonkRun::GetForwardAngle() const
{
    float angle;
    return GetHeading(angle) ? angle : m_player->GetOrientation();
}

bool BonkRun::IsAhead(float x, float y, float forward) const
{
    float const dx = x - m_player->GetPositionX();
    float const dy = y - m_player->GetPositionY();
    if (dx * dx + dy * dy > BONK_FRONT_RANGE * BONK_FRONT_RANGE)
        return false;
    return AngleBetween(std::atan2(dy, dx), forward) <= BONK_FRONT_ARC;
}

uint32 BonkRun::CountAhead(float forward) const
{
    Map* map = m_player->GetMap();
    uint32 count = 0;
    for (ObjectGuid const& guid : m_mobs)
        if (Creature const* mob = map->GetCreature(guid))
            if (mob->IsAlive() && !mob->HasCreatureState(CSTATE_DESPAWNING) && IsAhead(mob->GetPositionX(), mob->GetPositionY(), forward))
                ++count;
    return count;
}

uint32 BonkRun::GetFrontCap() const
{
    return std::max(BONK_FRONT_MIN, uint32(m_mobs.size() * BONK_FRONT_SHARE + 0.5f));
}

bool BonkRun::FindMobPosition(float& x, float& y, float& z, float minDistance, float maxDistance) const
{
    std::vector<float> const crowd = CountSectorCrowd();
    for (uint32 attempt = 0; attempt < BONK_SECTOR_TRIES; ++attempt)
    {
        int32 const sector = PickSector(crowd, false);
        if (sector < 0)
            break;
        if (FindSectorPosition(uint32(sector), minDistance, maxDistance, x, y, z))
            return true;
    }
    return FindSpawnPosition(x, y, z, minDistance, maxDistance);
}

void BonkRun::UpdatePerch(uint32 diff)
{
    if (m_perched)
        UpdateFlyers(diff);

    m_perchTimer += diff;
    if (m_perchTimer < BONK_PERCH_CHECK_MS || m_player->IsFalling() || m_player->IsBeingTeleported())
        return;
    m_perchTimer = 0;

    if (IsHeroReachable())
    {
        if (m_perched && ++m_reachChecks < BONK_PERCH_CLEAR_CHECKS)
            return;
        m_perched = false;
        m_perchChecks = 0;
        m_reachChecks = 0;
        m_hasGround = true;
        m_groundX = m_reachFound ? m_reachX : m_player->GetPositionX();
        m_groundY = m_reachFound ? m_reachY : m_player->GetPositionY();
        m_groundZ = m_reachFound ? m_reachZ : m_player->GetPositionZ();
        return;
    }

    m_reachChecks = 0;
    if (m_perched || ++m_perchChecks < BONK_PERCH_CONFIRM_CHECKS)
        return;
    m_perched = true;
    m_perchedMs = 0;
    m_flyerTimer = BONK_FLYER_FIRST_MS;
}

bool BonkRun::IsHeroReachable() const
{
    m_reachFound = false;
    if (HasStrandedMobs())
        return false;

    uint32 probes = 0;
    for (Creature* mob : GetMobsInRange(BONK_PERCH_SENSE_RANGE))
    {
        BonkMobAI const* ai = dynamic_cast<BonkMobAI const*>(mob->AI());
        if (!ai || ai->IsFlyer() || !ai->CanBePulled())
            continue;
        if (IsReachableFrom(mob->GetPositionX(), mob->GetPositionY(), mob->GetPositionZ(), mob))
            return true;
        if (++probes >= BONK_PERCH_PROBES)
            return false;
    }

    Map const* map = m_player->GetMap();
    float const heroZ = m_player->GetPositionZ();
    for (uint32 point = 0; point < BONK_PERCH_RING_POINTS; ++point)
    {
        float const angle = 2.0f * M_PI_F * point / BONK_PERCH_RING_POINTS;
        float const x = m_player->GetPositionX() + BONK_PERCH_RING_DISTANCE * std::cos(angle);
        float const y = m_player->GetPositionY() + BONK_PERCH_RING_DISTANCE * std::sin(angle);
        float const z = map->GetHeight(x, y, heroZ + BONK_SPAWN_MAX_HEIGHT_DIFF, true, 2.0f * BONK_SPAWN_MAX_HEIGHT_DIFF);
        if (z > INVALID_HEIGHT && IsReachableFrom(x, y, z, nullptr))
            return true;
    }
    return false;
}

bool BonkRun::HasStrandedMobs() const
{
    if (m_totalMs - m_lastStruckMs < BONK_PERCH_UNSTRUCK_MS)
        return false;
    uint32 stranded = 0;
    for (Creature* mob : GetMobsInRange(BONK_PERCH_STRANDED_RANGE))
    {
        BonkMobAI const* ai = dynamic_cast<BonkMobAI const*>(mob->AI());
        if (!ai || ai->IsFlyer() || !ai->CanBePulled() || mob->IsMoving() ||
            mob->HasUnitState(UNIT_STATE_STUNNED | UNIT_STATE_ROOT | UNIT_STATE_CONFUSED | UNIT_STATE_FLEEING))
            continue;
        if (mob->GetMotionMaster()->GetCurrentMovementGeneratorType() != CHASE_MOTION_TYPE)
            continue;
        ++stranded;
    }
    return stranded >= BONK_PERCH_STRANDED_MOBS;
}

bool BonkRun::IsReachableFrom(float x, float y, float z, Creature const* prober) const
{
    float const heroX = m_player->GetPositionX();
    float const heroY = m_player->GetPositionY();
    float const heroZ = m_player->GetPositionZ();

    PathFinder path(prober ? static_cast<Unit const*>(prober) : m_player);
    path.calculate(Vector3(x, y, z), Vector3(heroX, heroY, heroZ));
    if (path.getPathType() & PATHFIND_NOPATH)
        return false;

    Vector3 const end = path.getActualEndPosition();
    float const dx = end.x - heroX;
    float const dy = end.y - heroY;
    float const dz = end.z - heroZ;
    bool const reaches = prober ? prober->CanReachWithMeleeAutoAttackAtPosition(m_player, end.x, end.y, end.z) :
        dx * dx + dy * dy + dz * dz <= BONK_PERCH_REACH * BONK_PERCH_REACH;
    if (reaches)
    {
        m_reachFound = true;
        m_reachX = end.x;
        m_reachY = end.y;
        m_reachZ = end.z;
    }
    return reaches;
}

void BonkRun::UpdateFlyers(uint32 diff)
{
    m_perchedMs += diff;
    if (m_flyerTimer > diff)
    {
        m_flyerTimer -= diff;
        return;
    }
    m_flyerTimer = BONK_FLYER_INTERVAL_MS;

    uint32 const alive = CountFlyers();
    if (alive >= BONK_FLYER_MAX_ALIVE)
        return;
    uint32 const ramp = BONK_FLYER_PACK_MIN + m_perchedMs / BONK_FLYER_RAMP_MS;
    SpawnFlyerPack(std::min(std::min(ramp, BONK_FLYER_PACK_MAX), BONK_FLYER_MAX_ALIVE - alive));
}

void BonkRun::SpawnFlyerPack(uint32 count)
{
    float x, y, z;
    if (!FindFlyerPosition(x, y, z))
        return;

    for (uint32 index = 0; index < count; ++index)
    {
        BonkMapCreature const* picked = PickFlyer();
        if (!picked)
            return;
        BonkMapCreature creature = *picked;
        creature.kind = BONK_SPAWN_NORMAL;
        creature.archetype = BONK_ARCHETYPE_BRUISER;
        float const angle = frand(0.0f, 2.0f * M_PI_F);
        float const offset = frand(0.0f, BONK_FLYER_PACK_RADIUS);
        BonkMobAI::SetNextFlyer(true);
        if (!SpawnMobAt(creature, x + offset * std::cos(angle), y + offset * std::sin(angle), z + frand(-1.0f, 1.0f)))
            BonkMobAI::SetNextFlyer(false);
    }
}

bool BonkRun::FindFlyerPosition(float& x, float& y, float& z) const
{
    Map const* map = m_player->GetMap();
    float const heroX = m_player->GetPositionX();
    float const heroY = m_player->GetPositionY();
    float const heroZ = m_player->GetPositionZ();

    for (uint32 attempt = 0; attempt < BONK_FLYER_TRIES; ++attempt)
    {
        float const angle = frand(0.0f, 2.0f * M_PI_F);
        float const distance = frand(BONK_FLYER_MIN_DISTANCE, BONK_FLYER_MAX_DISTANCE);
        x = heroX + distance * std::cos(angle);
        y = heroY + distance * std::sin(angle);
        float const ground = map->GetHeight(x, y, heroZ + BONK_SPAWN_MAX_HEIGHT_DIFF, true, 2.0f * BONK_SPAWN_MAX_HEIGHT_DIFF);
        z = std::max(ground, heroZ) + BONK_FLYER_HEIGHT;
        if (m_map.zoneId && map->GetTerrain()->GetZoneId(x, y, z) != m_map.zoneId)
            continue;
        if (map->isInLineOfSight(x, y, z, heroX, heroY, heroZ + 2.0f))
            return true;
    }
    return false;
}

BonkMapCreature const* BonkRun::PickFlyer() const
{
    std::vector<BonkMapCreature const*> candidates;
    uint32 totalWeight = 0;
    for (auto const& creature : m_map.creatures)
    {
        if (!creature.flyer)
            continue;
        candidates.push_back(&creature);
        totalWeight += creature.weight;
    }
    if (!totalWeight)
        return nullptr;

    uint32 roll = urand(1, totalWeight);
    for (BonkMapCreature const* creature : candidates)
    {
        if (roll <= creature->weight)
            return creature;
        roll -= creature->weight;
    }
    return nullptr;
}

uint32 BonkRun::CountFlyers() const
{
    Map* map = m_player->GetMap();
    uint32 count = 0;
    for (ObjectGuid const& guid : m_mobs)
        if (Creature const* mob = map->GetCreature(guid))
            if (mob->IsAlive() && !mob->HasCreatureState(CSTATE_DESPAWNING) && mob->IsBonkFlyer())
                ++count;
    return count;
}

void BonkRun::UpdateLanes(uint32 diff)
{
    m_laneTimer += diff;
    if (m_lanes[0] >= 0 && m_laneTimer < BONK_LANE_SHIFT_MS)
        return;
    m_laneTimer = 0;

    int32 const sectors = int32(BONK_SPAWN_SECTORS);
    if (m_lanes[0] < 0)
    {
        m_lanes[0] = irand(0, sectors - 1);
        m_lanes[1] = (m_lanes[0] + sectors / 2 + irand(-1, 1) + sectors) % sectors;
        return;
    }
    for (uint32 i = 0; i < 2; ++i)
    {
        int32 const moved = (m_lanes[i] + irand(-1, 1) + sectors) % sectors;
        int32 const apart = std::abs(moved - m_lanes[1 - i]);
        if (std::min(apart, sectors - apart) >= int32(BONK_LANE_MIN_GAP))
            m_lanes[i] = moved;
    }
}

bool BonkRun::IsLane(uint32 sector) const
{
    return int32(sector) == m_lanes[0] || int32(sector) == m_lanes[1];
}

bool BonkRun::IsLaneEdge(uint32 sector) const
{
    return IsLane((sector + 1) % BONK_SPAWN_SECTORS) || IsLane((sector + BONK_SPAWN_SECTORS - 1) % BONK_SPAWN_SECTORS);
}

float BonkRun::GetSectorAngle(uint32 sector) const
{
    return (float(sector) + 0.5f) * 2.0f * M_PI_F / float(BONK_SPAWN_SECTORS);
}

uint32 BonkRun::GetSectorOf(float x, float y) const
{
    float angle = std::atan2(y - m_player->GetPositionY(), x - m_player->GetPositionX());
    if (angle < 0.0f)
        angle += 2.0f * M_PI_F;
    return std::min(BONK_SPAWN_SECTORS - 1, uint32(angle / (2.0f * M_PI_F) * float(BONK_SPAWN_SECTORS)));
}

std::vector<float> BonkRun::CountSectorCrowd() const
{
    std::vector<float> crowd(BONK_SPAWN_SECTORS, 0.0f);
    Map* map = m_player->GetMap();
    auto const countIn = [&](ObjectGuid const& guid)
    {
        if (Creature const* mob = map->GetCreature(guid))
            if (mob->IsAlive() && mob->IsWithinDistInMap(m_player, BONK_SECTOR_RANGE))
                crowd[GetSectorOf(mob->GetPositionX(), mob->GetPositionY())] += 1.0f;
    };
    for (ObjectGuid const& guid : m_mobs)
        countIn(guid);
    for (TimedMob const& emerging : m_emerging)
        countIn(emerging.guid);
    return crowd;
}

BonkRun::SpawnOpenness BonkRun::GetSpawnOpenness() const
{
    if (m_tier.key == "normal")
        return { -0.5f, true, 1 };
    if (m_tier.key == "heroic")
        return { 0.0f, true, 0 };
    return { 1.0f, false, 0 };
}

std::vector<uint32> BonkRun::CountNearSectors(float range) const
{
    std::vector<uint32> nearby(BONK_SPAWN_SECTORS, 0);
    Map* map = m_player->GetMap();
    for (ObjectGuid const& guid : m_mobs)
        if (Creature const* mob = map->GetCreature(guid))
            if (mob->IsAlive() && mob->IsWithinDistInMap(m_player, range))
                ++nearby[GetSectorOf(mob->GetPositionX(), mob->GetPositionY())];
    return nearby;
}

std::vector<bool> BonkRun::GetGuardedSectors() const
{
    std::vector<bool> guarded(BONK_SPAWN_SECTORS, false);
    SpawnOpenness const openness = GetSpawnOpenness();
    if (!openness.guardGap)
        return guarded;

    std::vector<uint32> const nearby = CountNearSectors(BONK_GAP_RANGE);
    auto const occupied = std::find_if(nearby.begin(), nearby.end(), [](uint32 count) { return count > 0; });
    if (occupied == nearby.end())
        return guarded;

    uint32 centre = uint32(std::min_element(nearby.begin(), nearby.end()) - nearby.begin());
    uint32 const first = uint32(occupied - nearby.begin());
    uint32 bestStart = 0, bestLength = 0, runStart = 0, runLength = 0;
    for (uint32 step = 1; step <= BONK_SPAWN_SECTORS; ++step)
    {
        uint32 const sector = (first + step) % BONK_SPAWN_SECTORS;
        if (nearby[sector])
        {
            runLength = 0;
            continue;
        }
        if (!runLength)
            runStart = sector;
        if (++runLength > bestLength)
        {
            bestLength = runLength;
            bestStart = runStart;
        }
    }
    if (bestLength)
        centre = (bestStart + (bestLength - 1) / 2) % BONK_SPAWN_SECTORS;

    for (int32 offset = -int32(openness.guardHalfWidth); offset <= int32(openness.guardHalfWidth); ++offset)
        guarded[(int32(centre) + offset + int32(BONK_SPAWN_SECTORS)) % int32(BONK_SPAWN_SECTORS)] = true;
    return guarded;
}

void BonkRun::UpdateRingSample(uint32 diff)
{
    m_ringTimer += diff;
    if (m_ringTimer < BONK_RING_SAMPLE_MS)
        return;
    m_ringTimer = 0;
    std::vector<uint32> const nearby = CountNearSectors(BONK_RING_RANGE);
    uint32 const blocked = uint32(std::count_if(nearby.begin(), nearby.end(), [](uint32 count) { return count > 0; }));
    ++m_ringSamples;
    m_ringBlocked += blocked;
    if (blocked + 1 >= BONK_SPAWN_SECTORS)
        ++m_ringClosed;
}

void BonkRun::LogRing()
{
    if (m_ringSamples)
        sLog.Player(m_player->GetSession(), LOG_LEVELUP, LOG_LVL_BASIC,
            "Bonk ring: %s at run minute %u (%s, %s): %.1f of %u directions blocked within %.0f yd, closed %u%% of the time",
            m_player->GetName(), m_nextLogMinute, m_map.key.c_str(), m_tier.key.c_str(), float(m_ringBlocked) / float(m_ringSamples),
            BONK_SPAWN_SECTORS, BONK_RING_RANGE, m_ringClosed * 100 / m_ringSamples);
    m_ringSamples = 0;
    m_ringBlocked = 0;
    m_ringClosed = 0;
}

int32 BonkRun::PickSector(std::vector<float> const& crowd, bool frontOnly) const
{
    std::vector<bool> const guarded = GetGuardedSectors();
    float const fill = GetSpawnOpenness().fill;
    float heading;
    bool const moving = GetHeading(heading);
    float const forward = moving ? heading : m_player->GetOrientation();
    bool const frontFull = CountAhead(forward) >= GetFrontCap();
    if (m_sectorBlockedUntil.size() != BONK_SPAWN_SECTORS)
        m_sectorBlockedUntil.assign(BONK_SPAWN_SECTORS, 0);

    std::vector<float> weights(BONK_SPAWN_SECTORS, 0.0f);
    float total = 0.0f;
    for (uint32 sector = 0; sector < BONK_SPAWN_SECTORS; ++sector)
    {
        if (IsLane(sector) || guarded[sector] || m_sectorBlockedUntil[sector] > m_elapsedMs)
            continue;
        float const offset = AngleBetween(GetSectorAngle(sector), forward);
        if (frontOnly && offset > M_PI_F / 2.0f)
            continue;
        float bias = 1.0f;
        if (moving && offset <= BONK_FRONT_ARC)
            bias = frontFull ? 0.0f : BONK_FORWARD_WEIGHT;
        else if (moving && offset >= M_PI_F - BONK_FRONT_ARC)
            bias = BONK_BEHIND_WEIGHT;
        if (IsLaneEdge(sector))
            bias *= BONK_LANE_EDGE_WEIGHT;
        weights[sector] = bias * std::pow(1.0f + crowd[sector], -fill);
        total += weights[sector];
    }
    if (total <= 0.0f)
        return -1;

    float roll = frand(0.0f, total);
    for (uint32 sector = 0; sector < BONK_SPAWN_SECTORS; ++sector)
    {
        if (weights[sector] <= 0.0f)
            continue;
        if (roll <= weights[sector])
            return int32(sector);
        roll -= weights[sector];
    }
    return -1;
}

bool BonkRun::FindSectorPosition(uint32 sector, float minDistance, float maxDistance, float& x, float& y, float& z) const
{
    float const half = M_PI_F / float(BONK_SPAWN_SECTORS);
    float heading;
    bool const ahead = GetHeading(heading) && AngleBetween(GetSectorAngle(sector), heading) <= BONK_FRONT_ARC;
    for (uint32 attempt = 0; attempt < BONK_SECTOR_ANGLE_TRIES; ++attempt)
    {
        float const angle = GetSectorAngle(sector) + frand(-half, half);
        float const distance = ahead ? frand(BONK_FAR_MIN_DISTANCE, BONK_FAR_MAX_DISTANCE) : frand(minDistance, maxDistance);
        if (FindSpawnPositionAt(angle, distance, x, y, z))
            return true;
    }
    if (m_sectorBlockedUntil.size() == BONK_SPAWN_SECTORS)
        m_sectorBlockedUntil[sector] = m_elapsedMs + BONK_SECTOR_BLOCK_MS;
    return false;
}

void BonkRun::SpawnTrashPacks(uint32 count)
{
    std::vector<float> crowd = CountSectorCrowd();
    float const playerX = m_player->GetPositionX(), playerY = m_player->GetPositionY();
    while (count > 0)
    {
        uint32 const size = std::min(count, RollSpawnGroupSize());
        count -= size;
        uint32 const groupDelayMs = urand(0, BONK_SPAWN_TICK_MS);

        int32 sector = -1;
        float centreX = 0.0f, centreY = 0.0f, centreZ = 0.0f;
        bool found = false;
        for (uint32 attempt = 0; attempt < BONK_SECTOR_TRIES && !found; ++attempt)
        {
            sector = PickSector(crowd, false);
            if (sector < 0)
                break;
            found = FindSectorPosition(uint32(sector), m_map.spawnMinDistance, m_map.spawnMaxDistance, centreX, centreY, centreZ);
        }
        if (!found && !FindSpawnPosition(centreX, centreY, centreZ, m_map.spawnMinDistance, m_map.spawnMaxDistance))
            continue;

        for (uint32 i = 0; i < size; ++i)
        {
            BonkMapCreature const* creature = PickTrash();
            if (!creature)
                continue;
            float x = centreX, y = centreY, z = centreZ;
            if (i)
            {
                float const offsetAngle = frand(0.0f, 2.0f * M_PI_F);
                float const offset = frand(BONK_SPAWN_GROUP_MIN_GAP, BONK_SPAWN_GROUP_RADIUS);
                float const memberX = centreX + offset * std::cos(offsetAngle);
                float const memberY = centreY + offset * std::sin(offsetAngle);
                float const angle = std::atan2(memberY - playerY, memberX - playerX);
                float const distance = std::sqrt((memberX - playerX) * (memberX - playerX) + (memberY - playerY) * (memberY - playerY));
                if (!FindSpawnPositionAt(angle, distance, x, y, z))
                {
                    x = centreX;
                    y = centreY;
                    z = centreZ;
                }
            }
            bool const veteran = RollVeteran(*creature);
            bool const spawned = m_stress.on ? SpawnMobAt(*creature, x, y, z, veteran) != nullptr :
                EmergeMobAt(*creature, x, y, z, veteran, groupDelayMs + urand(0, BONK_EMERGE_MEMBER_STAGGER_MS));
            if (spawned && found)
                crowd[uint32(sector)] += 1.0f;
        }
    }
}

uint32 BonkRun::GetSpawnKit()
{
    return s_spawnKit;
}

void BonkRun::SetSpawnKit(uint32 kit)
{
    s_spawnKit = kit;
}

uint32 BonkRun::GetSpawnLookLeadMs()
{
    return s_spawnLookLeadMs;
}

void BonkRun::SetSpawnLookLeadMs(uint32 leadMs)
{
    s_spawnLookLeadMs = std::min(leadMs, BONK_EMERGE_HIDDEN_MS);
}

std::string BonkRun::DescribeSpawnLook() const
{
    return "Spawned enemies turn hostile " + std::to_string(s_spawnLookLeadMs) + " ms after their spawn flash; emerging now " +
        std::to_string(m_emerging.size()) + ".";
}

void BonkRun::PreviewSpawnLook()
{
    float const angle = m_player->GetOrientation();
    float x = m_player->GetPositionX() + BONK_SPAWN_LOOK_PREVIEW_DISTANCE * std::cos(angle);
    float y = m_player->GetPositionY() + BONK_SPAWN_LOOK_PREVIEW_DISTANCE * std::sin(angle);
    float z = m_player->GetMap()->GetHeight(x, y, m_player->GetPositionZ() + 5.0f, true, 15.0f);
    if (z <= INVALID_HEIGHT)
        z = m_player->GetPositionZ();
    ShowKitAt(s_spawnKit, x, y, z);
}

uint32 BonkRun::RollSpawnGroupSize() const
{
    uint32 const roll = urand(0, 99);
    if (roll < BONK_SPAWN_SINGLE_CHANCE)
        return 1;
    if (roll < BONK_SPAWN_SINGLE_CHANCE + BONK_SPAWN_PAIR_CHANCE)
        return 2;
    return urand(BONK_SPAWN_GROUP_MIN, BONK_SPAWN_GROUP_MAX);
}

bool BonkRun::EmergeMobAt(BonkMapCreature const& creature, float x, float y, float z, bool veteran, uint32 delayMs)
{
    if (m_sandbox)
        return SpawnMobAt(creature, x, y, z, veteran) != nullptr;
    if (!delayMs)
    {
        EmergeNow(creature, x, y, z, veteran);
        return true;
    }
    PendingSpawn pending;
    pending.creature = creature;
    pending.x = x;
    pending.y = y;
    pending.z = z;
    pending.veteran = veteran;
    pending.leftMs = int32(delayMs);
    m_pendingSpawns.push_back(pending);
    return true;
}

void BonkRun::EmergeNow(BonkMapCreature const& creature, float x, float y, float z, bool veteran)
{
    Creature* mob = SpawnMobAt(creature, x, y, z, veteran);
    if (!mob)
        return;
    ObjectGuid const guid = mob->GetObjectGuid();
    m_mobs.erase(std::remove(m_mobs.begin(), m_mobs.end(), guid), m_mobs.end());
    mob->SetFactionTemplateId(BONK_EMERGE_FACTION);
    mob->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE | UNIT_FLAG_IMMUNE_TO_PLAYER | UNIT_FLAG_IMMUNE_TO_NPC);
    if (BonkMobAI* ai = dynamic_cast<BonkMobAI*>(mob->AI()))
        ai->Hold(s_spawnLookLeadMs + BONK_EMERGE_SETTLE_MS);
    if (s_spawnKit)
        SendBonkKitAt(m_player, s_spawnKit, x, y, mob->GetPositionZ(), mob->GetOrientation(), BONK_SPAWN_LOOK_HOLD_MS);
    m_emerging.push_back({ guid, int32(s_spawnLookLeadMs) });
}

void BonkRun::UpdateEmerging(uint32 diff)
{
    for (auto itr = m_pendingSpawns.begin(); itr != m_pendingSpawns.end();)
    {
        itr->leftMs -= int32(diff);
        if (itr->leftMs > 0)
        {
            ++itr;
            continue;
        }
        PendingSpawn const due = *itr;
        itr = m_pendingSpawns.erase(itr);
        EmergeNow(due.creature, due.x, due.y, due.z, due.veteran);
    }

    Map* map = m_player->GetMap();
    for (auto itr = m_emerging.begin(); itr != m_emerging.end();)
    {
        Creature* mob = map->GetCreature(itr->guid);
        if (!mob || !mob->IsAlive() || mob->HasCreatureState(CSTATE_DESPAWNING))
        {
            itr = m_emerging.erase(itr);
            continue;
        }
        if (!itr->shown)
        {
            ShowEmergingBody(mob);
            itr->shown = true;
        }
        itr->leftMs -= int32(diff);
        if (itr->leftMs > 0)
        {
            ++itr;
            continue;
        }
        ActivateEmerged(mob);
        itr = m_emerging.erase(itr);
    }
}

void BonkRun::ShowEmergingBody(Creature* mob)
{
    mob->SetFacingTo(std::atan2(m_player->GetPositionY() - mob->GetPositionY(), m_player->GetPositionX() - mob->GetPositionX()));
}

void BonkRun::ActivateEmerged(Creature* mob)
{
    mob->SetFactionTemplateId(BONK_ENEMY_FACTION);
    mob->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE | UNIT_FLAG_IMMUNE_TO_PLAYER | UNIT_FLAG_IMMUNE_TO_NPC);
    if (BonkMobAI* ai = dynamic_cast<BonkMobAI*>(mob->AI()))
        ai->Hold(BONK_EMERGE_SETTLE_MS);
    m_mobs.push_back(mob->GetObjectGuid());
}

void BonkRun::ShowTeleportFlash(Creature* mob, float x, float y, float z)
{
    if (!s_spawnKit)
        return;
    SendBonkKitAt(m_player, s_spawnKit, mob->GetPositionX(), mob->GetPositionY(), mob->GetPositionZ(), mob->GetOrientation(),
        BONK_SPAWN_LOOK_HOLD_MS);
    SendBonkKitAt(m_player, s_spawnKit, x, y, z, mob->GetOrientation(), BONK_SPAWN_LOOK_HOLD_MS);
}

void BonkRun::UpdateCrowd(uint32 diff)
{
    std::vector<Creature*> stuck;
    m_crowd.Update(diff, m_player, m_mobs, m_perched, stuck);
    for (Creature* mob : stuck)
        OnMobStuck(mob);
}

void BonkRun::UpdateStragglers(uint32 diff)
{
    m_stragglerTimer += diff;
    if (m_stragglerTimer < BONK_STRAGGLER_TICK_MS)
        return;
    m_stragglerTimer = 0;

    uint32 const now = m_elapsedMs;
    m_packSpots.erase(std::remove_if(m_packSpots.begin(), m_packSpots.end(), [now](PackSpot const& spot) { return spot.untilMs <= now; }), m_packSpots.end());

    Map* map = m_player->GetMap();
    float const forward = GetForwardAngle();
    std::map<ObjectGuid, uint32> farSince;
    std::vector<std::pair<Creature*, uint32>> bosses, waiting;
    for (ObjectGuid const& guid : m_mobs)
    {
        Creature* mob = map->GetCreature(guid);
        if (!mob || !mob->IsAlive() || mob->HasCreatureState(CSTATE_DESPAWNING) || mob->IsWithinDistInMap(m_player, BONK_STRAY_DISTANCE) ||
            IsAhead(mob->GetPositionX(), mob->GetPositionY(), forward))
            continue;
        if (IsMiniEventTarget(guid))
            continue;
        if (BonkMobAI const* ward = dynamic_cast<BonkMobAI const*>(mob->AI()))
            if (ward->IsWard())
                continue;
        auto const itr = m_farSince.find(guid);
        uint32 const since = itr != m_farSince.end() ? itr->second : now;
        farSince[guid] = since;
        if (now - since < BONK_STRAY_MS)
            continue;

        BonkMobAI const* ai = dynamic_cast<BonkMobAI const*>(mob->AI());
        if (ai && ai->GetKind() == BONK_SPAWN_NORMAL && !ai->IsFlyer() && !IsInRoster(ai->GetArchetype()))
        {
            farSince.erase(guid);
            mob->DespawnOrUnsummon();
            continue;
        }
        bool const boss = ai && (ai->GetKind() == BONK_SPAWN_BOSS || ai->GetKind() == BONK_SPAWN_MINIBOSS);
        (boss ? bosses : waiting).emplace_back(mob, since + BONK_STRAY_MS);
    }
    m_farSince = std::move(farSince);
    if (bosses.empty() && waiting.empty())
        return;

    float heading;
    if (!GetHeading(heading))
    {
        waiting.insert(waiting.end(), bosses.begin(), bosses.end());
        for (auto const& itr : waiting)
        {
            float x, y, z;
            if (now - itr.second >= BONK_STRAGGLER_SIDE_MS && FindMobPosition(x, y, z, m_map.spawnMinDistance, m_map.spawnMaxDistance))
                MoveMob(itr.first, x, y, z);
        }
        return;
    }

    uint32 ahead = CountAhead(heading);
    uint32 const cap = GetFrontCap();

    for (auto const& itr : bosses)
    {
        if (ahead < cap && PlacePack({ itr.first }, heading))
            ++ahead;
        else
            PlaceAside(itr.first, heading);
    }

    std::stable_sort(waiting.begin(), waiting.end(), [](auto const& a, auto const& b) { return a.second < b.second; });
    size_t next = 0;
    while (next < waiting.size())
    {
        uint32 const room = cap > ahead ? cap - ahead : 0;
        uint32 const left = uint32(waiting.size() - next);
        uint32 const size = std::min({ urand(BONK_PACK_MIN, BONK_PACK_MAX), left, room });
        bool const waitedLong = now - waiting[next].second >= BONK_PACK_WAIT_MS;
        if (!size || (size < BONK_PACK_MIN && !waitedLong))
            break;

        std::vector<Creature*> pack;
        for (uint32 i = 0; i < size; ++i)
            pack.push_back(waiting[next + i].first);
        if (!PlacePack(pack, heading))
            break;
        next += size;
        ahead += size;
    }

    for (size_t i = next; i < waiting.size(); ++i)
        if (now - waiting[i].second >= BONK_STRAGGLER_SIDE_MS)
            PlaceAside(waiting[i].first, heading);
}

bool BonkRun::PlacePack(std::vector<Creature*> const& pack, float heading)
{
    Map* map = m_player->GetMap();
    float const playerX = m_player->GetPositionX();
    float const playerY = m_player->GetPositionY();

    std::vector<float> const crowd = CountSectorCrowd();
    float const half = M_PI_F / float(BONK_SPAWN_SECTORS);
    for (uint32 attempt = 0; attempt < 8; ++attempt)
    {
        float const distance = attempt < BONK_FAR_TRIES ? frand(BONK_FAR_MIN_DISTANCE, BONK_FAR_MAX_DISTANCE) :
            frand(m_map.spawnMinDistance, m_map.spawnMaxDistance);
        int32 const sector = PickSector(crowd, true);
        float const angle = sector >= 0 ? GetSectorAngle(uint32(sector)) + frand(-half, half) : heading + frand(-BONK_FRONT_ARC, BONK_FRONT_ARC);
        float centreX, centreY, centreZ;
        if (!FindSpawnPositionAt(angle, distance, centreX, centreY, centreZ))
            continue;

        auto const within = [centreX, centreY](float x, float y, float range)
        {
            return (x - centreX) * (x - centreX) + (y - centreY) * (y - centreY) < range * range;
        };
        if (std::any_of(m_packSpots.begin(), m_packSpots.end(), [&within](PackSpot const& spot) { return within(spot.x, spot.y, BONK_PACK_SPACING); }))
            continue;
        bool crowded = false;
        for (ObjectGuid const& guid : m_mobs)
            if (Creature const* other = map->GetCreature(guid))
                if (other->IsAlive() && std::find(pack.begin(), pack.end(), other) == pack.end() &&
                    within(other->GetPositionX(), other->GetPositionY(), BONK_PACK_CLEARANCE))
                {
                    crowded = true;
                    break;
                }
        if (crowded)
            continue;

        for (size_t i = 0; i < pack.size(); ++i)
        {
            float x = centreX, y = centreY, z = centreZ;
            if (i)
            {
                float const offsetAngle = frand(0.0f, 2.0f * M_PI_F);
                float const offset = frand(1.0f, BONK_PACK_RADIUS);
                float const memberX = centreX + offset * std::cos(offsetAngle);
                float const memberY = centreY + offset * std::sin(offsetAngle);
                float const angle = std::atan2(memberY - playerY, memberX - playerX);
                float const distance = std::sqrt((memberX - playerX) * (memberX - playerX) + (memberY - playerY) * (memberY - playerY));
                if (!FindSpawnPositionAt(angle, distance, x, y, z))
                {
                    x = centreX;
                    y = centreY;
                    z = centreZ;
                }
            }
            MoveMob(pack[i], x, y, z);
        }

        m_packSpots.push_back({ centreX, centreY, m_elapsedMs + BONK_PACK_SPOT_MS });
        return true;
    }
    return false;
}

bool BonkRun::PlaceAside(Creature* mob, float heading)
{
    for (uint32 attempt = 0; attempt < 10; ++attempt)
    {
        float const side = urand(0, 1) ? 1.0f : -1.0f;
        float const angle = heading + side * frand(BONK_SIDE_MIN_ANGLE, BONK_SIDE_MAX_ANGLE);
        float x, y, z;
        if (FindSpawnPositionAt(angle, frand(m_map.spawnMinDistance, m_map.spawnMaxDistance), x, y, z))
        {
            MoveMob(mob, x, y, z);
            return true;
        }
    }
    return false;
}

void BonkRun::MoveMob(Creature* mob, float x, float y, float z)
{
    float const facing = std::atan2(m_player->GetPositionY() - y, m_player->GetPositionX() - x);
    ShowTeleportFlash(mob, x, y, z);
    mob->NearTeleportTo(x, y, z, facing);
    if (BonkMobAI* ai = dynamic_cast<BonkMobAI*>(mob->AI()))
        ai->Arrive(false);
}

float BonkRun::GetSpawnHealthUncapped(BonkSpawnKind kind, BonkArchetype archetype, float creatureMultiplier) const
{
    uint32 const level = std::min(GetMobLevel() + GetKindLevelBonus(kind), 63u);
    float const archetypeHealth = kind == BONK_SPAWN_NORMAL ? sBonkMgr.GetArchetype(archetype).healthMultiplier : 1.0f;
    return std::max(1.0f, GetMobHealth(level) * GetStageHealthMultiplier() * m_map.healthMultiplier * creatureMultiplier * archetypeHealth *
        GetCrowdOverflow() * GetKindHealth(kind) * GetTierKindHealth(kind) * GetScalingNow().health * GetCoopHealthScale(kind));
}

float BonkRun::GetCoopHealthScale(BonkSpawnKind kind) const
{
    float const perPlayer = kind == BONK_SPAWN_BOSS ? BONK_COOP_BOSS_HEALTH_PER_PLAYER : BONK_COOP_HEALTH_PER_PLAYER;
    return 1.0f + perPlayer * float(m_coopPlayers - 1);
}

void BonkRun::SetCoopPlayers(uint32 players, bool sharedPause, bool sharedXp)
{
    m_coopPlayers = std::max(1u, players);
    m_coopNoPause = !sharedPause;
    m_coopOwnXp = !sharedXp;
    ApplyPause(WantsPause());
    m_player->GetBonkPlayer()->SendAddon(std::string("coopnopause~") + (m_coopNoPause ? "1" : "0"));
    sLog.Player(m_player->GetSession(), LOG_LEVELUP, LOG_LVL_BASIC,
        "Bonk coop: %s runs with %u players (stage %u, %s, %s, class %u, %s, pause %s, xp %s)", m_player->GetName(), m_coopPlayers, m_stage,
        m_map.key.c_str(), m_tier.key.c_str(), uint32(m_player->GetClass()), IsCoopGuest() ? "guest" : "host",
        sharedPause ? "shared" : "none", sharedXp ? "shared" : "individual");
}

uint32 BonkRun::GetSpawnHealth(BonkSpawnKind kind, BonkArchetype archetype, float creatureMultiplier) const
{
    return uint32(std::min(BONK_MAX_MOB_HEALTH, GetSpawnHealthUncapped(kind, archetype, creatureMultiplier)));
}

float BonkRun::GetTierKindHealth(BonkSpawnKind kind) const
{
    return kind == BONK_SPAWN_BOSS ? m_tier.bossHealthMultiplier : kind == BONK_SPAWN_MINIBOSS ? m_tier.minibossHealthMultiplier : 1.0f;
}

float BonkRun::GetProcBaseHealth() const
{
    return GetMobHealth(GetMobLevel()) * sBonkMgr.GetArchetype(BONK_ARCHETYPE_GRUNT).healthMultiplier * GetScalingNow().health;
}

std::vector<std::string> BonkRun::DescribeScaling() const
{
    std::vector<std::string> lines;
    char line[256];
    uint32 const grunt = GetSpawnHealth(BONK_SPAWN_NORMAL, BONK_ARCHETYPE_GRUNT, 1.0f);
    uint32 const bruiser = GetSpawnHealth(BONK_SPAWN_NORMAL, BONK_ARCHETYPE_BRUISER, 1.0f);
    uint32 const elite = GetSpawnHealth(BONK_SPAWN_ELITE, BONK_ARCHETYPE_GRUNT, 1.0f);
    snprintf(line, sizeof(line), "Minute %.1f, level %u, stage %u: grunt %u health (x%.2f its level's base), bruiser %u, elite %u; %u alive, %u wanted.",
        GetRunMinutes(), m_player->GetLevel(), m_stage, grunt, grunt / std::max(1.0f, GetMobHealth(GetMobLevel())), bruiser, elite,
        uint32(m_mobs.size()), GetSpawnTarget());
    lines.push_back(line);
    BonkScalingPoint const scaling = GetScalingNow();
    snprintf(line, sizeof(line), "Block \"%s\" (from %u s); a grunt hit takes %.1f%% of %.0f base health (%.0f before armor); toughness x%.2f.",
        GetWave().name.c_str(), GetWave().startSecond, scaling.hitShare * 100.0f * GetStageDamageMultiplier(), GetHeroBaseHealth(),
        GetHeroBaseHealth() * scaling.hitShare * GetStageDamageMultiplier(), scaling.health);
    lines.push_back(line);

    for (OwnedWeapon const& owned : m_weapons)
    {
        SpellEntry const* spellInfo = sSpellMgr.GetSpellEntry(owned.weapon.spellId);
        float const hit = std::max(1.0f, GetWeaponDamage(owned) * GetDamageMultiplier(spellInfo, nullptr));
        snprintf(line, sizeof(line), "%s rank %u: %.0f a hit (+%.0f%% from ranks) every %.1f s; grunt %.0f hits, bruiser %.0f, elite %.0f.",
            owned.weapon.name.c_str(), owned.rank, hit, owned.damageBonus, GetWeaponCooldown(owned) / float(IN_MILLISECONDS),
            std::ceil(grunt / hit), std::ceil(bruiser / hit), std::ceil(elite / hit));
        lines.push_back(line);
    }
    return lines;
}

uint32 BonkRun::GetMobLevel() const
{
    int32 const level = int32(m_player->GetLevel()) + m_map.levelOffset;
    return uint32(std::max(1, std::min(level, 63)));
}

float BonkRun::GetMobHealth(uint32 level) const
{
    uint32 const tableLevel = std::min(level, BONK_MOB_HEALTH_TABLE_LEVELS);
    CreatureClassLevelStats const* stats = sObjectMgr.GetCreatureClassLevelStats(CLASS_WARRIOR, tableLevel);
    float const tableHealth = stats ? float(stats->health) : 40.0f * tableLevel;
    uint32 const levelsPastTable = level - tableLevel;
    return tableHealth * (1.0f + BONK_MOB_HEALTH_LEVEL_GROWTH * float(levelsPastTable));
}

float BonkRun::StageHealthScale(BonkTier const& tier, BonkLevel const& level, uint32 stage)
{
    float const stages = float(stage - 1);
    return tier.healthMultiplier * level.health * (1.0f + tier.stageHealthGrowth * stages + tier.stageHealthAccel * stages * std::max(0.0f, stages - 1.0f));
}

float BonkRun::StageDamageScale(BonkTier const& tier, BonkLevel const& level, uint32 stage)
{
    float const stages = float(stage - 1);
    return tier.damageMultiplier * level.damage * (1.0f + tier.stageDamageGrowth * stages + tier.stageDamageAccel * stages * std::max(0.0f, stages - 1.0f));
}

float BonkRun::GetStageHealthMultiplier() const
{
    return StageHealthScale(m_tier, m_levelRow, m_stage) * (1.0f + m_bonuses.enemyHealth / 100.0f) * (IsChallenge() ? BONK_DUNGEON_HEALTH : 1.0f);
}

float BonkRun::GetStageDamageMultiplier() const
{
    return StageDamageScale(m_tier, m_levelRow, m_stage) * (1.0f + m_bonuses.enemyDamage / 100.0f) * (IsChallenge() ? BONK_DUNGEON_DAMAGE : 1.0f);
}

void BonkRun::UpdateGear()
{
    BonkGearTotals gear = m_sandboxBench ? sBonkMgr.EvaluateItems(m_sandboxBenchItems) : sBonkMgr.EvaluateGear(m_player);
    if (!m_sandboxBench)
        AddEnchantStats(gear.passives);
    bool const passivesChanged = !std::equal(std::begin(gear.passives), std::end(gear.passives), std::begin(m_gear.passives));
    m_gear = gear;

    if (passivesChanged)
        ApplyPassives();
    UpdateWornTrinkets();

    SyncGearCards();
}

uint32 BonkRun::MitigateDamage(uint32 damage)
{
    if (!damage)
        return 0;

    if (m_away || IsCardImmune() || IsIceBlocked() || IsHiddenInSmoke() || IsDivinelyShielded() || BlockWithDivineShield(damage))
        return 0;

    damage = ApplyDruidDamageTaken(ApplyPaladinDamageTaken(ApplyPriestDamageTaken(ApplyTalentDamageTaken(damage))));
    damage = uint32(damage * GetTrinketDamageTaken());
    OnTrinketHitTaken();
    m_hurt = true;
    m_lastHurtMs = m_totalMs;

    if (float const reduction = GetArmorReduction())
    {
        uint32 const before = damage;
        damage = std::max(1u, uint32(damage * (1.0f - reduction)));
        m_armorUnreported += before - damage;
    }

    m_shieldIdleMs = 0;
    uint32 const unshielded = damage;

    if (m_spellShield)
    {
        uint32 const absorbed = AbsorbSpellShield(damage);
        damage -= absorbed;
        OnPriestShieldAbsorbed(absorbed);
        if (!m_spellShield && HasBehavior(BONK_BEHAVIOR_WARD))
            m_wardBurst = true;
        if (!m_spellShield)
        {
            BreakBloodShield();
            OnShieldBroken();
        }
    }
    m_damageAbsorbed += unshielded - damage;
    damage = PreventDeath(GuardLowHealth(damage));
    if (damage && GetGlyph(BONK_GLYPH_NOMAD))
        m_nomadPenaltyUntilMs = m_totalMs + uint32(GetGlyphValue(BONK_GLYPH_NOMAD, 2));
    if (!m_spellShield)
        m_player->RemoveAurasDueToSpell(BONK_SPELL_SHIELD_VISUAL);
    NoteHeroHit(unshielded, damage);

    m_minuteDamage += damage;
    m_damageTaken += damage;
    m_lastStrikeDamage = damage;
    m_recentHits.emplace_back(m_totalMs, damage);
    OnCardHitTaken();
    return damage;
}

void BonkRun::AddNecroticStack()
{
    if (m_necroticStacks < BONK_NECROTIC_MAX_STACKS)
        SetNecroticStacks(m_necroticStacks + 1);
}

void BonkRun::SetNecroticStacks(uint32 stacks)
{
    if (stacks == m_necroticStacks)
        return;

    bool const had = m_necroticStacks > 0;
    m_necroticStacks = stacks;
    if (stacks && !had && m_player->IsAlive())
        m_player->AddAura(BONK_SPELL_NECROTIC);
    else if (!stacks && had)
        m_player->RemoveAurasDueToSpell(BONK_SPELL_NECROTIC);
    m_player->GetBonkPlayer()->SendAddon("necrotic~" + std::to_string(stacks) + "~" + Percent(stacks * BONK_NECROTIC_PER_STACK));
}

void BonkRun::UpdateNecrotic(uint32 diff)
{
    if (!m_necroticStacks)
        return;
    m_necroticCheckMs += diff;
    if (m_necroticCheckMs < BONK_NECROTIC_CHECK_MS)
        return;
    m_necroticCheckMs = 0;

    Map* map = m_player->GetMap();
    for (ObjectGuid const& guid : GetEnemyGuids())
        if (Creature const* mob = map->GetCreature(guid))
            if (mob->IsAlive())
                if (BonkMobAI const* ai = dynamic_cast<BonkMobAI const*>(mob->AI()))
                    if (float const range = ai->GetNecroticRange())
                        if (m_player->IsWithinDistInMap(mob, range))
                            return;
    SetNecroticStacks(0);
}

uint32 BonkRun::ReduceHealing(uint32 heal) const
{
    float const cut = m_necroticStacks * BONK_NECROTIC_PER_STACK + m_bossWoundStacks * m_bossWoundCut + m_bonuses.healingCut;
    if (cut <= 0.0f)
        return heal;
    float const kept = 1.0f - std::min(100.0f, cut) / 100.0f;
    return uint32(heal * kept);
}

uint32 BonkRun::GetEvolveRank() const
{
    return BONK_EVOLVE_RANK > m_bonuses.earlierEvolutions ? BONK_EVOLVE_RANK - m_bonuses.earlierEvolutions : 1;
}

void BonkRun::BringBossEarlier()
{
    uint32 const earlier = std::min(m_bonuses.earlierBossSeconds, m_map.durationSeconds / 2);
    m_map.durationSeconds -= earlier;
}

void BonkRun::OnAffixedDeath(Creature* mob)
{
    BonkMobAI const* ai = dynamic_cast<BonkMobAI const*>(mob->AI());
    if (!ai)
        return;

    if (ai->IsBolstering())
        for (Creature* other : GetMobsInRange(BONK_BOLSTER_RANGE, mob))
            if (BonkMobAI* otherAi = dynamic_cast<BonkMobAI*>(other->AI()))
                if (otherAi->GetKind() == BONK_SPAWN_NORMAL || otherAi->GetKind() == BONK_SPAWN_ELITE)
                    otherAi->Bolster();

    if (ai->IsSanguine())
    {
        Pool pool;
        pool.x = mob->GetPositionX();
        pool.y = mob->GetPositionY();
        pool.z = mob->GetPositionZ();
        pool.leftMs = BONK_SANGUINE_DURATION_MS;
        pool.marker = NextBonkAuraKey();
        SendBonkMark(m_player, GetSanguineMarkKey(pool.marker), BONK_TELEGRAPH_CIRCLE, pool.x, pool.y, pool.z, 0.0f, BONK_SANGUINE_RADIUS, 0,
            true, BONK_TELEGRAPH_STYLE_BLOOD, ObjectGuid(), "", 0, true);
        m_pools.push_back(pool);
    }
}

void BonkRun::SpawnPhaseAdds(Creature const* boss, uint32 count, uint32 elites)
{
    for (uint32 i = 0; i < count + elites; ++i)
    {
        if (m_mobs.size() >= m_tier.maxAlive)
            return;
        BonkMapCreature const* pick = PickCreature(i < count ? BONK_SPAWN_NORMAL : BONK_SPAWN_ELITE);
        if (!pick)
            continue;
        float const angle = 2.0f * M_PI_F * i / float(count + elites) + frand(-0.3f, 0.3f);
        float const distance = frand(BONK_PHASE_ADD_MIN, BONK_PHASE_ADD_MAX);
        float const x = boss->GetPositionX() + distance * std::cos(angle);
        float const y = boss->GetPositionY() + distance * std::sin(angle);
        float const z = m_player->GetMap()->GetHeight(x, y, boss->GetPositionZ() + 5.0f, true, 15.0f);
        if (z <= INVALID_HEIGHT || !boss->IsWithinLOS(x, y, z + 2.0f))
            continue;
        if (Creature* mob = SpawnMobAt(*pick, x, y, z))
        {
            if (BonkMobAI* ai = dynamic_cast<BonkMobAI*>(mob->AI()))
                ai->Arrive(false);
            mob->CastSpell(mob, BONK_SPELL_BOSS_ADD_SMOKE, true);
        }
    }
}

void BonkRun::SpawnWards(Creature const* boss, uint32 count, float healthShare, float healPercent)
{
    BonkMapCreature ward;
    ward.entry = BONK_WARD_ENTRY;
    ward.scale = BONK_WARD_SCALE;
    float const turn = frand(0.0f, 2.0f * M_PI_F);
    for (uint32 i = 0; i < count; ++i)
    {
        float const angle = turn + 2.0f * M_PI_F * i / float(count);
        float const distance = frand(BONK_WARD_MIN_DISTANCE, BONK_WARD_MAX_DISTANCE);
        float const x = boss->GetPositionX() + distance * std::cos(angle);
        float const y = boss->GetPositionY() + distance * std::sin(angle);
        float const z = m_player->GetMap()->GetHeight(x, y, boss->GetPositionZ() + 5.0f, true, 15.0f);
        if (z <= INVALID_HEIGHT || !boss->IsWithinLOS(x, y, z + 2.0f))
            continue;
        Creature* mob = SpawnMobAt(ward, x, y, z);
        if (!mob)
            continue;
        uint32 const health = std::max(1u, uint32(boss->GetMaxHealth() * healthShare));
        mob->SetModifierValue(UNIT_MOD_HEALTH, BASE_VALUE, float(health));
        mob->SetMaxHealth(health);
        mob->SetHealth(health);
        if (BonkMobAI* ai = dynamic_cast<BonkMobAI*>(mob->AI()))
            ai->SetWard(boss->GetObjectGuid(), healPercent);
        mob->CastSpell(mob, BONK_SPELL_BOSS_ADD_SMOKE, true);
    }
}

void BonkRun::StartVoidZone(BonkEnemyHit const& hit, float x, float y, float z, float radius, uint32 durationMs)
{
    if (m_state != STATE_RUNNING || m_paused)
        return;
    bool const vortex = hit.impactKit == BONK_KIT_SAND_VORTEX && sSpellMgr.GetSpellEntry(BONK_SPELL_SAND_VORTEX_LOOK);
    if (vortex)
        durationMs = BONK_SAND_VORTEX_LIFE_MS;
    VoidZone zone;
    zone.hit = hit;
    zone.x = x;
    zone.y = y;
    zone.z = z;
    zone.radius = radius;
    zone.leftMs = durationMs;
    zone.tickMs = 0;
    zone.markKey = NextBonkAuraKey();
    SendBonkMark(m_player, "zone" + std::to_string(zone.markKey), BONK_TELEGRAPH_CIRCLE, x, y, z, 0.0f, radius, 0, true,
        BONK_TELEGRAPH_STYLE_DANGER, ObjectGuid(), "", 0, true);
    if (vortex)
    {
        zone.vortexKey = NextBonkAuraKey();
        SendBonkAuraAt(m_player, zone.vortexKey, BONK_SPELL_SAND_VORTEX_LOOK, x, y, z);
    }
    m_voidZones.push_back(zone);
}

void BonkRun::EndVoidZone(VoidZone const& zone)
{
    if (zone.markKey)
        SendBonkUnmark(m_player, "zone" + std::to_string(zone.markKey));
    if (zone.vortexKey)
        SendBonkAuraGone(m_player, zone.vortexKey);
}

void BonkRun::MarkHero(BonkEnemyHit const& burst, BonkEnemyHit const& pool, float radius, uint32 followMs, uint32 warnMs, uint32 poolMs)
{
    if (m_state != STATE_RUNNING || m_paused || IsHeroMarked() || !m_player->IsAlive())
        return;
    m_heroMark.burst = burst;
    m_heroMark.pool = pool;
    m_heroMark.radius = radius;
    m_heroMark.leftMs = std::max(1u, followMs);
    m_heroMark.warnMs = warnMs;
    m_heroMark.poolMs = poolMs;
    ShowHeroRing(BonkHeroRing::MarkOutline, radius);
    ShowHeroRing(BonkHeroRing::MarkFill, radius, m_heroMark.leftMs);
}

void BonkRun::UpdateHeroMark(uint32 diff)
{
    if (!IsHeroMarked())
        return;
    if (m_heroMark.leftMs > diff)
    {
        m_heroMark.leftMs -= diff;
        return;
    }
    m_heroMark.leftMs = 0;
    HideHeroMarkRings();
    if (!m_player->IsAlive())
        return;

    float x, y, z;
    GetHeroPositionNow(m_player, x, y, z);
    HeroMark const mark = m_heroMark;
    AddTelegraph(BONK_TELEGRAPH_CIRCLE, x, y, z, 0.0f, mark.radius, mark.warnMs, [this, mark, x, y, z]()
    {
        Erupt(mark.burst, x, y, z, mark.radius);
        if (mark.poolMs)
            StartVoidZone(mark.pool, x, y, z, mark.radius, mark.poolMs);
    });
}

void BonkRun::HideHeroMarkRings()
{
    HideHeroRing(BonkHeroRing::MarkOutline);
    HideHeroRing(BonkHeroRing::MarkFill);
}

void BonkRun::AddBossWound(uint32 maxStacks, float cutPercent, uint32 durationMs)
{
    if (!maxStacks || cutPercent <= 0.0f || !m_player->IsAlive())
        return;
    m_bossWoundCut = cutPercent;
    m_bossWoundLeftMs = durationMs;
    SetBossWoundStacks(std::min(maxStacks, m_bossWoundStacks + 1));
}

void BonkRun::SetBossWoundStacks(uint32 stacks)
{
    if (stacks == m_bossWoundStacks)
        return;
    m_bossWoundStacks = stacks;
    m_player->GetBonkPlayer()->SendAddon("wounds~" + std::to_string(stacks) + "~" + Percent(stacks * m_bossWoundCut));
}

void BonkRun::UpdateBossWounds(uint32 diff)
{
    if (!m_bossWoundStacks)
        return;
    if (m_bossWoundLeftMs > diff)
    {
        m_bossWoundLeftMs -= diff;
        return;
    }
    m_bossWoundLeftMs = 0;
    SetBossWoundStacks(0);
}

void BonkRun::ClearBossPressure()
{
    m_heroMark.leftMs = 0;
    HideHeroMarkRings();
    m_bossWoundLeftMs = 0;
    SetBossWoundStacks(0);
}

void BonkRun::UpdateVoidZones(uint32 diff)
{
    for (auto itr = m_voidZones.begin(); itr != m_voidZones.end();)
    {
        itr->tickMs += diff;
        if (itr->tickMs >= IN_MILLISECONDS)
        {
            itr->tickMs -= IN_MILLISECONDS;
            Erupt(itr->hit, itr->x, itr->y, itr->z, itr->radius, false, false);
        }
        itr->vortexMs -= int32(diff);
        uint32 const burstKit = itr->hit.impactKit ? itr->hit.impactKit : BONK_KIT_FIRE_BURST;
        uint32 const burstMs = GetPoolBurstMs(burstKit);
        if (!itr->vortexKey && itr->vortexMs <= 0 && itr->leftMs >= burstMs)
        {
            itr->vortexMs += BONK_VOID_LOOK_EVERY_MS;
            ShowKitOnStandIn(burstKit, itr->x, itr->y, itr->z, burstMs);
        }
        itr->leftMs = itr->leftMs > diff ? itr->leftMs - diff : 0;
        if (itr->leftMs)
        {
            ++itr;
            continue;
        }
        EndVoidZone(*itr);
        itr = m_voidZones.erase(itr);
    }
}

void BonkRun::UpdatePools(uint32 diff)
{
    if (m_pools.empty())
        return;

    m_poolTimer += diff;
    bool const tick = m_poolTimer >= IN_MILLISECONDS;
    if (tick)
        m_poolTimer -= IN_MILLISECONDS;

    Map* map = m_player->GetMap();
    for (auto itr = m_pools.begin(); itr != m_pools.end();)
    {
        if (tick)
            for (ObjectGuid const& guid : GetEnemyGuids())
                if (Creature* mob = map->GetCreature(guid))
                    if (mob->IsAlive() && mob->GetHealth() < mob->GetMaxHealth() &&
                        mob->GetDistance2d(itr->x, itr->y) <= BONK_SANGUINE_RADIUS)
                        mob->ModifyHealth(int32(std::max(1.0f, mob->GetMaxHealth() * BONK_SANGUINE_HEAL)));

        itr->leftMs = itr->leftMs > diff ? itr->leftMs - diff : 0;
        if (itr->leftMs)
        {
            ++itr;
            continue;
        }
        SendBonkUnmark(m_player, GetSanguineMarkKey(itr->marker));
        itr = m_pools.erase(itr);
    }
}

void BonkRun::DespawnPools()
{
    for (Pool const& pool : m_pools)
        SendBonkUnmark(m_player, GetSanguineMarkKey(pool.marker));
    m_pools.clear();
}

uint32 BonkRun::GetMaxShield() const
{
    float const shield = GetPassiveTotal(BONK_PASSIVE_SHIELD);
    return shield > 0.0f ? std::min(GetSpellShieldCap(), uint32(m_player->GetMaxHealth() * shield / 100.0f)) : 0;
}

BonkSupportSource BonkRun::ShieldStatSupport() const
{
    BonkSupportSource const passive = PassiveSupport(BONK_PASSIVE_SHIELD);
    return passive.name.empty() ? CapstoneSupport(BONK_CAPSTONE_JUGGERNAUT) : passive;
}

void BonkRun::RefillShield(uint32 target)
{
    if (m_spellShield >= target)
        return;
    SupportScope scope(*this, ShieldStatSupport());
    AddSpellShield(target - m_spellShield);
}

void BonkRun::UpdateShield(uint32 diff)
{
    uint32 const max = GetMaxShield();
    if (m_spellShield < max)
    {
        m_shieldIdleMs += diff;
        if (m_shieldIdleMs >= BONK_SHIELD_REGEN_DELAY_MS)
            RefillShield(max);
    }
    RechargeJuggernautShield(diff, max);

    uint32 const total = m_spellShield;
    if (m_player->IsAlive())
    {
        bool const shown = m_player->HasAura(BONK_SPELL_SHIELD_VISUAL);
        if (total && !shown)
            m_player->AddAura(BONK_SPELL_SHIELD_VISUAL, ADD_AURA_PERMANENT);
        else if (!total && shown)
            m_player->RemoveAurasDueToSpell(BONK_SPELL_SHIELD_VISUAL);
    }

    m_shieldSendTimer += diff;
    if ((total != m_shieldSent || max != m_shieldMaxSent) && m_shieldSendTimer >= BONK_SHIELD_SEND_MS)
    {
        m_shieldSendTimer = 0;
        m_shieldSent = total;
        m_shieldMaxSent = max;
        m_player->GetBonkPlayer()->SendAddon("shield~" + std::to_string(total) + "~" + std::to_string(max));
    }
}

BonkRun* BonkRun::FromAttack(SpellCaster const* attacker, Unit const* victim)
{
    Unit const* unit = attacker ? attacker->ToUnit() : nullptr;
    if (!unit || !victim || !victim->IsCreature())
        return nullptr;

    Player const* player = unit->GetCharmerOrOwnerPlayerOrPlayerItself();
    if (!player)
        return nullptr;

    BonkMobAI const* ai = dynamic_cast<BonkMobAI const*>(victim->AI());
    if (!ai)
        return nullptr;
    if (ai->GetOwnerGuid() != player->GetObjectGuid() && !(player->GetGroup() && player->GetGroup()->IsMember(ai->GetOwnerGuid())))
        return nullptr;
    return player->GetBonkPlayer()->GetRun();
}

float BonkRun::GetHealthScale(Unit const* victim)
{
    BonkMobAI const* ai = victim && victim->IsCreature() ? dynamic_cast<BonkMobAI const*>(victim->AI()) : nullptr;
    return ai ? 1.0f / ai->GetHealthOverflow() : 1.0f;
}

uint64 BonkRun::GetTrueAmount(Unit const* victim, uint32 amount)
{
    BonkMobAI const* ai = victim && victim->IsCreature() ? dynamic_cast<BonkMobAI const*>(victim->AI()) : nullptr;
    return ai ? ai->GetTrueHealth(amount) : amount;
}

uint32 BonkRun::ToDamage(double damage)
{
    return damage <= 0.0 ? 0 : uint32(std::min(damage, double(MaxHitDamage)));
}

uint32 BonkRun::GetMissileDelay(SpellCaster const* caster, Unit const* target, float speed)
{
    if (speed <= 0.0f || !caster || !FromAttack(caster, target))
        return 0;

    float x = caster->GetPositionX(), y = caster->GetPositionY(), z = caster->GetPositionZ();
    if (Player const* hero = caster->ToPlayer())
        GetHeroPositionNow(hero, x, y, z);
    float const t = target->GetDistance3dToCenter(x, y, z) / speed;
    return std::max(1u, uint32(t * IN_MILLISECONDS));
}

float BonkRun::GetCritChance(SpellEntry const* spell, Unit const* victim, Unit const* attacker) const
{
    float const chance = GetPaladinCritChance(spell, GetRogueCritChance(spell,
        BONK_BASE_CRIT + GetPassiveTotal(BONK_PASSIVE_CRIT) + GetTalentCritChance(victim, attacker)));
    return IsAlwaysCrit(spell, victim) ? 100.0f + chance : chance;
}

bool BonkRun::IsAlwaysCrit(SpellEntry const* spell, Unit const* victim) const
{
    if (IsReckless() || IsFrostSpellOnFrozen(spell, victim) || IsWeaponMasterStrike(spell) || IsLavaBurstOnBurning(spell, victim) ||
        IsFreshMeat(spell, victim))
        return true;
    if (!spell)
        return false;
    return std::any_of(m_weapons.begin(), m_weapons.end(), [spell](OwnedWeapon const& owned)
    {
        return owned.weapon.spellId == spell->Id && (owned.weapon.HasFlag(BONK_WEAPON_FLAG_ALWAYS_CRIT) || owned.forcedCrit);
    });
}

float BonkRun::GetCritBonus(bool weaponAttack, Unit const* victim) const
{
    float bonus = (weaponAttack ? 1.0f : 0.5f) + std::max(0.0f, GetPassiveTotal(BONK_PASSIVE_CRIT_DAMAGE)) / 100.0f;
    if (victim && (IsFrozen(victim) || (victim->HasAuraType(SPELL_AURA_MOD_ROOT) && HasBehavior(BONK_BEHAVIOR_FREEZE))))
        bonus += BONK_FROZEN_CRIT_BONUS + m_mastery.Special(BONK_SPECIAL_WINTERS_CHILL) / 100.0f;
    if (IsEnraged())
        bonus += m_mastery.Special(BONK_SPECIAL_RECKLESS_ABANDON) * m_mastery.EnrageBoost() / 100.0f;
    return bonus;
}

float BonkRun::RollCritMultiplier(bool weaponAttack, Unit const* victim, SpellEntry const* spell, Unit const* attacker) const
{
    float const bonus = GetCritBonus(weaponAttack, victim);
    float multiplier = (1.0f + bonus) * (1.0f + GetCapstoneCritDamage(spell));
    ++m_crits;

    float const overcrit = GetCritChance(spell, victim, attacker == m_player ? nullptr : attacker) - 100.0f;
    if (overcrit > 0.0f && roll_chance_f(overcrit))
        multiplier *= 1.0f + bonus;
    return multiplier;
}

float BonkRun::GetMinionScale(Unit const* attacker) const
{
    return IsSummon(attacker) ? 1.0f + GetPackPowerShare() : 1.0f;
}

float BonkRun::GetDamageMultiplier(SpellEntry const* spell, Unit const* victim, float minionScale, float extraPower) const
{
    float percent = (GetRunPower() + GetBuffPower() + GetCardPower(victim) + GetGlyphPower() + extraPower +
        GetCastChargePower(spell, victim));
    float const mark = GetKeywordDamageMultiplier(spell, victim) * GetMageDamageMultiplier(victim) *
        GetShamanDamageMultiplier(spell, victim) * GetPriestDamageMultiplier(victim) * GetRogueDamageMultiplier(victim) *
        GetPaladinDamageMultiplier(victim) * GetTrinketDamageMultiplier();
    if (!spell)
        return std::max(0.0f, 1.0f + percent / 100.0f) * GetGearDamageScale() * mark * GetCapstoneDamageMultiplier(false, victim) * minionScale;

    uint32 const school = spell->GetSpellSchoolMask();
    auto root = m_spellRoots.find(spell->Id);
    if (root != m_spellRoots.end())
    {
        auto bonus = m_mastery.spellDamage.find(root->second);
        if (bonus != m_mastery.spellDamage.end())
            percent += bonus->second;
        percent += GetSpecDamage(root->second) + GetTormentBonus(root->second, victim);
        if (root->second == BONK_WEAPON_AIMED_SHOT && victim && m_mastery.Special(BONK_SPECIAL_KILL_SHOT) > 0.0f &&
            victim->GetHealthPercent() < m_mastery.Special(BONK_SPECIAL_KILL_SHOT))
            if (BonkMobAI const* ai = dynamic_cast<BonkMobAI const*>(victim->ToCreature() ? victim->ToCreature()->AI() : nullptr))
                if (ai->GetKind() == BONK_SPAWN_NORMAL)
                    percent += BONK_KILL_SHOT_PERCENT;
    }
    percent += GetTalentDamagePercent();
    for (auto const& bonus : m_mastery.schoolDamage)
        if (bonus.first & school)
            percent += bonus.second;

    if (victim)
    {
        if (m_mastery.shatter > 0.0f && (victim->HasAuraType(SPELL_AURA_MOD_DECREASE_SPEED) || victim->HasAuraType(SPELL_AURA_MOD_ROOT)))
            percent += m_mastery.shatter;

        float const health = victim->GetHealthPercent();
        if (health > BONK_MASTERY_OPENER_HEALTH)
            for (auto const& bonus : m_mastery.opener)
                if (!bonus.first || (bonus.first & school))
                    percent += bonus.second;
        if (health < BONK_MASTERY_EXECUTE_HEALTH)
            for (auto const& bonus : m_mastery.execute)
                if (!bonus.first || (bonus.first & school))
                    percent += bonus.second;
    }

    float multiplier = std::max(0.0f, 1.0f + percent / 100.0f) * GetGearDamageScale() * mark * GetCapstoneDamageMultiplier(true, victim) * minionScale *
        GetGlyphDamageMultiplier(spell);

    if (victim)
        for (OwnedWeapon const& owned : m_weapons)
            if (owned.weapon.behavior == BONK_BEHAVIOR_EXECUTE && owned.weapon.spellId == spell->Id &&
                victim->GetHealthPercent() < owned.weapon.behaviorValue)
                multiplier *= owned.weapon.altDamage + m_mastery.Special(BONK_SPECIAL_ANNIHILATOR);
    return multiplier;
}

uint32 BonkRun::GetRootWeapon(BonkWeapon const& weapon) const
{
    auto itr = m_spellRoots.find(weapon.spellId);
    return itr != m_spellRoots.end() ? itr->second : weapon.id;
}

void BonkRun::SoulSiphon(uint32 killingSpell)
{
    if (m_mastery.soulSiphon.empty() || !killingSpell || !m_player->IsAlive())
        return;
    SpellEntry const* spell = sSpellMgr.GetSpellEntry(killingSpell);
    if (!spell)
        return;

    float percent = 0.0f;
    for (auto const& siphon : m_mastery.soulSiphon)
        if (siphon.first & spell->GetSpellSchoolMask())
            percent += siphon.second;

    uint32 const now = WorldTimer::getMSTime();
    if (WorldTimer::getMSTimeDiff(m_siphonWindow, now) >= IN_MILLISECONDS)
    {
        m_siphonWindow = now;
        m_siphonHealed = 0.0f;
    }
    percent = std::min(percent, BONK_MASTERY_SIPHON_CAP - m_siphonHealed);
    if (percent <= 0.0f)
        return;
    m_siphonHealed += percent;
    SupportScope scope(*this, TalentSupport(BONK_MASTERY_SOUL_SIPHON));
    m_player->ModifyHealth(int32(ReduceHealing(uint32(std::max(1.0f, m_player->GetMaxHealth() * percent / 100.0f)))));
}

void BonkRun::AwardMastery()
{
    if (m_masteryAwarded || m_sandbox)
        return;
    m_masteryAwarded = true;
    m_player->GetBonkPlayer()->AddMasteryXp(m_rewardMs / (MINUTE * IN_MILLISECONDS) * m_tier.masteryXpMinute + m_masteryXp);
}

uint32 BonkRun::PreventDeath(uint32 damage)
{
    if (!damage || damage < m_player->GetHealth() || !SaveFromDeath())
        return damage;
    return 0;
}

bool BonkRun::SaveFromDeath()
{
    if (m_sandbox || !m_player->IsAlive() || (m_state != STATE_RUNNING && m_state != STATE_PORTAL))
        return false;
    if (m_state == STATE_RUNNING && IsDivineShieldReady())
        Revive(false, GetGlyph(BONK_GLYPH_DIVINE_SHIELD), true);
    else if (m_revives)
        Revive(false, nullptr, true);
    else if (m_soulstones && m_state == STATE_RUNNING)
        Revive(true, nullptr, true);
    else
        return false;
    return true;
}

void BonkRun::Revive(bool soulstone, BonkGlyph const* glyph, bool stillAlive)
{
    if (glyph)
        m_divineShieldStage = m_stage;
    else if (soulstone)
        --m_soulstones;
    else
        --m_revives;
    BonkCard const* stone = m_cardByEffect[BONK_CARD_SOULSTONE];
    float const restore = glyph ? glyph->values[0] / 100.0f : soulstone && stone ? stone->values[0] / 100.0f : 0.5f;
    if (stillAlive)
        m_player->SetHealth(std::max(m_player->GetHealth(), std::max(1u, uint32(m_player->GetMaxHealth() * restore))));
    else
    {
        m_player->ResurrectPlayer(restore);
        m_player->SpawnCorpseBones();
    }
    m_player->CastSpell(m_player, BONK_SPELL_REVIVE_SHIELD, true);
    SmiteAroundRevive();

    if (glyph)
        m_player->GetBonkPlayer()->Notify(BONK_STRING_GLYPH_OF_DIVINE_SHIELD_SAVED, { DescribeDivineShieldRecharge(*glyph) });
    else if (soulstone)
        m_player->GetBonkPlayer()->Notify(!m_soulstones ? BONK_STRING_SOULSTONE_SAVED_YOU : m_soulstones == 1 ? BONK_STRING_SOULSTONE_SAVED_YOU_CHARGE_LEFT :
            BONK_STRING_SOULSTONE_SAVED_YOU_CHARGES_LEFT, { std::to_string(m_soulstones) });
    else
        m_player->GetBonkPlayer()->Notify(!m_revives ? BONK_STRING_YOU_GOT_BACK_UP : m_revives == 1 ? BONK_STRING_YOU_GOT_BACK_UP_REVIVE_LEFT :
            BONK_STRING_YOU_GOT_BACK_UP_REVIVES_LEFT, { std::to_string(m_revives) });
    SendState();
}

void BonkRun::SmiteAroundRevive()
{
    m_player->SendPlaySpellVisualKit(BONK_KIT_HOLY_NOVA);
    SpellEntry const* nova = sSpellMgr.GetSpellEntry(BONK_SPELL_REVIVE_NOVA);
    if (!nova)
        return;

    std::vector<Creature*> smitten;
    Map* map = m_player->GetMap();
    for (ObjectGuid const& guid : GetEnemyGuids())
        if (Creature* mob = map->GetCreature(guid))
            if (mob->IsAlive() && m_player->IsWithinDistInMap(mob, BONK_REVIVE_CLEAR_RADIUS))
                if (BonkMobAI* ai = dynamic_cast<BonkMobAI*>(mob->AI()))
                    if (ai->GetKind() == BONK_SPAWN_NORMAL)
                        smitten.push_back(mob);

    for (Creature* mob : smitten)
    {
        mob->SendPlaySpellVisualKit(BONK_KIT_REVIVE_SMITE);
        DealSpellHit(nova, mob, GetTrueAmount(mob, mob->GetMaxHealth()) * BONK_REVIVE_OVERKILL);
    }
}

Creature* BonkRun::SpawnMob(BonkMapCreature const& creature)
{
    float x, y, z;
    if (!FindMobPosition(x, y, z, m_map.spawnMinDistance, m_map.spawnMaxDistance))
        return nullptr;
    return SpawnMobAt(creature, x, y, z);
}

bool BonkRun::RollVeteran(BonkMapCreature const& creature) const
{
    if (m_stress.on || creature.kind != BONK_SPAWN_NORMAL || m_tier.veteranChance <= 0.0f)
        return false;
    if (creature.archetype != BONK_ARCHETYPE_GRUNT && creature.archetype != BONK_ARCHETYPE_CASTER && creature.archetype != BONK_ARCHETYPE_CHARGER)
        return false;
    return roll_chance_f(m_tier.veteranChance + m_levelRow.veteranChance);
}

Creature* BonkRun::SpawnMobAt(BonkMapCreature const& creature, float x, float y, float z, bool veteran)
{
    float const facing = std::atan2(m_player->GetPositionY() - y, m_player->GetPositionX() - x);
    m_lastPlateId = m_lastPlateId % BONK_MAX_PLATE_ID + 1;
    BonkMobAI::SetNextPlateId(m_lastPlateId);
    uint32 const level = std::min(GetMobLevel() + GetKindLevelBonus(creature.kind), 63u);
    BonkMobAI::SetNextLevel(level);
    Creature* mob = m_player->SummonCreature(creature.entry, x, y, z, facing, TEMPSUMMON_CORPSE_TIMED_DESPAWN, 2000, false, 0, &BonkMobAI::Install);
    if (!mob)
        return nullptr;

    mob->RemoveAllAuras();
    mob->ClearCreatureState(CSTATE_REGEN_HEALTH);
    mob->SetInvincibilityHpThreshold(0);
    mob->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IMMUNE_TO_PLAYER | UNIT_FLAG_IMMUNE_TO_NPC | UNIT_FLAG_NOT_SELECTABLE);

    mob->InitStatsForLevel();
    mob->SetModifierValue(UNIT_MOD_ARMOR, TOTAL_PCT, 0.0f);
    mob->UpdateArmor();

    BonkArchetypeInfo const* archetype = creature.kind == BONK_SPAWN_NORMAL ? &sBonkMgr.GetArchetype(creature.archetype) : nullptr;
    float const archetypeDamage = archetype ? archetype->damageMultiplier : 1.0f;
    float const veteranDamage = (veteran ? BONK_VETERAN_DAMAGE : 1.0f) *
        (creature.kind == BONK_SPAWN_MINIBOSS ? 1.0f + GetGlyphValue(BONK_GLYPH_AMBITION, 2) / 100.0f : 1.0f);
    float const fullHealth = GetSpawnHealthUncapped(creature.kind, creature.archetype, creature.healthMultiplier * (veteran ? BONK_VETERAN_HEALTH : 1.0f));
    uint32 const health = uint32(std::min(BONK_MAX_MOB_HEALTH, fullHealth));
    mob->SetModifierValue(UNIT_MOD_HEALTH, BASE_VALUE, float(health));
    mob->SetMaxHealth(health);
    mob->SetHealth(health);
    if (BonkMobAI* ai = dynamic_cast<BonkMobAI*>(mob->AI()))
        ai->SetHealthOverflow(fullHealth / float(health));

    {
        float const damage = std::max(1.0f, GetHeroBaseHealth() * GetScalingNow().hitShare * GetStageDamageMultiplier() *
            m_map.damageMultiplier * creature.damageMultiplier * archetypeDamage * veteranDamage * GetKindDamage(creature.kind) * m_spawnDamageScale);
        mob->SetAttackTime(BASE_ATTACK, BONK_MOB_SWING_MS);
        mob->SetBaseWeaponDamage(BASE_ATTACK, MINDAMAGE, damage * 0.85f);
        mob->SetBaseWeaponDamage(BASE_ATTACK, MAXDAMAGE, damage * 1.15f);
        mob->UpdateDamagePhysical(BASE_ATTACK);
    }

    mob->SetFactionTemplateId(BONK_ENEMY_FACTION);
    mob->SetBaseSpeedRate(MOVE_WALK, creature.speed);
    mob->SetBaseSpeedRate(MOVE_RUN, creature.speed);
    if (creature.scale > 1.0f)
    {
        mob->SetObjectScale(mob->GetObjectScale() * creature.scale);
        mob->UpdateModelData();
    }

    if (archetype && creature.archetype == BONK_ARCHETYPE_GRUNT)
        m_gruntRadius = std::max(m_gruntRadius, mob->GetObjectBoundingRadius());
    else if (archetype && creature.archetype == BONK_ARCHETYPE_BRUISER && m_gruntRadius > 0.0f)
    {
        float const radius = mob->GetObjectBoundingRadius();
        float const wanted = BONK_BRUISER_SIZE * m_gruntRadius;
        if (radius > 0.0f && radius < wanted)
        {
            mob->SetObjectScale(mob->GetObjectScale() * std::min(wanted / radius, BONK_BRUISER_MAX_GROWTH));
            mob->UpdateModelData();
        }
    }
    if (veteran)
    {
        mob->SetObjectScale(mob->GetObjectScale() * BONK_VETERAN_SIZE);
        mob->UpdateModelData();
    }
    if (creature.kind == BONK_SPAWN_BOSS || creature.kind == BONK_SPAWN_MINIBOSS)
    {
        float const radius = mob->GetObjectBoundingRadius();
        float const wanted = creature.kind == BONK_SPAWN_BOSS ? BONK_BOSS_MIN_RADIUS : BONK_MINIBOSS_MIN_RADIUS;
        if (radius > 0.0f && radius < wanted)
        {
            mob->SetObjectScale(mob->GetObjectScale() * std::min(wanted / radius, BONK_BOSS_MAX_GROWTH));
            mob->UpdateModelData();
        }
    }
    mob->SetNoXP();

    if (BonkMobAI* ai = dynamic_cast<BonkMobAI*>(mob->AI()))
    {
        ai->SetArchetype(creature.archetype);
        ai->SetVeteran(veteran);
        ai->Setup(creature.kind);
    }

    m_mobs.push_back(mob->GetObjectGuid());
    if (m_sandbox)
    {
        PrepareSandboxMob(mob);
        return mob;
    }
    if (creature.kind == BONK_SPAWN_BOSS || creature.kind == BONK_SPAWN_MINIBOSS)
    {
        m_bossArrivals[mob->GetObjectGuid()] = { m_totalMs, m_damageTaken };
        SpeakBossLine(mob, BONK_BOSS_LINE_ARRIVE);
        bool const boss = creature.kind == BONK_SPAWN_BOSS;
        m_objectives.Follow(m_player, boss ? BONK_OBJECTIVE_BOSS : BONK_OBJECTIVE_MINIBOSS, mob, mob->GetName(), 0,
            boss ? BONK_BOSS_PIN_ICON : BONK_MINIBOSS_PIN_ICON, boss ? "Boss" : "Mini-boss");
    }
    if (creature.kind == BONK_SPAWN_BOSS)
    {
        m_bossGuid = mob->GetObjectGuid();
        SendBossMusic(true);
    }
    return mob;
}

std::vector<Creature*> BonkRun::GetMobsInRange(float range, WorldObject const* center) const
{
    if (!center)
        center = m_player;

    Map* map = m_player->GetMap();
    std::vector<std::pair<float, Creature*>> found;
    for (ObjectGuid const& guid : GetEnemyGuids())
    {
        Creature* mob = map->GetCreature(guid);
        if (!mob || !mob->IsAlive() || mob->HasCreatureState(CSTATE_DESPAWNING))
            continue;

        if (mob == center)
            continue;
        float const distance = center->GetDistance(mob);
        if (distance <= range)
            found.emplace_back(distance, mob);
    }

    std::sort(found.begin(), found.end(), [](auto const& a, auto const& b) { return a.first < b.first; });

    std::vector<Creature*> mobs;
    mobs.reserve(found.size());
    for (auto const& itr : found)
        mobs.push_back(itr.second);
    return mobs;
}

void BonkRun::DespawnMobs()
{
    Map* map = m_player->FindMap();
    if (map)
        for (ObjectGuid const& guid : m_mobs)
            if (Creature* mob = map->GetCreature(guid))
            {
                mob->SetTimeFrozen(false);
                mob->DespawnOrUnsummon();
            }
    if (map)
        for (TimedMob const& emerging : m_emerging)
            if (Creature* mob = map->GetCreature(emerging.guid))
                mob->DespawnOrUnsummon();
    m_emerging.clear();
    m_pendingSpawns.clear();
    m_mobs.clear();
    m_farSince.clear();
    m_crowd.Clear();
    m_packSpots.clear();
}

GameObject* BonkRun::PlaceObject(uint32 entry, float x, float y, float z, uint32 lifetimeSeconds)
{
    return m_player->SummonGameObject(entry, x, y, z, frand(0.0f, 2.0f * M_PI_F), 0.0f, 0.0f, 0.0f, 0.0f, lifetimeSeconds, false);
}

bool BonkRun::SpawnChest(bool free, float x, float y, float z, uint32 choices)
{
    GameObject* chest = PlaceObject(free ? BONK_GO_CHEST_FREE : BONK_GO_CHEST, x, y, z, BONK_OBJECT_LIFETIME_SEC);
    if (!chest)
        return false;
    m_chests.push_back(chest->GetObjectGuid());
    m_chestChoices[chest->GetObjectGuid()] = choices;
    m_chestObjectives[chest->GetObjectGuid()] = m_objectives.Add(m_player, BONK_OBJECTIVE_CHEST, x, y, z, m_player->GetBonkPlayer()->GetLocalName(chest), 0,
        BONK_CHEST_PIN_ICON, GetString(free ? BONK_STRING_OBJECTIVE_CHEST_FREE : BONK_STRING_OBJECTIVE_CHEST_VALOR));
    return true;
}

void BonkRun::ForgetChestObjective(ObjectGuid const& chest)
{
    auto const itr = m_chestObjectives.find(chest);
    if (itr == m_chestObjectives.end())
        return;
    m_objectives.Remove(m_player, itr->second);
    m_chestObjectives.erase(itr);
}

void BonkRun::DropChest(Creature const* mob, BonkSpawnKind kind)
{
    BonkMobAI const* ai = dynamic_cast<BonkMobAI const*>(mob->AI());
    uint32 const affixes = ai ? ai->GetAffixCount() : 0;

    if (kind == BONK_SPAWN_ELITE)
    {
        float const chance = BONK_ELITE_CHEST_CHANCE + BONK_ELITE_CHEST_PER_AFFIX * affixes +
            BONK_ELITE_CHEST_PER_LUCK * std::max(0.0f, GetPassiveTotal(BONK_PASSIVE_LUCK));
        if (m_elitesWithoutChest < BONK_ELITE_CHEST_PITY && !roll_chance_f(chance))
        {
            ++m_elitesWithoutChest;
            return;
        }
        m_elitesWithoutChest = 0;
    }

    uint32 const choices = std::min(uint32(BONK_OFFER_SIZE) + affixes, uint32(BONK_MAX_OFFER_SIZE));
    SpawnChest(true, mob->GetPositionX(), mob->GetPositionY(), mob->GetPositionZ(), choices);
}

bool BonkRun::IsFarFromChests(float x, float y) const
{
    Map* map = m_player->FindMap();
    if (!map)
        return true;
    for (ObjectGuid const& guid : m_chests)
        if (GameObject const* chest = map->GetGameObject(guid))
            if (chest->GetDistance2d(x, y) < BONK_CHEST_MIN_SPACING)
                return false;
    return true;
}

bool BonkRun::SpawnChestNearby()
{
    for (uint32 attempt = 0; attempt < BONK_CHEST_PLACE_ATTEMPTS; ++attempt)
    {
        float x, y, z;
        if (!FindSpawnPosition(x, y, z, BONK_CHEST_MIN_DISTANCE, BONK_CHEST_MAX_DISTANCE))
            continue;
        if (!IsFarFromChests(x, y) || !m_player->IsWithinLOS(x, y, z))
            continue;
        return SpawnChest(false, x, y, z);
    }
    return false;
}

void BonkRun::GainValor(uint32 amount)
{
    m_coins += amount;
    m_valorEarned += amount;
}

uint32 BonkRun::GetChestCost() const
{
    return uint32(m_tier.chestCost * (1.0f + m_tier.chestCostGrowth * m_chestsBought) * (1.0f + m_tier.chestCostMinuteGrowth * GetRunMinutes()) + 0.5f);
}

uint32 BonkRun::GetChestPrice() const
{
    float const haggle = std::min(GetUtility(BONK_UTILITY_HAGGLE), BONK_MAX_HAGGLE);
    return std::max(1u, uint32(GetChestCost() * (1.0f - haggle / 100.0f) + 0.5f));
}

void BonkRun::SpawnPowerup(WorldObject const* at)
{
    bool const magnetBlocked = !m_powerupObjectives.empty() || m_coopOwnXp;
    auto allowed = [magnetBlocked](BonkPowerup const& powerup)
    {
        return !magnetBlocked || powerup.effect != BONK_POWERUP_MAGNET;
    };
    std::vector<BonkPowerup> const& powerups = sBonkMgr.GetPowerups();
    uint32 totalWeight = 0;
    for (BonkPowerup const& powerup : powerups)
        if (allowed(powerup))
            totalWeight += powerup.weight;
    if (!totalWeight)
        return;

    uint32 roll = urand(1, totalWeight);
    BonkPowerup const* chosen = nullptr;
    for (BonkPowerup const& powerup : powerups)
    {
        if (!allowed(powerup))
            continue;
        if (roll <= powerup.weight)
        {
            chosen = &powerup;
            break;
        }
        roll -= powerup.weight;
    }
    if (!chosen)
        return;

    uint32 const lifetime = uint32(BONK_POWERUP_LIFETIME_SEC * (1.0f + GetGlyphValue(BONK_GLYPH_PACKRAT, 0) / 100.0f));
    if (at)
    {
        PlacePowerup(*chosen, at->GetPositionX(), at->GetPositionY(), at->GetPositionZ(), lifetime);
        return;
    }
    for (uint32 attempt = 0; attempt < 5; ++attempt)
    {
        float x, y, z;
        if (!FindSpawnPosition(x, y, z, BONK_POWERUP_MIN_DISTANCE, BONK_POWERUP_MAX_DISTANCE))
            return;
        if (!m_player->IsWithinLOS(x, y, z))
            continue;
        PlacePowerup(*chosen, x, y, z, lifetime);
        return;
    }
}

GameObject* BonkRun::PlacePowerup(BonkPowerup const& powerup, float x, float y, float z, uint32 lifetimeSeconds)
{
    bool const lasting = powerup.effect == BONK_POWERUP_MAGNET;
    GameObject* go = PlaceObject(powerup.entry, x, y, z, lasting ? BONK_OBJECT_LIFETIME_SEC : lifetimeSeconds);
    if (!go)
        return nullptr;
    m_powerups.push_back(go->GetObjectGuid());
    if (lasting)
        m_powerupObjectives[go->GetObjectGuid()] = m_objectives.Add(m_player, BONK_OBJECTIVE_CHEST, x, y, z, m_player->GetBonkPlayer()->GetLocalName(go), 0,
            BONK_MAGNET_PIN_ICON, GetString(BONK_STRING_OBJECTIVE_MAGNET));
    return go;
}

void BonkRun::ForgetPowerupObjective(ObjectGuid const& powerup)
{
    auto const itr = m_powerupObjectives.find(powerup);
    if (itr == m_powerupObjectives.end())
        return;
    m_objectives.Remove(m_player, itr->second);
    m_powerupObjectives.erase(itr);
}

bool BonkRun::PlacePowerupNow(uint32 entry, std::string& error)
{
    std::vector<BonkPowerup> const& powerups = sBonkMgr.GetPowerups();
    auto const powerup = std::find_if(powerups.begin(), powerups.end(), [entry](BonkPowerup const& p) { return p.entry == entry; });
    if (powerup == powerups.end())
    {
        error = GetString(BONK_STRING_THAT_POWER_UP_IS_NOT);
        return false;
    }
    float x, y, z;
    if (!FindSpawnPositionAt(m_player->GetOrientation(), BONK_POWERUP_COMMAND_DISTANCE, x, y, z) || !m_player->IsWithinLOS(x, y, z))
    {
        error = GetString(BONK_STRING_NO_OPEN_GROUND_10_YARDS);
        return false;
    }
    if (!PlacePowerup(*powerup, x, y, z, BONK_POWERUP_LIFETIME_SEC))
    {
        error = GetString(BONK_STRING_POWER_UP_COULD_NOT_BE);
        return false;
    }
    return true;
}

void BonkRun::UpdatePowerups(uint32 diff, bool spawn)
{
    m_powerupCheckTimer += diff;
    if (m_powerupCheckTimer < BONK_POWERUP_CHECK_MS)
        return;
    uint32 const elapsed = m_powerupCheckTimer;
    m_powerupCheckTimer = 0;

    Map* map = m_player->GetMap();
    float heroX, heroY, heroZ;
    GetHeroPositionNow(m_player, heroX, heroY, heroZ);
    std::vector<std::pair<BonkPowerup const*, std::string>> picked;
    for (auto itr = m_powerups.begin(); itr != m_powerups.end();)
    {
        GameObject* go = map->GetGameObject(*itr);
        if (!go || !go->isSpawned())
        {
            ForgetPowerupObjective(*itr);
            itr = m_powerups.erase(itr);
            continue;
        }

        if (GetHeroDistance(m_player, heroX, heroY, heroZ, go) > BONK_POWERUP_PICKUP_RANGE)
        {
            ++itr;
            continue;
        }

        auto powerup = std::find_if(sBonkMgr.GetPowerups().begin(), sBonkMgr.GetPowerups().end(),
            [go](BonkPowerup const& p) { return p.entry == go->GetEntry(); });
        std::string const name = m_player->GetBonkPlayer()->GetLocalName(go);
        ForgetPowerupObjective(go->GetObjectGuid());
        go->Delete();
        itr = m_powerups.erase(itr);
        if (powerup != sBonkMgr.GetPowerups().end())
            picked.emplace_back(&*powerup, name);
    }

    for (auto const& pick : picked)
    {
        bool const thing = pick.first->effect == BONK_POWERUP_MAGNET || pick.first->effect == BONK_POWERUP_VALOR;
        m_player->GetBonkPlayer()->Notify(thing ? BONK_STRING_YOU_PICKED_UP_A : BONK_STRING_YOU_PICKED_UP, { pick.second });
        UsePowerup(*pick.first);
    }

    if (!spawn || !m_map.powerupIntervalSeconds)
        return;

    m_powerupSpawnTimer -= int32(elapsed);
    if (m_powerupSpawnTimer > 0)
        return;
    m_powerupSpawnTimer += m_map.powerupIntervalSeconds * IN_MILLISECONDS;

    if (m_powerups.size() - m_powerupObjectives.size() < m_map.powerupMax)
        SpawnPowerup();
}

void BonkRun::UsePowerup(BonkPowerup const& powerup)
{
    switch (powerup.effect)
    {
        case BONK_POWERUP_MAGNET:
            PullAllCurrency();
            break;
        case BONK_POWERUP_HOLY_NOVA:
        {
            SpellEntry const* spellInfo = sSpellMgr.GetSpellEntry(powerup.spellId);
            if (!spellInfo)
                break;
            m_player->SendPlaySpellVisualKit(BONK_KIT_HOLY_NOVA);
            for (Creature* mob : GetMobsInRange(powerup.value))
            {
                BonkMobAI const* ai = dynamic_cast<BonkMobAI const*>(mob->AI());
                if (ai && ai->GetKind() == BONK_SPAWN_NORMAL && mob->IsAlive())
                    DealSpellHit(spellInfo, mob, float(mob->GetMaxHealth()) * 2.0f);
            }
            break;
        }
        case BONK_POWERUP_FROST_NOVA:
            m_player->SendPlaySpellVisualKit(BONK_KIT_FROST_NOVA);
            for (Creature* mob : GetMobsInRange(powerup.value))
                ApplyKeyword(mob, BONK_KEYWORD_FROZEN, 1);
            break;
        case BONK_POWERUP_VALOR:
            GainValor(std::max(1u, uint32(GetChestCost() * powerup.value + 0.5f)));
            m_player->GetBonkPlayer()->SendAddon("valor");
            SendState();
            break;
        default:
            m_player->CastSpell(m_player, powerup.spellId, true);
            break;
    }
}

void BonkRun::DespawnObjects()
{
    if (Map* map = m_player->FindMap())
    {
        for (ObjectGuid const& guid : m_chests)
        {
            ForgetChestObjective(guid);
            BonkCoop::ForgetHiddenObject(guid);
            if (GameObject* chest = map->GetGameObject(guid))
                chest->Delete();
        }
        for (ObjectGuid const& guid : m_powerups)
        {
            ForgetPowerupObjective(guid);
            if (GameObject* powerup = map->GetGameObject(guid))
                powerup->Delete();
        }
        for (ShardDrop const& drop : m_shardDrops)
            RemovePickup(drop.guid, drop.marker, drop.sparkle);
        for (GlyphDrop const& drop : m_glyphDrops)
            RemovePickup(drop.guid, drop.marker, drop.sparkle);
        for (HealDrop const& drop : m_healDrops)
            RemoveHealDrop(drop, map->GetCreature(drop.guid), false);
        for (GearDrop const& drop : m_gearDrops)
            RemovePickup(drop.guid, drop.marker, drop.swirl, drop.core);
        if (GameObject* portal = map->GetGameObject(m_portalGuid))
            portal->Delete();
    }
    DespawnVault();
    DespawnEnchanter();
    ClearDropLooks();
    m_chests.clear();
    m_chestOpenedBy.clear();
    m_chestObjectives.clear();
    m_powerups.clear();
    m_powerupObjectives.clear();
    for (VoidZone const& zone : m_voidZones)
        EndVoidZone(zone);
    m_voidZones.clear();
    ClearHidden();
    m_shardDrops.clear();
    m_glyphDrops.clear();
    m_healDrops.clear();
    m_gearDrops.clear();
    m_portalGuid.Clear();
}

void BonkRun::DespawnPaidChests()
{
    Map* map = m_player->FindMap();
    for (auto itr = m_chests.begin(); itr != m_chests.end();)
    {
        GameObject* chest = map ? map->GetGameObject(*itr) : nullptr;
        if (chest && chest->GetEntry() == BONK_GO_CHEST_FREE)
        {
            ++itr;
            continue;
        }
        ForgetChestObjective(*itr);
        m_chestChoices.erase(*itr);
        m_chestOpenedBy.erase(*itr);
        BonkCoop::ForgetHiddenObject(*itr);
        if (chest)
            chest->Delete();
        itr = m_chests.erase(itr);
    }
}

void BonkRun::UpdateObjectives(uint32 diff)
{
    m_spawnPoints.Update(m_player, m_map.zoneId, diff);
    LearnStarts(m_spawnPoints.TakeFresh());
    for (BonkObjective const& gone : m_objectives.Update(m_player, diff))
        m_spawnPoints.Release(gone.x, gone.y);
}

uint32 BonkRun::AddObjective(BonkObjectiveKind kind, float x, float y, float z, std::string const& label, uint32 seconds)
{
    return m_objectives.Add(m_player, kind, x, y, z, label, seconds);
}

void BonkRun::RemoveObjective(uint32 id)
{
    if (BonkObjective const* objective = m_objectives.Get(id))
    {
        if (objective->follows.IsEmpty())
            m_spawnPoints.Release(objective->x, objective->y);
        m_objectives.Remove(m_player, id);
    }
}

bool BonkRun::OffersChest(ObjectGuid chest, Player* opener) const
{
    if (std::find(m_chests.begin(), m_chests.end(), chest) == m_chests.end())
        return false;
    auto opened = m_chestOpenedBy.find(chest);
    return opened == m_chestOpenedBy.end() || !opened->second.count(opener->GetObjectGuid());
}

void BonkRun::RemoveChest(ObjectGuid chest)
{
    ForgetChestObjective(chest);
    m_chests.erase(std::remove(m_chests.begin(), m_chests.end(), chest), m_chests.end());
    m_chestChoices.erase(chest);
    m_chestOpenedBy.erase(chest);
    BonkCoop::ForgetHiddenObject(chest);
}

uint32 BonkRun::OpenChestFor(GameObject* chest, Player* opener)
{
    ObjectGuid const guid = chest->GetObjectGuid();
    auto choices = m_chestChoices.find(guid);
    uint32 const offered = choices != m_chestChoices.end() ? choices->second : uint32(BONK_OFFER_SIZE);

    std::set<ObjectGuid>& opened = m_chestOpenedBy[guid];
    opened.insert(opener->GetObjectGuid());
    bool everyoneOpened = true;
    for (Player* member : BonkCoop::RunGroupOf(m_player))
    {
        BonkRun* run = member->GetBonkPlayer()->GetRun();
        if (run && !run->IsOver() && !opened.count(member->GetObjectGuid()))
            everyoneOpened = false;
    }
    if (everyoneOpened)
    {
        RemoveChest(guid);
        chest->Delete();
        return offered;
    }

    if (opener == m_player)
        ForgetChestObjective(guid);
    BonkCoop::HideObjectFor(guid, opener->GetObjectGuid());
    opener->UpdateVisibilityOf(opener, chest);
    return offered;
}

void BonkRun::UseChest(GameObject* chest)
{
    if (m_state != STATE_RUNNING && m_state != STATE_PORTAL)
        return;

    BonkRun* owner = nullptr;
    for (Player* member : BonkCoop::RunGroupOf(m_player))
        if (BonkRun* run = member->GetBonkPlayer()->GetRun())
            if (run->OffersChest(chest->GetObjectGuid(), m_player))
            {
                owner = run;
                break;
            }
    if (!owner)
        return;

    BonkPlayer* bonk = m_player->GetBonkPlayer();
    if (chest->GetEntry() != BONK_GO_CHEST_FREE)
    {
        uint32 const cost = GetChestPrice();
        if (m_coins < cost)
        {
            bonk->Notify(BONK_STRING_THIS_CHEST_COSTS_VALOR_YOU, { std::to_string(cost), std::to_string(m_coins) });
            return;
        }
        if (RollFreeChest())
            bonk->Notify(BONK_STRING_YOUR_ARCANITE_SKELETON_KEY_OPENED);
        else
        {
            m_coins -= cost;
            m_valorOnChests += cost;
            ++m_chestsBought;
        }
        sBonkQuests.OnProgress(m_player, BONK_QUEST_CHESTS_BOUGHT, 0, 1);
    }

    m_pendingChestChoices.push_back(owner->OpenChestFor(chest, m_player));

    UpdateOffers();
    SendState();
}

bool BonkRun::IsOwnPortal(GameObject const* portal) const
{
    return m_state == STATE_PORTAL && portal->GetObjectGuid() == m_portalGuid;
}

void BonkRun::RewardLevelClear()
{
    BonkPlayer* bonk = m_player->GetBonkPlayer();
    uint8 const trainingCap = bonk->GetTrainingRankCap();
    LogLevelClear();
    if (!bonk->RecordLevelClear(m_difficultyLevel, m_totalMs))
        return;

    uint32 const tickets = GetLevelFirstClearTickets(m_difficultyLevel);
    bonk->AddShards(tickets);
    m_shardsEarned += tickets;
    bonk->Notify(BONK_STRING_FIRST_CLEAR_LEVEL, { bonk->GetLevelName(m_difficultyLevel), FormatBonkShards(tickets, GetLocale()) });

    if (bonk->GetTrainingRankCap() > trainingCap)
        bonk->Notify(BONK_STRING_ACCOUNT_TRAINING_CAN_NOW_GO, { std::to_string(bonk->GetTrainingRankCap()) });
}

void BonkRun::PayFinale()
{
    if (!IsFinale() || m_sandbox || IsCoopGuest())
        return;

    float const minutes = m_elapsedMs / float(MINUTE * IN_MILLISECONDS);
    uint32 const gold = uint32(m_map.stageGold * BONK_FINALE_GOLD_PER_MINUTE * minutes * m_levelRow.reward);
    uint32 const scrap = uint32(BONK_FINALE_SCRAP_PER_MINUTE * minutes * m_levelRow.reward);
    BonkPlayer* bonk = m_player->GetBonkPlayer();
    GiveGold(gold);
    bonk->AddScrap(scrap);
    bonk->Notify(BONK_STRING_FINALE_PAYS, { FormatTime(m_elapsedMs / IN_MILLISECONDS), FormatBonkMoney(gold, GetLocale()), std::to_string(scrap) });
    if (!IsMythic())
        return;
    uint32 const score = uint32(GetBandLevel() * minutes);
    LogFinale(minutes, score);
    bonk->RecordFinale(score);
}

BonkMap const* BonkRun::GetNextMap() const
{
    return sBonkMgr.GetMap(m_nextMapId);
}

void BonkRun::OpenPortal(Creature* boss)
{
    m_state = STATE_PORTAL;
    m_bossGuid.Clear();
    m_telegraphs.Clear(m_player);
    RemoveHeroLooks();
    GiveGold(m_map.stageGold * m_stage);
    sBonkQuests.OnProgress(m_player, BONK_QUEST_DARK_FORTUNES, 0, m_player->GetBonkPlayer()->CountDarkFortunes(), true);
    CollectAllOrbs();
    CollectAllGear();
    DespawnShrines();
    DespawnMiniEvent();
    DespawnPaidChests();
    ClearHidden();

    BonkPlayer* bonk = m_player->GetBonkPlayer();
    bool const last = m_stage >= m_tier.stages;
    if (last)
    {
        m_victory = true;
        if (!m_sandbox)
            RewardLevelClear();
    }
    BonkMap const* next = !last || m_tier.finale ? ChooseNextMap() : nullptr;
    m_nextMapId = next ? next->id : 0;

    if (boss)
    {
        float const angle = m_player->GetAngle(boss);
        PlacePortal(boss->GetPositionX() + BONK_PORTAL_DISTANCE * std::cos(angle),
            boss->GetPositionY() + BONK_PORTAL_DISTANCE * std::sin(angle), boss->GetPositionZ());
    }
    else
    {
        float const angle = m_player->GetOrientation();
        PlacePortal(m_player->GetPositionX() + BONK_PORTAL_DISTANCE * std::cos(angle),
            m_player->GetPositionY() + BONK_PORTAL_DISTANCE * std::sin(angle), m_player->GetPositionZ());
    }

    if (GameObject const* portal = m_player->GetMap()->GetGameObject(m_portalGuid))
    {
        float const side = m_player->GetAngle(portal) + M_PI_F / 2.0f;
        if (BonkCoop::InGroupRun(m_player))
            BonkCoop::PlaceGroupVaults(m_player, portal, side);
        else
            PlaceVault(portal->GetPositionX() + BONK_VAULT_SIDE * std::cos(side), portal->GetPositionY() + BONK_VAULT_SIDE * std::sin(side),
                portal->GetPositionZ());
        if ((IsMythic() || m_tier.key == "heroic") && (!last || m_tier.finale))
            PlaceEnchanter(portal->GetPositionX() - BONK_VAULT_SIDE * std::cos(side), portal->GetPositionY() - BONK_VAULT_SIDE * std::sin(side),
                portal->GetPositionZ(), m_player->GetAngle(portal) + M_PI_F);
    }

    if (!last)
        bonk->Notify(BONK_STRING_CLEARED_STAGE_TAKE_PORTAL, { std::to_string(m_stage) });
    else
        bonk->Notify(m_tier.finale ? BONK_STRING_VICTORY_YOU_CLEARED_TAKE_PORTAL_HOME : BONK_STRING_VICTORY_YOU_CLEARED_TAKE_PORTAL,
            { bonk->GetLevelName(m_difficultyLevel) });
    SendState();
}

void BonkRun::PlacePortal(float x, float y, float baseZ)
{
    float const heroX = m_player->GetPositionX(), heroY = m_player->GetPositionY();
    float heroZ = m_player->GetMap()->GetHeight(heroX, heroY, m_player->GetPositionZ() + 2.0f, true, 2.0f * BONK_SPAWN_MAX_HEIGHT_DIFF);
    if (heroZ <= INVALID_HEIGHT)
        heroZ = m_player->GetPositionZ();

    auto walkable = [&](float& spotX, float& spotY, float& spotZ, float heightFrom)
    {
        float const ground = m_player->GetMap()->GetHeight(spotX, spotY, heightFrom + 5.0f, true, 2.0f * BONK_SPAWN_MAX_HEIGHT_DIFF);
        if (ground <= INVALID_HEIGHT || !m_player->IsWithinLOS(spotX, spotY, ground))
            return false;
        PathFinder path(m_player);
        path.calculate(Vector3(heroX, heroY, heroZ), Vector3(spotX, spotY, ground));
        if (!(path.getPathType() & PATHFIND_NORMAL) || (path.getPathType() & PATHFIND_INCOMPLETE))
            return false;
        Vector3 const end = path.getActualEndPosition();
        float const dx = end.x - spotX;
        float const dy = end.y - spotY;
        if (dx * dx + dy * dy > 4.0f)
            return false;
        float walked = 0.0f;
        PointsArray const& points = path.getPath();
        for (size_t i = 1; i < points.size(); ++i)
            walked += (points[i] - points[i - 1]).length();
        float const straight = Vector3(end.x - heroX, end.y - heroY, end.z - heroZ).length();
        if (walked > straight * BONK_PORTAL_MAX_DETOUR + BONK_PORTAL_DETOUR_SLACK)
            return false;
        spotX = end.x;
        spotY = end.y;
        spotZ = end.z;
        return true;
    };

    float z = baseZ;
    bool placed = walkable(x, y, z, baseZ);
    for (uint32 step = 0; !placed && step < 8; ++step)
    {
        float const angle = m_player->GetOrientation() + step * M_PI_F / 4.0f;
        x = heroX + BONK_PORTAL_DISTANCE * std::cos(angle);
        y = heroY + BONK_PORTAL_DISTANCE * std::sin(angle);
        placed = walkable(x, y, z, heroZ);
    }
    if (!placed)
    {
        x = heroX;
        y = heroY;
        z = heroZ;
    }

    if (GameObject* portal = PlaceObject(BONK_GO_PORTAL, x, y, z, BONK_OBJECT_LIFETIME_SEC))
        m_portalGuid = portal->GetObjectGuid();
}

void BonkRun::EnterNextStage()
{
    if (m_state != STATE_PORTAL)
        return;

    BonkPlayer* bonk = m_player->GetBonkPlayer();
    if (IsCoopGuest())
    {
        bonk->Notify(BONK_STRING_COOP_HOST_NEXT_STAGE);
        return;
    }
    if (!m_offer.empty())
    {
        bonk->Notify(BONK_STRING_MAKE_YOUR_CHOICE_FIRST);
        return;
    }

    BonkMap const* next = GetNextMap();
    if (!next)
        next = ChooseNextMap();
    if (!next)
    {
        bonk->Notify(BONK_STRING_PORTAL_HAS_NOWHERE_TO_TAKE);
        return;
    }

    ClearStageObjects();

    std::string error;
    if (!TeleportToStage(*next, error))
    {
        bonk->Notify(BONK_STRING_TRY_PORTAL_AGAIN, { error });
        BonkMap const* other = ChooseNextMap();
        m_nextMapId = other ? other->id : 0;
        return;
    }

    ++m_stage;
    m_map = *next;
    ApplyInvader();
    BringBossEarlier();
    m_nextMapId = 0;
    BonkCoop::OnHostEnteredStage(m_player, m_map, m_stage);
}

void BonkRun::ClearStageObjects()
{
    CollectAllShards();
    CollectAllGlyphs();
    EndHearth();
    DespawnMiniEvent();
    DespawnObjects();
    DespawnMobs();
    DespawnOrbs();
    DespawnTraps();
    DespawnLandMines();
    DespawnPets();
    DespawnShotSources();
    DespawnPools();
    DespawnWeaponOrbs();
    DespawnProjectiles();
    ClearHunterEvolutions();
    m_telegraphs.Dismiss(m_player);
    m_objectives.Clear(m_player);
    DespawnShrines();
    DespawnVendor();
}

void BonkRun::FollowToStage(BonkMap const& map, uint32 stage)
{
    if (IsOver() || m_sandbox)
        return;
    if (m_away)
    {
        m_followPending = true;
        m_followMap = map;
        m_followStage = stage;
        return;
    }
    SetPaused(false);
    ClearStageObjects();
    std::string error;
    if (!TeleportToStage(map, error))
    {
        m_player->GetBonkPlayer()->Notify(error);
        return;
    }
    m_stage = stage;
    m_map = map;
    ApplyInvader();
    BringBossEarlier();
    m_nextMapId = 0;
}

void BonkRun::EndWithHost()
{
    if (m_state == STATE_ENTERING)
    {
        Exit();
        return;
    }
    m_player->GetBonkPlayer()->Notify(BONK_STRING_COOP_HOST_ENDED_RUN);
    Finish(true);
}

void BonkRun::EndInWipe()
{
    if (m_state == STATE_ENTERING)
    {
        Exit();
        return;
    }
    m_player->GetBonkPlayer()->Notify(BONK_STRING_COOP_GROUP_WIPED);
    Finish(false);
}

void BonkRun::ReviveFromGroup(float health)
{
    if (m_player->IsAlive())
        return;
    m_player->ResurrectPlayer(health);
    m_player->SpawnCorpseBones();
    m_player->CastSpell(m_player, BONK_SPELL_REVIVE_SHIELD, true);
    SmiteAroundRevive();
    SendState();
}

bool BonkRun::SkipTime(uint32 seconds, std::string& error)
{
    if (m_state != STATE_RUNNING)
    {
        error = GetString(BONK_STRING_ONLY_RUNNING_STAGE_SKIPPED);
        return false;
    }
    if (m_bossSpawned)
    {
        error = GetString(BONK_STRING_BOSS_IS_ALREADY_HERE);
        return false;
    }

    uint32 const bossMs = m_map.durationSeconds * IN_MILLISECONDS;
    uint32 const target = seconds ? std::min(m_elapsedMs + seconds * IN_MILLISECONDS, bossMs) : bossMs;
    if (target <= m_elapsedMs)
    {
        error = GetString(BONK_STRING_BOSS_IS_ALREADY_DUE);
        return false;
    }

    m_totalMs += target - m_elapsedMs;
    m_rewardMs += (target - m_elapsedMs) * GetBonkMapRewardPercent(m_map.type) / 100;
    m_elapsedMs = target;

    while (m_nextEvent < m_map.events.size() && m_map.events[m_nextEvent].second * IN_MILLISECONDS < target)
        ++m_nextEvent;

    SendState();
    return true;
}

void BonkRun::GoHome()
{
    if (m_state == STATE_PORTAL)
        Finish(true);
}

float BonkRun::GetMagnetRange() const
{
    return std::min(BONK_MAX_MAGNET_RANGE, BONK_ORB_MAGNET_RANGE * (1.0f + std::max(0.0f, GetPassiveTotal(BONK_PASSIVE_PICKUP)) / 100.0f));
}

float BonkRun::GetRunPower() const
{
    return GetPassiveTotal(BONK_PASSIVE_POWER) - GetGearTotal(BONK_PASSIVE_POWER);
}

float BonkRun::GetGearDamageScale() const
{
    return 1.0f + std::max(0.0f, GetGearTotal(BONK_PASSIVE_POWER)) / (100.0f + BONK_GEAR_POWER_REFERENCE);
}

float BonkRun::GetGearTotal(BonkPassiveEffect effect) const
{
    float const total = m_gear.passives[effect];
    return effect == BONK_PASSIVE_HASTE ? std::min(total, BONK_MAX_GEAR_HASTE) : total;
}

float BonkRun::GetPassiveTotal(BonkPassiveEffect effect) const
{
    float total = m_bonuses.passives[effect] + GetGearTotal(effect);
    for (auto const& itr : m_passiveRanks)
        if (BonkPassive const* passive = sBonkMgr.GetPassive(itr.first))
            if (passive->effect == effect)
                total += passive->valuePerRank * passive->RankScale(itr.second);
    if (effect == BONK_PASSIVE_HASTE)
        total += GetEnrageHaste() + GetDruidHaste();
    if (effect == BONK_PASSIVE_MOVE_SPEED)
        total += GetTalentMoveSpeed() + BONK_BASE_MOVE_SPEED;
    total += m_stress.bonuses[effect];
    total += m_cardStats[effect];
    total += m_shrineBonuses[effect];
    total += GetTrinketStat(effect);
    return total;
}

float BonkRun::GetStatCap(BonkPassiveEffect effect)
{
    switch (effect)
    {
        case BONK_PASSIVE_DODGE:  return BONK_MAX_DODGE;
        case BONK_PASSIVE_AMOUNT: return BONK_MAX_COUNT;
        case BONK_PASSIVE_MOVE_SPEED: return BONK_MAX_MOVE_SPEED;
        case BONK_PASSIVE_PICKUP: return (BONK_MAX_MAGNET_RANGE / BONK_ORB_MAGNET_RANGE - 1.0f) * 100.0f;
        default:                  return 0.0f;
    }
}

float CurveStat(float raw, float ceiling)
{
    if (raw <= 0.0f)
        return raw;
    return ceiling * (1.0f - std::exp(-raw / ceiling));
}

float CurveHaste(float raw)
{
    return CurveStat(raw, BONK_MAX_HASTE);
}

float BonkRun::GetArmorReduction() const
{
    float const armor = std::max(0.0f, GetStat(BONK_PASSIVE_ARMOR));
    return std::min(BONK_ARMOR_MAX_REDUCTION, armor / (armor * BONK_ARMOR_SLOPE + BONK_ARMOR_BASE));
}

float BonkRun::GetArmorOverflowHealth() const
{
    return std::max(0.0f, GetStat(BONK_PASSIVE_ARMOR) - BONK_ARMOR_CAP_POINT) * BONK_ARMOR_OVERFLOW_HEALTH;
}

float BonkRun::GetStat(BonkPassiveEffect effect) const
{
    float const total = GetPassiveTotal(effect);
    if (effect == BONK_PASSIVE_HASTE)
        return CurveHaste(total) + GetBurstHaste();
    if (effect == BONK_PASSIVE_AREA)
        return CurveStat(total, BONK_MAX_AREA);
    float const cap = GetStatCap(effect);
    float const lasting = cap > 0.0f ? std::min(total, cap) : total;
    return effect == BONK_PASSIVE_MOVE_SPEED ? lasting + GetBoostMoveSpeed() + GetDruidMoveSpeed() : lasting;
}

float BonkRun::GetBoostMoveSpeed() const
{
    float boost = GetPriestMoveSpeed();
    if (IsLastStand())
        boost += GetGlyphValue(BONK_GLYPH_LAST_STAND, 2);
    if (BonkCard const* potion = m_cardByEffect[BONK_CARD_SWIFTNESS])
        if (m_cardSwiftUntilMs)
            boost += potion->values[1];
    boost += GetTrinketMoveSpeed();
    return boost;
}

float BonkRun::GetHasteRate() const
{
    return 1.0f + std::max(0.0f, GetStat(BONK_PASSIVE_HASTE)) / 100.0f;
}

float BonkRun::GetRawWeaponHaste(OwnedWeapon const& owned) const
{
    return std::max(0.0f, GetPassiveTotal(BONK_PASSIVE_HASTE)) + GetSpellUpgrade(owned, BONK_SPELL_UPGRADE_HASTE) + GetTalentHaste();
}

void BonkRun::SendStats() const
{
    float totals[MAX_BONK_PASSIVE_EFFECT];
    for (uint8 effect = 0; effect < MAX_BONK_PASSIVE_EFFECT; ++effect)
        totals[effect] = GetPassiveTotal(BonkPassiveEffect(effect)) - GetGearTotal(BonkPassiveEffect(effect));
    m_player->GetBonkPlayer()->SendStatTotals(totals, true);
}

float BonkRun::GetWeaponDamage(OwnedWeapon const& owned) const
{
    float const bonus = owned.damageBonus + GetSpellUpgrade(owned, BONK_SPELL_UPGRADE_PET_DAMAGE);
    return GetMobHealth(GetMobLevel()) * owned.weapon.damageFraction * (1.0f + bonus / 100.0f) * GetDruidFormScale(owned);
}

float BonkRun::GetSpellUpgrade(OwnedWeapon const& owned, BonkSpellUpgrade upgrade) const
{
    if (upgrade == BONK_SPELL_UPGRADE_NONE || upgrade >= MAX_BONK_SPELL_UPGRADE || owned.weapon.GetUpgradeStep(upgrade) <= 0.0f)
        return 0.0f;
    float const cap = BONK_SPELL_UPGRADE_CAP[upgrade];
    return cap > 0.0f ? std::min(owned.upgrades[upgrade], cap) : owned.upgrades[upgrade];
}

uint32 BonkRun::GetSpellCount(OwnedWeapon const& owned) const
{
    uint32 const echoes = uint32(GetSpellUpgrade(owned, BONK_SPELL_UPGRADE_ECHO));
    bool const aoe = owned.weapon.HasFlag(BONK_WEAPON_FLAG_AOE);
    if (aoe && !owned.weapon.HasFlag(BONK_WEAPON_FLAG_TOTEM))
        return echoes;
    return uint32(GetStat(BONK_PASSIVE_AMOUNT) + GetGlyphValue(BONK_GLYPH_MULTI_SHOT, 0)) + (aoe ? echoes : 0);
}

float BonkRun::GetWeaponDurationScale(OwnedWeapon const& owned) const
{
    return std::max(0.1f, 1.0f + (GetPassiveTotal(BONK_PASSIVE_DURATION) + GetSpellUpgrade(owned, BONK_SPELL_UPGRADE_DURATION) +
        GetTrapTalentDuration(owned)) / 100.0f);
}

float BonkRun::GetWeaponHasteRate(OwnedWeapon const& owned) const
{
    return 1.0f + (CurveHaste(GetRawWeaponHaste(owned)) + GetBurstHaste()) / 100.0f;
}

uint32 BonkRun::GetWeaponCooldown(OwnedWeapon const& owned) const
{
    float mastery = 0.0f;
    auto itr = m_mastery.spellCooldown.find(GetRootWeapon(owned.weapon));
    if (itr != m_mastery.spellCooldown.end())
        mastery = itr->second;
    float const rate = 1.0f + (CurveHaste(GetRawWeaponHaste(owned) + mastery) + GetBurstHaste()) / 100.0f;
    float const steady = IsSteadyAiming(owned) ? BONK_STEADY_RATE : 1.0f;
    float const trigger = 1.0f - GetSpellUpgrade(owned, BONK_SPELL_UPGRADE_TRIGGER_COOLDOWN) / 100.0f;
    return std::max(200u, uint32(owned.weapon.cooldownMs * trigger / (rate * steady)));
}

void BonkRun::UpdateWeapons(uint32 diff)
{
    uint32 index = 0;
    for (auto& owned : m_weapons)
    {
        ++index;
        owned.timer -= int32(diff);
        if (owned.timer <= 0)
        {
            SupportScope scope(*this, WeaponSupport(owned));
            FireWeapon(owned);
            if (m_state == STATE_RUNNING && HasCard(BONK_CARD_ECHO) && roll_chance_f(GetCardChance(BONK_CARD_ECHO)))
            {
                int32 const timer = owned.timer;
                FireWeapon(owned);
                owned.timer = timer;
            }
            NoteWeaponFired(owned);
        }
        if (owned.pieResync)
        {
            owned.pieResync = false;
            SendCooldownPie(index, owned);
        }

        if (m_state != STATE_RUNNING)
            return;
    }
}

float BonkRun::GetStrikeRange(OwnedWeapon const& owned) const
{
    return owned.weapon.range * (owned.weapon.IsMeleeStrike() ? GetWeaponArea(owned) : 1.0f);
}

float BonkRun::GetWeaponArea(OwnedWeapon const& owned) const
{
    if (owned.weapon.HasFlag(BONK_WEAPON_FLAG_AOE))
        return std::max(0.1f, 1.0f + CurveStat(GetPassiveTotal(BONK_PASSIVE_AREA) + GetTalentReach(owned), BONK_MAX_AREA) / 100.0f);
    return 1.0f + CurveStat(GetSpellUpgrade(owned, BONK_SPELL_UPGRADE_REACH) + GetTalentReach(owned), BONK_MAX_AREA) / 100.0f;
}

float BonkRun::GetWeaponRadius(OwnedWeapon const& owned) const
{
    return owned.weapon.radius * GetWeaponArea(owned);
}

uint32 BonkRun::GetWeaponTargets(OwnedWeapon const& owned) const
{
    uint32 mastery = 0;
    auto itr = m_mastery.spellTargets.find(GetRootWeapon(owned.weapon));
    if (itr != m_mastery.spellTargets.end())
        mastery = itr->second;
    return owned.weapon.baseTargets + GetSpellCount(owned) + mastery + uint32(GetSpellUpgrade(owned, BONK_SPELL_UPGRADE_TARGETS));
}

void BonkRun::GetBasePoints(OwnedWeapon const& owned, SpellEntry const* spellInfo, float damageShare, optional<int32>* basePoints) const
{
    BonkWeapon const& weapon = owned.weapon;
    float const damage = GetWeaponDamage(owned) * damageShare;

    if (spellInfo->Id != weapon.spellId)
    {
        int32 const index = FindDamageEffectOf(spellInfo);
        if (index >= 0)
            basePoints[index] = std::max(1, int32(damage));
        return;
    }

    if (weapon.damageEffectIndex >= 0)
    {
        float share = 1.0f;
        if (weapon.tickMs && spellInfo->EffectApplyAuraName[weapon.damageEffectIndex] == SPELL_AURA_PERIODIC_DAMAGE)
            share = weapon.tickMs / float(IN_MILLISECONDS);
        basePoints[weapon.damageEffectIndex] = std::max(1, int32(damage * share));
    }

    if (weapon.tickEffectIndex >= 0)
    {
        float share = 1.0f;
        if (spellInfo->Effect[weapon.tickEffectIndex] == SPELL_EFFECT_APPLY_AURA ||
            (weapon.behavior == BONK_BEHAVIOR_TRAP && spellInfo->Effect[weapon.tickEffectIndex] == SPELL_EFFECT_PERSISTENT_AREA_AURA))
        {
            int32 const duration = m_player->GetBonkPlayer()->AdjustDuration(weapon.spellId, spellInfo->GetDuration());
            uint32 const amplitude = weapon.tickMs ? weapon.tickMs : spellInfo->EffectAmplitude[weapon.tickEffectIndex];
            if (duration > 0 && amplitude)
                share = std::min(1.0f, float(amplitude) / duration);
        }
        basePoints[weapon.tickEffectIndex] = std::max(1, int32(damage * share));
    }

    if (weapon.IsWeaken())
        for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
            if (spellInfo->EffectApplyAuraName[i] == SPELL_AURA_MOD_MELEE_HASTE)
                basePoints[i] = -int32(weapon.behaviorValue);
}

void BonkRun::StrikeWith(OwnedWeapon& owned, SpellEntry const* spellInfo, Creature* target, float damageShare, bool followUp, Creature* source)
{
    ++m_secondHits;
    BonkWeapon const& weapon = owned.weapon;
    SpellEntry const* const shotInfo = spellInfo;
    float const shotShare = damageShare;

    if (!followUp && shotInfo->speed > 0.0f && !weapon.HasFlag(BONK_WEAPON_FLAG_AOE))
        ++m_projectilesUnreported;

    if (CanKill(weapon, target))
    {
        if (weapon.HasFlag(BONK_WEAPON_FLAG_KILL_WITH_ALT) && weapon.altSpellId)
            if (SpellEntry const* killInfo = sSpellMgr.GetSpellEntry(weapon.altSpellId))
                spellInfo = killInfo;
        damageShare = std::max(damageShare, 2.0f * target->GetHealth() / std::max(1.0f, GetWeaponDamage(owned)));
        owned.killed = true;
    }
    if (weapon.IsShieldSlam())
        damageShare *= GetShieldSlamMultiplier(owned);
    if (weapon.behavior == BONK_BEHAVIOR_VENGEANCE)
        damageShare *= m_vengeanceShare;

    optional<int32> basePoints[MAX_EFFECT_INDEX];
    GetBasePoints(owned, spellInfo, damageShare, basePoints);
    if (weapon.weaponDamage)
        m_player->GetBonkPlayer()->TweakSpell(weapon.spellId).weaponDamage = std::max(1, int32(GetWeaponDamage(owned) * damageShare));

    if (weapon.HasFlag(BONK_WEAPON_FLAG_LOOK_ONLY))
        ShowStrikeOn(source ? static_cast<Unit*>(source) : m_player, spellInfo, target);
    else if (source)
        source->CastCustomSpell(target, spellInfo, basePoints[0], basePoints[1], basePoints[2], true, nullptr, nullptr, m_player->GetObjectGuid());
    else
    {
        m_player->CastCustomSpell(target, spellInfo, basePoints[0], basePoints[1], basePoints[2], true);
        if (uint32 const kit = GetCastAnimationKit(spellInfo->Id))
        {
            uint32 const animationNow = WorldTimer::getMSTime();
            if (WorldTimer::getMSTimeDiff(m_castAnimationMs, animationNow) >= BONK_CAST_ANIMATION_GAP_MS)
            {
                m_castAnimationMs = animationNow;
                m_player->SendPlaySpellVisualKit(kit);
            }
        }
    }

    PendingKeyword keyword;
    if (weapon.IsKeywordOnly() && GetWeaponKeyword(owned, keyword))
        ApplyKeyword(target, keyword.keyword, keyword.stacks, keyword.durationMs, keyword.damageScale, keyword.spellId);

    if (followUp)
        return;

    QueueFlareSplit(owned, shotInfo, target, shotShare, source ? static_cast<SpellCaster const*>(source) : m_player);

    uint32 const now = WorldTimer::getMSTime();
    if (weapon.behavior == BONK_BEHAVIOR_SPREAD)
    {
        int32 const duration = m_player->GetBonkPlayer()->AdjustDuration(weapon.spellId, spellInfo->GetDuration());
        owned.carriers[target->GetObjectGuid()] = now + uint32(std::max(0, duration));
    }

    if (weapon.behavior == BONK_BEHAVIOR_VULNERABLE && weapon.altSpellId)
        m_player->CastSpell(target, weapon.altSpellId, true);

    if (weapon.behavior == BONK_BEHAVIOR_DELAYED && weapon.altSpellId)
    {
        PendingCast burst;
        burst.weaponId = weapon.id;
        burst.spellId = weapon.altSpellId;
        burst.leftMs = int32(weapon.behaviorValue);
        burst.target = target->GetObjectGuid();
        burst.x = target->GetPositionX();
        burst.y = target->GetPositionY();
        burst.z = target->GetPositionZ();
        m_pendingCasts.push_back(burst);
    }

    if (weapon.behavior == BONK_BEHAVIOR_BURST)
        for (uint32 shot = 1; shot <= uint32(weapon.behaviorValue); ++shot)
        {
            PendingCast follow;
            follow.weaponId = weapon.id;
            follow.spellId = spellInfo->Id;
            follow.leftMs = int32(shot * BONK_BURST_INTERVAL_MS);
            follow.target = target->GetObjectGuid();
            follow.strike = true;
            follow.share = damageShare;
            m_pendingCasts.push_back(follow);
        }

    if (weapon.IsArcaneMissiles())
        QueueMissiles(owned, spellInfo, target, damageShare);

    OnMageStrike(owned, shotInfo, target, shotShare, source ? static_cast<Unit const*>(source) : m_player);
    OnShamanStrike(owned, shotInfo, target, shotShare);
    OnPriestStrike(owned, shotInfo, target, shotShare);
    OnWarlockStrike(owned, shotInfo, target, shotShare);
    OnDruidStrike(owned, shotInfo, target);

    if (weapon.HasFlag(BONK_WEAPON_FLAG_KNOCKBACK))
        KnockBack(target);

    if (weapon.HasFlag(BONK_WEAPON_FLAG_REFRESH_DOTS))
        for (OwnedWeapon& other : m_weapons)
            if (&other != &owned && other.weapon.lasting && other.weapon.IsAimed() && target->HasAura(other.weapon.spellId))
                if (SpellEntry const* otherInfo = sSpellMgr.GetSpellEntry(other.weapon.spellId))
                    StrikeWith(other, otherInfo, target, 1.0f, true);
}

bool BonkRun::CanKill(BonkWeapon const& weapon, Creature const* target) const
{
    if (weapon.killBelowPct <= 0.0f || !target->IsAlive() || target->GetHealthPercent() >= weapon.killBelowPct)
        return false;
    BonkMobAI const* ai = dynamic_cast<BonkMobAI const*>(target->AI());
    return ai && ai->GetKind() == BONK_SPAWN_NORMAL;
}

void BonkRun::SortStrongestFirst(std::vector<Creature*>& mobs) const
{
    std::stable_sort(mobs.begin(), mobs.end(), [](Creature const* a, Creature const* b)
    {
        uint32 const rankA = GetKindRank(a);
        uint32 const rankB = GetKindRank(b);
        if (rankA != rankB)
            return rankA > rankB;
        return a->GetHealth() > b->GetHealth();
    });
}

std::vector<Creature*> BonkRun::GetMobsInCone(OwnedWeapon const& owned, float range) const
{
    std::vector<Creature*> mobs = GetMobsInRange(range);
    mobs.erase(std::remove_if(mobs.begin(), mobs.end(), [&](Creature* mob) { return !m_player->IsWithinLOSInMap(mob); }), mobs.end());
    if (mobs.empty())
        return mobs;

    float const aim = m_player->GetAngle(mobs.front());
    float const halfArc = std::min(M_PI_F, BONK_CONE_ARC * GetWeaponArea(owned)) / 2.0f;
    mobs.erase(std::remove_if(mobs.begin(), mobs.end(), [&](Creature* mob)
    {
        float diff = std::fabs(m_player->GetAngle(mob) - aim);
        if (diff > M_PI_F)
            diff = 2.0f * M_PI_F - diff;
        return diff > halfArc;
    }), mobs.end());

    if (owned.weapon.baseTargets > 1 && mobs.size() > owned.weapon.baseTargets)
        mobs.resize(owned.weapon.baseTargets);
    return mobs;
}

void BonkRun::KnockBack(Creature* mob)
{
    BonkMobAI* ai = dynamic_cast<BonkMobAI*>(mob->AI());
    if (!ai || !ai->CanBePulled() || !mob->IsAlive() || mob->HasUnitState(UNIT_STATE_ROOT | UNIT_STATE_STUNNED) || !CanKnockBack(mob))
        return;

    float const angle = m_player->GetAngle(mob);
    float x = mob->GetPositionX() + BONK_KNOCKBACK_DISTANCE * std::cos(angle);
    float y = mob->GetPositionY() + BONK_KNOCKBACK_DISTANCE * std::sin(angle);
    float z = mob->GetPositionZ();
    mob->GetMap()->GetWalkHitPosition(nullptr, mob->GetPositionX(), mob->GetPositionY(), mob->GetPositionZ(), x, y, z);
    ai->Pull(x, y, z, BONK_KNOCKBACK_SPEED, BONK_KNOCKBACK_SETTLE_MS);
}

void BonkRun::QueueRicochet(OwnedWeapon const& owned, SpellEntry const* spellInfo, Creature* target, SpellCaster const* from, float share,
    std::vector<ObjectGuid> struck, uint32 hops)
{
    if (!hops)
        return;

    struck.push_back(target->GetObjectGuid());
    PendingCast hop;
    hop.weaponId = owned.weapon.id;
    hop.spellId = spellInfo->Id;
    hop.leftMs = GetBounceDelay(from, target, spellInfo->speed);
    hop.ricochet = true;
    hop.hops = hops;
    hop.share = share;
    hop.target = target->GetObjectGuid();
    float o;
    if (!target->ExtrapolateMovement(target->m_movementInfo, uint32(hop.leftMs), hop.x, hop.y, hop.z, o))
        target->GetPosition(hop.x, hop.y, hop.z);
    hop.source = HoldShotSource(hop.x, hop.y, hop.z);
    hop.struck = std::move(struck);
    m_pendingCasts.push_back(std::move(hop));
}

void BonkRun::Ricochet(OwnedWeapon& owned, SpellEntry const* spellInfo, PendingCast const& cast)
{
    float x = cast.x, y = cast.y, z = cast.z;
    Creature* last = m_player->GetMap()->GetCreature(cast.target);
    if (last)
        last->GetPosition(x, y, z);

    Creature* source = ReleaseShotSource(cast.source, x, y, z);
    if (source && last && source->GetObjectGuid() == cast.source)
        source->NearTeleportTo(x, y, z + BONK_PROJECTILE_HEIGHT, source->GetOrientation());
    Creature* next = source ? PickRicochetTarget(source, cast.struck) : nullptr;
    if (!next)
        return;

    float const share = cast.share * BONK_RICOCHET_GROWTH;
    StrikeFromMob(owned, spellInfo, next, share, source, last);
    QueueRicochet(owned, spellInfo, next, source, share, cast.struck, cast.hops - 1);
}

ObjectGuid BonkRun::GetMissileLookSource()
{
    return t_missileLookSource;
}

int32 BonkRun::GetBounceDelay(SpellCaster const* from, Unit const* target, float speed)
{
    return std::max(0, int32(GetMissileDelay(from, target, speed)) - BONK_RICOCHET_LEAD_MS);
}

void BonkRun::StrikeFromMob(OwnedWeapon& owned, SpellEntry const* spellInfo, Creature* target, float share, Creature* source,
    Creature const* lookFrom)
{
    FiringWeapon firing(m_player->GetBonkPlayer());
    t_missileLookSource = lookFrom && lookFrom->IsInWorld() && lookFrom->IsAlive() ? lookFrom->GetObjectGuid() : ObjectGuid();
    StrikeWith(owned, spellInfo, target, share, true, source);
    t_missileLookSource = ObjectGuid();
}

ObjectGuid BonkRun::HoldShotSource(float x, float y, float z)
{
    if (m_heldShotSources.size() >= BONK_SHOT_SOURCES_HELD_MAX)
        return ObjectGuid();
    Creature* source = TakeShotSource(x, y, z);
    if (!source)
        return ObjectGuid();
    m_heldShotSources.insert(source->GetObjectGuid());
    return source->GetObjectGuid();
}

Creature* BonkRun::ReleaseShotSource(ObjectGuid guid, float x, float y, float z)
{
    m_heldShotSources.erase(guid);
    Creature* source = guid.IsEmpty() ? nullptr : m_player->GetMap()->GetCreature(guid);
    return source && source->IsAlive() ? source : TakeShotSource(x, y, z);
}

Creature* BonkRun::PickRicochetTarget(WorldObject const* from, std::vector<ObjectGuid> const& struck) const
{
    std::vector<Creature*> const nearby = GetMobsInRange(BONK_RICOCHET_RANGE, from);
    auto isOpen = [&](Creature* mob)
    {
        return std::find(struck.begin(), struck.end(), mob->GetObjectGuid()) == struck.end() && from->IsWithinLOSInMap(mob);
    };
    for (Creature* mob : nearby)
        if (GetKeywordStacks(mob, BONK_KEYWORD_HUNTERS_MARK) && isOpen(mob))
            return mob;
    for (Creature* mob : nearby)
        if (!GetKeywordStacks(mob, BONK_KEYWORD_HUNTERS_MARK) && isOpen(mob))
            return mob;
    return nullptr;
}

void BonkRun::QueueTrickShot(Creature const* dead, uint32 killingSpell)
{
    OwnedWeapon const* owned = killingSpell ? FindOwnedBySpell(killingSpell) : nullptr;
    if (!owned || owned->weapon.behavior != BONK_BEHAVIOR_TRICK_SHOTS || !TakeRateSlot(m_trickShotTimes, BONK_MAX_TRICK_SHOTS))
        return;

    PendingCast volley;
    volley.weaponId = owned->weapon.id;
    volley.spellId = owned->weapon.spellId;
    volley.leftMs = BONK_TRICK_SHOT_DELAY_MS;
    volley.trickShot = true;
    dead->GetPosition(volley.x, volley.y, volley.z);
    volley.source = HoldShotSource(volley.x, volley.y, volley.z);
    m_pendingCasts.push_back(std::move(volley));
}

void BonkRun::FireTrickShot(OwnedWeapon& owned, SpellEntry const* spellInfo, PendingCast const& cast)
{
    Creature* source = ReleaseShotSource(cast.source, cast.x, cast.y, cast.z);
    if (!source)
        return;

    uint32 const arrows = std::max(1u, uint32(owned.weapon.behaviorValue));
    uint32 fired = 0;
    FiringWeapon firing(m_player->GetBonkPlayer());
    for (Creature* mob : GetMobsInRange(BONK_TRICK_SHOT_RANGE, source))
    {
        if (fired >= arrows)
            break;
        if (!source->IsWithinLOSInMap(mob))
            continue;
        StrikeWith(owned, spellInfo, mob, 1.0f, true, source);
        ++fired;
    }
}

bool BonkRun::UsesShotSources() const
{
    for (BonkWeaponBehavior behavior : { BONK_BEHAVIOR_RICOCHET, BONK_BEHAVIOR_TRICK_SHOTS, BONK_BEHAVIOR_CHAIN_DETONATION, BONK_BEHAVIOR_GRAVITY_PULL,
        BONK_BEHAVIOR_FLARE, BONK_BEHAVIOR_SHARD_BURST, BONK_BEHAVIOR_VENOM_BOLTS, BONK_BEHAVIOR_VENOM_CLOUD, BONK_BEHAVIOR_WAKE_OF_BLADES,
        BONK_BEHAVIOR_FOURTH_SWING, BONK_BEHAVIOR_BLOOD_POOL, BONK_BEHAVIOR_AFTERSHOCK, BONK_BEHAVIOR_STEEL_CYCLONE, BONK_BEHAVIOR_AVENGERS_SHIELD,
        BONK_BEHAVIOR_ICE_SHARDS, BONK_BEHAVIOR_FROST_SHATTER, BONK_BEHAVIOR_ICE_NOVA, BONK_BEHAVIOR_LIVING_BOMB, BONK_BEHAVIOR_METEOR,
        BONK_BEHAVIOR_METEOR_SHOWER })
        if (HasBehavior(behavior))
            return true;
    return std::any_of(m_weapons.begin(), m_weapons.end(), [](OwnedWeapon const& owned)
    {
        return owned.weapon.HasFlag(BONK_WEAPON_FLAG_CLUSTER_BOMBS) || owned.weapon.HasFlag(BONK_WEAPON_FLAG_SHATTER) ||
            owned.weapon.HasFlag(BONK_WEAPON_FLAG_CHAIN_EXECUTION);
    });
}

Creature* BonkRun::SummonShotSource()
{
    s_nextHelperLevel = sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL);
    Creature* source = m_player->SummonCreature(BONK_PROJECTILE, m_player->GetPositionX(), m_player->GetPositionY(), m_player->GetPositionZ(), 0.0f,
        TEMPSUMMON_TIMED_DESPAWN, BONK_SHOT_SOURCE_LIFETIME_MS, false, 0, &InstallProjectileAI);
    if (!source)
        return nullptr;
    source->SetFactionTemplateId(m_player->GetFactionTemplateId());
    source->SetOwnerGuid(m_player->GetObjectGuid());
    source->SetVirtualItem(RANGED_ATTACK, BONK_SHOT_SOURCE_BOW);
    source->SetUInt32Value(UNIT_VIRTUAL_ITEM_SLOT_DISPLAY + RANGED_ATTACK, 0);
    return source;
}

void BonkRun::WarmShotSources()
{
    if (!m_player->IsInWorld())
        return;

    Map* map = m_player->GetMap();
    m_shotSources.resize(BONK_SHOT_SOURCES);
    for (ObjectGuid& guid : m_shotSources)
    {
        Creature* source = guid.IsEmpty() ? nullptr : map->GetCreature(guid);
        if (source && source->IsAlive())
            continue;
        if (source)
            source->DespawnOrUnsummon();
        source = SummonShotSource();
        guid = source ? source->GetObjectGuid() : ObjectGuid();
    }
}

Creature* BonkRun::TakeShotSource(float x, float y, float z)
{
    WarmShotSources();
    Map* map = m_player->GetMap();
    for (size_t tries = 0; tries < m_shotSources.size(); ++tries)
    {
        ObjectGuid const guid = m_shotSources[m_nextShotSource];
        m_nextShotSource = (m_nextShotSource + 1) % m_shotSources.size();
        Creature* source = guid.IsEmpty() ? nullptr : map->GetCreature(guid);
        if (!source || !source->IsAlive() || m_heldShotSources.count(guid))
            continue;
        source->NearTeleportTo(x, y, z + BONK_PROJECTILE_HEIGHT, source->GetOrientation());
        return source;
    }
    return nullptr;
}

void BonkRun::UpdateShotSources(uint32 diff)
{
    m_shotSourceTimer += diff;
    if (m_shotSourceTimer < BONK_SHOT_SOURCE_CHECK_MS)
        return;
    m_shotSourceTimer = 0;
    if (!UsesShotSources())
        return;

    WarmShotSources();
    Map* map = m_player->GetMap();
    for (ObjectGuid const& guid : m_shotSources)
        if (Creature* source = guid.IsEmpty() || m_heldShotSources.count(guid) ? nullptr : map->GetCreature(guid))
            if (!source->IsWithinDistInMap(m_player, BONK_SHOT_SOURCE_GATHER))
                source->NearTeleportTo(m_player->GetPositionX(), m_player->GetPositionY(), m_player->GetPositionZ() + BONK_PROJECTILE_HEIGHT,
                    source->GetOrientation());
}

void BonkRun::DespawnShotSources()
{
    DespawnPooledMarkers(m_boltTargets);
    DespawnPooledMarkers(m_hazardSources);
    m_heldShotSources.clear();
    if (Map* map = m_player->FindMap())
        for (ObjectGuid const& guid : m_shotSources)
            if (Creature* source = guid.IsEmpty() ? nullptr : map->GetCreature(guid))
                source->DespawnOrUnsummon();
    m_shotSources.clear();
    m_nextShotSource = 0;
}

bool BonkRun::TakeRateSlot(std::vector<uint32>& times, size_t cap)
{
    times.erase(std::remove_if(times.begin(), times.end(), [this](uint32 at) { return at + BONK_RATE_WINDOW_MS <= m_totalMs; }), times.end());
    if (times.size() >= cap)
        return false;
    times.push_back(m_totalMs);
    return true;
}

std::vector<Creature*> BonkRun::GetMobsNear(float x, float y, float radius) const
{
    Map* map = m_player->GetMap();
    std::vector<std::pair<float, Creature*>> found;
    for (ObjectGuid const& guid : GetEnemyGuids())
    {
        Creature* mob = map->GetCreature(guid);
        if (!mob || !mob->IsAlive() || mob->HasCreatureState(CSTATE_DESPAWNING))
            continue;
        float const distance = mob->GetDistance2d(x, y);
        if (distance <= radius)
            found.emplace_back(distance, mob);
    }
    std::sort(found.begin(), found.end(), [](auto const& a, auto const& b) { return a.first < b.first; });

    std::vector<Creature*> mobs;
    mobs.reserve(found.size());
    for (auto const& itr : found)
        mobs.push_back(itr.second);
    return mobs;
}

void BonkRun::PlayKitAt(ObjectGuid const& guid, float x, float y, float z, uint32 kit, bool atFeet)
{
    Creature* mob = guid.IsEmpty() ? nullptr : m_player->GetMap()->GetCreature(guid);
    if (mob && mob->IsAlive())
    {
        mob->SendPlaySpellVisualKit(kit);
        return;
    }
    SendBonkKitAt(m_player, kit, x, y, atFeet ? z : z + BONK_PROJECTILE_HEIGHT, 0.0f, BONK_ERUPTION_LOOK_MS, !atFeet);
}

void BonkRun::QueueDetonations(OwnedWeapon const& owned, SpellEntry const* spellInfo, float share, std::vector<Creature*> struck)
{
    std::vector<std::pair<float, Creature*>> line;
    line.reserve(struck.size());
    for (Creature* mob : struck)
        line.emplace_back(m_player->GetDistance2d(mob), mob);
    std::sort(line.begin(), line.end(), [](auto const& a, auto const& b) { return a.first < b.first; });
    if (line.size() > BONK_MAX_DETONATIONS)
        line.resize(BONK_MAX_DETONATIONS);

    int32 delay = BONK_DETONATION_FIRST_MS - BONK_DETONATION_GAP_MS;
    for (auto const& itr : line)
    {
        Creature* mob = itr.second;
        int32 const landed = int32(GetMissileDelay(m_player, mob, spellInfo->speed));
        delay = std::max(delay + BONK_DETONATION_GAP_MS, landed + BONK_DETONATION_FIRST_MS);

        PendingCast blast;
        blast.weaponId = owned.weapon.id;
        blast.spellId = spellInfo->Id;
        blast.leftMs = delay;
        blast.detonate = true;
        blast.share = share;
        blast.target = mob->GetObjectGuid();
        mob->GetPosition(blast.x, blast.y, blast.z);
        m_pendingCasts.push_back(std::move(blast));
    }
}

void BonkRun::Detonate(OwnedWeapon& owned, SpellEntry const* spellInfo, PendingCast const& cast)
{
    float x = cast.x, y = cast.y, z = cast.z;
    if (Creature* mob = m_player->GetMap()->GetCreature(cast.target))
        mob->GetPosition(x, y, z);
    PlayKitAt(cast.target, x, y, z, BONK_KIT_DETONATION, false);

    float const damage = GetWeaponDamage(owned) * cast.share;
    for (Creature* victim : GetMobsNear(x, y, GetWeaponRadius(owned)))
        DealSpellHit(spellInfo, victim, damage);
}

bool BonkRun::IsSteadyAiming(OwnedWeapon const& owned) const
{
    return m_steady && owned.weapon.behavior == BONK_BEHAVIOR_STEADY_AIM;
}

float BonkRun::GetSteadyWidth(OwnedWeapon const& owned) const
{
    if (!IsSteadyAiming(owned))
        return 1.0f;
    float const steps = float((m_totalMs - m_steadySinceMs) / BONK_STEADY_STEP_MS);
    return std::max(1.0f, std::min(owned.weapon.behaviorValue, 1.0f + steps));
}

void BonkRun::UpdateSteadyAim()
{
    auto steadyWeapon = std::find_if(m_weapons.begin(), m_weapons.end(), [](OwnedWeapon const& owned)
    {
        return owned.weapon.behavior == BONK_BEHAVIOR_STEADY_AIM;
    });
    if (steadyWeapon == m_weapons.end())
    {
        m_steady = false;
        ShowHeroLook(BonkHeroLook::SteadyAim, false);
        return;
    }

    float const x = m_player->GetPositionX(), y = m_player->GetPositionY();
    bool const moved = m_player->IsMoving() || std::fabs(x - m_steadyX) > BONK_STEADY_DRIFT || std::fabs(y - m_steadyY) > BONK_STEADY_DRIFT;
    m_steadyX = x;
    m_steadyY = y;
    bool const steady = !moved && m_player->IsAlive();
    if (steady != m_steady)
    {
        m_steady = steady;
        m_steadySinceMs = m_totalMs;
        m_steadyPulseMs = BONK_STEADY_FIRST_PULSE_MS;
        RetimeSteadyWeapons();
    }
    uint32 const stillMs = m_steady ? m_totalMs - m_steadySinceMs : 0;
    ShowHeroLook(BonkHeroLook::SteadyAim, m_steady && stillMs >= BONK_STEADY_FIRST_PULSE_MS);
    if (!m_steady || stillMs < m_steadyPulseMs)
        return;
    if (m_steadyPulseMs > BONK_STEADY_FIRST_PULSE_MS)
        m_player->SendPlaySpellVisualKit(BONK_KIT_STEADY_AIM_STEP);
    uint32 const widestMs = uint32(std::max(0.0f, steadyWeapon->weapon.behaviorValue - 1.0f)) * BONK_STEADY_STEP_MS;
    m_steadyPulseMs = stillMs < widestMs ? (stillMs / BONK_STEADY_STEP_MS + 1) * BONK_STEADY_STEP_MS : std::numeric_limits<uint32>::max();
}

void BonkRun::RetimeSteadyWeapons()
{
    uint32 index = 0;
    for (OwnedWeapon& owned : m_weapons)
    {
        ++index;
        if (owned.weapon.behavior != BONK_BEHAVIOR_STEADY_AIM)
            continue;
        if (m_steady)
            owned.timer = int32(owned.timer / BONK_STEADY_RATE);
        else
            owned.timer = std::min(int32(owned.timer * BONK_STEADY_RATE), int32(GetWeaponCooldown(owned)));
        SendWeaponInfo(index, owned);
    }
}

uint32 BonkRun::GetArrowStormTickMs(OwnedWeapon const& owned) const
{
    return std::max(200u, uint32(std::max(200u, owned.weapon.tickMs) / GetWeaponHasteRate(owned)));
}

void BonkRun::StartArrowStorm(OwnedWeapon const& owned, SpellEntry const* spellInfo, float x, float y, float z)
{
    ShowMissileAt(m_player, spellInfo, x, y, z);
    if (m_arrowStorms.size() >= BONK_MAX_ARROW_STORMS)
        m_arrowStorms.erase(m_arrowStorms.begin());

    ArrowStorm storm;
    storm.weaponId = owned.weapon.id;
    storm.x = storm.aimX = x;
    storm.y = storm.aimY = y;
    storm.z = storm.aimZ = z;
    storm.leftMs = int32(owned.weapon.durationMs * GetWeaponDurationScale(owned));
    storm.tickMs = int32(GetArrowStormTickMs(owned));
    m_arrowStorms.push_back(storm);
}

void BonkRun::UpdateArrowStorms(uint32 diff)
{
    for (size_t i = 0; i < m_arrowStorms.size();)
    {
        ArrowStorm& storm = m_arrowStorms[i];
        OwnedWeapon* owned = FindOwned(storm.weaponId);
        SpellEntry const* spellInfo = owned ? sSpellMgr.GetSpellEntry(owned->weapon.spellId) : nullptr;
        storm.leftMs -= int32(diff);
        if (!spellInfo || storm.leftMs <= 0 || !m_player->IsAlive())
        {
            m_arrowStorms.erase(m_arrowStorms.begin() + i);
            continue;
        }

        float const radius = owned->weapon.groundRadius * GetWeaponArea(*owned);
        storm.aimMs -= int32(diff);
        if (storm.aimMs <= 0)
        {
            storm.aimMs = BONK_STORM_AIM_MS;
            AimArrowStorm(storm, radius);
        }
        DriftArrowStorm(storm, owned->weapon.behaviorValue, diff);

        storm.rainMs -= int32(diff);
        if (storm.rainMs <= 0)
        {
            storm.rainMs = BONK_STORM_RAIN_MS;
            PlaceAreaVisual(m_player, spellInfo->Id, storm.x, storm.y, storm.z, radius, uint32(std::min(BONK_STORM_RAIN_LIFE_MS, storm.leftMs)));
        }

        storm.tickMs -= int32(diff);
        bool const rains = storm.tickMs <= 0;
        if (rains)
            storm.tickMs += int32(GetArrowStormTickMs(*owned));
        ArrowStorm const rained = storm;
        ++i;
        if (rains)
            RainArrowStorm(*owned, spellInfo, rained, radius);
    }
}

void BonkRun::AimArrowStorm(ArrowStorm& storm, float radius) const
{
    std::vector<Creature*> nearby = GetMobsNear(storm.x, storm.y, BONK_STORM_SEEK_RANGE);
    if (nearby.size() > BONK_STORM_SEEK_MOBS)
        nearby.resize(BONK_STORM_SEEK_MOBS);
    std::vector<Creature*> const groups = PickGroundSpots(nearby, radius, 1);
    if (groups.empty())
    {
        storm.aim.Clear();
        return;
    }
    storm.aim = groups.front()->GetObjectGuid();
    groups.front()->GetPosition(storm.aimX, storm.aimY, storm.aimZ);
}

void BonkRun::DriftArrowStorm(ArrowStorm& storm, float speed, uint32 diff) const
{
    if (!storm.aim.IsEmpty())
        if (Creature* aim = m_player->GetMap()->GetCreature(storm.aim))
            if (aim->IsAlive())
                aim->GetPosition(storm.aimX, storm.aimY, storm.aimZ);

    float const dx = storm.aimX - storm.x, dy = storm.aimY - storm.y;
    float const distance = std::sqrt(dx * dx + dy * dy);
    float const step = std::max(0.0f, speed) * diff / float(IN_MILLISECONDS);
    if (distance <= step)
    {
        storm.x = storm.aimX;
        storm.y = storm.aimY;
        storm.z = storm.aimZ;
        return;
    }
    float const part = step / distance;
    storm.x += dx * part;
    storm.y += dy * part;
    storm.z += (storm.aimZ - storm.z) * part;
}

void BonkRun::RainArrowStorm(OwnedWeapon& owned, SpellEntry const* spellInfo, ArrowStorm const& storm, float radius)
{
    float const damage = GetWeaponDamage(owned) * owned.weapon.tickMs / float(IN_MILLISECONDS);
    float const twice = GetSpellUpgrade(owned, BONK_SPELL_UPGRADE_HIT_TWICE);
    for (Creature* mob : GetMobsNear(storm.x, storm.y, radius))
    {
        DealSpellHit(spellInfo, mob, damage);
        if (twice > 0.0f && roll_chance_f(twice))
            DealSpellHit(spellInfo, mob, damage);
    }
}

void BonkRun::GravityPull(OwnedWeapon const& owned, SpellEntry const* spellInfo, float x, float y, float z, float radius)
{
    PlayKitAt(ObjectGuid(), x, y, z, BONK_KIT_GRAVITY_PULL, true);
    for (Creature* mob : GetMobsNear(x, y, radius * std::max(1.0f, owned.weapon.behaviorValue)))
        PullMob(mob, x, y, z);

    PendingCast rain;
    rain.weaponId = owned.weapon.id;
    rain.spellId = spellInfo->Id;
    rain.leftMs = BONK_GRAVITY_RAIN_DELAY_MS;
    rain.x = x;
    rain.y = y;
    rain.z = z;
    m_pendingCasts.push_back(std::move(rain));
}

void BonkRun::PullMob(Creature* mob, float x, float y, float z)
{
    BonkMobAI* ai = dynamic_cast<BonkMobAI*>(mob->AI());
    if (!ai || mob->HasUnitState(UNIT_STATE_ROOT | UNIT_STATE_STUNNED) || mob->GetDistance2d(x, y) <= BONK_GRAVITY_PILE_RADIUS || !CanPull(mob))
        return;

    float const angle = std::atan2(mob->GetPositionY() - y, mob->GetPositionX() - x);
    float px = x + BONK_GRAVITY_PILE_RADIUS * std::cos(angle);
    float py = y + BONK_GRAVITY_PILE_RADIUS * std::sin(angle);
    float pz = z;
    mob->GetMap()->GetWalkHitPosition(nullptr, mob->GetPositionX(), mob->GetPositionY(), mob->GetPositionZ(), px, py, pz);
    ai->Pull(px, py, pz, BONK_GRAVITY_PULL_SPEED, BONK_GRAVITY_SETTLE_MS);
}

void BonkRun::LightFlare(OwnedWeapon const& owned, float x, float y, float z, float radius)
{
    uint32 const burnMs = uint32(std::max(0.0f, owned.weapon.behaviorValue) * GetWeaponDurationScale(owned));
    if (!burnMs)
        return;
    if (m_flares.size() >= BONK_MAX_FLARES)
        m_flares.erase(m_flares.begin());
    PlaceAreaVisual(m_player, owned.weapon.spellId, x, y, z, radius, burnMs);

    Flare flare;
    flare.x = x;
    flare.y = y;
    flare.radius = radius;
    flare.untilMs = m_totalMs + burnMs;
    m_flares.push_back(flare);
}

BonkRun::Flare const* BonkRun::FindFlareAt(float x, float y)
{
    m_flares.erase(std::remove_if(m_flares.begin(), m_flares.end(), [this](Flare const& flare) { return flare.untilMs <= m_totalMs; }), m_flares.end());
    for (Flare const& flare : m_flares)
    {
        float const dx = x - flare.x, dy = y - flare.y;
        if (dx * dx + dy * dy <= flare.radius * flare.radius)
            return &flare;
    }
    return nullptr;
}

void BonkRun::QueueFlareSplit(OwnedWeapon const& owned, SpellEntry const* spellInfo, Creature* target, float share, SpellCaster const* from)
{
    BonkWeapon const& weapon = owned.weapon;
    if (m_flares.empty() || weapon.behavior == BONK_BEHAVIOR_PET || weapon.behavior == BONK_BEHAVIOR_WRATH ||
        (weapon.damageEffectIndex < 0 && !weapon.weaponDamage))
        return;
    if (!FindFlareAt(target->GetPositionX(), target->GetPositionY()) || !TakeRateSlot(m_flareSplitTimes, BONK_MAX_FLARE_SPLITS))
        return;

    PendingCast split;
    split.weaponId = weapon.id;
    split.spellId = spellInfo->Id;
    split.leftMs = GetBounceDelay(from, target, spellInfo->speed);
    split.flareSplit = true;
    split.share = share;
    split.target = target->GetObjectGuid();
    target->GetPosition(split.x, split.y, split.z);
    split.source = HoldShotSource(split.x, split.y, split.z);
    m_pendingCasts.push_back(std::move(split));
}

void BonkRun::FireFlareSplit(OwnedWeapon& owned, SpellEntry const* spellInfo, PendingCast const& cast)
{
    float x = cast.x, y = cast.y, z = cast.z;
    Creature* struck = m_player->GetMap()->GetCreature(cast.target);
    if (struck)
        struck->GetPosition(x, y, z);
    Creature* source = ReleaseShotSource(cast.source, x, y, z);

    Flare const* flare = FindFlareAt(cast.x, cast.y);
    if (!flare || !source)
        return;
    float const flareX = flare->x, flareY = flare->y, flareRadius = flare->radius;

    for (Creature* mob : GetMobsNear(x, y, 2.0f * flareRadius))
    {
        if (mob->GetObjectGuid() == cast.target || mob->GetDistance2d(flareX, flareY) > flareRadius)
            continue;
        if (!source->IsWithinLOSInMap(mob))
            continue;
        StrikeFromMob(owned, spellInfo, mob, cast.share, source, struck);
        return;
    }
}

void BonkRun::QueueShardBurst(Creature const* dead)
{
    auto owned = std::find_if(m_weapons.begin(), m_weapons.end(), [](OwnedWeapon const& weapon)
    {
        return weapon.weapon.behavior == BONK_BEHAVIOR_SHARD_BURST;
    });
    if (owned == m_weapons.end() || !TakeRateSlot(m_shardBurstTimes, BONK_MAX_SHARD_BURSTS))
        return;

    PendingCast burst;
    burst.weaponId = owned->weapon.id;
    burst.spellId = owned->weapon.spellId;
    burst.leftMs = BONK_SHARD_BURST_DELAY_MS;
    burst.shardBurst = true;
    burst.target = dead->GetObjectGuid();
    dead->GetPosition(burst.x, burst.y, burst.z);
    m_pendingCasts.push_back(std::move(burst));
}

void BonkRun::ShardBurst(OwnedWeapon& owned, SpellEntry const* spellInfo, PendingCast const& cast)
{
    PlayKitAt(cast.target, cast.x, cast.y, cast.z, BONK_KIT_SHARD_BURST, true);
    float const damage = GetWeaponDamage(owned);
    float const radius = std::max(0.0f, owned.weapon.behaviorValue) * GetWeaponArea(owned);
    m_keywordlessHits = true;
    for (Creature* mob : GetMobsNear(cast.x, cast.y, radius))
        DealSpellHit(spellInfo, mob, damage);
    m_keywordlessHits = false;
}

void BonkRun::OnPoisonedDeath(Creature const* dead)
{
    for (OwnedWeapon const& owned : m_weapons)
    {
        BonkWeaponBehavior const behavior = owned.weapon.behavior;
        if ((behavior == BONK_BEHAVIOR_VENOM_BOLTS && TakeRateSlot(m_venomBoltTimes, BONK_MAX_VENOM_VOLLEYS)) ||
            (behavior == BONK_BEHAVIOR_VENOM_CLOUD && TakeRateSlot(m_venomCloudTimes, BONK_MAX_NEW_VENOM_CLOUDS)))
            QueueVenom(owned, dead);
    }
}

void BonkRun::QueueVenom(OwnedWeapon const& owned, Creature const* dead)
{
    bool const cloud = owned.weapon.behavior == BONK_BEHAVIOR_VENOM_CLOUD;
    PendingCast venom;
    venom.weaponId = owned.weapon.id;
    venom.spellId = owned.weapon.spellId;
    venom.leftMs = cloud ? 0 : BONK_VENOM_BOLT_DELAY_MS;
    venom.keywordCloud = cloud;
    venom.venomBolts = !cloud;
    dead->GetPosition(venom.x, venom.y, venom.z);
    if (!cloud)
        venom.source = HoldShotSource(venom.x, venom.y, venom.z);
    m_pendingCasts.push_back(std::move(venom));
}

void BonkRun::FireVenomBolts(OwnedWeapon& owned, SpellEntry const* spellInfo, PendingCast const& cast)
{
    PendingKeyword venom;
    if (!GetWeaponKeyword(owned, venom))
        return;
    uint32 const stacks = BONK_VENOM_BOLT_STACKS + uint32(GetSpellUpgrade(owned, BONK_SPELL_UPGRADE_STACKS));
    uint32 const arrows = std::max(1u, uint32(owned.weapon.behaviorValue));

    Creature* source = ReleaseShotSource(cast.source, cast.x, cast.y, cast.z);
    uint32 fired = 0;
    FiringWeapon firing(m_player->GetBonkPlayer());
    for (Creature* mob : GetMobsNear(cast.x, cast.y, BONK_VENOM_BOLT_RANGE))
    {
        if (fired >= arrows)
            break;
        if (!source)
            return;
        if (!source->IsWithinLOSInMap(mob))
            continue;
        source->CastSpell(mob, spellInfo, true, nullptr, nullptr, m_player->GetObjectGuid());
        ApplyKeyword(mob, venom.keyword, stacks, venom.durationMs, venom.damageScale, venom.spellId);
        ++fired;
    }
}

BonkRun::KeywordCloudKind BonkRun::GetCloudKind(OwnedWeapon const& owned) const
{
    if (owned.weapon.behavior == BONK_BEHAVIOR_BLOOD_POOL)
        return GetBloodPoolKind();
    KeywordCloudKind mage;
    if (GetMageCloudKind(owned, mage))
        return mage;
    KeywordCloudKind shaman;
    if (GetShamanCloudKind(owned, shaman))
        return shaman;
    KeywordCloudKind priest;
    if (GetPriestCloudKind(owned, priest))
        return priest;
    KeywordCloudKind rogue;
    if (GetRogueCloudKind(owned, rogue))
        return rogue;
    KeywordCloudKind paladin;
    if (GetPaladinCloudKind(owned, paladin))
        return paladin;
    KeywordCloudKind druid;
    if (GetDruidCloudKind(owned, druid))
        return druid;

    KeywordCloudKind venom;
    venom.lifeMs = BONK_VENOM_CLOUD_MS;
    venom.pulseEveryMs = BONK_VENOM_CLOUD_PULSE_MS;
    venom.stacks = BONK_VENOM_CLOUD_STACKS;
    venom.maxAlive = BONK_MAX_VENOM_CLOUDS;
    venom.lookSpellId = BONK_SPELL_VENOM_CLOUD_LOOK;
    venom.burstKit = BONK_KIT_VENOM_BURST;
    return venom;
}

float BonkRun::GetCloudRadius(OwnedWeapon const& owned) const
{
    switch (owned.weapon.behavior)
    {
        case BONK_BEHAVIOR_METEOR:
        case BONK_BEHAVIOR_BURNING_GROUND:
        case BONK_BEHAVIOR_SWIPE:
        case BONK_BEHAVIOR_FAN_OF_KNIVES:
            return std::max(0.0f, owned.weapon.behaviorValue) * GetWeaponArea(owned);
        default:
            return GetWeaponRadius(owned);
    }
}

void BonkRun::StartKeywordCloud(OwnedWeapon const& owned, float x, float y, float z)
{
    KeywordCloudKind const kind = GetCloudKind(owned);
    auto const fromWeapon = [&owned](KeywordCloud const& cloud) { return cloud.weaponId == owned.weapon.id; };
    if (size_t(std::count_if(m_keywordClouds.begin(), m_keywordClouds.end(), fromWeapon)) >= kind.maxAlive)
    {
        auto const oldest = std::find_if(m_keywordClouds.begin(), m_keywordClouds.end(), fromWeapon);
        EndKeywordCloud(*oldest);
        m_keywordClouds.erase(oldest);
    }

    KeywordCloud cloud;
    cloud.weaponId = owned.weapon.id;
    cloud.x = x;
    cloud.y = y;
    cloud.radius = GetCloudRadius(owned);
    cloud.leftMs = int32(kind.lifeMs * GetWeaponDurationScale(owned));
    cloud.pulseEveryMs = kind.pulseEveryMs;
    cloud.stacks = kind.stacks;
    if (kind.bloodRing || kind.natureRing)
        ShowRingAt(cloud, z, kind.natureRing ? BonkHeroRing::Nature : BonkHeroRing::BloodAura);
    else
        cloud.area = PlaceAreaVisual(m_player, kind.lookSpellId, x, y, z, cloud.radius, uint32(cloud.leftMs));
    m_keywordClouds.push_back(cloud);
    if (kind.burstKit)
        PlayKitAt(ObjectGuid(), x, y, z, kind.burstKit, true);
}

void BonkRun::ShowRingAt(KeywordCloud& cloud, float z, BonkHeroRing ring)
{
    cloud.markKey = NextBonkAuraKey();
    SendBonkMark(m_player, "cloud" + std::to_string(cloud.markKey), BONK_TELEGRAPH_CIRCLE, cloud.x, cloud.y, z, 0.0f, cloud.radius, 0, true,
        GetRingStyle(ring), ObjectGuid(), "", 0, true);
}

void BonkRun::EndKeywordCloud(KeywordCloud const& cloud)
{
    RemoveAreaVisual(m_player, cloud.area);
    if (cloud.markKey)
        SendBonkUnmark(m_player, "cloud" + std::to_string(cloud.markKey));
}

void BonkRun::ClearKeywordClouds()
{
    for (KeywordCloud const& cloud : m_keywordClouds)
        EndKeywordCloud(cloud);
    m_keywordClouds.clear();
}

void BonkRun::UpdateKeywordClouds(uint32 diff)
{
    for (size_t i = 0; i < m_keywordClouds.size();)
    {
        KeywordCloud& cloud = m_keywordClouds[i];
        OwnedWeapon const* owned = FindOwned(cloud.weaponId);
        cloud.leftMs -= int32(diff);
        cloud.pulseMs -= int32(diff);
        if (!owned || cloud.leftMs <= 0)
        {
            if (owned)
                OnDruidCloudEnd(*owned, cloud);
            EndKeywordCloud(cloud);
            m_keywordClouds.erase(m_keywordClouds.begin() + i);
            continue;
        }

        bool const pulses = cloud.pulseMs <= 0;
        if (pulses)
            cloud.pulseMs += cloud.pulseEveryMs;
        KeywordCloud const pulsed = cloud;
        ++i;
        if (pulses)
            PulseKeywordCloud(*owned, pulsed);
    }
}

void BonkRun::PulseKeywordCloud(OwnedWeapon const& owned, KeywordCloud const& cloud)
{
    OnPaladinCloudPulse(owned, cloud);
    OnDruidCloudPulse(owned, cloud);
    PendingKeyword keyword;
    if (GetWeaponKeyword(owned, keyword))
    {
        uint32 const stacks = cloud.stacks + uint32(GetSpellUpgrade(owned, BONK_SPELL_UPGRADE_STACKS));
        for (Creature* mob : GetMobsNear(cloud.x, cloud.y, cloud.radius))
            ApplyKeyword(mob, keyword.keyword, stacks, keyword.durationMs, keyword.damageScale, keyword.spellId);
    }
    OnShamanCloudPulse(owned, cloud);
    OnRogueCloudPulse(owned, cloud);
}

void BonkRun::ScatterBomblets(OwnedWeapon const& owned, SpellEntry const* spellInfo, float x, float y, float z)
{
    struct Spot
    {
        float x = 0.0f, y = 0.0f, z = 0.0f;
        float distance = 0.0f;
    };
    std::vector<Spot> spots;
    Map* map = m_player->GetMap();
    float const area = GetWeaponArea(owned);
    float const startAngle = frand(0.0f, 2.0f * M_PI_F);
    for (uint32 i = 0; i < BONK_CLUSTER_BOMBLETS; ++i)
    {
        float const angle = startAngle + 2.0f * M_PI_F * (i + frand(-0.3f, 0.3f)) / BONK_CLUSTER_BOMBLETS;
        float const toss = frand(BONK_BOMBLET_MIN_TOSS, BONK_BOMBLET_MAX_TOSS) * area;
        Spot spot;
        spot.x = x + toss * std::cos(angle);
        spot.y = y + toss * std::sin(angle);
        spot.z = z;
        map->GetWalkHitPosition(nullptr, x, y, z, spot.x, spot.y, spot.z);
        spot.distance = std::sqrt((spot.x - x) * (spot.x - x) + (spot.y - y) * (spot.y - y));
        spots.push_back(spot);
    }
    std::sort(spots.begin(), spots.end(), [](Spot const& a, Spot const& b) { return a.distance < b.distance; });

    SpellEntry const* tossInfo = sSpellMgr.GetSpellEntry(BONK_SPELL_BOMBLET_TOSS);
    int32 delay = BONK_BOMBLET_FIRST_MS - BONK_BOMBLET_GAP_MS;
    for (Spot const& spot : spots)
    {
        int32 landed = 0;
        if (tossInfo)
        {
            ShowMissileBetween(tossInfo, x, y, z + BONK_PROJECTILE_HEIGHT, spot.x, spot.y, spot.z);
            if (tossInfo->speed > 0.0f)
                landed = int32(spot.distance / tossInfo->speed * IN_MILLISECONDS);
        }
        delay = std::max(delay + BONK_BOMBLET_GAP_MS, landed);

        PendingCast bomblet;
        bomblet.weaponId = owned.weapon.id;
        bomblet.spellId = spellInfo->Id;
        bomblet.leftMs = delay;
        bomblet.bomblet = true;
        bomblet.x = spot.x;
        bomblet.y = spot.y;
        bomblet.z = spot.z;
        m_pendingCasts.push_back(std::move(bomblet));
    }
}

void BonkRun::Bomblet(OwnedWeapon& owned, SpellEntry const* spellInfo, PendingCast const& cast)
{
    PlayKitAt(ObjectGuid(), cast.x, cast.y, cast.z, BONK_KIT_BOMBLET, true);
    float const damage = GetWeaponDamage(owned) * BONK_BOMBLET_SHARE;
    for (Creature* mob : GetMobsNear(cast.x, cast.y, BONK_BOMBLET_RADIUS * GetWeaponArea(owned)))
        DealSpellHit(spellInfo, mob, damage);
}

void BonkRun::ReleaseSnakes(OwnedWeapon const& owned, float x, float y, float z)
{
    Map* map = m_player->GetMap();
    uint32 const lifeMs = uint32(owned.weapon.durationMs * GetWeaponDurationScale(owned));
    float const startAngle = frand(0.0f, 2.0f * M_PI_F);
    for (uint32 i = 0; i < BONK_SNAKES_PER_TRAP; ++i)
    {
        if (m_snakes.size() >= BONK_MAX_SNAKES)
        {
            if (Creature* oldest = map->GetCreature(m_snakes.front().guid))
                oldest->DespawnOrUnsummon();
            m_snakes.erase(m_snakes.begin());
        }

        float const angle = startAngle + 2.0f * M_PI_F * i / BONK_SNAKES_PER_TRAP;
        float sx = x + BONK_SNAKE_SPREAD * std::cos(angle);
        float sy = y + BONK_SNAKE_SPREAD * std::sin(angle);
        float sz = z;
        map->GetWalkHitPosition(nullptr, x, y, z, sx, sy, sz);
        s_nextHelperLevel = m_player->GetLevel();
        Creature* snake = m_player->SummonCreature(BONK_SNAKE, sx, sy, sz, angle, TEMPSUMMON_TIMED_DESPAWN,
            BONK_OBJECT_LIFETIME_SEC * IN_MILLISECONDS, false, 0, &InstallPetAI);
        if (!snake)
            continue;
        snake->SetOwnerGuid(m_player->GetObjectGuid());
        snake->SetUInt32Value(UNIT_CREATED_BY_SPELL, owned.weapon.spellId);
        snake->SetFactionTemplateId(m_player->GetFactionTemplateId());
        snake->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IMMUNE_TO_PLAYER);
        snake->SetHealth(snake->GetMaxHealth());
        snake->SetInvincibilityHpThreshold(snake->GetMaxHealth());

        Snake released;
        released.guid = snake->GetObjectGuid();
        released.weaponId = owned.weapon.id;
        released.untilMs = m_totalMs + lifeMs;
        m_snakes.push_back(released);
    }
    DressSnakes();
    m_petTimer = BONK_PET_CHECK_MS;
}

void BonkRun::DressSnakes()
{
    Map* map = m_player->GetMap();
    bool const frenzied = IsPetFrenzied();
    float const speed = GetSummonSpeedRate(GetStat(BONK_PASSIVE_MOVE_SPEED));
    for (Snake const& released : m_snakes)
    {
        OwnedWeapon const* owned = FindOwned(released.weaponId);
        Creature* snake = owned ? map->GetCreature(released.guid) : nullptr;
        if (!snake || !snake->IsAlive())
            continue;

        float const bite = GetWeaponDamage(*owned) * (frenzied ? GetFrenzyDamage() : 1.0f);
        float const pace = GetWeaponCooldown(*owned) / float(std::max(1u, owned->weapon.cooldownMs));
        float const scale = GetSummonLookScale(*owned, BONK_SNAKE_SCALE);
        if (snake->GetBaseRunSpeedRate() != speed)
            snake->SetBaseSpeedRate(MOVE_RUN, speed);
        snake->SetBaseWeaponDamage(BASE_ATTACK, MINDAMAGE, bite * 0.9f);
        snake->SetBaseWeaponDamage(BASE_ATTACK, MAXDAMAGE, bite * 1.1f);
        snake->UpdateDamagePhysical(BASE_ATTACK);
        snake->SetAttackTime(BASE_ATTACK, std::max(300u, uint32(BONK_SNAKE_ATTACK_MS * pace)));
        if (snake->GetObjectScale() != scale)
        {
            snake->SetObjectScale(scale);
            snake->UpdateModelData();
        }
        if (frenzied && !snake->HasAura(BONK_SPELL_BESTIAL_WRATH))
            snake->AddAura(BONK_SPELL_BESTIAL_WRATH);
        else if (!frenzied && snake->HasAura(BONK_SPELL_BESTIAL_WRATH))
            snake->RemoveAurasDueToSpell(BONK_SPELL_BESTIAL_WRATH);
    }
}

void BonkRun::UpdateSnakes()
{
    if (m_snakes.empty())
        return;
    Map* map = m_player->GetMap();
    m_snakes.erase(std::remove_if(m_snakes.begin(), m_snakes.end(), [this, map](Snake const& released)
    {
        Creature* snake = map->GetCreature(released.guid);
        if (snake && snake->IsAlive() && FindOwned(released.weaponId) && int32(released.untilMs - m_totalMs) > 0)
            return false;
        if (snake)
            snake->DespawnOrUnsummon();
        return true;
    }), m_snakes.end());
}

void BonkRun::DespawnSnakes()
{
    if (Map* map = m_player->FindMap())
        for (Snake const& released : m_snakes)
            if (Creature* snake = map->GetCreature(released.guid))
                snake->DespawnOrUnsummon();
    m_snakes.clear();
}

bool BonkRun::IsSnake(ObjectGuid const& guid) const
{
    return std::any_of(m_snakes.begin(), m_snakes.end(), [&guid](Snake const& released) { return released.guid == guid; });
}

void BonkRun::QueueShatter(OwnedWeapon const& owned, float x, float y, float z)
{
    PendingCast shatter;
    shatter.weaponId = owned.weapon.id;
    shatter.spellId = owned.weapon.spellId;
    shatter.leftMs = int32(owned.weapon.durationMs * GetWeaponDurationScale(owned));
    shatter.shatter = true;
    shatter.x = x;
    shatter.y = y;
    shatter.z = z;
    m_pendingCasts.push_back(std::move(shatter));
}

void BonkRun::Shatter(OwnedWeapon& owned, SpellEntry const* spellInfo, PendingCast const& cast)
{
    PlayKitAt(ObjectGuid(), cast.x, cast.y, cast.z, BONK_KIT_FROST_NOVA, true);
    float const damage = GetWeaponDamage(owned);
    for (Creature* mob : GetMobsNear(cast.x, cast.y, GetWeaponRadius(owned) * (1.0f + GetSpellUpgrade(owned, BONK_SPELL_UPGRADE_TRAP_AREA) / 100.0f)))
        DealSpellHit(spellInfo, mob, damage);
}

void BonkRun::FormIcePath(OwnedWeapon const& owned)
{
    if (m_icePath.weaponId != owned.weapon.id)
        m_icePath = IcePath();
    m_icePath.weaponId = owned.weapon.id;
    m_icePath.leftMs = std::max(m_icePath.leftMs, int32(owned.weapon.durationMs * GetWeaponDurationScale(owned)));
    m_icePath.slowMs = 0;
}

void BonkRun::UpdateIcePath(uint32 diff)
{
    if (!m_icePath.weaponId)
        return;

    OwnedWeapon const* owned = FindOwned(m_icePath.weaponId);
    SpellEntry const* spellInfo = owned ? sSpellMgr.GetSpellEntry(owned->weapon.spellId) : nullptr;
    m_icePath.leftMs -= int32(diff);
    if (!spellInfo || m_icePath.leftMs <= 0 || !m_player->IsAlive())
    {
        m_icePath = IcePath();
        HideHeroRing(BonkHeroRing::IcePath);
        return;
    }

    float x, y, z;
    GetHeroPositionNow(m_player, x, y, z);
    float const radius = GetWeaponRadius(*owned);
    ShowHeroRing(BonkHeroRing::IcePath, radius);

    m_icePath.slowMs -= int32(diff);
    if (m_icePath.slowMs > 0)
        return;
    m_icePath.slowMs = BONK_ICE_PATH_SLOW_PULSE_MS;
    for (Creature* mob : GetMobsNear(x, y, radius))
        SlowInIcePath(mob, spellInfo);
}

void BonkRun::SlowInIcePath(Creature* mob, SpellEntry const* spellInfo)
{
    SlowNormalMob(mob, spellInfo, nullptr, BONK_ICE_PATH_SLOW_MS);
}

void BonkRun::SlowNormalMob(Creature* mob, SpellEntry const* spellInfo, int32 const* points, int32 durationMs)
{
    BonkMobAI const* ai = dynamic_cast<BonkMobAI const*>(mob->AI());
    if (!ai || ai->GetKind() != BONK_SPAWN_NORMAL || !mob->IsAlive())
        return;

    if (SpellAuraHolder* holder = mob->GetSpellAuraHolder(spellInfo->Id, m_player->GetObjectGuid()))
    {
        holder->SetAuraMaxDuration(durationMs);
        holder->RefreshHolder();
        return;
    }

    SpellAuraHolder* holder = CreateSpellAuraHolder(spellInfo, mob, m_player, m_player);
    for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
        if (spellInfo->EffectApplyAuraName[i] == SPELL_AURA_MOD_DECREASE_SPEED)
        {
            int32 slow = points ? *points : 0;
            Aura* aura = CreateAura(spellInfo, SpellEffectIndex(i), points ? &slow : nullptr, holder, mob, m_player);
            aura->SetPositive(false);
            holder->AddAura(aura, SpellEffectIndex(i));
        }
    if (holder->IsEmptyHolder())
    {
        delete holder;
        return;
    }
    holder->SetPermanent(false);
    holder->SetAuraMaxDuration(durationMs);
    holder->SetAuraDuration(durationMs);
    mob->AddSpellAuraHolder(holder);
}

void BonkRun::ApplyMasterySlow(Creature* mob, float percent)
{
    SpellEntry const* spellInfo = sSpellMgr.GetSpellEntry(BONK_SPELL_ARCANE_SLOW);
    int32 const points = -int32(percent + 0.5f);
    if (spellInfo && points < 0)
        SlowNormalMob(mob, spellInfo, &points, BONK_ARCANE_SLOW_MS);
}

void BonkRun::ClearHunterEvolutions()
{
    m_arrowStorms.clear();
    m_flares.clear();
    m_flareSplitTimes.clear();
    m_shardBurstTimes.clear();
    m_steady = false;
    m_steadySinceMs = m_totalMs;
    m_venomBoltTimes.clear();
    ClearKeywordClouds();
    m_venomCloudTimes.clear();
    m_icePath = IcePath();
    HideHeroRing(BonkHeroRing::IcePath);
    m_pendingSlows.clear();
    m_stampedeTimer = BONK_STAMPEDE_FIRST_MS;
    m_plagueTimer = BONK_PLAGUE_PULSE_MS;
    m_screechTimer = 0;
    m_frenzyGlowMs = 0;
}

float BonkRun::GetBuffPower() const
{
    float power = 0.0f;
    uint32 const now = WorldTimer::getMSTime();
    for (auto const& itr : m_buffs)
        if (int32(itr.second - now) > 0)
            if (OwnedWeapon const* owned = FindOwned(itr.first))
                power += owned->weapon.behaviorValue * (1.0f + owned->damageBonus / 100.0f);
    return power;
}

bool BonkRun::IsReckless() const
{
    uint32 const now = WorldTimer::getMSTime();
    for (auto const& itr : m_buffs)
        if (int32(itr.second - now) > 0)
            if (OwnedWeapon const* owned = FindOwned(itr.first))
                if (owned->weapon.HasFlag(BONK_WEAPON_FLAG_ALWAYS_CRIT))
                    return true;
    return false;
}

void BonkRun::SpreadFrom(Creature const* dead)
{
    uint32 const now = WorldTimer::getMSTime();
    FiringWeapon firing(m_player->GetBonkPlayer());
    for (auto& owned : m_weapons)
    {
        BonkWeapon const& weapon = owned.weapon;
        if (weapon.behavior != BONK_BEHAVIOR_SPREAD)
            continue;

        auto carrier = owned.carriers.find(dead->GetObjectGuid());
        if (carrier == owned.carriers.end())
            continue;
        bool const carried = int32(carrier->second - now) > 0;
        owned.carriers.erase(carrier);

        if (!carried || (owned.rank < BONK_SPREAD_RANK && !weapon.evolvesFrom))
            continue;
        SpellEntry const* spellInfo = sSpellMgr.GetSpellEntry(weapon.spellId);
        if (!spellInfo)
            continue;

        uint32 const jumps = uint32(weapon.behaviorValue) + GetSpellCount(owned);
        uint32 spread = 0;
        for (Creature* next : GetMobsInRange(BONK_SPREAD_RADIUS * GetWeaponArea(owned), dead))
        {
            if (spread >= jumps)
                break;
            auto itr = owned.carriers.find(next->GetObjectGuid());
            if (itr != owned.carriers.end() && int32(itr->second - now) > 0)
                continue;
            StrikeWith(owned, spellInfo, next, 1.0f);
            ++spread;
        }
    }
}

void BonkRun::FireWeapon(OwnedWeapon& owned)
{
    BonkWeapon const& weapon = owned.weapon;
    SpellEntry const* spellInfo = sSpellMgr.GetSpellEntry(weapon.spellId);
    if (!spellInfo)
        return;
    owned.killed = false;
    RollCastCharges(owned);
    TalentCastScope talentCast(*this, owned);

    owned.firedByClass = true;
    if (FirePriestSpell(owned, spellInfo) || FireRogueSpell(owned, spellInfo) || FirePaladinSpell(owned, spellInfo) || FireWarlockSpell(owned, spellInfo) ||
        FireDruidSpell(owned, spellInfo))
    {
        if (owned.timer == int32(GetWeaponCooldown(owned)))
            OnTalentCastDone(owned);
        if (owned.killed && weapon.HasFlag(BONK_WEAPON_FLAG_RESET_ON_KILL))
            owned.timer = std::min<int32>(owned.timer, BONK_CLEARCAST_DELAY_MS);
        return;
    }
    owned.firedByClass = false;

    if (weapon.IsShieldBlock() || weapon.behavior == BONK_BEHAVIOR_WARD)
    {
        if (m_player->IsAlive())
        {
            AddSpellShield(GetShieldGain(owned));
            if (weapon.IsShieldBlock())
                m_player->SendPlaySpellVisualKit(BONK_KIT_SHIELD_BLOCK);
        }
        owned.timer = GetWeaponCooldown(owned);
        return;
    }

    if (weapon.behavior == BONK_BEHAVIOR_DEEP_WOUNDS || weapon.behavior == BONK_BEHAVIOR_MAELSTROM || weapon.IsBlink())
    {
        owned.timer = BONK_PASSIVE_TRIGGER_IDLE_MS;
        return;
    }

    if (weapon.behavior == BONK_BEHAVIOR_LIGHTNING_SHIELD)
    {
        ChargeLightningShield();
        owned.timer = GetWeaponCooldown(owned);
        return;
    }

    if (weapon.behavior == BONK_BEHAVIOR_BLOOD_AURA)
    {
        PulseBloodAura(owned);
        owned.timer = GetWeaponCooldown(owned);
        return;
    }

    if (weapon.behavior == BONK_BEHAVIOR_ROLLING_THUNDER && !IsThunderReady(owned))
    {
        owned.timer = BONK_PASSIVE_TRIGGER_POLL_MS;
        return;
    }

    float const areaScale = GetWeaponArea(owned);
    for (uint32 spellId : { weapon.spellId, weapon.altSpellId })
    {
        if (!spellId)
            continue;
        BonkPlayer::SpellTweak& tweak = m_player->GetBonkPlayer()->TweakSpell(spellId);
        tweak.areaScale = areaScale * (weapon.target == BONK_TARGET_GROUND && spellId == weapon.spellId ? weapon.SpellRadiusScale() : 1.0f);
        tweak.chainBonus = 0;
        tweak.jumpRadius = weapon.HasFlag(BONK_WEAPON_FLAG_LONG_JUMPS) ? BONK_LONG_JUMP_RADIUS : CHAIN_SPELL_JUMP_RADIUS;
        tweak.durationMs = weapon.durationMs;
        tweak.durationScale = GetWeaponDurationScale(owned);
        tweak.weaponDamage = weapon.weaponDamage ? std::max(1, int32(GetWeaponDamage(owned))) : 0;
        tweak.tickMs = weapon.tickMs;
        tweak.tickScale = 1.0f / GetWeaponHasteRate(owned);
        tweak.normalEnemiesOnly = weapon.HasFlag(BONK_WEAPON_FLAG_NORMAL_ONLY);
        tweak.oneTargetPerCast = weapon.target == BONK_TARGET_SELF_AREA;
    }
    BonkPlayer::SpellTweak& tweak = m_player->GetBonkPlayer()->TweakSpell(weapon.spellId);
    uint32 const now = WorldTimer::getMSTime();

    if (weapon.behavior == BONK_BEHAVIOR_ICE_NOVA)
    {
        owned.timer = BurstIceNovas(owned, spellInfo) ? GetWeaponCooldown(owned) : 250;
        return;
    }

    if (weapon.behavior == BONK_BEHAVIOR_ARCANE_BARRAGE && m_player->IsMoving() && IsBlinkDash())
    {
        BankMissiles(owned);
        owned.timer = GetWeaponCooldown(owned);
        return;
    }

    if (weapon.behavior == BONK_BEHAVIOR_BUFF)
    {
        if (m_player->IsAlive())
        {
            FiringWeapon firing(m_player->GetBonkPlayer());
            m_player->CastSpell(m_player, spellInfo, true);
            m_buffs[weapon.id] = now + uint32(weapon.durationMs * tweak.durationScale);
        }
        owned.timer = GetWeaponCooldown(owned);
        return;
    }

    if (weapon.behavior == BONK_BEHAVIOR_PET)
    {
        UpdatePets(owned);
        owned.timer = GetWeaponCooldown(owned);
        return;
    }

    if (weapon.behavior == BONK_BEHAVIOR_WRATH)
    {
        UpdatePets(owned);
        bool const howled = Howl(owned);
        if (howled && weapon.healPct > 0.0f && m_player->IsAlive())
            m_player->DealHeal(m_player, ReduceHealing(std::max(1u, uint32(m_player->GetMaxHealth() * weapon.healPct / 100.0f))), spellInfo);
        owned.timer = howled ? GetWeaponCooldown(owned) : 250;
        return;
    }

    if ((weapon.behavior == BONK_BEHAVIOR_TRAP && weapon.target == BONK_TARGET_SELF) || weapon.behavior == BONK_BEHAVIOR_ICE_PATH)
    {
        if (GetMobsInRange(BONK_TRAP_AWARE_RANGE).empty())
        {
            owned.timer = 250;
            return;
        }
        if (weapon.behavior == BONK_BEHAVIOR_ICE_PATH)
            FormIcePath(owned);
        else
            DropTraps(owned);
        owned.timer = GetWeaponCooldown(owned);
        return;
    }

    if (weapon.IsRevenge() && !WasStruckRecently())
    {
        owned.timer = 100;
        return;
    }

    bool const reachGrows = weapon.target == BONK_TARGET_SELF_AREA || weapon.target == BONK_TARGET_CONE || weapon.behavior == BONK_BEHAVIOR_CLEAVE ||
        weapon.IsMeleeStrike();
    bool const thrown = weapon.behavior == BONK_BEHAVIOR_TRAP && weapon.target == BONK_TARGET_GROUND;
    bool const radial = weapon.target == BONK_TARGET_SELF_AREA || weapon.target == BONK_TARGET_CONE;
    float const range = thrown ? BONK_TRAP_THROW_RANGE : radial ? weapon.radius * areaScale : weapon.range * (reachGrows ? areaScale : 1.0f);
    std::vector<Creature*> mobs = GetMobsInRange(range);

    if (weapon.target == BONK_TARGET_RANDOM)
    {
        for (auto itr = owned.recentTargets.begin(); itr != owned.recentTargets.end();)
        {
            if (now - itr->second > BONK_DOT_RETARGET_MS)
                itr = owned.recentTargets.erase(itr);
            else
                ++itr;
        }
        mobs.erase(std::remove_if(mobs.begin(), mobs.end(), [&](Creature* mob)
        {
            return mob->HasAura(weapon.spellId) || owned.recentTargets.count(mob->GetObjectGuid());
        }), mobs.end());
    }
    else if (weapon.target == BONK_TARGET_STRONGEST)
        SortStrongestFirst(mobs);
    else if (weapon.target == BONK_TARGET_FARTHEST)
        std::reverse(mobs.begin(), mobs.end());
    else if (weapon.IsFireBlast() || weapon.behavior == BONK_BEHAVIOR_LAVA_BURST)
        SortMostBurningFirst(mobs);
    else if (weapon.behavior == BONK_BEHAVIOR_EARTH_SHOCK)
        SortMostStaticFirst(mobs);

    if (weapon.killBelowPct > 0.0f)
        std::stable_partition(mobs.begin(), mobs.end(), [&](Creature* mob) { return CanKill(weapon, mob); });

    uint32 const swept = weapon.HasFlag(BONK_WEAPON_FLAG_SWEEP) ? SweepExecute(owned, spellInfo, range) : 0;
    if (mobs.empty())
    {
        owned.timer = swept ? GetWeaponCooldown(owned) : 250;
        return;
    }
    if (weapon.IsRevenge())
        m_struck = false;

    if (weapon.behavior == BONK_BEHAVIOR_ORB)
    {
        LaunchOrb(owned);
        owned.timer = GetWeaponCooldown(owned);
        return;
    }

    if (weapon.IsPyroblast())
    {
        owned.timer = HurlPyroblasts(owned, spellInfo, std::move(mobs)) ? GetWeaponCooldown(owned) : 250;
        return;
    }

    ++owned.casts;
    SpellEntry const* castInfo = spellInfo;
    float castShare = 1.0f;
    if (weapon.altEvery && weapon.altSpellId && owned.casts % weapon.altEvery == 0)
        if (SpellEntry const* altInfo = sSpellMgr.GetSpellEntry(weapon.altSpellId))
        {
            castInfo = altInfo;
            castShare = weapon.altDamage;
        }

    optional<int32> basePoints[MAX_EFFECT_INDEX];
    GetBasePoints(owned, castInfo, castShare, basePoints);

    FiringWeapon firing(m_player->GetBonkPlayer());

    auto castAgain = [&]()
    {
        uint32 const pulses = std::min(GetSpellCount(owned), BONK_MAX_COUNT_PULSES);
        for (uint32 i = 1; i <= pulses; ++i)
        {
            PendingCast again;
            again.weaponId = weapon.id;
            again.spellId = castInfo->Id;
            again.leftMs = int32(i * BONK_COUNT_RECAST_MS);
            again.recast = true;
            m_pendingCasts.push_back(again);
        }
    };

    if (weapon.target == BONK_TARGET_SELF_AREA)
    {
        m_vengeanceShare = TakeVengeanceShare(owned);
        if (!SwingWide(owned, castInfo, areaScale))
            CastSelfArea(owned, castInfo, areaScale);
        m_vengeanceShare = 1.0f;
        castAgain();

        if (weapon.IsWhirlwind())
            SpinWhirlwind(owned, castInfo);
        if (weapon.IsRevenge())
            OnRevenge(owned);
        if (weapon.behavior == BONK_BEHAVIOR_DEMORALIZING_SHOUT)
            DemoralizingShout(owned, castInfo, mobs);

        if (weapon.behavior == BONK_BEHAVIOR_ECHO)
        {
            PendingCast echo;
            echo.weaponId = weapon.id;
            echo.spellId = castInfo->Id;
            echo.leftMs = int32(weapon.behaviorValue);
            echo.echo = true;
            m_pendingCasts.push_back(echo);
        }

        if (weapon.behavior == BONK_BEHAVIOR_SPIN && weapon.behaviorValue >= 100.0f)
        {
            uint32 const every = uint32(weapon.behaviorValue);
            uint32 const length = uint32(weapon.durationMs * tweak.durationScale);
            for (uint32 at = every; at <= length; at += every)
            {
                PendingCast pulse;
                pulse.weaponId = weapon.id;
                pulse.spellId = castInfo->Id;
                pulse.leftMs = int32(at);
                pulse.selfArea = true;
                m_pendingCasts.push_back(pulse);
            }
        }

        if (weapon.behavior == BONK_BEHAVIOR_DELAYED && weapon.altSpellId)
        {
            PendingCast wave;
            wave.weaponId = weapon.id;
            wave.spellId = weapon.altSpellId;
            wave.leftMs = int32(weapon.behaviorValue);
            wave.selfArea = true;
            m_pendingCasts.push_back(wave);
        }

        if (weapon.behavior == BONK_BEHAVIOR_SHIELD_PER_HIT && m_player->IsAlive())
            AddSpellShield(uint32(m_player->GetMaxHealth() * weapon.behaviorValue / 100.0f * mobs.size()));

        if (weapon.behavior == BONK_BEHAVIOR_FREEZE && weapon.altSpellId)
            for (Creature* mob : mobs)
                if (m_player->HasInArc(mob, BONK_FREEZE_ARC))
                    m_player->CastSpell(mob, weapon.altSpellId, true);
    }
    else if (weapon.target == BONK_TARGET_CONE)
    {
        std::vector<Creature*> const cone = GetMobsInCone(owned, range);
        for (Creature* mob : cone)
            StrikeWith(owned, castInfo, mob, castShare);
        if (cone.empty())
        {
            owned.timer = 250;
            return;
        }
        castAgain();
    }
    else if (weapon.target == BONK_TARGET_GROUND)
    {
        mobs.erase(std::remove_if(mobs.begin(), mobs.end(), [&](Creature* mob)
        {
            return !m_player->IsWithinLOSInMap(mob);
        }), mobs.end());

        if (weapon.applies == BONK_KEYWORD_HUNTERS_MARK)
        {
            std::vector<Creature*> unmarked;
            for (Creature* mob : mobs)
                if (!GetKeywordStacks(mob, BONK_KEYWORD_HUNTERS_MARK))
                    unmarked.push_back(mob);
            if (!unmarked.empty())
                mobs.swap(unmarked);
        }

        bool const keywordOnly = weapon.damageEffectIndex < 0 && !weapon.weaponDamage && weapon.applies != BONK_KEYWORD_NONE;
        float const radius = weapon.groundRadius * areaScale;

        uint32 const drops = 1 + GetSpellCount(owned) + GetWarlockExtraDrops(owned);
        std::vector<Creature*> const spots = PickGroundSpots(mobs, radius, drops);
        if (spots.empty())
        {
            owned.timer = 250;
            return;
        }
        for (Creature* spot : spots)
        {
            if (weapon.behavior == BONK_BEHAVIOR_TRAP)
            {
                ThrowTrap(owned, spot->GetPositionX(), spot->GetPositionY(), spot->GetPositionZ());
                continue;
            }

            float const x = spot->GetPositionX(), y = spot->GetPositionY(), z = spot->GetPositionZ();
            if (keywordOnly)
                ShowMissileAt(m_player, castInfo, x, y, z);
            else if (weapon.behavior == BONK_BEHAVIOR_ARROW_STORM)
                StartArrowStorm(owned, castInfo, x, y, z);
            else if (weapon.behavior == BONK_BEHAVIOR_GRAVITY_PULL)
                GravityPull(owned, castInfo, x, y, z, radius);
            else if (weapon.behavior == BONK_BEHAVIOR_FIREWALK)
                WalkFire(owned, castInfo, x, y, z, radius);
            else
                CastAtGround(m_player, castInfo, basePoints, x, y, z);
            if (weapon.behavior == BONK_BEHAVIOR_FLARE)
                LightFlare(owned, x, y, z, radius);
            OnMageGroundDrop(owned, x, y, z);
            OnWarlockGroundDrop(owned, x, y, z);

            PendingKeyword keyword;
            if (keywordOnly && GetWeaponKeyword(owned, keyword))
            {
                std::vector<Creature*> inside = GetMobsInRange(radius, spot);
                inside.push_back(spot);
                for (Creature* mob : inside)
                    ApplyKeyword(mob, keyword.keyword, keyword.stacks, keyword.durationMs, keyword.damageScale, keyword.spellId);
            }

            if (weapon.behavior == BONK_BEHAVIOR_DELAYED && weapon.altSpellId)
            {
                PendingCast burst;
                burst.weaponId = weapon.id;
                burst.spellId = weapon.altSpellId;
                burst.leftMs = int32(weapon.behaviorValue);
                burst.x = spot->GetPositionX();
                burst.y = spot->GetPositionY();
                burst.z = spot->GetPositionZ();
                m_pendingCasts.push_back(burst);
            }
        }
    }
    else if (weapon.target == BONK_TARGET_LINE)
    {
        mobs.erase(std::remove_if(mobs.begin(), mobs.end(), [&](Creature* mob) { return !m_player->IsWithinLOSInMap(mob); }), mobs.end());
        std::vector<Creature*> marked;
        for (Creature* mob : mobs)
            if (GetKeywordStacks(mob, BONK_KEYWORD_HUNTERS_MARK))
                marked.push_back(mob);
        uint32 const shots = 1 + GetSpellCount(owned);
        std::vector<Creature*> aims = PickGroundSpots(marked, BONK_LINE_GROUP_RADIUS, shots);
        if (aims.size() < shots)
            for (Creature* aim : PickGroundSpots(mobs, BONK_LINE_GROUP_RADIUS, shots))
                if (aims.size() < shots && std::find(aims.begin(), aims.end(), aim) == aims.end())
                    aims.push_back(aim);

        std::vector<Creature*> struck;
        for (Creature* aim : aims)
            for (Creature* mob : GetMobsInLine(owned, range, m_player->GetAngle(aim)))
                if (std::find(struck.begin(), struck.end(), mob) == struck.end() && m_player->IsWithinLOSInMap(mob))
                {
                    StrikeWith(owned, castInfo, mob, castShare);
                    struck.push_back(mob);
                }
        if (struck.empty())
        {
            owned.timer = 250;
            return;
        }
        if (weapon.behavior == BONK_BEHAVIOR_CHAIN_DETONATION)
            QueueDetonations(owned, castInfo, castShare * weapon.behaviorValue, std::move(struck));
    }
    else
    {
        if (weapon.target == BONK_TARGET_RANDOM)
            for (size_t i = mobs.size(); i > 1; --i)
                std::swap(mobs[i - 1], mobs[urand(0, uint32(i - 1))]);

        if (weapon.behavior == BONK_BEHAVIOR_SPREAD)
            for (auto itr = owned.carriers.begin(); itr != owned.carriers.end();)
            {
                if (int32(itr->second - now) <= 0)
                    itr = owned.carriers.erase(itr);
                else
                    ++itr;
            }

        PreferMarked(mobs);
        BonkProjectileInfo const* projectile = sBonkMgr.GetProjectile(castInfo->Id);
        bool const bloodbath = IsBloodbathing(owned);
        uint32 const targets = bloodbath ? uint32(mobs.size()) : GetWeaponTargets(owned);
        std::vector<Creature*> struck;
        for (Creature* mob : mobs)
        {
            if (struck.size() >= targets)
                break;
            if (!m_player->IsWithinLOSInMap(mob))
                continue;
            if (projectile)
                LaunchProjectiles(owned, *projectile, castInfo, castShare, m_player, mob);
            else if (weapon.behavior == BONK_BEHAVIOR_AVENGERS_SHIELD)
                ThrowShield(owned, castInfo, mob, castShare);
            else
            {
                StrikeWith(owned, castInfo, mob, castShare);
                if (weapon.behavior == BONK_BEHAVIOR_RICOCHET)
                    QueueRicochet(owned, castInfo, mob, m_player, castShare, {}, uint32(weapon.behaviorValue));
            }
            if (weapon.target == BONK_TARGET_RANDOM)
                owned.recentTargets[mob->GetObjectGuid()] = now;
            struck.push_back(mob);
        }
        uint32 const fired = uint32(struck.size());

        bool const cleave = weapon.behavior == BONK_BEHAVIOR_CLEAVE;
        bool const pierce = weapon.IsAimed() && !weapon.chains && weapon.behavior != BONK_BEHAVIOR_SPREAD && !projectile;
        auto strikeNear = [&](uint32 victims, float share, float radius)
        {
            for (size_t i = 0; i < fired; ++i)
            {
                uint32 hit = 0;
                for (Creature* next : GetMobsInRange(radius, struck[i]))
                {
                    if (hit >= victims)
                        break;
                    if (std::find(struck.begin(), struck.end(), next) != struck.end())
                        continue;
                    StrikeWith(owned, castInfo, next, share * castShare);
                    if (weapon.target == BONK_TARGET_RANDOM)
                        owned.recentTargets[next->GetObjectGuid()] = now;
                    struck.push_back(next);
                    ++hit;
                }
            }
        };
        uint32 const victims = cleave ? 1 + weapon.baseJumps : pierce ? weapon.baseJumps : 0;
        if (victims)
            strikeNear(victims, cleave ? weapon.behaviorValue : 1.0f, BONK_CLEAVE_RADIUS * (cleave ? areaScale : 1.0f));
        float const spellCleave = GetSpellUpgrade(owned, BONK_SPELL_UPGRADE_CLEAVE) / 100.0f;
        if (spellCleave > 0.0f && !projectile)
            strikeNear(BONK_SPELL_CLEAVE_TARGETS, spellCleave, BONK_CLEAVE_RADIUS);
        if ((victims || spellCleave > 0.0f) && weapon.weaponDamage)
            tweak.weaponDamage = std::max(1, int32(GetWeaponDamage(owned)));

        if (!fired)
        {
            owned.timer = 250;
            return;
        }
        SwingWeapon(owned);
        if (weapon.behavior != BONK_BEHAVIOR_AVENGERS_SHIELD)
            ShowStrikeImpact(castInfo, struck.front());
        if (weapon.behavior == BONK_BEHAVIOR_BLOODTHIRST)
            HealFromStrikes(owned, spellInfo, fired);
        if (bloodbath)
            ShowBloodbath();
    }

    if (weapon.healPct > 0.0f && weapon.behavior != BONK_BEHAVIOR_BLOODTHIRST && weapon.behavior != BONK_BEHAVIOR_DEMORALIZING_SHOUT && m_player->IsAlive())
        m_player->DealHeal(m_player, ReduceHealing(std::max(1u, uint32(m_player->GetMaxHealth() * weapon.healPct / 100.0f))), spellInfo);

    owned.timer = GetWeaponCooldown(owned);
    OnPriestCast(owned);
    if (weapon.behavior == BONK_BEHAVIOR_ROLLING_THUNDER)
        SpendThunder(owned);

    OnTalentCastDone(owned);
    bool const clearcast = m_mastery.clearcasting > 0.0f && roll_chance_f(m_mastery.clearcasting);
    if (clearcast && m_mastery.Special(BONK_SPECIAL_CLEARCASTING_BOOST) > 0.0f)
        m_clearcastBoostReady = true;
    if (clearcast || (owned.killed && weapon.HasFlag(BONK_WEAPON_FLAG_RESET_ON_KILL)))
        owned.timer = std::min<int32>(owned.timer, BONK_CLEARCAST_DELAY_MS);
}

void BonkRun::ResetOnKill(uint32 spellId)
{
    for (OwnedWeapon& owned : m_weapons)
        if (owned.weapon.spellId == spellId && owned.weapon.HasFlag(BONK_WEAPON_FLAG_RESET_ON_KILL))
        {
            owned.timer = std::min<int32>(owned.timer, BONK_CLEARCAST_DELAY_MS);
            owned.killed = true;
        }
}

bool BonkRun::HasBehavior(BonkWeaponBehavior behavior) const
{
    return std::any_of(m_weapons.begin(), m_weapons.end(), [behavior](OwnedWeapon const& owned) { return owned.weapon.behavior == behavior; });
}

void BonkRun::CastSelfArea(OwnedWeapon& owned, SpellEntry const* spellInfo, float areaScale)
{
    float const share = spellInfo->Id == owned.weapon.spellId ? 1.0f : owned.weapon.altDamage;
    FiringWeapon firing(m_player->GetBonkPlayer());
    if (owned.weapon.IsWhirlwind())
        OnWhirlwindSpin(owned);

    if (spellInfo->EffectImplicitTargetA[EFFECT_INDEX_0] == TARGET_UNIT_ENEMY)
    {
        uint32 struck = 0;
        Creature* first = nullptr;
        for (Creature* mob : GetMobsInRange(owned.weapon.radius * areaScale))
            if (m_player->IsWithinLOSInMap(mob))
            {
                OnAroundStrike(owned, mob);
                StrikeWith(owned, spellInfo, mob, share);
                if (!first)
                    first = mob;
                ++struck;
            }
        if (first)
            ShowStrikeImpact(spellInfo, first);
        OnAroundCast(owned, spellInfo, struck);
        OnMageSelfArea(owned);
        return;
    }

    bool const own = spellInfo->Id == owned.weapon.spellId;
    m_player->GetBonkPlayer()->TweakSpell(spellInfo->Id).areaScale = areaScale * (own ? owned.weapon.SpellRadiusScale() : 1.0f);
    optional<int32> basePoints[MAX_EFFECT_INDEX];
    GetBasePoints(owned, spellInfo, share, basePoints);
    m_player->CastCustomSpell(m_player, spellInfo, basePoints[0], basePoints[1], basePoints[2], true);
    ShowAreaImpact(spellInfo, owned.weapon.radius * areaScale);
    if (owned.weapon.HasFlag(BONK_WEAPON_FLAG_KNOCKBACK))
        for (Creature* mob : GetMobsInRange(owned.weapon.radius * areaScale))
            KnockBack(mob);
    OnMageSelfArea(owned);
}

void BonkRun::ShowStrike(Unit* caster, SpellEntry const* spellInfo, Unit* target) const
{
    ShowStrikeOn(caster, spellInfo, target);
}


void BonkRun::ShowSwing(SpellEntry const* look, Unit* target) const
{
    ShowSwingOn(m_player, look, target);
}

void BonkRun::ScaleRadius(SpellEntry const* spellInfo, float radius)
{
    float const own = GetSpellRadiusOf(spellInfo);
    m_player->GetBonkPlayer()->TweakSpell(spellInfo->Id).areaScale = own > 0.0f ? radius / own : 1.0f;
}

void BonkRun::UpdatePendingCasts(uint32 diff)
{
    if (m_wardBurst)
    {
        m_wardBurst = false;
        for (OwnedWeapon& owned : m_weapons)
            if (owned.weapon.behavior == BONK_BEHAVIOR_WARD && owned.weapon.altSpellId)
                if (SpellEntry const* info = sSpellMgr.GetSpellEntry(owned.weapon.altSpellId))
                    CastSelfArea(owned, info, GetWeaponArea(owned));
    }

    Map* map = m_player->GetMap();

    if (m_pendingCasts.empty())
        return;

    std::vector<PendingCast> due;
    for (auto itr = m_pendingCasts.begin(); itr != m_pendingCasts.end();)
    {
        itr->leftMs -= int32(diff);
        if (itr->leftMs > 0)
        {
            ++itr;
            continue;
        }
        due.push_back(*itr);
        itr = m_pendingCasts.erase(itr);
    }

    for (PendingCast const& cast : due)
    {
        if (cast.releaseLook)
        {
            SendBonkPropGone(m_player, cast.prop);
            continue;
        }

        if (cast.kit)
        {
            PlayKitAt(cast.target, cast.x, cast.y, cast.z, cast.kit, true);
            continue;
        }

        OwnedWeapon* owned = FindOwned(cast.weaponId);
        SpellEntry const* info = sSpellMgr.GetSpellEntry(cast.spellId);
        if (!owned || !info || !m_player->IsAlive())
        {
            if (cast.springTrap)
                SendBonkPropGone(m_player, cast.prop);
            m_heldShotSources.erase(cast.source);
            continue;
        }

        if (cast.springTrap)
        {
            ShowTrapSprung(*owned, cast.prop, cast.x, cast.y, cast.z);
            SpringTrap(*owned, cast.x, cast.y, cast.z);
            continue;
        }

        if (cast.recast)
        {
            float const area = GetWeaponArea(*owned);
            if (owned->weapon.target == BONK_TARGET_CONE)
            {
                float const share = info->Id == owned->weapon.spellId ? 1.0f : owned->weapon.altDamage;
                FiringWeapon firing(m_player->GetBonkPlayer());
                for (Creature* mob : GetMobsInCone(*owned, owned->weapon.radius * area))
                    StrikeWith(*owned, info, mob, share);
            }
            else
                CastSelfArea(*owned, info, area);
            continue;
        }

        if (cast.field)
        {
            float const radius = GetWeaponRadius(*owned) * (1.0f + GetSpellUpgrade(*owned, BONK_SPELL_UPGRADE_TRAP_AREA) / 100.0f);
            float const damage = GetWeaponDamage(*owned) * cast.share;
            for (ObjectGuid const& guid : GetEnemyGuids())
                if (Creature* mob = map->GetCreature(guid))
                    if (mob->IsAlive() && mob->GetDistance2d(cast.x, cast.y) <= radius)
                        DealSpellHit(info, mob, damage);
            continue;
        }

        if (cast.pounce)
        {
            Creature* pet = map->GetCreature(cast.source);
            Creature* prey = map->GetCreature(cast.target);
            BonkPetFamily const* family = GetPetFamily(*owned);
            if (!pet || !pet->IsAlive() || !family)
                continue;
            m_petStates[cast.source].busyUntilMs = 0;
            if (prey && prey->IsAlive())
            {
                for (BonkPetAbility const& ability : family->abilities)
                    if (ability.kind == BONK_PET_ABILITY_POUNCE)
                    {
                        if (pet->CanReachWithMeleeAutoAttack(prey, BONK_POUNCE_GIVE))
                            PetStrike(*owned, pet, prey, ability);
                        break;
                    }
                pet->AI()->AttackStart(prey);
            }
            continue;
        }

        if (cast.petCleave)
        {
            Creature* pet = map->GetCreature(cast.source);
            float const damage = GetPetCleaveDamage(*owned);
            if (!pet || !pet->IsAlive() || damage <= 0.0f)
                continue;
            for (Creature* mob : GetMobsInRange(BONK_FRENZY_REACH * GetSummonReach(*owned), pet))
                if (mob->GetObjectGuid() != cast.target && pet->HasInArc(mob, BONK_FRENZY_ARC))
                    DealSpellHit(info, mob, damage, pet);
            continue;
        }

        if (cast.echo)
        {
            CastSelfArea(*owned, info, GetWeaponArea(*owned) * BONK_ECHO_AREA);
            continue;
        }

        if (cast.selfArea)
        {
            float scale = GetWeaponArea(*owned);
            if (info->Id != owned->weapon.spellId)
                if (float const own = GetSpellRadiusOf(info))
                    scale *= owned->weapon.radius * BONK_ECHO_AREA / own;
            CastSelfArea(*owned, info, scale);
            continue;
        }

        if (cast.ricochet)
        {
            Ricochet(*owned, info, cast);
            continue;
        }

        if (cast.trickShot)
        {
            FireTrickShot(*owned, info, cast);
            continue;
        }

        if (cast.detonate)
        {
            Detonate(*owned, info, cast);
            continue;
        }

        if (cast.flareSplit)
        {
            FireFlareSplit(*owned, info, cast);
            continue;
        }

        if (cast.shardBurst)
        {
            ShardBurst(*owned, info, cast);
            continue;
        }

        if (cast.venomBolts)
        {
            FireVenomBolts(*owned, info, cast);
            continue;
        }

        if (cast.keywordCloud)
        {
            StartKeywordCloud(*owned, cast.x, cast.y, cast.z);
            continue;
        }

        if (cast.bomblet)
        {
            Bomblet(*owned, info, cast);
            continue;
        }

        if (cast.shatter)
        {
            Shatter(*owned, info, cast);
            continue;
        }

        if (cast.wakeOfBlades || cast.aftershock || cast.chainExecution || cast.shieldThrow)
        {
            ResolveWarriorCast(*owned, info, cast);
            continue;
        }

        if (cast.blinkLanding || cast.pyroblastSplash || cast.iceShard || cast.frostShatter || cast.livingBomb || cast.meteor ||
            cast.phaseRush || cast.presenceOfMind)
        {
            ResolveMageCast(*owned, info, cast);
            continue;
        }

        if (cast.windfury || cast.staticDischarge || cast.maelstrom)
        {
            ResolveShamanCast(*owned, info, cast);
            continue;
        }

        if (cast.halo || cast.penanceLaunch || cast.penanceHit)
        {
            ResolvePriestCast(*owned, info, cast);
            continue;
        }

        if (cast.killingSpree || cast.fanOfKnives || cast.shadowyShuriken)
        {
            ResolveRogueCast(*owned, info, cast);
            continue;
        }

        if (cast.crusaderStrike)
        {
            LandCrusaderStrike(*owned, info, cast);
            continue;
        }

        if (cast.hammerOfWrath)
        {
            LandHammerOfWrath(*owned, info, cast);
            continue;
        }

        if (cast.justiceHammer)
        {
            LandJusticeHammer(*owned, info, cast);
            continue;
        }

        if (cast.lightsHammerLaunch)
        {
            LaunchLightsHammer(*owned, cast);
            continue;
        }

        if (cast.lightsHammer)
        {
            LandLightsHammer(*owned, info, cast);
            continue;
        }

        if (cast.strike)
        {
            Creature* target = map->GetCreature(cast.target);
            if (!target || !target->IsAlive())
            {
                std::vector<Creature*> const mobs = GetMobsInRange(GetStrikeRange(*owned));
                target = mobs.empty() ? nullptr : mobs.front();
            }
            if (target)
            {
                FiringWeapon firing(m_player->GetBonkPlayer());
                uint32 const critsBefore = m_crits;
                StrikeWith(*owned, info, target, cast.share, true);
                if (cast.arcaneEcho)
                    EchoMissile(*owned, info, target, cast.share);
                if (m_crits != critsBefore)
                    SplitCriticalMissile(*owned, info, target, cast.share);
            }
            continue;
        }

        if (cast.freezingWinds)
        {
            BlowFreezingWinds(cast);
            continue;
        }

        float x = cast.x, y = cast.y, z = cast.z;
        if (Creature* target = map->GetCreature(cast.target))
            if (target->IsAlive())
            {
                x = target->GetPositionX();
                y = target->GetPositionY();
                z = target->GetPositionZ();
            }

        FiringWeapon firing(m_player->GetBonkPlayer());
        if (info->EffectImplicitTargetA[EFFECT_INDEX_0] == TARGET_UNIT_ENEMY)
        {
            float const radius = std::max(BONK_CLEAVE_RADIUS, owned->weapon.groundRadius) * GetWeaponArea(*owned);
            for (ObjectGuid const& guid : GetEnemyGuids())
                if (Creature* mob = map->GetCreature(guid))
                    if (mob->IsAlive() && mob->GetDistance2d(x, y) <= radius)
                        m_player->CastSpell(mob, info, true);
        }
        else
        {
            optional<int32> basePoints[MAX_EFFECT_INDEX];
            GetBasePoints(*owned, info, owned->weapon.altDamage, basePoints);
            m_player->GetBonkPlayer()->TweakSpell(info->Id).areaScale = GetWeaponArea(*owned);
            CastAtGround(m_player, info, basePoints, x, y, z);
        }
    }
}

void BonkRun::LaunchOrb(OwnedWeapon& owned)
{
    BonkWeapon const& weapon = owned.weapon;
    float const angle = m_player->GetOrientation();
    float const x = m_player->GetPositionX();
    float const y = m_player->GetPositionY();
    float const z = m_player->GetPositionZ();
    uint32 const lifetime = std::max(1000u, weapon.durationMs);

    Creature* orb = m_player->SummonCreature(BONK_ORB_SMALL, x, y, z + 1.0f, angle, TEMPSUMMON_TIMED_DESPAWN, lifetime + 2000, false, 0, &InstallOrbAI);
    if (!orb)
        return;
    orb->SetDisplayId(BONK_DISPLAY_SPELL_ORB);
    orb->SetObjectScale(BONK_WEAPON_ORB_SCALE);
    orb->AddAura(BONK_SPELL_WEAPON_ORB);
    orb->SetSpeedRate(MOVE_RUN, BONK_WEAPON_ORB_SPEED);

    float const distance = BONK_WEAPON_ORB_SPEED * BONK_PLAYER_RUN_SPEED * lifetime / float(IN_MILLISECONDS);
    float dx = x + distance * std::cos(angle);
    float dy = y + distance * std::sin(angle);
    float dz = z;
    m_player->GetMap()->GetWalkHitPosition(nullptr, x, y, z, dx, dy, dz);
    orb->GetMotionMaster()->MovePoint(0, dx, dy, dz, MOVE_RUN_MODE);

    WeaponOrb launched;
    launched.creature = orb->GetObjectGuid();
    launched.weaponId = weapon.id;
    launched.leftMs = int32(lifetime);
    launched.tickMs = 0;
    m_weaponOrbs.push_back(launched);
}

void BonkRun::UpdateWeaponOrbs(uint32 diff)
{
    if (m_weaponOrbs.empty())
        return;

    Map* map = m_player->GetMap();
    for (auto itr = m_weaponOrbs.begin(); itr != m_weaponOrbs.end();)
    {
        itr->leftMs -= int32(diff);
        Creature* orb = map->GetCreature(itr->creature);
        OwnedWeapon* owned = FindOwned(itr->weaponId);
        if (!orb || !owned || itr->leftMs <= 0)
        {
            if (orb)
                orb->DespawnOrUnsummon();
            itr = m_weaponOrbs.erase(itr);
            continue;
        }

        itr->tickMs -= int32(diff);
        if (itr->tickMs <= 0)
        {
            BonkWeapon const& weapon = owned->weapon;
            itr->tickMs = int32(std::max(150.0f, std::max(200u, weapon.tickMs) / GetHasteRate()));
            if (SpellEntry const* info = sSpellMgr.GetSpellEntry(weapon.spellId))
            {
                BonkProjectileInfo const* projectile = sBonkMgr.GetProjectile(info->Id);
                uint32 const targets = GetWeaponTargets(*owned);
                uint32 struck = 0;
                FiringWeapon firing(m_player->GetBonkPlayer());
                for (Creature* mob : GetMobsInRange(weapon.range * GetWeaponArea(*owned), orb))
                {
                    if (struck >= targets)
                        break;
                    if (projectile)
                        LaunchProjectiles(*owned, *projectile, info, 1.0f, orb, mob);
                    else
                        StrikeWith(*owned, info, mob, 1.0f);
                    ++struck;
                }
            }
        }
        ++itr;
    }
}

void BonkRun::DespawnWeaponOrbs()
{
    if (Map* map = m_player->FindMap())
        for (WeaponOrb const& orb : m_weaponOrbs)
            if (Creature* creature = map->GetCreature(orb.creature))
                creature->DespawnOrUnsummon();
    m_weaponOrbs.clear();
}

float BonkRun::GetLeadAngle(WorldObject const* from, Unit const* target, float speed) const
{
    float x = target->GetPositionX();
    float y = target->GetPositionY();
    if (speed > 0.0f && target->IsMoving())
    {
        float const run = target->GetSpeed(target->IsWalking() ? MOVE_WALK : MOVE_RUN) * from->GetDistance2d(target) / speed;
        x += run * std::cos(target->GetOrientation());
        y += run * std::sin(target->GetOrientation());
    }
    return from->GetAngle(x, y);
}

void BonkRun::LaunchProjectiles(OwnedWeapon& owned, BonkProjectileInfo const& info, SpellEntry const* spellInfo, float damageShare,
    WorldObject const* from, Creature* target)
{
    BonkWeapon const& weapon = owned.weapon;
    float const aim = GetLeadAngle(from, target, info.speed);

    if (weapon.chains)
    {
        uint32 chain = 1;
        for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
            chain = std::max(chain, spellInfo->EffectChainTarget[i]);
        uint32 const count = chain;
        for (uint32 i = 0; i < count; ++i)
            LaunchProjectile(owned, info, spellInfo->Id, damageShare, from, aim + (float(i) - (count - 1) / 2.0f) * BONK_PROJECTILE_FAN,
                weapon.baseJumps);
        return;
    }

    uint32 const pierce = weapon.behavior == BONK_BEHAVIOR_SPREAD ? 0 : weapon.baseJumps;
    LaunchProjectile(owned, info, spellInfo->Id, damageShare, from, aim, pierce);
}

void BonkRun::LaunchProjectile(OwnedWeapon& owned, BonkProjectileInfo const& info, uint32 spellId, float damageShare,
    WorldObject const* from, float angle, uint32 pierce)
{
    ++m_projectilesUnreported;

    float const x = from->GetPositionX();
    float const y = from->GetPositionY();
    float const z = from->GetPositionZ() + (from == m_player ? BONK_PROJECTILE_HEIGHT : 0.0f);
    float const length = owned.weapon.range;
    float ex = x + length * std::cos(angle);
    float ey = y + length * std::sin(angle);
    float ez = z;
    m_player->GetMap()->GetLosHitPosition(x, y, z, ex, ey, ez, -0.5f);
    float const flight = std::sqrt((ex - x) * (ex - x) + (ey - y) * (ey - y));
    if (flight < 1.0f)
        return;

    uint32 const flightMs = uint32(flight / info.speed * IN_MILLISECONDS);
    Projectile projectile;
    projectile.flightId = NextBonkFlightId();
    SendBonkFlight(m_player, projectile.flightId, x, y, z, ex, ey, ez, flightMs, info.displayId, info.scale);
    projectile.weaponId = owned.weapon.id;
    projectile.spellId = spellId;
    projectile.damageShare = damageShare;
    projectile.x = x;
    projectile.y = y;
    projectile.z = z;
    projectile.dirX = std::cos(angle);
    projectile.dirY = std::sin(angle);
    projectile.speed = info.speed;
    projectile.length = flight;
    projectile.radius = info.radius;
    projectile.pierce = pierce;
    m_projectiles.push_back(projectile);
}

void BonkRun::UpdateProjectiles(uint32 diff)
{
    if (m_projectiles.empty())
        return;

    Map* map = m_player->GetMap();
    for (auto itr = m_projectiles.begin(); itr != m_projectiles.end();)
    {
        Projectile& projectile = *itr;
        OwnedWeapon* owned = FindOwned(projectile.weaponId);
        SpellEntry const* spellInfo = sSpellMgr.GetSpellEntry(projectile.spellId);
        bool done = !owned || !spellInfo;

        if (!done)
        {
            projectile.flownMs += diff;
            float const reached = std::min(projectile.length, projectile.speed * projectile.flownMs / IN_MILLISECONDS);

            std::vector<std::pair<float, Creature*>> touched;
            for (ObjectGuid const& guid : GetEnemyGuids())
            {
                Creature* mob = map->GetCreature(guid);
                if (!mob || !mob->IsAlive() || std::find(projectile.struck.begin(), projectile.struck.end(), guid) != projectile.struck.end())
                    continue;
                float const dx = mob->GetPositionX() - projectile.x;
                float const dy = mob->GetPositionY() - projectile.y;
                float const reach = projectile.radius + mob->GetObjectBoundingRadius();
                float const along = dx * projectile.dirX + dy * projectile.dirY;
                if (along < projectile.checked - reach || along > reached + reach)
                    continue;
                if (std::fabs(dx * projectile.dirY - dy * projectile.dirX) > reach ||
                    std::fabs(mob->GetPositionZ() + BONK_PROJECTILE_HEIGHT - projectile.z) > BONK_PROJECTILE_Z_REACH)
                    continue;
                touched.emplace_back(along, mob);
            }
            std::sort(touched.begin(), touched.end(), [](auto const& a, auto const& b) { return a.first < b.first; });
            projectile.checked = reached;

            FiringWeapon firing(m_player->GetBonkPlayer());
            for (auto const& hit : touched)
            {
                projectile.struck.push_back(hit.second->GetObjectGuid());
                owned->killed = false;
                float const along = hit.first;
                Creature* source = TakeShotSource(projectile.x + along * projectile.dirX, projectile.y + along * projectile.dirY,
                    projectile.z - BONK_PROJECTILE_HEIGHT);
                StrikeWith(*owned, spellInfo, hit.second, projectile.damageShare, false, source);
                if (owned->killed && owned->weapon.HasFlag(BONK_WEAPON_FLAG_RESET_ON_KILL))
                    owned->timer = std::min<int32>(owned->timer, BONK_CLEARCAST_DELAY_MS);
                if (!projectile.pierce)
                {
                    done = true;
                    break;
                }
                --projectile.pierce;
            }
            if (reached >= projectile.length)
                done = true;
        }

        if (!done)
        {
            ++itr;
            continue;
        }
        SendBonkLanding(m_player, projectile.flightId);
        itr = m_projectiles.erase(itr);
    }
}

void BonkRun::DespawnProjectiles()
{
    for (Projectile const& projectile : m_projectiles)
        SendBonkLanding(m_player, projectile.flightId);
    for (EnemyMissile const& missile : m_enemyMissiles)
        if (missile.flightId)
            SendBonkLanding(m_player, missile.flightId);
    m_projectiles.clear();
    m_enemyMissiles.clear();
}

void BonkRun::LaunchEnemyMissile(ObjectGuid caster, ObjectGuid target, uint32 spellId, uint32 delayMs, uint32 flightMs, float x, float y, float z)
{
    if (flightMs < BONK_ENEMY_MISSILE_MIN_MS || !sBonkMgr.GetBoltLook(spellId))
        return;

    EnemyMissile missile;
    missile.caster = caster;
    missile.target = target;
    missile.spellId = spellId;
    missile.delayMs = delayMs;
    missile.leftMs = flightMs;
    missile.x = x;
    missile.y = y;
    missile.z = z + BONK_ENEMY_MISSILE_AIM;
    m_enemyMissiles.push_back(missile);
}

void BonkRun::UpdateEnemyMissiles(uint32 diff)
{
    for (auto itr = m_enemyMissiles.begin(); itr != m_enemyMissiles.end();)
    {
        EnemyMissile& missile = *itr;
        if (!missile.launched)
        {
            if (missile.delayMs > diff)
                missile.delayMs -= diff;
            else
            {
                missile.launched = true;
                FlyEnemyMissile(missile);
            }
            ++itr;
            continue;
        }
        if (missile.leftMs > diff)
        {
            missile.leftMs -= diff;
            ++itr;
            continue;
        }
        LandEnemyMissile(missile);
        itr = m_enemyMissiles.erase(itr);
    }
}

void BonkRun::DropEnemyMissilesFrom(ObjectGuid const& caster)
{
    Map* map = m_player->FindMap();
    m_enemyMissiles.erase(std::remove_if(m_enemyMissiles.begin(), m_enemyMissiles.end(), [&](EnemyMissile const& missile)
    {
        if (missile.caster != caster)
            return false;
        if (missile.flightId)
            SendBonkLanding(m_player, missile.flightId);
        return true;
    }), m_enemyMissiles.end());
}

void BonkRun::FlyEnemyMissile(EnemyMissile& missile)
{
    Creature* caster = m_player->GetMap()->GetCreature(missile.caster);
    BonkBoltLook const* look = sBonkMgr.GetBoltLook(missile.spellId);
    if (!caster || !caster->IsAlive() || !look)
        return;

    float const angle = std::atan2(missile.y - caster->GetPositionY(), missile.x - caster->GetPositionX());
    float const x = caster->GetPositionX() + caster->GetObjectBoundingRadius() * std::cos(angle);
    float const y = caster->GetPositionY() + caster->GetObjectBoundingRadius() * std::sin(angle);
    float const z = caster->GetPositionZ() + BONK_ENEMY_MISSILE_HEIGHT;
    float const dx = missile.x - x, dy = missile.y - y, dz = missile.z - z;
    if (dx * dx + dy * dy + dz * dz < 1.0f)
        return;

    missile.flightId = NextBonkFlightId();
    SendBonkFlight(m_player, missile.flightId, x, y, z, missile.x, missile.y, missile.z, missile.leftMs, look->displayId, look->scale);
}

void BonkRun::LandEnemyMissile(EnemyMissile const& missile)
{
    if (!missile.flightId)
        return;
    BonkBoltLook const* look = sBonkMgr.GetBoltLook(missile.spellId);
    if (look && look->impactKit)
        SendBonkKitAt(m_player, look->impactKit, missile.x, missile.y, missile.z - BONK_ENEMY_MISSILE_AIM, 0.0f, BONK_ERUPTION_LOOK_MS);
}

float BonkRun::GetMobDamageMultiplier(Creature const* mob) const
{
    float reduction = 0.0f;
    for (OwnedWeapon const& owned : m_weapons)
        if (owned.weapon.behavior == BONK_BEHAVIOR_DEMORALIZE && mob->HasAura(owned.weapon.spellId))
            reduction = std::max(reduction, owned.weapon.behaviorValue * (1.0f + owned.damageBonus / 100.0f));
        else if (owned.weapon.behavior == BONK_BEHAVIOR_DEMORALIZING_SHOUT && mob->HasAura(owned.weapon.spellId))
            reduction = std::max(reduction, BONK_DEMORALIZING_SHOUT_REDUCTION);
    reduction = std::max(reduction, GetDruidRoarReduction(mob));
    return 1.0f - std::min(reduction, BONK_MAX_DEMORALIZE) / 100.0f;
}

void BonkRun::AddTelegraph(BonkTelegraphShape shape, float x, float y, float z, float orientation, float size, uint32 durationMs,
    std::function<void()> onFire, bool lingers)
{
    if (m_state != STATE_RUNNING || m_paused)
        return;
    m_telegraphs.Add(m_player, shape, x, y, z, orientation, size, durationMs, std::move(onFire), lingers);
}

void BonkRun::AddEnemyTelegraph(Creature const* caster, BonkTelegraphShape shape, float x, float y, float z, float orientation, float size,
    uint32 durationMs, std::function<void()> onFire, uint32 windId)
{
    if (m_state != STATE_RUNNING || m_paused)
        return;
    m_telegraphs.Add(m_player, shape, x, y, z, orientation, size, durationMs, std::move(onFire), false, caster->GetObjectGuid(), windId);
}

void BonkRun::CancelEnemyWindup(Creature const* caster, uint32 windId)
{
    if (windId)
        m_telegraphs.CancelFrom(m_player, caster->GetObjectGuid(), windId);
}

void BonkRun::ForgetEnemyAttacks(Creature const* caster)
{
    m_telegraphs.CancelFrom(m_player, caster->GetObjectGuid());
    DropEnemyMissilesFrom(caster->GetObjectGuid());
}

void BonkRun::ShowImpact(BonkEnemyHit const& hit, float x, float y, float z, float radius)
{
    uint32 const kit = hit.impactKit ? hit.impactKit : BONK_KIT_FIRE_BURST;
    if (radius > 0.0f)
        ShowAreaImpact(kit, x, y, z, radius);
    else
        ShowKitAt(kit, x, y, z);
}

void BonkRun::ShowAreaImpact(uint32 kit, float x, float y, float z, float radius)
{
    ShowKitAt(kit, x, y, z, std::min(BONK_IMPACT_MAX_STRETCH, std::max(1.0f, radius / BONK_IMPACT_KIT_RADIUS)));
    if (radius < BONK_IMPACT_RING_FROM)
        return;

    Map* map = m_player->GetMap();
    float const ring = radius * BONK_IMPACT_RING_SHARE;
    uint32 const count = std::max(BONK_IMPACT_RING_MIN, std::min(BONK_IMPACT_RING_MAX, uint32(2.0f * M_PI_F * ring / BONK_IMPACT_RING_SPACING)));
    float const turn = frand(0.0f, 2.0f * M_PI_F);
    for (uint32 i = 0; i < count; ++i)
    {
        float const angle = turn + 2.0f * M_PI_F * float(i) / float(count);
        float const px = x + ring * std::cos(angle), py = y + ring * std::sin(angle);
        float pz = map->GetHeight(px, py, z + 5.0f, true, 15.0f);
        if (pz <= INVALID_HEIGHT)
            pz = z;
        ShowKitAt(kit, px, py, pz);
    }
}

bool BonkRun::ShowKitOnStandIn(uint32 kit, float x, float y, float z, uint32 holdMs)
{
    SendBonkKitAt(m_player, kit, x, y, z, 0.0f, holdMs);
    return true;
}

void BonkRun::ShowKitAt(uint32 kit, float x, float y, float z, float)
{
    SendBonkKitAt(m_player, kit, x, y, z, 0.0f, BONK_ERUPTION_LOOK_MS);
}

void BonkRun::ShowLineImpacts(uint32 kit, float x, float y, float z, float orientation, float length)
{
    Map* map = m_player->GetMap();
    for (float along = BONK_LINE_IMPACT_SPACING / 2.0f; along < length; along += BONK_LINE_IMPACT_SPACING)
    {
        float const px = x + along * std::cos(orientation), py = y + along * std::sin(orientation);
        float pz = map->GetHeight(px, py, z + 5.0f, true, 15.0f);
        if (pz <= INVALID_HEIGHT)
            pz = z;
        ShowKitAt(kit, px, py, pz);
    }
}

void BonkRun::ShowAttackLook(BonkEnemyHit const& hit, BonkTelegraphShape shape, float x, float y, float z, float orientation, float size)
{
    if (hit.castKit)
        if (Creature* looker = m_player->GetMap()->GetCreature(hit.looker))
            if (looker->IsAlive())
            {
                if (hit.facesShape)
                    looker->SetFacingTo(orientation);
                looker->SendPlaySpellVisualKit(hit.castKit);
            }
    if (shape == BONK_TELEGRAPH_LINE && hit.impactKit)
        ShowLineImpacts(hit.impactKit, x, y, z, orientation, size);
}

Creature* BonkRun::SummonHazard(float x, float y, float z)
{
    return TakePooledMarker(m_hazardSources, x, y, z, BONK_HAZARD_MS, &InstallOrbAI);
}

Creature* BonkRun::TakePooledMarker(std::vector<PooledMarker>& pool, float x, float y, float z, uint32 busyMs, void (*install)(Creature*))
{
    Map* map = m_player->GetMap();
    for (PooledMarker& marker : pool)
    {
        if (marker.freeAtMs > m_totalMs)
            continue;
        Creature* creature = map->GetCreature(marker.guid);
        if (!creature || !creature->IsAlive())
        {
            creature = m_player->SummonCreature(BONK_PICKUP_MARKER, x, y, z, 0.0f, TEMPSUMMON_TIMED_DESPAWN, BONK_SHOT_SOURCE_LIFETIME_MS, false, 0,
                install);
            if (!creature)
                return nullptr;
            marker.guid = creature->GetObjectGuid();
        }
        else
            creature->NearTeleportTo(x, y, z, creature->GetOrientation());
        marker.freeAtMs = m_totalMs + busyMs;
        return creature;
    }
    if (pool.size() >= BONK_MARKER_POOL_MAX)
        return m_player->SummonCreature(BONK_PICKUP_MARKER, x, y, z, 0.0f, TEMPSUMMON_TIMED_DESPAWN, busyMs, false, 0, install);
    Creature* creature = m_player->SummonCreature(BONK_PICKUP_MARKER, x, y, z, 0.0f, TEMPSUMMON_TIMED_DESPAWN, BONK_SHOT_SOURCE_LIFETIME_MS, false, 0,
        install);
    if (creature)
        pool.push_back({ creature->GetObjectGuid(), m_totalMs + busyMs });
    return creature;
}

void BonkRun::FreePooledMarker(std::vector<PooledMarker>& pool, ObjectGuid const& guid, uint32 afterMs)
{
    for (PooledMarker& marker : pool)
        if (marker.guid == guid)
        {
            marker.freeAtMs = std::min(marker.freeAtMs, m_totalMs + afterMs);
            return;
        }
    if (Creature* creature = guid.IsEmpty() ? nullptr : m_player->GetMap()->GetCreature(guid))
        creature->DespawnOrUnsummon(afterMs);
}

void BonkRun::DespawnPooledMarkers(std::vector<PooledMarker>& pool)
{
    if (Map* map = m_player->FindMap())
        for (PooledMarker const& marker : pool)
            if (Creature* creature = map->GetCreature(marker.guid))
                creature->DespawnOrUnsummon();
    pool.clear();
}

void BonkRun::Erupt(BonkEnemyHit const& hit, float x, float y, float z, float radius, bool burst, bool showImpact)
{
    if (m_state != STATE_RUNNING)
        return;
    if (showImpact)
        ShowImpact(hit, x, y, z, burst ? radius : 0.0f);

    if (!hit.damage || !m_player->IsAlive() || !IsHeroWithin(m_player, x, y, radius))
        return;
    uint32 const hitSpell = hit.impactKit ? BONK_SPELL_PLAIN_HIT : BONK_SPELL_ERUPTION_HIT;
    Creature* mob = m_player->GetMap()->GetCreature(hit.caster);
    if (mob && mob->IsAlive())
        mob->CastCustomSpell(m_player, hitSpell, int32(hit.damage), {}, {}, true);
    else
        StrikeHero(hit, hitSpell, SummonHazard(x, y, z));
}

void BonkRun::StrikeHero(BonkEnemyHit const& hit, uint32 spellId, Unit* source)
{
    SpellEntry const* spellInfo = sSpellMgr.GetSpellEntry(spellId);
    if (m_state != STATE_RUNNING || !hit.damage || !spellInfo || !source || !source->IsInWorld() || !m_player->IsAlive() ||
        m_player->GetBonkPlayer()->IsDashInvulnerable())
        return;

    uint32 const raw = std::max(1u, uint32(hit.damage * hit.scale));
    NoteStruck(hit.striker, hit.caster);
    SpellNonMeleeDamage strike(source, m_player, spellInfo->Id, SpellSchools(spellInfo->School));
    strike.damage = MitigateDamage(raw);
    strike.absorb = raw - strike.damage;
    source->DealDamageMods(m_player, strike.damage, &strike.absorb);
    source->SendSpellNonMeleeDamageLog(&strike);
    source->DealSpellDamage(&strike, false);
    if (Creature* striker = m_player->GetMap()->GetCreature(hit.looker))
        if (BonkMobAI* ai = dynamic_cast<BonkMobAI*>(striker->AI()))
            ai->OnHitHero(this);
}

void BonkRun::LandWarning(BonkEnemyHit const& hit, BonkTelegraphShape shape, float x, float y, float z, float orientation, float size)
{
    if (m_state != STATE_RUNNING)
        return;

    if (Creature* caster = m_player->GetMap()->GetCreature(hit.caster))
        if (BonkMobAI* ai = dynamic_cast<BonkMobAI*>(caster->AI()))
        {
            if (caster->IsAlive())
            {
                ai->LandWindup(hit.windId);
                return;
            }
            if (!ai->IsWindingUp(hit.windId))
                return;
        }

    ShowAttackLook(hit, shape, x, y, z, orientation, size);
    if (!hit.damage || !m_player->IsAlive() || m_player->GetBonkPlayer()->IsDashInvulnerable())
        return;
    float heroX, heroY, heroZ;
    GetHeroPositionNow(m_player, heroX, heroY, heroZ);
    if (!IsInBonkTelegraph(shape, x, y, orientation, size, heroX, heroY))
        return;

    if (shape != BONK_TELEGRAPH_CIRCLE && hit.impactKit)
        ShowKitAt(hit.impactKit, heroX, heroY, heroZ);
    if (shape == BONK_TELEGRAPH_CIRCLE)
        ShowImpact(hit, x, y, z, size);
    StrikeHero(hit, hit.spellId, SummonHazard(x, y, z));
    if (shape == BONK_TELEGRAPH_LINE && m_player->IsAlive())
        BonkMobAI::KnockBackHero(m_player, orientation);
}

std::vector<std::string> BonkRun::DescribeMob(Creature const* mob) const
{
    bool const tracked = std::find(m_mobs.begin(), m_mobs.end(), mob->GetObjectGuid()) != m_mobs.end();
    std::vector<Creature*> const around = GetMobsInRange(BONK_MOBINFO_STRIKE_RANGE);
    bool const struck = std::find(around.begin(), around.end(), mob) != around.end();
    auto yesNo = [](bool value) { return std::string(value ? "yes" : "no"); };
    return {
        "Run: tracked as a run enemy: " + yesNo(tracked) + ", hit by a " + std::to_string(int32(BONK_MOBINFO_STRIKE_RANGE)) +
            "-yard strike around you: " + yesNo(struck) +
            ", mini-event target: " + yesNo(IsMiniEventTarget(mob->GetObjectGuid())),
        "Run: stage running: " + yesNo(m_state == STATE_RUNNING) + ", paused: " + yesNo(m_paused) + ", enemies tracked: " +
            std::to_string(m_mobs.size()) + ", telegraphs up: " + std::to_string(m_telegraphs.Count())
    };
}

bool BonkRun::ReserveBolt(uint32 landMs)
{
    m_boltLandings.erase(std::remove_if(m_boltLandings.begin(), m_boltLandings.end(), [this](uint32 at) { return at <= m_totalMs; }),
        m_boltLandings.end());
    if (m_boltLandings.size() >= BONK_MAX_BOLTS)
        return false;
    m_boltLandings.push_back(m_totalMs + landMs);
    return true;
}

ObjectGuid BonkRun::PlaceBoltTarget(float x, float y, float z, uint32 lifetimeMs)
{
    Creature* target = TakePooledMarker(m_boltTargets, x, y, z, lifetimeMs, &InstallBoltTargetAI);
    if (!target)
        return ObjectGuid();

    target->SetFactionTemplateId(m_player->GetFactionTemplateId());
    target->SetMaxHealth(1000000);
    target->SetHealth(1000000);
    return target->GetObjectGuid();
}

void BonkRun::LandBolt(BonkEnemyHit const& hit, ObjectGuid targetGuid, float x, float y, float radius)
{
    Map* map = m_player->GetMap();
    Creature* target = map->GetCreature(targetGuid);
    if (target)
        FreePooledMarker(m_boltTargets, targetGuid, BONK_BOLT_TARGET_FREE_MS);
    if (hit.impactKit && m_state == STATE_RUNNING)
        ShowKitAt(hit.impactKit, x, y, target ? target->GetPositionZ() : m_player->GetPositionZ());

    SpellEntry const* spellInfo = sSpellMgr.GetSpellEntry(hit.spellId);
    if (m_state != STATE_RUNNING || !spellInfo || !m_player->IsAlive() || !IsHeroWithin(m_player, x, y, radius))
        return;

    Creature* mob = map->GetCreature(hit.caster);
    if (!mob || !mob->IsAlive())
    {
        StrikeHero(hit, hit.spellId, SummonHazard(x, y, target ? target->GetPositionZ() : m_player->GetPositionZ()));
        return;
    }

    SpellNonMeleeDamage strike(mob, m_player, hit.spellId, SpellSchools(spellInfo->School));
    strike.damage = std::max(1u, hit.damage);
    mob->DealDamageMods(m_player, strike.damage, &strike.absorb);
    mob->SendSpellNonMeleeDamageLog(&strike);
    mob->DealSpellDamage(&strike, false);
}

void BonkRun::HitAgain(SpellEntry const* spellInfo, Unit* target, float damage, SpellEffectIndex effectIndex, WeaponAttackType attackType)
{
    if (m_state != STATE_RUNNING || !spellInfo || !target || !target->IsAlive() || damage <= 0.0f || !m_player->IsAlive())
        return;

    OwnedWeapon const* owned = nullptr;
    for (OwnedWeapon const& itr : m_weapons)
        if (itr.weapon.spellId == spellInfo->Id || itr.weapon.altSpellId == spellInfo->Id)
        {
            owned = &itr;
            break;
        }
    if (!owned)
        return;
    float const chance = GetSpellUpgrade(*owned, BONK_SPELL_UPGRADE_HIT_TWICE);
    if (chance <= 0.0f || !roll_chance_f(chance))
        return;

    SpellNonMeleeDamage hit(m_player, target, spellInfo->Id, SpellSchools(spellInfo->School));
    bool const crit = spellInfo->CanCrit() && roll_chance_f(GetCritChance(spellInfo, target));
    m_player->CalculateSpellDamage(&hit, damage, spellInfo, effectIndex, attackType, nullptr, crit);
    target->CalculateAbsorbResistBlock(m_player, &hit, spellInfo, BASE_ATTACK, nullptr);
    m_player->DealDamageMods(target, hit.damage, &hit.absorb);
    m_player->SendSpellNonMeleeDamageLog(&hit);
    m_player->DealSpellDamage(&hit, true);
}

void BonkRun::OnHeroDodge(Unit* attacker)
{
    if (m_state != STATE_RUNNING)
        return;
    ++m_dodgesUnreported;
    OnRogueDodge(attacker);
}

void BonkRun::OnHeroDamaged()
{
    if (m_state == STATE_RUNNING)
        TryPaladinLastStands();
}

void BonkRun::OnHeroStruck(Creature* attacker)
{
    m_lastStruckMs = m_totalMs;
    NoteStruck(DescribeStriker(attacker), attacker->GetObjectGuid());
    OnShamanStruck(attacker);
    OnPriestStruck(attacker);
    OnPaladinStruck(attacker);
    OnDruidStruck(attacker);
}

std::string BonkRun::DescribeStriker(Creature const* attacker)
{
    BonkMobAI const* ai = dynamic_cast<BonkMobAI const*>(attacker->AI());
    if (!ai)
        return attacker->GetName();
    static char const* const kinds[MAX_BONK_SPAWN_KIND] = { "", "elite", "boss", "mini-boss" };
    std::string const what = ai->GetKind() == BONK_SPAWN_NORMAL ? sBonkMgr.GetArchetype(ai->GetArchetype()).name : kinds[ai->GetKind()];
    return std::string(attacker->GetName()) + " (" + what + ")";
}

void BonkRun::NoteStruck(std::string const& striker, ObjectGuid strikerGuid)
{
    m_lastStriker = striker;
    m_lastStrikerGuid = strikerGuid;
    if (m_state != STATE_RUNNING || IsPeriodicHit())
        return;
    m_struck = true;
    m_struckAtMs = m_totalMs;
}

void BonkRun::OnPeriodicDamage(uint32 spellId, uint32 damage)
{
    if (m_state != STATE_RUNNING)
        return;
    for (OwnedWeapon const& owned : m_weapons)
        if (owned.weapon.behavior == BONK_BEHAVIOR_LEECH && owned.weapon.spellId == spellId)
        {
            m_bleedHeal += damage * owned.weapon.behaviorValue / 100.0f;
            m_bleedHealSource = WeaponSupport(owned);
        }
    OnPriestPeriodic(spellId, damage);
}

void BonkRun::DropTraps(OwnedWeapon& owned, bool armed)
{
    BonkWeapon const& weapon = owned.weapon;
    uint32 const count = 1 + GetSpellCount(owned);
    int32 const lifetime = int32(BONK_TRAP_LIFETIME * GetWeaponCooldown(owned));
    TrapLook const* look = GetTrapLook(uint32(weapon.behaviorValue));
    Map* map = m_player->GetMap();

    float cx = m_player->GetPositionX();
    float cy = m_player->GetPositionY();
    float cz = m_player->GetPositionZ();
    float const ground = map->GetHeight(cx, cy, cz + 0.5f, true, BONK_TRAP_GROUND_SEARCH);
    if (ground > INVALID_HEIGHT)
        cz = ground;
    float const deploy = GetSpellUpgrade(owned, BONK_SPELL_UPGRADE_DEPLOY);
    if (deploy > 0.0f)
    {
        std::vector<Creature*> const nearby = GetMobsInRange(BONK_TRAP_AWARE_RANGE);
        if (!nearby.empty())
        {
            float const reach = std::min(deploy, m_player->GetDistance2d(nearby.front()));
            float const angle = m_player->GetAngle(nearby.front());
            float dx = cx + reach * std::cos(angle);
            float dy = cy + reach * std::sin(angle);
            float dz = cz;
            map->GetWalkHitPosition(nullptr, cx, cy, cz, dx, dy, dz);
            cx = dx;
            cy = dy;
            cz = dz;
        }
    }

    float const startAngle = frand(0.0f, 2.0f * M_PI_F);
    for (uint32 i = 0; i < count; ++i)
    {
        float x = cx;
        float y = cy;
        float z = cz;
        if (i)
        {
            float const angle = startAngle + 2.0f * M_PI_F * i / count;
            float dx = x + BONK_TRAP_SPACING * std::cos(angle);
            float dy = y + BONK_TRAP_SPACING * std::sin(angle);
            float dz = z;
            map->GetWalkHitPosition(nullptr, x, y, z, dx, dy, dz);
            x = dx;
            y = dy;
            z = dz;
        }

        size_t out = 0;
        for (Trap const& trap : m_traps)
            if (trap.weaponId == weapon.id)
                ++out;
        if (out >= BONK_MAX_TRAPS)
            for (auto itr = m_traps.begin(); itr != m_traps.end(); ++itr)
                if (itr->weaponId == weapon.id)
                {
                    PutAwayTrap(*itr);
                    m_traps.erase(itr);
                    break;
                }

        Trap trap;
        trap.weaponId = weapon.id;
        trap.x = x;
        trap.y = y;
        trap.z = z;
        trap.armMs = armed ? 0 : BONK_TRAP_ARM_MS;
        trap.leftMs = lifetime;
        if (look)
            trap.prop = PlaceTrapProp(look->set, x, y, z);
        else if (GameObject* go = PlaceObject(uint32(weapon.behaviorValue), x, y, z, lifetime / IN_MILLISECONDS + 5))
            trap.object = go->GetObjectGuid();
        m_traps.push_back(trap);
    }
}

uint32 BonkRun::PlaceTrapProp(uint32 display, float x, float y, float z)
{
    uint32 const key = NextBonkAuraKey();
    SendBonkProp(m_player, key, display, x, y, z, 0.0f);
    return key;
}

void BonkRun::PutAwayTrap(Trap const& trap)
{
    if (trap.prop)
        SendBonkPropGone(m_player, trap.prop);
    else if (Map* map = m_player->FindMap())
        if (GameObject* go = map->GetGameObject(trap.object))
            go->Delete();
}

void BonkRun::ShowTrapSprung(OwnedWeapon const& owned, uint32 prop, float x, float y, float z)
{
    TrapLook const* look = GetTrapLook(uint32(owned.weapon.behaviorValue));
    SendBonkPropGone(m_player, prop);
    if (!look)
        return;
    PendingCast release;
    release.weaponId = owned.weapon.id;
    release.spellId = owned.weapon.spellId;
    release.releaseLook = true;
    release.prop = PlaceTrapProp(look->sprung, x, y, z);
    release.leftMs = int32(look->sprungMs);
    m_pendingCasts.push_back(release);
}

void BonkRun::UpdateTraps(uint32 diff)
{
    m_trapTimer += diff;
    if (m_trapTimer < BONK_TRAP_CHECK_MS || m_traps.empty())
        return;
    int32 const elapsed = int32(m_trapTimer);
    m_trapTimer = 0;

    Map* map = m_player->GetMap();
    std::vector<Trap> sprung;
    for (auto itr = m_traps.begin(); itr != m_traps.end();)
    {
        itr->armMs -= elapsed;
        itr->leftMs -= elapsed;
        bool remove = itr->leftMs <= 0 || !FindOwned(itr->weaponId);
        bool springs = false;
        if (!remove && itr->armMs <= 0)
            for (ObjectGuid const& guid : GetEnemyGuids())
                if (Creature* mob = map->GetCreature(guid))
                    if (mob->IsAlive() && mob->GetDistance2d(itr->x, itr->y) <= BONK_TRAP_TRIGGER_RADIUS)
                    {
                        springs = remove = true;
                        break;
                    }
        if (!remove)
        {
            ++itr;
            continue;
        }
        if (springs)
            sprung.push_back(*itr);
        if (!springs || !itr->prop)
            PutAwayTrap(*itr);
        itr = m_traps.erase(itr);
    }

    for (Trap const& trap : sprung)
    {
        OwnedWeapon* owned = FindOwned(trap.weaponId);
        if (!owned)
        {
            PutAwayTrap(trap);
            continue;
        }
        if (trap.prop)
            ShowTrapSprung(*owned, trap.prop, trap.x, trap.y, trap.z);
        SpringTrap(*owned, trap.x, trap.y, trap.z);
    }
}

void BonkRun::SpringTrap(OwnedWeapon& owned, float x, float y, float z)
{
    BonkWeapon const& weapon = owned.weapon;
    SpellEntry const* spellInfo = sSpellMgr.GetSpellEntry(weapon.spellId);
    if (!spellInfo || !m_player->IsAlive())
        return;

    if (weapon.HasFlag(BONK_WEAPON_FLAG_SNAKES))
    {
        ReleaseSnakes(owned, x, y, z);
        return;
    }

    FiringWeapon firing(m_player->GetBonkPlayer());
    float const radius = GetWeaponRadius(owned) * (1.0f + GetSpellUpgrade(owned, BONK_SPELL_UPGRADE_TRAP_AREA) / 100.0f);
    ScaleRadius(spellInfo, radius);
    optional<int32> basePoints[MAX_EFFECT_INDEX];
    GetBasePoints(owned, spellInfo, 1.0f, basePoints);
    CastAtGround(m_player, spellInfo, basePoints, x, y, z);
    if (spellInfo->Id == BONK_SPELL_FROST_TRAP_AURA)
        PlayKitAt(ObjectGuid(), x, y, z, BONK_KIT_FROST_TRAP_SOUND, true);
    SpringEntrapment(owned, x, y, radius);
    if (m_mastery.Special(BONK_SPECIAL_FREEZING_WINDS) > 0.0f && GetRootWeapon(weapon) == BONK_WEAPON_BLIZZARD)
    {
        PendingCast winds;
        winds.weaponId = weapon.id;
        winds.spellId = weapon.spellId;
        winds.leftMs = int32(weapon.durationMs * GetWeaponDurationScale(owned));
        winds.freezingWinds = true;
        winds.x = x;
        winds.y = y;
        winds.z = z;
        m_pendingCasts.push_back(winds);
    }
    if (weapon.HasFlag(BONK_WEAPON_FLAG_CLUSTER_BOMBS))
        ScatterBomblets(owned, spellInfo, x, y, z);
    if (weapon.HasFlag(BONK_WEAPON_FLAG_SHATTER))
        QueueShatter(owned, x, y, z);

    if (weapon.damageEffectIndex < 0 && !weapon.weaponDamage && weapon.tickMs && weapon.durationMs)
    {
        uint32 const length = uint32(weapon.durationMs * GetWeaponDurationScale(owned));
        uint32 const every = std::max(200u, uint32(weapon.tickMs / GetWeaponHasteRate(owned)));
        for (uint32 at = 0; at < length; at += every)
        {
            PendingCast tick;
            tick.weaponId = weapon.id;
            tick.spellId = weapon.spellId;
            tick.leftMs = int32(at);
            tick.field = true;
            tick.x = x;
            tick.y = y;
            tick.z = z;
            m_pendingCasts.push_back(tick);
        }
    }

    SpellEntry const* altInfo = weapon.altSpellId ? sSpellMgr.GetSpellEntry(weapon.altSpellId) : nullptr;
    if (!altInfo)
        return;
    if (altInfo->EffectImplicitTargetA[EFFECT_INDEX_0] == TARGET_UNIT_ENEMY)
    {
        Map* map = m_player->GetMap();
        for (ObjectGuid const& guid : GetEnemyGuids())
            if (Creature* mob = map->GetCreature(guid))
                if (mob->IsAlive() && mob->GetDistance2d(x, y) <= radius)
                    m_player->CastSpell(mob, altInfo, true);
        return;
    }
    ScaleRadius(altInfo, radius);
    optional<int32> altPoints[MAX_EFFECT_INDEX];
    GetBasePoints(owned, altInfo, weapon.altDamage, altPoints);
    CastAtGround(m_player, altInfo, altPoints, x, y, z);
}

void BonkRun::DespawnTraps()
{
    for (Trap const& trap : m_traps)
        PutAwayTrap(trap);
    m_traps.clear();
}

void BonkRun::ThrowTrap(OwnedWeapon& owned, float x, float y, float z)
{
    TrapLook const* look = GetTrapLook(uint32(owned.weapon.behaviorValue));
    if (!look)
    {
        SpringTrap(owned, x, y, z);
        return;
    }

    PendingCast spring;
    spring.weaponId = owned.weapon.id;
    spring.spellId = owned.weapon.spellId;
    spring.leftMs = int32(BONK_THROWN_TRAP_ARM_MS);
    spring.springTrap = true;
    spring.prop = PlaceTrapProp(look->set, x, y, z);
    spring.x = x;
    spring.y = y;
    spring.z = z;
    m_pendingCasts.push_back(spring);
}

void BonkRun::DealSpellHit(SpellEntry const* spellInfo, Unit* target, float damage, Unit* caster, bool forceCrit)
{
    if (!caster)
        caster = m_player;
    if (!spellInfo || !target || !target->IsAlive() || damage <= 0.0f || !m_player->IsAlive() || !caster->IsInWorld())
        return;

    SpellNonMeleeDamage hit(caster, target, spellInfo->Id, SpellSchools(spellInfo->School));
    Unit const* summon = caster == m_player ? nullptr : caster;
    bool const crit = forceCrit || (spellInfo->CanCrit() && roll_chance_f(GetCritChance(spellInfo, target, summon)));
    caster->CalculateSpellDamage(&hit, damage, spellInfo, EFFECT_INDEX_0, BASE_ATTACK, nullptr, crit);
    target->CalculateAbsorbResistBlock(caster, &hit, spellInfo, BASE_ATTACK, nullptr);
    caster->DealDamageMods(target, hit.damage, &hit.absorb);
    caster->SendSpellNonMeleeDamageLog(&hit);
    caster->DealSpellDamage(&hit, true);
}

std::vector<Creature*> BonkRun::GetMobsInLine(OwnedWeapon const& owned, float range, float angle) const
{
    float const halfWidth = BONK_LINE_WIDTH * GetWeaponArea(owned) * GetSteadyWidth(owned) / 2.0f;
    float const dx = std::cos(angle), dy = std::sin(angle);
    std::vector<Creature*> mobs = GetMobsInRange(range);
    mobs.erase(std::remove_if(mobs.begin(), mobs.end(), [&](Creature* mob)
    {
        float const x = mob->GetPositionX() - m_player->GetPositionX();
        float const y = mob->GetPositionY() - m_player->GetPositionY();
        float const along = x * dx + y * dy;
        float const side = std::fabs(x * dy - y * dx);
        return along < 0.0f || side > halfWidth + mob->GetObjectBoundingRadius();
    }), mobs.end());
    return mobs;
}

bool BonkRun::Howl(OwnedWeapon& owned)
{
    Creature* wolf = owned.pets.empty() ? nullptr : m_player->GetMap()->GetCreature(owned.pets.front());
    if (!wolf || !wolf->IsAlive() || GetMobsInRange(BONK_PET_HUNT_RANGE).empty())
        return false;

    wolf->HandleEmote(EMOTE_ONESHOT_ROAR);
    OnTalentHowl(wolf);
    uint32 const until = WorldTimer::getMSTime() + uint32(owned.weapon.durationMs * GetWeaponDurationScale(owned));
    if (!IsHowlFrenzied() || int32(until - m_petFrenzyUntil) > 0)
        m_petFrenzyUntil = until;
    for (OwnedWeapon& other : m_weapons)
        if (other.weapon.behavior == BONK_BEHAVIOR_PET)
            UpdatePets(other);
    DressSnakes();
    if (owned.weapon.HasFlag(BONK_WEAPON_FLAG_HOWL_BEASTS))
        CallHowlBeast(owned);
    DressBeasts();
    return true;
}

float BonkRun::GetFrenzyDamage() const
{
    if (!IsHowlFrenzied() && m_mastery.Has(BONK_CAPSTONE_BESTIAL_WRATH))
        return GetBestialWrathFrenzy();
    float damage = BONK_FRENZY_DAMAGE;
    for (OwnedWeapon const& owned : m_weapons)
        if (owned.weapon.behavior == BONK_BEHAVIOR_WRATH)
            damage += GetSpellUpgrade(owned, BONK_SPELL_UPGRADE_FRENZY) / 100.0f;
    return damage;
}

bool BonkRun::IsHowlFrenzied() const
{
    return int32(m_petFrenzyUntil - WorldTimer::getMSTime()) > 0;
}

bool BonkRun::IsKeywordAbilityHit(OwnedWeapon const& owned, SpellEntry const* spell) const
{
    if (!spell)
        return false;
    if (spell->Id == BONK_SPELL_SWIPE)
        return true;
    BonkPetFamily const* family = owned.weapon.behavior == BONK_BEHAVIOR_PET ? GetPetFamily(owned) : nullptr;
    if (!family)
        return false;
    return std::any_of(family->abilities.begin(), family->abilities.end(), [this, &owned, spell](BonkPetAbility const& ability)
    {
        return ability.stacks > 0 && (ability.spellId == spell->Id || GetWarlockAbilitySpell(owned, ability.spellId) == spell->Id);
    });
}

bool BonkRun::HasStackingPetAbility(OwnedWeapon const& owned) const
{
    BonkPetFamily const* family = owned.weapon.behavior == BONK_BEHAVIOR_PET ? GetPetFamily(owned) : nullptr;
    return family && std::any_of(family->abilities.begin(), family->abilities.end(), [](BonkPetAbility const& ability) { return ability.stacks > 0; });
}

void BonkRun::ApplySummonHitKeyword(Unit* summon, Unit* victim, SpellEntry const* spell)
{
    OwnedWeapon const* owned = FindSummonOwner(summon);
    if (!owned || !victim->IsAlive() || IsKeywordAbilityHit(*owned, spell))
        return;
    PendingKeyword keyword;
    if (!GetWeaponKeyword(*owned, keyword))
        return;
    bool const plainBite = !spell && owned->weapon.behavior == BONK_BEHAVIOR_PET && !HasStackingPetAbility(*owned);
    if (!plainBite)
        keyword.stacks = BONK_SUMMON_HIT_KEYWORD_STACKS;
    keyword.mob = victim->GetObjectGuid();
    m_pendingKeywords.push_back(keyword);
}

void BonkRun::OnPetBite(Creature* pet, Unit* victim)
{
    if (m_state != STATE_RUNNING || !pet || !victim)
        return;
    OnShamanPetBite(pet, victim);
    OnPriestPetBite(pet, victim);
    if (!IsPetFrenzied() || !IsSummon(pet))
        return;

    for (OwnedWeapon const& owned : m_weapons)
        if (owned.weapon.behavior == BONK_BEHAVIOR_PET && std::find(owned.pets.begin(), owned.pets.end(), pet->GetObjectGuid()) != owned.pets.end())
        {
            QueuePetCleave(owned, pet, victim);
            return;
        }
    for (Snake const& released : m_snakes)
        if (released.guid == pet->GetObjectGuid())
        {
            if (OwnedWeapon const* owned = FindOwned(released.weaponId))
                QueuePetCleave(*owned, pet, victim);
            return;
        }
    for (Beast const& beast : m_beasts)
        if (beast.guid == pet->GetObjectGuid())
        {
            if (beast.kind != BEAST_SPECTRAL)
                if (OwnedWeapon const* owned = FindOwned(beast.weaponId))
                    QueuePetCleave(*owned, pet, victim);
            return;
        }
}

void BonkRun::QueuePetCleave(OwnedWeapon const& owned, Creature* pet, Unit* victim)
{
    PendingCast cleave;
    cleave.weaponId = owned.weapon.id;
    cleave.spellId = owned.weapon.spellId;
    cleave.petCleave = true;
    cleave.source = pet->GetObjectGuid();
    cleave.target = victim->GetObjectGuid();
    m_pendingCasts.push_back(cleave);
}

float BonkRun::GetPetCleaveDamage(OwnedWeapon const& owned) const
{
    if (owned.weapon.HasFlag(BONK_WEAPON_FLAG_SNAKES))
        return GetWeaponDamage(owned) * GetFrenzyDamage();
    BonkPetFamily const* family = GetPetFamily(owned);
    if (!family)
        return 0.0f;
    return GetWeaponDamage(owned) * family->attackMs / float(std::max(500u, owned.weapon.tickMs)) * GetFrenzyDamage();
}

void BonkRun::UpdateDefensive(uint32 diff)
{
    m_defensiveTimer = std::max(0, m_defensiveTimer - int32(diff));
    if (m_defensiveTimer || m_player->GetClass() != CLASS_HUNTER || !m_player->IsAlive() || m_player->GetHealthPercent() >= BONK_DETERRENCE_HEALTH)
        return;

    BonkPlayer::SpellTweak& tweak = m_player->GetBonkPlayer()->TweakSpell(BONK_SPELL_DETERRENCE);
    tweak.durationMs = BONK_DETERRENCE_MS;
    tweak.durationScale = 1.0f;
    m_player->CastCustomSpell(m_player, BONK_SPELL_DETERRENCE, 0, BONK_DETERRENCE_DODGE, {}, true);
    m_defensiveTimer = BONK_DETERRENCE_COOLDOWN_MS;
}

void BonkRun::UpdatePets(OwnedWeapon& owned)
{
    BonkWeapon const& weapon = owned.weapon;
    bool const wolf = weapon.behavior == BONK_BEHAVIOR_WRATH;
    bool const plague = weapon.HasFlag(BONK_WEAPON_FLAG_PLAGUE);
    BonkPetFamily const* family = wolf ? nullptr : GetPetFamily(owned);
    uint32 const entry = wolf ? uint32(weapon.behaviorValue) : !family ? 0 : plague ? BONK_PLAGUE_BEAST : family->entry;
    uint32 const attackMs = wolf ? BONK_WOLF_ATTACK_MS : family ? family->attackMs : 0;
    bool const shifting = entry && weapon.HasFlag(BONK_WEAPON_FLAG_ELEMENTAL_SHIFT);
    uint32 const summonEntry = shifting ? BONK_HYBRID_ELEMENTAL : entry;
    Map* map = m_player->GetMap();
    owned.pets.erase(std::remove_if(owned.pets.begin(), owned.pets.end(), [this, map](ObjectGuid const& guid)
    {
        Creature* pet = map->GetCreature(guid);
        if (pet && pet->IsAlive())
            return false;
        if (pet)
            pet->DespawnOrUnsummon();
        m_petStates.erase(guid);
        return true;
    }), owned.pets.end());

    if (entry)
    {
        for (ObjectGuid& guid : owned.pets)
        {
            Creature* old = map->GetCreature(guid);
            if (old && shifting && old->GetEntry() == summonEntry)
                ShiftElemental(old, entry);
            if (!old || old->GetEntry() == summonEntry)
                continue;
            float x, y, z;
            old->GetPosition(x, y, z);
            m_petStates.erase(guid);
            Creature* risen = SummonPet(owned, summonEntry, x, y, z, old->GetOrientation());
            if (risen && shifting)
                ShiftElemental(risen, entry);
            old->DespawnOrUnsummon();
            guid = risen ? risen->GetObjectGuid() : ObjectGuid();
            if (!risen || !plague)
                continue;
            PendingCast rise;
            rise.kit = BONK_KIT_PLAGUE_RISE;
            rise.leftMs = BONK_PLAGUE_RISE_DELAY_MS;
            rise.target = guid;
            rise.x = x;
            rise.y = y;
            rise.z = z;
            m_pendingCasts.push_back(rise);
        }
        owned.pets.erase(std::remove(owned.pets.begin(), owned.pets.end(), ObjectGuid()), owned.pets.end());
    }

    uint32 const wanted = entry ? std::min(wolf ? 1u : GetWeaponTargets(owned) + GetShamanExtraPets(owned), GetSummonRoom(owned)) : 0;
    if (owned.pets.size() < wanted && m_player->IsAlive())
    {
        float const angle = frand(0.0f, 2.0f * M_PI_F);
        float x = m_player->GetPositionX() + 2.0f * std::cos(angle);
        float y = m_player->GetPositionY() + 2.0f * std::sin(angle);
        float z = m_player->GetPositionZ();
        m_player->GetMap()->GetWalkHitPosition(nullptr, m_player->GetPositionX(), m_player->GetPositionY(), m_player->GetPositionZ(), x, y, z);
        if (Creature* pet = SummonPet(owned, summonEntry, x, y, z, m_player->GetOrientation()))
        {
            if (shifting)
                ShiftElemental(pet, entry);
            if (!weapon.HasFlag(BONK_WEAPON_FLAG_TOTEM))
                pet->GetMotionMaster()->MoveFollow(m_player, 2.0f, angle);
            owned.pets.push_back(pet->GetObjectGuid());
            if (uint32 const kit = GetDruidSummonKit(owned))
                PlayKitAt(ObjectGuid(), x, y, z, kit, true);
        }
    }
    while (owned.pets.size() > wanted)
    {
        if (Creature* pet = map->GetCreature(owned.pets.back()))
            pet->DespawnOrUnsummon();
        owned.pets.pop_back();
    }
    if (!entry)
        return;

    uint32 const spellMs = std::max(500u, weapon.tickMs);
    bool const frenzied = !wolf && IsPetFrenzied();
    float const bite = wolf ? GetWeaponDamage(owned) * attackMs / float(spellMs) : GetPetBiteDamage(owned, *family);
    float const damage = bite * (frenzied ? GetFrenzyDamage() : 1.0f);
    float const pace = GetWeaponCooldown(owned) / float(std::max(1u, weapon.cooldownMs));
    uint32 const swing = std::max(300u, uint32(attackMs * pace));
    float const baseScale = wolf ? BONK_WOLF_SCALE : family->scale * (weapon.behaviorValue > 0.0f ? weapon.behaviorValue : 1.0f);
    float const scale = weapon.HasFlag(BONK_WEAPON_FLAG_TOTEM) ? baseScale * GetSummonReach(owned) : GetSummonLookScale(owned, baseScale);
    float const speed = GetSummonSpeedRate(GetStat(BONK_PASSIVE_MOVE_SPEED));
    for (ObjectGuid const& guid : owned.pets)
        if (Creature* pet = map->GetCreature(guid))
        {
            if (pet->GetBaseRunSpeedRate() != speed)
                pet->SetBaseSpeedRate(MOVE_RUN, speed);
            pet->SetBaseWeaponDamage(BASE_ATTACK, MINDAMAGE, damage * 0.9f);
            pet->SetBaseWeaponDamage(BASE_ATTACK, MAXDAMAGE, damage * 1.1f);
            pet->UpdateDamagePhysical(BASE_ATTACK);
            pet->SetAttackTime(BASE_ATTACK, swing);
            if (pet->GetInvincibilityHpThreshold() != pet->GetMaxHealth())
            {
                pet->SetHealth(pet->GetMaxHealth());
                pet->SetInvincibilityHpThreshold(pet->GetMaxHealth());
            }
            float const grown = scale * GetConsumedGrowth(guid);
            if (pet->GetObjectScale() != grown)
            {
                pet->SetObjectScale(grown);
                pet->UpdateModelData();
            }
            if (weapon.altSpellId && !pet->HasAura(weapon.altSpellId))
                pet->AddAura(weapon.altSpellId, ADD_AURA_PERMANENT);
            if (IsPlanted(weapon))
                PlantHydra(pet);
            if (plague && !pet->HasAura(BONK_SPELL_PLAGUE_LOOK))
                pet->AddAura(BONK_SPELL_PLAGUE_LOOK, ADD_AURA_PERMANENT);
            if (frenzied && !pet->HasAura(BONK_SPELL_BESTIAL_WRATH))
                pet->AddAura(BONK_SPELL_BESTIAL_WRATH);
            else if (!frenzied && pet->HasAura(BONK_SPELL_BESTIAL_WRATH))
                pet->RemoveAurasDueToSpell(BONK_SPELL_BESTIAL_WRATH);
        }
    ResendShownDamage(owned);
}

void BonkRun::ResendShownDamage(OwnedWeapon const& owned) const
{
    if (owned.shownDamage == DescribeShownDamage(owned))
        return;
    uint32 index = 0;
    for (OwnedWeapon const& held : m_weapons)
    {
        ++index;
        if (&held == &owned)
        {
            SendWeaponInfo(index, owned);
            return;
        }
    }
}

bool BonkRun::IsSummon(Unit const* unit) const
{
    if (!unit || !unit->IsCreature())
        return false;
    for (OwnedWeapon const& owned : m_weapons)
        if (std::find(owned.pets.begin(), owned.pets.end(), unit->GetObjectGuid()) != owned.pets.end())
            return true;
    return IsSnake(unit->GetObjectGuid()) || IsBeast(unit->GetObjectGuid()) || IsSkeleton(unit->GetObjectGuid()) || IsWarlockMinion(unit->GetObjectGuid());
}

Creature* BonkRun::SummonGlyphHelper(uint32 entry, float x, float y, float z, float orientation)
{
    s_nextHelperLevel = m_player->GetLevel();
    Creature* helper = m_player->SummonCreature(entry, x, y, z, orientation, TEMPSUMMON_TIMED_DESPAWN, BONK_OBJECT_LIFETIME_SEC * IN_MILLISECONDS,
        false, 0, &InstallPetAI);
    if (!helper)
        return nullptr;
    helper->SetOwnerGuid(m_player->GetObjectGuid());
    helper->SetFactionTemplateId(m_player->GetFactionTemplateId());
    helper->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IMMUNE_TO_PLAYER);
    return helper;
}

Creature* BonkRun::SummonPet(OwnedWeapon const& owned, uint32 entry, float x, float y, float z, float orientation)
{
    s_nextHelperLevel = m_player->GetLevel();
    Creature* pet = m_player->SummonCreature(entry, x, y, z, orientation, TEMPSUMMON_TIMED_DESPAWN, BONK_OBJECT_LIFETIME_SEC * IN_MILLISECONDS,
        false, 0, &InstallPetAI);
    if (!pet)
        return nullptr;
    pet->SetOwnerGuid(m_player->GetObjectGuid());
    pet->SetUInt32Value(UNIT_CREATED_BY_SPELL, owned.weapon.spellId);
    pet->SetFactionTemplateId(m_player->GetFactionTemplateId());
    pet->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IMMUNE_TO_PLAYER);
    DressWarlockPet(owned, pet);
    if (m_paused)
        HoldPet(pet, true, IsPlanted(owned.weapon));
    return pet;
}

float BonkRun::GetPetRunSpeed() const
{
    return GetSummonSpeedRate(GetStat(BONK_PASSIVE_MOVE_SPEED));
}

uint32 BonkRun::GetWantedSummons(OwnedWeapon const& owned)
{
    if (owned.weapon.behavior == BONK_BEHAVIOR_WRATH)
        return 1;
    if (owned.weapon.behavior != BONK_BEHAVIOR_PET)
        return 0;
    return GetWeaponTargets(owned) + GetShamanExtraPets(owned) + GetDruidExtraPets(owned);
}

uint32 BonkRun::GetSummonRoom(OwnedWeapon const& owned)
{
    std::vector<std::pair<OwnedWeapon const*, uint32>> wants;
    for (OwnedWeapon const& other : m_weapons)
        if (uint32 const wanted = GetWantedSummons(other))
            wants.emplace_back(&other, wanted);

    std::vector<uint32> given(wants.size(), 0);
    uint32 left = BONK_MAX_SUMMONS;
    for (bool handedOut = true; left && handedOut;)
    {
        handedOut = false;
        for (size_t i = 0; i < wants.size() && left; ++i)
            if (given[i] < wants[i].second)
            {
                ++given[i];
                --left;
                handedOut = true;
            }
    }
    for (size_t i = 0; i < wants.size(); ++i)
        if (wants[i].first == &owned)
            return given[i];
    return 0;
}

float BonkRun::GetSummonGrowth(OwnedWeapon const& owned) const
{
    float const growth = GetSpellUpgrade(owned, BONK_SPELL_UPGRADE_PET_SIZE);
    return std::min(growth, BONK_MAX_AREA);
}

float BonkRun::GetSummonReach(OwnedWeapon const& owned) const
{
    return 1.0f + CurveStat(GetSummonGrowth(owned) + GetTalentReach(owned), BONK_MAX_AREA) / 100.0f;
}

float BonkRun::GetSummonLookScale(OwnedWeapon const& owned, float base) const
{
    float const growth = std::min(GetSummonGrowth(owned) * BONK_SUMMON_LOOK_GROWTH, BONK_MAX_SUMMON_LOOK_GROWTH);
    return base * (1.0f + growth / 100.0f);
}

BonkPetFamily const* BonkRun::GetPetFamily(OwnedWeapon const& owned) const
{
    if (uint32 const warlockFamily = GetWarlockPetFamily(owned))
        return sBonkMgr.GetPetFamily(warlockFamily);
    if (uint32 const druidFamily = GetDruidPetFamily(owned))
        return sBonkMgr.GetPetFamily(druidFamily);
    uint32 const classMask = owned.weapon.classMask;
    if (classMask & (1 << (CLASS_WARLOCK - 1)))
        return nullptr;
    if (classMask & (1 << (CLASS_SHAMAN - 1)))
        return sBonkMgr.GetPetFamily(GetShamanPetFamily(owned));
    if (classMask & (1 << (CLASS_PRIEST - 1)))
        return sBonkMgr.GetPetFamily(GetPriestPetFamily(owned));
    return sBonkMgr.GetPetFamily(classMask & (1 << (CLASS_MAGE - 1)) ? GetElementalFamily() : BONK_PET_FAMILY_CAT);
}

float BonkRun::GetPetBiteDamage(OwnedWeapon const& owned, BonkPetFamily const& family) const
{
    float rate = IN_MILLISECONDS / float(family.attackMs);
    for (BonkPetAbility const& ability : family.abilities)
        if (ability.kind != BONK_PET_ABILITY_DASH)
            rate += ability.damage * IN_MILLISECONDS / float(ability.cooldownMs);
    uint32 const spellMs = std::max(500u, owned.weapon.tickMs);
    return GetWeaponDamage(owned) * IN_MILLISECONDS / float(spellMs) / rate * GetDemonDamageScale() * GetSummonTalentScale(owned);
}

float BonkRun::GetSummonTalentScale(OwnedWeapon const& owned) const
{
    auto talentDamage = m_mastery.spellDamage.find(GetRootWeapon(owned.weapon));
    float const talentPercent = talentDamage != m_mastery.spellDamage.end() ? talentDamage->second : 0.0f;
    return 1.0f + talentPercent / 100.0f;
}

void BonkRun::StartPetAttack(OwnedWeapon const& owned, Creature* pet, Creature* target)
{
    BonkPetFamily const* family = GetPetFamily(owned);
    if (!family || !IsCasterPetFamily(*family))
    {
        pet->AI()->AttackStart(target);
        return;
    }
    if (!pet->Attack(target, false))
        return;
    pet->SetInCombatWith(target);
    pet->GetMotionMaster()->MoveChase(target, BONK_CASTER_PET_DISTANCE);
}

void BonkRun::UsePetAbilities(OwnedWeapon& owned, Creature* pet, Creature* prey)
{
    BonkPetFamily const* family = GetPetFamily(owned);
    if (!family || family->abilities.empty() || !prey->IsAlive())
        return;

    PetState& state = m_petStates[pet->GetObjectGuid()];
    state.readyAtMs.resize(family->abilities.size(), 0);
    uint32 const now = WorldTimer::getMSTime();
    float const pace = GetWeaponCooldown(owned) / float(std::max(1u, owned.weapon.cooldownMs));
    for (size_t i = 0; i < family->abilities.size(); ++i)
    {
        BonkPetAbility const& ability = family->abilities[i];
        if (ability.kind == BONK_PET_ABILITY_PULSE && int32(now - state.readyAtMs[i]) >= 0 && PetPulse(owned, pet, ability))
            state.readyAtMs[i] = now + uint32(ability.cooldownMs * pace);
    }
    if (!prey->IsAlive())
        return;

    bool const inReach = pet->CanReachWithMeleeAutoAttack(prey) || IsPetInRunningReach(pet, prey);
    for (size_t i = 0; i < family->abilities.size(); ++i)
    {
        BonkPetAbility const& ability = family->abilities[i];
        if (int32(now - state.readyAtMs[i]) < 0)
            continue;
        bool const strikeReach = ability.value > 0.0f ? pet->IsWithinDistInMap(prey, ability.value) && pet->IsWithinLOSInMap(prey) : inReach;
        if (ability.kind == BONK_PET_ABILITY_STRIKE && strikeReach)
        {
            PetStrike(owned, pet, PickHydraHeadTarget(*family, pet, prey, i, ability.value), ability);
            StrikeSearingTotemSecondTarget(owned, *family, pet, prey, ability);
            state.readyAtMs[i] = now + uint32(ability.cooldownMs * pace * GetClawRate(owned));
            OnWarlockPetStrike(owned, pet, prey, uint32(i));
            OnDruidPetStrike(owned, pet, prey);
            return;
        }
        if (ability.kind == BONK_PET_ABILITY_CONE && strikeReach && PetPulse(owned, pet, ability, prey))
        {
            state.readyAtMs[i] = now + uint32(ability.cooldownMs * pace);
            return;
        }
        if (ability.kind == BONK_PET_ABILITY_DASH && !pet->IsWithinDistInMap(prey, BONK_PET_DASH_RANGE))
        {
            if (SpellAuraHolder* holder = pet->AddAura(ability.spellId))
            {
                int32 const lasts = std::max(500, int32(ability.value));
                holder->SetAuraMaxDuration(lasts);
                holder->SetAuraDuration(lasts);
                holder->RefreshHolder();
            }
            state.readyAtMs[i] = now + uint32(ability.cooldownMs * pace);
        }
    }
}

void BonkRun::PetStrike(OwnedWeapon& owned, Creature* pet, Creature* prey, BonkPetAbility const& ability)
{
    BonkPetFamily const* family = GetPetFamily(owned);
    if (!family)
        return;

    float const frenzy = IsPetFrenzied() ? GetFrenzyDamage() : 1.0f;
    int32 const damage = std::max(1, int32(std::min(MaxHitDamage, GetPetBiteDamage(owned, *family) * ability.damage * frenzy * GetWarlockPetStrikeScale(owned, prey))));
    pet->SetFacingToObject(prey);
    uint32 const abilitySpell = GetWarlockAbilitySpell(owned, ability.spellId);
    SpellEntry const* abilityInfo = sSpellMgr.GetSpellEntry(abilitySpell);
    if (owned.weapon.HasFlag(BONK_WEAPON_FLAG_TOTEM) && abilityInfo)
    {
        ShowStrike(pet, abilityInfo, prey);
        DealSpellHit(abilityInfo, prey, float(damage), pet);
    }
    else
        pet->CastCustomSpell(prey, abilitySpell, damage, {}, {}, true);

    OnTalentPetStrike(owned, pet, prey, abilityInfo, damage);

    PendingKeyword keyword;
    uint32 stacks = owned.weapon.HasFlag(BONK_WEAPON_FLAG_PLAGUE) ? BONK_PLAGUE_CLAW_STACKS : ability.stacks;
    if (stacks && GetRootWeapon(owned.weapon) == BONK_WEAPON_CALL_PET)
        stacks += uint32(m_mastery.Special(BONK_SPECIAL_PACK_TACTICS));
    auto talentStacks = m_mastery.spellStacks.find(GetRootWeapon(owned.weapon));
    if (stacks && talentStacks != m_mastery.spellStacks.end())
        stacks += uint32(talentStacks->second);
    if (ability.stacks && prey->IsAlive() && (GetWeaponKeyword(owned, keyword) || GetPetAbilityKeyword(owned, ability, keyword)))
        ApplyKeyword(prey, keyword.keyword, stacks + uint32(GetSpellUpgrade(owned, BONK_SPELL_UPGRADE_STACKS)), keyword.durationMs,
            keyword.damageScale, keyword.spellId);
}

bool BonkRun::PetPulse(OwnedWeapon& owned, Creature* pet, BonkPetAbility const& ability, Creature* coneTarget)
{
    BonkPetFamily const* family = GetPetFamily(owned);
    SpellEntry const* spellInfo = sSpellMgr.GetSpellEntry(ability.spellId);
    if (!family || !spellInfo)
        return false;

    float const radius = ability.value * GetSummonReach(owned);
    std::vector<Creature*> mobs = GetMobsNear(pet->GetPositionX(), pet->GetPositionY(), radius);
    if (coneTarget)
    {
        pet->SetFacingToObject(coneTarget);
        bool const allAround = IsWarlockFullLash(owned);
        mobs.erase(std::remove_if(mobs.begin(), mobs.end(), [pet, coneTarget, allAround](Creature* mob)
        {
            return mob != coneTarget && !allAround && !pet->HasInArc(mob, BONK_PET_CONE_ARC);
        }), mobs.end());
        ShowStrike(pet, spellInfo, coneTarget);
    }
    if (mobs.empty())
        return false;

    if (ability.kit)
        pet->SendPlaySpellVisualKit(ability.kit);

    float const frenzy = IsPetFrenzied() ? GetFrenzyDamage() : 1.0f;
    uint32 const root = GetRootWeapon(owned.weapon);
    float const hit = std::max(1.0f, GetPetBiteDamage(owned, *family) * ability.damage * frenzy);
    PendingKeyword keyword;
    bool const applies = ability.stacks && (GetWeaponKeyword(owned, keyword) || GetPetAbilityKeyword(owned, ability, keyword));
    auto talentStacks = m_mastery.spellStacks.find(root);
    uint32 const stacks = ability.stacks + uint32(GetSpellUpgrade(owned, BONK_SPELL_UPGRADE_STACKS)) +
        (talentStacks != m_mastery.spellStacks.end() ? uint32(talentStacks->second) : 0);
    for (Creature* mob : mobs)
    {
        if (!pet->IsWithinLOSInMap(mob))
            continue;
        DealSpellHit(spellInfo, mob, hit, pet);
        if (applies && mob->IsAlive())
            ApplyKeyword(mob, keyword.keyword, stacks, keyword.durationMs, keyword.damageScale, keyword.spellId);
    }
    OnShamanPetPulse(owned, pet, radius);
    return true;
}

bool BonkRun::Pounce(OwnedWeapon& owned, Creature* pet, Creature* prey)
{
    BonkPetFamily const* family = GetPetFamily(owned);
    if (!family)
        return false;

    PetState& state = m_petStates[pet->GetObjectGuid()];
    state.readyAtMs.resize(family->abilities.size(), 0);
    uint32 const now = WorldTimer::getMSTime();
    for (size_t i = 0; i < family->abilities.size(); ++i)
    {
        BonkPetAbility const& ability = family->abilities[i];
        if (ability.kind != BONK_PET_ABILITY_POUNCE || int32(now - state.readyAtMs[i]) < 0 ||
            pet->IsWithinDistInMap(prey, ability.value) || !pet->IsWithinLOSInMap(prey))
            continue;

        float const flightMs = pet->GetDistance(prey) / BONK_POUNCE_SPEED * IN_MILLISECONDS;
        float x, y, z, o;
        if (!prey->ExtrapolateMovement(prey->m_movementInfo, uint32(flightMs), x, y, z, o))
            prey->GetPosition(x, y, z);
        float const reach = pet->GetCombatReachToTarget(prey, false, 0.0f, true) / 2.0f;
        float const dx = pet->GetPositionX() - x;
        float const dy = pet->GetPositionY() - y;
        float const length = std::sqrt(dx * dx + dy * dy);
        if (length > reach)
        {
            x += dx / length * reach;
            y += dy / length * reach;
        }
        pet->AttackStop();
        pet->GetMotionMaster()->Clear(false);
        pet->GetMotionMaster()->MovePoint(0, x, y, z, MOVE_PATHFINDING | MOVE_RUN_MODE | MOVE_FORCE_DESTINATION, BONK_POUNCE_SPEED);
        uint32 const landMs = (pet->movespline->Finalized() ? uint32(flightMs) : uint32(pet->movespline->Duration())) + 50;

        float const pace = GetWeaponCooldown(owned) / float(std::max(1u, owned.weapon.cooldownMs));
        state.busyUntilMs = now + landMs;
        state.readyAtMs[i] = now + uint32(ability.cooldownMs * pace);

        PendingCast land;
        land.weaponId = owned.weapon.id;
        land.spellId = owned.weapon.spellId;
        land.leftMs = int32(landMs);
        land.pounce = true;
        land.source = pet->GetObjectGuid();
        land.target = prey->GetObjectGuid();
        m_pendingCasts.push_back(land);
        return true;
    }
    return false;
}

uint32 BonkRun::GetPetPreyRank(Creature const* mob) const
{
    bool const isTreasureGoblin = m_miniEventActive && m_miniEvent.type == BONK_MINI_EVENT_GOBLIN && mob->GetObjectGuid() == m_miniEvent.target;
    return isTreasureGoblin ? 0 : GetKindRank(mob);
}

void BonkRun::UpdatePetTargets(uint32 diff)
{
    m_petTimer += diff;
    if (m_petTimer < BONK_PET_CHECK_MS)
        return;
    m_petTimer = 0;
    UpdateSkeletons();

    if (IsPetFrenzied() != m_petsFrenzied)
    {
        m_petsFrenzied = !m_petsFrenzied;
        for (OwnedWeapon& owned : m_weapons)
            if (owned.weapon.behavior == BONK_BEHAVIOR_PET)
                UpdatePets(owned);
        DressSnakes();
        DressBeasts();
    }
    UpdateSnakes();

    std::vector<Creature*> const prey = GetMobsInRange(BONK_PET_HUNT_RANGE);
    uint32 biggest = 0;
    for (Creature* mob : prey)
        biggest = std::max(biggest, GetPetPreyRank(mob));

    Map* map = m_player->GetMap();
    uint32 const now = WorldTimer::getMSTime();
    std::vector<ObjectGuid> hunted;
    std::vector<std::pair<OwnedWeapon*, Creature*>> seeking;
    auto consider = [&](OwnedWeapon& owned, ObjectGuid const& guid, bool minion)
    {
        Creature* pet = map->GetCreature(guid);
        if (!pet || !pet->IsAlive() || (owned.weapon.damageFraction <= 0.0f && !minion))
            return;
        auto state = m_petStates.find(guid);
        if (state != m_petStates.end() && int32(state->second.busyUntilMs - now) > 0)
            return;

        if (!pet->IsWithinDistInMap(m_player, BONK_PET_LEASH_RANGE))
        {
            pet->AttackStop();
            pet->NearTeleportTo(m_player->GetPositionX(), m_player->GetPositionY(), m_player->GetPositionZ(), m_player->GetOrientation());
            return;
        }

        Creature* victim = pet->GetVictim() ? pet->GetVictim()->ToCreature() : nullptr;
        if (victim && victim->IsAlive() && victim->IsWithinDistInMap(m_player, BONK_PET_HUNT_RANGE) && GetPetPreyRank(victim) >= biggest)
        {
            hunted.push_back(victim->GetObjectGuid());
            if (owned.weapon.behavior == BONK_BEHAVIOR_PET)
                UsePetAbilities(owned, pet, victim);
        }
        else
            seeking.emplace_back(&owned, pet);
    };
    for (OwnedWeapon& owned : m_weapons)
        for (ObjectGuid const& guid : owned.pets)
            consider(owned, guid, false);
    for (Snake const& released : m_snakes)
        if (OwnedWeapon* owned = FindOwned(released.weaponId))
            consider(*owned, released.guid, false);
    for (Beast const& beast : m_beasts)
        if (beast.kind != BEAST_SPECTRAL && !beast.charging)
            if (OwnedWeapon* owned = FindOwned(beast.weaponId))
                consider(*owned, beast.guid, false);
    for (ObjectGuid const& guid : GetWarlockMinionGuids())
        if (OwnedWeapon* owned = FindOwned(GetWarlockMinionWeapon(guid)))
            consider(*owned, guid, true);
    if (seeking.empty())
        return;

    auto isFree = [&hunted](Creature* mob)
    {
        return std::find(hunted.begin(), hunted.end(), mob->GetObjectGuid()) == hunted.end();
    };
    for (auto const& seeker : seeking)
    {
        Creature* pet = seeker.second;
        if (!prey.empty())
        {
            auto nearest = [pet, &prey](auto const& wanted)
            {
                Creature* found = nullptr;
                float best = 0.0f;
                for (Creature* mob : prey)
                {
                    float const distance = pet->GetDistance(mob);
                    if (wanted(mob) && (!found || distance < best))
                    {
                        found = mob;
                        best = distance;
                    }
                }
                return found;
            };
            Creature* target = nullptr;
            if (biggest > 0)
                target = nearest([this, biggest](Creature* mob) { return GetPetPreyRank(mob) == biggest; });
            else
            {
                for (Creature* mob : prey)
                    if (isFree(mob) && pet->IsWithinDistInMap(mob, BONK_PET_PICK_RANGE) && (!target || mob->GetHealth() > target->GetHealth()))
                        target = mob;
                if (!target)
                    target = nearest(isFree);
                if (!target)
                    target = nearest([](Creature*) { return true; });
            }
            hunted.push_back(target->GetObjectGuid());
            if (pet->GetVictim() != target &&
                !(seeker.first->weapon.behavior == BONK_BEHAVIOR_PET && Pounce(*seeker.first, pet, target)))
                StartPetAttack(*seeker.first, pet, target);
            continue;
        }
        if (pet->GetVictim() || pet->GetMotionMaster()->GetCurrentMovementGeneratorType() != FOLLOW_MOTION_TYPE)
        {
            pet->AttackStop();
            pet->GetMotionMaster()->Clear(false);
            pet->GetMotionMaster()->MoveFollow(m_player, 2.0f, frand(0.0f, 2.0f * M_PI_F));
        }
    }
}

void BonkRun::DespawnPets()
{
    ReleaseDrainBeams();
    DespawnWarlockMinions();
    Map* map = m_player->FindMap();
    for (OwnedWeapon& owned : m_weapons)
    {
        if (map)
            for (ObjectGuid const& guid : owned.pets)
                if (Creature* pet = map->GetCreature(guid))
                    pet->DespawnOrUnsummon();
        owned.pets.clear();
    }
    DespawnSnakes();
    DespawnBeasts();
    DespawnSkeletons();
    m_petStates.clear();
    m_petFrenzyUntil = 0;
}

std::vector<Creature*> BonkRun::PickGroupCenters(std::vector<Creature*> const& mobs, float radius, uint32 count) const
{
    return PickGroundSpots(mobs, radius, count);
}

void BonkRun::ShowFallingAt(SpellEntry const* spellInfo, float x, float y, float z)
{
    ShowMissileBetween(spellInfo, x, y, z + BONK_PROJECTILE_HEIGHT, x, y, z);
}

void BonkRun::ShowMissileFrom(Unit* source, SpellEntry const* spellInfo, float x, float y, float z)
{
    ShowMissileAt(source, spellInfo, x, y, z);
}

void BonkRun::ShowMissileBetween(SpellEntry const* spellInfo, float fromX, float fromY, float fromZ, float toX, float toY, float toZ)
{
    float const distance = std::sqrt((toX - fromX) * (toX - fromX) + (toY - fromY) * (toY - fromY) + (toZ - fromZ) * (toZ - fromZ));
    uint32 const flightMs = spellInfo->speed > 0.0f ? uint32(distance / spellInfo->speed * IN_MILLISECONDS) : 0;
    SendBonkMissile(m_player, GetQuietStrikeLook(spellInfo->Id), fromX, fromY, fromZ, toX, toY, toZ, flightMs + BONK_ERUPTION_LOOK_MS);
}

void BonkRun::RefreshHeroPositionNow()
{
    if (m_player->IsInWorld())
        GetHeroPositionNow(m_player, m_heroNow.x, m_heroNow.y, m_heroNow.z);
}

void BonkRun::ShowThrowAtSpot(Unit* source, SpellEntry const* spellInfo, Unit const* target)
{
    ShowMissileAt(source, spellInfo, target->GetPositionX(), target->GetPositionY(),
        target->GetPositionZ() + target->GetCollisionHeight() / 2.0f);
}

void BonkRun::ShowMissilesAround(SpellEntry const* spellInfo, float radius, uint32 count)
{
    float const x = m_player->GetPositionX(), y = m_player->GetPositionY(), z = m_player->GetPositionZ();
    float const startAngle = frand(0.0f, 2.0f * M_PI_F);
    for (uint32 i = 0; i < count; ++i)
    {
        float const angle = startAngle + 2.0f * M_PI_F * i / count;
        float tx = x + radius * std::cos(angle), ty = y + radius * std::sin(angle), tz = z;
        m_player->GetMap()->GetWalkHitPosition(nullptr, x, y, z, tx, ty, tz);
        ShowMissileAt(m_player, spellInfo, tx, ty, tz);
    }
}

void BonkRun::PlaceAreaLook(uint32 spellId, float x, float y, float z, float radius, uint32 durationMs)
{
    PlaceAreaVisual(m_player, spellId, x, y, z, radius, durationMs);
}

bool BonkRun::FindLargestGroup(float range, float& x, float& y, float& z) const
{
    std::vector<Creature*> mobs = GetMobsInRange(range);
    if (mobs.empty())
        return false;
    if (mobs.size() > BONK_GROUP_SEEK_MOBS)
        mobs.resize(BONK_GROUP_SEEK_MOBS);
    std::vector<Creature*> const groups = PickGroundSpots(mobs, BONK_LINE_GROUP_RADIUS, 1);
    (groups.empty() ? mobs.front() : groups.front())->GetPosition(x, y, z);
    return true;
}

void BonkRun::UpdateStampede(uint32 diff)
{
    auto stampede = std::find_if(m_weapons.begin(), m_weapons.end(), [](OwnedWeapon const& owned)
    {
        return owned.weapon.HasFlag(BONK_WEAPON_FLAG_STAMPEDE);
    });
    if (stampede == m_weapons.end())
        return;

    m_stampedeTimer -= int32(diff);
    if (m_stampedeTimer > 0)
        return;
    float const pace = GetWeaponCooldown(*stampede) / float(std::max(1u, stampede->weapon.cooldownMs));
    m_stampedeTimer = LaunchStampede(*stampede) ? int32(BONK_STAMPEDE_MS * pace) : BONK_STAMPEDE_RETRY_MS;
}

bool BonkRun::LaunchStampede(OwnedWeapon const& owned)
{
    float cx, cy, cz;
    if (!m_player->IsAlive() || !FindLargestGroup(BONK_PET_HUNT_RANGE, cx, cy, cz))
        return false;

    float hx, hy, hz;
    GetHeroPositionNow(m_player, hx, hy, hz);
    float const angle = std::fabs(cx - hx) + std::fabs(cy - hy) > 0.1f ? std::atan2(cy - hy, cx - hx) : m_player->GetOrientation();
    float const dx = std::cos(angle), dy = std::sin(angle);
    float const area = GetWeaponArea(owned);
    Map* map = m_player->GetMap();
    bool launched = false;
    for (uint32 i = 0; i < BONK_STAMPEDE_BEASTS; ++i)
    {
        float const side = (float(i) - (BONK_STAMPEDE_BEASTS - 1) / 2.0f) * BONK_STAMPEDE_LANE * area;
        float sx = cx - dx * BONK_STAMPEDE_LEAD - dy * side;
        float sy = cy - dy * BONK_STAMPEDE_LEAD + dx * side;
        float sz = cz;
        float ex = cx + dx * BONK_STAMPEDE_PAST - dy * side;
        float ey = cy + dy * BONK_STAMPEDE_PAST + dx * side;
        float ez = cz;
        map->GetWalkHitPosition(nullptr, cx, cy, cz, sx, sy, sz);
        map->GetWalkHitPosition(nullptr, cx, cy, cz, ex, ey, ez);
        if (!SummonBeast(owned, BEAST_SPECTRAL, sx, sy, sz, angle, uint32(BONK_STAMPEDE_LIFE_MS * GetWeaponDurationScale(owned))))
            continue;
        QueueCharge(m_beasts.back(), ex, ey, ez, 1.0f, BONK_STAMPEDE_SPEED);
        launched = true;
    }
    return launched;
}

void BonkRun::CallHowlBeast(OwnedWeapon const& owned)
{
    BeastKind const kinds[] = { BEAST_BOAR, BEAST_BEAR, BEAST_WYVERN };
    BeastKind const kind = kinds[urand(0, 2)];
    Map* map = m_player->GetMap();
    float hx, hy, hz;
    GetHeroPositionNow(m_player, hx, hy, hz);
    float gx = hx, gy = hy, gz = hz;
    bool const group = FindLargestGroup(BONK_PET_HUNT_RANGE, gx, gy, gz);

    float const angle = frand(0.0f, 2.0f * M_PI_F);
    float x = hx + BONK_HOWL_BEAST_SPREAD * std::cos(angle);
    float y = hy + BONK_HOWL_BEAST_SPREAD * std::sin(angle);
    float z = hz;
    if (kind == BEAST_BEAR && group)
    {
        x = gx;
        y = gy;
        z = gz;
    }
    map->GetWalkHitPosition(nullptr, hx, hy, hz, x, y, z);

    float const facing = group && (std::fabs(gx - x) + std::fabs(gy - y) > 0.1f) ? std::atan2(gy - y, gx - x) : angle;
    if (!SummonBeast(owned, kind, x, y, z, facing, uint32(BONK_HOWL_BEAST_MS * GetWeaponDurationScale(owned))))
        return;

    Beast& beast = m_beasts.back();
    if (kind == BEAST_BEAR)
        beast.actMs = BONK_BEAR_FIRST_MAUL_MS;
    else if (kind == BEAST_WYVERN)
        beast.actMs = BONK_WYVERN_FIRST_SCREECH_MS;
    else if (group)
    {
        float ex = gx + std::cos(facing) * BONK_BOAR_CHARGE_PAST;
        float ey = gy + std::sin(facing) * BONK_BOAR_CHARGE_PAST;
        float ez = gz;
        map->GetWalkHitPosition(nullptr, gx, gy, gz, ex, ey, ez);
        QueueCharge(beast, ex, ey, ez, BONK_BOAR_CHARGE_SHARE, BONK_BOAR_CHARGE_SPEED);
    }
    DressBeasts();
    m_petTimer = BONK_PET_CHECK_MS;
}

Creature* BonkRun::SummonBeast(OwnedWeapon const& owned, BeastKind kind, float x, float y, float z, float orientation, uint32 lifeMs)
{
    bool const spectral = kind == BEAST_SPECTRAL;
    MakeRoomForBeast(spectral);

    uint32 entry = BONK_SPECTRAL_BEAST;
    switch (kind)
    {
        case BEAST_BOAR:   entry = BONK_HOWL_BOAR; break;
        case BEAST_BEAR:   entry = BONK_HOWL_BEAR; break;
        case BEAST_WYVERN: entry = BONK_HOWL_WYVERN; break;
        default:           break;
    }
    s_nextHelperLevel = m_player->GetLevel();
    Creature* creature = m_player->SummonCreature(entry, x, y, z, orientation, TEMPSUMMON_TIMED_DESPAWN, BONK_OBJECT_LIFETIME_SEC * IN_MILLISECONDS,
        false, 0, spectral ? &InstallChargerAI : &InstallPetAI);
    if (!creature)
        return nullptr;

    creature->SetOwnerGuid(m_player->GetObjectGuid());
    creature->SetUInt32Value(UNIT_CREATED_BY_SPELL, owned.weapon.spellId);
    creature->SetFactionTemplateId(m_player->GetFactionTemplateId());
    if (!spectral)
        creature->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IMMUNE_TO_PLAYER);
    creature->SetHealth(creature->GetMaxHealth());
    creature->SetInvincibilityHpThreshold(creature->GetMaxHealth());
    if (spectral)
    {
        creature->SetObjectScale(GetSummonLookScale(owned, BONK_SPECTRAL_SCALE));
        creature->UpdateModelData();
    }

    Beast beast;
    beast.guid = creature->GetObjectGuid();
    beast.weaponId = owned.weapon.id;
    beast.kind = kind;
    beast.untilMs = m_totalMs + lifeMs;
    beast.lastX = x;
    beast.lastY = y;
    m_beasts.push_back(std::move(beast));
    return creature;
}

void BonkRun::MakeRoomForBeast(bool spectral)
{
    auto alike = [spectral](Beast const& beast) { return (beast.kind == BEAST_SPECTRAL) == spectral; };
    size_t count = size_t(std::count_if(m_beasts.begin(), m_beasts.end(), alike));
    size_t const cap = spectral ? BONK_MAX_SPECTRAL_BEASTS : BONK_MAX_HOWL_BEASTS;
    std::vector<ObjectGuid> oldest;
    for (Beast const& beast : m_beasts)
    {
        if (count < cap)
            break;
        if (!alike(beast))
            continue;
        oldest.push_back(beast.guid);
        --count;
    }
    RemoveBeasts(oldest);
}

void BonkRun::RemoveBeasts(std::vector<ObjectGuid> const& gone)
{
    if (gone.empty())
        return;
    Map* map = m_player->GetMap();
    for (ObjectGuid const& guid : gone)
    {
        if (Creature* creature = map->GetCreature(guid))
            creature->DespawnOrUnsummon();
        m_petStates.erase(guid);
    }
    m_beasts.erase(std::remove_if(m_beasts.begin(), m_beasts.end(), [&gone](Beast const& beast)
    {
        return std::find(gone.begin(), gone.end(), beast.guid) != gone.end();
    }), m_beasts.end());
}

void BonkRun::QueueCharge(Beast& beast, float x, float y, float z, float share, float speed)
{
    beast.charging = true;
    beast.chargeDelayMs = BONK_CHARGE_DELAY_MS;
    beast.share = share;
    beast.speed = speed;
    beast.toX = x;
    beast.toY = y;
    beast.toZ = z;
    beast.struck.clear();
}

void BonkRun::BeginCharge(Beast& beast, Creature* creature)
{
    beast.lastX = creature->GetPositionX();
    beast.lastY = creature->GetPositionY();
    creature->AttackStop();
    creature->GetMotionMaster()->Clear(false);
    creature->GetMotionMaster()->MovePoint(0, beast.toX, beast.toY, beast.toZ, MOVE_PATHFINDING | MOVE_RUN_MODE | MOVE_FORCE_DESTINATION, beast.speed);
    float const travelMs = creature->movespline->Finalized() ? creature->GetDistance2d(beast.toX, beast.toY) / beast.speed * IN_MILLISECONDS :
        float(creature->movespline->Duration());
    beast.chargeLeftMs = int32(travelMs) + BONK_CHARGE_GRACE_MS;
    creature->SendPlaySpellVisualKit(BONK_KIT_CHARGE_TRAIL);
}

void BonkRun::UpdateCharge(Beast& beast, Creature* creature, uint32 diff, std::vector<BeastHit>& hits)
{
    if (beast.chargeDelayMs > 0)
    {
        beast.chargeDelayMs -= int32(diff);
        if (beast.chargeDelayMs <= 0)
            BeginCharge(beast, creature);
        return;
    }

    OwnedWeapon const* owned = FindOwned(beast.weaponId);
    float const reach = BONK_CHARGE_REACH * (owned ? GetWeaponArea(*owned) : 1.0f);
    float const x = creature->GetPositionX(), y = creature->GetPositionY();
    Map* map = m_player->GetMap();
    for (ObjectGuid const& guid : GetEnemyGuids())
    {
        Creature* mob = map->GetCreature(guid);
        if (!mob || !mob->IsAlive() || std::find(beast.struck.begin(), beast.struck.end(), guid) != beast.struck.end())
            continue;
        if (std::fabs(mob->GetPositionZ() - creature->GetPositionZ()) > BONK_PROJECTILE_Z_REACH ||
            GetSegmentDistance(mob->GetPositionX(), mob->GetPositionY(), beast.lastX, beast.lastY, x, y) > reach + mob->GetObjectBoundingRadius())
            continue;
        beast.struck.push_back(guid);
        BeastHit hit;
        hit.beast = beast.guid;
        hit.victim = guid;
        hit.weaponId = beast.weaponId;
        hit.share = beast.share;
        hits.push_back(hit);
    }
    beast.lastX = x;
    beast.lastY = y;

    beast.chargeLeftMs -= int32(diff);
    if (creature->movespline->Finalized() || beast.chargeLeftMs <= 0)
        EndCharge(beast, creature);
}

void BonkRun::EndCharge(Beast& beast, Creature* creature)
{
    beast.charging = false;
    if (beast.kind == BEAST_SPECTRAL)
        return;
    creature->StopMoving();
    creature->GetMotionMaster()->Clear(false);
    m_petTimer = BONK_PET_CHECK_MS;
}

void BonkRun::ResumeCharges()
{
    Map* map = m_player->FindMap();
    if (!map)
        return;
    for (Beast& beast : m_beasts)
        if (beast.charging && beast.chargeDelayMs <= 0)
            if (Creature* creature = map->GetCreature(beast.guid))
                BeginCharge(beast, creature);
}

bool BonkRun::ActBeast(Beast& beast, Creature* creature, uint32 diff, std::vector<ObjectGuid>& mauls)
{
    if (beast.kind != BEAST_BEAR && beast.kind != BEAST_WYVERN)
        return false;
    beast.actMs -= int32(diff);
    if (beast.actMs > 0)
        return false;

    if (beast.kind == BEAST_BEAR)
    {
        beast.actMs = BONK_BEAR_IDLE_MS;
        mauls.push_back(beast.guid);
        return false;
    }
    creature->HandleEmote(EMOTE_ONESHOT_ROAR);
    creature->SendPlaySpellVisualKit(BONK_KIT_SCREECH);
    beast.actMs = BONK_WYVERN_SCREECH_MS;
    bool const first = !beast.acted;
    beast.acted = true;
    return first;
}

bool BonkRun::Maul(Creature* bear, OwnedWeapon const& owned)
{
    SpellEntry const* swipe = sSpellMgr.GetSpellEntry(BONK_SPELL_SWIPE);
    std::vector<Creature*> const mobs = GetMobsNear(bear->GetPositionX(), bear->GetPositionY(), BONK_BEAR_MAUL_RADIUS * GetWeaponArea(owned));
    if (!swipe || mobs.empty())
        return false;

    bear->HandleEmote(EMOTE_ONESHOT_ATTACKUNARMED);
    bear->SendPlaySpellVisualKit(BONK_KIT_MAUL);
    BonkKeywordInfo const* bleed = sBonkMgr.GetKeyword(BONK_KEYWORD_BLEED);
    uint32 const bleedMs = bleed ? uint32(bleed->durationMs * GetWeaponDurationScale(owned)) : 0;
    float const bleedScale = 1.0f + (owned.damageBonus + GetSpellUpgrade(owned, BONK_SPELL_UPGRADE_PET_DAMAGE)) / 100.0f;
    float const damage = GetBeastHitDamage(owned);
    for (Creature* mob : mobs)
    {
        DealSpellHit(swipe, mob, damage, bear);
        if (bleed && mob->IsAlive())
            ApplyKeyword(mob, BONK_KEYWORD_BLEED, BONK_BEAR_MAUL_STACKS, bleedMs, bleedScale, owned.weapon.spellId);
    }
    return true;
}

float BonkRun::GetBeastHitDamage(OwnedWeapon const& owned) const
{
    return GetWeaponDamage(owned) * (IsPetFrenzied() ? GetFrenzyDamage() : 1.0f);
}

void BonkRun::UpdateBeasts(uint32 diff)
{
    UpdateStampede(diff);
    PulsePlague(diff);
    if (m_beasts.empty())
    {
        UpdateScreech(diff, false);
        return;
    }

    Map* map = m_player->GetMap();
    std::vector<BeastHit> hits;
    std::vector<ObjectGuid> mauls;
    std::vector<ObjectGuid> gone;
    bool screeched = false;
    for (Beast& beast : m_beasts)
    {
        Creature* creature = map->GetCreature(beast.guid);
        if (!creature || !creature->IsAlive() || !FindOwned(beast.weaponId) || int32(beast.untilMs - m_totalMs) <= 0)
        {
            gone.push_back(beast.guid);
            continue;
        }
        if (beast.charging)
        {
            UpdateCharge(beast, creature, diff, hits);
            if (!beast.charging && beast.kind == BEAST_SPECTRAL)
                gone.push_back(beast.guid);
            continue;
        }
        screeched |= ActBeast(beast, creature, diff, mauls);
    }

    for (BeastHit const& hit : hits)
    {
        Creature* beast = map->GetCreature(hit.beast);
        Creature* victim = map->GetCreature(hit.victim);
        OwnedWeapon const* owned = FindOwned(hit.weaponId);
        SpellEntry const* spellInfo = owned ? sSpellMgr.GetSpellEntry(owned->weapon.spellId) : nullptr;
        if (spellInfo && beast && victim)
            DealSpellHit(spellInfo, victim, GetBeastHitDamage(*owned) * hit.share, beast);
    }

    for (ObjectGuid const& guid : mauls)
    {
        auto beast = std::find_if(m_beasts.begin(), m_beasts.end(), [&guid](Beast const& entry) { return entry.guid == guid; });
        Creature* bear = map->GetCreature(guid);
        OwnedWeapon const* owned = beast != m_beasts.end() ? FindOwned(beast->weaponId) : nullptr;
        if (!bear || !bear->IsAlive() || !owned)
            continue;
        float const pace = GetWeaponCooldown(*owned) / float(std::max(1u, owned->weapon.cooldownMs));
        int32 const next = Maul(bear, *owned) ? int32(BONK_BEAR_MAUL_MS * pace) : BONK_BEAR_IDLE_MS;
        beast = std::find_if(m_beasts.begin(), m_beasts.end(), [&guid](Beast const& entry) { return entry.guid == guid; });
        if (beast != m_beasts.end())
            beast->actMs = next;
    }

    RemoveBeasts(gone);
    UpdateScreech(diff, screeched || !gone.empty());
}

void BonkRun::DressBeasts()
{
    if (m_beasts.empty())
        return;

    Map* map = m_player->GetMap();
    bool const frenzied = IsPetFrenzied();
    float const speed = GetSummonSpeedRate(GetStat(BONK_PASSIVE_MOVE_SPEED));
    for (Beast const& beast : m_beasts)
    {
        if (beast.kind == BEAST_SPECTRAL)
            continue;
        OwnedWeapon const* owned = FindOwned(beast.weaponId);
        Creature* creature = owned ? map->GetCreature(beast.guid) : nullptr;
        if (!creature || !creature->IsAlive())
            continue;

        float const pace = GetWeaponCooldown(*owned) / float(std::max(1u, owned->weapon.cooldownMs));
        float const bite = GetBeastHitDamage(*owned) * BONK_WOLF_ATTACK_MS / float(std::max(500u, owned->weapon.tickMs));
        float const base = beast.kind == BEAST_BOAR ? BONK_BOAR_SCALE : beast.kind == BEAST_BEAR ? BONK_BEAR_SCALE : BONK_WYVERN_SCALE;
        float const scale = GetSummonLookScale(*owned, base);
        if (creature->GetBaseRunSpeedRate() != speed)
            creature->SetBaseSpeedRate(MOVE_RUN, speed);
        creature->SetBaseWeaponDamage(BASE_ATTACK, MINDAMAGE, bite * 0.9f);
        creature->SetBaseWeaponDamage(BASE_ATTACK, MAXDAMAGE, bite * 1.1f);
        creature->UpdateDamagePhysical(BASE_ATTACK);
        if (beast.kind == BEAST_BEAR)
        {
            creature->SetAttackTime(BASE_ATTACK, BONK_BEAST_NO_SWING_MS);
            creature->ResetAttackTimer(BASE_ATTACK);
        }
        else
            creature->SetAttackTime(BASE_ATTACK, std::max(300u, uint32(BONK_WOLF_ATTACK_MS * pace)));
        if (creature->GetObjectScale() != scale)
        {
            creature->SetObjectScale(scale);
            creature->UpdateModelData();
        }
        if (frenzied && !creature->HasAura(BONK_SPELL_BESTIAL_WRATH))
            creature->AddAura(BONK_SPELL_BESTIAL_WRATH);
        else if (!frenzied && creature->HasAura(BONK_SPELL_BESTIAL_WRATH))
            creature->RemoveAurasDueToSpell(BONK_SPELL_BESTIAL_WRATH);
    }
}

void BonkRun::DespawnBeasts()
{
    if (Map* map = m_player->FindMap())
        for (Beast const& beast : m_beasts)
            if (Creature* creature = map->GetCreature(beast.guid))
                creature->DespawnOrUnsummon();
    m_beasts.clear();
    m_screeching = false;
}

bool BonkRun::IsBeast(ObjectGuid const& guid) const
{
    return std::any_of(m_beasts.begin(), m_beasts.end(), [&guid](Beast const& beast) { return beast.guid == guid; });
}

std::vector<Creature*> BonkRun::GetAllPets(bool frenziedOnly) const
{
    std::vector<Creature*> pets;
    Map* map = m_player->FindMap();
    if (!map)
        return pets;

    auto add = [map, &pets](ObjectGuid const& guid)
    {
        Creature* pet = map->GetCreature(guid);
        if (pet && pet->IsAlive())
            pets.push_back(pet);
    };
    for (OwnedWeapon const& owned : m_weapons)
        if (!frenziedOnly || owned.weapon.behavior == BONK_BEHAVIOR_PET)
            for (ObjectGuid const& guid : owned.pets)
                add(guid);
    for (Snake const& released : m_snakes)
        add(released.guid);
    for (Beast const& beast : m_beasts)
        if (!frenziedOnly || beast.kind != BEAST_SPECTRAL)
            add(beast.guid);
    return pets;
}

bool BonkRun::IsScreeching() const
{
    return std::any_of(m_beasts.begin(), m_beasts.end(), [](Beast const& beast) { return beast.kind == BEAST_WYVERN && beast.acted; });
}

void BonkRun::UpdateScreech(uint32 diff, bool force)
{
    m_screechTimer += diff;
    if (!force && m_screechTimer < BONK_SCREECH_CHECK_MS)
        return;
    m_screechTimer = 0;

    bool const screeching = IsScreeching();
    SpellEntry const* buff = sSpellMgr.GetSpellEntry(BONK_SPELL_SCREECH_BUFF);
    if ((!screeching && !m_screeching) || !buff)
        return;
    m_screeching = screeching;

    for (Creature* pet : GetAllPets(false))
    {
        bool const buffed = pet->HasAura(BONK_SPELL_SCREECH_BUFF);
        if (!screeching && buffed)
            pet->RemoveAurasDueToSpell(BONK_SPELL_SCREECH_BUFF);
        if (!screeching || buffed)
            continue;

        SpellAuraHolder* holder = CreateSpellAuraHolder(buff, pet, m_player, m_player);
        Aura* aura = CreateAura(buff, EFFECT_INDEX_0, nullptr, holder, pet, m_player);
        aura->SetPositive(true);
        holder->AddAura(aura, EFFECT_INDEX_0);
        holder->SetPermanent(true);
        holder->SetAuraMaxDuration(-1);
        holder->SetAuraDuration(-1);
        pet->AddSpellAuraHolder(holder);
    }
}

void BonkRun::PulsePlague(uint32 diff)
{
    m_plagueTimer -= int32(diff);
    if (m_plagueTimer > 0)
        return;
    m_plagueTimer = BONK_PLAGUE_PULSE_MS;

    Map* map = m_player->GetMap();
    for (OwnedWeapon const& owned : m_weapons)
    {
        PendingKeyword poison;
        if (!owned.weapon.HasFlag(BONK_WEAPON_FLAG_PLAGUE) || owned.pets.empty() || !GetWeaponKeyword(owned, poison))
            continue;
        float const radius = GetWeaponRadius(owned);
        for (ObjectGuid const& guid : owned.pets)
        {
            Creature* pet = map->GetCreature(guid);
            if (!pet || !pet->IsAlive())
                continue;
            for (Creature* mob : GetMobsNear(pet->GetPositionX(), pet->GetPositionY(), radius))
                ApplyKeyword(mob, poison.keyword, poison.stacks, poison.durationMs, poison.damageScale, poison.spellId);
        }
    }
}

void BonkRun::FeedFrenzy()
{
    if (!IsPetFrenzied() || std::none_of(m_weapons.begin(), m_weapons.end(), [](OwnedWeapon const& owned)
        {
            return owned.weapon.HasFlag(BONK_WEAPON_FLAG_FEEDING_FRENZY);
        }))
        return;

    uint32 const now = WorldTimer::getMSTime();
    uint32 const cap = now + BONK_FEEDING_FRENZY_MAX_MS;
    uint32 const fed = m_petFrenzyUntil + BONK_FEEDING_FRENZY_MS;
    uint32 const until = int32(fed - cap) > 0 ? cap : fed;
    if (int32(until - m_petFrenzyUntil) <= 0)
        return;
    m_petFrenzyUntil = until;

    if (WorldTimer::getMSTimeDiff(m_frenzyGlowMs, now) < BONK_FRENZY_GLOW_GAP_MS)
        return;
    m_frenzyGlowMs = now;
    GlowFrenzy();
}

void BonkRun::GlowFrenzy()
{
    for (Creature* pet : GetAllPets(true))
    {
        pet->SendPlaySpellVisualKit(BONK_KIT_FRENZY_GLOW);
        if (SpellAuraHolder* holder = pet->GetSpellAuraHolder(BONK_SPELL_BESTIAL_WRATH))
            holder->RefreshHolder();
    }
}

void BonkRun::OnWeaponDamage(uint32 damage)
{
    if (m_state != STATE_RUNNING || !m_player->IsAlive())
        return;

    float const chance = GetPassiveTotal(BONK_PASSIVE_LEECH);
    if (chance <= 0.0f || !damage || !roll_chance_f(chance))
        return;

    float const maxHealth = float(m_player->GetMaxHealth());
    float const capPercent = chance * BONK_LEECH_CAP_PER_CHANCE;
    m_leechCarry = std::min(maxHealth * capPercent / 100.0f, m_leechCarry + maxHealth * BONK_LEECH_HEAL / 100.0f);
}

void BonkRun::AddWeapon(BonkWeapon const& weapon)
{
    OwnedWeapon owned;
    owned.weapon = weapon;
    ApplyMasteryTo(owned.weapon);
    owned.timer = 500;
    m_weapons.push_back(owned);
    RebuildWeaponSpells();
    if (weapon.IsBlink() || weapon.IsArcaneMissiles())
        m_player->GetBonkPlayer()->SendDash();
}

void BonkRun::ApplyPassives()
{
    m_player->RemoveAurasDueToSpell(BONK_SPELL_MOVE_SPEED);
    if (int32 const speed = int32(GetStat(BONK_PASSIVE_MOVE_SPEED)))
        m_player->CastCustomSpell(m_player, BONK_SPELL_MOVE_SPEED, speed, {}, {}, true);

    m_player->RemoveAurasDueToSpell(BONK_SPELL_MAX_HEALTH);
    if (int32 const health = int32(GetPassiveTotal(BONK_PASSIVE_MAX_HEALTH) + GetArmorOverflowHealth()))
        m_player->CastCustomSpell(m_player, BONK_SPELL_MAX_HEALTH, health, {}, {}, true);

    m_player->RemoveAurasDueToSpell(BONK_SPELL_DODGE);
    m_player->UpdateDodgePercentage();

    SendStats();
}

void BonkRun::SendRunXp() const
{
    uint32 const maxLevel = sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL);
    uint32 const xp = m_player->GetLevel() >= maxLevel ? m_overcapXp : m_player->GetUInt32Value(PLAYER_XP);
    m_player->GetBonkPlayer()->SendAddon("runxp~" + std::to_string(m_player->GetLevel() + m_overcapLevel) + "~" + std::to_string(xp) + "~" +
        std::to_string(GetNextLevelXp()) + "~" + std::to_string(m_orbsSinceXpSent));
    m_orbsSinceXpSent = 0;
}

void BonkRun::UpdateRunXp(bool levelled)
{
    uint32 const now = WorldTimer::getMSTime();
    if (levelled || WorldTimer::getMSTimeDiff(m_runXpSentAt, now) >= BONK_RUN_XP_SEND_MS)
    {
        SendRunXp();
        m_runXpSentAt = now;
        m_runXpDirty = false;
    }
    else
        m_runXpDirty = true;
}

uint32 BonkRun::GetNextLevelXp() const
{
    uint32 const maxLevel = sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL);
    if (m_player->GetLevel() < maxLevel)
        return m_player->GetUInt32Value(PLAYER_NEXT_LEVEL_XP);
    return sObjectMgr.GetXPForLevel(maxLevel - 1);
}

bool BonkRun::IsOwnedOffer(Offer const& offer) const
{
    if (offer.isCard)
        return std::any_of(m_cards.begin(), m_cards.end(), [&offer](OwnedCard const& held) { return held.card && held.card->id == offer.id; });
    if (offer.isWeapon)
        return FindOwned(offer.id) != nullptr;
    auto rank = m_passiveRanks.find(offer.id);
    return rank != m_passiveRanks.end() && rank->second > 0;
}

bool BonkRun::CanBanish(Offer const& offer) const
{
    return offer.step == BONK_STEP_EVOLVE || !IsOwnedOffer(offer);
}

void BonkRun::GiveStartLevels()
{
    uint32 const levels = uint32(GetUtility(BONK_UTILITY_START_LEVEL));
    for (uint32 i = 0; i < levels; ++i)
    {
        uint32 const next = GetNextLevelXp();
        uint32 const have = m_player->GetUInt32Value(PLAYER_XP);
        if (!next || next <= have)
            break;
        GiveExperience(next - have);
    }
}

void BonkRun::GiveCollectedExperience(uint32 xp)
{
    uint32 const next = GetNextLevelXp();
    GiveExperience(xp);
    if (next)
        BonkCoop::ShareExperience(m_player, float(xp) / float(next));
}

void BonkRun::ShareExperience(float levelFraction)
{
    if (m_state != STATE_RUNNING && m_state != STATE_PORTAL)
        return;
    GiveExperience(uint32(std::ceil(GetNextLevelXp() * levelFraction)));
}

void BonkRun::GiveExperience(uint32 xp)
{
    uint32 const maxLevel = sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL);
    uint32 level = m_player->GetLevel();
    if (!xp)
        return;

    uint32 const health = m_player->GetHealth();

    if (level >= maxLevel)
    {
        uint32 const next = GetNextLevelXp();
        if (!next)
            return;
        uint32 const startOvercap = m_overcapLevel;
        m_overcapXp += xp;
        while (m_overcapXp >= next)
        {
            m_overcapXp -= next;
            ++m_overcapLevel;
            ++m_pendingLevels;
            if (!m_sandbox)
                sLog.Player(m_player->GetSession(), LOG_LEVELUP, LOG_LVL_BASIC,
                    "Bonk level: %s reaches level %u at run minute %.2f (stage %u, %s, %s), %u kills, class %u, +%g%% experience",
                    m_player->GetName(), level + m_overcapLevel, GetRunMinutes(), m_stage, m_map.key.c_str(), m_tier.key.c_str(), m_kills,
                    uint32(m_player->GetClass()), GetPassiveTotal(BONK_PASSIVE_GROWTH));
        }
        if (m_overcapLevel != startOvercap && m_player->IsAlive())
        {
            uint32 const heal = ReduceHealing(uint32(m_player->GetMaxHealth() * BONK_LEVEL_UP_HEAL * (m_overcapLevel - startOvercap)));
            m_player->SetHealth(std::min(health + heal, m_player->GetMaxHealth()));
        }
        UpdateRunXp(m_overcapLevel != startOvercap);
        if (m_overcapLevel != startOvercap)
            OfferLevelsBeforeDing();
        return;
    }

    uint32 const startLevel = level;
    uint32 const startMaxHealth = m_player->GetMaxHealth();
    uint32 current = m_player->GetUInt32Value(PLAYER_XP) + xp;
    uint32 next = m_player->GetUInt32Value(PLAYER_NEXT_LEVEL_XP);
    while (current >= next && level < maxLevel)
    {
        current -= next;
        m_player->GiveLevel(level + 1);
        level = m_player->GetLevel();
        next = m_player->GetUInt32Value(PLAYER_NEXT_LEVEL_XP);
    }
    m_player->SetUInt32Value(PLAYER_XP, level >= maxLevel ? 0 : current);

    for (uint32 reached = startLevel + 1; !m_sandbox && reached <= level; ++reached)
        sLog.Player(m_player->GetSession(), LOG_LEVELUP, LOG_LVL_BASIC,
            "Bonk level: %s reaches level %u at run minute %.2f (stage %u, %s, %s), %u kills, class %u, +%g%% experience",
            m_player->GetName(), reached, GetRunMinutes(), m_stage, m_map.key.c_str(), m_tier.key.c_str(), m_kills,
            uint32(m_player->GetClass()), GetPassiveTotal(BONK_PASSIVE_GROWTH));

    if (level != startLevel && m_player->IsAlive())
    {
        uint32 const gained = m_player->GetMaxHealth() > startMaxHealth ? m_player->GetMaxHealth() - startMaxHealth : 0;
        uint32 const heal = ReduceHealing(uint32(m_player->GetMaxHealth() * BONK_LEVEL_UP_HEAL * (level - startLevel)));
        m_player->SetHealth(std::min(health + gained + heal, m_player->GetMaxHealth()));
    }
    UpdateRunXp(level != startLevel);
    if (level != startLevel)
        OfferLevelsBeforeDing();
}

void BonkRun::OfferLevelsBeforeDing()
{
    m_levelPushPending = true;
    if (m_state == STATE_RUNNING || m_state == STATE_PORTAL)
        UpdateOffers();
}

void BonkRun::DropOrb(Creature const* mob, uint32 experience, BonkOrb colour)
{
    if (!experience)
        return;

    Map* map = m_player->GetMap();
    while (m_orbs.size() >= BONK_MAX_ORBS)
    {
        auto victim = m_orbs.end();
        for (auto itr = m_orbs.begin(); itr != m_orbs.end(); ++itr)
            if (!itr->pulled && (victim == m_orbs.end() || itr->colour < victim->colour))
                victim = itr;
        if (victim == m_orbs.end())
            victim = m_orbs.begin();
        Orb const gone = *victim;
        m_orbs.erase(victim);
        if (Creature* creature = map->GetCreature(gone.guid))
            VanishOrb(creature);
    }

    SpawnOrb(mob->GetPositionX(), mob->GetPositionY(), mob->GetPositionZ(), experience, colour);
}

bool BonkRun::SpawnOrb(float x, float y, float z, uint32 experience, BonkOrb colour)
{
    float const ground = m_player->GetMap()->GetHeight(x, y, z + BONK_ORB_GROUND_SEARCH, true, 2.0f * BONK_ORB_GROUND_SEARCH);
    if (ground > INVALID_HEIGHT && ground > z)
        z = ground;
    Creature* orb = m_player->SummonCreature(GetOrbEntry(colour), x, y, z, 0.0f,
        TEMPSUMMON_TIMED_DESPAWN, BONK_ORB_LIFETIME_MS, false, 0, &InstallOrbAI);
    if (!orb)
    {
        GiveExperience(experience);
        return false;
    }

    orb->MonsterMoveWithSpeed(x, y, z + BONK_ORB_LIFT, -10.0f, BONK_ORB_RISE_SPEED, MOVE_FLY_MODE);

    Orb dropped;
    dropped.guid = orb->GetObjectGuid();
    dropped.experience = experience;
    dropped.colour = colour;
    dropped.droppedMs = WorldTimer::getMSTime();
    m_orbs.push_back(dropped);
    return true;
}

void BonkRun::UpdateOrbMerges(uint32 diff)
{
    for (size_t i = 0; i < m_orbMerges.size();)
    {
        m_orbMerges[i].leftMs -= int32(diff);
        if (m_orbMerges[i].leftMs > 0)
        {
            ++i;
            continue;
        }
        OrbMerge const merge = m_orbMerges[i];
        m_orbMerges.erase(m_orbMerges.begin() + i);
        FinishOrbMerge(merge);
    }

    m_orbMergeTimer += diff;
    if (m_orbMergeTimer < BONK_ORB_MERGE_CHECK_MS)
        return;
    m_orbMergeTimer = 0;
    MergeOrbs();
}

void BonkRun::MergeOrbs()
{
    Map* map = m_player->GetMap();
    uint32 const now = WorldTimer::getMSTime();
    for (uint8 colour = BONK_ORB_BLUE; colour <= BONK_ORB_PURPLE; ++colour)
    {
        struct Candidate
        {
            size_t index;
            Creature* creature;
            bool taken;
        };
        std::vector<Candidate> candidates;
        for (size_t i = 0; i < m_orbs.size(); ++i)
        {
            Orb const& orb = m_orbs[i];
            if (orb.colour != colour || orb.pulled || orb.mergeId || WorldTimer::getMSTimeDiff(orb.droppedMs, now) < BONK_ORB_MERGE_MIN_AGE_MS)
                continue;
            if (Creature* creature = map->GetCreature(orb.guid))
                candidates.push_back({ i, creature, false });
        }
        if (candidates.size() < BONK_ORB_MERGE_COUNT)
            continue;

        for (Candidate& anchor : candidates)
        {
            if (anchor.taken)
                continue;
            std::vector<std::pair<float, Candidate*>> nearby;
            for (Candidate& other : candidates)
                if (&other != &anchor && !other.taken)
                {
                    float const distance = anchor.creature->GetDistance2d(other.creature->GetPositionX(), other.creature->GetPositionY(), SizeFactor::None);
                    if (distance <= BONK_ORB_MERGE_RANGE)
                        nearby.push_back({ distance, &other });
                }
            if (nearby.size() + 1 < BONK_ORB_MERGE_COUNT)
                continue;
            std::sort(nearby.begin(), nearby.end(), [](std::pair<float, Candidate*> const& a, std::pair<float, Candidate*> const& b) { return a.first < b.first; });

            OrbMerge merge;
            merge.id = ++m_lastOrbMergeId;
            merge.anchor = m_orbs[anchor.index].guid;
            merge.colour = BonkOrb(std::min<uint8>(colour + 1, BONK_ORB_PURPLE));
            merge.leftMs = int32(BONK_ORB_MERGE_SLIDE_MS);
            anchor.taken = true;
            m_orbs[anchor.index].mergeId = merge.id;
            float const x = anchor.creature->GetPositionX(), y = anchor.creature->GetPositionY(), z = anchor.creature->GetPositionZ();
            for (size_t i = 0; i + 1 < BONK_ORB_MERGE_COUNT; ++i)
            {
                Candidate& other = *nearby[i].second;
                other.taken = true;
                m_orbs[other.index].mergeId = merge.id;
                other.creature->MonsterMoveWithSpeed(x, y, z, -10.0f, BONK_ORB_MERGE_SLIDE_SPEED, MOVE_FLY_MODE);
            }
            m_orbMerges.push_back(merge);
        }
    }
}

void BonkRun::FinishOrbMerge(OrbMerge const& merge)
{
    Map* map = m_player->GetMap();
    auto anchor = std::find_if(m_orbs.begin(), m_orbs.end(), [&](Orb const& orb) { return orb.guid == merge.anchor; });
    Creature* anchorCreature = anchor != m_orbs.end() ? map->GetCreature(anchor->guid) : nullptr;
    if (!anchorCreature || anchor->pulled || anchor->mergeId != merge.id)
    {
        for (Orb& orb : m_orbs)
            if (orb.mergeId == merge.id)
                orb.mergeId = 0;
        return;
    }

    uint32 experience = 0;
    for (auto itr = m_orbs.begin(); itr != m_orbs.end();)
    {
        if (itr->mergeId != merge.id || itr->guid == merge.anchor || itr->pulled)
        {
            if (itr->mergeId == merge.id && itr->guid != merge.anchor)
                itr->mergeId = 0;
            ++itr;
            continue;
        }
        experience += itr->experience;
        if (Creature* creature = map->GetCreature(itr->guid))
            VanishOrb(creature);
        itr = m_orbs.erase(itr);
    }

    anchor = std::find_if(m_orbs.begin(), m_orbs.end(), [&](Orb const& orb) { return orb.guid == merge.anchor; });
    anchor->experience += experience;
    anchor->colour = merge.colour;
    anchor->mergeId = 0;
    anchor->droppedMs = WorldTimer::getMSTime();
    if (CreatureInfo const* info = sObjectMgr.GetCreatureTemplate(GetOrbEntry(merge.colour)))
    {
        anchorCreature->SetDisplayId(info->display_id[0]);
        anchorCreature->SetObjectScale(info->display_scale[0]);
    }
}

void BonkRun::UpdateOrbs(uint32 diff)
{
    UpdateOrbMerges(diff);

    m_orbTimer += diff;
    if (m_orbTimer < BONK_ORB_CHECK_MS || m_orbs.empty())
        return;
    m_orbTimer = 0;

    float const magnet = GetMagnetRange();
    Map* map = m_player->GetMap();
    float heroX, heroY, heroZ;
    GetHeroChestPositionNow(m_player, heroX, heroY, heroZ);
    uint32 const now = WorldTimer::getMSTime();
    if (!m_orbSweepAtMs || now - m_orbSweepAtMs > 2 * BONK_ORB_CHECK_MS)
        m_orbSweepFrom = Position(heroX, heroY, heroZ, 0.0f);
    uint32 collected = 0;
    for (auto itr = m_orbs.begin(); itr != m_orbs.end();)
    {
        Creature* orb = map->GetCreature(itr->guid);
        float const distance = orb ? GetHeroDistance(m_player, heroX, heroY, heroZ, orb) : 0.0f;

        if (!orb || GetHeroSweepDistance(m_player, m_orbSweepFrom, heroX, heroY, heroZ, orb) <= BONK_ORB_PICKUP_RANGE)
        {
            collected += itr->experience;
            ++m_orbsSinceXpSent;
            if (orb)
                VanishOrb(orb);
            itr = m_orbs.erase(itr);
            continue;
        }

        float const x = heroX;
        float const y = heroY;
        float const z = heroZ;
        if (!itr->pulled ? distance <= magnet :
            (x - itr->aimX) * (x - itr->aimX) + (y - itr->aimY) * (y - itr->aimY) + (z - itr->aimZ) * (z - itr->aimZ) >
                BONK_ORB_REAIM_DISTANCE * BONK_ORB_REAIM_DISTANCE)
        {
            itr->pulled = true;
            itr->aimX = x;
            itr->aimY = y;
            itr->aimZ = z;
            orb->MonsterMoveWithSpeed(x, y, z, -10.0f, BONK_ORB_FLY_SPEED, MOVE_FLY_MODE);
        }
        ++itr;
    }
    m_orbSweepFrom = Position(heroX, heroY, heroZ, 0.0f);
    m_orbSweepAtMs = now;

    if (collected)
        GiveCollectedExperience(collected);
}

void BonkRun::PullAllOrbs()
{
    Map* map = m_player->GetMap();
    float heroX, heroY, heroZ;
    GetHeroChestPositionNow(m_player, heroX, heroY, heroZ);
    for (Orb& orb : m_orbs)
    {
        if (orb.pulled)
            continue;
        if (Creature* creature = map->GetCreature(orb.guid))
        {
            orb.pulled = true;
            orb.aimX = heroX;
            orb.aimY = heroY;
            orb.aimZ = heroZ;
            creature->MonsterMoveWithSpeed(orb.aimX, orb.aimY, orb.aimZ, -10.0f, BONK_ORB_FLY_SPEED, MOVE_FLY_MODE);
        }
    }
}

void BonkRun::PullAllCurrency()
{
    PullAllOrbs();
    CollectAllShards();
    CollectAllValorPouches();
}

void BonkRun::CollectAllValorPouches()
{
    Map* map = m_player->GetMap();
    std::vector<BonkPowerup const*> pouches;
    for (auto itr = m_powerups.begin(); itr != m_powerups.end();)
    {
        GameObject* go = map->GetGameObject(*itr);
        auto powerup = go ? std::find_if(sBonkMgr.GetPowerups().begin(), sBonkMgr.GetPowerups().end(),
            [go](BonkPowerup const& p) { return p.entry == go->GetEntry(); }) : sBonkMgr.GetPowerups().end();
        if (powerup == sBonkMgr.GetPowerups().end() || powerup->effect != BONK_POWERUP_VALOR || !go->isSpawned())
        {
            ++itr;
            continue;
        }
        ForgetPowerupObjective(*itr);
        go->Delete();
        itr = m_powerups.erase(itr);
        pouches.push_back(&*powerup);
    }
    for (BonkPowerup const* pouch : pouches)
        UsePowerup(*pouch);
}

void BonkRun::CollectAllOrbs()
{
    uint32 collected = 0;
    for (Orb const& orb : m_orbs)
        collected += orb.experience;
    m_orbsSinceXpSent += uint32(m_orbs.size());
    DespawnOrbs();
    if (collected)
        GiveCollectedExperience(collected);
}

Creature* BonkRun::SummonCaravan(uint32 entry, float x, float y, float z, float orientation)
{
    Creature* caravan = m_player->SummonCreature(entry, x, y, z, orientation, TEMPSUMMON_TIMED_DESPAWN, 10 * MINUTE * IN_MILLISECONDS, false, 0,
        &InstallCaravanAI);
    if (!caravan)
        return nullptr;
    m_lastPlateId = m_lastPlateId % BONK_MAX_PLATE_ID + 1;
    caravan->SetPlateId(m_lastPlateId);
    caravan->ForceValuesUpdateAtIndex(UNIT_FIELD_HEALTH);
    caravan->ForceValuesUpdateAtIndex(UNIT_FIELD_MAXHEALTH);
    m_player->GetBonkPlayer()->SendAddon("ally~" + std::string(caravan->GetName()) + "~" + std::to_string(caravan->GetPlateId()) + "~" +
        BonkMobAI::GetUnitGuidText(caravan));
    return caravan;
}

bool BonkRun::FindLyingOrb(float heading, float spread, float minDistance, float maxDistance, float& x, float& y, float& z) const
{
    std::vector<Creature const*> lying;
    Map* map = m_player->GetMap();
    for (Orb const& orb : m_orbs)
        if (!orb.pulled && !orb.mergeId)
            if (Creature const* creature = map->GetCreature(orb.guid))
            {
                float const distance = m_player->GetDistance2d(creature);
                float const offAngle = std::fabs(std::remainder(m_player->GetAngle(creature) - heading, 2.0f * M_PI_F));
                if (distance >= minDistance && distance <= maxDistance && offAngle <= spread)
                    lying.push_back(creature);
            }
    if (lying.empty())
        return false;
    Creature const* pick = lying[urand(0, uint32(lying.size()) - 1)];
    x = pick->GetPositionX();
    y = pick->GetPositionY();
    z = pick->GetPositionZ();
    return true;
}

uint32 BonkRun::StealOrbs(float x, float y, float range)
{
    std::set<ObjectGuid> anchors;
    for (OrbMerge const& merge : m_orbMerges)
        anchors.insert(merge.anchor);
    uint32 experience = 0;
    Map* map = m_player->GetMap();
    for (auto itr = m_orbs.begin(); itr != m_orbs.end();)
    {
        Creature* creature = map->GetCreature(itr->guid);
        if (!creature || itr->pulled || itr->mergeId || anchors.count(itr->guid) || creature->GetDistance2d(x, y) > range)
        {
            ++itr;
            continue;
        }
        experience += itr->experience;
        VanishOrb(creature);
        itr = m_orbs.erase(itr);
    }
    return experience;
}

void BonkRun::DespawnOrbs()
{
    if (Map* map = m_player->FindMap())
        for (Orb const& orb : m_orbs)
            if (Creature* creature = map->GetCreature(orb.guid))
                VanishOrb(creature);
    m_orbs.clear();
    m_orbMerges.clear();
}

void BonkRun::DropShards(Creature const* mob, BonkSpawnKind kind)
{
    uint32 count = 0;
    switch (kind)
    {
        case BONK_SPAWN_NORMAL:   count = roll_chance_f(m_tier.shardChanceNormal) ? 1 : 0; break;
        case BONK_SPAWN_ELITE:    count = roll_chance_f(m_tier.shardChanceElite) ? 1 : 0; break;
        case BONK_SPAWN_MINIBOSS: count = m_tier.shardsMiniboss; break;
        case BONK_SPAWN_BOSS:     count = m_tier.shardsBoss; break;
        default: break;
    }
    uint32 const scaled = uint32(count * (GetBonkMapRewardPercent(m_map.type) + GetGreedRewardPercent()) * GetAmbitionReward(kind) * m_levelRow.reward);
    count = scaled / 100 + (roll_chance_u(scaled % 100) ? 1 : 0);
    if (!count)
        return;

    float const angle = frand(0.0f, 2.0f * M_PI_F);
    float x = mob->GetPositionX() + BONK_SHARD_SCATTER * std::cos(angle);
    float y = mob->GetPositionY() + BONK_SHARD_SCATTER * std::sin(angle);
    float z = m_player->GetMap()->GetHeight(x, y, mob->GetPositionZ() + 2.0f, true, 10.0f);
    if (z <= INVALID_HEIGHT)
    {
        x = mob->GetPositionX();
        y = mob->GetPositionY();
        z = mob->GetPositionZ();
    }

    GameObject* go = PlaceObject(BONK_GO_SHARD, x, y, z, BONK_OBJECT_LIFETIME_SEC);
    if (!go)
    {
        GiveShards(count);
        return;
    }
    ShowFromFar(go);

    ShardDrop drop;
    drop.guid = go->GetObjectGuid();
    drop.count = count;
    drop.marker = PlaceMarker(go, BONK_SPELL_TICKET_POOL, BONK_DROP_POOL_SCALE);
    drop.sparkle = PlaceMarker(go, BONK_SPELL_FEET_SPARKLE, 1.0f);
    m_shardDrops.push_back(drop);
    m_player->GetBonkPlayer()->Notify(count == 1 ? BONK_STRING_DARKMOON_TICKET_DROPPED : BONK_STRING_TICKETS_DROPPED, { FormatBonkShards(count, GetLocale()) });
}

void BonkRun::UpdateShards(uint32 diff)
{
    m_shardTimer += diff;
    if (m_shardTimer < BONK_SHARD_CHECK_MS || (m_shardDrops.empty() && m_glyphDrops.empty()))
        return;
    m_shardTimer = 0;

    Map* map = m_player->GetMap();
    float heroX, heroY, heroZ;
    GetHeroPositionNow(m_player, heroX, heroY, heroZ);
    for (auto itr = m_glyphDrops.begin(); itr != m_glyphDrops.end();)
    {
        GameObject* go = map->GetGameObject(itr->guid);
        if (go && go->isSpawned() && GetHeroDistance(m_player, heroX, heroY, heroZ, go) > BONK_SHARD_PICKUP_RANGE)
        {
            ++itr;
            continue;
        }
        RemovePickup(itr->guid, itr->marker, itr->sparkle);
        itr = m_glyphDrops.erase(itr);
        QueueGlyphPick();
    }

    uint32 taken = 0;
    for (auto itr = m_shardDrops.begin(); itr != m_shardDrops.end();)
    {
        GameObject* go = map->GetGameObject(itr->guid);
        if (go && go->isSpawned() && GetHeroDistance(m_player, heroX, heroY, heroZ, go) > BONK_SHARD_PICKUP_RANGE)
        {
            ++itr;
            continue;
        }

        taken += itr->count;
        RemovePickup(itr->guid, itr->marker, itr->sparkle);
        itr = m_shardDrops.erase(itr);
    }

    if (taken)
        GiveShards(taken);
}

void BonkRun::DropGlyph(Creature const* mob)
{
    if (!CanGainGlyphPick())
        return;

    float const angle = frand(0.0f, 2.0f * M_PI_F);
    float x = mob->GetPositionX() + BONK_SHARD_SCATTER * std::cos(angle);
    float y = mob->GetPositionY() + BONK_SHARD_SCATTER * std::sin(angle);
    float z = m_player->GetMap()->GetHeight(x, y, mob->GetPositionZ() + 2.0f, true, 10.0f);
    if (z <= INVALID_HEIGHT)
    {
        x = mob->GetPositionX();
        y = mob->GetPositionY();
        z = mob->GetPositionZ();
    }

    GameObject* go = PlaceObject(BONK_GO_GLYPH, x, y, z, BONK_OBJECT_LIFETIME_SEC);
    if (!go)
    {
        QueueGlyphPick();
        return;
    }
    ShowFromFar(go);

    GlyphDrop drop;
    drop.guid = go->GetObjectGuid();
    drop.marker = PlaceMarker(go, BONK_SPELL_GLYPH_POOL, BONK_DROP_POOL_SCALE);
    drop.sparkle = PlaceMarker(go, BONK_SPELL_FEET_SPARKLE, 1.0f);
    m_glyphDrops.push_back(drop);
    m_player->GetBonkPlayer()->Notify(BONK_STRING_GLYPH_DROPPED_PICK_IT_UP);
}

void BonkRun::CollectAllGlyphs()
{
    for (GlyphDrop const& drop : m_glyphDrops)
        RemovePickup(drop.guid, drop.marker, drop.sparkle);
    uint32 const count = uint32(m_glyphDrops.size());
    m_glyphDrops.clear();
    for (uint32 i = 0; i < count; ++i)
        QueueGlyphPick();
}

void BonkRun::CollectAllShards()
{
    uint32 taken = 0;
    for (ShardDrop const& drop : m_shardDrops)
    {
        taken += drop.count;
        RemovePickup(drop.guid, drop.marker, drop.sparkle);
    }
    m_shardDrops.clear();

    if (taken)
        GiveShards(taken);
}

void BonkRun::DropHeal(Creature const* mob, BonkSpawnKind kind)
{
    float chance = 100.0f;
    if (kind == BONK_SPAWN_NORMAL)
        chance = BONK_HEAL_CHANCE_NORMAL;
    else if (kind == BONK_SPAWN_ELITE)
        chance = BONK_HEAL_CHANCE_ELITE;
    chance *= 1.0f + GetCardTotal(BONK_CARD_JERKY, 0) / 100.0f;
    if (m_healDrops.size() >= BONK_MAX_HEAL_DROPS || !roll_chance_f(chance))
        return;

    float const angle = frand(0.0f, 2.0f * M_PI_F);
    float x = mob->GetPositionX() - BONK_SHARD_SCATTER * std::cos(angle);
    float y = mob->GetPositionY() - BONK_SHARD_SCATTER * std::sin(angle);
    float z = m_player->GetMap()->GetHeight(x, y, mob->GetPositionZ() + 2.0f, true, 10.0f);
    if (z <= INVALID_HEIGHT)
    {
        x = mob->GetPositionX();
        y = mob->GetPositionY();
        z = mob->GetPositionZ();
    }

    Creature* leg = m_player->SummonCreature(BONK_HEAL_PICKUP, x, y, z, frand(0.0f, 2.0f * M_PI_F),
        TEMPSUMMON_TIMED_DESPAWN, BONK_ORB_LIFETIME_MS, false, 0, &InstallOrbAI);
    if (!leg)
        return;
    HealDrop drop;
    drop.guid = leg->GetObjectGuid();
    if (Creature* sparkle = m_player->SummonCreature(BONK_HEAL_SPARKLE, x, y, z, 0.0f, TEMPSUMMON_TIMED_DESPAWN, BONK_ORB_LIFETIME_MS, false, 0,
        &InstallOrbAI))
        drop.sparkle = sparkle->GetObjectGuid();
    m_healDrops.push_back(drop);
}

void BonkRun::MoveHealDrop(HealDrop const& drop, Creature* leg, float x, float y, float z)
{
    leg->MonsterMoveWithSpeed(x, y, z, -10.0f, BONK_ORB_FLY_SPEED, MOVE_FLY_MODE);
    if (Creature* sparkle = m_player->GetMap()->GetCreature(drop.sparkle))
    {
        sparkle->NearTeleportTo(leg->GetPositionX(), leg->GetPositionY(), leg->GetPositionZ(), sparkle->GetOrientation());
        sparkle->MonsterMoveWithSpeed(x, y, z, -10.0f, BONK_ORB_FLY_SPEED, MOVE_FLY_MODE);
    }
}

void BonkRun::StopHealDrop(HealDrop const& drop, Creature* leg)
{
    leg->StopMoving();
    if (Creature* sparkle = m_player->GetMap()->GetCreature(drop.sparkle))
        sparkle->NearTeleportTo(leg->GetPositionX(), leg->GetPositionY(), leg->GetPositionZ(), sparkle->GetOrientation());
}

void BonkRun::RemoveHealDrop(HealDrop const& drop, Creature* leg, bool vanish)
{
    Map* map = m_player->FindMap();
    Creature* sparkle = map ? map->GetCreature(drop.sparkle) : nullptr;
    for (Creature* creature : { leg, sparkle })
    {
        if (!creature)
            continue;
        if (vanish)
            VanishOrb(creature);
        else
            creature->DespawnOrUnsummon();
    }
}

void BonkRun::UpdateHeals(uint32 diff)
{
    m_healTimer += diff;
    if (m_healTimer < BONK_HEAL_CHECK_MS || m_healDrops.empty())
        return;
    m_healTimer = 0;

    bool hurt = m_player->IsAlive() && m_player->GetHealth() < m_player->GetMaxHealth();
    float const range = GetMagnetRange();
    Map* map = m_player->GetMap();
    float heroX, heroY, heroZ;
    GetHeroPositionNow(m_player, heroX, heroY, heroZ);
    for (auto itr = m_healDrops.begin(); itr != m_healDrops.end();)
    {
        Creature* leg = map->GetCreature(itr->guid);
        if (!leg)
        {
            RemoveHealDrop(*itr, nullptr, false);
            itr = m_healDrops.erase(itr);
            continue;
        }
        if (!hurt)
        {
            if (itr->pulled)
            {
                itr->pulled = false;
                StopHealDrop(*itr, leg);
            }
            ++itr;
            continue;
        }

        float const distance = GetHeroDistance(m_player, heroX, heroY, heroZ, leg);
        if (distance <= BONK_ORB_PICKUP_RANGE)
        {
            float const amount = BONK_HEAL_AMOUNT * (1.0f + GetCardTotal(BONK_CARD_JERKY, 1) / 100.0f);
            SupportScope scope(*this, { "Spell_Holy_Heal", GetString(BONK_STRING_SUPPORT_HEALING_ORBS) });
            HealHero(ReduceHealing(std::max(1u, uint32(m_player->GetMaxHealth() * amount))), BONK_SPELL_HEAL_TEXT);
            RemoveHealDrop(*itr, leg, true);
            itr = m_healDrops.erase(itr);
            hurt = m_player->GetHealth() < m_player->GetMaxHealth();
            continue;
        }

        float const x = heroX, y = heroY, z = heroZ + 0.5f;
        if (!itr->pulled ? distance <= range :
            (x - itr->aimX) * (x - itr->aimX) + (y - itr->aimY) * (y - itr->aimY) + (z - itr->aimZ) * (z - itr->aimZ) >
                BONK_ORB_REAIM_DISTANCE * BONK_ORB_REAIM_DISTANCE)
        {
            itr->pulled = true;
            itr->aimX = x;
            itr->aimY = y;
            itr->aimZ = z;
            MoveHealDrop(*itr, leg, x, y, z);
        }
        ++itr;
    }
}

void BonkRun::GiveShards(uint32 count)
{
    if (m_sandbox)
        return;
    BonkPlayer* bonk = m_player->GetBonkPlayer();
    bonk->AddShards(count);
    sBonkQuests.OnShards(m_player, count);
    m_shardsEarned += count;
    bonk->SendAddon("shard~" + std::to_string(count) + "~" + std::to_string(bonk->GetShards()));
    bonk->Notify(BONK_STRING_YOU_COLLECTED, { FormatBonkShards(count, GetLocale()) });
    SendState();
}

uint32 BonkRun::PlaceMarker(GameObject const* go, uint32 spellId, float scale)
{
    uint32 const key = NextBonkAuraKey();
    SendBonkAuraAt(m_player, key, spellId, go->GetPositionX(), go->GetPositionY(), go->GetPositionZ(), scale);
    return key;
}

void BonkRun::ShowFromFar(WorldObject* object)
{
    object->SetVisibilityModifier(BONK_DROP_SIGHT);
    object->SetActiveObjectState(true);
}

void BonkRun::RemovePickup(ObjectGuid const& goGuid, uint32 beamKey, uint32 sparkleKey, uint32 coreKey)
{
    for (uint32 key : { beamKey, sparkleKey, coreKey })
        if (key)
            SendBonkAuraGone(m_player, key);
    Map* map = m_player->FindMap();
    if (!map)
        return;
    if (GameObject* go = map->GetGameObject(goGuid))
        go->Delete();
}

float BonkRun::GetGoldMultiplier() const
{
    float const mapBonus = float(GetBonkMapRewardPercent(m_map.type)) - 100.0f;
    return (1.0f + (GetPassiveTotal(BONK_PASSIVE_GREED) + m_bonuses.goldBonus + mapBonus + GetGreedRewardPercent()) / 100.0f) *
        (m_invaderId ? BONK_INVASION_GOLD : IsChallenge() ? BONK_DUNGEON_GOLD : 1.0f) * m_levelRow.reward;
}

void BonkRun::GiveGold(uint32 copper)
{
    copper = uint32(copper * GetGoldMultiplier());
    if (!copper || m_sandbox)
        return;
    m_player->ModifyMoney(int32(copper));
    m_goldEarned += copper;
}

void BonkRun::OnMobKilled(Creature* mob, BonkSpawnKind kind, Unit* killer)
{
    if (m_state != STATE_RUNNING)
        return;
    ShakeOnKill(kind);

    ForgetEnemyAttacks(mob);
    if (kind == BONK_SPAWN_BOSS || kind == BONK_SPAWN_MINIBOSS)
    {
        m_objectives.RemoveFollowing(m_player, mob->GetObjectGuid());
        SpeakBossLine(mob, BONK_BOSS_LINE_DEATH);
    }
    if (mob->GetObjectGuid() == m_bossGuid)
        SendBossMusic(false);

    bool const eventGroup = m_miniEventActive &&
        std::find(m_miniEvent.targets.begin(), m_miniEvent.targets.end(), mob->GetObjectGuid()) != m_miniEvent.targets.end();
    bool const eventPaysChest = eventGroup || (m_miniEventActive && mob->GetObjectGuid() == m_miniEvent.target);
    OnMiniEventTargetKilled(mob);

    BonkRun* rewarded = GetKillRewardRun(killer);
    rewarded->RewardKill(mob, kind, killer, eventGroup);
    auto const arrival = m_bossArrivals.find(mob->GetObjectGuid());
    if (arrival != m_bossArrivals.end())
    {
        sBonkQuests.OnBossFight(m_player, kind, (m_totalMs - arrival->second.ms) / IN_MILLISECONDS, !m_hurt || m_lastHurtMs < arrival->second.ms,
            uint32(m_player->GetHealthPercent()));
        LogBossFight(mob, kind, arrival->second);
        m_bossArrivals.erase(arrival);
    }
    if (kind == BONK_SPAWN_BOSS)
        sBonkQuests.OnBossKilled(m_player, m_tier.id, m_stage, m_elapsedMs / IN_MILLISECONDS);

    if (kind != BONK_SPAWN_NORMAL)
    {
        if (!eventPaysChest && !m_sandbox)
        {
            DropChest(mob, kind);
            DropGear(mob, kind);
        }
        OnAffixedDeath(mob);
        rewarded->SendMobDead(mob, true);
        if (rewarded != this)
            SendMobDead(mob, true);
    }
    else if (IsPlatedMob(mob))
    {
        rewarded->SendMobDead(mob, false);
        if (rewarded != this)
            SendMobDead(mob, false);
    }

    ObjectGuid const hiddenBoss = m_hidden.boss;
    OnHiddenKilled(mob);
    if (kind == BONK_SPAWN_MINIBOSS && !m_sandbox && mob->GetObjectGuid() != hiddenBoss && ++m_glyphMiniBosses == 1)
        DropGlyph(mob);

    if (mob->GetObjectGuid() == m_bossGuid)
    {
        if (m_stoneBoss)
            GiveExperience(GetNextLevelXp());
        if (IsGlyphBossStage())
            DropGlyph(mob);
        OpenPortal(mob);
    }
}

bool BonkRun::IsPlatedMob(Creature const* mob)
{
    BonkMobAI const* ai = dynamic_cast<BonkMobAI const*>(mob->AI());
    return ai && ai->GetPlateKind();
}

BonkRun* BonkRun::GetKillRewardRun(Unit* killer)
{
    Player* player = killer ? killer->GetCharmerOrOwnerPlayerOrPlayerItself() : nullptr;
    if (!player || player == m_player || player->GetMapId() != m_player->GetMapId())
        return this;
    if (!m_player->GetGroup() || !m_player->GetGroup()->IsMember(player->GetObjectGuid()))
        return this;
    BonkRun* run = player->GetBonkPlayer()->GetRun();
    return run && run->m_state == STATE_RUNNING ? run : this;
}

void BonkRun::SendMobDead(Creature const* mob, bool named) const
{
    if (named)
        m_player->GetBonkPlayer()->SendAddon("mobdead~" + m_player->GetBonkPlayer()->GetLocalName(mob) + "~" + std::to_string(mob->GetPlateId()) + "~" +
            BonkMobAI::GetUnitGuidText(mob));
    else
        m_player->GetBonkPlayer()->SendAddon("mobdead~~" + std::to_string(mob->GetPlateId()) + "~" + BonkMobAI::GetUnitGuidText(mob));
}

void BonkRun::RewardKill(Creature* mob, BonkSpawnKind kind, Unit* killer, bool eventGroup)
{
    ++m_kills;
    SpreadFrom(mob);
    SpreadKeywords(mob);
    m_petTimer = BONK_PET_CHECK_MS;
    if (IsSummon(killer))
        FeedFrenzy();
    uint32 const killingSpell = BonkStats::CountKill(m_player->GetObjectGuid(), mob->GetObjectGuid(), kind);
    QueueTrickShot(mob, killingSpell);
    EnrageOnKill(killingSpell);
    QueueChainExecution(mob, killingSpell);
    float heading;
    sBonkQuests.OnKill(m_player, kind, killingSpell, mob->GetCreatureType(), mob->GetEntry(), !GetHeading(heading));
    SoulSiphon(killingSpell);
    if (kind == BONK_SPAWN_MINIBOSS)
        m_masteryXp += uint32(m_tier.masteryXpMiniboss * GetBonkMapRewardPercent(m_map.type) / 100 * GetAmbitionReward(kind));
    else if (kind == BONK_SPAWN_BOSS)
        m_masteryXp += m_tier.masteryXpBoss * GetBonkMapRewardPercent(m_map.type) / 100;
    BonkMobAI const* mobAI = dynamic_cast<BonkMobAI const*>(mob->AI());
    bool const veteran = mobAI && mobAI->IsVeteran();
    float const reward = GetKindReward(kind) * (veteran ? BONK_VETERAN_REWARD : 1.0f) * GetAmbitionReward(kind);
    GiveGold(uint32(m_map.goldPerKill * reward + 0.5f));
    m_cardValorCarry += m_map.coinsPerKill * reward * (1.0f + GetCardTotal(BONK_CARD_VALOR, 0) / 100.0f);
    GainValor(uint32(m_cardValorCarry));
    m_cardValorCarry -= float(uint32(m_cardValorCarry));
    if (HasCard(BONK_CARD_KILL_POWER))
        ++m_cardKills;

    if (kind != BONK_SPAWN_BOSS)
    {
        uint32 const level = m_player->GetLevel() + m_overcapLevel;
        float const overcapSlowdown = 1.0f + float(std::max(0, int32(level) - BONK_OVERCAP_XP_START_LEVEL)) / BONK_OVERCAP_XP_DOUBLING_LEVELS;
        float const killsForLevel = (4.0f + 1.5f * level + 0.05f * level * level) * overcapSlowdown;
        float const growth = 1.0f + GetPassiveTotal(BONK_PASSIVE_GROWTH) / 100.0f;

        float experience = reward;
        if (kind == BONK_SPAWN_ELITE && GetGlyph(BONK_GLYPH_HUNT))
            experience *= GetGlyphValue(BONK_GLYPH_HUNT, 1);
        BonkOrb colour = BONK_ORB_RED;
        if (kind == BONK_SPAWN_NORMAL)
        {
            BonkArchetypeInfo const& archetype = sBonkMgr.GetArchetype(mobAI ? mobAI->GetArchetype() : BONK_ARCHETYPE_GRUNT);
            experience *= archetype.experience;
            colour = veteran ? BONK_ORB_GREEN : archetype.orb;
        }
        uint32 xp = uint32(std::ceil(GetNextLevelXp() / killsForLevel * experience * growth));
        if (IsInMoonwell(mob->GetPositionX(), mob->GetPositionY()))
            xp *= 2;
        DropOrb(mob, xp, colour);
    }
    if (!m_sandbox)
        DropShards(mob, kind);
    DropHeal(mob, kind);
    if (kind == BONK_SPAWN_ELITE && GetGlyph(BONK_GLYPH_SCAVENGER) && !eventGroup)
        SpawnPowerup(mob);
    OnGlyphKill(mob, kind);
}

void BonkRun::SpeakBossLine(Creature const* boss, BonkBossLine line) const
{
    if (!boss)
        return;
    BonkMobAI const* ai = dynamic_cast<BonkMobAI const*>(boss->AI());
    if (!ai || (ai->GetKind() != BONK_SPAWN_BOSS && ai->GetKind() != BONK_SPAWN_MINIBOSS))
        return;
    if (uint32 const soundId = sBonkMgr.PickBossLineSound(boss->GetEntry(), line))
        boss->PlayDirectSound(soundId, m_player);
}

void BonkRun::UpdateBossVoices()
{
    Map* map = m_player->GetMap();
    for (auto& arrival : m_bossArrivals)
    {
        if (arrival.second.spokeFightLine)
            continue;
        Creature* boss = map->GetCreature(arrival.first);
        if (!boss || !boss->IsAlive() || boss->GetHealthPercent() > BONK_BOSS_FIGHT_LINE_HEALTH_PCT)
            continue;
        arrival.second.spokeFightLine = true;
        SpeakBossLine(boss, BONK_BOSS_LINE_FIGHT);
    }
}

void BonkRun::SendBossMusic(bool playing) const
{
    m_player->GetBonkPlayer()->SendAddon(playing ? "music~boss" : "music~stop");
}

BonkRun::OwnedWeapon* BonkRun::FindOwned(uint32 weaponId)
{
    auto itr = std::find_if(m_weapons.begin(), m_weapons.end(), [weaponId](OwnedWeapon const& owned) { return owned.weapon.id == weaponId; });
    return itr != m_weapons.end() ? &*itr : nullptr;
}

BonkRun::OwnedWeapon const* BonkRun::FindOwned(uint32 weaponId) const
{
    auto itr = std::find_if(m_weapons.begin(), m_weapons.end(), [weaponId](OwnedWeapon const& owned) { return owned.weapon.id == weaponId; });
    return itr != m_weapons.end() ? &*itr : nullptr;
}

bool BonkRun::IsEvolutionBanished(uint32 evolvedId) const
{
    Offer probe;
    probe.step = BONK_STEP_EVOLVE;
    probe.evolveTo = evolvedId;
    return m_banished.count(GetBanishKey(probe)) != 0;
}

std::vector<BonkWeapon const*> BonkRun::GetOpenEvolutions(OwnedWeapon const& owned) const
{
    std::vector<BonkWeapon const*> open;
    if (owned.weapon.evolvesFrom || IsWildShapeshift(owned))
        return open;
    uint32 const account = m_player->GetBonkPlayer()->GetProgressAccount();
    for (BonkWeapon const* evolved : sBonkMgr.GetEvolutions(owned.weapon.id))
        if (!sBonkQuests.IsLocked(account, BONK_REWARD_WEAPON, evolved->id) && !IsEvolutionBanished(evolved->id))
            open.push_back(evolved);
    return open;
}

bool BonkRun::IsOpenEvolution(OwnedWeapon const& owned, uint32 evolvedId) const
{
    std::vector<BonkWeapon const*> const open = GetOpenEvolutions(owned);
    return std::any_of(open.begin(), open.end(), [evolvedId](BonkWeapon const* evolved) { return evolved->id == evolvedId; });
}

uint32 BonkRun::PickEvolution(OwnedWeapon const& owned) const
{
    std::vector<BonkWeapon const*> const open = GetOpenEvolutions(owned);
    return open.empty() ? 0 : open[urand(0, uint32(open.size() - 1))]->id;
}

bool BonkRun::CanEvolve(OwnedWeapon const& owned) const
{
    return !GetOpenEvolutions(owned).empty();
}

bool BonkRun::IsMaxAtPickup(BonkWeapon const& weapon)
{
    if (weapon.evolvesFrom)
        return false;
    return weapon.behavior == BONK_BEHAVIOR_BLADE_FLURRY || weapon.behavior == BONK_BEHAVIOR_SHADOW_DANCE ||
        weapon.behavior == BONK_BEHAVIOR_EVASION;
}

uint32 BonkRun::GetSpellRankTotal() const
{
    uint32 total = 0;
    for (OwnedWeapon const& owned : m_weapons)
        if (!IsMaxAtPickup(owned.weapon))
            total += owned.rank;
    return total;
}

uint32 BonkRun::GetWindowEvolveRanks() const
{
    return std::max(1u, BONK_WINDOW_EVOLVE_TOTAL_RANKS * GetEvolveRank() / BONK_EVOLVE_RANK);
}

bool BonkRun::HasEvolveRank(OwnedWeapon const& owned) const
{
    if (IsMaxAtPickup(owned.weapon))
        return GetSpellRankTotal() >= GetWindowEvolveRanks();
    return owned.rank >= GetEvolveRank();
}

uint32 BonkRun::GetRanksPastEvolveRank(OwnedWeapon const& owned) const
{
    if (!HasEvolveRank(owned))
        return 0;
    if (IsMaxAtPickup(owned.weapon))
        return GetSpellRankTotal() - GetWindowEvolveRanks();
    return owned.rank - GetEvolveRank();
}

float BonkRun::GetLegendaryChance(OwnedWeapon const& owned) const
{
    if (!HasEvolveRank(owned) || !CanEvolve(owned))
        return 0.0f;
    float const fromRank = std::min(BONK_LEGENDARY_MAX_RANK_CHANCE,
        BONK_LEGENDARY_BASE_CHANCE + BONK_LEGENDARY_CHANCE_PER_RANK * float(GetRanksPastEvolveRank(owned)));
    return fromRank + BONK_LEGENDARY_CHANCE_PER_LUCK * std::max(0.0f, GetPassiveTotal(BONK_PASSIVE_LUCK));
}

std::string BonkRun::DescribeEvolutionChance(OwnedWeapon const& owned, uint32& chance) const
{
    chance = 0;
    if (IsWildShapeshift(owned))
        return GetString(BONK_STRING_EVOLVE_HEART_OF_THE_WILD);
    uint32 const account = m_player->GetBonkPlayer()->GetProgressAccount();
    std::vector<BonkWeapon const*> const evolutions = sBonkMgr.GetEvolutions(owned.weapon.id);
    std::vector<std::string> sentences;
    std::vector<std::string> locks;
    for (BonkWeapon const* evolved : evolutions)
    {
        if (!sBonkQuests.IsLocked(account, BONK_REWARD_WEAPON, evolved->id))
            continue;
        BonkQuest const* quest = sBonkQuests.GetUnlockingQuest(BONK_REWARD_WEAPON, evolved->id);
        std::string const name = Localize(BONK_FIELD_WEAPON_NAME, evolved->id, evolved->name);
        locks.push_back(quest ? GetString(BONK_STRING_FINISH_QUEST_TO_UNLOCK, { Localize(BONK_FIELD_QUEST_NAME, quest->id, quest->name), name }) :
            GetString(BONK_STRING_EVOLVE_LOCKED, { name }));
    }

    if (!CanEvolve(owned))
    {
        if (locks.empty())
            sentences.push_back(GetString(evolutions.size() == 1 ? BONK_STRING_EVOLVE_BANISHED : BONK_STRING_EVOLVE_BANISHED_ALL));
    }
    else if (!HasEvolveRank(owned) && IsMaxAtPickup(owned.weapon))
        sentences.push_back(GetString(BONK_STRING_EVOLVE_AT_SPELL_RANK_TOTAL,
            { std::to_string(GetWindowEvolveRanks()), std::to_string(GetSpellRankTotal()) }));
    else if (!HasEvolveRank(owned))
        sentences.push_back(GetString(BONK_STRING_EVOLVE_FROM_RANK, { std::to_string(GetEvolveRank()) }));
    else if (FindAscensionWeapon() == &owned)
    {
        chance = 100;
        BonkCard const* ascension = m_cardByEffect[BONK_CARD_ASCENSION];
        sentences.push_back(GetString(BONK_STRING_EVOLVE_ASCENSION, { Localize(BONK_FIELD_CARD_NAME, ascension->id, ascension->name) }));
    }
    else
    {
        float const legendary = GetLegendaryChance(owned);
        chance = uint32(legendary + 0.5f);
        sentences.push_back(GetString(BONK_STRING_EVOLVE_CHANCE, { Percent(std::round(legendary * 10.0f) / 10.0f) }));
    }
    sentences.insert(sentences.end(), locks.begin(), locks.end());

    std::string text;
    for (std::string const& sentence : sentences)
        text += (text.empty() ? "" : " ") + sentence;
    return text;
}

bool BonkRun::HasEvolutionLeft() const
{
    if (m_weapons.size() < GetSpellSlots())
        return true;
    return std::any_of(m_weapons.begin(), m_weapons.end(), [this](OwnedWeapon const& owned) { return CanEvolve(owned); });
}

BonkRun::OwnedWeapon const* BonkRun::FindAscensionWeapon() const
{
    if (!HasCard(BONK_CARD_ASCENSION))
        return nullptr;
    for (OwnedWeapon const& owned : m_weapons)
        if (HasEvolveRank(owned) && CanEvolve(owned))
            return &owned;
    return nullptr;
}

void BonkRun::UseAscension(OwnedWeapon const& owned)
{
    BonkCard const* ascension = m_cardByEffect[BONK_CARD_ASCENSION];
    if (!ascension)
        return;
    std::string const name = Localize(BONK_FIELD_CARD_NAME, ascension->id, ascension->name);
    RemoveCard(*ascension);
    m_player->GetBonkPlayer()->Notify(BONK_STRING_ASCENSION_REVEALS_EVOLUTION, { name, Localize(BONK_FIELD_WEAPON_NAME, owned.weapon.id, owned.weapon.name) });
}

void BonkRun::ShowAllEvolutions(std::vector<Offer>& cards) const
{
    if (!m_bonuses.secondSight)
        return;

    auto hasEvolution = [](std::vector<Offer> const& list, uint32 evolvedId)
    {
        return std::any_of(list.begin(), list.end(), [evolvedId](Offer const& card)
        {
            return card.isWeapon && card.step == BONK_STEP_EVOLVE && card.evolveTo == evolvedId;
        });
    };

    std::vector<Offer> shown;
    for (Offer const& card : cards)
    {
        shown.push_back(card);
        if (!card.isWeapon || card.step != BONK_STEP_EVOLVE)
            continue;
        OwnedWeapon const* owned = FindOwned(card.id);
        if (!owned)
            continue;
        for (BonkWeapon const* evolved : GetOpenEvolutions(*owned))
            if (!hasEvolution(cards, evolved->id) && !hasEvolution(shown, evolved->id))
                shown.push_back(MakeEvolveOffer(*owned, evolved->id));
    }

    while (shown.size() > BONK_MAX_OFFER_SIZE)
    {
        auto spare = std::find_if(shown.rbegin(), shown.rend(), [](Offer const& card)
        {
            return !card.locked && card.step != BONK_STEP_EVOLVE;
        });
        if (spare == shown.rend())
            break;
        shown.erase(std::next(spare).base());
    }
    if (shown.size() > BONK_MAX_OFFER_SIZE)
        shown.resize(BONK_MAX_OFFER_SIZE);
    cards = shown;
}

float BonkRun::GetRarityWeight(uint8 rarity, BonkRarity minimum) const
{
    if (rarity >= BONK_RARITY_LEGENDARY || rarity < std::max(minimum, m_rarityFloor))
        return 0.0f;
    float const luck = std::max(0.0f, GetPassiveTotal(BONK_PASSIVE_LUCK));
    return BONK_RARITY_WEIGHT[rarity] * (1.0f + BONK_RARITY_LUCK[rarity] * luck / 100.0f);
}

BonkRarity BonkRun::RollRarity(BonkRarity minimum) const
{
    float weights[BONK_RARITY_LEGENDARY];
    float total = 0.0f;
    for (uint8 rarity = 0; rarity < BONK_RARITY_LEGENDARY; ++rarity)
    {
        weights[rarity] = GetRarityWeight(rarity, minimum);
        total += weights[rarity];
    }

    float roll = frand(0.0f, total);
    for (uint8 rarity = 0; rarity < BONK_RARITY_LEGENDARY; ++rarity)
    {
        if (weights[rarity] > 0.0f && roll < weights[rarity])
            return BonkRarity(rarity);
        roll -= weights[rarity];
    }
    return BONK_RARITY_EPIC;
}

void BonkRun::RollUpgrades(OwnedWeapon const& owned, Offer& offer) const
{
    for (BonkSpellUpgrade& upgrade : offer.upgrades)
        upgrade = BONK_SPELL_UPGRADE_NONE;
    if (offer.step != BONK_STEP_RANK || offer.rarity >= BONK_RARITY_LEGENDARY)
        return;

    float rolled[MAX_BONK_SPELL_UPGRADE] = {};
    uint8 const count = GetRankUpgradeCount(&owned, offer.rarity);
    for (uint8 i = 0; i < count; ++i)
    {
        std::vector<BonkWeaponUpgrade const*> const options = GetUpgradeOptions(owned, offer.rarity, rolled);
        if (options.empty())
            break;
        float totalWeight = 0.0f;
        for (BonkWeaponUpgrade const* option : options)
            totalWeight += GetUpgradeRollWeight(option->upgrade);
        float roll = frand(0.0f, totalWeight);
        BonkWeaponUpgrade const* pick = options.back();
        for (BonkWeaponUpgrade const* option : options)
        {
            roll -= GetUpgradeRollWeight(option->upgrade);
            if (roll <= 0.0f)
            {
                pick = option;
                break;
            }
        }
        offer.upgrades[i] = pick->upgrade;
        rolled[pick->upgrade] += pick->step;
    }
}

std::vector<BonkWeaponUpgrade const*> BonkRun::GetUpgradeOptions(OwnedWeapon const& owned, BonkRarity rarity, float const* rolled) const
{
    std::vector<BonkWeaponUpgrade const*> options;
    for (BonkWeaponUpgrade const& upgrade : owned.weapon.upgrades)
    {
        if ((upgrade.upgrade == BONK_SPELL_UPGRADE_HIT_TWICE || upgrade.upgrade == BONK_SPELL_UPGRADE_ECHO || upgrade.upgrade == BONK_SPELL_UPGRADE_TARGETS) &&
            rarity != BONK_RARITY_EPIC)
            continue;
        float const cap = BONK_SPELL_UPGRADE_CAP[upgrade.upgrade];
        if (cap > 0.0f && owned.upgrades[upgrade.upgrade] + rolled[upgrade.upgrade] + upgrade.step > cap + 0.001f)
            continue;
        if (upgrade.upgrade == BONK_SPELL_UPGRADE_HASTE && !HasteShortensCooldown(owned, rolled[upgrade.upgrade], upgrade.step))
            continue;
        if (!DurationGrowsWindow(owned, rolled, upgrade.upgrade, upgrade.step))
            continue;
        options.push_back(&upgrade);
    }
    return options;
}

bool BonkRun::OfferGainsAnything(OwnedWeapon const& owned, Offer const& offer) const
{
    if (offer.step != BONK_STEP_RANK || offer.rarity >= BONK_RARITY_LEGENDARY || RankDamageCounts(owned))
        return true;
    return owned.rank < BONK_DAMAGELESS_MAX_RANK || offer.upgrades[0] != BONK_SPELL_UPGRADE_NONE;
}

bool BonkRun::IsRankMaxed(OwnedWeapon const& owned) const
{
    if (IsMaxAtPickup(owned.weapon))
        return true;
    if (owned.rank < BONK_DAMAGELESS_MAX_RANK || RankDamageCounts(owned))
        return false;
    float const nothingRolled[MAX_BONK_SPELL_UPGRADE] = {};
    return GetUpgradeOptions(owned, BONK_RARITY_EPIC, nothingRolled).empty();
}

bool BonkRun::HasteShortensCooldown(OwnedWeapon const& owned, float alreadyRolled, float step) const
{
    OwnedWeapon before = owned;
    before.upgrades[BONK_SPELL_UPGRADE_HASTE] += alreadyRolled;
    OwnedWeapon after = before;
    after.upgrades[BONK_SPELL_UPGRADE_HASTE] += step;
    return GetWeaponCooldown(before) >= GetWeaponCooldown(after) + BONK_MIN_SHOWN_COOLDOWN_GAIN_MS;
}

BonkRun::Offer BonkRun::MakeRankOffer(OwnedWeapon const& owned, BonkRarity minimum) const
{
    float const legendary = GetLegendaryChance(owned);
    if (legendary > 0.0f && roll_chance_f(legendary))
        if (uint32 const evolveTo = PickEvolution(owned))
            return MakeEvolveOffer(owned, evolveTo);

    Offer offer;
    offer.id = owned.weapon.id;
    offer.newLevel = owned.rank + 1;
    offer.rarity = RollRarity(minimum);
    offer.step = BONK_STEP_RANK;
    RollUpgrades(owned, offer);
    return offer;
}

BonkRun::Offer BonkRun::MakeEvolveOffer(OwnedWeapon const& owned, uint32 evolveTo) const
{
    Offer offer;
    offer.id = owned.weapon.id;
    offer.newLevel = owned.rank + 1;
    offer.rarity = BONK_RARITY_LEGENDARY;
    offer.step = BONK_STEP_EVOLVE;
    offer.evolveTo = evolveTo;
    return offer;
}

void BonkRun::ApplyStep(OwnedWeapon& owned, Offer const& offer)
{
    ++owned.rank;
    owned.rarities += char('0' + int(offer.rarity));
    if (offer.step == BONK_STEP_EVOLVE)
    {
        BonkWeapon const* evolved = sBonkMgr.GetWeapon(offer.evolveTo);
        if (evolved && evolved->evolvesFrom == owned.weapon.id)
            EvolveWeapon(owned, *evolved);
        return;
    }
    if (offer.step != BONK_STEP_RANK || offer.rarity >= BONK_RARITY_LEGENDARY)
        return;

    AddRankGains(owned, offer);

    if (owned.weapon.behavior == BONK_BEHAVIOR_PET || owned.weapon.behavior == BONK_BEHAVIOR_WRATH)
        for (OwnedWeapon& other : m_weapons)
            if (other.weapon.behavior == BONK_BEHAVIOR_PET || other.weapon.behavior == BONK_BEHAVIOR_WRATH)
                UpdatePets(other);
    if (owned.weapon.behavior == BONK_BEHAVIOR_WRATH || owned.weapon.HasFlag(BONK_WEAPON_FLAG_SNAKES))
        DressSnakes();
    if (owned.weapon.behavior == BONK_BEHAVIOR_WRATH)
        DressBeasts();
}

void BonkRun::EvolveWeapon(OwnedWeapon& owned, BonkWeapon const& evolved)
{
    std::string const from = Localize(BONK_FIELD_WEAPON_NAME, owned.weapon.id, owned.weapon.name);
    owned.weapon = evolved;
    ApplyMasteryTo(owned.weapon);
    owned.timer = 500;
    RebuildWeaponSpells();
    if (UsesShotSources())
        WarmShotSources();
    if (evolved.IsBlink() || evolved.IsArcaneMissiles())
        m_player->GetBonkPlayer()->SendDash();
    m_player->GetBonkPlayer()->Notify(BONK_STRING_EVOLVED_INTO, { from, Localize(BONK_FIELD_WEAPON_NAME, evolved.id, evolved.name) });
}

bool BonkRun::ForceEvolve(uint32 weaponId, std::string& error)
{
    if (m_state != STATE_RUNNING && m_state != STATE_PORTAL)
    {
        error = GetString(BONK_STRING_YOU_ARE_NOT_IN_RUNNING);
        return false;
    }

    BonkWeapon const* evolved = sBonkMgr.GetWeapon(weaponId);
    if (!evolved)
    {
        error = GetString(BONK_STRING_THERE_IS_NO_SPELL, { std::to_string(weaponId) });
        return false;
    }

    std::string const name = Localize(BONK_FIELD_WEAPON_NAME, evolved->id, evolved->name);
    bool const held = std::any_of(m_weapons.begin(), m_weapons.end(), [weaponId](OwnedWeapon const& owned)
    {
        return owned.weapon.id == weaponId || owned.weapon.evolvesFrom == weaponId;
    });
    if (!evolved->evolvesFrom && !held)
    {
        if (m_weapons.size() >= GetSpellSlots())
        {
            error = GetString(BONK_STRING_NO_FREE_SPELL_SLOT_FOR, { name });
            return false;
        }
        AddWeapon(*evolved);
        SendLoadout();
        return true;
    }
    if (!evolved->evolvesFrom)
    {
        std::string choices;
        for (BonkWeapon const* evolution : sBonkMgr.GetEvolutions(weaponId))
            choices += (choices.empty() ? "" : ", ") + Localize(BONK_FIELD_WEAPON_NAME, evolution->id, evolution->name) + " (" + std::to_string(evolution->id) + ")";
        error = choices.empty() ? GetString(BONK_STRING_NOT_AN_EVOLUTION_NONE, { name }) : GetString(BONK_STRING_NOT_AN_EVOLUTION, { name, choices });
        return false;
    }

    OwnedWeapon* owned = FindOwned(evolved->evolvesFrom);
    if (!owned)
    {
        auto sibling = std::find_if(m_weapons.begin(), m_weapons.end(), [evolved](OwnedWeapon const& weapon)
        {
            return weapon.weapon.evolvesFrom == evolved->evolvesFrom;
        });
        owned = sibling != m_weapons.end() ? &*sibling : nullptr;
    }
    if (!owned)
    {
        BonkWeapon const* base = sBonkMgr.GetWeapon(evolved->evolvesFrom);
        if (!base || m_weapons.size() >= GetSpellSlots())
        {
            error = base ? GetString(BONK_STRING_NO_FREE_SPELL_SLOT_FOR, { Localize(BONK_FIELD_WEAPON_NAME, base->id, base->name) }) : GetString(BONK_STRING_NO_FREE_SPELL_SLOT_FOR_BASE);
            return false;
        }
        AddWeapon(*base);
        owned = FindOwned(base->id);
    }
    if (!owned || owned->weapon.id == evolved->id)
    {
        error = GetString(BONK_STRING_YOU_ALREADY_HAVE, { name });
        return false;
    }

    if (owned->weapon.id == evolved->evolvesFrom)
        ApplyStep(*owned, MakeEvolveOffer(*owned, evolved->id));
    else
        EvolveWeapon(*owned, *evolved);
    SendLoadout();
    return true;
}

bool BonkRun::GivePassive(uint32 passiveId, std::string& error)
{
    BonkPassive const* passive = sBonkMgr.GetPassive(passiveId);
    if (!passive)
    {
        error = GetString(BONK_STRING_THERE_IS_NO_PASSIVE, { std::to_string(passiveId) });
        return false;
    }

    std::string const name = Localize(BONK_FIELD_PASSIVE_NAME, passive->id, passive->name);

    auto rank = m_passiveRanks.find(passiveId);
    uint8 const current = rank != m_passiveRanks.end() ? rank->second : 0;
    size_t const ownedPassives = std::count_if(m_passiveRanks.begin(), m_passiveRanks.end(), [](auto const& itr) { return itr.second > 0; });
    if (passive->IsMaxed(current))
    {
        error = GetString(BONK_STRING_ALREADY_HIGHEST_RANK, { name });
        return false;
    }
    if (!current && ownedPassives >= GetPassiveSlots())
    {
        error = GetString(BONK_STRING_NO_FREE_PASSIVE_SLOT_FOR, { name });
        return false;
    }

    m_passiveRanks[passiveId] = uint8(current + 1);
    ApplyPassives();
    SendLoadout();
    m_player->GetBonkPlayer()->Notify(BONK_STRING_NOW_RANK, { name, std::to_string(current + 1) });
    return true;
}

bool BonkRun::IsPassiveAllowed(BonkPassive const& passive) const
{
    return passive.effect != BONK_PASSIVE_POISON_DAMAGE || sBonkMgr.HasPoisonSpell(m_player->GetClassMask());
}

size_t BonkRun::GetSpellSlots() const
{
    if (m_sandboxBench)
        return BONK_BENCH_SPELL_SLOTS;
    return std::min(BONK_MAX_SPELL_SLOTS, BONK_START_SPELL_SLOTS + m_bonuses.spellSlots);
}

size_t BonkRun::GetPassiveSlots() const
{
    return std::min(BONK_MAX_PASSIVE_SLOTS, BONK_START_PASSIVE_SLOTS + m_bonuses.passiveSlots);
}

bool BonkRun::IsStatCapped(BonkPassiveEffect effect) const
{
    float const cap = GetStatCap(effect);
    return cap > 0.0f && GetPassiveTotal(effect) >= cap;
}

bool BonkRun::IsGainTooSmall(BonkPassiveEffect effect, float gain) const
{
    if (IsStatCapped(effect))
        return true;
    float const ceiling = effect == BONK_PASSIVE_HASTE ? BONK_MAX_HASTE : effect == BONK_PASSIVE_AREA ? BONK_MAX_AREA : 0.0f;
    if (ceiling <= 0.0f || gain <= 0.0f)
        return false;
    float const raw = GetPassiveTotal(effect);
    return CurveStat(raw + gain, ceiling) - CurveStat(raw, ceiling) < BONK_MIN_CURVED_GAIN;
}

bool BonkRun::IsPassiveCapped(BonkPassive const& passive) const
{
    auto rank = m_passiveRanks.find(passive.id);
    uint32 const next = (rank != m_passiveRanks.end() ? rank->second : 0) + 1;
    return IsGainTooSmall(passive.effect, passive.valuePerRank * passive.RankWeight(next));
}

bool BonkRun::RefreshKeptOffer(Offer& offer) const
{
    if (offer.isCard)
    {
        BonkCard const* card = sBonkMgr.GetCard(offer.id);
        if (!card || m_banished.count(GetBanishKey(offer)) || (card->maxCopies && GetCardCopies(offer.id) >= card->maxCopies) || IsCardCapped(*card))
            return false;
        offer.newLevel = GetCardCopies(offer.id) + 1;
        return true;
    }

    if (offer.isWeapon)
    {
        if (m_banished.count(GetBanishKey(offer)))
            return false;

        OwnedWeapon const* owned = FindOwned(offer.id);
        if (offer.step == BONK_STEP_NEW)
            return !owned && m_weapons.size() < GetSpellSlots() &&
                std::none_of(m_weapons.begin(), m_weapons.end(), [&](OwnedWeapon const& w) { return w.weapon.evolvesFrom == offer.id; });
        if (!owned)
            return false;
        if (IsMaxAtPickup(owned->weapon) && (offer.step != BONK_STEP_EVOLVE || !IsOpenEvolution(*owned, offer.evolveTo)))
            return false;

        offer.newLevel = owned->rank + 1;
        if (offer.step == BONK_STEP_EVOLVE && !IsOpenEvolution(*owned, offer.evolveTo))
        {
            offer.step = BONK_STEP_RANK;
            offer.rarity = BONK_RARITY_EPIC;
            offer.evolveTo = 0;
            if (m_banished.count(GetBanishKey(offer)))
                return false;
        }
        RollUpgrades(*owned, offer);
        return true;
    }

    BonkPassive const* passive = sBonkMgr.GetPassive(offer.id);
    if (!passive || m_banished.count({ false, offer.id }) || !IsPassiveAllowed(*passive) || IsPassiveCapped(*passive))
        return false;
    auto rank = m_passiveRanks.find(offer.id);
    uint8 const current = rank != m_passiveRanks.end() ? rank->second : 0;
    size_t const ownedPassives = std::count_if(m_passiveRanks.begin(), m_passiveRanks.end(), [](auto const& itr) { return itr.second > 0; });
    if (passive->IsMaxed(current) || (!current && ownedPassives >= GetPassiveSlots()))
        return false;
    offer.newLevel = current + 1;
    return true;
}

bool BonkRun::OfferUpgrades(OfferSource source, std::vector<Offer> const* avoid, std::vector<Offer> const* kept, std::vector<Offer>* drawn)
{
    struct Candidate
    {
        Offer offer;
        uint32 weight;
    };
    std::vector<Candidate> candidates;
    BonkRarity const minimum = source == OFFER_CHEST ? BONK_CHEST_MIN_RARITY : BONK_LEVEL_MIN_RARITY;
    size_t offerSize = BONK_OFFER_SIZE;
    if (source == OFFER_CHEST)
        offerSize = (!m_pendingChestChoices.empty() ? m_pendingChestChoices.front() : BONK_OFFER_SIZE) +
            size_t(GetUtility(BONK_UTILITY_CARD_CHOICES));
    else
        offerSize += size_t(GetUtility(BONK_UTILITY_LEVEL_CHOICES));
    offerSize = std::min(offerSize, BONK_MAX_OFFER_SIZE);
    if (source == OFFER_LEVEL)
        offerSize = std::min(offerSize + GetGlyphLevelChoices(), BONK_MAX_OFFER_SIZE);
    if (source == OFFER_LEVEL)
        if (BonkGlyph const* gambler = GetGlyph(BONK_GLYPH_GAMBLER))
            offerSize = std::max<size_t>(1, std::min(offerSize, size_t(gambler->values[0])));

    if (source == OFFER_CHEST && !sBonkMgr.GetCards().empty())
        return OfferCards(offerSize, avoid, kept, drawn);

    std::vector<Offer> cards;
    std::vector<Offer> carried;
    if (kept)
        carried = *kept;
    else if (source == OFFER_CHEST)
    {
        carried = m_lockedOffers;
        m_lockedOffers.clear();
    }
    for (Offer offer : carried)
        if (cards.size() < offerSize && RefreshKeptOffer(offer))
        {
            offer.locked = true;
            cards.push_back(offer);
        }
    auto isCard = [&cards](bool isWeapon, uint32 id)
    {
        return std::any_of(cards.begin(), cards.end(), [&](Offer const& card) { return card.isWeapon == isWeapon && card.id == id; });
    };

    OwnedWeapon const* ascended = source == OFFER_LEVEL && !drawn ? FindAscensionWeapon() : nullptr;
    for (auto const& owned : m_weapons)
    {
        if (isCard(true, owned.weapon.id))
            continue;
        if (&owned == ascended && cards.size() < offerSize)
        {
            cards.push_back(MakeEvolveOffer(owned, PickEvolution(owned)));
            continue;
        }
        Offer const offer = MakeRankOffer(owned, BONK_RARITY_COMMON);
        if (IsMaxAtPickup(owned.weapon) && offer.step != BONK_STEP_EVOLVE)
            continue;
        if (!OfferGainsAnything(owned, offer))
            continue;
        if (!m_banished.count(GetBanishKey(offer)))
            candidates.push_back({ offer, 3 });
    }

    uint32 const classMask = m_player->GetClassMask();
    uint32 const account = m_player->GetBonkPlayer()->GetProgressAccount();
    if (m_weapons.size() < GetSpellSlots())
        for (auto const& itr : sBonkMgr.GetWeapons())
        {
            BonkWeapon const& weapon = itr.second;
            if (weapon.evolvesFrom || (weapon.classMask && !(weapon.classMask & classMask)))
                continue;
            if (weapon.IsBlink() && weapon.spec && weapon.spec != m_runSpec)
                continue;
            if (m_banished.count({ true, weapon.id }) || isCard(true, weapon.id) || sBonkQuests.IsLocked(account, BONK_REWARD_WEAPON, weapon.id))
                continue;
            if (std::any_of(m_weapons.begin(), m_weapons.end(), [&](OwnedWeapon const& owned)
                {
                    return owned.weapon.id == weapon.id || owned.weapon.evolvesFrom == weapon.id;
                }))
                continue;
            Offer offer;
            offer.id = weapon.id;
            candidates.push_back({ offer, 2 });
        }

    size_t const ownedPassives = std::count_if(m_passiveRanks.begin(), m_passiveRanks.end(), [](auto const& itr) { return itr.second > 0; });
    for (auto const& itr : sBonkMgr.GetPassives())
    {
        auto rank = m_passiveRanks.find(itr.first);
        uint8 const current = rank != m_passiveRanks.end() ? rank->second : 0;
        if (itr.second.IsMaxed(current) || (!current && ownedPassives >= GetPassiveSlots()))
            continue;
        if (m_banished.count({ false, itr.first }) || isCard(false, itr.first) || sBonkQuests.IsLocked(account, BONK_REWARD_PASSIVE, itr.first))
            continue;
        if (!IsPassiveAllowed(itr.second) || IsPassiveCapped(itr.second))
            continue;
        Offer offer;
        offer.isWeapon = false;
        offer.id = itr.first;
        offer.newLevel = current + 1;
        candidates.push_back({ offer, itr.second.rare ? 1u : 2u });
    }

    std::vector<Candidate> repeats;
    if (avoid)
    {
        auto shown = [avoid](Candidate const& candidate)
        {
            return std::any_of(avoid->begin(), avoid->end(), [&](Offer const& offer)
            {
                return offer.isWeapon == candidate.offer.isWeapon && offer.id == candidate.offer.id;
            });
        };
        for (auto itr = candidates.begin(); itr != candidates.end();)
        {
            if (shown(*itr))
            {
                repeats.push_back(*itr);
                itr = candidates.erase(itr);
            }
            else
                ++itr;
        }
    }

    for (std::vector<Candidate>* pool : { &candidates, &repeats })
    {
        while (cards.size() < offerSize && !pool->empty())
        {
            uint32 totalWeight = 0;
            for (auto const& candidate : *pool)
                totalWeight += candidate.weight;

            uint32 roll = urand(1, totalWeight);
            for (auto itr = pool->begin(); itr != pool->end(); ++itr)
            {
                if (roll > itr->weight)
                {
                    roll -= itr->weight;
                    continue;
                }
                cards.push_back(itr->offer);
                pool->erase(itr);
                break;
            }
        }
    }

    if (source == OFFER_LEVEL && std::none_of(cards.begin(), cards.end(), [](Offer const& card) { return !card.isWeapon; }))
    {
        std::vector<Offer> passives;
        std::vector<Offer> rarePassives;
        for (std::vector<Candidate> const* pool : { &candidates, &repeats })
            for (Candidate const& candidate : *pool)
                if (!candidate.offer.isWeapon)
                {
                    BonkPassive const* info = sBonkMgr.GetPassive(candidate.offer.id);
                    (info && info->rare ? rarePassives : passives).push_back(candidate.offer);
                }
        if (passives.empty())
            passives = rarePassives;
        if (!passives.empty())
        {
            Offer const& passive = passives[urand(0, uint32(passives.size() - 1))];
            auto replaced = std::find_if(cards.rbegin(), cards.rend(), [](Offer const& card)
            {
                return !card.locked && card.step != BONK_STEP_EVOLVE;
            });
            if (cards.size() < offerSize)
                cards.push_back(passive);
            else if (replaced != cards.rend())
                *replaced = passive;
        }
    }

    bool met = false;
    std::vector<size_t> raisable;
    for (size_t i = 0; i < cards.size(); ++i)
    {
        if (!cards[i].isWeapon || cards[i].step == BONK_STEP_NEW)
            continue;
        if (cards[i].rarity >= minimum)
            met = true;
        else if (!cards[i].locked)
            raisable.push_back(i);
    }
    if (!met && !raisable.empty())
    {
        Offer& card = cards[raisable[urand(0, uint32(raisable.size() - 1))]];
        if (OwnedWeapon const* owned = FindOwned(card.id))
        {
            card.rarity = RollRarity(minimum);
            card.step = BONK_STEP_RANK;
            RollUpgrades(*owned, card);
        }
    }

    if (source == OFFER_LEVEL && GetGlyph(BONK_GLYPH_GAMBLER))
        for (Offer& card : cards)
            if (card.isWeapon && card.step == BONK_STEP_RANK && !card.locked && card.rarity < BONK_RARITY_EPIC)
                if (OwnedWeapon const* owned = FindOwned(card.id))
                {
                    card.rarity = BonkRarity(card.rarity + 1);
                    RollUpgrades(*owned, card);
                }

    cards.erase(std::remove_if(cards.begin(), cards.end(), [this](Offer const& card) { return DescribeStep(card).empty(); }), cards.end());

    if (drawn)
    {
        *drawn = cards;
        return !cards.empty();
    }

    ShowAllEvolutions(cards);
    bool const ascensionShown = ascended && std::any_of(cards.begin(), cards.end(), [ascended](Offer const& card)
    {
        return card.isWeapon && card.step == BONK_STEP_EVOLVE && card.id == ascended->weapon.id;
    });
    if (ascensionShown)
        UseAscension(*ascended);

    m_offer = cards;
    if (m_offer.empty())
    {
        if (source != OFFER_CHEST)
            --m_pendingLevels;
        else if (!m_pendingChestChoices.empty())
            m_pendingChestChoices.erase(m_pendingChestChoices.begin());
        uint32 const gold = m_map.goldPerKill * 25;
        uint32 const paid = uint32(gold * GetGoldMultiplier());
        GiveGold(gold);
        m_player->GetBonkPlayer()->Notify(paid ? BONK_STRING_NOTHING_LEFT_TO_LEARN_GOLD : BONK_STRING_NOTHING_LEFT_TO_LEARN, { FormatBonkMoney(paid, GetLocale()) });
        return false;
    }

    m_offerSource = source;
    SetPaused(true);
    SendOffer();
    return true;
}

std::string BonkRun::DescribeOffer(Offer const& offer, std::string* icon) const
{
    if (offer.isCard)
    {
        BonkCard const* card = sBonkMgr.GetCard(offer.id);
        if (icon)
            *icon = card ? card->icon : "";
        return card ? Localize(BONK_FIELD_CARD_NAME, card->id, card->name) : "?";
    }

    if (offer.isWeapon)
    {
        BonkWeapon const* weapon = sBonkMgr.GetWeapon(offer.step == BONK_STEP_EVOLVE ? offer.evolveTo : offer.id);
        if (!weapon)
            return "?";
        if (icon)
            *icon = weapon->icon;
        return Localize(BONK_FIELD_WEAPON_NAME, weapon->id, weapon->name);
    }

    BonkPassive const* passive = sBonkMgr.GetPassive(offer.id);
    if (!passive)
        return "?";
    if (icon)
        *icon = passive->icon;
    return Localize(BONK_FIELD_PASSIVE_NAME, passive->id, passive->name);
}

std::string BonkRun::DescribeStep(Offer const& offer) const
{
    if (offer.isCard)
    {
        BonkCard const* card = sBonkMgr.GetCard(offer.id);
        if (!card)
            return "";
        uint32 const owned = GetCardCopies(card->id);
        std::string const total = owned ? BonkCodex::DescribeCardTotal(*card, owned + 1, GetLocale()) : std::string();
        std::string const description = Localize(BONK_FIELD_CARD_DESCRIPTION, card->id, card->description);
        return total.empty() ? description : description + BONK_LINE_BREAK + GetString(BONK_STRING_WITH_THIS_COPY, { total });
    }

    if (!offer.isWeapon)
    {
        BonkPassive const* passive = sBonkMgr.GetPassive(offer.id);
        return passive ? sBonkMgr.DescribePassiveRankUp(*passive, offer.newLevel, GetLocale()) : "";
    }

    BonkWeapon const* weapon = sBonkMgr.GetWeapon(offer.id);
    if (!weapon)
        return "";

    if (offer.step == BONK_STEP_NEW)
    {
        std::string description = Localize(BONK_FIELD_WEAPON_DESCRIPTION, weapon->id, weapon->description);
        if (weapon->IsBlink() && IsBlinkDash())
            description = BonkLocale::AfterFirstSentence(description);
        if (IsMaxAtPickup(*weapon))
            description += " " + GetString(BONK_STRING_STARTS_AT_MAX_RANK, { std::to_string(GetWindowEvolveRanks()) });
        return description;
    }
    if (offer.step == BONK_STEP_EVOLVE)
    {
        BonkWeapon const* evolved = sBonkMgr.GetWeapon(offer.evolveTo);
        return evolved ? GetString(BONK_STRING_EVOLVES_INTO_OFFER, { Localize(BONK_FIELD_WEAPON_NAME, weapon->id, weapon->name),
            Localize(BONK_FIELD_WEAPON_NAME, evolved->id, evolved->name), sBonkMgr.DescribeEvolutionAdds(*evolved, GetLocale()) }) : "";
    }
    if (offer.rarity >= BONK_RARITY_LEGENDARY)
        return "";

    std::string text = DescribeRankUp(offer, *weapon);
    if (text.empty())
        text = DescribeBareRankUp(offer);
    if (weapon->behavior == BONK_BEHAVIOR_SPREAD && !weapon->evolvesFrom && offer.newLevel == BONK_SPREAD_RANK)
        text += " " + GetString(BONK_STRING_RANK_NOW_SPREADS);
    return text;
}

std::string BonkRun::DescribeBareRankUp(Offer const& offer) const
{
    OwnedWeapon const* owned = FindOwned(offer.id);
    if (owned && offer.newLevel < GetEvolveRank() && CanEvolve(*owned))
        return GetString(BONK_STRING_CAN_EVOLVE_FROM_RANK, { std::to_string(GetEvolveRank()) });
    if (offer.newLevel >= BONK_DAMAGELESS_MAX_RANK)
        return GetString(BONK_STRING_REACHES_MAX_RANK);
    return GetString(BONK_STRING_MAX_RANK_AT, { std::to_string(BONK_DAMAGELESS_MAX_RANK) });
}

std::string BonkRun::DescribeRankUp(Offer const& offer, BonkWeapon const& weapon) const
{
    OwnedWeapon const* owned = FindOwned(offer.id);
    BonkWeapon const& current = owned ? owned->weapon : weapon;
    bool const damageCounts = !owned || RankDamageCounts(*owned);
    float const rankScale = GetRankDamageScale(offer.newLevel);
    float damage = GetRankDamage(owned, offer.rarity) * rankScale;
    float amounts[MAX_BONK_SPELL_UPGRADE] = {};
    std::vector<BonkSpellUpgrade> order;
    uint8 const count = GetRankUpgradeCount(owned, offer.rarity);
    for (uint8 i = 0; i < count; ++i)
    {
        BonkSpellUpgrade const upgrade = offer.upgrades[i];
        float const step = current.GetUpgradeStep(upgrade);
        if (step <= 0.0f)
        {
            if (damageCounts)
                damage += BONK_RANK_SPARE_DAMAGE * rankScale;
            continue;
        }
        if (!amounts[upgrade])
            order.push_back(upgrade);
        amounts[upgrade] += step;
    }

    float now[MAX_BONK_SPELL_UPGRADE] = {};
    GetSpellUpgrades(owned, now);
    float after[MAX_BONK_SPELL_UPGRADE] = {};
    for (uint8 upgrade = 0; upgrade < MAX_BONK_SPELL_UPGRADE; ++upgrade)
        after[upgrade] = now[upgrade] + amounts[upgrade];
    float const damageNow = owned ? owned->damageBonus : 0.0f;
    WeaponStrength before, strength;
    bool const strengthens = GetWeaponStrength(current, damageNow, now, before) &&
        GetWeaponStrength(current, damageNow + damage, after, strength);

    BonkKeywordInfo const* keyword = sBonkMgr.GetKeyword(current.applies);
    bool const ticks = keyword && keyword->IsDamageOverTime();
    auto describeDamage = [&](float bonus) -> std::string
    {
        std::string const amount = "+" + Percent(std::round(bonus * 10.0f) / 10.0f);
        if (current.damageFraction <= 0.0f && current.healPct > 0.0f)
            return GetString(BONK_STRING_RANK_HEALING, { amount });
        if (current.damageFraction > 0.0f || (!ticks && !strengthens))
            return GetString(BONK_STRING_RANK_DAMAGE, { amount });
        if (ticks && keyword)
            return GetString(BONK_STRING_RANK_KEYWORD_DAMAGE, { amount, Localize(BONK_FIELD_KEYWORD_NAME, uint32(keyword->id), keyword->name) });
        return "";
    };
    auto join = [this](std::vector<std::string> const& parts)
    {
        std::string text;
        for (std::string const& part : parts)
            if (!part.empty())
                text = text.empty() ? part : GetString(BONK_STRING_LIST_ITEM, { text, part });
        return text;
    };

    std::vector<std::string> adds;
    if (damage > 0.0f)
        adds.push_back(describeDamage(damage));
    for (BonkSpellUpgrade upgrade : order)
        if (!strengthens || upgrade != strength.folded)
            adds.push_back(DescribeBonkSpellUpgrade(current, upgrade, amounts[upgrade], GetLocale()));
    if (strengthens)
    {
        float const gain = strength.folded == BONK_SPELL_UPGRADE_SHIELD_SIZE ?
            (before.value > 0.0f ? (strength.value / before.value - 1.0f) * 100.0f : 0.0f) : strength.value - before.value;
        if (gain >= 0.05f)
            adds.push_back(DescribeBonkSpellUpgrade(current, strength.folded, std::round(gain * 10.0f) / 10.0f, GetLocale()));
    }

    std::stable_partition(adds.begin(), adds.end(), [](std::string const& part)
    {
        return !part.empty() && (part[0] == '+' || part[0] == '-');
    });
    std::string text = join(adds);
    if (text.empty())
        return text;
    text[0] = char(toupper(static_cast<unsigned char>(text[0])));
    return GetString(BONK_STRING_SENTENCE, { text });
}

void BonkRun::GetSpellUpgrades(OwnedWeapon const* owned, float* upgrades) const
{
    for (uint8 upgrade = 0; upgrade < MAX_BONK_SPELL_UPGRADE; ++upgrade)
        upgrades[upgrade] = owned && upgrade != BONK_SPELL_UPGRADE_NONE ? GetSpellUpgrade(*owned, BonkSpellUpgrade(upgrade)) : 0.0f;
}

bool BonkRun::GetWeaponStrength(BonkWeapon const& weapon, float damageBonus, float const* upgrades, WeaponStrength& strength) const
{
    if (GetPaladinStrength(weapon, damageBonus, strength))
        return true;
    BonkWeapon const* row = sBonkMgr.GetWeapon(weapon.id);
    if (row && row->applies == BONK_KEYWORD_HUNTERS_MARK)
    {
        strength.sentence = BONK_STRING_STRENGTH_MARKED;
        strength.label = BONK_STRING_STRENGTH_MARKED_LABEL;
        strength.value = GetMarkStrength(damageBonus, upgrades[BONK_SPELL_UPGRADE_MARK]);
        strength.folded = BONK_SPELL_UPGRADE_MARK;
        return true;
    }
    if (weapon.IsShieldBlock() || weapon.behavior == BONK_BEHAVIOR_WARD || weapon.behavior == BONK_BEHAVIOR_POWER_WORD_SHIELD)
    {
        strength.sentence = BONK_STRING_STRENGTH_SHIELD;
        strength.label = BONK_STRING_STRENGTH_SHIELD_LABEL;
        strength.value = GetShieldPercent(weapon, damageBonus, upgrades[BONK_SPELL_UPGRADE_SHIELD_SIZE]);
        strength.folded = BONK_SPELL_UPGRADE_SHIELD_SIZE;
        return true;
    }
    if (weapon.damageFraction <= 0.0f && weapon.healPct > 0.0f)
    {
        strength.sentence = BONK_STRING_STRENGTH_HEAL;
        strength.label = BONK_STRING_STRENGTH_HEAL_LABEL;
        strength.value = GetHealPercent(weapon, damageBonus);
        return true;
    }
    if (weapon.HasFlag(BONK_WEAPON_FLAG_NORMAL_ONLY) && weapon.IsKeywordOnly())
    {
        strength.sentence = BONK_STRING_STRENGTH_SLOWED;
        strength.label = BONK_STRING_STRENGTH_SLOWED_LABEL;
        strength.value = GetSlowedStrength(damageBonus, upgrades[BONK_SPELL_UPGRADE_SLOWED]);
        strength.folded = BONK_SPELL_UPGRADE_SLOWED;
        return true;
    }
    return false;
}

bool BonkRun::Pick(uint32 choice, std::string& error)
{
    if ((m_state != STATE_RUNNING && m_state != STATE_PORTAL) || m_offer.empty())
    {
        error = GetString(BONK_STRING_THERE_IS_NOTHING_TO_CHOOSE);
        m_player->GetBonkPlayer()->SendAddon("picked");
        return false;
    }

    if (choice < 1 || choice > m_offer.size())
    {
        error = GetString(BONK_STRING_CHOOSE_NUMBER_FROM_1_TO, { std::to_string(m_offer.size()) });
        SendOffer();
        return false;
    }

    Offer const offer = m_offer[choice - 1];

    {
        std::string const rarity = (offer.isWeapon && offer.step != BONK_STEP_NEW) || offer.isCard ? GetBonkRarityName(offer.rarity) : "new";
        sLog.Player(m_player->GetSession(), LOG_LEVELUP, LOG_LVL_BASIC,
            "Bonk pick: %s takes %s rank %u (%s, %s) at run minute %.2f, level %u: %s",
            m_player->GetName(), DescribeOffer(offer, nullptr).c_str(), offer.newLevel, rarity.c_str(),
            m_offerSource == OFFER_CHEST ? "chest" : "level", GetRunMinutes(), m_player->GetLevel(), JoinBonkLines(DescribeStep(offer)).c_str());
    }

    if (offer.isCard)
    {
        if (BonkCard const* card = sBonkMgr.GetCard(offer.id))
            AddCard(*card);
    }
    else if (offer.isWeapon)
    {
        if (OwnedWeapon* owned = FindOwned(offer.id))
        {
            ApplyStep(*owned, offer);
            EchoRankPick(*owned, offer);
        }
        else if (BonkWeapon const* weapon = sBonkMgr.GetWeapon(offer.id))
            AddWeapon(*weapon);
    }
    else
    {
        m_passiveRanks[offer.id] = uint8(offer.newLevel);
        ApplyPassives();
    }

    CloseOffer(choice - 1);
    return true;
}

void BonkRun::CloseOffer(uint32 pickedIndex)
{
    if (m_offerSource == OFFER_CHEST)
    {
        m_lockedOffers.clear();
        for (size_t i = 0; i < m_offer.size(); ++i)
            if (m_offer[i].locked && i != pickedIndex)
                m_lockedOffers.push_back(m_offer[i]);
    }

    m_offer.clear();
    if (m_offerSource == OFFER_CHEST)
    {
        if (!m_pendingChestChoices.empty())
            m_pendingChestChoices.erase(m_pendingChestChoices.begin());
    }
    else if (m_pendingLevels)
        --m_pendingLevels;

    BonkPlayer* bonk = m_player->GetBonkPlayer();
    bonk->SendAddon("picked");
    SendLoadout();

    ResumeUnlessChoiceOpen();
    UpdateOffers();
}

bool BonkRun::Reroll(std::string& error)
{
    if ((m_state != STATE_RUNNING && m_state != STATE_PORTAL) || m_offer.empty())
    {
        error = GetString(BONK_STRING_THERE_IS_NOTHING_TO_REROLL);
        m_player->GetBonkPlayer()->SendAddon("picked");
        return false;
    }

    if (!m_rerolls)
    {
        error = GetString(BONK_STRING_NO_REROLLS_LEFT);
        SendOffer();
        return false;
    }

    --m_rerolls;
    std::vector<Offer> const previous = m_offer;
    std::vector<Offer> kept;
    for (Offer const& offer : previous)
        if (offer.locked)
            kept.push_back(offer);
    if (!OfferUpgrades(m_offerSource, &previous, &kept))
    {
        SetPaused(false);
        m_player->GetBonkPlayer()->SendAddon("picked");
    }
    return true;
}

bool BonkRun::Banish(uint32 choice, std::string& error)
{
    if ((m_state != STATE_RUNNING && m_state != STATE_PORTAL) || m_offer.empty())
    {
        error = GetString(BONK_STRING_THERE_IS_NOTHING_TO_BANISH);
        m_player->GetBonkPlayer()->SendAddon("picked");
        return false;
    }

    if (!m_banishes || choice < 1 || choice > m_offer.size())
    {
        error = m_banishes ? GetString(BONK_STRING_CHOOSE_NUMBER_FROM_1_TO, { std::to_string(m_offer.size()) }) : GetString(BONK_STRING_NO_BANISHES_LEFT);
        SendOffer();
        return false;
    }

    Offer const offer = m_offer[choice - 1];
    if (!CanBanish(offer))
    {
        error = GetString(BONK_STRING_CANT_BANISH_OWNED);
        SendOffer();
        return false;
    }
    m_banished.insert(GetBanishKey(offer));
    m_player->GetBonkPlayer()->Notify(BONK_STRING_YOU_BANISHED_FOR_REST_OF, { DescribeOffer(offer, nullptr) });
    --m_banishes;

    ReplaceBanishedOffer(choice - 1);
    if (!m_offer.empty())
    {
        SendOffer();
        return true;
    }
    std::vector<Offer> const none;
    if (!OfferUpgrades(m_offerSource, nullptr, &none))
    {
        SetPaused(false);
        m_player->GetBonkPlayer()->SendAddon("picked");
    }
    return true;
}

void BonkRun::ReplaceBanishedOffer(size_t slot)
{
    std::vector<Offer> const none;
    std::vector<Offer> drawn;
    OfferUpgrades(m_offerSource, &m_offer, &none, &drawn);
    auto onBoard = [this](Offer const& card)
    {
        return std::any_of(m_offer.begin(), m_offer.end(), [&card](Offer const& shown)
        {
            return shown.isCard == card.isCard && shown.isWeapon == card.isWeapon && shown.id == card.id;
        });
    };
    auto replacement = std::find_if(drawn.begin(), drawn.end(), [&](Offer const& card)
    {
        return !onBoard(card) && !m_banished.count(GetBanishKey(card));
    });
    if (replacement != drawn.end())
        m_offer[slot] = *replacement;
    else
        m_offer.erase(m_offer.begin() + slot);
}

bool BonkRun::Skip(std::string& error)
{
    if ((m_state != STATE_RUNNING && m_state != STATE_PORTAL) || m_offer.empty())
    {
        error = GetString(BONK_STRING_THERE_IS_NOTHING_TO_SKIP);
        m_player->GetBonkPlayer()->SendAddon("picked");
        return false;
    }

    if (!m_skips)
    {
        error = GetString(BONK_STRING_NO_SKIPS_LEFT);
        SendOffer();
        return false;
    }

    --m_skips;
    uint32 const coins = std::max(1u, uint32(GetChestPrice() * BONK_SKIP_COINS));
    GainValor(coins);
    m_player->GetBonkPlayer()->Notify(BONK_STRING_YOU_SKIPPED_THIS_CHOICE_AND, { std::to_string(coins) });
    CloseOffer(uint32(m_offer.size()));
    SendState();
    return true;
}

bool BonkRun::Lock(uint32 choice, std::string& error)
{
    if ((m_state != STATE_RUNNING && m_state != STATE_PORTAL) || m_offer.empty())
    {
        error = GetString(BONK_STRING_THERE_IS_NOTHING_TO_LOCK);
        return false;
    }

    if (m_offerSource != OFFER_CHEST)
    {
        error = GetString(BONK_STRING_ONLY_CHEST_CHOICES_CAN_BE);
        return false;
    }

    if (choice < 1 || choice > m_offer.size())
    {
        error = GetString(BONK_STRING_CHOOSE_NUMBER_FROM_1_TO, { std::to_string(m_offer.size()) });
        return false;
    }

    m_offer[choice - 1].locked = !m_offer[choice - 1].locked;
    SendOffer();
    return true;
}

void BonkRun::SendState() const
{
    std::string damageTypes;
    for (auto const& owned : m_weapons)
        damageTypes += (IsShapeshift(owned) ? DescribeShapeshiftDamage() : BonkStats::DescribeDamageTypes(m_player->GetObjectGuid(), owned.weapon.spellId)) + ",";
    uint32 const shownSeconds = (m_sandbox ? m_totalMs : m_elapsedMs) / IN_MILLISECONDS;
    m_player->GetBonkPlayer()->SendAddon("state~" + std::to_string(shownSeconds) + "~" + std::to_string(m_sandbox ? 0 : m_map.durationSeconds) + "~" +
        std::to_string(m_kills) + "~" + std::to_string(m_goldEarned) + "~" + std::to_string(m_player->GetLevel()) + "~" + (m_bossSpawned ? "1" : "0") + "~" +
        std::to_string(m_coins) + "~" + std::to_string(GetChestPrice()) + "~" + std::to_string(m_stage) + "~" + (m_state == STATE_PORTAL ? "1" : "0") + "~" +
        std::to_string(m_shardsEarned) + "~" + DescribeWeaponStats() + "~" + std::to_string(m_totalMs / IN_MILLISECONDS) + "~" + damageTypes);
    m_player->GetBonkPlayer()->SendAddon("support~" + DescribeSupport());
    m_player->GetBonkPlayer()->SendAddon("gmeter~" + DescribeGlyphStats());
}

std::string BonkRun::DescribeWeaponStats() const
{
    std::string text;
    for (auto const& owned : m_weapons)
    {
        std::pair<uint64, uint32> const totals = GetOwnedSpellTotals(owned);
        text += std::to_string(totals.first) + ":" + std::to_string(totals.second) + ",";
    }
    return text;
}

std::pair<uint64, uint32> BonkRun::GetOwnedSpellTotals(OwnedWeapon const& owned) const
{
    std::pair<uint64, uint32> totals{ 0, 0 };
    for (uint32 spellId : IsShapeshift(owned) ? BonkStats::GetWeaponSpells(owned.weapon) : std::vector<uint32>{ owned.weapon.spellId })
    {
        BonkRunStats::Spell const part = BonkStats::GetSpellStats(m_player->GetObjectGuid(), spellId);
        totals.first += part.damage;
        totals.second += part.kills;
    }
    return totals;
}

std::string BonkRun::DescribeBuildTotals() const
{
    std::string spells;
    for (OwnedWeapon const& owned : m_weapons)
    {
        std::pair<uint64, uint32> const totals = GetOwnedSpellTotals(owned);
        spells += (spells.empty() ? "" : ",") + std::to_string(owned.weapon.id) + ":" + std::to_string(owned.rank) + ":" +
            std::to_string(totals.first) + ":" + std::to_string(totals.second);
    }
    std::string passives;
    for (auto const& itr : m_passiveRanks)
        passives += (passives.empty() ? "" : ",") + std::to_string(itr.first) + ":" + std::to_string(itr.second);
    std::string cards;
    for (OwnedCard const& owned : m_cards)
        if (owned.card)
            cards += (cards.empty() ? "" : ",") + std::to_string(owned.card->id) + ":" + std::to_string(owned.copies);
    std::string glyphs;
    for (BonkGlyph const* glyph : m_glyphs)
        glyphs += (glyphs.empty() ? "" : ",") + DescribeGlyphTotals(*glyph);
    std::string stats;
    for (uint8 effect = 0; effect < MAX_BONK_PASSIVE_EFFECT; ++effect)
    {
        float value = GetPassiveTotal(BonkPassiveEffect(effect));
        if (effect == BONK_PASSIVE_CRIT)
            value += BONK_BASE_CRIT;
        if (std::fabs(value) < 0.05f)
            continue;
        char entry[32];
        snprintf(entry, sizeof(entry), "%u:%.1f", uint32(effect), value);
        stats += (stats.empty() ? "" : ",") + std::string(entry);
    }
    return "spells [" + spells + "]; passives [" + passives + "]; cards [" + cards + "]; glyphs [" + glyphs + "]; gear [" +
        m_player->GetBonkPlayer()->DescribeGear() + "]; stats [" + stats + "]; found [" + m_player->GetBonkPlayer()->DescribeFoundGear() +
        "]; enchants [" + DescribeEnchants() + "]";
}

void BonkRun::SendLoadout() const
{
    std::string line = "loadout~";
    for (auto const& owned : m_weapons)
        line += std::string(owned.weapon.evolvesFrom ? "e:" : "w:") + owned.weapon.icon + ":" + std::to_string(owned.rank) + ":" +
            std::to_string(IsRankMaxed(owned) ? owned.rank : 0) + ",";
    for (auto const& itr : m_passiveRanks)
        if (BonkPassive const* passive = sBonkMgr.GetPassive(itr.first))
            line += "p:" + passive->icon + ":" + std::to_string(itr.second) + ":" + std::to_string(passive->maxRank) + ",";
    line += "~" + std::to_string(GetSpellSlots()) + "~" + std::to_string(GetPassiveSlots());
    BonkPlayer* bonk = m_player->GetBonkPlayer();
    bonk->SendAddon(line);

    uint32 index = 0;
    for (auto const& owned : m_weapons)
    {
        ++index;
        BonkWeapon const& weapon = owned.weapon;
        SendWeaponInfo(index, owned);

        std::string evolution, evolutionText, evolvedFrom, chanceText;
        uint32 chance = 0;
        std::vector<BonkWeapon const*> const evolutions = sBonkMgr.GetEvolutions(weapon.id);
        if (weapon.evolvesFrom)
        {
            if (BonkWeapon const* base = sBonkMgr.GetWeapon(weapon.evolvesFrom))
                evolvedFrom = Localize(BONK_FIELD_WEAPON_NAME, base->id, base->name);
        }
        else if (!evolutions.empty())
        {
            evolution = sBonkMgr.DescribeEvolutionNames(weapon.id, GetLocale());
            if (evolutions.size() == 1)
                evolutionText = sBonkMgr.DescribeEvolutionAdds(*evolutions.front(), GetLocale());
            chanceText = DescribeEvolutionChance(owned, chance);
        }
        bonk->SendAddon("pwe~" + std::to_string(index) + "~" + evolution + "~" + evolutionText + "~" + evolvedFrom + "~" + chanceText + "~" +
            std::to_string(chance));
        if (!weapon.evolvesFrom && evolutions.size() > 1)
            for (BonkWeapon const* evolved : evolutions)
                bonk->SendAddon("pwev~" + std::to_string(index) + "~" + Localize(BONK_FIELD_WEAPON_NAME, evolved->id, evolved->name) + "~" +
                    sBonkMgr.DescribeEvolutionAdds(*evolved, GetLocale()));
    }
    for (auto const& itr : m_passiveRanks)
    {
        BonkPassive const* passive = sBonkMgr.GetPassive(itr.first);
        if (!passive)
            continue;
        ++index;
        bonk->SendAddon("pw~" + std::to_string(index) + "~" + Localize(BONK_FIELD_PASSIVE_NAME, passive->id, passive->name) + "~~0~0~~" +
            sBonkMgr.DescribePassiveNow(*passive, itr.second, GetLocale()));
    }
}

void BonkRun::NoteWeaponFired(OwnedWeapon& owned)
{
    ++m_secondCasts;
    if (owned.weapon.IsTrigger() || owned.weapon.IsBlink())
        return;
    bool const waiting = owned.timer <= BONK_WEAPON_RETRY_MS;
    if (waiting && !owned.held)
        owned.pieResync = true;
    else if (!waiting)
    {
        float const interval = float(GetWeaponCooldown(owned));
        bool const drifted = std::fabs(interval - float(owned.shownIntervalMs)) > interval * BONK_PIE_DRIFT;
        bool const shortened = float(owned.timer) < interval * (1.0f - BONK_PIE_DRIFT);
        if (owned.held || drifted || shortened)
            owned.pieResync = true;
    }
    owned.held = waiting;
}

void BonkRun::SendCooldownPie(uint32 index, OwnedWeapon const& owned) const
{
    uint32 const interval = GetWeaponCooldown(owned);
    owned.shownIntervalMs = interval;
    uint32 const pieMs = owned.weapon.IsBlink() || owned.weapon.IsSeal() || owned.weapon.behavior == BONK_BEHAVIOR_PET ? 0u : interval;
    m_player->GetBonkPlayer()->SendAddon("pcd~" + std::to_string(index) + "~" + std::to_string(pieMs) + "~" +
        std::to_string(std::max<int32>(0, owned.timer)) + "~" + (owned.held ? "1" : "0"));
}

void BonkRun::RefreshChangedWeaponInfo() const
{
    uint32 index = 0;
    for (OwnedWeapon const& owned : m_weapons)
        SendWeaponInfo(++index, owned, true);
}

bool BonkRun::IsUpgradeShownInNumbers(BonkSpellUpgrade upgrade)
{
    switch (upgrade)
    {
        case BONK_SPELL_UPGRADE_HASTE:
        case BONK_SPELL_UPGRADE_DURATION:
        case BONK_SPELL_UPGRADE_PET_DAMAGE:
        case BONK_SPELL_UPGRADE_STACKS:
        case BONK_SPELL_UPGRADE_REACH:
        case BONK_SPELL_UPGRADE_TARGETS:
        case BONK_SPELL_UPGRADE_ECHO:
            return true;
        default:
            return false;
    }
}

void BonkRun::SendWeaponInfo(uint32 index, OwnedWeapon const& owned, bool onlyIfChanged) const
{
    BonkWeapon const& weapon = owned.weapon;
    uint32 const interval = weapon.IsBlink() ? m_player->GetBonkPlayer()->GetDashCooldownMs() : GetWeaponCooldown(owned);
    owned.shownIntervalMs = interval;
    float upgrades[MAX_BONK_SPELL_UPGRADE] = {};
    GetSpellUpgrades(&owned, upgrades);
    WeaponStrength strength;
    bool const strengthens = GetWeaponStrength(weapon, owned.damageBonus, upgrades, strength);
    std::string picks;
    auto addPick = [&picks](std::string text)
    {
        text[0] = char(toupper(static_cast<unsigned char>(text[0])));
        picks += (picks.empty() ? "" : " \xC2\xB7 ") + text;
    };
    if (strengthens)
        addPick(GetString(strength.sentence, { Percent(std::round(strength.value * 10.0f) / 10.0f) }));
    for (uint8 upgrade = BONK_SPELL_UPGRADE_NONE + 1; upgrade < MAX_BONK_SPELL_UPGRADE; ++upgrade)
        if (float const amount = GetSpellUpgrade(owned, BonkSpellUpgrade(upgrade)))
        {
            if ((strengthens && upgrade == strength.folded) || IsUpgradeShownInNumbers(BonkSpellUpgrade(upgrade)))
                continue;
            std::string const added = DescribeBonkSpellUpgrade(weapon, BonkSpellUpgrade(upgrade), amount, GetLocale());
            if (!added.empty())
                addPick(added);
        }
    std::string description = Localize(BONK_FIELD_WEAPON_DESCRIPTION, weapon.id, weapon.description);
    if (weapon.behavior == BONK_BEHAVIOR_PET && !weapon.HasFlag(BONK_WEAPON_FLAG_HYDRA) && !weapon.HasFlag(BONK_WEAPON_FLAG_ELEMENTAL_SHIFT))
        if (BonkPetFamily const* family = GetPetFamily(owned))
            if (!family->description.empty())
                description += " " + Localize(BONK_FIELD_PET_FAMILY_DESCRIPTION, family->id, family->description);
    description = DescribeLiveSpell(owned, description);
    if (weapon.behavior == BONK_BEHAVIOR_ROLLING_THUNDER)
        description += " " + DescribeThunderCount(owned);
    bool faster = false;
    std::string const header = DescribeSpellHeader(owned, interval, faster);
    std::string const radius = std::to_string(std::round(GetWeaponRadius(owned) * 10.0f) / 10.0f) + "~" +
        std::to_string(std::round(weapon.range * (weapon.IsMeleeStrike() ? GetWeaponArea(owned) : 1.0f) * 10.0f) / 10.0f);
    std::string const shown = description + "~" + header + "~" + picks + "~" + DescribeShownDamage(owned) + "~" + radius;
    if (onlyIfChanged && shown == owned.shownTip)
        return;
    owned.shownTip = shown;
    m_player->GetBonkPlayer()->SendAddon("pw~" + std::to_string(index) + "~" + Localize(BONK_FIELD_WEAPON_NAME, weapon.id, weapon.name) + "~" + owned.rarities + "~" + std::to_string(weapon.IsTrigger() || weapon.IsBlink() || weapon.IsSeal() || weapon.behavior == BONK_BEHAVIOR_PET ? 0u : interval) + "~" +
        std::to_string(std::max<int32>(0, owned.timer)) + "~" + picks + "~" + description + "~" + (owned.shownDamage = DescribeShownDamage(owned)) + "~" +
        header + "~" + (faster ? "1" : "0") + "~" + DescribeBonkWeaponTags(weapon, GetShownSchool(owned), GetLocale()) + "~" + radius);
}

void BonkRun::SendOffer() const
{
    BonkPlayer* bonk = m_player->GetBonkPlayer();
    if (bonk->IsAddonReady())
    {
        bonk->SendAddon("offer~" + std::to_string(m_player->GetLevel() + m_overcapLevel) + "~" + std::to_string(m_offer.size()) + "~" +
            (m_offerSource == OFFER_CHEST ? "chest" : "level") + "~" + std::to_string(m_rerolls) + "~" + std::to_string(m_banishes) + "~" +
            std::to_string(m_skips));
        for (size_t i = 0; i < m_offer.size(); ++i)
        {
            Offer const& offer = m_offer[i];
            std::string icon;
            std::string const name = DescribeOffer(offer, &icon);
            uint32 maxRank = 0;
            if (offer.isCard)
            {
                if (BonkCard const* card = sBonkMgr.GetCard(offer.id))
                    maxRank = card->maxCopies;
            }
            else if (!offer.isWeapon)
                if (BonkPassive const* passive = sBonkMgr.GetPassive(offer.id))
                    maxRank = passive->maxRank;
            bool const ranked = (offer.isWeapon && offer.step != BONK_STEP_NEW) || offer.isCard;
            std::string before, after, label;
            DescribeOfferDamage(offer, before, after, label);
            std::vector<BonkStatChange> stats;
            DescribeOfferStats(offer, stats);

            bonk->SendAddon("opt~" + std::to_string(i + 1) + "~" + (offer.isCard ? "c" : offer.step == BONK_STEP_EVOLVE ? "e" : offer.isWeapon ? "w" : "p") + "~" +
                std::to_string(offer.newLevel) + "~" + std::to_string(maxRank) + "~" + name + "~" + icon + "~" + DescribeStep(offer) + "~" +
                (ranked ? std::to_string(offer.rarity) : "") + "~" + (offer.locked ? "1" : "0") + "~" + before + "~" + after + "~" + label + "~" + (CanBanish(offer) ? "1" : "0"));
            for (BonkStatChange const& stat : stats)
                bonk->SendAddon("ostat~" + std::to_string(i + 1) + "~" + stat.label + "~" + stat.before + "~" + stat.after);
        }
        bonk->SendAddon("offerend");
        return;
    }

    ChatHandler handler(m_player);
    if (m_offerSource == OFFER_CHEST)
        handler.SendSysMessage("Chest! Choose an upgrade with .bonk pick <number>:");
    else
        handler.PSendSysMessage("Level %u! Choose an upgrade with .bonk pick <number>:", m_player->GetLevel());
    for (size_t i = 0; i < m_offer.size(); ++i)
    {
        Offer const& offer = m_offer[i];
        std::string const name = DescribeOffer(offer, nullptr);
        std::string kind;
        if (offer.isCard)
            kind = std::string(GetBonkRarityName(offer.rarity)) + " card";
        else if (offer.isWeapon && offer.step == BONK_STEP_NEW)
            kind = "new";
        else if (offer.isWeapon)
            kind = std::string(GetBonkRarityName(offer.rarity)) + " rank " + std::to_string(offer.newLevel);
        else
            kind = "rank " + std::to_string(offer.newLevel);
        handler.PSendSysMessage("  %u) %s (%s): %s", uint32(i + 1), name.c_str(), kind.c_str(), JoinBonkLines(DescribeStep(offer)).c_str());
    }
    if (m_rerolls)
        handler.PSendSysMessage("  .bonk reroll for new choices (%u left)", m_rerolls);
    if (m_banishes)
        handler.PSendSysMessage("  .bonk banish <number> to remove one for this run (%u left)", m_banishes);
    if (m_skips)
        handler.PSendSysMessage("  .bonk pass to skip this choice for a little Valor (%u left)", m_skips);
}

void BonkRun::Finish(bool wentHome)
{
    if (m_state != STATE_RUNNING && m_state != STATE_PORTAL)
        return;

    m_state = STATE_ENDING;
    if (IsCoopGuest())
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "Co-op: guest %s run ends, home %u alive %u", m_player->GetName(), uint32(wentHome), uint32(m_player->IsAlive()));
    if (!IsCoopGuest())
        BonkCoop::OnHostFinished(m_player);
    m_wentHome = wentHome;
    SetPaused(false);
    RemoveHeroLooks();
    if (!wentHome && !m_player->IsAlive())
    {
        sBonkQuests.OnProgress(m_player, BONK_QUEST_DEATHS, 0, 1);
        LogDeath();
        m_player->GetBonkPlayer()->AddDetermination();
    }

    if (wentHome)
        CollectAllShards();
    PayFinale();

    std::string const build = DescribeBuildTotals();
    DropOffers();
    ResolveRunGear(wentHome);
    if (!m_sandbox)
        LogRunEnd(wentHome, build);

    m_player->GetBonkPlayer()->RecordRun(m_victory ? m_difficultyLevel : 0, m_totalMs / IN_MILLISECONDS, m_kills, m_goldEarned);
    AwardMastery();
    m_endTimer = BONK_END_DELAY_MS;

    if (m_player->IsAlive())
        m_player->CombatStop(true);

    BonkPlayer* bonk = m_player->GetBonkPlayer();
    uint32 const seconds = m_totalMs / IN_MILLISECONDS;
    if (bonk->IsAddonReady())
        bonk->SendAddon("end~" + std::string(wentHome ? "1" : "0") + "~" + std::to_string(seconds) + "~" + std::to_string(m_kills) + "~" +
            std::to_string(m_goldEarned) + "~" + std::to_string(m_player->GetLevel()) + "~" + std::to_string(m_stage) + "~" + std::to_string(m_shardsEarned) +
            "~" + (m_victory ? "1" : "0") + "~" + (IsMythic() ? "1" : "0") + "~" + std::to_string(bonk->GetBestLevel()) + "~" +
            std::to_string(m_levelBestAtStart) + "~" + bonk->GetLevelName(m_difficultyLevel) + "~" + std::to_string(IsFinale() ? m_elapsedMs / IN_MILLISECONDS : 0));
    else
        ChatHandler(m_player).PSendSysMessage("%s You reached stage %u in %s with %u kills at level %u, and earned %s and %s.",
            m_victory ? "Victory!" : wentHome ? "Run complete!" : "Defeated.",
            m_stage, FormatTime(seconds).c_str(), m_kills, m_player->GetLevel(), FormatBonkMoney(m_goldEarned).c_str(), FormatBonkShards(m_shardsEarned).c_str());

    if (bonk->IsAddonReady())
    {
        std::vector<BonkSummaryWeapon> weapons;
        for (OwnedWeapon const& owned : m_weapons)
        {
            BonkSummaryWeapon weapon;
            weapon.spellId = owned.weapon.spellId;
            weapon.spellIds = BonkStats::GetWeaponSpells(owned.weapon);
            weapon.name = Localize(BONK_FIELD_WEAPON_NAME, owned.weapon.id, owned.weapon.name);
            weapon.icon = owned.weapon.icon;
            weapon.level = owned.rank;
            weapon.maxLevel = 0;
            weapon.evolved = owned.weapon.evolvesFrom != 0;
            weapons.push_back(weapon);
        }
        bonk->SendAddon("support~" + DescribeSupport());
        BonkStats::SendSummary(m_player, weapons);
    }

    ReportQuestCounts();
    sBonkQuests.Save(m_player->GetBonkPlayer()->GetProgressAccount());
    sBonkQuests.SendLog(m_player, "");
}

void BonkRun::SetMenuOpen(bool open)
{
    if (m_menuOpen == open)
        return;
    m_menuOpen = open;
    if (open)
    {
        if (m_state == STATE_RUNNING)
            SetPaused(true);
        return;
    }
    ResumeUnlessChoiceOpen();
    UpdateOffers();
}

bool BonkRun::HasOpenChoice() const
{
    return !m_offer.empty() || !m_leyOffer.empty() || m_vendorOpen || m_vaultOpen || m_gearOfferEntry || !m_glyphOffer.empty();
}

void BonkRun::ResumeUnlessChoiceOpen()
{
    if (!HasOpenChoice())
        SetPaused(false);
}

void BonkRun::SetPaused(bool paused)
{
    if (!paused && m_menuOpen)
        return;
    m_ownPaused = paused;
    ApplyPause(WantsPause());
}

void BonkRun::RefreshGroupEnemies()
{
    m_groupEnemies.clear();
    if (!m_player->GetGroup())
        return;
    std::vector<Player*> const group = BonkCoop::RunGroupOf(m_player);
    if (group.size() < 2)
        return;
    m_groupEnemies = m_mobs;
    for (Player* member : group)
        if (member != m_player && member->GetMapId() == m_player->GetMapId())
            if (BonkRun const* run = member->GetBonkPlayer()->GetRun())
                m_groupEnemies.insert(m_groupEnemies.end(), run->GetMobGuids().begin(), run->GetMobGuids().end());
}

void BonkRun::SetGroupHeld(bool held)
{
    if (m_groupHeld == held)
        return;
    m_groupHeld = held;
    ApplyPause(WantsPause());
}

void BonkRun::SetAway(bool away)
{
    if (m_away == away)
        return;
    m_away = away;
    ApplyPause(WantsPause());
    if (!away && m_followPending)
    {
        m_followPending = false;
        FollowToStage(m_followMap, m_followStage);
    }
}

void BonkRun::ResolveChoiceForGroup()
{
    std::string error;
    if (!m_glyphOffer.empty())
        ChooseGlyph(urand(1, uint32(m_glyphOffer.size())), error);
    else if (!m_leyOffer.empty())
        PickLeyBuff(urand(1, uint32(m_leyOffer.size())), error);
    else if (!m_offer.empty())
        Pick(urand(1, uint32(m_offer.size())), error);
    else
        return;
    m_player->GetBonkPlayer()->Notify(BONK_STRING_COOP_CHOICE_TIMED_OUT);
}

void BonkRun::ApplyPause(bool paused)
{
    if (m_paused == paused)
        return;
    m_paused = paused;
    if (paused)
        m_pausedAtMs = WorldTimer::getMSTime();
    else
        ShiftDotStacks(WorldTimer::getMSTime() - m_pausedAtMs);

    m_player->SetTimeFrozen(paused);
    PauseHearth(paused);
    if (m_player->IsInWorld())
    {
        for (UnitMoveType type : { MOVE_WALK, MOVE_RUN, MOVE_RUN_BACK, MOVE_SWIM })
            m_player->UpdateSpeed(type, false);
        m_player->SetSpeedRate(MOVE_SWIM_BACK, paused ? 0.0f : 1.0f);
    }

    if (Map* map = m_player->FindMap())
    {
        for (ObjectGuid const& guid : m_mobs)
            if (Creature* mob = map->GetCreature(guid))
                if (BonkMobAI* ai = dynamic_cast<BonkMobAI*>(mob->AI()))
                    ai->SetFrozen(paused);

        if (m_miniEventActive && m_miniEvent.type == BONK_MINI_EVENT_CARAVAN)
            PauseCaravan(paused);

        auto hold = [this, map, paused](ObjectGuid const& guid, bool planted)
        {
            if (Creature* pet = map->GetCreature(guid))
                HoldPet(pet, paused, planted);
        };
        for (OwnedWeapon const& owned : m_weapons)
            for (ObjectGuid const& guid : owned.pets)
                hold(guid, IsPlanted(owned.weapon));
        for (Snake const& released : m_snakes)
            hold(released.guid, false);
        for (Beast const& beast : m_beasts)
            hold(beast.guid, false);
        for (RisenSkeleton const& risen : m_skeletons)
            hold(risen.guid, false);
        for (WarlockMinion const& minion : m_warlockMinions)
            hold(minion.guid, false);
        if (!paused)
            ResumeCharges();
    }

    if (!paused && m_player->IsInWorld())
    {
        m_objectives.ResendTimed(m_player);
        SendCardCooldowns();
    }

    if (m_player->GetBonkPlayer()->IsAddonReady())
        m_player->GetBonkPlayer()->SendAddon(std::string("paused~") + (paused ? "1" : "0"));
}

void BonkRun::ResyncPause()
{
    char const* resent = "nothing open, unpaused";
    bool const menuWasOpen = m_menuOpen;
    if (!m_paused)
        resent = "not paused";
    else if (!m_offer.empty())
    {
        resent = "offer";
        SendOffer();
    }
    else if (!m_leyOffer.empty())
    {
        resent = "ley line";
        SendLeyOffer();
    }
    else if (!m_glyphOffer.empty())
    {
        resent = "glyph";
        SendGlyphOffer();
    }
    else if (m_gearOfferEntry)
    {
        resent = "gear";
        SendGearOffer();
    }
    else if (m_vendorOpen)
    {
        resent = "vendor";
        SendVendor();
    }
    else if (m_vaultOpen)
    {
        resent = "vault";
        SendVault();
    }
    else
    {
        m_menuOpen = false;
        SetPaused(false);
        UpdateOffers();
    }

    sLog.Player(m_player->GetSession(), LOG_LEVELUP, LOG_LVL_BASIC, "Bonk resync: %s at run minute %.2f (stage %u, %s, %s): %s, menu flag %s",
        m_player->GetName(), GetRunMinutes(), m_stage, m_map.key.c_str(), m_tier.key.c_str(), resent, menuWasOpen ? "set" : "clear");
    if (!m_paused && m_player->GetBonkPlayer()->IsAddonReady())
        m_player->GetBonkPlayer()->SendAddon("paused~0");
}

void BonkRun::HoldPet(Creature* pet, bool hold, bool planted)
{
    if (hold)
    {
        pet->AttackStop();
        pet->GetMotionMaster()->Clear(false);
        pet->GetMotionMaster()->MoveIdle();
        pet->StopMoving(true);
        pet->AddUnitState(UNIT_STATE_ROOT);
        pet->SetRooted(true);
    }
    else if (!planted)
    {
        pet->ClearUnitState(UNIT_STATE_ROOT);
        pet->SetRooted(false);
    }
    pet->SetTimeFrozen(hold && pet->IsAlive());
}

bool BonkRun::IsPlanted(BonkWeapon const& weapon)
{
    return weapon.HasFlag(BONK_WEAPON_FLAG_HYDRA) || weapon.HasFlag(BONK_WEAPON_FLAG_TOTEM);
}

void BonkRun::HoldAuraTimers(uint32 diff)
{
    m_pauseAuraMs += diff;
    if (m_pauseAuraMs < BONK_PAUSE_AURA_MS)
        return;
    m_pauseAuraMs = 0;
    for (auto const& itr : m_player->GetSpellAuraHolderMap())
        itr.second->UpdateAuraDuration();
}

void BonkRun::OnMobStuck(Creature* mob)
{
    if (m_state != STATE_RUNNING || m_paused)
        return;
    if (BonkMobAI* ai = dynamic_cast<BonkMobAI*>(mob->AI()))
        if (ai->IsFlyer())
        {
            ai->FlyIn(m_player);
            return;
        }
    if (m_perched || IsWaitingInCrowd(mob))
        return;

    float x, y, z;
    if (FindMobPosition(x, y, z, m_map.spawnMinDistance, m_map.spawnMaxDistance))
        MoveMob(mob, x, y, z);
}

bool BonkRun::IsWaitingInCrowd(Creature const* mob) const
{
    if (!mob->IsWithinDistInMap(m_player, BONK_CROWD_WAIT_RANGE))
        return false;
    for (Creature const* other : GetMobsInRange(mob->GetObjectBoundingRadius() + BONK_CROWD_TOUCH_SLACK + BONK_CROWD_MAX_BODY, mob))
        if (mob->GetDistance2dToCenter(other) <= mob->GetObjectBoundingRadius() + other->GetObjectBoundingRadius() + BONK_CROWD_TOUCH_SLACK)
            return true;
    return false;
}

void BonkRun::CleanUp()
{
    SetPaused(false);
    m_player->SetTimeFrozen(false);
    m_player->GetBonkPlayer()->ClearSpellTweaks();
    DropOffers();
    ResolveRunGear(false);
    DespawnMiniEvent();
    DespawnMobs();
    DespawnObjects();
    m_objectives.Clear(m_player);
    DespawnShrines();
    DespawnVendor();
    DespawnOrbs();
    m_player->SetCreatureSummonLimit(sWorld.getConfig(CONFIG_UINT32_CREATURE_SUMMON_LIMIT));
    m_player->RemoveAurasDueToSpell(BONK_SPELL_MOVE_SPEED);
    m_player->RemoveAurasDueToSpell(BONK_SPELL_MAX_HEALTH);
    m_player->RemoveAurasDueToSpell(BONK_SPELL_DODGE);
    m_player->RemoveAurasDueToSpell(BONK_SPELL_SHIELD_VISUAL);
    m_player->GetBonkPlayer()->SendAddon("shield~0~0");
    SetNecroticStacks(0);
    ClearBossPressure();
    DespawnPools();
    DespawnWeaponOrbs();
    DespawnProjectiles();
    DespawnShotSources();
    m_trickShotTimes.clear();
    ClearHunterEvolutions();
    m_pendingCasts.clear();
    m_heldShotSources.clear();
    m_telegraphs.Dismiss(m_player);
    DespawnTraps();
    DespawnLandMines();
    DespawnPets();
    m_pendingKeywords.clear();
    m_pendingConsumes.clear();
    m_dyingKeywords.clear();
    m_dotStacks.clear();
    m_crowdControl.clear();
    m_buffs.clear();
    ResetWarriorState();
    ResetMageState();
    ResetShamanState();
    ResetPriestState();
    ResetRogueState();
    ResetPaladinState();
    ResetDruidState();
    m_bleedHeal = 0.0f;
    m_spellShield = 0;
    m_spellShieldParts.clear();
    m_chestChoices.clear();
    m_player->GetBonkPlayer()->SendAddon("mobclear");
    SetMovementImmunity(m_player, false);
    if (m_sandbox)
        EndSandbox();
}

void BonkRun::Exit()
{
    if (IsCoopGuest())
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "Co-op: guest %s exits run, state %u", m_player->GetName(), uint32(m_state));
    CleanUp();
    m_state = STATE_FINISHED;

    BonkPlayer* bonk = m_player->GetBonkPlayer();
    bonk->ResetCharacter();
    ReturnBroughtGear();
    ClearSky();
    bonk->TeleportToCamp();
}

void BonkRun::Abort()
{
    if (m_state == STATE_FINISHED)
        return;

    if (m_state == STATE_RUNNING || m_state == STATE_PORTAL || (m_state == STATE_ENTERING && m_stage > 1))
    {
        m_player->GetBonkPlayer()->RecordRun(m_stage, m_totalMs / IN_MILLISECONDS, m_kills, m_goldEarned);
        AwardMastery();
    }

    CleanUp();
    m_state = STATE_FINISHED;
    m_player->GetBonkPlayer()->ResetCharacter();
    ReturnBroughtGear();
}

void BonkRun::Surrender()
{
    if (m_sandbox)
        LeaveSandbox();
    else if (m_state == STATE_RUNNING)
        Finish(IsFinale() && m_player->IsAlive());
    else if (m_state == STATE_PORTAL)
        Finish(true);
    else if (m_state == STATE_ENTERING)
        Exit();
}
