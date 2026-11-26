"""Tests for tools/check_workflow_exprs.py.

Written with unittest so they run with pytest and without it.
"""

import glob
import importlib.util
import os
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TOOL = os.path.join(ROOT, "tools", "check_workflow_exprs.py")

_spec = importlib.util.spec_from_file_location("check_workflow_exprs", TOOL)
cwe = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(cwe)

OLD_LINE_95 = """\
    steps:
      - name: The tag names the package
        if: startsWith(github.ref, 'refs/tags/pipos-')
        run: test -f "build/${{ github.ref_name }}.zip"
"""

FIXED = """\
    steps:
      - name: The tag names the package
        if: startsWith(github.ref, 'refs/tags/pipos-')
        env:
          TAG: ${{ github.ref_name }}
        run: |
          test -f "build/$TAG.zip"
"""


def found(text):
    return list(cwe.findings(text))


def lines(text):
    return [n for n, _ in found(text)]


def step(run_line):
    return "    steps:\n      - name: s\n        " + run_line + "\n"


class MustFail(unittest.TestCase):
    def test_old_line_95(self):
        self.assertEqual(lines(OLD_LINE_95), [4])

    def test_forms(self):
        for expr in (
            "github.ref",
            "github.ref_name",
            "github['ref_name']",
            "github.event.pull_request.title",
            "inputs.x",
            "matrix.x",
            "secrets.x",
        ):
            with self.subTest(expr=expr):
                text = step("run: |\n          echo \"${{ " + expr + " }}\"")
                self.assertEqual(lines(text), [3])
                self.assertIn(expr, found(text)[0][1])

    def test_split_across_folded_lines(self):
        text = step("run: >-\n          echo ${{\n          github.head_ref }}")
        self.assertEqual(lines(text), [3])
        self.assertIn("github.head_ref", found(text)[0][1])

    def test_single_line_quoted(self):
        self.assertEqual(lines(step("run: 'echo ${{ github.ref }}'")), [3])
        self.assertEqual(lines(step('run: "echo ${{ github.ref }}"')), [3])

    def test_chomping_and_indent_indicators(self):
        for ind in ("|-", "|+", ">+", "|2-", ">-2"):
            with self.subTest(ind=ind):
                text = step("run: " + ind + "\n          echo ${{ github.ref }}")
                self.assertEqual(lines(text), [3])

    def test_shell_comment_in_script(self):
        # Actions interpolates the whole script text, shell comments too.
        text = step("run: |\n          # was ${{ github.ref }}\n          echo ok")
        self.assertEqual(lines(text), [3])

    def test_plain_multiline(self):
        text = step("run: echo a\n          ${{ github.ref }}")
        self.assertEqual(lines(text), [3])


class MustPass(unittest.TestCase):
    def test_fixed_step(self):
        self.assertEqual(found(FIXED), [])

    def test_env_if_with_name(self):
        text = (
            "    steps:\n"
            "      - name: Build ${{ matrix.board }}\n"
            "        if: ${{ github.ref == 'refs/heads/master' }}\n"
            "        uses: actions/x@v1\n"
            "        with:\n"
            "          ref: ${{ github.ref }}\n"
            "      - env:\n"
            "          X: ${{ github.ref_name }}\n"
            "        run: echo \"$X\"\n"
        )
        self.assertEqual(found(text), [])

    def test_yaml_comments(self):
        text = (
            "    steps:\n"
            "      # - run: echo ${{ github.ref }}\n"
            "      - run: echo ok # ${{ github.ref }}\n"
        )
        self.assertEqual(found(text), [])

    def test_defaults_run_mapping(self):
        text = "defaults:\n  run:\n    shell: bash\n"
        self.assertEqual(found(text), [])


class Refused(unittest.TestCase):
    def assertRefused(self, text):
        hits = found(text)
        self.assertTrue(hits, text)
        self.assertTrue(all("refused" in m for _, m in hits), hits)

    def test_flow_mapping(self):
        self.assertRefused("    steps:\n      - {run: echo hi}\n")

    def test_quoted_key(self):
        self.assertRefused(step('"run": echo hi'))

    def test_anchor_and_alias(self):
        self.assertRefused("    steps:\n      - &s\n        run: echo hi\n      - *s\n")
        self.assertRefused(step("run: *script"))

    def test_merge_key(self):
        self.assertRefused("    steps:\n      - <<: *s\n")


class Cli(unittest.TestCase):
    def test_main_exit_codes(self):
        self.assertEqual(cwe.main(["x"]), 2)


class RepoWorkflows(unittest.TestCase):
    def test_repo_workflows_are_clean(self):
        files = sorted(glob.glob(os.path.join(ROOT, ".github", "workflows", "*.yml")))
        self.assertTrue(files)
        for path in files:
            with open(path, encoding="utf-8") as f:
                self.assertEqual(found(f.read()), [], path)


if __name__ == "__main__":
    unittest.main()
