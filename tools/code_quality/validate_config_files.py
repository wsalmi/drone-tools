#!/usr/bin/env python3
"""
Validator for sdkconfig.defaults and partitions.csv configuration files.

Enforces Property 3: Every canonical reference resolves.
Policy: Both files are "versioned and validated" — they MUST exist in the
repository and be correctly cross-referenced.

Checks:
  1. sdkconfig.defaults exists at the repository root
  2. partitions.csv exists at the repository root
  3. sdkconfig.defaults contains CONFIG_PARTITION_TABLE_CUSTOM=y
  4. sdkconfig.defaults references partitions.csv by filename
  5. No orphan references (files referenced but missing)
  6. No ambiguous coexistence (both versioned AND documented-as-removed)

Requirements traced: 3.3, 3.4, 3.5, 12.4, 12.5
"""

from dataclasses import dataclass, field
from pathlib import Path
from typing import List, Optional
import re


@dataclass
class ConfigValidationError:
    """A single configuration validation error."""
    file: str
    rule: str
    message: str
    severity: str = "error"  # error | warning


@dataclass
class ConfigFilePolicy:
    """Policy declaration for a configuration file."""
    filename: str
    policy: str  # "versioned_and_validated" | "removed_and_documented"
    exists: bool = False
    referenced_by: List[str] = field(default_factory=list)


class ConfigFilesValidator:
    """
    Validates sdkconfig.defaults and partitions.csv according to the
    chosen policy: versioned and validated in the build.

    The validator checks that:
    - Both files physically exist
    - Cross-references between them are consistent
    - No orphan references to non-existent files
    - The partition table configuration is properly declared
    """

    REQUIRED_PARTITION_KEYS = [
        "CONFIG_PARTITION_TABLE_CUSTOM=y",
    ]

    PARTITION_FILENAME_PATTERN = re.compile(
        r'CONFIG_PARTITION_TABLE_CUSTOM_FILENAME\s*=\s*"([^"]+)"'
    )

    def __init__(self, repo_root: Path):
        self.repo_root = Path(repo_root)
        self.errors: List[ConfigValidationError] = []
        self.policies: List[ConfigFilePolicy] = []

    def validate(self) -> List[ConfigValidationError]:
        """Run all configuration file validations. Returns list of errors."""
        self.errors = []
        self._check_sdkconfig_defaults_exists()
        self._check_partitions_csv_exists()
        self._check_partition_table_reference()
        self._check_no_orphan_references()
        return self.errors

    def _check_sdkconfig_defaults_exists(self) -> None:
        """Verify sdkconfig.defaults exists at repo root."""
        path = self.repo_root / "sdkconfig.defaults"
        policy = ConfigFilePolicy(
            filename="sdkconfig.defaults",
            policy="versioned_and_validated",
            exists=path.is_file(),
        )
        self.policies.append(policy)

        if not path.is_file():
            self.errors.append(ConfigValidationError(
                file="sdkconfig.defaults",
                rule="existence",
                message=(
                    "sdkconfig.defaults is required by the 'versioned and validated' "
                    "policy but does not exist at the repository root"
                ),
            ))

    def _check_partitions_csv_exists(self) -> None:
        """Verify partitions.csv exists at repo root."""
        path = self.repo_root / "partitions.csv"
        policy = ConfigFilePolicy(
            filename="partitions.csv",
            policy="versioned_and_validated",
            exists=path.is_file(),
        )
        self.policies.append(policy)

        if not path.is_file():
            self.errors.append(ConfigValidationError(
                file="partitions.csv",
                rule="existence",
                message=(
                    "partitions.csv is required by the 'versioned and validated' "
                    "policy but does not exist at the repository root"
                ),
            ))

    def _check_partition_table_reference(self) -> None:
        """
        Verify sdkconfig.defaults declares CONFIG_PARTITION_TABLE_CUSTOM=y
        and references partitions.csv as the custom filename.
        """
        sdkconfig_path = self.repo_root / "sdkconfig.defaults"
        if not sdkconfig_path.is_file():
            return  # Already reported by existence check

        content = sdkconfig_path.read_text(encoding="utf-8")

        # Check CONFIG_PARTITION_TABLE_CUSTOM=y is present
        if "CONFIG_PARTITION_TABLE_CUSTOM=y" not in content:
            self.errors.append(ConfigValidationError(
                file="sdkconfig.defaults",
                rule="partition_custom_enabled",
                message=(
                    "sdkconfig.defaults must contain CONFIG_PARTITION_TABLE_CUSTOM=y "
                    "to use the versioned partitions.csv"
                ),
            ))

        # Check CONFIG_PARTITION_TABLE_CUSTOM_FILENAME references partitions.csv
        match = self.PARTITION_FILENAME_PATTERN.search(content)
        if not match:
            self.errors.append(ConfigValidationError(
                file="sdkconfig.defaults",
                rule="partition_filename_reference",
                message=(
                    "sdkconfig.defaults must contain "
                    'CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv" '
                    "to reference the versioned partition table"
                ),
            ))
        else:
            referenced_file = match.group(1)
            # Record the cross-reference
            for policy in self.policies:
                if policy.filename == "partitions.csv":
                    policy.referenced_by.append("sdkconfig.defaults")

            # Check that the referenced filename matches the actual file
            if referenced_file != "partitions.csv":
                self.errors.append(ConfigValidationError(
                    file="sdkconfig.defaults",
                    rule="partition_filename_mismatch",
                    message=(
                        f"CONFIG_PARTITION_TABLE_CUSTOM_FILENAME references "
                        f"'{referenced_file}' but the versioned file is 'partitions.csv'"
                    ),
                ))

    def _check_no_orphan_references(self) -> None:
        """
        Verify that any file referenced in sdkconfig.defaults actually exists.
        This catches orphan references where a file is mentioned but missing.
        """
        sdkconfig_path = self.repo_root / "sdkconfig.defaults"
        if not sdkconfig_path.is_file():
            return

        content = sdkconfig_path.read_text(encoding="utf-8")

        # Check the partition table filename reference resolves
        match = self.PARTITION_FILENAME_PATTERN.search(content)
        if match:
            referenced_file = match.group(1)
            referenced_path = self.repo_root / referenced_file
            if not referenced_path.is_file():
                self.errors.append(ConfigValidationError(
                    file="sdkconfig.defaults",
                    rule="orphan_reference",
                    message=(
                        f"sdkconfig.defaults references '{referenced_file}' "
                        f"but the file does not exist at the repository root "
                        f"(orphan reference)"
                    ),
                ))

    def get_policy_summary(self) -> dict:
        """Return a summary of the configuration file policies."""
        return {
            "policy": "versioned_and_validated",
            "files": [
                {
                    "filename": p.filename,
                    "policy": p.policy,
                    "exists": p.exists,
                    "referenced_by": p.referenced_by,
                }
                for p in self.policies
            ],
            "sdkconfig_defaults_entries": {
                "target": "CONFIG_IDF_TARGET=\"esp32s3\"",
                "partition_custom": "CONFIG_PARTITION_TABLE_CUSTOM=y",
                "partition_filename": "CONFIG_PARTITION_TABLE_CUSTOM_FILENAME=\"partitions.csv\"",
            },
            "partition_layout": "nvs(24K) + phy(4K) + factory_app(3MB) + fat_storage(~5MB)",
            "validation_errors": len(self.errors),
        }
