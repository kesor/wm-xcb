#!/usr/bin/env bash
# scripts/check-orphan-sources.sh exists because a tracked .c file missing from
# SRC is invisible to the compiler and to check-async at once: wm-kbd-mouse.c
# kept an xcb_grab_pointer_reply() that no gate could see. The verdict is in
# scripts/lib/orphan-sources-report.sh so this feeds it recorded lists, and the
# shapes that matter are:
#
#   - a tracked source that the build does not compile;
#   - tests/ and tools/, which have targets of their own and are not in SRC;
#   - an empty tracked list or an empty built list, which is a failed read and
#     not a clean tree.
source "$(dirname "$0")/lib.sh"
# shellcheck source=../../scripts/lib/orphan-sources-report.sh
. "$ROOT_DIR/scripts/lib/orphan-sources-report.sh"

DIR="$TMPDIR_SEAM/orphan-sources"
rm -rf "$DIR"
mkdir -p "$DIR"

TRACKED="$DIR/tracked.txt"
BUILT="$DIR/built.txt"
LAST="$DIR/last.txt"

cat >"$BUILT" <<'EOF'
src/components/tiling.c
wm-hub.c
EOF

# Three WM sources, a test and a tool. Only the first two are in print-src; the
# test and the tool are built by their own targets.
cat >"$TRACKED" <<'EOF'
src/components/tiling.c
wm-hub.c
wm-kbd-mouse.c
tests/unit/test-wm-hub.c
tools/xclient.c
EOF

ORPHAN_SOURCES_VERBOSE=0 orphan_sources_verify "$TRACKED" "$BUILT" check-orphan-sources >"$LAST" 2>&1
check_eq "a tracked source the build does not compile fails" 1 "$?"
check_eq "the orphan is named" 1 "$(grep -c 'NOT BUILT  wm-kbd-mouse.c' "$LAST")"
check_eq "the failure says where the file is invisible" 1 \
  "$(grep -c 'check-async scans the build' "$LAST")"
check_eq "the failure says what to do" 1 "$(grep -c 'Add the file to' "$LAST")"

# A test or a tool is built by its own make target, so print-src not naming it is
# not an orphan and must not be reported.
cat >"$TRACKED" <<'EOF'
wm-hub.c
tests/unit/test-wm-hub.c
tools/xclient.c
EOF
ORPHAN_SOURCES_VERBOSE=1 orphan_sources_verify "$TRACKED" "$BUILT" check-orphan-sources >"$LAST" 2>&1
check_eq "tests/ and tools/ are not orphans" 0 "$?"
check_eq "and the pass line counts only the WM sources" 1 \
  "$(grep -c '1 tracked source(s), every one in the build' "$LAST")"

# A failed git ls-files leaves the same empty file as a tree with no sources, so
# the gate refuses it instead of reporting every built file as surplus.
: >"$TRACKED"
ORPHAN_SOURCES_VERBOSE=0 orphan_sources_verify "$TRACKED" "$BUILT" check-orphan-sources >"$LAST" 2>&1
check_eq "an empty tracked list fails" 1 "$?"
check_eq "and says git gave nothing to check" 1 \
  "$(grep -c 'git ls-files gave nothing to check' "$LAST")"

printf 'wm-hub.c\n' >"$TRACKED"
: >"$BUILT"
ORPHAN_SOURCES_VERBOSE=0 orphan_sources_verify "$TRACKED" "$BUILT" check-orphan-sources >"$LAST" 2>&1
check_eq "an empty built list fails" 1 "$?"
check_eq "and points at make print-src" 1 "$(grep -c "make -s print-src" "$LAST")"

finish
