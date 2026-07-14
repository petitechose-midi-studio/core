# Project persistence fixtures

These `.mspj` files are compatibility fixtures for the project migration engine.

## Fixtures

| Path | Source | Expected migration status |
| --- | --- | --- |
| `v1_0/stale-sequencer.mspj` | Copied from the UX capture `project/navigation/new-project-reset`; it contains a stale `SEQR` chunk written before project snapshot chunk version `1.1`. | `partial`, `overwriteSafe=false` |
| `v1_1/current-from-stale-sequencer.mspj` | Produced from `v1_0/stale-sequencer.mspj` with `ms-core-file-tool migrate --allow-partial`; its Macro Automation v1.4 chunk is losslessly upgraded to the lifecycle-aware v1.5 payload. | `migrated`, `overwriteSafe=true` |

The `v1_1/current-from-stale-sequencer.mspj` fixture intentionally does not
prove a lossless migration from `v1_0`; it proves that recovered/defaulted data
can be rewritten as a current-format project only after an explicit partial
output decision. It remains intentionally versioned as a compatibility fixture:
the later Macro Automation lifecycle upgrade must be reported as a safe
migration, not silently treated as current bytes.
