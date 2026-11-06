# 舱内能量响应诊断

本工具独立扫描窄能带，定位舱内统计不足与哪些入射能量相关。它不改变 Geant4 物理，也不将窄能带点插值折算成任务总剂量。原响应矩阵要求各粒子共用同一能量网格；此诊断分别运行电子与质子能点，避免对现有不同网格的任务谱作隐式重分箱。

## 固定设置

使用[代表性整星装配](SPACECRAFT_TID_BENCHMARK.md)：16 个材料体、6 个敏感区，固定内腔比较 0.8 和 2.0 mm 铝壳。电子中心能量 0.1、0.5、1、2、3 MeV；质子 1、5、10、20、50、100 MeV，每个窄能带为中心能量 ±0.1%，带内线性常数谱。每项 100,000 历史，共 22 个任务、2,200,000 历史、132 条目标／能点／壁厚结果。

每个源的全向标量注量为 1 cm⁻²，源球半径仍为 390 mm。因此 Worker 的预期入射数为 πR(cm)²；表中响应为单位全向注量的 Gy。工具也保存每个源球初级粒子的剂量系数、全部沉积与非电离沉积响应。初级入靶计数和所有粒子产生的沉积计数分别记录，不能将两者混为一谈。

## OBC 的实际观察

| 壁厚 mm | 粒子 / MeV | 初级入靶历史 | 非零电离沉积历史 | 观测 RSE |
| ---: | --- | ---: | ---: | ---: |
| 0.8 | 电子 / 0.5 | 0 | 2 | 84.4% |
| 0.8 | 电子 / 2 | 165 | 180 | 8.70% |
| 0.8 | 质子 / 10 | 0 | 0 | 未知 |
| 0.8 | 质子 / 20 | 156 | 156 | 8.49% |
| 2.0 | 电子 / 1 | 0 | 2 | 71.4% |
| 2.0 | 质子 / 20 | 0 | 0 | 未知 |
| 2.0 | 质子 / 50 | 331 | 336 | 6.43% |

低能电子存在未记录到初级入靶、但观察到非零沉积的情况，提示次级贡献仍需保留。低能质子的零观测不是剂量上界；100,000 历史对稀有次级过程可能仍不足。不同能点与壁厚采用独立种子；不以点间连线推断缺失能点，也不假设响应或剂量随壁厚单调。

## 外部射程量级对照

独立检索 [NIST PSTAR](https://physics.nist.gov/PhysRefData/Star/Text/PSTAR-t.html)，材料编号 013（Aluminum）。[NIST 定义](https://pml.nist.gov/PhysRefData/Star/Text/appendix.html)指出 CSDA 射程近似平均路径长度，忽略能损涨落；它不是硬穿透阈值，也不能限制二次辐射剂量。固定参考点、请求参数和原始响应 SHA-256 保存在 `examples/reference/pstar-aluminum-range.json`。

使用 Worker 铝密度 2.699 g/cm³，10 MeV 质子的 CSDA 射程约 0.632 mm，20 MeV 约 2.130 mm。OBC 所在路径至少涉及舱体与 0.5 mm 局部铝壳：两种配置的名义壁厚和为 1.3 与 2.5 mm。这个量级与所观察到的初级入靶变化一致，但不能将其当作复杂装配或次级剂量的独立物理验证。

## 复现与审计

~~~powershell
$cli = '.\dist\SpaceRad-statistics-review-local\spacerad_cli.exe'
$runtime = '.\dist\SpaceRad-statistics-review-local\runtime.json'
$src = 'reports/spacecraft-tid-reproduce/project.json'
$out = 'reports/energy-response-new'
python tools/run_energy_response.py run --cli $cli --project $src --settings examples/scans/spacecraft-bus-thickness.json --runtime $runtime --output-dir $out --histories 100000 --parallel 3
python tools/run_energy_response.py inspect --cli $cli --runtime $runtime --output-dir $out
python tools/check_energy_response_scaling.py --study-dir $out --cli $cli --runtime $runtime --output-dir "$out/scaling-check"
.\build-ae8ap8-venv\Scripts\python.exe tools/plot_energy_response.py --response "$out/response.json" --target OBC_Si --al-wall-context-mm 1.3 2.5 --output-dir "$out/figures"
~~~

输出目录须不存在。`response.json`、`response.csv` 和每个 `case-xxx/job/` 共同保留。离线检查重新调用 CLI 验证每个任务、输入快照 SHA、运行时身份、几何、物理注量、三通道原始矩、抽样粒子与能量，精确重建汇总 JSON 和 CSV。

新增真实归一化检查以同一50 MeV质子源、相同种子和历史数把注量放大8倍，并把 fluence 的上下文时长改为999 s：原始沉积矩、初级抽样和实际几何逐项不变，剂量和标准误均为8倍，未重复乘以时长。7项检查通过。这是软件归一化验证，不是独立物理实验。

本机最终数据：`reports/energy-response/spacecraft/response.json`，响应图与 PDF 位于 `reports/energy-response/figures-final/`，原始 NIST 响应位于 `reports/energy-response/reference/`。大档案与图像保留本地；绘图脚本可在装有 matplotlib 的环境复现。完整任务谱折合、能量网格收敛及舱内低能区域误差上界仍未验收。

Linux Worker CI 加入了同一流程的低预算配方（每项2,000历史），检查计划、档案重建和归一化；该 CI 配方不承担物理精度验收。本机已完整执行该配方。改变计划中的历史预算后，离线工具在读取子任务前拒绝。

绘图的铝壁厚量级线须由调用方明确提供；工具不会从任意 CAD 自动推断实际射线路径。参考图的质量射程按 2.699 g/cm³ 转换为毫米。
