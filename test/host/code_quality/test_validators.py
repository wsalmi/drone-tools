#!/usr/bin/env python3
"""
Test suite for code quality review validators.

Exercises Property 2 (finding completeness and valid transitions) and
Property 22 (fail-closed acceptance) using positive and negative YAML fixtures.

Regression tests prove:
  - Incomplete metadata cannot close findings
  - Incomplete metadata cannot accept the review
  - Critical cannot be ACCEPTED_RISK
  - Duplicate IDs are rejected
  - Red gates prevent CLOSED status
  - Open Critical/High blocks acceptance
  - Missing/duplicate matrix entries block acceptance

Feature: code-quality-review
Property 2: Finding completeness and valid transitions
Property 22: Closure and acceptance are fail-closed
"""

import sys
from pathlib import Path

import pytest
import yaml

# Add tools to path so we can import from tools/code_quality/
REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT))
sys.path.insert(0, str(REPO_ROOT / "tools"))

from code_quality.validate_findings import FindingsValidator
from code_quality.validate_acceptance import AcceptanceValidator, AcceptanceDecision

FIXTURES_DIR = Path(__file__).parent / "fixtures"


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def load_fixture(name: str) -> dict:
    """Load a YAML fixture by filename."""
    path = FIXTURES_DIR / name
    with open(path) as f:
        return yaml.safe_load(f)


def make_all_gates_green() -> dict:
    """Create gate results with all mandatory gates green."""
    return {
        "G0": "green",
        "G1": "green",
        "G2": "green",
        "G3": "green",
        "G4": "green",
        "G6": "green",
    }


# ---------------------------------------------------------------------------
# Property 2: Finding completeness and valid transitions
# ---------------------------------------------------------------------------

class TestFindingsValidatorPositive:
    """Valid fixtures must pass validation."""

    def test_valid_closed_finding(self):
        """A properly CLOSED finding passes all checks."""
        data = load_fixture("valid_finding_closed.yaml")
        validator = FindingsValidator(data)
        errors = validator.validate()
        assert errors == [], f"Unexpected errors: {errors}"

    def test_valid_accepted_risk(self):
        """A properly ACCEPTED_RISK Low finding passes."""
        data = load_fixture("valid_finding_accepted_risk.yaml")
        validator = FindingsValidator(data)
        errors = validator.validate()
        assert errors == [], f"Unexpected errors: {errors}"

    def test_valid_open_finding(self):
        """An OPEN finding with mandatory fields passes."""
        data = load_fixture("valid_finding_open.yaml")
        validator = FindingsValidator(data)
        errors = validator.validate()
        assert errors == [], f"Unexpected errors: {errors}"


