# 0.1.6 空间剂量体素验收

## 范围

0.1.6 将既有解析几何体素打分接入公开工程编辑 API、桌面工程配置、输运结果严格验收、VTK 三维叠加、二维切片表格和 VTI/CSV/JSON 导出。工程 schema v7 保存网格声明。配置 API 是只读输入到独立配置副本。

## 验收步骤

- [x] Model API 合同：合法 v7 roundtrip、输入不变、关闭 scorer、非敏感体/曲面几何/边界条件拒绝。
- [x] 实际 Geant4 Worker 对 2×2×2 均匀硅敏感盒计分并校验整个结果。
- [x] Worker 用同一几何和历史累计整体分数与体素分数；逐格能量矩之和与整体矩守恒。
- [x] 单事件能谱计数覆盖零沉积历史与能量上溢历史，概率总和为 1。
- [x] Worker 输出字段通过 validator 后生成 field.json、field.vti、field.csv 与 SHA manifest。
- [x] 原生桌面测试从 v7 工程打开、运行真实 Worker、显示 2D 切片和三维叠加并执行导出。
- [x] 原有 TID、CLI、剂量对比、扫描及 authoring/environment UI 回归。
- [x] Linux CI 已加入真实体素 Worker 接受检查；具体 hosted action 随源提交推送后记录。

## 本地 Windows 证据

- 40/40 desktop CTests，14 条 voxel API contract。
- 13 项真实 Worker runtime 检查：4000 历史、8 cells、每源历史能量矩守恒、总目标质量加权剂量对应、沉积能谱零事件与溢出均计入、JSON/VTI/CSV 文件长度和 SHA manifest。
- 16 项 native GUI 检查：普通 schema-v6 工程可通过控件启用并保存为 v7、真实输运、整区剂量质量加权对应、重新打开、三轴切片表、VTK 3D overlay 和整套导出。
- 原环境编辑 31 项、部件 authoring 27 项；actual TKMaster 回归需在最终 SDK/宿主复建后记录。
- headless clean CMake build/install，外部 consumer 编译并运行 voxel-scoring API；VTK 不参与。

Worker workflow 已加入 Linux voxel runtime 检查，model-only Windows/Linux workflow 覆盖 Model API。尚未在本 turn 提交后运行 hosted actions；这里只记录本机结果。

所有实际剂量检查仅使用仓库合成谱及小型几何，不验证任何航天器任务预测。
