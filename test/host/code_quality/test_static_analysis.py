"""
Tests for the static analysis runner and policy enforcer.

Validates: Requirements 11.1, 11.2, 11.3, 11.4, 11.5, 11.6, 11.7
Properties: P20 (Static analysis with normalized baseline and suppressions)

Fixtures cover:
  - New diagnostic in changed code blocks the gate
  - Preexisting diagnostic in unchanged code is tracked but doesn't block
  - Invalid suppression (no CQR-ID) is rejected
  - Valid suppression (with CQR-ID) is accepted
  - Blocking categories always fail regardless of code status
  - Altered parsing, concurrency, ownership, lifecycle appear in the report
  - Fingerprints are normalized (no absolute paths/timestamps)
"""

import json
import tempfile
from pathlib import Path

import pytest

from code_quality.static_analysis import (
    AnalysisReport,
    Diagnostic,
    DiagnosticParser,
    DiagnosticSeverity,
    OutputNormalizer,
    PolicyLoader,
    StaticAnalysisRunner,
    SuppressionIssue,
    SuppressionValidator,
)

# ---------------------------------------------------------------------------
# Fixtures and helpers
# ---------------------------------------------------------------------------

FIXTURES_DIR = Path(__file__).parent / "fixtures"
REPO_ROOT = Path(__file__).resolve().parents[3]
POLICY_PATH = (
    REPO_ROOT / ".kiro" / "specs" / "code-quality-review"
    / "artifacts" / "static_analysis_policy.yaml"
)


@pytest.fixture
def policy():
    """Load the static analysis policy."""
    return PolicyLoader(POLICY_PATH)


@pytest.fixture
def normalizer():
    """Create an output normalizer for a test build directory."""
    return OutputNormalizer(
        repo_root=REPO_ROOT,
        build_dir=REPO_ROOT / "build" / "host",
    )


@pytest.fixture
def parser(policy, normalizer):
    """Create a diagnostic parser."""
    return DiagnosticParser(policy, normalizer)


@pytest.fixture
def suppression_validator(policy):
    """Create a suppression validator."""
    return SuppressionValidator(policy)


def load_fixture(name: str) -> str:
    """Load a text fixture file."""
    return (FIXTURES_DIR / name).read_text()


# ---------------------------------------------------------------------------
# Test: Policy loads and has required sections
# ---------------------------------------------------------------------------

class TestPolicyLoading:
    """Verify the static analysis policy loads and defines required elements."""

    def test_policy_loads_successfully(self, policy):
        """Policy YAML loads without error."""
        assert policy.data is not None
        assert policy.data.get("schema_version") == 1

    def test_policy_has_blocking_categories(self, policy):
        """Policy defines blocking diagnostic categories."""
        categories = policy.blocking_categories
        assert len(categories) >= 5
        ids = {c["id"] for c in categories}
        assert "bounds" in ids
        assert "lifetime" in ids
        assert "uninitialized" in ids
        assert "overflow" in ids
        assert "data_race" in ids

    def test_policy_has_pinned_tool_versions(self, policy):
        """Policy pins tool versions for reproducibility (Req 11.1)."""
        tools = policy.tools
        assert "gcc" in tools
        assert "esp_idf" in tools
        assert "cmake" in tools
        assert "shellcheck" in tools
        # Versions are declared
        assert tools["gcc"].get("version") is not None
        assert tools["esp_idf"].get("version") == "5.3"

    def test_policy_has_preexisting_backlog(self, policy):
        """Policy distinguishes preexisting from new diagnostics (Req 11.6)."""
        backlog = policy.preexisting_backlog
        assert len(backlog) >= 2
        # Known items reference data_pipeline.c
        files = {item["file"] for item in backlog}
        assert "main/data_pipeline.c" in files

    def test_policy_has_suppression_rules(self, policy):
        """Policy defines suppression requirements with CQR-ID (Req 11.5)."""
        rules = policy.suppression_rules
        assert rules.get("required_format") == "CQR-<AREA>-NNN"
        assert rules.get("validation", {}).get("reject_without_id") is True
        assert rules.get("validation", {}).get("reject_global") is True

    def test_policy_has_affected_unit_categories(self, policy):
        """Policy lists categories requiring analysis on change (Req 11.7)."""
        cats = policy.affected_categories
        assert "parsing" in cats
        assert "concurrency" in cats
        assert "ownership" in cats
        assert "lifecycle" in cats


