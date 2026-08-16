# Implementation Plan: Quality Review and Remediation

## Overview

Incremental plan to transform quality findings from the ESP32-S3 firmware and host tests into reproducible, tested, and CI-gated fixes. Execution starts with the baseline and inventory, fixes host/documentation infrastructure, establishes gates, and then remediates RemoteID, queues, NRF24, SDR/USB/SPI, readiness/Spectrum, and placeholders. Each behavioral fix includes a red-green regression or equivalent proof; HIL is mandatory only when host doubles cannot demonstrate the physical or temporal contract.

## Tasks

- [x] 1. Establish reproducible baseline, policy, and initial inventory
  - [x] 1.1 Capture the initial baseline on a clean checkout
    - Create `.kiro/specs/code-quality-review/artifacts/baseline.yaml` with commit, OS, architecture, ESP-IDF v5.3, CMake, compilers, and versions of Unity/Theft/cJSON dependencies.
    - Record separate canonical commands for host configure/build/test and firmware set-target/build, always in fresh `build/host` and `build/firmware` directories.
    - Preserve exit code, duration, classification, and log reference for each execution, including current failures, without depending on unversioned local files.
    - Validate command repetition in two clean directories and document the expected result after remediation.
    - _Requirements: 1.1, 1.2, 1.3, 1.4, 1.6, 11.6, 12.10_

  - [x] 1.2 Create the canonical findings inventory
    - Create `.kiro/specs/code-quality-review/artifacts/findings.yaml` with IDs `CQR-<AREA>-NNN`, justified severity, open status, owner per area, original commit/evidence `file:line`, reproduction, initial cause, and closure criteria.
    - Include at minimum host paths, README/configuration, CI/analysis gaps, RemoteID, queues, NRF24, SDR/USB, SPI, readiness/rollback, Spectrum Analyzer, and placeholders.
    - Keep baseline evidence immutable and a separate field for current evidence; record skips, suppressions, and intentional debt as linked findings.
    - Do not allow `CLOSED` without remediation, regression/proof, and green gates, nor `ACCEPTED_RISK` without owner, justification, and condition/date for review.
    - _Requirements: 2.1, 2.2, 2.3, 2.4, 2.5, 2.6, 10.5, 12.2_

  - [x] 1.3 Version policy and review artifact validators
    - Create `.kiro/specs/code-quality-review/artifacts/policy.yaml` and validators in `tools/code_quality/` for schema, ID uniqueness, transitions, severity, skips, suppressions, and fail-closed acceptance decision.
    - Add positive and negative fixtures in `test/host/code_quality/` covering incomplete finding, invalid accepted risk, open Critical/High, red gate, and duplicate/missing matrix.
    - Implement verification of Property 2 and Property 22; regression must prove that incomplete metadata cannot close findings nor accept the review.
    - _Requirements: 2.1, 2.2, 2.4, 2.6, 10.5, 12.2, 12.8, 12.9_

- [x] 2. Fix host build, documentation, and canonical configuration
  - [x] 2.1 Fix the HAL component path in the host build with regression
    - In `test/host/CMakeLists.txt`, replace the nonexistent reference `components/hal` with `components/hw_hal` using capitalization identical to the repository.
    - Add a test/fixture in `test/host/code_quality/` that configures the project in a new directory, confirms resolution of `components/hw_hal`, and fails if `components/hal` reappears.
    - First record the failure reproduction, then validate host configure and build with exit code zero.
    - _Requirements: 3.1, 3.5, 12.1, 12.4_

  - [x] 2.2 Align README to actual commands and artifacts with documentation smoke test
    - Update `README.md` to call the same entry points as the baseline; remove `run_tests` and any nonexistent script/target, or create the versioned entry point before documenting it.
    - Create a validator in `tools/code_quality/check_doc_references.*` and fixtures in `test/host/code_quality/` for commands, targets, scripts, paths, and capitalization.
    - Cover negative regression for missing `run_tests` and incorrect case reference; execute commands extracted from README on a clean checkout.
    - _Requirements: 3.2, 3.5, 3.6, 12.4, 12.5_

  - [x] 2.3 Explicitly resolve `sdkconfig.defaults` and `partitions.csv`
    - Inspect `CMakeLists.txt`, `sdkconfig*`, scripts, and workflow; for each file, choose a single policy: version and validate in the build, or remove references and document effective defaults/partitions.
    - Add fixtures that fail for orphan reference, missing required file, and ambiguous coexistence of both policies.
    - Execute a clean firmware build and record in the baseline which configuration entries and partition scheme were actually used.
    - _Requirements: 3.3, 3.4, 3.5, 12.4, 12.5_

  - [x] 2.4 Update host build/docs evidence without prematurely closing findings
    - Re-execute host configure/build/CTest and documentation smoke tests in new directories; attach results to `CQR-BUILD-*` and `CQR-DOCS-*` manifests.
    - Update `current_evidence` to current lines, preserving original commit and reproduction; keep status `VERIFYING` until CI and firmware also pass.
    - _Requirements: 1.3, 1.4, 1.5, 2.3, 2.6, 12.1, 12.2_

