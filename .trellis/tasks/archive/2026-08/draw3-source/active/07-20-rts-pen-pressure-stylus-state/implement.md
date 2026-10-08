# RTS 笔压与笔姿态支持实施计划

## 1. 输入契约与 RTS 解码

- [x] 扩展 `ContactSnapshot`/`ContactRecord` 的 tilt、orientation 发布与读取，并更新并发快照测试。
- [x] 为 RTS metadata 增加可选属性 descriptor、压力归一化和角度转换 helper；保留慢路径缓存与热路径固定成本。
- [x] 扩展 desired packet description，并在扩展请求失败时回退 X/Y。
- [x] 增加 `DRAW3_TESTING` 下的纯转换测试入口，覆盖压力范围和两套角度来源。

## 2. 配置、模型和宽度

- [x] 新增设备宽度枚举/设置和默认值，扩展内部 resolved 宽度模式。
- [x] 为 `DrawingController` 增加线程安全 Set/Get；Down 锁定新笔画模式，非普通笔保持固定宽度。
- [x] 将 pressure/tilt/orientation 写入 Down/Move/Up 模型输入，并保留每字段最后有效值。
- [x] 实现 1–7px 硬件压力映射、短点击半径和预测冻结最后真实半径；保留现有 taper/半径约束。
- [x] 增加模式矩阵、运行时设置、硬件压力曲线、缺失回退和预测尾宽测试。

## 3. Validation

- [x] 运行 `git diff --check`，检查 UTF-8 BOM + CRLF 和修改范围。
- [x] 使用本机 ARM64 MSBuild 完整 Rebuild `inkStrokeModelerTest.sln`：`Debug|ARM64`；两个 Shader、资源链、主程序和测试工程成功。
- [x] 运行 ARM64 `inkStrokeModelerTestTests.exe`；contact/压感/姿态/宽度模式与荧光笔测试全部通过。
- [x] 使用 MPP2.0、Mouse、Touch 人工验证 prediction、Up、resize 与运行时模式切换，窗口测试无异常。
- [ ] 检查 D3D Debug Layer；此项会启动窗口，按用户要求必须先提醒并取得确认。
- [x] 运行 `trellis-check`，修正角度上限、属性分支可读性并更新 native 运行时规范。

## Risk and rollback points

- RTS 属性单位/方向转换是最高风险点；转换必须独立测试，设备诊断只在慢路径输出。
- `ContactRecord` 增加原子字段会改变 cache-line 占用；必须确认无锁断言和对象池测试继续通过。
- 硬件压力不得改变既有模拟/固定模式；若视觉回归，先回滚 mode resolution/estimator 分支，不动 RTS 数据传输。
