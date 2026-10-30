//
// Steady-state (CSSS) solver tests. Baselines are the analytic steady states of each model.
//
#include <algorithm>
#include <complex>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <gtest/gtest.h>

#include "SundialsSolverInterface.h"
#include "SteadyStateLinearAlgebra.h"

void compare(const std::filesystem::path& file1, const std::filesystem::path& file2, float tolerance); // smoke_test.cpp

static int runSteadyState(const std::string& modelName, const std::filesystem::path& output) {
	const std::filesystem::path input{std::filesystem::path{RESOURCE_DIR} / (modelName + ".cvodeInput")};
	std::ifstream inputFileStream{input};
	if (!inputFileStream.is_open()) throw std::runtime_error("input file [" + input.string() + "] doesn't exist!");
	FILE* outputFile = fopen(output.string().c_str(), "w");
	if (outputFile == nullptr) throw std::runtime_error("Could not open output file [" + output.string() + "] for writing.");
	const int returnCode = activateSolver(inputFileStream, outputFile, -1);
	fclose(outputFile);
	return returnCode;
}

static void performSteadyStateTest(const std::string& modelName) {
	const std::filesystem::path output{std::filesystem::temp_directory_path() / (modelName + ".ida")};
	EXPECT_EQ(0, runSteadyState(modelName, output));
	compare(output, std::filesystem::path{RESOURCE_DIR} / (modelName + ".ida.expected"), 1e-7);
}

TEST(SteadyStateTest, NewtonFromInitialCondition) {
	performSteadyStateTest("steady_state_newton");
}

TEST(SteadyStateTest, ForwardIntegrationFallback) {
	performSteadyStateTest("steady_state_integration");
}

TEST(SteadyStateTest, EventsFireDuringIntegration) {
	performSteadyStateTest("steady_state_events");
}

TEST(SteadyStateTest, NoSteadyStateIsAnError) {
	const std::filesystem::path output{std::filesystem::temp_directory_path() / "steady_state_none.ida"};
	EXPECT_NE(0, runSteadyState("steady_state_none", output));
}

TEST(SteadyStateLinearAlgebraTest, Eigenvalues) {
	using Complex = std::complex<double>;
	auto sorted = [](const std::vector<double>& re, const std::vector<double>& im) {
		std::vector<Complex> values;
		for (std::size_t i = 0; i < re.size(); i++) values.emplace_back(re[i], im[i]);
		std::ranges::sort(values, [](const Complex& a, const Complex& b) { return a.real() != b.real() ? a.real() < b.real() : a.imag() < b.imag(); });
		return values;
	};
	std::vector<double> re, im;

	// Brusselator Jacobian at its steady state (1, 1): -1/2 +- i sqrt(3)/2
	ASSERT_TRUE(SteadyStateLinearAlgebra::eigenvalues({0.0, 1.0, -1.0, -1.0}, 2, re, im));
	auto values = sorted(re, im);
	EXPECT_NEAR(values[0].real(), -0.5, 1e-14);
	EXPECT_NEAR(values[0].imag(), -std::sqrt(3.0) / 2, 1e-14);
	EXPECT_NEAR(values[1].imag(), std::sqrt(3.0) / 2, 1e-14);

	// Second-difference matrix: 2cos(k pi / 4) - 2 for k = 1..3
	ASSERT_TRUE(SteadyStateLinearAlgebra::eigenvalues({-2, 1, 0, 1, -2, 1, 0, 1, -2}, 3, re, im));
	values = sorted(re, im);
	EXPECT_NEAR(values[0].real(), -2 - std::sqrt(2.0), 1e-13);
	EXPECT_NEAR(values[1].real(), -2, 1e-13);
	EXPECT_NEAR(values[2].real(), -2 + std::sqrt(2.0), 1e-13);
	for (const Complex& v : values) EXPECT_EQ(v.imag(), 0.0);

	EXPECT_FALSE(SteadyStateLinearAlgebra::eigenvalues({std::nan(""), 0, 0, 1}, 2, re, im));
}

TEST(SteadyStateLinearAlgebraTest, LeastSquares) {
	std::vector<double> x;
	// Full rank: exact solve
	EXPECT_EQ(2u, SteadyStateLinearAlgebra::leastSquaresSolve({2, 1, 1, 3}, 2, 2, {3, 5}, x));
	EXPECT_NEAR(x[0], 0.8, 1e-14);
	EXPECT_NEAR(x[1], 1.4, 1e-14);
	// Rank 1 (a conservation law): minimum-norm solution of -h1 + 2 h2 = -1
	EXPECT_EQ(1u, SteadyStateLinearAlgebra::leastSquaresSolve({-1, 2, 1, -2}, 2, 2, {-1, 1}, x));
	EXPECT_NEAR(x[0], 0.2, 1e-14);
	EXPECT_NEAR(x[1], -0.4, 1e-14);
}

TEST(SteadyStateLinearAlgebraTest, StabilityAnalysis) {
	const SteadyStateLinearAlgebra::StabilityAnalysis focus({-0.5, -0.5}, {0.8660254037844386, -0.8660254037844386}, 1e-9);
	EXPECT_EQ(2u, focus.numComplex);
	EXPECT_EQ(2u, focus.numNegativeReal);
	EXPECT_DOUBLE_EQ(-0.5, focus.maxRealPart);

	const SteadyStateLinearAlgebra::StabilityAnalysis saddle({-1.0, 2.0, 1e-12}, {0.0, 0.0, 0.0}, 1e-9);
	EXPECT_EQ(1u, saddle.numPositiveReal);
	EXPECT_EQ(1u, saddle.numNegativeReal);
	EXPECT_EQ(1u, saddle.numZero);
	EXPECT_DOUBLE_EQ(2.0, saddle.realParts[0]); // sorted descending
}
