#!/usr/bin/env python3
"""
Test suite for CI gate conjunction and local/CI parity.

Validates Property 4: The CI result is success if and only if ALL mandatory
gates pass, there is no prohibited skip, every regression is auto-discovered,
and CI uses the same canonical commands exercised locally.

Tests cover:
  1. Gate conjunction — each gate red in isolation causes rejection
  2. Prohibited skip — skip without CQR-ID causes gate failure
  3. Newly added regression auto-discovery via CTest/CMake add_test
  4. Local/CI parity — workflow, README, and baseline use same commands

Feature: code-quality-review
Property 4: CI gate is the conjunction of mandatory checks
Validates: Requirements 3.6, 4.2, 4.3, 4.4, 4.6, 12.3
"""

import sys
from pathlib import Path

import pytest
import yaml

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "tools"))

from code_quality.validate_ci_gates import (
    MANDATORY_CI_JOBS,
    CIGateValidator,
    GateViolation,
    LocalCIParityValidator,
    ParityViolation,
    SkipRecord,
    extract_baseline_canonical_commands,
    extract_readme_commands,
    extract_workflow_run_commands,
    normalize_command,
    validate_ctest_autodiscovery,
)

FIXTURES_DIR = Path(__file__).parent / "fixtures"
BASELINE_PATH = (
    REPO_ROOT / ".kiro" / "specs" / "code-quality-review" / "artifacts" / "baseline.yaml"
)
WORKFLOW_PATH = REPO_ROOT / ".github" / "workflows" / "build-and-release.yml"
README_PATH = REPO_ROOT / "README.md"
HOST_CMAKELISTS_PATH = REPO_ROOT / "test" / "host" / "CMakeLists.txt"


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def load_fixture(name: str) -> dict:
    """Load a YAML fixture file."""
    with open(FIXTURES_DIR / name) as f:
        return yaml.safe_load(f)


# ===========================================================================
# 1. Gate Conjunction — Property 4
# ===========================================================================

class TestGateConjunctionAllGreen:
    """All mandatory gates green → ACCEPTED."""

    def test_all_gates_green_is_accepted(self):
        """When every mandatory job is green, CI should be accepted."""
        gate_results = load_fixture("ci_gates_all_green.yaml")
        validator = CIGateValidator(gate_results)
        assert validator.is_accepted() is True

    def test_all_green_no_violations(self):
        """All green produces zero violations."""
        gate_results = load_fixture("ci_gates_all_green.yaml")
        validator = CIGateValidator(gate_results)
        violations = validator.validate()
        assert violations == []


class TestGateConjunctionEachRedInIsolation:
    """Each mandatory gate red in isolation causes rejection."""

    def test_host_test_red_is_rejected(self):
        """host-test red, others green → REJECTED."""
        gate_results = load_fixture("ci_gates_host_test_red.yaml")
        validator = CIGateValidator(gate_results)
        assert validator.is_accepted() is False
        violations = validator.validate()
        assert any(v.job == "host-test" for v in violations)

    def test_host_sanitizers_red_is_rejected(self):
        """host-sanitizers red, others green → REJECTED."""
        gate_results = load_fixture("ci_gates_sanitizers_red.yaml")
        validator = CIGateValidator(gate_results)
        assert validator.is_accepted() is False
        violations = validator.validate()
        assert any(v.job == "host-sanitizers" for v in violations)

    def test_static_analysis_red_is_rejected(self):
        """static-analysis red, others green → REJECTED."""
        gate_results = load_fixture("ci_gates_analysis_red.yaml")
        validator = CIGateValidator(gate_results)
        assert validator.is_accepted() is False
        violations = validator.validate()
        assert any(v.job == "static-analysis" for v in violations)

    def test_firmware_build_red_is_rejected(self):
        """firmware-build red, others green → REJECTED."""
        gate_results = load_fixture("ci_gates_firmware_red.yaml")
        validator = CIGateValidator(gate_results)
        assert validator.is_accepted() is False
        violations = validator.validate()
        assert any(v.job == "firmware-build" for v in violations)

    def test_missing_job_is_rejected(self):
        """A mandatory job with no result at all is rejected."""
        gate_results = {
            "host-test": "green",
            "host-sanitizers": "green",
            # static-analysis missing entirely
            "firmware-build": "green",
        }
        validator = CIGateValidator(gate_results)
        assert validator.is_accepted() is False
        violations = validator.validate()
        assert any(
            v.job == "static-analysis" and "not executed" in v.message
            for v in violations
        )

    def test_skipped_job_is_rejected(self):
        """A mandatory job with status 'skipped' is rejected."""
        gate_results = {
            "host-test": "green",
            "host-sanitizers": "skipped",
            "static-analysis": "green",
            "firmware-build": "green",
        }
        validator = CIGateValidator(gate_results)
        assert validator.is_accepted() is False