- [x] 3. Checkpoint — Foundation and canonical commands
  - Execute artifact validators, host configure/build/test, README smoke test, and firmware build on a clean checkout; halt promotion if any command depends on local state or a nonexistent reference.
  - Confirm that baseline and inventory preserve the original failure and the remediated results.

- [x] 4. Establish host tests, static analysis, and CI gates
  - [x] 4.1 Organize CTest discovery and doubles isolation
    - Adjust `test/host/CMakeLists.txt`, `test/mocks/`, and `test/host/generators/` to automatically register Unity/Theft in CTest, with `PBT_MIN_TRIALS=100` and reproducible seed.
    - Ensure separate targets for doubles/fakes and production; generate JUnit with identifiable passed, failed, and skipped tests.
    - Add an infrastructure test that fails if a new regression executable is not discovered by CTest or if a double is linked to a production target.
    - _Requirements: 4.4, 4.5, 4.6, 10.7, 12.3_

  - [x] 4.2 Add host jobs with sanitizers and deterministic stress
    - Create canonical presets/targets for ASan+UBSan and, separately, TSan on host-compilable parsing, queue, ownership, and lifecycle units.
    - Enable leak detection in saturation, cancellation, disconnection, and rollback scenarios; preserve seed and reduced counterexample from PBTs.
    - Validate with fixtures that synthetic over-read, use-after-free, double free, and data race cause the corresponding gate to fail.
    - _Requirements: 5.8, 6.4, 6.6, 7.8, 11.3, 11.7, 12.6_

  - [x] 4.3 Version static analysis policy and diagnostics
    - Implement in `tools/code_quality/` commands over `compile_commands.json`, host/ESP-IDF warnings, CMake, ShellCheck, and workflow; pin versions and normalize fingerprints without absolute paths/timestamps.
    - Make Critical/High, bounds, lifetime, uninitialized, relevant overflow, data race, and new warning in changed code block; require `CQR-*` in minimal/localized suppression.
    - Add fixtures for new diagnostic, preexisting backlog, and invalid suppression; validate that altered parsing, concurrency, ownership, and lifecycle appear in the report.
    - _Requirements: 11.1, 11.2, 11.3, 11.4, 11.5, 11.6, 11.7_

  - [x] 4.4 Expand `.github/workflows/build-and-release.yml` into identifiable gates
    - Create jobs `host-test`, `host-sanitizers`, `static-analysis`, and `firmware-build`, using exclusively the baseline commands and clean workspaces.
    - Publish JUnit, analysis reports, logs, and firmware map; remove `continue-on-error` from mandatory gates and make skips visible with a temporary allowlist linked to a finding.
    - Ensure that failure in host configure/build/test, analysis, or firmware fails CI, without allowing `firmware-build` alone to represent acceptance.
    - _Requirements: 4.1, 4.2, 4.3, 4.5, 4.6, 4.7, 12.3, 12.9_

  - [x] 4.5 Test gate conjunction and local/CI parity
    - Create tests/fixtures for Property 4 covering each gate red in isolation, prohibited skip, newly added regression, and fully green execution.
    - Compare local entry points, README, baseline, and workflow; fail if there is an alternative command not exercised locally.
    - _Requirements: 3.6, 4.2, 4.3, 4.4, 4.6, 12.3_

