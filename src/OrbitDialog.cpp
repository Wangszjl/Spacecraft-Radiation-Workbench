#include "OrbitDialog.h"
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTableWidget>
#include <QUuid>
#include <QVBoxLayout>

namespace spacerad {namespace {
class OrbitDialog final:public QDialog {
    Project source;std::function<void(const Project&)> applyProject;OrbitEnvironmentService* service;
    QGroupBox* editor;QLineEdit *python,*tools,*ephem,*gap,*emin,*emax,*pmin,*pmax,*output;
    QComboBox *frame,*unit;QSpinBox* timeout;QLabel *status,*notice,*summary;QProgressBar* progress;
    QTableWidget* spectra;QPushButton *run,*cancel,*apply,*open;Json report;
public:
    OrbitDialog(QWidget* parent,const Project& p,std::function<void(const Project&)> callback):QDialog(parent),source(p),applyProject(std::move(callback)){
        setObjectName("spaceradOrbitDialog");setAttribute(Qt::WA_DeleteOnClose);setWindowTitle(tr("SpaceRad · AP8/AE8 星历环境"));resize(1000,850);
        auto* outer=new QVBoxLayout(this);auto* scroll=new QScrollArea(this);scroll->setObjectName("spaceradOrbitScroll");scroll->setWidgetResizable(true);auto* body=new QWidget(scroll);scroll->setWidget(body);outer->addWidget(scroll);auto* layout=new QVBoxLayout(body);
        auto* help=new QLabel(tr("使用工程快照：%1。独立 SpacePy 0.7.0／IRBEM 进程根据外部星历计算 AE8MIN/AP8MIN 俘获粒子环境；模型谱不是实测谱。几何、材料、计分与输运参数保留，暴露取星历首尾实际时长。").arg(QString::fromStdString(p.name)),body);help->setWordWrap(true);help->setTextFormat(Qt::PlainText);layout->addWidget(help);
        editor=new QGroupBox(tr("轨道模型设置"),body);auto* form=new QFormLayout(editor);
        auto field=[&](const char* name,const QString& value){auto* e=new QLineEdit(value,editor);e->setObjectName(name);return e;};
        python=field("spaceradOrbitPython",{});tools=field("spaceradOrbitTools",QCoreApplication::applicationDirPath()+"/model-tools");ephem=field("spaceradOrbitEphemeris",{});
        auto browse=[&](const QString& label,QLineEdit* target,bool directory,const QString& filter){auto* row=new QHBoxLayout;row->addWidget(target);auto* button=new QPushButton(tr("选择"),editor);row->addWidget(button);form->addRow(label,row);connect(button,&QPushButton::clicked,this,[this,target,directory,filter]{QPointer<OrbitDialog> alive(this);auto path=directory?QFileDialog::getExistingDirectory(this,tr("选择模型工具目录")):QFileDialog::getOpenFileName(this,tr("选择模型输入"),{},filter);if(!alive)return;if(!path.isEmpty())target->setText(path);});};
        browse(tr("独立 Python 解释器"),python,false,tr("Python (*)"));browse(tr("模型脚本目录"),tools,true,{});browse(tr("时间戳星历 CSV"),ephem,false,"CSV (*.csv)");
        auto* coordinates=new QHBoxLayout;frame=new QComboBox(editor);frame->setObjectName("spaceradOrbitFrame");frame->addItems({"GEO","GEI"});unit=new QComboBox(editor);unit->setObjectName("spaceradOrbitUnit");unit->addItems({"km","Re"});gap=field("spaceradOrbitGap","60");coordinates->addWidget(new QLabel(tr("坐标系"),editor));coordinates->addWidget(frame);coordinates->addWidget(new QLabel(tr("位置单位"),editor));coordinates->addWidget(unit);coordinates->addWidget(new QLabel(tr("最大采样间隔 s"),editor));coordinates->addWidget(gap);form->addRow(coordinates);
        auto* bands=new QHBoxLayout;emin=field("spaceradOrbitElectronMin","0.1");emax=field("spaceradOrbitElectronMax","1");pmin=field("spaceradOrbitProtonMin","1");pmax=field("spaceradOrbitProtonMax","30");for(auto* e:{emin,emax,pmin,pmax})e->setMaximumWidth(110);bands->addWidget(new QLabel(tr("电子 MeV"),editor));bands->addWidget(emin);bands->addWidget(emax);bands->addWidget(new QLabel(tr("质子 MeV"),editor));bands->addWidget(pmin);bands->addWidget(pmax);form->addRow(bands);
        timeout=new QSpinBox(editor);timeout->setObjectName("spaceradOrbitTimeout");timeout->setRange(1,3600);timeout->setValue(300);form->addRow(tr("模型超时 s"),timeout);
        output=field("spaceradOrbitOutput",QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).filePath("SpaceRad/orbit-environments/"+QUuid::createUuid().toString(QUuid::WithoutBraces)));form->addRow(tr("新的任务目录"),output);layout->addWidget(editor);
        auto* caution=new QLabel(tr("CSV 表头：utc,x,y,z；时间严格递增，2–4096 点。GEO 为地心地固笛卡尔，GEI 为 SpacePy/IRBEM GEI；不可直接使用 TEME、J2000 标签或经纬高。谱按不等时间间隔梯形积分，不外推首尾。默认仅选电子 0.1–1、质子 1–30 MeV；超出能带的贡献未计入，缺测或逆序模型值会拒绝。仅太阳低年俘获粒子模型，尚不含 SPE/GCR。"),body);caution->setWordWrap(true);layout->addWidget(caution);
        auto* buttons=new QHBoxLayout;auto add=[&](const QString& title,const char* name){auto* b=new QPushButton(title,body);b->setObjectName(name);buttons->addWidget(b);return b;};run=add(tr("计算并核验模型谱"),"spaceradOrbitRun");cancel=add(tr("取消"),"spaceradOrbitCancel");open=add(tr("核验历史模型任务"),"spaceradOrbitOpen");apply=add(tr("载入当前工程"),"spaceradOrbitApply");buttons->addStretch();layout->addLayout(buttons);cancel->setEnabled(false);apply->setEnabled(false);
        progress=new QProgressBar(body);progress->setObjectName("spaceradOrbitProgress");progress->setRange(0,3);layout->addWidget(progress);status=new QLabel(tr("待运行。Python 模型环境需单独配置，不会修改宿主环境变量。"),body);status->setObjectName("spaceradOrbitStatus");status->setWordWrap(true);status->setTextFormat(Qt::PlainText);layout->addWidget(status);
        summary=new QLabel(body);summary->setObjectName("spaceradOrbitSummary");summary->setWordWrap(true);summary->setTextFormat(Qt::PlainText);layout->addWidget(summary);spectra=new QTableWidget(0,5,body);spectra->setObjectName("spaceradOrbitSpectra");spectra->setHorizontalHeaderLabels({tr("粒子"),"Emin MeV","Emax MeV",tr("平均带内通量 cm⁻²s⁻¹"),tr("带内注量 cm⁻²")});spectra->setEditTriggers(QAbstractItemView::NoEditTriggers);spectra->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);spectra->setMinimumHeight(110);spectra->setMaximumHeight(160);layout->addWidget(spectra);
        notice=new QLabel(body);notice->setObjectName("spaceradOrbitNotice");notice->setWordWrap(true);notice->setTextFormat(Qt::PlainText);layout->addWidget(notice);auto* close=new QPushButton(tr("关闭"),body);layout->addWidget(close);
        service=new OrbitEnvironmentService(this);connect(close,&QPushButton::clicked,this,&QDialog::close);connect(run,&QPushButton::clicked,this,[this]{try{report=nullptr;render();notice->clear();progress->setValue(0);OrbitModelRuntime runtime{python->text(),tools->text(),timeout->value()};service->configure(runtime);service->start(source,settings(),QFileInfo(output->text()).absoluteFilePath());}catch(const std::exception& e){notice->setText(QString::fromUtf8(e.what()));}});connect(cancel,&QPushButton::clicked,service,&OrbitEnvironmentService::cancel);
        connect(service,&OrbitEnvironmentService::stateChanged,this,[this](const QString& state){bool busy=service->running();editor->setEnabled(!busy);run->setEnabled(!busy);open->setEnabled(!busy);cancel->setEnabled(busy&&state!="cancelling");apply->setEnabled(!busy&&!report.is_null());if(state=="cancelled")status->setText(tr("已取消；未载入环境谱，已有工程结果保留。"));else if(state=="failed")status->setText(tr("模型失败，未接收环境谱。"));});connect(service,&OrbitEnvironmentService::failed,this,[this](const QString& e){notice->setText(e);});connect(service,&OrbitEnvironmentService::progress,this,[this](int n,int){progress->setValue(n);status->setText(tr("独立模型进程：阶段 %1 / 3；结束后复核原始积分谱与工程。%2").arg(n).arg(service->outputDirectory()));});connect(service,&OrbitEnvironmentService::completed,this,[this](const QByteArray& raw){report=Json::parse(raw.toStdString());render();progress->setValue(3);status->setText(tr("模型计算与原始谱复核完成。核对覆盖范围后，可载入当前工程。%1").arg(service->outputDirectory()));});
        connect(open,&QPushButton::clicked,this,[this]{QPointer<OrbitDialog> alive(this);auto dir=QFileDialog::getExistingDirectory(this,tr("选择已完成模型任务目录"));if(!alive||dir.isEmpty())return;report=nullptr;try{report=loadOrbitModelJob(dir);render();notice->clear();status->setText(tr("历史任务已在无 Python 的复核路径载入：%1").arg(dir));}catch(const std::exception& e){render();notice->setText(QString::fromUtf8(e.what()));}});
        connect(apply,&QPushButton::clicked,this,[this]{QPointer<OrbitDialog> alive(this);try{if(report.is_null())throw std::runtime_error("No accepted environment");auto candidate=Project::fromJson(report.at("project"));applyProject(candidate);if(alive){apply->setEnabled(false);notice->setText(tr("已载入模型谱；主工作台可继续 Geant4 输运、体素或屏蔽扫描。原任务档案保留。"));}}catch(const std::exception& e){if(alive)notice->setText(QString::fromUtf8(e.what()));}});
        auto runtimeFile=QCoreApplication::applicationDirPath()+"/orbit-model-runtime.json";if(QFileInfo::exists(runtimeFile))try{auto r=loadOrbitModelRuntime(runtimeFile);python->setText(r.python);tools->setText(r.toolsDirectory);timeout->setValue(r.timeoutSeconds);}catch(const std::exception& e){notice->setText(QString::fromUtf8(e.what()));}
    }
    ~OrbitDialog() override{disconnect(service,nullptr,this,nullptr);delete service;service=nullptr;}
private:
    OrbitEnvironmentSettings settings() const{OrbitEnvironmentSettings s;s.ephemeris=ephem->text();s.frame=frame->currentText();s.positionUnit=unit->currentText();auto number=[](QLineEdit* e){bool ok=false;double value=e->text().toDouble(&ok);if(!ok)throw std::runtime_error("Numeric model parameter required");return value;};s.maxGapSeconds=number(gap);s.electronBand={number(emin),number(emax)};s.protonBand={number(pmin),number(pmax)};return OrbitEnvironmentSettings::fromJson(s.toJson());}
    void render(){spectra->setRowCount(0);summary->clear();apply->setEnabled(!report.is_null());if(report.is_null())return;auto project=Project::fromJson(report.at("project"));summary->setText(tr("样本：%1 · 实际暴露：%2 s\n%3 至 %4\n只覆盖所选俘获电子／质子能带；模型、轨道和物理系统误差未计入。").arg(report.at("samples").get<int>()).arg(QString::number(project.duration,'g',12),QString::fromStdString(report.at("manifest").at("epoch_utc")),QString::fromStdString(report.at("manifest").at("interval_end_utc"))));for(const auto& s:project.spectra){int row=spectra->rowCount();spectra->insertRow(row);QStringList values{QString::fromStdString(s.particle),QString::number(s.energy.front(),'g',8),QString::number(s.energy.back(),'g',8),QString::number(s.integral,'g',10),QString::number(s.integral*project.duration,'g',10)};for(int c=0;c<5;++c)spectra->setItem(row,c,new QTableWidgetItem(values[c]));}}
};
}
void showOrbitDialog(QWidget* parent,const Project& project,std::function<void(const Project&)> apply){auto* d=new OrbitDialog(parent,project,std::move(apply));d->show();d->raise();}
}
