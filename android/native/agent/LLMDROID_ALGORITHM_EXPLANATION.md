/**
 * @authors zhao zhang
 */

# LLMDroid（GPTAgent） 算法与模式机详解 (https://dl.acm.org/doi/epdf/10.1145/3715763 我们这里将 LLMDroid 关键算法迁移到 Fastbot3 中)

一句话先概括：

> LLMDroid 不是替代 Fastbot 的 RL（如 DoubleSarsa），而是在「卡住时」加一个更聪明的导航与功能执行层，帮你跳出局部循环，提高覆盖率。

---

## 一、先用生活化类比理解

把 App 测试看成逛一个超大商场：

- **Fastbot RL（DoubleSarsa）**像一个很勤快的保安，平时按经验到处巡逻（探索）
- 巡逻久了会遇到问题：总在熟悉区域打转，新增发现越来越少（覆盖率增长变慢）
- **LLMDroid**像一个「会看地图的顾问」：
  - 平时不打扰保安
  - 只在保安明显卡住时，给一个「先去哪一层、测试哪种功能」的建议
  - 到了目标区域后，再一步步给操作提示，做完后让保安继续巡逻

这就是核心思想：  
**Autonomous Exploration（自主探索）** + **LLM Guidance（LLM 引导）** 交替运行。

---

## 二、流程图

下面是 Fastbot3-LLMDroid 逻辑流程图：

```text
+------------------------------+
| Start / App Running          |
+------------------------------+
               |
               v
+------------------------------+
| EXPLORE mode                 |
| (DoubleSarsa/RL 主导选动作)    |
+------------------------------+
               |
               v
+----------------------------------------------+
| 每步都做两件事                               |
| 1) processState: 维护 MergedState / 摘要队列   |
| 2) coverage monitor: 看覆盖率增长是否变慢       |
+----------------------------------------------+
               |
       +-------+--------+
       |                |
       | 未停滞          | 停滞/时间窗触发
       v                v
 (继续 EXPLORE)   +-----------------------------+
                  | prepareForNavigation        |
                  | - waitUntilQueueEmpty       |
                  | - ask GUIDE (LLM选目标页/功能)|
                  | - Graph::findPath 求路径      |
                  +-----------------------------+
                               |
                               v
                  +-----------------------------+
                  | NAVIGATE mode               |
                  | - 回放路径（含 fuzzy 匹配）    |
                  | - 失败重试/换路径/换目标       |
                  +-----------------------------+
                               |
                    +----------+----------+
                    |                     |
                    | 导航成功             | 导航失败过多
                    v                     v
         +----------------------+   +----------------------+
         | TEST_FUNCTION mode   |   | Back to EXPLORE      |
         | ask TEST_FUNCTION    |   | (并触发 REANALYSIS)   |
         | 最多 5 步 LLM 指导     |   +----------------------+
         +----------------------+
                    |
                    v
         +----------------------+
         | Back to EXPLORE      |
         | mark function tested |
         +----------------------+
```

---

## 三、核心概念

### 3.1 什么是模式机（Mode Machine）

模式机就是「当前测试策略处于哪种状态」：

1. `EXPLORE`：默认模式，主要靠 RL（DoubleSarsa）探索
2. `NAVIGATE`：卡住时，先导航到 LLM 认为更有价值的页面
3. `TEST_FUNCTION`：到目标页后，LLM 指导执行目标功能（最多 5 步）

模式切换不是随机的，而是由「覆盖率增长趋势」和「导航结果」驱动的。

### 3.2 什么是 MergedState（合并页面）

LLMDroid 不直接把每一帧 UI 当新页面，而是做页面聚类（相似页归并）：

- 相似标准本质是 widget 集合相似度（Dice 思路）
- 一个簇有 root state（根页）
- 这样可以减少 LLM 请求次数，不需要每一步都重新“读屏理解”

这层结构在 Fastbot3 中由 `MergedState` / `MergedStateGraph` 表示。

### 3.3 什么是 Offline Navigation（离线导航）

不是让 LLM 一步一步带路，而是：

1. 先让 LLM 选目标页 + 目标功能
2. 本地用 `Graph::findPath`（Dijkstra + traceback）找最短路径
3. 回放路径动作，过程中做相似度容错（fuzzy）

好处：省钱、省时、稳定。

---

## 四、为什么 LLMDroid 能提升覆盖率

来自论文结论：

1. **减少“盲走”时间**：不是每一步都问 LLM，只有关键节点才问
2. **避免局部循环**：停滞检测触发后，主动换目标区域
3. **优先测试高价值功能**：LLM 选目标偏向导航能力强、潜在覆盖面大的功能
4. **探索与总结并行**：EXPLORE 期间后台做页面总结，减少阻塞

