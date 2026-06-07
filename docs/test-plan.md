# Test plan ledger

Tracks behavior verified manually (one-shot) but not yet covered by a committed
test. Per [AGENTS.md](../AGENTS.md#testing-discipline), an entry here is a debt:
it names the test that must replace the manual check. An empty ledger is the
goal — a growing one is a smell.

| Behavior verified by hand | How verified | Test that must replace it | Status |
| --- | --- | --- | --- |
| `carve` exit-code mapping: missing subcommand → 2, unknown subcommand → 2, known-but-unimplemented → 1, `--help` prints usage | Ran `bazel-bin/carve/carve` with no args / `frobnicate` / `refresh` / `--help` and checked exit codes and stderr | End-to-end test under `//carve/e2e` that runs the built binary and asserts exit codes + stderr, once the e2e harness exists (CARVE_DESIGN.md §9.3) | Open |
| `carve refresh --aquery_proto=FILE` produces a valid compile_commands.json from real `bazel aquery --output=proto` output (12 entries over carve's own sources, JSON validated, de-Bazel applied) | Ran `bazel aquery 'mnemonic(CppCompile, //carve/...)' --output=proto` into a file, ran `carve refresh` on it, validated the JSON with python | `//carve/e2e` test that runs aquery against a synthetic `testdata/` workspace and snapshots/diffs the produced CDB (CARVE_DESIGN.md §9.3) | Open |
| `carve refresh --targets=…` runs `bazel aquery` in-process (no pre-captured proto) and emits a CDB | Ran `carve refresh --targets=//carve/cdb/... --bazel=bazel` against this repo; got a 2-entry CDB | `//carve/e2e` test driving `carve refresh --targets` against a synthetic `testdata/` workspace (the `RunAquery` subprocess glue is otherwise only dogfooded) | Open |
