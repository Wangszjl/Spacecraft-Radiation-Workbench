# 屏蔽后器件响应的双预算复核

同一工程、靶区、能箱、响应表和 Geant4 运行环境下，用不同种子分别执行低预算和高预算输运。每次运行保留初级历史及其次级粒子的合并响应矩。研究工具检查同一档预算的种子差异，并从已核验的逐历史矩合并这一档的响应估计和标准误。

## 复核规则

- 每档至少两次重复，种子在两档之间也保持唯一。把不同种子的历史作为独立样本是本研究的统计假设。
- 两档都使用运行前固定的计划。默认要求同档每次运行至少有 20 个正响应历史、单次相对标准误（RSE）≤10%，且同档任意两次估计的差值 ≤3 倍合成标准误。高预算通过整体筛查还要求两档合并估计的差值 ≤3 倍合成标准误。该阈值用于描述性筛查。
- 同档所有运行均有完整估计时，合并均值按历史数加权，合并 M2 为各次 M2 之和加上 Σ Nᵢ(μᵢ−μ)²，标准误由合并的历史数和 M2 计算。报告同时给出种子间样本标准差和各次标准误的均方根。参见 [Chan、Golub 和 LeVeque 的样本方差合并方法](https://doi.org/10.1080/00031305.1983.10483115)。
- 任一次缺少完整估计时，该档合并值、标准误和跨预算差值保持未知。逐次记录未覆盖的粒子分组或能区、非零历史数和加权注量。旧档案只有其他粒子汇总组；启用 /3 计分设置后，缺失覆盖记录的 identified_bins 会列出 Geant4 粒子名、PDG 编码和能箱。
- 逐次报告已覆盖部分的响应有效历史数 ESS，按 N/[1+(N−1)×RSE²] 计算。少数历史主导响应时，ESS 可远低于非零历史计数。覆盖不完整时，ESS 的适用范围仍限于已覆盖部分。

## 整星算例结果

采用解析盒体整星装配的舱内 OBC 硅靶区、0.8 mm 铝舱壁和既有的 Van Allen Probe A 公开轨迹片段。暴露 680.896 s，AE8MIN 电子谱 0.1–3 MeV、AP8MIN 质子谱 1–100 MeV。源抽样采用 0.2 的物理谱混合比例和 0.001 的尾段比例。Geant4 11.4.3、FTFP_BERT_EMZ、0.1 mm 产生阈值。轨迹、环境模型生成方法与几何见[整星基准](SPACECRAFT_TID_BENCHMARK.md)。响应使用随仓库提供的合成 DDD 曲线，各粒子响应随能量变化。

| 每次初级历史 | 种子数 | 有完整估计的任务 | 可计算的单次 RSE | 档内合并结果 |
| ---: | ---: | ---: | --- | --- |
| 300,000 | 4 | 3/4 | 12.01%、12.93%、48.31% | 未知 |
| 1,200,000 | 4 | 1/4 | 6.01% | 未知 |

八个真实 Worker 任务共运行 600 万历史。四个任务的覆盖缺口均出现在“其他粒子”组，每次 2–6 个非零历史。低预算种子 1409251 有 266 个正响应历史，但响应 ESS 约为 4.3。在当前响应曲线和抽样设置下，两档均未通过预先固定的经验筛查。模型谱来自公开轨道片段，器件曲线和整星几何为算例输入，结论适用于本算例的覆盖与抽样诊断。

## 次级粒子诊断复跑

启用靶区明细后，原抽样设置和物理谱混合比例 0.8 的补充设置均按原种子复跑，共 16 个任务、1200 万历史。16 份新旧档案的 TID、靶区总注量、六类粒子的分箱注量、其他粒子汇总以及逐历史响应统计逐项一致。8 个原有的覆盖缺口均可定位为下表中的硅核素。

| Geant4 粒子 | PDG 编码 | 有贡献任务数 | 观测到的能箱 MeV |
| --- | ---: | ---: | --- |
| Si28 | 1000140280 | 8 | [0.01, 0.1)、[0.1, 0.5)、[0.5, 1) |
| Si29 | 1000140290 | 3 | [0.1, 0.5)、[0.5, 1)、[1, 2)、[3, 5) |
| Si30 | 1000140300 | 2 | [0.01, 0.1)、[0.1, 0.5)、[0.5, 1) |

一个任务可同时包含多个核素。这里记录的是实际步前动能所在的能箱；能箱端点可用于定位响应表需要覆盖的范围。现有合成响应表缺少这些核素，覆盖判定与原研究一致。本地复核档案与粒子汇总位于 reports/secondary-fluence。

## 复跑与离线核验

先按[整星基准](SPACECRAFT_TID_BENCHMARK.md)生成模型谱工程。使用具有逐历史响应功能的 CLI 和对应 runtime，替换示例路径。输出目录需为新目录。

~~~powershell
$cli = 'dist/SpaceRad-history-response-local/spacerad_cli.exe'
$runtime = 'dist/SpaceRad-history-response-local/runtime.json'
$dir = 'reports/history-response-study'
New-Item -ItemType Directory -Force $dir | Out-Null
python tools/enable_importance.py --project reports/spacecraft-tid-benchmark/project.json --output "$dir/weighted-project.json" --physical-fraction 0.2 --tail-fraction 0.001
python tools/history_response_study.py run --cli $cli --runtime $runtime --project "$dir/weighted-project.json" --fluence-settings examples/device-response/obc-fluence-settings.json --response examples/device-response/obc-convergence-synthetic-ddd.csv --histories 300000 1200000 --seeds 1409251 9812233 31618401 72460799 104729 20110159 61837477 178361109 --parallel 4 --output-dir "$dir/reproduce"
python tools/history_response_study.py inspect --cli $cli --runtime $runtime --output-dir "$dir/reproduce"
~~~

工具保存原始输入、固定计划、子任务档案和汇总 JSON。离线复核逐个调用 CLI 验收档案，校验输入和响应表 SHA-256、工程及运行环境身份，然后重建汇总并逐字节核对。报告中的筛查结果和档案核验是两个独立结果，统计不足的有效档案仍可通过核验。

- [x] 用显式历史样本独立核对合并方差，检查覆盖缺失的传播。
- [x] 用四个小型真实输运检查双预算、档案复核和篡改拒绝。
- [x] 运行八个模型谱任务并离线重建结果。现有本地证据位于 reports/history-response-study/ae8ap8-obc。
- [x] 对 16 个 OBC 任务按原种子复跑，查明原覆盖缺口的核素和能箱。
- [ ] 为观测到的硅核素确定可追溯的响应数据，并接入核素级响应折合。
- [ ] 在代表性 CAD、任务谱与已标定响应数据上验证收敛和物理结果。
