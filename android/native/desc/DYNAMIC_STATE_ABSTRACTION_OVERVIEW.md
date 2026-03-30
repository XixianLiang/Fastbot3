/**
 * @authors Zhao Zhang
 */

## Fastbot Native：APE 动态抽象（Naming / StateKey）算法介绍

本文档介绍 **Fastbot native 中实现的动态抽象/细化算法**：它通过动态选择 `Naming`（抽象函数）来改变 `StateKey` 的构造方式，从而在：

- **抽象太粗**（ND：同一 source+action 对应多个 target）
- **抽象太细**（refine 后状态/影响面爆炸，需要回退）

之间自适应折中。


---

### 1) 一句话理解：APE “抽象”是什么？

在 native APE 中，我们用一个叫 `Naming` 的“抽象函数”把同一棵 GUI tree 映射成一个稳定可比较的 **StateKey**。

先说 `Naming` 本身：

1) `Naming` 在实现里就是一个**按顺序**的 `Namelet` 列表（`Naming::getNamelets()`）。

```text
Naming = [Namelet1, Namelet2, ...]
```

2) 每个 `Namelet` 由两部分组成（对应 `Namelet` 的字段）：

```text
Namelet = (expr_str, namer)
```

- `expr_str`：一段用于在 XML DOM 上筛选节点的 XPath 表达式（通过 `XPathNodeMapper::nodesForXPath(nl->getExprString())` 找到“哪些 GUITree nodes 属于这一条规则”）
- `namer`：一个规则对象，负责把“被这个 Namelet 命中的节点”映射成抽象的 `Name`（再由 `Name::toXPath()` 产生用于哈希的 XPath token）

所以这里你看到的 `Namelet1 / Namelet2`，不是“最终的 xpath segment 本身”，而是两条（expr_str + namer）命名规则；同一个节点可能被多个 Namelet 匹配，最终会由 `NamingFactory` 的 `selectNameletForNode()` 在候选里选出一个 Namelet 来给该节点赋值。

3) 当 `NamingFactory::rebuildTree()` 执行完之后：

- 每个节点会拿到一个抽象 `Name`
- `GUITree::rebuild()` 收集所有 `Name::toXPath()` token，并排序缓存成 `GUITree::getCurrentXPaths()`

因此 `StateKey` 的结构可以理解为：

```text
StateKey = activityKey + namingFingerprint + sorted(widgetXPath[])
```

其中 `widgetXPath[]` 就是上面说的 `Name::toXPath()` token 的“多重集合”（排序后便于确定性比较）。

直觉上：

- **Naming 更细** ⇒ `namer` 生成的 `widgetXPath[]` 更能区分真实差异 ⇒ 拆得更开
- **Naming 更粗** ⇒ `widgetXPath[]` 更容易合并 ⇒ 拆得更少

#### 一个例子：两条 Namelet 如何把页面变成 StateKey

假设当前界面（GUI tree）里有这些节点（只画关键属性）：

```text
Root
├─ Button  resource-id=@id/login   clickable=true  text="Login"
└─ TextView resource-id=@id/title  clickable=false text="Welcome"
```

我们构造一个很简单的 `Naming`，包含两条 `Namelet`（按顺序）：

```text
Namelet1:
  expr_str = "//*[@clickable='true']"
  namer    = BitmaskNamer(TYPE + RES_ID)     // 举例：用 class + resource-id 命名

Namelet2:
  expr_str = "//*[@clickable='false']"
  namer    = BitmaskNamer(TYPE)              // 举例：只用 class 命名
```

执行 `NamingFactory::evaluateNaming(naming, tree, dom)` 时发生的事情（按实现顺序）：

1) **先用 expr_str 把节点“圈出来”**（这是“匹配哪些节点属于这条规则”）：
   - `Namelet1` 命中：`Button@login`
   - `Namelet2` 命中：`TextView@title`

   命中逻辑来自实现：对每条 `Namelet`，`NamingFactory::evaluateNaming()` 会调用  
   `XPathNodeMapper::nodesForXPath(namelet.expr_str)` 在 GUI XML DOM 上执行这段 XPath。  
   在这个例子里，`Button@login` 的属性满足 `@clickable='true'`，所以会被 `Namelet1.expr_str="//*[@clickable='true']"` 选中；  
   `TextView@title` 满足 `@clickable='false'`，所以会被 `Namelet2` 选中。

