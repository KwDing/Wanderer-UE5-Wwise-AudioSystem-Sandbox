# Tests：自动化验证代码

Tests 是源码分组，仍编译在 WandererEditor 模块中。它集中用例与测试夹具，不是音频资产目录，也不是一个新的运行时模块。测试逻辑随功能契约一起维护，开发与 QA 共同负责；“归到 Tests”不表示开发者修改功能后可以忽略断言。

## 目录

| 目录 | 验证范围 |
|---|---|
| AudioRuntime | 事件/诊断、Subsystem、原生地表映射、几何查询 |
| AudioManager | 面板、列偏好、来源/Room/建筑作者操作及PIE播放关系 |
| WwiseSync | Browser目录、选择边界、隔离资产同步与保存的Event源 |
| Support | 共享测试引用夹具和临时备份清理辅助 |

InteriorFitEditor 的制作菜单测试放在该模块的 Private/QA；DSP测试在 AudioPlugins/AirwindowsWwise/QA，由 test.ps1 编译运行。第三方SDK自身测试和历史AgentKit实验脚本不属于本次源代码分组。

## AudioEventsTests 是什么

WandererAudioEventsTests.cpp 包含控制台事件过滤、音频诊断通道过滤，以及两轮PIE的场景进入事件检查。它通过 UE Automation 注册运行，不是一个需要制作人员绑定的声音资产，也不是 EventBus 的实现。角色动作执行实现仍在 Wanderer 模块的 Systems/Audio。

## 执行与结果

在 Session Frontend / Automation 中按 Wanderer.Audio.Manager、Wanderer.AudioEvents、Wanderer.Audio.Subsystems 等筛选执行。迁移目录后保留原筛选名，便于持续使用已有执行入口。

涉及原生播放的管理器PIE测试需要独立进程及 BuildingAmbienceMutedProbe / NearbySourceRelationsMutedProbe 参数，并在测试内部确认静音。不要绕过这些前提。部分用例会创建临时地图、对象或隔离资产；按用例的清理路径运行，不把其输出作为正式Content。

自动化断言覆盖配置与行为，不替代手工操作、声音听感和Profiler检查。
