# Sanitizer Probe Fixtures

These tests **intentionally** trigger memory errors and data races. They exist
to validate that the sanitizer CI gate is correctly configured: each probe MUST
fail under the corresponding sanitizer. If a probe passes, the gate is broken.

## Probes

| File | Sanitizer | Defect |
|------|-----------|--------|
| `probe_heap_overread.c` | ASan | heap-buffer-overflow (over-read) |
| `probe_use_after_free.c` | ASan | heap-use-after-free |
| `probe_double_free.c` | ASan | double-free / invalid-free |
| `probe_data_race.c` | TSan | unsynchronized shared variable access |

## Running

Probes are excluded from the normal CTest run. Use the `sanitizer_probe` label:

```bash
# ASan probes (all should report "Passed" because WILL_FAIL is set)
cmake --preset asan -S test/host
cmake --build --preset asan
ctest --preset asan-probes

# TSan probes
cmake --preset tsan -S test/host
cmake --build --preset tsan
ctest --preset tsan-probes
```

## CTest Behavior

Each probe is registered with `WILL_FAIL TRUE`. CTest inverts the exit code:
- If the sanitizer **aborts** the process (exit != 0) → CTest reports **Passed**
- If the probe runs to completion (exit == 0) → CTest reports **Failed**

This ensures the CI gate fails if sanitizers stop working.

## Adding New Probes

1. Create a minimal `.c` file that triggers exactly one defect class.
2. Register it in `test/host/CMakeLists.txt` under the appropriate sanitizer guard.
3. Set `WILL_FAIL TRUE` and the `sanitizer_probe` label.
4. Verify locally that the probe fails under the sanitizer.
