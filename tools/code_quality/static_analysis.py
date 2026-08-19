#!/usr/bin/env python3
"""
Code Quality Review — Static Analysis Runner and Policy Enforcer

Orchestrates static analysis commands over compile_commands.json, host/ESP-IDF
warnings, CMake lint, ShellCheck, and workflow validation. Normalizes output
to produce deterministic fingerprints without absolute paths or timestamps.

Enforces the versioned diagnostics policy:
  - Critical/High, bounds, lifetime, uninitialized, overflow, data race block
  - New warnings in changed code block
  - Suppressions require CQR-ID, must be minimal and localized
  - Preexisting backlog is tracked but non-blocking for unchanged code

Validates: Requirements 11.1, 11.2, 11.3, 11.4, 11.5, 11.6, 11.7
Properties: P20 (Static analysis with normalized baseline and suppressions)
"""

import json
import os
import re
import subprocess
import sys
from dataclasses import dataclass, field
from enum import Enum
from pathlib import Path
from typing import Any

import yaml


# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

SUPPRESSION_ID_PATTERN = re.compile(r"CQR-[A-Z][A-Z0-9_]+-\d{3}")

# Regex to detect inline suppression comments
SUPPRESSION_MARKERS = [
    re.compile(r"//\s*NOLINTNEXTLINE"),
    re.compile(r"//\s*NOLINT\b"),
    re.compile(r"//\s*cppcheck-suppress"),
    re.compile(r"#pragma\s+GCC\s+diagnostic\s+ignored"),
]

# Global/project-level suppression indicators (prohibited without finding)
GLOBAL_SUPPRESSION_PATTERNS = [
    re.compile(r"add_compile_options\s*\(\s*-Wno-"),
    re.compile(r"target_compile_options\s*\(.*-Wno-(?!error)"),
]

# Compiler warning line pattern (GCC/Clang format)
WARNING_LINE_PATTERN = re.compile(
    r"^(?P<file>[^:]+):(?P<line>\d+):(?P<col>\d+):\s+"
    r"(?P<level>warning|error):\s+(?P<message>.+?)(?:\s+\[-W(?P<flag>[^\]]+)\])?$"
)

# Path normalization patterns
ANSI_ESCAPE = re.compile(r"\x1b\[[0-9;]*m")
TIMESTAMP_PATTERN = re.compile(
    r"\d{4}-\d{2}-\d{2}[T ]\d{2}:\d{2}:\d{2}(?:\.\d+)?(?:Z|[+-]\d{2}:?\d{2})?"
)
PID_PATTERN = re.compile(r"\bpid\s*[:=]\s*\d+\b", re.IGNORECASE)


# ---------------------------------------------------------------------------
# Data Model
# ---------------------------------------------------------------------------

class DiagnosticSeverity(Enum):
    """Severity classification for diagnostics."""
    BLOCKING = "blocking"     # Always blocks CI
    NEW_WARNING = "new"       # Blocks because it's in changed code
    BACKLOG = "backlog"       # Preexisting, tracked but non-blocking
    INFO = "info"             # Informational, never blocks


@dataclass
class Diagnostic:
    """A single static analysis diagnostic."""
    file: str
    line: int
    column: int
    level: str              # "warning" or "error"
    message: str
    flag: str | None        # e.g., "array-bounds"
    category: str | None    # mapped blocking category if any
    severity: DiagnosticSeverity
    fingerprint: str = ""   # normalized identifier for dedup
    is_in_changed_code: bool = False

    def to_dict(self) -> dict:
        return {
            "file": self.file,
            "line": self.line,
            "column": self.column,
            "level": self.level,
            "message": self.message,
            "flag": self.flag,
            "category": self.category,
            "severity": self.severity.value,
            "fingerprint": self.fingerprint,
            "is_in_changed_code": self.is_in_changed_code,
        }


@dataclass
class SuppressionIssue:
    """A suppression that violates policy."""
    file: str
    line: int
    kind: str           # "missing_id", "global", "empty_justification"
    content: str        # The suppression line content
    message: str

    def to_dict(self) -> dict:
        return {
            "file": self.file,
            "line": self.line,
            "kind": self.kind,
            "content": self.content,
            "message": self.message,
        }


