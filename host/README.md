# host/ — host-side tooling

Everything that runs on the PC to talk to the board over USB.

## Files

| File | Description |
|------|-------------|
| `criptolite-ice.py` | Command-line client for the CryptoLite-RP firmware. Implements the binary USB Vendor protocol (the one defined in `mcu/protocol.h`) on top of `pyusb`. |
| `99-cryptolite.rules` | Linux udev rule that grants non-root access to the device (VID `2e8a`, PID `000f`). Without it, `pyusb` needs `sudo`. |

## Requirements

- Python 3.9 or newer
- `pyusb` and a working `libusb-1.0`

```bash
python3 -m venv .venv
.venv/bin/pip install -r requirements.txt
```

## Installing the udev rule (Linux)

```bash
sudo cp 99-cryptolite.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
sudo udevadm trigger
```

## Usage

```bash
./criptolite-ice.py status                              # board status block
./criptolite-ice.py random -n 32                        # 32 bytes, printed as hex
./criptolite-ice.py random -n 1048576 -o rng.bin        # 1 MiB to a file
./criptolite-ice.py random --bits 1000000 -o rng.bin    # N bits -> ceil(N/8) bytes
./criptolite-ice.py update-fpga    bitstream.bin   [-y]  # re-flash the iCE40
./criptolite-ice.py update-rp2040  firmware.uf2    [-y]  # re-flash the RP2040
```

| Subcommand | Description |
|------------|-------------|
| `status` | Print board status: CDONE, LED state, NIST health, bytes processed. |
| `random` | Generate random bytes. Without `-o` it prints hex to stdout; with `-o file` it writes raw bytes. Use `-n N` for bytes or `--bits N` (rounded up to whole bytes). The firmware caps a single response at 256 bytes; the host loops internally for larger requests. |
| `update-fpga` | Erase the configuration flash and program a new bitstream. The FPGA is held in reset during programming and rebooted at the end. Asks for confirmation unless `-y` is given. |
| `update-rp2040` | Reboot the RP2040 into the BOOTSEL ROM and copy a `.uf2` to the resulting `RPI-RP2` volume. Asks for confirmation unless `-y` is given. |

To get a global command, symlink the script into your `PATH`:

```bash
ln -s "$PWD/criptolite-ice.py" ~/.local/bin/criptolite-ice
```
