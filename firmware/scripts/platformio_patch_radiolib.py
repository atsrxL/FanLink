"""Apply the FanLink RadioLib 7.7.1 SX128x safety integration patch.

PlatformIO installs each environment's dependencies under ``.pio/libdeps``.
This pre-build hook makes narrow, fail-closed edits to the pinned RadioLib
source before it is compiled:

* honour FANLINK_RADIO_USE_DCDC during the initial SX128x configuration, and
* bound the otherwise unbounded BUSY wait in SX128x::launchMode(), and
* hard-cap the blocking transmit wait even when FLRC time-on-air is invalid,
* verify TX_DONE with a checked IRQ read before declaring transmit success,
* release the RF front end on failed mode transitions without more SPI waits,
* preserve cleanup errors and release the RF front end before IRQ cleanup, and
* make chip discovery a single bounded attempt for runtime radio recovery.

The exact upstream version and source snippets are checked so an upstream
change cannot silently receive a partial or misplaced patch.
"""

from __future__ import annotations

import json
from pathlib import Path

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

BUSY_PREVIOUS = """      // wait for BUSY to go low (= PA ramp up done), but never forever.
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

BUSY_PATCHED = BUSY_PREVIOUS.replace(
    "          return(RADIOLIB_ERR_SPI_CMD_TIMEOUT);",
    "          this->stagedMode = RADIOLIB_RADIO_MODE_NONE;\n"
    "          return(RADIOLIB_ERR_SPI_CMD_TIMEOUT);",
)

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

TX_VERIFY_UPSTREAM = """  return(finishTransmit());
}

int16_t SX128x::receive(uint8_t* data, size_t len, RadioLibTime_t timeout) {
"""

TX_VERIFY_PATCHED = """  // DIO1 may be stuck high: only a checked TX_DONE IRQ establishes completion.
  // Release the external front end before an IRQ read that could hit BUSY.
  this->mod->setRfSwitchState(Module::MODE_IDLE);
  uint8_t txIrqBytes[2] = { 0, 0 };
  state = this->mod->SPIreadStream(RADIOLIB_SX128X_CMD_GET_IRQ_STATUS, txIrqBytes, 2);
  if(state != RADIOLIB_ERR_NONE) {
    // Preserve the first SPI error and do not append cleanup SPI timeouts.
    this->stagedMode = RADIOLIB_RADIO_MODE_NONE;
    return(state);
  }
  uint16_t txIrq = ((uint16_t)txIrqBytes[0] << 8) | txIrqBytes[1];
  if(!(txIrq & RADIOLIB_SX128X_IRQ_TX_DONE)) {
    // IRQ/FIFO cleanup is bounded; its error must not erase missing TX_DONE.
    (void)finishTransmit();
    return(RADIOLIB_ERR_TX_TIMEOUT);
  }
  return(finishTransmit());
}

