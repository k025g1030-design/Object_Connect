#include "../TestSupport.hpp"

#include "ObjectConnect/Text/FontSystem.hpp"

#include <iostream>
#include <string>

int main() {
    object_connect::tests::TestContext context;
    object_connect::FontSystem fonts;

    fonts.Finalize();
    fonts.Finalize();
    context.Expect(!fonts.IsInitialized(),
                   "Finalize is idempotent before initialization");

    std::string error{"stale error"};
    const object_connect::FontHandle unloaded =
        fonts.LoadFont("missing.ttf", error);
    context.Expect(!unloaded &&
                       error.find("must be initialized") != std::string::npos,
                   "LoadFont fails clearly before renderer initialization");

    constexpr object_connect::FontHandle stale{0x1234u};
    const object_connect::TextMetrics metrics =
        fonts.MeasureText(stale, 32, "stale");
    context.Expect(metrics.width == 0.0f && metrics.height == 0.0f &&
                       metrics.lineCount == 0,
                   "MeasureText safely rejects an invalid or stale handle");

    fonts.BeginFrame();
    context.Expect(!fonts.DrawText(stale, "stale", {}) && !fonts.Flush(),
                   "Draw and Flush safely reject an uninitialized lifecycle");
    fonts.UnloadFont(stale);
    fonts.Finalize();

    const object_connect::FontSystemStats statistics = fonts.GetStatistics();
    context.Expect(statistics.utf8DecodeCount == 0 &&
                       statistics.layoutCacheHits == 0 &&
                       statistics.layoutCacheMisses == 0 &&
                       statistics.glyphRasterizations == 0 &&
                       statistics.atlasUploads == 0,
                   "rejected lifecycle calls do not mutate cache statistics");

    if (context.GetFailureCount() != 0) {
        std::cerr << context.GetFailureCount()
                  << " FontSystem lifecycle test(s) failed.\n";
        return 1;
    }
    std::cout << "All FontSystem lifecycle tests passed.\n";
    return 0;
}
