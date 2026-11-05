# 整星装配 TID 基准：固定内腔铝壳对照

这个基准把有来源的轨道环境谱用于一套代表性整星装配，检查同一个 Worker 能否在同一几何中处理舱内外靶、重复部件和屏蔽变体。模型是可编辑的解析盒体装配，不是实物 CAD 或公开卫星的结构。

- 舱体外尺寸 350 × 270 × 210 mm，基线壁厚 0.8 mm；固定内腔，向外增厚至 2.0 mm。
- 舱内保留安装板、两个局部铝罩，以及 OBC、存储器、载荷、电源四个硅靶。两侧太阳翼由同一模板实例化，含铝背板、硅靶和二氧化硅盖层。共 16 个材料体、6 个计分靶。
- 采用 [NASA CDAWeb](https://cdaweb.gsfc.nasa.gov/registry/hdp/hapi/) 的 Van Allen Probe A 公开轨迹片段。轨道时间为 2014-01-01 00:00:18.368 至 00:11:39.264 UTC。28 个位置样本产生 AE8MIN 电子 0.1–3 MeV 和 AP8MIN 质子 1–100 MeV 的轨道平均谱；两个能段在此片段均有有效模型输出。该谱是模型计算，不是测量值，也不代表整圈或整任务。
- Geant4 11.4.3、FTFP_BERT_EMZ、0.1 mm 产生阈值。每个壁厚分别运行电子与质子，保留次级贡献。试跑每项 15 万历史，高预算每项 200 万历史；共 860 万历史。两档复用相同的确定性种子，预算对比用于观察统计量变化，不当作独立重复试验。

## 数值结果

下表为高预算的 TID ± 一倍蒙特卡罗标准误。每个来源至少 20 个非零沉积历史且合计 RSE ≤20%，才通过本基准的统计筛查。

| 硅靶 | 0.8 mm TID (Gy) | 2.0 mm TID (Gy) | 两种厚度能否比较 |
| --- | ---: | ---: | --- |
| OBC | 0.00503 ± 0.00331 | 0.00421 ± 0.00268 | 统计不足 |
| 存储器 | 0.01508 ± 0.00713 | 0.00473 ± 0.00419 | 统计不足 |
| 载荷 | 0.00368 ± 0.00148 | 0.00132 ± 0.00111 | 统计不足 |
| 电源 | 0.00668 ± 0.00189 | 0.00049 ± 0.00026 | 统计不足 |
| 左太阳翼 | 0.50671 ± 0.01341 | 0.50440 ± 0.01346 | 差值未超出 2 倍合成标准误 |
| 右太阳翼 | 0.49996 ± 0.01344 | 0.49045 ± 0.01318 | 差值未超出 2 倍合成标准误 |

两档预算均保留 12 条厚度／靶区结果。太阳翼合计 RSE 由约 9–10% 降至约 3%。舱内虽由多项零命中变为都有计分，部分工况每粒子种类只有个位数非零历史，因此不能用表中的点估计比较舱内屏蔽优劣。太阳翼位于外壳之外，两个厚度的差值未在当前统计精度下分辨。误差不包含轨道、环境模型、能段截断或几何的系统误差。

专项审计通过 35 项检查：用原始模型积分谱重建环境，离线核对扫描档案，确认每种粒子的暴露仅归一化一次、四个输运工况和六个靶区均存在。独立核对 Geant4 几何审计记录：固定内腔 348.4 × 268.4 × 208.4 mm，除舱壁外的实际放置与质量均不变，舱壁质量从 0.965 kg 增至 2.433 kg。这里的“审计通过”和“剂量统计充分”是两个分别记录的结果。

## 复现

先按[外部星历工作流](AE8_AP8_EPHEMERIS.md)准备模型环境和同一份 NASA 位置 CSV。以下从仓库根目录执行，将 `$ephemeris` 改为该 CSV 路径。示例工程中的合成谱仅用于打开和检查几何，正式基准由生成命令替换为模型谱。

```powershell
$ephemeris = 'reports/rbspa-reproduce/rbspa-geo-km.csv'
$dir = 'reports/spacecraft-tid-reproduce'
$cli = '.\dist\SpaceRad-0.1.6-local\spacerad_cli.exe'
$settings = 'examples/scans/spacecraft-bus-thickness.json'
.\build-ae8ap8-venv\Scripts\python.exe tools/generate_ae8ap8_example.py --ephemeris $ephemeris --frame GEO --position-unit km --max-gap-seconds 60 --electron-band-mev 0.1 3 --proton-band-mev 1 100 --base-project examples/projects/spacecraft-tid-assembly.json --output-dir $dir
& $cli scan --project "$dir/project.json" --scan-settings $settings --histories 150000 --output "$dir/pilot" --timeout-seconds 840
& $cli scan --project "$dir/project.json" --scan-settings $settings --histories 2000000 --output "$dir/production" --timeout-seconds 840
python tools/check_spacecraft_tid.py --environment-dir $dir --pilot "$dir/pilot" --production "$dir/production" --cli $cli > "$dir/acceptance.json"
```

可在桌面打开生成的 `project.json`，或打开 `production` 作为历史扫描。应完整保留输入星历、模型清单和两个扫描目录；复核脚本会再次读取原始谱、运行 CLI 离线核对并生成机器可读的质量报告。

下一步需要把抽样预算更有针对性地分给能穿透屏蔽的粒子，并验证加权合并的偏差与方差；同时将这一装配工作流扩展到代表性 CAD 输入。基准完成了整星级软件流程的实跑，仍未完成可用于工程决策的舱内剂量验收。
