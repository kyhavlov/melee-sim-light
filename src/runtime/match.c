#include "runtime/match.h"

#include "ft/fighter.h"
#include "ft/ftdevice.h"
#include "ft/ftlib.h"
#include "gm/forward.h"
#include "gm/gm_1601.h"
#include "gr/ground.h"
#include "gr/stage.h"
#include "mp/mpcoll.h"
#include "pl/player.h"
#include "sfx/crowdsfx.h"

#include <string.h>
#include <dolphin/mtx.h>
#include <MetroTRK/intrinsics.h>

// Source callbacks do not carry a match argument. Bind them to the scalar
// match selected by the private runtime API while keeping their mutable rule
// and UCF ownership in caller-provided match storage.
// refs/melee/src/melee/gm/gm_16AE.c
// refs/ucf/src/pad_buffer/pad_buffer.cpp
static _Thread_local MslCoreMatchRules* msl_bound_match_rules;

#define msl_is_teams (msl_bound_match_rules->is_teams)
#define msl_friendly_fire (msl_bound_match_rules->friendly_fire)
#define msl_damage_ratio (msl_bound_match_rules->damage_ratio)
#define msl_online_fnmsubs_zero (msl_bound_match_rules->online_fnmsubs_zero)
#define msl_brawl_offscreen_damage \
    (msl_bound_match_rules->brawl_offscreen_damage)
#define msl_freeze_dead_up_fall_physics \
    (msl_bound_match_rules->freeze_dead_up_fall_physics)
#define msl_ucf_cardinals_1_0_enabled \
    (msl_bound_match_rules->ucf_cardinals_1_0_enabled)
#define msl_ucf_shield_sdi_enabled \
    (msl_bound_match_rules->ucf_shield_sdi_enabled)
#define msl_ucf_sdi_enabled (msl_bound_match_rules->ucf_sdi_enabled)
#define msl_ucf_shield_drop_extended_enabled \
    (msl_bound_match_rules->ucf_shield_drop_extended_enabled)
#define msl_match_frame_count (msl_bound_match_rules->frame_count)
#define msl_match_ended (msl_bound_match_rules->ended)
#define msl_respawn_reservation_timer                                         \
    (msl_bound_match_rules->respawn_reservation_timer)
#define msl_respawn_reservation_character                                     \
    (msl_bound_match_rules->respawn_reservation_character)
#define msl_ucf_pad (msl_bound_match_rules->ucf_pad)

void ftKb_SpecialN_800F1F1C(Fighter_GObj* gobj, Vec3* pos)
{
    (void) gobj;
    (void) pos;
    // The shared wall/ceiling-reflect owners call this hook without a kind
    // switch. Its source body spawns Kirby's copied-neutral-special effect
    // only when kind == FTKIND_KIRBY, so the Fox projection is an
    // exact no-op rather than an unresolved abort.
    // refs/melee/src/melee/ft/chara/ftKirby/ftkirby.c::
    //     ftKb_SpecialN_800F1F1C
}

static float msl_absf(float value)
{
    return value < 0.0F ? -value : value;
}

static int msl_ucf_rim_coord(float value)
{
    // refs/ucf/include/util/melee/pad.h::{abs_coord_to_int,is_rim_coord}.
    // The injected PPC assembly uses fmsubs before fctiwz; preserve that one
    // rounding step rather than allowing the host compiler to reassociate it.
    return (int) __fmsubs(msl_absf(value), 80.0F, 0.0001F) + 2;
}

static bool msl_ucf_is_rim_coord(Vec2 stick)
{
    int x = msl_ucf_rim_coord(stick.x);
    int y = msl_ucf_rim_coord(stick.y);
    return x * x + y * y > 80 * 80;
}

static bool msl_ucf_check_xsmash(const Fighter* fp)
{
    int slot = fp->x618_player_id;
    int difference = (int) msl_ucf_pad[slot].raw_x[0] -
                     (int) msl_ucf_pad[slot].raw_x[2];

    // refs/ucf/include/ucf/pad_buffer.h::check_ucf_xsmash
    return difference * difference > 75 * 75;
}

void msl_core_bind_match_rules(MslCoreMatchRules* rules)
{
    msl_bound_match_rules = rules;
}

