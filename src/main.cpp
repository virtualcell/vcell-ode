#include <pybind11/pybind11.h>
#include <SundialsSolverInterface.h>

#define STRINGIFY(x) #x
#define MACRO_STRINGIFY(x) STRINGIFY(x)

namespace py = pybind11;

PYBIND11_MODULE(_core, m) {
    m.doc() = R"pbdoc(
        VCell CVODE / IDA solver
        -------------------------

        .. currentmodule:: pyvcell_odesolver

        .. autosummary::
           :toctree: _generate

           version
           solve
    )pbdoc";

    m.def("version", &version, R"pbdoc(
        version of build

        version string of build using git hash
    )pbdoc");

    m.def("solve", &solve, R"pbdoc(
        solve the ODE

        The inputFilename expects a .cvodeInput file, and an output directory to put the results into
    )pbdoc",
        py::arg("cvode_input_file_path"), py::arg("output_file_path"), py::arg("tid") = -1);

#ifdef VERSION_INFO
    m.attr("__version__") = MACRO_STRINGIFY(VERSION_INFO);
#else
    m.attr("__version__") = "dev";
#endif
}
