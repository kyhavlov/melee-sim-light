#include "itanimlist.h"

#include "it_2725.h"
#include "it_3F14.h"
#include "iteffect.h"
#include "ithitbox.h"

#include "it/forward.h"

#include "it/inlines.h"
#include "it/itcoll.h"
#include "it/item.h"
#include "lb/inlines.h"
#include "lb/lbcommand.h"
#include "lb/lbspdisplay.h"

#include <baselib/gobjproc.h>

static void sdata2_order(void)
{
    (void) 0.00390599994f;
    (void) 4503599627370496.0;
    (void) 4503601774854144.0;
    (void) 1.0f;
    (void) 0.0f;
    (void) 3.40282347e+38f;
}

ItCmd it_803F22A8[16] = {
    it_80278F2C, it_802790C0, it_80279544, it_802795EC,
    it_80279680, it_802796C4, it_8027978C, it_802796FC,
    it_80279720, it_80279744, it_80279768, it_80279888,
    it_802798D4, it_8027990C, it_80279958, it_802799A8,
};

static inline u16 itAnimlist_GetU16(const CommandInfo* cmd, int index)
{
#ifdef MSL_CORE_NATIVE
    const u8* bytes = (const u8*) cmd->u;

    // Native DAT translation retains each authored PPC command word in
    // source byte order. Read its halfwords explicitly instead of applying
    // the host's little-endian u16 view.
    // refs/melee/src/melee/it/itanimlist.c::{it_80278F2C,it_80279544}
    return ((u16) bytes[index * 2] << 8) | bytes[index * 2 + 1];
#else
    return ((const u16*) cmd->u)[index];
#endif
}

static inline s16 itAnimlist_GetS16(const CommandInfo* cmd, int index)
{
    return (s16) itAnimlist_GetU16(cmd, index);
}

static inline u32 itAnimlist_GetU32(const CommandInfo* cmd)
{
#ifdef MSL_CORE_NATIVE
    const u8* bytes = (const u8*) cmd->u;

    // Native DAT translation keeps authored PPC command words in source byte
    // order inside widened CmdUnion slots. Decode the word explicitly: GCC's
    // scalar-storage-order bitfield views are not reliable for every signed /
    // unsigned overlay used by the item command interpreter.
    // refs/melee/src/melee/it/itanimlist.c::it_802790C0
    // refs/melee/src/melee/lb/types.h::{it_create_hitbox_0,
    //                                  spawn_hitbox_1,spawn_hitbox_2,
    //                                  spawn_hitbox_3,it_create_hitbox_4}
    return ((u32) bytes[0] << 24) | ((u32) bytes[1] << 16) |
           ((u32) bytes[2] << 8) | bytes[3];
#else
    return *(const u32*) cmd->u;
#endif
}

void it_80278F2C(Item_GObj* item_gobj, CommandInfo* cmd)
{
    Vec3 sp20;
    Vec3 sp14;
    s32 arg2;
    u16 ef_id;
    s32 arg6;
    PAD_STACK(4);

    arg2 = itAnimlist_GetU16(cmd, 0);
    arg2 = arg2 & 0x3FF;
    ++cmd->u;
    arg6 = (f32) itAnimlist_GetU16(cmd, 1);
    ef_id = itAnimlist_GetU16(cmd, 0);
    ++cmd->u;
    sp20.x = 0.003906f * itAnimlist_GetS16(cmd, 0);
    sp20.y = 0.003906f * itAnimlist_GetS16(cmd, 1);
    ++cmd->u;
    sp20.z = 0.003906f * itAnimlist_GetS16(cmd, 0);
    sp14.x = 0.003906f * itAnimlist_GetS16(cmd, 1);
    ++cmd->u;
    sp14.y = 0.003906f * itAnimlist_GetS16(cmd, 0);
    sp14.z = 0.003906f * itAnimlist_GetS16(cmd, 1);
    ++cmd->u;
    it_80278800(item_gobj, ef_id, arg2, &sp20, &sp14, 0, arg6);
}

#pragma push
#pragma dont_inline on

