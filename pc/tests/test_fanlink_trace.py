"""Offline synthetic RTE traces only: no serial, SSH, radio or hardware."""
import json
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import fanlink_trace_analyze as trace


def rte(index=0, count=1, *, generation=1, kind=3, timestamp=100,
        event=15, validity=15, pins=1, value=0, result=0, dropped=0, overwrites=0):
    return (f'RTE g{generation} k{kind} i{index} c{count} d{dropped} w{overwrites} '
            f't{timestamp} e{event} v{validity} p{pins} a{value:08X} r{result}')


def line(text, kind='tx_diag', elapsed=1):
    return json.dumps(dict(kind=kind, elapsed=elapsed, text=text))


def snapshot(kind=3, source='tx_diag', generation=1, start=100, **kwargs):
    values = [(7, 0, 0), (9, 0x42 if kind == 1 else 2, 0),
              (11, 0x01001220 if kind == 1 else 0x01000220, 0),
              (15, 0, -7 if kind == 1 else 0)]
    return [line(rte(i, 4, kind=kind, generation=generation, event=e, value=a,
                     result=r, timestamp=(start+i*100)&trace.UINT32, **kwargs), source, i+1)
            for i,(e,a,r) in enumerate(values)]


class TraceTests(unittest.TestCase):
    def test_hex_and_signed_parse(self):
        record = trace.parse_trace(rte(value=0xABCDEF01, result=-32768))
        self.assertEqual(record['a'], 0xABCDEF01)
        self.assertEqual(record['r'], -32768)

    def test_strict_malformed_and_ranges(self):
        for text in (rte()+' x1', 'prefix '+rte(), rte(generation=0), rte(count=33),
                     rte(index=2,count=2), rte(timestamp=1<<32), rte(validity=16),
                     rte(pins=4), rte(event=99), rte(kind=0), rte(result=-32769)):
            with self.subTest(text=text), self.assertRaises(ValueError):
                trace.parse_trace(text)

    def test_good_crc_and_source_separation(self):
        report = trace.analyze_lines(snapshot()+snapshot(1,'rx'))
        self.assertEqual(report['serialized_complete_snapshots'], 2)
        good = report['comparison']['tx']['good']
        crc = report['comparison']['rx']['crc']
        self.assertEqual(good['software_evidence_samples']['irq_crc_samples'], 0)
        self.assertEqual(crc['software_evidence_samples']['irq_crc_samples'], 1)
        self.assertEqual(crc['software_evidence_samples']['packet_crc_samples'], 1)
        self.assertEqual(crc['intervals_us']['DioObserved_to_RxResult']['max'], 300)

    def test_partial(self):
        report = trace.analyze_lines(snapshot()[1:])
        self.assertEqual(report['snapshots'][0]['missing_indices'], [0])
        self.assertFalse(report['trace_evidence_available'])
        self.assertEqual(report['snapshots'][0]['intervals_us'], {})

    def test_duplicate_idempotent_conflict_rejected(self):
        records = snapshot()
        report = trace.analyze_lines(records+[records[0]])
        self.assertTrue(report['snapshots'][0]['serialized_complete'])
        self.assertEqual(report['snapshots'][0]['duplicate_records'], 1)
        report = trace.analyze_lines(records+[line(rte(0,4,event=7,timestamp=999))])
        self.assertIn('conflicting_duplicate_index', report['snapshots'][0]['errors'])

    def test_header_inconsistency(self):
        records = snapshot()
        records[1] = line(rte(1,4,event=9,dropped=2))
        self.assertFalse(trace.analyze_lines(records)['snapshots'][0]['serialized_complete'])

    def test_wrap_and_ambiguous_timestamp(self):
        summary = trace.analyze_lines(snapshot(start=trace.UINT32-150))['snapshots'][0]
        self.assertTrue(summary['timeline_valid'])
        self.assertEqual(summary['micros32_wraps'], 1)
        self.assertEqual(summary['intervals_us']['DioObserved_to_RxResult'], [300])
        records = [line(rte(0,2,event=7,timestamp=100)), line(rte(1,2,timestamp=99))]
        summary = trace.analyze_lines(records)['snapshots'][0]
        self.assertFalse(summary['timeline_valid'])
        self.assertNotIn('relative_us', summary['records'][0])

    def test_invalid_value_and_pins_not_evidence(self):
        records = snapshot(1, validity=1)
        report = trace.analyze_lines(records)
        self.assertNotIn('irq_crc_samples', report['comparison']['tx']['crc']['software_evidence_samples'])
        self.assertIsNone(report['snapshots'][0]['records'][1]['pins'])

    def test_missing_time_invalidates_timeline(self):
        report = trace.analyze_lines(snapshot(validity=14))
        self.assertFalse(report['snapshots'][0]['timeline_valid'])
        self.assertTrue(report['snapshots'][0]['serialized_complete'])

    def test_history_overwrite_not_transport_loss(self):
        report = trace.analyze_lines(snapshot(overwrites=trace.UINT32,dropped=8))
        summary = report['snapshots'][0]
        self.assertTrue(summary['serialized_complete'])
        self.assertTrue(summary['timeline_valid'])
        self.assertTrue(summary['history_counter_saturated'])
        self.assertEqual(summary['dropped_triggers_at_freeze'], 8)

    def test_old_crc_history_not_good_evidence(self):
        old = [(7,0), (9,0x42), (15,0), (7,0), (9,2), (15,0)]
        report = trace.analyze_lines([line(rte(i,6,event=e,value=a,timestamp=100+i*10))
                                      for i,(e,a) in enumerate(old)])
        evidence = report['comparison']['tx']['good']['software_evidence_samples']
        self.assertEqual(evidence['irq_crc_samples'], 0)
        self.assertEqual(evidence['checked_irq_samples'], 1)

    def test_generation_wrap_reset_and_interleave(self):
        report = trace.analyze_lines([line(rte(generation=trace.UINT32)),line(rte(generation=1))])
        self.assertEqual(report['diagnostics']['generation_wraps'], 1)
        report = trace.analyze_lines([line(rte(generation=1)),line(rte(generation=2)),line(rte(generation=1))])
        self.assertIn('generation_interleaved_or_reused',report['snapshots'][0]['errors'])
        records = [json.dumps(dict(kind='tx_status',uptime=100)),line(rte()),
                   json.dumps(dict(kind='tx_status',uptime=1)),line(rte())]
        report = trace.analyze_lines(records)
        self.assertEqual(report['trace_snapshots'], 2)
        self.assertEqual(report['diagnostics']['tx_observed_uptime_resets'], 1)

    def test_rx_uptime_reset(self):
        records = [line('RXDIAG u100 L1','rx'),line(rte(),'rx'),
                   line('RXDIAG u1 L1','rx'),line(rte(),'rx')]
        self.assertEqual(trace.analyze_lines(records)['trace_snapshots'], 2)

    def test_counter_decrease(self):
        records = [line(rte(generation=1,dropped=4)),line(rte(generation=2,dropped=3))]
        self.assertIn('saturating_counter_decreased',trace.analyze_lines(records)['snapshots'][1]['errors'])

    def test_bounded_output_and_bad_input(self):
        records = ['{', '[]', json.dumps(dict(elapsed=float('nan'))),
                   json.dumps(dict(kind='tx_diag',text=42)),line(rte()),line(rte(generation=2))]
        report = trace.analyze_lines(records,max_snapshots=1)
        self.assertEqual(report['trace_snapshots'], 1)
        self.assertEqual(report['diagnostics']['bad_json_or_event'], 3)
        self.assertEqual(report['diagnostics']['snapshot_limit_reached'], 1)

    def test_exclusion_and_old_capture_no_trace(self):
        report = trace.analyze_lines(snapshot(),exclude_seconds=2)
        self.assertEqual(report['snapshots'][0]['missing_indices'], [0])
        report = trace.analyze_lines([line('RFN n2 p100'),json.dumps(dict(kind='complete',parser_rejects=0))])
        self.assertFalse(report['trace_evidence_available'])
        self.assertTrue(report['capture_complete_record'])

    def test_checked_gpio_irq_mismatch_candidate(self):
        records = [line(rte(0,3,event=7)),line(rte(1,3,event=9,timestamp=110)),
                   line(rte(2,3,event=15,timestamp=120))]
        evidence=trace.analyze_lines(records)['comparison']['tx']['good']['software_evidence_samples']
        self.assertEqual(evidence['dio_high_irq_zero_samples'], 1)

    def test_io_cleanup_and_arm_phases(self):
        events = [3,4,5,6,7,13,14,3,4,5,6,18]
        report = trace.analyze_lines([line(rte(i,len(events),kind=2,event=e,timestamp=i*10))
                                      for i,e in enumerate(events)])
        intervals = report['snapshots'][0]['intervals_us']
        self.assertEqual(intervals['ArmBegin_to_ArmEnd_pre_dio'], [30])
        self.assertEqual(intervals['ArmBegin_to_ArmEnd_post_dio'], [30])
        self.assertEqual(intervals['DioObserved_to_FaultCleanup'], [70])

    def test_start_separates_identical_generations(self):
        records = [json.dumps(dict(kind='start')),line(rte()),
                   json.dumps(dict(kind='start')),line(rte())]
        self.assertEqual(trace.analyze_lines(records)['trace_snapshots'], 2)

    def test_tx_failure_does_not_reclassify_old_receive(self):
        events = [7,9,18,16,1,2]
        report = trace.analyze_lines([line(rte(i,len(events),kind=2,event=e,timestamp=i*10,result=-7))
                                      for i,e in enumerate(events)])
        self.assertFalse(report['snapshots'][0]['latest_receive_segment_present'])
        self.assertEqual(report['snapshots'][0]['intervals_us'], {})


if __name__ == '__main__':
    unittest.main()
