#!/bin/bash
# Deploy the CrankLab libretro core to a handheld Linux system (EmuELEC, ArkOS, ROCKNIX, ...) over SSH.
#
#   ./deploy.sh [options] USER@HOST
#
# It detects the device architecture, builds the matching core if needed, reads the
# directories RetroArch actually uses from its retroarch.cfg, copies the files, and prints
# the command to start a game by hand. Nothing is installed into read-only locations.
#
# Options:
#   --diagnose            only print what the device looks like; copy nothing
#   --arch ARCH           aarch64 | armhf | x86_64 (default: detect)
#   --dest DIR            where the core and info file go on the device (default: /storage/cranklab)
#   --system-dir DIR      RetroArch system directory (default: read from retroarch.cfg)
#   --cfg FILE            retroarch.cfg on the device (default: search common places)
#   --game FILE           also upload a local game (.zip, or any file) to --rom-dir
#   --rom-dir DIR         where --game goes (default: /storage/roms/playdate)
#   --install-core        also copy the core into RetroArch's own cores directory if writable
#   --no-build            never build; fail if the core for this architecture is missing
#   --dry-run             print the plan without touching the device
#   -h, --help
#
# Environment:
#   ZIG           how to call zig for the build, e.g. ZIG="python3 -m ziglang"
#   DEPLOY_SSH    ssh command (default: ssh), e.g. "ssh -p 2222 -i key"
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
arch="" dest="/storage/cranklab" system_dir="" cfg="" game="" rom_dir="/storage/roms/playdate"
diagnose=0 install_core=0 build=1 dry=0 target=""

usage() { sed -n '2,/^set -euo/p' "${BASH_SOURCE[0]}" | sed '$d' | sed 's/^# \{0,1\}//'; }
die() { echo "error: $*" >&2; exit 1; }
say() { echo "==> $*"; }

while [ $# -gt 0 ]; do
    case "$1" in
        --diagnose) diagnose=1 ;;
        --arch) arch="${2:?--arch needs a value}"; shift ;;
        --dest) dest="${2:?--dest needs a value}"; shift ;;
        --system-dir) system_dir="${2:?--system-dir needs a value}"; shift ;;
        --cfg) cfg="${2:?--cfg needs a value}"; shift ;;
        --game) game="${2:?--game needs a value}"; shift ;;
        --rom-dir) rom_dir="${2:?--rom-dir needs a value}"; shift ;;
        --install-core) install_core=1 ;;
        --no-build) build=0 ;;
        --dry-run) dry=1 ;;
        -h|--help) usage; exit 0 ;;
        -*) die "unknown option $1 (see --help)" ;;
        *) [ -z "$target" ] || die "only one USER@HOST is accepted"; target="$1" ;;
    esac
    shift
done
[ -n "$target" ] || { usage >&2; exit 2; }
case "$arch" in ""|aarch64|armhf|x86_64) ;; *) die "--arch must be aarch64, armhf or x86_64" ;; esac
[ -z "$game" ] || [ -f "$game" ] || die "game file not found: $game"

# One connection for every step, so a password is asked for once.
control="$(mktemp -u /tmp/cranklab-deploy.XXXXXX)"
read -r -a ssh_cmd <<<"${DEPLOY_SSH:-ssh}"
# Connection sharing only for real ssh; a custom DEPLOY_SSH wrapper gets no extra options.
case "${DEPLOY_SSH:-ssh}" in
    ssh|ssh\ *) ssh_opts=(-o ControlMaster=auto -o "ControlPath=$control" -o ControlPersist=60) ;;
    *) ssh_opts=() ;;
esac
remote() { "${ssh_cmd[@]}" "${ssh_opts[@]}" "$target" "$@"; }
cleanup() { [ "${#ssh_opts[@]}" = 0 ] || "${ssh_cmd[@]}" -o "ControlPath=$control" -O exit "$target" >/dev/null 2>&1 || true; }
trap cleanup EXIT