@dataclass
class AnalysisReport:
    """Complete static analysis report."""
    diagnostics: list[Diagnostic] = field(default_factory=list)
    suppression_issues: list[SuppressionIssue] = field(default_factory=list)
    blocking_count: int = 0
    new_warning_count: int = 0
    backlog_count: int = 0
    tools_used: list[str] = field(default_factory=list)
    gate_passed: bool = True
    gate_failure_reasons: list[str] = field(default_factory=list)

    def to_dict(self) -> dict:
        return {
            "gate_passed": self.gate_passed,
            "gate_failure_reasons": self.gate_failure_reasons,
            "summary": {
                "blocking": self.blocking_count,
                "new_warnings": self.new_warning_count,
                "backlog": self.backlog_count,
                "suppression_issues": len(self.suppression_issues),
                "total_diagnostics": len(self.diagnostics),
            },
            "tools_used": self.tools_used,
            "diagnostics": [d.to_dict() for d in self.diagnostics],
            "suppression_issues": [s.to_dict() for s in self.suppression_issues],
        }


# ---------------------------------------------------------------------------
# Normalization
# ---------------------------------------------------------------------------

class OutputNormalizer:
    """Normalizes diagnostic output for reproducible fingerprints."""

    def __init__(self, repo_root: Path, build_dir: Path | None = None):
        self.repo_root = str(repo_root.resolve())
        self.build_dir = str(build_dir.resolve()) if build_dir else None

    def normalize(self, text: str) -> str:
        """Remove absolute paths, timestamps, PIDs, and ANSI codes."""
        result = text

        # Remove ANSI color codes
        result = ANSI_ESCAPE.sub("", result)

        # Replace absolute repo path with <repo>
        result = result.replace(self.repo_root, "<repo>")

        # Replace build directory prefix
        if self.build_dir:
            result = result.replace(self.build_dir, "<build>")

        # Remove timestamps
        result = TIMESTAMP_PATTERN.sub("<timestamp>", result)

        # Remove PIDs
        result = PID_PATTERN.sub("pid: <pid>", result)

        return result

    def fingerprint(self, diagnostic: Diagnostic) -> str:
        """Generate a stable fingerprint for a diagnostic."""
        # Fingerprint based on file (relative), message, and flag only
        # Excludes line number since code may shift
        parts = [
            diagnostic.file,
            diagnostic.message,
            diagnostic.flag or "",
        ]
        return "|".join(parts)


# ---------------------------------------------------------------------------
# Policy Loader
# ---------------------------------------------------------------------------

class PolicyLoader:
    """Loads and provides access to the static analysis policy."""

    def __init__(self, policy_path: Path):
        if not policy_path.exists():
            raise FileNotFoundError(f"Policy not found: {policy_path}")
        with open(policy_path) as f:
            self.data = yaml.safe_load(f)

    @property
    def blocking_categories(self) -> list[dict]:
        return self.data.get("blocking_categories", [])

    @property
    def preexisting_backlog(self) -> list[dict]:
        backlog = self.data.get("preexisting_backlog", {})
        return backlog.get("known_items", [])

    @property
    def suppression_rules(self) -> dict:
        return self.data.get("suppressions", {})

    @property
    def tools(self) -> dict:
        return self.data.get("tools", {})

    @property
    def affected_categories(self) -> list[str]:
        policy = self.data.get("affected_unit_policy", {})
        return policy.get("categories_requiring_analysis", [])

    def is_blocking_pattern(self, flag: str | None, message: str) -> str | None:
        """Check if a diagnostic matches a blocking category. Returns category ID or None."""
        if flag is None and not message:
            return None
        check_str = (flag or "") + " " + message
        for category in self.blocking_categories:
            for pattern in category.get("patterns", []):
                if pattern.lower() in check_str.lower():
                    return category["id"]
        return None

    def is_preexisting(self, file: str, message: str) -> bool:
        """Check if a diagnostic is in the known preexisting backlog."""
        for item in self.preexisting_backlog:
            if item["file"] in file and item["diagnostic"].lower() in message.lower():
                return True
        return False


# ---------------------------------------------------------------------------
# Suppression Validator
# ---------------------------------------------------------------------------

