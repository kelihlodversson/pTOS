"""The API guard must reject declaration removal even when text remains."""
import contextlib
import importlib.util
import io
from pathlib import Path
import re
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("compatibility", ROOT / "tools/check-libaes-compatibility.py")
compatibility = importlib.util.module_from_spec(spec)
spec.loader.exec_module(compatibility)


class DeclarationGuard(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)
        for name in ("doc/libaes-compatibility.md", "lib/aes/exports.map",
                     "include/libaes.h", "include/rectfunc.h", "aes/gemsuper.c",
                     "lib/gemlib/ptos/gem.h",
                     *[str(path.relative_to(ROOT)) for path in (ROOT / "lib/gemlib/ptos").glob("*.c")]):
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes((ROOT / name).read_bytes())
        # Only file existence is checked for behavioral evidence. Retain real
        # declaration/export/opcode inputs without copying unrelated fixtures.
        matrix = (self.root / "doc/libaes-compatibility.md").read_text()
        for name in re.findall(r"`((?:tests|lib|include)/[^`]+)`", matrix):
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            if not path.exists():
                path.touch()

    def check(self):
        with contextlib.redirect_stdout(io.StringIO()):
            compatibility.check(self.root)

    def test_real_headers(self):
        self.check()

    def test_private_native_export(self):
        path = self.root / "lib/aes/exports.map"
        path.write_text(path.read_text().replace("global:", "global: aes_session_open;", 1))
        with self.assertRaisesRegex(AssertionError, "non-mt libaes exports: aes_session_open"):
            self.check()

    def test_classic_binding_exports_are_rejected(self):
        path = self.root / "lib/aes/exports.map"
        original = path.read_text()
        for name in ("appl_init", "aes_global", "inside"):
            with self.subTest(name=name):
                path.write_text(original.replace("global:", "global: " + name + ";", 1))
                with self.assertRaisesRegex(AssertionError, "non-mt libaes exports: " + name):
                    self.check()

    def test_missing_gemlib_implementation(self):
        path = self.root / "lib/gemlib/ptos/a_appl_init.c"
        path.write_text("/* appl_init(...) { return mt_appl_init(...); } */\n")
        with self.assertRaisesRegex(AssertionError, "missing GEMlib definition: appl_init"):
            self.check()

    def test_commented_exports(self):
        path = self.root / "lib/aes/exports.map"
        original = path.read_text()
        bindings = r"\b(?:appl_init|mt_appl_init)\s*;"
        path.write_text(re.sub(bindings, lambda match: "/* " + match[0] + " */", original))
        with self.assertRaisesRegex(AssertionError, "missing_exports") as failure:
            self.check()
        self.assertIn("'mt_appl_init'", str(failure.exception))

    def test_export_comment_labels(self):
        path = self.root / "lib/aes/exports.map"
        path.write_text("/* global: commented_symbol; local: */\n" + path.read_text())
        self.check()

    def test_legacy_case_spacing(self):
        path = self.root / "aes/gemsuper.c"
        original = path.read_text()
        for label in ("case NEW_OPERATION:", "case NEW_OPERATION :",
                      "case\tNEW_OPERATION\n:", "case /* application */ NEW_OPERATION:",
                      "case/**/NEW_OPERATION/**/:", "case\\\n NEW_OPERATION:"):
            with self.subTest(label=label):
                path.write_text(original + "\n" + label)
                with self.assertRaisesRegex(AssertionError, "unclassified legacy operations: NEW_OPERATION"):
                    self.check()

    def test_inactive_legacy_cases(self):
        path = self.root / "aes/gemsuper.c"
        path.write_text(path.read_text() + '''
/* case NEW_OPERATION: */
// case NEW_OPERATION:
const char *example = "case NEW_OPERATION:";
''')
        self.check()

    def test_commented_prototypes(self):
        for header, binding in (("include/libaes.h", "mt_appl_init"),
                                ("include/rectfunc.h", "inside")):
            path = self.root / header
            original = path.read_text()
            pattern = r"[^\n;{}]*\b" + binding + r"\s*\([^;]*;"
            prototype = re.search(pattern, original)[0]
            for comment in ("/* " + prototype + " */", "// " + prototype,
                            "// continued \\\n" + prototype,
                            'const char *example = "' + binding + '(...)";'):
                with self.subTest(binding=binding, comment=comment):
                    path.write_text(original.replace(prototype, comment))
                    with self.assertRaisesRegex(AssertionError, "missing public declaration: " + binding):
                        self.check()
            path.write_text(original)

    def test_comment_markers_in_literals(self):
        path = self.root / "include/libaes.h"
        path.write_text('const char *examples[] = {"/*", "//", "\\\"*/"};\n' + path.read_text())
        self.check()

    def test_comment_between_name_and_arguments(self):
        path = self.root / "include/libaes.h"
        path.write_text(path.read_text().replace("mt_appl_init(", "mt_appl_init /* note */ ("))
        self.check()


if __name__ == "__main__":
    unittest.main()
