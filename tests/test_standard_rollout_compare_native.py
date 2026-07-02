import numpy as np
import pytest

import msl_binding
from tools.eval.validation_dtypes import COMPARE_DTYPE


_CODE_TO_FIELD = {
    0: None,
    1: "action_id",
    2: "animation_index",
    3: "on_ground",
    4: "hitlag",
    5: "hitstun",
    6: "state_flags",
}


def _row_bytes(row: np.ndarray) -> np.ndarray:
    return np.ascontiguousarray(row).view(np.uint8).reshape(1, COMPARE_DTYPE.itemsize)


def _native_result(
    out: np.ndarray, ref: np.ndarray, *, players: tuple[int, ...], profile_name: str
) -> tuple[str | None, str | None]:
    code = int(
        msl_binding.standard_rollout_compare(
            _row_bytes(out),
            _row_bytes(ref),
            np.asarray(players, dtype=np.uint8),
            int(profile_name == "rl1_gameplay"),
        )
    )
    scored = _CODE_TO_FIELD[code & 0xFF]
    ignored = "state_flags[4]&0x80" if code & 0x100 else None
    return scored, ignored


def _python_result(
    out: np.ndarray, ref: np.ndarray, *, players: tuple[int, ...], profile_name: str
) -> tuple[str | None, str | None]:
    out_row = out[0]
    ref_row = ref[0]
    for field in ("action_id", "animation_index", "on_ground", "hitlag", "hitstun"):
        for p in players:
            if int(out_row[field][p]) != int(ref_row[field][p]):
                return field, None

    out_sf = out_row["state_flags"]
    ref_sf = ref_row["state_flags"]
    if profile_name == "rl1_gameplay":
        for p in players:
            for sub in range(4):
                if int(out_sf[p, sub]) != int(ref_sf[p, sub]):
                    return "state_flags", None
            if ((int(out_sf[p, 4]) ^ int(ref_sf[p, 4])) & 0x7F) != 0:
                return "state_flags", None
        for p in players:
            if ((int(out_sf[p, 4]) ^ int(ref_sf[p, 4])) & 0x80) != 0:
                return None, "state_flags[4]&0x80"
        return None, None

    for p in players:
        for sub in range(5):
            if int(out_sf[p, sub]) != int(ref_sf[p, sub]):
                return "state_flags", None
    return None, None


def _assert_native_matches_python(
    out: np.ndarray, ref: np.ndarray, *, players: tuple[int, ...], profile_name: str
) -> None:
    assert _native_result(out, ref, players=players, profile_name=profile_name) == _python_result(
        out, ref, players=players, profile_name=profile_name
    )


@pytest.mark.parametrize("profile_name", ["rl1_gameplay", "strict"])
@pytest.mark.parametrize(
    "field,value",
    [
        ("action_id", 11),
        ("animation_index", 22),
        ("on_ground", 1),
        ("hitlag", 33),
        ("hitstun", 44),
    ],
)
def test_standard_rollout_compare_matches_python_standard_fields(
    field: str, value: int, profile_name: str
) -> None:
    out = np.zeros(1, dtype=COMPARE_DTYPE)
    ref = np.zeros(1, dtype=COMPARE_DTYPE)
    ref[field][0, 1] = value

    _assert_native_matches_python(out, ref, players=(1, 0), profile_name=profile_name)


@pytest.mark.parametrize("profile_name", ["rl1_gameplay", "strict"])
@pytest.mark.parametrize("player", [0, 1])
def test_standard_rollout_compare_respects_player_order(player: int, profile_name: str) -> None:
    out = np.zeros(1, dtype=COMPARE_DTYPE)
    ref = np.zeros(1, dtype=COMPARE_DTYPE)
    ref["hitstun"][0, player] = 7

    _assert_native_matches_python(out, ref, players=(1, 0), profile_name=profile_name)


@pytest.mark.parametrize("profile_name", ["rl1_gameplay", "strict"])
@pytest.mark.parametrize("sub", [0, 1, 2, 3])
def test_standard_rollout_compare_matches_python_state_flags_low_bytes(
    sub: int, profile_name: str
) -> None:
    out = np.zeros(1, dtype=COMPARE_DTYPE)
    ref = np.zeros(1, dtype=COMPARE_DTYPE)
    ref["state_flags"][0, 1, sub] = 1

    _assert_native_matches_python(out, ref, players=(1, 0), profile_name=profile_name)


@pytest.mark.parametrize("profile_name", ["rl1_gameplay", "strict"])
def test_standard_rollout_compare_matches_python_state_flags_byte4_low_bits(
    profile_name: str,
) -> None:
    out = np.zeros(1, dtype=COMPARE_DTYPE)
    ref = np.zeros(1, dtype=COMPARE_DTYPE)
    ref["state_flags"][0, 1, 4] = 0x01

    _assert_native_matches_python(out, ref, players=(1, 0), profile_name=profile_name)


def test_standard_rollout_compare_matches_python_rl1_ignored_state_flags_high_bit() -> None:
    out = np.zeros(1, dtype=COMPARE_DTYPE)
    ref = np.zeros(1, dtype=COMPARE_DTYPE)
    ref["state_flags"][0, 1, 4] = 0x80

    _assert_native_matches_python(out, ref, players=(1, 0), profile_name="rl1_gameplay")


def test_standard_rollout_compare_matches_python_strict_state_flags_high_bit() -> None:
    out = np.zeros(1, dtype=COMPARE_DTYPE)
    ref = np.zeros(1, dtype=COMPARE_DTYPE)
    ref["state_flags"][0, 1, 4] = 0x80

    _assert_native_matches_python(out, ref, players=(1, 0), profile_name="strict")


def test_standard_rollout_compare_matches_python_no_mismatch() -> None:
    out = np.zeros(1, dtype=COMPARE_DTYPE)
    ref = np.zeros(1, dtype=COMPARE_DTYPE)

    _assert_native_matches_python(out, ref, players=(1, 0), profile_name="rl1_gameplay")
