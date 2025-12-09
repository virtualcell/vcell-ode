// Messaging

#include <VCELL/SimulationMessaging.h>
#include <memory.h>
// Standard Includes
#include <iomanip>
#include <fstream>
#include <sstream>
// Local Includes
#include "SundialsSolverInterface.h"

#include "StoppedByUserException.h"
#include <VCELL/GitDescribe.h>
#include <argparse/argparse.hpp>

#define CVODE_SOLVER "CVODE"
#define IDA_SOLVER "IDA"

int parseAndRunWithArgParse(int argc, char *argv[]);

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
	argumentParser.add_argument("-tid").help("id of the job").store_into(taskID);
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
	} catch (const std::runtime_error& err) {
		std::cerr << err.what() << std::endl;
		returnCode = 5;
	} catch (...) {
		std::cerr << "Unknown exception thrown." << std::endl;
		returnCode = 255;
	}

	if (outputFile != NULL) { fclose(outputFile); }
	if (inputFileStream.is_open()) { inputFileStream.close(); }
	return returnCode;
}
