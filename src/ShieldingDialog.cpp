#include "ShieldingDialog.h"
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTableWidget>
#include <QUuid>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace spacerad {
namespace {
QString text(const Json& value){return value.is_null()?QString::fromUtf8("—"):QString::fromStdString(value.is_string()?value.get<std::string>():value.dump());}
class DoseCurve final:public QWidget {
    Json rows=Json::array();QString id;
public:
    explicit DoseCurve(QWidget* parent):QWidget(parent){setObjectName("spaceradShieldingPlot");setMinimumHeight(190);}
    void setData(const Json& values,const QString& target){rows=values;id=target;update();}
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);p.fillRect(rect(),Qt::white);
        QRectF area(100,25,std::max(1,width()-125),std::max(1,height()-70));
        p.setPen(QColor("#33495b"));p.drawLine(area.bottomLeft(),area.topLeft());p.drawLine(area.bottomLeft(),area.bottomRight());
        p.drawText(QRectF(area.left(),height()-32,area.width(),24),Qt::AlignCenter,tr("盒壳壁厚 mm（内腔固定）"));
        p.drawText(QRectF(5,0,width()-10,22),Qt::AlignLeft,tr("%1 · TID Gy · 线段仅辅助读图，误差棒为各点 MC SE").arg(id));
        std::vector<std::array<double,3>> points;double maxX=0,maxY=0;
        for(const auto& row:rows)if(text(row.at("target_id"))==id){double x=row.at("thickness_mm"),y=row.at("tid_Gy"),e=row.at("tid_standard_error_Gy");points.push_back({x,y,e});maxX=std::max(maxX,x);maxY=std::max(maxY,y+e);}
        if(points.empty()){p.drawText(area,Qt::AlignCenter,tr("完成同一厚度的所有粒子项后显示数据点"));return;}
        if(maxX<=0)maxX=1;if(maxY<=0)maxY=1;maxY*=1.1;
        auto pixel=[&](double x,double y){return QPointF(area.left()+area.width()*x/maxX,area.bottom()-area.height()*y/maxY);};
        for(int i=0;i<=4;++i){double x=maxX*i/4,y=maxY*i/4;p.setPen(QColor("#dae3e9"));p.drawLine(pixel(0,y),pixel(maxX,y));p.setPen(QColor("#33495b"));p.drawText(QRectF(0,pixel(0,y).y()-9,94,18),Qt::AlignRight,QString::number(y,'g',3));p.drawText(QRectF(pixel(x,0).x()-30,area.bottom()+3,60,20),Qt::AlignCenter,QString::number(x,'g',4));}
        p.setPen(QPen(QColor("#1971a8"),2));
        for(size_t i=0;i<points.size();++i){const auto& a=points[i];auto point=pixel(a[0],a[1]);if(i)p.drawLine(pixel(points[i-1][0],points[i-1][1]),point);p.drawLine(pixel(a[0],std::max(0.0,a[1]-a[2])),pixel(a[0],a[1]+a[2]));p.setBrush(QColor("#1971a8"));p.drawEllipse(point,3,3);}
    }
};
class ShieldingDialog final:public QDialog {
    Project source;ShieldingScanService* service;
    QComboBox *shell,*target;QLineEdit *thickness,*output;QSpinBox* histories;
    QLabel *preview,*status,*notice;QGroupBox* editor;QProgressBar* progress;
    QTableWidget* table;DoseCurve* plot;QPushButton *run,*cancel,*csv,*json;
    Json report;
public:
    ShieldingDialog(QWidget* parent,const Project& p,const RuntimeOptions& runtime,const QString& archive):QDialog(parent),source(p) {
        setObjectName("spaceradShieldingDialog");setAttribute(Qt::WA_DeleteOnClose);setWindowTitle(tr("SpaceRad · 自动屏蔽厚度扫描"));resize(1150,850);
        auto* layout=new QVBoxLayout(this);
        auto* help=new QLabel(tr("使用打开窗口时的工程快照：%1。保持所选盒壳内腔、位置和材料不变，向外增加壁厚；0 mm 移除所选盒壳。每个厚度分别运行电子/质子，当前工程不变。").arg(QString::fromStdString(source.name)),this);help->setWordWrap(true);help->setTextFormat(Qt::PlainText);layout->addWidget(help);
        editor=new QGroupBox(tr("扫描设置"),this);auto* form=new QFormLayout(editor);
        shell=new QComboBox(editor);shell->setObjectName("spaceradScanShell");for(const auto& v:source.volumes)if(v.shape=="box"&&v.wall>0&&!v.sensitive)shell->addItem(QString::fromStdString(v.id));form->addRow(tr("直接定义的非敏感盒壳"),shell);
        thickness=new QLineEdit("0, 0.5, 1, 2, 3",editor);thickness->setObjectName("spaceradScanThicknesses");form->addRow(tr("壁厚 mm（严格递增，2–64 项）"),thickness);
        histories=new QSpinBox(editor);histories->setRange(2,100000000);histories->setValue(source.events);histories->setObjectName("spaceradScanHistories");form->addRow(tr("每个厚度 / 粒子项的历史数"),histories);
        auto* outRow=new QHBoxLayout;output=new QLineEdit(editor);output->setObjectName("spaceradScanOutput");outRow->addWidget(output);auto* newOutput=new QPushButton(tr("新任务目录"),editor);outRow->addWidget(newOutput);form->addRow(tr("输出到新目录"),outRow);
        preview=new QLabel(editor);preview->setObjectName("spaceradScanPreview");preview->setWordWrap(true);form->addRow(preview);layout->addWidget(editor);
        auto* controls=new QHBoxLayout;run=new QPushButton(tr("开始扫描"),this);run->setObjectName("spaceradScanRun");controls->addWidget(run);cancel=new QPushButton(tr("取消扫描"),this);cancel->setObjectName("spaceradScanCancel");cancel->setEnabled(false);controls->addWidget(cancel);auto* reopen=new QPushButton(tr("打开历史扫描"),this);reopen->setObjectName("spaceradScanOpen");controls->addWidget(reopen);controls->addStretch();layout->addLayout(controls);
        progress=new QProgressBar(this);progress->setRange(0,1000);layout->addWidget(progress);
        status=new QLabel(tr("待运行。"),this);status->setObjectName("spaceradScanStatus");status->setWordWrap(true);status->setTextFormat(Qt::PlainText);layout->addWidget(status);
        auto* caution=new QLabel(tr("固定抽样预算，不做自适应收敛或单调性假设。总剂量及各粒子 MC 方差相加采用独立运行假设；不同种子不证明随机流严格独立。零命中粒子项使合计 RSE 不可估计，零命中不代表零风险。不估计厚度间差值的显著性；独立剂量物理验证、轨道模型及 DDD / SEE 尚未完成。"),this);caution->setWordWrap(true);layout->addWidget(caution);
        target=new QComboBox(this);target->setObjectName("spaceradScanTarget");layout->addWidget(target);plot=new DoseCurve(this);layout->addWidget(plot,1);
        table=new QTableWidget(0,6,this);table->setObjectName("spaceradScanTable");table->setHorizontalHeaderLabels({tr("壁厚 mm"),tr("敏感区"),"TID Gy","MC SE Gy","RSE",tr("零命中粒子项")});table->setEditTriggers(QAbstractItemView::NoEditTriggers);table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);layout->addWidget(table,1);
        notice=new QLabel(this);notice->setObjectName("spaceradScanNotice");notice->setTextFormat(Qt::PlainText);notice->setWordWrap(true);layout->addWidget(notice);
        auto* exports=new QHBoxLayout;csv=new QPushButton(tr("导出扫描 CSV"),this);csv->setObjectName("spaceradScanCsv");exports->addWidget(csv);json=new QPushButton(tr("导出扫描 JSON"),this);json->setObjectName("spaceradScanJson");exports->addWidget(json);csv->setEnabled(false);json->setEnabled(false);exports->addStretch();auto* close=new QPushButton(tr("关闭"),this);exports->addWidget(close);layout->addLayout(exports);
        service=new ShieldingScanService(this);service->configure(runtime);
        connect(close,&QPushButton::clicked,this,&QDialog::close);
        connect(newOutput,&QPushButton::clicked,this,[this]{freshDirectory();});freshDirectory();
        connect(shell,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{updatePlan();});connect(thickness,&QLineEdit::textChanged,this,[this]{updatePlan();});connect(histories,qOverload<int>(&QSpinBox::valueChanged),this,[this]{updatePlan();});
        connect(run,&QPushButton::clicked,this,[this]{try{report=nullptr;render();progress->setValue(0);notice->clear();service->start(project(),settings(),QFileInfo(output->text()).absoluteFilePath());}catch(const std::exception& e){status->setText(QString::fromUtf8(e.what()));}});
        connect(cancel,&QPushButton::clicked,service,&ShieldingScanService::cancel);
        connect(service,&ShieldingScanService::stateChanged,this,[this,reopen](const QString& state){bool busy=service->running();editor->setEnabled(!busy);run->setEnabled(!busy);reopen->setEnabled(!busy);cancel->setEnabled(busy&&state!="cancelling");if(state=="cancelled"||state=="failed"){try{report=loadShieldingScan(service->outputDirectory());render();}catch(...){}status->setText(state=="cancelled"?tr("已取消；仅保留已完成且核对通过的厚度行。未接收整组完成结果。"):tr("扫描失败；请检查任务目录中的各项 worker.log。"));}});
        connect(service,&ShieldingScanService::progress,this,[this](int n,int total,int h,int budget){progress->setValue(total?int(1000.*(n+(budget?double(h)/budget:0))/total):0);status->setText(tr("已完成 %1 / %2 个厚度 / 粒子项；当前 %3 / %4 历史。%5").arg(n).arg(total).arg(h).arg(budget).arg(service->outputDirectory()));});
        connect(service,&ShieldingScanService::failed,this,[this](const QString& error){notice->setText(error);});
        connect(service,&ShieldingScanService::completed,this,[this](const QByteArray& raw){report=Json::parse(raw.toStdString());render();progress->setValue(1000);status->setText(tr("扫描完成并核对：%1 个计算项，%2 条敏感区 / 厚度记录。%3").arg(report.at("cases").size()).arg(report.at("curves").size()).arg(service->outputDirectory()));});
        connect(reopen,&QPushButton::clicked,this,[this]{auto dir=QFileDialog::getExistingDirectory(this,tr("选择历史扫描目录"));if(!dir.isEmpty())openArchive(dir);});
        connect(csv,&QPushButton::clicked,this,[this]{exportResult(true);});connect(json,&QPushButton::clicked,this,[this]{exportResult(false);});
        connect(target,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{plot->setData(report.is_null()?Json::array():report.at("curves"),target->currentText());});
        updatePlan();if(!archive.isEmpty())openArchive(archive);
    }
    ~ShieldingDialog() override {disconnect(service,nullptr,this,nullptr);delete service;service=nullptr;}
