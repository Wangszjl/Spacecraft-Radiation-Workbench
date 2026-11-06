#include "SourceSamplingDialog.h"
#include "spacerad/ImportanceConfig.h"
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>
#include <cmath>
#include <memory>
#include <stdexcept>
namespace spacerad {
std::optional<Project> editSourceSamplingDialog(QWidget* parent,const Project& source){
    source.validate();
    std::unique_ptr<QDialog> owner(new QDialog(parent));QPointer<QDialog> alive(owner.get());
    auto& dialog=*owner;dialog.setObjectName("spaceradSamplingDialog");
    dialog.setWindowTitle(QObject::tr("SpaceRad · 重要性抽样设置"));dialog.resize(700,610);
    auto* layout=new QVBoxLayout(&dialog);
    auto* help=new QLabel(QObject::tr("改变源的提议抽样频率，Worker 按完整初级历史施加似然权重恢复原始谱。能谱数值、轨道来源、时长与几何保留。配置成功后旧结果会清除。零命中区域仍需单独核查。"),&dialog);
    help->setWordWrap(true);layout->addWidget(help);
    auto* enabled=new QCheckBox(QObject::tr("启用重要性抽样"),&dialog);enabled->setObjectName("spaceradSamplingEnabled");
    enabled->setChecked(!source.sourceSampling.is_null());layout->addWidget(enabled);
    auto* form=new QFormLayout;
    auto initial=[&](const char* key,double fallback){return source.sourceSampling.is_null()?fallback:source.sourceSampling.at(key).get<double>();};
    auto* physical=new QLineEdit(QString::number(initial("physical_fraction",.2),'g',17),&dialog);
    physical->setObjectName("spaceradSamplingPhysical");
    auto* tail=new QLineEdit(QString::number(initial("tail_fraction",.001),'g',17),&dialog);
    tail->setObjectName("spaceradSamplingTail");
    form->addRow(QObject::tr("原谱混合率 f（0.05≤f<1）"),physical);
    form->addRow(QObject::tr("最高能尾部物理概率 τ（0.00001–0.5）"),tail);
    layout->addLayout(form);
    auto* custom=new QCheckBox(QObject::tr("自定义入射粒子提议份额"),&dialog);custom->setObjectName("spaceradSamplingCustomSpecies");
    custom->setChecked(!source.sourceSampling.is_null()&&source.sourceSampling.contains("proposal_species_fractions"));layout->addWidget(custom);
    auto* table=new QTableWidget(int(source.spectra.size()),3,&dialog);table->setObjectName("spaceradSamplingSpecies");
    table->setMinimumHeight(110);table->setMaximumHeight(210);
    table->setHorizontalHeaderLabels({QObject::tr("入射粒子"),QObject::tr("原始物理份额"),QObject::tr("提议份额（正值且总和为 1）")});
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    for(int row=0;row<int(source.spectra.size());++row){
        const auto& s=source.spectra[row];const double fraction=s.integral/source.integratedSpectrum();
        auto* particle=new QTableWidgetItem(QString::fromStdString(s.particle));particle->setFlags(particle->flags()&~Qt::ItemIsEditable);table->setItem(row,0,particle);
        auto* physicalItem=new QTableWidgetItem(QString::number(fraction,'g',8));physicalItem->setToolTip(QString::number(fraction,'g',17));physicalItem->setFlags(physicalItem->flags()&~Qt::ItemIsEditable);table->setItem(row,1,physicalItem);
        double proposal=fraction;
        if(custom->isChecked())proposal=source.sourceSampling.at("proposal_species_fractions").at(s.particle);
        table->setItem(row,2,new QTableWidgetItem(QString::number(proposal,'g',17)));
    }
    layout->addWidget(table);
    auto* preview=new QLabel(&dialog);preview->setObjectName("spaceradSamplingPreview");preview->setWordWrap(true);preview->setMinimumHeight(55);layout->addWidget(preview);
    auto* error=new QLabel(&dialog);error->setObjectName("spaceradSamplingError");error->setWordWrap(true);error->setMinimumHeight(20);error->setStyleSheet("color:#a02a22");layout->addWidget(error);
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);
    buttons->setObjectName("spaceradSamplingButtons");layout->addWidget(buttons);
    auto real=[](const QString& text){bool ok=false;double v=text.toDouble(&ok);if(!ok||!std::isfinite(v))throw std::runtime_error("Finite numeric sampling parameters are required");return v;};
    auto update=[&]{
        physical->setEnabled(enabled->isChecked());tail->setEnabled(enabled->isChecked());custom->setEnabled(enabled->isChecked());
        table->setEnabled(enabled->isChecked()&&custom->isChecked());
        if(!enabled->isChecked()){preview->setText(QObject::tr("使用原始物理谱和粒子份额抽样。"));return;}
        try{
            double f=real(physical->text()),t=real(tail->text());
            preview->setText(QObject::tr("高能尾部提议概率：%1%。事件同时乘粒子和能量似然权重。较小 f 会减少主体谱采样，较大的尾部概率不保证舱内统计充分。")
                .arg(QString::number((1-f+f*t)*100,'g',7)));
        }catch(...){preview->setText(QObject::tr("输入有效参数以预览提议概率。"));}
    };
    QObject::connect(enabled,&QCheckBox::toggled,&dialog,[&](bool){update();});
    QObject::connect(custom,&QCheckBox::toggled,&dialog,[&](bool){update();});
    QObject::connect(physical,&QLineEdit::textChanged,&dialog,[&](const QString&){update();});
    QObject::connect(tail,&QLineEdit::textChanged,&dialog,[&](const QString&){update();});
    std::optional<Project> result;
    QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    QObject::connect(buttons,&QDialogButtonBox::accepted,&dialog,[&]{
        try{
            if(!enabled->isChecked())result=disableImportanceSampling(source);
            else{
                std::map<std::string,double> fractions;
                if(custom->isChecked())for(int row=0;row<table->rowCount();++row)
                    fractions[table->item(row,0)->text().toStdString()]=real(table->item(row,2)->text());
                result=configureImportanceSampling(source,real(physical->text()),real(tail->text()),fractions);
            }
            dialog.accept();
        }catch(const std::exception& e){error->setText(QString::fromUtf8(e.what()));}
    });
    update();const auto code=dialog.exec();
    if(!alive){owner.release();return std::nullopt;}
    return code==QDialog::Accepted?result:std::nullopt;
}
}
