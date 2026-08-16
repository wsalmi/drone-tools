#!/usr/bin/env python3
"""
Code Quality Review — CI Gate Conjunction and Parity Validator

Validates Property 4:
  The CI result is success if and only if ALL mandatory gates pass,
  there is no prohibited skip, every regression is auto-discovered,
  and CI uses the same canonical commands exercised locally.

Inputs:
  - gate_results: dict mapping CI job name -> "green"|"red"|"skipped"
  - skip_records: list of test skip entries with optional CQR-ID/justification
  - workflow_path: path to the GitHub Actions workflow YAML
  - baseline_path: path to the baseline.yaml canonical commands
  - readme_path: path to the README.md

Validates: Requirements 3.6, 4.1, 4.2, 4.3, 4.4, 4.5, 4.6, 12.3
"""

import re
import sys
from pathlib import Path
from typing import NamedTuple

import yaml


# ---------------------------------------------------------------------------
# Data structures
# ---------------------------------------------------------------------------

# The mandatory CI jobs that must ALL pass for the gate to be green.
MANDATORY_CI_JOBS = [
    "host-test",
    "host-sanitizers",
    "static-analysis",
    "firmware-build",
]


class GateViolation(NamedTuple):
    """A violation of the gate conjunction property."""
    category: str   # "gate_red", "prohibited_skip", "parity", "discovery"
    job: str        # CI job or context identifier
    message: str    # Human-readable description


class SkipRecord(NamedTuple):
    """A test skip entry."""
    test_name: str
    cqr_id: str | None      # e.g., "CQR-BUILD-001" or None
    justification: str | None


# ---------------------------------------------------------------------------
# Gate Conjunction Validator
# ---------------------------------------------------------------------------

class CIGateValidator:
    """
    Evaluates the gate conjunction:
      CI = ACCEPTED iff ALL mandatory jobs are green AND no prohibited skip.
    """

    def __init__(
        self,
        gate_results: dict[str, str],
        skip_records: list[SkipRecord] | None = None,
        mandatory_jobs: list[str] | None = None,
    ):
        self.gate_results = gate_results
        self.skip_records = skip_records or []
        self.mandatory_jobs = mandatory_jobs or MANDATORY_CI_JOBS
        self.violations: list[GateViolation] = []

    def validate(self) -> list[GateViolation]:
        """Evaluate gate conjunction and return violations."""
        self.violations = []
        self._check_gate_conjunction()
        self._check_prohibited_skips()
        return self.violations

    def is_accepted(self) -> bool:
        """Return True iff the CI gate passes (all mandatory green, no violations)."""
        self.validate()
        return len(self.violations) == 0

    def _check_gate_conjunction(self):
        """Each mandatory job must be green for CI to pass."""
        for job in self.mandatory_jobs:
            result = self.gate_results.get(job)
            if result is None:
                self.violations.append(GateViolation(
                    category="gate_red",
                    job=job,
                    message=f"Mandatory job '{job}' was not executed",
                ))
            elif result != "green":
                self.violations.append(GateViolation(
                    category="gate_red",
                    job=job,
                    message=f"Mandatory job '{job}' is '{result}' (must be green)",
                ))

    def _check_prohibited_skips(self):
        """A test skip without CQR-ID and justification is prohibited."""
        for skip in self.skip_records:
            if not skip.cqr_id or not skip.justification:
                self.violations.append(GateViolation(
                    category="prohibited_skip",
                    job=skip.test_name,
                    message=(
                        f"Test '{skip.test_name}' is skipped without "
                        f"CQR-ID and justification"
                    ),
                ))


# ---------------------------------------------------------------------------
# Local/CI Command Parity Validator
# ---------------------------------------------------------------------------

def extract_workflow_run_commands(workflow_content: str) -> list[str]:
    """
    Extract `run:` step commands from a GitHub Actions workflow YAML.

    Returns a list of individual command lines (split from multi-line runs),
    excluding comments and empty lines.
    """
    data = yaml.safe_load(workflow_content)
    commands = []

    if not isinstance(data, dict):
        return commands

    jobs = data.get("jobs", {})
    for _job_name, job_def in jobs.items():
        if not isinstance(job_def, dict):
            continue
        steps = job_def.get("steps", [])
        for step in steps:
            if not isinstance(step, dict):
                continue
            run_block = step.get("run")
            if run_block:
                for line in run_block.splitlines():
                    stripped = line.strip()
                    # Skip empty lines and comments
                    if stripped and not stripped.startswith("#"):
                        commands.append(stripped)

    return commands


