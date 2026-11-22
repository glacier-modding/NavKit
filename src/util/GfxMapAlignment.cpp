#include "../../include/NavKit/util/GfxMapAlignment.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <limits>
#include <map>
#include <numbers>

#include <glm/geometric.hpp>

namespace GfxMapAlignment {
    namespace {
        // The official maps' wall and solid object colour (#767f86).
        constexpr int wallRed = 0x76;
        constexpr int wallGreen = 0x7f;
        constexpr int wallBlue = 0x86;
        constexpr int wallTolerance = 10;
        constexpr float coarseCell = 2.0f;
        constexpr int coarseCap = 5;
        constexpr float fineCapMetres = 4.0f;
        constexpr size_t minimumWallCells = 20;

        using Complex = std::complex<float>;

        struct Grid {
            int width = 0;
            int height = 0;
            std::vector<unsigned char> cells;

            Grid(const int width, const int height) :
                width(width), height(height), cells(static_cast<size_t>(width) * height, 0) {}

            unsigned char& at(const int x, const int y) {
                return cells[static_cast<size_t>(y) * width + x];
            }
            [[nodiscard]] unsigned char at(const int x, const int y) const {
                return cells[static_cast<size_t>(y) * width + x];
            }
            [[nodiscard]] size_t count() const {
                return static_cast<size_t>(std::ranges::count(cells, 1));
            }
        };

        // Grows a mask by one cell in every direction (a 3x3 square).
        void dilateOnce(Grid& grid) {
            Grid rows(grid.width, grid.height);
            for (int y = 0; y < grid.height; ++y) {
                for (int x = 0; x < grid.width; ++x) {
                    rows.at(x, y) =
                        grid.at(x, y) | (x > 0 ? grid.at(x - 1, y) : 0) | (x + 1 < grid.width ? grid.at(x + 1, y) : 0);
                }
            }
            for (int y = 0; y < grid.height; ++y) {
                for (int x = 0; x < grid.width; ++x) {
                    grid.at(x, y) =
                        rows.at(x, y) | (y > 0 ? rows.at(x, y - 1) : 0) | (y + 1 < grid.height ? rows.at(x, y + 1) : 0);
                }
            }
        }

        // Chessboard distance to the mask, truncated at `cap`.
        std::vector<float> truncatedDistance(const Grid& mask, const int cap) {
            std::vector<float> distance(mask.cells.size(), static_cast<float>(cap));
            Grid frontier = mask;
            for (int step = 0; step < cap; ++step) {
                for (size_t index = 0; index < distance.size(); ++index) {
                    if (frontier.cells[index] && distance[index] == static_cast<float>(cap)) {
                        distance[index] = static_cast<float>(step);
                    }
                }
                dilateOnce(frontier);
            }
            return distance;
        }

        void fft(std::vector<Complex>& data, const size_t offset, const size_t stride, const size_t count,
            const bool inverse) {
            for (size_t i = 1, j = 0; i < count; ++i) {
                size_t bit = count >> 1;
                for (; j & bit; bit >>= 1) {
                    j ^= bit;
                }
                j ^= bit;
                if (i < j) {
                    std::swap(data[offset + i * stride], data[offset + j * stride]);
                }
            }
            for (size_t length = 2; length <= count; length <<= 1) {
                const float angle =
                    2.0f * std::numbers::pi_v<float> / static_cast<float>(length) * (inverse ? 1.0f : -1.0f);
                const Complex step(std::cos(angle), std::sin(angle));
                for (size_t start = 0; start < count; start += length) {
                    Complex twiddle(1.0f, 0.0f);
                    for (size_t k = 0; k < length / 2; ++k) {
                        Complex& even = data[offset + (start + k) * stride];
                        Complex& odd = data[offset + (start + k + length / 2) * stride];
                        const Complex product = odd * twiddle;
                        odd = even - product;
                        even += product;
                        twiddle *= step;
                    }
                }
            }
        }

        void fft2d(std::vector<Complex>& data, const size_t width, const size_t height, const bool inverse) {
            for (size_t row = 0; row < height; ++row) {
                fft(data, row * width, 1, width, inverse);
            }
            for (size_t column = 0; column < width; ++column) {
                fft(data, column, width, height, inverse);
            }
            if (inverse) {
                const float scale = 1.0f / static_cast<float>(width * height);
                for (Complex& value : data) {
                    value *= scale;
                }
            }
        }

        // corr(a, b)(t) = sum_p a(p + t) b(p), for all cyclic shifts t.
        std::vector<float> correlate(
            std::vector<Complex> a, std::vector<Complex> b, const size_t width, const size_t height) {
            fft2d(a, width, height, false);
            fft2d(b, width, height, false);
            for (size_t index = 0; index < a.size(); ++index) {
                a[index] *= std::conj(b[index]);
            }
            fft2d(a, width, height, true);
            std::vector<float> result(a.size());
            for (size_t index = 0; index < a.size(); ++index) {
                result[index] = a[index].real();
            }
            return result;
        }

