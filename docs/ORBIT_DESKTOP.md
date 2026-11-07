# AP8/AE8 星历环境桌面工作流

工作台的“AP8/AE8 星历环境”窗口使用独立 Python／SpacePy 0.7.0 进程运行 AE8MIN/AP8MIN，结束后由 C++ 从逐时积分通量复算微分谱、时间积分和物理暴露，再允许载入当前工程。独立桌面与嵌入式工作台使用同一窗口和 Qt Core 服务。

## 完成范围

- [x] 外部带时间戳 GEO／GEI 笛卡尔星历输入，显式位置单位、最大采样间隔和电子／质子能带。
- [x] 输入、脚本和工程快照，独立进程、阶段进度、超时、取消及运行中关闭。
- [x] 原始积分谱与逐时轨迹的 C++ 离线复核，无需 Python 即可查看已完成的模型任务。
- [x] 载入环境后继续普通或加权 Geant4 输运；保留几何、材料、计分与运行参数。
- [x] 防止旧窗口覆盖宿主已修改的工程；生成模型时保留主任务既有结果，仅在显式载入后清除旧结果。
- [ ] 桌面轨道传播器、AE9/AP9、太阳高年选择、SPE/GCR、方向分布及合格任务环境。

这里计算的是太阳低年俘获电子／质子模型，不是实测粒子谱。外部轨迹片段和选定能带不能直接代表整任务。模型、轨道、几何及物理过程的系统误差没有计入。

## 本机使用

启动 `dist/SpaceRad-orbit-desktop-local/spacerad.exe`，打开工程并点击“AP8/AE8 星历环境”。本机包的 `orbit-model-runtime.json` 已指向独立 `build-ae8ap8-venv`，Geant4 数据仍在本机外置 SDK 目录。

1. 选择已经安装 SpacePy 0.7.0 的 Python 解释器和随包 `model-tools` 目录。新电脑需单独配置模型依赖；程序不会自动安装或修改宿主环境。
2. 选择 UTF-8 星历 CSV，表头必须是 `utc,x,y,z`。时间为 UTC、严格递增，支持最多六位小数；2–4096 个点。位置为显式 GEO／GEI 笛卡尔，单位 km 或 Re。1 Re=6371.2 km。
3. 设置最大时间间隔以及能带。允许电子 0.1–4 MeV、质子 1–100 MeV 内的子带；默认选择 0.1–1 和 1–30 MeV。所选能带外的剂量贡献不在结果内，不能据此称为完整环境。
4. 选择新的任务目录，点击“计算并核验模型谱”。生成期间可取消；失败时查看界面错误与任务中的 `model.stderr.log`。
5. 核对样本数、时间区间、暴露、带内通量和注量。点击“载入当前工程”后，再执行 Geant4、屏蔽或体素流程。生成阶段不修改工作台工程。

GEO 是地心地固地理笛卡尔；GEI 使用现有 SpacePy/IRBEM 约定。不可把 TEME、J2000 标签或经纬高未经转换填入。模型按星历原采样点计算，各相邻时间段采用梯形积分，不等间隔不能等权平均。不外推首尾，不以零填补缺测、无效或逆序积分通量。跨 UTC 闰秒区间由模型生成器拒绝。

源码／安装包包含 `examples/ephemerides/synthetic-stationary-geo-km.csv`：它是固定 GEO 2 Re 位置的人工样例，覆盖 680.896 s，用于输入、积分和软件验收，不是传播轨道或真实任务。公开 NASA 轨迹的本地获取与复现见[外部星历](AE8_AP8_EPHEMERIS.md)。理想圆轨道仍可通过[原脚本](AE8_AP8_EXAMPLE.md)执行，本轮桌面入口针对外部星历。

## 模型环境配置

在程序旁放置 `orbit-model-runtime.json`：

~~~json
{
  "schema": "spacerad.orbit-model-runtime/1",
  "python": "C:/absolute/path/to/model-venv/Scripts/python.exe",
  "tools_directory": "model-tools",
  "timeout_seconds": 300
}
~~~

