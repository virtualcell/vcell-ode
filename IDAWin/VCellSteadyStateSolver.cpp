//
// Created by Logan Drescher on 6/25/25.
//
#include "VCellSteadyStateSolver.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>

#include <cvode/cvode.h>
#include <nvector/nvector_serial.h>
#include <Exception.h>
#include <ExpressionException.h>

#include "OdeResultSet.h"
#include "SteadyStateLinearAlgebra.h"
#ifdef USE_MESSAGING
#include <VCELL/SimulationMessaging.h>
#endif

/**
 * Port of COPASI's steady-state Newton method (copasi/steadystate/CNewtonMethod.cpp, CSteadyStateMethod.cpp,
 * CMathContainer::calculateJacobian and utilities/CLeastSquareSolution.cpp). Function names follow COPASI's so
 * the two can be read side by side. Differences from COPASI:
 *   - VCell has no species/compartment metadata, so every variable is treated like a COPASI global quantity
 *     when building absolute tolerances. For concentration variables this is equivalent to COPASI's species
 *     rule expressed in concentration units.
 *   - LAPACK is replaced by SteadyStateLinearAlgebra: a Jacobi SVD for DGELSY's minimum-norm least squares and
 *     balanced Hessenberg QR for DGEES's eigenvalues.
 *   - DISTANCE_OR_RATE (min of the two criteria) is a VCell addition; COPASI only offers
 *     "Distance and Rate" (max), "Distance" and "Rate".
 *   - Events fire during forward integration (as in a VCell time course); Newton ignores them, as in COPASI.
 *     Backward integration is skipped for models with events, since delays and triggers run forward in time.
 *   - With a single Jacobian (no moiety reduction), the stability analysis corresponds to COPASI's
 *     "Eigenvalues of Jacobian"; it is printed to stdout together with the Jacobian rather than to the output file.
 */

namespace {
	constexpr int MAX_DAMPING_STEPS = 32;
	constexpr long MAX_INTERNAL_STEPS = 100000; // COPASI's deterministic time-course "Max Internal Steps" default
	constexpr double EPSILON = std::numeric_limits<double>::epsilon();
	constexpr double INF = std::numeric_limits<double>::infinity();
	constexpr double NOT_A_NUMBER = std::numeric_limits<double>::quiet_NaN();
}

using SteadyStateLinearAlgebra::leastSquaresSolve;

void VCellSteadyStateSolver::configureFromInput(VCellSolverInputBreakdown& inputBreakdown) {
	settings = inputBreakdown.steadyStateSettings;
	if (!(settings.USE_NEWTON || settings.USE_INTEGRATION || settings.USE_BACK_INTEGRATION))
		throw VCell::Exception("Steady state: at least one of Newton, forward integration or backward integration must be enabled");
	if (settings.MAX_FORWARD_DURATION <= 0) throw VCell::Exception("Steady state: maximum forward integration duration must be positive");
	if (settings.MAX_BACKWARD_DURATION <= 0) throw VCell::Exception("Steady state: maximum backward integration duration must be positive");
	if (settings.MAX_ITERATIONS < 0) throw VCell::Exception("Steady state: maximum Newton iterations must not be negative");

	VCellCVodeSolver::configureFromInput(inputBreakdown);
	determineAutonomy();
}

void VCellSteadyStateSolver::determineAutonomy() {
	std::vector<std::string> symbols;
	for (int i = 0; i < NUM_EQUATIONS; i++) rateExpressions[i]->getSymbols(symbols);
	for (int i = 0; i < numDiscontinuities; i++) odeDiscontinuities[i]->discontinuityExpression->getSymbols(symbols);
	isAutonomous = std::ranges::find(symbols, "t") == symbols.end();
}

