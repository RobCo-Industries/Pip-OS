"""pytest plugin for the tests of a Pip-OS ROM project.

    pytest_plugins = ["pipos_testing.plugin"]

It loads RobCo-Core's harness.plugin: the --board option, the board
fixture, need_mtools and robco_build. Importing pipos_testing first puts
the harness on sys.path.
"""

import pipos_testing  # noqa: F401  puts RobCo-Core's harness and tools on sys.path

pytest_plugins = ["harness.plugin"]
