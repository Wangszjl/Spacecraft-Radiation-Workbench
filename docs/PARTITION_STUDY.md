# 全任务谱分层与独立种子复核

`tools/stratified_study.py` 在同一任务谱、几何、物理设置和 Worker 下比较两套完整能域分层，每套至少两个不同种子。各能段保留原始谱形和物理暴露，再相加得到任务谱剂量；未将窄能点响应插值折合。改变分层边界检验的是抽样稳定性，不是确定性响应网格的离散误差收敛。

本文记录 Python 统计复核工具、离线 HTML 报告及本地数值研究。后续增加的[原生桌面窗口与 C++ 服务](NATIVE_PARTITION_STUDY.md)支持计划配置、运行和聚合审查；本研究的既有 Python 档案仍按本文流程复现。任务谱来源和限制见[整星基准](SPACECRAFT_TID_BENCHMARK.md)，方法见[能段分层](STRATIFIED_TID.md)。

## 固定计划与实际计算

使用已生成的 AE8MIN/AP8MIN 公开轨迹片段工程，保留环境谱、680.896 s 暴露、390 mm 源球、16 个材料体和六个敏感靶。0.8 / 2.0 mm 舱壁保持固定内腔。

| 分层 | 电子能段 MeV | 质子能段 MeV | 每段、每厚度历史 |
| --- | --- | --- | ---: |
| coarse | 0.1–1 / 1–3 | 1–20 / 20–100 | 250,000 |
| fine | 0.1–0.5 / 0.5–1 / 1–2 / 2–3 | 1–8 / 8–20 / 20–50 / 50–100 | 125,000 |

每套分层各跑两次。每个粒子、每种壁厚、每次重复均为 500,000 历史；合计 **8,000,000 历史、48 个实际输运任务**。细分层严格包含粗分层切点，两套方案逐粒子预算相同。子任务种子均不同；这是独立运行的工程假设，不是随机数流严格不相交的数学证明。

本机输出为 `reports/partition-study/production/`，图像为 `reports/partition-study/figures/partition-study.png` 与 `.pdf`。逐任务档案、JSON、CSV、HTML 与 SHA-256 清单保留本地；大档案不上传 GitHub。

## 实际结果与统计限制

预先固定的经验筛查要求：**每个能段至少 20 个非零沉积历史、每段 RSE≤20%、合计 RSE≤20%**。通过只表示当前观测具有指定统计支持，不替代独立物理验证。

四个估计取等权平均，观测标准误按独立运行假设平方相加后除以四。只要任一估计存在零命中能段，平均估计的完整标准误和 RSE 均保留未知；已观测部分的标准误单独提供。下表不包含未观测贡献的剂量上界。

| 靶区 | 壁厚 mm | 平均估计 Gy | 全谱 RSE | 四次均通过经验筛查 |
| --- | ---: | ---: | --- | --- |
| OBC | 0.8 | 0.0093320 | 未知 | 否 |
| OBC | 2.0 | 0.0020008 | 未知 | 否 |
| 载荷 | 0.8 | 0.0051754 | 未知 | 否 |
| 载荷 | 2.0 | 0.0018437 | 未知 | 否 |
| 左太阳翼 | 0.8 | 0.4957796 | 3.43% | 是 |
| 右太阳翼 | 0.8 | 0.4714539 | 3.43% | 否 |
| 左太阳翼 | 2.0 | 0.4659255 | 3.39% | 是 |
| 右太阳翼 | 2.0 | 0.4725427 | 3.36% | 是 |

全部 12 个靶区／壁厚组合中，3 个四次均通过经验筛查，8 个舱内组合全部未通过。右太阳翼 0.8 mm 的合计 RSE 较小，但某次分层仍有能段级支持不足，因此不能仅凭合计误差勾选通过。

各估计两两比较，标记同一分层的种子重复与不同分层／种子的比较。双方通过全部能段筛查时，才报告差值除以合成经验标准误。本次有 21 对符合条件，最大为 1.79；这是多次比较下的描述量，不是显著性检验或正式收敛判定。

