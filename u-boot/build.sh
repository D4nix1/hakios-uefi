#!/bin/bash
# Build U-Boot for a board profile on macOS or Linux.
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
UEFI_ROOT=$(cd "$HERE/.." && pwd)
PROFILE_ROOT=${HAKIOS_PROFILE_ROOT:-$UEFI_ROOT/platform}
PROFILE=${1:-}
if [ -z "$PROFILE" ]; then
	echo "usage: $0 <vendor/device>" >&2
	exit 2
fi
if [[ ! "$PROFILE" =~ ^[a-z0-9_-]+/[a-z0-9_-]+$ ]]; then
	echo "build.sh: profile must be vendor/device, got '$PROFILE'" >&2
	exit 2
fi
PROFILE_DIR="$PROFILE_ROOT/$PROFILE"
if [ ! -f "$PROFILE_DIR/profile.env" ]; then
	echo "build.sh: no buildable profile at platform/$PROFILE/profile.env" >&2
	exit 2
fi
source "$PROFILE_DIR/profile.env"
if [[ "${PROFILE_STATUS:-ready}" != "ready" ]]; then
	if [[ "${PROFILE_STATUS:-}" == "experimental" && "${ALLOW_EXPERIMENTAL:-0}" == "1" ]]; then
		echo "WARNING: building experimental profile $PROFILE; do not treat this image as boot-tested" >&2
	else
		echo "U-Boot: profile $PROFILE is not marked ready (PROFILE_STATUS=${PROFILE_STATUS:-unset}); set ALLOW_EXPERIMENTAL=1 only for an offline experimental build" >&2
		exit 1
	fi
fi
DEVICE=${PROFILE#*/}
UB=${U_BOOT:-$UEFI_ROOT/build/u-boot-$DEVICE-src}
if [[ "${IN_TREE_BUILD:-0}" == "1" ]]; then
	OUT="$UB"
else
	OUT=${U_BOOT_OUT:-$UEFI_ROOT/build/u-boot-$DEVICE}
fi
JOBS=${JOBS:-}

U_BOOT="$UB" bash "$HERE/apply.sh" "$PROFILE"

if [ -z "${CROSS_COMPILE:-}" ]; then
	for prefix in aarch64-elf- aarch64-linux-gnu- aarch64-none-elf-; do
		if command -v "${prefix}gcc" >/dev/null 2>&1; then
			CROSS_COMPILE=$prefix
			break
		fi
	done
fi
if [ -z "${CROSS_COMPILE:-}" ]; then
	echo "build.sh: install an AArch64 GCC toolchain or set CROSS_COMPILE" >&2
	exit 1
fi

if [ "$(uname -s)" = Darwin ]; then
	BREW=${HOMEBREW_PREFIX:-$(brew --prefix)}
	TOOLS="$UEFI_ROOT/build/u-boot-host-tools"
	mkdir -p "$TOOLS"
	GMAKE=${GNU_MAKE:-$(command -v gmake || true)}
	GBASH=${GNU_BASH:-$BREW/bin/bash}
	NCPU=$(sysctl -n hw.ncpu)
	PYTHON=${PYTHON3:-$(command -v python3.12 || command -v python3)}
	OPENSSL=${OPENSSL_PREFIX:-$(brew --prefix openssl@3)}
	[ -x "$GMAKE" ] || { echo "build.sh: GNU make is required (brew install make)" >&2; exit 1; }
	[ -x "$GBASH" ] || { echo "build.sh: GNU Bash 5 is required (brew install bash)" >&2; exit 1; }
	[ -n "$PYTHON" ] && [ -x "$PYTHON" ] || { echo "build.sh: Python 3 is required" >&2; exit 1; }
	[ -d "$OPENSSL/include/openssl" ] || { echo "build.sh: OpenSSL 3 headers are required (brew install openssl@3)" >&2; exit 1; }
	cat > "$TOOLS/make" <<EOF
#!/bin/sh
exec "$GMAKE" "\$@"
EOF
	cat > "$TOOLS/bash" <<EOF
#!/bin/sh
exec "$GBASH" "\$@"
EOF
	cat > "$TOOLS/nproc" <<EOF
#!/bin/sh
echo "$NCPU"
EOF
	ln -sf "$PYTHON" "$TOOLS/python3"
	chmod +x "$TOOLS/make" "$TOOLS/bash" "$TOOLS/nproc"
	export PATH="$TOOLS:$PATH"
	export HOSTCFLAGS="${HOSTCFLAGS:-} -I$OPENSSL/include"
	export HOSTLDFLAGS="${HOSTLDFLAGS:-} -L$OPENSSL/lib"
	export PKG_CONFIG_PATH="$OPENSSL/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
	JOBS=${JOBS:-$NCPU}
else
	JOBS=${JOBS:-$(nproc)}
fi

# MSYS2 process startup is expensive for the per-symbol grep loop in autoconf.mk.
# The generated Kconfig values are authoritative; duplicate make assignments are harmless.
if [[ -n "${MSYSTEM:-}" ]]; then
	export KCONFIG_IGNORE_DUPLICATES=1
fi
MAKE=${MAKE:-make}
if [[ "${IN_TREE_BUILD:-0}" == "1" ]]; then
	echo "build.sh: using disposable in-tree build directory $UB"
	"$MAKE" -C "$UB" -s CROSS_COMPILE="$CROSS_COMPILE" "$UBOOT_DEFCONFIG"
	"$MAKE" -C "$UB" -s CROSS_COMPILE="$CROSS_COMPILE" -j"$JOBS"
else
	"$MAKE" -C "$UB" -s O="$OUT" CROSS_COMPILE="$CROSS_COMPILE" "$UBOOT_DEFCONFIG"
	"$MAKE" -C "$UB" -s O="$OUT" CROSS_COMPILE="$CROSS_COMPILE" -j"$JOBS"
fi

IMAGE="$OUT/u-boot.bin"
[ -s "$IMAGE" ] || { echo "build.sh: U-Boot did not produce $IMAGE" >&2; exit 1; }
if ! "${CROSS_COMPILE}readelf" -h "$OUT/u-boot" 2>/dev/null | grep -q "Machine:.*AArch64"; then
	echo "build.sh: $OUT/u-boot is not an AArch64 ELF executable" >&2
	exit 1
fi
ls -lh "$IMAGE"
echo "build.sh: profile $PROFILE built"
echo "build.sh: U-Boot: $IMAGE"
echo "build.sh: DTB:    ${DEVICE_FIRMWARE:-$UEFI_ROOT/local-firmware}/$FIRMWARE_DIR/$PROFILE_DTB"
