# 全谱重要性抽样与验证

schema v8 提供可选的初级粒子能量和粒子种类提议分布。通过对完整初级历史及其次级沉积乘一次似然权重，恢复原始物理谱和通量／注量。源球空间和方向分布、Geant4 相互作用过程均保持不变。

## 配置与计分

~~~json
{"schema":"spacerad.source-importance/1","physical_fraction":0.9,"tail_fraction":0.05,"proposal_species_fractions":{"e-":0.15,"proton":0.85}}
~~~

该对象写入工程顶层 `source_sampling`。对粒子 i，原始积分份额为 pᵢ、提议份额为 qᵢ，粒子权重为 pᵢ/qᵢ。未设置 `proposal_species_fractions` 时保留原始粒子份额。显式配置必须精确覆盖源中的电子／质子，所有概率为正且和为 1。

设原始归一化能谱为 Pᵢ、原始能谱混合率为 f、最高能尾部的物理概率为 τ。提议为 Qᵢ=fPᵢ+(1−f)Pᵢ(· | tail)，尾部抽样概率为 1−f+fτ。主体与尾部能量权重分别为 1/f 和 τ/(1−f+fτ)。事件总权重是粒子和能量权重的乘积。允许 0.05≤f<1、0.00001≤τ≤0.5。尾部由每种谱各自的积分分位确定，保留原有线性／分箱常数插值。

各区域保存“入射粒子 × 主体／尾部”的条件历史数及三个沉积通道的均值、M2、非零计数。按条件历史分数重建全事件均值和二阶矩，核对报告剂量、经验标准误、原始物理暴露、样本权重和与权重平方和。物理暴露不按权重和二次归一化。源权重 ESS 不能替代目标或来源区域的有效沉积历史。

报告标准误只描述已观察到的加权事件。如果某个粒子／能量区域命中不足，它不是未观测贡献的上界。必须检查每个分区，不能仅看合计 RSE。

## 使用

普通 CLI `run`、独立和嵌入工作台可打开、保存和运行 v8 工程；桌面显示已加权提示，并可在[计算参数编辑器](SOURCE_SAMPLING_EDITOR.md)配置提议。体素、旧版批处理／响应缓存及 M1 自动审计不支持此模式。旧 `scan` 规划器复用批处理规则，加权厚度扫描使用 `run_importance_scan.py`，每个厚度运行一个完整混合源任务。

先按[整星基准](SPACECRAFT_TID_BENCHMARK.md)生成模型谱工程。示例使用新源码构建或本机 `dist/SpaceRad-importance-20261105/` 预览包；Geant4 数据仍外置，尚不是经过跨机验收的正式安装包。

~~~powershell
$dir = 'reports/importance-reproduce'
New-Item -ItemType Directory -Path $dir
$src = 'reports/spacecraft-tid-reproduce/project.json'
$cli = '.\dist\SpaceRad-importance-20261105\spacerad_cli.exe'
$runtime = '.\dist\SpaceRad-importance-20261105\runtime.json'
$settings = 'examples/scans/spacecraft-bus-thickness.json'
python tools/enable_importance.py --project $src --output "$dir/weighted-project.json" --physical-fraction 0.9 --tail-fraction 0.05 --species-fraction e- 0.15 --species-fraction proton 0.85 --seed 91726345
& $cli scan --project $src --scan-settings $settings --runtime $runtime --output "$dir/direct" --histories 2000000 --timeout-seconds 840
python tools/run_importance_scan.py --cli $cli --project "$dir/weighted-project.json" --scan-settings $settings --runtime $runtime --output-dir "$dir/weighted" --histories-per-thickness 4000000 --timeout-seconds 840
python tools/compare_importance_scans.py --cli $cli --direct "$dir/direct" --weighted "$dir/weighted" --output "$dir/comparison.json"
~~~

直接方法按两种厚度、两种粒子、每项 200 万，共 800 万历史。加权混合源按两种厚度、每项 400 万，共 800 万历史。不要将分粒子预算与混合源总预算混淆。输出目录必须不存在。每个加权结果均是普通可验收任务；比较脚本重建固定内腔计划并重新调用 CLI 离线核对每个子任务，同时核对原始文件哈希、几何、暴露、预算和运行时身份。

## 最终本地对照

最终证据为 `reports/spacecraft-importance/final-accepted-pair/comparison.json`：相同公开轨迹片段、同一 Worker、相同几何，独立种子，各 800 万历史，12 个靶区／厚度结果。下列加权误差均为经验 RSE，不是漏采区域风险的上界。

| 靶区 | 壁厚 mm | 直接 TID Gy | 加权 TID Gy | 加权经验 RSE |
| --- | ---: | ---: | ---: | ---: |
| OBC | 0.8 | 0.0095329 | 0.0073051 | 27.0% |
| 载荷 | 0.8 | 0.0045364 | 0.0040572 | 17.2% |
| OBC | 2.0 | 0.0019719 | 0.0031367 | 39.3% |
| 载荷 | 2.0 | 0.0026834 | 0.0015917 | 30.9% |
| 左太阳翼 | 0.8 | 0.4854201 | 0.5103822 | 1.52% |
| 右太阳翼 | 2.0 | 0.4990855 | 0.4867211 | 1.53% |

四个太阳翼结果通过“每个来源区域至少 20 次非零沉积、合计 RSE≤20%”筛查；八个舱内结果全部未通过。所有舱内靶的主体谱质子仍零命中。增加质子份额和扩大尾部并未消除此限制，不足以作屏蔽选型。此前窄尾部试验虽出现更小经验误差，也没有获得统计验收，不能据此选择参数。

干净构建本地 CTest 43/43 通过。解析检查覆盖 v8 roundtrip、源权重恢复物理谱及粒子份额、条件区域矩合并、非法配置和篡改拒绝。打包版真实混合电子／质子检查 7/7 通过，包括剂量对照、粒子与尾部抽样频率和离线篡改拒绝。Linux Worker CI 已加入同一小型检查；托管结果以 Actions 状态为准。

最终重建后的预览包另通过原体素真实 Worker 回归 13 项及原生桌面烟雾流程 9 项（保存／重开、接受真实结果、取消和运行中销毁）。这些是既有流程回归，不是 v8 配置界面的专门验收。本轮没有重新验收实际 TKMaster 宿主；源模型结构增加字段后，SDK 消费者和宿主需要重新编译。

这一阶段交付的是加权输运、分区审计和可复现对照。完整舱内剂量精度、外部物理验证、CAD 整星验收和器件 DDD/SEE 仍未完成。
