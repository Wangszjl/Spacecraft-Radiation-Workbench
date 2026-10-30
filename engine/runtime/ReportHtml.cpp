#include "ReportHtml.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace spacerad::engine {
namespace {

QString escaped(const QString& value) {
    return value.toHtmlEscaped();
}

QString escaped(const std::string& value) {
    return QString::fromUtf8(value.data(), int(value.size())).toHtmlEscaped();
}

QString escapedJson(const Json& value) {
    return escaped(QString::fromUtf8(value.dump(2).c_str()));
}

const Json& field(const Json& value, const char* key) {
    static const Json nullValue;
    return value.is_object() && value.contains(key) ? value.at(key) : nullValue;
}

QString display(const Json& value) {
    if (value.is_null()) return QStringLiteral("未记录");
    if (value.is_string()) return escaped(value.get<std::string>());
    if (value.is_boolean()) return value.get<bool>() ? QStringLiteral("是") : QStringLiteral("否");
    if (value.is_number()) return escaped(QString::fromStdString(value.dump()));
    return escapedJson(value);
}

QString numeric(const Json& value, const QString& suffix = {}) {
    if (!value.is_number()) return value.is_null() ? QStringLiteral("不可估计") : display(value);
    const double number = value.get<double>();
    if (!std::isfinite(number)) return QStringLiteral("不可估计");
    return escaped(QString::number(number, 'g', 8) + suffix);
}

QString item(const QString& label, const Json& value) {
    return QStringLiteral("<dt>%1</dt><dd>%2</dd>").arg(escaped(label), display(value));
}

QString jsonBlock(const Json& value, const QString& empty = QStringLiteral("无可用记录")) {
    if (value.is_null()) return QStringLiteral("<p class=\"muted\">%1</p>").arg(escaped(empty));
    return QStringLiteral("<pre>%1</pre>").arg(escapedJson(value));
}

bool finiteNumber(const Json& value, long double& out) {
    if (!value.is_number()) return false;
    const double number = value.get<double>();
    if (!std::isfinite(number)) return false;
    out = static_cast<long double>(number);
    return std::isfinite(out);
}

const Json& either(const Json& object, const char* first, const char* second) {
    if (object.is_object() && object.contains(first)) return object.at(first);
    return field(object, second);
}

struct PlotRow {
    QString id;
    long double dose = 0;
    long double error = 0;
    bool hasDose = false;
    bool hasError = false;
};

QString svgOverview(const Json& targets, bool hasStatistics) {
    if (!hasStatistics || !targets.is_array() || targets.empty())
        return QStringLiteral("<p class=\"muted\">无可用剂量：报告中没有完整的目标剂量统计。</p>");

    constexpr int maxRows = 40;
    constexpr int width = 1000;
    constexpr int left = 300;
    constexpr int plotWidth = 640;
    constexpr int rowHeight = 28;
    constexpr int top = 34;
    std::vector<PlotRow> rows;
    const int count = std::min<int>(int(targets.size()), maxRows);
    rows.reserve(std::size_t(count));
    long double scale = 0;
    for (int i = 0; i < count; ++i) {
        const auto& target = targets.at(std::size_t(i));
        PlotRow row;
        const auto& id = either(target, "id", "name");
        row.id = id.is_string() ? QString::fromUtf8(id.get<std::string>().c_str()) : QStringLiteral("目标 %1").arg(i + 1);
        row.hasDose = finiteNumber(either(target, "tid_Gy", "dose_Gy"), row.dose);
        row.hasError = finiteNumber(either(target, "tid_standard_error_Gy", "standard_error_Gy"), row.error);
        if (!row.hasError || row.error < 0) row.hasError = false;
        if (row.hasDose) scale = std::max(scale, std::abs(row.dose));
        if (row.hasError) scale = std::max(scale, (row.hasDose ? std::abs(row.dose) : 0.0L) + row.error);
        rows.push_back(std::move(row));
    }
    if (scale == 0) scale = 1;

    const int height = top + rowHeight * count + 34;
    QString svg = QStringLiteral(
        "<svg class=\"dose-chart\" xmlns=\"http://www.w3.org/2000/svg\" role=\"img\" "
        "aria-label=\"目标总剂量与一个标准误差概览\" viewBox=\"0 0 %1 %2\">"
        "<rect width=\"100%\" height=\"100%\" fill=\"#fff\"/>"
        "<text x=\"%3\" y=\"20\" class=\"axis-label\">剂量 Gy（横轴按本图最大绝对值缩放）</text>")
        .arg(width).arg(height).arg(left);

    for (int i = 0; i < count; ++i) {
        const auto& row = rows[std::size_t(i)];
        const int y = top + i * rowHeight;
        const QString label = escaped(row.id);
        svg += QStringLiteral("<text x=\"8\" y=\"%1\" class=\"row-label\">%2</text>").arg(y + 4).arg(label);
        if (!row.hasDose) {
            svg += QStringLiteral("<text x=\"%1\" y=\"%2\" class=\"muted-svg\">无可用剂量</text>").arg(left).arg(y + 4);
            continue;
        }
        const long double doseRatio = std::clamp(row.dose / scale, -1.0L, 1.0L);
        const long double errorRatio = row.hasError ? std::clamp(row.error / scale, 0.0L, 1.0L) : 0.0L;
        const int barWidth = int(std::llround(doseRatio * plotWidth));
        const int centerX = left + std::max(0, barWidth);
        if (doseRatio >= 0) {
            svg += QStringLiteral("<rect x=\"%1\" y=\"%2\" width=\"%3\" height=\"12\" rx=\"2\" fill=\"#1769aa\"/>")
                .arg(left).arg(y - 8).arg(std::max(1, barWidth));
        }
        if (row.hasError) {
            const long double low = std::clamp(doseRatio - errorRatio, 0.0L, 1.0L);
            const long double high = std::clamp(doseRatio + errorRatio, 0.0L, 1.0L);
            const int x1 = left + int(std::llround(low * plotWidth));
            const int x2 = left + int(std::llround(high * plotWidth));
            svg += QStringLiteral("<line x1=\"%1\" y1=\"%2\" x2=\"%3\" y2=\"%2\" stroke=\"#9a3412\" stroke-width=\"2\"/>"
                                  "<line x1=\"%1\" y1=\"%4\" x2=\"%1\" y2=\"%5\" stroke=\"#9a3412\"/>"
                                  "<line x1=\"%3\" y1=\"%4\" x2=\"%3\" y2=\"%5\" stroke=\"#9a3412\"/>")
                .arg(x1).arg(y - 2).arg(x2).arg(y - 7).arg(y + 3);
        } else {
            svg += QStringLiteral("<circle cx=\"%1\" cy=\"%2\" r=\"4\" fill=\"#1769aa\"/>").arg(centerX).arg(y - 2);
        }
    }
    svg += QStringLiteral("<line x1=\"%1\" y1=\"%2\" x2=\"%1\" y2=\"%3\" stroke=\"#475569\"/>"
                          "</svg><p class=\"muted\">图中仅展示前 %4 个目标，每个误差线表示 ±1 个标准误差；表格列出全部目标。"
                          "这是目标剂量概览，不是空间剂量场，也不是出版级科学图。</p>")
        .arg(left).arg(top - 16).arg(top + rowHeight * count).arg(count);
    if (int(targets.size()) > count)
        svg += QStringLiteral("<p class=\"muted\">概览已截取前 %1/%2 个目标。</p>").arg(count).arg(targets.size());
    return svg;
}

QString targetTable(const Json& targets, const Json& zeroHitTargets) {
    if (!targets.is_array() || targets.empty())
        return QStringLiteral("<p class=\"muted\">无可用剂量统计。</p>");
    QString html = QStringLiteral(
        "<div class=\"table-wrap\"><table><thead><tr><th>目标 ID</th><th>材料</th><th>质量 kg</th>"
        "<th>剂量 Gy</th><th>剂量 rad</th><th>标准误差 Gy</th><th>相对标准误差</th>"
        "<th>非零命中</th><th>主粒子入射数</th></tr></thead><tbody>");
    for (const auto& target : targets) {
        const auto& idValue = either(target, "id", "name");
        const QString id = idValue.is_string() ? QString::fromUtf8(idValue.get<std::string>().c_str()) : QStringLiteral("未命名");
        const auto& rse = field(target, "relative_standard_error");
        const auto& nonzero = field(target, "nonzero_histories");
        const bool zeroHit = nonzero.is_number_integer() && nonzero.get<std::int64_t>() == 0;
        QString rseText = numeric(rse);
        if (rse.is_null()) rseText = zeroHit ? QStringLiteral("不可估计（零命中）") : QStringLiteral("不可估计");
        const QString material = display(field(target, "material"));
        html += QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td><td>%4</td><td>%5</td>"
                               "<td>%6</td><td>%7</td><td>%8</td><td>%9</td></tr>")
            .arg(escaped(id), material, numeric(field(target, "mass_kg")),
                 numeric(either(target, "tid_Gy", "dose_Gy")),
                 numeric(either(target, "tid_rad", "dose_rad")),
                 numeric(either(target, "tid_standard_error_Gy", "standard_error_Gy")),
                 rseText, display(nonzero),
                 display(either(target, "primary_entry_histories", "primary_entries")));
    }
    html += QStringLiteral("</tbody></table></div>");
    if (zeroHitTargets.is_array() && !zeroHitTargets.empty())
        html += QStringLiteral("<p class=\"warning\">零命中目标的相对标准误差不可估计；零次命中不表示风险为零。</p>");
    return html;
}

