//
// Created by Logan Drescher on 6/26/25.
//

#ifndef VCELLSOLVERINPUT_H
#define VCELLSOLVERINPUT_H
#include <vector>
#include <utility>
#include <sundials/sundials_types.h>
#include "Expression.h"

enum VCellSolverTypes{ CVODE, IDA, STEADY_STATE};
enum VCellSteadyStateStopCriteria{DISTANCE_ONLY, RATE_ONLY, DISTANCE_OR_RATE, DISTANCE_AND_RATE};

/// TODO: For better memory management, vectors should have their size initialized by the
///		values in the input file, and allocated on the heap!
struct VCellSolverInputBreakdown {
	// Typedefs

	struct ModelSettings {
		std::vector<std::string> PARAMETER_NAMES;
		std::vector<std::string> VARIABLE_NAMES;
		std::vector<std::string> INITIAL_CONDITION_EXPRESSIONS;
		std::vector<std::string> RATE_EXPRESSIONS;
		std::vector<std::string> FLAT_TRANSFORM_MATRIX;
		std::vector<std::string> FLAT_INVERSE_TRANSFORM_MATRIX;
		int NUM_DIFFERENTIAL;
		int NUM_ALGEBRAIC;
	};

	struct TimeCourseSettings {
		realtype STARTING_TIME;
		realtype ENDING_TIME;
		realtype RELATIVE_TOLERANCE;
		realtype ABSOLUTE_TOLERANCE;
		double OUTPUT_TIME_STEP = 0.0;
		double MAX_TIME_STEP = 0.0;
		std::vector<double> TIME_POINTS;
		long KEEP_EVERY = 0.0l;
	};

	// Defaults mirror COPASI's CNewtonMethod / CSteadyStateMethod parameter defaults, except
	// ACCEPT_NEGATIVE: VCell variables are not necessarily concentrations (e.g. voltages).
	// Only the first five fields are read from the input file; the rest keep their defaults.
	struct SteadyStateSettings {
		realtype RESOLUTION = 1.0e-9;
		realtype DERIVATION_FACTOR = 1.0e-3; // relative perturbation for the finite-difference Jacobian
		int MAX_ITERATIONS = 50; // how many times to newton-step before giving up
		realtype MAX_FORWARD_DURATION = 1.0e9;
		VCellSteadyStateStopCriteria STOP_CRITERIA = DISTANCE_AND_RATE;
		realtype MAX_BACKWARD_DURATION = 1.0e6;
		bool USE_NEWTON = true;
		bool USE_INTEGRATION = true;
		bool USE_BACK_INTEGRATION = false;
		bool ACCEPT_NEGATIVE = true;
	};

	struct DiscontinuityComponents {
		std::string symbol;
		VCell::Expression* discontinuityExpression;
		VCell::Expression* rootFindingExpression;
	};


	struct DiscontinuitiesSettings {
		std::vector<DiscontinuityComponents> DISCONTINUITIES;
	};

	struct EventComponents {
		std::string eventName;
		std::string triggerExpression;
		std::string delayDurationExpression;
		std::vector<std::pair<int, std::string>> eventAssignments;
		bool shouldUseValuesAtTriggerTime = true;
	};

	struct EventSettings {
		std::vector<EventComponents> EVENTS;
	};

	VCellSolverTypes solverType;
	ModelSettings modelSettings;
	TimeCourseSettings timeCourseSettings;
	SteadyStateSettings steadyStateSettings;
	DiscontinuitiesSettings discontinuitiesSettings;
	EventSettings eventSettings;
};

#endif //VCELLSOLVERINPUT_H
