#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
source tools/ps2_env.sh
mode="${PS2_PERF_SCENE_MODE:-house_path}"
case "$mode" in
    house_path) title=0; default_frames=120 ;;
    title_cinematic) title=1; default_frames=480 ;;
    *) echo 'Invalid mode (house_path or title_cinematic).' >&2; exit 1 ;;
esac
frames="${PS2_PERF_SCENE_FRAMES:-$default_frames}"
warmup="${PS2_PERF_SCENE_WARMUP:-30}"
image="${PS2_PERF_SCENE_IMAGE:-build/PS2-ntsc12/oot.z64}"
target="${PS2_PERF_SCENE_TARGET:-oot_ps2_perf_scene.elf}"
[[ "$frames" =~ ^[0-9]+$ && "$warmup" =~ ^[0-9]+$ && "$frames" -ge 60 && "$frames" -le 600 && "$warmup" -ge 30 ]] || {
    echo 'Invalid frame counts (60..600 measured, >=30 warmup).' >&2; exit 1;
}
if [[ "$title" == 1 && "$frames" -lt 300 ]]; then
    echo 'The title cinematic needs at least 300 measured frames.' >&2; exit 1
fi
# This test links game/renderer objects but adds no USB runtime instrumentation.
# Frame counts are test-only compile flags; rebuild their two owned objects
# even when make does not notice a changed command-line variable.
rm -f build/PS2-ntsc12/tools/ps2_perf_scene.o build/PS2-ntsc12/tools/ps2_perf_scene_capture.o
make PLATFORM=PS2 OOT_VERSION=NTSC_1_2 BUILD_DIR=build/PS2-ntsc12 \
    OOT_UPSTREAM="${OOT_UPSTREAM:-$HOME/oot-ps2-tools/oot-upstream}" \
    PS2_PERF_SCENE_BENCH=1 PS2_PERF_SCENE_FRAMES="$frames" PS2_PERF_SCENE_WARMUP="$warmup" \
    PS2_PERF_SCENE_TITLE_CINEMATIC="$title" \
    TARGET="$target" -j2
mkdir -p build/PS2-perf-scene
cp "build/PS2-ntsc12/$target" build/PS2-perf-scene/oot_ps2_perf_scene.elf
cp "$image" build/PS2-perf-scene/oot.z64
python3 - "$frames" "$warmup" "$mode" <<'PY'
import hashlib, json, pathlib, sys
root = pathlib.Path('build/PS2-perf-scene')
def digest(name): return hashlib.sha256((root/name).read_bytes()).hexdigest()
manifest = dict(schema=3, views=3, frames=int(sys.argv[1]), warmup=int(sys.argv[2]),
                entrance=0x211, scene=85, seed=0x12345678, profile_every=60,
                day_time=32768, camera_fixture_version=2,
                seed_override=True, profile_alignment='fixture_frame',
                elf_sha256=digest('oot_ps2_perf_scene.elf'), image_sha256=digest('oot.z64'),
                synthetic_save=True, real_ps2_validated=False)
if sys.argv[3] == 'title_cinematic':
    manifest.update(schema=4, mode='title_cinematic', views=1, entrance=0xcd, scene=81,
                    day_time=None, camera_fixture_version=3, camera='original_moving',
                    title_initializer='TitleSetup_Init', cutscene_index=0xfff3,
                    link_age=0, game_mode=1, motion_frame_aligned=True)
(root/'fixture.json').write_text(json.dumps(manifest, indent=2)+'\n')
PY
