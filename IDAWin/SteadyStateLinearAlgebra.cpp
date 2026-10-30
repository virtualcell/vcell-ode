#include "SteadyStateLinearAlgebra.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <iomanip>
#include <limits>
#include <numeric>
#include <numbers>

namespace {
	constexpr double EPSILON = std::numeric_limits<double>::epsilon();
	constexpr double NOT_A_NUMBER = std::numeric_limits<double>::quiet_NaN();

	void rotateColumns(double* a, double* b, const std::size_t length, const double c, const double s) {
		for (std::size_t k = 0; k < length; k++) {
			const double ak = a[k];
			const double bk = b[k];
			a[k] = c * ak - s * bk;
			b[k] = s * ak + c * bk;
		}
	}

	/// Similarity-transform rows/columns by powers of 2 so their norms are comparable (EISPACK balanc).
	void balance(std::vector<double>& a, const std::size_t n) {
		constexpr double RADIX = 2.0;
		constexpr double SQUARED_RADIX = RADIX * RADIX;
		bool done = false;
		while (!done) {
			done = true;
			for (std::size_t i = 0; i < n; i++) {
				double r = 0.0, c = 0.0;
				for (std::size_t j = 0; j < n; j++) {
					if (j == i) continue;
					c += std::fabs(a[j * n + i]);
					r += std::fabs(a[i * n + j]);
				}
				if (c == 0.0 || r == 0.0) continue;
				double g = r / RADIX;
				double f = 1.0;
				const double s = c + r;
				while (c < g) { f *= RADIX; c *= SQUARED_RADIX; }
				g = r * RADIX;
				while (c > g) { f /= RADIX; c /= SQUARED_RADIX; }
				if ((c + r) / f < 0.95 * s) {
					done = false;
					g = 1.0 / f;
					for (std::size_t j = 0; j < n; j++) a[i * n + j] *= g;
					for (std::size_t j = 0; j < n; j++) a[j * n + i] *= f;
				}
			}
		}
	}

	/// Reduce to upper Hessenberg form by stabilized elementary similarity transforms (EISPACK elmhes).
	void reduceToHessenberg(std::vector<double>& a, const std::size_t n) {
		for (std::size_t m = 1; m + 1 < n; m++) {
			double x = 0.0;
			std::size_t pivot = m;
			for (std::size_t j = m; j < n; j++) {
				if (std::fabs(a[j * n + m - 1]) > std::fabs(x)) {
					x = a[j * n + m - 1];
					pivot = j;
				}
			}
			if (pivot != m) {
				for (std::size_t j = m - 1; j < n; j++) std::swap(a[pivot * n + j], a[m * n + j]);
				for (std::size_t j = 0; j < n; j++) std::swap(a[j * n + pivot], a[j * n + m]);
			}
			if (x == 0.0) continue;
			for (std::size_t i = m + 1; i < n; i++) {
				double y = a[i * n + m - 1];
				if (y == 0.0) continue;
				y /= x;
				a[i * n + m - 1] = y;
				for (std::size_t j = m; j < n; j++) a[i * n + j] -= y * a[m * n + j];
				for (std::size_t j = 0; j < n; j++) a[j * n + m] += y * a[j * n + i];
			}
		}
		// clear the multipliers left below the subdiagonal
		for (std::size_t i = 2; i < n; i++)
			for (std::size_t j = 0; j + 1 < i; j++) a[i * n + j] = 0.0;
	}

