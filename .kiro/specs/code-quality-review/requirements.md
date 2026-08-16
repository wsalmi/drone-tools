# Requirements Document

## Introduction

This spec defines the requirements to review and remediate the technical quality of the project's firmware and host tests. The goal is not merely to produce a report: each finding must be reproducible, traceable to code evidence, fixed or explicitly accepted, covered by automated verification, and prevented from regressing through continuous integration gates. The review covers build, documentation, configuration, memory safety, protocol parsing, concurrency, queues, hardware lifecycles, initialization, technical debt, and static analysis.

## Glossary

- **Review_System**: Process and set of artifacts used to identify, remediate, and verify quality issues in the project
- **Firmware**: Embedded software for ESP32-S3 produced by the project's main build
- **Host_Tests**: Tests compiled and executed in the development environment, without embedded hardware
- **Reproducible_Baseline**: Versioned record of environment, commands, results, and artifacts that allows repeating both the initial state and the remediated state
- **Finding**: Bug, technical risk, missing configuration, technical debt, or improvement opportunity identified in the review
- **Findings_Inventory**: Versioned artifact that maintains Findings and their traceability to evidence, reproduction, remediation, and verification
- **Evidence**: Precise reference in the format `file:line`, complemented by excerpt, log, or observable behavior
- **Severity**: Classification of Critical, High, Medium, or Low based on impact and probability
- **CI_Gate**: Mandatory automated verification whose failure prevents acceptance of the change
- **Regression_Test**: Automated test that fails in the presence of the original defect and passes after its remediation
- **RemoteID**: Remote identification protocol whose packets require integrity and semantic validation
- **CRC**: Integrity check value received in the packet and compared to the calculated value
- **RemoteID_Coordinate**: Latitude/longitude pair extracted from a RemoteID message
- **Event_Queue**: Concurrent structure used to transfer data between tasks, callbacks, or interrupts
- **Drop_Metric**: Observable counter of discarded items, segmented by source and reason
- **Hardware_Lifecycle**: Sequence of acquisition, use, completion, cancellation, disconnection, release, and eventual reinitialization of a resource
- **Async_Completion**: Explicit signal of operation termination, emitted by callback, event, or equivalent mechanism, without relying on fixed delay
- **Readiness**: State in which mandatory subsystems are operational and the system can announce it is ready
- **Degraded_Mode**: Limited operational state, with cause and unavailable capabilities explicitly reported
- **Init_Rollback**: Release, in safe order, of resources acquired by an initialization step that failed
- **Placeholder**: Temporary implementation, stub, or marker file that simulates success or is included in the production artifact without complete behavior
- **Static_Analysis**: Automated code verification without execution, including compiler diagnostics and specific tools

## Requirements

### Requirement 1: Reproducible Build and Test Baseline

**User Story:** As a project maintainer, I want to reproduce the firmware build and host tests in a clean environment, so that I can distinguish existing defects from regressions introduced by remediation.

#### Acceptance Criteria

1. WHEN the review is initiated on a clean checkout, THE Review_System SHALL record the commit, operating system, architecture, toolchain versions, ESP-IDF, CMake, and dependencies required for Firmware and Host_Tests.
2. THE Review_System SHALL define canonical, non-interactive, copy-pasteable commands to configure, compile, and test the Firmware and Host_Tests separately.
3. WHEN each canonical baseline command is executed, THE Review_System SHALL record exit code, duration, and sufficient log to reproduce every success or failure observed.
4. IF the initial baseline fails due to a known Finding, THEN THE Review_System SHALL preserve the reproducible failure as evidence and SHALL define the expected result after remediation.
5. WHEN the remediation is ready for acceptance, THE Firmware SHALL compile and the Host_Tests SHALL configure, compile, and execute with exit code zero from a clean checkout.
6. THE Reproducible_Baseline SHALL be executable without depending on build directories, generated files, or unversioned local state from a previous execution.

### Requirement 2: Traceable Findings Inventory

**User Story:** As the technical owner, I want a verifiable inventory of issues found, so that each risk has objective evidence, priority, remediation, and closure criteria.

#### Acceptance Criteria

