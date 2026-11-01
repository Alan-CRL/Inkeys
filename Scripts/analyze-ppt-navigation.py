"""Analyze opt-in PptPerf v1 logs without Office, GUI, or third-party packages.

Usage: python Scripts/analyze-ppt-navigation.py <capture-folder> --json summary.json
The measured intervals are diagnostic boundaries, not optical slide-change time.
"""

import argparse
from dataclasses import dataclass
import json
from pathlib import Path
import re
import sys
import unittest


@dataclass
class Span:
    begin: dict
    end: dict | None
    valid: bool = True

    @property
    def key(self):
        return self.begin['source'], self.begin['pid'], self.begin['span']

    @property
    def duration_ms(self):
        if not self.valid or self.end is None:
            return None
        return (self.end['qpc'] - self.begin['qpc']) * 1000 / self.begin['frequency']


def parse_lines(lines):
    events, issues, seen = [], [], set()
    required = {'v', 'source', 'pid', 'tid', 'qpc', 'frequency', 'span', 'parent', 'event', 'stage', 'op'}
    for number, line in enumerate(lines, 1):
        if '[PptPerf]' not in line:
            continue
        fields = dict(re.findall(r'(\w+)=([^\s]+)', line.split('[PptPerf]', 1)[1]))
        try:
            if not required <= fields.keys() or fields['source'] not in ('native', 'managed'):
                raise ValueError('missing or unsupported fields')
            for key in ('v', 'pid', 'tid', 'qpc', 'frequency', 'span', 'parent'):
                fields[key] = int(fields[key])
            if fields['v'] != 1 or min(fields['pid'], fields['tid'], fields['frequency']) <= 0:
                raise ValueError('unsupported schema or clock')
            if min(fields['qpc'], fields['span'], fields['parent']) < 0:
                raise ValueError('negative identity or timestamp')
            if fields['event'] not in ('begin', 'end', 'instant'):
                raise ValueError('unsupported event')
            if fields['event'] != 'instant' and fields['span'] == 0:
                raise ValueError('zero span identity')
        except (ValueError, KeyError) as error:
            issues.append(f'line {number}: malformed PptPerf record ({error})')
            continue
        identity = tuple(sorted(fields.items()))
        if identity not in seen:
            seen.add(identity)
            events.append(fields)
    return events, issues