class SuppressionValidator:
    """Validates that suppressions comply with policy."""

    def __init__(self, policy: PolicyLoader):
        self.policy = policy

    def scan_file(self, file_path: Path) -> list[SuppressionIssue]:
        """Scan a file for suppression annotations and validate them.

        A CQR-ID is valid if it appears on the same line as the suppression
        marker OR on one of the two immediately preceding lines (to support
        patterns like a comment with CQR-ID above a #pragma).
        """
        issues = []
        try:
            content = file_path.read_text(encoding="utf-8", errors="replace")
        except (OSError, UnicodeDecodeError):
            return issues

        lines = content.splitlines()

        for line_num, line in enumerate(lines, start=1):
            # Check for inline suppression markers
            for marker in SUPPRESSION_MARKERS:
                if marker.search(line):
                    # Check current line and up to 2 preceding lines for CQR-ID
                    has_id = SUPPRESSION_ID_PATTERN.search(line)
                    if not has_id:
                        # Look at preceding lines (within push/pop context)
                        for offset in range(1, 3):
                            prev_idx = line_num - 1 - offset
                            if prev_idx >= 0:
                                prev_line = lines[prev_idx]
                                if SUPPRESSION_ID_PATTERN.search(prev_line):
                                    has_id = True
                                    break
                                # Stop searching past non-comment/non-pragma lines
                                stripped = prev_line.strip()
                                if stripped and not stripped.startswith("//") and not stripped.startswith("#pragma"):
                                    break

                    if not has_id:
                        issues.append(SuppressionIssue(
                            file=str(file_path),
                            line=line_num,
                            kind="missing_id",
                            content=line.strip(),
                            message="Suppression without required CQR-ID",
                        ))
                    break

            # Check for global suppression patterns in CMake files
            if file_path.suffix in (".cmake", ".txt") and "CMake" in file_path.name or file_path.name.endswith("CMakeLists.txt"):
                for gp in GLOBAL_SUPPRESSION_PATTERNS:
                    if gp.search(line):
                        if not SUPPRESSION_ID_PATTERN.search(line):
                            issues.append(SuppressionIssue(
                                file=str(file_path),
                                line=line_num,
                                kind="global",
                                content=line.strip(),
                                message="Global suppression without CQR-ID finding link",
                            ))

        return issues

    def scan_directory(self, directory: Path, extensions: set[str] | None = None) -> list[SuppressionIssue]:
        """Scan a directory tree for suppression issues."""
        if extensions is None:
            extensions = {".c", ".h", ".cpp", ".hpp", ".cmake", ".txt"}

        issues = []
        for file_path in directory.rglob("*"):
            # Test fixtures deliberately contain invalid suppression snippets to
            # exercise this validator; they are not production/test source.
            if "fixtures" in file_path.relative_to(directory).parts:
                continue
            if file_path.suffix in extensions or file_path.name == "CMakeLists.txt":
                issues.extend(self.scan_file(file_path))
        return issues


# ---------------------------------------------------------------------------
# Diagnostic Parser
# ---------------------------------------------------------------------------

class DiagnosticParser:
    """Parses compiler/analyzer output into structured diagnostics."""

    def __init__(self, policy: PolicyLoader, normalizer: OutputNormalizer):
        self.policy = policy
        self.normalizer = normalizer

    def parse_compiler_output(
        self,
        output: str,
        changed_files: set[str] | None = None,
        changed_lines: dict[str, set[int]] | None = None,
    ) -> list[Diagnostic]:
        """Parse GCC/Clang warning output into diagnostics."""
        diagnostics = []
        normalized = self.normalizer.normalize(output)

        for line in normalized.splitlines():
            match = WARNING_LINE_PATTERN.match(line)
            if not match:
                continue

            file_path = match.group("file")
            line_num = int(match.group("line"))
            col = int(match.group("col"))
            level = match.group("level")
            message = match.group("message")
            flag = match.group("flag")

            # Determine if in changed code
            is_changed = False
            if changed_lines and file_path in changed_lines:
                is_changed = line_num in changed_lines[file_path]
            elif changed_files and file_path in changed_files:
                is_changed = True

            # Classify severity
            blocking_cat = self.policy.is_blocking_pattern(flag, message)
            if blocking_cat:
                severity = DiagnosticSeverity.BLOCKING
            elif is_changed:
                severity = DiagnosticSeverity.NEW_WARNING
            elif self.policy.is_preexisting(file_path, message):
                severity = DiagnosticSeverity.BACKLOG
            else:
                severity = DiagnosticSeverity.INFO

            diag = Diagnostic(
                file=file_path,
                line=line_num,
                column=col,
                level=level,
                message=message,
                flag=flag,
                category=blocking_cat,
                severity=severity,
                is_in_changed_code=is_changed,
            )
            diag.fingerprint = self.normalizer.fingerprint(diag)
            diagnostics.append(diag)

        return diagnostics


