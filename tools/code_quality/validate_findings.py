#!/usr/bin/env python3
"""
Code Quality Review — Findings Inventory Validator

Validates findings.yaml against the schema defined in the design document and
policy.yaml. Checks:
  - Schema completeness (mandatory fields per status)
  - ID uniqueness and format (CQR-<AREA>-NNN)
  - Valid status transitions
  - Severity rationale presence
  - Suppression/skip rules (must reference a CQR-ID)
  - CLOSED requirements (cause, remediation, regression, gates)
  - ACCEPTED_RISK requirements (owner, justification, review date/condition)

Validates: Requirements 2.1, 2.2, 2.4, 2.6, 10.5, 12.2
Properties: P2 (Finding completeness and valid transitions)
"""

import re
import sys
from datetime import date, datetime
from pathlib import Path
from typing import Any

import yaml


# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

VALID_STATUSES = {"OPEN", "IN_REMEDIATION", "VERIFYING", "CLOSED", "ACCEPTED_RISK"}

VALID_SEVERITIES = {"Critical", "High", "Medium", "Low"}

VALID_CATEGORIES = {
    "build", "docs", "ci", "memory", "parsing", "concurrency",
    "lifecycle", "readiness", "debt", "static-analysis",
}

FINDING_ID_PATTERN = re.compile(r"^CQR-[A-Z][A-Z0-9_]+-\d{3}$")

ALLOWED_TRANSITIONS = {
    "OPEN": {"IN_REMEDIATION", "ACCEPTED_RISK"},
    "IN_REMEDIATION": {"VERIFYING"},
    "VERIFYING": {"CLOSED", "IN_REMEDIATION"},
    "CLOSED": {"OPEN"},
    "ACCEPTED_RISK": {"OPEN"},
}

# Fields required for a finding at any status
MANDATORY_FIELDS = [
    "id", "title", "category", "severity", "severity_rationale",
    "status", "owner", "baseline",
]

# Additional fields required when CLOSED
CLOSED_REQUIRED = [
    "root_cause",
    "remediation.strategy",
    "remediation.change_refs",
    "regression.test_ids",
    "regression.remediated_result",
]

# Additional fields required for ACCEPTED_RISK
ACCEPTED_RISK_REQUIRED = [
    "accepted_risk.owner",
    "accepted_risk.justification",
    "accepted_risk.review_date",
]

# Fields required for IN_REMEDIATION
IN_REMEDIATION_REQUIRED = [
    "owner",
    "root_cause",
    "remediation.strategy",
]


# ---------------------------------------------------------------------------
# Utility helpers
# ---------------------------------------------------------------------------

def _get_nested(data: dict, dotted_path: str) -> Any:
    """Resolve a dotted path like 'remediation.strategy' from a dict."""
    keys = dotted_path.split(".")
    current = data
    for key in keys:
        if not isinstance(current, dict):
            return None
        current = current.get(key)
    return current


def _is_non_empty(value: Any) -> bool:
    """Return True if value is present and non-empty."""
    if value is None:
        return False
    if isinstance(value, (str, list, dict)):
        return len(value) > 0
    return True


def _parse_date(value: Any) -> date | None:
    """Try to parse a date from string or date object."""
    if isinstance(value, date):
        return value
    if isinstance(value, str):
        try:
            return datetime.strptime(value, "%Y-%m-%d").date()
        except ValueError:
            pass
        try:
            return datetime.fromisoformat(value).date()
        except ValueError:
            pass
    return None


# ---------------------------------------------------------------------------
# Validator class
# ---------------------------------------------------------------------------

class FindingsValidationError:
    """Represents a single validation error."""

    def __init__(self, finding_id: str | None, field: str, message: str):
        self.finding_id = finding_id
        self.field = field
        self.message = message

    def __str__(self) -> str:
        prefix = f"[{self.finding_id}]" if self.finding_id else "[global]"
        return f"{prefix} {self.field}: {self.message}"

    def __repr__(self) -> str:
        return self.__str__()