void msl_core_match_rules_init(MslCoreMatchRules* rules, int is_teams,
                               int friendly_fire, float damage_ratio,
                               int online_fnmsubs_zero,
                               int brawl_offscreen_damage,
                               int freeze_dead_up_fall_physics,
                               int ucf_cardinals_1_0_enabled,
                               int ucf_shield_sdi_enabled,
                               int ucf_sdi_enabled,
                               int ucf_shield_drop_extended_enabled)
{
    memset(rules, 0, sizeof(*rules));
    msl_core_bind_match_rules(rules);
    msl_is_teams = is_teams;
    msl_friendly_fire = friendly_fire != 0;
    msl_damage_ratio = damage_ratio;
    msl_online_fnmsubs_zero = online_fnmsubs_zero != 0;
    msl_brawl_offscreen_damage = brawl_offscreen_damage != 0;
    msl_freeze_dead_up_fall_physics = freeze_dead_up_fall_physics != 0;
    msl_ucf_cardinals_1_0_enabled = ucf_cardinals_1_0_enabled != 0;
    msl_ucf_shield_sdi_enabled = ucf_shield_sdi_enabled != 0;
    msl_ucf_sdi_enabled = ucf_sdi_enabled != 0;
    msl_ucf_shield_drop_extended_enabled =
        ucf_shield_drop_extended_enabled != 0;
}

bool msl_core_uses_online_fnmsubs_zero(void)
{
    return msl_online_fnmsubs_zero;
}

bool msl_core_has_brawl_offscreen_damage(void)
{
    return msl_brawl_offscreen_damage;
}

bool msl_core_freezes_dead_up_fall_physics(void)
{
    return msl_freeze_dead_up_fall_physics;
}

void msl_core_advance_match_frame(void)
{
    int i;

    // refs/melee/src/melee/gm/gm_1601.c::fn_8016758C
    for (i = 0; i < ARRAY_SIZE(msl_respawn_reservation_timer); ++i) {
        if (msl_respawn_reservation_timer[i] != 0) {
            --msl_respawn_reservation_timer[i];
        }
    }
    ++msl_match_frame_count;
}

void msl_core_apply_team_stock_steal(void)
{
    int slot;

    if (!msl_is_teams) {
        return;
    }

    for (slot = 0; slot < 4; ++slot) {
        int donor;
        int controller;

        if (Player_GetPlayerSlotType(slot) == Gm_PKind_NA ||
            Player_GetEntity(slot) == NULL || !Player_8003219C(slot) ||
            Player_GetStocks(slot) != 0)
        {
            continue;
        }
        controller = Player_GetPlayerId(slot);
        if ((HSD_PadCopyStatus[controller].trigger & HSD_PAD_START) == 0) {
            continue;
        }

        for (donor = 0; donor < 4; ++donor) {
            if (donor != slot &&
                Player_GetPlayerSlotType(donor) != Gm_PKind_NA &&
                Player_GetTeam(donor) == Player_GetTeam(slot) &&
                Player_GetStocks(donor) > 1)
            {
                break;
            }
        }
        if (donor == 4) {
            continue;
        }

        // Direct headless projection of the standard team-stock transfer.
        // ifStock_802F7EFC only reserves and animates HUD stock icons; its
        // gameplay gate is already guaranteed here by donor stocks > 1.
        // The source updates stocks and creates the fighter before the GObj
        // scheduler for this frame.
        // refs/melee/src/melee/gm/gm_16AE.c::fn_8016B918
        // refs/melee/src/melee/if/ifstock.c::ifStock_802F7EFC
        Player_LoseStock(donor);
        Player_SetStocks(slot, Player_GetStocks(slot) + 1);
        gm_80167320(slot, false);
    }
}

void msl_ucf_seed_pad(int slot, s8 raw_x, s8 raw_y, s8 raw_cx, s8 raw_cy)
{
    MslCoreUcfPadBuffer* buffer = &msl_ucf_pad[slot];

    // The runtime bootstrap represents the state immediately after its seed
    // frame. UCF's zero-initialized four-entry ring has therefore published
    // that one sample while the older entries remain zero.
    buffer->raw_x[0] = raw_x;
    buffer->raw_y[0] = raw_y;
    buffer->pending_x = raw_x;
    buffer->pending_y = raw_y;
    buffer->pending_cx = raw_cx;
    buffer->pending_cy = raw_cy;
}

