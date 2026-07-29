#include "runtime/scalar.h"

#include "ft/fighter.h"
#include "ft/ftparts.h"
#include "ft/inlines.h"

#include <baselib/jobj.h>

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

typedef struct FighterSpec {
    FighterKind kind;
    uint8_t external_id;
    const char* name;
} FighterSpec;

typedef struct TopologyCheck {
    const FighterSpec* fighter;
    int costume;
    const uint8_t* mask;
    const uint8_t* part_by_node;
    HSD_Joint** joint_by_node;
    int node_capacity;
    int node_count;
    uint64_t hash;
} TopologyCheck;

static MslCoreGameData game_data;
static MslCoreMatch match;

static uint64_t hash_byte(uint64_t hash, uint8_t value)
{
    return (hash ^ value) * UINT64_C(1099511628211);
}

static int inspect_joint_list(TopologyCheck* check, HSD_Joint* joint,
                              int parent_live, uint8_t depth)
{
    while (joint != NULL) {
        int node = check->node_count++;
        int part;
        int live;
        int has_child;

        if (node >= check->node_capacity) {
            fprintf(stderr, "%s costume %d has too many source joints\n",
                    check->fighter->name, check->costume);
            return -1;
        }
        part = check->part_by_node[node];
        live = check->mask[part] != 0;
        if (live && !parent_live) {
            fprintf(stderr,
                    "%s costume %d part %d has a cold source ancestor\n",
                    check->fighter->name, check->costume, part);
            return -1;
        }
        check->joint_by_node[node] = joint;
        has_child = !(joint->flags & JOBJ_INSTANCE) && joint->child != NULL;
        check->hash = hash_byte(check->hash, depth);
        check->hash = hash_byte(check->hash,
                                (joint->flags & JOBJ_INSTANCE) != 0);
        check->hash = hash_byte(check->hash, has_child);
        if (has_child &&
            inspect_joint_list(check, joint->child, live, depth + 1) != 0)
        {
            return -1;
        }
        joint = joint->next;
    }
    return 0;
}

static int verify_instance_references(const TopologyCheck* check)
{
    int node;

    for (node = 0; node < check->node_count; ++node) {
        HSD_Joint* joint = check->joint_by_node[node];
        int target;

        if (!(joint->flags & JOBJ_INSTANCE) ||
            !check->mask[check->part_by_node[node]])
        {
            continue;
        }
        for (target = 0; target < check->node_count; ++target) {
            if (check->joint_by_node[target] == joint->child) {
                break;
            }
        }
        if (target == check->node_count ||
            !check->mask[check->part_by_node[target]])
        {
            fprintf(stderr,
                    "%s costume %d live instance part %d targets a cold or "
                    "unknown joint\n",
                    check->fighter->name, check->costume,
                    check->part_by_node[node]);
            return -1;
        }
    }
    return 0;
}

static void init_config(MslCoreMatchConfig* config, const FighterSpec* fighter,
                        uint8_t costume)
{
    memset(config, 0, sizeof(*config));
    config->stage_id = 32;
    config->frame_id = -123;
    config->frame_pre_random_seed = 1;
    config->initial_random_seed = 1;
    config->match_damage_ratio = 1.0F;
    config->num_players = 2;
    config->stock_count = 4;
    config->players[0].char_id = fighter->external_id;
    config->players[0].costume_id = costume;
    config->players[1] = config->players[0];
}

static int verify_constructed_match(const FighterSpec* fighter, int part_count,
                                    const uint8_t* mask)
{
    Fighter* fp = GET_FIGHTER(match.fighters[0]);
    int owner;
    int part;

    if (fp->kind != fighter->kind) {
        fprintf(stderr, "%s constructed as kind %d\n", fighter->name,
                fp->kind);
        return -1;
    }
    for (part = 0; part < part_count; ++part) {
        HSD_JObj* joint = fp->parts[part].joint;
        int physical = ftParts_8007506C(fighter->kind, part) == 0;

        if (!physical) {
            if (joint != NULL) {
                fprintf(stderr, "%s nonphysical part %d has a JObj\n",
                        fighter->name, part);
                return -1;
            }
            continue;
        }
        if (joint == NULL ||
            ((joint->flags & JOBJ_MSL_GAMEPLAY_COLD) != 0) ==
                (mask[part] != 0))
        {
            fprintf(stderr,
                    "%s part %d does not match its live/cold admission\n",
                    fighter->name, part);
            return -1;
        }
    }
    for (owner = 0; owner < fp->hurt_capsules_len; ++owner) {
        HSD_JObj* joint = fp->hurt_capsules[owner].capsule.bone;
        if (joint == NULL || (joint->flags & JOBJ_MSL_GAMEPLAY_COLD)) {
            fprintf(stderr, "%s hurt capsule %d has a cold bone\n",
                    fighter->name, owner);
            return -1;
        }
    }
    for (owner = 0; owner < fp->x166C; ++owner) {
        HSD_JObj* joint = fp->x1670[owner].jobj;
        if (joint == NULL || (joint->flags & JOBJ_MSL_GAMEPLAY_COLD)) {
            fprintf(stderr, "%s collision owner %d has a cold bone\n",
                    fighter->name, owner);
            return -1;
        }
    }
    for (owner = 0; owner < fp->dynamics_num; ++owner) {
        struct DynamicsData* dynamic =
            fp->dynamic_bone_sets[owner].dyn_desc.data;
        for (; dynamic != NULL; dynamic = dynamic->next) {
            HSD_JObj* joint = dynamic->desc.lb_unk0.jobj;
            if (joint == NULL || (joint->flags & JOBJ_MSL_GAMEPLAY_COLD)) {
                fprintf(stderr, "%s retained dynamics owner %d is cold\n",
                        fighter->name, owner);
                return -1;
            }
        }
    }
    return 0;
}

