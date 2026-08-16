#!/usr/bin/env python3
"""
Test suite for documentation reference validation.

Exercises Property 3 (Every canonical reference resolves) with positive and
negative fixtures and validates the actual project README.md.

Regression tests prove:
  - ./run_tests reference is rejected (CQR-DOCS-001)
  - components/hal/ reference is rejected (CQR-DOCS-002)
  - Correct hw_hal/ reference passes
  - Canonical cmake/ctest commands in README are valid

Feature: code-quality-review
Property 3: Every canonical reference resolves
Validates: Requirements 3.2, 3.5, 3.6, 12.4, 12.5
"""

import sys
from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "tools"))

from code_quality.check_doc_references import (
    DocReferenceValidator,
    check_prohibited_references,
    extract_code_blocks,
    extract_script_references,
    extract_path_references,
)

FIXTURES_DIR = Path(__file__).parent / "fixtures"


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def validate_fixture(fixture_name: str, repo_root: Path | None = None) -> list:
    """Run the validator against a fixture README."""
    readme_path = FIXTURES_DIR / fixture_name
    root = repo_root or REPO_ROOT
    validator = DocReferenceValidator(readme_path, root)
    return validator.validate()


# ---------------------------------------------------------------------------
# Property 3: Positive — valid references pass
# ---------------------------------------------------------------------------

class TestDocReferencesPositive:
    """Valid documentation references must pass validation."""

    def test_valid_readme_fixture_passes(self):
        """A README with correct paths and commands passes."""
        errors = validate_fixture("readme_valid.md")
        assert errors == [], f"Unexpected errors: {errors}"

    def test_actual_project_readme_passes(self):
        """
        The actual project README.md must pass reference validation.
        This confirms the fix for CQR-DOCS-001 and CQR-DOCS-002.
        """
        readme_path = REPO_ROOT / "README.md"
        validator = DocReferenceValidator(readme_path, REPO_ROOT)
        errors = validator.validate()
        assert errors == [], f"README.md has reference errors: {errors}"

    def test_hw_hal_component_path_resolves(self):
        """components/hw_hal path resolves with correct case."""
        content = "The component at `components/hw_hal` provides HAL."
        refs = extract_path_references(content, 1)
        path_refs = [r for r in refs if r.value == "components/hw_hal"]
        assert len(path_refs) > 0
        # Verify it exists in the repo
        assert (REPO_ROOT / "components" / "hw_hal").exists()


# ---------------------------------------------------------------------------
# Property 3: Negative — invalid references fail
# ---------------------------------------------------------------------------

class TestDocReferencesNegative:
    """Invalid documentation references must fail validation."""

    def test_run_tests_reference_rejected(self):
        """
        Regression: ./run_tests must be rejected (CQR-DOCS-001).
        Validates Property 3, Requirement 3.2.
        """
        errors = validate_fixture("readme_with_run_tests.md")
        assert len(errors) > 0, "Expected errors for ./run_tests reference"
        run_tests_errors = [
            e for e in errors
            if "run_tests" in e.message or "run_tests" in e.reference.context
        ]
        assert len(run_tests_errors) > 0, f"Expected run_tests error, got: {errors}"

    def test_wrong_case_hal_reference_rejected(self):
        """
        Regression: components/hal/ must be rejected (CQR-DOCS-002).
        Validates Property 3, Requirement 3.5.
        """
        errors = validate_fixture("readme_with_wrong_case.md")
        assert len(errors) > 0, "Expected errors for hal/ reference"
        hal_errors = [
            e for e in errors
            if "hal" in e.message.lower() and "hw_hal" not in e.message.lower()
            or "CQR-DOCS-002" in e.message
        ]
        assert len(hal_errors) > 0, f"Expected hal case error, got: {errors}"

    def test_nonexistent_script_reference_rejected(self):
        """A script reference to a nonexistent file must fail."""
        content = "```bash\n./nonexistent_tool\n```"
        readme_path = FIXTURES_DIR / "_inline_test.md"
        readme_path.write_text(content)
        try:
            validator = DocReferenceValidator(readme_path, REPO_ROOT)
            errors = validator.validate()
            script_errors = [e for e in errors if "nonexistent_tool" in e.message]
            assert len(script_errors) > 0
        finally:
            readme_path.unlink(missing_ok=True)


# ---------------------------------------------------------------------------
# Extraction function unit tests
# ---------------------------------------------------------------------------

class TestExtraction:
    """Unit tests for reference extraction functions."""

    def test_extract_code_blocks(self):
        """Code blocks are correctly identified with line numbers."""
        content = "text\n```bash\ncmd1\ncmd2\n```\nmore text"
        blocks = extract_code_blocks(content)
        assert len(blocks) == 1
        assert blocks[0][0] == 2  # starts at line 2
        assert "cmd1" in blocks[0][1]
        assert "cmd2" in blocks[0][1]

    def test_extract_script_references(self):
        """Script references like ./something are found."""
        content = "Run ./run_tests to execute tests"
        refs = extract_script_references(content, 1)
        assert len(refs) == 1
        assert refs[0].value == "./run_tests"
        assert refs[0].kind == "script"

    def test_extract_component_path(self):
        """Component path references are extracted."""
        content = "Check components/hw_hal for the HAL layer."
        refs = extract_path_references(content, 1)
        path_refs = [r for r in refs if r.kind == "path"]
        assert any(r.value == "components/hw_hal" for r in path_refs)

    def test_extract_tree_structure_entry(self):
        """Tree diagram entries are extracted."""
        content = "│   ├── hw_hal/                 # HAL layer"
        refs = extract_path_references(content, 1)
        structure_refs = [r for r in refs if r.kind == "structure"]
        assert any(r.value == "hw_hal" for r in structure_refs)


# ---------------------------------------------------------------------------
# Prohibited references (always fail regardless of FS)
# ---------------------------------------------------------------------------

class TestProhibitedReferences:
    """Prohibited references are always rejected."""

    def test_run_tests_always_prohibited(self):
        """./run_tests is always prohibited even if a file existed."""
        content = "Run ./run_tests to test."
        errors = check_prohibited_references(content)
        assert len(errors) > 0
        assert any("CQR-DOCS-001" in e.message for e in errors)

    def test_hal_path_always_prohibited(self):
        """components/hal/ is always prohibited."""
        content = "See components/hal/ for drivers."
        errors = check_prohibited_references(content)
        assert len(errors) > 0
        assert any("CQR-DOCS-002" in e.message for e in errors)

    def test_hw_hal_is_not_prohibited(self):
        """components/hw_hal/ is the correct name and must not be flagged."""
        content = "See components/hw_hal/ for drivers."
        errors = check_prohibited_references(content)
        assert errors == []


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    pytest.main([__file__, "-v"])
