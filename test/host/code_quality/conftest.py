"""Configure sys.path for code_quality validator tests."""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
tools_path = str(REPO_ROOT / "tools")
if tools_path not in sys.path:
    sys.path.insert(0, tools_path)
