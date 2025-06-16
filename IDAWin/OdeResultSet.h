#ifndef ODERESULTSET_H
#define ODERESULTSET_H

#include <string>
#include <vector>

namespace VCell {
	class Expression;
}
class SymbolTable;

struct Column {
	std::string name;
	VCell::Expression* expression;
	
	Column(std::string arg_name, VCell::Expression* exp) {
		name = arg_name;
		expression = exp;
	}
};

class OdeResultSet
{
public:
	OdeResultSet();
	~OdeResultSet();
	void addColumn(const std::string& aColumn);
	void addFunctionColumn(const std::string& aColumn, const std::string& columnExpression);
	void addRow(double* aRow);
	void setColumnWeights(double* weights);
	
	void bindFunctionExpression(SymbolTable* symbolTable);

	double* getRowData(int index);
	const double* getRowData() {
		return rowData;
	}

	int findColumn(const std::string& aColumn);
	double getColumnWeight(int index);
	std::string& getColumnName(int index);
	void getColumnData(int index, int numParams, double* paramValues, double* colData);

	int getNumColumns();
	int getNumRows();
	int getNumFunctionColumns() { return numFunctionColumns; }
	int getNumDataColumns() { return numDataColumns; }
	std::vector<Column> getColumns(){ return columns;}

	VCell::Expression* getColumnFunctionExpression(int columnIndex);
	void clearData();

	void copyInto(OdeResultSet* otherOdeResultSet);
	void addEmptyRows(int numRowsToAdd);

private:
	// 0 : t
	// 1 ~ N : variable names;
	std::vector<Column> columns;
	double* columnWeights;
	double* rowData;
	int numRowsAllocated;
	int numRowsUsed;
	int numFunctionColumns;
	int numDataColumns;
};

#endif
