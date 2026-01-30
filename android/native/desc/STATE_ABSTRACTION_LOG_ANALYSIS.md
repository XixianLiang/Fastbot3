/**
 * @authors Zhao Zhang
 */

# State 抽象运行日志分析

本文结合 `fastbot.log` 梳理本次测试中 **State 抽象的完整过程**：初始 state key 维度、聚类/细化、回退（coarsen）及 split 数据积累。

---

## 1. 初始 State Key 维度（代码定义）

### 1.1 维度定义（Base.h）

State 等价类由 **Widget 的若干属性** 参与 hash 决定，由位掩码 **WidgetKeyMask**（`uint8_t`）选择：

| 维度 | 枚举 | 位 | 数值 |
|------|------|-----|------|
| Clazz | WidgetKeyAttr::Clazz | 1<<0 | 1 |
| ResourceID | WidgetKeyAttr::ResourceID | 1<<1 | 2 |
| OperateMask | WidgetKeyAttr::OperateMask | 1<<2 | 4 |
| ScrollType | WidgetKeyAttr::ScrollType | 1<<3 | 8 |
| Text | WidgetKeyAttr::Text | 1<<4 | 16 |
| ContentDesc | WidgetKeyAttr::ContentDesc | 1<<5 | 32 |
| Index | WidgetKeyAttr::Index | 1<<6 | 64 |

**代码位置**：`Base.h` 中 `WidgetKeyAttr` 与 `WidgetKeyMask`；`ReuseState::buildHashForState()` 用 `_widgetKeyMask` 调用 `widget->hashWithMask(mask)` 得到 widgetsHash，再与 activityHash 组合成 state hash。

### 1.2 默认 Mask（初始粗粒度）

```cpp
// Base.h
DefaultWidgetKeyMask = Clazz | ResourceID | OperateMask | ScrollType  // = 1+2+4+8 = 15
```

- **含义**：新 Activity 首次建 state 时，若 `_activityKeyMask` 中无该 activity，则使用 `DefaultWidgetKeyMask`（`Model::getActivityKeyMask()`，Model.cpp:41-46）。
- **不包含**：Text、ContentDesc、Index，以减少状态数、便于后续按需 refine。

### 1.3 本日志中的“初始”表现

- 日志开头（约 2284 行）：`state abstraction: enabled (check interval=50, batch every 50 steps)`，随后大量 `state abstraction: transition`（MainActivity）。
- **第一次批处理**（约 34108 行）时，MainActivity 的 mask 仍是 **15**（默认），说明此前该 activity 从未被 refine 过；本批因 **alpha=1** 进入 toRefine，执行 **15→47（+ContentDesc）**。
- 即：本段 log 从“部分 activity 仍为默认 15”开始，**初始 state key 维度在代码里是 DefaultWidgetKeyMask=15**，在 log 中由 MainActivity 首次 refine 体现。

---

## 2. 聚类与细化（Refine）过程

### 2.1 触发来源（与代码一致）

1. **非确定性（State Refinement）**  
   `Model::detectNonDeterminism()`（Model.cpp:616-632）：遍历 `_transitionLog`，同一 `(sourceStateHash, actionHash)` 对应 **≥ MinNonDeterminismCount（2）个不同 targetStateHash** → 该 source 所在 activity 进入待 refine 列表。
2. **α 触发（Action Refinement）**  
   每步 `getOperateOpt`（Model.cpp:536-538）：若 `state->getMaxWidgetsPerModelAction() > AlphaMaxGuiActionsPerModelAction（3）`，则把该 activity 加入 `_activitiesNeedingAlphaRefinement`；批处理时与 nonDet 合并去重得到 toRefine。

### 2.2 细化规则与顺序（代码）

`Model::refineActivity`（Model.cpp:635-688）按固定顺序**每批至多加一维**：

- **加维顺序**：ContentDesc → Index → Text。
- **约束**：已最细则 return false；`(activity, newMask)` 在黑名单则跳过；加 **Text** 前检查 §11.5：textCount、textRatio、uniqueAfterText 上限，任一超限则 skip refine（如 log 中 `reason=textRatio>50%`）。

### 2.3 本日志中的批处理事件（按时间顺序）

