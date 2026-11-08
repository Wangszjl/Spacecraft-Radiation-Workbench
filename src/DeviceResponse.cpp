#include "spacerad/DeviceResponse.h"
#include "Geometry.h"
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QSaveFile>
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>
#include <regex>

namespace spacerad {
namespace {
void need(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
QByteArray readFile(const QString& path,qint64 limit=8*1024*1024){QFile file(path);need(file.open(QIODevice::ReadOnly)&&file.size()>0&&file.size()<=limit,"Missing or oversized response CSV");auto raw=file.read(limit+1);need(file.error()==QFileDevice::NoError&&raw.size()<=limit&&file.atEnd(),"Cannot read bounded response artifact");return raw;}
QByteArray jsonBytes(const Json& value){return QByteArray::fromStdString(value.dump(2)+"\n");}
std::string sha(const QByteArray& value){return QCryptographicHash::hash(value,QCryptographicHash::Sha256).toHex().toStdString();}
void writeFile(const QString& path,const QByteArray& raw){QSaveFile file(path);need(file.open(QIODevice::WriteOnly)&&file.write(raw)==raw.size()&&file.commit(),"Cannot save response study artifact");}
std::vector<std::string> csvRow(const std::string& line){std::vector<std::string> fields;std::string field;bool quoted=false,closed=false;for(size_t i=0;i<line.size();++i){char c=line[i];if(quoted){if(c=='"'){if(i+1<line.size()&&line[i+1]=='"'){field.push_back('"');++i;}else{quoted=false;closed=true;}}else field.push_back(c);}else if(c==','){fields.push_back(field);field.clear();closed=false;}else if(c=='"'&&field.empty()&&!closed)quoted=true;else{need(!closed,"Invalid characters after a CSV quote");field.push_back(c);}}need(!quoted,"Unclosed CSV quote");fields.push_back(field);return fields;}
std::string trimmed(std::string value){auto isSpace=[](unsigned char c){return c==' '||c=='\t';};while(!value.empty()&&isSpace(value.front()))value.erase(value.begin());while(!value.empty()&&isSpace(value.back()))value.pop_back();return value;}
double parseNumber(const std::string& text){char* end=nullptr;double value=std::strtod(text.c_str(),&end);need(end!=text.c_str()&&*end=='\0'&&std::isfinite(value),"Response CSV contains an invalid number");return value;}
QString csvText(const std::string& value){auto text=QString::fromStdString(value);if(!text.isEmpty()&&(text.front().isSpace()||QString("=+-@").contains(text.front())))text.prepend('\'');text.replace('"',"\"\"");return '"'+text+'"';}
double spectrumAt(const Spectrum& spectrum,double energy){if(energy<=spectrum.energy.front())return spectrum.values.front();if(energy>=spectrum.energy.back())return spectrum.values.back();auto upper=std::upper_bound(spectrum.energy.begin(),spectrum.energy.end(),energy);size_t i=size_t(upper-spectrum.energy.begin()-1);if(spectrum.interpolation=="bin_constant")return spectrum.values[i];double f=(energy-spectrum.energy[i])/(spectrum.energy[i+1]-spectrum.energy[i]);return (1-f)*spectrum.values[i]+f*spectrum.values[i+1];}
double responseAt(const DeviceResponseCurve& curve,double energy){if(energy<=curve.energy_MeV.front())return curve.values.front();if(energy>=curve.energy_MeV.back())return curve.values.back();auto upper=std::upper_bound(curve.energy_MeV.begin(),curve.energy_MeV.end(),energy);size_t i=size_t(upper-curve.energy_MeV.begin()-1);double f=(energy-curve.energy_MeV[i])/(curve.energy_MeV[i+1]-curve.energy_MeV[i]);return (1-f)*curve.values[i]+f*curve.values[i+1];}
// Split on both grids. For a constant source bin, both endpoint limits
// belong to that same bin, even when the right edge starts the next bin.
double foldSpectrum(const Spectrum& spectrum,const DeviceResponseCurve& curve) {
    std::vector<double> knots=spectrum.energy;
    for(double energy:curve.energy_MeV)
        if(energy>spectrum.energy.front()&&energy<spectrum.energy.back()) knots.push_back(energy);
    std::sort(knots.begin(),knots.end());
    knots.erase(std::unique(knots.begin(),knots.end()),knots.end());
    long double integrated=0;
    const bool constant=spectrum.interpolation=="bin_constant";
    for(size_t i=1;i<knots.size();++i) {
        const double a=knots[i-1],b=knots[i];
        const long double sa=spectrumAt(spectrum,a),sb=constant?sa:spectrumAt(spectrum,b);
        const long double ra=responseAt(curve,a),rb=responseAt(curve,b);
        const long double width=static_cast<long double>(b)-a;
        // Exact integral of two linear factors; positive coefficients avoid
        // cancellation on steeply rising or falling spectra.
        integrated+=(width/3*sa)*ra+(width/6*sa)*rb+
                    (width/6*sb)*ra+(width/3*sb)*rb;
    }
    const double value=static_cast<double>(integrated);
    need(std::isfinite(value)&&value>=0,"Response integration overflow");
    return value;
}

bool sameFoldRows(const Json& a,const Json& b) {
    if(a.type()!=b.type())return false;
    if(a.is_array()||a.is_object()) {
        if(a.size()!=b.size())return false;
        if(a.is_array()) {
            for(size_t i=0;i<a.size();++i)if(!sameFoldRows(a[i],b[i]))return false;
        }else for(auto i=a.begin();i!=a.end();++i) {
            if(!b.contains(i.key()))return false;
            const auto& value=b.at(i.key());
            const auto key=i.key();
            if(key=="folded_value"||key=="spectrum_response_integral"||
               key=="niel_dose_Gy"||key=="poisson_probability_at_least_one") {
                if(!i->is_number_float()||!value.is_number_float())return false;
                const double x=i->get<double>(),y=value.get<double>();
                if(!std::isfinite(x)||!std::isfinite(y)||x<0||y<0||
                   std::abs(x-y)>1e-12*std::max(x,y))return false;
            }else if(!sameFoldRows(*i,value))return false;
        }
        return true;
    }
    return a==b;
}
DeviceResponseTable parseResponseCsv(const QByteArray& raw,const QString& fileName){need(raw.size()<=8*1024*1024,"Response CSV exceeds 8 MiB");QString text=QString::fromUtf8(raw);need(!text.contains(QChar::ReplacementCharacter),"Response CSV must be valid UTF-8");if(!text.isEmpty()&&text.front()==QChar(0xfeff))text.remove(0,1);text.replace("\r\n","\n");need(!text.contains('\r'),"Response CSV must use LF or CRLF line endings");auto lines=text.split('\n');if(!lines.isEmpty()&&lines.back().isEmpty())lines.removeLast();need(lines.size()>=3&&lines.size()<=65537,"Response CSV requires 2..65536 data rows");need(lines.front()=="device_id,metric,particle,energy_MeV,response,response_unit,angular_basis,reference","Expected response CSV header: device_id,metric,particle,energy_MeV,response,response_unit,angular_basis,reference");DeviceResponseTable table;table.sourceFile={{"file_name",fileName.toStdString()},{"size_bytes",raw.size()},{"sha256",sha(raw)}};std::map<std::pair<std::string,std::string>,DeviceResponseCurve> groups;int lineNumber=1;for(int index=1;index<lines.size();++index){++lineNumber;auto fields=csvRow(lines[index].toStdString());need(fields.size()==8,"Response CSV data row must have eight columns");for(auto& field:fields)field=trimmed(std::move(field));need(!fields[0].empty()&&!fields[2].empty()&&!fields[6].empty()&&!fields[7].empty(),"Response target, particle and reference are required");if(index==1){table.metric=fields[1];table.responseUnit=fields[5];table.angularBasis=fields[6];}need(fields[1]==table.metric&&fields[5]==table.responseUnit&&fields[6]==table.angularBasis,"One response CSV must use one metric, response unit and angular basis");need(fields[6]=="isotropic_omnidirectional","Angular basis must explicitly match the isotropic omnidirectional project environment");auto key=std::make_pair(fields[0],fields[2]);auto& curve=groups[key];if(curve.energy_MeV.empty()){curve.deviceId=fields[0];curve.particle=fields[2];curve.reference=fields[7];}need(curve.reference==fields[7],"Response reference must be consistent within each curve");try{curve.energy_MeV.push_back(parseNumber(fields[3]));curve.values.push_back(parseNumber(fields[4]));}catch(const std::exception&){throw std::runtime_error("Invalid response number at CSV row "+std::to_string(lineNumber));}}
    for(auto& item:groups)table.curves.push_back(std::move(item.second));table.validate();return table;}
Json fold(const Project& project,const DeviceResponseTable& table){project.validate();table.validate();auto geometry=engine::compileGeometry(project);std::set<std::string> sensitive;for(const auto& volume:geometry.volumes)if(volume.sensitive)sensitive.insert(volume.id);std::map<std::string,std::map<std::string,const DeviceResponseCurve*>> byTarget;for(const auto& curve:table.curves){need(sensitive.count(curve.deviceId)!=0,"Response device_id must match a sensitive project component");need(byTarget[curve.deviceId].emplace(curve.particle,&curve).second,"Duplicate response curve for device and particle");}
    Json rows=Json::array();for(const auto& device:byTarget){need(device.second.size()==project.spectra.size(),"Each selected device needs a response curve for every project species");Json components=Json::array();long double sum=0;for(const auto& spectrum:project.spectra){auto match=device.second.find(spectrum.particle);need(match!=device.second.end(),"Response table is missing a project particle species");const auto& curve=*match->second;need(curve.energy_MeV.front()<=spectrum.energy.front()&&curve.energy_MeV.back()>=spectrum.energy.back(),"Response curve must cover the complete spectrum; extrapolation is forbidden");double raw=foldSpectrum(spectrum,curve);double exposure=project.quantity==engine::SpectrumQuantity::Flux?project.duration:1;double value=raw*exposure;need(std::isfinite(value),"Response exposure multiplication overflow");sum+=value;components.push_back({{"particle",spectrum.particle},{"energy_range_MeV",{spectrum.energy.front(),spectrum.energy.back()}},{"response_reference",curve.reference},{"spectrum_response_integral",raw},{"exposure_multiplier",exposure},{"folded_value",value}});}double value=double(sum);need(std::isfinite(value),"Combined response overflow");Json row={{"device_id",device.first},{"metric",table.metric},{"response_unit",table.responseUnit},{"folded_value",value},{"components",components}};if(table.metric=="ddd"){row["result_unit"]="MeV_per_g";row["niel_dose_Gy"]=value*1.602176634e-10;need(std::isfinite(row["niel_dose_Gy"].get<double>()),"NIEL dose conversion overflow");}else{row["result_unit"]="expected_events_per_device";row["poisson_probability_at_least_one"]=-std::expm1(-value);row["probability_assumption"]="Poisson event model; independent arrivals and fixed response cross section";}rows.push_back(std::move(row));}
    return {{"schema","spacerad.device-response-study/2"},{"integration_method","piecewise_product_exact_v2"},{"sampling_basis","physical_spectrum_only; Monte Carlo sampling settings are not used"},{"metric",table.metric},{"spectrum_basis","Unshielded project environment spectrum folded directly; spacecraft attenuation and target-local secondaries are not applied"},{"response_unit",table.responseUnit},{"angular_basis",table.angularBasis},{"response_source",table.sourceFile},{"source_project",project.toJson()},{"source_quantity",engine::quantityName(project.quantity)},{"duration_s",project.duration},{"rows",rows},{"uncertainty","Response table fold has no inferred calibration uncertainty, cross-section covariance or Monte Carlo uncertainty"},{"limitations",Json::array({"Screening fold only; not a Geant4 DDD/DPA or SEE-event transport simulation","No extrapolation outside supplied response curves","Response units, device state and citation are supplied by the caller","The input spectrum is folded without spacecraft shielding or target-local secondary spectra","DDD is nonionizing-energy response in MeV/g and NIEL dose, not DPA or 1-MeV-neutron-equivalent fluence"})}};}
}
Json DeviceResponseTable::toJson() const{Json items=Json::array();for(const auto& curve:curves)items.push_back({{"device_id",curve.deviceId},{"particle",curve.particle},{"reference",curve.reference},{"energy_MeV",curve.energy_MeV},{"response",curve.values}});return {{"schema","spacerad.device-response-table/1"},{"metric",metric},{"response_unit",responseUnit},{"angular_basis",angularBasis},{"source_file",sourceFile},{"curves",items}};}
void DeviceResponseTable::validate() const{need(metric=="ddd"||metric=="see","Response metric must be ddd or see");need(angularBasis=="isotropic_omnidirectional","Unsupported or missing response angular basis");need(responseUnit==(metric=="ddd"?"MeV_cm2_per_g":"cm2_per_device"),"Response unit must match metric (MeV_cm2_per_g for DDD, cm2_per_device for SEE)");need(!curves.empty()&&curves.size()<=1024,"Response curve count must be 1..1024");need(sourceFile.is_object()&&sourceFile.contains("file_name")&&sourceFile.contains("size_bytes")&&sourceFile.at("size_bytes").is_number_integer()&&sourceFile.at("size_bytes").get<long long>()>=0&&sourceFile.contains("sha256")&&sourceFile.at("sha256").is_string()&&std::regex_match(sourceFile.at("sha256").get<std::string>(),std::regex("[a-f0-9]{64}")),"Response source identity is required");std::set<std::pair<std::string,std::string>> seen;for(const auto& curve:curves){need(!curve.deviceId.empty()&&curve.deviceId.size()<=128&&!curve.reference.empty()&&curve.reference.size()<=2048,"Response target and reference are required");need(curve.particle=="e-"||curve.particle=="proton"||curve.particle=="alpha"||curve.particle=="gamma"||curve.particle=="neutron","Unsupported response particle");need(seen.emplace(curve.deviceId,curve.particle).second,"Duplicate response curve for target/species");need(curve.energy_MeV.size()>=2&&curve.energy_MeV.size()<=4096&&curve.energy_MeV.size()==curve.values.size(),"Response curve requires 2..4096 paired points");for(size_t i=0;i<curve.energy_MeV.size();++i){need(std::isfinite(curve.energy_MeV[i])&&curve.energy_MeV[i]>0&&std::isfinite(curve.values[i])&&curve.values[i]>=0,"Response energies and values must be finite and nonnegative");if(i)need(curve.energy_MeV[i]>curve.energy_MeV[i-1],"Response energies must strictly increase");}}}
DeviceResponseTable loadDeviceResponseCsv(const QString& path){return parseResponseCsv(readFile(path),QFileInfo(path).fileName());}
Json foldDeviceResponseTable(const Project& project,const DeviceResponseTable& table){return fold(project,table);}
Json runDeviceResponseStudy(const Project& project,const QByteArray& csvBytes,const QString& sourceName){auto table=parseResponseCsv(csvBytes,sourceName);return fold(project,table);}
Json saveDeviceResponseStudy(const Project& project,const QString& responseCsvPath,const QString& newStudyDirectory) {
    const QFileInfo output(QDir::cleanPath(newStudyDirectory));
    need(output.isAbsolute()&&!output.exists(),"Choose a new absolute response study directory");
    const auto response=readFile(responseCsvPath);
    auto report=runDeviceResponseStudy(project,response,QFileInfo(responseCsvPath).fileName());
    const auto projectBytes=jsonBytes(project.toJson()),reportBytes=jsonBytes(report);
    need(projectBytes.size()<=8*1024*1024&&reportBytes.size()<=32*1024*1024,"Response study exceeds archive size limit");
    const auto parent=output.absoluteDir().absolutePath();
    need(QDir().mkpath(parent)&&QDir(parent).mkdir(output.fileName()),"Cannot create new response study directory");
    writeFile(newStudyDirectory+"/project.json",projectBytes);
    writeFile(newStudyDirectory+"/response.csv",response);
    writeFile(newStudyDirectory+"/study-result.json",reportBytes);
    writeFile(newStudyDirectory+"/study-manifest.json",jsonBytes({
        {"schema","spacerad.device-response-study-archive/2"},{"project_sha256",sha(projectBytes)},
        {"response_sha256",sha(response)},{"report_sha256",sha(reportBytes)}}));
    return report;
}
Json loadDeviceResponseStudy(const QString& directory) {
    need(!directory.trimmed().isEmpty(),"Choose a response study directory");
    auto projectBytes=readFile(directory+"/project.json"),response=readFile(directory+"/response.csv"),
         reportBytes=readFile(directory+"/study-result.json",32*1024*1024),
         manifestBytes=readFile(directory+"/study-manifest.json",65536);
    const auto manifest=Json::parse(manifestBytes.toStdString());
    const bool legacy=manifest.at("schema")=="spacerad.device-response-study-archive/1";
    need((legacy||manifest.at("schema")=="spacerad.device-response-study-archive/2")&&
         manifest.at("project_sha256")==sha(projectBytes)&&manifest.at("response_sha256")==sha(response)&&
         manifest.at("report_sha256")==sha(reportBytes),"Response study archive identity mismatch");
    auto stored=Json::parse(reportBytes.toStdString());
    const auto project=Project::fromJson(Json::parse(projectBytes.toStdString()));
    need(stored.at("schema")== (legacy?"spacerad.device-response-study/1":"spacerad.device-response-study/2"),
         "Response report and manifest versions differ");
    auto expected=runDeviceResponseStudy(project,response,QString::fromStdString(stored.at("response_source").at("file_name")));
    if(legacy) {
        need(stored.at("source_project")==project.toJson()&&stored.at("response_source")==expected.at("response_source"),
             "Legacy response report input identities differ");
        expected["archive_review"]="legacy_inputs_recomputed_v2";
        expected["legacy_report_sha256"]=sha(reportBytes);
    }else {
        need(sameFoldRows(stored.at("rows"),expected.at("rows")),"Stored response totals differ from source reconstruction");
        stored.erase("rows");auto metadata=expected;metadata.erase("rows");
        need(stored==metadata,"Stored response metadata differs from source reconstruction");
    }
    need(readFile(directory+"/study-manifest.json",65536)==manifestBytes,"Study changed while loading");
    return expected;
}
QByteArray deviceResponseCsv(const Json& report){need(report.at("schema")=="spacerad.device-response-study/2","Recompute legacy response studies before export");QString output="device_id,metric,response_unit,folded_value,result_unit,niel_dose_Gy,poisson_probability_at_least_one\n";for(const auto& row:report.at("rows")){auto cell=[&](const char* key){return row.contains(key)?QString::fromStdString(row.at(key).dump()):QString{};};output+=csvText(row.at("device_id").get<std::string>())+','+csvText(row.at("metric").get<std::string>())+','+csvText(row.at("response_unit").get<std::string>())+','+cell("folded_value")+','+csvText(row.at("result_unit").get<std::string>())+','+cell("niel_dose_Gy")+','+cell("poisson_probability_at_least_one")+'\n';}return output.toUtf8();}
}
