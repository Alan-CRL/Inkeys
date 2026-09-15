# 五入口配置、连续尺寸与验证状态

## 当前连续会话
普通橡皮使用ConfiguredEraser和完整五入口表；显式Fixed/Speed工具仍强制类型。笔尖/笔尾响应分别保存，Win7使用稳定枚举安全回退。非Touch各入口复用独立MouseLifecycle会话与所有权票据，普通Up/Down不再Reset/夹小；Detached状态按真实时间回落，真实重连仍使用原冻结协议。SessionConfigCompatible保留真实context/映射/单位安全检查，剔除无害revision与识别补全。首点与待接触光标共用解析，细节和验证见validation-entry-sessions-20260915.md。

## 当前精细区（2026-09-15）
只替代最小到标准的立即响应。精细观察使用140ms真实路程窗，平台/解除阈值为各模型原细段上界的0.20/0.35倍；100ms进入、160ms解除和方向确认并行，双向tau为200/260ms。迟滞带不累积解除证据，原idle历史参与同一精细确认；目标稳定40ms后精确收敛，统一double最小log以保证休眠。清扫资格和面积主导路径不串联新确认，跨standard连续。详情及测量见validation-fine-band-20260915.md。

## 分层
EraserSizes仍是DIP属性单一来源。真实InputSource、ResponseModel、motionSource分离；只有屏幕笔保留标准以上增量的beta补偿，Touch不再要求整目标毫米一致。物理、DIP和经验动作单位不混算。

## 面积
保留RTS原始宽高与per-context换算，校验实际返回的单位、分辨率和范围后才按轴转DIP。首次稳定拖擦50ms建立本接触参考；原位移解锁不变。参考锁存，普通拖擦下限无需清扫资格，但不能由面积或帧时间制造新移动。

面积下限是 `max(speedTarget, contactFloor)` 后的连续动态结果，实际几何、光标和历史宽度区间共用。缺包2s、显式无效200ms宽限、180ms参考释放；重连平移时钟，终态零面积不进入新参考。面积开关单独锁存，不改变Mouse/Pen Hover显示标尺。

## 已定位的正确性问题
- 每包目标变化小于settle tolerance时立即吸附，会让高回报率Touch跳过阻尼。Touch仅在目标稳定时吸附，其他模型保持不变。
- 休眠后诊断快照的idleSeconds不再增长。隐藏测试等待needsAnimation=false和实际尺寸，不等待年龄跨越任意阈值。
- 原错误等待额外耗时15s，暴露模型的单次2000输出上限。测试等待已修正；复查又补齐真实Touch笔速长停顿恢复：使用相同模型参数在最后已接受点短时间重锚，继续累计原结果/几何。保持原输出上限、其他设备行为及保存格式，合成种子不经过速度/面积统计。

## 实验入口与验证
面积辅助迁入绘制设置的橡皮擦块，原Experimental.Inkeys3.Draw3.TouchContactAreaAssistance键不变，程序调测继续同步读写；实验页仅保留独立控制台输出。五入口与两个笔响应选择写入Drawing.Eraser。headless保留第八轮回归并验证会话/配置写读；隐藏测试验证入口、首点、普通跨段及原有面积/历史链。
