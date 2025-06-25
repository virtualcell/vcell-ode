#ifndef VCELLSUNDIALSSOLVER_H
#define VCELLSUNDIALSSOLVER_H

#include <iostream>
#include <string>
#include <vector>
#include <list>


#include <Expression.h>


#include <nvector/nvector_serial.h>
#include <sundials/sundials_types.h>

class SymbolTable;
class OdeResultSet;

#define bytesPerSample 25
#define MaxFileSizeBytes 1000000000 /* 1 gigabyte */	
#define BAD_EXPRESSION_MSG " is not terminated by ';'"
#define MAX_NUM_EVENTS_DISCONTINUITIES_EVAL 50

struct EventAssignment {
	int varIndex;
	VCell::Expression* assignmentExpression;

	~EventAssignment() {
		delete assignmentExpression;
	}
	void bind(SymbolTable* symbolTable) const {
		assignmentExpression->bindExpression(symbolTable);
	}
};

struct Event {
	std::string name;
	VCell::Expression* triggerExpression;
	bool bUseValuesAtTriggerTime;
	VCell::Expression* delayDurationExpression;
	std::vector<EventAssignment*>* eventAssignmentsVec;
	bool triggerValue;	

	Event() {
		this->bUseValuesAtTriggerTime = false;
		this->triggerExpression = nullptr;
		this->triggerValue = false;
		this->delayDurationExpression = nullptr;
		this->eventAssignmentsVec = new std::vector<EventAssignment*>;
	}
	~Event() {
		delete this->triggerExpression;
		delete this->delayDurationExpression;
		for (const EventAssignment* elem : *this->eventAssignmentsVec) delete elem;
		this->eventAssignmentsVec->clear();
		delete this->eventAssignmentsVec;
	}

	[[nodiscard]] bool hasDelay() const {
		return this->delayDurationExpression != nullptr;
	}

	void bind(SymbolTable* symbolTable) const {
		this->triggerExpression->bindExpression(symbolTable);
		if (this->delayDurationExpression != nullptr) {
			this->delayDurationExpression->bindExpression(symbolTable);
		}
		for (const EventAssignment* elem : *this->eventAssignmentsVec) {
			elem->bind(symbolTable);
		}
	}
};

struct EventExecution {
	realtype exeTime;
	Event* event0;
	double* targetValues;

	EventExecution(Event* e) {
		this->exeTime = RCONST(0.0);
		this->event0 = e;
		this->targetValues = nullptr;
	}
	~EventExecution() {
		delete this->targetValues;
	}
};

struct OdeDiscontinuity {
	std::string discontinuitySymbol;
	VCell::Expression* discontinuityExpression;
	VCell::Expression* rootFindingExpression;

	~OdeDiscontinuity() {
		delete this->discontinuityExpression;
		delete this->rootFindingExpression;
	}
};

class VCellSundialsSolver {
public:
	VCellSundialsSolver();	
	virtual ~VCellSundialsSolver();

	void readInput(std::istream& inputstream);
	virtual void solve(double* paramValues=nullptr, bool bPrintProgress=false, FILE* outputFile=nullptr, void (*checkStopRequested)(double, long)=nullptr) = 0;
	OdeResultSet* getResultSet() { return this->odeResultSet; }
	[[nodiscard]] int getNumEquations() const { return this->NEQ; }
	VCell::Expression** getInitialConditionExpressions() { return this->initialConditionExpressions; }
	void setStartingTime(realtype newStartingTime) { this->STARTING_TIME = newStartingTime; }
	void setEndingTime(realtype newEndingTime) { this->ENDING_TIME = newEndingTime; }
	//void setOutputTimes(int count, double* newOutputTimes);
	SymbolTable* getSymbolTable() { return this->defaultSymbolTable;}

	static void checkStopRequested(double, long);

protected:	
	// 0 : t
	// 1 ~ N : variable values
	// N+1 ~ N+NPARAM : parameter values 
	// N+NPARAM+1 ~ N+NPARAM+numDiscontinuites : discontinuity values
	realtype* values; 
	// 0 ~ N-1 : equations
	VCell::Expression** initialConditionExpressions;
	SymbolTable* initialConditionSymbolTable;
	OdeResultSet* odeResultSet;		// mainly for parameter optimization use but it also stores column names

	void* solver;	// the memory for solver
	std::string recoverableErrMsg;

	int NEQ;
	int NPARAM;
	realtype STARTING_TIME;
	realtype ENDING_TIME;
	realtype RelativeTolerance;
	realtype AbsoluteTolerance;
	long keepEvery;
	double maxTimeStep;		
	std::vector<double> outputTimes;
	double* tempRowData; // data for current time to be written to output file and to be added to odeResultSet

	int numDiscontinuities;
	OdeDiscontinuity** odeDiscontinuities;
	SymbolTable* discontinuitySymbolTable;
	double* discontinuityValues;
	int* rootsFound;
	
	std::string* paramNames;
	std::string* variableNames; // variables
	std::string* allSymbols;
	int numAllSymbols;
	SymbolTable* defaultSymbolTable;

	N_Vector y;	
	void writeData(double currTime, FILE* outputFile);
	virtual void updateTempRowData(double currTime);
	void writeFileData(FILE* outputFile);
	void writeFileHeader(FILE* outputFile);
	void printProgress(double currTime, double& lastPercentile, clock_t& lastTime, double increment, FILE* outputFile) const;

	void readDiscontinuities(std::istream& inputstream);
	virtual void readEquations(std::istream& inputstream) = 0;
	virtual void initialize();

	void initDiscontinuities();
	bool updateDiscontinuities(realtype t, bool bOnRootReturn);
	void checkDiscontinuityConsistency() const;

	void solveInitialDiscontinuities(double t);
	virtual bool fixInitialDiscontinuities(double t)=0;

	void printVariableValues(realtype t);
	void printDiscontinuityValues();
	virtual void updateTandVariableValues(realtype t, N_Vector y)=0;

	int RootFn(realtype t, N_Vector y, realtype *gout);
	virtual std::string getSolverName()=0;

	static VCell::Expression* readExpression(std::istream& inputstream);
	bool executeEvents(realtype realTimeVar);
	double getNextEventTime();

private:
	Event** events;
	int numEvents;

	void readEvents(std::istream& inputstream);
	void testEventTriggers(realtype Time);
	std::list<EventExecution*> eventExeList;
};

#endif
