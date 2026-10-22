# UniXNU-instantnoodle

> **Work in progress.** XNU reaches `Failed to mount root device` on the OnePlus 8. Storage/root publication and the iPadOS display/input stack remain unfinished.

Device repository for the OnePlus 8 (instantnoodle, IN2010, SM8250), laid out like [UniXNU-sheng](https://github.com/UniXNU/UniXNU-sheng). It started as a fork of [UniXNU-pipa](https://github.com/xVanTuring/UniXNU-pipa) (Xiaomi Pad 6, the same SoC). Firmware: [uefi-instantnoodle](https://github.com/haxi0/uefi-instantnoodle), branch `instantnoodle`, a fork of [uefi-sheng](https://github.com/UniXNU/uefi-sheng).

## State

| Stage | State |
| --- | --- |
| 1. UEFI | works: `fastboot boot` of the header v1 image, the Mu-Silicium DEBUG log on the panel |
| 2. UniXNU menu | works with an empty ramdisk (2026-10-22); with no entry it falls through to the Silicium boot manager, which resets to the bootloader |
| 3. Payload | works: 203 MB header-v1 image accepted by fastboot and handed to XNU |
| 4–5. DarwinBoot, kernel entry | confirmed: user saw entry probe bars and `Failed to mount root device` (2026-10-22) |
| 6. Storage and root | register probe passes on hardware; SMMU/DMA, disk reads and signed root publication remain |

## What is here

| File | Contents |
| --- | --- |
| `board.toml` | Board, IPSW, firmware, payload and ADT settings |
| `configs/instantnoodle_defconfig` | Eight-core bring-up kernel with opt-in UFS probes and board hardware glue |
| `scripts/make-instantnoodle-adt.py` | Apple device tree for SM8250: GICv3 0x17a00000/0x17a60000, stride 0x20000, Cortex-A55/A77 cores, 19.2 MHz timebase |
| `scripts/check-instantnoodle.py` | Firmware preflight: UniXNU integration, FD and stack against the memory map, vendor inputs, splash region |
| `scripts/build-platform-power.py`, `package/ShengPlatformPower.cpp`, `scripts/patch-xnu-entry-probe.py`, `scripts/xnu-panic-screen.c` | Copied unchanged from UniXNU-sheng through UniXNU-pipa |

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

The device repository includes `shell.nix` for reproducible tool selection on either development host:

```sh
cd unixnu
nix-shell ../UniXNU-instantnoodle/shell.nix
```

This supplies Linux firmware, packaging, QEMU and host-check tools. XNU and APFS image construction still require macOS. The shell does not change system networking, udev rules or GitHub authentication.

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

Kernel-entry milestone (2026-10-22): the user saw the green/blue/pink entry bars and `Failed to mount root device` after the RAM boot. That confirms the real phone reaches the expected no-root endpoint. The confirmed image used kernel `dbdee76f3234c75f8d1011ec2f4dc4c328a21187ba96f6ae113016b86f5e5ba8`. The payload menu timeout is 30 seconds, verified inside its FAT volume. No partition was flashed and the touch, keys and clock flags remained off. `boot/kernel-entry-confirmed.img` preserves that image locally.

Expected bring-up endpoint: DarwinBoot hands over and the kernel reaches `Failed to mount root device`. NativeUfs is compiled in probe-only mode and cannot publish a root device. This is preparation for hardware tests, not an iPadOS home-screen image.

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
- **Hardware qualification.** ABL, the large payload and kernel entry work. UFS, input, clocks, reset and a working iPadOS root still need separate hardware tests.
- **Vendor drivers.** kebab's vendor driver set runs as is; `AcpiPlatform.efi` fails to start (status 1) without visible effect so far.

## Prepared hardware support

The sources below pass host tests and compile and link against UniXNU's XNU 10063. [CI run 39823376112](https://github.com/haxi0/UniXNU/actions/runs/39823376112) passes the two-pass kernel, original PMGR bridge, EFI package and QEMU storage/display ABI checks. The no-root endpoint has also run on the phone. The UFS register probe also passes on hardware. Touch, keys, clocks, reset, SMMU takeover and disk reads remain untested under UniXNU. Success in the earlier iOS ports does not establish success here.

| Component | Preparation | Enable for a diagnostic run |
| --- | --- | --- |
| NativeUfs | SM8250 GCC registers, compatible string, SMMU IDs 0xe0/0x4e0 with mask 0x400 | `native-ufs-probe=2` reads controller registers; `=7` reads SMMU mappings; `=10` adopts the controller and reads GPT/userdata, without writing storage |
| Touch | S6SY761 over GPIO36/37; reset GPIO38; RPMh ldoc1/ldoa13; publishes the full contact set in NativeHidInput coordinates | `instantnoodle-touch=1` |
| Keys | PM8150 power, GPIO6/7 volume keys over SPMI v5; debounce and release after bus errors | `instantnoodle-buttons=1` |
| CPU clocks | Three EPSS domains at 0x18591000; bounded LUT selection and disabled-domain refusal | `instantnoodle-cpufreq=1` |
| Timed reset | IMEM bootloader reason at 0x146bf65c, followed by PSCI SYSTEM_RESET | `instantnoodle-autoreset=120` (30–3600 seconds after the board thread starts) |

Hardware UFS register probe (2026-10-22, `native-ufs-probe=2`): the supplied photo shows `hc_read=1`, `ver=00000300`, `hcs=0000000f` and `hce=00000001`. The GCC power/clock gate permitted controller reads, and the controller identifies as UFS HCI 3.0 with its enable bit set. This does not prove DMA or disk I/O. The photo is saved locally as `.cache/hardware/ufs-registers-2026-10-22.png`; `boot/ufs-registers-confirmed.img` preserves the tested image. Other small hexadecimal fields in the photo are not transcribed without clearer evidence.

The ADT also carries UniXNU-qemu's `kern.maxvnodes = 66560` policy for the 4 GiB owned-memory window. The BCM2837 build's 1024 default is too small for the iPadOS process set. This prepares the later userland boot; it has not been tested on this phone.

These flags are off by default. `offline-mobile-graphics-load=1 offline-legacy-register=1` in the board arguments invokes the board init hook after the original graphics family loads. Touch and key publication has no HID consumer until NativeHidInput and the display backend are linked. The current hook does not create a display service.

The reset timer needs a running kernel thread. It cannot recover a hard hang. It writes only the volatile IMEM restart word. Persistent boot logging and Data persistence are not enabled; no write window on userdata is opened by this configuration.

`package/NativeUfs-sm8250.patch` is applied to a build-directory copy of UniXNU/NativeUfs at `af785c1777161a62931425c2faaa536ce0bebd5b`. Hashes of the four affected inputs are checked before staging. The upstream submodule remains unchanged. This makes the port reproducible without an unpublished NativeUfs fork or an absolute local path.

The probe build excludes the original block-device subclass. A full root build must remove probe-only mode, qualify the original IOStorageFamily ABI, link the verified block transport and owner crypto/key archive, and supply owner-signed OS/SystemOS policies. No audit records or root keys are fabricated by this port.

## Host checks

Run from this device repository, with a C and C++ compiler plus Python 3.11+ and `patch`:

```sh
python3 scripts/check-board.py
python3 scripts/check-storage.py ../unixnu/drivers/NativeUfs
```

`CC` and `CXX` override the host compilers. The storage checks stage the patch in a temporary directory, test SMMU conflicts/reuse, both SoC constants, root publication ordering and UFS completion handling, and refuse a changed patch base. The board checks test contact decoding and bounds, key masks, EPSS tables, malformed command databases, and the generated eight-core ADT. Neither script accesses the phone.

## Original class ABI checks under QEMU

These helpers come from [UniXNU-qemu](https://github.com/xVanTuring/UniXNU-qemu), under BSD-2-Clause-Patent. They observe the original IOStorageFamily and IOMobileGraphicsFamily classes in a real QEMU guest. They do not replace runtime evidence with guessed metadata. `qemu_esp.py` uses this board's unmodified kernel with a separate one-core QEMU device tree and a public test identity. It never copies the phone payload, Data seed or private platform seed.

On macOS, after selecting this device and installing QEMU:

```sh
./unixnu build kernel
./unixnu build kernel              # bind the original PMGR bridge using the first candidate
./unixnu build package
python3 out/instantnoodle/tree/scripts/qualify-kernel-abi.py
```

The audit reports and raw observations are exported to `out/instantnoodle/kernel/abi/` for the kernel artifact. The current workflow audits its pre-build candidate, then performs its normal final kernel pass; the reports record the exact observed candidate hash. They must not be presented as a runtime test of that later binary. They qualify the original class layout, not physical storage, the panel, or successful iPadOS startup. `build-darwinboot-efi.py` builds the standalone loader QEMU needs; it respects `CLANG_BIN` on NixOS and saves its build log. EFI compilation and isolated ESP staging pass locally. The two-pass kernel also boots under local QEMU and passes the original framebuffer class audit (358 slots, 0x18a0 bytes, nine abstract leaves). CI also passes the live storage audit: native 4 KiB translation and the 190-slot original block-storage class. These are class ABI observations, not physical UFS tests.

To repeat the staging check on Linux after building the package and standalone DarwinBoot:

```sh
python3 scripts/check-qemu-esp.py ../unixnu/out/instantnoodle/tree
```

## Remaining gates

1. Run the prepared read-only `native-ufs-probe=7` image and capture its SMMU report before attempting DMA/disk reads. `boot/boot.img` currently contains this probe, with the green CI kernel `ce9e96a4371d43297dd96bae30e0325e6065c0b379f3e4250802e6cefa13a7db`. It carries no Data image.
2. Preserve the two-pass kernel build: `build-platform-power.py` requires the first prelinked kernel to bind the real PMGR bridge. Both passes have built successfully; the first-pass bridge alone is only a build stub.
3. Bind the qualified original storage ABI and prepare owner keys, signed OS/SystemOS policies and a Data seed. The published tb128fu board references unpublished ABI/key inputs; it is a source reference, not a reproducible ready-made build.
4. Build the CPU display backend and NativeHidInput against this kernel. Reuse the platform archive's touch state and call `instantnoodle_platform_init()` from the real display init, replacing the no-display hook.
5. Test storage probes, input, clocks and reset in stages, then prepare the userdata image. Flashing and persistent logging require explicit approval.
6. Adreno 650, QCA6390 PCIe/MHI Wi-Fi, Bluetooth, battery/charging and audio remain unported. Audio was explicitly deferred.

## Credits and license

Unless a file says otherwise, BSD-2-Clause-Patent; see [LICENSE](LICENSE). Forked from UniXNU-pipa by xVanTuring; `make-instantnoodle-adt.py` is derived from its `make-pipa-adt.py`, itself derived from `make-sheng-adt.py` of UniXNU-sheng. `build-arm-driver.py` originated in UniXNU-sheng and now stages the SM8250 UFS variant. The two panic-screen files are unchanged copies. `ShengPlatformPower.cpp` comes from it under MIT (its SPDX header). The firmware port is Mu-Silicium's `kebabPkg`.

The adapted touch, EPSS and SPMI code credits **OOSEX GANG / HackiOS**, [hackios-xnu-kernel](https://github.com/oosexgang/hackios-xnu-kernel), and the [haxi0 instantnoodle work](https://github.com/haxi0/hackios-xnu-kernel/commit/ce8ef8ff007e9ac42029973e594484bf7270d413). Their original kernel repository carries APSL 2.0; adapted files retain that license, included at [package/licenses/APSL-2.0.txt](package/licenses/APSL-2.0.txt). Touch also derives from [ios-sweet](https://github.com/dipa-studio/ios-sweet) and [ios-guacamole](https://github.com/wochgn/ios-guacamole). The port uses UniXNU's boot chain and interfaces; it does not depend on a HackiOS checkout or loader.

`TouchState.c` follows the BSD-licensed UniXNU-qemu/UniXNU-tb128fu publication interface. Register facts were cross-checked against Linux [gcc-sm8250.c](https://github.com/torvalds/linux/blob/master/drivers/clk/qcom/gcc-sm8250.c), [sm8250.dtsi](https://github.com/torvalds/linux/blob/master/arch/arm64/boot/dts/qcom/sm8250.dtsi), and the captured OnePlus 8 device tree.
