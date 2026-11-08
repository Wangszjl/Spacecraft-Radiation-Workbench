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
spacerad_cli inspect-result --job new-job
spacerad_cli fold-local-response --job new-job --response device-response.csv
```

桌面可直接点击“靶区注量输运”，选择敏感区、能箱边界和新任务目录。配置随输运档案保存，不修改项目文件。完成后点击“靶区注量与局部响应”，界面自动填入刚完成的档案路径；也可手动选择历史输运档案。表格显示能箱注量、标准误和非零历史数。选择响应 CSV 后点击“折合局部响应”，可导出包含档案身份、结果和范围的 JSON。

运行档案保存设置原始字节及 SHA-256。实时验收与离线复核按原始矩重算归一化、标准误和总量，核对几何体积及粒子／分箱布局。SDK 可通过 `AnalysisService::start(project, directory, settings)` 启动，通过 `loadArchivedRun` 读取，并用 `foldTargetFluenceResponse` 折合；Qt 宿主可用 `WorkbenchWidget::showTargetFluence(archive)` 打开核验界面。

局部折合复用[器件响应 CSV](DEVICE_RESPONSE.md)的单位及来源字段。所有曲线必须对应被计分的靶区，且覆盖每个有观测贡献的粒子和能箱；发现下溢、上溢或其他粒子的非零贡献就拒绝。无非零历史时结果未知。对于有观测值的分箱，采用箱内均匀密度给出中心估计，用曲线在箱内的最小／最大值给出观测贡献的响应范围。该范围不是置信区间，也不约束漏采贡献；局部响应总标准误尚未估计。

这是体积平均、步前能量分箱下的屏蔽后响应筛查。使用用户声明的角度平均响应；不推断方向依赖、箱内谱形、步长或能量网格收敛，也不替代 DDD/DPA 或 SEE 器件级物理标定。

验收脚本 `tools/check_target_fluence.py` 使用真空球的解析弦长均值与方差验证注量和标准误，并覆盖重要性抽样、铝壳透射、合成常数响应、覆盖拒绝及档案篡改拒绝。铝壳工况检验软件链路，未作实测屏蔽基准。