class TestGateConjunctionMultipleRed:
    """Multiple gates red still produces correct violations for each."""

    def test_two_gates_red_produces_two_violations(self):
        """Two red gates produce at least two violations."""
        gate_results = {
            "host-test": "red",
            "host-sanitizers": "green",
            "static-analysis": "red",
            "firmware-build": "green",
        }
        validator = CIGateValidator(gate_results)
        violations = validator.validate()
        gate_violations = [v for v in violations if v.category == "gate_red"]
        assert len(gate_violations) == 2


# ===========================================================================
# 2. Prohibited Skip — Requirement 4.6
# ===========================================================================

class TestProhibitedSkip:
    """A test skip without CQR-ID and justification causes gate failure."""

    def test_skip_without_cqr_id_is_violation(self):
        """Skip missing CQR-ID causes prohibited_skip violation."""
        gate_results = {job: "green" for job in MANDATORY_CI_JOBS}
        skips = [
            SkipRecord(
                test_name="test_something_broken",
                cqr_id=None,
                justification="just because",
            ),
        ]
        validator = CIGateValidator(gate_results, skip_records=skips)
        assert validator.is_accepted() is False
        violations = validator.validate()
        skip_violations = [v for v in violations if v.category == "prohibited_skip"]
        assert len(skip_violations) == 1
        assert "test_something_broken" in skip_violations[0].message

    def test_skip_without_justification_is_violation(self):
        """Skip missing justification causes prohibited_skip violation."""
        gate_results = {job: "green" for job in MANDATORY_CI_JOBS}
        skips = [
            SkipRecord(
                test_name="test_incomplete_feature",
                cqr_id="CQR-DEBT-001",
                justification=None,
            ),
        ]
        validator = CIGateValidator(gate_results, skip_records=skips)
        assert validator.is_accepted() is False

    def test_skip_without_both_is_violation(self):
        """Skip missing both CQR-ID and justification is a violation."""
        gate_results = {job: "green" for job in MANDATORY_CI_JOBS}
        skips = [
            SkipRecord(
                test_name="test_lazy_skip",
                cqr_id=None,
                justification=None,
            ),
        ]
        validator = CIGateValidator(gate_results, skip_records=skips)
        assert validator.is_accepted() is False

    def test_skip_with_cqr_id_and_justification_is_allowed(self):
        """Skip with both CQR-ID and justification is permitted."""
        gate_results = {job: "green" for job in MANDATORY_CI_JOBS}
        skips = [
            SkipRecord(
                test_name="test_hil_only",
                cqr_id="CQR-NRF24-001",
                justification="Requires physical NRF24 module; host double validated",
            ),
        ]
        validator = CIGateValidator(gate_results, skip_records=skips)
        assert validator.is_accepted() is True

    def test_multiple_skips_one_invalid(self):
        """Multiple skips where one is invalid causes failure."""
        gate_results = {job: "green" for job in MANDATORY_CI_JOBS}
        skips = [
            SkipRecord(
                test_name="test_valid_skip",
                cqr_id="CQR-USB-001",
                justification="Needs USB hardware",
            ),
            SkipRecord(
                test_name="test_invalid_skip",
                cqr_id=None,
                justification=None,
            ),
        ]
        validator = CIGateValidator(gate_results, skip_records=skips)
        assert validator.is_accepted() is False
        violations = validator.validate()
        skip_violations = [v for v in violations if v.category == "prohibited_skip"]
        assert len(skip_violations) == 1
        assert "test_invalid_skip" in skip_violations[0].message


# ===========================================================================
# 3. Newly Added Regression Auto-Discovery — Requirement 4.4, 12.3
# ===========================================================================

