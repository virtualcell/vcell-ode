#ifndef STEADYSTATELINEARALGEBRA_H
#define STEADYSTATELINEARALGEBRA_H

#include <cstddef>
#include <ostream>
#include <vector>

/**
 * Dense linear algebra for the steady-state solver, standing in for the LAPACK routines COPASI uses
 * (DGELSY for least squares, DGEES for eigenvalues). Matrices are row-major std::vector<double>.
 */
namespace SteadyStateLinearAlgebra {
	/**
	 * Minimum-norm least-squares solution of A x = b (A is rows x cols). One-sided Jacobi SVD; singular values
	 * at or below 100 * epsilon * sigma_max count as zero. Returns the effective rank; 0 (and x all NaN) if A
	 * contains NaN.
	 */
	std::size_t leastSquaresSolve(const std::vector<double>& A, std::size_t rows, std::size_t cols,
								  const std::vector<double>& b, std::vector<double>& x);

	/**
	 * Eigenvalues of the general real n x n matrix A: balancing, reduction to Hessenberg form, then the
	 * shifted QR algorithm. Complex conjugate pairs are returned adjacently. Returns false if A is not finite or
	 * the QR iteration did not converge.
	 */
	bool eigenvalues(std::vector<double> A, std::size_t n, std::vector<double>& realParts, std::vector<double>& imaginaryParts);

	/**
	 * Port of COPASI's CEigen::stabilityAnalysis and its report: eigenvalue statistics of the Jacobian at a
	 * steady state, with |values| <= resolution treated as zero.
	 */
	struct StabilityAnalysis {
		std::vector<double> realParts;      // sorted by real part, descending
		std::vector<double> imaginaryParts;
		double resolution = 0.0;
		double maxRealPart = 0.0;
		double maxImaginaryPart = 0.0;
		double maxRealOfComplex = 0.0;
		double imaginaryOfMaxComplex = 0.0;
		std::size_t numReal = 0, numImaginary = 0, numComplex = 0, numZero = 0, numPositiveReal = 0, numNegativeReal = 0;
		double stiffness = 0.0;
		double timeHierarchy = 0.0;
		double oscillationIndicator = 0.0;
		double foldBifurcation = 0.0, foldBifurcationBDT = 0.0;
		double hopfBifurcation = 0.0, hopfBifurcationBDT = 0.0;

		StabilityAnalysis(std::vector<double> realParts, std::vector<double> imaginaryParts, double resolution);
	};

	std::ostream& operator<<(std::ostream& os, const StabilityAnalysis& analysis);
}

#endif //STEADYSTATELINEARALGEBRA_H
