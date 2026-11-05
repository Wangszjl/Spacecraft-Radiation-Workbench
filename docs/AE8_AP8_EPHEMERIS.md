# 外部星历驱动 AE8/AP8

用带时间戳的地心笛卡尔星历生成电子和质子环境谱。可选用既有 SpaceRad 工程作为几何和运行设置模板；生成器替换环境谱、暴露时长和工程标题，保留材料、几何、计分和运行参数。它不改变原工程。输出可用独立桌面工作台、嵌入式工作台或 CLI 打开。模型计算仍由外置 Python 工具完成。

## 输入和积分

输入 UTF-8 CSV，表头为 `utc,x,y,z`，每行一个采样点。例如：

```csv
utc,x,y,z
2014-01-01T00:00:18.368Z,-9.152e+03,9.523e+03,-6.661e+00
2014-01-01T00:00:41.064Z,-9.280e+03,9.522e+03,-2.724e+01
```

必须显式选择 `--frame GEO|GEI`、`--position-unit km|Re` 和 `--max-gap-seconds`。GEO 是地心地固地理坐标；GEI 指 [SpacePy/IRBEM 的 GEI](https://spacepy.github.io/coordinates.html)，不能把 J2000、TEME 或经纬高直接填入。km 以 6371.2 km/Re 换算。接受 2–4096 行、严格递增的 UTC 时间戳和最多六位小数；拒绝大于显式间隔阈值的间断、非法位置和跨 UTC 闰秒的区间。

工具在星历原采样点运行模型，以相邻能量阈值的积分通量差求各能箱通量，再按相邻时间点间隔用梯形法积分。仅使用第一点至最后一点的实际时长，不外推首尾，也不把不等间隔样本等权平均。当前工程元数据只接受整秒：有小数的端点保留在 `environment-manifest.json`、输入副本及逐时输出中，工程里相应时间字段留空，`duration_s` 保留实际时长。

默认能段为电子 0.1–4 MeV、质子 1–100 MeV；可显式缩小。若任一采样点的指定能段出现无效值、缺测或积分谱逆序，生成失败，不用零填补。缩小能段会舍去其余能量贡献，不能将结果视为完整剂量。

## NASA 公开轨迹复现

数据来自 [NASA CDAWeb HAPI](https://cdaweb.gsfc.nasa.gov/registry/hdp/hapi/)：Van Allen Probe A 的 `RBSPA_REL04_ECT-HOPE-MOM-L3@0` 数据集 `Position` 字段（GEO，km）。以下下载只取位置，不取实测粒子谱。按[模型工具安装步骤](AE8_AP8_EXAMPLE.md)准备独立 Python 环境。

```powershell
$dir = 'reports/rbspa-reproduce'
New-Item -ItemType Directory -Path $dir
$url = 'https://cdaweb.gsfc.nasa.gov/hapi/data?id=RBSPA_REL04_ECT-HOPE-MOM-L3%400&parameters=Position&time.min=2014-01-01T00:00:00Z&time.max=2014-01-01T00:12:00Z&format=csv'
curl.exe --fail --silent --show-error $url -o "$dir/nasa-hapi-original.csv"
if ($LASTEXITCODE -ne 0) { throw 'CDAWeb download failed' }
@('utc,x,y,z') + @(Get-Content "$dir/nasa-hapi-original.csv" | Where-Object { $_.Trim() }) |
    Set-Content "$dir/rbspa-geo-km.csv" -Encoding utf8
.\build-ae8ap8-venv\Scripts\python.exe tools/generate_ae8ap8_example.py --ephemeris "$dir/rbspa-geo-km.csv" --frame GEO --position-unit km --max-gap-seconds 60 --electron-band-mev 0.1 1 --proton-band-mev 1 30 --output-dir "$dir/model"
$cli = '.\dist\SpaceRad-0.1.6-local\spacerad_cli.exe'
& $cli scan --project "$dir/model/project.json" --scan-settings "$dir/model/scan-settings.json" --histories 500000 --output "$dir/model/scan" --timeout-seconds 600
python tools/check_ae8ap8_example.py --environment-dir "$dir/model" --scan-dir "$dir/model/scan" --cli $cli
```

要在自己的工程上运行，给生成命令增加 `--base-project <工程路径>`。这时不会生成示例的扫描设置；可先运行 `spacerad_cli validate`，然后运行普通输运或提供适用于该几何的扫描设置。只核对模型输出时，调用验收工具仅传入 `--environment-dir`。

## 本地验收

本次取得 28 个不等间隔采样点，UTC 2014-01-01 00:00:18.368 至 00:11:39.264，覆盖 680.896 s。时间加权平均带内通量为电子约 3.9152×10⁷、质子约 1.2037×10⁷ cm⁻² s⁻¹。默认电子能段存在无效输出；以明确缩小的能段计算后，两种粒子各 924 个积分通量值均有效。

几何沿用演示用铝外壳和两个硅靶，**不是 Van Allen Probe A 的结构**。Geant4 11.4.3 共运行 200 万个历史，模型到扫描档案通过 25 项检查。同一条谱替换进原有 v7 体素工程后，15 万历史的真实 Worker 运行保留了 2×2×2 计分。原理想轨道工作流也通过回归；本地 CTest 41/41 通过。

| 外壳厚度 (mm) | OBC 硅靶 TID (Gy) | 载荷硅靶 TID (Gy) |
| ---: | ---: | ---: |
| 0.5 | 0.02936 ± 0.00327 | 0.00265 ± 0.00108 |
| 2.0 | 0.00156 ± 0.00063 | 0.00054 ± 0.00036 |

误差为一倍蒙特卡罗标准误；较小的靶区剂量统计误差仍大。该算例覆盖的是一小段公开轨迹及指定能段，不能代表整圈、整任务或航天器实测剂量。

## 数值对照

- [SpacePy 手册的 AE8MAX 点](https://spacepy.github.io/autosummary/spacepy.irbempy.html#spacepy.irbempy.get_AEP8)：在 2017-02-02 12:00 UTC、GEO (3, 0, 0) Re、1 MeV，文档给出微分通量 1,932,209.442736；本机 GEO 与等价 GEI 坐标均得到 1,932,209.442735。
- 与 [RADBELT 的独立代码实现](https://github.com/jacobwilliams/radbelt/tree/50b5cb33fb00da3a86092ad191ff18e372fa9200)共享磁坐标 `L=2, B/B₀=1.2`，对照 AE8MIN 的 0.5、1 MeV 和 AP8MIN 的 10、30 MeV 积分通量。四点相对差均小于 10⁻⁶。此项只检验共享模型表的插值实现，不比较轨道磁场解或实测数据。

```powershell
# 从 SpaceRad 源码根目录开始；第三方源码留在被 Git 忽略的构建目录。
git clone https://github.com/jacobwilliams/radbelt.git build-radbelt-reference
git -C build-radbelt-reference checkout 50b5cb33fb00da3a86092ad191ff18e372fa9200
Push-Location build-radbelt-reference
gfortran -o point_compare.exe src/radbelt_kinds_module.F90 src/trmfun.f90 ../tools/reference/ae8ap8_radbelt_points.f90
.\point_compare.exe | Set-Content -Encoding ascii ../radbelt-points.csv
Pop-Location
.\build-ae8ap8-venv\Scripts\python.exe tools/check_ae8ap8_reference.py --radbelt-points radbelt-points.csv
```
