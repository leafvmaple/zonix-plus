#pragma once

// Antialiasing shared by the Desktop compositor and the controls. This header
// has no engine dependency.
namespace zonix_gui::shape {

// How much of pixel (x, y) a disc covers, from 0 to 255. The disc has the
// given radius and touches the top and left edges of the pixel grid, so its
// centre is at (radius, radius). Coverage is sampled at 4 x 4 points.
constexpr unsigned char disc_coverage(int radius, int x, int y) {
    constexpr int samples = 4;
    // Positions are in eighths of a pixel so the sample centres are integers.
    constexpr int unit = samples * 2;
    const int reach = radius * unit;
    int inside = 0;
    for (int sample = 0; sample < samples * samples; ++sample) {
        int dx = x * unit + sample % samples * 2 + 1 - reach;
        int dy = y * unit + sample / samples * 2 + 1 - reach;
        if (dx * dx + dy * dy <= reach * reach)
            ++inside;
    }
    return static_cast<unsigned char>(inside * 255 / (samples * samples));
}

}  // namespace zonix_gui::shape