void msl_ucf_set_pending_pad(int slot, s8 raw_x, s8 raw_y, s8 raw_cx,
                             s8 raw_cy)
{
    MslCoreUcfPadBuffer* buffer = &msl_ucf_pad[slot];
    buffer->pending_x = raw_x;
    buffer->pending_y = raw_y;
    buffer->pending_cx = raw_cx;
    buffer->pending_cy = raw_cy;
}

static void msl_ucf_apply_cardinal(s8 raw_x, s8 raw_y, Vec2* stick)
{
    // refs/ucf/src/pad_buffer/pad_buffer.cpp::apply_cardinals.
    if ((raw_x <= -80 || raw_x >= 80) && raw_y >= -6 && raw_y <= 6) {
        stick->x = raw_x < 0 ? -1.0F : 1.0F;
        stick->y = 0.0F;
    } else if ((raw_y <= -80 || raw_y >= 80) && raw_x >= -6 && raw_x <= 6)
    {
        stick->x = 0.0F;
        stick->y = raw_y < 0 ? -1.0F : 1.0F;
    }
}

void msl_ucf_apply_pad_buffer(Fighter* fp)
{
    int slot = fp->x618_player_id;
    MslCoreUcfPadBuffer* buffer = &msl_ucf_pad[slot];
    int delta_y;

    // The gecko wraps its whole body in !Player_IsCPU (GALE01r2 0x800A2040 =
    // ftCo_800A2040): a CPU-driven fighter neither shifts the port's ring nor
    // has its AI stick replaced by the cardinal-snapped raw pad. Nana shares
    // Popo's port, so without this gate she would double-shift his ring and
    // walk on his live controller values.
    // refs/ucf/src/pad_buffer/pad_buffer.cpp::gecko_entry
    // refs/ucf/GALE01r2.map::Player_IsCPU
    if (ftCo_800A2040(fp)) {
        return;
    }

    // refs/ucf/src/pad_buffer/pad_buffer.cpp::gecko_entry, injected at
    // Fighter_Spaghetti_8006AD10+0x750 after vanilla input timers publish.
    buffer->raw_x[2] = buffer->raw_x[1];
    buffer->raw_x[1] = buffer->raw_x[0];
    buffer->raw_x[0] = buffer->pending_x;
    buffer->raw_y[2] = buffer->raw_y[1];
    buffer->raw_y[1] = buffer->raw_y[0];
    buffer->raw_y[0] = buffer->pending_y;

    // UCF 1.0 cardinals is a separate patch from the UCF 0.84 mechanics that
    // own this buffer. Preserve recordings made before the cardinal patch was
    // enabled instead of inferring their input profile from replay rows.
    // refs/ucf/src/pad_buffer/pad_buffer.cpp::gecko_entry
    if (msl_ucf_cardinals_1_0_enabled &&
        !(fp->kind == FTKIND_ZELDA && fp->motion_id == 349))
    {
        // UCF deliberately preserves Zelda's unsnapped raw stick during the
        // Farore's Wind startup state so the teleport angle is not changed.
        // refs/ucf/src/pad_buffer/pad_buffer.cpp::should_apply_cardinals
        // refs/ucf/include/melee/characters/zelda.h::AS_Zelda_SpecialHiStart
        msl_ucf_apply_cardinal(buffer->pending_x, buffer->pending_y,
                               &fp->input.lstick);
        msl_ucf_apply_cardinal(buffer->pending_cx, buffer->pending_cy,
                               &fp->input.cstick);
    }

    delta_y = (int) buffer->raw_y[0] - (int) buffer->raw_y[2];
    if (fp->input.lstick.y <= -0.6125F &&
        msl_ucf_is_rim_coord(fp->input.lstick) &&
        (buffer->sdrop_up_frames != 0 ||
         (fp->x671_timer_lstick_tilt_y < 2 &&
          delta_y * delta_y > 44 * 44)))
    {
        if (buffer->sdrop_up_frames != 0xFF) {
            ++buffer->sdrop_up_frames;
        }
    } else {
        buffer->sdrop_up_frames = 0;
    }
}