        size_t powerOfTwoAtLeast(const size_t value) {
            size_t result = 1;
            while (result < value) {
                result <<= 1;
            }
            return result;
        }

        template <typename Visit>
        void sampleSegments(const std::vector<Segment>& segments, const float spacing, Visit visit) {
            for (const auto& [start, end] : segments) {
                const float length = glm::length(end - start);
                const int samples = std::max(1, static_cast<int>(std::ceil(length / spacing)));
                for (int sample = 0; sample <= samples; ++sample) {
                    visit(start + (end - start) * (static_cast<float>(sample) / static_cast<float>(samples)));
                }
            }
        }
    } // namespace

    std::vector<float> floorLevels(
        const std::vector<glm::vec3>& triangles, const float minimumZ, const float maximumZ, const float separation) {
        constexpr float bin = 0.25f;
        std::map<int, float> areaByBin;
        for (size_t index = 0; index + 2 < triangles.size(); index += 3) {
            const glm::vec3 cross =
                glm::cross(triangles[index + 1] - triangles[index], triangles[index + 2] - triangles[index]);
            const float doubleArea = glm::length(cross);
            if (doubleArea < 1e-6f || std::abs(cross.z) < 0.85f * doubleArea) {
                continue;
            }
            const float z = (triangles[index].z + triangles[index + 1].z + triangles[index + 2].z) / 3.0f;
            if (z < minimumZ || z > maximumZ) {
                continue;
            }
            areaByBin[static_cast<int>(std::lround(z / bin))] += doubleArea * 0.5f;
        }
        std::vector<std::pair<float, float>> peaks(areaByBin.size());
        std::ranges::transform(areaByBin, peaks.begin(),
            [](const auto& entry) { return std::pair{entry.second, static_cast<float>(entry.first) * bin}; });
        std::ranges::sort(peaks, std::greater{});
        std::vector<float> levels;
        for (const auto& [area, level] : peaks) {
            if (area < peaks.front().first * 0.1f) {
                break;
            }
            if (std::ranges::all_of(levels, [&](const float other) { return std::abs(other - level) > separation; })) {
                levels.push_back(level);
            }
        }
        std::ranges::sort(levels);
        return levels;
    }

    std::vector<Segment> sectionSegments(const std::vector<glm::vec3>& triangles, const float cut) {
        std::vector<Segment> segments;
        for (size_t index = 0; index + 2 < triangles.size(); index += 3) {
            std::array<glm::vec2, 2> points{};
            int found = 0;
            for (int edge = 0; edge < 3 && found < 2; ++edge) {
                const glm::vec3& a = triangles[index + edge];
                const glm::vec3& b = triangles[index + (edge + 1) % 3];
                if ((a.z < cut) != (b.z < cut)) {
                    const float t = (cut - a.z) / (b.z - a.z);
                    const glm::vec3 point = a + (b - a) * t;
                    points[found++] = {point.x, -point.y};
                }
            }
            if (found == 2) {
                segments.push_back(points);
            }
        }
        return segments;
    }

    std::vector<unsigned char> wallMask(const LayerImage& image) {
        std::vector<unsigned char> mask(static_cast<size_t>(image.width) * image.height, 0);
        for (size_t pixel = 0; pixel < mask.size(); ++pixel) {
            const unsigned char* rgba = &image.rgba[pixel * 4];
            mask[pixel] = rgba[3] > 128 && std::abs(rgba[0] - wallRed) <= wallTolerance &&
                std::abs(rgba[1] - wallGreen) <= wallTolerance && std::abs(rgba[2] - wallBlue) <= wallTolerance;
        }
        return mask;
    }

