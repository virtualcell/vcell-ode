// Debug
#ifdef _DEBUG
	//#define _CRTDBG_MAP_ALLOC
	#ifdef _CRTDBG_MAP_ALLOC
	#include <stdlib.h>
	#include <crtdbg.h>
	#else
	//#include <vld.h>
	#endif
#endif
// Messaging
#ifdef USE_MESSAGING
#include <VCELL/SimulationMessaging.h>
#endif
// Standard Includes
#include <iomanip>
#include <fstream>
#include <sstream>
#include <memory.h>
#include <cstdlib>
// Local Includes
#include "VCellCVodeSolver.h"
#include "VCellIDASolver.h"
#include "OdeResultSet.h"
#include "StoppedByUserException.h"
#include <VCELL/GitDescribe.h>



#define CVODE_SOLVER "CVODE"
#define IDA_SOLVER "IDA"

void printUsage() {
	std::string usageMessage{"Usage: SundialsSolverStandalone input output"};
	#ifdef USE_MESSAGING
	usageMessage += " [-tid 0]";
	#endif
	std::cout << usageMessage << std::endl;
}

void loadJMSInfo(std::istream& ifsInput, int taskID) {
	char *broker = new char[256];
	char *smqusername = new char[256];
	char *password = new char[256];
	char *qname = new char[256];
	char *tname = new char[256];
	char *vcusername = new char[256];
	string nextToken;
	int simKey, jobIndex;

	while (!ifsInput.eof()) {			
		nextToken = "";
		ifsInput >> nextToken;			
		if (nextToken.size() == 0) {
			continue;
		} else if (nextToken[0] == '#') {
			getline(ifsInput, nextToken);
			continue;
		}  else if (nextToken == "JMS_PARAM_END") {
			break;
		} else if (nextToken == "JMS_BROKER") {
			std::string brokerStr;
			ifsInput >> brokerStr;
			memset(broker, 0, 256 * sizeof(char));
			strncpy(broker, brokerStr.c_str(), 256);
		} else if (nextToken == "JMS_USER") {
			std::string usernameStr, passwordStr;
			ifsInput >> usernameStr >> passwordStr;
			memset(smqusername, 0, 256 * sizeof(char));
			memset(password, 0, 256 * sizeof(char));
			strncpy(smqusername, usernameStr.c_str(), 256);
			strncpy(password, passwordStr.c_str(), 256);
		} else if (nextToken == "JMS_QUEUE") {
			std::string qnameStr;
			ifsInput >> qnameStr;
			memset(qname, 0, 256 * sizeof(char));
			strncpy(qname, qnameStr.c_str(), 256);
		} else if (nextToken == "JMS_TOPIC") {
			std::string topicStr;
			ifsInput >> topicStr;
			memset(tname, 0, 256 * sizeof(char));
			strncpy(tname, topicStr.c_str(), 256);
		} else if (nextToken == "VCELL_USER") {
			std::string vcusernameStr;
			ifsInput >> vcusernameStr;
			memset(vcusername, 0, 256 * sizeof(char));
			strncpy(vcusername, vcusernameStr.c_str(), 256);
		} else if (nextToken == "SIMULATION_KEY") {
			ifsInput >> simKey;
			continue;
		} else if (nextToken == "JOB_INDEX") {
			ifsInput >> jobIndex;
			continue;
		} 
	}

#ifdef USE_MESSAGING	
	if (taskID >= 0) {
		SimulationMessaging::create(broker, smqusername, password, qname, tname, vcusername, simKey, jobIndex, taskID);
	} else {
		SimulationMessaging::create();
	}
#endif
}

void errExit(int returnCode, std::string& errorMsg) {
#ifdef USE_MESSAGING
	if (returnCode != 0) {
		if (SimulationMessaging::getInstVar() != 0 && !SimulationMessaging::getInstVar()->isStopRequested()) {
			SimulationMessaging::getInstVar()->setWorkerEvent(new WorkerEvent(JOB_FAILURE, errorMsg.c_str()));
		}	
	}
	if (SimulationMessaging::getInstVar() != 0) {
		SimulationMessaging::getInstVar()->waitUntilFinished();
		delete SimulationMessaging::getInstVar();
	} else {
		if (returnCode != 0) {	
			std::cerr << errorMsg << std::endl;
		}
	}
#else
	if (returnCode != 0) {	
		std::cerr << errorMsg << std::endl;
	}
#endif
}