int16_t SX128x::receive(uint8_t* data, size_t len, RadioLibTime_t timeout) {
"""

LAUNCH_RX_UPSTREAM = """      state = setRx(this->rxTimeout);
      RADIOLIB_ASSERT(state);
"""

LAUNCH_RX_PATCHED = """      state = setRx(this->rxTimeout);
      if(state != RADIOLIB_ERR_NONE) {
        // Do not leave the front end enabled, or add SPI waits after a fault.
        this->mod->setRfSwitchState(Module::MODE_IDLE);
        this->stagedMode = RADIOLIB_RADIO_MODE_NONE;
        return(state);
      }
"""

LAUNCH_TX_UPSTREAM = """      state = setTx(RADIOLIB_SX128X_TX_TIMEOUT_NONE);
      RADIOLIB_ASSERT(state);
"""

LAUNCH_TX_PATCHED = """      state = setTx(RADIOLIB_SX128X_TX_TIMEOUT_NONE);
      if(state != RADIOLIB_ERR_NONE) {
        // setTx itself can time out before the later BUSY guard is reached.
        this->mod->setRfSwitchState(Module::MODE_IDLE);
        this->stagedMode = RADIOLIB_RADIO_MODE_NONE;
        return(state);
      }
"""

FINISH_TX_UPSTREAM = """int16_t SX128x::finishTransmit() {
  // clear interrupt flags
  clearIrqStatus();

  // set mode to standby to disable transmitter/RF switch
  return(standby());
}
"""

FINISH_TX_PATCHED = """int16_t SX128x::finishTransmit() {
  // Release the external PA before any SPI cleanup, even with BUSY stuck high.
  this->mod->setRfSwitchState(Module::MODE_IDLE);
  this->stagedMode = RADIOLIB_RADIO_MODE_NONE;

  // Preserve the first cleanup error and do not add another SPI timeout to it.
  int16_t state = clearIrqStatus();
  RADIOLIB_ASSERT(state);
  return(standby());
}
"""

RESET_VERIFY_UPSTREAM = """  // set mode to standby
  RadioLibTime_t start = this->mod->hal->millis();
  while(true) {
    // try to set mode to standby
    int16_t state = standby();
    if(state == RADIOLIB_ERR_NONE) {
      // standby command successful
      return(RADIOLIB_ERR_NONE);
    }

    // standby command failed, check timeout and try again
    if(this->mod->hal->millis() - start >= 3000) {
      // timed out, possibly incorrect wiring
      return(state);
    }

    // wait a bit to not spam the module
    this->mod->hal->delay(10);
  }
"""

RESET_VERIFY_PATCHED = """  #if defined(FANLINK_RADIO_BUSY_TIMEOUT_MS)
    // Allow boot/calibration and BUSY to settle after releasing RESET. Then use
    // the normal SPI/BUSY guard; a freshly reset chip does not need a wake NOP.
    // Runtime retries belong to the controller, not a three-second loop here.
    this->mod->hal->delay(10);
    return(standby(RADIOLIB_SX128X_STANDBY_RC, false));
  #else
""" + "".join("  " + line for line in RESET_VERIFY_UPSTREAM.splitlines(keepends=True)) + """  #endif
"""

FIND_CHIP_UPSTREAM = """  while((i < 10) && !flagFound) {
    // reset the module
    reset(true);
"""

FIND_CHIP_PATCHED = """  #if defined(FANLINK_RADIO_BUSY_TIMEOUT_MS)
    const uint8_t maxAttempts = 1;
  #else
    const uint8_t maxAttempts = 10;
  #endif
  while((i < maxAttempts) && !flagFound) {
    // Reset once per controller recovery attempt; do not read a failed chip.
    #if defined(FANLINK_RADIO_BUSY_TIMEOUT_MS)
      if(reset(true) != RADIOLIB_ERR_NONE) {
        return(false);
      }
    #else
      reset(true);
    #endif
"""


def replace_exactly_once(
    text: str, upstream: str, patched: str, label: str, previous: str | None = None
) -> str:
    if patched in text:
        if text.count(patched) != 1:
            raise RuntimeError(f"RadioLib {label} patch marker is not unique")
        return text
    if previous is not None and previous in text:
        if text.count(previous) != 1:
            raise RuntimeError(f"RadioLib previous {label} patch marker is not unique")
        return text.replace(previous, patched, 1)
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
    updated = replace_exactly_once(
        updated, BUSY_UPSTREAM, BUSY_PATCHED, "BUSY timeout", BUSY_PREVIOUS
    )
    updated = replace_exactly_once(
        updated, TX_DONE_UPSTREAM, TX_DONE_PATCHED, "TX-done hard timeout"
    )
    for upstream, patched, label in (
        (TX_VERIFY_UPSTREAM, TX_VERIFY_PATCHED, "checked TX-done verification"),
        (LAUNCH_RX_UPSTREAM, LAUNCH_RX_PATCHED, "failed RX cleanup"),
        (LAUNCH_TX_UPSTREAM, LAUNCH_TX_PATCHED, "failed TX cleanup"),
        (FINISH_TX_UPSTREAM, FINISH_TX_PATCHED, "TX finish cleanup"),
        (RESET_VERIFY_UPSTREAM, RESET_VERIFY_PATCHED, "bounded reset verification"),
        (FIND_CHIP_UPSTREAM, FIND_CHIP_PATCHED, "single chip-discovery attempt"),
    ):
        updated = replace_exactly_once(updated, upstream, patched, label)
    if updated != original:
        source_path.write_text(updated, encoding="utf-8")
        print(f"FanLink: patched RadioLib {EXPECTED_VERSION} SX128x integration")
    else:
        print(f"FanLink: verified RadioLib {EXPECTED_VERSION} SX128x integration patch")


Import("env")  # type: ignore[name-defined]  # Provided by PlatformIO/SCons.
libdeps_root = Path(env.subst("$PROJECT_LIBDEPS_DIR"))  # type: ignore[name-defined]
environment = env.subst("$PIOENV")  # type: ignore[name-defined]
patch_radiolib(libdeps_root / environment / "RadioLib")