void VCellSteadyStateSolver::solve(double* paramValues, bool bPrintProgress, FILE* outputFile, void (*checkStopRequested)(double, long)) {
	if (checkStopRequested != nullptr) checkStopRequested(STARTING_TIME, 0);
	parameterValues = paramValues;
	stopRequestedCallback = checkStopRequested;
	writeFileHeader(outputFile);
	this->odeResultSet->clearData();

	// copy parameter values to the end of values, these will stay the same during solving
	memset(values, 0, (NUM_EQUATIONS + 1) * sizeof(double));
	if (NUM_PARAMETERS > 0) memcpy(values + 1 + NUM_EQUATIONS, paramValues, NUM_PARAMETERS * sizeof(double));
	memset(values + 1 + NUM_EQUATIONS + NUM_PARAMETERS, 0, numDiscontinuities * sizeof(double));

	// The start state is the initial condition at STARTING_TIME
	currentTime = STARTING_TIME;
	x.resize(NUM_EQUATIONS);
	dxdt.resize(NUM_EQUATIONS);
	absoluteTolerances.resize(NUM_EQUATIONS);
	for (int i = 0; i < NUM_EQUATIONS; i++) {
		x[i] = initialConditionExpressions[i]->evaluateVector(paramValues);
		// CMathContainer::initializeAtolVector: scale the resolution by the magnitude of the initial value
		absoluteTolerances[i] = settings.RESOLUTION;
		if (x[i] != 0.0) absoluteTolerances[i] *= std::min(1.0, std::fabs(x[i]));
	}

	methodLog.str("");
	const bool found = processInternal();
	std::cout << methodLog.str() << std::endl;
	if (!found) throw VCell::Exception("Steady state not found; see the method log for details");
	if (!allPositive()) std::cout << "WARNING: The steady state contains negative values." << std::endl;
	printStabilityAnalysis();

	for (int i = 0; i < NUM_EQUATIONS; i++) NV_Ith_S(y, i) = x[i];
	writeData(currentTime, outputFile);
#ifdef USE_MESSAGING
	SimulationMessaging::getInstVar()->setWorkerEvent(JobEvent::JOB_COMPLETED, 1, currentTime);
#endif
}

bool VCellSteadyStateSolver::processInternal() {
	const std::vector<double> startState = x;
	const double startTime = currentTime;

	if (!isAutonomous && settings.USE_NEWTON)
		methodLog << "WARNING: The model is not autonomous (it depends on t); Newton's method may not be meaningful.\n";

	if (settings.USE_NEWTON) {
		methodLog << "Try Newton's method. \n";
		if (processNewton() == NewtonResult::FOUND) return true;
	}
	if (settings.USE_INTEGRATION) {
		methodLog << "\nTry forward integration. \n";
		if (doIntegration(true)) return true;
	}
	if (settings.USE_BACK_INTEGRATION) {
		if (hasEvents()) {
			methodLog << "\nSkipping backward integration: the model has events.\n";
		} else {
			methodLog << "\nTry backward integration. \n";
			if (doIntegration(false)) return true;
		}
	}

	x = startState;
	currentTime = startTime;
	return false;
}

VCellSteadyStateSolver::NewtonResult VCellSteadyStateSolver::processNewton() {
	NewtonResult result = NewtonResult::NOT_FOUND;
	double targetValue = targetFunction();

	methodLog << "   Starting Newton Iterations...\n";
	for (int k = 0; k < settings.MAX_ITERATIONS && !isSteadyState(targetValue); k++) {
		if (stopRequestedCallback != nullptr) stopRequestedCallback(currentTime, k);
		result = doNewtonStep(targetValue);
		if (result == NewtonResult::SINGULAR_JACOBIAN || result == NewtonResult::DAMPING_LIMIT_EXCEEDED
			|| result == NewtonResult::NEGATIVE_VALUE_FOUND) break;
	}

	if (isSteadyState(targetValue)) result = NewtonResult::FOUND;
	else if (result == NewtonResult::STEP_SUCCESSFUL) result = NewtonResult::ITERATION_LIMIT_EXCEEDED;

	if (result == NewtonResult::FOUND)
		methodLog << "   Success: Target criterion matched by " << targetValueToString() << ".\n";
	else if (result == NewtonResult::ITERATION_LIMIT_EXCEEDED)
		methodLog << "   Failed: Target criterion not matched after reaching iteration limit. " << targetValueToString() << "\n";

	// do an additional Newton step to refine the result
	if (result == NewtonResult::FOUND && targetValue > 0.0) {
		methodLog << "   Do additional step to refine result...\n";
		const std::vector<double> refinedFrom = x;
		const double refinedFromValue = targetValue;
		if (doNewtonStep(targetValue) != NewtonResult::STEP_SUCCESSFUL) {
			methodLog << "   Additional step failed. Old values restored.\n";
			x = refinedFrom;
			targetValue = refinedFromValue;
			targetFunction();
		}
	}
	return result;
}

