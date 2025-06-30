// Messaging
#ifdef USE_MESSAGING
#include <VCELL/SimulationMessaging.h>
#include <memory.h>
#endif
// Standard Includes
#include <iomanip>
#include <fstream>
#include <sstream>
// Local Includes
#include "VCellCVodeSolver.h"
#include "VCellIDASolver.h"
#include "StoppedByUserException.h"
#include <VCELL/GitDescribe.h>
#include <argparse/argparse.hpp>

#include "VCellSolverFactory.h"
#define CVODE_SOLVER "CVODE"
#define IDA_SOLVER "IDA"

int parseAndRunWithArgParse(int argc, char *argv[]);
void activateSolver(std::ifstream& inputFileStream, FILE* outputFile, int taskID);
void loadJMSInfo(std::istream &ifsInput, int taskID);
void errExit(int returnCode, std::string &errorMsg);

int main(int argc, char *argv[]) {
	std::cout << std::setprecision(20);
	return parseAndRunWithArgParse(argc, argv);
}

int parseAndRunWithArgParse(int argc, char *argv[]) {
	int taskID = -1;
	std::string inputFilePath;
	std::string outputFilePath;
	std::string errMsg;
	int returnCode = 0;

	argparse::ArgumentParser argumentParser("program_name", g_GIT_DESCRIBE);
	argumentParser.add_argument("input").help("path to directory with input files.").store_into(inputFilePath);
	argumentParser.add_argument("output").help("path to directory for output files.").store_into(outputFilePath);
	#ifdef USE_MESSAGING
	argumentParser.add_argument("-tid").help("path to solver to run.").store_into(taskID);
	#endif

	try {
		argumentParser.parse_args(argc, argv);
	}
	catch (const std::exception& err) {
		std::cerr << err.what() << std::endl;
		std::cerr << argumentParser;
		return -1;
	}

	FILE *outputFile = NULL;
	std::ifstream inputFileStream{inputFilePath};
	try {
		if (!inputFileStream.is_open()) { throw std::runtime_error("input file [" + inputFilePath + "] doesn't exit!"); }

		// Open the output file...
		if ((outputFile = fopen(argv[2], "w")) == NULL) {
			throw std::runtime_error("Could not open output file[" + outputFilePath + "] for writing.");
		}
		activateSolver(inputFileStream, outputFile, taskID);

	} catch (const char *ex) {
		errMsg += ex;
		returnCode = -1;
	} catch (std::string &ex) {
		errMsg += ex;
		returnCode = -1;
	} catch (StoppedByUserException&) {
		returnCode = 0; // stopped by user;
	} catch (VCell::Exception &ex) {
		errMsg += ex.getMessage();
		returnCode = -1;
	} catch (const std::exception& err) {
		errMsg += err.what();
		returnCode = -1;
	} catch (...) {
		errMsg += "unknown error";
		returnCode = -1;
	}

	if (outputFile != NULL) { fclose(outputFile); }
	if (inputFileStream.is_open()) { inputFileStream.close(); }
	errExit(returnCode, errMsg);
	return returnCode;
}

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


	VCellSolver* targetSolver = VCellSolverFactory::produceVCellSolver(inputFileStream, taskID);
	targetSolver->solve(nullptr, true, outputFile, VCellSundialsSolver::checkStopRequested);
}

void errExit(int returnCode, std::string &errorMsg) {
	#ifdef USE_MESSAGING
	if (returnCode != 0) {
		if (SimulationMessaging::getInstVar() != nullptr && !SimulationMessaging::getInstVar()->isStopRequested()) {
			SimulationMessaging::getInstVar()->setWorkerEvent(new WorkerEvent(JOB_FAILURE, errorMsg.c_str()));
		}
	}
	#endif

	if (returnCode != 0) std::cerr << errorMsg << std::endl;
	#ifdef USE_MESSAGING
	else if (SimulationMessaging::getInstVar() != nullptr) {
		SimulationMessaging::getInstVar()->waitUntilFinished();
		delete SimulationMessaging::getInstVar();
	}
	#endif
}
