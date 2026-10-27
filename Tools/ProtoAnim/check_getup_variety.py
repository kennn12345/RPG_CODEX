"""Validate the two recorded recoveries; procedural variants are retired."""
import json
import subprocess
import sys
from pathlib import Path
import protoanim as p

assert p.SP.NAME == 'uefn', 'Use RPG_SKEL=uefn'
root=Path(__file__).resolve().parent
allowed={'RPG_GetUp_FaceDown_Capture8614','RPG_GetUp_FaceUp_Capture8611'}
for filename in ['clips_uefn.json','getup_variety_uefn.json','ue_import_report_uefn.json']:
    data=json.loads((root/filename).read_text())
    assert all(not name.startswith('RPG_GetUp_') or name in allowed for name in data),(filename,'Retired get-up')
for filename,name in [('capture_getups_uefn.json','RPG_GetUp_FaceDown_Capture8614'),('capture8611_getups_uefn.json','RPG_GetUp_FaceUp_Capture8611')]:
    subprocess.run([sys.executable,str(root/'check_capture_getup.py'),str(root/filename),name],check=True)
print('PASS: mocap-only get-up manifests and both captured animation bakes')
