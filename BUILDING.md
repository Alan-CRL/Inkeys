# Building Inkeys

The `InkeysRepo.sln` solution contains two main projects:

- `Inkeys.vcxproj`: the Inkeys main program (C++)
- `PptCOM.csproj`: the PowerPoint integration module (C#, .NET Framework 4.0)

---

## Build the main project `Inkeys.vcxproj`

### Environment
- Visual Studio 2022/2026  
> Select the `Desktop development with C++` and `Windows application development` workloads  
- MSVC v143 - VS 2022 C++ Build Tools (v14.44 or newer)  
- Windows 11 SDK (10.0.26100)  

### Code settings
- Unicode
- C++20

### Important
**Place the repository under a pure English path that contains no spaces.**  

### Steps
1. Clone the repository

```cmd
git clone "https://github.com/Alan-CRL/Inkeys.git"
cd "Inkeys"
```

2. Initialize the submodules (`Vcpkg` and `Inkeys/additional/HiMsg`)

```cmd
git submodule update --init --recursive
```

3. Bootstrap vcpkg

```cmd
.\Vcpkg\bootstrap-vcpkg.bat
```
`vcpkg integrate install` is **not** required. The project uses vcpkg manifest mode, and dependencies are installed automatically during the build.  

4. Open `InkeysRepo.sln` in Visual Studio 2022/2026
5. Select the `Inkeys` project
6. Switch to the `Release | Win32` configuration (change the platform to `x64` or `ARM64` as needed)
7. Click `Build -> Build Inkeys`

---

### FAQ
Some environments may fail to build.  
- **error LNK2001: unresolved external symbol `std_search_1`**  
VS 2022 17.8.x (MSVC v143.35 - v143.41) fails to build the project.  

**17.14.x** is recommended, matching the version used by the project developer (optional).  

---

## Build the auxiliary project `PptCOM.csproj`

Skip this section if you only change the main program. Building PptCOM alone does not need the submodules or vcpkg.

### Environment
- Visual Studio 2022/2026

> Select the `.NET desktop development` workload

- .NET Framework 4.0

### Steps
1. Restore the NuGet packages (PowerPoint / Office interop assemblies, restored to `Package/` in the repository root). Visual Studio restores them automatically when building; from the command line, run:

```cmd
nuget restore PptCOM\PptCOM.csproj -PackagesDirectory Package
```

2. Select the `PptCOM` project
3. Switch to `Release | Any CPU`
4. Click `Build -> Build PptCOM`