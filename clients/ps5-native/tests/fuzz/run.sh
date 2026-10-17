#!/bin/sh
# Builds the libFuzzer harnesses (clang, ASan + UBSan) and runs each for SECONDS (default 60) from
# the committed seeds. Needs scripts/fetch-deps.sh (stb_image.h) and clang with compiler-rt.
#
#   clients/ps5-native/tests/fuzz/run.sh [seconds] [clang]
set -eu
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
seconds=${1:-60}
cc=${2:-clang}
out=${AKN_FUZZ_OUT:-$(mktemp -d)}
mkdir -p "$out"
flags="-g -O1 -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all -I$root/src/core"
stb="$root/src/third/stb_image_impl.c"
"$cc" $flags -w -I"$root/third_party" -c "$stb" -o "$out/stb_image_impl.o"
"$cc" $flags -Wall -Wextra -Werror "$here/fuzz_image.c" "$root/src/core/image.c" "$out/stb_image_impl.o" -lm -o "$out/fuzz_image"
"$cc" $flags -Wall -Wextra -Werror "$here/fuzz_path.c" "$root/src/core/vector.c" "$root/src/core/image.c" "$out/stb_image_impl.o" -lm \
    -o "$out/fuzz_path"
for target in image path; do
    mkdir -p "$out/corpus-$target"
    cp "$here/seeds/$target"/* "$out/corpus-$target/"
    echo "== fuzz_$target: $seconds s"
    "$out/fuzz_$target" -max_total_time="$seconds" -max_len=65536 -rss_limit_mb=1024 -timeout=10 -print_final_stats=1 \
        -artifact_prefix="$out/crash-$target-" "$out/corpus-$target"
done
echo "fuzzing finished without findings ($out)"
