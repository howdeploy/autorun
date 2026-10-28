#!/bin/sh
# Package the existing native fork as an Autorun module, leaving it untouched.
set -eu
if [ "$#" -ne 2 ]; then
    echo "Usage: sh $0 /path/to/mkxp-native /new/output/directory" >&2
    exit 2
fi
native_root=$(realpath "$1")
integration_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
output_dir=$(realpath -m "$2")
if [ -e "$output_dir" ]; then
    echo "Output directory must not exist: $output_dir" >&2
    exit 1
fi
test -f "$native_root/retroarch/Makefile.libnx"
test -f "$native_root/mkxp-z/build-switch/mkxp-z_libretro.a"
cmp "$native_root/mkxp-z/build-switch/mkxp-z_libretro.a" "$native_root/retroarch/libretro_libnx.a"
mkdir -p "$output_dir"
cp -a --reflink=auto "$native_root/retroarch" "$output_dir/retroarch"
mkdir -p "$output_dir/romfs/retroarch"
cp "$integration_root/engines/mkxp-z/retroarch.cfg" "$output_dir/romfs/retroarch/retroarch.cfg"
for dir in assets autoconfig cheats config/mkxp-z config/remaps cores/savefiles cores/savestates \
    cores/system/mkxp-z/Fonts cores/system/mkxp-z/RTP cores/system/mkxp-z/Scripts/Preload \
    cores/system/mkxp-z/Scripts/Postload database/rdb downloads filters info logs \
    overlays/keyboards playlists records_config screenshots shaders thumbnails; do
    mkdir -p "$output_dir/romfs/retroarch/$dir"
done
cp "$integration_root/engines/mkxp-z/mkxp-z.opt" "$output_dir/romfs/retroarch/config/mkxp-z/mkxp-z.opt"
git -C "$native_root/mkxp-z" rev-parse HEAD > "$output_dir/mkxp-base.txt"
git -C "$native_root/mkxp-z" diff --binary HEAD > "$output_dir/mkxp-source.patch"
git -C "$native_root/retroarch" rev-parse HEAD > "$output_dir/retroarch-base.txt"
git -C "$native_root/retroarch" diff --binary HEAD > "$output_dir/retroarch-source.patch"
docker run --rm --user "$(id -u):$(id -g)" \
    -v "$output_dir":/output -v "$integration_root":/integration:ro \
    -w /output/retroarch mkxp-libnx-build:v4-10-0 bash -euc '
    make -f Makefile.libnx -f /integration/engines/mkxp-z/Makefile.autorun \
        LIBRETRO=mkxp-z OUTPUT=/output/mkxp-z NO_ICON=1 NO_NACP=1 /output/mkxp-z.elf -j"$(nproc)"
    /opt/devkitpro/devkitA64/bin/aarch64-none-elf-strip -o /output/mkxp-z.stripped.elf /output/mkxp-z.elf
    /opt/devkitpro/tools/bin/nacptool --create "mkxp-z (Autorun)" "mkxp-z / libretro contributors" "native-1" /output/mkxp-z.nacp
    /opt/devkitpro/tools/bin/elf2nro /output/mkxp-z.stripped.elf /output/mkxp-z.nro \
        --nacp=/output/mkxp-z.nacp --romfsdir=/output/romfs
    '
sha256sum "$output_dir/retroarch/libretro_libnx.a" "$output_dir/mkxp-z.nro" > "$output_dir/SHA256SUMS"
echo "Module: $output_dir/mkxp-z.nro"
