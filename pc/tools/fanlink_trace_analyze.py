"""Offline RTE software trace analysis of paired capture JSONL; no device access.

Usage: python3 pc/tools/fanlink_trace_analyze.py capture.jsonl
JSON is written to stdout. Complete transport snapshots are not hardware tests.
"""
from __future__ import annotations

import argparse
from collections import Counter
import json
import math
from pathlib import Path
import re
import sys

UINT32 = (1 << 32) - 1
HALF32 = 1 << 31
EVENTS = {1: 'TxBegin', 2: 'TxEnd', 3: 'ArmBegin', 4: 'ArmStageEnd',
          5: 'ArmLaunchBegin', 6: 'ArmEnd', 7: 'DioObserved', 8: 'StandbyEnd',
          9: 'IrqRead', 10: 'BufferRead', 11: 'PacketStatusRead',
          12: 'FifoRead', 13: 'ClearBegin', 14: 'ClearEnd',
          15: 'RxResult', 16: 'TxContext', 17: 'RxContext', 18: 'FaultCleanup'}
KINDS = {1: 'crc', 2: 'io', 3: 'good'}
PATTERN = re.compile(r'RTE g(\d+) k(\d+) i(\d+) c(\d+) d(\d+) w(\d+) t(\d+) '
                     r'e(\d+) v(\d+) p(\d+) a([0-9A-Fa-f]{8}) r(-?\d+)')
FIELDS = ('g', 'k', 'i', 'c', 'd', 'w', 't', 'e', 'v', 'p', 'a', 'r')
LIMITATIONS = [
    'Software timestamps and GPIO reads are not physical DIO edges or RF airtime measurements.',
    'TX and RX micros clocks are independent; host receipt timestamps do not synchronize them.',
    'CRC IRQ and packet-status flags are same-chip evidence, not independent sender authentication.',
    'Context labels identify a software transaction/poll window, not the source of a corrupt packet.',
    'A complete serialized snapshot need not contain complete transaction history; the ring is bounded.',
    'History overwrite counts are cumulative; nonzero does not alone prove the latest transaction was truncated.',
    'Snapshots contain older transactions. Kind applies only to the trigger, not every retained event.',
    'Good triggers are decimated (one in64); pending snapshots suppress triggers, so no trace-derived loss rate.',
    'Arm/clear intervals are split before/after software DIO observation; rearm timing is not initial readiness.',
    'DIO high with checked IRQ zero is a software mismatch candidate, not a proven CRC or stale IRQ.',
    'Dropped-trigger counters describe unsaved triggers, not RF losses; CRC and timeout must not be added.',
    'Good means adapter-level trace classification, not accepted application/network/node/sequence validation.',
    'Timestamp ordering assumes adjacent retained events are less than 2^31 microseconds apart.',
    'Reboot epochs can only be isolated when a capture start or uptime decrease is observed.',
]


def parse_trace(text):
    """Strict full-line grammar. Reject malformed trace rather than salvage fields."""
    match = PATTERN.fullmatch(text)
    if not match:
        raise ValueError('malformed RTE grammar')
    record = {key: int(value, 16 if key == 'a' else 10)
              for key, value in zip(FIELDS, match.groups())}
    if any(not 0 <= record[key] <= UINT32 for key in ('g', 'd', 'w', 't', 'a')):
        raise ValueError('uint32 field outside range')
    if not record['g'] or record['k'] not in KINDS:
        raise ValueError('invalid generation or kind')
    if not 1 <= record['c'] <= 32 or not 0 <= record['i'] < record['c']:
        raise ValueError('invalid index/count')
    if record['e'] not in EVENTS or not 0 <= record['v'] <= 15 or not 0 <= record['p'] <= 3:
        raise ValueError('invalid event/validity/pins')
    if not -32768 <= record['r'] <= 32767:
        raise ValueError('result outside printable int16 range')
    return record


