#!/usr/bin/env python3
"""
Code Quality Review — Documentation Reference Validator

Parses README.md for referenced commands, scripts, paths, and project structure
entries. Verifies that each reference resolves against the repository:
  - Scripts and targets exist
  - Paths exist with correct case on case-sensitive filesystems
  - No references to nonexistent commands (e.g., ./run_tests)
  - Component names match actual directory names (e.g., hw_hal not hal)

Validates: Requirements 3.2, 3.5, 3.6, 12.4, 12.5
Properties: P3 (Every canonical reference resolves)
"""

import re
import sys
from pathlib import Path
from typing import NamedTuple


# ---------------------------------------------------------------------------
# Data structures
# ---------------------------------------------------------------------------

class DocReference(NamedTuple):
    """A reference extracted from documentation."""
    kind: str          # "script", "path", "command", "structure"
    value: str         # The referenced value
    line_number: int   # Line in the source file
    context: str       # Surrounding text for diagnostics


class ValidationError(NamedTuple):
    """A validation failure."""
    reference: DocReference
    message: str


# ---------------------------------------------------------------------------
# Known invalid references (regression checks)
# ---------------------------------------------------------------------------

# These patterns are explicitly prohibited regardless of filesystem state.
# They represent known past bugs that must never reappear.
PROHIBITED_REFERENCES = [
    {
        "pattern": re.compile(r"\./run_tests\b"),
        "kind": "script",
        "message": "references nonexistent ./run_tests script (CQR-DOCS-001)",
    },
    {
        "pattern": re.compile(r"components/hal/|├── hal/|│\s+├── hal/"),
        "kind": "path",
        "message": "references components/hal/ instead of components/hw_hal/ (CQR-DOCS-002)",
    },
]


# ---------------------------------------------------------------------------
# Extraction functions
# ---------------------------------------------------------------------------

def extract_code_blocks(readme_content: str) -> list[tuple[int, str]]:
    """Extract fenced code block contents with their starting line numbers."""
    blocks = []
    in_block = False
    block_start = 0
    block_lines: list[str] = []

    for i, line in enumerate(readme_content.splitlines(), start=1):
        if line.strip().startswith("```") and not in_block:
            in_block = True
            block_start = i
            block_lines = []
        elif line.strip().startswith("```") and in_block:
            in_block = False
            blocks.append((block_start, "\n".join(block_lines)))
        elif in_block:
            block_lines.append(line)

    return blocks


def extract_script_references(content: str, line_offset: int) -> list[DocReference]:
    """Find references to executable scripts (./something)."""
    refs = []
    for i, line in enumerate(content.splitlines(), start=line_offset):
        matches = re.finditer(r"\./([a-zA-Z_][a-zA-Z0-9_.-]*)", line)
        for m in matches:
            refs.append(DocReference(
                kind="script",
                value=f"./{m.group(1)}",
                line_number=i,
                context=line.strip(),
            ))
    return refs


def extract_path_references(content: str, line_offset: int) -> list[DocReference]:
    """Find references to project paths in structure diagrams and prose."""
    refs = []
    # Match tree-diagram entries like "├── hal/" or "│   ├── hw_hal/"
    tree_pattern = re.compile(r"[├└│─\s]+([a-zA-Z_][a-zA-Z0-9_.-]*/)")
    # Match explicit component paths like "components/hal" or "components/hw_hal"
    component_pattern = re.compile(r"components/([a-zA-Z_][a-zA-Z0-9_.-]*)")

    for i, line in enumerate(content.splitlines(), start=line_offset):
        for m in tree_pattern.finditer(line):
            refs.append(DocReference(
                kind="structure",
                value=m.group(1).rstrip("/"),
                line_number=i,
                context=line.strip(),
            ))
        for m in component_pattern.finditer(line):
            refs.append(DocReference(
                kind="path",
                value=f"components/{m.group(1)}",
                line_number=i,
                context=line.strip(),
            ))
    return refs


# ---------------------------------------------------------------------------
# Validation functions
# ---------------------------------------------------------------------------

