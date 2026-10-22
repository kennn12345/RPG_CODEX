"""Which skeleton the generator builds for. Set RPG_SKEL=uefn for the GASP player (SK_UEFN_Mannequin);
unset (or 'manny') for the old UE5 Manny (SK_Mannequin). Each skeleton keeps its own dump, clip file and import folder."""
import os
NAME = os.environ.get('RPG_SKEL', 'manny').lower()
_P = {
    'manny': dict(mesh='/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple',
                  idle='/Game/Characters/Mannequins/Anims/Unarmed/MM_Idle',
                  skeleton='/Game/Characters/Mannequins/Meshes/SK_Mannequin',
                  folder='/Game/RPG/Animations/Prototype', suffix=''),
    'uefn': dict(mesh='/Game/Characters/UEFN_Mannequin/Meshes/SKM_UEFN_Mannequin',
                 idle='/Game/Characters/UEFN_Mannequin/Animations/Idle/M_Neutral_Stand_Idle_Loop',
                 skeleton='/Game/Characters/UEFN_Mannequin/Meshes/SK_UEFN_Mannequin',
                 folder='/Game/RPG/Animations/SK_UEFN_Mannequin', suffix='_uefn'),
}
P = _P[NAME]
HERE = os.path.dirname(os.path.abspath(__file__))
def path(base, ext):   # skeleton_dump.json / skeleton_dump_uefn.json ...
    return os.path.join(HERE, base + P['suffix'] + ext)
