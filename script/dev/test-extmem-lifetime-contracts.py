"""Reject unsafe allocator alternatives without weakening the lifetime guard."""
import importlib.util
from pathlib import Path

root = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location(
    "architecture_contracts", root / "script/dev/check-architecture-contracts.py")
contracts = importlib.util.module_from_spec(spec)
spec.loader.exec_module(contracts)
key = "src/app/ExtmemAllocator.hpp"
source = (root / key).read_text(encoding="utf-8")
assert not contracts.extmem_lifetime_contract_errors({key: source})

mutations = [
    ("auto overwritePool =", "auto& overwritePool ="),
    ("overwritePool = extmem_smalloc_pool", "overwritePool = another_pool"),
    ("overwritePool.do_zero = 0", "extmem_smalloc_pool.do_zero = 0"),
    ("overwritePool.do_zero = 0", "overwritePool.do_zero = 1"),
    (" && extmem_smalloc_pool.oomfn == nullptr", ""),
    ("} else {\n        allocated =", "}\n    {\n        allocated ="),
    ("sm_malloc_pool(&overwritePool, bytes)", "sm_malloc_pool(&another_pool, bytes)"),
    ("sm_malloc_pool(&extmem_smalloc_pool, bytes)", "malloc(bytes)"),
    ("sm_free_pool(&extmem_smalloc_pool, ptr)", "free(ptr)"),
    ("core::diagnostics::trackExtmemAllocationFailure();", ""),
    ("bool forOverwrite = false", "bool forOverwrite = true"),
    ("allocateExtmemStrict(sizeof(T), true)", "allocateExtmemStrict(sizeof(T), false)"),
]
for before, after in mutations:
    assert before in source, before
    altered = source.replace(before, after, 1)
    assert contracts.extmem_lifetime_contract_errors({key: altered}), before
print(f"PASS: valid allocator accepted; {len(mutations)} unsafe or regressive mutations rejected")
