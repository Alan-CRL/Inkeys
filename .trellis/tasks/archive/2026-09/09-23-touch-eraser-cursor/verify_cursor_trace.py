"""检查产品实际输出的光标事件链；0=未观察到目标链，1=需复核，2=证据不完整。

不模拟输入判定，也不把检查通过等同设备验收通过。支持原始日志与 schema=2。
"""

import argparse
import json
import re
import unittest
from pathlib import Path


RECORD = re.compile(r"\[CURSOR_TRACE\] seq=(\d+) tick=(\d+) thread=(\d+) (.*)")
FIELD = re.compile(r"(\w+)=([^ ]+)")


def analyze(text):
    findings = []
    incomplete = []
    previous = 0
    touch_sessions = 0
    touch_count = 0
    touch_pending = False
    last_touch = None
    suspect = None
    reported = set()
    frame = None
    primary_opacity = None
    terminal_present = False
    raw_states = []
    raw_events = 0
    # schema=2 的 before/filter/decision/after 必须配对，检测中途复制或尾部截断。
    events = {}

    def report(kind, seq):
        key = (suspect, kind)
        if key not in reported:
            findings.append({"kind": kind, "seq": seq, "takeover_seq": suspect})
            reported.add(key)

    for line in text.splitlines():
        if "[CURSOR_TRACE]" not in line:
            continue
        if "dropped=" in line or "[truncated]" in line:
            incomplete.append(line)
        match = RECORD.search(line)
        if not match:
            if "dropped=" not in line:
                incomplete.append("malformed record: " + line)
            continue
        seq = int(match[1])
        if seq != previous + 1:
            incomplete.append(f"sequence gap: {previous} -> {seq}")
        previous = seq
        body = match[4]
        if not body.strip():
            incomplete.append(f"empty record: {seq}")
            continue
        kind = body.split()[0]
        fields = dict(FIELD.findall(body))
        required_fields = {
            "mouse": {"message", "accepted", "source", "x", "y"},
            "touch-begin": {"tracked", "count"},
            "touch-end": {"count"},
            "present": {"success"},
            "visual": {"source", "opacity"},
        }.get(kind, set())
        if kind == "frame" and fields.get("selection") != "1":
            required_fields = {"primary", "liveTouch"}
        if not required_fields <= fields.keys():
            incomplete.append(f"seq {seq} missing fields {sorted(required_fields - fields.keys())}")
            continue

        def integer(name, default=0):
            value = fields.get(name)
            if value is None:
                return default
            return int(value, 16) if value.startswith("0x") else int(value)

        if "event" in fields:
            key = (match[3], fields["event"])
            parts = events.setdefault(key, set())
            parts.add(fields.get("phase") if kind == "mouse-state" else kind)
            if kind == "mouse-state" and fields.get("phase") == "after":
                required = {"mouse-source", "before", "after"}
                required |= {"mouse-wheel"} if "mouse-wheel" in parts else {"mouse", "mouse-filter"}
                if not required <= parts:
                    incomplete.append(f"event {key} missing {sorted(required - parts)}")
                del events[key]

        if kind == "raw-registration":
            raw_states.append({"seq": seq, **fields})
        elif kind == "raw-mouse":
            raw_events += 1
        elif kind == "touch-position":
            last_touch = (integer("x"), integer("y"))
        elif kind == "touch-begin":
            if integer("tracked"):
                touch_count = integer("count")
                if touch_count == 1:
                    touch_sessions += 1
                    suspect = None
                    terminal_present = False
                    touch_pending = not integer("keepMouse")
        elif kind == "touch-end":
            touch_count = integer("count")
        elif kind == "pen-sample":
            touch_pending = False
            suspect = None
        elif kind == "mouse-wheel" and integer("accepted") and integer("source") in (2, 16):
            touch_pending = False
            suspect = None
        elif kind == "mouse":
            point = (integer("x"), integer("y"))
            source = integer("source")
            extra = integer("extra")
            if source == 4 or (source == 0 and integer("promoted") and extra & 0x80):
                last_touch = point
            if integer("accepted"):
                if source in (2, 16):  # WinUser.h: IMDT_MOUSE / IMDT_TOUCHPAD。
                    touch_pending = False
                    suspect = None
                elif source == 0 and integer("message") == 0x200:
                    if touch_pending and point == last_touch and suspect is None:
                        suspect = seq
                        report("unattributed-touch-takeover", seq)
        elif kind == "frame":
            frame = fields
            primary_opacity = None
        elif kind == "visual" and fields.get("source") == "primary":
            primary_opacity = float(fields["opacity"])
        elif kind == "present" and integer("success") and frame:
            if touch_sessions and touch_count == 0:
                terminal_present = True
            if suspect and int(frame.get("primary", "0")):
                if int(frame.get("liveTouch", "0")):
                    report("primary-during-touch", seq)
                elif primary_opacity is not None:
                    report("pressed-after-up" if primary_opacity >= 0.99 else "hover-after-up", seq)

    if not previous or not touch_sessions:
        incomplete.append("no complete touch scenario")
    if touch_count or not terminal_present:
        incomplete.append("missing touch terminal/present")
    if events:
        incomplete.append(f"unfinished mouse events: {sorted(events)}")
    return {
        "complete": not incomplete,
        "records": previous,
        "touch_sessions": touch_sessions,
        "findings": findings,
        "incomplete": incomplete,
        "raw_events": raw_events,
        "raw_registration": raw_states,
    }