---

## 五、Fastbot3 中 LLMDroid 的工程实现

### 5.1 状态与模式控制

- `AbstractAgent.cpp`
  - `LlmdroidAgentOverlay`
  - `llmdroidSwitchMode`
  - `llmdroidPrepareForNavigation`
  - `llmdroidGuideCheck`
  - `llmdroidPrepareTestFunction`
  - `resolveNewAction` 内 NAVIGATE / TEST_FUNCTION 分支

### 5.2 页面聚类与图

- `desc/MergedState.{h,cpp}`
- `desc/reuse/ReuseState.{h,cpp}`
  - `StateGraphEdge`
  - `addSubSequentState`
  - `findSimilarAction`

### 5.3 路径规划

- `model/Graph.{h,cpp}`
  - `findPath`
  - `dijkstra`
  - `traceback`
  - `processPaths`
  - `transformPath`
- `model/GraphPath.{h,cpp}` 提供 `Step` / `Path`

### 5.4 LLM 调度

- `agent/GPTAgent.{h,cpp}`
  - worker 线程 + 队列
  - `STATE_OVERVIEW`, `REANALYSIS`, `GUIDE`, `TEST_FUNCTION`
  - `resetPromise` + future 链路（GUIDE/TEST_FUNCTION）

### 5.5 Java HTTP 桥

- `monkey/.../AiClient.java`
  - `llmdroid_state_overview`
  - `llmdroid_reanalysis`
  - `llmdroid_guide`
  - `llmdroid_test_function`

---

## 六、模式切换逻辑

```text
EXPLORE:
  - RL 正常选动作
  - 每步更新 coverage window
  - 若窗口内增长长期低于动态阈值 -> 触发 NAVIGATE

NAVIGATE:
  - 先等 overview/reanalysis 队列清空
  - LLM 选目标（GUIDE）
  - Graph::findPath 找路径
  - guideCheck 校验当前页是否符合预期
  - 失败: 换路径/降相似阈值/重问目标（有上限）
  - 成功: 切 TEST_FUNCTION

TEST_FUNCTION:
  - LLM 每步给一个动作（最多 5 步）
  - 若 LLM 返回完成或空动作 -> 回 EXPLORE
  - 标记功能已测，必要时触发 reanalysis
```

---

## 七、LLM 到底“怎么用”，以及为什么这样用

LLMDroid 不是把 LLM 当成「每步动作决策器」，而是分工使用：

1. **页面总结（STATE_OVERVIEW）**
   - 输入：页面 HTML 描述
   - 输出：页面概览 + 功能列表 + 重要性排序

2. **重分析（REANALYSIS）**
   - 输入：同簇页面里 root 未覆盖到的差异控件
   - 输出：补充功能标签

3. **目标选择（GUIDE）**
   - 输入：Top-K 高价值页面摘要
   - 输出：目标页 + 目标功能

4. **功能执行（TEST_FUNCTION）**
   - 输入：当前页 HTML + 目标功能 + 已执行动作
   - 输出：下一步动作（最多 5 步）

这种设计直接对应论文观点：  
**少问，但问关键问题；把“重体力活”离线化（路径回放）**。

---

## 八、与 DoubleSarsa / 动态抽象 / refine / coarsen 的关系

### 8.1 默认情况下（`max.llm.llmdroid=false`）

完全不进入 LLMDroid 分支，行为与原来一致。

### 8.2 开启 LLMDroid 后

1. **动态抽象（APE StateKey）**
   - 仍在 `Model::getOperateOpt` 原链路中工作
   - LLMDroid 不改 RL 身份哈希、不改 dedup key

2. **refine / coarsen**
   - 调度仍由 `Model` 的原逻辑触发
   - 不依赖 `LlmdroidMode`

3. **DoubleSarsa**
   - `EXPLORE` 下仍是 DoubleSarsa 主导
   - `NAVIGATE` / `TEST_FUNCTION` 这两段会按路径或 LLM 动作执行
   - 回到 `EXPLORE` 后继续 DoubleSarsa

也就是：  
**LLMDroid 是“阶段性接管动作源”，不是替换 RL 学习器。**


---

## 总结

LLMDroid 的本质不是“让 LLM 代替强化学习”，而是：

> 让 RL 负责大部分高速探索，让 LLM 在“卡住时”做高价值决策，并通过离线导航把成本打下来。

这也是它相比 step-by-step LLM 测试更实用、更便宜的关键。