def decode_record(record):
    out = {'index': record['i'], 'event': EVENTS[record['e']],
           'micros32': record['t'] if record['v'] & 1 else None,
           'pins': {'dio': record['p'] & 1, 'busy': (record['p'] >> 1) & 1}
                   if record['v'] & 2 else None,
           'result': record['r'] if record['v'] & 8 else None,
           'value_hex': f"{record['a']:08X}" if record['v'] & 4 else None}
    if record['v'] & 4:
        value = record['a']
        if record['e'] == 9:
            out['irq'] = {'raw': value, 'rx_done': bool(value & 2), 'crc': bool(value & 0x40)}
        elif record['e'] == 10:
            out['buffer'] = {'length': value & 255, 'offset': (value >> 8) & 255}
        elif record['e'] == 11:
            out['packet_status'] = {'rssi_raw': value & 255, 'errors': (value >> 8) & 255,
                                    'status': (value >> 16) & 255, 'sync': value >> 24,
                                    'crc': bool((value >> 8) & 0x10)}
        elif record['e'] in (16, 17):
            out['context'] = {'type': value >> 24, 'poll_or_node': (value >> 16) & 255,
                              'sequence': value & 65535, 'sender_authenticated': False}
        elif record['e'] == 7:
            out['software_observed_micros32'] = value
    return out


def summarize_snapshot(snapshot):
    records = [snapshot['records'][index] for index in sorted(snapshot['records'])]
    missing = sorted(set(range(snapshot['count'])) - snapshot['records'].keys())
    errors = list(snapshot['errors'])
    decoded = [decode_record(record) for record in records]
    wraps = 0
    offset = 0
    previous = None
    timeline_valid = not missing and not errors
    for record, item in zip(records, decoded):
        if not record['v'] & 1:
            timeline_valid = False
            errors.append('timestamp_not_valid')
            previous = None
            continue
        if previous is not None:
            delta = (record['t'] - previous) & UINT32
            if delta >= HALF32:
                timeline_valid = False
                errors.append('backward_or_ambiguous_timestamp')
            else:
                offset += delta
                wraps += int(record['t'] < previous)
        item['relative_us'] = offset
        previous = record['t']
    # A malformed timeline never produces apparently trusted relative offsets.
    if not timeline_valid:
        for item in decoded:
            item.pop('relative_us', None)
    # Only compare the latest receive segment; preceding rolling history can
    # contain an outcome different from the snapshot's triggering kind.
    terminal_event = 18 if snapshot['kind'] == 2 else 15
    result_indices = [i for i, r in enumerate(records) if r['e'] in (15, 18)]
    result_index = result_indices[-1] if result_indices else None
    if result_index is not None and (records[result_index]['e'] != terminal_event or result_index != len(records)-1):
        result_index = None
    prior_result = result_indices[-2] if len(result_indices) > 1 else -1
    dio_indices = [i for i, r in enumerate(records)
                   if r['e'] == 7 and prior_result < i and result_index is not None and i < result_index]
    latest_start = dio_indices[-1] if dio_indices else None
    arm_indices = [i for i, r in enumerate(records) if r['e'] == 3
                   and latest_start is not None and prior_result < i < latest_start]
    if arm_indices:
        latest_start = arm_indices[-1]
    selected = list(zip(records, decoded))[latest_start:result_index + 1] if latest_start is not None else []
    intervals = {}
    if timeline_valid:
        dio_offset = next((item['relative_us'] for record, item in selected if record['e'] == 7), None)
        for begin, end in ((1, 2), (3, 6), (4, 6), (5, 6), (7, 15), (7, 18), (13, 14)):
            start = None
            for record, item in selected:
                if record['e'] == begin:
                    start = item['relative_us']
                elif record['e'] == end and start is not None:
                    label = EVENTS[begin] + '_to_' + EVENTS[end]
                    if begin in (3, 4, 5, 13) and dio_offset is not None:
                        label += '_pre_dio' if start < dio_offset else '_post_dio'
                    intervals.setdefault(label, []).append(item['relative_us'] - start)
                    start = None
    return {'source': snapshot['source'], 'epoch': snapshot['epoch'],
            'generation': snapshot['generation'], 'kind': KINDS[snapshot['kind']],
            'count': snapshot['count'], 'received_records': len(records),
            'missing_indices': missing, 'duplicate_records': snapshot['duplicates'],
            'errors': sorted(set(errors)), 'serialized_complete': not missing and not snapshot['errors'],
            'timeline_valid': timeline_valid, 'micros32_wraps': wraps,
            'dropped_triggers_at_freeze': snapshot['dropped'],
            'history_overwrites_at_freeze': snapshot['overwrites'],
            'history_counter_saturated': snapshot['overwrites'] == UINT32,
            'dropped_counter_saturated': snapshot['dropped'] == UINT32,
            'latest_receive_segment_present': bool(selected),
            'latest_receive_records': [item for _, item in selected],
            'host_receipt_span': [snapshot['first_elapsed'], snapshot['last_elapsed']],
            'intervals_us': intervals, 'records': decoded}


