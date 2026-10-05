#pragma once

#include <filesystem>
#include <fstream>
#include <functional>

namespace NavKit::ProcessFlow {
    inline bool extractScene(const std::filesystem::path& outputPath, const std::function<int()>& connect,
        const std::function<int()>& extract, const std::function<int()>& close) {
        std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
        if (!output) {
            return false;
        }
        output.close();
        if (!output) {
            return false;
        }

        return connect() == 0 && extract() == 0 && close() == 0;
    }

    inline bool buildNavp(const std::function<bool()>& build, const std::function<void()>& prune) {
        if (!build()) {
            return false;
        }
        prune();
        return true;
    }

    inline bool buildAirg(const std::function<bool()>& build, const std::function<void(bool)>& finish) {
        const bool succeeded = build();
        finish(succeeded);
        return succeeded;
    }
} // namespace NavKit::ProcessFlow