private:
    Project project() const {auto p=source;p.events=histories->value();p.validate();return p;}
    ShieldingScanSettings settings() const {
        ShieldingScanSettings s;s.shellId=shell->currentText().toStdString();auto raw=thickness->text();raw.replace(QString::fromUtf8("，"),",");
        for(const auto& token:raw.split(QRegularExpression("[,\\s]+"),Qt::SkipEmptyParts)){bool ok=false;double t=token.toDouble(&ok);if(!ok||!std::isfinite(t))throw std::runtime_error("Finite numeric thickness required");s.thicknesses.push_back(t);}
        return ShieldingScanSettings::fromJson(s.toJson());
    }
    void updatePlan(){try{const auto plan=planShieldingScan(project(),settings());preview->setText(tr("计划 %1 项 × %2 历史 = %3 个初级历史；逐项运行，不自动缩减预算。全部厚度通过几何与源球包围检查后才启动。").arg(plan.at("cases").size()).arg(histories->value()).arg(text(plan.at("total_planned_histories"))));}catch(const std::exception& e){preview->setText(tr("设置待修正：%1").arg(QString::fromUtf8(e.what())));}}
    void freshDirectory(){output->setText(QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).filePath("SpaceRad/scans/"+QUuid::createUuid().toString(QUuid::WithoutBraces)));}
    void openArchive(const QString& directory){try{report=loadShieldingScan(directory);render();status->setText(tr("历史扫描已离线核对：%1 · %2").arg(text(report.at("status")),directory));}catch(const std::exception& e){report=nullptr;render();notice->setText(QString::fromUtf8(e.what()));}}
    void render(){
        table->setRowCount(0);const auto selected=target->currentText();target->clear();csv->setEnabled(!report.is_null());json->setEnabled(!report.is_null());
        if(report.is_null()){plot->setData(Json::array(),{});return;}
        std::set<QString> ids;
        for(const auto& row:report.at("curves")){int i=table->rowCount();table->insertRow(i);QStringList values{text(row.at("thickness_mm")),text(row.at("target_id")),text(row.at("tid_Gy")),text(row.at("tid_standard_error_Gy")),text(row.at("relative_standard_error")),text(row.at("zero_hit_components").dump())};for(int c=0;c<values.size();++c)table->setItem(i,c,new QTableWidgetItem(values[c]));ids.insert(text(row.at("target_id")));}
        for(const auto& id:ids)target->addItem(id);int index=target->findText(selected);if(index>=0)target->setCurrentIndex(index);plot->setData(report.at("curves"),target->currentText());
    }
    void exportResult(bool asCsv){auto path=QFileDialog::getSaveFileName(this,tr("导出到新文件"),{},asCsv?"CSV (*.csv)":"JSON (*.json)");if(path.isEmpty())return;try{if(QFileInfo::exists(path))throw std::runtime_error("Choose a new export filename");auto raw=asCsv?shieldingScanCsv(report):QByteArray::fromStdString(report.dump(2));QSaveFile file(path);if(!file.open(QIODevice::WriteOnly)||file.write(raw)!=raw.size()||!file.commit())throw std::runtime_error("Cannot export scan");notice->setText(tr("已导出：%1").arg(path));}catch(const std::exception& e){notice->setText(QString::fromUtf8(e.what()));}}
};
}
void showShieldingDialog(QWidget* parent,const Project& p,const RuntimeOptions& runtime,const QString& archive){auto* dialog=new ShieldingDialog(parent,p,runtime,archive);dialog->show();dialog->raise();}
}
