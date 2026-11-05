# CLAUDE.md — GlassineOS/shell

The shell, window model, settings app, first-run setup and sway session (see `README.md`). Every change: run `make check` and extend the tests for new behaviour.

- **Multi-device:** no device names in generic code. What differs per device comes from per-device config.
- The names in README's "Runtime interfaces" are used by other repositories: renaming one needs the consumers changed in the same release.
- Commits carry an `Assisted-by:` or `Co-Authored-By:` trailer. Never add `Signed-off-by`. Run `.glassine/org/scripts/safety-scan.sh --staged` before committing.
- Organization guidance for coding agents, including the project knowledge search (run `git submodule update --init` if the import below is missing):

@.glassine/org/AGENTS.md