int main(int argc, char *argv[]) {
    	std::cout 
	    << "Sundials Standalone version " << g_GIT_DESCRIBE
	    << std::endl; 
	std::cout << std::setprecision(20);

	int taskID = -1;
	string inputfname;
	string outputfname;
	string solver;
	string errMsg;
	int returnCode = 0;

	if (argc < 3) {
		std::cout << "Missing arguments!" << std::endl;
		printUsage();
		exit(1);
	}
	for (int i = 1; i < argc; i ++) {
		if (!strcmp(argv[i], "-tid")) {
#ifdef USE_MESSAGING
			i ++;
			if (i >= argc) {
				std::cout << "Missing taskID!" << std::endl;
				printUsage();
				exit(1);
			}
			for (int j = 0; j < (int)strlen(argv[i]); j ++) {
				if (argv[i][j] < '0' || argv[i][j] > '9') {
					std::cout << "Wrong argument : " << argv[i] << ", taskID must be an integer!" << std::endl;
					printUsage();
					exit(1);
				}
			}
			taskID = atoi(argv[i]);
#else
			std::cout << "Wrong argument : " << argv[i] << std::endl;
			printUsage();
			exit(1);
#endif
		} else {
			inputfname = argv[i];
			i ++;
			outputfname = argv[i];
		}	
	}

	FILE* outputFile = NULL;
	std::ifstream inputstream(inputfname.c_str());
	try {		
		if (!inputstream.is_open()) {
			throw std::string("input file [") + inputfname + "] doesn't exit!";
		}

		// Open the output file...		
		if ((outputFile = fopen(argv[2], "w")) == NULL) {
			throw std::string("Could not open output file[") +  outputfname + "] for writing.";
		}

		string nextToken;		

		while (!inputstream.eof()) {			
			nextToken = "";
			inputstream >> nextToken;	
			if (nextToken.empty()) {
				continue;
			} else if (nextToken[0] == '#') {
				getline(inputstream, nextToken);
				continue;
			} else if (nextToken == "JMS_PARAM_BEGIN") {
				loadJMSInfo(inputstream, taskID);
#ifdef USE_MESSAGING
				SimulationMessaging::getInstVar()->start(); // start the thread
#endif				
			} else if (nextToken == "SOLVER") {
				inputstream >> solver;
				break;
			}
		}
#ifdef USE_MESSAGING
		// should only happen during testing for solver compiled with messaging but run locally.
		if (SimulationMessaging::getInstVar() == nullptr) {
			SimulationMessaging::create();
		}
#endif

		if (solver.empty()) {
			throw "Solver not defined ";
		}

#ifdef _CRTDBG_MAP_ALLOC
		_CrtMemState s1, s2, s3;
#endif
		errMsg += solver + " solver failed : ";
#ifdef _CRTDBG_MAP_ALLOC
		_CrtMemCheckpoint( &s1 );
#endif
		VCellSundialsSolver* vss = 0;
		if (solver == IDA_SOLVER) {
			vss = new VCellIDASolver();
		} else if (solver == CVODE_SOLVER) {
			vss = new VCellCVodeSolver();
		} else {
			std::stringstream ss;
			ss << "Solver " << solver << " not defined!";
			throw ss.str();
		}
		vss->readInput(inputstream);
		vss->solve(0, true, outputFile, VCellSundialsSolver::checkStopRequested);

		delete vss;
#ifdef _CRTDBG_MAP_ALLOC
		_CrtMemCheckpoint( &s2 );
		if ( _CrtMemDifference( &s3, &s1, &s2) )
		_CrtMemDumpStatistics( &s3 );
		_CrtDumpMemoryLeaks();
#endif		
	} catch (const char* ex) {
		errMsg += ex;
		returnCode = -1;
	} catch (string& ex) {
		errMsg += ex;
		returnCode = -1;
	} catch (StoppedByUserException) {
		returnCode = 0;  // stopped by user;
	} catch (VCell::Exception& ex) {
		errMsg += ex.getMessage();
		returnCode = -1;
	} catch (...) {
		errMsg += "unknown error";
		returnCode = -1;
	}
	
	if (outputFile != NULL) {
		fclose(outputFile);
	}
	if (inputstream.is_open()) {
		inputstream.close();
	}

	errExit(returnCode, errMsg);
#ifdef _CRTDBG_MAP_ALLOC
	_CrtDumpMemoryLeaks();
#endif
	return returnCode;
}