VCellSteadyStateSolver::NewtonResult VCellSteadyStateSolver::doNewtonStep(double& currentValue) {
	const std::vector<double> xOld = x;

	std::vector<double> jacobian;
	calculateJacobian(false, jacobian);
	std::vector<double> h;
	if (leastSquaresSolve(jacobian, NUM_EQUATIONS, NUM_EQUATIONS, dxdt, h) != static_cast<std::size_t>(NUM_EQUATIONS)) {
		// Rank deficient: we can still step as long as the step is not zero
		const bool isZeroStep = std::ranges::none_of(h, [](const double v) { return std::fabs(v) > 100.0 * EPSILON; });
		if (isZeroStep) {
			methodLog << "    Newton step failed. Jacobian could not be inverted.\n\n";
			return NewtonResult::SINGULAR_JACOBIAN;
		}
	}

	// Force the loop to be entered at least once; halve the step until the target value decreases
	double newValue = currentValue * 1.001;
	int i;
	for (i = 0; i < MAX_DAMPING_STEPS && !(newValue < currentValue); i++) {
		for (int j = 0; j < NUM_EQUATIONS; j++) {
			x[j] = xOld[j] - h[j];
			h[j] *= 0.5;
		}
		newValue = targetFunction();
	}

	if (i == MAX_DAMPING_STEPS) {
		// discard the step
		x = xOld;
		currentValue = targetFunction();
		methodLog << "    Newton step failed. Damping limit exceeded.\n";
		return NewtonResult::DAMPING_LIMIT_EXCEEDED;
	}

	if (!(settings.ACCEPT_NEGATIVE || allPositive())) {
		methodLog << "    Newton step failed. Negative value found.\n\n";
		return NewtonResult::NEGATIVE_VALUE_FOUND;
	}

	currentValue = newValue;
	if (i <= 1) methodLog << "    Regular Newton step.      New value: " << targetValueToString() << "\n";
	else methodLog << "    Newton step with damping. New value: " << targetValueToString() << " (" << i - 1 << " damping iteration(s))\n";
	return NewtonResult::STEP_SUCCESSFUL;
}

bool VCellSteadyStateSolver::doIntegration(const bool forward) {
	const double iterationFactor = forward ? 10.0 : 2.0;
	const double maxDuration = forward ? settings.MAX_FORWARD_DURATION : -settings.MAX_BACKWARD_DURATION;
	// minimum duration is either hardcoded or equal to maximum duration, whichever is smaller
	const double minDuration = forward ? std::min(settings.MAX_FORWARD_DURATION, 1e-1)
									   : -std::min(settings.MAX_BACKWARD_DURATION, 1e-2);

	for (double duration = minDuration; std::fabs(duration) <= std::fabs(maxDuration); duration *= iterationFactor) {
		const IntegrationResult integrationResult = integrateFromStart(duration);
		if (integrationResult == IntegrationResult::FAILED) {
			methodLog << "  Integration with duration " << duration << " failed.\n\n";
			break;
		}
		if (containsNaN()) {
			methodLog << "  Integration with duration " << duration << " failed (NaN).\n\n";
			break;
		}
		if (!(settings.ACCEPT_NEGATIVE || allPositive())) {
			methodLog << "  Integration with duration " << duration << " resulted in negative values.\n\n";
			break;
		}

		const double value = targetFunction();
		if (isSteadyState(value)) {
			methodLog << "  Integration with duration " << duration << ". Criterion matched by " << targetValueToString() << ".\n\n";
			return true;
		}
		methodLog << "  Integration with duration " << duration << ". Criterion not matched by " << targetValueToString() << ".\n\n";

		if (settings.USE_NEWTON) {
			methodLog << "  Try Newton's method from this starting point. \n";
			const NewtonResult newtonResult = processNewton();
			methodLog << "\n";
			if (newtonResult == NewtonResult::FOUND) return true;
		}

		if (integrationResult == IntegrationResult::STEP_LIMIT_REACHED) {
			methodLog << "  Integration with duration " << duration << " reached internal step limit.\n";
			break;
		}
	}
	return false;
}

VCellSteadyStateSolver::IntegrationResult VCellSteadyStateSolver::integrateFromStart(const double duration) {
	try {
		resetEvents();
		initCVode(parameterValues); // resets y to the initial conditions at STARTING_TIME
		realtype time = STARTING_TIME;
		const realtype endTime = STARTING_TIME + duration;
		IntegrationResult result = IntegrationResult::COMPLETED;
		long steps = 0;
		while (duration > 0 ? time < endTime : time > endTime) {
			if (steps >= MAX_INTERNAL_STEPS) {
				result = IntegrationResult::STEP_LIMIT_REACHED;
				break;
			}
			if (stopRequestedCallback != nullptr) stopRequestedCallback(time, steps);
			// stop at pending (delayed) event executions; only forward integration runs with events
			CVodeSetStopTime(solver, duration > 0 ? std::min(endTime, getNextEventTime()) : endTime);
			const int returnCode = CVode(solver, endTime, y, &time, CV_ONE_STEP_TSTOP);
			steps++;
			if (returnCode == CV_SUCCESS || returnCode == CV_TSTOP_RETURN || returnCode == CV_ROOT_RETURN)
				onCVodeReturn(time, returnCode);
			else
				throwCVodeErrorMessage(returnCode);
		}
		currentTime = time;
		for (int i = 0; i < NUM_EQUATIONS; i++) x[i] = NV_Ith_S(y, i);
		return result;
	} catch (const char* message) {
		methodLog << "  " << message << "\n";
	} catch (const std::string& message) {
		methodLog << "  " << message << "\n";
	} catch (VCell::Exception& ex) {
		methodLog << "  " << ex.getMessage() << "\n";
	}
	return IntegrationResult::FAILED;
}