void msl_ucf_apply_dashback(Fighter* fp)
{
    float stick_x = fp->input.lstick.x;

    // Direct C translation of UCF 0.84/UCF/UCF Dashback.asm at the
    // ftCo_Turn_IASA 0x800C9A44 injection. The assembly reads the physical
    // current and two-frames-past raw X samples from HSD's five-entry ring.
    if (fp->x221F_b4 || fp->cur_anim_frame != 2.0F ||
        stick_x * fp->facing_dir < p_ftCommonData->x3C ||
        fp->x670_timer_lstick_tilt_x > 1)
    {
        return;
    }
    if (msl_ucf_check_xsmash(fp)) {
        fp->mv.co.turn.has_turned = 1;
        fp->mv.co.turn.just_turned = 1;
        // The gecko then retroactively writes the dashback into the paired
        // sub-character's mimic ring at the freshest slot so Nana dashes
        // back with Popo five frames later (the slot otherwise carries the
        // flick-transit stick sample and she tilt-turns instead).
        // refs/ucf/src/dashback/dashback.cpp::gecko_entry
        {
            HSD_GObj* sub_gobj = Player_GetEntityAtIndex(fp->player_id, 1);
            if (sub_gobj != NULL) {
                Fighter* sub_fp = GET_FIGHTER(sub_gobj);
                sub_fp->x1A88.x444->facing_dir = fp->facing_dir;
                sub_fp->x1A88.x444->lstickX =
                    (u8) (127 + (fp->facing_dir < 0.0F ? 1 : 0));
            }
        }
    }
}

bool msl_ucf_damagefall_wiggle_check(const Fighter* fp)
{
    u8 hold_time = fp->x670_timer_lstick_tilt_x;
    float last_stick_x;

    // Direct C translation of refs/ucf/src/tumble/tumble.cpp, injected at
    // ftCo_DamageFall_IASA+0xCC (0x800908F4). Vanilla already accepts a
    // zero-frame hold and rejects holds longer than one frame. For exactly one
    // frame, UCF accepts a hardware-polling swing only when the preceding
    // normalized sample was below the ordinary wiggle threshold.
    if (hold_time != 1) {
        return hold_time < 1;
    }
    last_stick_x = fp->input.lstick1.x;
    if (last_stick_x < 0.0F) {
        last_stick_x = -last_stick_x;
    }
    return last_stick_x < p_ftCommonData->x210 &&
           msl_ucf_check_xsmash(fp);
}

bool msl_ucf_sdi_check(const Fighter* fp)
{
    const MslCoreUcfPadBuffer* buffer = &msl_ucf_pad[fp->x618_player_id];
    int dx = (int) buffer->raw_x[0] - (int) buffer->raw_x[2];
    int dy = (int) buffer->raw_y[0] - (int) buffer->raw_y[2];
    float threshold = p_ftCommonData->sdi_min_stick_mag;

    // refs/ucf/src/sdi/sdi.cpp::check_f2_sdi. The caller invokes this only
    // after allow_sdi and the current-stick magnitude gate have succeeded.
    if (!msl_ucf_sdi_enabled || (fp->x673 >= 2 && fp->x674 >= 2)) {
        return false;
    }
    return fp->input.lstick1.x * fp->input.lstick1.x +
               fp->input.lstick1.y * fp->input.lstick1.y <
               threshold * threshold &&
           dx * dx + dy * dy > 62 * 62;
}

bool msl_ucf_shield_sdi_check(const Fighter* fp)
{
    const MslCoreUcfPadBuffer* buffer = &msl_ucf_pad[fp->x618_player_id];
    int dx = (int) buffer->raw_x[0] - (int) buffer->raw_x[2];

    // refs/ucf/src/shield_sdi/shield_sdi.cpp::check_f2_sdi. Preserve the
    // patch's signed last-X comparison; it intentionally does not use abs.
    return msl_ucf_shield_sdi_enabled && fp->x673 < 2 &&
           fp->input.lstick1.x < p_ftCommonData->sdi_min_stick_mag &&
           dx * dx > 62 * 62;
}

bool msl_ucf_suppress_spotdodge(const Fighter* fp)
{
    // Direct projection of refs/ucf/src/shielddrop/shielddrop.S. The common
    // Escape owner calls this at both vanilla spot-dodge entry sites.
    if (fp->input.cstick.y <= p_ftCommonData->x314 ||
        fp->x670_timer_lstick_tilt_x < p_ftCommonData->x320 ||
        fp->input.lstick.y <= -0.8F ||
        !mpColl_IsOnPlatform((CollData*) &fp->coll_data))
    {
        return false;
    }
    return msl_ucf_is_rim_coord(fp->input.lstick);
}