2) **再对命中的每个节点调用 namer 生成 Name**（这是“这个节点该叫什么抽象名字”）：
   - 对 `Button@login`：`BitmaskNamer(TYPE+RES_ID)` 生成一个 `Name`，其 `toXPath()` 可能类似：

```text
//Button[@resource-id='@id/login']
```

   - 对 `TextView@title`：`BitmaskNamer(TYPE)` 生成一个 `Name`，其 `toXPath()` 可能类似：

```text
//TextView
```

3) `GUITree::rebuild()` 会把这些 `Name::toXPath()` token **去重分组并排序**，得到：

```text
sorted(widgetXPath[]) = [
  "//Button[@resource-id='@id/login']",
  "//TextView"
]
```

4) 最终 `StateKey` 把它们和 `activityKey`、`namingFingerprint` 组合起来：

```text
StateKey = (activityKey, namingFingerprint, sorted(widgetXPath[]))
```

其中 `namingFingerprint` 是 `Naming` 的**稳定标识字符串**（实现：`Naming::fingerprintString()`），作用是：

- **区分不同 Naming**：同一棵 tree 在不同 naming 下生成的 key 不应混在一起
- **支持 blacklist/缓存**：很多地方不需要保存完整 `Naming` 对象，只要 fingerprint 就能作为稳定 key

它在代码里是这样构造的（实现：`android/native/desc/naming/Naming.cpp` 的 `computeFingerprintString(...)`）：

```text
out = "v3"
for (i, Namelet nl) in enumerate(naming.namelets) in order:
  out += "|" + to_string(i) + ":" + (isBase(nl) ? "B" : "R") + ":" + nl.expr_str + "#" + hex32(nl.namer.typeDimensionMask)
fingerprint = out
```

注意：**v3** 格式为可打印字符串（无 `\x1e` 等控制字符）；**不会**对 namelet 排序；顺序由 `i:` 与向量下标一致；`B`/`R` 区分 base/refine namelet。

##### 一个示例（形状示意）

沿用上面的两个 namelet：

- `Namelet1`：`expr_str="//*[@clickable='true']"`，`namerMask={TYPE, RES_ID}`
- `Namelet2`：`expr_str="//*[@clickable='false']"`，`namerMask={TYPE}`

那么 fingerprint 大致长这样（省略 bit 的具体数字，只展示结构；顺序与 `namelets` 向量一致）：

```text
v3|0:B://*[@clickable='true']#<hex mask>|1:B://*[@clickable='false']#<hex mask>
```
（`#` 后为 8 位十六进制 `typeDimensionMask`；示例省略具体 hex。）

这样一来：

- 如果你把 `Namelet2` 的 namer 从 `TYPE` 换成 `TYPE+RES_ID`（更细），`TextView` 的 token 会更具体，状态更容易被拆开
- 如果你把 `Namelet1` 的 namer 从 `TYPE+RES_ID` 换成 `TYPE`（更粗），很多不同按钮可能都变成同一个 `//Button`，状态更容易被合并

---

### 2) ND（非确定性）是什么？为什么它说明“抽象太粗”？

系统持续记录 transition 证据（概念化）：

```text
(sourceKeyHash, actionHash) -> targetKeyHash
```

若同一个 `(sourceKeyHash, actionHash)` 产生多个不同 targetKeyHash：

```text
(S, A) -> T1
(S, A) -> T2   (T1 != T2)
```

通常表示：当前 `Naming` 把本应区分的源状态合并成同一个 `S`（抽象太粗），因此触发 refine。

---

### 4) refine 怎么做（候选生成 → 谓词过滤 → 选择 → 应用）

#### 4.1 候选生成：沿 `NamerLattice` 往更细走

`Namer` 的 type bitmask 构成 lattice；从当前 naming 出发，可以拿到一组“更细一跳”的候选：

```text
currentNaming
  └─ candidates = immediateRefinements(currentNaming)   // lattice neighbors
```

#### 4.2 谓词过滤：哪些候选是“可接受”的？

候选 `Naming` 需要通过一组与 assertions 语义相近的检查：

