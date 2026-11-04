# SpaceRad命令行与脚本集成

0.1.3增加 `spacerad_cli`。它只链接Qt Core和任务服务，调用独立Geant4 Worker执行计算。桌面工作台、Qt宿主与CLI共用同一个 `AnalysisService` 和运行配置读取函数。

0.1.4增加离线 `inspect-result --job <任务目录>` 和 `compare --baseline <A目录> --candidate <B目录>`。它们无需运行配置或 Geant4，分别输出 `archived-result` / `comparison` JSON 事件。完整语义、对比标记和限制见[历史任务与剂量对比](RESULTS.md)。

0.1.5增加 `plan-scan`、`scan` 和 `inspect-scan`，用于固定内腔的盒壳厚度扫描。设置、总抽样预算与取消语义见[自动屏蔽厚度扫描](SHIELDING_SCAN.md)。

0.1.6 中普通 `run` 读取 schema-v7 `voxel_scoring` 工程，完成后返回含逐格 TID 的 `voxel_field` 和 `edep_spectra`。接受结果会按原始历史矩、敏感目标能量守恒与每格质量做服务端复验。要导出 ParaView 数据，仍需用同版本 Worker 的 `--export-voxels <project.json> <result.json> <new-dir>`；见[体素计分](VOXEL_SCORING.md)。

## 本机运行包

在本机 `dist/SpaceRad-0.1.5-local/` 打开PowerShell。包内 `runtime.json` 已配置本机外置Geant4数据，以下命令可以直接使用：

```powershell
.\spacerad_cli.exe validate --project .\examples\projects\electron-proton-flux.json
.\spacerad_cli.exe inspect-runtime
.\spacerad_cli.exe run --project .\examples\projects\electron-proton-flux.json --output "$env:TEMP\SpaceRad-job-001" --histories 100000
```

选择尚不存在的任务目录。数据仍在原固定SDK目录；复制运行包到其他机器时，需要配置对应数据路径和系统运行依赖。GitHub当前提供源码，尚无正式安装包发布。

自建或安装的程序可显式指定配置：

```bash
spacerad_cli run --runtime /path/to/runtime.json \
  --project /path/to/project.json --output /path/to/new-job \
  --histories 100000 --timeout-seconds 600
```

不提供 `--runtime` 时，读取CLI可执行文件旁的 `runtime.json`。配置内相对路径以配置文件所在目录为基准；命令行相对工程／输出路径以当前工作目录为基准。含空格或Unicode的路径应作为完整参数传入。

## 三个入口

| 命令 | 必需参数 | 行为 |
|---|---|---|
| `validate` | `--project` | 检查工程schema、几何、谱和归一化，返回敏感区ID及输入SHA；不需要Worker或物理数据，不做材料目录认证／任务认证 |
| `inspect-runtime` | 已有运行配置 | 实际调用Worker的 `--inspect`，审计构建、数据清单和版本，并核对Worker与资产身份；不执行输运 |
| `run` | `--project`、`--output`、已有运行配置 | 经任务服务启动、跟踪、取消和接收结果；保留原有身份绑定与覆盖保护 |

`validate`和`run`可用 `--histories 2..100000000` 覆盖当前运行的历史数，原工程文件不修改，覆盖值写入任务快照。`run`还接受 `--timeout-seconds 0..86400`（0为不限时）及 `--verbose`。与命令无关的参数和重复值会被拒绝。

`--help`和`--version`输出普通文本。其他命令的标准输出为逐行JSON（NDJSON），每行带 `schema=spacerad.cli-event/1`、`type`及从0递增的 `sequence`。输出采用ASCII转义，因此Windows旧代码页也能传输Unicode路径。

## 进度、结果和退出码

`run`依次输出 `launch`、状态／进度事件及一个终结事件。例如：

```json
{"schema":"spacerad.cli-event/1","type":"progress","sequence":3,"completed":1000,"total":100000}
```

成功终结事件 `completed` 包含 `exit_code=0`、结果路径及SHA、历史数、Geant4版本、期望初级数、逐靶结果和能力状态。完整结果（包括原始计分矩）仍在任务目录的 `result.json` 中；其他文件为 `project.json` 和 `worker.log`。

Worker原始文本不混入标准输出。默认写入任务日志；加 `--verbose` 时还转发到标准错误。任务使用新目录，已存在的目录不会覆盖。

| 退出码 | 终结事件／含义 |
|---|---|
| 0 | `validated`、`runtime`或`completed`，按命令区分 |
| 2 | `error`：参数、输入或配置错误，或输出目录不能用于新任务 |
| 3 | `error`：Worker启动／运行／审计失败，或完成响应不能形成CLI结果摘要 |
| 124 | `cancelled`：运行超时 |
| 130 | `cancelled`：中断请求 |
| 143 | `cancelled`：SIGTERM请求 |

Windows/Linux均已验证超时取消；Linux还验证了SIGINT、退出后无运行中Worker。中断处理在主事件循环中调用任务取消，信号处理器只设置标志。脚本应以退出码及终结事件判断是否接受结果，不能仅凭 `result.json` 文件存在判断成功。运行环境检查有单独的30秒进程超时，失败返回3。

## Python调用示例

```python
import json
import subprocess

process = subprocess.Popen([
    "spacerad_cli", "run", "--runtime", runtime_path,
    "--project", project_path, "--output", new_job_directory,
    "--timeout-seconds", "600",
], stdout=subprocess.PIPE, text=True, encoding="utf-8")

terminal = None
for line in process.stdout:
    event = json.loads(line)
    if event["type"] == "progress":
        print(event["completed"], event["total"])
    elif event["type"] in ("completed", "cancelled", "error"):
        terminal = event

code = process.wait()
if code != 0 or terminal is None or terminal["type"] != "completed":
    raise RuntimeError(f"SpaceRad analysis failed: {terminal}")
result_path = terminal["result_file"]
```

这里使用参数列表，便于安全传递路径；默认不启用 `--verbose`，因此可持续读取JSON进度。若应用要捕获标准错误，也应同步消费它或使用 `subprocess.run(..., capture_output=True)`，避免管道堵塞。

## Qt宿主与开发包

`loadRuntimeFile`现在由 `spacerad/AnalysisService.h` 声明并在 `SpaceRad::Services` 中实现，不需要链接Widgets。原本包含 `WorkbenchWidget.h` 的代码仍通过它包含的服务头获得该声明：

```cpp
#include <spacerad/AnalysisService.h>

spacerad::AnalysisService service;
service.configure(spacerad::loadRuntimeFile(runtimeFile));
```

构建开启 `SPACERAD_BUILD_SERVICES` 即生成CLI；可关闭桌面／Worker构建，通过配置调用其他位置的Worker。纯模型构建仍不需要Qt，也不生成CLI。CLI不创建GUI或链接Geant4；进程环境配置只传入子Worker。

`tools/check_cli_runtime.py`提供真实Worker的协议验收；日常 `ctest` 还包含不需要Geant4数据的参数／协议检查、配置解析与测试专用故障响应。故障注入程序仅参与测试，不安装或进入运行包。