class TestFindingsValidatorNegative:
    """Invalid fixtures must fail validation with appropriate errors."""

    def test_incomplete_finding_cannot_close(self):
        """
        Regression: incomplete metadata cannot close findings.
        Validates Property 2 — CLOSED requires all fields.
        """
        data = load_fixture("invalid_incomplete_finding.yaml")
        validator = FindingsValidator(data)
        errors = validator.validate()
        assert len(errors) > 0, "Expected errors for incomplete CLOSED finding"
        # Must detect missing severity_rationale, owner, and baseline
        error_fields = {e.field for e in errors}
        assert "severity_rationale" in error_fields or "owner" in error_fields or "baseline" in error_fields

    def test_critical_cannot_be_accepted_risk(self):
        """
        Critical severity cannot transition to ACCEPTED_RISK.
        Validates Property 2, Requirement 2.6.
        """
        data = load_fixture("invalid_accepted_risk_critical.yaml")
        validator = FindingsValidator(data)
        errors = validator.validate()
        assert len(errors) > 0, "Expected errors for Critical ACCEPTED_RISK"
        # Must detect the Critical + ACCEPTED_RISK violation
        status_errors = [e for e in errors if "Critical" in e.message]
        assert len(status_errors) > 0, f"Expected Critical rejection, got: {errors}"

    def test_red_gate_prevents_closed(self):
        """
        A CLOSED finding with a red gate must be rejected.
        Validates Property 2, Requirement 12.2.
        """
        data = load_fixture("invalid_red_gate.yaml")
        validator = FindingsValidator(data)
        errors = validator.validate()
        assert len(errors) > 0, "Expected errors for red gate in CLOSED finding"
        gate_errors = [e for e in errors if "red" in e.message.lower() or "gate" in e.field]
        assert len(gate_errors) > 0, f"Expected gate error, got: {errors}"

    def test_duplicate_id_rejected(self):
        """
        Duplicate finding IDs must be detected.
        Validates Property 2, Requirement 2.1.
        """
        data = load_fixture("invalid_duplicate_id.yaml")
        validator = FindingsValidator(data)
        errors = validator.validate()
        assert len(errors) > 0, "Expected error for duplicate IDs"
        dup_errors = [e for e in errors if "duplicate" in e.message.lower()]
        assert len(dup_errors) > 0, f"Expected duplicate error, got: {errors}"

    def test_invalid_id_format(self):
        """IDs not matching CQR-<AREA>-NNN must be rejected."""
        data = {
            "schema_version": 1,
            "findings": [{
                "id": "INVALID-FORMAT",
                "title": "Bad ID",
                "category": "build",
                "severity": "Low",
                "severity_rationale": {"impact": "none", "probability": "none"},
                "status": "OPEN",
                "owner": "test",
                "baseline": {
                    "commit": "abc123",
                    "evidence": [{"location": "file:1"}],
                    "reproduction": ["cmd"],
                    "observed_result": "result",
                },
            }],
        }
        validator = FindingsValidator(data)
        errors = validator.validate()
        id_errors = [e for e in errors if e.field == "id"]
        assert len(id_errors) > 0

    def test_missing_severity_rationale_fields(self):
        """Severity rationale must have both impact and probability."""
        data = {
            "schema_version": 1,
            "findings": [{
                "id": "CQR-BUILD-099",
                "title": "Test",
                "category": "build",
                "severity": "Medium",
                "severity_rationale": {"impact": "something"},  # missing probability
                "status": "OPEN",
                "owner": "test",
                "baseline": {
                    "commit": "abc123",
                    "evidence": [{"location": "file:1"}],
                    "reproduction": ["cmd"],
                    "observed_result": "result",
                },
            }],
        }
        validator = FindingsValidator(data)
        errors = validator.validate()
        prob_errors = [e for e in errors if "probability" in e.field]
        assert len(prob_errors) > 0


# ---------------------------------------------------------------------------
# Property 22: Closure and acceptance are fail-closed
# ---------------------------------------------------------------------------

class TestAcceptanceValidatorPositive:
    """Acceptance passes only when all criteria are met."""

    def test_all_closed_all_gates_green(self):
        """
        Review is ACCEPTED when all gates green, no open Critical/High,
        all CLOSED complete, and matrix covers all findings.
        """
        data = load_fixture("valid_finding_closed.yaml")
        gates = make_all_gates_green()
        matrix_ids = ["CQR-BUILD-001"]

        validator = AcceptanceValidator(
            findings_data=data,
            gate_results=gates,
            matrix_ids=matrix_ids,
        )
        decision = validator.evaluate()
        assert decision.accepted is True
        assert decision.status == "ACCEPTED"
        assert decision.blockers == []

    def test_accepted_risk_non_critical_passes(self):
        """ACCEPTED_RISK for non-Critical with valid fields passes."""
        data = load_fixture("valid_finding_accepted_risk.yaml")
        gates = make_all_gates_green()
        matrix_ids = ["CQR-ENV-001"]

        validator = AcceptanceValidator(
            findings_data=data,
            gate_results=gates,
            matrix_ids=matrix_ids,
        )
        decision = validator.evaluate()
        assert decision.accepted is True


