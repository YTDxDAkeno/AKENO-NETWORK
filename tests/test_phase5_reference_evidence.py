import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path


SCRIPT = Path(__file__).parents[1] / "tools/phase5_reference_evidence.py"
SPEC = importlib.util.spec_from_file_location("phase5_reference_evidence", SCRIPT)
assert SPEC and SPEC.loader
evidence = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = evidence
SPEC.loader.exec_module(evidence)


class Phase5ReferenceEvidenceTests(unittest.TestCase):
    def make_fixture(self, omit_marker: bool = False) -> Path:
        temporary_directory = tempfile.TemporaryDirectory()
        self.addCleanup(temporary_directory.cleanup)
        root = Path(temporary_directory.name)
        files: dict[Path, list[str]] = {}
        for index, (repository, relative_path, marker, _) in enumerate(evidence.EVIDENCE):
            path = root / repository / relative_path
            files.setdefault(path, []).append("" if omit_marker and index == 0 else marker)
        for path, markers in files.items():
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("fixture\n" + "\n".join(markers) + "\n")
        return root

    def test_finds_every_documented_source_marker(self):
        results = evidence.scan_sources(self.make_fixture())
        self.assertEqual(len(evidence.EVIDENCE), len(results))
        self.assertTrue(all(result.found for result in results))
        self.assertIn("FOUND: np-fake-signin/README.md", evidence.render_report(results))

    def test_reports_a_missing_marker_without_claiming_success(self):
        results = evidence.scan_sources(self.make_fixture(omit_marker=True))
        self.assertFalse(results[0].found)
        self.assertIn("MISSING: np-fake-signin/README.md", evidence.render_report(results))

    def test_rejects_an_incomplete_reference_tree(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            with self.assertRaises(evidence.EvidenceError):
                evidence.scan_sources(Path(temporary_directory))


if __name__ == "__main__":
    unittest.main()