- [x] 5. Remediate RemoteID safety and semantics
  - [x] 5.1 Implement safe bounds and parse-then-commit with regression
    - In `components/services/src/remoteid_decoder.c` and `components/services/include/remoteid_decoder.h`, validate pointers, minimum, declared size, offsets, and sums before reading/copying; decode into initialized staging and publish only after complete validation.
    - In `test/host/`, add cases for empty, truncation at each field, boundaries, declared smaller/larger, and oversized, plus the Property 5 PBT over buffers/offsets/sizes.
    - Demonstrate red on the reproduced defect and green under ASan+UBSan; rejected inputs must return an observable reason and preserve previous output/record.
    - _Requirements: 5.1, 5.2, 5.7, 5.8, 5.9, 12.1, 12.6_

  - [x] 5.2 Compare the received RemoteID CRC over the correct region
    - Define per WiFi/BLE variant the protected region and CRC position; compare calculated and received values before any commit.
    - Add known valid/invalid CRC vectors and the Property 6 PBT that alters protected bits; verify `RID_REJECT_CRC`, `integrity_errors +1`, and byte-for-byte unchanged state.
    - _Requirements: 5.3, 5.4, 5.7, 5.9, 12.1, 12.4, 12.6_

  - [x] 5.3 Fix joint latitude/longitude validity
    - Validate scale, sentinels, and intervals for latitude and longitude individually and replace any logical OR with AND for aircraft and operator position.
    - Add a boundary table and four validity combinations, plus the Property 7 PBT; a single invalid member must prevent `has_position` and partial publication.
    - _Requirements: 5.5, 5.6, 5.7, 12.1, 12.4_

  - [x] 5.4 Consolidate reasons/metrics and state preservation regression
    - Expose snapshots of accepted, rejected by reason, and integrity errors without reading bytes beyond the frame; update callers in `telemetry_decoder`/registry to commit only on success.
    - Implement Property 8 and a deterministic malformed input campaign, verifying absence of crash/uninitialized data and that only allowed reason/metrics change.
    - Execute CTest, PBT, ASan+UBSan, and static analysis on RemoteID units; attach results to the finding before promoting it.
    - _Requirements: 5.4, 5.8, 5.9, 11.7, 12.1, 12.6_

- [~] 6. Checkpoint — Gates and RemoteID
  - Execute `host-test`, sanitizers, static analysis, and firmware build with all RemoteID vectors/PBTs automatically discovered.
  - Keep any RemoteID finding open if the defective condition lacks red-green proof or if the previous state could be altered by a rejected input.

