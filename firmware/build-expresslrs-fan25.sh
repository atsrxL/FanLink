#!/bin/sh
set -eu

if [ "$#" -ne 4 ]; then
  echo "usage: $0 SOURCE_ROOT OPTIONS_JSON INPUT_DIR ARTIFACT_DIR" >&2
  exit 64
fi

source_root=$1
options_json=$2
input_dir=$3
artifact_dir=$4
build_env=Unified_ESP8285_2400_RX_via_UART
pio_venv=$(dirname "$source_root")/.pio-venv
build_log=$(dirname "$source_root")/platformio-build.log
firmware_bin=$source_root/src/.pio/build/$build_env/firmware.bin

python3 -m venv "$pio_venv"
"$pio_venv/bin/pip" install --disable-pip-version-check --quiet platformio==6.1.18 wheel

cd "$source_root/src"
PLATFORMIO_BUILD_FLAGS="-DRegulatory_Domain_ISM_2400" \
ELRS_UNIFIED_CONFIG=betafpv.rx_2400.nano \
  "$pio_venv/bin/pio" run -e "$build_env" >"$build_log" 2>&1

"$pio_venv/bin/python" - "$options_json" "$firmware_bin" <<'PY'
import json
import sys

sys.path.insert(0, "python")
from UnifiedConfiguration import doConfiguration

options_path, firmware_path = sys.argv[1:]
with open(options_path, encoding="utf-8") as handle:
    options = json.load(handle)

with open(firmware_path, "r+b") as firmware:
    doConfiguration(
        firmware,
        json.dumps(options, separators=(",", ":")),
        "betafpv.rx_2400.nano",
        "Unified_ESP8285_2400_RX_via_UART",
        None,
        None,
    )
PY

mkdir -p "$artifact_dir"
cp "$firmware_bin" "$artifact_dir/BETAFPV_Nano_2400_RX_4.1.0_FAN25_OpenDrain.bin"
gzip -9 -n -c "$firmware_bin" >"$artifact_dir/BETAFPV_Nano_2400_RX_4.1.0_FAN25_OpenDrain.bin.gz"
cp "$build_log" "$artifact_dir/platformio-build.log"
cp "$input_dir/expresslrs-4.1.0-25khz-open-drain.patch" "$artifact_dir/"
cp "$input_dir/betafpv-nano-2400-fan-hardware.json" "$artifact_dir/"
cp "$input_dir/README.md" "$artifact_dir/"

"$pio_venv/bin/python" - "$options_json" "$artifact_dir/BETAFPV_Nano_2400_RX_4.1.0_FAN25_OpenDrain.bin" <<'PY'
import io
import json
import struct
import sys

options_path, firmware_path = sys.argv[1:]
data = open(firmware_path, "rb").read()
stream = io.BytesIO(data)
magic, segments, _, _, _ = struct.unpack("<BBBBI", stream.read(8))
assert magic == 0xE9
if segments == 2:
    stream.seek(0x1000)
    magic, segments, _, _, _ = struct.unpack("<BBBBI", stream.read(8))
    assert magic == 0xE9
else:
    stream.seek(24)
for _ in range(segments):
    _, size = struct.unpack("<II", stream.read(8))
    stream.seek(size, 1)
firmware_end = (stream.tell() + 16) & ~15

product = data[firmware_end:firmware_end + 128].split(b"\0", 1)[0].decode()
lua_name = data[firmware_end + 128:firmware_end + 144].split(b"\0", 1)[0].decode()
options = json.loads(data[firmware_end + 144:firmware_end + 656].split(b"\0", 1)[0])
hardware = json.loads(data[firmware_end + 656:firmware_end + 2704].split(b"\0", 1)[0])
expected_options = json.load(open(options_path, encoding="utf-8"))

assert product == "BETAFPV 2.4GHz Nano RX"
assert lua_name == "BFPV Nano 2G4RX"
assert options == expected_options
assert isinstance(options.get("uid"), list) and len(options["uid"]) == 6
assert "serial_rx" not in hardware and "serial_tx" not in hardware
assert hardware["pwm_outputs"] == [1]
assert hardware["pwm_out_only"] is False
assert hardware["power_rxen"] == 9 and hardware["power_txen"] == 10
assert hardware["radio_dcdc"] is True
print("CONFIG_VERIFY_OK product=BETAFPV_Nano uid_preserved=yes pwm_gpio=1 uart0=disabled")
PY

gzip -dc "$artifact_dir/BETAFPV_Nano_2400_RX_4.1.0_FAN25_OpenDrain.bin.gz" \
  | cmp - "$artifact_dir/BETAFPV_Nano_2400_RX_4.1.0_FAN25_OpenDrain.bin"

{
  echo "ExpressLRS source: 4.1.0 ($(git -C "$source_root" rev-parse HEAD))"
  echo "ExpressLRS targets: $(git -C "$source_root/src/hardware" rev-parse HEAD)"
  echo "PlatformIO: 6.1.18"
  echo "Target: betafpv.rx_2400.nano"
  echo "RF domain: ISM 2.4 GHz"
  echo "PWM mode 6: 25 kHz open-drain fan duty"
  echo "GPIO1 PWM; UART0 RX/TX disabled"
  echo "Binding UID and firmware options preserved: yes"
  echo
  sha256sum "$artifact_dir"/BETAFPV_Nano_2400_RX_4.1.0_FAN25_OpenDrain.bin \
    "$artifact_dir"/BETAFPV_Nano_2400_RX_4.1.0_FAN25_OpenDrain.bin.gz
} >"$artifact_dir/MANIFEST.txt"

chmod 0600 "$artifact_dir"/*.bin "$artifact_dir"/*.bin.gz
chmod 0644 "$artifact_dir"/*.json "$artifact_dir"/*.md \
  "$artifact_dir"/*.patch "$artifact_dir"/*.log "$artifact_dir"/MANIFEST.txt
cat "$artifact_dir/MANIFEST.txt"
