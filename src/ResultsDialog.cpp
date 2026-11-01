#include "ResultsDialog.h"
#include "spacerad/Results.h"
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <optional>
#include <stdexcept>

namespace spacerad {
namespace {
QString s(const Json& value) { return value.is_null() ? QString::fromUtf8("—") : QString::fromStdString(value.is_string() ? value.get<std::string>() : value.dump()); }
QString state(const std::string& value) {
    if (value == "matched") return QObject::tr("敏感区一致");
    if (value == "target_changed") return QObject::tr("敏感区已变化");
    if (value == "baseline_only") return QObject::tr("仅 A 存在");
    return QObject::tr("仅 B 存在");
}
QString warning(const std::string& value) {
    if (value == "source_settings_differ") return QObject::tr("源谱、暴露时长或源球设置不同，差异不能仅归因于屏蔽。");
    if (value == "physics_or_runtime_assets_differ") return QObject::tr("物理列表、截断、材料快照或运行资产不同。");
    if (value == "environment_provenance_differs") return QObject::tr("环境来源或元数据不同。");
    if (value.rfind("zero_baseline_ratio_undefined:", 0) == 0) return QObject::tr("A 剂量为零，比值未定义：%1").arg(QString::fromStdString(value.substr(30)));
    return QObject::tr("仅列出各次 MC 标准误差；协方差未知，不估计差值误差或显著性。零命中不表示零风险。");
}
class ResultsDialog final : public QDialog {
    std::optional<ArchivedRun> a, b;
    Json report;
    QLineEdit *root, *apath, *bpath;
    QTreeWidget* jobs;
    QLabel *status, *identity;
    QTableWidget* table;
    QPushButton *csv, *json;
public:
    ResultsDialog(QWidget* parent, const QString& baseline, const QString& candidate) : QDialog(parent) {
        setObjectName("spaceradResultsDialog");
        setAttribute(Qt::WA_DeleteOnClose);
        setWindowTitle(tr("SpaceRad · 历史任务与剂量对比"));
        resize(1100, 720);
        auto* layout = new QVBoxLayout(this);
        auto* help = new QLabel(tr("载入完整任务目录（project.json + result.json）。离线核对保存快照的一致性；无需原 Worker 或数据路径。不会更改当前工程。"), this);
        help->setWordWrap(true); layout->addWidget(help);
        auto* rootRow = new QHBoxLayout;
        root = new QLineEdit(this); root->setObjectName("spaceradHistoryRoot");
        root->setText(baseline.isEmpty() ? QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).filePath("SpaceRad/jobs") : QFileInfo(baseline).absolutePath());
        rootRow->addWidget(root);
        auto* browse = new QPushButton(tr("浏览任务根目录"), this); rootRow->addWidget(browse);
        auto* refresh = new QPushButton(tr("刷新列表"), this); rootRow->addWidget(refresh); layout->addLayout(rootRow);
        jobs = new QTreeWidget(this); jobs->setObjectName("spaceradHistoryJobs");
        jobs->setHeaderLabels({tr("任务目录"), tr("文件状态（选中后载入核对）")});
        jobs->setMaximumHeight(150); jobs->setRootIsDecorated(false); layout->addWidget(jobs);
        auto* useRow = new QHBoxLayout;
        auto* useA = new QPushButton(tr("选中任务 → A 基准"), this); useA->setObjectName("spaceradHistoryUseA"); useRow->addWidget(useA);
        auto* useB = new QPushButton(tr("选中任务 → B 候选"), this); useB->setObjectName("spaceradHistoryUseB"); useRow->addWidget(useB);
        auto* clear = new QPushButton(tr("清除 B"), this); clear->setObjectName("spaceradHistoryClearB"); useRow->addWidget(clear); useRow->addStretch(); layout->addLayout(useRow);
        auto addSide = [&](bool baselineSide) {
            auto* row = new QHBoxLayout;
            row->addWidget(new QLabel(baselineSide ? tr("A 基准") : tr("B 候选"), this));
            auto* path = new QLineEdit(this); path->setObjectName(baselineSide ? "spaceradArchiveA" : "spaceradArchiveB"); row->addWidget(path);
            auto* choose = new QPushButton(tr("选择目录"), this); row->addWidget(choose);
            auto* load = new QPushButton(tr("载入"), this); load->setObjectName(baselineSide ? "spaceradArchiveLoadA" : "spaceradArchiveLoadB"); row->addWidget(load);
            layout->addLayout(row);
            connect(path, &QLineEdit::textEdited, this, [=] { (baselineSide ? a : b).reset(); render(); });
            connect(load, &QPushButton::clicked, this, [=] { loadSide(baselineSide, path->text()); });
            connect(choose, &QPushButton::clicked, this, [=] { auto dir = QFileDialog::getExistingDirectory(this, tr("选择完成任务目录"), path->text()); if (!dir.isEmpty()) { path->setText(dir); loadSide(baselineSide, dir); } });
            return path;
        };
        apath = addSide(true); bpath = addSide(false);
        identity = new QLabel(this); identity->setObjectName("spaceradArchiveIdentity"); identity->setTextFormat(Qt::PlainText); identity->setWordWrap(true); identity->setTextInteractionFlags(Qt::TextSelectableByMouse); layout->addWidget(identity);
        status = new QLabel(this); status->setObjectName("spaceradComparisonStatus"); status->setTextFormat(Qt::PlainText); status->setWordWrap(true); layout->addWidget(status);
        table = new QTableWidget(0, 10, this); table->setObjectName("spaceradComparisonTable");
        table->setHorizontalHeaderLabels({tr("敏感区"), tr("匹配状态"), tr("条件核对"), "A Gy", "A SE Gy", "B Gy", "B SE Gy", "B−A Gy", "B/A", tr("非零历史 A / B")});
        table->setEditTriggers(QAbstractItemView::NoEditTriggers); table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents); layout->addWidget(table, 1);
        auto* exports = new QHBoxLayout;
        csv = new QPushButton(tr("导出对比 CSV"), this); csv->setObjectName("spaceradComparisonCsv"); exports->addWidget(csv);
        json = new QPushButton(tr("导出核对摘要 JSON"), this); json->setObjectName("spaceradComparisonJson"); exports->addWidget(json); exports->addStretch();
        auto* close = new QPushButton(tr("关闭"), this); exports->addWidget(close); layout->addLayout(exports);
        connect(close, &QPushButton::clicked, this, &QDialog::close);
        connect(csv, &QPushButton::clicked, this, [this] { exportReport(true); });
        connect(json, &QPushButton::clicked, this, [this] { exportReport(false); });
        connect(browse, &QPushButton::clicked, this, [this] { auto path = QFileDialog::getExistingDirectory(this, tr("选择任务根目录"), root->text()); if (!path.isEmpty()) { root->setText(path); scan(); } });
        connect(refresh, &QPushButton::clicked, this, [this] { scan(); });
        connect(useA, &QPushButton::clicked, this, [this] { if (auto* item = jobs->currentItem()) { apath->setText(item->data(0, Qt::UserRole).toString()); loadSide(true, apath->text()); } });
        connect(useB, &QPushButton::clicked, this, [this] { if (auto* item = jobs->currentItem()) { bpath->setText(item->data(0, Qt::UserRole).toString()); loadSide(false, bpath->text()); } });
        connect(clear, &QPushButton::clicked, this, [this] { b.reset(); bpath->clear(); render(); });
        scan(); render();
        if (!baseline.isEmpty()) { apath->setText(baseline); loadSide(true, baseline); }
        if (!candidate.isEmpty()) { bpath->setText(candidate); loadSide(false, candidate); }
    }
private:
    void scan() {
        jobs->clear();
        if (root->text().trimmed().isEmpty() || !QFileInfo(root->text()).isDir()) {
            status->setText(tr("任务根目录不存在；可直接指定 A / B 路径。"));
            return;
        }
        QDirIterator it(root->text(), QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks);
        int visited = 0;
        while (it.hasNext() && visited < 500) {
            const auto path = it.next(); ++visited;
            const bool project = QFileInfo(QDir(path).filePath("project.json")).isFile();
            const bool result = QFileInfo(QDir(path).filePath("result.json")).isFile();
            if (!project && !result) continue;
            auto* item = new QTreeWidgetItem(jobs, {QFileInfo(path).fileName(), project && result ? tr("快照和结果存在，待核对") : tr("任务不完整")});
            item->setData(0, Qt::UserRole, path); item->setToolTip(0, path);
        }
        jobs->setHeaderLabels({tr("任务目录（每次最多扫描 500 个直接子目录）"), tr("文件状态（载入后核对）")});
        jobs->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
        if (it.hasNext()) status->setText(tr("扫描达到 500 个目录上限；可直接指定 A / B 路径。"));
    }
    void loadSide(bool baselineSide, const QString& directory) {
        // A failed new selection clears the old side so stale data cannot be
        // exported under a newly typed directory label.
        auto& side = baselineSide ? a : b;
        side.reset();
        try { side = loadArchivedRun(directory); render(); }
        catch (const std::exception& e) { render(); status->setText(tr("载入 %1 失败：%2").arg(baselineSide ? "A" : "B", QString::fromUtf8(e.what()))); }
    }
    void render() {
        report = nullptr; table->setRowCount(0); csv->setEnabled(false); json->setEnabled(false);
        identity->clear(); status->setText(tr("请选择 A 任务；可再载入 B 对比。核对不认证文件生成者，也不验证剂量物理模型。"));
        QStringList identities;
        for (auto side : {&a, &b}) if (*side) {
            const auto summary = (*side)->summary();
            identities << tr("%1 · %2 · %3 历史 · Geant4 %4 · %5\n结果 SHA-256：%6\n%7")
                .arg(side == &a ? "A" : "B", s(summary.at("name")), s(summary.at("histories")), s(summary.at("engine_version")), summary.at("synthetic").get<bool>() ? tr("合成谱") : tr("外部谱"), s(summary.at("result_sha256")), (*side)->directory());
        }
        identity->setText(identities.join('\n'));
        if (!a) return;
        if (b) {
            try { report = compareRuns(*a, *b); }
            catch (const std::exception& e) { status->setText(tr("对比失败：%1").arg(QString::fromUtf8(e.what()))); return; }
            QStringList messages;
            for (const auto& w : report.at("warnings")) messages << warning(w);
            status->setText(messages.join('\n')); csv->setEnabled(true);
            for (const auto& row : report.at("rows")) {
                const auto& at = row.at("baseline"); const auto& bt = row.at("candidate");
                auto cell = [&](const Json& t, const char* key) { return t.is_null() ? QString::fromUtf8("—") : s(t.at(key)); };
                append({s(row.at("id")), state(row.at("status")), row.at("controlled_comparison").get<bool>() ? tr("设置一致") : tr("需核对"),
                    cell(at, "tid_Gy"), cell(at, "tid_standard_error_Gy"), cell(bt, "tid_Gy"), cell(bt, "tid_standard_error_Gy"), s(row.at("delta_Gy")), s(row.at("ratio_B_over_A")),
                    cell(at, "nonzero_histories") + " / " + cell(bt, "nonzero_histories")});
            }
        } else {
            report = a->summary();
            status->setText(tr("A 已载入；仅完成离线文件一致性核对。零命中不表示零风险；DDD / SEE 与独立剂量物理验证尚未完成。"));
            for (const auto& t : a->result().at("targets")) append({s(t.at("id")), tr("仅查看 A"), {}, s(t.at("tid_Gy")), s(t.at("tid_standard_error_Gy")), {}, {}, {}, {}, s(t.at("nonzero_histories"))});
        }
        json->setEnabled(true);
    }
    void append(const QStringList& values) {
        const int row = table->rowCount(); table->insertRow(row);
        for (int col = 0; col < values.size(); ++col) { auto* item = new QTableWidgetItem(values[col]); item->setToolTip(values[col]); table->setItem(row, col, item); }
    }
    void exportReport(bool asCsv) {
        const auto path = QFileDialog::getSaveFileName(this, tr("导出到新文件"), {}, asCsv ? "CSV (*.csv)" : "JSON (*.json)");
        if (path.isEmpty()) return;
        try {
            if (QFileInfo::exists(path)) throw std::runtime_error("Choose a new export filename");
            const auto bytes = asCsv ? comparisonCsv(report) : QByteArray::fromStdString(report.dump(2));
            QSaveFile file(path);
            if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) throw std::runtime_error("Cannot save comparison export");
            status->setText(tr("已导出：%1").arg(path));
        } catch (const std::exception& e) { status->setText(QString::fromUtf8(e.what())); }
    }
};
}
void showResultsDialog(QWidget* parent, const QString& baseline, const QString& candidate) {
    auto* dialog = new ResultsDialog(parent, baseline, candidate);
    dialog->show(); dialog->raise();
}
}