1. WHEN a Finding is identified, THE Findings_Inventory SHALL assign a unique identifier and record category, Severity, status, Evidence in `file:line` format, technical impact, and reproduction steps.
2. THE Findings_Inventory SHALL record for each Finding the root cause, the proposed or applied remediation, the corresponding Regression_Test, and the verification result.
3. IF the Evidence line changes during remediation, THEN THE Findings_Inventory SHALL update the reference to the current line without losing the reference to the commit where the defect was reproduced.
4. WHEN Severity is assigned or changed, THE Findings_Inventory SHALL record justification based on impact and probability.
5. THE Findings_Inventory SHALL include, at minimum, all known issues enumerated in this spec, including build paths, missing documentation/configuration, CI coverage, RemoteID validation, SDR/USB, NRF24, queues, Spectrum Analyzer, readiness, and placeholders.
6. A Finding's status SHALL remain open until its remediation and its Regression_Test meet the acceptance criteria; an accepted risk SHALL record justification, owner, and condition or deadline for review.

### Requirement 3: Build Path, Documentation, and Configuration Correction

**User Story:** As a developer, I want paths, documentation, and configuration files to match the actual repository, so that documented builds work without implicit knowledge.

#### Acceptance Criteria

1. WHEN Host_Tests are configured, THE file `test/host/CMakeLists.txt` SHALL reference the existing component `components/hw_hal` and SHALL NOT depend on the nonexistent path `components/hal`.
2. WHEN a user follows the test instructions in `README.md` on a clean checkout, THE documented command or target SHALL exist and execute the Host_Tests; the documentation SHALL NOT cite `run_tests` if that script or target does not exist.
3. IF `sdkconfig.defaults` is required for the canonical build, THEN the file SHALL be versioned and validated by the build; ELSE every reference to it SHALL be removed and the effective defaults SHALL be documented.
4. IF `partitions.csv` is required for the canonical build, THEN the file SHALL be versioned and validated by the build; ELSE every reference to it SHALL be removed and the effective partition scheme SHALL be documented.
5. WHEN documented commands are executed in CI and on a clean checkout, THE paths and filenames SHALL resolve with the same capitalization used in the repository, including on case-sensitive filesystems.
6. THE CI SHALL execute an automated check that fails when canonical documentation references a nonexistent command, script, target, or required file.

### Requirement 4: Continuous Integration with Build and Tests

**User Story:** As a maintainer, I want CI to validate firmware, host tests, and static quality, so that defective changes are not accepted merely because the firmware compiles.

#### Acceptance Criteria

1. WHEN an eligible change is submitted to CI, THE CI_Gate SHALL execute in identifiable jobs the Firmware build, Host_Tests build, Host_Tests execution, and Static_Analysis.
2. IF the Firmware build, the Host_Tests configuration or build, any test, or mandatory Static_Analysis fails, THEN THE CI_Gate SHALL fail and SHALL expose the log of the responsible step.
3. THE CI_Gate SHALL use the same canonical commands documented in the Reproducible_Baseline, without maintaining an alternative flow not exercised locally.
4. WHEN a Regression_Test is added for a Finding, THE CI_Gate SHALL discover and execute it automatically.
5. THE CI_Gate SHALL publish or preserve machine-readable test results and SHALL identify tests executed, passed, failed, and skipped.
6. IF a test is disabled or skipped, THEN THE CI_Gate SHALL make the occurrence visible and the Findings_Inventory SHALL record justification and deadline or reactivation condition.
7. WHEN the remediation is evaluated, THE CI SHALL NOT be limited to the Firmware build and SHALL demonstrate successful execution of Host_Tests.

### Requirement 5: Memory Safety, Parsing, and Protocol Validation

**User Story:** As a firmware maintainer, I want to validate external data before accessing it, so that malformed packets do not cause memory corruption or incorrect telemetry data.

#### Acceptance Criteria