bool msl_ucf_pass_oos_stick_check(const Fighter* fp)
{
    // This is only the alternate stick-threshold result installed at
    // ftCo_8009A080+0x38. The original tilt-timer and platform checks execute
    // after the injection and remain owned by ftCo_Pass.c.
    // refs/slippi-ssbm-asm/External/UCF 0.84/UCF/
    //     UCF Shield Drop Extended.asm
    //
    // The extended (counter-based) shield drop is its own Slippi rollout,
    // independent of dashback, classic shield drop, SDI, and cardinal
    // snapping; recordings made before it shipped must not take the counter
    // branch (retail-probe-verified on the icies master-diamond frame -19:
    // the mimic-driven Nana keeps GuardOn off the port's hot counter and
    // vanilla-passes a frame later).
    if (!msl_ucf_shield_drop_extended_enabled) {
        return false;
    }
    return msl_ucf_pad[fp->x618_player_id].sdrop_up_frames >= 2;
}

float msl_ucf_squatrv_threshold(const Fighter* fp, float vanilla_threshold)
{
    // refs/ucf/src/dbooc/dbooc.S. Raise SquatRv's release threshold only for
    // a one-frame rim input; 0.5900 is the UCF 0.84 IC-safe value.
    if (fp->x670_timer_lstick_tilt_x < 1 &&
        msl_ucf_is_rim_coord(fp->input.lstick))
    {
        return 0.5900F;
    }
    return vanilla_threshold;
}

int gm_8016B014(void) { return 0; }
// refs/melee/src/melee/gm/gm_16AE.c::gm_8016B094. The current match is the
// ordinary stock-VS ruleset, so stock loss and final-stock gates are active.
int gm_8016B094(void) { return 1; }
int gm_8016B0B4(void) { return 0; }
int gm_8016B0D4(void) { return msl_friendly_fire; }
int gm_8016B0E8(void) { return 0; }
int gm_8016B110(void) { return 0; }
int gm_8016B168(void) { return msl_is_teams; }
int gm_8016B1C4(void) { return 0; }
// refs/melee/src/melee/gm/gm_16AE.c::gm_8016B1D8. Standard replay rules do
// not enable the x5_3 special-mode flag consumed by Sing's Stadium hook.
bool gm_8016B1D8(void) { return false; }
int gm_8016B204(void) { return 1; }
float gm_8016B248(void) { return msl_damage_ratio; }
static int msl_match_standings_score(int slot)
{
    int other;
    int kos = 0;
    int falls = Player_GetFalls(slot);
    int self_destructs = (int) Player_GetSuicideCount(slot);

    for (other = 0; other < 4; ++other) {
        int count;
        if (Player_GetPlayerSlotType(other) == Gm_PKind_NA) {
            continue;
        }
        count = Player_GetKOsByPlayerIndex(slot, other);
        if (other == slot) {
            self_destructs += count;
            falls += count;
        } else if (!msl_is_teams ||
                   Player_GetTeam(other) != Player_GetTeam(slot)) {
            kos += count;
        } else {
            self_destructs += count;
            falls += count;
        }
    }

    // Standard stock VS uses -1 for the self-destruct score rule.
    // refs/melee/src/melee/gm/gm_1601.c::{fn_8016588C,gm_80165AC0,
    //     fn_80165E7C,fn_80165FA4}
    return kos - (falls - self_destructs) - self_destructs;
}

