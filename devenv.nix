{ pkgs, ... }:

{
  # Native/viewer development tools; see CONTRIBUTING.md for setup.
  packages = with pkgs; [
    gnumake
    gcc
    uv # Python environment and dependencies
    pkg-config
    libusb1 # GameCube adapter bridge
    emscripten # wasm module and the live viewer
    nodejs # viewer build, wasm smokes
    git-lfs # replay fixtures; git hooks fail without it
    gdb
  ];

  # Let release builds retain the Makefile's -march=native tuning.
  env.NIX_ENFORCE_NO_NATIVE = "0";
}