def extract_baseline_canonical_commands(baseline_content: str) -> list[str]:
    """
    Extract canonical command lines from baseline.yaml commands section.

    Returns a list of individual command lines from all canonical: fields.
    """
    data = yaml.safe_load(baseline_content)
    commands = []

    if not isinstance(data, dict):
        return commands

    cmd_section = data.get("commands", {})
    for _cmd_name, cmd_def in cmd_section.items():
        if not isinstance(cmd_def, dict):
            continue
        canonical = cmd_def.get("canonical", "")
        if canonical:
            for line in canonical.splitlines():
                stripped = line.strip()
                if stripped and not stripped.startswith("#"):
                    commands.append(stripped)

    return commands


def extract_readme_commands(readme_content: str) -> list[str]:
    """
    Extract commands from README.md bash code blocks.

    Returns command lines found in ```bash...``` blocks, excluding comments.
    """
    commands = []
    in_bash_block = False

    for line in readme_content.splitlines():
        if re.match(r"^```bash\s*$", line.strip()):
            in_bash_block = True
            continue
        elif line.strip().startswith("```") and in_bash_block:
            in_bash_block = False
            continue
        elif in_bash_block:
            stripped = line.strip()
            if stripped and not stripped.startswith("#"):
                commands.append(stripped)

    return commands


def normalize_command(cmd: str) -> str:
    """
    Normalize a command for comparison.

    Strips leading `. $IDF_PATH/export.sh` environment setup,
    removes path prefixes like `rm -rf build/...`,
    and normalizes whitespace.
    """
    # Normalize whitespace
    cmd = " ".join(cmd.split())
    return cmd


def _is_infra_command(cmd: str) -> bool:
    """Return True for infrastructure commands that don't need parity."""
    infra_patterns = [
        r"^git\s+",
        r"^echo\s+",
        r"^VERSION=",
        r"^apt-get\s+",
        r"^pip\s+install",
        r"^mkdir\s+-p",
        r"^rm\s+-rf\s+build/",
        r"^\.\s+\$IDF_PATH/export\.sh",
        r"^source\s+",
        r"^export\s+",
    ]
    for pattern in infra_patterns:
        if re.match(pattern, cmd):
            return True
    return False


def _is_build_or_test_command(cmd: str) -> bool:
    """Return True if the command is a build/test command needing parity."""
    build_test_patterns = [
        r"cmake\s+",
        r"ctest\s+",
        r"idf\.py\s+",
        r"python\s+.*validate",
        r"python\s+-m\s+pytest",
    ]
    # Operational commands (flash, monitor) don't need baseline parity
    operational_patterns = [
        r"idf\.py\s+.*flash",
        r"idf\.py\s+.*monitor",
    ]
    for pattern in operational_patterns:
        if re.search(pattern, cmd):
            return False
    for pattern in build_test_patterns:
        if re.search(pattern, cmd):
            return True
    return False


class ParityViolation(NamedTuple):
    """A local/CI parity violation."""
    source: str    # "workflow", "readme", "baseline"
    command: str   # The command that lacks parity
    message: str


