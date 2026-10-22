# UniXNU-instantnoodle

> **Work in progress.** The firmware reaches the UniXNU menu on the phone; XNU has not run yet.

Device repository for the OnePlus 8 (instantnoodle, IN2010, SM8250), laid out like [UniXNU-sheng](https://github.com/UniXNU/UniXNU-sheng). It started as a fork of [UniXNU-pipa](https://github.com/xVanTuring/UniXNU-pipa) (Xiaomi Pad 6, the same SoC). Firmware: [uefi-instantnoodle](https://github.com/haxi0/uefi-instantnoodle), branch `instantnoodle`, a fork of [uefi-sheng](https://github.com/UniXNU/uefi-sheng).

## State

| Stage | State |
| --- | --- |
| 1. UEFI | works: `fastboot boot` of the header v1 image, the Mu-Silicium DEBUG log on the panel |
| 2. UniXNU menu | works with an empty ramdisk (2026-10-22); with no entry it falls through to the Silicium boot manager, which resets to the bootloader |
| 3. Payload | not tried: needs the kernel |
| 4–5. DarwinBoot, kernel entry | not tried |
| 6. Storage and root | needs NativeUfs for SM8250 |

## What is here

| File | Contents |
| --- | --- |
| `board.toml` | Board, IPSW, firmware, payload and ADT settings |
| `configs/instantnoodle_defconfig` | Kernel configuration of the first image (pipa's) |
| `scripts/make-instantnoodle-adt.py` | Apple device tree for SM8250: GICv3 0x17a00000/0x17a60000, stride 0x20000, Cortex-A55/A77 cores, 19.2 MHz timebase |
| `scripts/check-instantnoodle.py` | Firmware preflight: UniXNU integration, FD and stack against the memory map, vendor inputs, splash region |
| `scripts/build-arm-driver.py`, `scripts/build-platform-power.py`, `package/ShengPlatformPower.cpp`, `scripts/patch-xnu-entry-probe.py`, `scripts/xnu-panic-screen.c` | Copied unchanged from UniXNU-sheng through UniXNU-pipa |

pipa's ramoops log reader (`scripts/read-pipa-log.py`, `package/linux-log`) is not carried: it reads a pipa Linux kernel's pstore.

`uefi-instantnoodle` changes on top of uefi-sheng:

| File | Change |
| --- | --- |
| `Platforms/OnePlus/instantnoodlePkg/` | Mu-Silicium's OnePlus 8T port (`kebabPkg`: Kona, the same 1080x2400 frame buffer and FD layout) under the OnePlus 8's name; vendor drivers (`Binaries/kebab`) and ACPI tables (`kebab/AcpiTables.inf`) unchanged |
| `instantnoodle.dsc`, `instantnoodle.fdf` | UniXNU, UniXNUDxe, the ramdisk block device driver, DarwinBoot, UniXNULib and FdtLib, as in pipa |
| `Include/DXE.inc` | `ShengBootMemoryDxe` (publishes the boot image ramdisk as a block device) and DarwinBoot |
| `Resources/Configs/instantnoodle.toml` | kebab's, with the boot image base, offsets and page size written out (mkbootimg defaults, header v1, page 2048: what ABL accepts) |
| `Resources/DTBs/instantnoodle.dtb` | The first tree of the stock boot image's DTB (kona v2.1, msm-id 0x164 0x20001), the one ABL picks on this phone |

`ENABLE_SECUREBOOT` is `0` in `board.toml`: with it on, SecurityStubDxe never installs the Security Arch protocol on Kona, DxeCore asserts (`DxeMain.c(581)`) and DarwinBoot is never dispatched.

## Host tools

The [requirements page](https://unixnu.com/guide/requirements) lists them. The firmware, package and boot image steps run on Linux; the kernel and userdata steps need macOS (the macOS build workflow of a UniXNU fork). On NixOS the CLANGPDB tool chain needs `CLANG_BIN` pointing at an unwrapped clang with lld and the llvm tools: the Nix wrapper's `--gcc-toolchain` is an unused argument there, an error under `-Werror`.

## Stage 1 to 3: firmware, menu and payload volume

```sh
export UNIXNU_UEFI=$PWD/uefi-instantnoodle   # the preflight reads the checkout from here
cd UniXNU                                    # cloned beside this repository
git submodule update --init --recursive

./unixnu defconfig instantnoodle_defconfig -D ../UniXNU-instantnoodle
./unixnu fetch
./unixnu build package
./unixnu build firmware --uefi "$UNIXNU_UEFI"
./unixnu bootimg --uefi "$UNIXNU_UEFI" --dtb "$UNIXNU_UEFI/Resources/DTBs/instantnoodle.dtb" --ramdisk none

fastboot boot out/instantnoodle/boot/boot.img
```

`fastboot boot` writes nothing to the phone. After a reboot the OnePlus 8's bootloader often needs the USB cable replugged before fastboot answers.

## Stage 4 and 5: kernel entry

```sh
./unixnu ipsw fetch
./unixnu build kernel                        # macOS only

./unixnu build payload --unique-chip-id <16 hex digits>
./unixnu bootimg --uefi "$UNIXNU_UEFI" --dtb "$UNIXNU_UEFI/Resources/DTBs/instantnoodle.dtb"
fastboot boot out/instantnoodle/boot/boot.img
```

The platform seed is any nonzero 64-bit value kept for this phone, for example from `openssl rand -hex 8`. It never goes into this repository.

Expected, as on pipa: DarwinBoot hands over, the entry probe bars appear at the top of the screen, and the panic screen shows `Failed to mount root device`, as there is no storage driver yet.

## Values taken from the phone

Read under Android (OxygenOS 11) and recovery, 2026-10-21 and 2026-10-22.

| Value | Read | In this repository |
| --- | --- | --- |
| DRAM banks | 0x80000000+0x3bb00000, 0xc0000000+0xc0000000, 0x180000000+0x100000000 (8 GiB) | `dram_size = 0x200000000`, base to the end of the last bank |
| userdata | sda23 on LUN 0, LBA 4133800..30513146, 4096-byte blocks | `[userdata]` |
| Boot image of the stock `boot` | header v2, page 4096; slot `_b` | the firmware image uses header v1, page 2048 |
| CPUs, GIC | four Cortex-A55, four Cortex-A77, GICv3 0x17a00000/0x17a60000 | as in `make-instantnoodle-adt.py` |
| CPU frequency | EPSS 0x18591000, domains cpu0-3, cpu4-6, cpu7 | for a cpufreq driver |
| Touch | Samsung S6SY761 at 0x48 on the I2C of gpio36/37 (SE13), reset gpio38, interrupt gpio39; rails ldoc1 1.8 V, ldoa13 3.008 V | for the touch driver |
| Keys | power: PM8150 PON 0x0800 (KPDPWR); volume up/down: PM8150 GPIO6/GPIO7 (0xc500/0xc600); SPMI arbiter v5 at 0xc440000 | for a key driver |
| Panel | 1080x2400, `cont_splash_region` 0x9c000000 | the frame buffer of SimpleFbDxe |
| GPU | Adreno 650 | |

## Known risks

- **userdata.** The root image is written with `fastboot flash userdata`, which erases Android's data partition.
- **ABL acceptance** of a large ramdisk in a header v1 image is unverified on this phone.
- **Vendor drivers.** kebab's vendor driver set runs as is; `AcpiPlatform.efi` fails to start (status 1) without visible effect so far.

## Next steps

1. Kernel through the macOS build workflow, then the payload and stages 4–5.
2. NativeUfs for SM8250: GCC offsets from `gcc-sm8250.c` in place of the SM8550 ones.
3. Touch (S6SY761), keys (PM8150 PON and GPIO) and EPSS cpufreq as UniXNU drivers.
4. GPU: Adreno 650 needs its own GMU, HFI and PM4 work.

## Credits and license

BSD-2-Clause-Patent. See [LICENSE](LICENSE). Forked from UniXNU-pipa by xVanTuring; `make-instantnoodle-adt.py` is derived from its `make-pipa-adt.py`, itself derived from `make-sheng-adt.py` of UniXNU-sheng. `build-arm-driver.py` and the two panic-screen files are copies from UniXNU-sheng. `ShengPlatformPower.cpp` comes from it under MIT (its SPDX header). The firmware port is Mu-Silicium's `kebabPkg`.
