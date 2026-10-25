#!/bin/bash
# Build the kebab UEFI firmware without stuart.
#
# The edk2-pytool-extensions wrapper does not survive every Python update, and
# all it really does is set these variables and call `build`. This script is
# the distilled version; it reproduces the command recorded in
# Build/BUILDLOG_kebabPkg.txt.
#
#   ./build-kebab.sh            # DEBUG, CLANGPDB, AARCH64
#
# Output: Build/kebabPkg/DEBUG_CLANGPDB/FV/SILICIUM_UEFI.fd  (3 MiB)
set -euo pipefail
cd "$(dirname "$0")"

# The BaseTools wrappers look for `python`, which some distributions no longer
# provide. A private symlink keeps the system untouched.
BIN="$HOME/.local/share/kebab-fw-bin"
mkdir -p "$BIN"
[ -e "$BIN/python" ] || ln -sf "$(command -v python3)" "$BIN/python"

export WORKSPACE="$PWD"
# Recorded verbatim from the last successful stuart build.
export PACKAGES_PATH="$PWD:$PWD/Platforms/OnePlus:$PWD/Common/Mu:$PWD/Common/Mu_OEM_Sample:$PWD/Mu_Basecore:$PWD/Silicon/Qualcomm:$PWD/Silicon/Silicium:$PWD/Silicium-ACPI:$PWD/Silicium-ACPI/Platforms/OnePlus:$PWD/Silicium-ACPI/Silicon/Qualcomm"
export EDK_TOOLS_PATH="$PWD/Mu_Basecore/BaseTools"
export PATH="$BIN:$EDK_TOOLS_PATH/BinWrappers/PosixLike:$EDK_TOOLS_PATH/Bin/Mu-Basetools_extdep/Linux-x86:$EDK_TOOLS_PATH/Bin/mu_nasm_extdep/Linux-x86-64:/usr/lib/llvm-21/bin:$PATH"

build -p kebabPkg/kebab.dsc -b DEBUG -t CLANGPDB -a AARCH64 -n "$(nproc)" \
  -D ONE_CRYPTO_PATH="$WORKSPACE/Mu_Basecore/CryptoPkg/Binaries/onecrypto-bin_extdep" \
  -D BUILDID_STRING=Unknown -D MEMORY_PROTECTION=TRUE -D SHIP_MODE=FALSE \
  -D ENABLE_SECUREBOOT=0 -D FD_BASE=0x9fc00000 -D FD_SIZE=0x300000 -D FD_BLOCKS=0x300 \
  "$@"

ls -l Build/kebabPkg/DEBUG_CLANGPDB/FV/SILICIUM_UEFI.fd