    std::optional<Placement> placeLayer(const LayerImage& image, const std::vector<Segment>& segments) {
        if (image.width <= 0 || image.height <= 0 || segments.empty() || image.pixelsPerMetre <= 0.0f) {
            return std::nullopt;
        }
        const std::vector<unsigned char> walls = wallMask(image);

        // The image's walls on the coarse grid.
        const float pixelsPerCell = coarseCell * image.pixelsPerMetre;
        Grid map(static_cast<int>(std::ceil(image.width / pixelsPerCell)),
            static_cast<int>(std::ceil(image.height / pixelsPerCell)));
        for (int y = 0; y < image.height; ++y) {
            for (int x = 0; x < image.width; ++x) {
                if (walls[static_cast<size_t>(y) * image.width + x]) {
                    map.at(static_cast<int>(x / pixelsPerCell), static_cast<int>(y / pixelsPerCell)) = 1;
                }
            }
        }

        // The geometry's walls on the coarse grid.
        glm::vec2 minimum(std::numeric_limits<float>::max());
        glm::vec2 maximum(std::numeric_limits<float>::lowest());
        for (const auto& [start, end] : segments) {
            minimum = glm::min(minimum, glm::min(start, end));
            maximum = glm::max(maximum, glm::max(start, end));
        }
        Grid geometry(static_cast<int>(std::ceil((maximum.x - minimum.x) / coarseCell)) + 1,
            static_cast<int>(std::ceil((maximum.y - minimum.y) / coarseCell)) + 1);
        sampleSegments(segments, coarseCell / 2.0f, [&](const glm::vec2 point) {
            geometry.at(static_cast<int>((point.x - minimum.x) / coarseCell),
                static_cast<int>((point.y - minimum.y) / coarseCell)) = 1;
        });
        const size_t mapCount = map.count();
        const size_t geometryCount = geometry.count();
        if (mapCount < minimumWallCells || geometryCount < minimumWallCells) {
            return std::nullopt;
        }

        // Score every shift t (geometry cell p lands on map cell p + t): the mean distance from each geometry wall
        // to the nearest map wall, plus the same from the map's walls to the geometry's.
        const size_t width = powerOfTwoAtLeast(static_cast<size_t>(map.width + geometry.width));
        const size_t height = powerOfTwoAtLeast(static_cast<size_t>(map.height + geometry.height));
        const auto embed = [&](const Grid& grid, const std::vector<float>* values, const float fill) {
            std::vector<Complex> data(width * height, Complex(fill, 0.0f));
            for (int y = 0; y < grid.height; ++y) {
                for (int x = 0; x < grid.width; ++x) {
                    const size_t source = static_cast<size_t>(y) * grid.width + x;
                    data[static_cast<size_t>(y) * width + x] =
                        Complex(values ? (*values)[source] : static_cast<float>(grid.cells[source]), 0.0f);
                }
            }
            return data;
        };
        const std::vector<float> mapDistance = truncatedDistance(map, coarseCap);
        const std::vector<float> geometryDistance = truncatedDistance(geometry, coarseCap);
        const std::vector<float> forward =
            correlate(embed(map, &mapDistance, coarseCap), embed(geometry, nullptr, 0.0f), width, height);
        const std::vector<float> backward =
            correlate(embed(geometry, &geometryDistance, coarseCap), embed(map, nullptr, 0.0f), width, height);
        float bestScore = std::numeric_limits<float>::max();
        int bestX = 0;
        int bestY = 0;
        std::vector<float> scores;
        for (int ty = -geometry.height + 1; ty < map.height; ++ty) {
            for (int tx = -geometry.width + 1; tx < map.width; ++tx) {
                const size_t forwardIndex = static_cast<size_t>((ty + static_cast<int>(height)) % height) * width +
                    static_cast<size_t>((tx + static_cast<int>(width)) % width);
                const size_t backwardIndex = static_cast<size_t>((-ty + static_cast<int>(height)) % height) * width +
                    static_cast<size_t>((-tx + static_cast<int>(width)) % width);
                const float score = forward[forwardIndex] / static_cast<float>(geometryCount) +
                    backward[backwardIndex] / static_cast<float>(mapCount);
                scores.push_back(score);
                if (score < bestScore) {
                    bestScore = score;
                    bestX = tx;
                    bestY = ty;
                }
            }
        }
        std::ranges::nth_element(scores, scores.begin() + static_cast<std::ptrdiff_t>(scores.size() / 2));
        const float median = scores[scores.size() / 2];
        glm::vec2 topLeft = minimum - glm::vec2(static_cast<float>(bestX), static_cast<float>(bestY)) * coarseCell;

        // Refine at the image's resolution: the mean distance from the geometry's walls to the image's walls.
        Grid fine(image.width, image.height);
        fine.cells = walls;
        const int fineCap = std::max(2, static_cast<int>(std::ceil(fineCapMetres * image.pixelsPerMetre)));
        const std::vector<float> fineDistance = truncatedDistance(fine, fineCap);
        std::vector<glm::vec2> samples;
        sampleSegments(segments, 0.5f / image.pixelsPerMetre, [&](const glm::vec2 point) { samples.push_back(point); });
        const auto fineScore = [&](const glm::vec2 corner) {
            double total = 0.0;
            for (const glm::vec2 point : samples) {
                const int x = static_cast<int>(std::floor((point.x - corner.x) * image.pixelsPerMetre));
                const int y = static_cast<int>(std::floor((point.y - corner.y) * image.pixelsPerMetre));
                total += x >= 0 && y >= 0 && x < image.width && y < image.height
                    ? fineDistance[static_cast<size_t>(y) * image.width + x]
                    : static_cast<float>(fineCap);
            }
            return static_cast<float>(total / static_cast<double>(samples.size()));
        };
        const int reach = static_cast<int>(std::ceil(coarseCell * image.pixelsPerMetre));
        glm::vec2 bestCorner = topLeft;
        float bestFine = fineScore(topLeft);
        for (int dy = -reach; dy <= reach; ++dy) {
            for (int dx = -reach; dx <= reach; ++dx) {
                const glm::vec2 corner =
                    topLeft + glm::vec2(static_cast<float>(dx), static_cast<float>(dy)) / image.pixelsPerMetre;
                const float score = fineScore(corner);
                if (score < bestFine) {
                    bestFine = score;
                    bestCorner = corner;
                }
            }
        }
        return Placement{bestCorner, bestFine / image.pixelsPerMetre, bestScore > 0.0f ? median / bestScore : 1.0f};
    }
} // namespace GfxMapAlignment
