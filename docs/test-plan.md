# Test plan ledger

Tracks behavior verified manually (one-shot) but not yet covered by a committed
test. Per [AGENTS.md](../AGENTS.md#testing-discipline), an entry here is a debt:
it names the test that must replace the manual check. An empty ledger is the
goal — a growing one is a smell.

| Behavior verified by hand | How verified | Test that must replace it | Status |
| --- | --- | --- | --- |
| `carve refresh --targets=…` runs `bazel aquery` in-process (no pre-captured proto) and emits a CDB | Ran `carve refresh --targets=//carve/cdb/... --bazel=bazel` against this repo; got a 2-entry CDB | `//carve/e2e` test driving `carve refresh --targets` against a synthetic `testdata/` workspace (nested-Bazel; the `RunAquery`/`BazelExecRoot` glue is otherwise only dogfooded) | Open |

Resolved:
- `carve` exit-code mapping (missing/unknown subcommand → 2, failed refresh → 1)
  is now covered by `//carve/e2e:end_to_end_test`.
- `carve refresh --aquery_proto=FILE` producing a CDB is covered by the same
  end-to-end test.