# ---- 1. what is the device? ------------------------------------------------
say "Inspecting $target"
info="$(remote sh -s <<'REMOTE'
echo "uname=$(uname -m)"
ra=$(command -v retroarch 2>/dev/null || true)
[ -n "$ra" ] || for p in /usr/bin/retroarch /storage/.config/retroarch/retroarch /opt/retroarch/bin/retroarch; do [ -x "$p" ] && ra=$p && break; done
echo "retroarch=$ra"
# ELF class byte: 1 = 32-bit, 2 = 64-bit. Works without `file`.
if [ -n "$ra" ]; then echo "elfclass=$(dd if="$ra" bs=1 skip=4 count=1 2>/dev/null | od -An -tu1 | tr -d ' ')"; fi
echo "glibc=$( (ldd --version 2>&1 | head -1) || true)"
echo "os=$( (. /etc/os-release 2>/dev/null && echo "$PRETTY_NAME") || true)"
REMOTE
)" || die "cannot reach $target over SSH"
get() { sed -n "s/^$1=//p" <<<"$info" | head -1; }
dev_uname="$(get uname)"; dev_ra="$(get retroarch)"; dev_class="$(get elfclass)"
echo "$info" | sed 's/^/    /'

if [ -z "$arch" ]; then
    case "$dev_uname" in
        x86_64) arch=x86_64 ;;
        aarch64|arm64) [ "$dev_class" = 1 ] && arch=armhf || arch=aarch64 ;;
        armv7l|armv8l|armv6l|arm*) arch=armhf ;;
        *) die "cannot map architecture '$dev_uname'; pass --arch" ;;
    esac
    say "Detected architecture: $arch (uname=$dev_uname, retroarch ELF class=${dev_class:-unknown})"
else
    say "Using architecture: $arch"
fi
[ -n "$dev_ra" ] || echo "warning: no retroarch found on the device; the core cannot be tested there" >&2

# ---- 2. RetroArch's directories ---------------------------------------------
say "Reading RetroArch directories"
dirs="$(remote sh -s -- "$cfg" <<'REMOTE'
cfg="$1"
if [ -z "$cfg" ]; then
    for c in /storage/.config/retroarch/retroarch.cfg "$HOME/.config/retroarch/retroarch.cfg" /etc/retroarch.cfg /home/ark/.config/retroarch/retroarch.cfg; do
        [ -f "$c" ] && cfg="$c" && break
    done
fi
echo "cfg=$cfg"
[ -n "$cfg" ] && [ -f "$cfg" ] || exit 0
for key in libretro_directory libretro_info_path system_directory savefile_directory; do
    value=$(sed -n "s/^$key *= *\"\\(.*\\)\" *\$/\\1/p" "$cfg" | head -1)
    # ":" prefixed values and "default" are relative to the app, not real paths.
    case "$value" in :*|default) value="" ;; esac
    echo "$key=$value"
    if [ -n "$value" ]; then
        if [ -w "$value" ]; then echo "$key.writable=yes"; elif [ -d "$value" ]; then echo "$key.writable=no"; else echo "$key.writable=missing"; fi
    fi
done
REMOTE
)"
dget() { sed -n "s/^$1=//p" <<<"$dirs" | head -1; }
dev_cfg="$(dget cfg)"; cores_dir="$(dget libretro_directory)"; info_dir="$(dget libretro_info_path)"
[ -n "$system_dir" ] || system_dir="$(dget system_directory)"
saves_dir="$(dget savefile_directory)"
echo "$dirs" | sed 's/^/    /'
[ -n "$system_dir" ] || echo "warning: system directory unknown; menu font will not be installed (pass --system-dir)" >&2

if [ "$diagnose" = 1 ]; then
    say "Diagnose only: nothing copied."
    exit 0
fi

# ---- 3. the core ------------------------------------------------------------
dist="$here/dist/$arch"
if [ ! -f "$dist/cranklab_libretro.so" ]; then
    [ "$build" = 1 ] || die "$dist/cranklab_libretro.so is missing and --no-build was given"
    say "Building the $arch core"
    if [ "$dry" = 1 ]; then echo "    (dry run) $here/build.sh $arch"
    else
        "$here/build.sh" "$arch" || die "build failed (cross builds need zig: pip install ziglang; ZIG=\"python3 -m ziglang\")"
    fi