- **StatesFewerThan / explosion guard**：候选 naming 下 distinct state 数不能超过阈值（避免爆炸）
- **SourceDivergent / ActionDivergent**：refine 必须能“消解触发 ND 的分区发散”，否则是无效 refine
- **Blacklist**：rollback 过的 fingerprint 进入黑名单（防止来回震荡）
- （可选）**Replay**：用缓存 XML 重放部分证据集合，在候选 naming 下重新算 key 来打分/过滤

#### 4.3 选择：如何挑 bestCandidate？

常见目标是“解决 ND，同时尽量少扰动/少增 states”，因此会综合：

- ND 触发 pair 的 fan-out 是否下降
- distinct state 数/targets 数是否可控
- affected states / affected transitions 是否过大

最终选 `bestCandidate`，更新 `activityKey -> Naming`，并记录“上一次 naming/触发证据/阈值”等用于后续 rollback gate。

---

### 5) coarsen/rollback 怎么做（何时认为 refine “拆过头/无效”）

refine 后会进入观察窗口，如果出现以下情况会 rollback：

- **overTargets / overAffected**：新 naming 导致 targets/影响 states 超阈值（拆过头）
- **unresolved trigger pair**：触发 ND 的 `(S,A)` 在新 naming 下仍然明显发散（refine 无效）

rollback 动作：

- `activityKey -> previousNaming`
- finer fingerprint 加入 blacklist（防抖）
- 清理/裁剪与本次 refine 绑定的临时统计与谓词队列

---

### 6) 通俗示例：登录页为什么会触发 refine？为什么又可能 rollback？

Activity = `LoginActivity`，同样“点击登录”：

- 未输入用户名 → 弹 toast
- 已输入用户名 → 进入主页

若当前 naming 太粗，把两种页面合并成同一个 sourceKey：

```text
(S, click_login) -> T_toast
(S, click_login) -> T_home
```

则 ND 触发 refine，尝试引入更区分的 XPath 命名（更强 parent/ancestor 约束、patch predicates 等）把 source 拆成 `S1/S2`。

如果候选 naming 过细，把动态列表项等拆得过头导致 distinct states 激增或影响面过大，则触发 rollback 回到更粗的 naming，并拉黑该 fingerprint。

---

### 7) 流程图

#### 7.1 总体：收集证据 → refine →（必要时）rollback

```text
┌──────────────────────────────────────────────┐
│                 每一步执行                   │
│  build GUITree → StateKey/Naming → transition │
└─────────────────────────┬────────────────────┘
                          │ 记录 (srcKeyHash, actionHash, tgtKeyHash)
                          ▼
┌──────────────────────────────────────────────┐
│             周期性 batch / 调度点             │
│  collect ND evidence per activity             │
└─────────────────────────┬────────────────────┘
                          ▼
┌──────────────────────────────────────────────┐
│ refineActivityApeNaming(activity)             │
│  - lattice 生成候选 naming                     │
│  - predicates / replay / blacklist 过滤        │
│  - 选 bestCandidate 应用                         │
└─────────────────────────┬────────────────────┘
                          ▼
┌──────────────────────────────────────────────┐
│ coarsen/rollback gate (for refined activities)│
│  - overTargets / overAffected / unresolvedPair │
│  - rollback + blacklist finer fingerprint      │
└──────────────────────────────────────────────┘
```

#### 7.2 refine 内部：候选生成与过滤

```text
currentNaming
    │
    ├─ candidates = latticeRefinements(currentNaming)
    │
    ├─ for cand in candidates:
    │      - explosion guard? (states fewer than)
    │      - does it fix divergent pair? (source/action divergent)
    │      - blacklisted fingerprint?
    │      - (optional) replay score
    │
    └─ pick best cand → update activity->Naming
```

---

### 8) 代码入口（便于对照实现）

- APE refine/coarsen 主流程：`android/native/model/Model.cpp`
- Naming / lattice / evaluate：`android/native/desc/naming/NamingFactory.*`、`NamerLattice.*`
- StateKey：`android/native/desc/naming/StateKey.*`
- Naming manager：`android/native/desc/naming/ActivityNamingManager.*`、`StateNamingManager.*`