	/// Eigenvalues of an upper Hessenberg matrix by the Francis double-shift QR algorithm (EISPACK hqr).
	bool hessenbergQR(std::vector<double>& h, const int n, std::vector<double>& wr, std::vector<double>& wi) {
		// 1-based accessor to keep the indexing identical to the published algorithm
		auto a = [&h, n](const int i, const int j) -> double& { return h[(i - 1) * n + (j - 1)]; };
		auto sign = [](const double magnitude, const double s) { return s >= 0.0 ? std::fabs(magnitude) : -std::fabs(magnitude); };

		double anorm = 0.0;
		for (int i = 1; i <= n; i++)
			for (int j = std::max(i - 1, 1); j <= n; j++) anorm += std::fabs(a(i, j));

		int nn = n;
		double t = 0.0;
		double p = 0.0, q = 0.0, r = 0.0, s, w, x, y, z;
		while (nn >= 1) {
			int its = 0;
			int l;
			do {
				for (l = nn; l >= 2; l--) {
					s = std::fabs(a(l - 1, l - 1)) + std::fabs(a(l, l));
					if (s == 0.0) s = anorm;
					if (std::fabs(a(l, l - 1)) + s == s) {
						a(l, l - 1) = 0.0;
						break;
					}
				}
				x = a(nn, nn);
				if (l == nn) { // one root found
					wr[nn - 1] = x + t;
					wi[nn - 1] = 0.0;
					nn--;
				} else {
					y = a(nn - 1, nn - 1);
					w = a(nn, nn - 1) * a(nn - 1, nn);
					if (l == nn - 1) { // two roots found
						p = 0.5 * (y - x);
						q = p * p + w;
						z = std::sqrt(std::fabs(q));
						x += t;
						if (q >= 0.0) { // real pair
							z = p + sign(z, p);
							wr[nn - 2] = wr[nn - 1] = x + z;
							if (z != 0.0) wr[nn - 1] = x - w / z;
							wi[nn - 2] = wi[nn - 1] = 0.0;
						} else { // complex pair
							wr[nn - 2] = wr[nn - 1] = x + p;
							wi[nn - 2] = z;
							wi[nn - 1] = -z;
						}
						nn -= 2;
					} else { // no roots found; continue iteration
						if (its == 30) return false;
						if (its == 10 || its == 20) { // exceptional shift
							t += x;
							for (int i = 1; i <= nn; i++) a(i, i) -= x;
							s = std::fabs(a(nn, nn - 1)) + std::fabs(a(nn - 1, nn - 2));
							y = x = 0.75 * s;
							w = -0.4375 * s * s;
						}
						++its;
						int m;
						for (m = nn - 2; m >= l; m--) { // form shift and look for two consecutive small subdiagonal elements
							z = a(m, m);
							r = x - z;
							s = y - z;
							p = (r * s - w) / a(m + 1, m) + a(m, m + 1);
							q = a(m + 1, m + 1) - z - r - s;
							r = a(m + 2, m + 1);
							s = std::fabs(p) + std::fabs(q) + std::fabs(r);
							p /= s;
							q /= s;
							r /= s;
							if (m == l) break;
							const double u = std::fabs(a(m, m - 1)) * (std::fabs(q) + std::fabs(r));
							const double v = std::fabs(p) * (std::fabs(a(m - 1, m - 1)) + std::fabs(z) + std::fabs(a(m + 1, m + 1)));
							if (u + v == v) break;
						}
						for (int i = m + 2; i <= nn; i++) {
							a(i, i - 2) = 0.0;
							if (i != m + 2) a(i, i - 3) = 0.0;
						}
						for (int k = m; k <= nn - 1; k++) { // double QR step on rows l..nn and columns m..nn
							if (k != m) {
								p = a(k, k - 1);
								q = a(k + 1, k - 1);
								r = 0.0;
								if (k != nn - 1) r = a(k + 2, k - 1);
								if ((x = std::fabs(p) + std::fabs(q) + std::fabs(r)) != 0.0) {
									p /= x;
									q /= x;
									r /= x;
								}
							}
							if ((s = sign(std::sqrt(p * p + q * q + r * r), p)) != 0.0) {
								if (k == m) {
									if (l != m) a(k, k - 1) = -a(k, k - 1);
								} else {
									a(k, k - 1) = -s * x;
								}
								p += s;
								x = p / s;
								y = q / s;
								z = r / s;
								q /= p;
								r /= p;
								for (int j = k; j <= nn; j++) { // row modification
									p = a(k, j) + q * a(k + 1, j);
									if (k != nn - 1) {
										p += r * a(k + 2, j);
										a(k + 2, j) -= p * z;
									}
									a(k + 1, j) -= p * y;
									a(k, j) -= p * x;
								}
								const int mmin = std::min(nn, k + 3);
								for (int i = l; i <= mmin; i++) { // column modification
									p = x * a(i, k) + y * a(i, k + 1);
									if (k != nn - 1) {
										p += z * a(i, k + 2);
										a(i, k + 2) -= p * r;
									}
									a(i, k + 1) -= p * q;
									a(i, k) -= p;
								}
							}
						}
					}
				}
			} while (l < nn - 1);
		}
		return true;
	}
}

