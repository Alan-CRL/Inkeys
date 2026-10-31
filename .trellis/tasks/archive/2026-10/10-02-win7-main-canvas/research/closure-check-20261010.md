# Win7 主画布修复：结项独立检查（2026-10-10）

状态：**PASS，可以按用户本轮明确授权完成归档与记录 commit；不 push。** 本分工已是 trellis-check，无再次派发；本次仅审核收尾记录，未修改产品实现、旧报告或原日志，没有重编译、运行新测试或GUI。所有权仅本报告。

## 范围与验收

核对 check/implement.jsonl 当前新增引用、PRD、design、implement、task.json、closure-20261010.md及本轮 spec/真实现场记录。PRD五项主画布AC均有现有证据支持；PPT项保持未勾选并明确本轮未新增实测、不记PASS。用户本轮明确批准按主画布不出墨/工具光标不可见修复范围结项，故未新增PPT专项、偶发输入和未复现荧光现象不是此次归档阻断；这些后续边界没有被抹去。

产品修复commit是12d82884cf09928c2ae22eda2fe8851ac8581c9b。Draw3产品目录与IdtMain.cpp本轮git diff为空；本次文档不会引入新默认策略、shader、交换链或输入行为。已有共享策略仅实际OS6.1+实际WARP自动选择28980byte/SHA2171DDA6E709A3F4CD444E6281AB64635C3F969CD71722662B04DA0DD8055384的完整候选，Hardware等路径保持原资源。完整数学/薄线/AA/光影/工具/双源输出保留合同与此前正式质量门一致。

## 验证证据的闭合

- 冻结check-formal-warp-20261009.md和quality-formal-warp-20261009.md重新SHA核对保持，分别为B0FE340E…7D342、E300EC87…4DE03A。其完整ARM64 Debug/x64 Release、Headless、420GPU/108策略/72有效比较以及额外真实WinPS/50mock质量门有效；本轮没有冒称重新构建。
- 本分工上一轮已独立核全部42真实现场文件及315唯一GPU终态，54完整候选PASS、42有效参考delta0/12不可用、108策略PASS、145份限频ULW全部成功/fullOverride0。closure中的数字与独立数据一致，不重复逐项矩阵。
- Hardware真实VMware SVGA3D FL11_0与WARP分开；虚拟GPU不冒称物理直通。completed-with-failures保留原版WARP故障对照，不当修复候选FAIL；不可用Hardware或坏reference不当PASS。
- 新EXE SHA2CDE02ED…108D8与交付包一致；旧manifest仍如实说明构建时未提交workspace，不改写成纯12d82884构建。用户确认直接双击EXE两后端均正常，作为正常默认启动的人工验收；CMD0/0/1日志没有伪装成auto运行日志。
- 早前Aero开/关人工观察已记录选择穿透及定格切换/返回，本次正常启动和真实硬件补齐主要恢复验收。ULW像素与API成功仍不单独替代光学观察，也不承诺全部Win7补丁/驱动兼容。

## Findings (fixed)

发现closure曾写“本次和上一轮均未复现”荧光定格异常，与早前232746一次变深观察不一致。已通知root作为owner精确修为：“早前232746曾有一次定格后变深，随后重复未复现；本次未复现，不宣称已定位或修复。”本分工再次只读确认该句存在；旧现场报告保持不变。未修改其他owner文件。

## Findings (not fixed)

保留旧WARP内部唯一opcode/JIT未证明、激光横向受限的具体上游未唯一归因、荧光定格未定位及未新增PPT专项实测。closure明确这些边界，不猜测改代码，不伪造后续PASS；按用户批准的结项范围没有阻断。原mouse消息X=0已排除该现象由shader/ULW制造，但不能唯一归因VMware。

## Verification

- Lint/静态：限定git diff --check退出0；task JSON及check/implement JSONL解析、引用存在性、五项主AC与明确PPT边界检查通过。本报告UTF-8无BOM/纯CRLF。
- TypeCheck/构建：本轮不适用，无源码改变；复核已有两架构完整Solution质量门，不重复执行。
- Tests：本轮不运行新的产品测试；沿用已冻结正式质量与上一轮真实Win7独立验收，记录一致。
- 归档/commit由root执行；当前检查时task仍in_progress、commit字段为产品修复12d82884，archive后由原生task工具更新completed。root需将现有JSONL的当前task自路径调整为archive路径，并保持历史包/原报告不回写。journal须隔离其他任务Session54；检查者没有归档、提交或推送。
