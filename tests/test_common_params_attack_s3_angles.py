from __future__ import annotations

import json
from pathlib import Path


def test_ft_common_data_extracts_attack_s3_decideangle_thresholds() -> None:
    # Foundational correctness guard:
    # - ftCo_AttackS3_CheckInput first gates on ABS(ftCo_GetLStickAngle(fp)) < x20_radians.
    # - decideAngle then splits the accepted side-tilt cone with x9C/xA0/xA4/xA8.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackS3.c::{
    #   ftCo_AttackS3_CheckInput,decideAngle
    # }
    # refs/melee/src/melee/ft/types.h (ftCommonData offsets +9C/+A0/+A4/+A8)
    common = json.loads(Path("data/common/ft_common_data.json").read_text())

    attack_angle = float(common["attack_angle_threshold_radians"])
    hi = float(common["attack_s3_hi_angle_radians"])
    hi_s = float(common["attack_s3_hi_s_angle_radians"])
    lw_s = float(common["attack_s3_lw_s_angle_radians"])
    lw = float(common["attack_s3_lw_angle_radians"])

    assert attack_angle > 0.0
    assert attack_angle > hi > hi_s > 0.0
    assert 0.0 > lw_s > lw > -attack_angle