| 批处理 | 行号约 | nonDet | alpha | toRefine | 结果 |
|--------|--------|--------|-------|----------|------|
| 1 | 34108 | 0 | 1 | 1 | **MainActivity** 15→47 (+ContentDesc)，stateCount=16 |
| 2 | 67866 | 0 | 3 | 3 | **SelectAreaCodeActivity** 15→47；**MainActivity** 47→111 (+Index)，stateCount=33；**LandscapeActivity** 15→47 |
| 3 | 98538 | 0 | 1 | 1 | **MainActivity** 111→127 (+Text)，stateCount=55 |
| 4 | 127595 | 1 | 2 | 2 | **XAccountHostActivity** 15→47；MainActivity skip（already finest） |
| 5 | 145922 | 1 | 2 | 2 | **XAccountHostActivity** 47→111 (+Index)；MainActivity skip |
| 6 | 171513 | 1 | 1 | 2 | **XAccountHostActivity** skip (+Text) **reason=textRatio>50%**；MainActivity skip |
| 7–9 | 207267 等 | 1 | 1–2 | 2 | 均为 skip（textRatio>50% / already finest） |
| 10 | 261828 | 1 | 2 | 2 | **XAccountHostActivity** 111→127 (+Text)，stateCount=10；MainActivity skip |
| 11–12 | 280239 等 | 2 | 2 | 2 | 全部 skip（already finest） |
| 13 | 319239 | 2 | 2 | 3 | **NexusLauncherActivity** 15→47，stateCount=1；其余 skip |
| 14 | 538953 | 3 | 3 | 4 | **HybridActivity** 15→47，stateCount=4；其余 skip |
| 15 | 484502 | 3 | 1 | 3 | **SelectAreaCodeActivity** 47→111，stateCount=3；其余 skip |
| … | … | … | … | … | 后续多为 skip（already finest） |
| **含 Coarsen 的批** | **862031** | **3** | **2** | **4** | **MainActivity coarsen 127→111（split 9>8）**；MainActivity skip refine **reason=blacklisted**；**NexusLauncherActivity** 47→111 |

要点：

- **MainActivity** 在本 log 中完整走完一轮细化：15 → 47 → 111 → 127，随后因 split 9>8 触发 **coarsen 回退到 111**，且 mask=127 被加入黑名单，故之后该 activity 不再尝试加 Text。
- **XAccountHostActivity** 曾因 **textRatio>50%** 多次跳过 +Text，后来在条件允许时完成 111→127。
- **加维顺序** 与代码一致：ContentDesc → Index → Text；Text 受保护时会出现 skip。

---

## 3. Split 数据积累与 Coarsen（回退）

### 3.1 Split 记录逻辑（recordStateSplitIfRefined）

每次 `getOperateOpt` 在 `createAndAddState` 并 `recordTransition` 之后调用（Model.cpp:602-613）：

```cpp
recordStateSplitIfRefined(activity, state);
```

- 仅当该 activity **已有 refine 上下文**（`_activityAbstractionContext`）且 **当前 mask 比 previousMask 更细**（`ctx.previousMask != getActivityKeyMask(activity)`）时，才写入：
  - `oldHash = state->getHashUnderMask(ctx.previousMask)`（上一版 L′ 下的状态）
  - `newHash = state->hash()`（当前 mask L 下的状态）
  - `ctx.oldStateToNewStates[oldHash].insert(newHash)`。

日志中每条 split 形如：

```
state abstraction: split activity=... oldHash=... newHash=... setSize=...
```

- **setSize** 表示该 `oldHash` 在**当前已积累**的不同 `newHash` 个数（即 L′ 下一状态被 L 拆成几个新状态）。

### 3.2 Coarsen 条件与执行（代码）

`Model::coarsenActivityIfNeeded`（Model.cpp:692-713）：

- 遍历 `ctx.oldStateToNewStates`，若存在某 `oldHash` 对应 **set.size() > BetaMaxSplitCount（8）**，则执行 **coarsen**：
  - `setActivityKeyMask(activity, prev)`：mask 回退到 previousMask。
  - `_coarseningBlacklist.insert((activity, cur))`：当前细 mask 加入黑名单。
  - `ctx.oldStateToNewStates.clear()` 等。

**执行时机**：每 K 步批处理**开头**先对“上一批已 refine、已积累 split”的 activity 做 coarsen 检查（Model.cpp:717-724）；本批内刚 refine 的 activity 再调用一次 `coarsenActivityIfNeeded`（split 刚清空，相当于 no-op）。

### 3.3 本日志中的 split 与 Coarsen

- **setSize 分布**：log 中可见 setSize=1,2,3,4,5，以及达到 **9** 的个案：
  - 例如 `state abstraction: split activity=com.luna.biz.main.main.MainActivity oldHash=9961649879448318790 newHash=9915158667369897332 setSize=9`（约 857296 行）。
