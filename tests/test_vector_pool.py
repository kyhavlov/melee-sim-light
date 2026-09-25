import hashlib
from pathlib import Path
import unittest

import numpy as np

import melee_sim as msl


def _copy_recorded_fields(destination, source):
    for name in destination.dtype.names:
        if destination[name].dtype.names:
            _copy_recorded_fields(destination[name], source[name])
        else:
            destination[name] = source[name]


class VectorPoolTest(unittest.TestCase):
    def test_four_yoshis_materialize_hurtbox_scales_and_restore(self):
        fixture = np.load(Path(__file__).with_name('fixtures') / 'yoshi_vector_pool.npz')
        prefix_frames = int(fixture['prefix_frames'])
        recorded_frame = np.zeros((), dtype=fixture['observation_schema'].dtype)
        digest = hashlib.sha256()
        with msl.EnvBatch(batch_size=2, length=128, num_players=4, action_format='raw') as env:
            env.match_config_view[:] = fixture['config']
            env.reset_all()
            for frame, action in enumerate(fixture['inputs']):
                env.current_action_frame[:] = action
                env.step_and_reset()
                if frame < prefix_frames:
                    _copy_recorded_fields(recorded_frame, env.current_frame[0])
                    digest.update(recorded_frame.tobytes())
            self.assertEqual(digest.hexdigest(), str(fixture['prefix_sha256']))
            saved = env.save(0)
            for _ in range(120):
                if env.t == env.length:
                    env.reset_cursor()
                env.current_action_frame[:] = np.zeros((), env.current_action_frame.dtype)
                env.step()
            expected = env.current_frame[0].tobytes()
            env.restore(1, saved)
            for _ in range(120):
                if env.t == env.length:
                    env.reset_cursor()
                env.current_action_frame[:] = np.zeros((), env.current_action_frame.dtype)
                env.step()
            self.assertEqual(env.current_frame[1].tobytes(), expected)


if __name__ == '__main__':
    unittest.main()
