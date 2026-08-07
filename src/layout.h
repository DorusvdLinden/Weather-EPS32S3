#pragma once

// Ports InkyPiZero's layout.py 1:1 - pure arithmetic, no behavior change.
// v1 fidelity simplification (see README decision #3) drops nothing here;
// the region grid is identical, only what gets drawn *inside* each region
// (widgets/*.cpp) is simplified for now.

namespace layout {

struct Region {
    int x, y, w, h;

    constexpr int centerX() const { return x + w / 2; }
    constexpr int centerY() const { return y + h / 2; }
    constexpr int right() const { return x + w; }
    constexpr int bottom() const { return y + h; }
};

constexpr int kCanvasW = 800;
constexpr int kCanvasH = 480;

constexpr Region kHeader{0, 6, 800, 34};
constexpr Region kToday{0, kHeader.bottom() + 6, 800, 165};
constexpr Region kCurrentTemperature{kToday.x, kToday.y, 320, kToday.h};
constexpr Region kDataPoints{320, kToday.y, 480, kToday.h};
constexpr Region kChartArea{0, kToday.bottom() + 6, 800, 150};
constexpr Region kForecastRow{0, kChartArea.bottom() + 8, 800, 95};

constexpr int kDataPointCols = 2;
constexpr int kDataPointRows = 3;
constexpr double kDataPointIconFraction = 0.38;

constexpr Region dataPointCell(int index) {
    int col = index % kDataPointCols;
    int row = index / kDataPointCols;
    int cellW = kDataPoints.w / kDataPointCols;
    int cellH = kDataPoints.h / kDataPointRows;
    return Region{kDataPoints.x + col * cellW, kDataPoints.y + row * cellH, cellW, cellH};
}

constexpr int kForecastGap = 12;

inline Region forecastCard(int index, int count) {
    int cardW = (kForecastRow.w - kForecastGap * (count - 1)) / count;
    int x = kForecastRow.x + index * (cardW + kForecastGap);
    return Region{x, kForecastRow.y, cardW, kForecastRow.h};
}

}  // namespace layout
