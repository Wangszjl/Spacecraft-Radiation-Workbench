#include "ImportanceDialog.h"
#include "spacerad/ImportanceReview.h"
#include <QDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSaveFile>
#include <QTableWidget>
#include <QVBoxLayout>
#include <stdexcept>

namespace spacerad {
namespace {
QString string(const Json& x) {
    if(x.is_null())return QString::fromUtf8("—");
    if(x.is_number())return QString::number(x.get<double>(),'g',17);
    return QString::fromStdString(x.is_string()?x.get<std::string>():x.dump());
}
QString display(const Json& x) {
    return x.is_number()?QString::number(x.get<double>(),'g',6):string(x);
}
QString percent(const Json& x) {
    return x.is_null()?string(x):QString::number(x.get<double>()*100,'g',5);
}
QString regionList(const Json& values) {
    QStringList labels;
    for(const auto& item:values){
        const auto name=item.get<std::string>();
        const auto slash=name.find('/');
        const auto particle=name.substr(0,slash);
        const auto particleLabel=particle=="e-"?QObject::tr("电子"):particle=="proton"?QObject::tr("质子"):QString::fromStdString(particle);
        labels<<particleLabel+
            (name.substr(slash+1)=="body"?QObject::tr("·主体谱"):QObject::tr("·高能尾部"));
    }
    return labels.empty()?QObject::tr("无"):labels.join(QObject::tr("，"));
}
QString status(const Json& value) {
    if(value=="passes_screen")return QObject::tr("通过 MC 筛查");
    if(value=="sufficient_hits")return QObject::tr("命中数达标");
    if(value=="zero_hit")return QObject::tr("零命中");
    if(value=="low_hits")return QObject::tr("命中不足");
    return QObject::tr("统计不足");
}
QString quoted(const QString& raw) {
    auto s=raw;
    if(!s.isEmpty()&&(s.front().isSpace()||QString("=+-@").contains(s.front())))s.prepend('\'');
    s.replace('"',"\"\"");
    return '"'+s+'"';
}
QByteArray csv(const Json& report) {
    QString out="target,target_status,particle,region,energy_min_MeV,energy_max_MeV,sampled_histories,nonzero_histories,tid_contribution_Gy,observed_contribution_SE_Gy,fraction_of_observed_target_dose,region_status\n";
    for(const auto& target:report.at("targets"))for(const auto& group:target.at("groups")){
        QStringList line{quoted(string(target.at("id"))),quoted(string(target.at("status"))),
            quoted(string(group.at("particle"))),quoted(string(group.at("region")))};
        for(const auto& value:group.at("energy_range_MeV"))line<<string(value);
        for(const auto* key:{"sampled_histories","nonzero_histories","tid_contribution_Gy",
                            "observed_contribution_standard_error_Gy","fraction_of_observed_target_dose"})
            line<<(group.at(key).is_null()?QString{}:string(group.at(key)));
        line<<quoted(string(group.at("status")));
        out+=line.join(',')+'\n';
    }
    return out.toUtf8();
}
class ImportanceDialog final:public QDialog {
    Json report;
    QTableWidget *targets,*groups;
    QLabel *feedback;
public:
    ImportanceDialog(QWidget* parent,const Project& p,const Json& result,const QString& selected)
        :QDialog(parent),report(importanceReview(p,result)) {
        if(report.at("status")!="reviewed")throw std::runtime_error("This result has no importance sampling audit");
        setObjectName("spaceradImportanceDialog");setAttribute(Qt::WA_DeleteOnClose);
        setWindowTitle(tr("SpaceRad · 加权输运统计明细"));resize(1160,720);
        auto* layout=new QVBoxLayout(this);
        auto* headline=new QLabel(tr("%1 个靶区中 %2 个通过 MC 筛查 · %3 历史")
            .arg(report.at("targets").size()).arg(report.at("passing_targets").get<int>()).arg(report.at("histories").get<int>()),this);
        headline->setObjectName("spaceradImportanceSummary");layout->addWidget(headline);
        auto* warning=new QLabel(tr("筛查规则：每个粒子／主体或尾部区域至少 20 次非零沉积，靶区合计经验 RSE ≤20%。\n零命中和低命中区域没有剂量上界；经验误差小不代表完整谱已收敛。各区域共享同一批事件，区域误差不可按独立估计平方相加。通过 MC 筛查也不构成物理验证或工程验收。"),this);
        warning->setObjectName("spaceradImportanceWarning");warning->setWordWrap(true);
        warning->setStyleSheet("color:#805316;background:#fff4da;padding:8px");layout->addWidget(warning);
        targets=new QTableWidget(0,7,this);targets->setObjectName("spaceradImportanceTargets");
        targets->setHorizontalHeaderLabels({tr("敏感区"),"TID Gy",tr("经验 SE Gy"),tr("经验 RSE %"),tr("筛查状态"),tr("不足区域"),tr("零命中区域")});
        targets->setMinimumHeight(210);
        targets->setEditTriggers(QAbstractItemView::NoEditTriggers);targets->setSelectionBehavior(QAbstractItemView::SelectRows);
        targets->setSelectionMode(QAbstractItemView::SingleSelection);
        targets->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        layout->addWidget(targets,1);
        auto* regionHelp=new QLabel(tr("选中敏感区查看来源分组。剂量贡献已包含完整事件似然权重和原谱物理暴露；分组 SE 单独描述该分组，不能直接合成为整区误差。"),this);
        regionHelp->setWordWrap(true);layout->addWidget(regionHelp);
        groups=new QTableWidget(0,9,this);groups->setObjectName("spaceradImportanceGroups");
        groups->setHorizontalHeaderLabels({tr("入射粒子"),tr("来源区域"),tr("物理能段 MeV"),tr("抽样历史"),tr("非零历史"),tr("剂量贡献 Gy"),tr("贡献经验 SE Gy"),tr("占已观测剂量 %"),tr("区域状态")});
        groups->setEditTriggers(QAbstractItemView::NoEditTriggers);groups->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        layout->addWidget(groups,2);
        auto* actions=new QHBoxLayout;
        auto* json=new QPushButton(tr("导出统计 JSON"),this);json->setObjectName("spaceradImportanceJson");
        auto* table=new QPushButton(tr("导出分组 CSV"),this);table->setObjectName("spaceradImportanceCsv");
        auto* close=new QPushButton(tr("关闭"),this);actions->addWidget(json);actions->addWidget(table);actions->addStretch();actions->addWidget(close);layout->addLayout(actions);
        feedback=new QLabel(this);feedback->setObjectName("spaceradImportanceExportStatus");feedback->setWordWrap(true);layout->addWidget(feedback);
        connect(close,&QPushButton::clicked,this,&QDialog::close);
        connect(json,&QPushButton::clicked,this,[this]{exportReport(false);});
        connect(table,&QPushButton::clicked,this,[this]{exportReport(true);});
        connect(targets,&QTableWidget::currentCellChanged,this,[this](int row,int,int,int){renderGroups(row);});
        int focus=0;
        for(const auto& t:report.at("targets")){
            const int row=targets->rowCount();targets->insertRow(row);
            QStringList values{string(t.at("id")),display(t.at("tid_Gy")),display(t.at("observed_standard_error_Gy")),
                percent(t.at("observed_relative_standard_error")),status(t.at("status")),regionList(t.at("low_hit_regions")),regionList(t.at("zero_hit_regions"))};
            for(int c=0;c<values.size();++c){auto* item=new QTableWidgetItem(values[c]);item->setToolTip(values[c]);targets->setItem(row,c,item);}
            targets->item(row,1)->setToolTip(string(t.at("tid_Gy")));
            targets->item(row,2)->setToolTip(string(t.at("observed_standard_error_Gy")));
            targets->item(row,3)->setToolTip(string(t.at("observed_relative_standard_error")));
            targets->item(row,4)->setData(Qt::UserRole,string(t.at("status")));
            if(string(t.at("id"))==selected)focus=row;
        }
        if(targets->rowCount())targets->setCurrentCell(focus,0);
    }
private:
    void renderGroups(int row) {
        groups->setRowCount(0);if(row<0||row>=int(report.at("targets").size()))return;
        for(const auto& g:report.at("targets").at(row).at("groups")){
            const int i=groups->rowCount();groups->insertRow(i);
            QStringList values{string(g.at("particle")),g.at("region")=="body"?tr("主体谱"):tr("高能尾部"),
                display(g.at("energy_range_MeV").at(0))+" – "+display(g.at("energy_range_MeV").at(1)),
                string(g.at("sampled_histories")),string(g.at("nonzero_histories")),display(g.at("tid_contribution_Gy")),
                display(g.at("observed_contribution_standard_error_Gy")),percent(g.at("fraction_of_observed_target_dose")),status(g.at("status"))};
            for(int c=0;c<values.size();++c){auto* item=new QTableWidgetItem(values[c]);item->setToolTip(values[c]);groups->setItem(i,c,item);}
            groups->item(i,2)->setToolTip(string(g.at("energy_range_MeV")));
            groups->item(i,5)->setToolTip(string(g.at("tid_contribution_Gy")));
            groups->item(i,6)->setToolTip(string(g.at("observed_contribution_standard_error_Gy")));
            groups->item(i,7)->setToolTip(string(g.at("fraction_of_observed_target_dose")));
            groups->item(i,8)->setData(Qt::UserRole,string(g.at("status")));
        }
    }
    void exportReport(bool table) {
        const auto path=QFileDialog::getSaveFileName(this,tr("导出统计到新文件"),{},table?"CSV (*.csv)":"JSON (*.json)");
        if(path.isEmpty())return;
        try{
            if(QFileInfo::exists(path))throw std::runtime_error("Choose a new export file");
            const auto bytes=table?csv(report):QByteArray::fromStdString(report.dump(2));
            QSaveFile file(path);
            if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size()||!file.commit())throw std::runtime_error("Cannot save importance review");
            feedback->setText(tr("已导出：%1").arg(path));
        }catch(const std::exception& e){feedback->setText(QString::fromUtf8(e.what()));}
    }
};
}
void showImportanceDialog(QWidget* parent,const Project& p,const Json& result,const QString& target) {
    auto* dialog=new ImportanceDialog(parent,p,result,target);dialog->show();dialog->raise();
}
}
