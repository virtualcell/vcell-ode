# Steady-state solver (`SOLVER CSSS`)

`VCellSteadyStateSolver` finds a steady state of a VCell ODE model. It is a port of COPASI's steady-state
Newton method (`CNewtonMethod`), reusing this repo's CVODE machinery for the integration fallback. It runs in
the same `SundialsSolverStandalone_x64` binary (and the same `pyvcell_odesolver.solve()`), selected by
`SOLVER CSSS` in the `.cvodeInput` file.

Verified against COPASI 4.48 itself: on every model in `tests/copasi_comparison/compare_with_copasi.py`, the
steady-state values and Jacobian eigenvalues agree to within 1e-6 relative.

## Input format

A `CSSS` input is an ordinary CVODE input (same `ODE … INIT …; RATE …;` equations, discontinuities, events,
tolerances) with `SOLVER CSSS` and a steady-state section:

```
SOLVER CSSS
STARTING_TIME 0.0
ENDING_TIME 1.0
RELATIVE_TOLERANCE 1.0E-9
ABSOLUTE_TOLERANCE 1.0E-12
STEADY_STATE TRUE
RESOLUTION 1e-9
MAX_NEWTON_ITERATIONS 50
MAXIMUM_FORWARD_DURATION 1e9
DERIVATION_FACTOR 0.001
STOP_CRITERIA DISTANCE_AND_RATE
NUM_EQUATIONS 1
ODE x INIT 5.0;
	 RATE (1.0 - x);
```

- `SOLVER` must come before `NUM_EQUATIONS` (the equation parser is chosen from it). The `STEADY_STATE`
  section can go anywhere at top level.
- After `STEADY_STATE TRUE`, all five keys are required, in any order, each exactly once. Unknown, duplicate,
  missing or non-numeric entries are parse errors.
- `STARTING_TIME` is where integration starts; the tolerances apply to the CVODE integrations. `ENDING_TIME`,
  `MAX_TIME_STEP` and output-time settings are accepted but unused.

| Key | Meaning | COPASI parameter | Default |
|---|---|---|---|
| `RESOLUTION` | Convergence threshold for the target function; also the zero threshold in the stability analysis | Resolution | 1e-9 |
| `MAX_NEWTON_ITERATIONS` | Newton iterations per Newton attempt (0 disables Newton in practice) | Iteration Limit | 50 |
| `MAXIMUM_FORWARD_DURATION` | Longest forward integration tried | Maximum duration for forward integration | 1e9 |
| `DERIVATION_FACTOR` | Relative perturbation for the finite-difference Jacobian | Derivation Factor | 1e-3 |
| `STOP_CRITERIA` | `DISTANCE_AND_RATE`, `DISTANCE_ONLY`, `RATE_ONLY`, `DISTANCE_OR_RATE` | Target Criterion | `DISTANCE_AND_RATE` |

