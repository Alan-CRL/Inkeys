# OpenCV vcpkg 接入设计

## Manifest

`vcpkg.json` 是唯一依赖声明入口。`opencv4` 依赖关闭默认 features，只显式开启 DirectShow、MSMF、SIMD intrinsics 和 OpenCV 自身线程支持。override 使用 port 元数据的 `version` 方案锁定 `4.10.0#3`，baseline 保持不变。

## Build Integration

`Directory.Build.props/targets` 已将根 manifest 和三套静态 triplet 接入所有 VC++ 项目，并在 module 扫描前执行 manifest 安装。因此不增加工程属性、手工 include/lib 路径或额外链接标识。

官方 port 在静态 triplet 下会删除其 package `bin` 目录。本阶段不使用 `BUILD_LIST`，所以安装目录可能包含四个目标模块之外的静态库，但没有 OpenCV 符号引用时不会将其对象带入 EXE。

## Documentation

沿用已有第三方组件表格/条目，只添加库名、官网、Apache License 2.0 和版权信息。不在依赖接入阶段声称图形识别、摄像头或扫描功能已实现。

## Rollback

若 manifest 无法在 ARM64 解析或构建，先根据 vcpkg 日志核对 `4.10.0#3` 与 feature 支持；不通过升级 baseline、打补丁 port、开启默认 features 或改为动态链接规避失败。
