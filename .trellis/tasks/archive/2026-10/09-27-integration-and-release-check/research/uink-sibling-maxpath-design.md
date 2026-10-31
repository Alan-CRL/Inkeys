# UInk内部sibling名称导致合法目标越MAX_PATH（设计待独立审）

Active task: .trellis/tasks/09-27-integration-and-release-check

2026-10-01 Root依据实际生产错误与原Win32边界取证；未修改产品/manifest/registry/最低Win7。C3 Desktop sandbox AccessDenied5已同PE outside连续保存/strict fresh读回归runner环境，不改ReplaceFile flags。

## 证据与分类

- 主生产Inkeys.vcxproj直接编译共享 `inkStrokeModelerTest/draw3/uink_file.cpp(.m)`，不是将demo算法外推主产品。SaveUInkFile真实WriteFailed/system_error3/diagnostic26，PPT最终path230 WCHAR，UniqueSiblingPath追加41后temp271，当前Inkeys与Codex runtime pwsh manifest无longPathAware。outside同PE仍复现，parent/files目录确实存在。
- Root真实Win32 CreateFileW同GENERIC_READ|WRITE/share0/CREATE_NEW/NORMAL|SEQUENTIAL_SCAN：普通229成功，普通271且父存在失败3，对照extended271成功，短missing-parent229失败3。该独立root在TestResults/release-hardening/uink-path-boundary-8a3a5b885c174579bf0654cc975944a2/result.json，actual pwsh host已记录；没有设置longPathAware/LongPathsEnabled。首脚本UInt32类型错误没有调用API，旧root2d8da… result为无效证据保留；修正decimal access后exit0。
- 根因确认于本轮内部长度：合法目标末名被再次拼入temp/backup/recovery，内部附加可越系统限制。不证明所有长路径错误3都同原因、不承诺任意>260最终路径支持。

## 最小技术选择A / 合同

仅改UniqueSiblingPath：从已规范化绝对target取现有同父目录，候选leaf使用独立GUID + 原固定suffix(.tmp/.bak/.new-recovery)，不再重复target末名。保持同卷/同目录原ReplaceFile/Move atomic事务、32次GUID冲突检查、CREATE_NEW、原access/locks/strong sourceRevision/SHA、既有status/recoveryPath/cleanup错误语义。各候选自己的实际路径会返回result.recoveryPath，消费者不应猜字符串追加规则。

不缩夹具目录冒产品修复，不改Win7SP1+KB2670838/长路径manifest/registry/API基线；不全仓加extended前缀或变namedMutex/path identity。普通仍短且同目录原flags与文件归属，最终过长/父目录本身过长按原错误失败。

## 验收/RED→GREEN

1. 独立设计审查后唯一writer拥有共享uink_file.cpp与现uink_tests.cpp（必要实际NoGUI入口识别先向Root给精确注册方案，工程Root独占）。
2. RED先只新增真实SaveUInkFile230→strict Read、SaveExisting update→strict Read/sourceRevision/同guid内容与最后有效点，原UniqueSibling不改；使用create-new独立root/outside runner，不能测试里复制正确文件算法。
3. 真实save目标合法普通path<MAX，父存在，临时确>MAX；assert明确WriteFailed/error3及读取未被新file伪造。改变Sibling生成后，同测试真正Committed/Read Complete；sourceChanged、失败注入rollback/backup/recovery、多个独立文件及GUID冲突/不同父/中文空格/drive root路径边界适用旧测试复验。
4. Shared helper编译进入完整主Solution Debug|ARM64，独立test按实际已有Solution/target参数，不另建系统。PPT unchanged长最终路径C10release→hold3轮/严格same/foreign fresh，禁止只用纯CreateFile成功当生产保存PASS。
5. 最后Release3arch/Win7人工与所有newsource精确HF审。普通最终path>260支持仍未实现/未验证，备份/恢复文件保留与用户数据不删。

## 所有权/下一步

Root唯一Main/SS/sharedHeader/工程/spec/账本/build-run，Shared UInk唯一实施writer待Root明确指定，B3只有UI，C3只有其精确诊断源。独立design/checker不改source、不运行GUI/递归。所有writer停写才MSBuild，输入/效果/存储语义不降。当前状态设计待审/产品修补未开始，数据与阶段PASS不删。