相对路径按此文件所在目录解析。超时支持 1–3600 s。模型依赖的安装方法见[模型示例](AE8_AP8_EXAMPLE.md)。打包脚本可选 `-ModelPython <路径>`；不提供时程序仍可查看档案或由用户在窗口指定解释器。SDK 安装也携带模型脚本，位于 `share/spacerad/model-tools`。

## 原始结果核验与档案

模型任务目录包含 `input-project.json`、`input-ephemeris.csv`、`settings.json`、复制的两份模型脚本、`model-job.json`、标准输出／错误日志和 `environment/` 子目录。源星历原文件名与字节身份、Python 解释器身份记录在任务清单；SpacePy 版本、IRBEM 库身份和模型输出身份记录在环境清单。

生成器按相邻能量阈值积分通量差得到每箱通量，按 UTC 间隔积分后除以实际时长和能箱宽度。C++ 检查每份原始表的身份、坐标和微秒时间、逐能点通量有效性与非增性、逐箱复算、总通量／注量、工程暴露，以及基准工程保留项。工程元数据只接受整秒，有小数的首尾时间保存在清单与轨迹表中，对应工程时间字段保持空值。

“核验历史模型任务”选择含 `model-job.json` 的完整任务目录，使用 `loadOrbitModelJob` 从原始表重建结果。不启动 Python，不要求 Geant4。修改输入、脚本、原始表或审核结果会被拒绝；SHA 清单用于一致性核验，不是数字签名。模型库的数值正确性和真实环境代表性需要独立物理验证。

生成的 `environment/project.json` 可直接打开。若从旧任务载入到新工作台，其几何和运行参数必须与窗口快照一致；宿主在生成期间修改工程或启动主输运，会使旧窗口的载入操作被拒绝，应重新打开窗口。

## SDK 与嵌入

`SpaceRad::Services` 导出 `spacerad/OrbitEnvironment.h`，无需 Widgets。`WorkbenchWidget::showOrbitEnvironment()` 打开共享窗口。

~~~cpp
spacerad::OrbitModelRuntime runtime{pythonPath, toolsDirectory, 300};
spacerad::OrbitEnvironmentSettings settings;
settings.ephemeris = ephemerisPath; // GEO, km, max gap 60 s by default
auto* model = new spacerad::OrbitEnvironmentService(parent);
model->configure(runtime);
// Connect completed/failed/stateChanged/progress before start.
model->start(project, settings, newAbsoluteDirectory);
// Keep the Qt event loop running. cancel() is asynchronous.
~~~

服务完成结果含生成工程、模型清单、复核身份及带内通量。服务不自动替换宿主工程；宿主可自行执行状态和输入版本检查。`loadOrbitEnvironment` 可单独复核旧的外部星历生成目录；`loadOrbitModelJob` 另核对原生任务的输入与脚本快照。示例 `examples/orbit-consumer` 验证安装后的公共 API 与无模型执行的离线读取。

## 本地验收

- [x] 全工程 49/49 CTests；其中 C++ 契约测试覆盖微秒 UTC、不等间隔权重、通量守恒、非法帧／单位／能带、重新计算哈希后的错误谱／工程参数仍被拒绝。
- [x] 20 项真实服务检查：公开轨迹与人工样例、schema v8 参数保留、真实 Geant4 输运、六类篡改拒绝、取消、销毁、直接信号销毁、超时及错误诊断。
- [x] 19 项原生界面检查：真实既有结果保留、生成后显式载入、Geant4 后续输运、历史查看、宿主改动拒绝、取消、运行中关闭和小窗口滚动。
- [x] C++ 复核与原有 Python 校验工具对公开轨迹及人工样例的结论一致。
- [x] 打包后重跑上述 20／19 项服务与界面检查；移除开发库路径的独立／嵌入程序烟雾验收分别通过 9／11 项，迁移后的 SDK 消费者完成无 Python 的离线复核。
- [ ] 合格整任务环境、独立实测对照、完整 CAD 航天器物理精度。

证据目录为 `reports/orbit-desktop/`。原生界面和打包流程在 Windows 验收；Linux CI 覆盖 Qt Core 模型服务和迁移后的 SDK 消费者，不代表 Linux 桌面验收。实际 TKMaster 宿主本轮未重新编译。
