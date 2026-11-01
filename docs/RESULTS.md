# 历史任务与剂量对比

0.1.4 在独立桌面、嵌入工作台、Qt Core SDK 和 CLI 中提供同一套离线结果核对与比较功能。

## 桌面操作

1. 点击“历史任务 / 剂量对比”。运行完成时会自动以该任务作为 A；也可以在没有配置 Worker 的情况下打开窗口。
2. 浏览任务根目录，在列表中选一个任务并载入 A。列表只检查文件是否存在，载入时才检查内容。每次最多扫描 500 个直接子目录，不递归扫描；也可以直接指定任务路径。
3. 将另一次任务载入 B。表格按敏感区 ID 显示两次 TID、各自 MC 标准误差、B−A 和 B/A，以及非零历史数。
4. 导出对比 CSV 或包含结果指纹、设置核对和提示的 JSON。请选择新文件名。清除 B 后可以单独回看 A。

历史窗口不替换当前工程、不设置当前运行的完成结果，也不发出 `resultReady`。打开多个历史窗口或查看旧结果不会影响当前输运。档案载入为内存快照；文件随后变化时，需重新载入。摘要中的 SHA-256 对应载入时保存的字节。

## 任务目录与核对范围

保留整个任务目录中的 `project.json` 和 `result.json`。目录可以移动；两个文件请保留原名。仅导出一个结果 JSON 不能构成可核对档案。取消任务、失败任务、缺少快照的任务不能作为完成档案载入。

载入不执行 Geant4、不需要原 Worker、数据集或原来的绝对路径。核对包括：

- 项目最多 8 MiB，结果最多 64 MiB；拒绝重复 JSON 键和超过 128 层的 JSON。
- 完成状态、`result/2`、快照 SHA-256、规范化项目内容、历史数和源归一化。
- 运行审计状态、Worker / 构建上下文 / 数据集清单指纹的存在与格式。
- 敏感区 ID 集合、正质量、非负有限剂量和标准误差、Gy/rad 换算、非零历史数及 RSE 的内部一致性。
- 保存的原始谱字节与其指纹一致；载入期间文件未改变。

这是保存文件之间的一致性核对。文件没有数字签名，不能认证生成者，也无法检测一并修改后重新计算哈希的档案。不会重新检查历史 Worker 的二进制或数据集内容；数据集清单指纹不等于所有数据文件的内容校验。这里不执行独立剂量物理验证。

## 如何理解对比

结果按 ID 并集列出，不会把不同 ID 自动当作同一个元器件。相同 ID 的材料、形状、尺寸、壁厚、世界坐标/旋转或质量不同，会标记“敏感区已变化”。仅一侧存在的敏感区保留在表中，差值和比值留空。

“设置一致”要求敏感区定义和质量一致，且以下项目一致：

- 粒子种类、能量和值、插值方式、flux/fluence 类型、暴露时长、源球半径。
- 物理列表、production cut、材料快照、Geant4 版本、Worker、构建上下文、数据集清单指纹和能力声明。

这些是保守的精确设置比较；不会自动将两种物理等价但表达不同的谱归一成同一种表示。历史数和随机种子可以不同。环境来源、合成标志或元数据改变会另外提示。非敏感屏蔽结构可以改变，这是屏蔽方案对比的用途；“设置一致”不意味着两次几何相同，也不代表科学认证。

差值定义为 B−A，比值为 B/A；A 为零时比值未定义。CSV 中空字段表示没有定义或缺少对应目标，不能读作零。ID 字段会转义引号，并对可能被电子表格解释为公式的前缀加单引号；JSON 保留原 ID。

仅报告每次计算各自的 MC 标准误差。相同种子不证明两个估计独立，也不提供完整协方差，因此不计算差值的标准误差、显著性或“优化百分比”的置信区间。零命中不代表零风险。当前 DDD/SEE 和独立剂量物理验证仍未完成。

## CLI

下面两个命令不读取 `runtime.json`，也不接受 `--runtime`：

```powershell
.\spacerad_cli.exe inspect-result --job C:\SpaceRad\jobs\case-A
.\spacerad_cli.exe compare --baseline C:\SpaceRad\jobs\case-A --candidate C:\SpaceRad\jobs\case-B
```

成功分别输出一个 `archived-result` 或 `comparison` 类型的 `spacerad.cli-event/1` JSON 事件，退出码 0。输入或档案错误输出 `error`，退出码 2。比较报告使用 `spacerad.comparison/1`，包含 baseline、candidate、warnings 和 rows。数值保留 double 往返精度；对比结果存在设置差异时仍输出，但会标记并提示。

## 嵌入接口

仅链接 `SpaceRad::Services` 即可离线处理，无需 Widgets / VTK / Geant4：

```cpp
#include <spacerad/Results.h>
auto a = spacerad::loadArchivedRun(directoryA);
auto b = spacerad::loadArchivedRun(directoryB);
auto comparison = spacerad::compareRuns(a, b);
auto csvBytes = spacerad::comparisonCsv(comparison);
```

Qt GUI 宿主可调用 `workbench->showResults(directoryA, directoryB)` 打开由该工作台拥有的非模态窗口。参数可省略，用户再自行选择目录。

## 验证

`results_contract` 使用人工一致文件验证离线核对、损坏拒绝和比较边界，不执行物理输运。`tools/check_results_runtime.py` 则实际执行两个 50,000 历史的合成电子/质子任务：直径 20 mm 的硅球置于 30 mm 铝盒壳内，铝壁厚分别 0.5 mm 和 2 mm，源球半径 30 mm。它检查归档、比较算术、搬迁、损坏拒绝与源文件不变；不把剂量差当作独立物理基准。原生桌面测试另外验证界面载入、导出和宿主工程保持。
