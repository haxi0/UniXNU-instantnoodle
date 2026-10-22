# UniXNU on NixOS: `nix-shell ../UniXNU-instantnoodle/shell.nix` from the unixnu checkout gives the Linux-side host
# tools (docs/guide/requirements.md). The kernel and userdata steps need macOS (the macOS build workflow).
{ pkgs ? import <nixpkgs> { } }:
let
  llvm = pkgs.llvmPackages_21;
  # Mu-Silicium's CLANGPDB toolchain (edk2 tools_def: $CLANG_BIN): unwrapped clang with lld and the
  # llvm tools; the Nix wrapper's --gcc-toolchain is an unused argument there, an error under -Werror
  clangPdb = pkgs.symlinkJoin { name = "clang-pdb"; paths = [ llvm.clang-unwrapped llvm.lld llvm.llvm ]; };
in
pkgs.mkShell {
  packages = with pkgs; [
    git curl gcc llvm.clang-unwrapped llvm.lld llvm.llvm python3 gnumake patch qemu
    libuuid                     # edk2 BaseTools
    nasm acpica-tools           # Mu-Silicium: nasm in place of mu_nasm, iasl for ACPI
    mtools dtc                  # payload volume, device tree properties
    android-tools               # fastboot
  ];
  hardeningDisable = [ "all" ];
  CC = "${pkgs.gcc}/bin/gcc";
  CXX = "${pkgs.gcc}/bin/g++";
  CLANG_BIN = "${clangPdb}/bin/";
}
