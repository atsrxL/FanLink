#!/usr/bin/env python3
"""Compile resolved PlatformIO board constants; no device/toolchain download."""

import configparser
import os
from pathlib import Path
import re
import shlex
import subprocess
import unittest


ROOT = Path(__file__).resolve().parents[1]
REFERENCE = re.compile(r"\$\{([^{}]+)\}")


def resolved_flags(config, section, seen=()):
    """Expand PlatformIO ${section.option} references, rejecting cycles."""
    def expand(section_name, option, chain):
        key = (section_name, option)
        if key in chain:
            raise ValueError(f"cyclic PlatformIO reference: {key}")
        value = config.get(section_name, option, raw=True)

        def replace(match):
            target_section, target_option = match.group(1).rsplit(".", 1)
            return expand(target_section, target_option, chain + (key,))

        return REFERENCE.sub(replace, value)

    return shlex.split(expand(section, "build_flags", seen))


class BoardProfilesTests(unittest.TestCase):
    def compile_assertions(self, definitions, assertions):
        source = '#include "fanlink/build_config.hpp"\n'
        source += "namespace b = fanlink::build;\n"
        source += "\n".join(
            f'static_assert({condition}, "{condition}");'
            for condition in assertions
        )
        result = subprocess.run(
            shlex.split(os.environ.get("CXX", "c++"))
            + ["-std=c++17", "-fsyntax-only", "-x", "c++",
               "-I", str(ROOT / "include")]
            + definitions + ["-"],
            input=source, text=True, capture_output=True, check=False,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_all_lite_environments_and_preserved_pins(self):
        config = configparser.ConfigParser(interpolation=None)
        config.read(ROOT / "platformio.ini")
        expected = {"env:fanlink_tx_esp8285"}
        expected.update(f"env:fanlink_rx_node{node:02d}" for node in range(1, 9))
        expected.add("env:fanlink_rx_node02_uart_diag")
        self.assertEqual({s for s in config.sections() if s.startswith("env:")}, expected)
        for section in sorted(expected):
            with self.subTest(environment=section):
                definitions = [flag for flag in resolved_flags(config, section)
                               if flag.startswith("-D")]
                self.assertIn("-DFANLINK_TARGET_BETAFPV_2400_LITE=1", definitions)
                self.assertFalse(any("NANO" in flag for flag in definitions))
                assertions = [
                    "b::kRadioOutputPowerDbm == 13",
                    "!b::kRadioUseDcdc",
                    "b::kRadioRxEnPin == -1", "b::kRadioTxEnPin == -1",
                    "b::kRadioNssPin == 15", "b::kRadioDio1Pin == 4",
                    "b::kRadioRstPin == 2", "b::kRadioBusyPin == 5",
                ]
                if section == "env:fanlink_tx_esp8285":
                    self.assertIn("-DFANLINK_BUILD_TX=1", definitions)
                    assertions += ["b::kStatusLedPin == -1"]
                else:
                    self.assertIn("-DFANLINK_BUILD_RX=1", definitions)
                    node = int(re.search(r"node(\d+)", section).group(1))
                    assertions += [f"b::kNodeId == {node}",
                                   "b::kStatusLedPin == 16", "b::kStatusLedActiveHigh"]
                    if section.endswith("uart_diag"):
                        self.assertIn("-DFANLINK_RX_UART_DIAGNOSTIC=1", definitions)
                        pwm, tach, gate = -1, -1, 1
                    elif node == 1:
                        pwm, tach, gate = 3, 1, 0
                    else:
                        pwm, tach, gate = 1, 3, 1
                    assertions += [f"b::kFanPwmPin == {pwm}",
                                   f"b::kTachPin == {tach}",
                                   f"b::kGateActiveHigh == {gate}"]
                self.compile_assertions(definitions, assertions)

    def test_rx_settings_profile_matches_lite_build(self):
        source = (ROOT / "src/main_rx.cpp").read_text()
        declaration = re.search(
            r"SettingsService\s+settingsService\s*\((.*?)\);", source, re.S
        )
        self.assertIsNotNone(declaration)
        self.assertRegex(declaration.group(1),
                         r"settings::Profile::Lite\s*,\s*defaultPinMode\s*,\s*"
                         r"fanlink::build::kRadioOutputPowerDbm")

    def test_unknown_board_keeps_low_power_fallback(self):
        self.compile_assertions([], ["b::kRadioOutputPowerDbm == -18"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