def fixture(events):
    return "\n".join(f"[CURSOR_TRACE] seq={i} tick={i} thread=1 {event}"
                     for i, event in enumerate(events, 1))


class TraceTests(unittest.TestCase):
    def test_pressed_to_hover_chain(self):
        events = [
            "touch-begin tracked=1 count=1 keepMouse=0",
            "mouse message=0x0200 accepted=0 source=4 x=1082 y=932",
            "mouse message=0x0200 accepted=1 source=0 x=1082 y=932 buttonDown=1",
            "frame primary=1 touchVisuals=1 liveTouch=1",
            "visual source=primary opacity=1.000",
            "present success=1",
            "touch-end count=0",
            "frame primary=1 touchVisuals=0 liveTouch=0",
            "visual source=primary opacity=1.000",
            "present success=1",
            "mouse message=0x0200 accepted=1 source=0 x=2378 y=993 buttonDown=0",
            "frame primary=1 touchVisuals=0 liveTouch=0",
            "visual source=primary opacity=0.500",
            "present success=1",
        ]
        result = analyze(fixture(events))
        self.assertTrue(result["complete"])
        self.assertEqual([f["kind"] for f in result["findings"]], [
            "unattributed-touch-takeover", "primary-during-touch", "pressed-after-up", "hover-after-up"])

    def test_real_mouse_and_pen_takeover(self):
        for takeover in ("mouse message=0x0200 accepted=1 source=2 x=10 y=20",
                         "mouse message=0x0200 accepted=1 source=16 x=10 y=20",
                         "pen-sample contact=0 x=10 y=20"):
            with self.subTest(takeover=takeover):
                result = analyze(fixture([
                    "touch-begin tracked=1 count=1 keepMouse=0",
                    "mouse message=0x0200 accepted=0 source=4 x=10 y=20",
                    takeover,
                    "frame primary=1 liveTouch=1", "present success=1",
                    "touch-end count=0",
                    "frame primary=1 liveTouch=0", "present success=1",
                ]))
                self.assertTrue(result["complete"])
                self.assertFalse(result["findings"])

    def test_filtered_move_and_incomplete_trace(self):
        clean = fixture([
            "touch-begin tracked=1 count=1 keepMouse=0", "touch-end count=0",
            "mouse message=0x0200 accepted=0 source=0 x=10 y=20",
            "frame primary=0 liveTouch=0", "present success=1",
        ])
        self.assertTrue(analyze(clean)["complete"])
        self.assertFalse(analyze(clean)["findings"])
        for broken in (clean.replace("seq=3", "seq=9"), clean + "\n[CURSOR_TRACE] dropped=1",
                       clean + " [truncated]", clean.replace("source=0", ""),
                       fixture(["touch-begin tracked=1 count=1"]), ""):
            self.assertFalse(analyze(broken)["complete"])

    def test_multitouch_terminal(self):
        result = analyze(fixture([
            "touch-begin tracked=1 count=1 keepMouse=0",
            "touch-begin tracked=1 count=2 keepMouse=0",
            "frame primary=0 liveTouch=2", "present success=1",
            "touch-end count=1", "frame primary=0 liveTouch=1", "present success=1",
            "touch-end count=0", "frame primary=0 liveTouch=0", "present success=1",
        ]))
        self.assertTrue(result["complete"])
        self.assertEqual(result["touch_sessions"], 1)
        self.assertFalse(result["findings"])

    def test_schema_two_pairing(self):
        events = [
            "touch-begin tracked=1 count=1 keepMouse=0", "touch-end count=0",
            "mouse-source event=1 api=1 ok=1 device=0 origin=0",
            "mouse-state event=1 phase=before suppressed=1",
            "mouse-filter event=1 touchKnown=1 touchX=10 touchY=20",
            "mouse event=1 message=0x0200 accepted=0 source=0 x=10 y=20",
            "mouse-state event=1 phase=after suppressed=1",
            "frame primary=0 liveTouch=0", "present success=1",
        ]
        self.assertTrue(analyze(fixture(events))["complete"])
        self.assertFalse(analyze(fixture([e for e in events if "phase=after" not in e]))["complete"])


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", nargs="?", type=Path)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(TraceTests))
        raise SystemExit(0 if result.wasSuccessful() else 1)
    if not args.log:
        parser.error("provide a log path or --self-test")
    try:
        result = analyze(args.log.read_text(encoding="utf-8-sig"))
    except (ValueError, KeyError) as error:
        print(json.dumps({"complete": False, "parse_error": str(error)}))
        raise SystemExit(2)
    print(json.dumps(result, ensure_ascii=False, indent=2))
    raise SystemExit(2 if not result["complete"] else 1 if result["findings"] else 0)