class TestCTestAutoDiscovery:
    """CTest discovers new test executables without manual registration."""

    def test_cmakelists_has_enable_testing(self):
        """
        test/host/CMakeLists.txt calls enable_testing() to enable CTest.
        This ensures new add_test() calls are automatically discovered.
        """
        content = HOST_CMAKELISTS_PATH.read_text()
        issues = validate_ctest_autodiscovery(content)
        enable_issues = [i for i in issues if "enable_testing" in i]
        assert enable_issues == [], f"CTest not enabled: {enable_issues}"

    def test_cmakelists_has_add_test(self):
        """
        test/host/CMakeLists.txt uses add_test() to register tests.
        New executables with add_test() are discovered by CTest automatically.
        """
        content = HOST_CMAKELISTS_PATH.read_text()
        issues = validate_ctest_autodiscovery(content)
        add_test_issues = [i for i in issues if "add_test" in i]
        assert add_test_issues == [], f"No add_test pattern: {add_test_issues}"

    def test_no_parallel_manual_list_required(self):
        """
        Verify that CTest discovery pattern means no manual list is needed.
        The pattern: add_test(NAME <name> COMMAND <target>) registers the test
        directly in CMake so CTest discovers it without a separate manifest.
        """
        content = HOST_CMAKELISTS_PATH.read_text()
        # Should have at least the same number of add_test calls as test targets
        import re
        add_test_calls = re.findall(r"add_test\s*\(", content)
        # We expect a reasonable number of test registrations
        assert len(add_test_calls) >= 5, (
            f"Expected at least 5 add_test() calls for auto-discovery, "
            f"found {len(add_test_calls)}"
        )

    def test_autodiscovery_rejects_missing_enable_testing(self):
        """A CMakeLists without enable_testing() is flagged."""
        content = """
cmake_minimum_required(VERSION 3.16)
project(my_tests C)
add_executable(test_foo test_foo.c)
add_test(NAME test_foo COMMAND test_foo)
"""
        issues = validate_ctest_autodiscovery(content)
        assert any("enable_testing" in i for i in issues)

    def test_autodiscovery_rejects_missing_add_test(self):
        """A CMakeLists without add_test() is flagged."""
        content = """
cmake_minimum_required(VERSION 3.16)
project(my_tests C)
enable_testing()
add_executable(test_foo test_foo.c)
# Forgot to add_test!
"""
        issues = validate_ctest_autodiscovery(content)
        assert any("add_test" in i for i in issues)


# ===========================================================================
# 4. Local/CI Parity — Requirements 3.6, 4.3
# ===========================================================================

class TestLocalCIParityPositive:
    """Workflow commands that match baseline pass parity check."""

    def test_parity_fixture_passes(self):
        """A workflow with only canonical commands passes parity."""
        workflow_content = (FIXTURES_DIR / "ci_workflow_with_parity.yaml").read_text()
        baseline_content = BASELINE_PATH.read_text()
        readme_content = README_PATH.read_text()

        workflow_cmds = extract_workflow_run_commands(workflow_content)
        baseline_cmds = extract_baseline_canonical_commands(baseline_content)
        readme_cmds = extract_readme_commands(readme_content)

        validator = LocalCIParityValidator(workflow_cmds, baseline_cmds, readme_cmds)
        violations = validator.validate()
        assert violations == [], f"Unexpected parity violations: {violations}"

    def test_baseline_commands_exist(self):
        """baseline.yaml contains canonical commands."""
        baseline_content = BASELINE_PATH.read_text()
        cmds = extract_baseline_canonical_commands(baseline_content)
        assert len(cmds) > 0, "No commands found in baseline.yaml"

    def test_readme_commands_exist(self):
        """README.md contains build/test commands in bash blocks."""
        readme_content = README_PATH.read_text()
        cmds = extract_readme_commands(readme_content)
        assert len(cmds) > 0, "No commands found in README.md bash blocks"

    def test_readme_cmake_commands_in_baseline(self):
        """README cmake/ctest commands should appear in baseline."""
        baseline_content = BASELINE_PATH.read_text()
        readme_content = README_PATH.read_text()

        baseline_cmds = extract_baseline_canonical_commands(baseline_content)
        readme_cmds = extract_readme_commands(readme_content)

        # Filter to cmake/ctest commands from README
        import re
        cmake_cmds = [c for c in readme_cmds if re.search(r"(cmake|ctest)", c)]
        baseline_normalized = {normalize_command(c) for c in baseline_cmds}

        for cmd in cmake_cmds:
            normalized = normalize_command(cmd)
            # Should find a baseline match
            found = any(
                normalized in bc or bc in normalized
                for bc in baseline_normalized
            )
            assert found, (
                f"README cmake command not in baseline: '{cmd}'"
            )


