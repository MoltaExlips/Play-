# Experiment: link compiled blocks with wasm tail calls (not adopted)

Idea: native builds link blocks by patching jumps; the browser build returns to
`CGenericMipsExecutor::Execute` after every block (~14% of the emulation thread in
profiles). This branch ends each EE/IOP block with a tail call (`return_call_indirect`)
to `JsLinkTrampoline`, which looks up the next block and `[[clang::musttail]]`-calls it.
Needs `-DPLAYJS_TAIL_CALLS=ON` plus `tailcall-linking-codegen.patch` applied to deps/CodeGen.

Result (headless Chrome, PS2SDK vu1 sample scaled to 48x48 cubes, 15 s, two runs):

| Build | fps | renderer CPU / frame |
|-------|----:|---------------------:|
| without linking | 23.7, 21.5 | 24.83, 25.45 ms |
| with tail-call linking | 23.3, 23.9 | 26.96, 26.98 ms |

Correct (cube, vu1, TLB-mode cube render identically) but ~7% more CPU: the trampoline
still does the block lookup, and a cross-instance tail call isn't cheaper in V8 than
returning to the loop. Real gains would need direct links (e.g. a per-block cache of the
successor's table index checked in generated code) rather than a lookup per block.
