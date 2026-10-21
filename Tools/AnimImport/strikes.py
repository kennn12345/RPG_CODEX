# Finds strikes in ramsterz_analysis.json: moments a hand / foot reaches furthest forward (local max of reach in front
# of the pelvis), with the peak speed on the way out and when the wind-up began. Manny faces +Y in mesh space.
import json, sys
data = json.load(open('ramsterz_analysis.json'))
for name, clip in sorted(data.items()):
    rows = clip['rows']
    if 'Idle' in name: continue
    print('== %s  %.2fs  %d frames' % (name, clip['length'], clip['frames']))
    for limb in ['hand_r', 'hand_l', 'foot_r', 'foot_l']:
        reach = [r[limb][1] - r['pelvis'][1] for r in rows]  # forward of the pelvis, cm
        height = [r[limb][2] for r in rows]
        base = sorted(reach)[len(reach)//4]
        for i in range(3, len(rows) - 3):
            if reach[i] == max(reach[i-3:i+4]) and reach[i] - base > (25 if 'hand' in limb else 40):
                j = i
                while j > 0 and reach[j-1] < reach[j]: j -= 1   # start of the outward move
                k = i
                while k < len(rows)-1 and reach[k+1] < reach[k] + 0.5 and reach[k] - reach[k+1] < 999: k += 1
                k = min(k, len(rows)-1)
                vmax = max(r[limb + '_v'] for r in rows[j:i+1])
                print('  %-6s out %.2fs -> contact %.2fs (reach %+.0f cm, %.0f cm high, %.0f cm/s) -> back %.2fs' % (
                    limb, rows[j]['t'], rows[i]['t'], reach[i], height[i], vmax, rows[k]['t']))