- **Coarsen 事件**（约 862033 行）：
  ```
  state abstraction: coarsen activity=com.luna.biz.main.main.MainActivity mask 127->111 (split 9>8) dims=[Clazz|ResourceID|OperateMask|ScrollType|Text|ContentDesc|Index]->[Clazz|ResourceID|OperateMask|ScrollType|ContentDesc|Index]
  ```
  - MainActivity 在加上 Text（mask=127）后，在过去若干步内，某个在 mask=111 下的 oldHash 被拆成了 **9 个**不同的 newHash（mask=127 下），超过 β=8，触发回退。
  - 回退后同一批中 MainActivity 尝试 refine 时被跳过：`skip refine activity=... MainActivity newMask=127 reason=blacklisted`。

即：**本段测试中发生了一次回退（coarsen）**，且回退与黑名单行为与代码设计一致。

---

## 4. 完整流程串联（本 log）

1. **初始 state key 维度**  
   - 代码默认 **DefaultWidgetKeyMask=15**（Clazz|ResourceID|OperateMask|ScrollType）。  
   - 本 log 中 MainActivity 首次批处理前为 15，其余 activity 随批处理陆续从 15 或 47 开始细化。

2. **每步（getOperateOpt）**  
   - `createAndAddState` 使用当前 `getActivityKeyMask(activity)` 建 state、算 hash（Model.cpp:353-354）。  
   - `recordTransition` 写入 (srcStateHash, actionHash, targetStateHash, activity)。  
   - `recordStateSplitIfRefined` 在“已 refine 且未 coarsen”的 activity 上积累 oldStateToNewStates。  
   - 若 `getMaxWidgetsPerModelAction() > 3`，将 activity 加入 α 列表。  
   - `_stepCountSinceLastCheck++`；满 50 步触发一次批处理（Model.cpp:569-574）。

3. **每 50 步批处理（runRefinementAndCoarseningIfScheduled）**  
   - **阶段 0**：对 `_activityAbstractionContext` 中满足 `ctx.previousMask != getActivityKeyMask(activity)` 的 activity 调用 `coarsenActivityIfNeeded`（用过去 K 步的 split 数据）；本 log 中 MainActivity 在某一批达到 split 9>8，执行 **coarsen 127→111**。  
   - **Refine 阶段**：`detectNonDeterminism()` + 合并 α 列表 → toRefine；对 toRefine 依次 `refineActivity`；对本次 refine 成功的 activity 再调用一次 `coarsenActivityIfNeeded`。  
   - Refine 结果：多轮中 MainActivity 15→47→111→127，随后 coarsen 回 111 并黑名单 127；XAccountHostActivity 曾因 textRatio>50% 跳过 +Text，后 111→127；HybridActivity、SelectAreaCodeActivity、NexusLauncherActivity、LandscapeActivity 等按需 15→47 或 47→111。

4. **回退之后**  
   - MainActivity 的 mask 稳定为 **111**（不含 Text），且 (MainActivity, 127) 在黑名单，不再尝试加 Text。  
   - Split 在 coarsen 时被清空，后续若该 activity 再次被 refine（加其他维），会重新积累 split。

---

## 5. 与代码/技术档案的对应关系

| 项目 | 代码/常量 | 本 log 体现 |
|------|-----------|-------------|
| 批处理间隔 | RefinementCheckInterval=50 | batch at step 50 (interval=50) |
| 非确定性阈值 | MinNonDeterminismCount=2 | nonDet≥1 时对应 activity 进入 toRefine |
| α 阈值 | AlphaMaxGuiActionsPerModelAction=3 | alpha=1~3，与 toRefine 合并 |
| Coarsen 阈值 β | BetaMaxSplitCount=8 | split 9>8 触发 MainActivity coarsen 127→111 |
| 初始 mask | DefaultWidgetKeyMask=15 | MainActivity 首次 refine 15→47 |
| 细化顺序 | ContentDesc → Index → Text | 各 activity 15→47→111→127 或跳过 Text |
| Text 保护 | textRatio>50% 等 | XAccountHostActivity skip (+Text) reason=textRatio>50% |
| Coarsen 与黑名单 | coarsenActivityIfNeeded + _coarseningBlacklist | coarsen 127→111；MainActivity skip newMask=127 reason=blacklisted |

以上即为结合 `fastbot.log` 与代码的 **State 抽象完整过程分析**：初始 state key 维度、聚类/细化时机与结果、split 积累、以及本段测试中**发生的一次回退（coarsen）**及黑名单行为。


## 6. 总结

下面是简要结论和对应关系。

