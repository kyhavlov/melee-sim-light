from __future__ import annotations

import subprocess
import textwrap
from pathlib import Path


def test_psvecnormalize_matches_gekko_instruction_bits(tmp_path: Path) -> None:
    # Lock the frsqrte estimate and the scalar-single Force25Bit rounding used by the SDK Newton
    # step without exposing a gameplay debug API solely for arithmetic tests.
    # refs/melee/build/GALE01/asm/dolphin/mtx/vec.s::PSVECNormalize
    # refs/Ishiiruka/Source/Core/Common/MathUtil.cpp::ApproximateReciprocalSquareRoot
    # refs/Ishiiruka/Source/Core/Core/PowerPC/Interpreter/Interpreter_FPUtils.h::Force25Bit
    root = Path(__file__).resolve().parents[1]
    source = tmp_path / "psvec_normalize_test.c"
    executable = tmp_path / "psvec_normalize_test"
    source.write_text(
        textwrap.dedent(
            """
            #include <assert.h>
            #include <stdint.h>

            #include "msl_math.h"

            static void assert_normal_bits(float x, float y, uint32_t want_x, uint32_t want_y) {
              float nx = 0.0f;
              float ny = 0.0f;
              assert(msl_psvec2_normalize(x, y, &nx, &ny));
              assert(msl_float_bits(nx) == want_x);
              assert(msl_float_bits(ny) == want_y);
            }

            int main(void) {
              assert(msl_double_bits(msl_ppc_frsqrte(1.0)) == UINT64_C(0x3FEFFE8000000000));
              assert(msl_double_bits(msl_ppc_frsqrte(2.0)) == UINT64_C(0x3FE69FA000000000));
              assert_normal_bits(1.0f, 0.0f, UINT32_C(0x3F7FFFFF), UINT32_C(0x00000000));
              assert_normal_bits(3.0f, 4.0f, UINT32_C(0x3F19999A), UINT32_C(0x3F4CCCCD));
              assert_normal_bits(1.0f, 192.0f, UINT32_C(0x3BAAAA12), UINT32_C(0x3F7FFF1B));
              return 0;
            }
            """
        )
    )
    subprocess.run(
        [
            "cc",
            "-std=c11",
            "-O3",
            "-ffp-contract=off",
            "-I",
            str(root / "src"),
            str(source),
            "-lm",
            "-o",
            str(executable),
        ],
        check=True,
        cwd=root,
    )
    subprocess.run([str(executable)], check=True, cwd=root)