`VCellSolverInputBreakdown::SteadyStateSettings` (`IDAWin/VCellSolverInput.h`) also carries COPASI's remaining
options, which currently have **no input keyword** and always take their defaults: `USE_NEWTON` (true),
`USE_INTEGRATION` (true), `USE_BACK_INTEGRATION` (false), `MAX_BACKWARD_DURATION` (1e6) and `ACCEPT_NEGATIVE`
(true; COPASI's default is false, see below). Adding a keyword for any of them is a change to
`collectSteadyStateTerms()` in `IDAWin/VCellSolverFactory.cpp` plus the VCell writer.

## Algorithm

Function names in `IDAWin/VCellSteadyStateSolver.cpp` follow COPASI's, so the two can be read side by side
(COPASI sources: `copasi/steadystate/CNewtonMethod.cpp`, `CSteadyStateMethod.cpp`, `CEigen.cpp`,
`copasi/math/CMathContainer.cpp`, `copasi/utilities/CLeastSquareSolution.cpp`).

**Overall strategy (`processInternal`).** Starting from the initial conditions at `STARTING_TIME`:
1. Newton from the initial state. Done if it converges.
2. Forward integration (`doIntegration(true)`), integrating **from the initial state** for durations 0.1, 1,
   10, … up to `MAXIMUM_FORWARD_DURATION`. After each run: stop if the integrated state already satisfies the
   criterion; otherwise run Newton from that state; stop if Newton converges. Integration failure, NaNs or the
   internal step limit (100,000 steps, COPASI's default) end the loop.
3. Backward integration, the same with durations −0.01, −0.02, −0.04, … (off by default).
4. Otherwise: no steady state.

**Newton (`processNewton`, `doNewtonStep`).** Each step solves `J h = f(x)` and tries `x − h`, halving `h`
up to 32 times until the target value decreases; if it never does, the step is discarded and Newton gives up.
A rank-deficient Jacobian is fine as long as the step is nonzero. After convergence, one extra step is taken
to refine the result (kept only if it improves the target).

**Jacobian (`calculateJacobian`).** Central differences: each variable `v` is evaluated at
`v(1 ± DERIVATION_FACTOR)`, or at `0` and `±2·DERIVATION_FACTOR` when `|v|` is smaller than the factor. For
models whose rates depend on `t`, the distance criterion also uses a time column.

**Target function (`targetFunction`).** With per-variable tolerance `atol_i = RESOLUTION · min(1, |x0_i|)`
(or `RESOLUTION` when the initial value is 0):
- **Rate**: the largest of `|f_i| / max(|x_i|, atol_i)` and `|f_i|` over all variables.
- **Distance**: the minimum-norm least-squares solution `d` of `J d = f`, i.e. the Newton step still needed.
  The larger of its relative norm `‖d_i / max(|x_i|, atol_i)‖₂` and absolute norm `‖d‖₂`, each including the
  residual of the least-squares fit.
- `DISTANCE_AND_RATE` uses the larger of the two, `DISTANCE_OR_RATE` the smaller, the others one each.
- Converged when the target is `≤ RESOLUTION` and the state has no NaN/Inf.

**Stability analysis.** At the steady state, the Jacobian's eigenvalues are classified exactly as COPASI's
`CEigen::stabilityAnalysis` does (counts of real/complex/zero/positive/negative eigenvalues, stiffness, time
hierarchy, Fold/Hopf bifurcation test functions, oscillation indicator), with `|value| ≤ RESOLUTION` treated
as zero.

## Differences from COPASI

- **No species/compartment metadata.** Every variable gets COPASI's tolerance rule for non-species
  quantities. For concentration variables this equals COPASI's species rule expressed in concentration units,
  so results match.
- **No LAPACK.** `IDAWin/SteadyStateLinearAlgebra.{h,cpp}` replaces DGELSY (a one-sided Jacobi SVD for the
  minimum-norm least-squares solve; rank cutoff `100·ε·σ_max`) and DGEES (balancing, Hessenberg reduction and
  the double-shift QR algorithm). Checked against numpy/LAPACK on 311 matrices: worst eigenvalue error
  1.3e-14 relative, worst least-squares error 6e-12.
- **`DISTANCE_OR_RATE`** is a VCell addition; COPASI only has "Distance and Rate", "Distance" and "Rate".
- **Negative values are accepted by default** (`ACCEPT_NEGATIVE = true`), because VCell variables are not
  always concentrations (e.g. voltages). A negative steady state prints a warning.
- **Events** fire during forward integration as in a VCell time course; Newton ignores them, as in COPASI.
  Backward integration is skipped for models with events.
- **One Jacobian.** COPASI reports eigenvalues of both the full and the moiety-reduced Jacobian; VCell
  produces only the full one (COPASI's "Eigenvalues of Jacobian").
- **Refinement step.** If COPASI's extra refinement step fails on negative values it keeps the new values
  anyway; this port restores the previous ones, which is what COPASI's log message says it does.

## Output

- **Output file (`.ida`)**: the usual header plus **one row** with the steady state. The `t` column is the
  time at which it was found: `STARTING_TIME` when Newton succeeds from the initial state, otherwise the end of
  the integration that led to it. VCell's `ODESimData.readIDADataFile` reads this as a one-row result set.
- **stdout**: the method log (every Newton and integration attempt and its target values), then the Jacobian,
  its eigenvalues and the stability report.
- **Exit code**: 0 on success. If no steady state is found, the process exits non-zero (253, i.e. `-3`) with
  `Steady state not found; see the method log for details` on stderr, and the output file has only the header.
  The same commit made every solver failure exit non-zero (previously the CLI always exited 0), so
  `pyvcell_odesolver.solve()` now returns 1 for failures too.

## Known limitations

- **Conservation laws.** If the ODEs still contain a conserved total (e.g. `A' = −A + 2B`, `B' = A − 2B`), the
  Jacobian is singular and Newton can converge to a point that breaks the conserved total (here
  `A = 0.8, B = 0.4` from `A0 = 1, B0 = 0`). COPASI does exactly the same on these ODEs; it only avoids this for
  reaction networks, where it removes conserved totals first. VCell normally removes them when generating the
  math ("conservation laws" option), so this matters mainly when that is off or for hand-written math models.
  A possible fix is to restrict Newton steps to the range of the Jacobian.
- **Non-autonomous models** (rates or discontinuities depend on `t`): handled as in COPASI, but a "steady
  state" of a time-dependent model may not be meaningful; a warning is logged.
- Newton runs between integrations do not see events, and backward integration is unavailable with events.

## Code layout

| File | Role |
|---|---|
| `IDAWin/VCellSteadyStateSolver.{h,cpp}` | The solver; subclasses `VCellCVodeSolver` |
| `IDAWin/SteadyStateLinearAlgebra.{h,cpp}` | Least squares, eigenvalues, stability statistics and report |
| `IDAWin/VCellSolverInput.h` | `SteadyStateSettings` and its defaults |
| `IDAWin/VCellSolverFactory.cpp` | `CSSS` → `STEADY_STATE`; `collectSteadyStateTerms()` parses the section |
| `IDAWin/VCellSundialsSolver.{h,cpp}` | `resetEvents()` / `hasEvents()`, used to restart integrations |
| `IDAWin/SundialsSolverInterface.{h,cpp}` | `activateSolver()` now returns the error code |

## Tests

- `ctest -R SteadyState` runs the regression tests in `tests/unit/steady_state_test.cpp`: Newton path,
  integration path, events, no-steady-state error, and unit tests of the linear algebra. Baselines in
  `tests/unit/resources/steady_state_*.ida.expected` are analytic steady states, not solver output.
- Cross-check against COPASI (not in ctest; needs network the first time):
  ```bash
  uv run --no-project --with python-copasi python tests/copasi_comparison/compare_with_copasi.py \
      build/bin/SundialsSolverStandalone_x64
  ```

## Integrating with VCell

As of VCell `master` (Sept 2026), VCell never asks this solver for a steady state. The only related work is the
unmerged `Steady-State-Feature` branch (PR #1553): a CVODE-only "Steady State" button and a settings panel with
Resolution, Max Newton Iterations, Max Forward Duration, Derivation Factor and the four stop criteria. Those map
one-to-one onto the input keys above, but the fields are not read, saved or written anywhere yet. What is
needed, with paths relative to the VCell repo:

1. **Store the settings on the task.** Reuse the existing steady-state task type rather than the new
   `bSteadyState` flag the branch adds to `SolverTaskDescription` (its own TODO suggests this):
   `SolverTaskDescription.TASK_STEADY` already exists and is saved to VCML as `TaskType="Steady"`
   (`cbit/vcell/xml/Xmlproducer.java`, `XmlReader.java`). Add a small settings object (e.g.
   `SteadyStateSolverSpec`) holding the five values, with defaults matching the table above, and save/load it
   in `Xmlproducer` / `XmlReader` and in `SolverTaskDescription`'s VCML read/write, `compareEqual` and copy
   constructor.

2. **Wire the panel.** In `vcell-client/.../gui/SolverTaskDescriptionAdvancedPanel.java` on the branch, read the
   text fields and combo box into that settings object and populate them from it. Map the combo box labels to
   the input keywords: "Distance only" → `DISTANCE_ONLY`, "Rate only" → `RATE_ONLY`, "Distance OR Rate" →
   `DISTANCE_OR_RATE`, "Distance AND Rate" → `DISTANCE_AND_RATE`. Validate: Resolution, Max Forward Duration
   and Derivation Factor > 0; Max Newton Iterations an integer ≥ 0.

3. **Write the input file.** In `vcell-core/.../solver/ode/CVodeFileWriter.java` / `OdeFileWriter.write()`:
   when the task is steady, have `getSolverName()` return `"CSSS"` instead of `"CVODE"` and emit the section
   right after the tolerances:
   ```java
   printWriter.println("STEADY_STATE TRUE");
   printWriter.println("RESOLUTION " + spec.getResolution());
   printWriter.println("MAX_NEWTON_ITERATIONS " + spec.getMaxNewtonIterations());
   printWriter.println("MAXIMUM_FORWARD_DURATION " + spec.getMaxForwardDuration());
   printWriter.println("DERIVATION_FACTOR " + spec.getDerivationFactor());
   printWriter.println("STOP_CRITERIA " + spec.getStopCriteria());   // e.g. DISTANCE_AND_RATE
   ```
   Everything else the writer emits (equations, discontinuities, events, tolerances) stays as is. No new
   executable or `SolverDescription` is needed: `CVodeSolverStandalone` already launches the right binary.

4. **Handle the result.** The `.ida` output needs no new reader, but it contains one row, so anything
   downstream that assumes a time course (plots, `KEEP_EVERY` logic, progress based on `ENDING_TIME`) should
   show it as a single steady state, e.g. a table of values. The solver sends no progress messages, only a
   completion event at the end.

5. **Failures.** "No steady state found" is a non-zero exit, so `org.vcell.util.exe.Executable` marks the run
   as failed and includes stderr in the error. That should be shown to the user as "no steady state found",
   ideally with the method log from stdout, which explains what was tried.

6. **Optional follow-ups.** Surface the stability report (stdout) in the UI; add keywords for
   `USE_NEWTON` / `USE_INTEGRATION` / `USE_BACK_INTEGRATION` / `ACCEPT_NEGATIVE` / `MAX_BACKWARD_DURATION` on both
   sides if users need them; consider whether the time-course "steady state" approach in the tutorials (last row
   of a long run) should switch to this solver.
