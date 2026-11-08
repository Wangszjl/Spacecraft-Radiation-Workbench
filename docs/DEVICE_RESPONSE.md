# DDD / SEE 器件响应筛查

工作台的“DDD/SEE 响应筛查”接收用户提供的器件响应曲线，并把当前工程环境谱与响应表做确定性折合。它补充器件级数据处理和审计，不改变 Geant4 输运物理，也不把响应表折合称为完整 DDD、DPA 或 SEE 事件模拟。

CSV 支持 UTF-8（含常见 Excel BOM）；表头必须为：

```text
device_id,metric,particle,energy_MeV,response,response_unit,angular_basis,reference
```

`metric=ddd` 时单位必须为 `MeV_cm2_per_g`；`metric=see` 时必须为 `cm2_per_device`。单个 CSV 只包含一个指标和响应单位；如同时评估 DDD 与 SEE，请分别提供两个 CSV。每个敏感器件、每种工程入射粒子各提供一条严格递增的能量曲线。响应曲线必须覆盖工程谱的完整能域，程序拒绝外推。`angular_basis` 必须填写 `isotropic_omnidirectional`，表示响应曲线对应当前工程的各向同性全向标量谱约定。`reference` 应填写数据集、论文、测试报告或标定工况标识。

DDD 输出为 NIEL 响应折合后的 MeV/g 及对应 NIEL Gy；它不是 DPA 或 1-MeV 中子等效注量。SEE 输出为期望事件数／器件，以及在独立到达、固定横截面和 Poisson 假设下至少一次事件的概率。

工程几何屏蔽、局部次级粒子、方向变化、响应标定误差和协方差不在此折合内。当前结果适合做响应数据的可追溯筛查，不应单独用于器件寿命、SEE 失效率或任务放行判定。

桌面点击“DDD/SEE 响应筛查”后选择 CSV 和新的输出目录；结果可导出 CSV／JSON，历史目录会重新核对 SHA 后离线重算。SDK 使用 `spacerad/DeviceResponse.h` 的 `saveDeviceResponseStudy`、`loadDeviceResponseStudy` 和 `deviceResponseCsv`。

新版在源谱网格与响应网格的所有节点处分段，解析积分每段的乘积。分箱常值谱在跳变点采用该区间自身的端点极限；旧算法在分箱边界可能混入邻箱的值。新版报告为 `spacerad.device-response-study/2`。打开旧版档案时先核对原始输入及旧报告的哈希，再显示按新版重算的结果，保留旧文件。报告以 `archive_review=legacy_inputs_recomputed_v2` 标记。需要持久化修正结果时，在新目录重新折合。

蒙特卡罗重要性抽样设置可以保留在工程中。该功能只使用工程中的物理谱；不读取输运结果，也不对谱施加抽样权重。

同一服务可用于批量处理，无需 Geant4 或 Python：

```powershell
spacerad_cli fold-response --project examples/projects/spacecraft-tid-assembly.json --response examples/device-response/synthetic-obc-ddd.csv --output reports/response-new
spacerad_cli inspect-response --job reports/response-new
```

示例 DDD 与 SEE 表仅用于软件演示，没有器件标定含义。使用真实器件前，应替换为适用工况的响应曲线。

需要使用屏蔽后的靶区粒子谱时，可选[靶区轨迹长度计分与局部响应折合](TARGET_FLUENCE.md)。后续仍需完成 Geant4 位移损伤／DPA 物理验证、SEE 事件和器件结构模型、实验标定，以及代表性 CAD 的独立对照。
