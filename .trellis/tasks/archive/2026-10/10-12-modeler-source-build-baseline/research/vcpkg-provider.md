# vcpkg provider decision（已验收PASS）

2026-10-12用户授权可行且维护方便时切vcpkg20250512.1#0；已停止手工lib作为产品路线。复用现有manifest/MSBuild/static-v143 triplet，避免维护构建脚本、merge过程和三份产物分发。

## Version and port
- baseline保持99a97de2cb371449d4fb9dc970f2ac562d689ec2；其默认Abseil较新，必须追加精确override20250512.1（省略#0即port-version0）。其他依赖不升级。
- 精确历史port tree2999911d2655e693394770d9dd7600b277a86f37，已用GitHub只读API核对全部4文件。port不强制C++17，开启ABSL_PROPAGATE_CXX_STD，static CRT时ABSL_MSVC_STATIC_RUNTIME=ON；关闭tests/helpers。
- 两个port patch：MinGW DLL补系统库（MSVC静态不生效），string-view.patch强制std string_view；目标C++20本就采用std别名，不改Modeler算法。
- 三triplet仅PORT=abseil追加CMAKE_CXX_STANDARD20/REQUIRED/EXTENSIONS OFF。其余port实际配置不变，但triplet文件hash参与所有包ABI缓存，首次可重新编译原固定版本依赖。

## Integration and ABI
三消费者统一VcpkgConfiguration=Release，避免Demo Debug UseDebugLibraries=true自动选择MTd。现有MSBuild targets根据VcpkgInstalledDir+VcpkgTriplet得出_ZVcpkgCurrentInstalledDir并自动链接lib/*.lib；共享props以该installed include优先，再放canonical Modeler目录。
旧vendor.0头和merge保留；active package需同版本头与库。实际patch assert、std string_view/ordering、iterator0、lts_20250512、CRT与库symbols三架构验证通过。
Modeler13TU继续/O1 /Ob1 /MT C++20 NDEBUG /fp:precise /EHsc /GR。Abseil采用vcpkg Release /O2 /Oi /Gy /DNDEBUG /Z7，与旧MinSizeRel不同，必须精确回放和性能验收。

## Acceptance
旧四配置EXE/report已经冻结；依赖版本metadata仅允许明确的.0→.1，模型全部语义bit exact仍必须通过。30轮交替paired性能无超噪声可重复退化。三架构/主solution/headless/shader/link/imports/旧头库hash检查均PASS，阶段1基线门已满足；后续尾段算法仍须单独实施/验收。
三架构provider/全部Rebuild/精确回放/各30paired性能/Headless及静态来源检查已通过；其他依赖保持原版本。证据归Build/modeler-baseline/vcpkg-abseil、vcpkg-provider、source-vcpkg、main-vcpkg与benchmarks；最终验收见verification.md。

## Primary references
- [vcpkg精确overrides与#0](https://learn.microsoft.com/en-us/vcpkg/users/versioning)
- [per-port triplet / CMake configure options](https://learn.microsoft.com/en-us/vcpkg/users/triplets)
- [MSBuild自动include/link与Release选择](https://learn.microsoft.com/en-us/vcpkg/users/buildsystems/msbuild-integration)
- [Abseil兼容合同（patch也不保证ABI）](https://abseil.io/about/compatibility)
- [Abseil20250512.1](https://github.com/abseil/abseil-cpp/releases/tag/20250512.1)
