# 环境谱编辑与归一化：0.1.2 本地验收

日期：2026-10-30。独立桌面与嵌入工作台现在共用可编辑环境谱流程，不再必须先准备外部 CSV。历史版本及证据保持原样。

## 立即使用

打开 [SpaceRad 0.1.2](../dist/SpaceRad-0.1.2-local/spacerad.exe)，切换“环境谱”。新增或选择一行编辑，可设置粒子、能量和微分值，预览积分及曲线；也可删除选中谱，最后一条谱受保护。窗口较小时可滚动访问全部控件。

线性模式每个能点对应一个微分值；分箱常数模式最后一行只填写右边界，值留空。支持2048个能量行，数字按17位精度读写。非法能序、负值、无穷值、全零积分、重复粒子会被拒绝，不会提交部分更改。

“换算 flux / fluence”使用当前暴露时长逐项乘/除，并写入转换历史。界面展示总谱积分、各粒子抽样份额、源球半径、暴露时长及期望初级粒子数。flux 会乘时长，fluence 不再乘时长；修改工程后旧结果会清除。手工编辑明确标为合成输入，清除旧轨道模型元数据；其他谱的来源记录保留。

包内 `examples/projects/` 有一对可直接打开的[电子／质子示例](../examples/projects/README.md)。质子线性谱积分20、电子积分4，flux暴露64秒；等价fluence积分1536 cm-2。两者源球半径500 mm，期望初级粒子数均约12,063,715.7898，抽样份额分别5/6与1/6。示例请求100,000个MC历史，仍须检查统计误差。

![谱点编辑器](../reports/environment/native-scroll/spectrum-editor.png)

![环境谱与归一化](../reports/environment/native-scroll/environment.png)

## 验证结果

| 验证 | 结果 | 本机证据（reports/environment/） |
|---|---|---|
| 独立模型/几何/服务回归 | 35/35 CTests | `ctest.log` |
| 环境编辑与数值校验 | 41项检查；复制操作、积分、非法输入、来源保留、数量换算、序列化 | `model-contract.log` |
| 实际Qt/VTK环境工作流 | 31/31，包括取消、错误拒绝、宿主更新期间弹窗保护、分箱重开、忙时禁用、小窗口滚动 | `native-scroll/summary.json` |
| 打包Worker重复同一环境流程 | 31/31 | `native-bundle/summary.json` |
| 原有部件编辑/定位回归 | 27/27 | `authoring-regression/summary.json` |
| 独立窗口及嵌入示例 | 9/9、11/11，含取消与运行中销毁 | `standalone-release/`、`embedded-release/` |
| 实际TKMaster双粒子环境 | 8/8；20,000历史，输入谱点和来源保持，宿主场景逐字节不变 | `tkmaster-release/` |
| 安装后开发包 | 消费环境编辑API，旧同名头优先搜索仍可编译执行 | `consumer-build.log`、`consumer-run.log` |
| 去除开发DLL搜索路径后运行新包 | 9/9；物理数据仍显式配置为外置本机目录 | `bundle-release/` |
| 随包两个100,000历史示例 | 两次真实Worker运行完成，逐靶剂量及原始计分矩相同 | `example-flux/`、`example-fluence/` |

原生环境流程分别运行flux与等价fluence，各20,000历史；逐靶剂量、原始计分矩、期望初级数相同，并在打包Worker上复现。实际TKMaster的双粒子结果也与同种flux运行一致。大数一般换算使用严格相对容差比较；这组可精确缩放的测试数据还检查了逐项相等。

`tools/verify_environment.py` 复核指定的正式证据、输入/Worker/构建/清单身份和示例内容，采集 `reports/environment/acceptance.json`。`native-first` 是修正测试断言前的记录，`native-release` 是滚动布局前的记录；本表指定 `native-scroll` 与 `native-bundle` 为本批正式界面验收。失败/中间记录保留，不当作通过证据。

## 边界与后续工作

这是合成环境下的软件流程与归一化等价检查，不是AP8/AE8、AE9/AP9或真实轨道环境模型执行，也不是独立物理剂量验证。零命中靶区的RSE仍为未知，不能解释为零风险。曲线采用线性坐标，不进行外推或隐式单位换算；谱数据的能量范围之外没有另加谱尾。

Geant4 11.4.3 物理数据仍放在原固定SDK目录，由包内 `runtime.json` 指定；跨机器安装尚未验收。DDD/SEE、完整CAD流程、参考环境模型、独立物理基准、CI、许可证与GitHub公开发布事项仍在[后续清单](RELEASE_READINESS.md)中。
