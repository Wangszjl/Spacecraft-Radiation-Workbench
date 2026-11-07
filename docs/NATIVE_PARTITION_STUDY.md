# 原生桌面全谱分层研究

工作台的“全谱分层研究”窗口在独立程序与嵌入宿主中共用。它使用 `SpaceRad::Services` 的 C++／Qt 调度与 Geant4 子进程，不需要 Python。原始工程、主任务的已接受结果和宿主信号保持独立。

## 操作流程

1. 打开含电子／质子物理谱及敏感部件的工程，进入“全谱分层研究”。窗口固定使用打开时的工程快照。
2. 选择直接定义的非敏感盒壳和至少两种递增壁厚。所选盒壳保持内腔，向外增加材料。
3. 为每种粒子填写粗、细能段切点和逐段历史数。切点严格位于原谱内部；端点沿用原谱，完整覆盖能域。细分层包含全部粗分层切点，且逐粒子总预算相同。
4. 设置 2–4 次重复、最少非零沉积历史、最大 RSE 和零事件概率族覆盖率。启动前会校验所有壁厚的几何、源球包围、预算与谱积分。
5. 选择新的输出目录并启动。界面显示能段扫描数和实际历史进度；运行时锁定设置，可取消或关闭窗口。
6. 完成后，选中某个靶区／壁厚／重复，查看逐能段剂量、计数、沉积 ESS、概率界及同一靶区的六个估计比较（两次重复时）。可导出 CSV 与完整精度 JSON。

默认每个细能段 5,000 历史、粗能段 10,000 历史，切点按各源能域的中点和四分位位置生成，适合配置与流程试跑；这些切点不是经过优化的物理分位数。谱值、通量／注量和暴露均保留，历史预算只控制抽样精度。

“保存研究配置”生成含盒壳设置与研究设置的 `spacerad.native-partition-config/1` JSON；“导入研究配置”也接受脚本共用的 `spacerad.partition-study-settings/1`，例如 `examples/scans/spacecraft-partition-study.json`。导入前会针对当前快照完整验证，失败不修改已有设置。配置文本与导出保留双精度；结果表显示 8 位有效数字。

## 统计语义

每段保留原谱的线性或分箱常数形状，在自己的物理暴露下输运并相加；没有窄能点响应插值。各估计和重复之间采用独立运行假设，种子不同不证明随机流严格不相交。

任一能段零沉积时，全谱标准误和 RSE 保留未知；仍提供已观测部分的标准误。非零计数、能段 RSE 和总 RSE 均满足阈值才通过经验筛查。只有双方通过筛查，才显示差值除以合成经验标准误；它是描述量，不是正式显著性或收敛检验。

零事件概率界采用预先固定单元族的 Bonferroni 分配，约束该条件能段内一次初级历史出现非零沉积的概率。`dose_upper_bound_Gy` 保留空值。概率界不提供单次沉积严重程度，不能据此推导剂量上界。物理精度的现有限制见[800 万历史研究](PARTITION_STUDY.md)。

本模式使用未加权、整区计分工程；重要性抽样与体素场需使用其各自流程。每种粒子最多 16 段，总预算最多 1 亿历史。取消保留已完成子扫描档案；聚合结果只在整组完成并复验后发布，重启使用新目录。

## 历史档案与来源信息

“打开历史研究”选择原生桌面生成、含 `study-manifest.json` 的目录；无需配置 Worker 或数据集。Python 研究档案继续使用其 HTML／脚本查看，设置 JSON 可以共享。

原生档案包含工程、盒壳设置、研究设置、重建计划、结果、CSV、校验清单和逐能段的常规屏蔽扫描。汇总记录工程 SHA-256、配置及 Worker/runtime 身份。加载会重新规划、逐任务验收原始输运和统计数据，再核对聚合行与 CSV；修改根计划、汇总或子任务文件会被拒绝。SHA 清单提供一致性检查，不是数字签名。

## 宿主与 SDK

公开头文件为 `spacerad/PartitionStudy.h`，链接 `SpaceRad::Services`。宿主调用同一个非模态窗口：

~~~cpp
workbench->showPartitionStudy();
workbench->showPartitionStudy(savedNativeStudyDirectory);
~~~

无 Widgets 的 Qt Core 宿主可使用服务接口：

~~~cpp
auto config = spacerad::defaultPartitionStudySettings(project, 5000);
spacerad::ShieldingScanSettings scan{"Bus_Al", {0.8, 2.0}};
auto plan = spacerad::planPartitionStudy(project, scan, config);
auto* study = new spacerad::PartitionStudyService(parent);
study->configure(runtime);
// Connect progress/completed/failed/stateChanged before starting.
study->start(project, scan, config, newAbsoluteDirectory);
// The Qt event loop must keep running. cancel() is asynchronous.
~~~

离线函数包括 `loadPartitionStudy`、`partitionStudyCsv`；`reviewPartitionStudy` 汇总按计划排列的已验收屏蔽扫描，调用方应先完成档案验收。`examples/partition-consumer` 可独立编译、读取迁移后的安装 SDK。Windows/MSVC 消费者须与 SDK 使用相同架构及运行库配置，本地验收使用 Release。

~~~powershell
cmake -S examples/partition-consumer -B build-partition-consumer -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH='<SpaceRad安装前缀>;<Qt前缀>;<VTK前缀>'
cmake --build build-partition-consumer
.\build-partition-consumer\partition_consumer.exe '<原生研究目录>'
~~~

全桌面安装包还导出 Widgets，查找其 CMake 包会需要匹配 Qt／VTK；Qt Core 应用也可使用单独构建的 headless Services 安装包。宿主本体不链接 Geant4。

## 验收清单

- [x] 48 项全工程 CTests，包括原生分层规划、精确谱积分、通量／注量一致性和未知误差传播。
- [x] 19 项真实服务检查：48 个完成工况、档案重建、五类篡改拒绝、取消、销毁、直接信号销毁及取消后立即重启。
- [x] 原生统计与独立 Python 原始沉积矩复算一致，包括全谱积分、真实种子、几何与经验支持。
- [x] 34 项原生界面检查覆盖配置文件往返、非法设置、真实主任务结果保留、48 个整星工况、逐段查看、导出、小窗口、历史加载及运行中关闭。
- [x] 安装并迁移后的 Services SDK 消费者编译运行。
- [x] 独立与嵌入预览在移除开发库／插件路径后完成真实输运烟雾检查。
- [ ] 舱内稀有沉积的合格剂量误差界、独立物理验证、代表性 CAD 与正式二进制发布。

本地证据位于 `reports/native-partition/`，预览包为 `dist/SpaceRad-partition-desktop-local/`。Geant4 数据仍使用本机外置目录；本包尚未完成全新机器安装验收。Linux Worker CI 加入原生服务、独立复算和迁移 SDK 消费者检查；对应提交的托管结果以 Actions 为准。
