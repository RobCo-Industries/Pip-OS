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
    return list(cwe.hits(text))


class Hits(unittest.TestCase):
    def test_old_line_95_fails(self):
        self.assertEqual([n for n, _ in found(OLD_LINE_95)], [4])

    def test_fixed_step_passes(self):
        self.assertEqual(found(FIXED), [])

    def test_block_scalar(self):
        text = "      - run: |\n          echo ok\n          echo ${{ github.head_ref }}\n"
        self.assertEqual([n for n, _ in found(text)], [3])

    def test_event_field(self):
        text = "      - run: echo \"${{ github.event.pull_request.title }}\"\n"
        self.assertEqual(len(found(text)), 1)

    def test_safe_expressions_pass(self):
        text = (
            "      - run: |\n"
            "          echo ${{ matrix.board }}\n"
            "          echo ${{ github.ref == 'refs/heads/master' }}\n"
        )
        self.assertEqual(found(text), [])

    def test_block_ends_at_next_key(self):
        text = (
            "      - run: echo hi\n"
            "        env:\n"
            "          X: ${{ github.ref_name }}\n"
        )
        # env: is a sibling of run:, not run text.
        self.assertEqual(found(text), [])

    def test_main_exit_codes(self):
        self.assertEqual(cwe.main(["x"]), 2)


class RepoWorkflows(unittest.TestCase):
    def test_repo_workflows_are_clean(self):
        files = sorted(glob.glob(os.path.join(ROOT, ".github", "workflows", "*.yml")))
        self.assertTrue(files)
        for path in files:
            with open(path, encoding="utf-8") as f:
                self.assertEqual(list(cwe.hits(f.read())), [], path)


if __name__ == "__main__":
    unittest.main()
