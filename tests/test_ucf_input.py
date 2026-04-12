from __future__ import annotations

import numpy as np

from tools.eval.dataset import INPUT_DTYPE


def _mk_input_bytes(batch: int, input_stride: int) -> np.ndarray:
    return np.zeros((batch, input_stride), dtype=np.uint8)


def test_ucf_cardinals_and_clamp_mapping() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    input_stride = int(sizes["input"])
    processed_stride = int(sizes["processed_input"])

    handle = msl_binding.init(
        batch_size=1,
        num_players=2,
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )

    prev_inp = _mk_input_bytes(1, input_stride)
    inp = _mk_input_bytes(1, input_stride)

    inp_view = inp.view(INPUT_DTYPE).reshape(-1)
    # P1 main stick: raw (80, 6) is within UCF cardinal snap window.
    inp_view["p"]["main_x"][0, 0] = np.int8(80)
    inp_view["p"]["main_y"][0, 0] = np.int8(6)

    # P1 c-stick: illegal diagonal should be legalized by clamp.
    inp_view["p"]["c_x"][0, 0] = np.int8(127)
    inp_view["p"]["c_y"][0, 0] = np.int8(127)

    msl_binding.step_input(handle, prev_inp, inp)

    out = np.empty((1, processed_stride), dtype=np.uint8)
    msl_binding.debug_write_processed_input(handle, out)
    out_view = out.view(INPUT_DTYPE).reshape(-1)

    # 1.0 cardinals: snap to exact (80, 0).
    assert int(out_view["p"]["main_x"][0, 0]) == 80
    assert int(out_view["p"]["main_y"][0, 0]) == 0

    # Clamp legalization: (127,127) -> (56,56) under HSD_PadClampCheck3(max=80) truncation.
    assert int(out_view["p"]["c_x"][0, 0]) == 56
    assert int(out_view["p"]["c_y"][0, 0]) == 56


def test_ucf_cardinals_disabled_does_not_snap() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    input_stride = int(sizes["input"])
    processed_stride = int(sizes["processed_input"])

    handle = msl_binding.init(
        batch_size=1,
        num_players=2,
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=0,
    )

    prev_inp = _mk_input_bytes(1, input_stride)
    inp = _mk_input_bytes(1, input_stride)

    inp_view = inp.view(INPUT_DTYPE).reshape(-1)
    inp_view["p"]["main_x"][0, 0] = np.int8(80)
    inp_view["p"]["main_y"][0, 0] = np.int8(6)

    msl_binding.step_input(handle, prev_inp, inp)

    out = np.empty((1, processed_stride), dtype=np.uint8)
    msl_binding.debug_write_processed_input(handle, out)
    out_view = out.view(INPUT_DTYPE).reshape(-1)

    # Without cardinals, this should be legalized by clamp (not snapped).
    assert int(out_view["p"]["main_x"][0, 0]) == 79
    assert int(out_view["p"]["main_y"][0, 0]) == 5


def test_ucf_clamp_legalizes_partial_diag_77_neg23_to_76_neg22() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    input_stride = int(sizes["input"])
    processed_stride = int(sizes["processed_input"])

    handle = msl_binding.init(
        batch_size=1,
        num_players=2,
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )

    prev_inp = _mk_input_bytes(1, input_stride)
    inp = _mk_input_bytes(1, input_stride)

    inp_view = inp.view(INPUT_DTYPE).reshape(-1)
    inp_view["p"]["main_x"][0, 0] = np.int8(77)
    inp_view["p"]["main_y"][0, 0] = np.int8(-23)

    msl_binding.step_input(handle, prev_inp, inp)

    out = np.empty((1, processed_stride), dtype=np.uint8)
    msl_binding.debug_write_processed_input(handle, out)
    out_view = out.view(INPUT_DTYPE).reshape(-1)

    assert int(out_view["p"]["main_x"][0, 0]) == 76
    assert int(out_view["p"]["main_y"][0, 0]) == -22
