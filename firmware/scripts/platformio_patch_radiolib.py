"""Apply the FanLink RadioLib 7.7.1 SX128x safety integration patch.

PlatformIO installs each environment's dependencies under ``.pio/libdeps``.
This pre-build hook makes three narrow, fail-closed edits to the pinned RadioLib
source before it is compiled:

* honour FANLINK_RADIO_USE_DCDC during the initial SX128x configuration, and
* bound the otherwise unbounded BUSY wait in SX128x::launchMode(), and
* hard-cap the blocking transmit wait even when FLRC time-on-air is invalid.

The exact upstream version and source snippets are checked so an upstream
change cannot silently receive a partial or misplaced patch.
"""

from __future__ import annotations

import json
from pathlib import Path

Import("env")  # type: ignore[name-defined]  # Provided by PlatformIO/SCons.


EXPECTED_VERSION = "7.7.1"

REGULATOR_UPSTREAM = """  // set regulator mode to DC-DC
  data[0] = RADIOLIB_SX128X_REGULATOR_DC_DC;
"""

REGULATOR_PATCHED = """  // FanLink target data selects the regulator from reset onward. Preserve
  // RadioLib's upstream DC-DC default when the integration macro is absent.
  #if defined(FANLINK_RADIO_USE_DCDC)
    data[0] = FANLINK_RADIO_USE_DCDC ? RADIOLIB_SX128X_REGULATOR_DC_DC : RADIOLIB_SX128X_REGULATOR_LDO;
  #else
    data[0] = RADIOLIB_SX128X_REGULATOR_DC_DC;
  #endif
"""

BUSY_UPSTREAM = """      // wait for BUSY to go low (= PA ramp up done)
      while(this->mod->hal->digitalRead(this->mod->getGpio())) {
        this->mod->hal->yield();
      }
"""

BUSY_PATCHED = """      // wait for BUSY to go low (= PA ramp up done), but never forever.
      // Module SPI transfers already use this same timeout; applying it here
      // closes the remaining unbounded wait in the blocking TX path.
      RadioLibTime_t busyStart = this->mod->hal->millis();
      while(this->mod->hal->digitalRead(this->mod->getGpio())) {
        this->mod->hal->yield();
        if(this->mod->hal->millis() - busyStart >= this->mod->spiConfig.timeout) {
          this->mod->setRfSwitchState(Module::MODE_IDLE);
          return(RADIOLIB_ERR_SPI_CMD_TIMEOUT);
        }
      }
"""

TX_DONE_UPSTREAM = """  // wait for packet transmission or timeout
  RadioLibTime_t start = this->mod->hal->millis();
  while(!this->mod->hal->digitalRead(this->mod->getIrq())) {
    this->mod->hal->yield();
    if(this->mod->hal->millis() - start > timeout) {
      finishTransmit();
      return(RADIOLIB_ERR_TX_TIMEOUT);
    }
  }
"""

TX_DONE_PATCHED = """  // wait for packet transmission or timeout. SX128x::getTimeOnAir()
  // does not support FLRC in RadioLib 7.7.1 and returns a negative error code
  // through an unsigned type, so never trust the calculated timeout alone.
  RadioLibTime_t start = this->mod->hal->millis();
  while(!this->mod->hal->digitalRead(this->mod->getIrq())) {
    this->mod->hal->yield();
    RadioLibTime_t elapsed = this->mod->hal->millis() - start;
    if((elapsed > timeout) || (elapsed >= this->mod->spiConfig.timeout)) {
      this->mod->setRfSwitchState(Module::MODE_IDLE);
      (void)finishTransmit();
      return(RADIOLIB_ERR_TX_TIMEOUT);
    }
  }
"""


def replace_exactly_once(text: str, upstream: str, patched: str, label: str) -> str:
    if patched in text:
        if text.count(patched) != 1:
            raise RuntimeError(f"RadioLib {label} patch marker is not unique")
        return text
    if text.count(upstream) != 1:
        raise RuntimeError(
            f"RadioLib {label} source does not match pinned {EXPECTED_VERSION}; "
            "refusing an unsafe partial patch"
        )
    return text.replace(upstream, patched, 1)


def patch_radiolib(lib_dir: Path) -> None:
    metadata_path = lib_dir / "library.json"
    source_path = lib_dir / "src" / "modules" / "SX128x" / "SX128x.cpp"
    if not metadata_path.is_file() or not source_path.is_file():
        raise RuntimeError(f"pinned RadioLib dependency not found under {lib_dir}")

    metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
    version = str(metadata.get("version", ""))
    if version != EXPECTED_VERSION:
        raise RuntimeError(
            f"expected RadioLib {EXPECTED_VERSION}, found {version or 'unknown'}; "
            "review the integration patch before upgrading"
        )

    original = source_path.read_text(encoding="utf-8")
    updated = replace_exactly_once(
        original, REGULATOR_UPSTREAM, REGULATOR_PATCHED, "regulator"
    )
    updated = replace_exactly_once(updated, BUSY_UPSTREAM, BUSY_PATCHED, "BUSY timeout")
    updated = replace_exactly_once(
        updated, TX_DONE_UPSTREAM, TX_DONE_PATCHED, "TX-done hard timeout"
    )
    if updated != original:
        source_path.write_text(updated, encoding="utf-8")
        print(f"FanLink: patched RadioLib {EXPECTED_VERSION} SX128x integration")
    else:
        print(f"FanLink: verified RadioLib {EXPECTED_VERSION} SX128x integration patch")


libdeps_root = Path(env.subst("$PROJECT_LIBDEPS_DIR"))  # type: ignore[name-defined]
environment = env.subst("$PIOENV")  # type: ignore[name-defined]
patch_radiolib(libdeps_root / environment / "RadioLib")