1. WHEN a parser receives external data, THE parser SHALL validate minimum size, declared size, field bounds, and byte availability before dereferencing, indexing, copying, or converting any field.
2. IF a size, offset, or arithmetic operation exceeds the received buffer or could overflow, THEN THE parser SHALL reject the input without reading or writing out of bounds and SHALL return an observable error.
3. WHEN a RemoteID packet contains a CRC, THE RemoteID parser SHALL calculate the CRC over the correct protocol region and SHALL compare the calculated value to the received CRC.
4. IF the calculated CRC differs from the received CRC, THEN THE RemoteID parser SHALL reject the packet, SHALL NOT update aircraft state, and SHALL increment an integrity error metric.
5. WHEN a RemoteID_Coordinate is accepted as a valid position, THE latitude SHALL be valid AND the longitude SHALL be valid individually, respecting protocol intervals, sentinel values, and defined scale; validity SHALL NOT be obtained by a logical OR condition between the two fields.
6. IF only one of latitude and longitude is valid, THEN THE RemoteID parser SHALL treat the pair as an invalid position and SHALL NOT publish a partial position as a valid coordinate.
7. WHEN parsers are tested, THE Host_Tests SHALL cover empty, truncated, and oversized buffers, boundary sizes, valid and invalid CRC, valid coordinates, each coordinate invalid in isolation, and both invalid.
8. THE Host_Tests SHALL include deterministic or generated malformed inputs that demonstrate the absence of crash, out-of-bounds access, use of uninitialized data, and silent acceptance of invalid packets.
9. IF a packet is rejected, THEN THE system SHALL preserve the previous valid state and SHALL record the rejection reason without exposing bytes beyond the received buffer.

### Requirement 6: Concurrency, Queues, and Drop Observability

**User Story:** As an operator and maintainer, I want losses and concurrent contention to be handled deterministically and measurably, so that overload does not produce silent failures.

#### Acceptance Criteria

1. WHEN a send operation to an Event_Queue returns success or failure, THE producer SHALL check the result and SHALL apply an explicit policy for the item and its associated memory.
2. IF an Event_Queue is full, unavailable, or closed, THEN THE system SHALL increment a Drop_Metric segmented by queue, source, and reason, without double counting.
3. THE system SHALL expose consistent snapshots of metrics for items received, enqueued, processed, and dropped, allowing verification of the relationship between these totals.
4. WHILE producers, consumers, callbacks, and interrupts operate concurrently, THE access to shared state and metrics SHALL be free of data races and SHALL use primitives compatible with the execution context.
5. IF an operation occurs in an interrupt context, THEN THE implementation SHALL NOT block, allocate memory unsafely, nor call APIs incompatible with interrupts.
6. WHEN an Event_Queue is saturated in test, THE Host_Tests SHALL verify the drop policy, per-reason counters, consumer continuity, and the absence of leak, double free, or use-after-free.
7. WHEN the system returns to normal flow after saturation, THE queues SHALL continue processing new items and SHALL preserve the defined order for accepted items.
8. THE system SHALL NOT silently discard telemetry, detection, or hardware events; every discard SHALL be accounted for or explicitly classified as intentional sampling.

### Requirement 7: SPI/USB Lifecycle and SDR Async Completion

**User Story:** As a hardware maintainer, I want explicit lifecycles for SPI, USB, and SDR, so that connection, failure, and disconnection do not leave invalid resources nor depend on arbitrary timing.

#### Acceptance Criteria

1. WHEN an SPI or USB resource is acquired, THE owning module SHALL register its state and SHALL release it exactly once on failure, disconnection, cancellation, or normal termination.
2. IF an initialization fails after partially acquiring SPI or USB resources, THEN THE module SHALL execute Init_Rollback in safe order and SHALL allow a retry without restarting the entire process.
3. WHEN an SDR USB device is enumerated, THE system SHALL use the address and descriptors provided by the current enumeration and SHALL NOT assume a fixed USB address equal to `1`.
4. WHEN an asynchronous SDR transfer is initiated, THE buffer, context, and associated handle SHALL remain valid until Async_Completion, confirmed cancellation, or handled timeout.
5. THE SDR flow SHALL determine success by callback, event, or explicit completion state and SHALL NOT use fixed delays as proof of transfer termination.
6. IF an SDR transfer exceeds the defined timeout, THEN THE system SHALL request cancellation, wait for or reconcile the completion according to the API contract, and SHALL prevent a late callback from accessing freed context.
7. WHEN a USB/SDR device is disconnected during operation, THE system SHALL stop new transfers, complete or cancel pending operations, release resources, and publish unavailable or degraded state.
8. THE Host_Tests or integration tests with doubles SHALL cover immediate completion, late completion, timeout, error, cancellation, callback after cancellation request, disconnection, and reconnection with a different USB address.
9. WHILE modules share an SPI bus, THE ownership transitions and configuration SHALL be serialized and SHALL prevent use of the bus by a module after its deactivation.

