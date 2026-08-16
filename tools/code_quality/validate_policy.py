#!/usr/bin/env python3
"""
Code Quality Review — Policy File Validator

Validates that policy.yaml defines all required sections:
  - Severity levels with implications
  - Valid status transitions
  - Gate definitions
  - Suppression/skip rules
  - Acceptance criteria with fail-closed rule
  - Finding ID format and pattern

Validates: Requirements 2.1, 2.4, 10.5, 12.9
"""

import re
import sys
from pathlib import Path
from typing import Any

import yaml


class PolicyValidationError:
    """A single policy validation error."""

    def __init__(self, section: str, message: str):
        self.section = section
        self.message = message

    def __str__(self) -> str:
        return f"[{self.section}] {self.message}"


class PolicyValidator:
    """Validates the structure and completeness of policy.yaml."""

    REQUIRED_SECTIONS = [
        "schema_version",
        "severity",
        "status_transitions",
        "gates",
        "suppressions",
        "skips",
        "acceptance",
        "finding_id",
    ]

    REQUIRED_SEVERITIES = {"Critical", "High", "Medium", "Low"}

    REQUIRED_STATUSES = {"OPEN", "IN_REMEDIATION", "VERIFYING", "CLOSED", "ACCEPTED_RISK"}

    def __init__(self, policy_data: dict):
        self.data = policy_data
        self.errors: list[PolicyValidationError] = []

    def validate(self) -> list[PolicyValidationError]:
        """Run all policy validations."""
        self.errors = []

        # Top-level sections
        for section in self.REQUIRED_SECTIONS:
            if section not in self.data:
                self.errors.append(PolicyValidationError(
                    section, "missing required section"))

        if self.errors:
            return self.errors  # Can't validate further without sections

        self._validate_severity()
        self._validate_transitions()
        self._validate_gates()
        self._validate_suppressions()
        self._validate_acceptance()
        self._validate_finding_id()

        return self.errors

    def _validate_severity(self):
        severity = self.data.get("severity", {})
        levels = severity.get("levels", [])
        if not isinstance(levels, list) or len(levels) == 0:
            self.errors.append(PolicyValidationError(
                "severity", "must define at least one level"))
            return

        defined_names = {lvl.get("name") for lvl in levels}
        missing = self.REQUIRED_SEVERITIES - defined_names
        if missing:
            self.errors.append(PolicyValidationError(
                "severity", f"missing required levels: {sorted(missing)}"))

        # Each level must have implications
        for lvl in levels:
            name = lvl.get("name", "<unknown>")
            if "implications" not in lvl:
                self.errors.append(PolicyValidationError(
                    "severity", f"level '{name}' missing implications"))

        # Rationale must be required
        if not severity.get("rationale_required"):
            self.errors.append(PolicyValidationError(
                "severity", "rationale_required must be true"))

    def _validate_transitions(self):
        transitions = self.data.get("status_transitions", {})
        statuses = set(transitions.get("valid_statuses", []))
        missing = self.REQUIRED_STATUSES - statuses
        if missing:
            self.errors.append(PolicyValidationError(
                "status_transitions", f"missing statuses: {sorted(missing)}"))

        allowed = transitions.get("allowed_transitions", {})
        if not isinstance(allowed, dict) or len(allowed) == 0:
            self.errors.append(PolicyValidationError(
                "status_transitions", "allowed_transitions must be non-empty"))

        # CLOSED and ACCEPTED_RISK must have requirements
        requirements = transitions.get("status_requirements", {})
        for status in ("CLOSED", "ACCEPTED_RISK"):
            if status not in requirements:
                self.errors.append(PolicyValidationError(
                    "status_transitions",
                    f"missing status_requirements for {status}"))

    def _validate_gates(self):
        gates = self.data.get("gates", {})
        definitions = gates.get("definitions", {})
        if not isinstance(definitions, dict) or len(definitions) == 0:
            self.errors.append(PolicyValidationError(
                "gates", "must define at least one gate"))
            return

        # Must have at least one mandatory gate
        mandatory = [name for name, defn in definitions.items()
                     if defn.get("mandatory")]
        if not mandatory:
            self.errors.append(PolicyValidationError(
                "gates", "must have at least one mandatory gate"))

    def _validate_suppressions(self):
        suppressions = self.data.get("suppressions", {})
        rules = suppressions.get("rules", [])
        if not isinstance(rules, list) or len(rules) == 0:
            self.errors.append(PolicyValidationError(
                "suppressions", "must define at least one rule"))

        required = suppressions.get("required_fields", [])
        if "finding_id" not in required:
            self.errors.append(PolicyValidationError(
                "suppressions", "required_fields must include finding_id"))

    def _validate_acceptance(self):
        acceptance = self.data.get("acceptance", {})
        if not acceptance.get("fail_closed"):
            self.errors.append(PolicyValidationError(
                "acceptance", "fail_closed must be true"))

        criteria = acceptance.get("criteria", [])
        if not isinstance(criteria, list) or len(criteria) == 0:
            self.errors.append(PolicyValidationError(
                "acceptance", "must define at least one criterion"))

    def _validate_finding_id(self):
        finding_id = self.data.get("finding_id", {})
        pattern = finding_id.get("pattern")
        if not pattern:
            self.errors.append(PolicyValidationError(
                "finding_id", "must define a regex pattern"))
            return

        # Pattern must be compilable
        try:
            re.compile(pattern)
        except re.error as e:
            self.errors.append(PolicyValidationError(
                "finding_id", f"invalid regex pattern: {e}"))


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def main():
    if len(sys.argv) < 2:
        print("Usage: validate_policy.py <policy.yaml>")
        sys.exit(2)

    path = Path(sys.argv[1])
    if not path.exists():
        print(f"ERROR: {path} not found")
        sys.exit(2)

    with open(path) as f:
        data = yaml.safe_load(f)

    if data is None:
        print("ERROR: policy file is empty")
        sys.exit(1)

    validator = PolicyValidator(data)
    errors = validator.validate()

    if errors:
        print(f"FAILED: {len(errors)} error(s):")
        for err in errors:
            print(f"  {err}")
        sys.exit(1)
    else:
        print("PASSED: policy.yaml is valid")
        sys.exit(0)


if __name__ == "__main__":
    main()
