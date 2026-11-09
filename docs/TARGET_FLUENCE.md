# 靶区能量分箱注量与器件响应

普通输运可选对一个敏感部件增加体积平均标量注量计分。它按粒子种类和步前动能累计实际 Geant4 步长，包含在该体积内产生或停止的粒子及外部进入的粒子。屏蔽作用来自实际输运几何。测量量不是过界计数、表面方向通量或 TID。

计分以 Geant4 的[体积轨迹长度计分定义](https://geant4.web.cern.ch/documentation/dev/bfad_html/ForApplicationDevelopers/Detector/hit.html)为基础。每步使用 Geant4 步前权重，初级历史的抽样似然权重只对整段历史应用一次。将每历史加权长度均值乘以工程预期初级数并除以靶区体积；若使用 mm 和 mm³，最终乘 100 得到 cm⁻²。每个能箱及全体积总量均保留包括零历史的均值和 M2。全量标准误直接来自完整历史的总长度，不能通过各能箱标准误简单相加得到。

设置 JSON 示例：

```json
{
  "schema": "spacerad.target-fluence-settings/1",
  "target_id": "OBC_Si",
  "energy_edges_MeV": [0.000001, 0.1, 0.5, 1, 10, 30, 100]
}
```

当前支持普通单次输运，可与重要性抽样同时使用；暂不与体素计分组合。允许 2–129 个严格递增的能量边界。记录电子、正电子、质子、α、光子和中子，以及独立的能域下溢／上溢和其他粒子。每箱是区间积分注量（cm⁻²），不是单位 MeV 的密度。

```powershell
spacerad_cli run --project project.json --runtime runtime.json --fluence-settings fluence.json --output new-job
spacerad_cli run --project project.json --runtime runtime.json --fluence-settings fluence.json --history-response device-response.csv --output response-job
spacerad_cli run --project project.json --runtime runtime.json --fluence-settings fluence.json --history-response device-response.csv --identify-other-species --output diagnostic-job
spacerad_cli inspect-result --job diagnostic-job
spacerad_cli fold-local-response --job new-job --response device-response.csv
```

桌面可直接点击“靶区注量输运”，选择敏感区、能箱边界和新任务目录。“逐历史响应（可选）”可在启动前选定器件响应 CSV。设置随输运档案保存，项目文件继续保留原有配置。完成后点击“靶区注量与局部响应”，界面自动填入刚完成的档案路径；也可手动选择历史输运档案。表格显示能箱注量、标准误和非零历史数。若输运时指定了响应表，核验档案后可直接查看逐历史响应、蒙特卡罗标准误和覆盖状态，并导出带档案身份的 JSON。也可另选响应 CSV，点击“离线能箱折合”复用已有分箱注量。

运行档案保存设置原始字节及 SHA-256。实时验收与离线复核按原始矩重算归一化、标准误和总量，核对几何体积及粒子／分箱布局。SDK 可通过 `AnalysisService::start(project, directory, settings)` 启动，通过 `loadArchivedRun` 读取，并用 `foldTargetFluenceResponse` 折合；Qt 宿主可用 `WorkbenchWidget::showTargetFluence(archive)` 打开核验界面。

局部折合复用[器件响应 CSV](DEVICE_RESPONSE.md)的单位及来源字段。所有曲线必须对应被计分的靶区，且覆盖每个有观测贡献的粒子和能箱；发现下溢、上溢或其他粒子的非零贡献就拒绝。无非零历史时结果未知。对于有观测值的分箱，采用箱内均匀密度给出中心估计，用曲线在箱内的最小／最大值给出观测贡献的响应范围。该范围不是置信区间，也不约束漏采贡献；离线更换响应表后，分箱边际统计无法恢复箱间协方差，因此离线折合的总标准误保持未知。

## 识别其他粒子

桌面启动靶区注量时自动保存其他粒子明细。CLI 可使用 `--identify-other-species`，SDK 可传入 `spacerad.target-fluence-settings/3`。该设置接受可选的 `response_table`，输出的靶区注量字段使用 `spacerad.target-fluence/2`。原 /1 和 /2 设置及既有档案仍可读取。

`other_species_detail` 按 Geant4 粒子名、PDG 编码和 `bin_index` 保存实际有贡献的行。与常规粒子使用同一能箱边界，索引 0 表示下溢，索引等于边界数时表示上溢，其余索引 i 表示 `[edge[i−1], edge[i])`。行中的 `score` 包含逐历史矩、注量和标准误。粒子名和 PDG 编码直接来自 Geant4 的粒子定义。 [Geant4 粒子定义手册](https://geant4.web.cern.ch/documentation/pipelines/master/bfad_html/ForApplicationDevelopers/TrackingAndPhysics/particle.html)

明细是 `other_species` 汇总的分解。各行注量可求和，非零历史数或标准误需按相关性理解，同一初级历史可能在多个明细行出现。档案核验检查注量守恒、历史计数、二阶矩可行性及能箱边界。明细最多 4096 个非零粒子／能箱组合，超出时任务报错。旧档案若只保存汇总，需重新输运才能获得明细。

其他粒子仍属于响应未覆盖项。新明细用于定位所缺的响应数据；逐历史响应和离线折合继续使用现有六类粒子的曲线规则。双预算工具在原有未覆盖汇总项下附上 `identified_bins`，并保留完整响应为空的判定。

## 输运时固定响应表

设第 `i` 个初级历史在分箱 `b` 的步长之和为 `L_{ib}`，其中已应用 Geant4 步前权重。令 `c_b` 为完全覆盖该分箱的响应曲线在箱内的平均值，`w_i` 为初级历史的抽样似然权重。Geant4 计算每个历史的 `Y_i = w_i Σ_b c_b L_{ib}`，再对包括零贡献在内的所有历史累计均值和 M2。工程预期初级数及靶区体积用于归一化响应均值与样本均值标准误。先合并同一历史中的粒子和能箱贡献，可以保留这些贡献的协方差。统计方法参见 [Walters 等的逐历史方差估计](https://pubmed.ncbi.nlm.nih.gov/12512706/)。

SDK 可用 `spacerad.target-fluence-settings/2`，在原设置字段之外传入 `response_table`，其值为 `DeviceResponseTable::toJson()` 的规范化响应表，包含 CSV SHA-256、曲线和来源字段。设置快照最多 64 KiB，随结果归档。读取 `target_fluence.history_response` 可取得响应值、`standard_error`、响应表身份和覆盖状态。旧版 `/1` 设置继续用于仅计分注量的任务。

若观测到响应表未完全覆盖的粒子或能箱，完整响应值和标准误均为空，并报告未覆盖的观测能箱数和注量。完全未观测到靶区注量时，两者也为空。若已观测到注量且所有覆盖贡献的响应都为零，报告观测响应值为零，标准误保持未知。

`covered_moments` 保留响应系数加权后的历史矩，其中沿用了注量计分的长度字段名，实际单位还需乘以响应系数的单位。档案复核从各箱均值校验响应均值，根据已归档的逐历史矩重算标准误，并用各箱标准误的加权和核对上界。各箱边际矩本身无法独立重建完整历史的 M2。报告的标准误只量化此次输运抽样，器件标定、环境模型和能量分箱误差需分别评估。

这是体积平均、步前能量分箱下的屏蔽后响应筛查。使用用户声明的角度平均响应；不推断方向依赖、箱内谱形、步长或能量网格收敛，也不替代 DDD/DPA 或 SEE 器件级物理标定。

验收脚本 `tools/check_target_fluence.py` 使用真空球的解析弦长均值与方差验证注量和标准误，并覆盖重要性抽样、铝壳透射、合成 DDD 与 SEE 响应、覆盖拒绝及档案篡改拒绝。双能箱真空算例使用可独立计算的协方差验证逐历史响应标准误。铝壳工况检验软件链路，未作实测屏蔽基准。

对同一响应表的多种子和双预算收敛筛查见[屏蔽后器件响应复核](HISTORY_RESPONSE_STUDY.md)。