## 零命中概率与剂量上界

固定的 288 个靶区／能段／重复单元中有 56 个零沉积单元。对某段 N 次独立历史的零成功观测，使用二项分布单侧概率界：

`p_upper = 1 − (α / M)^(1/N)`

取 `α=0.05`、`M=288`，通过 Bonferroni 分配覆盖预先固定的整个单元族，不要求单元之间独立。实现使用对数与 `expm1` 避免大 N 的相消误差。N=250,000 / 125,000 时，概率上限约 **3.46×10⁻⁵ / 6.93×10⁻⁵**。

该界只约束条件能段内一次初级历史发生非零沉积的概率。没有单次沉积严重程度的独立界，不能换算成剂量上界；`dose_upper_bound_Gy` 均保留空值。零命中段也不能从任务谱中删除。独立历史是此处的统计假设。

逐段还报告沉积 ESS：`N / [1 + (N−1)×RSE²]`，由能量沉积一、二阶矩推导，用于识别少数大沉积事件主导的情况。它与源粒子权重 ESS 不同。

## 复现与复核

先按[整星基准](SPACECRAFT_TID_BENCHMARK.md)生成模型谱工程。默认切点要求电子覆盖 0.1–3 MeV、质子覆盖 1–100 MeV；源码中的合成占位工程不能直接套用。其他能域需提供完整覆盖的合法嵌套分层设置。

~~~powershell
$cli = '.\dist\SpaceRad-source-config-local\spacerad_cli.exe'
$runtime = '.\dist\SpaceRad-source-config-local\runtime.json'
$out = 'reports/partition-study-new'
python tools/stratified_study.py run --cli $cli --runtime $runtime --project reports/spacecraft-tid-benchmark/project.json --scan-settings examples/scans/spacecraft-bus-thickness.json --study-settings examples/scans/spacecraft-partition-study.json --output-dir $out --parallel 3
python tools/stratified_study.py inspect --cli $cli --runtime $runtime --output-dir $out
.\build-ae8ap8-venv\Scripts\python.exe tools/plot_partition_study.py --study-dir $out --output-dir "$out/figures"
~~~

输出目录必须不存在。绘图需要 matplotlib 与 numpy。`review.html` 无外部依赖、脚本或网络请求，可离线展开逐能段表格。图中细分支持采用两个重复的逐段最小非零计数，空心点表示支持不足；存在零命中段时不画完整误差棒。

离线检查从原工程重建总计划，逐个调用 CLI 验收子扫描，精确核对子汇总 JSON/CSV、原始结果 SHA、跨重复的种子唯一性、质量、同厚度真实几何和 Worker/runtime 身份，再逐字节复核总 JSON、CSV 和 HTML。不重新运行 Geant4 输运。

## 验收与后续清单

- [x] 9 项单元检查：谱积分与预算守恒、不修改源、拒绝非嵌套方案、零事件界、沉积 ESS、未知误差传播、导出转义及计划篡改。
- [x] 全桌面工程重新构建，47/47 CTests 通过。
- [x] 小型合成谱真实验收：64,000 历史、48 个工况、11 项检查通过，包括六类篡改拒绝和恢复后复核。证据：`reports/partition-study/runtime-check/summary.json`。
- [x] 800 万历史生产研究及档案重建，48 个任务全部通过，PNG/PDF 已目视检查。
- [ ] 舱内低能次级贡献的合格误差界；当前事件概率界不解决剂量严重程度。
- [ ] 有独立验证的输运方差缩减方案及精度／效率比较。
- [x] 全谱分层计划与聚合审查的[原生桌面配置工作流](NATIVE_PARTITION_STUDY.md)。
- [ ] 代表性 CAD、物理设置敏感性与独立物理基准。

Linux Worker CI 加入同一小型真实验收配方；托管结果以对应源码提交的 Actions 为准。当前研究说明单纯增加历史数的局限，后续需推进方差缩减或可辩护的独立沉积约束，并将计划与聚合审查接入桌面。
