# Test plan ledger

Tracks behavior verified manually (one-shot) but not yet covered by a committed
test. Per [AGENTS.md](../AGENTS.md#testing-discipline), an entry here is a debt:
it names the test that must replace the manual check. An empty ledger is the
goal — a growing one is a smell.

| Behavior verified by hand | How verified | Test that must replace it | Status |
| --- | --- | --- | --- |
| `carve` exit-code mapping: missing subcommand → 2, unknown subcommand → 2, known-but-unimplemented → 1, `--help` prints usage | Ran `bazel-bin/carve/carve` with no args / `frobnicate` / `refresh` / `--help` and checked exit codes and stderr | End-to-end test under `//carve/e2e` that runs the built binary and asserts exit codes + stderr, once the e2e harness exists (CARVE_DESIGN.md §9.3) | Open |