def analyze(lines):
    events, issues = parse_lines(lines)
    frequencies = {}
    for event in events:
        frequencies.setdefault(event['pid'], set()).add(event['frequency'])
    invalid_pids = {pid for pid, values in frequencies.items() if len(values) != 1}
    issues.extend(f'pid {pid}: conflicting QPC frequencies; intervals are unknown' for pid in sorted(invalid_pids))
    grouped = {}
    instants = []
    for event in events:
        if event['event'] == 'instant':
            instants.append(event)
        else:
            grouped.setdefault((event['source'], event['pid'], event['span']), []).append(event)
    spans = []
    for key, records in grouped.items():
        begins = [record for record in records if record['event'] == 'begin']
        ends = [record for record in records if record['event'] == 'end']
        if len(begins) != 1 or len(ends) > 1:
            issues.append(f'{key}: ambiguous or missing begin/end')
            continue
        begin, end = begins[0], ends[0] if ends else None
        valid = begin['pid'] not in invalid_pids
        identity_fields = (('request', 'show_session', 'binding') if begin['source'] == 'managed'
                           else ('epoch', 'local_session'))
        if end is None:
            issues.append(f'{key}: missing end (capture incomplete or operation still active)')
        elif (any(begin[k] != end[k] for k in ('stage', 'op', 'parent', 'frequency'))
              or any(begin.get(k) != end.get(k) for k in identity_fields)
              or end['qpc'] < begin['qpc']):
            valid = False
            issues.append(f'{key}: incompatible span boundaries or identity')
        spans.append(Span(begin, end, valid))
    by_key = {span.key: span for span in spans}
    child_spans = {}
    for span in spans:
        parent_key = span.begin['source'], span.begin['pid'], span.begin['parent']
        if span.begin['parent']:
            child_spans.setdefault(parent_key, []).append(span)
            if (span.begin['source'] == 'managed'
                    and span.begin['stage'].startswith(('observe-', 'read-'))
                    and parent_key not in by_key):
                issues.append(f'{span.key}: missing maintenance parent; child attribution is unknown')

    def belongs(fields, parent):
        if fields['source'] != parent.begin['source'] or fields['pid'] != parent.begin['pid']:
            return False
        visited = set()
        while fields['parent']:
            key = fields['source'], fields['pid'], fields['parent']
            if key == parent.key:
                return True
            if key in visited or key not in by_key:
                break
            visited.add(key)
            fields = by_key[key].begin
        return False

    def descendants(parent, stage):
        return [span for span in spans if span.begin['stage'] == stage and belongs(span.begin, parent)]

    def single(parent, stage, notes):
        candidates = descendants(parent, stage)
        if len(candidates) != 1:
            notes.append(f'{stage}: expected one span, found {len(candidates)}')
            return None
        span = candidates[0]
        if not contains(parent, span):
            notes.append(f'{stage}: incomplete or outside its parent')
            return None
        return span

    requests = []
    for request in sorted((span for span in spans if span.begin['source'] == 'native' and span.begin['stage'] == 'native-request'), key=lambda s: (s.begin['pid'], s.begin['qpc'])):
        b = request.begin
        row = {'pid': b['pid'], 'native_span': b['span'], 'op': b['op'],
               'local_session': b.get('local_session'), 'epoch': b.get('epoch'),
               'result': request.end.get('result') if request.end else None,
               'total_ms': request.duration_ms, 'native_queue_ms': None, 'native_com_ms': None,
               'managed_request_ms': None, 'managed_wait_ms': None, 'mailbox_queue_ms': None,
               'owner_queue_upper_bound_ms': None, 'owner_ms': None, 'native_focus_ms': None,
               'com_before_managed_ms': None, 'com_after_managed_ms': None,
               'owner_end_after_wait_return_ms': None, 'native_other_ms': None,
               'owner_dispatch_ms': None, 'managed_request_id': None,
               'maintenance_overlap': [], 'native_dispositions': [], 'notes': []}
        row['native_dispositions'] = [e for e in instants if e['stage'] in ('native-discard', 'native-com-error') and belongs(e, request)]
        notes = row['notes']
        queue = single(request, 'native-queue', notes)
        com = single(request, 'native-com', notes)
        focus = single(request, 'native-focus', notes)
        for name, span in [('native_queue_ms', queue), ('native_com_ms', com), ('native_focus_ms', focus)]:
            row[name] = span.duration_ms if span else None
        if all(span is not None for span in (queue, com, focus)):
            if disjoint([queue, com, focus]):
                residual = request.duration_ms - sum(span.duration_ms for span in (queue, com, focus))
                row['native_other_ms'] = max(0, residual) if residual >= -0.001 else None
            else:
                notes.append('native boundaries overlap; no residual was calculated')
        if com is not None:
            candidates = [span for span in spans if span.begin['source'] == 'managed'
                          and span.begin['stage'] == 'managed-request'
                          and span.begin['op'].lower() == b['op'].lower() and contains(com, span)]
            same_thread = [span for span in candidates if span.begin['tid'] == com.begin['tid']]
            preferred = same_thread if same_thread else candidates
            if len(preferred) != 1:
                notes.append(f'managed-request: ambiguous or absent ({len(preferred)} candidates)')
            else:
                managed = preferred[0]
                mb = managed.begin
                row['managed_request_id'] = mb.get('request')
                row['managed_result'] = managed.end.get('result')
                row['managed_request_ms'] = managed.duration_ms
                row['com_before_managed_ms'] = difference_ms(com.begin, managed.begin)
                row['com_after_managed_ms'] = difference_ms(managed.end, com.end)
                waits = descendants(managed, 'mailbox-wait')
                wait = waits[0] if len(waits) == 1 and contains(managed, waits[0]) else None
                row['managed_wait_ms'] = wait.duration_ms if wait else None
                if wait:
                    row['wait_result'] = wait.end.get('result')
                    row['wait_executing'] = wait.end.get('executing')
                    row['wait_cancelled'] = wait.end.get('cancelled')
                owners = [span for span in spans if span.begin['source'] == 'managed'
                          and span.begin['pid'] == b['pid'] and span.begin['stage'] == 'owner'
                          and mb.get('request') not in (None, '0') and span.begin.get('request') == mb['request']]
                if len(owners) == 1:
                    owner = owners[0]
                    row['owner_ms'] = owner.duration_ms
                    if wait and owner.valid and owner.begin['qpc'] >= wait.begin['qpc']:
                        row['owner_queue_upper_bound_ms'] = difference_ms(wait.begin, owner.begin)
                        if owner.end is not None:
                            row['owner_end_after_wait_return_ms'] = (difference_ms(wait.end, owner.end)
                                                                    if owner.end['qpc'] > wait.end['qpc'] else 0)
                    queue_events = {}
                    for stage in ('mailbox-enqueue', 'mailbox-dequeue'):
                        queue_events[stage] = [e for e in instants if e['source'] == 'managed' and e['pid'] == b['pid']
                                               and e.get('request') == mb.get('request') and e['stage'] == stage]
                    enqueued, dequeued = queue_events['mailbox-enqueue'], queue_events['mailbox-dequeue']
                    if len(enqueued) == 1 and len(dequeued) == 1 and b['pid'] not in invalid_pids:
                        row['mailbox_queue_ms'] = difference_ms(enqueued[0], dequeued[0])
                        if row['mailbox_queue_ms'] is None or dequeued[0]['qpc'] > owner.begin['qpc']:
                            row['mailbox_queue_ms'] = None
                            notes.append('mailbox instant order incompatible with owner begin')
                        else:
                            row['owner_dispatch_ms'] = difference_ms(dequeued[0], owner.begin)
                    elif wait:
                        notes.append('exact mailbox queue unavailable; upper bound includes lock/enqueue/wake')
                    if wait and owner.valid and b['pid'] not in invalid_pids:
                        start = enqueued[0]['qpc'] if len(enqueued) == 1 else wait.begin['qpc']
                        stop = owner.begin['qpc']
                        maintenance = [span for span in spans if span.begin['source'] == 'managed' and span.begin['pid'] == b['pid']
                                       and span.begin['tid'] == owner.begin['tid']
                                       and span.begin['stage'] in ('observe', 'maintain-binding', 'rot', 'descriptor', 'descriptor-fast', 'descriptor-full', 'annotation', 'caption')
                                       and span.duration_ms is not None and span.begin['qpc'] < stop and span.end['qpc'] > start]
                        # 维护父/子阶段不累加，避免把 ROT 同时计入 maintain-binding 两次。
                        outer = [span for span in maintenance if not any(other.key != span.key and contains(other, span)
                                 and (other.duration_ms > span.duration_ms or other.key < span.key) for other in maintenance)]
                        row['maintenance_overlap'] = [maintenance_detail(span, child_spans, start, stop, owner.begin['tid'])
                                                     for span in sorted(outer, key=lambda s: s.begin['qpc'])]
                        if any(item['breakdown_status'] == 'partial' for item in row['maintenance_overlap']):
                            notes.append('maintenance child attribution is partial; unknown durations were not summed')
                else:
                    notes.append(f'owner: ambiguous or absent ({len(owners)} candidates); no cancellation inferred')
        requests.append(row)
    status = ('no-usable-trace' if not events else 'partial' if issues or any(r['notes'] for r in requests)
              else 'no-native-navigation' if not requests else 'complete')
    return {'schema_version': 1, 'status': status,
            'event_count': len(events), 'span_count': len(spans), 'issues': issues, 'requests': requests,
            'incomplete_spans': [{'source': s.begin['source'], 'pid': s.begin['pid'], 'span': s.begin['span'], 'stage': s.begin['stage']}
                                 for s in spans if s.duration_ms is None]}


