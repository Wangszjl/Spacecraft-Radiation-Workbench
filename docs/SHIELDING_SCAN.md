# 自动屏蔽厚度扫描

0.1.5 提供可嵌入的固定预算扫描服务、桌面窗口和 CLI。复用现有引擎的几何规划规则，通过 `AnalysisService` 逐项调用真实 Geant4 Worker；GUI / Qt Core 宿主不链接 Geant4。

## 桌面流程

1. 打开工程，点击“自动屏蔽厚度扫描”。可先打开 `examples/projects/shielding-scan.json`，它使用明确标记的合成电子/质子谱。
2. 选择直接定义的、非敏感空心盒壳，填写严格递增的 2–64 个壁厚。`0, 0.5, 2` 表示无该盒壳、0.5 mm 和 2 mm 三种方案。
3. 设置每个厚度 / 入射粒子项的历史数。两种粒子、三种厚度、每项 10,000 历史，共运行 60,000 个历史；这不是全扫描共用 10,000 个历史。
4. 选择尚不存在的输出目录，开始扫描。服务逐项执行，每项保留自己的输入、结果和 `worker.log`。可以取消或关闭窗口；关闭会停止本窗口的 Worker，并记录取消状态。
5. 查看按敏感区选择的剂量曲线、各点 MC 标准误差棒、数值表和零命中粒子项。导出 CSV / JSON，或以后通过“打开历史扫描”离线回看。

窗口使用打开时的工程和运行配置快照；扫描历史数的调整只作用于本窗口的副本。不修改工作台当前工程、当前完成结果，也不发送工作台的 `resultReady`。每个窗口最多运行一个 Worker；多个独立窗口的资源预算需要使用者自行安排。

## 几何与物理规则

所选盒壳原尺寸为 `D`、壁厚为 `w`，固定内腔为 `D−2w`。扫描厚度 `t>0` 时外尺寸变为 `D−2w+2t`，位置、旋转、材料保持不变。`t=0` 移除这一个盒壳；其他结构仍保留。扫描不等同于保持外尺寸不变后向内加厚。

只支持直接定义的非敏感空心盒壳。模板实例生成的壳体、敏感壳体、实心盒、曲面壳不接受为扫描对象。所有方案先通过既有几何校验和源球包围检查，再创建计算任务。源球半径不会随壁厚自动增加；向外扩展发生碰撞或越出源球时需先调整工程。

每个厚度按入射 `e-` / `proton` 分别执行，保留相应粒子产生的全部次级粒子贡献。每项采用不同的确定性种子和完整固定历史数；不做自适应停止。每项独立计算它所属粒子的源谱积分和一次暴露归一化，再把各项 TID 相加。不会把总混合谱的暴露重复用于每种粒子。

同一厚度的全部入射粒子项完成后才生成合计曲线行。采用独立运行假设，合计 MC 标准误差为各粒子标准误差平方和的平方根。不同种子不构成随机流数学独立性的证明。零命中的粒子项使合计 RSE 不可估计；此时输出空 RSE 和明确的 `zero_hit_components`，已有样本的 SE 不构成未命中过程的风险上界。

曲线使用线性坐标，点间线段仅辅助读图，不拟合、不插值计算缺失厚度，也不假设剂量随厚度单调下降。不计算不同厚度之差的显著性。当前结果仍是研究软件的前向 MC 计算，未完成独立剂量物理验证、轨道环境模型执行或经校准的 DDD/SEE。

## 命令行

设置文件 `examples/scans/three-thicknesses.json`：

```json
{"schema":"spacerad.shielding-settings/1","shell_id":"Shield_Al","thicknesses_mm":[0,0.5,2]}
```

```powershell
.\spacerad_cli.exe plan-scan --project .\examples\projects\shielding-scan.json --scan-settings .\examples\scans\three-thicknesses.json
.\spacerad_cli.exe scan --project .\examples\projects\shielding-scan.json --scan-settings .\examples\scans\three-thicknesses.json --output C:\SpaceRad\new-scan --timeout-seconds 600
.\spacerad_cli.exe inspect-scan --job C:\SpaceRad\new-scan
```

本地运行包默认使用旁边的 `runtime.json`；其他位置可通过 `--runtime` 指定。`--histories N` 只覆盖扫描副本的每项预算。规划和历史核对不需要 Worker 或运行配置。

stdout 是有序的 `spacerad.cli-event/1` NDJSON。规划输出 `scan-planned`，运行输出 `scan-launch`、`progress`，最后输出一次 `scan-completed`；历史核对输出 `archived-scan`。正常完成退出 0，输入错误 2，Worker / 结果核对错误 3，超时 124，SIGINT 130，SIGTERM 143。取消后保留终止状态档案，不作为整组完成结果接受。`--verbose` 将 Worker 文本转发到 stderr；各项日志始终保存在档案中。

## 档案与离线核对

保留整个目录：

```text
project.json
scan-settings.json
scan-plan.json
scan-result.json
scan-manifest.json
scan.csv
cases/case-000000/{project.json,result.json,worker.log}
cases/case-000001/...
```

档案可整体移动。加载时核对根快照 / 设置 / 计划 / 结果的哈希，重建几何规划，逐项调用离线结果核对，并从已完成项重建合计曲线和 CSV。运行中的档案不能作为终止档案加载。取消或失败档案可回看；仅包含同一厚度的所有粒子项都已完成的曲线行，不补齐缺失项。

项目限制 8 MiB、设置 64 KiB、计划 / 报告各 32 MiB，拒绝重复 JSON 键和过深 JSON。每项沿用普通任务的快照与结果核对。整个扫描固定 Worker、构建上下文和数据集清单指纹，项目间发生变化时停止。清单指纹不等于数据集全部内容的哈希。档案一致性核对不认证生成者，也不证明剂量物理正确。

只导出扫描 CSV / JSON 不构成完整可核对档案。CSV 中空 RSE 表示不可估计，不能读成零；可能被电子表格视为公式的 ID 前缀会加单引号，JSON 保留原 ID。

## Qt SDK

链接 `SpaceRad::Services`，包含 `spacerad/ShieldingScan.h`：

```cpp
spacerad::ShieldingScanSettings settings{"Shield_Al", {0, 0.5, 2}};
auto plan = spacerad::planShieldingScan(project, settings);
auto* scan = new spacerad::ShieldingScanService(owner);
scan->configure(runtime);
// 连接 completed / failed / stateChanged / progress 后启动；宿主提供 Qt 事件循环。
scan->start(project, settings, newAbsoluteDirectory);
// scan->cancel();
auto report = spacerad::loadShieldingScan(savedDirectory); // 离线
```

`progress(completedCases, totalCases, histories, totalHistories)` 报告已完成项及当前项进度。取消可重复调用。销毁服务会停止它的 Worker。`result()` 只在全部完成且档案核对通过后返回完整报告；取消 / 失败时为空。GUI 宿主可以调用 `WorkbenchWidget::showShieldingScan()`，或传入已保存扫描目录进行回看。

`examples/scan-consumer` 是独立 CMake 消费者示例，可针对已安装的仅服务 SDK 构建，离线加载扫描并调用规划 / CSV API；无需 VTK 或 Geant4。