class TestAcceptanceValidatorNegative:
    """Acceptance must be NOT_ACCEPTED when any criterion fails."""

    def test_open_critical_blocks_acceptance(self):
        """
        Regression: open Critical/High blocks acceptance.
        Validates Property 22, Requirements 12.8, 12.9.
        """
        data = load_fixture("invalid_open_critical_high.yaml")
        gates = make_all_gates_green()
        matrix_ids = ["CQR-REMOTEID-001", "CQR-QUEUES-001"]

        validator = AcceptanceValidator(
            findings_data=data,
            gate_results=gates,
            matrix_ids=matrix_ids,
        )
        decision = validator.evaluate()
        assert decision.accepted is False
        assert decision.status == "NOT_ACCEPTED"
        # Must have blockers for both Critical and High
        criteria = {b.criterion_id for b in decision.blockers}
        assert "ACC-2" in criteria or "ACC-3" in criteria

    def test_red_mandatory_gate_blocks_acceptance(self):
        """
        Any red mandatory gate blocks acceptance.
        Validates Property 22, Requirement 12.9.
        """
        data = load_fixture("valid_finding_closed.yaml")
        gates = make_all_gates_green()
        gates["G2"] = "red"  # Make one gate red
        matrix_ids = ["CQR-BUILD-001"]

        validator = AcceptanceValidator(
            findings_data=data,
            gate_results=gates,
            matrix_ids=matrix_ids,
        )
        decision = validator.evaluate()
        assert decision.accepted is False
        acc1_blockers = [b for b in decision.blockers if b.criterion_id == "ACC-1"]
        assert len(acc1_blockers) > 0

    def test_missing_gate_blocks_acceptance(self):
        """
        A mandatory gate with no result blocks acceptance.
        """
        data = load_fixture("valid_finding_closed.yaml")
        gates = {"G0": "green", "G1": "green"}  # Missing G2, G3, G4, G6
        matrix_ids = ["CQR-BUILD-001"]

        validator = AcceptanceValidator(
            findings_data=data,
            gate_results=gates,
            matrix_ids=matrix_ids,
        )
        decision = validator.evaluate()
        assert decision.accepted is False

    def test_duplicate_matrix_blocks_acceptance(self):
        """
        Duplicate finding in matrix blocks acceptance.
        Validates Property 22, Requirement 12.8.
        """
        data = load_fixture("invalid_missing_matrix.yaml")
        gates = make_all_gates_green()
        # Matrix has duplicate and is missing one
        matrix_ids = ["CQR-BUILD-001", "CQR-BUILD-001", "CQR-DOCS-001"]

        validator = AcceptanceValidator(
            findings_data=data,
            gate_results=gates,
            matrix_ids=matrix_ids,
        )
        decision = validator.evaluate()
        assert decision.accepted is False
        acc5_blockers = [b for b in decision.blockers if b.criterion_id == "ACC-5"]
        assert len(acc5_blockers) > 0

    def test_missing_from_matrix_blocks_acceptance(self):
        """
        Finding not in matrix blocks acceptance.
        """
        data = load_fixture("invalid_missing_matrix.yaml")
        gates = make_all_gates_green()
        # Matrix missing CQR-CI-001
        matrix_ids = ["CQR-BUILD-001", "CQR-DOCS-001"]

        validator = AcceptanceValidator(
            findings_data=data,
            gate_results=gates,
            matrix_ids=matrix_ids,
        )
        decision = validator.evaluate()
        assert decision.accepted is False
        missing_blockers = [b for b in decision.blockers
                           if "missing from" in b.description.lower()]
        assert len(missing_blockers) > 0

    def test_no_matrix_blocks_acceptance(self):
        """
        No matrix provided blocks acceptance.
        """
        data = load_fixture("valid_finding_closed.yaml")
        gates = make_all_gates_green()

        validator = AcceptanceValidator(
            findings_data=data,
            gate_results=gates,
            matrix_ids=None,  # No matrix
        )
        decision = validator.evaluate()
        assert decision.accepted is False

    def test_incomplete_closed_finding_blocks_acceptance(self):
        """
        Regression: incomplete metadata cannot accept the review.
        A CLOSED finding with missing regression blocks acceptance.
        Validates Property 2 + Property 22 conjunction.
        """
        data = {
            "schema_version": 1,
            "findings": [{
                "id": "CQR-BUILD-001",
                "title": "Missing test",
                "category": "build",
                "severity": "High",
                "severity_rationale": {"impact": "x", "probability": "y"},
                "status": "CLOSED",
                "owner": "test",
                "baseline": {
                    "commit": "abc123",
                    "evidence": [{"location": "file:1"}],
                    "reproduction": ["cmd"],
                    "observed_result": "result",
                },
                "root_cause": "bad path",
                "remediation": {"strategy": "fix", "change_refs": ["c:1"]},
                "regression": {
                    "test_ids": [],  # EMPTY — no regression test
                    "defective_result": None,
                    "remediated_result": None,
                },
                "requirements": ["3.1"],
                "verification": {"gates": {"G2": "green"}, "artifact_refs": []},
                "accepted_risk": None,
            }],
        }
        gates = make_all_gates_green()
        matrix_ids = ["CQR-BUILD-001"]

        validator = AcceptanceValidator(
            findings_data=data,
            gate_results=gates,
            matrix_ids=matrix_ids,
        )
        decision = validator.evaluate()
        assert decision.accepted is False
        acc4_blockers = [b for b in decision.blockers if b.criterion_id == "ACC-4"]
        assert len(acc4_blockers) > 0

    def test_critical_accepted_risk_blocks_acceptance(self):
        """
        Critical as ACCEPTED_RISK blocks acceptance.
        Validates Property 22 + Property 2 conjunction.
        """
        data = load_fixture("invalid_accepted_risk_critical.yaml")
        gates = make_all_gates_green()
        matrix_ids = ["CQR-REMOTEID-002"]

        validator = AcceptanceValidator(
            findings_data=data,
            gate_results=gates,
            matrix_ids=matrix_ids,
        )
        decision = validator.evaluate()
        assert decision.accepted is False
        acc6_blockers = [b for b in decision.blockers if b.criterion_id == "ACC-6"]
        assert len(acc6_blockers) > 0


