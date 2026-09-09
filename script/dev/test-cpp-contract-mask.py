"""Keep numeric separators visible to architecture checks without exposing literals."""
import runpy
from pathlib import Path

contracts = runpy.run_path(str(Path(__file__).with_name("check-architecture-contracts.py")))
source = '''void Owner::advance() {
    auto bytes = 30'720 + 0xA'B + 1'000'000;
    char quote = '\\'';
    const char* ignored = "hidden { }";
    /* hidden } */ execute(); // hidden {
}'''
masked = contracts["cpp_code_mask"](source)
assert len(masked) == len(source)
assert "30'720 + 0xA'B + 1'000'000" in masked
assert "hidden" not in masked
bodies = contracts["cpp_function_bodies"](source, "Owner::advance")
assert len(bodies) == 1 and "execute();" in bodies[0]
print("PASS: C++ numeric separators, escaped quotes, comments and balanced function bodies")