### Requirement 8: Correct NRF24 Payload Handling

**User Story:** As the NRF24 receiver maintainer, I want to process only the bytes actually received, so that variable payloads do not include stale bytes nor are truncated or accepted with invalid size.

#### Acceptance Criteria

1. WHEN the NRF24_Module receives a payload with dynamic width enabled, THE module SHALL query and validate the actually reported width before reading or publishing the payload.
2. WHEN the NRF24_Module operates with configured static width, THE module SHALL use the explicitly configured width for the corresponding pipe and SHALL NOT assume `32` bytes regardless of configuration.
3. IF the reported width is zero, greater than 32 bytes, or incompatible with the active configuration, THEN THE module SHALL reject the payload, recover the RX state according to the device contract, and increment an invalid payload metric.
4. WHEN a valid payload is published, THE associated length SHALL correspond to the number of valid bytes and consumers SHALL NOT read bytes past that length.
5. THE module SHALL initialize or overwrite every byte that could become observable and SHALL NOT reuse residual bytes from previous receptions.
6. THE Host_Tests SHALL cover valid payloads of 1, 31, and 32 bytes, consecutive payloads of different sizes, and invalid widths of 0 and greater than 32 bytes.
7. WHEN a smaller payload succeeds a larger one, THE Host_Tests SHALL verify that no byte exclusive to the previous payload is published as part of the current payload.

### Requirement 9: Readiness, Degradation, and Init Rollback

**User Story:** As an operator, I want the announced state to reflect the capabilities actually initialized, so that "Ready" does not hide failures and degraded modes are safe and understandable.

#### Acceptance Criteria

1. WHEN the application starts, THE system SHALL classify each subsystem as required or optional and SHALL record the result of its initialization.
2. THE application SHALL announce `Ready` only when all required subsystems are in operational state and the dependencies needed for the main flow are available.
3. IF an optional subsystem fails and safe operation exists without it, THEN THE application SHALL announce Degraded_Mode, identify the cause, and list the unavailable capability instead of announcing unrestricted readiness.
4. IF a required subsystem fails, THEN THE application SHALL NOT announce `Ready`, SHALL enter an observable failure state, and SHALL prevent the start of operations that depend on the subsystem.
5. WHEN the initialization of a step fails, THE system SHALL execute Init_Rollback for all resources acquired by that step before returning an error or retrying.
6. WHEN the Spectrum Analyzer is initialized or activated, THE spectrum subsystem SHALL proceed only if its dependencies are in an explicitly permitted operational state; any state other than `INACTIVE` SHALL NOT be treated generically as ready.
7. IF the Spectrum Analyzer's dependency is in `INITIALIZING`, `ERROR`, `STOPPING`, `DISCONNECTED`, or equivalent non-operational state, THEN THE Spectrum Analyzer SHALL refuse activation, preserve consistent state, and report the reason.
8. THE Host_Tests SHALL cover full initialization, failure of each required dependency, failure of an optional dependency, rollback after partial failure, retry, and all relevant Spectrum Analyzer transitions.
9. WHEN a retry of initialization occurs after rollback, THE system SHALL NOT reuse invalid handles, duplicate tasks, or leave resources from the previous attempt active.

### Requirement 10: Placeholder Removal and Technical Debt Management

**User Story:** As a maintainer, I want production artifacts to not include placeholders or simulated success, so that the build represents real functionality and remaining debt is explicit.

#### Acceptance Criteria

