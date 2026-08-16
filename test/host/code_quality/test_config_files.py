#!/usr/bin/env python3
"""
Test suite for sdkconfig.defaults and partitions.csv validation.

Exercises Property 3: Every canonical reference resolves.
Policy chosen: "versioned and validated" for both files.

Fixtures cover:
  - Both files exist and are correctly cross-referenced (positive)
  - Orphan reference: sdkconfig.defaults references a file that doesn't exist
  - Missing required file: partitions.csv absent but referenced
  - Missing sdkconfig.defaults while policy says "versioned"
  - Ambiguous coexistence: partition config enabled but filename wrong
  - CONFIG_PARTITION_TABLE_CUSTOM=y missing from sdkconfig.defaults

Feature: code-quality-review
Property 3: Every canonical reference resolves
Requirements: 3.3, 3.4, 3.5, 12.4, 12.5
"""

import sys
import tempfile
from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "tools"))

from code_quality.validate_config_files import ConfigFilesValidator, ConfigValidationError


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def create_temp_repo(
    sdkconfig_content: str = None,
    partitions_content: str = None,
) -> Path:
    """
    Create a temporary directory simulating a repository root.
    If content is None, the file is not created.
    """
    tmp = Path(tempfile.mkdtemp())
    if sdkconfig_content is not None:
        (tmp / "sdkconfig.defaults").write_text(sdkconfig_content, encoding="utf-8")
    if partitions_content is not None:
        (tmp / "partitions.csv").write_text(partitions_content, encoding="utf-8")
    return tmp


VALID_SDKCONFIG = """\
# Test sdkconfig.defaults
CONFIG_IDF_TARGET="esp32s3"
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"
CONFIG_LOG_DEFAULT_LEVEL=3
"""

VALID_PARTITIONS = """\
# ESP-IDF Partition Table
# Name,   Type, SubType, Offset,  Size, Flags
nvs,      data, nvs,     0x9000,  0x6000,
phy_init, data, phy,     0xf000,  0x1000,
factory,  app,  factory, 0x10000, 0x300000,
storage,  data, fat,     0x310000,0x4F0000,
"""


# ---------------------------------------------------------------------------
# Positive tests: real repository
# ---------------------------------------------------------------------------

class TestConfigFilesPositive:
    """Validate that the real repository passes all checks."""

    def test_real_repo_sdkconfig_defaults_exists(self):
        """sdkconfig.defaults exists at the real repository root."""
        assert (REPO_ROOT / "sdkconfig.defaults").is_file()

    def test_real_repo_partitions_csv_exists(self):
        """partitions.csv exists at the real repository root."""
        assert (REPO_ROOT / "partitions.csv").is_file()

    def test_real_repo_passes_validation(self):
        """The real repository passes all config file validation checks."""
        validator = ConfigFilesValidator(REPO_ROOT)
        errors = validator.validate()
        assert errors == [], f"Unexpected validation errors: {errors}"

    def test_real_repo_has_partition_custom_enabled(self):
        """sdkconfig.defaults contains CONFIG_PARTITION_TABLE_CUSTOM=y."""
        content = (REPO_ROOT / "sdkconfig.defaults").read_text(encoding="utf-8")
        assert "CONFIG_PARTITION_TABLE_CUSTOM=y" in content

    def test_real_repo_references_partitions_csv(self):
        """sdkconfig.defaults references partitions.csv by filename."""
        content = (REPO_ROOT / "sdkconfig.defaults").read_text(encoding="utf-8")
        assert 'CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"' in content

    def test_real_repo_policy_summary(self):
        """Policy summary reports both files as versioned_and_validated."""
        validator = ConfigFilesValidator(REPO_ROOT)
        validator.validate()
        summary = validator.get_policy_summary()
        assert summary["policy"] == "versioned_and_validated"
        assert summary["validation_errors"] == 0
        for file_info in summary["files"]:
            assert file_info["exists"] is True
            assert file_info["policy"] == "versioned_and_validated"


# ---------------------------------------------------------------------------
# Positive tests: synthetic fixtures
# ---------------------------------------------------------------------------

class TestConfigFilesSyntheticPositive:
    """Synthetic repos with correct setup pass validation."""

    def test_valid_synthetic_repo(self):
        """A synthetic repo with both files correctly set up passes."""
        tmp = create_temp_repo(VALID_SDKCONFIG, VALID_PARTITIONS)
        validator = ConfigFilesValidator(tmp)
        errors = validator.validate()
        assert errors == []


# ---------------------------------------------------------------------------
# Negative tests: orphan reference
# ---------------------------------------------------------------------------