### 1. 初始 State Key 维度（代码 + log）

- **代码（Base.h）**：7 个维度用位掩码 `WidgetKeyMask` 选择；默认粗粒度  
  `DefaultWidgetKeyMask = Clazz | ResourceID | OperateMask | ScrollType = 15`，不含 Text、ContentDesc、Index。
- **实现**：`Model::getActivityKeyMask(activity)` 若 `_activityKeyMask` 无该 activity 则返回 15；`createAndAddState` 用该 mask 建 state、算 hash（Model.cpp:353-354，ReuseState::buildHashForState）。
- **本 log**：第一次批处理（约 34108 行）时 MainActivity 仍为 15，本批因 alpha=1 被 refine 为 15→47，说明本段 log 里“初始”就是默认 15。

### 2. 聚类与细化（Refine）过程

- **触发（与代码一致）**  
  - 非确定性：`detectNonDeterminism()`，同一 (srcStateHash, actionHash) 对应 ≥2 个不同 targetStateHash → 该 activity 进 toRefine。  
  - α：每步若 `getMaxWidgetsPerModelAction() > 3`，activity 进 `_activitiesNeedingAlphaRefinement`；批处理时与 nonDet 合并去重。
- **细化规则（Model::refineActivity）**：每批至多加一维，顺序 ContentDesc → Index → Text；加 Text 前有 textCount/textRatio/uniqueAfterText 保护；已最细或 (activity, newMask) 在黑名单则跳过。
- **本 log 中的典型批处理**  
  - 第 1 批：MainActivity 15→47 (+ContentDesc)，stateCount=16。  
  - 第 2 批：SelectAreaCodeActivity 15→47，MainActivity 47→111 (+Index)，LandscapeActivity 15→47。  
  - 第 3 批：MainActivity 111→127 (+Text)，stateCount=55。  
  - 后续：XAccountHostActivity 多次 skip (+Text) reason=textRatio>50%，后在某批完成 111→127；HybridActivity 15→47；SelectAreaCodeActivity 47→111；NexusLauncherActivity 15→47、47→111 等。

### 3. Split 积累与回退（Coarsen）

- **Split 记录（recordStateSplitIfRefined）**：仅当 activity 已有 refine 上下文且当前 mask 比 previousMask 更细时，用 `oldHash = state->getHashUnderMask(ctx.previousMask)`、`newHash = state->hash()` 更新 `ctx.oldStateToNewStates[oldHash].insert(newHash)`；log 里每条 split 带 setSize（该 oldHash 下已积累的不同 newHash 数）。
- **Coarsen 条件（coarsenActivityIfNeeded）**：若存在某 oldHash 对应 `oldStateToNewStates[oldHash].size() > BetaMaxSplitCount(8)`，则回退到 previousMask 并把当前细 mask 加入黑名单。
- **本 log**  
  - setSize 在 log 中可见 1、2、3、4、5，以及 9（例如 MainActivity，oldHash=9961649879448318790，约 857296 行）。  
  - 一次 Coarsen（约 862033 行）：  
    `state abstraction: coarsen activity=com.luna.biz.main.main.MainActivity mask 127->111 (split 9>8)`  
    即 MainActivity 在 mask=127（含 Text）下，某个在 mask=111 下的 oldHash 被拆成 9 个 newHash，超过 β=8，执行回退 127→111；同一批中 MainActivity 再 refine 时被 skip reason=blacklisted（mask=127 已进黑名单）。

### 4. 与代码的对应关系（简要）

| 项目 | 代码/常量 | 本 log 体现 |
|------|-----------|-------------|
| 批处理间隔 | RefinementCheckInterval=50 | batch at step 50 (interval=50) |
| 非确定性阈值 | MinNonDeterminismCount=2 | nonDet≥1 时对应 activity 进 toRefine |
| α 阈值 | AlphaMaxGuiActionsPerModelAction=3 | alpha=1~3 与 nonDet 合并 toRefine |
| Coarsen 阈值 | BetaMaxSplitCount=8 | split 9>8 触发 MainActivity coarsen 127→111 |
| 初始 mask | DefaultWidgetKeyMask=15 | MainActivity 首次 refine 15→47 |
| 细化顺序 | ContentDesc → Index → Text | 各 activity 15→47→111→127 或跳过 Text |
| Text 保护 | textRatio>50% 等 | XAccountHostActivity skip (+Text) textRatio>50% |
| 回退与黑名单 | coarsen + _coarseningBlacklist | coarsen 127→111；MainActivity blacklisted 127 |

