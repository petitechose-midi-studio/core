# Project persistence fixtures

These `.mspj` files are compatibility fixtures for the project migration engine.

## Fixtures

| Path | Source | Expected migration status |
| --- | --- | --- |
| `v1_0/stale-sequencer.mspj` | Copied from the UX capture `project/navigation/new-project-reset`; it contains a stale `SEQR` chunk written before project snapshot chunk version `1.1`. | `partial`, `overwriteSafe=false` |
| `v1_0/modg-application-1.0.mspj` | Deterministic native fixture with one centered LFO assigned once as legacy `BIPOLAR` and once as legacy `UNIPOLAR`. Regenerate with `test_ProjectMigration --write-modg10-fixture <path>`. | `migrated`, `overwriteSafe=true`; assignments lift to `Around Base` and `From Base`, implicit Global Depth remains 100%, and the next write is canonical `MODG 1.3` |
| `v1_1/current-from-stale-sequencer.mspj` | Produced from `v1_0/stale-sequencer.mspj` with `ms-core-file-tool migrate --allow-partial`; its Macro Automation v1.4 chunk is losslessly upgraded to the lifecycle-aware v1.5 payload. | `migrated`, `overwriteSafe=true` |
| `v1_3/adsr-source-1.3.mspj` | Deterministic native fixture with one Project ADSR, typed Track Note wildcard route, and Macro destination. Regenerate with `test_ProjectMigration --write-adsr13-fixture <path>`. | `current`, `overwriteSafe=true`; exact native bytes and host-tool decoding retain the ADSR parameters, trigger route, and assignment |

The `v1_1/current-from-stale-sequencer.mspj` fixture intentionally does not
prove a lossless migration from `v1_0`; it proves that recovered/defaulted data
can be rewritten as a current-format project only after an explicit partial
output decision. It remains intentionally versioned as a compatibility fixture:
the later Macro Automation lifecycle upgrade must be reported as a safe
migration, not silently treated as current bytes.

`modg-application-1.0.mspj` is byte-compared against its native generator on
every Core test run. The host fixture validator then inspects it with
`ms-core-file-tool`, migrates it, and verifies that the output is current.
`adsr-source-1.3.mspj` receives the same native byte-identity check and is
independently inspected as current by the host file tool.
