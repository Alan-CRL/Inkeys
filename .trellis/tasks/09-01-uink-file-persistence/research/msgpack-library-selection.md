# MessagePack 库选型调查

调查日期：2026-09-01

## 1. Repository Context

- 项目使用 root `vcpkg.json` manifest 和固定 builtin baseline。
- `Directory.Build.props` / `Directory.Build.targets` 已为 ARM64 配置 manifest install，triplet 为 `arm64-windows-static-v143`，安装目录位于仓库的 `VcpkgInstalled/Arm64`。
- 当前 manifest 没有 MessagePack 依赖。
- 固定 baseline 可用端口版本：
  - `msgpack`：7.0.0，官方 msgpack-cxx，header-only；
  - `msgpack-c`：6.1.0，C API；
  - `msgpack11`：0.0.10#4，较老的独立 C++ 实现。

上游 msgpack-c/msgpack-cxx 当前已有更新版本。用户已允许在确有必要时升级 vcpkg baseline，但版本新旧不应压过仓库可复现构建；首选仍是先验证当前 7.0.0，只有必要能力、已修复缺陷或构建兼容证据才触发升级。

## 2. UInk-Specific Selection Criteria

库必须支持或允许我们可靠实现：

1. 连续 MessagePack 顶层对象逐个解码，并获得每个对象消费的准确字节数；
2. writer 明确选择 uint16/uint32/uint64/int32/float32/float64，而不是自动最小编码；
3. Header UUID 强制 str8，而不是 fixstr；
4. 遍历 Map 原始 key/value entries，发现重复已知键；
5. 在分配前/解码中限制深度、字符串、Array、Map 和总对象大小；
6. 对截断和非法字节区分“需要更多数据”与“格式错误”；
7. 第三方异常、zone/对象生命周期和 buffer ownership 能封装在 `.cpp` 内；
8. ARM64 static v143、C++20 modules、现有 vcpkg manifest 下可复现构建。
9. 不静态导入 Windows 8+ API，保持 Windows 7 SP1 + KB2670838 启动兼容。

## 3. Candidates

### 3.1 `msgpack` / msgpack-cxx 7.0.0

优点：

- 官方项目的 C++ 实现，与 MessagePack 格式演进保持一致；
- header-only，不增加单独静态库 ABI 组合；
- `msgpack::packer` 提供显式宽度 pack 操作，可手工写 Header 和所有字段；
- 解码对象的 Map 表示为 entry 数组而非强制 unique-key dictionary，能够在项目校验层发现重复键；
- 支持单对象 unpack、流式 unpacker、对象消费位置和 unpack limits，适合连续对象流；
- 当前 vcpkg baseline 已有 ARM64 可用端口，不需要 overlay 或手工 vendoring。

注意点：

- 默认自适应 `pack(value)` 不能用于需要精确 wire width 的字段，必须封装显式 pack helper。
- 库的 generic `object::convert` 会把协议校验隐藏在类型转换中，也不适合 duplicate-key/field-path 诊断；应手工遍历 object tree。
- zone 持有 object 引用，不能把 `msgpack::object` 暴露到长期 UInk model；每个顶层对象验证后复制到项目类型。
- limits 只能作为第一道防线；还需项目级累计文件、对象、点和诊断预算。
- 第三方 header 不导出到公共 `.cppm` API，避免 module BMI 和调用方耦合。

### 3.2 `msgpack-c` 6.1.0

优点：稳定 C API、低层控制明确、可避免 C++ template/module 交互。

缺点：需要手工管理 unpacker/zone/错误状态和 C 到项目类型的转换；C++20 项目中样板和生命周期风险更高，而精确编码、流式读取等能力 msgpack-cxx 已提供。本任务没有 C ABI 或跨语言嵌入需求。

结论：可作为 msgpack-cxx 在 ARM64/module 环境出现不可解决问题时的后备，不是首选。

### 3.3 `msgpack11` 0.0.10#4

优点：API 小、C++ 使用简单。

