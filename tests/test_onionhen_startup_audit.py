import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path


SCRIPT = Path(__file__).parents[1] / "tools/onionhen_startup_audit.py"
SPEC = importlib.util.spec_from_file_location("onionhen_startup_audit", SCRIPT)
assert SPEC and SPEC.loader
audit = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = audit
SPEC.loader.exec_module(audit)


class OnionHenStartupAuditTests(unittest.TestCase):
    def make_source_tree(self) -> Path:
        tempdir = tempfile.TemporaryDirectory()
        self.addCleanup(tempdir.cleanup)
        root = Path(tempdir.name)
        bootstrapper = root / "source/bootstrapper/source"
        shellui = root / "source/shellui/src"
        bootstrapper.mkdir(parents=True)
        shellui.mkdir(parents=True)

        (bootstrapper / "bootstrap_assets.c").write_text(
            'mkdir("/system_ex/vsh_asset", 0777);\n'
            'write_file_atomic("/system_ex/vsh_asset/onionhen.png", icon, size);\n'
        )
        (bootstrapper / "bootstrap_filesystem.c").write_text(
            'if (!remount("/dev/ssd0.system_ex", "/system_ex")) return false;\n'
            'if (!remount("/dev/ssd0.system", "/system")) return false;\n'
            'unlink("/update/PS5UPDATE.PUP");\n'
            'unlink("/update/PS5UPDATE.PUP.net.temp");\n'
        )
        (shellui / "hook_functions.cpp").write_text(
            'return mono_str_ui("/system_ex/vsh_asset/onionhen.png");\n'
        )
        (shellui / "homeui_top_nav_patch.cpp").write_text(
            'constexpr auto kIcon = "/system_ex/vsh_asset/onionhen.png";\n'
        )
        return root

    def test_detects_all_startup_behaviors(self):
        results = audit.audit_source_tree(self.make_source_tree())

        self.assertEqual(2, len(results["system-partition writes"]))
        self.assertEqual(
            ["/system_ex", "/system"],
            [finding.detail for finding in results["system-partition remounts"]],
        )
        self.assertEqual(
            ["/update/PS5UPDATE.PUP", "/update/PS5UPDATE.PUP.net.temp"],
            [finding.detail for finding in results["downloaded firmware-update deletions"]],
        )
        self.assertEqual(3, len(results["hardcoded system-partition icon paths"]))

    def test_report_marks_manual_review_as_outstanding(self):
        results = audit.audit_source_tree(self.make_source_tree())
        report = audit.render_report(audit.PINNED_REVISION, results)

        self.assertIn("system-partition remounts: 2", report)
        self.assertIn("downloaded firmware-update deletions: 2", report)
        self.assertIn("manual review still required:", report)
        self.assertIn("not a console-safety approval", report)

    def test_missing_source_directory_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            with self.assertRaises(audit.AuditError):
                audit.audit_source_tree(Path(temporary_directory))


if __name__ == "__main__":
    unittest.main()