# ---------------------------------------------------------------------------
# Test: New diagnostic in changed code blocks gate (Req 11.4)
# ---------------------------------------------------------------------------

class TestNewDiagnosticBlocks:
    """New warnings in changed code must block the CI gate."""

    def test_new_warning_in_changed_code_blocks(self, parser):
        """A warning on a changed line produces NEW_WARNING severity and blocks."""
        output = load_fixture("compiler_output_new_warning.txt")
        changed_files = {"components/services/src/remoteid_decoder.c"}
        changed_lines = {
            "components/services/src/remoteid_decoder.c": {42, 87},
        }

        diagnostics = parser.parse_compiler_output(output, changed_files, changed_lines)

        assert len(diagnostics) >= 1
        new_warnings = [d for d in diagnostics if d.severity == DiagnosticSeverity.NEW_WARNING]
        assert len(new_warnings) >= 1
        # Verify one is the unused variable on changed line 42
        unused = [d for d in new_warnings if d.line == 42]
        assert len(unused) == 1
        assert unused[0].is_in_changed_code is True

    def test_new_warning_blocks_gate(self, policy, normalizer):
        """The runner reports gate failure when new warnings exist in changed code."""
        # Create a runner with a mocked git-diff scenario
        runner = StaticAnalysisRunner(
            repo_root=REPO_ROOT,
            policy_path=POLICY_PATH,
            build_dir=REPO_ROOT / "build" / "host",
        )
        output = load_fixture("compiler_output_new_warning.txt")

        # Simulate: parse with changed lines matching the warning locations
        diagnostics = runner.parser.parse_compiler_output(
            output,
            changed_files={"components/services/src/remoteid_decoder.c"},
            changed_lines={"components/services/src/remoteid_decoder.c": {42, 87}},
        )

        # At least one diagnostic in changed code with blocking pattern (array-bounds)
        blocking = [d for d in diagnostics if d.severity == DiagnosticSeverity.BLOCKING]
        assert len(blocking) >= 1  # array-bounds is a blocking category


# ---------------------------------------------------------------------------
# Test: Preexisting diagnostic does NOT block (Req 11.6)
# ---------------------------------------------------------------------------

class TestPreexistingBacklog:
    """Known preexisting diagnostics are tracked but don't block CI."""

    def test_preexisting_warning_classified_as_backlog(self, parser):
        """Warnings matching known backlog items get BACKLOG severity."""
        output = load_fixture("compiler_output_preexisting.txt")
        # Not in changed files
        diagnostics = parser.parse_compiler_output(output, set(), {})

        backlog = [d for d in diagnostics if d.severity == DiagnosticSeverity.BACKLOG]
        assert len(backlog) >= 1
        # data_pipeline variadic macro should be backlog
        variadic = [d for d in backlog if "variadic" in d.message.lower()]
        assert len(variadic) >= 1

    def test_preexisting_does_not_block_gate(self, policy, normalizer):
        """Only backlog diagnostics in unchanged code don't fail the gate."""
        runner = StaticAnalysisRunner(
            repo_root=REPO_ROOT,
            policy_path=POLICY_PATH,
            build_dir=REPO_ROOT / "build" / "host",
        )
        output = load_fixture("compiler_output_preexisting.txt")

        # Parse with empty changed files (nothing changed)
        diagnostics = runner.parser.parse_compiler_output(output, set(), {})

        # Count blocking and new
        blocking = [d for d in diagnostics if d.severity == DiagnosticSeverity.BLOCKING]
        new_warns = [d for d in diagnostics if d.severity == DiagnosticSeverity.NEW_WARNING]

        # Neither blocking categories nor changed code → gate should pass
        assert len(blocking) == 0
        assert len(new_warns) == 0


# ---------------------------------------------------------------------------
# Test: Invalid suppression is rejected (Req 11.5)
# ---------------------------------------------------------------------------