缺点：端口和上游较老；更偏向通用值树和简洁序列化，UInk 对精确数值宽度、str8、连续流恢复、duplicate known key 和限额的要求需要更多绕行；端口还引入不必要的构建/测试依赖。使用它不会减少本任务最难的协议工作。

结论：不选。

### 3.4 Existing JSON Libraries Or Hand-Written MessagePack

仓库已有 JSON 库不等于适合 MessagePack 协议。通用 JSON-to-MessagePack 转换通常丢失整数宽度、float32/float64、Map 重复键和对象边界信息。手写完整 MessagePack parser 也超出任务必要范围并扩大安全审计面。

结论：不选。

## 4. Recommendation

在 root `vcpkg.json` 增加：

```json
"msgpack"
```

先使用仓库 baseline 锁定的 msgpack-cxx `7.0.0`，不启用可选 Boost feature。若 build spike 证明版本缺少任务必需能力、包含影响正确性的已知缺陷或无法在当前 module 工程使用，可以升级 baseline；升级必须记录全部端口变化并经过完整构建、测试和 Win7 import 审计。

选择理由不是“API 最短”，而是它能让项目在成熟 MessagePack framing/primitive 实现之上，集中精力实现 UInk 特有的精确编码、字段校验、恢复状态机和文件事务。

## 5. Planned Wrapper

第三方库只出现在 `uink_codec.cpp` 的 private implementation。建议封装：

```text
ExactPacker
  packUInt16 / packUInt32 / packUInt64
  packInt32
  packFloat32 / packFloat64
  packStr8
  packArrayHeader / packMapHeader

ObjectReader
  readExactUInt*/Int32/Float*
  readString/Bool/Array/Map
  forEachMapEntryPreservingDuplicates
  fieldPath + diagnostic context

StreamDecoder
  decodeOne(bytes, offset, limits)
  -> Complete(endOffset) | NeedMoreData | Malformed | LimitExceeded
```

公共 module API 只暴露项目自有 `std::span<std::byte>`、model、result、diagnostic 和 limits 类型。

## 6. Required Build Spike Before Full Implementation

在正式铺开 codec 前，用一个小测试验证以下事实并把输出字节固定为断言：

- 小值 `0` 通过显式 uint16 仍输出 `0xcd 00 00`，而不是 fixint；
- Header 的 version、counts、time 宽度正确；
- 36-byte UUID 使用 str8 prefix，而不是 fixstr；
- float32 与 float64 明确区分；
- 一个 Map 中两个相同 string key 在 object view 中都可见；
- 两个连续顶层对象能逐个解码并得到准确 end offset；
- 截断对象报告 need-more-data，非法 opcode/结构报告 malformed；
- depth/container/string limits 在大分配前生效；
- include/import 方式在 `Debug|ARM64` module scan 下无问题。
- 最终目标不新增无 fallback 的 Win8+ 静态 imports。

若其中任何关键能力不成立，再评估 C API 后备；不要先写协议层再发现第三方抽象吞掉必要信息。

## 7. ZIP Deferred

`.uink.extra` 需要 ZIP，但用户已经确认它不属于首版，因此本任务不增加 ZIP 依赖。后续资源包任务再单独按以下条件比较：

- 能只读中央目录而不落盘解压；
- 支持逐 entry 流式读取和中途停止；
- 能在读取前取得声明压缩/解压大小，但允许项目按实际输出再次限额；
- 能创建 deterministic-enough ZIP 并替换条目；
- 支持 ARM64 static v143；
- API 不鼓励自动路径解压。

资源安全由项目层负责，不能把 ZIP 库的默认行为当作路径、Unicode、压缩比或 MIME 安全保证。

## 8. Primary References

- msgpack-c/msgpack-cxx official repository: `https://github.com/msgpack/msgpack-c`
- official releases: `https://github.com/msgpack/msgpack-c/releases`
- repository-pinned vcpkg port metadata under the configured vcpkg builtin registry

实现阶段以 baseline 实际安装的 7.0.0 headers 为 API 权威，不照抄新版本文档中可能尚未存在的接口。
