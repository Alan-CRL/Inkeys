"""Driver that replays Draw3.DrawingController.cpp updateCanvasNavigation (L13523-L13910) for
synthetic contact streams, calling the exact-port functions in navport.py.

Faithful pieces:
  * per contact only the *latest* mailbox snapshot is consumed per frame (TryReadSnapshot L13571;
    unchanged sequence skipped via ShouldConsumeCanvasPanContactSnapshot)
  * centroid / velocityCentroid / previousPanCentroid handling (L13620-L13717)
  * Up only commits final displacement, never a velocity sample (L13599-L13615)
  * topology change -> rebuildPanContactBaseline(max(releaseQpc,newestPosQpc), true) ->
    ResetCanvasPanVelocitySamples (L13768-L13772 + L11865-L11890)
  * inertia stepping with dt = nowQpc-lastNavigationQpc (L13789-L13801)
  * release: CanvasPanReleaseAgeSeconds(panReleaseQpc,lastPanInputQpc) + EndCanvasPan (L13845-L13908)
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))  # python -I drops the script dir

import math

import navport as N
from navport import QPC_FREQ


class Contact:
    def __init__(self, events):
        self.ev = events
        self.i = 0                 # events with q <= T are visible
        self.snapshot = None
        self.vpos = 0.0
        self.last_seq = 0
        self.alive = False
        self.pan = False
        self.pending_up = False

    def latest(self, T):
        while self.i < len(self.ev) and self.ev[self.i].q <= T:
            self.i += 1
        return self.ev[self.i - 1] if self.i > 0 else None


def run_current(contact_events, frames, pen_hover_at=None, max_frames=2500, begin_with=None,
                motion=None, inherit=False, until_q=None):
    """contact_events: list of event lists (one per finger).  Pan starts when the LAST finger's
    Down is dequeued (two-finger entry); with a single list it starts at that finger's Down.
    Returns a dict of metrics."""
    m = motion or N.Motion()
    contacts = [Contact(ev) for ev in contact_events]
    begin_q = max(c.ev[0].q for c in contacts)
    res = dict(peak_direct=0.0, peak_active=0.0, first_emit=None, pan_dist=0.0, glide=0.0,
               release_speed=0.0, selected_speed=0.0, release_age_ms=None, release_src='none',
               n_samples_release=0, max_inertia_step=0.0, release_q=None, release_v=(0.0, 0.0),
               trace=[], released=False, topology_events=0, first_dt_ms=None, released_frame=None)
    pan_active = False
    centroid_valid = False
    prev_centroid = 0.0
    prev_count = 0
    last_nav = 0
    last_pan_input = 0
    last_release_q = 0
    released = False
    frame_no = 0
    for T in frames:
        frame_no += 1
        if last_nav == 0:
            last_nav = T - int(QPC_FREQ / 120)   # first frame: nominal one-frame dt (harness artefact guard)
        if frame_no > max_frames:
            break
        # ---- ingress: Down dequeue (drainIngressBatch happens before updateCanvasNavigation)
        if not pan_active and not released and T >= begin_q:
            for c in contacts:
                c.alive = True
                c.pan = True
                if c.ev[0].q == begin_q:
                    snap = c.ev[0]            # Down anchor (DownSnapshot) for the joining finger
                    c.i = max(c.i, 1)
                else:
                    snap = c.latest(T)        # handoff anchor: current snapshot of the promoted finger
                c.snapshot = snap
                c.vpos = snap.x
                c.last_seq = snap.seq
            N.begin_pan(m, inherit, begin_q)
            last_pan_input = begin_q
            pan_active = True
            prev_centroid = sum(c.snapshot.x for c in contacts) / len(contacts)
            prev_count = len(contacts)
            centroid_valid = True
        # ---- updateCanvasNavigation(nowQpc = T)
        topology_changed = False
        pan_pos = pan_vel = False
        newest_pos_q = newest_vel_q = release_q = 0
        if pan_active or any(c.alive for c in contacts):
            for c in contacts:
                if not c.alive:
                    continue
                snap = c.latest(T)
                if snap is None or snap.seq == c.last_seq:
                    continue
                prev = c.snapshot
                c.last_seq = snap.seq
                c.snapshot = snap
                if snap.kind == 'move':
                    if c.pan:
                        c.vpos = snap.x
                        pan_pos = pan_vel = True
                        newest_pos_q = max(newest_pos_q, snap.q)
                        newest_vel_q = max(newest_vel_q, snap.q)
                else:  # up
                    if c.pan:
                        if snap.x != prev.x:
                            pan_pos = True
                            newest_pos_q = max(newest_pos_q, snap.q)
                        release_q = max(release_q, snap.q)
        content = 0.0
        if pan_active:
            pcs = [c for c in contacts if c.alive and c.pan]
            if pcs:
                cen = sum(c.snapshot.x for c in pcs) / len(pcs)
                vcen = sum(c.vpos for c in pcs) / len(pcs)
                cnt = len(pcs)
                if (not centroid_valid) or cnt != prev_count:
                    prev_centroid = cen
                    centroid_valid = True
                    prev_count = cnt
                elif pan_pos:
                    cdelta = cen - prev_centroid
                    upd = pan_vel
                    iq = newest_vel_q if upd else newest_pos_q
                    vdelta = (vcen - prev_centroid) if upd else 0.0
                    before = m.last_vel_sample_qpc
                    content = N.update_pan(m, (cdelta, 0.0), (vdelta, 0.0), iq, QPC_FREQ, upd)[0]
                    res['pan_dist'] += content
                    if upd and m.last_vel_sample_qpc != before:
                        sp = math.hypot(*m.direct)
                        if res['first_emit'] is None:
                            res['first_emit'] = sp
                            res['first_dt_ms'] = (m.samples[-1][0] - m.samples[0][0]) / QPC_FREQ * 1000
                    prev_centroid = cen
                if m.last_vel_sample_qpc > 0:
                    last_pan_input = m.last_vel_sample_qpc
                res['peak_direct'] = max(res['peak_direct'], math.hypot(*m.direct))
                res['peak_active'] = max(res['peak_active'], m.speed())
        # ---- Up retirement
        for c in contacts:
            if c.alive and c.snapshot.kind == 'up':
                c.alive = False
                if c.pan:
                    topology_changed = True
        remaining = [c for c in contacts if c.alive]
        if pan_active and not remaining:
            pan_active = False
        if pan_active and topology_changed:
            res['topology_events'] += 1
            rem_pan = [c for c in remaining if c.pan]
            for c in rem_pan:
                c.vpos = c.snapshot.x
            prev_centroid = sum(c.snapshot.x for c in rem_pan) / len(rem_pan)
            prev_count = len(rem_pan)
            centroid_valid = True
            N.reset_pan_velocity_samples(m, max(release_q, newest_pos_q))
        elif m.inertia_active:
            dt = (T - last_nav) / QPC_FREQ
            pen = pen_hover_at is not None and T >= pen_hover_at
            d = N.step_inertia(m, dt if dt > 0 else 1 / 120, pen)[0]
            res['glide'] += d
            res['max_inertia_step'] = max(res['max_inertia_step'], abs(d))
            res['peak_active'] = max(res['peak_active'], m.speed())
            content = d
        res['trace'].append((T, content))
        if (not pan_active) and centroid_valid:
            centroid_valid = False
            prev_count = 0
            age = N.release_age_seconds(release_q, last_pan_input, QPC_FREQ, False)
            N.end_pan(m, age)
            res['release_speed'] = m.speed()
            res['selected_speed'] = math.hypot(*m.selected)
            res['release_v'] = m.velocity
            res['release_age_ms'] = age * 1000 if math.isfinite(age) else float('inf')
            res['release_src'] = m.release_src
            res['n_samples_release'] = len(m.samples)
            res['release_q'] = release_q
            res['released'] = True
            res['released_frame'] = frame_no
            released = True
            last_release_q = release_q
        last_nav = T
        if released and not m.inertia_active and not pan_active:
            break
        if until_q is not None and released and T >= until_q:
            break
    res['m'] = m
    return res


def quantiles(vals, qs=(0.5, 0.9, 0.95, 0.99, 1.0)):
    if not vals:
        return [float('nan')] * len(qs)
    s = sorted(vals)
    out = []
    for q in qs:
        idx = min(len(s) - 1, int(math.ceil(q * len(s))) - 1)
        out.append(s[max(0, idx)])
    return out
