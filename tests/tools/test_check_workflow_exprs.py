"""Tests for tools/check_workflow_exprs.py.

Written with unittest so they run with pytest and without it.
"""

import importlib.util
import os
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TOOL = os.path.join(ROOT, "tools", "check_workflow_exprs.py")

_spec = importlib.util.spec_from_file_location("check_workflow_exprs", TOOL)
cwe = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(cwe)


def lines(text):
    return [num for num, _ in cwe.findings(text)]


MUST_FAIL = {
    "run": '      - name: x\n        run: echo "${{ github.ref_name }}"\n',
    "space before colon": "        run : echo ${{ github.ref_name }}\n",
    "dash run": "      - run: echo ${{ github.ref_name }}\n",
    "block ref": "        run: |\n          echo ${{ github.ref }}\n",
    "block bracket": "        run: |\n          echo ${{ github['ref_name'] }}\n",
    "block inputs": "        run: >-\n          echo ${{ inputs.x }}\n",
    "block matrix": "        run: |2\n          echo ${{ matrix.x }}\n",
    "block secrets": "        run: |\n          echo ok\n\n          X: ${{ secrets.x }}\n",
    "block under other key": "        body: |\n          ${{ github.ref }}\n",
    "split": "        X: ${{ github.ref\n          }}\n",
    "flow mapping": "      - {run: echo ${{ x }}}\n",
    "quoted key": '        "run": echo ${{ x }}\n',
    "comment": "        # echo ${{ github.ref_name }}\n",
    "script": "          script: console.log('${{ x }}')\n",
    "shell": "        shell: ${{ x }} {0}\n",
    "args": "          args: ${{ x }}\n",
    "entrypoint upper": "          Entrypoint: ${{ x }}\n",
}

MUST_PASS = {
    "env entry": "        env:\n          TAG: ${{ github.ref_name }}\n",
    "dash run then env": (
        "      - run: echo ok\n        env:\n          TAG: ${{ github.ref_name }}\n"
    ),
    "if": "        if: ${{ github.event_name == 'push' }}\n",
    "group": "  group: sdk-package-${{ github.ref }}\n",
    "with": "        with:\n          path: build/${{ matrix.board }}\n",
    "quoted text no expr": '        name: "Example {run: echo hi}"\n',
    "block ends then env": (
        "      - run: |\n          echo ok\n        env:\n          TAG: ${{ x }}\n"
    ),
}


class Rule(unittest.TestCase):
    def test_must_fail(self):
        for name, text in MUST_FAIL.items():
            with self.subTest(name):
                self.assertTrue(lines(text), name)

    def test_must_pass(self):
        for name, text in MUST_PASS.items():
            with self.subTest(name):
                self.assertEqual(lines(text), [], name)

    def test_old_workflow_fails_on_line_95(self):
        old = subprocess.run(
            ["git", "show", "origin/master:.github/workflows/sdk-package.yml"],
            cwd=ROOT, capture_output=True, text=True,
        )
        if old.returncode != 0:
            self.skipTest("origin/master is not available")
        self.assertEqual(lines(old.stdout), [95])

    def test_repo_workflows_pass(self):
        folder = os.path.join(ROOT, ".github", "workflows")
        for name in sorted(os.listdir(folder)):
            if name.endswith((".yml", ".yaml")):
                with open(os.path.join(folder, name), encoding="utf-8") as handle:
                    self.assertEqual(lines(handle.read()), [], name)

    def test_yaml_file_is_scanned(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "w.yaml")
            with open(path, "w", encoding="utf-8") as handle:
                handle.write(MUST_FAIL["run"])
            run = subprocess.run(
                [sys.executable, "-I", TOOL, path], capture_output=True, text=True
            )
            self.assertEqual(run.returncode, 1)
            self.assertIn("w.yaml:2:", run.stdout)

    def test_usage(self):
        self.assertEqual(cwe.main([]), 2)


if __name__ == "__main__":
    unittest.main()
