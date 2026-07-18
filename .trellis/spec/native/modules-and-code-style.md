# Modules and Code Style

## C++20 Module Shape

`draw3` 的每个核心模块使用相同的两文件结构：

- `<name>.cppm`：导出模块、公开 enum/struct/class 和简短中文职责注释。
- `<name>.cpp`：`module draw3.<name>;` 实现；只在实现需要时 `import` 其他模块。

代表文件：

- `draw3/renderer.cppm` 与 `draw3/renderer.cpp`
- `draw3/ink_prediction.cppm` 与 `draw3/ink_prediction.cpp`
- `draw3/transparent_presentation.cppm` 与 `draw3/transparent_presentation.cpp`

接口和实现均先使用 global module fragment：

```cpp
module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

export module draw3.example; // 实现文件改为 module draw3.example;
```

Windows/标准库头放在模块声明前；模块依赖用 `import draw3.xxx;`，不要在一个 `draw3` 模块中包含另一个模块的实现文件。

## Public Versus Private Code

- 导出符号放在 `export namespace draw3`。
- 仅实现使用的 helper、常量和小类型放在 `.cpp` 的匿名 namespace。
- `TransparentPresentationController` 用 `Impl` 隐藏 presenter 细节；其他模块当前直接公开数据结构，不要在无任务要求时统一重构成 PImpl。
- `InkRenderer` 公开的 D3D device/context、纹理、view 和操作层是当前实现暴露，不是稳定公共 API。现有依赖可以维持，但没有专门架构任务时禁止新增或扩大直接资源依赖。
- `main.cpp` 只做应用级初始化、消息编排和顶层失败返回；单笔循环属于 `DrawingController`。

## Naming And Formatting

当前自研核心代码遵循：

- 类型、enum 和公开方法：`PascalCase`。
- 局部变量和参数：`camelCase`。
- 成员字段：尾部下划线，如 `window_`、`configuration_`。
- 常量：`kPascalCase`。
- C++ 文件使用 tab 缩进，Allman 风格大括号。
- 简单 guard 可单行 `if (...) return ...;`；复杂失败路径使用完整块。
- 作用域使用 RAII：COM 接口使用 `Microsoft::WRL::ComPtr`，动态实现对象使用 `std::unique_ptr`。

不要以 `additional/`、`HiEasyX/` 或 `ResTest/` 的第三方/参考风格覆盖 `draw3` 的本地风格。

## Third-Party Patch Boundary

`additional/`、`HiEasyX/`、`lib/` 以及同类路径默认属于第三方或外部来源：

- 原则上不直接修改。
- 必须修改时，使用与自研功能分离的补丁或变更批次。
- 补丁说明至少记录：修改原因、上游项目与版本/来源、本地差异、回合上游计划和验证结果。
- 未确认精确上游版本时标记为“待验证”，不要猜测版本或许可证关系。

`ResTest/` 是参考工程而不是第三方实现模板；复用代码前仍要单独核对许可证、当前 `draw3` 契约和平台兼容要求。

## Comments

- 对模块职责、跨线程发布、GPU 绑定、回退原因、数学退化和不直观的顺序要求写简短中文注释。
- 注释解释“为什么”和必须保持的语义，不逐行翻译代码。
- 新公开函数在 `.cppm` 中提供一句职责说明。
- CPU/GPU 布局使用 `static_assert` 记录硬契约，示例见 `InkPoint`、`HighlighterPrimitive`、`GlobalShaderConstants`。

## Encoding And Line Endings

当前代表性 `.cpp/.cppm/.hlsl/.hlsli/.vcxproj` 均为 UTF-8 BOM + CRLF。修改这些文件必须保持该编码和换行。

Markdown 与 `.trellis/` 文档当前为 UTF-8 无 BOM + LF，也应保持原格式。