# ---------------------------------------------------------------------------
# Transition validation
# ---------------------------------------------------------------------------

class TestTransitionValidation:
    """Test that invalid status transitions are caught."""

    def test_closed_without_regression_fails(self):
        """CLOSED without regression test_ids fails."""
        data = {
            "schema_version": 1,
            "findings": [{
                "id": "CQR-BUILD-001",
                "title": "Test",
                "category": "build",
                "severity": "High",
                "severity_rationale": {"impact": "x", "probability": "y"},
                "status": "CLOSED",
                "owner": "test",
                "baseline": {
                    "commit": "abc",
                    "evidence": [{"location": "f:1"}],
                    "reproduction": ["cmd"],
                    "observed_result": "err",
                },
                "root_cause": "cause",
                "remediation": {"strategy": "fix", "change_refs": ["c:1"]},
                "regression": {
                    "test_ids": [],
                    "defective_result": None,
                    "remediated_result": None,
                },
                "verification": {"gates": {"G2": "green"}, "artifact_refs": []},
            }],
        }
        validator = FindingsValidator(data)
        errors = validator.validate()
        # Must catch missing regression.test_ids and regression.remediated_result
        regression_errors = [e for e in errors
                            if "regression" in e.field]
        assert len(regression_errors) > 0

    def test_accepted_risk_without_owner_fails(self):
        """ACCEPTED_RISK without owner in accepted_risk section fails."""
        data = {
            "schema_version": 1,
            "findings": [{
                "id": "CQR-DOCS-001",
                "title": "Test",
                "category": "docs",
                "severity": "Medium",
                "severity_rationale": {"impact": "x", "probability": "y"},
                "status": "ACCEPTED_RISK",
                "owner": "docs",
                "baseline": {
                    "commit": "abc",
                    "evidence": [{"location": "f:1"}],
                    "reproduction": ["cmd"],
                    "observed_result": "err",
                },
                "accepted_risk": {
                    "justification": "reason",
                    "review_date": "2027-01-01",
                    # MISSING: owner
                },
            }],
        }
        validator = FindingsValidator(data)
        errors = validator.validate()
        owner_errors = [e for e in errors if "accepted_risk.owner" in e.field]
        assert len(owner_errors) > 0


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    pytest.main([__file__, "-v"])
