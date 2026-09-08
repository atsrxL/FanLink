.PHONY: ota-test test recovery-test fan-output-test tachometer-test reply-timing-test panel-test selftest verify panel panel-demo package

PYTHON ?= python3

test:
	./firmware/scripts/run_native_tests.sh

recovery-test:
	bash firmware/tools/run_runtime_recovery_tests.sh

fan-output-test:
	bash firmware/tools/run_fan_output_tests.sh

tachometer-test:
	bash firmware/tools/run_tachometer_tests.sh

reply-timing-test:
	bash firmware/scripts/run_rx_reply_timing_tests.sh
	bash firmware/scripts/run_joint_reply_timing_tests.sh

panel-test:
	$(PYTHON) -m unittest discover -s pc/tests -p 'test_fanlink_panel.py' -v
	$(PYTHON) -m unittest discover -s pc/tests -p 'test_fanlink_host_v2.py' -v
	node --check pc/panel/app.js

selftest:
	$(PYTHON) pc/tools/fanlink_cli.py --self-test
	$(PYTHON) -m py_compile pc/tools/*.py firmware/tools/*.py firmware/scripts/*.py

ota-test:
	bash firmware/tools/run_tx_local_tests.sh
	bash firmware/tools/run_ota_tests.sh
	bash firmware/tools/run_ota_storage_tests.sh
	bash firmware/tools/run_ota_integration_tests.sh
	$(PYTHON) -m unittest discover -s pc/tests -p 'test_fanlink_ota.py' -v
	$(PYTHON) -m unittest discover -s pc/tests -p 'test_fanlink_tx_update.py' -v

settings-test:
	$(PYTHON) firmware/tools/test_board_profiles.py
	bash firmware/tools/run_settings_tests.sh
	bash firmware/tools/run_settings_tx_tests.sh
	$(PYTHON) -m unittest discover -s pc/tests -p 'test_fanlink_config.py' -v

diagnostic-scheduler-test:
	bash firmware/tools/run_tx_diagnostic_scheduler_tests.sh

radio-trace-test:
	bash firmware/tools/run_radio_trace_tests.sh
	$(PYTHON) -m unittest discover -s pc/tests -p 'test_fanlink_trace.py' -v

verify: radio-trace-test diagnostic-scheduler-test settings-test ota-test test recovery-test fan-output-test tachometer-test reply-timing-test panel-test selftest
	bash firmware/tools/run_rx_uart_diagnostic_tests.sh
	./firmware/tools/run_firmware_syntax_smoke.sh
	./scripts/source_manifest.sh --check

panel:
	uv run --with 'pyserial>=3.5,<4' $(PYTHON) pc/tools/fanlink_panel.py --open

panel-demo:
	$(PYTHON) pc/tools/fanlink_panel.py --demo --open

package: verify
	./scripts/package.sh