bool VCellSteadyStateSolver::evaluateRates(const double t, const std::vector<double>& state, std::vector<double>& rates) {
	values[0] = t;
	memcpy(values + 1, state.data(), NUM_EQUATIONS * sizeof(double));
	double* discontinuities = values + 1 + NUM_EQUATIONS + NUM_PARAMETERS;
	try {
		for (int i = 0; i < numDiscontinuities; i++)
			discontinuities[i] = odeDiscontinuities[i]->discontinuityExpression->evaluateVector(values);
		for (int i = 0; i < NUM_EQUATIONS; i++) rates[i] = RHS(values, i);
		return true;
	} catch (ExpressionException&) {
		std::ranges::fill(rates, NOT_A_NUMBER);
		return false;
	}
}

/**
 * Central finite differences, as in CMathContainer::calculateJacobian. Row-major, NUM_EQUATIONS rows; when
 * includeTime is set, column 0 is the derivative with respect to t and the variables follow.
 */
void VCellSteadyStateSolver::calculateJacobian(const bool includeTime, std::vector<double>& jacobian) {
	const std::size_t rows = NUM_EQUATIONS;
	const std::size_t offset = includeTime ? 1 : 0;
	const std::size_t columns = rows + offset;
	jacobian.assign(rows * columns, 0.0);

	const double derivationFactor = std::max(settings.DERIVATION_FACTOR, 100.0 * EPSILON);
	double t = currentTime;
	std::vector<double> state = x;
	std::vector<double> y1(rows), y2(rows);

	for (std::size_t col = 0; col < columns; col++) {
		double& perturbed = (col < offset) ? t : state[col - offset];
		const double store = perturbed;
		double x1, x2;
		// We only need to make sure that we do not have an underflow problem
		if (std::fabs(store) < derivationFactor) {
			x1 = 0.0;
			x2 = (store < 0.0) ? -2.0 * derivationFactor : 2.0 * derivationFactor;
		} else {
			x1 = store * (1.0 + derivationFactor);
			x2 = store * (1.0 - derivationFactor);
		}
		const double invDelta = 1.0 / (x2 - x1);

		perturbed = x1;
		evaluateRates(t, state, y1);
		perturbed = x2;
		evaluateRates(t, state, y2);
		perturbed = store;

		for (std::size_t row = 0; row < rows; row++) jacobian[row * columns + col] = (y2[row] - y1[row]) * invDelta;
	}
}

double VCellSteadyStateSolver::targetFunction() {
	evaluateRates(currentTime, x, dxdt);
	// The rate is always computed: the non-autonomous distance needs it even under DISTANCE_ONLY
	targetRate = targetFunctionRate();
	targetDistance = (settings.STOP_CRITERIA == RATE_ONLY) ? 0.0 : targetFunctionDistance(targetRate);

	switch (settings.STOP_CRITERIA) {
		case DISTANCE_ONLY: return targetDistance;
		case RATE_ONLY: return targetRate;
		case DISTANCE_OR_RATE: return std::min(targetRate, targetDistance);
		case DISTANCE_AND_RATE:
		default: return std::max(targetRate, targetDistance);
	}
}

double VCellSteadyStateSolver::targetFunctionRate() const {
	double maxRate = 0.0;
	for (int i = 0; i < NUM_EQUATIONS; i++) {
		// relative rate, then absolute rate
		const double relative = std::fabs(dxdt[i]) / std::max(std::fabs(x[i]), absoluteTolerances[i]);
		const double absolute = std::fabs(dxdt[i]);
		if (std::isnan(relative) || std::isnan(absolute)) return INF;
		maxRate = std::max({maxRate, relative, absolute});
	}
	return maxRate;
}

/**
 * The size of the step needed to reach the steady state: solve J * distance = dxdt in the least-squares sense,
 * then take the larger of the relative and absolute norms of the distance, each including the residual of the
 * least-squares fit (CLeastSquareSolution::solve).
 */