namespace SteadyStateLinearAlgebra {
	std::size_t leastSquaresSolve(const std::vector<double>& A, const std::size_t rows, const std::size_t cols,
								  const std::vector<double>& b, std::vector<double>& x) {
		x.assign(cols, NOT_A_NUMBER);
		if (std::ranges::any_of(A, [](const double a) { return std::isnan(a); })) return 0;

		// Column-major: the columns of U are rotated until mutually orthogonal (U = A V), V accumulates rotations
		std::vector<double> U(rows * cols);
		std::vector<double> V(cols * cols, 0.0);
		for (std::size_t r = 0; r < rows; r++)
			for (std::size_t c = 0; c < cols; c++) U[c * rows + r] = A[r * cols + c];
		for (std::size_t c = 0; c < cols; c++) V[c * cols + c] = 1.0;

		for (int sweep = 0; sweep < 100; sweep++) {
			bool rotated = false;
			for (std::size_t p = 0; p + 1 < cols; p++) {
				for (std::size_t q = p + 1; q < cols; q++) {
					double* up = &U[p * rows];
					double* uq = &U[q * rows];
					double alpha = 0.0, beta = 0.0, gamma = 0.0;
					for (std::size_t k = 0; k < rows; k++) {
						alpha += up[k] * up[k];
						beta += uq[k] * uq[k];
						gamma += up[k] * uq[k];
					}
					if (gamma == 0.0 || std::fabs(gamma) <= EPSILON * std::sqrt(alpha * beta)) continue;
					rotated = true;
					const double zeta = (beta - alpha) / (2.0 * gamma);
					const double t = std::copysign(1.0, zeta) / (std::fabs(zeta) + std::sqrt(1.0 + zeta * zeta));
					const double c = 1.0 / std::sqrt(1.0 + t * t);
					rotateColumns(up, uq, rows, c, c * t);
					rotateColumns(&V[p * cols], &V[q * cols], cols, c, c * t);
				}
			}
			if (!rotated) break;
		}

		// x = sum_j v_j (u_j . b) / sigma_j, with u_j = U_j / sigma_j
		std::vector<double> sigma(cols);
		for (std::size_t j = 0; j < cols; j++) {
			double norm = 0.0;
			for (std::size_t k = 0; k < rows; k++) norm += U[j * rows + k] * U[j * rows + k];
			sigma[j] = std::sqrt(norm);
		}
		const double threshold = cols == 0 ? 0.0 : 100.0 * EPSILON * *std::ranges::max_element(sigma);
		std::size_t rank = 0;
		x.assign(cols, 0.0);
		for (std::size_t j = 0; j < cols; j++) {
			if (sigma[j] == 0.0 || sigma[j] <= threshold) continue;
			rank++;
			double projection = 0.0;
			for (std::size_t k = 0; k < rows; k++) projection += U[j * rows + k] * b[k];
			const double coefficient = projection / (sigma[j] * sigma[j]);
			for (std::size_t i = 0; i < cols; i++) x[i] += coefficient * V[j * cols + i];
		}
		return rank;
	}

	bool eigenvalues(std::vector<double> A, const std::size_t n, std::vector<double>& realParts, std::vector<double>& imaginaryParts) {
		realParts.assign(n, NOT_A_NUMBER);
		imaginaryParts.assign(n, NOT_A_NUMBER);
		if (std::ranges::any_of(A, [](const double a) { return !std::isfinite(a); })) return false;
		balance(A, n);
		reduceToHessenberg(A, n);
		return hessenbergQR(A, static_cast<int>(n), realParts, imaginaryParts);
	}