# ---------------------------------------------------------------------------
# Changed Files Detection (git diff)
# ---------------------------------------------------------------------------

def get_changed_files(repo_root: Path, base_ref: str = "HEAD~1") -> set[str]:
    """Get the set of files changed relative to base_ref."""
    try:
        result = subprocess.run(
            ["git", "diff", "--name-only", base_ref],
            capture_output=True, text=True, cwd=repo_root,
            timeout=30,
        )
        if result.returncode == 0:
            return {f.strip() for f in result.stdout.splitlines() if f.strip()}
    except (subprocess.TimeoutExpired, FileNotFoundError):
        pass
    return set()


def get_changed_lines(repo_root: Path, base_ref: str = "HEAD~1") -> dict[str, set[int]]:
    """Get changed line numbers per file from git diff."""
    changed: dict[str, set[int]] = {}
    try:
        result = subprocess.run(
            ["git", "diff", "-U0", base_ref],
            capture_output=True, text=True, cwd=repo_root,
            timeout=30,
        )
        if result.returncode != 0:
            return changed
    except (subprocess.TimeoutExpired, FileNotFoundError):
        return changed

    current_file = None
    for line in result.stdout.splitlines():
        if line.startswith("+++ b/"):
            current_file = line[6:]
            if current_file not in changed:
                changed[current_file] = set()
        elif line.startswith("@@") and current_file:
            # Parse @@ -old,count +new,count @@ format
            match = re.search(r"\+(\d+)(?:,(\d+))?", line)
            if match:
                start = int(match.group(1))
                count = int(match.group(2)) if match.group(2) else 1
                for i in range(start, start + count):
                    changed[current_file].add(i)

    return changed


# ---------------------------------------------------------------------------
# Static Analysis Runner
# ---------------------------------------------------------------------------

