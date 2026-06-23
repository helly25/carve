# Test plan ledger

Tracks behavior verified manually (one-shot) but not yet covered by a committed
test. Per [AGENTS.md](../AGENTS.md#testing-discipline), an entry here is a debt:
it names the test that must replace the manual check. An empty ledger is the
goal — a growing one is a smell.

| Behavior verified by hand                                                          | How verified | Test that must replace it | Status |
| ---------------------------------------------------------------------------------- | ------------ | ------------------------- | ------ |
| _(empty — every previously-dogfooded behavior is now covered by a committed test)_ |              |                           |        |

Resolved:
- `carve` exit-code mapping (missing/unknown subcommand → 2, failed refresh → 1)
  is covered by `//carve/e2e:end_to_end_test`.
- `carve refresh --aquery_proto=FILE` producing a CDB is covered by the same
  end-to-end test.
- `carve refresh --targets=…` (in-process `bazel aquery` plus the
  `bazel info execution_root` directory default) is covered hermetically by
  `//carve/e2e:end_to_end_test` using a fake `bazel` stub.
