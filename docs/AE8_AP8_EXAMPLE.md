# AE8MIN / AP8MIN 示例轨道与屏蔽分析

这个示例从实际运行的 [SpacePy/IRBEM AE8/AP8](https://spacepy.github.io/autosummary/spacepy.irbempy.html#spacepy.irbempy.get_AEP8) 取得俘获电子和质子谱，生成可直接在 SpaceRad 工作台打开的工程。轨道为理想圆轨道：地心惯性坐标 GEI、半径 1.6 个地球半径（约 3,823 km 高度）、倾角 15°、2025-01-01 00:00:00 UTC 起算，单圈 10,243 s。几何为铝外壳、两块硅靶和载荷局部铝壳。这个示例没有使用实测星历。

生成器通过 [IRBEM 的轨道接口](https://prbem.github.io/IRBEM/api/radiation_models.html#ae8-and-ap8-models) 选择太阳活动低年模型，计算每个时间中点的全向积分通量 `J(>E)`，并用 SpacePy 的公开单点接口对照每种粒子的三个轨道和能量位置。相邻阈值之差给出该能箱的通量；对等时间采样取平均、除以能箱宽度后，以 `bin_constant` 微分通量写入工程。这样不会把积分谱当作微分谱，也不会重复乘以暴露时长。所有采样点必须有有效、非负且随能量阈值不增的积分值；缺测不会补零。

| 模型 | 能量范围 (MeV) | 分箱数 | 轨道平均带内通量 (cm⁻² s⁻¹) | 单圈带内注量 (cm⁻²) |
| --- | ---: | ---: | ---: | ---: |
| AE8MIN 电子 | 0.1–4 | 32 | 4.8862×10⁷ | 5.0049×10¹¹ |
| AP8MIN 质子 | 1–100 | 32 | 6.2221×10⁵ | 6.3733×10⁹ |

以 192 个时间中点生成工程。增至 384 个中点时，两种粒子的带内通量相对变化不超过 0.012%，单箱相对变化不超过 0.028%；这是时间采样敏感性检查，不包含能量分箱、模型系统误差或轨道误差。

## 生成与运行

从源码根目录执行；Python 模型环境单独放在 `build-ae8ap8-venv`，不会改变 Geant4 SDK 或桌面运行时。Windows 验收使用 Python 3.14、SpacePy 0.7.0、NumPy 2.5.4、Geant4 11.4.3。

```powershell
python -m venv build-ae8ap8-venv
.\build-ae8ap8-venv\Scripts\python.exe -m pip --isolated install --index-url https://pypi.org/simple --only-binary=:all: spacepy==0.7.0
.\build-ae8ap8-venv\Scripts\python.exe tools/generate_ae8ap8_example.py --output-dir reports/ae8ap8-example

$case = 'reports/ae8ap8-example'
$cli = '.\dist\SpaceRad-0.1.6-local\spacerad_cli.exe'
& $cli validate --project "$case/project.json"
& $cli scan --project "$case/project.json" --scan-settings "$case/scan-settings.json" --histories 500000 --output "$case/scan" --timeout-seconds 600
python tools/check_ae8ap8_example.py --environment-dir $case --scan-dir "$case/scan" --cli $cli
```

生成目录和扫描输出目录须不存在。CLI 默认读取同目录的 `runtime.json`；用其他构建时可以为扫描指定 `--runtime`。也可在桌面打开生成的 `project.json`，在环境谱页查看两种谱，再用“自动屏蔽厚度扫描”选择 `Shield_Al`、0.5 和 2 mm。优先用分粒子扫描比较屏蔽方案：这个例子的电子积分通量约为质子的 79 倍，混合源按通量抽样时质子历史少得多。

生成目录保留逐时轨迹、每种粒子的逐时积分谱、工程、扫描设置，以及模型库与输入文件的 SHA-256。离线验收工具不依赖 SpacePy：它核对原始谱哈希、由原始值重建工程每个能箱，再通过 CLI 验证扫描档案与逐粒子暴露归一化。第三方模型库和生成的大文件只留在本地环境。

## 本地输运结果

固定内腔，扫描两种外壳壁厚；载荷局部铝壳保持 1 mm。每个厚度 / 粒子组合 50 万历史，共 200 万。下表为单圈 TID 和一倍蒙特卡罗标准误，电子和质子贡献按现有[扫描规则](SHIELDING_SCAN.md)合成。

| 外壳厚度 (mm) | OBC 硅靶 (Gy) | 载荷硅靶 (Gy) |
| ---: | ---: | ---: |
| 0.5 | 1.1265 ± 0.0330 | 0.3493 ± 0.0202 |
| 2.0 | 0.2653 ± 0.0157 | 0.1826 ± 0.0137 |

5 万与 50 万历史 / 项的两套本地档案均通过 22 项离线检查。普通混合谱 `run` 也完成，50,000 个历史中只有 647 个质子历史。低预算扫描中，2 mm 外壳的载荷靶出现电子零命中；高预算下为 20 次非零电子沉积。低预算的零估计不能解释为没有电子剂量。表中误差不包含模型、几何或物理过程的系统误差。同一环境重复生成时，轨道、原始谱、工程和环境清单逐字节一致。

这个算例没有高能尾部外推，也未计入太阳粒子事件、银河宇宙射线或入射方向分布。现在也可使用[外部星历](AE8_AP8_EPHEMERIS.md)，该流程另有 NASA 公开轨迹实跑和跨实现点对照。
