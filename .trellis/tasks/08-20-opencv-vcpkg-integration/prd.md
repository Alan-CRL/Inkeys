# OpenCV vcpkg 静态依赖接入

## Goal

在不引入 OpenCV 业务源码的前提下，将 `opencv4 4.10.0#3` 作为精简、静态的 vcpkg manifest 依赖接入 Inkeys，补齐中英文第三方声明，并通过完整 Solution 构建。

## Confirmed Facts

- 根 manifest baseline 与 `Vcpkg` 子模块均为 `99a97de2cb371449d4fb9dc970f2ac562d689ec2`。
- 该 baseline 默认提供 `opencv4 4.12.0#3`，版本库仍包含 `4.10.0#3`，其版本方案字段为 `version`。
- x86、x64 和 ARM64 现有 triplet 均使用静态 CRT、静态库和 v143，无需修改。
- MSBuild manifest 目标会在 C++ module 扫描前自动安装依赖，不需要 `vcpkg integrate install`。

## Requirements

- R1：在 `vcpkg.json` 添加 `opencv4`，`default-features` 为 `false`，只启用 `dshow`、`msmf`、`intrinsics`、`thread`。
- R2：保持 builtin baseline 不变，使用 `"version": "4.10.0"` 与 `"port-version": 3` 固定 OpenCV。
- R3：不启用默认 features、world、FFmpeg、DNN、GUI、图像编解码依赖、TBB、OpenMP 或其他额外 feature。
- R4：在中英文 README、双语 TOS 和 NOTICE 中按现有组件格式增加 OpenCV、Apache License 2.0 和 `Copyright (C) 2000-2026 OpenCV contributors`。
- R5：复用 `ThirdpartyLicenses/Apache License 2.0`，不新建 OpenCV 许可证文件。
- R6：使用 ARM64 主机版 MSBuild 构建 `InkeysRepo.sln` 的 `Debug|ARM64`，不单独构建 `Inkeys.vcxproj`。

## Acceptance Criteria

- [x] `vcpkg.json` 可解析，OpenCV 解析为 `4.10.0#3` 且直接 feature 集合为 `dshow, intrinsics, msmf, thread`。
- [x] 三套 triplet 未被修改，且仍为静态 CRT/静态库/v143。
- [x] 五份对外文档均有与现有列表一致的 OpenCV 声明，没有声称尚未实现的产品功能。
- [x] `Debug|ARM64` 完整 Solution 构建成功。
- [x] 安装/产物中没有 `opencv_world.dll`、FFmpeg DLL 或 OpenCV 插件 DLL。
- [x] `git diff --check`、变更范围、编码和 CRLF 检查通过。

## Out of Scope

- 不创建 `InkeysCV`、ShapeRecognizer、CameraCapture、DocumentScanner 或 ImageProcessor 源码。
- 不修改 `Inkeys.vcxproj`、Windows 版本宏、自定义 triplet、`Vcpkg/` port 或 overlay-port。
- 不运行主程序、摄像头、Windows 7/8 真机或任何可见窗口测试。
