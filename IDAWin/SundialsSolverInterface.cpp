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
#include <VCELL/SimulationMessaging.h>
#include "StoppedByUserException.h"

#define CVODE_SOLVER "CVODE"
#define IDA_SOLVER "IDA"

void errExit(int returnCode, const std::string &errorMsg);

void activateSolver(std::ifstream& inputFileStream, FILE* outputFile, int taskID) {
	int returnCode = 0;
	std::string errorMsg;
	VCellSolver* targetSolver;
	// First try block - create the solver; failure => no need to delete targetSolver!
	try {
		targetSolver = VCellSolverFactory::produceVCellSolver(inputFileStream, taskID);
	} catch (const char *ex) {
		returnCode = -1;
		errorMsg = ex;
		targetSolver = nullptr;
	} catch (std::string &ex) {
		returnCode = -2;
		errorMsg = ex;
		targetSolver = nullptr;
	} catch (StoppedByUserException&) {
		returnCode = 0;
		errorMsg = "Execution Stopped By User";
		targetSolver = nullptr;
	} catch (VCell::Exception &ex) {
		returnCode = -3;
		errorMsg = ex.getMessage();
		targetSolver = nullptr;
	} catch (const std::exception& err) {
		returnCode = -4;
		errorMsg = err.what();
		targetSolver = nullptr;
	} catch (...) {
		returnCode = -5;
		errorMsg = "Unknown Error Detected";
		targetSolver = nullptr;
	}
	// second try block - solver is created; must delete it!
	if (nullptr != targetSolver) {
		try {
			targetSolver->solve(nullptr, true, outputFile, VCellSundialsSolver::checkStopRequested);
		} catch (const char *ex) {
			returnCode = -1;
			errorMsg = ex;
			// errExit(-1, ex);
		} catch (std::string &ex) {
			returnCode = -2;
			errorMsg = ex;
		} catch (StoppedByUserException&) {
			returnCode = 0;
			errorMsg = "Execution Stopped By User";
		} catch (VCell::Exception &ex) {
			returnCode = -3;
			errorMsg = ex.getMessage();
		} catch (const std::exception& err) {
			returnCode = -4;
			errorMsg = err.what();
		} catch (...) {
			returnCode = -5;
			errorMsg = "Unknown Error Detected";
		}
		// !!! DELETE THE SOLVER !!!
		delete targetSolver;
	}

	// cleanup
	SimulationMessaging::cleanupInstanceVar();

	if (!errorMsg.empty()) errExit(returnCode, errorMsg);
}

void errExit(int returnCode, const std::string &errorMsg) {
	#ifdef USE_MESSAGING
	if (returnCode && SimulationMessaging::getInstVar() && !SimulationMessaging::getInstVar()->isStopRequested()) {
		SimulationMessaging::getInstVar()->setWorkerEvent(JobEvent::JOB_FAILURE, errorMsg.c_str());
	}
	#endif
	std::cerr << errorMsg << std::endl;
}

