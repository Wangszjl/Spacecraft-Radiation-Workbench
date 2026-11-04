#include "GeometryView.h"
#include "Geometry.h"
#include "runtime/VoxelExport.h"
#include <QVBoxLayout>
#include <QVTKOpenGLNativeWidget.h>
#include <vtkActor.h>
#include <vtkCamera.h>
#include <vtkCubeSource.h>
#include <vtkCylinderSource.h>
#include <vtkSphereSource.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkDataSetMapper.h>
#include <vtkImageData.h>
#include <vtkLookupTable.h>
#include <vtkMatrix3x3.h>
#include <vtkCellData.h>
#include <vtkDoubleArray.h>
#include <vtkMatrix4x4.h>
#include <vtkNew.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkRenderer.h>
#include <vtkSmartPointer.h>
#include <map>
#include <limits>
#include <stdexcept>
namespace spacerad {
struct GeometryView::Impl {
 struct Entry{std::vector<vtkActor*> actors;bool visible=true,sensitive=false;};
 vtkNew<vtkGenericOpenGLRenderWindow> window;vtkNew<vtkRenderer> renderer;QVTKOpenGLNativeWidget* widget=nullptr;std::map<std::string,Entry> parts;vtkSmartPointer<vtkActor> voxelActor;std::string voxelVolume,selection;
};
GeometryView::GeometryView(QWidget* parent):QWidget(parent),d(new Impl){auto* l=new QVBoxLayout(this);l->setContentsMargins(0,0,0,0);d->widget=new QVTKOpenGLNativeWidget(this);d->widget->setRenderWindow(d->window);d->window->AddRenderer(d->renderer);d->renderer->SetBackground(.91,.94,.97);d->renderer->SetBackground2(.99,.995,1.);d->renderer->GradientBackgroundOn();l->addWidget(d->widget);setMinimumSize(320,220);}
GeometryView::~GeometryView()=default;
void GeometryView::setProject(const Project& project){
 auto compiled=spacerad::engine::compileGeometry(project);d->parts.clear();d->renderer->RemoveAllViewProps();d->voxelActor=nullptr;d->voxelVolume.clear();d->selection.clear();
 for(const auto& placed:compiled.placements){const auto& v=compiled.volumes.at(placed.index);auto& entry=d->parts[v.id];entry.visible=placed.visible;entry.sensitive=v.sensitive;
  auto add=[&](vtkAlgorithmOutput* output,bool cylinder){vtkNew<vtkPolyDataMapper> mapper;mapper->SetInputConnection(output);vtkNew<vtkActor> actor;actor->SetMapper(mapper);vtkNew<vtkMatrix4x4> matrix;matrix->Identity();for(int i=0;i<3;++i){for(int k=0;k<3;++k)matrix->SetElement(i,k,placed.transform.rotation[i][k]);matrix->SetElement(i,3,placed.transform.translation[i]);}actor->SetUserMatrix(matrix);if(cylinder)actor->RotateX(90);actor->GetProperty()->SetColor(v.sensitive?.97:.20,v.sensitive?.54:.49,v.sensitive?.13:.68);actor->GetProperty()->SetOpacity(v.sensitive?1.:.30);actor->GetProperty()->EdgeVisibilityOn();actor->GetProperty()->SetEdgeColor(.19,.28,.36);actor->SetVisibility(placed.visible);d->renderer->AddActor(actor);entry.actors.push_back(actor.GetPointer());};
  if(v.shape=="sphere"){vtkNew<vtkSphereSource> s;s->SetRadius(v.size[0]/2);s->SetThetaResolution(48);s->SetPhiResolution(32);add(s->GetOutputPort(),false);}
  else if(v.shape=="cylinder"){vtkNew<vtkCylinderSource> s;s->SetRadius(v.size[0]/2);s->SetHeight(v.size[2]);s->SetResolution(64);add(s->GetOutputPort(),true);}
  else for(const auto& b:spacerad::engine::materialBoxes(v)){vtkNew<vtkCubeSource> s;s->SetCenter(b.center.data());s->SetXLength(b.size[0]);s->SetYLength(b.size[1]);s->SetZLength(b.size[2]);add(s->GetOutputPort(),false);}
 }
 auto* camera=d->renderer->GetActiveCamera();camera->SetPosition(1,-1,.8);camera->SetFocalPoint(0,0,0);camera->SetViewUp(0,0,1);fit();
}
void GeometryView::fit(){d->renderer->ResetCamera();d->window->Render();}
void GeometryView::select(const std::string& id){
 if(!id.empty()&&!d->parts.count(id))throw std::runtime_error("Component is not present in the displayed project");
 d->selection=id;
 for(auto& item:d->parts)for(auto* actor:item.second.actors){actor->SetVisibility(item.second.visible);actor->GetProperty()->SetOpacity(item.second.sensitive?1.:.30);actor->GetProperty()->SetEdgeColor(.19,.28,.36);actor->GetProperty()->SetLineWidth(1);}
 if(!id.empty()){
  const double inf=std::numeric_limits<double>::infinity();double bounds[6]={inf,-inf,inf,-inf,inf,-inf};
  for(auto* actor:d->parts.at(id).actors){actor->SetVisibility(true);actor->GetProperty()->SetOpacity(1);actor->GetProperty()->SetEdgeColor(.98,.15,.05);actor->GetProperty()->SetLineWidth(3);auto* b=actor->GetBounds();for(int k=0;k<3;++k){bounds[2*k]=std::min(bounds[2*k],b[2*k]);bounds[2*k+1]=std::max(bounds[2*k+1],b[2*k+1]);}}
  d->renderer->ResetCamera(bounds);d->renderer->ResetCameraClippingRange();
 }
 if(d->voxelActor&&!id.empty()&&id==d->voxelVolume){d->renderer->ResetCamera(d->voxelActor->GetBounds());d->renderer->ResetCameraClippingRange();}
 d->window->Render();
}
void GeometryView::setVoxelField(const Json* field){
 if(d->voxelActor){d->renderer->RemoveActor(d->voxelActor);d->voxelActor=nullptr;d->voxelVolume.clear();}
 if(!field){d->window->Render();return;}
 (void)engine::voxelVti(*field); // Validate the grid before allocating a VTK image.
 const auto counts=field->at("counts").get<std::array<int,3>>();
 const auto spacing=field->at("spacing_mm").get<std::array<double,3>>();
 const auto center=field->at("world_center_mm").get<std::array<double,3>>();
 const auto local=field->at("origin_local_mm").get<std::array<double,3>>();
 const auto rotation=field->at("world_rotation");
 vtkNew<vtkImageData> image;image->SetDimensions(counts[0]+1,counts[1]+1,counts[2]+1);image->SetSpacing(spacing.data());
 vtkNew<vtkMatrix3x3> direction;
 for(int row=0;row<3;++row)for(int col=0;col<3;++col)direction->SetElement(row,col,rotation.at(row).at(col).get<double>());
 image->SetDirectionMatrix(direction);
 double origin[3]={center[0],center[1],center[2]};
 for(int row=0;row<3;++row)for(int col=0;col<3;++col)origin[row]+=rotation.at(row).at(col).get<double>()*local[col];
 image->SetOrigin(origin);
 vtkNew<vtkDoubleArray> doses;doses->SetName("TID_Gy");doses->SetNumberOfComponents(1);
 double lo=std::numeric_limits<double>::infinity(),hi=0;
 for(const auto& row:field->at("rows")){const double value=row.at("tid_Gy").get<double>();doses->InsertNextValue(value);lo=std::min(lo,value);hi=std::max(hi,value);}
 image->GetCellData()->SetScalars(doses);
 if(hi<=lo)hi=lo+std::max(1e-30,std::abs(lo)*1e-6);
 vtkNew<vtkLookupTable> colors;colors->SetNumberOfTableValues(256);colors->SetHueRange(.66,0.0);colors->SetSaturationRange(.9,.95);colors->SetValueRange(.95,.9);colors->SetAlphaRange(.45,.9);colors->Build();
 vtkNew<vtkDataSetMapper> mapper;mapper->SetInputData(image);mapper->SetScalarModeToUseCellData();mapper->SetLookupTable(colors);mapper->SetScalarRange(lo,hi);mapper->SetColorModeToMapScalars();
 d->voxelActor=vtkSmartPointer<vtkActor>::New();d->voxelActor->SetMapper(mapper);d->voxelActor->GetProperty()->SetOpacity(.82);d->voxelActor->GetProperty()->EdgeVisibilityOn();d->voxelActor->GetProperty()->SetEdgeColor(.08,.12,.18);
 d->renderer->AddActor(d->voxelActor);d->voxelVolume=field->at("volume_id").get<std::string>();
 d->voxelActor->SetVisibility(true);d->window->Render();
}
}