fi
if [ "$dry" = 0 ]; then
    [ -f "$dist/cranklab_libretro.so" ] || die "no core at $dist"
    # Catch an architecture mix-up before it reaches the device.
    magic="$(od -An -tx1 -j18 -N2 "$dist/cranklab_libretro.so" | tr -d ' \n')"
    case "$arch:$magic" in
        aarch64:b700|armhf:2800|x86_64:3e00) ;;
        *) die "$dist/cranklab_libretro.so is not a $arch binary (ELF machine $magic)" ;;
    esac
fi

# ---- 4. copy ----------------------------------------------------------------
# `cat > file` over ssh needs no sftp-server or scp on the device.
put() { # local remote-path
    local src="$1" dst="$2"
    echo "    $src -> $dst"
    [ "$dry" = 1 ] && return 0
    remote "mkdir -p \"\$(dirname '$dst')\" && cat > '$dst.part' && mv '$dst.part' '$dst'" <"$src" || die "copy to $dst failed"
}
say "Copying to $target"
put "$dist/cranklab_libretro.so" "$dest/cranklab_libretro.so"
chmod_remote() { [ "$dry" = 1 ] || remote "chmod 755 '$1'"; }
chmod_remote "$dest/cranklab_libretro.so"
put "$dist/cranklab_libretro.info" "$dest/cranklab_libretro.info"

if [ -n "$system_dir" ] && [ -f "$dist/system/cranklab/fonts/cranklab-zh.pft" ]; then
    put "$dist/system/cranklab/fonts/cranklab-zh.pft" "$system_dir/cranklab/fonts/cranklab-zh.pft"
fi

if [ "$install_core" = 1 ]; then
    if [ -n "$cores_dir" ] && [ "$(dget libretro_directory.writable)" = yes ]; then
        put "$dist/cranklab_libretro.so" "$cores_dir/cranklab_libretro.so"
        if [ -n "$info_dir" ] && [ "$(dget libretro_info_path.writable)" = yes ]; then
            put "$dist/cranklab_libretro.info" "$info_dir/cranklab_libretro.info"
        else
            echo "    info directory is not writable or unknown; skipped the .info file"
        fi
    else
        echo "warning: RetroArch's cores directory ('${cores_dir:-unknown}') is not writable; use the full path instead" >&2
    fi
fi

game_path=""
if [ -n "$game" ]; then
    game_path="$rom_dir/$(basename "$game")"
    put "$game" "$game_path"
fi

# ---- 5. verify and explain --------------------------------------------------
if [ "$dry" = 0 ]; then
    if command -v sha256sum >/dev/null 2>&1; then
        local_sum="$(sha256sum "$dist/cranklab_libretro.so" | cut -d' ' -f1)"
        remote_sum="$(remote "sha256sum '$dest/cranklab_libretro.so' 2>/dev/null | cut -d' ' -f1" || true)"
        if [ -z "$remote_sum" ]; then echo "    (no sha256sum on the device; skipped checksum)"
        elif [ "$local_sum" = "$remote_sum" ]; then say "Checksum matches on the device"
        else die "checksum mismatch after copy"; fi
    fi
fi

cat <<EOF

==> Done. Try it by hand before touching EmulationStation:

    # 1. Free the screen (find the service name with: systemctl list-units | grep -i emu)
    systemctl stop <emulationstation service>

    # 2. Run a game with the core's full path
    ${dev_ra:-retroarch} -L $dest/cranklab_libretro.so ${game_path:-<path to a .zip, .pdxl or main.pdz>} --verbose 2>&1 | tee /tmp/cranklab.log

    # 3. Look for the core's own messages
    grep CrankLab /tmp/cranklab.log

    # 4. Bring the front end back
    systemctl start <emulationstation service>

Then follow section 5 of docs/GUIDE.md to add a Playdate system to EmulationStation.
Device info for a bug report:  ./deploy.sh --diagnose $target
EOF