	StabilityAnalysis::StabilityAnalysis(std::vector<double> realPartsIn, std::vector<double> imaginaryPartsIn, const double resolutionIn)
		: resolution(resolutionIn) {
		const std::size_t n = realPartsIn.size();
		if (n == 0) return;

		// sort by real part, descending
		std::vector<std::size_t> order(n);
		std::iota(order.begin(), order.end(), 0);
		std::ranges::stable_sort(order, [&realPartsIn](const std::size_t i, const std::size_t j) { return realPartsIn[i] > realPartsIn[j]; });
		for (const std::size_t i : order) {
			realParts.push_back(realPartsIn[i]);
			imaginaryParts.push_back(imaginaryPartsIn[i]);
		}
		auto& R = realParts;
		auto& I = imaginaryParts;

		maxRealPart = R[0];
		maxRealOfComplex = R[n - 1] - 1;
		maxImaginaryPart = std::fabs(I[0]);
		imaginaryOfMaxComplex = 0.0;
		for (std::size_t i = 0; i < n; i++) {
			maxRealPart = std::max(maxRealPart, R[i]);
			maxImaginaryPart = std::max(maxImaginaryPart, std::fabs(I[i]));
			if (std::fabs(I[i]) > resolution && R[i] > maxRealOfComplex) {
				maxRealOfComplex = R[i];
				imaginaryOfMaxComplex = std::fabs(I[i]);
			}
			if (std::fabs(R[i]) > resolution) {
				if (R[i] >= resolution) numPositiveReal++;
				if (R[i] <= -resolution) numNegativeReal++;
				if (std::fabs(I[i]) > resolution) {
					numComplex++;
				} else {
					I[i] = 0.0;
					numReal++;
				}
			} else {
				R[i] = 0.0;
				if (std::fabs(I[i]) > resolution) {
					numImaginary++;
				} else {
					I[i] = 0.0;
					numZero++;
				}
			}
		}
		if (imaginaryOfMaxComplex == 0) maxRealOfComplex = R[n - 1];

		// indices of the largest and smallest absolute real parts
		std::size_t mx, mn;
		if (numPositiveReal > 0) {
			mx = (R[0] > std::fabs(R[n - 1])) ? 0 : n - 1;
			if (numPositiveReal == n) mn = numPositiveReal - 1;
			else if (R[numPositiveReal - 1] < std::fabs(R[numPositiveReal])) mn = numPositiveReal - 1;
			else mn = numPositiveReal;
		} else {
			mx = n - 1;
			mn = 0;
		}
		stiffness = std::fabs(R[mx]) / std::fabs(R[mn]);

		const double maxt = std::fabs(1 / R[mn]);
		double tott = maxt;
		double distt = 0.0;
		for (std::size_t i = 1; i < n; i++) { // COPASI starts at 1, so the first eigenvalue only counts as mn
			if (i == mn) continue;
			distt += maxt - std::fabs(1 / R[i]);
			tott += std::fabs(1 / R[i]);
		}
		timeHierarchy = distt / tott / static_cast<double>(n - 1);

		// heuristic, eigenvalue-based oscillation indicator
		if (n < 2) oscillationIndicator = 0;
		else if (std::fabs(I[0]) > resolution)
			oscillationIndicator = R[0] * (R[0] > 0 ? std::fabs(I[0]) / (0.01 + std::fabs(I[0])) : 2 - std::fabs(I[0]) / (0.01 + std::fabs(I[0])));
		else
			oscillationIndicator = 2 * R[1] - (R[0] > 0 ? 2 * R[0] : 0);

		// bifurcation test functions (Kuznetsov; and the "Bifurcation discovery tool", Chickarmane et al.)
		std::complex<double> product = 1.0;
		std::size_t indexMin = 0;
		double minAbsReal = 1e300;
		for (std::size_t i = 0; i < n; i++) {
			product *= std::complex<double>(R[i], I[i]);
			if (std::fabs(R[i]) < minAbsReal) {
				minAbsReal = std::fabs(R[i]);
				indexMin = i;
			}
		}
		foldBifurcation = std::abs(product);

		product = 1.0;
		for (std::size_t i = 0; i + 1 < n; i++) product *= std::complex<double>(R[i], I[i]) + std::complex<double>(R[i + 1], I[i + 1]);
		hopfBifurcation = product.real();

		product = 1.0;
		for (std::size_t i = 0; i < n; i++)
			if (i != indexMin) product *= std::complex<double>(R[i], I[i]);
		foldBifurcationBDT = foldBifurcation / (1 - 0.99 * std::exp(-std::abs(product)));

		double realProduct = 1.0;
		for (std::size_t i = 0; i < n; i++) realProduct *= R[i] / (1 - 0.99 * std::exp(-I[i]));
		hopfBifurcationBDT = realProduct;
	}

	std::ostream& operator<<(std::ostream& os, const StabilityAnalysis& analysis) {
		const auto& A = analysis;
		os << "\nKINETIC STABILITY ANALYSIS\n";
		os << "The linear stability analysis based on the eigenvalues\n";
		os << "of the Jacobian matrix is only valid for steady states.\n\n";
		os << "Summary:\nThis state ";
		if (A.maxRealPart > A.resolution) os << "is unstable";
		else if (A.maxRealPart < -A.resolution) os << "is asymptotically stable";
		else os << "'s stability is undetermined";
		if (A.maxImaginaryPart > A.resolution) os << ",\ntransient states in its vicinity have oscillatory components";
		os << ".\n\n";

		os << "Eigenvalue statistics:\n";
		os << " Largest real part: " << std::setprecision(6) << A.maxRealPart << "\n";
		os << " Largest absolute imaginary part:  " << std::setprecision(6) << A.maxImaginaryPart << "\n";
		if (A.imaginaryOfMaxComplex > A.resolution)
			os << " The complex eigenvalues with the largest real part are:  " << A.maxRealOfComplex << " +|- " << A.imaginaryOfMaxComplex << "i\n";
		os << " " << A.numReal << " are purely real\n";
		os << " " << A.numImaginary << " are purely imaginary\n";
		os << " " << A.numComplex << " are complex\n";
		os << " " << A.numZero << " are equal to zero\n";
		os << " " << A.numPositiveReal << " have positive real part\n";
		os << " " << A.numNegativeReal << " have negative real part\n";
		os << " stiffness = " << A.stiffness << "\n";
		os << " time hierarchy = " << A.timeHierarchy << "\n\n";
		os << "Fold bifurcation test functions (standard, bifurcation discovery tool): " << A.foldBifurcation << ", " << A.foldBifurcationBDT << "\n";
		os << "Hopf bifurcation test functions (standard, bifurcation discovery tool): " << A.hopfBifurcation << ", " << A.hopfBifurcationBDT << "\n\n";
		os << "Oscillation indicator: " << A.oscillationIndicator << "\n";
		return os;
	}
}
