#include "PartDialog.h"
#include <QComboBox>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <memory>
#include <QVBoxLayout>
#include <cmath>
#include <stdexcept>
namespace spacerad {
std::optional<Project> editPartDialog(QWidget* parent,const Project& source,const Volume& initial,const std::string& replaceId){
    std::unique_ptr<QDialog> owner(new QDialog(parent));QPointer<QDialog> alive(owner.get());QDialog& dialog=*owner;dialog.setObjectName("spaceradPartDialog");dialog.setWindowTitle(replaceId.empty()?QObject::tr("添加部件"):QObject::tr("编辑部件"));dialog.resize(500,600);
    auto* layout=new QVBoxLayout(&dialog);auto* form=new QFormLayout;layout->addLayout(form);
    auto* id=new QLineEdit(QString::fromStdString(initial.id),&dialog);id->setObjectName("spaceradPartId");form->addRow(QObject::tr("稳定 ID"),id);
    auto* shape=new QComboBox(&dialog);shape->setObjectName("spaceradPartShape");for(auto value:{"box","cylinder","sphere"})shape->addItem(value,value);shape->setCurrentIndex(shape->findData(QString::fromStdString(initial.shape)));form->addRow(QObject::tr("形状"),shape);
    auto* material=new QComboBox(&dialog);material->setObjectName("spaceradPartMaterial");material->setEditable(true);material->addItems({"G4_Al","G4_Si","G4_Cu","G4_W","G4_KAPTON","G4_SILICON_DIOXIDE"});material->setCurrentText(QString::fromStdString(initial.material));form->addRow(QObject::tr("NIST 材料标识"),material);
    auto* sensitive=new QCheckBox(QObject::tr("记录此部件的平均 TID"),&dialog);sensitive->setObjectName("spaceradPartSensitive");sensitive->setChecked(initial.sensitive);form->addRow(QObject::tr("敏感区"),sensitive);
    auto* parentId=new QComboBox(&dialog);parentId->addItem(QObject::tr("世界坐标"),"");for(const auto& a:source.assemblies)parentId->addItem(QString::fromStdString(a.id),QString::fromStdString(a.id));auto parentText=QString::fromStdString(initial.parent);if(parentId->findData(parentText)<0)parentId->addItem(parentText,parentText);parentId->setCurrentIndex(parentId->findData(parentText));form->addRow(QObject::tr("父装配"),parentId);
    std::array<QLineEdit*,3> center{},size{},rotation{};
    auto coordinate=[&](const QString& label,const QString& name,double value){auto* field=new QLineEdit(QString::number(value,'g',17),&dialog);field->setObjectName(name);form->addRow(label,field);return field;};
    for(int k=0;k<3;++k)center[k]=coordinate(QObject::tr("中心 %1 mm").arg(QChar("XYZ"[k])),QString("spaceradPartCenter%1").arg(k),initial.center[k]);
    for(int k=0;k<3;++k)size[k]=coordinate(QObject::tr("尺寸 D%1 mm").arg(QChar("XYZ"[k])),QString("spaceradPartSize%1").arg(k),initial.size[k]);
    auto* wall=coordinate(QObject::tr("箱壳壁厚 mm（0为实心）"),"spaceradPartWall",initial.wall);
    for(int k=0;k<3;++k)rotation[k]=coordinate(QObject::tr("旋转 R%1 °").arg(QChar("XYZ"[k])),QString("spaceradPartRotation%1").arg(k),initial.rotation[k]);
    auto* hint=new QLabel(QObject::tr("圆柱沿局部 Z 轴，Dx=Dy为直径、Dz为高度；球的三个尺寸均为直径。仅箱体支持空壳。建议位置可修改；提交时仍须通过原有几何、敏感区和源球门禁。"),&dialog);hint->setWordWrap(true);layout->addWidget(hint);
    auto* error=new QLabel(&dialog);error->setObjectName("spaceradPartError");error->setWordWrap(true);error->setStyleSheet("color:#a73520;");layout->addWidget(error);
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);buttons->setObjectName("spaceradPartButtons");layout->addWidget(buttons);
    auto constrain=[&]{auto value=shape->currentData().toString();bool box=value=="box",sphere=value=="sphere";size[1]->setEnabled(box);size[2]->setEnabled(!sphere);wall->setEnabled(box);if(!box){size[1]->setText(size[0]->text());wall->setText("0");}if(sphere)size[2]->setText(size[0]->text());};
    QObject::connect(shape,qOverload<int>(&QComboBox::currentIndexChanged),&dialog,[&](int){constrain();});QObject::connect(size[0],&QLineEdit::textChanged,&dialog,[&]{constrain();});constrain();
    std::optional<Project> result;
    QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    QObject::connect(buttons,&QDialogButtonBox::accepted,&dialog,[&]{try{
        auto number=[](QLineEdit* field){bool ok=false;double value=field->text().toDouble(&ok);if(!ok||!std::isfinite(value))throw std::runtime_error("Enter finite numeric coordinates and sizes");return value;};
        auto part=initial;part.id=id->text().toStdString();part.shape=shape->currentData().toString().toStdString();part.material=material->currentText().toStdString();part.sensitive=sensitive->isChecked();part.parent=parentId->currentData().toString().toStdString();
        for(int k=0;k<3;++k){part.center[k]=number(center[k]);part.size[k]=number(size[k]);part.rotation[k]=number(rotation[k]);}part.wall=number(wall);
        result=replaceId.empty()?addPart(source,part):replacePart(source,replaceId,part);dialog.accept();
    }catch(const std::exception& e){error->setText(QString::fromUtf8(e.what()));}});
    auto code=dialog.exec();if(!alive){owner.release();return std::nullopt;}if(code!=QDialog::Accepted)return std::nullopt;return result;
}
}
