#include "VoxelDialog.h"
#include "spacerad/VoxelExport.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QHeaderView>
#include <QVBoxLayout>
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace spacerad {
namespace {
QString str(const Json& value){return value.is_null()?QString::fromUtf8("—"):QString::fromStdString(value.is_string()?value.get<std::string>():value.dump());}
class Slice final:public QWidget {
    Json field;QString axis="Z";int index=0;
public:
    explicit Slice(QWidget* parent):QWidget(parent){setObjectName("spaceradVoxelSlice");setMinimumSize(360,300);}
    void setField(const Json& f){field=f;update();}
    void setSlice(const QString& a,int i){axis=a;index=i;update();}
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);painter.fillRect(rect(),Qt::white);
        if(field.is_null()){painter.drawText(rect(),Qt::AlignCenter,tr("无体素结果"));return;}
        const auto counts=field.at("counts").get<std::array<int,3>>();const int ax=axis=="X"?0:axis=="Y"?1:2;
        const int fixed=std::clamp(index,0,counts[ax]-1);const int nx=counts[ax==0?1:0],ny=counts[ax==2?1:2];
        const int widthCells=axis=="Y"?counts[0]:axis=="X"?counts[1]:counts[0];
        const int heightCells=axis=="X"?counts[2]:axis=="Y"?counts[2]:counts[1];
        const auto spacing=field.at("spacing_mm").get<std::array<double,3>>();double maximum=0;
        for(const auto& row:field.at("rows"))maximum=std::max(maximum,row.at("tid_Gy").get<double>());
        const QRectF area(55,30,std::max(10,width()-85),std::max(10,height()-85));const double cw=area.width()/widthCells,ch=area.height()/heightCells;
        for(int v=0;v<heightCells;++v)for(int u=0;u<widthCells;++u){std::array<int,3> ijk{};
            if(axis=="Z")ijk={u,v,fixed};else if(axis=="Y")ijk={u,fixed,v};else ijk={fixed,u,v};
            const auto flat=ijk[0]+counts[0]*(ijk[1]+counts[1]*ijk[2]);const auto& row=field.at("rows").at(flat);
            const auto quality=row.at("quality").get<std::string>();const double value=row.at("tid_Gy").get<double>();
            QColor fill;
            if(quality=="zero_hit")fill=QColor("#687783");
            else {const double q=maximum>0?std::clamp(value/maximum,0.0,1.0):0.0;fill.setHsvF(.68*(1-q),.88,.98-.1*q);}
            const QRectF cell(area.left()+u*cw,area.bottom()-(v+1)*ch,cw,ch);painter.fillRect(cell,fill);painter.setPen(QColor(255,255,255,190));painter.drawRect(cell);
        }
        painter.setPen(QColor("#20364a"));painter.drawText(QRectF(5,3,width()-10,22),Qt::AlignLeft,tr("%1 · %2 / %3 · TID Gy · 线性颜色；灰色表示零命中").arg(str(field.at("volume_id"))).arg(fixed+1).arg(counts[ax]));
        const QString planeU=axis=="X"?"Y":"X",planeV=axis=="Z"?"Y":"Z";
        painter.drawText(QRectF(0,area.bottom()+8,width(),22),Qt::AlignCenter,tr("切片内 %1 / %2 网格位置 · 单元宽度 %3 / %4 mm").arg(planeU,planeV).arg(spacing[axis=="X"?1:0]).arg(spacing[axis=="Z"?1:2]));
        painter.drawText(QRectF(0,height()-21,width(),18),Qt::AlignCenter,tr("剂量范围：0 – %1 Gy · 灰格零命中不表示零风险").arg(QString::number(maximum,'g',5)));
    }
};
class VoxelDialog final:public QDialog {
    Project project;Json result,field;QComboBox* axis;QSpinBox* index;Slice* slice;QTableWidget* table;QLabel* status;
public:
    VoxelDialog(QWidget* parent,const Project& p,const Json& r):QDialog(parent),project(p),result(r),field(r.at("voxel_field")){
        setObjectName("spaceradVoxelDialog");setAttribute(Qt::WA_DeleteOnClose);setWindowTitle(tr("SpaceRad · 体素剂量场"));resize(1000,720);auto* layout=new QVBoxLayout(this);
        auto* info=new QLabel(tr("整区敏感部件 %1 · %2 × %3 × %4 · %5 历史 · 单格质量 %6 kg。每格显示均匀质量体素的 TID 估计；使用线性色标。").arg(str(field.at("volume_id"))).arg(field.at("counts").at(0).get<int>()).arg(field.at("counts").at(1).get<int>()).arg(field.at("counts").at(2).get<int>()).arg(field.at("histories").get<int>()).arg(str(field.at("cell_mass_kg"))),this);info->setWordWrap(true);layout->addWidget(info);
        auto* controls=new QHBoxLayout;axis=new QComboBox(this);axis->setObjectName("spaceradVoxelAxis");axis->addItems({"X","Y","Z"});controls->addWidget(new QLabel(tr("切片法向"),this));controls->addWidget(axis);index=new QSpinBox(this);index->setObjectName("spaceradVoxelSliceIndex");controls->addWidget(new QLabel(tr("层号（从 1 开始）"),this));controls->addWidget(index);controls->addStretch();auto* exportButton=new QPushButton(tr("导出 JSON / VTI / CSV"),this);exportButton->setObjectName("spaceradVoxelExport");controls->addWidget(exportButton);layout->addLayout(controls);
        slice=new Slice(this);slice->setField(field);layout->addWidget(slice,2);
        table=new QTableWidget(0,8,this);table->setObjectName("spaceradVoxelTable");table->setHorizontalHeaderLabels({"i","j","k","TID Gy","SE Gy","RSE",tr("非零历史"),tr("统计分类")});table->setEditTriggers(QAbstractItemView::NoEditTriggers);table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);layout->addWidget(table,1);
        status=new QLabel(tr("体素是所选均匀敏感盒的实体细分。空间统计误差为逐格 MC 误差；体素间协方差未导出，不能把格间最大值解释为显著热点。"),this);status->setObjectName("spaceradVoxelStatus");status->setWordWrap(true);layout->addWidget(status);
        auto* close=new QPushButton(tr("关闭"),this);layout->addWidget(close,0,Qt::AlignRight);connect(close,&QPushButton::clicked,this,&QDialog::close);
        connect(axis,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{updateSlice();});connect(index,qOverload<int>(&QSpinBox::valueChanged),this,[this]{updateSlice();});
        connect(exportButton,&QPushButton::clicked,this,[this]{exportField();});
        int total=0,sufficient=0,insufficient=0,zero=0;
        for(const auto& row:field.at("rows")){++total;const auto quality=row.at("quality").get<std::string>();if(quality=="sufficient")++sufficient;else if(quality=="insufficient")++insufficient;else ++zero;}
        status->setText(tr("逐格统计分类：达标 %1，未达标 %2，零命中 %3 / %4。阈值来自工程设置；MC 标准误差不包含体素相关性或物理模型系统误差。").arg(sufficient).arg(insufficient).arg(zero).arg(total));
        updateSlice();
    }
