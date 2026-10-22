"""Launch with UnrealEditor -ExecutePythonScript=<absolute path to this file>.
Runs saved-player PIE checks without screenshots, writes a JSON result, then closes Unreal.
"""
import sys
from pathlib import Path
import unreal
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
sys.path.insert(0,str(Path(__file__).resolve().parent))
import stage2_checks
