# Project persistence fixtures

These `.mspj` files are compatibility fixtures for the project migration engine.

## Fixtures

| Path | Source | Expected migration status |
| --- | --- | --- |
| `v1_0/stale-sequencer.mspj` | Copied from the UX capture `project/navigation/new-project-reset`; it contains a stale `SEQR` chunk written before project snapshot chunk version `1.1`. | `partial`, `overwriteSafe=false` |
| `v1_0/modg-application-1.0.mspj` | Deterministic native fixture with one centered LFO assigned once as legacy `BIPOLAR` and once as legacy `UNIPOLAR`. Regenerate with `test_ProjectMigration --write-modg10-fixture <path>`. | `migrated`, `overwriteSafe=true`; assignments lift to `Around Base` and `From Base` and the next write is canonical `MODG 1.1` |
| `v1_1/current-from-stale-sequencer.mspj` | Produced from `v1_0/stale-sequencer.mspj` with `ms-core-file-tool migrate --allow-partial`; its Macro Automation v1.4 chunk is losslessly upgraded to the lifecycle-aware v1.5 payload. | `migrated`, `overwriteSafe=true` |

The `v1_1/current-from-stale-sequencer.mspj` fixture intentionally does not
prove a lossless migration from `v1_0`; it proves that recovered/defaulted data
can be rewritten as a current-format project only after an explicit partial
output decision. It remains intentionally versioned as a compatibility fixture:
the later Macro Automation lifecycle upgrade must be reported as a safe
migration, not silently treated as current bytes.

`modg-application-1.0.mspj` is byte-compared against its native generator on
every Core test run. The host fixture validator then inspects it with
`ms-core-file-tool`, migrates it, and verifies that the output is current.
