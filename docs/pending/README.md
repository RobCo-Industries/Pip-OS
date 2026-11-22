# Pending docs

Each system-call group PR adds `docs/pending/<group>.md`: its part of the API reference in the format of [API.md](../API.md), and the behaviour a user sees.
It also adds `docs/pending/<group>.changelog.md`: its `CHANGELOG.md` lines under `### Added` or `### Changed`.
Group PRs do not edit `docs/API.md`, `CHANGELOG.md`, `README.md` or `docs/ROM_DEVELOPMENT.md`.
A later PR folds the fragments into those files and empties this folder.
