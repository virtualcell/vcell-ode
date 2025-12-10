project(IDAWin)

set (SRC_FILES
		SundialsSolverInterface.cpp
		OdeResultSet.cpp
		StoppedByUserException.cpp
		VCellCVodeSolver.cpp
		VCellIDASolver.cpp
		VCellSundialsSolver.cpp
		SteadyStateDriver.cpp
		VCellSolverFactory.cpp
		VCellSolverInput.cpp
)
set (HEADER_FILES 
		OdeResultSet.h
		StoppedByUserException.h
		VCellSolverFactory.h
		VCellSolver.h
		VCellSolverInput.h
		VCellSundialsSolver.h
		VCellCVodeSolver.h
		VCellIDASolver.h
        SteadyStateDriver.h
		SundialsSolverInterface.h
)

set (EXE_SRC_FILES
	SundialsSolverStandalone.cpp
)

add_library(IDAWin STATIC ${SRC_FILES} ${HEADER_FILES})
target_link_libraries(IDAWin sundials ExpressionParser vcellmessaging argparse spdlog::spdlog) # if MinGW is enabled, add `$<$<BOOL:${MINGW}>:ws2_32>`
set(EXE_FILE SundialsSolverStandalone)
if (ARCH_64bit) 
	set(EXE_FILE ${EXE_FILE}_x64)
endif()

target_include_directories(IDAWin PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
add_executable(${EXE_FILE} ${EXE_SRC_FILES})	
target_link_libraries(${EXE_FILE} IDAWin)

install(TARGETS ${EXE_FILE} RUNTIME DESTINATION ${OPTION_EXE_DIRECTORY})
install(TARGETS IDAWin ARCHIVE DESTINATION bin)