class TestOrphanReference:
    """
    Regression: orphan reference must be detected.
    sdkconfig.defaults references a file that doesn't exist.
    """

    def test_sdkconfig_references_missing_partitions(self):
        """
        Fails when sdkconfig.defaults references partitions.csv
        but partitions.csv does not exist (orphan reference).
        """
        tmp = create_temp_repo(
            sdkconfig_content=VALID_SDKCONFIG,
            partitions_content=None,  # partitions.csv NOT created
        )
        validator = ConfigFilesValidator(tmp)
        errors = validator.validate()
        assert len(errors) > 0, "Expected errors for orphan reference"

        # Must detect both the existence error and the orphan reference
        rules = {e.rule for e in errors}
        assert "existence" in rules, f"Expected 'existence' error, got: {rules}"
        assert "orphan_reference" in rules, f"Expected 'orphan_reference' error, got: {rules}"

    def test_sdkconfig_references_wrong_filename(self):
        """
        Fails when sdkconfig.defaults references a different filename
        that doesn't exist.
        """
        sdkconfig_with_wrong_ref = """\
CONFIG_IDF_TARGET="esp32s3"
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="custom_partitions.csv"
"""
        tmp = create_temp_repo(
            sdkconfig_content=sdkconfig_with_wrong_ref,
            partitions_content=VALID_PARTITIONS,  # exists as partitions.csv, not custom_partitions.csv
        )
        validator = ConfigFilesValidator(tmp)
        errors = validator.validate()
        assert len(errors) > 0, "Expected errors for wrong filename reference"

        rules = {e.rule for e in errors}
        # Should detect filename mismatch and orphan reference
        assert "partition_filename_mismatch" in rules or "orphan_reference" in rules


# ---------------------------------------------------------------------------
# Negative tests: missing required file
# ---------------------------------------------------------------------------

class TestMissingRequiredFile:
    """
    Regression: missing required file must be detected.
    """

    def test_missing_partitions_csv(self):
        """Fails when partitions.csv is missing but policy says versioned."""
        tmp = create_temp_repo(
            sdkconfig_content=VALID_SDKCONFIG,
            partitions_content=None,
        )
        validator = ConfigFilesValidator(tmp)
        errors = validator.validate()
        existence_errors = [e for e in errors if e.rule == "existence"]
        assert len(existence_errors) > 0
        assert any("partitions.csv" in e.message for e in existence_errors)

    def test_missing_sdkconfig_defaults(self):
        """Fails when sdkconfig.defaults is missing but policy says versioned."""
        tmp = create_temp_repo(
            sdkconfig_content=None,
            partitions_content=VALID_PARTITIONS,
        )
        validator = ConfigFilesValidator(tmp)
        errors = validator.validate()
        existence_errors = [e for e in errors if e.rule == "existence"]
        assert len(existence_errors) > 0
        assert any("sdkconfig.defaults" in e.message for e in existence_errors)

    def test_both_files_missing(self):
        """Fails when both config files are missing."""
        tmp = create_temp_repo(
            sdkconfig_content=None,
            partitions_content=None,
        )
        validator = ConfigFilesValidator(tmp)
        errors = validator.validate()
        existence_errors = [e for e in errors if e.rule == "existence"]
        assert len(existence_errors) == 2


# ---------------------------------------------------------------------------
# Negative tests: ambiguous coexistence / wrong configuration
# ---------------------------------------------------------------------------

class TestAmbiguousConfiguration:
    """
    Regression: ambiguous or incomplete configuration must be detected.
    """

    def test_missing_partition_custom_flag(self):
        """
        Fails when CONFIG_PARTITION_TABLE_CUSTOM=y is missing
        but partitions.csv exists and is supposedly used.
        """
        sdkconfig_no_custom = """\
CONFIG_IDF_TARGET="esp32s3"
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"
CONFIG_LOG_DEFAULT_LEVEL=3
"""
        tmp = create_temp_repo(
            sdkconfig_content=sdkconfig_no_custom,
            partitions_content=VALID_PARTITIONS,
        )
        validator = ConfigFilesValidator(tmp)
        errors = validator.validate()
        custom_errors = [e for e in errors if e.rule == "partition_custom_enabled"]
        assert len(custom_errors) > 0, (
            "Expected error for missing CONFIG_PARTITION_TABLE_CUSTOM=y"
        )

    def test_missing_partition_filename_reference(self):
        """
        Fails when CONFIG_PARTITION_TABLE_CUSTOM=y is present
        but CONFIG_PARTITION_TABLE_CUSTOM_FILENAME is missing.
        """
        sdkconfig_no_filename = """\
CONFIG_IDF_TARGET="esp32s3"
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_LOG_DEFAULT_LEVEL=3
"""
        tmp = create_temp_repo(
            sdkconfig_content=sdkconfig_no_filename,
            partitions_content=VALID_PARTITIONS,
        )
        validator = ConfigFilesValidator(tmp)
        errors = validator.validate()
        filename_errors = [e for e in errors if e.rule == "partition_filename_reference"]
        assert len(filename_errors) > 0, (
            "Expected error for missing CONFIG_PARTITION_TABLE_CUSTOM_FILENAME"
        )

    def test_filename_points_to_nonexistent_file(self):
        """
        Fails when the referenced partition file does not exist at repo root.
        """
        sdkconfig_bad_ref = """\
CONFIG_IDF_TARGET="esp32s3"
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="nonexistent_partitions.csv"
"""
        tmp = create_temp_repo(
            sdkconfig_content=sdkconfig_bad_ref,
            partitions_content=VALID_PARTITIONS,  # exists as partitions.csv, not nonexistent_partitions.csv
        )
        validator = ConfigFilesValidator(tmp)
        errors = validator.validate()
        rules = {e.rule for e in errors}
        assert "orphan_reference" in rules or "partition_filename_mismatch" in rules


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    pytest.main([__file__, "-v"])