int gm_8016C5C0(int slot)
{
    int other;
    int score = msl_match_standings_score(slot);
    int loser_rank = 0;
    u8 seen_teams = 0;

    // gm_8016C5C0 publishes MatchPlayerData::is_big_loser in singles and the
    // equivalent team standing in teams. Recompute the bounded source table
    // from the live Player statistics instead of retaining the scene-owned
    // MatchEnd reporting aggregate.
    // refs/melee/src/melee/gm/{gm_16AE.c::gm_8016C5C0,
    //     gm_1601.c::gm_80166378}
    for (other = 0; other < 4; ++other) {
        if (other == slot ||
            Player_GetPlayerSlotType(other) == Gm_PKind_NA) {
            continue;
        }
        if (!msl_is_teams) {
            if (score < msl_match_standings_score(other)) {
                ++loser_rank;
            }
        } else if (Player_GetTeam(other) != Player_GetTeam(slot)) {
            int i;
            int own_team_score = 0;
            int other_team_score = 0;
            int other_team = Player_GetTeam(other);
            if ((seen_teams & (1U << other_team)) != 0) {
                continue;
            }
            seen_teams |= (u8) (1U << other_team);
            for (i = 0; i < 4; ++i) {
                if (Player_GetPlayerSlotType(i) == Gm_PKind_NA) {
                    continue;
                }
                if (Player_GetTeam(i) == Player_GetTeam(slot)) {
                    own_team_score += msl_match_standings_score(i);
                }
                if (Player_GetTeam(i) == other_team) {
                    other_team_score += msl_match_standings_score(i);
                }
            }
            if (own_team_score < other_team_score) {
                ++loser_rank;
            }
        }
    }
    return loser_rank;
}

// CPU-input target selection reads the per-fighter standings through the
// GObj wrappers. Both forward to the recomputed source standings above.
// refs/melee/src/melee/gm/gm_16AE.c::{gm_8016C6C0,gm_8016C75C}
s32 gm_8016C6C0(HSD_GObj* arg0)
{
    return gm_8016C5C0(ftLib_80086BE0(arg0));
}

// MatchPlayerData::x20 accumulates KOs of non-teammates; team kills fold
// into the self-destruct/fall columns instead. Retail serves the
// lbl_8046B6A0.x24C scratch block guarded by the scene pointer, but that
// scratch is dirtied between reads within a match, so the observable
// behavior is a LIVE recompute: a playback-Dolphin probe of Nana's first
// state-10 tick after a KO (fd-falco frame 345) returns the updated count
// (1), which arms the x88 reaction timer in ftCo_800ADE48. Recompute per
// call; only CPU-input consumers reach this.
// refs/melee/src/melee/gm/{gm_16AE.c::gm_8016C75C,gm_1601.c::gm_80166378}
int gm_8016C75C(HSD_GObj* arg0)
{
    int slot = ftLib_80086BE0(arg0);

    {
        int self;

        for (self = 0; self < 6; ++self) {
            int other;
            int kos = 0;

            if (Player_GetPlayerSlotType(self) == Gm_PKind_NA) {
                continue;
            }
            for (other = 0; other < 6; ++other) {
                if (other == self ||
                    Player_GetPlayerSlotType(other) == Gm_PKind_NA) {
                    continue;
                }
                if (!msl_is_teams ||
                    Player_GetTeam(other) != Player_GetTeam(self)) {
                    kos += Player_GetKOsByPlayerIndex(self, other);
                }
            }
            msl_bound_match_rules->cpu_standings_kos[self] = kos;
        }
    }
    return msl_bound_match_rules->cpu_standings_kos[slot];
}

// The hosted bootstrap constructs the ordinary versus scene, so the scene
// router always reports the standard VS major mode.
// refs/melee/src/melee/gm/gm_1A3F.c::gm_801A4310
u8 gm_801A4310(void) { return GM_VS; }

// refs/melee/src/melee/gm/gm_16AE.c::gm_8016B14C reports the singles rule.
bool gm_8016B14C(void) { return !msl_is_teams; }

unsigned int gm_801A4BB8(void) { return 0; }
bool gm_801693BC(int slot)
{
    (void) slot;
    // Fox is neither a wireframe fighter nor CKIND_BOY/CKIND_GIRL.
    return false;
}
u32 gm_8016AEDC(void) { return msl_match_frame_count; }
u32 gm_8016AEEC(void) { return 0; }
int gm_8017E068(void) { return -1; }
bool gm_8018841C(void) { return false; }

