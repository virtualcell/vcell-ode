import ctypes
import platform
from _ctypes import byref
from importlib.resources import files

import pyvcell_odesolver
from pyvcell_odesolver._internal import check_arch


class VCellNativeLibraryLoader:
    lib: ctypes.CDLL

    def __init__(self) -> None:
        self.lib = self._load_library()
        self._define_entry_points()

    def _load_library(self) -> ctypes.CDLL:
        system = platform.system()
        lib_ext = {"Linux": ".so", "Darwin": ".dylib", "Windows": ".dll"}.get(system)
        version_identifier = "VCell ODE solver (CVODE/IDA) version"

        if lib_ext is None:
            raise OSError(f"Unsupported operating system: {system}")

        libs_dir = files(pyvcell_odesolver).joinpath("lib")
        if not libs_dir.is_dir():
            raise OSError(f"Could not find the shared library directory {libs_dir}")


        valid_libraries = check_arch.get_library_archs(libs_dir)
        for file in valid_libraries:
            print(f"Found shared library: {file}")
            lib = ctypes.CDLL(name=str(file))
            try:
                lib.version_ctypes.restype = ctypes.c_char_p # signals ctypes to auto-convert to python-str
                version_str: str = lib.version_ctypes()
                if version_identifier not in version_str:
                    continue
            except AttributeError as e:
                continue # If we didn't get a version, we didn't get the correct lib
            return lib

        raise OSError("Could not find the shared library")

    def _define_entry_points(self) -> None:
        self.lib.version_ctypes.restype = ctypes.c_char_p # technically, we did this above, but just in case
        self.lib.version_ctypes.argtypes = []

        self.lib.solve_ctypes.restype = ctypes.c_int
        self.lib.solve_ctypes.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_int]