- [ ] 7. Make queues observable and ownership deterministic
  - [~] 7.1 Inventory queues, producers, consumers, and execution context
    - Map `components/services/src/detection_service.c`, `main/data_pipeline.*`, HAL callbacks, and other `xQueueSend*` calls, recording queue/source IDs, policy, copied vs. heap type, and task/ISR usage in the inventory.
    - Define in common headers the contract where `ACCEPTED` transfers declared copy/ownership and `REJECTED` keeps ownership with the producer; adopt non-blocking `DROP_NEW` where no finding justifies another policy.
    - Create a fixture that detects an enqueue call whose return continues to be ignored.
    - _Requirements: 6.1, 6.5, 6.8, 12.7_

  - [~] 7.2 Implement submit and segmented metrics with regression
    - Add task/ISR wrappers, reasons `FULL`, `UNAVAILABLE`, `CLOSED`, `INVALID`, and `INTENTIONAL_SAMPLE`, and `received/enqueued/processed/queued/dropped` snapshots per queue and source.
    - Migrate inventoried producers/consumers to check return, free exactly once according to ownership, and count exactly one reason per rejection.
    - Add regressions for full/unavailable/closed, copy and heap, proving absence of silent drop, leak, double free, and double counting.
    - _Requirements: 6.1, 6.2, 6.3, 6.6, 6.8, 12.1, 12.6_

  - [~] 7.3 Verify saturation, recovery, and accepted FIFO
    - Create programmable FreeRTOS doubles to saturate, drain, and reopen flow; test consumer continuity and new item acceptance without reinitialization.
    - Implement Properties 10 and 11 with submit/process/drain sequences, verifying FIFO for accepted items and exactly-once release of objects.
    - _Requirements: 6.6, 6.7, 12.1, 12.6_

  - [~] 7.4 Make snapshots and ISR paths safe under concurrency
    - Use supported atomics or short critical section and consistent snapshot; in the ISR wrapper, use only `FromISR` APIs, without blocking or unsafe allocation.
    - Implement Property 9 and host/TSan stress for invariants `received = enqueued + Σdropped` and `enqueued = processed + queued`, without torn or regressive counters.
    - Mark HIL mandatory for producers actually triggered in ISR when the host double does not demonstrate temporal/contextual compatibility.
    - _Requirements: 6.3, 6.4, 6.5, 11.7, 12.6_

- [ ] 8. Fix NRF24 payload and RX recovery
  - [~] 8.1 Respect dynamic/static width with SPI double and regression
    - In `components/hw_hal/src/hal_nrf24.c` and header, query `R_RX_PL_WID` in dynamic mode and use `static_width[pipe]` in static mode, reading exactly the validated width.
    - Extend the SPI double to record commands and pipes; test payloads of 1, 31, and 32 bytes and static width per pipe, failing if the driver assumes 32.
    - _Requirements: 8.1, 8.2, 8.4, 8.6, 12.1, 12.4_

  - [~] 8.2 Reject invalid widths and recover RX once
    - Handle width 0, greater than 32, or incompatible without publishing output; execute flush/flag reconciliation per device contract and increment `invalid_width` exactly once.
    - Add regressions for 0, 33, SPI failure, and subsequent recovery, verifying previous output intact and valid reception after error.
    - _Requirements: 8.3, 8.6, 12.1, 12.6_

  - [~] 8.3 Eliminate residual bytes and publish exact length
    - Read into zeroed staging, fill metadata, and copy to output only after success; zero `[length, 32)` and require consumers to respect `length`.
    - Implement Property 15 with larger-to-smaller sequences and arbitrary 1–32 byte payloads under ASan+UBSan; no byte exclusive to the previous reception may become observable.
    - _Requirements: 8.4, 8.5, 8.6, 8.7, 11.7, 12.1, 12.4_

  - [~] 8.4 Validate NRF24 in directed HIL
    - Execute on real hardware payloads of 1/31/32 and invalid widths, confirming recovery command, RX continuity, and metrics; record module, firmware, script, timestamps, and logs.
    - Keep the finding `VERIFYING` if physical recovery after invalid width cannot be executed; doubles do not substitute this HIL criterion.
    - _Requirements: 8.1, 8.3, 8.6, 12.1, 12.6_

- [~] 9. Checkpoint — Queues and NRF24
  - Execute CTest/PBT, ASan+UBSan, TSan, static analysis, and firmware build; verify metrics conservation, ownership, post-saturation recovery, and absence of NRF24 residue.
  - Require HIL report for real ISR and RX recovery when applicable before closing the respective findings.

