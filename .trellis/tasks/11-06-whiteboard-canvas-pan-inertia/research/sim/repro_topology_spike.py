"""Deterministic (no randomness) repro of the topology-reset spike, using only the exact port.

Two fingers move at a constant 3000 DIP/s (3 DIP per ms).  QPC unit = 1 ms (freq 1000).
Finger A lifts at t=236 ms; the controller rebuilds the baseline from the remaining finger's LAST
CONSUMED snapshot (t=231 ms, pos=693 DIP) but stamps the reset zero-point with max(Up.qpc, lastUpdate)
= 236 ms.  The next Move of finger B (t=239 ms, pos=717) is then divided by 3 ms instead of 8 ms.

python -I repro_topology_spike.py
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))  # python -I drops the script dir
import navport as N

F = 1000
m = N.Motion()
N.begin_pan(m, False, 0)
pos_prev = 0.0
for t in range(7, 232, 8):                       # 8 ms report cadence (7,15,...,231), constant 3000 DIP/s
    pos = 3.0 * t
    N.update_pan(m, (pos - pos_prev, 0), (pos - pos_prev, 0), t, F)
    pos_prev = pos
print('before topology change: direct =', round(m.direct[0]), 'DIP/s (true 3000), samples =', len(m.samples))
# --- finger A Up at 236 ms  => rebuildPanContactBaseline(..., true) => ResetCanvasPanVelocitySamples
N.reset_pan_velocity_samples(m, max(236, m.last_update_qpc))
baseline_pos = pos_prev                           # remaining finger's last consumed snapshot (t=231 ms -> 693)
# --- finger B next report: t=239 ms, true position 3*239 = 717
pos = 3.0 * 239
N.update_pan(m, (pos - baseline_pos, 0), (pos - baseline_pos, 0), 239, F)
print('first Move after reset: delta =', pos - baseline_pos, 'DIP over 3 ms (reset zero-point 236) ->',
      round(m.direct[0]), 'DIP/s  =', round(m.direct[0] / 3000.0, 2), 'x true')
# --- finger B lifts at 247 ms after one more sample -> EndCanvasPan uses this 3-sample estimate
pos2 = 3.0 * 247
N.update_pan(m, (pos2 - pos, 0), (pos2 - pos, 0), 247, F)
N.end_pan(m, 0.009)
print('after 2nd Move + release: release velocity =', round(m.velocity[0]), 'DIP/s =', round(m.velocity[0] / 3000.0, 2), 'x true;',
      'glide =', round(m.velocity[0] ** 2 / (2 * N.DECEL)), 'DIP vs correct', round(3000 ** 2 / (2 * N.DECEL)), 'DIP')
