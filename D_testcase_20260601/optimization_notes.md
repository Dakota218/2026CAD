# Optimization Notes

This file records persistent optimization changes. It is separate from
`optimization_log.md`, which is generated and overwritten by each solver run.

## 2026-06-18 Status

### Testcase0 baseline and current result

Checker result before optimization:

```text
SS: WNS -0.5286 | TNS -1134.1601 | NVP 18737
FF: WNS -0.1085 | TNS    -3.0381 | NVP   123
Area: 6997.0091
```

Latest completed checker result before the global target solver experiment:

```text
SS: WNS -0.4593 | TNS  -867.4163 | NVP 15918
FF: WNS -0.0810 | TNS    -2.0334 | NVP    77
Area: 7001.7788 (+0.07%)
Legality: input/output floating 0, multi-drive 0, illegal fanout 0, illegal path 0
```

### Clock period parsing fix

The delay reports use:

```text
Clock Period : 0.3
```

The old parser only matched `Clock Period:` exactly, so `clock_period` stayed
at zero. That made the optimizer's internal timing model disagree with the
checker. The parser now accepts `Clock Period` with spaces before the colon.

### NVP-oriented cleanup

NVP means number of violating paths. The cleanup phase now considers a much
larger setup violation window and ranks sinks by net capture benefit:

- setup capture sinks want more clock delay
- setup launch sinks are penalized because delaying them hurts setup
- hold launch/capture pressure is protected by phase acceptance checks

Internal insertion was also enabled during cleanup so one move can affect an
entire subtree, not only one leaf sink.

### Beam / sequence search

The cleanup and group phases no longer use pure one-step greedy selection.
They use a bounded beam-style sequence search:

- evaluate a first layer of moves
- keep several promising states
- expand a second move from those states
- accept the best legal sequence found within the exact-evaluation budget

The output best-tree selection now uses robust score first, then NVP/TNS/WNS
tie-breakers, so useful NVP reductions are not discarded just because WNS is
not the first sorting key.

### Global sink skew target solver, first version

The global sink target solver estimates target delay deltas and feeds
additional candidates into TNS/NVP cleanup. It was first tried as a standalone
phase after WNS repair, but testcase0 showed that this spent too much time on
hold-balanced targets and slowed SS NVP reduction. The standalone phase was
therefore disabled. A second attempt mixed direct target-specific insertion
candidates into cleanup, but testcase0 first-iteration NVP was slightly worse
because those candidates crowded out the high-count cleanup candidates. The
solver is now used inside cleanup only to bias sink/ancestor priority; direct
target-specific insertion candidates are kept behind the disabled standalone
phase for later experiments.

Because target-biased priorities add more high-ranking candidates, the cleanup
exact-evaluation budget was raised from 768 to 1024. On testcase0 this restored
the first cleanup iteration to the previous beam-search result:

```text
After WNS repair: SS NVP 18745
Cleanup iter 0:  SS NVP 17590
```

The intended flow is:

1. Use all current setup and hold violations to estimate a target delay delta
   per sink.
2. Positive target means the sink/subtree should receive more clock delay.
3. Negative target means the sink/subtree should receive less clock delay.
4. Map positive targets to leaf/internal buffer insertion candidates whose
   chain delay is close to the target.
5. Map negative targets into ancestor resize priorities toward faster cells.
6. Let the existing exact timing evaluation and beam search choose legal
   candidates, rather than trusting the target estimate blindly.

This is not a full ILP yet, but it is more global than local greedy move
ranking because the target for each sink is accumulated from many paths before
candidate generation. Exact timing evaluation still decides which candidates
are actually accepted.

## 2026-06-18 incremental STA and target-delay rerun

This update replaced the hottest candidate-evaluation path with an incremental
timing engine and added target-delay driven WNS repair candidates.

### Code changes

- `TimingEngine` now builds reusable timing indices during initialization:
  - clock node to descendant sink IDs
  - sink to launch/capture setup paths
  - sink to launch/capture hold paths
  - cached per-path slack state
  - negative-slack multisets for WNS/TNS/NVP updates
- `TimingEngine::evaluateMove()` estimates `ResizeBuffer` and inserted-chain
  moves by computing only the affected subtree delay delta and then touching
  only incident paths. The engine rolls temporary slack updates back before
  returning, so the optimizer can use it as a fast what-if evaluator.
- `optimizer.cpp` now converts optimizer `Move` objects into `TimingMove`
  objects and uses incremental evaluation in the first-layer and beam-expanded
  candidate loops. The selected best candidate is still calibrated once with
  full `analyzeTiming()` before it is returned.
- WNS repair candidate generation no longer blindly enumerates every 1/2/3
  buffer combination. A `ChainCandidate` lookup table is built from the buffer
  library, and each violated setup path asks for a small number of chains whose
  SS delay is closest to the target delta `-slack`, with area as a tie-breaker.
- `main.cpp` now prints a final summary in the same format as the checker-style
  report:
  - Original SS/FF WNS, TNS, NVP
  - Modified SS/FF WNS, TNS, NVP
  - Area before/after and percentage change

### Rerun protocol

`make` was run first and is not counted in the 10-minute runtime limit.
Each solver invocation was run with `timeout 600`; checker runtime was run
afterward and is not counted in the solver limit.

Logs from this run:

```text
/tmp/cadd_tc0_rerun.log
/tmp/cadd_tc2_rerun.log
```

### testcase0 result

Solver exit: 0
Checker exit: 0
Solver wall time: 570.72 sec

```text
Original:
SS WNS = -0.5286, TNS = -1134.1601, NVP = 18737
FF WNS = -0.1085, TNS = -3.0381,  NVP = 123

Modified:
SS WNS = -0.3385, TNS = -798.6013, NVP = 15299
FF WNS = -0.0279, TNS = -0.4664,  NVP = 41

Area:
6997.0091 -> 7000.4360 (+0.05%)
```

Legality:

```text
Input floating: 0
Output floating: 0
Multi-drive: 0
Illegal input: 0
Illegal buffer: 0
Illegal FF: 0
Illegal fanout: 0
Mismatched nodes with clk_tree.structure: 0
Illegal path: 0
```

### testcase2 result

Solver exit: 0
Checker exit: 0
Solver wall time: 552.84 sec

```text
Original:
SS WNS = -0.3020, TNS = -98.6976, NVP = 2156
FF WNS = -0.0305, TNS = -0.1023,  NVP = 5

Modified:
SS WNS = -0.0932, TNS = -19.6859, NVP = 784
FF WNS = 0, TNS = 0.0000,  NVP = 0

Area:
980.4140 -> 801.3197 (-18.27%)
```

Legality:

```text
Input floating: 0
Output floating: 0
Multi-drive: 0
Illegal input: 0
Illegal buffer: 0
Illegal FF: 0
Illegal fanout: 0
Mismatched nodes with clk_tree.structure: 0
Illegal path: 0
```
