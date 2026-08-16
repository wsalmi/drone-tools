#!/usr/bin/env python3
"""
Test suite for the policy.yaml validator.

Verifies that the policy validator correctly rejects incomplete or
invalid policy definitions and accepts the actual project policy.

Feature: code-quality-review
Property 2: Finding completeness and valid transitions
"""

import sys
from pathlib import Path

import pytest
import yaml

REPO_ROOT = Path(__file__).resolve().parents[3]

from code_quality.validate_policy import PolicyValidator


POLICY_PATH = REPO_ROOT / ".kiro" / "specs" / "code-quality-review" / "artifacts" / "policy.yaml"


class TestPolicyValidatorPositive:
    """The actual policy.yaml must pass validation."""

    def test_project_policy_is_valid(self):
        """The committed policy.yaml passes all checks."""
        with open(POLICY_PATH) as f:
            data = yaml.safe_load(f)
        validator = PolicyValidator(data)
        errors = validator.validate()
        assert errors == [], f"Policy validation errors: {errors}"

    def test_policy_has_fail_closed(self):
        """Policy explicitly declares fail_closed: true."""
        with open(POLICY_PATH) as f:
            data = yaml.safe_load(f)
        assert data["acceptance"]["fail_closed"] is True

    def test_policy_critical_blocks_acceptance(self):
        """Critical severity has blocks_acceptance: true."""
        with open(POLICY_PATH) as f:
            data = yaml.safe_load(f)
        critical = next(
            lvl for lvl in data["severity"]["levels"]
            if lvl["name"] == "Critical"
        )
        assert critical["implications"]["blocks_acceptance"] is True

    def test_policy_critical_no_accepted_risk(self):
        """Critical severity has accepted_risk_permitted: false."""
        with open(POLICY_PATH) as f:
            data = yaml.safe_load(f)
        critical = next(
            lvl for lvl in data["severity"]["levels"]
            if lvl["name"] == "Critical"
        )
        assert critical["implications"]["accepted_risk_permitted"] is False


class TestPolicyValidatorNegative:
    """Invalid policies must be rejected."""

    def test_missing_severity_section(self):
        """Policy without severity section fails."""
        data = {
            "schema_version": 1,
            "status_transitions": {"valid_statuses": [], "allowed_transitions": {}},
            "gates": {"definitions": {}},
            "suppressions": {"rules": [], "required_fields": []},
            "skips": {"rules": []},
            "acceptance": {"fail_closed": True, "criteria": []},
            "finding_id": {"pattern": "^CQR-.*$"},
        }
        validator = PolicyValidator(data)
        errors = validator.validate()
        section_errors = [e for e in errors if e.section == "severity"]
        assert len(section_errors) > 0

    def test_missing_fail_closed(self):
        """Policy without fail_closed: true fails."""
        data = {
            "schema_version": 1,
            "severity": {
                "levels": [
                    {"name": "Critical", "implications": {}},
                    {"name": "High", "implications": {}},
                    {"name": "Medium", "implications": {}},
                    {"name": "Low", "implications": {}},
                ],
                "rationale_required": True,
            },
            "status_transitions": {
                "valid_statuses": ["OPEN", "IN_REMEDIATION", "VERIFYING", "CLOSED", "ACCEPTED_RISK"],
                "allowed_transitions": {"OPEN": ["IN_REMEDIATION"]},
                "status_requirements": {
                    "CLOSED": {"required_fields": []},
                    "ACCEPTED_RISK": {"required_fields": []},
                },
            },
            "gates": {"definitions": {"G0": {"mandatory": True}}},
            "suppressions": {"rules": [{"description": "x"}], "required_fields": ["finding_id"]},
            "skips": {"rules": [{"description": "x"}]},
            "acceptance": {"fail_closed": False, "criteria": [{"id": "ACC-1"}]},
            "finding_id": {"pattern": "^CQR-.*$"},
        }
        validator = PolicyValidator(data)
        errors = validator.validate()
        acceptance_errors = [e for e in errors if e.section == "acceptance"]
        assert len(acceptance_errors) > 0

    def test_missing_mandatory_gate(self):
        """Policy with no mandatory gate fails."""
        data = {
            "schema_version": 1,
            "severity": {
                "levels": [
                    {"name": "Critical", "implications": {}},
                    {"name": "High", "implications": {}},
                    {"name": "Medium", "implications": {}},
                    {"name": "Low", "implications": {}},
                ],
                "rationale_required": True,
            },
            "status_transitions": {
                "valid_statuses": ["OPEN", "IN_REMEDIATION", "VERIFYING", "CLOSED", "ACCEPTED_RISK"],
                "allowed_transitions": {"OPEN": ["IN_REMEDIATION"]},
                "status_requirements": {
                    "CLOSED": {"required_fields": []},
                    "ACCEPTED_RISK": {"required_fields": []},
                },
            },
            "gates": {"definitions": {"G0": {"mandatory": False}}},
            "suppressions": {"rules": [{"description": "x"}], "required_fields": ["finding_id"]},
            "skips": {"rules": [{"description": "x"}]},
            "acceptance": {"fail_closed": True, "criteria": [{"id": "ACC-1"}]},
            "finding_id": {"pattern": "^CQR-.*$"},
        }
        validator = PolicyValidator(data)
        errors = validator.validate()
        gate_errors = [e for e in errors if e.section == "gates"]
        assert len(gate_errors) > 0

    def test_suppression_without_finding_id_requirement(self):
        """Suppression rules must require finding_id."""
        data = {
            "schema_version": 1,
            "severity": {
                "levels": [
                    {"name": "Critical", "implications": {}},
                    {"name": "High", "implications": {}},
                    {"name": "Medium", "implications": {}},
                    {"name": "Low", "implications": {}},
                ],
                "rationale_required": True,
            },
            "status_transitions": {
                "valid_statuses": ["OPEN", "IN_REMEDIATION", "VERIFYING", "CLOSED", "ACCEPTED_RISK"],
                "allowed_transitions": {"OPEN": ["IN_REMEDIATION"]},
                "status_requirements": {
                    "CLOSED": {"required_fields": []},
                    "ACCEPTED_RISK": {"required_fields": []},
                },
            },
            "gates": {"definitions": {"G0": {"mandatory": True}}},
            "suppressions": {"rules": [{"description": "x"}], "required_fields": ["justification"]},
            "skips": {"rules": [{"description": "x"}]},
            "acceptance": {"fail_closed": True, "criteria": [{"id": "ACC-1"}]},
            "finding_id": {"pattern": "^CQR-.*$"},
        }
        validator = PolicyValidator(data)
        errors = validator.validate()
        supp_errors = [e for e in errors if e.section == "suppressions"]
        assert len(supp_errors) > 0


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
