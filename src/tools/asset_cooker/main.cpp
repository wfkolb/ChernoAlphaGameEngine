#include "tools/AssetImporter.h"
#include "tools/IblCooker.h"
#include "core/log.h"
#include <iostream>
#include <string>
#include <string_view>

int main(int argc, char* argv[]) {
    // Parse flags
    std::string input, output;
    bool        cubemapMode = false;

    for (int i = 1; i < argc - 1; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == "--input")   input       = argv[i + 1];
        if (arg == "--output")  output      = argv[i + 1];
        if (arg == "--cubemap") cubemapMode = true;
    }
    // --cubemap flag may be the last argument (no following value)
    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--cubemap") cubemapMode = true;
    }

    if (input.empty() || output.empty()) {
        std::cerr
            << "Usage:\n"
            << "  asset_cooker --input <file.gltf|glb> --output <file.easset>\n"
            << "  asset_cooker --cubemap --input <file.hdr> --output <file.easset>\n";
        return 1;
    }

    if (cubemapMode) {
        // IBL precompute: HDR equirectangular → .easset v4 with CMAP+BRDF sections
        const bool ok = engine::tools::cookIblAsset(input, output);
        if (!ok) {
            std::cerr << "IBL cook failed for: " << input << "\n";
            return 1;
        }
        std::cout << "IBL asset written to: " << output << "\n";
        return 0;
    }

    // Default: mesh import
    auto result = engine::tools::importGltf(input, output);
    if (!result.ok) {
        std::cerr << "Import failed: " << result.errorMessage << "\n";
        return 1;
    }
    return 0;
}
