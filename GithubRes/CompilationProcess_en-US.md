# Build Process

For general build requirements, you only need to build `Inkeys.vcxproj`. There is an associated project, `PptCOM.csproj`, which serves as the PPT integration module for 智绘教Inkeys.
The repository maintains prebuilt `PptCOM.dll` and `PptCOM.tlb` in `Inkeys/`. The projects have no project dependency, so you can build `Inkeys.vcxproj` directly with only the C++ desktop environment. After PptCOM source or interface changes, the maintainer manually updates both files from the same build as described below.

### Build the main project `Inkeys.vcxproj`

智绘教Inkeys is fully open source; all source code and resources are publicly available.

#### Environment requirements
- Visual Studio 2022/2026  
  > Select the `Desktop development with C++` and `Windows application development` workloads  
- MSVC v143 - VS 2022 C++ Build Tools (v14.44 or newer)  
- Windows 11 SDK (10.0.26100)

#### Code settings
- Unicode
- C++20

#### Important note
**The repository directory must be located under a pure English path, and the path must not contain spaces.**

#### Build steps
1. Clone the repository

```cmd
git clone "https://github.com/Alan-CRL/Inkeys.git"
cd "Inkeys"
```

2. Initialize submodules

```cmd
git submodule update --init --recursive
```

3. Bootstrap vcpkg

```cmd
.\vcpkg\bootstrap-vcpkg.bat
```

**You do NOT need** `vcpkg integrate install`.

4. Open `InkeysRepo.sln` with Visual Studio 2022/2026

> When building Inkeys with Visual Studio 2026, make sure to keep the MSVC v143 toolset, and do not upgrade to MSVC v145.

5. Select the `Inkeys` project
6. Switch to the `Release | Win32` configuration (adjust the target platform as needed, e.g., `x64` and `Arm64`)
7. Click `Build -> Build Inkeys`

> When you build a platform for the first time, vcpkg will automatically download/build dependencies, which may take some time. Subsequent builds will reuse the cache.

You can also build the native project directly; output remains under the repository root at `Build/<Platform>/<Configuration>`:

```cmd
msbuild Inkeys\Inkeys.vcxproj /m /p:Configuration=Release /p:Platform=Win32 /p:PreferredToolArchitecture=x64
```

`PreferredToolArchitecture` selects the host tools without changing the target architecture. ARM64 machines default to native host tools. The embedded UI3 Presentation Harness defaults to off in Release and on in Debug; use `/p:EnableUi3PresentationHarness=true` for an explicit test build. Both standalone test projects remain in the solution but are excluded from the three default Release builds.

---

### FAQ

In some special environments, the build may fail.

* **error LNK2001: unresolved external symbol `std_search_1`**
  This can happen in VS 2022 17.8.x (MSVC v143.35 to v143.41).

Recommended: **17.14.x**, to match the version used by the project developer (optional).

---

### Build the auxiliary project `PptCOM.csproj`

#### Environment requirements

* Visual Studio 2022/2026

  > Select the `.NET desktop development` workload
* .NET Framework 4.0 SDK or later (4.x)

#### Build steps

1. Select the `PptCOM` project
2. Switch to `Release | AnyCPU`
3. Click `Build -> Build PptCOM`, or run `msbuild PptCOM\PptCOM.csproj /p:Configuration=Release /p:Platform=AnyCPU`. PptCOM alone does not require submodules or vcpkg.
4. The DLL and matching TLB are generated in `PptCOM/bin/Release/`. They are no longer copied automatically. After changing PptCOM source/interfaces, the maintainer manually copies both files to `Inkeys/` before building the native project. Both bundled files remain tracked by Git; the RC and `#import` paths stay unchanged.
