//
// Created by Logan Drescher on 11/12/25.
//
#ifndef VCELL_ODE_NUMERICS_SUNDIALSSOLVERINTERFACE_H
#define VCELL_ODE_NUMERICS_SUNDIALSSOLVERINTERFACE_H

#include <fstream>

void activateSolver(std::ifstream& inputFileStream, FILE* outputFile, int taskID);

#endif //VCELL_ODE_NUMERICS_SUNDIALSSOLVERINTERFACE_H