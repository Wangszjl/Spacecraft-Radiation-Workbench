#include "TargetFluenceDialog.h"
#include "Geometry.h"
#include "runtime/TargetFluence.h"
#include <spacerad/Results.h>
#include <spacerad/DeviceResponse.h>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFile>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QTableWidget>
#include <QVBoxLayout>
namespace spacerad {
std::optional<std::pair<Json,QString>> configureTargetFluence(QWidget* parent,const Project& p,const QString& path){
    const auto geometry=engine::compileGeometry(p);
    auto* dialog=new QDialog(parent);
    QPointer<QDialog> alive(dialog);
    dialog->setObjectName("spaceradTargetFluenceSetup");
    dialog->setWindowTitle(QObject::tr("靶区注量输运设置"));
    dialog->resize(680,280);
    auto* form=new QFormLayout(dialog);
    auto* target=new QComboBox(dialog);
    target->setObjectName("spaceradTargetFluenceTarget");
    for(const auto& v:geometry.volumes)if(v.sensitive)target->addItem(QString::fromStdString(v.id));
    form->addRow(QObject::tr("敏感靶区"),target);
    auto* edges=new QLineEdit("0.000001, 0.1, 0.5, 1, 10, 30, 100",dialog);
    edges->setObjectName("spaceradTargetFluenceEdges");
    form->addRow(QObject::tr("能箱边界 MeV"),edges);
    auto* output=new QLineEdit(path,dialog);
    output->setObjectName("spaceradTargetFluenceOutput");
    form->addRow(QObject::tr("新任务目录"),output);
    auto* help=new QLabel(QObject::tr("按步前动能对靶区内轨迹长度分箱。当前计分可与重要性抽样组合，不能与体素同时启用。"),dialog);
    help->setWordWrap(true);form->addRow(help);
    auto* status=new QLabel(dialog);status->setObjectName("spaceradTargetFluenceSetupStatus");status->setTextFormat(Qt::PlainText);status->setWordWrap(true);form->addRow(status);
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QObject::tr("开始输运"));
    form->addRow(buttons);
    std::optional<std::pair<Json,QString>> chosen;
    QObject::connect(buttons,&QDialogButtonBox::rejected,dialog,&QDialog::reject);
    QObject::connect(buttons,&QDialogButtonBox::accepted,dialog,[&]{
        try{
            Json values=Json::array();
            for(const auto& token:edges->text().split(QRegularExpression("[,，\\s]+"),Qt::SkipEmptyParts)){
                bool ok=false;const auto value=token.toDouble(&ok);
                if(!ok)throw std::runtime_error("Energy edges must be numeric");
                values.push_back(value);
            }
            auto spec=engine::TargetFluenceSpec::fromJson({{"schema","spacerad.target-fluence-settings/1"},{"target_id",target->currentText().toStdString()},{"energy_edges_MeV",values}});
            spec.volume(p);
            if(output->text().trimmed().isEmpty()||!QFileInfo(output->text()).isAbsolute()||QFileInfo::exists(output->text()))
                throw std::runtime_error("Choose a new absolute job directory");
            chosen=std::make_pair(spec.toJson(),output->text());dialog->accept();
        }catch(const std::exception& e){status->setText(QString::fromUtf8(e.what()));}
    });
    dialog->exec();
    if(!alive)return std::nullopt;
    delete dialog;
    return chosen;
}
namespace {
QString text(const Json& v){
    if(v.is_null())return QObject::tr("未知");
    if(v.is_number_float())return QString::number(v.get<double>(),'g',10);
    return QString::fromStdString(v.is_string()?v.get<std::string>():v.dump());
}
class Review final:public QDialog {
    QLineEdit *archive,*response;
    QLabel* status;
    QTableWidget* bins;
    QPlainTextEdit* folded;
    QPushButton *fold,*exportButton;
    Json report;
    void clearFold(){report=nullptr;folded->clear();exportButton->setEnabled(false);}
    std::optional<ArchivedRun> load(){
        clearFold();bins->setRowCount(0);fold->setEnabled(false);
        try{
            auto run=loadArchivedRun(archive->text());
            if(!run.result().contains("target_fluence"))throw std::runtime_error("This archive has no target fluence scoring");
            const auto& f=run.result().at("target_fluence");
            const auto& spec=f.at("settings");const auto& edges=spec.at("energy_edges_MeV");
            auto add=[&](const QString& particle,const QString& range,const Json& row){
                QStringList values{particle,range,text(row.at("fluence_cm-2")),text(row.at("standard_error_cm-2")),text(row.at("moments").at("nonzero_histories"))};
                const auto r=bins->rowCount();bins->insertRow(r);
                for(int c=0;c<values.size();++c)bins->setItem(r,c,new QTableWidgetItem(values[c]));
            };
            add(tr("总注量"),tr("全能域"),f.at("total"));
            add(tr("其他粒子"),tr("全能域"),f.at("other_species"));
            for(const auto& [species,rows]:f.at("species").items())for(size_t i=0;i<rows.size();++i){
                const auto range=i==0?tr("下溢"):i==edges.size()?tr("上溢"):QString("[%1, %2)").arg(text(edges[i-1]),text(edges[i]));
                add(QString::fromStdString(species),range,rows[i]);
            }
            status->setText(tr("档案已核验：%1。零命中能箱的标准误未知。").arg(text(spec.at("target_id"))));
            fold->setEnabled(true);
            return run;
        }catch(const std::exception& e){status->setText(QString::fromUtf8(e.what()));}
        return std::nullopt;
    }
    void foldResponse(){
        const auto run=load();
        if(!run)return;
        try{
            report=foldTargetFluenceResponse(*run,loadDeviceResponseCsv(response->text()));
            folded->setPlainText(tr("靶区 %1 · %2\n观测折合值：%3 %4\n观测能箱响应范围：%5\n总响应标准误：未知。此范围不是置信区间。")
                .arg(text(report.at("target_id")),text(report.at("metric")),text(report.at("observed_folded_value")),
                     text(report.at("result_unit")),text(report.at("observed_energy_bin_response_range"))));
            exportButton->setEnabled(true);status->setText(tr("已按输运档案中的靶区注量折合。"));
        }catch(const std::exception& e){status->setText(QString::fromUtf8(e.what()));}
    }
public:
    Review(QWidget* parent,const QString& path):QDialog(parent){
        setAttribute(Qt::WA_DeleteOnClose);setObjectName("spaceradTargetFluenceReview");
        setWindowTitle(tr("靶区注量与屏蔽后器件响应"));resize(1040,700);
        auto* outer=new QVBoxLayout(this);
        auto* help=new QLabel(tr("读取已核验的输运档案。表中为体积平均、按粒子和能箱积分的标量注量；响应折合使用用户提供的角度平均曲线。"),this);
        help->setWordWrap(true);outer->addWidget(help);
        auto pathRow=[&](const QString& label,const char* object,const QString& initial,bool directory){
            auto* row=new QHBoxLayout;row->addWidget(new QLabel(label,this));
            auto* field=new QLineEdit(initial,this);field->setObjectName(object);row->addWidget(field);
            auto* browse=new QPushButton(tr("选择"),this);row->addWidget(browse);outer->addLayout(row);
            connect(browse,&QPushButton::clicked,this,[this,field,directory]{
                QPointer<Review> alive(this);auto p=directory?QFileDialog::getExistingDirectory(this,tr("选择输运任务档案"))
                    :QFileDialog::getOpenFileName(this,tr("选择响应 CSV"),{},tr("CSV (*.csv)"));
                if(alive&&!p.isEmpty()){field->setText(p);if(directory)load();}
            });return field;
        };
        archive=pathRow(tr("输运档案"),"spaceradTargetFluenceArchive",path,true);
        response=pathRow(tr("响应表 CSV"),"spaceradTargetFluenceResponse",{},false);
        auto* buttons=new QHBoxLayout;outer->addLayout(buttons);
        auto add=[&](const QString& title,const char* name){auto* b=new QPushButton(title,this);b->setObjectName(name);buttons->addWidget(b);return b;};
        auto* inspect=add(tr("核验档案"),"spaceradTargetFluenceInspect");
        fold=add(tr("折合局部响应"),"spaceradTargetFluenceFold");
        exportButton=add(tr("导出响应 JSON"),"spaceradTargetFluenceExport");
        fold->setEnabled(false);exportButton->setEnabled(false);
        auto* close=add(tr("关闭"),"spaceradTargetFluenceClose");
        status=new QLabel(tr("选择含靶区注量的输运档案。"),this);
        status->setObjectName("spaceradTargetFluenceStatus");status->setTextFormat(Qt::PlainText);status->setWordWrap(true);outer->addWidget(status);
        bins=new QTableWidget(0,5,this);bins->setObjectName("spaceradTargetFluenceBins");
        bins->setHorizontalHeaderLabels({tr("粒子"),tr("能箱 MeV"),tr("注量 cm⁻²"),tr("标准误 cm⁻²"),tr("非零历史")});
        bins->setEditTriggers(QAbstractItemView::NoEditTriggers);bins->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);outer->addWidget(bins,1);
        folded=new QPlainTextEdit(this);folded->setObjectName("spaceradTargetFluenceFolded");folded->setReadOnly(true);folded->setMaximumHeight(160);outer->addWidget(folded);
        connect(inspect,&QPushButton::clicked,this,[this]{load();});
        connect(fold,&QPushButton::clicked,this,[this]{foldResponse();});
        connect(archive,&QLineEdit::textChanged,this,[this]{clearFold();bins->setRowCount(0);fold->setEnabled(false);status->setText(tr("请核验新档案。"));});
        connect(response,&QLineEdit::textChanged,this,[this]{clearFold();});
        connect(close,&QPushButton::clicked,this,&QDialog::close);
        connect(exportButton,&QPushButton::clicked,this,[this]{
            QPointer<Review> alive(this);auto path=QFileDialog::getSaveFileName(this,tr("导出到新 JSON 文件"),{},tr("JSON (*.json)"));
            if(!alive||path.isEmpty()||report.is_null())return;
            QFile file(path);const auto raw=QByteArray::fromStdString(report.dump(2)+"\n");
            if(!file.open(QIODevice::WriteOnly|QIODevice::NewOnly)||file.write(raw)!=raw.size()||!file.flush())
                status->setText(tr("无法写入新文件，请检查路径。"));
            else status->setText(tr("已导出：%1").arg(path));
        });
        if(!path.isEmpty())load();
    }
};
}
void showTargetFluenceDialog(QWidget* parent,const QString& archive){auto* d=new Review(parent,archive);d->show();d->raise();}
}