1. WHEN the production Firmware is linked, THE source list and artifact map SHALL NOT include `domain_placeholder.c`, `hal_placeholder.c`, or any other Placeholder without an approved production implementation.
2. IF a feature is not yet implemented, THEN THE production build SHALL exclude it or mark it explicitly as unavailable and SHALL NOT return fictitious success.
3. WHEN a Placeholder is removed or replaced, THE Review_System SHALL record the affected component, the definitive behavior or exclusion decision, and the corresponding verification.
4. THE CI_Gate SHALL fail when files, symbols, or messages classified by the project's policy as Placeholder are included in the production artifact.
5. IF technical debt remains intentionally, THEN THE Findings_Inventory SHALL record identifier, impact, owner, resolution criteria, and condition or deadline for reevaluation.
6. THE production code SHALL NOT contain reachable paths that represent success through empty stubs, unjustified constant values, or TODO/FIXME comments used as substitutes for required behavior.
7. WHEN the test build uses doubles, stubs, or fakes, THE configuration SHALL distinguish them from production components and SHALL prevent their accidental linking to the final Firmware.

### Requirement 11: Static Analysis and Diagnostics Policy

**User Story:** As a reviewer, I want reproducible static analysis with an explicit policy, so that automatically detectable defects are fixed before acceptance.

#### Acceptance Criteria

1. THE Review_System SHALL define and version the Static_Analysis set, rules, versions, and commands applicable to the C/C++ code, scripts, and build files that were changed.
2. WHEN Static_Analysis is executed locally or in CI, THE result SHALL be deterministic for the same commit and declared environment.
3. IF Static_Analysis identifies a Critical or High diagnostic, potential out-of-bounds access, use-after-free, double free, uninitialized variable, relevant overflow, or data race, THEN THE CI_Gate SHALL fail.
4. WHEN a compiler or analyzer emits a new warning in changed code, THE CI_Gate SHALL fail according to the versioned diagnostics policy.
5. IF a diagnostic is suppressed, THEN the suppression SHALL be minimal, localized, and linked to a justification and a Finding in the Findings_Inventory.
6. THE Reproducible_Baseline SHALL distinguish preexisting diagnostics from new diagnostics and SHALL define a verifiable trajectory to eliminate the accepted backlog.
7. WHEN the remediation changes parsing, concurrency, ownership, or lifecycle, THE Static_Analysis SHALL include the affected units and the result SHALL be attached to the acceptance evidence.

### Requirement 12: Acceptance, Closure, and Non-Regression Criteria

**User Story:** As the delivery owner, I want objective criteria to close each finding and accept the review, so that unverified fixes or regressions are not declared complete.

#### Acceptance Criteria

1. WHEN a fixable Finding is remediated, THE linked Regression_Test SHALL demonstrate failure against the reproduced defective condition and success with the remediation applied, or SHALL document an equivalent proof when execution against the defective version is not technically feasible.
2. WHEN a Finding is marked as closed, THE Findings_Inventory SHALL contain evidence of the fix, Regression_Test result, CI result, and reference to the requirement and acceptance criterion met.
3. THE review SHALL be accepted only when the clean Firmware build, the Host_Tests build and execution, all Regression_Tests, and mandatory Static_Analysis complete successfully.
4. THE regression suite SHALL explicitly cover: path `components/hw_hal`; actual README commands; handling of `sdkconfig.defaults` and `partitions.csv`; RemoteID CRC comparison; joint latitude and longitude validation; SDR completion without fixed delay; USB enumeration without fixed address; variable NRF24 payload; queue drops; Spectrum Analyzer states; readiness/degradation/rollback; and placeholder exclusion.
5. IF a remediation changes public behavior, configuration format, or development command, THEN THE documentation and corresponding tests SHALL be updated in the same change.
6. WHEN failure tests are executed, THE system SHALL preserve consistent state, release acquired resources, and produce an observable error or metric compatible with the scenario.
7. THE remediation SHALL NOT reduce coverage of existing build, parsing, hardware, or initialization requirements without an accepted risk recorded per Requirement 2.
8. WHEN all Findings are evaluated, THE Review_System SHALL produce a final matrix that relates Finding, Severity, Evidence, remediation, Regression_Test, requirement, result, and status.
9. IF any Critical or High Finding remains open or any mandatory CI_Gate fails, THEN THE review SHALL be considered not accepted.
10. WHEN the review is accepted, THE Reproducible_Baseline commands SHALL allow a third party to reproduce the final results from a clean checkout without additional undocumented instructions.