int main(int argc, char** argv)
{
    static const FighterSpec fighters[] = {
        { FTKIND_FOX, 1, "Fox" },       { FTKIND_CAPTAIN, 2, "Falcon" },
        { FTKIND_SEAK, 7, "Sheik" },    { FTKIND_PEACH, 9, "Peach" },
        { FTKIND_PURIN, 15, "Puff" },   { FTKIND_LUIGI, 17, "Luigi" },
    { FTKIND_MARIO, 0, "Mario" },
    { FTKIND_DRMARIO, 21, "Dr. Mario" },
    { FTKIND_SAMUS, 13, "Samus" },
    { FTKIND_POPO, 10, "Popo" },
    { FTKIND_PIKACHU, 12, "Pikachu" },
        { FTKIND_MARS, 18, "Marth" },   { FTKIND_ZELDA, 19, "Zelda" },
        { FTKIND_FALCO, 22, "Falco" },
    };
    MslCoreInput previous_input = { 0 };
    int match_initialized = 0;
    size_t fighter_index;

    if (argc != 2) {
        fprintf(stderr, "usage: %s GAME_DATA\n", argv[0]);
        return 2;
    }
    if (msl_core_game_data_init(&game_data, argv[1]) != 0) {
        return 1;
    }

    for (fighter_index = 0;
         fighter_index < sizeof(fighters) / sizeof(fighters[0]);
         ++fighter_index)
    {
        const FighterSpec* fighter = &fighters[fighter_index];
        const struct UnkCostumeList* costumes =
            &game_data.source.fighter.costume_lists[fighter->kind];
        const uint8_t* mask = ftParts_HeadlessGameplayMask(fighter->kind);
        uint8_t part_by_node[FIGHTER_PARTS_ALLOC_COUNT];
        HSD_Joint* joint_by_node[FIGHTER_PARTS_ALLOC_COUNT];
        uint64_t expected_topology = 0;
        int part_count = ftPartsTable[fighter->kind]->parts_num;
        int physical_count = 0;
        int live_count = 0;
        int part;
        int costume;

        if (part_count <= 0 || part_count > FIGHTER_PARTS_ALLOC_COUNT ||
            costumes->numCostumes == 0)
        {
            fprintf(stderr, "%s has invalid source part/costume counts\n",
                    fighter->name);
            return 1;
        }
        for (part = 0; part < part_count; ++part) {
            if (ftParts_8007506C(fighter->kind, part) != 0) {
                continue;
            }
            part_by_node[physical_count++] = part;
            live_count += mask[part] != 0;
        }
        for (part = part_count; part < FIGHTER_PARTS_ALLOC_COUNT; ++part) {
            if (mask[part] != 0) {
                fprintf(stderr, "%s admits out-of-range part %d\n",
                        fighter->name, part);
                return 1;
            }
        }
        if (live_count == 0 || live_count >= physical_count) {
            fprintf(stderr, "%s does not remove a meaningful cold set\n",
                    fighter->name);
            return 1;
        }

        for (costume = 0; costume < costumes->numCostumes; ++costume) {
            MslCoreMatchConfig config;
            TopologyCheck topology = {
                .fighter = fighter,
                .costume = costume,
                .mask = mask,
                .part_by_node = part_by_node,
                .joint_by_node = joint_by_node,
                .node_capacity = physical_count,
                .hash = UINT64_C(1469598103934665603),
            };
            HSD_Joint* joint = costumes->costume_list[costume].joint;
            int result;

            if (joint == NULL ||
                inspect_joint_list(&topology, joint, 1, 0) != 0 ||
                topology.node_count != physical_count ||
                verify_instance_references(&topology) != 0)
            {
                fprintf(stderr, "%s costume %d topology is invalid\n",
                        fighter->name, costume);
                return 1;
            }
            if (costume == 0) {
                expected_topology = topology.hash;
            } else if (topology.hash != expected_topology) {
                fprintf(stderr,
                        "%s costume %d topology differs from costume 0\n",
                        fighter->name, costume);
                return 1;
            }

            init_config(&config, fighter, costume);
            result = match_initialized
                         ? msl_core_match_reset(&match, &game_data, &config,
                                                &previous_input)
                         : msl_core_match_init(&match, &game_data, &config,
                                               &previous_input);
            if (result != 0) {
                fprintf(stderr, "%s costume %d construction failed\n",
                        fighter->name, costume);
                return 1;
            }
            match_initialized = 1;
            if (verify_constructed_match(fighter, part_count, mask) != 0) {
                fprintf(stderr, "%s costume %d construction is invalid\n",
                        fighter->name, costume);
                return 1;
            }
        }
        printf("gameplay-parts: %-6s costumes=%u parts=%d live=%d cold=%d\n",
               fighter->name, costumes->numCostumes, physical_count,
               live_count, physical_count - live_count);
    }

    if (match_initialized) {
        msl_core_match_destroy(&match);
    }
    msl_core_game_data_deinit(&game_data);
    return 0;
}
