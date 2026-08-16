#!/usr/bin/env python3
"""
Code Quality Review — Acceptance Decision Validator (Property 22)

Evaluates the fail-closed acceptance decision:
  ACCEPTED if and only if:
    - All mandatory gates are green
    - Zero open Critical/High findings
    - Every CLOSED finding has complete evidence/regression
    - Final matrix covers each finding exactly once
    - ACCEPTED_RISK findings meet policy

Otherwise: NOT_ACCEPTED with objective blockers.

Validates: Requirements 2.6, 12.1, 12.2, 12.3, 12.7, 12.8, 12.9, 12.10
Property: P22 (Closure and acceptance are fail-closed)
"""

import sys
from pathlib import Path
from typing import Any

import yaml

try:
    from .validate_findings import (
        VALID_SEVERITIES,
        FindingsValidator,
        _get_nested,
        _is_non_empty,
    )
except ImportError:
    from code_quality.validate_findings import (
        VALID_SEVERITIES,
        FindingsValidator,
        _get_nested,
        _is_non_empty,
    )


# ---------------------------------------------------------------------------
# Acceptance Decision
# ---------------------------------------------------------------------------

class AcceptanceBlocker:
    """Represents a single reason the review cannot be accepted."""

    def __init__(self, criterion_id: str, description: str, details: str = ""):
        self.criterion_id = criterion_id
        self.description = description
        self.details = details

    def __str__(self) -> str:
        base = f"[{self.criterion_id}] {self.description}"
        if self.details:
            base += f" — {self.details}"
        return base

    def __repr__(self) -> str:
        return self.__str__()


class AcceptanceDecision:
    """Result of the acceptance evaluation."""

    def __init__(self, accepted: bool, blockers: list[AcceptanceBlocker]):
        self.accepted = accepted
        self.blockers = blockers

    @property
    def status(self) -> str:
        return "ACCEPTED" if self.accepted else "NOT_ACCEPTED"


class AcceptanceValidator:
    """
    Evaluates whether the review meets acceptance criteria.

    Inputs:
      - findings_data: parsed findings.yaml
      - gate_results: dict mapping gate name -> "green"|"red"|"skipped"|"pending"
      - matrix_ids: list of finding IDs present in the traceability matrix
                    (None if matrix not yet generated)
    """

    def __init__(
        self,
        findings_data: dict,
        gate_results: dict[str, str],
        matrix_ids: list[str] | None = None,
        policy_data: dict | None = None,
    ):
        self.findings_data = findings_data
        self.gate_results = gate_results
        self.matrix_ids = matrix_ids
        self.policy = policy_data
        self.blockers: list[AcceptanceBlocker] = []

    def evaluate(self) -> AcceptanceDecision:
        """Evaluate the fail-closed acceptance decision."""
        self.blockers = []
        findings = self.findings_data.get("findings", [])

        self._check_mandatory_gates()
        self._check_open_critical_high(findings)
        self._check_closed_completeness(findings)
        self._check_accepted_risk_policy(findings)
        self._check_matrix_coverage(findings)

        accepted = len(self.blockers) == 0
        return AcceptanceDecision(accepted=accepted, blockers=self.blockers)

    def _check_mandatory_gates(self):
        """ACC-1: All mandatory gates must be green."""
        mandatory_gates = self._get_mandatory_gates()
        for gate_name in mandatory_gates:
            result = self.gate_results.get(gate_name)
            if result is None:
                self.blockers.append(AcceptanceBlocker(
                    "ACC-1", "Mandatory gate not executed",
                    f"{gate_name} has no result"))
            elif result != "green":
                self.blockers.append(AcceptanceBlocker(
                    "ACC-1", "Mandatory gate not green",
                    f"{gate_name} is '{result}'"))

    def _check_open_critical_high(self, findings: list):
        """ACC-2/ACC-3: Zero open Critical/High."""
        open_statuses = {"OPEN", "IN_REMEDIATION", "VERIFYING"}
        for finding in findings:
            severity = finding.get("severity")
            status = finding.get("status")
            fid = finding.get("id", "<unknown>")
            if severity in ("Critical", "High") and status in open_statuses:
                criterion = "ACC-2" if severity == "Critical" else "ACC-3"
                self.blockers.append(AcceptanceBlocker(
                    criterion,
                    f"Open {severity} finding",
                    f"{fid} is {status}"))

    def _check_closed_completeness(self, findings: list):
        """ACC-4: Every CLOSED finding has complete evidence."""
        for finding in findings:
            if finding.get("status") != "CLOSED":
                continue
            fid = finding.get("id", "<unknown>")

            # Check required CLOSED fields
            required = [
                "root_cause",
                "remediation.strategy",
                "remediation.change_refs",
                "regression.test_ids",
                "regression.remediated_result",
            ]
            for field in required:
                if not _is_non_empty(_get_nested(finding, field)):
                    self.blockers.append(AcceptanceBlocker(
                        "ACC-4",
                        "CLOSED finding missing required field",
                        f"{fid}: {field}"))
                    break  # One blocker per finding is enough

            # Check gates
            gates = _get_nested(finding, "verification.gates")
            if not isinstance(gates, dict) or len(gates) == 0:
                self.blockers.append(AcceptanceBlocker(
                    "ACC-4",
                    "CLOSED finding has no gate results",
                    fid))
            elif any(v == "red" for v in gates.values()):
                self.blockers.append(AcceptanceBlocker(
                    "ACC-4",
                    "CLOSED finding has red gate",
                    fid))

    def _check_accepted_risk_policy(self, findings: list):
        """ACC-6: ACCEPTED_RISK findings meet policy."""
        for finding in findings:
            if finding.get("status") != "ACCEPTED_RISK":
                continue
            fid = finding.get("id", "<unknown>")

            # Critical cannot be accepted risk
            if finding.get("severity") == "Critical":
                self.blockers.append(AcceptanceBlocker(
                    "ACC-6",
                    "Critical finding cannot be ACCEPTED_RISK",
                    fid))

            # Required fields
            accepted_risk = finding.get("accepted_risk")
            if not isinstance(accepted_risk, dict):
                self.blockers.append(AcceptanceBlocker(
                    "ACC-6",
                    "ACCEPTED_RISK missing accepted_risk section",
                    fid))
                continue

            for field in ("owner", "justification", "review_date"):
                if not _is_non_empty(accepted_risk.get(field)):
                    self.blockers.append(AcceptanceBlocker(
                        "ACC-6",
                        f"ACCEPTED_RISK missing {field}",
                        fid))

    def _check_matrix_coverage(self, findings: list):
        """ACC-5: Matrix covers each finding exactly once."""
        if self.matrix_ids is None:
            # Matrix not provided — cannot validate coverage
            self.blockers.append(AcceptanceBlocker(
                "ACC-5",
                "Traceability matrix not provided",
                "Cannot verify finding coverage"))
            return

        finding_ids = {f.get("id") for f in findings if f.get("id")}
        matrix_set = set(self.matrix_ids)

        # Missing from matrix
        missing = finding_ids - matrix_set
        for fid in sorted(missing):
            self.blockers.append(AcceptanceBlocker(
                "ACC-5",
                "Finding missing from traceability matrix",
                fid))

        # Duplicates in matrix
        seen = set()
        for fid in self.matrix_ids:
            if fid in seen:
                self.blockers.append(AcceptanceBlocker(
                    "ACC-5",
                    "Duplicate finding in traceability matrix",
                    fid))
            seen.add(fid)

        # In matrix but not in findings
        extra = matrix_set - finding_ids
        for fid in sorted(extra):
            self.blockers.append(AcceptanceBlocker(
                "ACC-5",
                "Matrix references non-existent finding",
                fid))

    def _get_mandatory_gates(self) -> list[str]:
        """Get list of mandatory gate names from policy or defaults."""
        if self.policy:
            gates = self.policy.get("gates", {}).get("definitions", {})
            return [name for name, defn in gates.items()
                    if defn.get("mandatory", False)]
        # Default mandatory gates
        return ["G0", "G1", "G2", "G3", "G4", "G6"]


