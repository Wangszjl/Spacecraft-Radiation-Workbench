# 0.1.3命令行与共享任务服务验收

日期：2026-10-31。本批交付可直接运行的命令行入口，并将运行配置读取放到Qt Core服务层。独立桌面和TKMaster嵌入入口继续使用同一套服务。

## 使用入口

本机打开 [SpaceRad 0.1.3桌面版](../dist/SpaceRad-0.1.3-local/spacerad.exe)。同目录提供 `spacerad_cli.exe`、运行配置、简单谱示例及 `CLI.md`。数据仍通过配置指向本机固定SDK目录；旧运行包保留。

CLI提供工程校验、实际运行环境检查、正式计算，以及逐行JSON进度和终结事件。具体命令、退出码与Python集成示例见 [CLI使用说明](CLI.md)。命令行默认读取可执行文件旁的 `runtime.json`，也可显式传入其他配置。没有新增输运物理或轨道模型。

## 实现与兼容性

- `spacerad_cli`链接 `SpaceRad::Services`，不创建GUI或链接Geant4。Windows依赖检查仅发现Qt Core和系统／编译器运行库，没有Widgets、VTK或G4库。
- `loadRuntimeFile`现在由 `AnalysisService.h` 提供，原有包含 `WorkbenchWidget.h` 的代码继续可调用。已安装SDK的外部工程通过仅链接服务库使用该函数。
- 配置读取按文件目录解析相对路径，支持UTF-8 BOM、空库路径数组及Unicode路径；拒绝重复字段、未知字段、非字符串路径、过深或超大输入。进程环境不改变。
- CLI计算复用任务服务，保留新目录保护、输入／Worker／构建／数据清单绑定。完成摘要异常会生成终结错误事件，不让异常越过Qt回调边界。
- 测试专用故障响应程序仅用于注入缺失完成字段，不执行物理输运，不安装或打包。

## 验证结果

[Linux实际成功运行](https://github.com/Wangszjl/Spacecraft-Radiation-Workbench/actions/runs/41194351330)对应源码 `6c0e81ef7dd35487b7a8172f92090e9d0cfcc81f`。同一提交的Windows/Linux纯模型CI也成功。

| 验证 | 结果 | 本机证据（reports/cli/） |
|---|---|---|
| Windows无桌面构建 | 37/37 CTests；含37项配置解析与66项无数据CLI协议检查 | `headless-ctest-final.log` |
| Linux无桌面构建 | 37/37 CTests | `github-linux.log`、`linux-artifact/worker-ci/worker-tests.xml` |
| Windows真实CLI | 49/49；环境审计、三次计算、覆盖拒绝、超时取消、Worker故障 | `windows-first/summary.json` |
| Linux真实CLI | 53/53；另验证SIGINT及无残留运行Worker | `linux-artifact/cli-acceptance/summary.json` |
| Linux既有Worker／服务检查 | 57/57 | `linux-artifact/worker-acceptance/summary.json` |
| 完整桌面回归 | 37/37 CTests；31项环境流程、27项建模／定位检查 | `desktop-ctest.log`、`environment-regression/`、`authoring-regression/` |
| 实际TKMaster | 7/7；结果返回、定位、宿主场景及旧工程保持 | `tkmaster-regression/summary.json` |
| 安装后SDK服务库 | 外部工程调用Core运行配置读取函数并运行 | `consumer-run.log` |
| 新运行包移除开发DLL搜索路径 | GUI 9/9；CLI默认配置审计、校验与10,000历史计算完成 | `bundle-gui/`、`bundle-*.stdout.log`、`bundle-cli-job/` |

真实CLI分别运行flux、同seed重复和等价fluence，均覆盖三靶区、10,000历史。逐靶剂量与原始计分矩一致，CLI完成摘要与接受的结果文件字节SHA相符。修改历史数只改变任务快照，原始工程保持不变。超时返回124；Linux中断返回130；故障返回3，覆盖或配置错误返回2。每次命令仅有一个最终终结事件，Worker文本保留在任务日志，verbose文本在标准错误中。

Linux工件已下载并核对ZIP SHA-256：`022660cf0a8c5c13cc130b43347c46455e69db9f8c82c8bd4b775eb5fdf93685`。本机保存完整副本，不依赖GitHub工件长期保留。`reports/cli/acceptance.json`采集正式证据与本批源文件／运行包身份。

## 当前边界

本批证明命令行、共享服务和桌面／嵌入入口的工程连通性与数值回归。真实轨道环境、独立剂量物理验证、器件DDD/SEE、Linux桌面、干净机器安装、许可证与正式二进制发布仍在[后续清单](RELEASE_READINESS.md)中。
