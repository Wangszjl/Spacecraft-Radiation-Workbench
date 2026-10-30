#pragma once
#include "spacerad/Project.h"
#include <QWidget>
#include <memory>
namespace spacerad {
class GeometryView final:public QWidget {
public:
 explicit GeometryView(QWidget* parent=nullptr);
 ~GeometryView() override;
 void setProject(const Project&);
 void fit();
 void select(const std::string& id);
private:struct Impl;std::unique_ptr<Impl> d;
};
}
