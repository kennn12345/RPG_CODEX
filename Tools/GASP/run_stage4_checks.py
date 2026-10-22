"""Run saved-player montage/layer/proxy checks; creates only transient PIE fixtures and no screenshots."""
import sys,unreal
from pathlib import Path
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
sys.path.insert(0,str(Path(__file__).resolve().parent))
import stage4_checks
