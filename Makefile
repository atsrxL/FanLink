.PHONY: test panel-test selftest verify panel panel-demo package

PYTHON ?= python3

test:
	./firmware/scripts/run_native_tests.sh

panel-test:
	$(PYTHON) -m unittest discover -s pc/tests -p 'test_fanlink_panel.py' -v
	node --check pc/panel/app.js

selftest:
	$(PYTHON) pc/tools/fanlink_cli.py --self-test
	$(PYTHON) -m py_compile pc/tools/*.py firmware/tools/*.py firmware/scripts/*.py

verify: test panel-test selftest
	./firmware/tools/run_firmware_syntax_smoke.sh
	./scripts/source_manifest.sh --check

panel:
	uv run --with 'pyserial>=3.5,<4' $(PYTHON) pc/tools/fanlink_panel.py --open

panel-demo:
	$(PYTHON) pc/tools/fanlink_panel.py --demo --open

package: verify
	./scripts/package.sh