def difference_ms(start, end):
    if start is None or end is None or start['pid'] != end['pid'] or start['frequency'] != end['frequency'] or end['qpc'] < start['qpc']:
        return None
    return (end['qpc'] - start['qpc']) * 1000 / start['frequency']


def contains(parent, child):
    return (parent.duration_ms is not None and child.duration_ms is not None
            and parent.begin['pid'] == child.begin['pid'] and parent.begin['frequency'] == child.begin['frequency']
            and parent.begin['qpc'] <= child.begin['qpc'] <= child.end['qpc'] <= parent.end['qpc'])


def disjoint(spans):
    ordered = sorted(spans, key=lambda span: span.begin['qpc'])
    return all(first.end['qpc'] <= second.begin['qpc'] for first, second in zip(ordered, ordered[1:]))



def maintenance_detail(span, child_spans, start, stop, owner_tid, parent=None, ancestors=()):
    """Show each direct child separately; totals never add grandchildren again."""
    result = {'stage': span.begin['stage'], 'span': span.begin['span'],
              'total_ms': None, 'overlap_ms': None, 'children': None,
              'breakdown_status': 'unavailable', 'accounted_total_ms': None,
              'accounted_overlap_ms': None, 'unattributed_total_ms': None,
              'unattributed_overlap_ms': None, 'notes': []}
    if (start > stop or span.key in ancestors or span.duration_ms is None
            or span.begin['tid'] != owner_tid or span.end['tid'] != owner_tid
            or (parent is not None and not contains(parent, span))):
        result['breakdown_status'] = 'partial'
        result['notes'].append('incomplete, incompatible or outside maintenance parent; duration is unknown')
        return result
    result['total_ms'] = span.duration_ms
    result['overlap_ms'] = max(0, min(stop, span.end['qpc']) - max(start, span.begin['qpc'])) * 1000 / span.begin['frequency']
    direct = sorted((child for child in child_spans.get(span.key, []) if child.begin['tid'] == owner_tid),
                    key=lambda child: (child.begin['qpc'], child.key))
    if not direct:
        # 旧 v1 没有子阶段记录；未知不能伪装成零耗时或使完整聚合日志变成失败。
        return result
    result['children'] = [maintenance_detail(child, child_spans, start, stop, owner_tid, span,
                                             ancestors + (span.key,)) for child in direct]
    complete = all(child['total_ms'] is not None for child in result['children'])
    if complete and disjoint(direct):
        # 只加直接子项；getter/release 的孙级明细已包含在相应子项中。
        accounted = sum(child['total_ms'] for child in result['children'])
        overlap = sum(child['overlap_ms'] for child in result['children'])
        total_rest = result['total_ms'] - accounted
        overlap_rest = result['overlap_ms'] - overlap
        if min(total_rest, overlap_rest) >= -0.001:
            result.update(accounted_total_ms=accounted, accounted_overlap_ms=overlap,
                          unattributed_total_ms=max(0, total_rest), unattributed_overlap_ms=max(0, overlap_rest))
        else:
            complete = False
    elif complete:
        complete = False
        result['notes'].append('direct child intervals overlap; no child totals were summed')
    result['breakdown_status'] = ('complete' if complete and all(child['breakdown_status'] != 'partial'
                                                              for child in result['children']) else 'partial')
    return result