- [ ] 10. Remediate async SDR/USB lifecycle
  - [~] 10.1 Create USB/event/clock doubles and transfer state machine regressions
    - In `test/mocks/` and `test/host/`, model enumerated address/descriptors, immediate/late callback, error, timeout, cancel race, `DEV_GONE`, and reconnection; virtual clock must not convert sleep into completion.
    - Implement sequence test/Property 12 that rejects premature release, second free, and callback on dead context, covering exactly one reconciled terminal state.
    - Record red for the fixed delay and address `1` observed in `components/hw_hal/src/hal_sdr.c` before correction.
    - _Requirements: 7.1, 7.4, 7.5, 7.6, 7.7, 7.8, 12.1, 12.4_

  - [~] 10.2 Use current enumeration and USB generation
    - Modify `hal_sdr.c`/header to store the address and descriptors from the current enumeration by generation, without assuming address `1`; block submits for a disconnected generation.
    - Implement Property 13 with reconnection at a different address and late callback from previous generation, proving isolation of handles/context and exclusive use of the current generation.
    - _Requirements: 7.3, 7.6, 7.7, 7.8, 12.1, 12.4_

  - [~] 10.3 Replace delays with explicit completion, cancellation, and cleanup
    - Keep transfer, buffer, and `user_context` valid until terminal callback/event; on timeout, request cancellation and reconcile the race before releasing.
    - On disconnect/deinit, prevent new submits, complete/cancel pending, and release transfers, device, and client in reverse order exactly once; allow new initialization.
    - Execute regressions for immediate/late success, timeout, error, cancellation, callback after cancel, and disconnect/reconnect under leak detection and static analysis.
    - _Requirements: 7.1, 7.2, 7.4, 7.5, 7.6, 7.7, 7.8, 11.7, 12.1, 12.6_

  - [~] 10.4 Validate SDR enumeration and callbacks in HIL
    - Execute SDR enumerated at address other than `1`, unplug during transfer, real timeout/cancellation, and reconnect at new address, preserving stack logs and terminal states.
    - Do not close findings whose contract depends on the USB stack without green HIL; absence of hardware keeps `VERIFYING` or accepted risk only when severity/policy permit.
    - _Requirements: 7.3, 7.4, 7.6, 7.7, 12.1, 12.6_

- [ ] 11. Serialize SPI bus ownership and lifecycle
  - [~] 11.1 Introduce single owner, generation token, and host regressions
    - In `components/hw_hal/src/hw_manager.c`, headers, and LoRa/NRF24 boundaries, serialize `quiesce → release → reconfigure → acquire` and invalidate token on module deactivation.
    - Extend the SPI double to detect concurrent I/O and stale token; implement Property 14 with interleavings of swap, partial failure, and post-deactivation operation.
    - Verify that rollback releases partial resources in safe order, allows retry, and leaves exactly one coherent owner, without SPI access after deactivation.
    - _Requirements: 7.1, 7.2, 7.9, 11.7, 12.1, 12.6_

  - [~] 11.2 Integrate NRF24/LoRa into the `hw_manager` contract
    - Remove parallel bus acquisition/release in modules and propagate `ESP_ERR_INVALID_STATE` before I/O with invalid generation.
    - Add regressions for hot-swap under traffic, acquire failure, and rollback, counting acquire/release exactly once.
    - _Requirements: 7.1, 7.2, 7.9, 12.1, 12.6_

  - [~] 11.3 Validate SPI arbitration and ISR path in HIL
    - Execute LoRa↔NRF24 swap under traffic and, if applicable, ISR events during quiesce; record absence of simultaneous access, continuity, and post-rollback state.
    - Keep finding open when physical mutual exclusion or ISR compatibility cannot be demonstrated on host.
    - _Requirements: 6.5, 7.2, 7.9, 12.1, 12.6_

- [~] 12. Checkpoint — Hardware lifecycle
  - Execute host sequence suites, sanitizers, static analysis, and firmware build; validate resource count and generation/token across all injectable failures.
  - Consolidate NRF24, SDR/USB, and SPI HIL reports; no pending physical dependency can be treated as green by inference.

