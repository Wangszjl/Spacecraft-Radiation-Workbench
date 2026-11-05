#include <spacerad/Authoring.h>
#include <spacerad/Environment.h>
#include <spacerad/ImportanceReview.h>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv) {
    try {
        auto project = spacerad::Project::demo();
        if(spacerad::importanceReview(project,spacerad::Json::object()).at("status")!="not_applicable")
            throw std::runtime_error("Installed importance review API failed");
        if (argc == 2) {
            std::ifstream input(argv[1]);
            if (!input) throw std::runtime_error("Cannot read example project");
            spacerad::Json json;
            input >> json;
            project = spacerad::Project::fromJson(json);
        }
        const auto original = project.toJson();
        const auto part = spacerad::suggestPart(project, "box", true);
        const auto authored = spacerad::addPart(project, part);
        if (spacerad::removePart(authored, part.id).toJson() != original)
            throw std::runtime_error("Installed authoring roundtrip failed");
        auto manual = spacerad::setManualSpectrum(project, 0, project.spectra[0].particle,
                                                   {1, 2, 3}, {4, 2, 0});
        if (manual.spectra[0].integral != 4 || project.toJson() != original)
            throw std::runtime_error("Installed spectrum edit failed");
        const auto converted = manual.convertedQuantity(spacerad::engine::SpectrumQuantity::Fluence);
        if (std::abs(converted.expectedPrimaries() / manual.expectedPrimaries() - 1) > 1e-12)
            throw std::runtime_error("Installed normalization equivalence failed");
        if (spacerad::Project::fromJson(manual.toJson()).toJson() != manual.toJson())
            throw std::runtime_error("Installed serialization roundtrip failed");
        std::cout << "model-consumer=passed; spectra=" << project.spectra.size()
                  << "; integral=" << manual.spectra[0].integral << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
