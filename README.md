<p align="center" width="100%">
 <a href="https://vcell.org">
    <img width="10%" src="https://github.com/biosimulations/biosimulations/blob/dev/docs/src/assets/images/about/partners/vcell.svg">
 </a>
</p>

## The Virtual Cell Project
The Virtual Cell is a modeling and simulation framework for computational biology.  For details see http://vcell.org and http://github.com/virtualcell.

---
# vcell-ode
![CI](https://github.com/virtualcell/vcell-ode/actions/workflows/cd.yml/badge.svg)

Virtual Cell ODE [virtualcell/vcell-ode](https://github.com/virtualcell/vcell-ode) is a collection of numerical 
simulation libraries and protocols used to process ODEs in the Virtual Cell framework [virtualcell/vcell](https://github.com/virtualcell/vcell)).

## Building VCell ODE with Conan on Windows

Windows builds require a native compiler. MinGW is not supported. Install Visual Studio
Build Tools with the C++ workload and Windows SDK, plus Python 3.10 or newer. Conan 2,
CMake 3.16 or newer, and Ninja 1.12 or newer are required; Conan installs CMake and
Ninja as build requirements, so they do not need to be installed separately.

From a Visual Studio Developer PowerShell, install Conan and create a detected host
profile:

```powershell
py -m pip install --upgrade conan
$pythonScripts = py -c "import sysconfig; print(sysconfig.get_path('scripts'))"
$env:Path = "$pythonScripts;$env:Path"
conan profile detect --force
```

If PowerShell still cannot find `conan`, run the following once, then open a new
PowerShell window:

```powershell
$pythonScripts = py -c "import sysconfig; print(sysconfig.get_path('scripts'))"
$userPath = [Environment]::GetEnvironmentVariable('Path', 'User')
[Environment]::SetEnvironmentVariable('Path', "$userPath;$pythonScripts", 'User')
```

Build the solver without messaging:

```powershell
conan install . --build=missing -o "&:include_messaging=False" -s compiler.cppstd=20
conan build . -s compiler.cppstd=20
```

The recipe invokes CMake and Ninja, and produces the executable and DLL under
`build/bin`. To use the checked-in LLVM/Clang-CL profile instead, replace the install
command with:

```powershell
conan install . `
  --profile:host conan-profiles/CI-CD/Windows-AMD64_profile.txt `
  --profile:build default `
  --build=missing -o "&:include_messaging=False" -s:h compiler.cppstd=20
conan build . -s compiler.cppstd=20
```

The checked-in profile requires LLVM/Clang 21 (`clang-cl`) on `PATH` and Visual Studio
2022 Build Tools with the v143 toolset. The auto-detected MSVC profile is recommended
when using the compiler supplied by Visual Studio. The checked-in profile is configured
for Visual Studio 2022; it does not target Visual Studio 18.

Messaging is disabled automatically for Windows Conan builds. It remains enabled by
default on Linux and macOS, where the libcurl dependency is supported. The
`include_messaging` option can be used to disable it on those platforms when needed.
