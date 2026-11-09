#!/usr/bin/env python3
"""Run the #907 review-model counterexamples; no public receipt ABI is enabled."""
from pathlib import Path
import subprocess
import sys
ROOT = Path(__file__).resolve().parents[1]
subprocess.run([sys.executable, str(ROOT / 'tests/cuis/publication_receipt.py')], check=True, cwd=ROOT)