void it_802790C0(Item_GObj* item_gobj, CommandInfo* cmd)
{
    u8 _padA[16];
    Item* item = item_gobj->user_data;
    struct ItemHitbox* hb;
    HitCapsule* hit;
    u32 word = itAnimlist_GetU32(cmd);
    u32 hitbox_idx = (word >> 23) & 7;
    u32 x4 = (word >> 20) & 7;
    u32 bone_idx;
    hb = &item->x5D4_hitboxes[hitbox_idx];
    hit = &hb->hit;

    if (hit->state == HitCapsule_Disabled || hit->x4 != x4) {
        hit->x4 = x4;
        hit->state = HitCapsule_Enabled;
        item->xDC8_word.flags.x16 = 1;
        item->xDAA_flag.b2 = 0;
        it_8026FCF8(item, hit);
    }

    bone_idx = (word >> 13) & 0x7F;
    if (bone_idx != 0) {
        if (item->xBBC_dynamicBoneTable == NULL) {
            HSD_ASSERTREPORT(0x8B, 0, "item can\'t set attack!\n");
        }
        hit->jobj = item->xBBC_dynamicBoneTable->bones[bone_idx];
    } else {
        hit->jobj = item_gobj->hsd_obj;
    }
    it_80272460(hit,
                item->xC3C *
                    ((f32) (word & 0x1FFF) * item->xC40),
                item_gobj);
    ++cmd->u;

    word = itAnimlist_GetU32(cmd);
    hit->scale = 0.003906f * (word >> 16);
    item->x3C = hit->scale;
    it_80275594(item_gobj, hitbox_idx, 1.0f / item->scl);
    hit->b_offset.x = 0.003906f * (s16) (word & 0xFFFF);
    ++cmd->u;
    word = itAnimlist_GetU32(cmd);
    hit->b_offset.y = 0.003906f * (s16) (word >> 16);
    hit->b_offset.z = 0.003906f * (s16) (word & 0xFFFF);
    ++cmd->u;

    word = itAnimlist_GetU32(cmd);
    hit->kb_angle = (word >> 23) & 0x1FF;
    hit->x24 = (word >> 14) & 0x1FF;
    hit->x28 = (word >> 5) & 0x1FF;
    hit->x43_b1 = 0;
    ++cmd->u;

    word = itAnimlist_GetU32(cmd);
    hit->x2C = (word >> 23) & 0x1FF;
    hit->element = (word >> 18) & 0x1F;
    hit->x40_b0 = (word >> 17) & 1;
    hit->x40_b1 = 0;
    hit->x34 = (s8) ((word >> 9) & 0xFF);
    hit->sfx_severity = (word >> 6) & 7;
    hit->sfx_kind = (word >> 2) & 0xF;
    hit->x40_b2 = word & 1;
    hit->x40_b3 = (word >> 1) & 1;
    ++cmd->u;

    hit->x40_b4 = ((u8*) cmd->u)[0];
    hit->x41_b4 = (((u8*) cmd->u)[1] >> 7) & 1;
    hit->x41_b5 = (((u8*) cmd->u)[1] >> 6) & 1;
    hit->x41_b6 = (((u8*) cmd->u)[1] >> 5) & 1;
    hit->x41_b7 = (((u8*) cmd->u)[1] >> 4) & 1;
    hit->x42_b0 = (((u8*) cmd->u)[1] >> 3) & 1;
    hit->x42_b1 = (((u8*) cmd->u)[1] >> 2) & 1;
    hit->x42_b2 = (((u8*) cmd->u)[1] >> 1) & 1;
    hit->x42_b3 = ((u8*) cmd->u)[1] & 1;
    hit->x42_b4 = (((u8*) cmd->u)[2] >> 7) & 1;
    hit->x42_b5 = (((u8*) cmd->u)[2] >> 6) & 1;
    hit->x42_b6 = (((u8*) cmd->u)[2] >> 5) & 1;
    hit->x42_b7 = (((u8*) cmd->u)[2] >> 4) & 1;
    hit->x43_b0 = (((u8*) cmd->u)[2] >> 3) & 1;
    hb->x138 = (((u8*) cmd->u)[2] >> 2) & 1;
    ++cmd->u;

    hit->x43_b2 = 0;
    if (HSD_GObj_804D7838 != NULL && HSD_GObj_804D7838->s_link > 11) {
        it_8027129C(item_gobj, hitbox_idx);
    }
}