static int msl_respawn_position(int slot, Vec3* position, Vec3* offset)
{
    static const f32 offsets[6] = { 0.0F, 1.0F, -1.0F,
                                    2.0F, 0.0F, 0.0F };
    int character = Player_GetPlayerCharacter(slot);
    int index;

    // refs/melee/src/melee/gm/gm_1601.c::fn_80167638
    // data/raw/main.dol::lbl_803B7A44 (symbols.txt address 0x803B7A44)
    if (stage_info.unk8C.b4) {
        Stage_80224E38(position, slot);
        memset(offset, 0, sizeof(*offset));
        return slot;
    }

    index = 0;
    while (index < ARRAY_SIZE(msl_respawn_reservation_timer) &&
           msl_respawn_reservation_timer[index] != 0)
    {
        ++index;
    }
    if (index == ARRAY_SIZE(msl_respawn_reservation_timer)) {
        index = 0;
    }
    Stage_80224E38(position, 0);
    offset->x = 16.0F * offsets[index];
    offset->y = 0.0F;
    offset->z = 0.0F;
    msl_respawn_reservation_timer[index] = 0x90;
    msl_respawn_reservation_character[index] = character;
    return 0;
}

// Source respawn owner for the supported local-stock match domain. The
// single-player and transformed sub-character teardown branches remain
// outside this boundary.
// refs/melee/src/melee/gm/gm_1601.c::{fn_8016719C,fn_80167638,gm_80167320}
void gm_80167320(int slot, bool subchar)
{
    Vec3 respawn_pos;
    Vec3 offset;
    int ground_index;

    if (Player_GetFlagsBit1(slot) != 0) {
        // Spectator/wireframe teardown is outside the supported local-match
        // domain. Normal human Fighter creation leaves this flag clear.
        return;
    }

    if (Player_GetStocks(slot) == 0) {
        // The source does not create another fighter on the final stock.
        // gm_8016AC44 only updates scene-owned costume bookkeeping; retain the
        // gameplay-visible terminal fact locally for the headless match owner.
        msl_match_ended = true;
        return;
    }

    ground_index = msl_respawn_position(slot, &respawn_pos, &offset);
    respawn_pos.x += offset.x;
    Player_SetSpawnPlatformPos(slot, &respawn_pos);
    if (!stage_info.unk8C.b5) {
        Player_80032FA4(slot, ground_index);
        Player_SetSomePos(slot, &offset);
    }
    respawn_pos.y = Stage_GetCamBoundsTopOffset();
    Player_80032768(slot, &respawn_pos);
    Player_SetFacingDirection(slot,
                              respawn_pos.x >= 0.0F ? -1.0F : 1.0F);
    Player_SetHPByIndex(slot, subchar, 0);
    Player_80032070(slot, subchar);
}

bool msl_core_match_is_over(void) { return msl_match_ended; }

// Source respawn calls these to reset HUD presentation state. They do not
// publish any fighter, item, stage-collision, or match-rule gameplay data in
// the headless boundary. The linked camera owner retains its own state reset.
// refs/melee/src/melee/if/ifstatus.c::{ifStatus_802F6508,ifStatus_802F6E1C}
void ifStatus_802F6508(int slot) { (void) slot; }
void ifStatus_802F6E1C(int slot) { (void) slot; }

// refs/melee/src/melee/ft/chara/ftKirby/ftkirbyspecialn.c. ProcessHit calls
// this for every fighter, but its only behavior (and RNG consumption) is
// guarded by FTKIND_KIRBY. The declared Fox/FD domain contains Fox only.
void ftKb_SpecialN_800F5BA4(Fighter* fp) { (void) fp; }

// Same Kirby-only copy-ability loss gate as 800F5BA4, used by a second
// common damage/throw path (ftkirbyspecialn.c::ftKb_SpecialN_800F5C34).
void ftKb_SpecialN_800F5C34(Fighter* fp) { (void) fp; }

// refs/melee/src/melee/ft/chara/ftMewtwo/ftMt_SpecialN.c. Forward throw
// calls this common hook unconditionally; its complete body is guarded by
// FTKIND_MEWTWO and therefore has no Fox-domain behavior.
void ftMt_SpecialN_Shoot(Fighter_GObj* gobj) { (void) gobj; }

// GALE01 versus replays use the US language setting. These are the direct
// fixed-domain projections of refs/melee/src/melee/lb/lblanguage.c; retaining
// the memory-card/menu-owned gmMainLib aggregate would add no gameplay state.
enum_t lbLang_GetLanguageSetting(void) { return 1; }
bool lbLang_IsSettingJP(void) { return false; }
bool lbLang_IsSettingUS(void) { return true; }

// Source stage accessors. the hosted core initializes stage_info directly from
// the loaded Final Destination archive instead of retaining the scene
// frontend.
float Stage_GetCamBoundsBottomOffset(void)
{
    return stage_info.cam_info.cam_bounds.bottom +
           stage_info.cam_info.cam_y_offset;
}