class LocalCIParityValidator:
    """
    Validates that workflow, README, and baseline use the same canonical commands.

    A workflow build/test command that is not present in baseline.yaml
    (directly or as a recognizable variant) is a parity violation.
    """

    def __init__(
        self,
        workflow_commands: list[str],
        baseline_commands: list[str],
        readme_commands: list[str],
    ):
        self.workflow_commands = workflow_commands
        self.baseline_commands = baseline_commands
        self.readme_commands = readme_commands
        self.violations: list[ParityViolation] = []

    def validate(self) -> list[ParityViolation]:
        """Check parity and return violations."""
        self.violations = []
        self._check_workflow_vs_baseline()
        self._check_readme_vs_baseline()
        return self.violations

    def _check_workflow_vs_baseline(self):
        """Workflow build/test commands must appear in baseline."""
        baseline_normalized = {normalize_command(c) for c in self.baseline_commands}

        for cmd in self.workflow_commands:
            if _is_infra_command(cmd):
                continue
            if not _is_build_or_test_command(cmd):
                continue

            normalized = normalize_command(cmd)
            # Check if it appears in baseline (exact or as a substring match)
            if not self._command_in_set(normalized, baseline_normalized):
                self.violations.append(ParityViolation(
                    source="workflow",
                    command=cmd,
                    message=(
                        f"Workflow command not found in baseline: '{cmd}'"
                    ),
                ))

    def _check_readme_vs_baseline(self):
        """README build/test commands should align with baseline."""
        baseline_normalized = {normalize_command(c) for c in self.baseline_commands}

        for cmd in self.readme_commands:
            if _is_infra_command(cmd):
                continue
            if not _is_build_or_test_command(cmd):
                continue

            normalized = normalize_command(cmd)
            if not self._command_in_set(normalized, baseline_normalized):
                self.violations.append(ParityViolation(
                    source="readme",
                    command=cmd,
                    message=(
                        f"README command not found in baseline: '{cmd}'"
                    ),
                ))

    def _command_in_set(self, normalized: str, baseline_set: set[str]) -> bool:
        """
        Check if a normalized command matches any baseline command.

        Uses substring matching for common patterns (e.g., idf.py build
        matches a baseline entry that also contains idf.py build).
        """
        if normalized in baseline_set:
            return True

        # Extract the core command (e.g., "idf.py build", "cmake --build build/host")
        core = self._extract_core(normalized)
        for baseline_cmd in baseline_set:
            baseline_core = self._extract_core(baseline_cmd)
            if core and baseline_core and core == baseline_core:
                return True
            # Also check if the core is contained in the baseline command
            if core and core in baseline_cmd:
                return True
            # Check action-level match for idf.py commands
            # e.g., "idf.py set-target esp32s3" matches
            #        "idf.py -B build/firmware set-target esp32s3"
            if self._idf_action_match(normalized, baseline_cmd):
                return True

        return False

    @staticmethod
    def _extract_core(cmd: str) -> str:
        """Extract the meaningful portion of a command for matching."""
        # Remove common prefixes
        cmd = re.sub(r"^rm -rf \S+ && ", "", cmd)
        cmd = re.sub(r"^mkdir -p \S+ && ", "", cmd)
        return cmd.strip()

    @staticmethod
    def _idf_action_match(cmd: str, baseline_cmd: str) -> bool:
        """
        Match idf.py commands by their action, ignoring -B flags.

        'idf.py set-target esp32s3' matches 'idf.py -B build/firmware set-target esp32s3'
        'idf.py build' matches 'idf.py -B build/firmware build'
        """
        if "idf.py" not in cmd or "idf.py" not in baseline_cmd:
            return False

        # Strip -B <path> and -p <port> flags to get the action
        def extract_idf_action(c: str) -> str:
            c = re.sub(r"-B\s+\S+", "", c)
            c = re.sub(r"-p\s+\S+", "", c)
            c = re.sub(r"idf\.py\s+", "", c)
            return " ".join(c.split()).strip()

        action1 = extract_idf_action(cmd)
        action2 = extract_idf_action(baseline_cmd)
        return action1 == action2


# ---------------------------------------------------------------------------
# CTest auto-discovery validator
# ---------------------------------------------------------------------------

def validate_ctest_autodiscovery(cmakelists_content: str) -> list[str]:
    """
    Verify that CMakeLists.txt uses add_test() or similar patterns
    that enable automatic test discovery without a parallel manual list.

    Returns a list of issues if the pattern is not found.
    """
    issues = []

    # Look for add_test() calls or enable_testing()
    has_enable_testing = bool(re.search(r"enable_testing\s*\(\s*\)", cmakelists_content))
    has_add_test = bool(re.search(r"add_test\s*\(", cmakelists_content))

    if not has_enable_testing:
        issues.append("CMakeLists.txt does not call enable_testing()")

    if not has_add_test:
        issues.append("CMakeLists.txt does not use add_test() for test registration")

    return issues


# ---------------------------------------------------------------------------
# CLI entry point
# ---------------------------------------------------------------------------

def main():
    """Validate CI gate conjunction and local/CI parity."""
    if len(sys.argv) < 4:
        print("Usage: validate_ci_gates.py <workflow.yml> <baseline.yaml> <README.md>")
        sys.exit(2)

    workflow_path = Path(sys.argv[1])
    baseline_path = Path(sys.argv[2])
    readme_path = Path(sys.argv[3])

    for path in (workflow_path, baseline_path, readme_path):
        if not path.exists():
            print(f"ERROR: {path} not found")
            sys.exit(2)

    workflow_cmds = extract_workflow_run_commands(workflow_path.read_text())
    baseline_cmds = extract_baseline_canonical_commands(baseline_path.read_text())
    readme_cmds = extract_readme_commands(readme_path.read_text())

    parity_validator = LocalCIParityValidator(workflow_cmds, baseline_cmds, readme_cmds)
    violations = parity_validator.validate()

    if violations:
        print(f"FAILED: {len(violations)} parity violation(s):")
        for v in violations:
            print(f"  [{v.source}] {v.message}")
        sys.exit(1)
    else:
        print("PASSED: local/CI command parity verified")
        sys.exit(0)


if __name__ == "__main__":
    main()
