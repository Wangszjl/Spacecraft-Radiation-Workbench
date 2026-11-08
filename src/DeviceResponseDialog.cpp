#include "DeviceResponseDialog.h"
#include <QDialog>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTableWidget>
#include <QUuid>
#include <QVBoxLayout>
#include <stdexcept>
namespace spacerad {namespace {
QString shown(const Json& v){return v.is_null()?QString::fromUtf8("未知"):QString::fromStdString(v.is_string()?v.get<std::string>():v.dump());}
class Dialog final:public QDialog{Project source;QLineEdit *input,*output;QLabel *status,*notice;QTableWidget *table,*components;QPushButton *csv,*json;Json report;
public:Dialog(QWidget* parent,const Project& p,const QString& archive):QDialog(parent),source(p){setObjectName("spaceradDeviceResponseDialog");setAttribute(Qt::WA_DeleteOnClose);setWindowTitle(tr("SpaceRad · 器件 DDD / SEE 响应折合"));resize(1000,720);auto* outer=new QVBoxLayout(this);auto* scroll=new QScrollArea(this);scroll->setObjectName("spaceradDeviceResponseScroll");scroll->setWidgetResizable(true);auto* body=new QWidget(scroll);scroll->setWidget(body);outer->addWidget(scroll);auto* layout=new QVBoxLayout(body);auto* help=new QLabel(tr("工程快照：%1。导入按器件和粒子给出的响应 CSV；结果仅将工程环境谱与响应表折合，未应用航天器屏蔽或靶区次级粒子谱。不能替代 Geant4 DDD/DPA 或 SEE 事件输运，也不推断校准误差。").arg(QString::fromStdString(p.name)),body);help->setWordWrap(true);layout->addWidget(help);input=new QLineEdit(body);input->setObjectName("spaceradDeviceResponseInput");auto* row=new QHBoxLayout;row->addWidget(input);auto* browse=new QPushButton(tr("选择响应 CSV"),body);browse->setObjectName("spaceradDeviceResponseBrowse");row->addWidget(browse);layout->addLayout(row);output=new QLineEdit(body);output->setObjectName("spaceradDeviceResponseOutput");output->setText(QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).filePath("SpaceRad/device-response/"+QUuid::createUuid().toString(QUuid::WithoutBraces)));auto* outrow=new QHBoxLayout;outrow->addWidget(output);auto* fresh=new QPushButton(tr("新任务目录"),body);outrow->addWidget(fresh);layout->addLayout(outrow);auto* buttons=new QHBoxLayout;auto add=[&](const QString& title,const char* name){auto* b=new QPushButton(title,body);b->setObjectName(name);buttons->addWidget(b);return b;};auto* run=add(tr("校验并折合"),"spaceradDeviceResponseRun");auto* open=add(tr("打开历史折合"),"spaceradDeviceResponseOpen");csv=add(tr("导出 CSV"),"spaceradDeviceResponseCsv");json=add(tr("导出 JSON"),"spaceradDeviceResponseJson");buttons->addStretch();layout->addLayout(buttons);csv->setEnabled(false);json->setEnabled(false);status=new QLabel(tr("等待响应表。"),body);status->setObjectName("spaceradDeviceResponseStatus");status->setWordWrap(true);layout->addWidget(status);table=new QTableWidget(0,7,body);table->setObjectName("spaceradDeviceResponseResults");table->setHorizontalHeaderLabels({tr("器件"),tr("指标"),tr("响应单位"),tr("折合值"),tr("结果单位"),tr("NIEL Gy"),tr("Poisson 概率")});table->setEditTriggers(QAbstractItemView::NoEditTriggers);table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);table->setMinimumHeight(120);layout->addWidget(table);components=new QTableWidget(0,5,body);components->setObjectName("spaceradDeviceResponseComponents");components->setHorizontalHeaderLabels({tr("器件"),tr("粒子"),tr("能段 MeV"),tr("折合贡献"),tr("响应来源")});components->setEditTriggers(QAbstractItemView::NoEditTriggers);components->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);components->setMinimumHeight(120);layout->addWidget(components);notice=new QLabel(body);notice->setObjectName("spaceradDeviceResponseNotice");notice->setWordWrap(true);layout->addWidget(notice);auto* close=new QPushButton(tr("关闭"),body);layout->addWidget(close);connect(close,&QPushButton::clicked,this,&QDialog::close);connect(browse,&QPushButton::clicked,this,[this]{QPointer<Dialog> alive(this);auto path=QFileDialog::getOpenFileName(this,tr("选择用户响应曲线 CSV"),{},"CSV (*.csv)");if(alive&&!path.isEmpty())input->setText(path);});connect(fresh,&QPushButton::clicked,this,[this]{output->setText(QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).filePath("SpaceRad/device-response/"+QUuid::createUuid().toString(QUuid::WithoutBraces)));});connect(run,&QPushButton::clicked,this,[this]{try{report=saveDeviceResponseStudy(source,input->text(),QFileInfo(output->text()).absoluteFilePath());render();status->setText(tr("响应表来源、器件 ID、单位、粒子覆盖和能域已核验；折合档案：%1").arg(output->text()));notice->setText(tr("结果是环境谱的未屏蔽响应筛查。DDD 显示 NIEL 能量 MeV/g 和对应 NIEL Gy，不是 DPA；SEE 是用户横截面折合的期望事件，并附 Poisson 假设概率。"));}catch(const std::exception& e){report=nullptr;render();status->setText(tr("折合失败，未接受结果。"));notice->setText(QString::fromUtf8(e.what()));}});connect(open,&QPushButton::clicked,this,[this]{QPointer<Dialog> alive(this);auto dir=QFileDialog::getExistingDirectory(this,tr("选择历史响应折合目录"));if(!alive||dir.isEmpty())return;openArchive(dir);});connect(csv,&QPushButton::clicked,this,[this]{exportReport(true);});connect(json,&QPushButton::clicked,this,[this]{exportReport(false);});table->setSelectionBehavior(QAbstractItemView::SelectRows);connect(table,&QTableWidget::currentCellChanged,this,[this](int r,int,int,int){renderComponents(r);});if(!archive.isEmpty())openArchive(archive);}
private:void render(){csv->setEnabled(!report.is_null());json->setEnabled(!report.is_null());table->setRowCount(0);components->setRowCount(0);if(report.is_null())return;for(const auto& r:report.at("rows")){int i=table->rowCount();table->insertRow(i);QStringList values{shown(r.at("device_id")),shown(r.at("metric")),shown(r.at("response_unit")),shown(r.at("folded_value")),shown(r.at("result_unit")),shown(r.value("niel_dose_Gy",Json(nullptr))),shown(r.value("poisson_probability_at_least_one",Json(nullptr)))};for(int c=0;c<values.size();++c)table->setItem(i,c,new QTableWidgetItem(values[c]));}if(table->rowCount())table->setCurrentCell(0,0);}
void renderComponents(int row){components->setRowCount(0);if(report.is_null()||row<0||size_t(row)>=report.at("rows").size())return;const auto& r=report.at("rows")[row];for(const auto& c:r.at("components")){int i=components->rowCount();components->insertRow(i);QStringList v{shown(r.at("device_id")),shown(c.at("particle")),shown(c.at("energy_range_MeV")),shown(c.at("folded_value")),shown(c.at("response_reference"))};for(int j=0;j<v.size();++j)components->setItem(i,j,new QTableWidgetItem(v[j]));}}
void exportReport(bool asCsv){QPointer<Dialog> alive(this);auto path=QFileDialog::getSaveFileName(this,tr("导出到新文件"),{},asCsv?"CSV (*.csv)":"JSON (*.json)");if(!alive||path.isEmpty())return;try{if(QFileInfo::exists(path))throw std::runtime_error("Choose a new export filename");auto raw=asCsv?deviceResponseCsv(report):QByteArray::fromStdString(report.dump(2));QSaveFile f(path);if(!f.open(QIODevice::WriteOnly)||f.write(raw)!=raw.size()||!f.commit())throw std::runtime_error("Cannot export device response");notice->setText(tr("已导出：%1").arg(path));}catch(const std::exception& e){notice->setText(QString::fromUtf8(e.what()));}}
void openArchive(const QString& path){
    try{
        report=loadDeviceResponseStudy(path);render();
        const bool legacy=report.contains("legacy_report_sha256");
        status->setText(legacy?tr("旧版档案已按修正算法重算，未沿用旧结果：%1").arg(path)
                              :tr("响应折合已离线核验：%1").arg(path));
        notice->setText(tr("使用未屏蔽环境谱。可将当前复核结果导出为新文件，原档案保持不变。"));
    }catch(const std::exception& e){
        report=nullptr;render();status->setText(tr("档案未通过核验。"));
        notice->setText(QString::fromUtf8(e.what()));
    }
}
};}
void showDeviceResponseDialog(QWidget* parent,const Project& project,const QString& archive){auto* d=new Dialog(parent,project,archive);d->show();d->raise();}
}
