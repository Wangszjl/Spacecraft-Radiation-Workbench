# 独立桌面与可嵌入组件：首批交付

日期：2026-10-30。独立项目位于同级目录 `Spacecraft Radiation Workbench`，本批交付可运行的本地研发版本，不代表所有辐射效应功能或公开发布条件已经完成。

## 立即使用

本机可直接打开 [SpaceRad 应用](../dist/SpaceRad-0.1.0-local/spacerad.exe)。选择“简化整星示例”，查看三维模型、材料和敏感标记，设置历史数后运行。可保存/重开工程，导入外部全向微分 CSV，导出剂量 CSV 和完整结果 JSON。每次计算创建新目录，包含输入快照、Worker 日志和结果。

运行包约158 MiB，包含独立程序、嵌入示例、Geant4 Worker、90个解析出的依赖 DLL 及 Qt 插件。匹配的 Geant4 物理数据仍使用本机固定 SDK，通过 `runtime.json` 显式指定，没有复制到包中。因此这是本机可搬移运行验证，不是干净机器安装验收。

在当前 TKMaster 中，打开 **仿真菜单 → SpaceRad 辐射分析组件...**。适配器创建包含同一工作台的浮动停靠面板，可停靠到主窗口。本机构建目录已经配置 `spacerad-runtime.json`，旧辐射分析入口保留。任意现有三维 actor 不会被猜测为合格辐射几何；宿主通过 JSON 传入明确的辐射工程，接收完成结果。

![独立工作台与逐区剂量](../reports/bootstrap/bundle-style-release/completed.png)

## 本批实现

- Qt 无关的模型库、Qt Core 任务服务、可嵌入 `QWidget`、独立程序和宿主示例。
- Geant4 在单独 Worker 中运行，GUI/宿主不因这个组件而链接 Geant4；库不创建 `QApplication` 或修改宿主全局环境。
- 源球/暴露/谱、材料、敏感标记与解析模型参数可操作，逐靶显示质量、TID、标准误差、RSE和非零历史。
- Worker/输入/构建清单身份绑定，拒绝旧目录、篡改快照、忙时替换和取消后的结果接收。
- 私有 C++ 类型隔离为 `spacerad::engine`；沿用兼容 JSON schema，避免与宿主旧 `scdat::radiation` 模块冲突。公共工程头采用唯一包含路径。
- 安装并导出 `SpaceRad::Model`、`SpaceRad::Services`、`SpaceRad::Widgets`，可由其他 CMake 工程消费。

## 验收证据

原始日志与结果位于本机 `reports/bootstrap/`，由 `tools/verify_bootstrap.py` 复核。

| 验证 | 结果与证据 |
|---|---|
| 独立工程回归 | `ctest-final.log`：33/33 |
| 真实 Worker 服务契约 | `service-runtime-final.log`：16项，包含真实中间进度、同种子结果/计分矩一致、忙时拒绝、同步/异步取消、快照篡改和宿主环境不变 |
| 独立桌面 | `standalone-release/summary.json`：9/9；真实10000历史、四个敏感区、保存重开、精确参数、清旧结果、取消及运行中析构 |
| 通用嵌入宿主 | `embedded-release/summary.json`：11/11；同一工作台与服务，宿主标记保持 |
| 真实 TKMaster | `tkmaster-default-release/summary.json`：6/6；真实10000历史、默认运行配置、结果回传、宿主场景与旧辐射工程保持 |
| 本机搬移运行包 | `bundle-style-release/summary.json`：9/9；移除开发库搜索路径后仍完成真实输运 |
| 安装 SDK 消费 | `consumer-run.log`：成功；刻意将旧同名头目录置前，并静态断言新旧模型类型不同 |
| 跨入口一致性 | 四种运行方式的逐靶结果及原始计分矩完全相同；每种10000历史，仅证明相同实现/配置下复现 |

四个正式入口运行合计40000历史；不以此宣称统计精度达标。示例中部分靶命中很少，界面保留其较大的误差。不得把这些合成谱数值当作真实任务预测。

开发过程保留了失败证据：首次旧测试相对包含路径错误；服务遍历临时几何导致目标数误读；早期宿主测试在500 ms延迟初始化之前取基线。后者经前后场景快照定位为宿主自身初始化，最终在初始化完成后比较原始场景字节，没有删除比较字段。代码审查还修正了运行中析构、参数截断、完成信号重入、公共头冲突和同步取消问题。

## 仍未完成

首版 UI 主要面向已有合格解析工程；完整 CAD 创建/导入与设备定位交互、参考空间环境执行、校准器件 DDD/SEE、独立物理验证、CI、干净机器安装和 Linux 尚未完成。代码权属、项目许可证、第三方与数据分发条款尚需审查，本批没有向 GitHub 推送或发布。

详见 [后续发布清单](RELEASE_READINESS.md)、[嵌入接口](EMBEDDING.md)与[本批实施清单](superpowers/plans/2026-10-30-dual-host-bootstrap.md)。源码保留本地 Git 仓库；大型 SDK、数据、构建产物、运行包和测试日志不进入版本库。
