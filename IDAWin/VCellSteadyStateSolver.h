//
// Created by Logan Drescher on 6/25/25.
//

#ifndef VCELLSTEADYSTATESOLVER_H
#define VCELLSTEADYSTATESOLVER_H

#include <sstream>
#include <string>
#include <vector>

#include "VCellCVodeSolver.h"

/**
 * COPASI-style steady-state solver (`SOLVER CSSS`).
 *
 * A port of COPASI's CNewtonMethod: damped Newton iterations on the ODE right-hand side, falling back to
 * forward (and optionally backward) integration with geometrically increasing durations, re-trying Newton
 * after each integration. Convergence is judged by COPASI's "distance" (size of the Newton step needed to
 * reach the steady state) and "rate" (size of the time derivatives) criteria against RESOLUTION.
 *
 * Integration reuses the CVODE machinery (discontinuities included) of VCellCVodeSolver.
 */
class VCellSteadyStateSolver : public VCellCVodeSolver {
public:
	VCellSteadyStateSolver() = default;
	~VCellSteadyStateSolver() override = default;

	void configureFromInput(VCellSolverInputBreakdown& inputBreakdown) override;

	void solve(double* paramValues=nullptr, bool bPrintProgress=false, FILE* outputFile=nullptr, void (*checkStopRequested)(double, long)=nullptr) override;

protected:
	std::string getSolverName() override { return "CSSS"; }
	VCellSolverTypes getSolverType() override { return VCellSolverTypes::STEADY_STATE; }

private:
	enum class NewtonResult { FOUND, NOT_FOUND, ITERATION_LIMIT_EXCEEDED, DAMPING_LIMIT_EXCEEDED, SINGULAR_JACOBIAN, NEGATIVE_VALUE_FOUND, STEP_SUCCESSFUL };
	enum class IntegrationResult { COMPLETED, STEP_LIMIT_REACHED, FAILED };

	VCellSolverInputBreakdown::SteadyStateSettings settings;
	double* parameterValues = nullptr;
	void (*stopRequestedCallback)(double, long) = nullptr;
	bool isAutonomous = true;

	// Current state: time and variable values, plus the rates evaluated there
	double currentTime = 0.0;
	std::vector<double> x;
	std::vector<double> dxdt;
	std::vector<double> absoluteTolerances;
	double targetRate = 0.0;
	double targetDistance = 0.0;
	std::ostringstream methodLog;

	bool processInternal();
	NewtonResult processNewton();
	NewtonResult doNewtonStep(double& currentValue);
	bool doIntegration(bool forward);
	IntegrationResult integrateFromStart(double duration);

	bool evaluateRates(double t, const std::vector<double>& state, std::vector<double>& rates);
	void calculateJacobian(bool includeTime, std::vector<double>& jacobian);
	double targetFunction();
	double targetFunctionRate() const;
	double targetFunctionDistance(double rateForTime);
	bool isSteadyState(double value) const;
	bool containsNaN() const;
	bool allPositive() const;
	std::string targetValueToString() const;
	void determineAutonomy();
	void printStabilityAnalysis();
};

#endif //VCELLSTEADYSTATESOLVER_H
