import unreal,sys
from pathlib import Path
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
sys.path.insert(0,str(Path(__file__).resolve().parent))
import stage3_checks