- [ ] 13. Fix readiness, init rollback, and Spectrum Analyzer
  - [~] 13.1 Centralize initialization results and readiness regression
    - In `main/main.c`, `main/task_manager.*`, and service/HAL interfaces, classify subsystems as required/optional and record state, cause, capability mask, and generation.
    - Make `Ready` derive from the snapshot: all required `OPERATIONAL`; safe optional failure generates `DEGRADED`; required failure generates `FAILED` and blocks dependents.
    - Implement Property 16 and tests for all operational, each required failed, and each optional failed; first reproduce the observed unconditional announcement.
    - _Requirements: 9.1, 9.2, 9.3, 9.4, 12.1, 12.4_

  - [~] 13.2 Implement rollback by generation and clean retry
    - Orchestrate acquisitions in a stack and release the prefix in reverse order exactly once on failure; invalidate handles/task tokens from the generation and start retry on a new generation.
    - Inject failure after each step and implement Property 17, verifying absence of invalid handles, duplicate tasks, and resources active from the previous attempt.
    - Cover partially acquired SPI/USB resources and maintain observable error/unavailable capability in all outputs.
    - _Requirements: 7.2, 9.5, 9.8, 9.9, 12.1, 12.6_

  - [~] 13.3 Restrict Spectrum Analyzer to operational allowlist
    - In `components/services/src/spectrum_analyzer.c`, accept SDR only in `OPERATIONAL`; refuse `UNINITIALIZED`, `INITIALIZING`, non-permitted `DEGRADED`, `STOPPING`, `DISCONNECTED`, and `ERROR` without altering consistent state.
    - Make SDR initialization owned by a single owner; Spectrum consumes capability and does not reinitialize the driver.
    - Implement Property 18 with the complete state table and regression for the old condition "any state other than INACTIVE".
    - _Requirements: 9.6, 9.7, 9.8, 12.1, 12.4, 12.6_

  - [~] 13.4 Propagate readiness/degradation to operations and UI
    - Make dependent services consult capability/state, and align display/log/public state to the same snapshot instead of independent strings.
    - Add host integration tests to block dependent operations after required failure, list unavailable optional capability, and transition `DEGRADED↔READY` only after validated recovery.
    - Execute HIL when readiness depends on real USB/SPI connection/disconnection; record events and prevent closure without that result.
    - _Requirements: 9.2, 9.3, 9.4, 9.8, 9.9, 12.1, 12.5, 12.6_

- [ ] 14. Remove placeholders from the product and make remaining debt explicit
  - [~] 14.1 Create scanner and production artifact regressions
    - Implement a scanner in `tools/code_quality/` for CMake lists, symbols/map, prohibited messages, TODO/FIXME that substitute behavior, and mock/fake/stub objects.
    - Add positive/negative fixtures and Property 19; the gate must fail for `domain_placeholder.c`, `hal_placeholder.c`, `services_placeholder.c`, or a double linked to firmware.
    - _Requirements: 10.1, 10.4, 10.6, 10.7, 12.1, 12.4_

  - [~] 14.2 Exclude placeholders from production lists without simulating success
    - Update `components/domain/CMakeLists.txt`, `components/hw_hal/CMakeLists.txt`, `components/services/CMakeLists.txt`, and other lists to link only approved implementations.
    - For incomplete functionality, exclude from target or return unavailability/explicit missing capability; add regression that fails for constant success/reachable empty stub.
    - Validate firmware, map, and component list; keep doubles only in explicitly non-production host targets.
    - _Requirements: 10.1, 10.2, 10.3, 10.6, 10.7, 12.1, 12.4_

  - [~] 14.3 Record and govern remaining technical debt
    - Update `findings.yaml` for each exclusion/replacement with component, decision, owner, impact, resolution criteria, and reevaluation; do not convert debt to accepted risk without valid policy.
    - Add an inventory test that fails for TODO/suppression/skip allowed by policy without a current `CQR-ID`.
    - _Requirements: 2.6, 10.3, 10.5, 11.5, 12.2_

- [~] 15. Checkpoint — Readiness, Spectrum, and production artifact
  - Execute fault injection per step, Spectrum state table, placeholder scanner, firmware build/map, and conditional HIL for capability loss/recovery.
  - Confirm that `Ready` never appears with a non-operational required subsystem and that no placeholder/double is linked to firmware.

