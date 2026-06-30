import time

import numpy as np

import msl_binding


def test_marth_thrown_fobj_eof_path_is_not_pathological() -> None:
    handle = msl_binding.init(batch_size=1, num_players=2)
    cases = (
        (18, 263, 14.0, 4),   # Marth ThrownB top-body chain
        (18, 265, 14.0, 4),   # Marth ThrownLw top-body chain
        (18, 265, 31.0, 12),  # Marth ThrownLw side-body chain
    )
    try:
        for args in cases:
            mat = msl_binding.anim_pose_collision_matrix_f32(*args)
            assert mat.shape == (12,)
            assert mat.dtype == np.float32
            assert np.isfinite(mat).all()

        start = time.perf_counter()
        for _ in range(64):
            for args in cases:
                msl_binding.anim_pose_collision_matrix_f32(*args)
        elapsed = time.perf_counter() - start
    finally:
        msl_binding.destroy(handle)

    assert elapsed < 0.25
