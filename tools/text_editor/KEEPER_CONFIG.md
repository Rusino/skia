# Project KEEPER: Local Harness Configuration

Please fill in project-specific execution commands so AI subagents can build and test deterministically:

- **Build Command**: `ninja -C ../../out/Debug dm`
- **Unit Test Command**: `../../out/Debug/dm --match TextEditor`
- **Fuzz Command**: `python3 traps/fuzz_gate.py --cmd "../../out/Debug/dm --match TextEditor_Fuzz"`
- **Incremental Build Timeout**: `60s`
- **Fast Unit Test Timeout**: `10s`
- **Sanitizer Matrix**:
  - ASan+UBSan Command: `../../out/ASan/dm --match TextEditor`
  - TSan Command: `../../out/TSan/dm --match TextEditor`
  - MSan Command: `../../out/MSan/dm --match TextEditor`

See `docs/BUILD_ADAPTERS.md` in Dungeons for examples on wiring GN/Ninja, CMake, Cargo, and Bazel.