- [ ] 16. Execute final validation and produce traceability matrix
  - [~] 16.1 Execute all gates on a clean checkout
    - Recreate host/firmware directories, execute validators, CTest/PBT, ASan+UBSan, TSan, static analysis, firmware build, map inspection, and mandatory HIL.
    - Preserve exit codes, durations, JUnit, reports, seeds, logs, map, and manifests with hashes; compare results to the initial baseline.
    - Fail acceptance for any red mandatory gate, unauthorized skip, or open Critical/High.
    - _Requirements: 1.5, 4.1, 4.2, 4.5, 11.2, 12.3, 12.9, 12.10_

  - [~] 16.2 Update findings and generate the final matrix
    - Generate `.kiro/specs/code-quality-review/artifacts/traceability.md` with one row per finding: ID, severity, baseline/current evidence, cause, remediation, test/proof, requirement/criterion, gates, result, and status.
    - Validate exact coverage of all findings and criteria, current line references, red-green evidence, and `CLOSED`/`ACCEPTED_RISK` rules.
    - Keep as `OPEN`/`VERIFYING` any item without regression, CI, or indispensable HIL, without masking the global decision.
    - _Requirements: 2.2, 2.3, 2.6, 12.1, 12.2, 12.7, 12.8, 12.9_

  - [~] 16.3 Reproduce acceptance by a third party and record fail-closed decision
    - Execute README/baseline commands without additional instructions and confirm firmware, host tests, regressions, and analysis with exit code zero.
    - Apply Property 22 to the final set: accept only with green gates, zero open Critical/High, and complete matrix; otherwise publish `NOT_ACCEPTED` with objective blockers.
    - _Requirements: 1.5, 3.5, 4.3, 12.3, 12.8, 12.9, 12.10_

## Notes

- No task starts marked as completed; checkpoints are gates, not success declarations.
- Every behavioral fix includes a regression test in the same slice and records the defective condition before the remediated result.
- PBTs use Theft, minimum 100 trials, reproducible seed/counterexample, and tags `Feature: code-quality-review, Property N`.
- Unity covers examples/boundaries; host doubles cover calls and ownership; ASan/UBSan/TSan and static analysis complement but do not substitute physical HIL.
- HIL is mandatory for real USB enumeration/callbacks, NRF24 RX recovery, SPI/ISR arbitration, and readiness based on real disconnection when the criterion is not demonstrable by doubles.
- The production language remains C with ESP-IDF v5.3; scripts/validators may use the language already adopted by the repository, with non-interactive commands and pinned versions.

## Task Dependency Graph

```json
{
  "waves": [
    { "id": 0, "tasks": ["1.1"] },
    { "id": 1, "tasks": ["1.2", "1.3"] },
    { "id": 2, "tasks": ["2.1", "2.2", "2.3"] },
    { "id": 3, "tasks": ["2.4"] },
    { "id": 4, "tasks": ["4.1", "4.2", "4.3"] },
    { "id": 5, "tasks": ["4.4", "4.5"] },
    { "id": 6, "tasks": ["5.1"] },
    { "id": 7, "tasks": ["5.2", "5.3"] },
    { "id": 8, "tasks": ["5.4"] },
    { "id": 9, "tasks": ["7.1"] },
    { "id": 10, "tasks": ["7.2"] },
    { "id": 11, "tasks": ["7.3", "7.4"] },
    { "id": 12, "tasks": ["8.1"] },
    { "id": 13, "tasks": ["8.2", "8.3"] },
    { "id": 14, "tasks": ["8.4"] },
    { "id": 15, "tasks": ["10.1"] },
    { "id": 16, "tasks": ["10.2", "10.3"] },
    { "id": 17, "tasks": ["10.4"] },
    { "id": 18, "tasks": ["11.1"] },
    { "id": 19, "tasks": ["11.2", "11.3"] },
    { "id": 20, "tasks": ["13.1"] },
    { "id": 21, "tasks": ["13.2", "13.3"] },
    { "id": 22, "tasks": ["13.4"] },
    { "id": 23, "tasks": ["14.1"] },
    { "id": 24, "tasks": ["14.2", "14.3"] },
    { "id": 25, "tasks": ["16.1"] },
    { "id": 26, "tasks": ["16.2"] },
    { "id": 27, "tasks": ["16.3"] }
  ]
}
```