#pragma pop

#pragma push
#pragma dont_inline on

void it_80279544(Item_GObj* item_gobj, CommandInfo* cmd)
{
    Item* item = item_gobj->user_data;
    HitCapsule* hit =
        &item->x5D4_hitboxes[MSL_CMD_FIELD(cmd, set_hitbox_damage, idx)].hit;
    u32 val = itAnimlist_GetU16(cmd, 1) & 0x1FFF;
    PAD_STACK(8);
    it_80272460(hit, (u32) (item->xC3C * ((f32) val * item->xC40)), item_gobj);
    ++cmd->u;
}

void it_802795EC(Item_GObj* item_gobj, CommandInfo* cmd)
{
    s32 idx = MSL_CMD_FIELD(cmd, set_hitbox_scale, idx);
    Item* item = item_gobj->user_data;
    HitCapsule* hit = &item->x5D4_hitboxes[idx].hit;
    PAD_STACK(8);
    hit->scale = 0.003906f * MSL_CMD_FIELD(cmd, set_hitbox_scale, value);
    item->x3C = hit->scale;
    it_80275594(item_gobj, idx, 1.0f / item->scl);
    ++cmd->u;
}

#pragma pop

#pragma push
#pragma dont_inline on

void it_80279680(Item_GObj* item_gobj, CommandInfo* cmd)
{
    it_80272560(item_gobj, MSL_CMD_FIELD(cmd, set_throw_flags, hit_idx));
    ++cmd->u;
}

void it_802796C4(Item_GObj* item_gobj, CommandInfo* cmd)
{
    it_802725D4(item_gobj);
    ++cmd->u;
}

#pragma pop

void it_802796FC(Item_GObj* item_gobj, CommandInfo* cmd)
{
    Item* it = GET_ITEM(item_gobj);
    it->xDAC_itcmd_var0 = MSL_CMD_FIELD(cmd, set_throw_flags, hit_idx);
    ++cmd->u;
}

void it_80279720(Item_GObj* item_gobj, CommandInfo* cmd)
{
    Item* it = GET_ITEM(item_gobj);
    it->xDB0_itcmd_var1 = MSL_CMD_FIELD(cmd, set_throw_flags, hit_idx);
    ++cmd->u;
}

void it_80279744(Item_GObj* item_gobj, CommandInfo* cmd)
{
    Item* it = GET_ITEM(item_gobj);
    it->xDB4_itcmd_var2 = MSL_CMD_FIELD(cmd, set_throw_flags, hit_idx);
    ++cmd->u;
}

void it_80279768(Item_GObj* gobj, CommandInfo* cmd)
{
    Item* ip = GET_ITEM(gobj);
    ip->xDBC_itcmd_var4.flags.x0 = true;
    ++cmd->u;
}

#pragma push
#pragma dont_inline on
void it_8027978C(Item_GObj* item_gobj, CommandInfo* cmd)
{
    Item* item = item_gobj->user_data;
    u32 word = itAnimlist_GetU32(cmd);
    s32 opcode = (word >> 18) & 0xFF;
    u32 arg1;
    u8 arg2;
    u8 arg3;
    PAD_STACK(8);
    // The source cursor advances one 4-byte CmdUnion. Native CmdUnion is
    // widened to hold translated control-flow pointers, so advancing the
    // temporary 4-byte command view would land in slot padding.
    // refs/melee/src/melee/it/itanimlist.c::it_8027978C
    // refs/melee/src/melee/lb/types.h::CmdUnion
    ++cmd->u;
    if (opcode < 10) {
        if (opcode < 3) {
            if (opcode >= 0) {
                goto low_opcode;
            }
        }
        goto done;
    } else {
        if (opcode >= 12) {
            goto done;
        }
        goto high_opcode;
    }

low_opcode:
    arg1 = itAnimlist_GetU32(cmd);
    ++cmd->u;
    arg2 = ((u8*) cmd->u)[2];
    arg3 = ((u8*) cmd->u)[3];
    switch (opcode) {
    case 0:
        Item_8026AE84(item, arg1, arg2, arg3);
        break;
    case 1:
        Item_8026AF0C(item, arg1, arg2, arg3);
        break;
    case 2:
        Item_8026AFA0(item, arg1, arg2, arg3);
        break;
    }
    goto done;

high_opcode: {
    ++cmd->u;
    switch (opcode) {
    case 10:
        Item_8026B034(item);
        break;
    case 11:
        Item_8026B074(item);
        break;
    }
}
done:
    ++cmd->u;
}
#pragma pop