# ---------------------------------------------------------------------------
# CLI entry point
# ---------------------------------------------------------------------------

def main():
    """Evaluate acceptance decision from findings.yaml and gate results."""
    if len(sys.argv) < 3:
        print("Usage: validate_acceptance.py <findings.yaml> <gates.yaml> [matrix.yaml] [policy.yaml]")
        sys.exit(2)

    findings_path = Path(sys.argv[1])
    gates_path = Path(sys.argv[2])

    if not findings_path.exists():
        print(f"ERROR: {findings_path} not found")
        sys.exit(2)
    if not gates_path.exists():
        print(f"ERROR: {gates_path} not found")
        sys.exit(2)

    with open(findings_path) as f:
        findings_data = yaml.safe_load(f)
    with open(gates_path) as f:
        gate_results = yaml.safe_load(f)

    matrix_ids = None
    if len(sys.argv) >= 4:
        matrix_path = Path(sys.argv[3])
        if matrix_path.exists():
            with open(matrix_path) as f:
                matrix_data = yaml.safe_load(f)
            if isinstance(matrix_data, dict):
                matrix_ids = matrix_data.get("finding_ids", [])
            elif isinstance(matrix_data, list):
                matrix_ids = matrix_data

    policy_data = None
    if len(sys.argv) >= 5:
        policy_path = Path(sys.argv[4])
        if policy_path.exists():
            with open(policy_path) as f:
                policy_data = yaml.safe_load(f)

    validator = AcceptanceValidator(
        findings_data=findings_data,
        gate_results=gate_results,
        matrix_ids=matrix_ids,
        policy_data=policy_data,
    )
    decision = validator.evaluate()

    print(f"Decision: {decision.status}")
    if decision.blockers:
        print(f"\nBlockers ({len(decision.blockers)}):")
        for blocker in decision.blockers:
            print(f"  {blocker}")
        sys.exit(1)
    else:
        print("All acceptance criteria satisfied.")
        sys.exit(0)


if __name__ == "__main__":
    main()