def analyze_lines(lines, *, max_snapshots=1000, exclude_seconds=0.0):
    if max_snapshots < 1 or not math.isfinite(exclude_seconds) or exclude_seconds < 0:
        raise ValueError('invalid analyzer limits')
    snapshots = {}
    epochs = Counter()
    uptimes = {}
    last_generation = {}
    last_counters = {}
    diagnostics = Counter()
    issues = []
    transport = Counter()
    complete = False
    def issue(line, detail):
        diagnostics[detail] += 1
        if len(issues) < 100:
            issues.append({'line': line, 'issue': detail})
    for line_number, line in enumerate(lines, 1):
        try:
            event = json.loads(line)
            if not isinstance(event, dict):
                raise ValueError('not object')
            elapsed = event.get('elapsed', 0)
            if isinstance(elapsed, bool) or not isinstance(elapsed, (int, float)) or not math.isfinite(elapsed):
                raise ValueError('bad elapsed')
        except (ValueError, TypeError):
            issue(line_number, 'bad_json_or_event')
            continue
        kind = event.get('kind')
        if kind == 'start':
            epochs['tx'] += 1
            epochs['rx'] += 1
            uptimes.clear()
        if kind == 'complete':
            complete = True
            rejects = event.get('parser_rejects', 0)
            if isinstance(rejects, int) and not isinstance(rejects, bool) and rejects >= 0:
                transport['parser_rejects'] += rejects
            else:
                issue(line_number, 'invalid_parser_reject_count')
        if kind in ('transport_error', 'failure', 'release_failure', 'rx_unframed', 'rx_tail'):
            transport[kind] += 1
        source = 'tx' if kind in ('tx_diag', 'tx_status') else 'rx' if kind == 'rx' else None
        text = event.get('text', '')
        if not isinstance(text, str):
            issue(line_number, 'text_not_string')
            continue
        uptime = event.get('uptime') if kind == 'tx_status' else None
        if kind == 'rx' and text.startswith('RXDIAG '):
            match = re.search(r'(?:^| )u(\d+)(?: |$)', text)
            uptime = int(match.group(1)) if match else None
        if source and isinstance(uptime, int) and not isinstance(uptime, bool):
            previous = uptimes.get(source)
            if previous is not None and uptime < previous:
                if ((uptime - previous) & UINT32) >= HALF32:
                    epochs[source] += 1
                    diagnostics[source + '_observed_uptime_resets'] += 1
                else:
                    diagnostics[source + '_uptime_wraps'] += 1
            uptimes[source] = uptime
        if not text.startswith('RTE'):
            continue
        if kind not in ('tx_diag', 'rx'):
            issue(line_number, 'trace_wrong_capture_channel')
            continue
        if elapsed < exclude_seconds:
            diagnostics['excluded_trace_records'] += 1
            continue
        try:
            record = parse_trace(text)
        except ValueError as error:
            issue(line_number, str(error))
            continue
        key = (source, epochs[source], record['g'])
        if key not in snapshots:
            if len(snapshots) >= max_snapshots:
                issue(line_number, 'snapshot_limit_reached')
                continue
            sequence_key = (source, epochs[source])
            prior = last_generation.get(sequence_key)
            sequence_errors = []
            if prior is not None:
                step = (record['g'] - prior) & UINT32
                if step >= HALF32 or step == 0:
                    sequence_errors.append('generation_backward_or_reused')
                elif record['g'] < prior:
                    diagnostics['generation_wraps'] += 1
                elif step > 1:
                    diagnostics['generation_gaps'] += step - 1
            prior_counters = last_counters.get(sequence_key)
            if prior_counters is not None and any(record[k] < prior_counters[k] for k in ('d', 'w')):
                sequence_errors.append('saturating_counter_decreased')
            last_counters[sequence_key] = record
            last_generation[sequence_key] = record['g']
            snapshots[key] = {'source': source, 'epoch': epochs[source], 'generation': record['g'],
                              'kind': record['k'], 'count': record['c'], 'dropped': record['d'], 'overwrites': record['w'],
                              'records': {}, 'errors': sequence_errors, 'duplicates': 0,
                              'first_elapsed': elapsed, 'last_elapsed': elapsed}
        snapshot = snapshots[key]
        if record['g'] != last_generation[(source, epochs[source])]:
            if 'generation_interleaved_or_reused' not in snapshot['errors']:
                snapshot['errors'].append('generation_interleaved_or_reused')
        snapshot['last_elapsed'] = elapsed
        if (record['k'], record['c'], record['d'], record['w']) != (snapshot['kind'], snapshot['count'], snapshot['dropped'], snapshot['overwrites']):
            if 'inconsistent_snapshot_header' not in snapshot['errors']:
                snapshot['errors'].append('inconsistent_snapshot_header')
        previous = snapshot['records'].get(record['i'])
        if previous is not None:
            snapshot['duplicates'] += 1
            if previous != record:
                if 'conflicting_duplicate_index' not in snapshot['errors']:
                    snapshot['errors'].append('conflicting_duplicate_index')
        else:
            snapshot['records'][record['i']] = record
    summarized = [summarize_snapshot(snapshot) for snapshot in snapshots.values()]
    comparison = {}
    for source in ('tx', 'rx'):
        comparison[source] = {}
        for name in KINDS.values():
            selected = [s for s in summarized if s['source'] == source and s['kind'] == name]
            valid = [s for s in selected if s['serialized_complete'] and s['timeline_valid']]
            intervals = {}
            evidence = Counter()
            for snapshot in valid:
                if snapshot['latest_receive_segment_present']:
                    evidence['latest_receive_segments'] += 1
                for record in snapshot['latest_receive_records']:
                    if 'irq' in record:
                        evidence['checked_irq_samples'] += 1
                        evidence['irq_crc_samples'] += int(record['irq']['crc'])
                        if record['pins'] and record['pins']['dio'] and record['irq']['raw'] == 0:
                            evidence['dio_high_irq_zero_samples'] += 1
                    if 'packet_status' in record:
                        evidence['checked_packet_status_samples'] += 1
                        evidence['packet_crc_samples'] += int(record['packet_status']['crc'])
                    if record['pins']:
                        evidence['software_gpio_samples'] += 1
                        evidence['busy_high_samples'] += record['pins']['busy']
                for label, values in snapshot['intervals_us'].items():
                    intervals.setdefault(label, []).extend(values)
            comparison[source][name] = {'snapshots': len(selected), 'valid_timing_snapshots': len(valid),
                                       'software_evidence_samples': dict(evidence),
                                       'intervals_us': {label: {'n': len(values), 'min': min(values),
                                                              'max': max(values),
                                                              'mean': sum(values) / len(values)}
                                                        for label, values in intervals.items()}}
    return {'schema': 'fanlink-rte-analysis-v1', 'capture_complete_record': complete,
            'trace_snapshots': len(summarized), 'serialized_complete_snapshots': sum(s['serialized_complete'] for s in summarized),
            'partial_or_invalid_snapshots': sum(not s['serialized_complete'] for s in summarized),
            'trace_evidence_available': any(s['serialized_complete'] for s in summarized),
            'diagnostics': dict(diagnostics), 'issues': issues, 'transport': dict(transport),
            'comparison': comparison, 'snapshots': summarized, 'limitations': LIMITATIONS,
            'limits': {'max_snapshots': max_snapshots, 'exclude_seconds': exclude_seconds, 'max_issue_details': 100}}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path)
    parser.add_argument('--max-snapshots', type=int, default=1000)
    parser.add_argument('--exclude-seconds', type=float, default=0.0,
                        help='Drop earlier trace records; partial snapshots stay explicitly partial')
    args = parser.parse_args(argv)
    try:
        with args.capture.open(encoding='utf-8') as stream:
            report = analyze_lines(stream, max_snapshots=args.max_snapshots, exclude_seconds=args.exclude_seconds)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0


if __name__ == '__main__':
    sys.exit(main())