#pragma push
#pragma dont_inline on

void it_80279888(Item_GObj* item_gobj, CommandInfo* cmd)
{
    PAD_STACK(4);
    it_80273598(item_gobj, MSL_CMD_FIELD(cmd, unk33, unk0),
                MSL_CMD_FIELD(cmd, unk33, unk1));
    NEXT_CMD(cmd);
}

void it_802798D4(Item_GObj* item_gobj, CommandInfo* cmd)
{
    PAD_STACK(4);
    it_80273600(item_gobj);
    NEXT_CMD(cmd);
}

void it_8027990C(Item_GObj* item_gobj, CommandInfo* cmd)
{
    PAD_STACK(4);
    it_80273648(item_gobj, MSL_CMD_FIELD(cmd, unk33, unk0),
                MSL_CMD_FIELD(cmd, unk33, unk1));
    NEXT_CMD(cmd);
}

#pragma pop

void it_80279958(Item_GObj* item_gobj, CommandInfo* cmd)
{
    Item* it = GET_ITEM(item_gobj);
    it_80279B88(it, MSL_CMD_FIELD(cmd, unk13, unk1),
                MSL_CMD_FIELD(cmd, unk13, unk2));
    NEXT_CMD(cmd);
}

void it_802799A8(Item_GObj* item_gobj, CommandInfo* cmd)
{
    it_80279BBC(item_gobj->user_data);
    ++cmd->u;
}

void it_802799E4(Item_GObj* item_gobj)
{
    Item* item = GET_ITEM(item_gobj);
    CommandInfo* cmd = &item->x524_cmd;
    u32 opcode;

    cmd->frame_count = item->x5CC_currentAnimFrame;
    item->xDBC_itcmd_var4_word = 0;

    if (cmd->u == NULL) {
        return;
    }

    if (cmd->timer != F32_MAX) {
        cmd->timer -= item->x5D0_animFrameSpeed;
    }

loop:
    if (cmd->u == NULL) {
        return;
    }
    if (cmd->timer == F32_MAX) {
        if (cmd->frame_count >= item->x5D0_animFrameSpeed) {
            return;
        }
        cmd->timer = -cmd->frame_count;
    } else if (cmd->timer > 0.0f) {
        return;
    }

    opcode = MSL_CMD_FIELD(cmd, unk0, opcode);
    if (Command_Execute(cmd, opcode) != 0) {
        goto loop;
    }
    opcode -= 10;
    it_803F22A8[opcode](item_gobj, cmd);
    goto loop;
}

#pragma push
#pragma dont_inline on

void it_80279AF0(Item_GObj* item_gobj, CommandInfo* cmd)
{
    it_80278F2C(item_gobj, (CommandInfo*) cmd);
}

#pragma pop

void it_80279B10(Item_GObj* item_gobj, CommandInfo* cmd)
{
    it_8027978C(item_gobj, cmd);
}

static ItCmd it_804D51C8[2] ATTRIBUTE_ALIGN(8) = {
    it_80279AF0,
    it_80279B10,
};

void fn_80279B30(Item_GObj* item_gobj, CommandInfo* cmd, int arg2)
{
    int idx = arg2 - 21;
    it_804D51C8[idx](item_gobj, cmd);
}

void it_80279B64(Item* item)
{
    lb_80014498(&item->x548_colorOverlay);
}

void it_80279B88(Item* item, s32 arg1, s32 arg2)
{
    lb_800144C8(&item->x548_colorOverlay, it_804D6D04, arg1, arg2);
}

void it_80279BBC(Item* item)
{
    lb_80014498(&item->x548_colorOverlay);
}

void it_80279BE0(Item_GObj* item_gobj)
{
    Item* item = GET_ITEM(item_gobj);
    while (lb_80014258(item_gobj, &item->x548_colorOverlay, fn_80279B30)) {
        lb_80014498(&item->x548_colorOverlay);
    }
}