def fixture(queue=10, focus=10, timeout=False):
    records = []
    def span(source, number, parent, stage, start, end, tid, **extra):
        base = dict(v=1, source=source, pid=123, tid=tid, frequency=1000, span=number, parent=parent, stage=stage, op='Previous')
        base.update(extra)
        for event, qpc in [('begin', start), ('end', end)]:
            records.append('[PptPerf] ' + ' '.join(f'{k}={v}' for k, v in dict(base, event=event, qpc=qpc).items()))
    owner_start = 30 + queue
    owner_end = owner_start + (3000 if timeout else 140)
    wait_end = 2030 if timeout else owner_end + 2
    com_end = wait_end + 4
    span('native', 1, 0, 'native-request', 0, com_end + focus + 4, 11, epoch=16, local_session=2)
    span('native', 2, 1, 'native-queue', 0, 10, 11)
    span('native', 3, 1, 'native-business', 10, com_end + focus + 4, 12)
    span('native', 4, 3, 'native-com', 20, com_end, 12)
    span('native', 5, 3, 'native-focus', com_end + 1, com_end + focus + 1, 12)
    span('managed', 100, 0, 'managed-request', 25, wait_end + 1, 12, request=17)
    span('managed', 101, 100, 'mailbox-wait', 30, wait_end, 12, request=17, result=-1 if timeout else 1, executing=1, cancelled=0)
    span('managed', 102, 0, 'owner', owner_start, owner_end, 13, request=17)
    return records


