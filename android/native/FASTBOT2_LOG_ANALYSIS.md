# fastbot2.log 分析报告（dumpBinary / dumpXml 耗时）

基于 **fastbot2.log** 中带 `event time breakdown` 与 `// dumpXml: N ms` 的日志，对 **dumpBinary** 与 **dumpXml** 耗时的统计与结论。

**说明**：若 log 为 **treeDumpMode=xml** 运行（仅 dumpXml），见 **第六节**；若为 **binary** 运行（先 binary 再 XML 回退），见第二–五节。

---

## 一、样本范围（binary 运行时）

- **总 event 数**（含 `event time:` 的条数）：约 **2,533** 条。
- **breakdown 条数**：仅当 event time > 200 ms 或 getRootSlow / dumpXml 显著时打印，共 **299** 条。
- **XML 回退**：binary 运行时本 log 中**无** “fallback to XML”，dumpXml 在 breakdown 内均为 0。

---

## 二、dumpBinary 耗时（breakdown 样本内，299 条）

| 指标 | 数值 |
|------|------|
| **样本数** | 299 条（均有 dumpBinary） |
| **总和** | 119,300 ms |
| **平均值** | **399 ms** |
| **最小值** | 0 ms |
| **最大值** | **6,435 ms** |
| **P50（中位数）** | 308 ms |
| **P90** | 652 ms |
| **P95** | 761 ms |
| **P99** | 1,707 ms |

结论：在“高 event time”样本内，dumpBinary 平均约 **399 ms**，单次最大 **6,435 ms**（约 6.4 s），是 event 耗时的主要来源之一。

---

## 三、dumpXml 耗时（breakdown 样本内）

| 指标 | 数值 |
|------|------|
| **dumpXml > 0 的条数** | **0** |
| **平均（仅当 >0）** | — |
| **单次最大** | 0 ms |

结论：本 log 中**未触发 XML 回退**，所有 299 条 breakdown 的 dumpXml 均为 0，无法在本 log 内对比 dumpXml 与 dumpBinary 的耗时。

---

## 四、与 fastbot1.log（单遍优化前）对比（breakdown 样本）

| 阶段 | fastbot1.log（348 条） | fastbot2.log（299 条） |
|------|------------------------|-------------------------|
| **dumpBinary 平均** | 413 ms | **399 ms** |
| **dumpBinary 最大** | 3,920 ms | **6,435 ms** |
| **dumpXml >0 条数** | 161 条 | 0 条 |
| **dumpXml 平均（>0）** | 291 ms | — |

说明：

- fastbot2 为**单遍 binary**（占位 + 回填）实现；fastbot1 为**两遍**（先数 visible 再 dump）。
- dumpBinary 均值略降（413 → 399 ms），样本与场景不同，仅作参考。
- dumpBinary 极值在 fastbot2 中更高（6,435 ms），可能与大树或设备状态有关，需结合同场景多轮 log 对比。
- 本 log 未出现 XML 回退，无法在本文件中得到 dumpXml 与 dumpBinary 的耗时对比。

---

## 五、小结（binary 运行）

| 项目 | 结论 |
|------|------|
| **dumpBinary（breakdown 内）** | 299 条均有，平均 **399 ms**，最大 **6,435 ms**，P99 约 1.7 s。 |
| **dumpXml** | binary 运行时 **0 条** 走 XML；若需 dumpXml 数据见第六节（treeDumpMode=xml）。 |

---

## 六、dumpXml 耗时（treeDumpMode=xml 运行）

当 **max.treeDumpMode=xml** 时，每轮只做 `dumpDocumentStrWithOutTree`，不打 binary；log 中为 `// dumpXml: N ms (treeDumpMode=xml)` 与 breakdown 内 `dumpBinary=0 dumpXml=N`。

### 6.1 全量样本（所有带 dumpXml 的 event）

| 指标 | 数值 |
|------|------|
| **样本数** | **753** 条 |
| **总和** | 47,822 ms |
| **平均值** | **63.5 ms** |
| **最小值** | 0 ms |
| **最大值** | **3,560 ms** |
| **P50（中位数）** | 1 ms |
| **P90** | 130 ms |
| **P95** | 322 ms |
| **P99** | 987 ms |

结论：在 **treeDumpMode=xml** 下，dumpXml 全量平均约 **63.5 ms**，中位数 1 ms（大量简单界面很快），单次最大 **3,560 ms**（约 3.6 s），P99 约 1 s。

### 6.2 breakdown 样本（高 event time 子集，195 条）

| 指标 | 数值 |
|------|------|
| **样本数** | 195 条 |
| **平均值** | **229 ms** |
| **最小值** | 0 ms |
| **最大值** | **3,560 ms** |

结论：在打印了 breakdown 的“高耗时”样本内，dumpXml 平均 **229 ms**，极值 **3.56 s**。

### 6.3 dumpXml vs dumpBinary（同 log 或同场景对比参考）

| 阶段 | 全量平均 | breakdown 内平均 | 单次最大 |
|------|----------|------------------|----------|
| **dumpXml（本节，treeDumpMode=xml）** | 63.5 ms | 229 ms | 3,560 ms |
| **dumpBinary（第二节，binary 运行）** | — | 399 ms | 6,435 ms |

在“高 event time”样本内，**dumpXml 平均（229 ms）低于 dumpBinary（399 ms）**；dumpXml 极值（3.56 s）也低于 dumpBinary（6.4 s），与 FASTBOT1_LOG_ANALYSIS §2.3 的“Java 侧 XML 常快于 binary”结论一致。
