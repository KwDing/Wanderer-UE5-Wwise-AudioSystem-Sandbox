# Wanderer

Wanderer 是一个 Unreal Engine 5 游戏原型，包含角色选择、地图探索、环境交互和 Wwise 空间音频。由于游戏项目资产体积较大，此处仅公开 `Source/` 下的项目代码。

[![播放 Wanderer Audio System 演示](./Docs/Images/showcase-cover-play.jpg)](https://www.bilibili.com/video/BV183WS6HEot/)

## 代码架构与目录结构 (Code Architecture)

### Runtime Module

| 目录 | 职责 |
|---|---|
| `Types/`、`Interfaces/` | 定义共享类型与跨系统接口。 |
| `Core/`、`Data/` | 提供事件总线、通用工具和数据配置。 |
| `Framework/` | 协调游戏流程、选角、场景加载与地图交互。 |
| `Systems/` | 管理世界昼夜、音频调度与资源生命周期。 |
| `Characters/`、`Actors/`、`Components/` | 组装角色和场景实体，提供脚步、地表交互及声音播放等可复用能力。 |
| `UI/` | 展示状态并处理界面交互。 |

模块间共用的接口和类型通过 `Public/` 暴露，内部实现放在 `Private/`。界面通过模型或接口读取状态，运行时模块无需依赖编辑器工具。

### Editor Module

| 工具 | 职责 |
|---|---|
| [Audio Manager](./Wanderer/Source/WandererEditor/Private/Features/AudioManager) | 定位和配置 Point、Area、Spline、Room 与 Portal，管理建筑共享底噪和区域预算，在 PIE 中查看发声位置与播放状态。 |
| [Wwise Browser 扩展](./Wanderer/Source/WandererEditor/Private/Features/WwiseBrowserExt) | 将 Wwise Event、Switch、RTPC 等资产按Wwise中的目录同步到 UE Content。 |

## 核心功能模块与源码索引 (Core Features & Code Index)

| 类别 | 功能 | 实现位置 |
|---|---|---|
| Gameplay | 角色预览与切换 | [选角模型](./Wanderer/Source/Wanderer/Private/Framework/CharacterSelectionModel.cpp) · [选角界面](./Wanderer/Source/Wanderer/Private/UI/Characters) |
| Gameplay | 地图扫描与传送 | [地图逻辑](./Wanderer/Source/Wanderer/Private/Framework/Mapping) · [传送实现](./Wanderer/Source/Wanderer/Private/Core/CharacterTeleportLibrary.cpp) |
| Gameplay | 昼夜切换 | [世界环境状态](./Wanderer/Source/Wanderer/Private/Systems/WandererEnvironmentSubsystem.cpp) |
| Gameplay | 地表识别与浅水交互 | [地表采样](./Wanderer/Source/Wanderer/Private/Framework/Surface) · [场景交互组件](./Wanderer/Source/Wanderer/Private/Components/SurfaceInteraction) |
| Runtime Audio | 环境声的区域预载、播放预算与释放 | [音频调度](./Wanderer/Source/Wanderer/Private/Systems/Audio) · [资源组配置](./Wanderer/Source/Wanderer/Private/Data/Audio/WorldAudioBudgetData.cpp) |
| Runtime Audio | Point、Area、Spline、Room、建筑底噪与昼夜环境声 | [声源几何](./Wanderer/Source/Wanderer/Private/Actors/Audio) · [世界声源播放](./Wanderer/Source/Wanderer/Private/Components/WorldAudio) |
| Runtime Audio | 角色脚步与动作音效 | [脚步接触](./Wanderer/Source/Wanderer/Private/Components/Footsteps) · [角色音频](./Wanderer/Source/Wanderer/Private/Components/CharacterAudio) |
| Runtime Audio | Radio 控制与同步播放 | [控制模型](./Wanderer/Source/Wanderer/Private/Framework/RadioControlModel.cpp) · [播放管理](./Wanderer/Source/Wanderer/Private/Systems/Audio/RadioPlaybackManager.cpp) |
| Runtime Audio | UI 音效、菜单压低与水下混音 | [UI 音效](./Wanderer/Source/Wanderer/Private/Systems/Audio/WandererUIAudioSubsystemPlayback.cpp) · [监听者混音](./Wanderer/Source/Wanderer/Private/Systems/Audio/ListenerAudioManager.cpp) |

用于自动生成 Area／Spline 几何的 UE 插件在 GitHub 独立发布。[InteriorFit](https://github.com/KwDing/InteriorFit)

## 资源组处理流程

世界音频子系统 按监听者到资源组包围盒的最短距离（m）判断是否需要驻留，盒内距离为 0。休眠组用加载半径 `LoadDistanceM`，已驻留组用较远一档的卸载半径 `UnloadDistanceM`（必须>=LoadDistanceM），在卸载半径外超过 `UnloadDelaySec`会把资源变为dormant。

```mermaid
stateDiagram-v2
    [*] --> Dormant
    Dormant --> Loading: 进入加载半径、要求常驻或被依赖
    Loading --> Active: 预载完成且依赖就绪
    Loading --> Dormant: 加载期间已无需驻留
    Active --> Draining: 超出卸载半径且延迟已到，无其他驻留需求
    Draining --> Active: 再次需要该组
    Draining --> Dormant: 播放与尾音排空，释放组资源
```

## Event去重

- **同一个 Event 被多处请求。** 区域预载和多个声源按同一个 `UAkAudioEvent` 查共享记录，增加使用计数。后来的请求可提高这条记录的优先级。媒体就绪后，每个声源仍可独立 Post。
- **多 Event 都在排队。** 区域预载用 `-100 − 到组的距离（米）`，环境声播放请求用自身的 `EffectivePriority`。队列取同一 Event 各次请求中的最高优先级。

## Event 的加载优先级

1. **先检查资格。** `UWandererAudioSourceComponent::Evaluate` 要求指定的资源组处于 `Active`（未指定组则通过）、配置有效、声源启用且几何查询成功。只有合格声源才提交 `FAudioSchedulingInfo` 参与排序。
2. **受保护者排最前。** 例如监听者位于某个 Room 内，该 Room 的环境声`bProtected` 为真时，`EffectivePriority` 直接取 **100.0**。
3. **普通环境声按距离算有效分。** `EffectivePriority` 是 `float`，取 Profile 的 `Priority` 加实例偏移 `PriorityBias`，减去距离扣分，达到该距离尺度后扣满。
4. **给在播声源加2分，再按名次准入。** 同一保护等级内按排序分降序，在播取 `EffectivePriority + 2`，减少相近声源反复切换。同分时按 `OwnerPath`（所属 Actor 的完整对象路径）升序。调度器依次检查预算；新常规声还需媒体就绪，且 `RegularSlots + TransitionSlots` 的合计容量有空余才能启动。[排序](./Wanderer/Source/Wanderer/Private/Systems/Audio/WandererAudioSubsystemAdmission.cpp#L87) · [准入与启动](./Wanderer/Source/Wanderer/Private/Systems/Audio/WandererAudioSubsystemAdmission.cpp#L119)

## 环境声 Post 失败时如何回收

`PostEvent` 返回 `AK_INVALID_PLAYING_ID`（0）时，声源立即记录失败并调用 `RequestStop(0)`。即使没有有效 ID，也会按零尾音在下一次回收时处理，**无需等待 `AK_EndOfEvent`**。