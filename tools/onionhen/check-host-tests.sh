#!/usr/bin/env bash
# Runs onionHEN's own host test suite on the patched tree (pinned v0.9.0 + akeno-direct-entry.patch +
# akeno-direct-entry-fix.patch) three times: plain, ASan/UBSan and TSan (sanitizers in recover mode,
# so every report is seen). Each run must pass every test, and no sanitizer report may point at a line
# the patches add or change. Reports from upstream v0.9.0's own lines are listed, not fatal. PC only.
#
#   KEYSTONE_PREFIX=<dir with lib/libkeystone.so> tools/onionhen/check-host-tests.sh [onionhen checkout]
set -euo pipefail

ONIONHEN_URL=https://github.com/aydencharles/onionHEN.git
ONIONHEN_COMMIT=aaa4264588e00da33faccc7642d27c3c4cc339b1 # tag v0.9.0
PATCHES=(akeno-direct-entry.patch akeno-direct-entry-fix.patch)

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
keystone="${KEYSTONE_PREFIX:?KEYSTONE_PREFIX must hold lib/libkeystone.so}"
work="${TMPDIR:-/tmp}/akeno-onionhen-host-tests"
src="${1:-$work/onionhen}"
mkdir -p "$work"
if [[ ! -d "$src/.git" ]]; then
  rm -rf "$src"
  git clone -q "$ONIONHEN_URL" "$src"
fi
git -C "$src" -c advice.detachedHead=false checkout -q -f --detach "$ONIONHEN_COMMIT"
test "$(git -C "$src" rev-parse HEAD)" = "$ONIONHEN_COMMIT"
git -C "$src" clean -qfdx -- source
for patch in "${PATCHES[@]}"; do git -C "$src" apply --index "$here/$patch"; done
git -C "$src" diff --cached -U0 > "$work/patches.diff"

run() { # name, extra compiler flags, environment
  local name="$1" flags="$2"
  shift 2
  echo "--- host tests: $name"
  local status=0
  env "$@" LD_LIBRARY_PATH="$keystone/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    make -C "$src/source/util/tests" -j"${JOBS:-$(nproc)}" test KEYSTONE_PREFIX="$keystone" \
    BUILD_DIR="$work/build-$name" HOST_CC="cc $flags" HOST_CXX="c++ $flags" > "$work/$name.log" 2>&1 || status=$?
  echo "    $(grep -cE '^\[PASS\]' "$work/$name.log") passed, $(grep -cE '^\[FAIL\]' "$work/$name.log") failed (make exit $status)"
  grep -E '^\[FAIL\]' "$work/$name.log" || true
  test "$status" -eq 0
  grep -q 'All util host tests passed' "$work/$name.log"
}

run plain ""
run asan-ubsan "-fsanitize=address,undefined -fsanitize-recover=address,undefined -fno-omit-frame-pointer" \
  ASAN_OPTIONS=halt_on_error=0:detect_leaks=1 UBSAN_OPTIONS=print_stacktrace=1
run tsan "-fsanitize=thread -fno-omit-frame-pointer -O1" TSAN_OPTIONS=halt_on_error=0

python3 - "$src" "$work/patches.diff" "$work/asan-ubsan.log" "$work/tsan.log" << 'EOF'
import re, sys

src, diff_path, logs = sys.argv[1].rstrip("/") + "/", sys.argv[2], sys.argv[3:]
# Lines the patches add or change, per file (new-file line numbers).
changed, current = {}, None
for line in open(diff_path, errors="replace"):
    if line.startswith("+++ "):
        current = line[6:].strip() if line.startswith("+++ b/") else None
    elif line.startswith("@@") and current:
        m = re.match(r"@@ -\S+ \+(\d+)(?:,(\d+))? @@", line)
        start, count = int(m.group(1)), int(m.group(2) or 1)
        changed.setdefault(current, set()).update(range(start, start + count))

frame = re.compile(re.escape(src) + r"(\S+?):(\d+)")
ours, upstream = [], []
for log in logs:
    lines = open(log, errors="replace").read().splitlines()
    for i, line in enumerate(lines):
        if not re.search(r"ERROR: (Address|Leak)Sanitizer|WARNING: ThreadSanitizer|runtime error:", line):
            continue
        block = [line] + lines[i + 1:i + 60]
        frames = []
        for text in block[1:] if "runtime error" not in line else block[:1]:
            if text.startswith(("SUMMARY", "==")) and frames:
                break
            frames += [(f, int(n)) for f, n in frame.findall(text)]
        kind = re.sub(r"^.*?(ERROR: \w+: [\w-]+|WARNING: ThreadSanitizer: [\w ]+|runtime error: [^0-9]+).*$", r"\1", line)
        top = frames[0] if frames else ("?", 0)
        hit = any(n in changed.get(f, ()) for f, n in frames[:4])
        (ours if hit else upstream).append(f"{kind.strip()} at {top[0]}:{top[1]}")
for text in sorted(set(upstream)):
    print(f"    upstream v0.9.0 (not changed by the patches): {text}")
for text in sorted(set(ours)):
    print(f"    PATCHED CODE: {text}")
print(f"sanitizers: {len(set(ours))} report(s) in patched lines, {len(set(upstream))} in upstream lines")
sys.exit(1 if ours else 0)
EOF