QString batchSummary(const Json& batch) {
    if (batch.is_null()) return QStringLiteral("<p class=\"muted\">非批次任务，或没有批次信息。</p>");
    Json summary = Json::object();
    for (const char* key : {"schema", "completed_batches", "stop_reason", "elapsed_seconds",
                            "merge_rule", "reproducibility", "random_stream_scope", "resume"}) {
        if (batch.contains(key)) summary[key] = batch.at(key);
    }
    if (batch.contains("plan") && batch.at("plan").is_array())
        summary["planned_batches"] = batch.at("plan").size();
    if (batch.contains("batches") && batch.at("batches").is_array()) {
        summary["batch_records"] = batch.at("batches").size();
        std::size_t reused = 0;
        for (const auto& record : batch.at("batches"))
            if (record.is_object() && record.contains("reused") &&
                record.at("reused").is_boolean() && record.at("reused").get<bool>()) ++reused;
        summary["reused_batches"] = reused;
    }
    return jsonBlock(summary);
}

} // namespace

QString renderReproHtml(const Json& report) {
    const Json& summary = field(report, "summary");
    const Json& source = field(report, "source");
    const Json& environment = field(report, "environment");
    const Json& geometry = field(report, "geometry");
    const Json& materials = field(report, "materials");
    const Json& configuration = field(report, "configuration");
    const Json& statistics = field(report, "statistics");
    const Json& targets = field(report, "targets");
    const Json& limitations = field(report, "limitations");
    const Json& replay = field(report, "replay");
    const Json& batch = field(report, "batch");

    const auto& name = field(summary, "name");
    const QString title = name.is_string() ? escaped(name.get<std::string>()) : QStringLiteral("辐射输运复现报告");
    const bool synthetic =
        (field(summary, "synthetic").is_boolean() && field(summary, "synthetic").get<bool>()) ||
        (field(environment, "synthetic").is_boolean() && field(environment, "synthetic").get<bool>());
    const auto& statisticalState = field(summary, "statistical_state");
    const QString state = statisticalState.is_string()
        ? QString::fromUtf8(statisticalState.get<std::string>().c_str()) : QStringLiteral("not_configured");
    const bool hasTargets = targets.is_array() && !targets.empty();
    bool hasDose = false;
    if (targets.is_array()) {
        for (const auto& target : targets) {
            long double dose = 0;
            if (finiteNumber(either(target, "tid_Gy", "dose_Gy"), dose)) {
                hasDose = true;
                break;
            }
        }
    }
    QString stateLabel = state == QStringLiteral("converged") ? QStringLiteral("统计目标已达到")
        : state == QStringLiteral("unconverged") ? QStringLiteral("未收敛")
        : QStringLiteral("未配置统计停止条件");

    QString html = QStringLiteral(
        "<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<meta http-equiv=\"Content-Security-Policy\" content=\"default-src 'none'; style-src 'unsafe-inline'; img-src 'self' data:\">"
        "<title>%1</title><style>"
        ":root{color-scheme:light}*{box-sizing:border-box}body{margin:0;background:#f1f5f9;color:#172033;"
        "font:15px/1.55 'Microsoft YaHei',sans-serif}.page{max-width:1120px;margin:24px auto;padding:36px 44px;"
        "background:#fff;box-shadow:0 4px 20px #0f172a18}h1{margin:0 0 8px;font-size:28px}h2{margin-top:30px;"
        "padding-bottom:6px;border-bottom:1px solid #cbd5e1;font-size:20px}.badges{display:flex;gap:8px;flex-wrap:wrap;margin:16px 0 22px}"
        ".badge{display:inline-block;padding:4px 10px;border-radius:14px;background:#e2e8f0;font-size:13px}"
        ".warning{color:#9a3412;background:#fff7ed;padding:10px 12px;border-left:4px solid #ea580c}"
        ".muted{color:#64748b}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(240px,1fr));gap:8px 24px}"
        "dl{display:grid;grid-template-columns:150px 1fr;gap:6px 12px;margin:8px 0}dt{font-weight:600;color:#475569}"
        "dd{margin:0;overflow-wrap:anywhere}table{width:100%;border-collapse:collapse;font-size:13px}th,td{padding:7px 8px;"
        "border:1px solid #cbd5e1;text-align:left;vertical-align:top}th{background:#eaf0f6}.table-wrap{overflow-x:auto}"
        "pre{padding:14px;background:#f8fafc;border:1px solid #e2e8f0;white-space:pre-wrap;overflow-wrap:anywhere;"
        "font:12px/1.45 Consolas,monospace}.dose-chart{display:block;width:100%;max-height:1600px;border:1px solid #e2e8f0}"
        ".row-label{font:12px sans-serif;fill:#172033}.axis-label{font:13px sans-serif;fill:#334155}"
        ".muted-svg{font:12px sans-serif;fill:#64748b}.command{font:13px/1.5 Consolas,monospace;overflow-wrap:anywhere}"
        "footer{margin-top:34px;padding-top:12px;border-top:1px solid #cbd5e1;color:#64748b;font-size:12px}"
        "@media print{body{background:#fff}.page{max-width:none;margin:0;padding:12mm;box-shadow:none}h2{break-after:avoid}"
        "table,svg,pre{break-inside:avoid}.table-wrap{overflow:visible}}"
        "</style></head><body><main class=\"page\"><h1>%1</h1>"
        "<div class=\"badges\"><span class=\"badge\">未进行外部物理验证</span><span class=\"badge\">%2</span>%3</div>"
        "<p>本报告整理一次输运任务的输入身份、运行配置、统计结果与可复现信息。软件回归或统计目标达成不等于外部物理验证。</p>")
        .arg(title, escaped(stateLabel),
             synthetic ? QStringLiteral("<span class=\"badge\">合成数据（synthetic）</span>") : QString());

    const auto& status = field(summary, "job_status");
    html += QStringLiteral("<section><h2>任务概况</h2><dl class=\"grid\">%1%2%3%4%5%6%7%8</dl>")
        .arg(item(QStringLiteral("任务状态"), status),
             item(QStringLiteral("任务类型"), field(summary, "kind")),
             item(QStringLiteral("完成历史数"), field(summary, "histories")),
             item(QStringLiteral("请求历史数"), field(summary, "requested_histories")),
             item(QStringLiteral("统计状态"), statisticalState),
             item(QStringLiteral("源归一化预期粒子数"), field(environment, "expected_primaries")),
             item(QStringLiteral("报告说明"), Json("软件/数值结果不表示物理验证完成")),
             QString());
    html += QStringLiteral("<p class=\"warning\">%1</p></section>")
        .arg(field(statistics, "caution").is_null()
                 ? QStringLiteral("结果只在记录的输入、物理列表、数据集、统计预算和模型范围内适用。")
                 : display(field(statistics, "caution")));

    const Json& projectIdentity = field(source, "project_identity");
    const Json& resultIdentity = field(source, "result_identity");
    html += QStringLiteral("<section><h2>输入身份与来源</h2><dl>%1%2%3%4%5%6%7</dl></section>")
        .arg(item(QStringLiteral("任务目录"), field(source, "job_directory")),
             item(QStringLiteral("输入文件名"), field(projectIdentity, "file_name")),
             item(QStringLiteral("输入 SHA-256"), field(projectIdentity, "sha256")),
             item(QStringLiteral("输入字节数"), field(projectIdentity, "size_bytes")),
             item(QStringLiteral("结果文件名"), field(resultIdentity, "file_name")),
             item(QStringLiteral("结果 SHA-256"), field(resultIdentity, "sha256")),
             item(QStringLiteral("结果字节数"), field(resultIdentity, "size_bytes")));

    html += QStringLiteral("<section><h2>环境与源</h2><dl>%1%2%3%4%5%6%7%8%9</dl>")
        .arg(item(QStringLiteral("谱物理量"), field(environment, "quantity")),
             item(QStringLiteral("谱单位"), field(environment, "unit")),
             item(QStringLiteral("任务时长 s"), field(environment, "duration_s")),
             item(QStringLiteral("源半径 mm"), field(environment, "source_radius_mm")),
             item(QStringLiteral("环境来源"), field(environment, "source_label")),
             item(QStringLiteral("归一化约定"), field(environment, "normalization")),
             item(QStringLiteral("缺失元数据字段"), field(environment, "missing_metadata")),
             item(QStringLiteral("环境元数据"), field(environment, "metadata")),
             item(QStringLiteral("合成标记"), field(environment, "synthetic")));

    html += QStringLiteral("<section><h2>目标剂量与 ±1SE 概览</h2>");
    html += svgOverview(targets, hasTargets && hasDose);
    if (hasTargets && hasDose) html += targetTable(targets, field(statistics, "zero_hit_targets"));
    html += QStringLiteral("</section>");

    html += QStringLiteral("<section><h2>统计状态</h2><dl>%1%2%3%4</dl>%5%6</section>")
        .arg(item(QStringLiteral("停止条件"), field(statistics, "criterion")),
             item(QStringLiteral("收敛判定"), field(statistics, "convergence")),
             item(QStringLiteral("零命中目标"), field(statistics, "zero_hit_targets")),
             item(QStringLiteral("未收敛目标"), field(statistics, "unconverged_targets")),
             QStringLiteral("<p class=\"warning\">零命中时相对标准误差不可估计；零次命中不表示风险为零。</p>"),
             batchSummary(batch));

    html += QStringLiteral("<section><h2>几何与材料审核</h2><h3>几何状态：%1</h3>%2"
                           "<h3>实际几何审核</h3>%3<h3>材料快照状态</h3><dl>%4</dl>"
                           "<h3>材料审核与定义</h3>%5</section>")
        .arg(display(field(geometry, "status")),
             jsonBlock(field(geometry, "cpu_contract")),
             jsonBlock(field(geometry, "actual_audit"), QStringLiteral("没有 Geant4 实际几何审核")),
             item(QStringLiteral("输入材料快照已固定"), field(materials, "input_snapshot_pinned")),
             jsonBlock(field(materials, "audit"), QStringLiteral("没有材料审核记录")));

    html += QStringLiteral("<section><h2>计算配置与运行环境</h2><dl>%1%2%3%4%5%6%7</dl>"
                           "<h3>请求配置</h3>%8<h3>生效配置</h3>%9<h3>运行时审核</h3>%10")
        .arg(item(QStringLiteral("工程 schema"), field(configuration, "project_schema")),
             item(QStringLiteral("输运引擎"), field(configuration, "engine")),
             item(QStringLiteral("Geant4 版本"), field(configuration, "engine_version")),
             item(QStringLiteral("Worker SHA-256"), field(field(configuration, "worker"), "sha256")),
             item(QStringLiteral("Worker 路径"), field(field(configuration, "worker"), "path")),
             item(QStringLiteral("统计状态"), statisticalState),
             item(QStringLiteral("任务状态"), status))
        .arg(jsonBlock(field(configuration, "requested")),
             jsonBlock(field(configuration, "effective")),
             jsonBlock(field(configuration, "runtime_audit"))) + QStringLiteral("</section>");

    if(report.contains("voxel_summary"))html+=QStringLiteral("<section><h2>体素与逐事件沉积能谱</h2><p>体素为同材料敏感箱体的实体细分；无命中不表示零风险。最大估计格不是已经确认的热点。能谱为逐源历史沉积能概率，不是粒子注量谱。</p>%1%2</section>")
        .arg(jsonBlock(report.at("voxel_summary")),jsonBlock(report.at("edep_spectra")));
    html += QStringLiteral("<section><h2>限制与未覆盖范围</h2>");
    if (limitations.is_array() && !limitations.empty()) {
        html += QStringLiteral("<ul>");
        for (const auto& limitation : limitations)
            html += QStringLiteral("<li>%1</li>").arg(display(limitation));
        html += QStringLiteral("</ul>");
    } else {
        html += QStringLiteral("<p class=\"muted\">未提供限制清单。</p>");
    }
    html += QStringLiteral("<p class=\"warning\">本报告不推断空间剂量场，不把软件测试或统计收敛称作物理验证完成。</p></section>");

    html += QStringLiteral("<section><h2>复现信息</h2><dl>%1%2%3%4</dl><h3>命令</h3><pre class=\"command\">%5</pre></section>")
        .arg(item(QStringLiteral("可重跑"), field(replay, "eligible")),
             item(QStringLiteral("复现类型"), field(replay, "kind")),
             item(QStringLiteral("Worker SHA-256"), field(replay, "worker_sha256")),
             item(QStringLiteral("重跑批次索引"), field(replay, "indices")),
             display(field(replay, "command")));
    html += QStringLiteral("<footer>生成内容为离线 HTML。所有工程与来源文本均作为转义文本展示；图表仅概览目标总剂量和一个标准误差。</footer>"
                           "</main></body></html>");
    return html;
}

} // namespace spacerad::engine
