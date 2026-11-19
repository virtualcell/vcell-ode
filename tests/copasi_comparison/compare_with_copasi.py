"""Cross-check the CSSS steady-state solver against COPASI itself.

Builds each model as COPASI ODE-type global quantities, runs COPASI's Steady-State task (Newton method,
default settings, stability analysis on), runs SundialsSolverStandalone on the equivalent .cvodeInput, and
compares steady-state values and Jacobian eigenvalues.

Usage (needs network the first time, to fetch python-copasi):
    uv run --no-project --with python-copasi python tests/copasi_comparison/compare_with_copasi.py \
        build/bin/SundialsSolverStandalone_x64
"""

import re
import subprocess
import sys
import tempfile
from pathlib import Path

import COPASI

# name -> list of (variable, initial value, rate expression); rates use COPASI infix syntax
MODELS = {
    "linear": [("x", 5.0, "1 - x")],
    "smoke": [
        ("s2", 4.0, "-((2 * (-1 + 0.5 * s2) * s2) - (4 * (3.5 - 0.5 * s2)))"),
        ("s4", 0.0, "0"),
    ],
    "chain": [
        ("S", 0.0, "10 - 5 * S / (1 + S) - 0.1 * S"),
        ("E", 1.0, "5 * S / (1 + S) - 2 * E"),
        ("P", 0.0, "2 * E - 0.5 * P * P"),
    ],
    "brusselator": [("X", 0.5, "1 - 2 * X + X * X * Y"), ("Y", 3.0, "X - X * X * Y")],
    "quadratic": [("x", 0.0, "1 - x * x")],
}
TOLERANCE = 1e-6  # relative; both solvers stop at RESOLUTION 1e-9 but take different paths there


def solve_with_copasi(variables):
    data_model = COPASI.CRootContainer.addDatamodel()
    model = data_model.getModel()
    quantities = {}
    for name, initial, _ in variables:
        quantity = model.createModelValue(name, initial)
        quantity.setStatus(COPASI.CModelEntity.Status_ODE)
        quantities[name] = quantity
    for name, _, rate in variables:
        def to_cn(match):
            symbol = match.group(0)
            if symbol not in quantities:
                return symbol
            return "<" + quantities[symbol].getValueReference().getCN().getString() + ">"
        quantities[name].setExpression(re.sub(r"[A-Za-z_]\w*", to_cn, rate))
    model.compileIfNecessary()

    task = data_model.getTask("Steady-State")
    task.getProblem().setJacobianRequested(True)
    task.getProblem().setStabilityAnalysisRequested(True)
    task.setScheduled(True)
    if not task.initialize(COPASI.CCopasiTask.OUTPUT_UI):
        raise RuntimeError(COPASI.CCopasiMessage.getAllMessageText())
    task.process(True)
    if task.getResult() not in (COPASI.CSteadyStateMethod.found, COPASI.CSteadyStateMethod.foundEquilibrium):
        raise RuntimeError(f"COPASI found no steady state (result {task.getResult()})")

    values = {name: quantities[name].getValue() for name, _, _ in variables}
    eigen = task.getEigenValues()
    real, imaginary = eigen.getR(), eigen.getI()
    eigenvalues = sorted((complex(real.get(i), imaginary.get(i)) for i in range(real.size())), key=lambda c: (c.real, c.imag))
    COPASI.CRootContainer.removeDatamodel(data_model)
    return values, eigenvalues


def solve_with_vcell(executable, variables, workdir):
    lines = [
        "SOLVER CSSS", "STARTING_TIME 0.0", "ENDING_TIME 1.0", "RELATIVE_TOLERANCE 1.0E-9",
        "ABSOLUTE_TOLERANCE 1.0E-12", "STEADY_STATE TRUE", "RESOLUTION 1e-9", "MAX_NEWTON_ITERATIONS 50",
        "MAXIMUM_FORWARD_DURATION 1e9", "DERIVATION_FACTOR 0.001", "STOP_CRITERIA DISTANCE_AND_RATE",
        f"NUM_EQUATIONS {len(variables)}",
    ]
    for name, initial, rate in variables:
        lines += [f"ODE {name} INIT {initial};", f"\t RATE ({rate});"]
    input_path, output_path = workdir / "model.cvodeInput", workdir / "model.ida"
    input_path.write_text("\n".join(lines) + "\n")
    run = subprocess.run([executable, str(input_path), str(output_path)], capture_output=True, text=True)
    if run.returncode != 0:
        raise RuntimeError(f"vcell-ode failed (exit {run.returncode}): {run.stderr}")

    header, row = output_path.read_text().splitlines()[:2]
    values = dict(zip(header.rstrip(":").split(":")[1:], map(float, row.split()[1:])))
    eigen_block = run.stdout.split("EIGENVALUES OF JACOBIAN (real, imaginary)\n")[1].split("\n\n")[0]
    eigenvalues = sorted((complex(*map(float, line.split())) for line in eigen_block.strip().splitlines()), key=lambda c: (c.real, c.imag))
    return values, eigenvalues


def close(a, b):
    return abs(a - b) <= TOLERANCE * max(1.0, abs(a), abs(b))


def main():
    executable = sys.argv[1] if len(sys.argv) > 1 else "build/bin/SundialsSolverStandalone_x64"
    failures = 0
    with tempfile.TemporaryDirectory() as tmp:
        for model_name, variables in MODELS.items():
            copasi_values, copasi_eigen = solve_with_copasi(variables)
            vcell_values, vcell_eigen = solve_with_vcell(executable, variables, Path(tmp))
            ok = all(close(copasi_values[v], vcell_values[v]) for v in copasi_values) and \
                len(copasi_eigen) == len(vcell_eigen) and all(close(a, b) for a, b in zip(copasi_eigen, vcell_eigen))
            failures += not ok
            print(f"{'OK  ' if ok else 'DIFF'} {model_name}")
            if not ok:
                print(f"     COPASI values {copasi_values}\n     vcell  values {vcell_values}")
                print(f"     COPASI eigenvalues {copasi_eigen}\n     vcell  eigenvalues {vcell_eigen}")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
