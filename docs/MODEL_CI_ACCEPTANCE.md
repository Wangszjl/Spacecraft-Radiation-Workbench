# 模型库独立构建与跨平台CI验收

日期：2026-10-31。已完成Qt无关模型库的独立构建、安装搬移和GitHub自动化检查。桌面应用仍使用现有0.1.2本地运行包，本批没有发布新安装包。

## 已完成

- [x] 增加 `SPACERAD_BUILD_SERVICES` 开关；关闭服务、桌面和Worker时，无需查找Qt、VTK或Geant4。默认完整构建方式保留。
- [x] 模型包仅导出模型库及相关头文件，传播C++17要求；`find_package(SpaceRad COMPONENTS Model)`可用，未构建的Services/Widgets组件会被拒绝。
- [x] SDK安装到含空格的路径，搬移后另一个CMake工程可编译、执行，并读取两个合成谱示例。
- [x] GitHub Actions在Windows/MSVC与Linux/GCC上实际完成构建、13组CTest及10项安装验收。
- [x] 当前Windows完整构建的35组CTest、31项桌面环境交互检查及完整SDK外部消费回归通过。

使用方式和公开API见[模型构建指南](MODEL_BUILD.md)。其他程序可以只链接 `SpaceRad::Model` 来使用工程、几何检查、部件编辑、谱编辑与归一化；桌面宿主仍可链接完整的服务和Widgets组件。

## 证据

[GitHub实际成功运行](https://github.com/Wangszjl/Spacecraft-Radiation-Workbench/actions/runs/41189019059)对应源码提交 `8f4af5d61350f780a594f9d8eeb77cdcc070d258`。两个作业均成功，且没有下载或安装本项目的Qt/VTK/Geant4依赖。

| 验证 | 结果 | 记录 |
|---|---|---|
| Ubuntu 24.04 / GCC | 13/13 CTests；10项SDK验收通过 | GitHub上述运行；本机 `reports/model-ci/github-linux.log` |
| Windows 2022 / MSVC | 13/13 CTests；10项SDK验收通过 | GitHub上述运行；本机 `reports/model-ci/github-windows.log` |
| 本机MSVC模型构建 | 13/13 CTests | `reports/model-ci/ctest.log` |
| 本机安装与搬移 | 10/10，包括组件缺失和错误构建组合拒绝 | `reports/model-ci/sdk final/summary.json` |
| 完整桌面回归 | 35/35 CTests，31项实际交互及两次20,000历史输运 | `reports/model-ci/full-ctest.log`、`desktop-regression/summary.json` |
| 完整SDK消费 | 旧同名头搜索路径优先时，模型/服务类型仍隔离且程序可运行 | `reports/model-ci/full-consumer-run.log` |

工作流随push、PR或手动触发，保留短期测试日志artifact；不会上传SDK、物理数据或桌面运行包。GitHub日志/工件有保留期限，本机已保存本次原始日志与作业状态。首次检查中SDK负向验收的报错文字判断写反，已在正式CI提交前修正；中间记录保留，正式本机安装证据为 `sdk final/`。

## 范围

本次Linux验收限于模型库、测试和SDK消费。Qt桌面渲染、Geant4输运、数据安装和Linux桌面发行尚未验收；模型测试也不替代独立物理基准。完整桌面／Worker CI、干净机器运行与安装包、许可证和第三方发布事项仍在[发布清单](RELEASE_READINESS.md)中。