double VCellSteadyStateSolver::targetFunctionDistance(const double rateForTime) {
	if (NUM_EQUATIONS == 0) return 0.0;

	std::vector<double> jacobian;
	calculateJacobian(!isAutonomous, jacobian);
	const std::size_t offset = isAutonomous ? 0 : 1;
	const std::size_t columns = NUM_EQUATIONS + offset;

	std::vector<double> distance;
	if (leastSquaresSolve(jacobian, NUM_EQUATIONS, columns, dxdt, distance) == 0) return INF;

	double relativeError = 0.0, absoluteError = 0.0;
	for (int row = 0; row < NUM_EQUATIONS; row++) {
		double ax = 0.0;
		for (std::size_t col = 0; col < columns; col++) ax += jacobian[row * columns + col] * distance[col];
		const double residual = std::fabs(ax - dxdt[row]);
		const double relative = residual / std::max((std::fabs(ax) + std::fabs(dxdt[row])) / 2.0, absoluteTolerances[row]);
		relativeError += relative * relative;
		absoluteError += residual * residual;
	}
	relativeError = std::isnan(relativeError) ? INF : std::sqrt(relativeError);
	absoluteError = std::isnan(absoluteError) ? INF : std::sqrt(absoluteError);

	double relativeDistance = 0.0, absoluteDistance = 0.0;
	// For a non-autonomous model the first component measures time and is weighted by the rate
	if (!isAutonomous) relativeDistance = absoluteDistance = distance[0] * distance[0] * rateForTime * rateForTime;
	for (int i = 0; i < NUM_EQUATIONS; i++) {
		const double d = distance[i + offset];
		const double relative = d / std::max(std::fabs(x[i]), absoluteTolerances[i]);
		relativeDistance += relative * relative;
		absoluteDistance += d * d;
	}
	relativeDistance = std::isnan(relativeDistance) ? INF : std::sqrt(relativeDistance + relativeError * relativeError);
	absoluteDistance = std::isnan(absoluteDistance) ? INF : std::sqrt(absoluteDistance + absoluteError * absoluteError);
	return std::max(relativeDistance, absoluteDistance);
}

bool VCellSteadyStateSolver::isSteadyState(const double value) const {
	if (value > settings.RESOLUTION) return false;
	return !containsNaN();
}

bool VCellSteadyStateSolver::containsNaN() const {
	if (!std::isfinite(currentTime)) return true;
	return std::ranges::any_of(x, [](const double v) { return !std::isfinite(v); });
}

bool VCellSteadyStateSolver::allPositive() const {
	return std::ranges::all_of(x, [this](const double v) { return v >= -settings.RESOLUTION; });
}

std::string VCellSteadyStateSolver::targetValueToString() const {
	std::ostringstream os;
	switch (settings.STOP_CRITERIA) {
		case DISTANCE_ONLY: os << "Distance: " << targetDistance; break;
		case RATE_ONLY: os << "Rate: " << targetRate; break;
		case DISTANCE_AND_RATE:
		case DISTANCE_OR_RATE: os << "Distance: " << targetDistance << ", Rate: " << targetRate; break;
	}
	return os.str();
}

void VCellSteadyStateSolver::printStabilityAnalysis() {
	std::vector<double> jacobian;
	calculateJacobian(false, jacobian);
	evaluateRates(currentTime, x, dxdt); // restore values[] to the steady state

	std::ostringstream os;
	os << std::setprecision(6);
	os << "\nJACOBIAN MATRIX (row: d(rate of variable)/d(column variable))\n";
	for (int col = 0; col < NUM_EQUATIONS; col++) os << "\t" << variableNames[col];
	os << "\n";
	for (int row = 0; row < NUM_EQUATIONS; row++) {
		os << variableNames[row];
		for (int col = 0; col < NUM_EQUATIONS; col++) os << "\t" << jacobian[row * NUM_EQUATIONS + col];
		os << "\n";
	}

	std::vector<double> realParts, imaginaryParts;
	if (!SteadyStateLinearAlgebra::eigenvalues(jacobian, NUM_EQUATIONS, realParts, imaginaryParts)) {
		os << "\nEigenvalues of the Jacobian could not be computed.\n";
		std::cout << os.str() << std::endl;
		return;
	}
	const SteadyStateLinearAlgebra::StabilityAnalysis analysis(realParts, imaginaryParts, settings.RESOLUTION);
	os << "\nEIGENVALUES OF JACOBIAN (real, imaginary)\n" << std::setprecision(17);
	for (std::size_t i = 0; i < analysis.realParts.size(); i++)
		os << analysis.realParts[i] << "\t" << analysis.imaginaryParts[i] << "\n";
	os << analysis;
	std::cout << os.str() << std::endl;
}
