//
// Created by Logan Drescher on 11/12/25.
//
// Messaging
#include "SundialsSolverInterface.h"
#include "VCellSundialsSolver.h"
#include "VCellSolverFactory.h"

// included referenced in commented out code:
#include <iomanip>
#include <sstream>
#include <VCELL/GitDescribe.h>

#define CVODE_SOLVER "CVODE"
#define IDA_SOLVER "IDA"

void activateSolver(std::ifstream& inputFileStream, FILE* outputFile, int taskID) {
	VCellSolver* targetSolver = VCellSolverFactory::produceVCellSolver(inputFileStream, taskID);
	targetSolver->solve(nullptr, true, outputFile, VCellSundialsSolver::checkStopRequested);
}