class TestLocalCIParityNegative:
    """Workflow commands not in baseline are caught."""

    def test_extra_workflow_command_detected(self):
        """
        A workflow with an alternative build command not in baseline
        produces a parity violation.
        """
        workflow_content = (FIXTURES_DIR / "ci_workflow_with_extra_command.yaml").read_text()
        baseline_content = BASELINE_PATH.read_text()
        readme_content = README_PATH.read_text()

        workflow_cmds = extract_workflow_run_commands(workflow_content)
        baseline_cmds = extract_baseline_canonical_commands(baseline_content)
        readme_cmds = extract_readme_commands(readme_content)

        validator = LocalCIParityValidator(workflow_cmds, baseline_cmds, readme_cmds)
        violations = validator.validate()
        # The extra command "cmake --build build/host --target run_all_tests"
        # should be flagged
        workflow_violations = [v for v in violations if v.source == "workflow"]
        assert len(workflow_violations) > 0, "Expected parity violation for extra command"
        assert any("run_all_tests" in v.command for v in workflow_violations)

    def test_completely_different_workflow_command(self):
        """A completely non-canonical build command is caught."""
        workflow_cmds = ["cmake --build build/host --target custom_stuff"]
        baseline_content = BASELINE_PATH.read_text()
        baseline_cmds = extract_baseline_canonical_commands(baseline_content)
        readme_cmds = []

        validator = LocalCIParityValidator(workflow_cmds, baseline_cmds, readme_cmds)
        violations = validator.validate()
        assert len(violations) > 0

    def test_readme_with_noncanonical_command(self):
        """README with a command not in baseline is flagged."""
        workflow_cmds = []
        baseline_content = BASELINE_PATH.read_text()
        baseline_cmds = extract_baseline_canonical_commands(baseline_content)
        readme_cmds = ["cmake --build build/host --target run_all_integration"]

        validator = LocalCIParityValidator(workflow_cmds, baseline_cmds, readme_cmds)
        violations = validator.validate()
        readme_violations = [v for v in violations if v.source == "readme"]
        assert len(readme_violations) > 0


# ===========================================================================
# 5. Integration — Actual project consistency check
# ===========================================================================

class TestActualProjectParity:
    """Verify the actual project files maintain local/CI parity."""

    def test_actual_workflow_extracts_commands(self):
        """The actual workflow file can be parsed for commands."""
        workflow_content = WORKFLOW_PATH.read_text()
        cmds = extract_workflow_run_commands(workflow_content)
        assert len(cmds) > 0, "No commands extracted from actual workflow"

    def test_actual_baseline_extracts_commands(self):
        """The actual baseline.yaml provides canonical commands."""
        baseline_content = BASELINE_PATH.read_text()
        cmds = extract_baseline_canonical_commands(baseline_content)
        # Should have at least: host-configure, host-build, host-test,
        # firmware-set-target, firmware-build
        assert len(cmds) >= 5, f"Expected >= 5 baseline commands, got {len(cmds)}"

    def test_actual_idf_build_command_in_baseline(self):
        """The workflow's idf.py build is covered by baseline."""
        workflow_content = WORKFLOW_PATH.read_text()
        baseline_content = BASELINE_PATH.read_text()

        workflow_cmds = extract_workflow_run_commands(workflow_content)
        baseline_cmds = extract_baseline_canonical_commands(baseline_content)

        # Find idf.py commands in workflow
        idf_cmds = [c for c in workflow_cmds if "idf.py" in c]
        assert len(idf_cmds) > 0, "No idf.py commands in workflow"

        # Each idf.py command should have a baseline equivalent
        baseline_normalized = {normalize_command(c) for c in baseline_cmds}
        for cmd in idf_cmds:
            # Extract the core idf.py action
            import re
            match = re.search(r"idf\.py\s+(\S+)", cmd)
            if match:
                action = match.group(1)
                # Should find it in baseline
                found = any(f"idf.py" in bc and action in bc for bc in baseline_normalized)
                assert found, (
                    f"idf.py {action} from workflow not in baseline"
                )


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    pytest.main([__file__, "-v"])
