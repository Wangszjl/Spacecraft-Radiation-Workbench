#pragma once
#include "BatchStatistics.h"
#include <QString>
namespace spacerad::engine {
struct ShieldingScanOptions {
    std::string shellId;
    std::vector<double> thicknesses;
    BatchOptions batch;
    static ShieldingScanOptions fromJson(const Json&);
    Json toJson() const;
};
struct ShieldingCase { int index; double thickness; std::string particle; Project project; };
std::vector<ShieldingCase> planShieldingScan(const Project&,const ShieldingScanOptions&);
Json shieldingPlanJson(const Project&,const ShieldingScanOptions&);
// Result slots correspond to planned cases; null slots were not completed.
Json shieldingCurves(const std::vector<ShieldingCase>&,const std::vector<Json>&);
QString shieldingCsv(const Json& result);
}
