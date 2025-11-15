/*
 * Copyright (c) 2025 Bruce Zawadzki (Tone Coder)
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include "hid/display/oled_canvas/canvas.h"
#include "visualizer_common.h"
#include <vector>

namespace deluge::hid::display {

// Render circle visualization with frequency bands as concentric circles
void renderVisualizerCircle(oled_canvas::Canvas& canvas);

// Calculate frequency band data for circle rendering
void calculateCircleBandData(FFTResult& fft_result, std::vector<float>& lowBand, std::vector<float>& midBand,
                             std::vector<float>& highBand);

// Render circular frequency band on canvas
void renderCircularBand(oled_canvas::Canvas& canvas, const std::vector<float>& bandData, float radius, float thickness,
                        int32_t centerX, int32_t centerY, int32_t maxRadiusX, int32_t maxRadiusY, int32_t displayWidth,
                        int32_t displayHeight);

} // namespace deluge::hid::display