float Stage_GetCamBoundsTopOffset(void)
{
    return stage_info.cam_info.cam_bounds.top +
           stage_info.cam_info.cam_y_offset;
}

// refs/melee/src/melee/gr/stage.c::Stage_80224E38.
void Stage_80224E38(Vec3* out, s32 spawn_index)
{
    Ground_801C2D24(spawn_index + 4, out);
}

float Stage_GetBlastZoneRightOffset(void)
{
    return stage_info.blast_zone.right + stage_info.cam_info.cam_x_offset;
}

float Stage_GetBlastZoneLeftOffset(void)
{
    return stage_info.blast_zone.left + stage_info.cam_info.cam_x_offset;
}

float Stage_GetBlastZoneTopOffset(void)
{
    return stage_info.blast_zone.top + stage_info.cam_info.cam_y_offset;
}

float Stage_GetBlastZoneBottomOffset(void)
{
    return stage_info.blast_zone.bottom + stage_info.cam_info.cam_y_offset;
}

float Stage_CalcUnkCamY(void)
{
    // refs/melee/src/melee/gr/stage.c::Stage_CalcUnkCamY.
    float cam_y_offset = stage_info.cam_info.cam_y_offset;
    float y_pos = stage_info.cam_info.cam_bounds.bottom + cam_y_offset +
                  (stage_info.blast_zone.bottom + cam_y_offset);
    return 0.5F * y_pos;
}

float Stage_CalcUnkCamYBounds(void)
{
    // refs/melee/src/melee/gr/stage.c::Stage_CalcUnkCamYBounds. Retain the
    // source grouping because this threshold is observed by fighter state.
    float cam_offset = stage_info.cam_info.cam_bounds.bottom +
                       stage_info.cam_info.cam_y_offset;
    float y_pos_product =
        0.5F * ((stage_info.cam_info.cam_bounds.bottom +
                 stage_info.cam_info.cam_y_offset) +
                (stage_info.blast_zone.bottom +
                 stage_info.cam_info.cam_y_offset));
    return 0.5F * (cam_offset + y_pos_product);
}

void Stage_UnkSetVec3TCam_Offset(Vec3* out)
{
    out->x = stage_info.cam_info.cam_x_offset;
    out->y = stage_info.cam_info.cam_y_offset;
    out->z = 0.0F;
}

// Corneria's Fox/Falco easter-egg gates are queried by common taunt input even
// on other stages. Their exact Final Destination projection is false; the
// follow-up stage callbacks cannot be entered.
bool grCorneria_801E2CE8(void) { return false; }
bool grCorneria_801E2B80(void) { return false; }
bool grCorneria_801E2C34(void) { return false; }
void grCorneria_801E2AF4(void) {}

// Crowd audio is headless, but the crowd configuration's angle multiplier is
// used in source knockback calculation and therefore remains gameplay state.
float un_803222EC(float magnitude, float angle)
{
    if (!(angle > gCrowdConfig->angle_min) ||
        !(angle < gCrowdConfig->angle_max))
    {
        return magnitude;
    }
    return magnitude * gCrowdConfig->angle_mult;
}

void un_8032233C(u32 source, u32 victim)
{
    (void) source;
    (void) victim;
}

// These source declarations rely on adjacent DOL data placement. Hosted ELF
// gives them explicit zero-initialized storage; Final Destination registers no
// fighter catch/bury devices.

// Debug sound enables the source grounded IK pass when nonzero.
int db_804D4AF8 = 1;

// Original sdata2 constants referenced by ftCo_Attack100.c. The matching DOL
// values are the source's ubiquitous 0.0/1.0 motion-state tuple constants.
float ftCo_804D9018 = 0.0F;
int ftCo_804D9020 = 0x00000000;
int ftCo_804D9024 = 0x3F800000;
int ftCo_804D9028 = 0x00000000;
int ftCo_804D902C = 0x3F800000;
int ftCo_804D9030 = 0x00000000;
int ftCo_804D9034 = 0x3F800000;
int ftCo_804D9038 = 0x00000000;
int ftCo_804D903C = 0x3F800000;
float ftCo_804D90D0 = 0.0F;
float ftCo_804D90D4 = 1.0F;
double ftCo_804D90D8 = 1.0;