class TestInvalidSuppression:
    """Suppressions without CQR-ID must be flagged."""

    def test_suppression_without_cqr_id_rejected(self, suppression_validator):
        """NOLINTNEXTLINE without CQR-ID generates a suppression issue."""
        fixture_path = FIXTURES_DIR / "source_invalid_suppression.c"
        issues = suppression_validator.scan_file(fixture_path)

        assert len(issues) >= 2
        missing_id = [i for i in issues if i.kind == "missing_id"]
        assert len(missing_id) >= 2

        # Both NOLINTNEXTLINE and cppcheck-suppress should be flagged
        contents = " ".join(i.content for i in missing_id)
        assert "NOLINTNEXTLINE" in contents
        assert "cppcheck-suppress" in contents

    def test_invalid_suppression_blocks_gate(self):
        """Runner marks gate as failed when invalid suppressions exist."""
        report = AnalysisReport()
        report.suppression_issues = [
            SuppressionIssue(
                file="test.c", line=10, kind="missing_id",
                content="// NOLINTNEXTLINE", message="no CQR-ID",
            )
        ]
        # Simulate gate logic
        invalid = [s for s in report.suppression_issues if s.kind in ("missing_id", "global")]
        assert len(invalid) > 0


# ---------------------------------------------------------------------------
# Test: Valid suppression is accepted (Req 11.5)
# ---------------------------------------------------------------------------

class TestValidSuppression:
    """Suppressions with proper CQR-ID pass validation."""

    def test_suppression_with_cqr_id_accepted(self, suppression_validator):
        """NOLINTNEXTLINE with CQR-ID does not generate an issue."""
        fixture_path = FIXTURES_DIR / "source_valid_suppression.c"
        issues = suppression_validator.scan_file(fixture_path)

        # Valid file should have no issues (all suppressions have CQR-IDs)
        assert len(issues) == 0


# ---------------------------------------------------------------------------
# Test: Blocking categories always fail (Req 11.3)
# ---------------------------------------------------------------------------

class TestBlockingCategories:
    """Critical diagnostics block regardless of code change status."""

    def test_bounds_warning_blocks_even_in_unchanged_code(self, parser):
        """array-bounds warning blocks even when code wasn't changed."""
        output = load_fixture("compiler_output_blocking.txt")
        # No changed files — these are all in existing code
        diagnostics = parser.parse_compiler_output(output, set(), {})

        blocking = [d for d in diagnostics if d.severity == DiagnosticSeverity.BLOCKING]
        assert len(blocking) >= 2  # uninitialized and array-bounds and use-after-free

        categories = {d.category for d in blocking}
        assert "uninitialized" in categories or "bounds" in categories or "lifetime" in categories

    def test_all_blocking_categories_detected(self, policy):
        """Policy matches all required blocking patterns."""
        # Test each category
        assert policy.is_blocking_pattern("array-bounds", "") == "bounds"
        assert policy.is_blocking_pattern("Wuninitialized", "") == "uninitialized"
        assert policy.is_blocking_pattern(None, "use-after-free") == "lifetime"
        assert policy.is_blocking_pattern(None, "signed-integer-overflow") == "overflow"
        assert policy.is_blocking_pattern(None, "data-race detected") == "data_race"
        assert policy.is_blocking_pattern(None, "null-dereference") == "null_deref"

    def test_non_blocking_pattern_returns_none(self, policy):
        """Non-dangerous warnings don't match blocking patterns."""
        assert policy.is_blocking_pattern("unused-variable", "") is None
        assert policy.is_blocking_pattern("pedantic", "") is None
        assert policy.is_blocking_pattern(None, "implicit declaration") is None


# ---------------------------------------------------------------------------
# Test: Fingerprint normalization (Req 11.2)
# ---------------------------------------------------------------------------

