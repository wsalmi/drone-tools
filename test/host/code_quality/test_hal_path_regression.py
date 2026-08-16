#!/usr/bin/env python3
"""
Regression test for CQR-BUILD-001: Host build references components/hw_hal.

Validates that test/host/CMakeLists.txt uses the correct component path
(components/hw_hal) and never references the nonexistent components/hal.
Also validates that the host project configures and builds with exit code zero.

Feature: code-quality-review
Property 3: Every canonical reference resolves
Validates: Requirements 3.1, 3.5, 12.1, 12.4
"""

import re
import subprocess
import shutil
import tempfile
from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).resolve().parents[3]
HOST_CMAKE = REPO_ROOT / "test" / "host" / "CMakeLists.txt"
COMPONENTS_DIR = REPO_ROOT / "components"


# ---------------------------------------------------------------------------
# Static checks: CMakeLists.txt content validation
# ---------------------------------------------------------------------------

class TestHalPathStatic:
    """Static analysis of CMakeLists.txt for HAL component path references."""

    def test_cmakelists_exists(self):
        """test/host/CMakeLists.txt must exist."""
        assert HOST_CMAKE.exists(), f"Missing: {HOST_CMAKE}"

    def test_hw_hal_component_directory_exists(self):
        """components/hw_hal directory must exist in the repository."""
        hw_hal_dir = COMPONENTS_DIR / "hw_hal"
        assert hw_hal_dir.is_dir(), f"Missing directory: {hw_hal_dir}"

    def test_hw_hal_include_directory_exists(self):
        """components/hw_hal/include directory must exist."""
        include_dir = COMPONENTS_DIR / "hw_hal" / "include"
        assert include_dir.is_dir(), f"Missing directory: {include_dir}"

    def test_nonexistent_hal_directory_absent(self):
        """components/hal must NOT exist (only hw_hal exists)."""
        bad_dir = COMPONENTS_DIR / "hal"
        assert not bad_dir.exists(), (
            f"Unexpected directory {bad_dir} exists — "
            "the component was renamed to hw_hal"
        )

    def test_no_reference_to_components_hal(self):
        """
        Regression CQR-BUILD-001: CMakeLists.txt must NOT reference
        ${COMPONENTS_DIR}/hal/ (the nonexistent path).

        This pattern matches /hal/ but not /hw_hal/ to avoid false positives.
        """
        content = HOST_CMAKE.read_text()
        # Match COMPONENTS_DIR}/hal/ but NOT COMPONENTS_DIR}/hw_hal/
        pattern = re.compile(
            r'\$\{COMPONENTS_DIR\}/hal/',
            re.MULTILINE,
        )
        matches = pattern.findall(content)
        assert matches == [], (
            f"Found {len(matches)} reference(s) to nonexistent "
            f"${{COMPONENTS_DIR}}/hal/ in {HOST_CMAKE.name}. "
            "The correct path is ${COMPONENTS_DIR}/hw_hal/. "
            "Affected lines:\n" +
            "\n".join(
                f"  {i+1}: {line.strip()}"
                for i, line in enumerate(content.splitlines())
                if "${COMPONENTS_DIR}/hal/" in line
                and "${COMPONENTS_DIR}/hw_hal/" not in line
            )
        )

    def test_references_hw_hal_include(self):
        """CMakeLists.txt must reference ${COMPONENTS_DIR}/hw_hal/include."""
        content = HOST_CMAKE.read_text()
        assert "${COMPONENTS_DIR}/hw_hal/include" in content, (
            "Expected ${COMPONENTS_DIR}/hw_hal/include in CMakeLists.txt"
        )

    def test_hal_mocks_uses_hw_hal_include(self):
        """
        The hal_mocks target include directories must reference hw_hal.
        This was the original compilation failure point.
        """
        content = HOST_CMAKE.read_text()
        # Find the hal_mocks section
        hal_mocks_start = content.find("add_library(hal_mocks")
        assert hal_mocks_start != -1, "hal_mocks target not found"
        # Find the next target_include_directories after hal_mocks
        include_start = content.find(
            "target_include_directories(hal_mocks", hal_mocks_start
        )
        assert include_start != -1, (
            "target_include_directories for hal_mocks not found"
        )
        # Extract until closing paren
        section_end = content.find(")", include_start)
        section = content[include_start:section_end]
        assert "${COMPONENTS_DIR}/hw_hal/include" in section, (
            "hal_mocks target_include_directories must use hw_hal/include"
        )
        assert "${COMPONENTS_DIR}/hal/include" not in section, (
            "hal_mocks must NOT reference the nonexistent hal/include"
        )


# ---------------------------------------------------------------------------
# Build validation: configure and build the host project
# ---------------------------------------------------------------------------

class TestHalPathBuild:
    """Validate that the host project configures and builds successfully."""

    @pytest.fixture(scope="class")
    def build_dir(self, tmp_path_factory):
        """Create a temporary build directory for CMake configure+build."""
        build = tmp_path_factory.mktemp("host_build")
        return build

    def test_host_configure_succeeds(self, build_dir):
        """
        CMake configure of test/host must succeed (exit code 0).
        This proves that components/hw_hal/include resolves.
        """
        result = subprocess.run(
            ["cmake", "-S", str(REPO_ROOT / "test" / "host"), "-B", str(build_dir)],
            capture_output=True,
            text=True,
            timeout=120,
        )
        assert result.returncode == 0, (
            f"CMake configure failed (exit {result.returncode}):\n"
            f"stderr: {result.stderr[-2000:]}"
        )

    def test_host_build_succeeds(self, build_dir):
        """
        CMake build of test/host must succeed (exit code 0).
        This was previously failing with 'hal_lora.h' file not found.
        """
        result = subprocess.run(
            ["cmake", "--build", str(build_dir)],
            capture_output=True,
            text=True,
            timeout=300,
        )
        assert result.returncode == 0, (
            f"Host build failed (exit {result.returncode}):\n"
            f"stderr: {result.stderr[-2000:]}"
        )

    def test_hal_lora_header_found(self):
        """
        Verify that hal_lora.h exists in the hw_hal include directory.
        This is the file whose absence caused the original build failure.
        """
        header = COMPONENTS_DIR / "hw_hal" / "include" / "hal_lora.h"
        assert header.exists(), (
            f"hal_lora.h not found at {header} — "
            "this was the file causing the original build failure"
        )


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    pytest.main([__file__, "-v"])
