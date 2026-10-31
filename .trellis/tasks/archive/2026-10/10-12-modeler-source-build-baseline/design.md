# Design: vcpkg Abseil + canonical Modeler source

## Change Boundary
构建归属从固定merge改为本地Modeler13传统TU + vcpkg Abseil20250512.1#0。保持模型数学代码/参数/调用方几何不变，复用现有solution和依赖系统。
必要修改：manifest/三个triplet、canonical 13cc及共享MSBuild清单/filters、三个project与两个pragma、无窗口回放/计时入口和严格比较工具、README/规范。原vendor .0头和merge不删除，供独立对照和整套回退。

## Dependency Provider
根vcpkg.json追加abseil和精确override，保持baseline99a97de2cb371449d4fb9dc970f2ac562d689ec2及其他版本。
历史port tree2999911d2655e693394770d9dd7600b277a86f37，对应20250512.1#0。实际MSVC静态port设置ABSL_MSVC_STATIC_RUNTIME=ON，传播C++标准并关闭tests/helpers；现有static-v143 triplet给PORT=abseil追加CMAKE_CXX_STANDARD=20、REQUIRED=ON、EXTENSIONS=OFF。
直接使用vcpkg安装的匹配头/多个静态库，不手工合并/复制lib，也不自定义overlay port。MSBuild统一VcpkgConfiguration=Release，确保Demo Debug的UseDebugLibraries不会选MTd。
active vcpkg include置于canonical additional之前；通过patch版本assert、实际include链和options探针验证，不能让保留的absl.0遮蔽.1。
Abseil标准Release /O2 /Oi /Gy /DNDEBUG /Z7与旧MinSizeRel不同，数值/性能门不能省略。修改triplet可能使其他固定版本包cache ABI hash改变/首次重建，不代表升级版本或改其实际编译flags。
原独立构建及旧来源记录保留为历史证据，standalone-abseil-build.md已被vcpkg-provider.md替代。

## Source Integration
13cc从用户legacy只读复制，保留bytes/license与numbers.h兼容，排除type_matchers.cc。
共享清单只给13项设置Runtime marker，传统CPP、PCH/模块扫描关闭，/O1 /Ob1 /MT NDEBUG undef _DEBUG C++20 iterator0，/EHsc /GR /fp:precise，禁用RTC/GL/JMC。运行期obj目录更新逐项用marker限制，避免业务cpp/cppm同名覆盖。
产品/demo/tests统一canonical Modeler + active vcpkg Abseil头来源；不链接旧merge，不增加永久backend开关。

## Baseline and Validation
四配置旧程序/obj/report已在切换前保存于Build/modeler-baseline/legacy，fixture保持冻结；新程序使用独立输出。
--modeler-replay与--modeler-benchmark在wmain最早返回，不初始化GUI/GPU。报告包含输入、参数、状态、时间与全部Result浮点bits。唯一允许的非语义差异是声明的Abseil版本20250512.0→20250512.1，比较结果必须披露双方版本。
30轮成对计时交替启动新旧程序，预热后比较Move/Predict/Up/Reset分布、噪声和重复趋势；超噪声可重复退化失败。
原生ARM64 MSBuild完整solution Rebuild、>=5min timeout，同PowerShell规范化唯一Path并禁node reuse。补三架构Release/demo/tests；主Headless /MTd不消费Abseil，不改配置。记录shader链、link map真实符号来源、COFF ABI和x86/x64静态imports。GUI/D3D/实体笔/Win7真机不执行。
