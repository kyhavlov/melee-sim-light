#include "runtime/match.h"

#include "ft/ftdevice.h"
#include "ft/fighter.h"
#include "gr/ground.h"
#include "gr/stage.h"
#include "mp/mpcoll.h"
#include "pl/player.h"
#include "sfx/crowdsfx.h"

#include <MetroTRK/intrinsics.h>
#include <dolphin/mtx.h>
#include <string.h>

// Source callbacks do not carry a match argument. Bind them to the scalar
// match selected by the private runtime API while keeping their mutable rule
// and UCF ownership in caller-provided match storage.
// refs/melee/src/melee/gm/gm_16AE.c
// refs/ucf/src/pad_buffer/pad_buffer.cpp
static MslCoreMatchRules* msl_bound_match_rules;

#define msl_is_teams (msl_bound_match_rules->is_teams)
#define msl_damage_ratio (msl_bound_match_rules->damage_ratio)
#define msl_online_fnmsubs_zero (msl_bound_match_rules->online_fnmsubs_zero)
#define msl_brawl_offscreen_damage \
    (msl_bound_match_rules->brawl_offscreen_damage)
#define msl_freeze_dead_up_fall_physics \
    (msl_bound_match_rules->freeze_dead_up_fall_physics)
#define msl_match_frame_count (msl_bound_match_rules->frame_count)
#define msl_match_ended (msl_bound_match_rules->ended)
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
                               float damage_ratio, int online_fnmsubs_zero,
                               int brawl_offscreen_damage,
                               int freeze_dead_up_fall_physics)
{
    memset(rules, 0, sizeof(*rules));
    msl_core_bind_match_rules(rules);
    msl_is_teams = is_teams;
    msl_damage_ratio = damage_ratio;
    msl_online_fnmsubs_zero = online_fnmsubs_zero != 0;
    msl_brawl_offscreen_damage = brawl_offscreen_damage != 0;
    msl_freeze_dead_up_fall_physics = freeze_dead_up_fall_physics != 0;
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

void msl_core_advance_match_frame(void) { ++msl_match_frame_count; }

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

    // refs/ucf/src/pad_buffer/pad_buffer.cpp::gecko_entry, injected at
    // Fighter_Spaghetti_8006AD10+0x750 after vanilla input timers publish.
    buffer->raw_x[2] = buffer->raw_x[1];
    buffer->raw_x[1] = buffer->raw_x[0];
    buffer->raw_x[0] = buffer->pending_x;
    buffer->raw_y[2] = buffer->raw_y[1];
    buffer->raw_y[1] = buffer->raw_y[0];
    buffer->raw_y[0] = buffer->pending_y;

    // Fox always takes should_apply_cardinals' default branch. Zelda's
    // teleport exception belongs to the later supported-character phase.
    msl_ucf_apply_cardinal(buffer->pending_x, buffer->pending_y,
                           &fp->input.lstick);
    msl_ucf_apply_cardinal(buffer->pending_cx, buffer->pending_cy,
                           &fp->input.cstick);

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
        (stick_x < 0.0F ? -stick_x : stick_x) < p_ftCommonData->x3C ||
        fp->x670_timer_lstick_tilt_x > 1)
    {
        return;
    }
    if (msl_ucf_check_xsmash(fp)) {
        fp->mv.co.turn.has_turned = 1;
        fp->mv.co.turn.just_turned = 1;
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
    if (fp->x673 >= 2 && fp->x674 >= 2) {
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
    return fp->x673 < 2 &&
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
int gm_8016B0D4(void) { return 0; }
int gm_8016B0E8(void) { return 0; }
int gm_8016B110(void) { return 0; }
int gm_8016B168(void) { return msl_is_teams; }
int gm_8016B1C4(void) { return 0; }
int gm_8016B204(void) { return 1; }
float gm_8016B248(void) { return msl_damage_ratio; }
int gm_8016C5C0(int slot)
{
    (void) slot;
    return 0;
}
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

// Exact Fox/Final Destination branch of
// refs/melee/src/melee/gm/gm_1601.c::{fn_8016719C,gm_80167320}. The omitted
// branches belong to single-player modes, transformed/sub-character fighters,
// and stages whose respawn points move. Final Destination publishes b4/b5 in
// grLast_OnInit, so its source branch selects the player's fixed spawn index,
// uses a zero platform offset, and does not retain a moving-ground index.
void gm_80167320(int slot, bool subchar)
{
    Vec3 respawn_pos;

    if (Player_GetFlagsBit1(slot) != 0) {
        // Spectator/wireframe teardown is outside the two-human Fox/FD
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

    Stage_80224E38(&respawn_pos, slot);
    Player_SetSpawnPlatformPos(slot, &respawn_pos);
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

// Source stage accessors. the hosted core initializes stage_info directly from the
// loaded Final Destination archive instead of retaining the scene frontend.
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
struct ftDeviceUnk5 ftDevice_BuryThings[2];
struct ftDeviceUnk3 ft_80459A8C;

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