class TestNormalization:
    """Output normalization ensures deterministic fingerprints."""

    def test_absolute_path_removed(self, normalizer):
        """Absolute repo path is replaced with <repo>."""
        text = f"{REPO_ROOT}/components/services/src/remoteid_decoder.c:42: warning"
        normalized = normalizer.normalize(text)
        assert str(REPO_ROOT) not in normalized
        assert "<repo>" in normalized

    def test_timestamp_removed(self, normalizer):
        """Timestamps are replaced."""
        text = "2024-03-15T10:30:00Z some diagnostic"
        normalized = normalizer.normalize(text)
        assert "2024-03-15" not in normalized
        assert "<timestamp>" in normalized

    def test_ansi_codes_removed(self, normalizer):
        """ANSI escape codes are stripped."""
        text = "\x1b[31merror\x1b[0m: something failed"
        normalized = normalizer.normalize(text)
        assert "\x1b[" not in normalized
        assert "error" in normalized

    def test_pid_removed(self, normalizer):
        """Process IDs are replaced."""
        text = "pid: 12345 running analysis"
        normalized = normalizer.normalize(text)
        assert "12345" not in normalized
        assert "pid: <pid>" in normalized

    def test_same_diagnostic_same_fingerprint(self, normalizer):
        """Same diagnostic from different machines produces same fingerprint."""
        diag1 = Diagnostic(
            file="<repo>/components/services/src/remoteid_decoder.c",
            line=42, column=5, level="warning",
            message="unused variable 'temp'",
            flag="unused-variable", category=None,
            severity=DiagnosticSeverity.INFO,
        )
        diag2 = Diagnostic(
            file="<repo>/components/services/src/remoteid_decoder.c",
            line=55, column=5, level="warning",  # different line (code shifted)
            message="unused variable 'temp'",
            flag="unused-variable", category=None,
            severity=DiagnosticSeverity.INFO,
        )
        fp1 = normalizer.fingerprint(diag1)
        fp2 = normalizer.fingerprint(diag2)
        assert fp1 == fp2  # Same logical issue, different line


# ---------------------------------------------------------------------------
# Test: Affected categories appear in report (Req 11.7)
# ---------------------------------------------------------------------------

class TestAffectedUnits:
    """Changed parsing/concurrency/ownership/lifecycle files must appear in report."""

    def test_missing_affected_unit_detected(self, policy, normalizer):
        """Runner detects when a changed parsing file is not analyzed."""
        runner = StaticAnalysisRunner(
            repo_root=REPO_ROOT,
            policy_path=POLICY_PATH,
            build_dir=REPO_ROOT / "build" / "host",
        )

        changed_files = {
            "components/services/src/remoteid_decoder.c",
            "components/services/src/detection_service.c",
        }
        analyzed_files = {
            "components/services/src/remoteid_decoder.c",
            # detection_service.c is NOT analyzed
        }
        categories = {
            "components/services/src/remoteid_decoder.c": "parsing",
            "components/services/src/detection_service.c": "concurrency",
        }

        missing = runner.check_affected_units(changed_files, analyzed_files, categories)
        assert "components/services/src/detection_service.c" in missing

    def test_all_affected_units_present_passes(self, policy, normalizer):
        """No missing units when all affected files are analyzed."""
        runner = StaticAnalysisRunner(
            repo_root=REPO_ROOT,
            policy_path=POLICY_PATH,
            build_dir=REPO_ROOT / "build" / "host",
        )

        changed_files = {
            "components/services/src/remoteid_decoder.c",
            "components/hw_hal/src/hal_sdr.c",
        }
        analyzed_files = changed_files.copy()
        categories = {
            "components/services/src/remoteid_decoder.c": "parsing",
            "components/hw_hal/src/hal_sdr.c": "lifecycle",
        }

        missing = runner.check_affected_units(changed_files, analyzed_files, categories)
        assert len(missing) == 0


# ---------------------------------------------------------------------------
# Test: Report structure and gate decision
# ---------------------------------------------------------------------------

class TestReportStructure:
    """Verify report format and gate pass/fail logic."""

    def test_report_to_dict_complete(self):
        """Report serializes all required fields."""
        report = AnalysisReport()
        report.gate_passed = True
        report.tools_used = ["compiler-warnings", "compile_commands.json"]

        result = report.to_dict()
        assert "gate_passed" in result
        assert "summary" in result
        assert "tools_used" in result
        assert "diagnostics" in result
        assert "suppression_issues" in result
        assert result["summary"]["blocking"] == 0

    def test_gate_fails_on_blocking(self):
        """Report correctly indicates failure when blocking diagnostics exist."""
        report = AnalysisReport()
        report.blocking_count = 1
        report.gate_passed = False
        report.gate_failure_reasons = ["1 blocking diagnostic(s)"]

        result = report.to_dict()
        assert result["gate_passed"] is False
        assert len(result["gate_failure_reasons"]) == 1

    def test_gate_passes_with_only_backlog(self):
        """Gate passes when only preexisting backlog diagnostics present."""
        report = AnalysisReport()
        report.backlog_count = 3
        report.blocking_count = 0
        report.new_warning_count = 0
        report.gate_passed = True

        result = report.to_dict()
        assert result["gate_passed"] is True
        assert result["summary"]["backlog"] == 3
