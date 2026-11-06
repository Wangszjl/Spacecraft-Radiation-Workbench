# 桌面重要性抽样配置

在“计算参数”页点击“配置重要性抽样”，可启用或关闭全谱加权、设置原始能谱混合率 f 与尾部物理概率 τ，并选择是否自定义电子／质子的提议份额。界面会显示原物理粒子份额和尾部提议概率；配置接受后，环境页显示当前设置。

配置通过同一个 Qt-free Model API 提交，不改变能谱、源 provenance、暴露、几何、种子或历史预算。数值以 17 位有效数字读写，保存为 schema v8。取消、无效参数和不归一化的份额不会写入工程。体素与重要性抽样当前不兼容，界面会拒绝组合，保留原体素设置。原项目在对话框打开期间被宿主替换时，旧编辑被拒绝；Worker 运行期间不能编辑。

成功修改后，已接受结果及旧统计明细入口清除，须重新输运；取消编辑保留原结果。已有打开的只读统计快照不被改写。每次运行后仍需在[统计明细](IMPORTANCE_REVIEW.md)检查全部来源区域，配置本身不能保证收敛。

公开 API：

~~~cpp
#include <spacerad/ImportanceConfig.h>
auto edited = spacerad::configureImportanceSampling(project, .9, .05, {{"e-",.15},{"proton",.85}});
auto nominal = spacerad::disableImportanceSampling(edited);
~~~

本地验收：46/46 CTest，新增配置合同与能量响应计划合同；27 项原生界面检查覆盖取消、尾部非法、份额总和错误、体素冲突、精确往返、轨道元数据保留、运行时禁用、一次8,000历史真实输运、取消保留结果、关闭抽样清除旧结果与宿主并发替换。证据在 `reports/source-config/native-final/`；截图和运行档案不随源码发布。

同版本的纯 Model 构建通过 20/20 测试和 10 项重定位安装消费检查；原统计明细窗口 25 项回归通过。更新的预览运行包位于本机 `dist/SpaceRad-source-config-local/`，其中的 Worker 已完成配置窗口的实际输运验收；数据仍外置，尚无正式跨机安装发行。
