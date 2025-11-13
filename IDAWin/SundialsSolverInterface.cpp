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
	// std::string solver;
	//
	// while (!inputFileStream.eof()) { // Note break statement if "SOLVER" encountered
	// 	std::string nextToken;
	// 	inputFileStream >> nextToken;
	// 	if (nextToken.empty()) continue;
	// 	if (nextToken[0] == '#') getline(inputFileStream, nextToken);
	// 	else if (nextToken == "JMS_PARAM_BEGIN") {
	// 		loadJMSInfo(inputFileStream, taskID);
	// 		#ifdef USE_MESSAGING
	// 		SimulationMessaging::getInstVar()->start(); // start the thread
	// 		#endif
	// 	} else if (nextToken == "SOLVER") {
	// 		inputFileStream >> solver;
	// 		break;
	// 	}
	// }
	// #ifdef USE_MESSAGING
	// // should only happen during testing for solver compiled with messaging but run locally.
	// if (SimulationMessaging::getInstVar() == nullptr) { SimulationMessaging::create(); }
	// #endif
	//
	// if (solver.empty()) { throw "Solver not defined "; }
	// VCellSundialsSolver *vss = nullptr;
	//
	// if (solver == IDA_SOLVER) {
	// 	vss = new VCellIDASolver();
	// } else if (solver == CVODE_SOLVER) {
	// 	vss = new VCellCVodeSolver();
	// } else {
	// 	std::stringstream ss;
	// 	ss << "Solver " << solver << " not defined!";
	// 	throw ss.str();
	// }
	//
	// vss->solve(nullptr, true, outputFile, VCellSundialsSolver::checkStopRequested);
	// delete vss;

	// Check if we have messaging and a taskID of -1; if so; fail now.

	#ifdef USE_MESSAGING
	if (taskID < 0) {
		throw std::runtime_error("task id of value: " + std::to_string(taskID) + " not acceptable when this library is built with messaging");
	}
	#endif

	VCellSolver* targetSolver = VCellSolverFactory::produceVCellSolver(inputFileStream, taskID);
	targetSolver->solve(nullptr, true, outputFile, VCellSundialsSolver::checkStopRequested);
}

