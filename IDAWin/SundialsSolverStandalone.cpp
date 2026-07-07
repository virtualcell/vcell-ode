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

	const std::string programVersion{version()};

	argparse::ArgumentParser argumentParser("program_name", programVersion);
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

	return solve(inputFilePath, outputFilePath, taskID);
}
