#!/usr/bin/env python3
"""Apply the fail-closed AKENO Settings/ShellUI host changes to onionHEN v0.9.0.

Operates on a throwaway clone. Never connects to a PS5 or writes to /system.
This is an experimental PC-built host: NOT approved for hardware execution.
"""
from __future__ import annotations

import argparse
import subprocess
from pathlib import Path

UPSTREAM_SHA = "aaa4264588e00da33faccc7642d27c3c4cc339b1"


def replace_once(root: Path, path: str, before: str, after: str) -> None:
    target = root / path
    text = target.read_text(encoding="utf-8-sig")
    count = text.count(before)
    if count != 1:
        raise RuntimeError(f"{path}: expected one known source anchor, found {count}")
    target.write_text(text.replace(before, after, 1), encoding="utf-8")


def replace_function(root: Path, path: str, signature: str, replacement: str) -> None:
    target = root / path
    text = target.read_text(encoding="utf-8-sig")
    if text.count(signature) != 1:
        raise RuntimeError(f"{path}: missing/duplicate {signature!r}")
    start = text.index(signature)
    opening = text.index("{", start + len(signature))
    depth = 0
    for pos in range(opening, len(text)):
        depth += (text[pos] == "{") - (text[pos] == "}")
        if depth == 0:
            target.write_text(text[:start] + replacement + text[pos + 1 :], encoding="utf-8")
            return
    raise RuntimeError(f"{path}: unterminated function {signature!r}")


def require_exact_clean_checkout(root: Path) -> None:
    def git(*args: str) -> str:
        return subprocess.check_output(
            ["git", "-C", str(root), *args], text=True, stderr=subprocess.DEVNULL
        ).strip()

    if git("rev-parse", "HEAD") != UPSTREAM_SHA:
        raise RuntimeError("refusing to patch: onionHEN commit is not pinned v0.9.0")
    if git("status", "--porcelain", "--untracked-files=no"):
        raise RuntimeError("refusing to patch: tracked onionHEN source has local modifications")


def patch(root: Path) -> None:
    require_exact_clean_checkout(root)
    filesystem = "source/bootstrapper/source/bootstrap_filesystem.c"
    replace_function(
        root, filesystem, "static bool remount(const char *device, const char *path)",
        "/* AKENO host: no system partition remount helper. */",
    )
    replace_function(
        root, filesystem, "bool bootstrap_filesystem_mount_system(void)",
        """bool bootstrap_filesystem_mount_system(void) {
  LOG_WARN("AKENO Settings host: no /system or /system_ex remount");
  return true;
}""",
    )
    replace_function(
        root, filesystem, "void bootstrap_filesystem_disable_updates(void)",
        """void bootstrap_filesystem_disable_updates(void) {
  /* Existing firmware isolation is the owner's responsibility. No deletes. */
  LOG_WARN("AKENO Settings host: /update untouched. Keep network isolated.");
}""",
    )

    assets = "source/bootstrapper/source/bootstrap_assets.c"
    replace_once(
        root, assets,
        '  mkdir("/system_ex/vsh_asset", 0777);\n'
        '  (void)write_file_atomic("/system_ex/vsh_asset/onionhen.png", sicon_start,\n'
        '                          sicon_size);\n',
        '  /* AKENO host: do not create or write system_ex icons. */\n',
    )

    hooks = "source/shellui/src/hook_functions.cpp"
    replace_once(
        root, hooks,
        '    return mono_str_ui("/system_ex/vsh_asset/onionhen.png");',
        '    return CxmlUri ? CxmlUri(Instance, uri) : uri; /* preserve stock icon */',
    )
    replace_once(
        root, hooks,
        '      patch_homeui_top_nav(buffer, &args->size, args->size);',
        '      /* AKENO Settings-only host: no HomeUI top-nav bundle patch. */',
    )

    prx = "source/shellui/src/prx.cpp"
    replace_once(
        root, prx,
        '  install_homeui_top_nav_hooks(img.react_pui);',
        '  /* AKENO Settings-only host: HomeUI top-nav hooks deliberately disabled. */',
    )

    launch = "source/bootstrapper/source/launch_pipeline.c"
    replace_once(
        root, launch,
        '  prepare_service_markers();\n  if (!launch_util(&context))',
        '  /* Reuse existing kstuff; never start a second kstuff instance. */\n'
        '  if (!kstuff_already_running()) {\n'
        '    LOG_ERROR("AKENO Settings host requires existing kstuff: refusing startup");\n'
        '    return -2;\n'
        '  }\n'
        '  prepare_service_markers();\n  if (!launch_util(&context))',
    )
    replace_once(
        root, launch,
        '  launch_kstuff(&context, firmware_version, kstuff_autoload);',
        '  (void)kstuff_autoload;\n'
        '  launch_kstuff(&context, firmware_version, false); /* signal pre-existing kstuff */',
    )

    # Fail closed on the safety-critical bootstrap and UI call sites.
    edited_fs = (root / filesystem).read_text()
    edited_assets = (root / assets).read_text()
    edited_hooks = (root / hooks).read_text()
    edited_launch = (root / launch).read_text()
    assert 'unlink("/update/' not in edited_fs
    assert 'nmount(' not in edited_fs
    assert 'write_file_atomic("/system_ex/' not in edited_assets
    assert 'patch_homeui_top_nav(buffer' not in edited_hooks
    assert "launch_kstuff(&context, firmware_version, kstuff_autoload)" not in edited_launch
    assert 'if (!kstuff_already_running())' in edited_launch
    print("AKENO Settings/ShellUI host patch: OK (source only; no console tested)")


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("upstream", type=Path, help="clean local checkout of pinned onionHEN")
    args = p.parse_args()
    patch(args.upstream.resolve())


if __name__ == "__main__":
    main()
