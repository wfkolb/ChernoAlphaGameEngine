#include "tools/AssetImporter.h"
#include "core/log.h"
#include <iostream>
#include <string>
#include <string_view>

int main(int argc, char* argv[]) {
    // Parse --input <path> --output <path>
    std::string input, output;
    for (int i = 1; i < argc - 1; ++i) {
        if (std::string_view(argv[i]) == "--input")  input  = argv[i + 1];
        if (std::string_view(argv[i]) == "--output") output = argv[i + 1];
    }
    if (input.empty() || output.empty()) {
        std::cerr << "Usage: asset_cooker --input <file.gltf> --output <file.easset>\n";
        return 1;
    }

    auto result = engine::tools::importGltf(input, output);
    if (!result.ok) {
        std::cerr << "Import failed: " << result.errorMessage << "\n";
        return 1;
    }
    return 0;
}