private:
    void updateSlice(){const auto c=field.at("counts");const int n=c.at(axis->currentText()=="X"?0:axis->currentText()=="Y"?1:2);index->setRange(1,n);slice->setSlice(axis->currentText(),index->value()-1);const int a=axis->currentText()=="X"?0:axis->currentText()=="Y"?1:2;table->setRowCount(0);
        for(const auto& row:field.at("rows")){const auto& ijk=row.at("ijk");if(ijk.at(a).get<int>()!=index->value()-1)continue;const int target=table->rowCount();table->insertRow(target);QStringList values={str(ijk.at(0)),str(ijk.at(1)),str(ijk.at(2)),str(row.at("tid_Gy")),str(row.at("tid_standard_error_Gy")),str(row.at("relative_standard_error")),str(row.at("nonzero_histories")),QString::fromStdString(row.at("quality").get<std::string>())};for(int col=0;col<values.size();++col)table->setItem(target,col,new QTableWidgetItem(values[col]));}
    }
    void exportField(){auto directory=QFileDialog::getExistingDirectory(this,tr("选择父目录；将新建体素导出子目录"));if(directory.isEmpty())return;const auto target=QDir(directory).filePath("voxel-field-"+QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmss-zzz"));try{exportVoxelResult(result,target);status->setText(tr("已导出并校验字段：%1").arg(target));}catch(const std::exception& e){status->setText(QString::fromUtf8(e.what()));}}
};
}
void showVoxelDialog(QWidget* parent,const Project& project,const Json& result){auto* dialog=new VoxelDialog(parent,project,result);dialog->show();dialog->raise();}
}
