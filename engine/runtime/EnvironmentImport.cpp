#include "EnvironmentImport.h"
#include <QFile>
#include <QStringList>
#include <cmath>
#include <limits>
#include <set>
namespace spacerad::engine {
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
std::string utf8(const QString& s){return s.toUtf8().toStdString();}
QStringList fields(const QString& line){
    QStringList result;QString value;bool quoted=false,closed=false;
    for(int i=0;i<line.size();++i){auto c=line[i];
        if(c=='\''){
            if(quoted&&i+1<line.size()&&line[i+1]=='\''){value+='\'';++i;}
            else if(quoted){quoted=false;closed=true;}
            else{require(value.trimmed().isEmpty()&&!closed,"Unexpected quote in SPENVIS field");quoted=true;}
        }else if(c==','&&!quoted){result<<value.trimmed();value.clear();closed=false;}
        else{require(!closed||c.isSpace(),"Characters after quoted SPENVIS field");value+=c;}
    }
    require(!quoted,"Unterminated SPENVIS string");result<<value.trimmed();return result;
}
int integer(const QString& s){bool ok=false;int v=s.toInt(&ok);require(ok,"Expected integer in SPENVIS header");return v;}
double real(QString s){s.replace('D','E');s.replace('d','e');bool ok=false;double v=s.toDouble(&ok);require(ok&&std::isfinite(v),"Invalid or nonfinite SPENVIS number");return v;}
struct Variable {QString name,unit,title;int size=0,offset=0;};
struct Block {Json metadata=Json::object();QStringList comments;std::vector<Variable> variables;std::vector<std::vector<double>> rows;};
std::vector<Block> blocks(const QByteArray& bytes){
    require(!bytes.isEmpty()&&bytes.size()<=10*1024*1024,"SPENVIS import requires a nonempty file <=10 MiB");
    auto content=QString::fromUtf8(bytes);if(content.startsWith(QChar(0xFEFF)))content.remove(0,1);
    auto lines=content.split('\n');int pos=0;std::vector<Block> result;
    while(pos<lines.size()){
        if(lines[pos].trimmed().isEmpty()){++pos;continue;}
        auto head=fields(lines[pos++]);require(head.size()==9&&head[0]=="*","Expected SPENVIS new-format block header");
        int nh=integer(head[1]),nc=integer(head[2]),nm=integer(head[3]),na=integer(head[4]),nv=integer(head[5]);
        int nd=integer(head[6]),nl=integer(head[7]),remaining=integer(head[8]);
        require(nc>=0&&nm>=0&&na>=0&&nv>0&&nc<=10000&&nm<=10000&&na<=10000&&nv<=1000,"Invalid SPENVIS section counts");
        require(nh==1+nc+nm+na+nv&&nd>0&&nd<=100000&&(nl==-1||nl>=0)&&remaining>=0,"Inconsistent SPENVIS block counts");
        require(nh<=lines.size()-pos+1,"Truncated SPENVIS header");Block b;
        for(int i=0;i<nc;++i){auto f=fields(lines[pos++]);require(f.size()==1,"Malformed SPENVIS comment");b.comments<<f[0];}
        for(int i=0;i<nm;++i){auto f=fields(lines[pos++]);require(f.size()>=3,"Malformed SPENVIS metadata");
            auto key=utf8(f[0]);require(!b.metadata.contains(key),"Duplicate SPENVIS metadata key");int n=integer(f[1]);
            if(n==-1){require(f.size()==3,"Invalid string metadata dimension");b.metadata[key]=utf8(f[2]);}
            else{require(n>0&&n==f.size()-2,"Invalid numeric metadata dimension");Json values=Json::array();
                for(int k=0;k<n;++k)values.push_back(real(f[k+2]));b.metadata[key]=values;}
        }
        pos+=na;int offset=0;std::set<std::string> names;
        for(int i=0;i<nv;++i){auto f=fields(lines[pos++]);require(f.size()>=4,"Malformed SPENVIS variable definition");
            int size=integer(f[2]);require(size>0&&size<=nd-offset,"Invalid SPENVIS variable dimension");
            require(names.insert(utf8(f[0])).second,"Duplicate SPENVIS variable name");
            b.variables.push_back({f[0],f[1],f[3],size,offset});offset+=size;
        }
        require(offset==nd,"SPENVIS variable dimensions do not match data column count");bool footer=false;
        for(;pos<lines.size();++pos){auto line=lines[pos].trimmed();if(line.isEmpty())continue;
            if(line.startsWith('\'')){auto f=fields(line);require(f.size()==1&&!f[0].startsWith('*'),"Error or unsupported SPENVIS block footer");footer=true;++pos;break;}
            auto f=fields(line);require(f.size()==nd,"SPENVIS data column count mismatch");std::vector<double> row;
            for(const auto& item:f)row.push_back(real(item));b.rows.push_back(std::move(row));
            require(b.rows.size()<=100000,"Too many SPENVIS data rows");
        }
        require(footer&&!b.rows.empty(),"Missing SPENVIS footer or data");
        require(nl==-1||nl==b.rows.size(),"SPENVIS declared row count mismatch");result.push_back(std::move(b));
        require(result.size()<=1000,"Too many SPENVIS blocks");
        if(remaining>0)require(pos<lines.size(),"Missing remaining SPENVIS blocks");
    }
    require(!result.empty(),"No SPENVIS blocks found");return result;
}
}
Json inspectSpenvis(const QByteArray& bytes){
    auto input=blocks(bytes);Json report=Json::array();
    for(size_t i=0;i<input.size();++i){const auto& b=input[i];Json variables=Json::array();
        for(const auto& v:b.variables)variables.push_back({{"name",utf8(v.name)},{"unit",utf8(v.unit)},{"elements",v.size},{"title",utf8(v.title)}});
        report.push_back({{"block",i},{"rows",b.rows.size()},{"variables",variables},{"metadata",b.metadata}});
    }
    return report;
}
EnvironmentImport parseSpenvis(const QByteArray& bytes,const Json& options){
    require(options.at("schema")=="scdat.spenvis-import/1","Unsupported import options schema");
    require(options.at("assume_isotropic")==true,"Explicit isotropic assumption is required for transport");
    require(options.at("energy_unit")=="MeV_per_particle","Confirm source ENERGY is MeV per particle; other units are unsupported");
    require(options.at("block").is_number_integer()&&options.at("row").is_number_integer(),"Block and row must be integer indices");
    auto input=blocks(bytes);auto bi=options.at("block").get<std::int64_t>(),ri=options.at("row").get<std::int64_t>();
    require(bi>=0&&bi<input.size(),"SPENVIS block index outside file");const auto& b=input[size_t(bi)];
    require(ri>=0&&ri<b.rows.size(),"SPENVIS row index outside block");
    auto name=options.at("variable").get<std::string>();const Variable* selected=nullptr;
    for(const auto& v:b.variables)if(utf8(v.name)==name)selected=&v;
    require(selected,"Selected SPENVIS variable not found");const auto& v=*selected;
    require(name=="FLUX_EL"||name=="FLUX_PR","Only FLUX_EL / FLUX_PR spectral variables are supported");
    auto unit=v.unit;unit.remove(' ');bool perSr=unit.contains("sr-1");unit.remove("sr-1");
    bool flux=unit=="cm-2s-1"||unit=="cm-2s-1MeV-1";
    bool fluence=unit=="cm-2"||unit=="cm-2MeV-1";
    require(flux||fluence,"Unsupported SPENVIS spectral unit; no implicit unit conversion");bool integral=!unit.contains("MeV-1");
    require(!(integral&&v.title.contains("Differential",Qt::CaseInsensitive))&&
            !(!integral&&v.title.contains("Integral",Qt::CaseInsensitive)),"SPENVIS spectral title and unit disagree");
    std::vector<double> energy,values;
    if(v.size>1){
        if(b.metadata.contains("ENERGY_UNIT"))require(b.metadata.at("ENERGY_UNIT")=="MeV","Unsupported explicit ENERGY_UNIT metadata");
        require(b.metadata.contains("ENERGY")&&b.metadata.at("ENERGY").is_array(),"Vector spectrum requires numeric ENERGY metadata in MeV");
        energy=b.metadata.at("ENERGY").get<std::vector<double>>();require(energy.size()==v.size,"ENERGY/spectral vector size mismatch");
        const auto& row=b.rows[size_t(ri)];values.assign(row.begin()+v.offset,row.begin()+v.offset+v.size);
    }else{
        require(ri==0,"Scalar-column spectrum uses the whole block; row must be 0");const Variable* e=nullptr;
        for(const auto& candidate:b.variables)if(candidate.name=="ENERGY")e=&candidate;
        require(e&&e->size==1&&e->unit=="MeV","Scalar-column spectrum requires ENERGY variable with MeV unit");
        // A table of varying time/position samples must never become a single
        // spectrum just because it also has an ENERGY column.
        for(const auto& auxiliary:b.variables){
            if(auxiliary.name=="ENERGY"||auxiliary.name=="FLUX_EL"||auxiliary.name=="FLUX_PR")continue;
            for(const auto& row:b.rows)for(int k=0;k<auxiliary.size;++k)
                require(row[auxiliary.offset+k]==b.rows.front()[auxiliary.offset+k],
                    "Scalar energy block has varying auxiliary/time/position columns; split conditions explicitly");
        }
        for(const auto& row:b.rows){energy.push_back(row[e->offset]);values.push_back(row[v.offset]);}
    }
    EnvironmentImport output;output.quantity=flux?SpectrumQuantity::Flux:SpectrumQuantity::Fluence;
    std::string particle=name=="FLUX_EL"?"e-":"proton";
    if(integral)output.spectrum=integralSpectrum(energy,values,particle,perSr,output.quantity);
    else{auto& s=output.spectrum;s.particle=particle;s.energy=energy;s.values=values;for(auto& x:s.values)if(perSr)x*=4*pi;s.prepare();
        if(perSr)s.provenance["conversions"].push_back({{"operation","isotropic_per_sr_to_omnidirectional"},{"factor",4*pi}});}
    if(b.metadata.contains("MODEL")&&b.metadata.at("MODEL").is_string())output.metadata["model"]=b.metadata.at("MODEL");
    // Numeric EPOCH (often fractional year) is preserved raw, never invented as a UTC timestamp.
    output.mapping={{"format","SPENVIS new tabular"},{"block",bi},{"row",ri},{"variable",name},
        {"input_unit",utf8(v.unit)},{"input_title",utf8(v.title)},{"input_metadata",b.metadata},
        {"selection",v.size>1?"single vector row; not an orbit/time average":"all energy rows in scalar-column block"},
        {"angular_assumption","isotropic; explicitly selected by operator"},
        {"minimum_energy_MeV",energy.front()},{"maximum_energy_MeV",energy.back()},
        {"energy_unit_basis",v.size>1?"SPENVIS energetic-particle ENERGY metadata convention; MeV per particle":"ENERGY column declares MeV"}};
    output.spectrum.provenance["import_mapping"]=output.mapping;
    output.spectrum.provenance["original_quantity"]=integral?(flux?"integral_flux":"integral_fluence"):quantityName(output.quantity);
    output.spectrum.provenance["original_unit"]=utf8(v.unit);
    return output;
}
EnvironmentImport loadSpenvis(const QString& path,const Json& options){
    QFile file(path);require(file.open(QIODevice::ReadOnly),"Cannot open SPENVIS input");
    require(file.size()<=10*1024*1024,"SPENVIS file exceeds import limit");auto bytes=file.readAll();require(file.error()==QFileDevice::NoError,"Cannot read SPENVIS input");
    auto identity=sourceFileIdentity(path,bytes,true);
    if(options.contains("expected_sha256"))require(options.at("expected_sha256")==identity.at("sha256"),"Source file changed after import preview; select it again");
    auto output=parseSpenvis(bytes,options);output.spectrum.provenance["source_file"]=identity;return output;
}
Project applyEnvironmentImport(const Project& base,const EnvironmentImport& imported,bool synthetic){
    auto project=base;project.spectra={imported.spectrum};project.quantity=imported.quantity;
    project.environmentMetadata=imported.metadata;project.synthetic=synthetic;
    project.environmentSource="SPENVIS import: "+imported.spectrum.provenance.at("source_file").at("file_name").get<std::string>()+
        "; explicit isotropic assumption; selection in spectrum provenance";
    project.validate();return project;
}
}