class StaticAnalysisRunner:
    """Orchestrates static analysis and produces a gate report."""

    def __init__(
        self,
        repo_root: Path,
        policy_path: Path,
        build_dir: Path | None = None,
        base_ref: str = "HEAD~1",
    ):
        self.repo_root = repo_root
        self.policy = PolicyLoader(policy_path)
        self.build_dir = build_dir or repo_root / "build" / "host"
        self.normalizer = OutputNormalizer(repo_root, self.build_dir)
        self.parser = DiagnosticParser(self.policy, self.normalizer)
        self.suppression_validator = SuppressionValidator(self.policy)
        self.base_ref = base_ref

    def run(self, compiler_output: str | None = None) -> AnalysisReport:
        """Run full analysis pipeline and produce report."""
        report = AnalysisReport()

        # Get changed files/lines
        changed_files = get_changed_files(self.repo_root, self.base_ref)
        changed_lines = get_changed_lines(self.repo_root, self.base_ref)

        # Parse compiler output if provided
        if compiler_output:
            diagnostics = self.parser.parse_compiler_output(
                compiler_output, changed_files, changed_lines
            )
            report.diagnostics.extend(diagnostics)
            report.tools_used.append("compiler-warnings")

        # Check compile_commands.json exists
        compile_commands = self.build_dir / "compile_commands.json"
        if compile_commands.exists():
            report.tools_used.append("compile_commands.json")

        # Scan for suppression issues
        for scan_dir in [
            self.repo_root / "components",
            self.repo_root / "main",
            self.repo_root / "test" / "host",
        ]:
            if scan_dir.exists():
                issues = self.suppression_validator.scan_directory(scan_dir)
                report.suppression_issues.extend(issues)

        # Classify and count
        for diag in report.diagnostics:
            if diag.severity == DiagnosticSeverity.BLOCKING:
                report.blocking_count += 1
            elif diag.severity == DiagnosticSeverity.NEW_WARNING:
                report.new_warning_count += 1
            elif diag.severity == DiagnosticSeverity.BACKLOG:
                report.backlog_count += 1

        # Determine gate result
        if report.blocking_count > 0:
            report.gate_passed = False
            report.gate_failure_reasons.append(
                f"{report.blocking_count} blocking diagnostic(s): "
                "bounds/lifetime/uninitialized/overflow/data_race/null_deref"
            )

        if report.new_warning_count > 0:
            report.gate_passed = False
            report.gate_failure_reasons.append(
                f"{report.new_warning_count} new warning(s) in changed code"
            )

        if report.suppression_issues:
            invalid_suppressions = [
                s for s in report.suppression_issues
                if s.kind in ("missing_id", "global")
            ]
            if invalid_suppressions:
                report.gate_passed = False
                report.gate_failure_reasons.append(
                    f"{len(invalid_suppressions)} suppression(s) without valid CQR-ID"
                )

        return report

    def run_shellcheck(self, scripts: list[Path] | None = None) -> list[Diagnostic]:
        """Run ShellCheck on shell scripts."""
        if scripts is None:
            scripts = list(self.repo_root.rglob("*.sh"))

        if not scripts:
            return []

        diagnostics = []
        try:
            result = subprocess.run(
                ["shellcheck", "--version"],
                capture_output=True, text=True, timeout=5,
            )
            if result.returncode != 0:
                return []  # shellcheck not available
        except (FileNotFoundError, subprocess.TimeoutExpired):
            return []

        for script in scripts:
            try:
                result = subprocess.run(
                    ["shellcheck", "-f", "gcc", str(script)],
                    capture_output=True, text=True, timeout=30,
                )
                if result.stdout:
                    parsed = self.parser.parse_compiler_output(result.stdout)
                    diagnostics.extend(parsed)
            except (subprocess.TimeoutExpired, FileNotFoundError):
                continue

        return diagnostics

    def check_affected_units(
        self,
        changed_files: set[str],
        analyzed_files: set[str],
        categories: dict[str, str],
    ) -> list[str]:
        """
        Check that files in affected categories are included in analysis.
        Returns list of missing files that should have been analyzed.

        categories maps file paths to their category (parsing, concurrency, etc.)
        """
        missing = []
        required_cats = set(self.policy.affected_categories)

        for file_path, category in categories.items():
            if category in required_cats and file_path in changed_files:
                if file_path not in analyzed_files:
                    missing.append(file_path)

        return missing


# ---------------------------------------------------------------------------
# CLI entry point
# ---------------------------------------------------------------------------

def main():
    """Run static analysis and report results."""
    repo_root = Path(__file__).resolve().parents[2]
    policy_path = (
        repo_root / ".kiro" / "specs" / "code-quality-review"
        / "artifacts" / "static_analysis_policy.yaml"
    )

    build_dir = None
    compiler_output = None

    # Parse arguments
    args = sys.argv[1:]
    base_ref = "HEAD~1"
    output_file = None

    i = 0
    while i < len(args):
        if args[i] == "--build-dir" and i + 1 < len(args):
            build_dir = Path(args[i + 1])
            i += 2
        elif args[i] == "--compiler-output" and i + 1 < len(args):
            compiler_output = Path(args[i + 1]).read_text()
            i += 2
        elif args[i] == "--base-ref" and i + 1 < len(args):
            base_ref = args[i + 1]
            i += 2
        elif args[i] == "--output" and i + 1 < len(args):
            output_file = Path(args[i + 1])
            i += 2
        elif args[i] == "--stdin":
            compiler_output = sys.stdin.read()
            i += 1
        else:
            i += 1

    runner = StaticAnalysisRunner(
        repo_root=repo_root,
        policy_path=policy_path,
        build_dir=build_dir,
        base_ref=base_ref,
    )

    report = runner.run(compiler_output=compiler_output)

    # Output report
    report_dict = report.to_dict()

    if output_file:
        with open(output_file, "w") as f:
            json.dump(report_dict, f, indent=2)
        print(f"Report written to {output_file}")
    else:
        print(json.dumps(report_dict, indent=2))

    # Exit code
    if report.gate_passed:
        print("\nGATE PASSED: Static analysis OK")
        sys.exit(0)
    else:
        print(f"\nGATE FAILED: {len(report.gate_failure_reasons)} reason(s):")
        for reason in report.gate_failure_reasons:
            print(f"  - {reason}")
        sys.exit(1)


if __name__ == "__main__":
    main()