def observe_fixture():
    lines = fixture(queue=1000)
    for number, stage, qpc in [(300, 'mailbox-enqueue', 30), (301, 'mailbox-dequeue', 1030)]:
        lines.append(f'[PptPerf] v=1 source=managed pid=123 tid=13 qpc={qpc} frequency=1000 span={number} parent=0 event=instant stage={stage} op=Previous request=17')
    return lines


def append_maintenance_span(lines, number, parent, stage, start, end, tid=13):
    base = dict(v=1, source='managed', pid=123, tid=tid, frequency=1000, span=number,
                parent=parent, stage=stage, op='Maintenance', request=0, show_session=2, binding=1)
    for event, qpc in [('begin', start), ('end', end)]:
        if qpc is not None:
            lines.append('[PptPerf] ' + ' '.join(f'{k}={v}' for k, v in dict(base, event=event, qpc=qpc).items()))


class AnalyzerTests(unittest.TestCase):
    def test_normal_and_no_double_count(self):
        row = analyze(fixture())['requests'][0]
        self.assertEqual(row['owner_ms'], 140)
        self.assertEqual(row['native_focus_ms'], 10)
        self.assertEqual(row['native_other_ms'], 14)
        self.assertIsNone(row['mailbox_queue_ms'])
        self.assertEqual(row['owner_queue_upper_bound_ms'], 10)

    def test_long_queue_and_focus(self):
        self.assertEqual(analyze(fixture(queue=1000))['requests'][0]['owner_queue_upper_bound_ms'], 1000)
        self.assertEqual(analyze(fixture(focus=1000))['requests'][0]['native_focus_ms'], 1000)

    def test_exact_queue_and_maintenance_do_not_double_count(self):
        lines = fixture(queue=1000)
        for stage, qpc in [('mailbox-enqueue', 32), ('mailbox-dequeue', 1029)]:
            lines.append(f'[PptPerf] v=1 source=managed pid=123 tid=13 qpc={qpc} frequency=1000 span={qpc} parent=0 event=instant stage={stage} op=Previous request=17')
        for number, parent, stage, start, end in [(200, 0, 'maintain-binding', 30, 1000), (201, 200, 'rot', 31, 999)]:
            for event, qpc in [('begin', start), ('end', end)]:
                lines.append(f'[PptPerf] v=1 source=managed pid=123 tid=13 qpc={qpc} frequency=1000 span={number} parent={parent} event={event} stage={stage} op=None request=0')
        row = analyze(lines)['requests'][0]
        self.assertEqual(row['mailbox_queue_ms'], 997)
        self.assertEqual(len(row['maintenance_overlap']), 1)
        self.assertEqual(row['maintenance_overlap'][0]['stage'], 'maintain-binding')


    def test_maintenance_substage_tree_counts_only_direct_children(self):
        lines = observe_fixture()
        for number, parent, stage, start, end in [
                (200, 0, 'observe', 20, 1025),
                (201, 200, 'observe-active-presentation', 21, 25),
                (202, 200, 'observe-total', 40, 440),
                (203, 202, 'read-slides-get', 40, 140),
                (204, 202, 'read-slides-count', 140, 340),
                (205, 202, 'read-slides-release', 340, 430),
                (206, 200, 'observe-temporaries-release', 900, 1000)]:
            append_maintenance_span(lines, number, parent, stage, start, end)
        row = analyze(lines)['requests'][0]
        maintenance = row['maintenance_overlap'][0]
        self.assertEqual(maintenance['total_ms'], 1005)
        self.assertEqual(maintenance['overlap_ms'], 995)
        self.assertEqual(maintenance['breakdown_status'], 'complete')
        self.assertEqual(maintenance['accounted_total_ms'], 504)
        self.assertEqual(maintenance['accounted_overlap_ms'], 500)
        self.assertEqual(maintenance['unattributed_total_ms'], 501)
        self.assertEqual(maintenance['unattributed_overlap_ms'], 495)
        self.assertEqual(maintenance['children'][0]['overlap_ms'], 0)
        total = maintenance['children'][1]
        self.assertEqual(total['total_ms'], 400)
        self.assertEqual(total['accounted_total_ms'], 390)
        self.assertEqual(total['unattributed_total_ms'], 10)
        self.assertEqual(total['children'][2]['stage'], 'read-slides-release')
        self.assertEqual(analyze(list(reversed(lines)) + lines)['requests'][0], row)

    def test_missing_maintenance_child_end_remains_unknown(self):
        lines = observe_fixture()
        append_maintenance_span(lines, 200, 0, 'observe', 20, 1025)
        append_maintenance_span(lines, 201, 200, 'observe-view-get', 40, None)
        result = analyze(lines)
        parent = result['requests'][0]['maintenance_overlap'][0]
        child = parent['children'][0]
        self.assertEqual(result['status'], 'partial')
        self.assertEqual(parent['overlap_ms'], 995)
        self.assertEqual(parent['breakdown_status'], 'partial')
        self.assertIsNone(child['total_ms'])
        self.assertIsNone(child['overlap_ms'])
        self.assertIsNone(parent['accounted_total_ms'])
        self.assertIsNone(parent['unattributed_overlap_ms'])

    def test_orphan_maintenance_child_is_not_guessed_from_time(self):
        lines = observe_fixture()
        append_maintenance_span(lines, 200, 0, 'observe', 20, 1025)
        append_maintenance_span(lines, 201, 999, 'observe-view-get', 40, 100)
        result = analyze(lines)
        parent = result['requests'][0]['maintenance_overlap'][0]
        self.assertEqual(result['status'], 'partial')
        self.assertIsNone(parent['children'])
        self.assertIsNone(parent['accounted_overlap_ms'])
        self.assertTrue(any('missing maintenance parent' in issue for issue in result['issues']))

    def test_child_outside_parent_or_conflicting_identity_is_unknown(self):
        for outside in (False, True):
            with self.subTest(outside=outside):
                lines = observe_fixture()
                append_maintenance_span(lines, 200, 0, 'observe', 20, 1025)
                append_maintenance_span(lines, 201, 200, 'observe-view-get', 1030 if outside else 40, 1100 if outside else 100)
                if not outside:
                    lines = [line.replace('binding=1', 'binding=999') if 'span=201 ' in line and 'event=end' in line else line
                             for line in lines]
                result = analyze(lines)
                parent = result['requests'][0]['maintenance_overlap'][0]
                self.assertEqual(result['status'], 'partial')
                self.assertIsNone(parent['children'][0]['total_ms'])
                self.assertIsNone(parent['accounted_total_ms'])

    def test_maintenance_children_exclude_other_threads(self):
        lines = observe_fixture()
        append_maintenance_span(lines, 200, 0, 'observe', 20, 1025)
        append_maintenance_span(lines, 201, 200, 'observe-view-get', 40, 100, tid=14)
        append_maintenance_span(lines, 202, 200, 'observe-current', 110, 120)
        parent = analyze(lines)['requests'][0]['maintenance_overlap'][0]
        self.assertEqual([child['span'] for child in parent['children']], [202])
        self.assertEqual(parent['accounted_total_ms'], 10)
        self.assertEqual(parent['accounted_overlap_ms'], 10)

    def test_legacy_maintenance_without_children_stays_valid_unknown(self):
        lines = observe_fixture()
        append_maintenance_span(lines, 200, 0, 'observe', 20, 1025)
        result = analyze(lines)
        parent = result['requests'][0]['maintenance_overlap'][0]
        self.assertEqual(result['status'], 'complete')
        self.assertEqual(parent['breakdown_status'], 'unavailable')
        self.assertIsNone(parent['children'])
        self.assertIsNone(parent['accounted_total_ms'])
        self.assertIsNone(parent['unattributed_overlap_ms'])

    def test_overlapping_direct_children_are_not_summed(self):
        lines = observe_fixture()
        append_maintenance_span(lines, 200, 0, 'observe', 20, 1025)
        append_maintenance_span(lines, 201, 200, 'observe-total', 40, 500)
        append_maintenance_span(lines, 202, 200, 'observe-current', 400, 700)
        result = analyze(lines)
        parent = result['requests'][0]['maintenance_overlap'][0]
        self.assertEqual(result['status'], 'partial')
        self.assertEqual(parent['breakdown_status'], 'partial')
        self.assertIsNone(parent['accounted_total_ms'])
        self.assertIsNone(parent['unattributed_total_ms'])

    def test_timeout_preserves_late_owner(self):
        row = analyze(fixture(timeout=True))['requests'][0]
        self.assertEqual(row['owner_ms'], 3000)
        self.assertGreater(row['owner_end_after_wait_return_ms'], 0)
        self.assertEqual(row['wait_cancelled'], '0')

    def test_other_thread_maintenance_is_not_owner_queue_work(self):
        lines = fixture(queue=1000)
        for event, qpc in [('begin', 31), ('end', 999)]:
            lines.append(f'[PptPerf] v=1 source=managed pid=123 tid=14 qpc={qpc} frequency=1000 span=200 parent=0 event={event} stage=rot op=None request=0')
        row = analyze(lines)['requests'][0]
        self.assertEqual(row['owner_queue_upper_bound_ms'], 1000)
        self.assertEqual(row['maintenance_overlap'], [])

    def test_order_duplicates_and_missing(self):
        lines = fixture()
        normal = analyze(lines)
        self.assertEqual(analyze(list(reversed(lines)) + lines)['requests'], normal['requests'])
        missing = analyze([line for line in lines if not ('span=102 ' in line and 'event=end' in line)])
        self.assertIsNone(missing['requests'][0]['owner_ms'])
        self.assertTrue(missing['incomplete_spans'])

    def test_no_cross_pid_or_conflicting_clocks(self):
        lines = fixture()
        foreign = [line.replace('pid=123', 'pid=124') if 'source=managed' in line else line for line in lines]
        self.assertIsNone(analyze(foreign)['requests'][0]['managed_request_id'])
        conflict = [line.replace('frequency=1000', 'frequency=10000000') if 'source=managed' in line else line for line in lines]
        row = analyze(conflict)['requests'][0]
        self.assertIsNone(row['total_ms'])
        self.assertIsNone(row['native_com_ms'])

    def test_malformed_and_ambiguous(self):
        self.assertTrue(analyze(['[PptPerf] v=1'])['issues'])
        lines = fixture()
        duplicate_request = [line.replace('span=100 ', 'span=103 ') for line in lines if 'span=100 ' in line]
        self.assertIsNone(analyze(lines + duplicate_request)['requests'][0]['managed_request_id'])

    def test_maintenance_only_has_no_control_sample(self):
        lines = [f'[PptPerf] v=1 source=managed pid=123 tid=13 qpc={qpc} frequency=1000 span=1 parent=0 event={event} stage=observe op=None request=0 show_session=2 binding=1'
                 for event, qpc in [('begin', 10), ('end', 20)]]
        result = analyze(lines)
        self.assertEqual(result['status'], 'no-native-navigation')
        self.assertEqual(result['requests'], [])
        self.assertEqual(result['issues'], [])

    def test_conflicting_immutable_identities_are_unknown(self):
        for source, stage, field in [('managed', 'managed-request', 'request'),
                                     ('managed', 'managed-request', 'show_session'),
                                     ('managed', 'managed-request', 'binding'),
                                     ('native', 'native-request', 'epoch'),
                                     ('native', 'native-request', 'local_session')]:
            with self.subTest(source=source, field=field):
                lines = []
                for line in fixture():
                    if f'source={source}' in line and f'stage={stage} ' in line:
                        if f'{field}=' not in line:
                            line += f' {field}=1'
                        if 'event=end' in line:
                            line = re.sub(r'\b' + field + r'=\S+', field + '=999', line)
                    lines.append(line)
                result = analyze(lines)
                self.assertEqual(result['status'], 'partial')
                self.assertTrue(any('identity' in issue for issue in result['issues']))
                self.assertTrue(result['incomplete_spans'])
                key = 'managed_request_ms' if source == 'managed' else 'total_ms'
                self.assertIsNone(result['requests'][0][key])


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('inputs', nargs='*', type=Path, help='Capture folders or log files')
    parser.add_argument('--json', type=Path, help='Write detailed derived summary')
    parser.add_argument('--self-test', action='store_true', help='Run deterministic parser tests without GUI')
    args = parser.parse_args(argv)
    if args.self_test:
        return 0 if unittest.TextTestRunner().run(unittest.defaultTestLoader.loadTestsFromTestCase(AnalyzerTests)).wasSuccessful() else 1
    if not args.inputs:
        parser.error('provide a capture folder/log file, or --self-test')
    paths = []
    for item in args.inputs:
        paths.extend(sorted(item.rglob('*.log')) if item.is_dir() else [item])
    lines = []
    for path in dict.fromkeys(paths):
        lines.extend(path.read_text(encoding='utf-8-sig', errors='replace').splitlines())
    result = analyze(lines)
    if args.json:
        args.json.write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    def show(value):
        return '?' if value is None else f'{value:.3f}'
    print('PptPerf intervals (ms); ? = unknown, not zero; no optical slide-change measurement')
    print('PID/native-span op epoch | total native-queue mailbox-queue owner focus native-other')
    print('Maintenance child rows are nested; do not add parent and descendant durations.')
    if not result['requests']:
        print('No native control-navigation sample was captured; maintenance/keyboard-only traces do not validate control latency.')
    for row in result['requests']:
        columns = ('total_ms', 'native_queue_ms', 'mailbox_queue_ms', 'owner_ms', 'native_focus_ms', 'native_other_ms')
        print(f"{row['pid']}/{row['native_span']} {row['op']} {row['epoch']} | " + ' '.join(show(row[key]) for key in columns))
        detail = ('native_com_ms', 'com_before_managed_ms', 'managed_wait_ms', 'com_after_managed_ms',
                  'owner_dispatch_ms', 'owner_end_after_wait_return_ms')
        print('  ' + ' '.join(f'{key}={show(row[key])}' for key in detail))
        if row['mailbox_queue_ms'] is None and row['owner_queue_upper_bound_ms'] is not None:
            print('  owner_queue_upper_bound_ms=' + show(row['owner_queue_upper_bound_ms']))
        def show_children(parent, depth=0):
            indent = '    ' + '  ' * depth
            if parent['children'] is None:
                print(indent + 'child-breakdown=? (no recorded child spans)')
                return
            for child in parent['children']:
                print(indent + f"child {child['stage']} span={child['span']} total_ms={show(child['total_ms'])} queue_overlap_ms={show(child['overlap_ms'])}")
                for note in child['notes']:
                    print(indent + '  ' + note)
                if child['children'] is not None:
                    show_children(child, depth + 1)
            print(indent + 'direct-child-accounted total_ms=' + show(parent['accounted_total_ms'])
                  + ' queue_overlap_ms=' + show(parent['accounted_overlap_ms'])
                  + '; unattributed total_ms=' + show(parent['unattributed_total_ms'])
                  + ' queue_overlap_ms=' + show(parent['unattributed_overlap_ms']))
        for maintenance in row['maintenance_overlap']:
            print(f"  queue-overlap {maintenance['stage']} span={maintenance['span']} ms={show(maintenance['overlap_ms'])} total_ms={show(maintenance['total_ms'])}")
            show_children(maintenance)
        for note in row['notes']:
            print('  ' + note)
    print(f"status={result['status']} events={result['event_count']} spans={result['span_count']} issues={len(result['issues'])}")
    for issue in result['issues']:
        print('  ' + issue)
    return 2 if result['status'] == 'no-usable-trace' else 0


if __name__ == '__main__':
    sys.exit(main())