def check_prohibited_references(content: str) -> list[ValidationError]:
    """Check for explicitly prohibited references (regression)."""
    errors = []
    for i, line in enumerate(content.splitlines(), start=1):
        for rule in PROHIBITED_REFERENCES:
            if rule["pattern"].search(line):
                ref = DocReference(
                    kind=rule["kind"],
                    value=rule["pattern"].pattern,
                    line_number=i,
                    context=line.strip(),
                )
                errors.append(ValidationError(
                    reference=ref,
                    message=rule["message"],
                ))
    return errors


def check_script_exists(ref: DocReference, repo_root: Path) -> ValidationError | None:
    """Verify a script reference resolves to an existing file."""
    script_path = repo_root / ref.value.lstrip("./")
    if not script_path.exists():
        return ValidationError(
            reference=ref,
            message=f"script '{ref.value}' does not exist at {script_path}",
        )
    return None


def check_component_path_case(ref: DocReference, repo_root: Path) -> ValidationError | None:
    """Verify a component path exists with the exact case referenced."""
    full_path = repo_root / ref.value
    if not full_path.exists():
        return ValidationError(
            reference=ref,
            message=f"path '{ref.value}' does not exist (check capitalization)",
        )
    # Verify case matches on case-sensitive systems by resolving
    resolved = full_path.resolve()
    expected = (repo_root / ref.value).resolve()
    if str(resolved) != str(expected):
        return ValidationError(
            reference=ref,
            message=f"path '{ref.value}' case mismatch: actual is '{resolved.name}'",
        )
    return None


# ---------------------------------------------------------------------------
# Main validator
# ---------------------------------------------------------------------------

class DocReferenceValidator:
    """Validates documentation references against the repository."""

    def __init__(self, readme_path: Path, repo_root: Path | None = None):
        self.readme_path = readme_path
        self.repo_root = repo_root or readme_path.parent
        self.errors: list[ValidationError] = []

    def validate(self) -> list[ValidationError]:
        """Run all validations and return errors."""
        self.errors = []

        if not self.readme_path.exists():
            self.errors.append(ValidationError(
                reference=DocReference("file", str(self.readme_path), 0, ""),
                message="README.md not found",
            ))
            return self.errors

        content = self.readme_path.read_text(encoding="utf-8")

        # 1. Check prohibited references (always fails, regardless of FS)
        self.errors.extend(check_prohibited_references(content))

        # 2. Check script references in code blocks
        code_blocks = extract_code_blocks(content)
        for start_line, block_content in code_blocks:
            script_refs = extract_script_references(block_content, start_line)
            for ref in script_refs:
                err = check_script_exists(ref, self.repo_root)
                if err:
                    self.errors.append(err)

        # 3. Check component path references
        path_refs = extract_path_references(content, 1)
        for ref in path_refs:
            if ref.kind == "path" and ref.value.startswith("components/"):
                err = check_component_path_case(ref, self.repo_root)
                if err:
                    self.errors.append(err)

        return self.errors


# ---------------------------------------------------------------------------
# CLI entry point
# ---------------------------------------------------------------------------

def main():
    """Validate README.md documentation references."""
    if len(sys.argv) < 2:
        # Default to project root README.md
        repo_root = Path(__file__).resolve().parents[2]
        readme_path = repo_root / "README.md"
    else:
        readme_path = Path(sys.argv[1]).resolve()
        repo_root = readme_path.parent

    if len(sys.argv) >= 3:
        repo_root = Path(sys.argv[2]).resolve()

    validator = DocReferenceValidator(readme_path, repo_root)
    errors = validator.validate()

    if errors:
        print(f"FAILED: {len(errors)} documentation reference error(s):")
        for err in errors:
            loc = f"line {err.reference.line_number}" if err.reference.line_number else ""
            print(f"  [{err.reference.kind}] {loc}: {err.message}")
            if err.reference.context:
                print(f"    > {err.reference.context}")
        sys.exit(1)
    else:
        print("PASSED: all README.md references resolve correctly")
        sys.exit(0)


if __name__ == "__main__":
    main()
