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
	try {
		VCellSolver* targetSolver = VCellSolverFactory::produceVCellSolver(inputFileStream, taskID);
		targetSolver->solve(nullptr, true, outputFile, VCellSundialsSolver::checkStopRequested);
	} catch (const char *ex) {
		errExit(-1, ex);
	} catch (std::string &ex) {
		errExit(-2, ex);
	} catch (StoppedByUserException&) {
		errExit(0, "Execution Stopped By User");
	} catch (VCell::Exception &ex) {
		errExit(-3, ex.getMessage());
	} catch (const std::exception& err) {
		errExit(-4, err.what());
	} catch (...) {
		errExit(-5, "unknown error");
	}

	// cleanup
	if (SimulationMessaging::getInstVar() != nullptr) {
		SimulationMessaging::getInstVar()->waitUntilFinished();
		delete SimulationMessaging::getInstVar();
	}
}

void errExit(int returnCode, const std::string &errorMsg) {
	#ifdef USE_MESSAGING
	if (returnCode && SimulationMessaging::getInstVar() && !SimulationMessaging::getInstVar()->isStopRequested()) {
		SimulationMessaging::getInstVar()->setWorkerEvent(JobEvent::JOB_FAILURE, errorMsg.c_str());
	}
	#endif
	std::cerr << errorMsg << std::endl;
}