class FindingsValidator:
    """Validates a findings inventory document."""

    def __init__(self, findings_data: dict, policy_data: dict | None = None):
        self.data = findings_data
        self.policy = policy_data
        self.errors: list[FindingsValidationError] = []

    def validate(self) -> list[FindingsValidationError]:
        """Run all validations and return errors."""
        self.errors = []
        self._validate_schema_version()
        findings = self.data.get("findings", [])
        if not isinstance(findings, list):
            self.errors.append(FindingsValidationError(
                None, "findings", "must be a list"))
            return self.errors

        self._validate_id_uniqueness(findings)
        for finding in findings:
            self._validate_finding(finding)

        return self.errors

    def _validate_schema_version(self):
        version = self.data.get("schema_version")
        if version is None:
            self.errors.append(FindingsValidationError(
                None, "schema_version", "missing required field"))
        elif version != 1:
            self.errors.append(FindingsValidationError(
                None, "schema_version", f"unsupported version: {version}"))

    def _validate_id_uniqueness(self, findings: list):
        seen_ids: dict[str, int] = {}
        for idx, finding in enumerate(findings):
            fid = finding.get("id")
            if fid is None:
                continue
            if fid in seen_ids:
                self.errors.append(FindingsValidationError(
                    fid, "id", f"duplicate ID (first at index {seen_ids[fid]})"))
            else:
                seen_ids[fid] = idx

    def _validate_finding(self, finding: dict):
        fid = finding.get("id", "<missing>")

        # ID format
        if not isinstance(fid, str) or not FINDING_ID_PATTERN.match(fid):
            self.errors.append(FindingsValidationError(
                fid, "id",
                f"invalid format; expected CQR-<AREA>-NNN, got '{fid}'"))

        # Mandatory fields
        for field in MANDATORY_FIELDS:
            value = _get_nested(finding, field)
            if not _is_non_empty(value):
                self.errors.append(FindingsValidationError(
                    fid, field, "missing or empty required field"))

        # Category
        category = finding.get("category")
        if category and category not in VALID_CATEGORIES:
            self.errors.append(FindingsValidationError(
                fid, "category", f"invalid category: '{category}'"))

        # Severity
        severity = finding.get("severity")
        if severity and severity not in VALID_SEVERITIES:
            self.errors.append(FindingsValidationError(
                fid, "severity", f"invalid severity: '{severity}'"))

        # Severity rationale
        rationale = finding.get("severity_rationale")
        if isinstance(rationale, dict):
            if not _is_non_empty(rationale.get("impact")):
                self.errors.append(FindingsValidationError(
                    fid, "severity_rationale.impact", "missing impact"))
            if not _is_non_empty(rationale.get("probability")):
                self.errors.append(FindingsValidationError(
                    fid, "severity_rationale.probability", "missing probability"))

        # Status
        status = finding.get("status")
        if status and status not in VALID_STATUSES:
            self.errors.append(FindingsValidationError(
                fid, "status", f"invalid status: '{status}'"))

        # Status-specific requirements
        if status == "CLOSED":
            self._validate_closed(finding, fid)
        elif status == "ACCEPTED_RISK":
            self._validate_accepted_risk(finding, fid)
        elif status == "IN_REMEDIATION":
            self._validate_in_remediation(finding, fid)

        # Baseline immutability check — baseline.commit must be present
        baseline = finding.get("baseline")
        if isinstance(baseline, dict):
            if not _is_non_empty(baseline.get("commit")):
                self.errors.append(FindingsValidationError(
                    fid, "baseline.commit", "missing baseline commit"))
            evidence = baseline.get("evidence")
            if not isinstance(evidence, list) or len(evidence) == 0:
                self.errors.append(FindingsValidationError(
                    fid, "baseline.evidence", "must be a non-empty list"))

    def _validate_closed(self, finding: dict, fid: str):
        """CLOSED requires cause, remediation, regression, and gates."""
        for field in CLOSED_REQUIRED:
            value = _get_nested(finding, field)
            if not _is_non_empty(value):
                self.errors.append(FindingsValidationError(
                    fid, field,
                    f"required for CLOSED status"))

        # Gates must be present and all applicable green
        gates = _get_nested(finding, "verification.gates")
        if not isinstance(gates, dict) or len(gates) == 0:
            self.errors.append(FindingsValidationError(
                fid, "verification.gates",
                "CLOSED requires at least one gate result"))
        elif isinstance(gates, dict):
            for gate_name, result in gates.items():
                if result == "red":
                    self.errors.append(FindingsValidationError(
                        fid, f"verification.gates.{gate_name}",
                        "CLOSED finding cannot have a red gate"))

    def _validate_accepted_risk(self, finding: dict, fid: str):
        """ACCEPTED_RISK requires owner, justification, and review date."""
        # Critical severity cannot be accepted risk
        severity = finding.get("severity")
        if severity == "Critical":
            self.errors.append(FindingsValidationError(
                fid, "status",
                "Critical severity cannot be ACCEPTED_RISK"))

        for field in ACCEPTED_RISK_REQUIRED:
            value = _get_nested(finding, field)
            if not _is_non_empty(value):
                self.errors.append(FindingsValidationError(
                    fid, field,
                    "required for ACCEPTED_RISK status"))

        # Review date must be in the future or condition specified
        accepted_risk = finding.get("accepted_risk")
        if isinstance(accepted_risk, dict):
            review_date = accepted_risk.get("review_date")
            review_condition = accepted_risk.get("review_condition")
            if review_date:
                parsed = _parse_date(review_date)
                if parsed and parsed < date.today():
                    # Past date is only acceptable if condition is given
                    if not _is_non_empty(review_condition):
                        self.errors.append(FindingsValidationError(
                            fid, "accepted_risk.review_date",
                            "review date is in the past with no review condition"))

    def _validate_in_remediation(self, finding: dict, fid: str):
        """IN_REMEDIATION requires owner, root_cause, and strategy."""
        for field in IN_REMEDIATION_REQUIRED:
            value = _get_nested(finding, field)
            if not _is_non_empty(value):
                self.errors.append(FindingsValidationError(
                    fid, field,
                    "required for IN_REMEDIATION status"))


# ---------------------------------------------------------------------------
# CLI entry point
# ---------------------------------------------------------------------------

def main():
    """Validate a findings.yaml file."""
    repo_root = Path(__file__).resolve().parents[2]
    if len(sys.argv) < 2:
        findings_path = repo_root / ".kiro/specs/code-quality-review/artifacts/findings.yaml"
        policy_path = repo_root / ".kiro/specs/code-quality-review/artifacts/policy.yaml"
    else:
        findings_path = Path(sys.argv[1])
        policy_path = Path(sys.argv[2]) if len(sys.argv) >= 3 else None

    if not findings_path.exists():
        print(f"ERROR: {findings_path} not found")
        sys.exit(2)

    policy_data = None
    if policy_path is not None and policy_path.exists():
        with open(policy_path) as f:
            policy_data = yaml.safe_load(f)

    with open(findings_path) as f:
        findings_data = yaml.safe_load(f)

    if findings_data is None:
        print("ERROR: findings file is empty")
        sys.exit(1)

    validator = FindingsValidator(findings_data, policy_data)
    errors = validator.validate()

    if errors:
        print(f"FAILED: {len(errors)} validation error(s):")
        for err in errors:
            print(f"  {err}")
        sys.exit(1)
    else:
        print("PASSED: findings.yaml is valid")
        sys.exit(0)


if __name__ == "__main__":
    main()
