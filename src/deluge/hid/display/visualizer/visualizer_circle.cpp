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

#include "visualizer_circle.h"
#include "hid/display/oled.h"
#include "hid/display/oled_canvas/canvas.h"
#include "hid/display/visualizer.h"
#include "model/settings/runtime_feature_settings.h"
#include "util/functions.h"
#include "visualizer_common.h"
#include "visualizer_fft.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace deluge::hid::display {

// Circle-specific constants (matching mini_circle_visualizer for better response)
namespace {
constexpr int32_t kCircleFFTSize = 512;   // Use shared FFT size (can't change this)
constexpr int32_t kLowCutoff = 250;       // 20-250 Hz
constexpr int32_t kMidCutoff = 2000;      // 250-2000 Hz
constexpr int32_t kHighCutoff = 20000;    // 2000-20000 Hz
constexpr float kLowRadius = 0.8f;        // Outer circle radius (reversed - low freq is largest)
constexpr float kMidRadius = 0.5f;        // Middle circle radius (unchanged)
constexpr float kHighRadius = 0.2f;       // Inner circle radius (reversed - high freq is smallest)
constexpr float kThickness = 0.40f;       // Circle thickness (increased for even stronger response)
constexpr int32_t kNumCirclePoints = 100; // Number of line segments (matching mini_circle)

// Visualizer calibration constants
constexpr int32_t kFFTReferenceMagnitude = 60000000; // Q31 format for FFT output
} // namespace

/// Render circle visualization with frequency bands as concentric circles
void renderVisualizerCircle(oled_canvas::Canvas& canvas) {
	// Cache visualizer mode to avoid redundant runtime feature settings queries
	uint32_t visualizer_mode = deluge::hid::display::Visualizer::getMode();

	constexpr int32_t k_display_width = OLED_MAIN_WIDTH_PIXELS;
	constexpr int32_t k_display_height = OLED_MAIN_HEIGHT_PIXELS;

	// Compute FFT using shared helper function (with caching optimization)
	FFTResult fft_result = computeVisualizerFFT();
	if (!fft_result.isValid) {
		// Not enough samples or FFT config not available, draw empty
		return;
	}

	// Fixed reference magnitude for fixed-amplitude display aligned with VU meter
	// When VU meter shows clipping, circle bands should be at peak
	// FFT output is Q31 complex format, so magnitude is sqrt(r^2 + i^2) in Q31 range.
	constexpr int32_t k_fixed_reference_magnitude = kFFTReferenceMagnitude;

	// Check for silence by examining a few representative bins
	// If all bins are very small, don't update display to avoid flicker from brief gaps
	// Previous circle frame remains visible
	if (fft_result.isSilent) {
		return;
	}

	// Clear the entire display area before drawing
	canvas.clearAreaExact(0, 0, k_display_width - 1, k_display_height - 1);

	// Calculate center position for circles (center of display)
	int32_t centerX = k_display_width / 2;
	int32_t centerY = k_display_height / 2;
	// Calculate max radius to fill the display (elongated to fit rectangular shape)
	int32_t maxRadiusX = centerX - 1; // Leave 1 pixel margin
	int32_t maxRadiusY = centerY - 1; // Leave 1 pixel margin

	// Calculate frequency band data
	std::vector<float> lowBand, midBand, highBand;
	calculateCircleBandData(fft_result, lowBand, midBand, highBand);

	// Render each band as a concentric circle (from smallest/inner to largest/outer)
	renderCircularBand(canvas, highBand, kHighRadius, kThickness, centerX, centerY, maxRadiusX, maxRadiusY,
	                   k_display_width, k_display_height);
	renderCircularBand(canvas, midBand, kMidRadius, kThickness, centerX, centerY, maxRadiusX, maxRadiusY,
	                   k_display_width, k_display_height);
	renderCircularBand(canvas, lowBand, kLowRadius, kThickness, centerX, centerY, maxRadiusX, maxRadiusY,
	                   k_display_width, k_display_height);

	// Mark OLED as changed so it gets sent to display
	OLED::markChanged();
}

/// Calculate frequency band data for circle rendering (returns individual bin magnitudes for waveform effect)
void calculateCircleBandData(FFTResult& fft_result, std::vector<float>& lowBand, std::vector<float>& midBand,
                             std::vector<float>& highBand) {
	constexpr int32_t k_fixed_reference_magnitude = kFFTReferenceMagnitude;

	// Calculate bin ranges for frequency bands
	int32_t lowStartBin = 0;
	int32_t lowEndBin = (kLowCutoff * kCircleFFTSize) / ::kSampleRate;
	int32_t midStartBin = lowEndBin;
	int32_t midEndBin = (kMidCutoff * kCircleFFTSize) / ::kSampleRate;
	int32_t highStartBin = midEndBin;
	int32_t highEndBin = (kHighCutoff * kCircleFFTSize) / ::kSampleRate;

	// Clamp to valid range
	lowEndBin = std::min(lowEndBin, kCircleFFTSize / 2);
	midEndBin = std::min(midEndBin, kCircleFFTSize / 2);
	highEndBin = std::min(highEndBin, kCircleFFTSize / 2);

	// Process low frequency band - collect individual bin magnitudes for waveform effect
	for (int32_t i = lowStartBin; i < lowEndBin; i++) {
		int32_t magnitude = fastPythag(fft_result.output[i].r, fft_result.output[i].i);
		float amplitude = static_cast<float>(magnitude) / static_cast<float>(k_fixed_reference_magnitude);
		// Apply frequency-dependent scaling like mini_circle
		float freqScaling = std::pow(static_cast<float>(i) / (lowStartBin + 1), 0.5f);
		amplitude *= freqScaling * 1.5f;              // Low band scaling (increased)
		amplitude = std::min(1.0f, amplitude / 8.0f); // Normalize for much stronger response
		lowBand.push_back(amplitude);
	}

	// Process mid frequency band - collect individual bin magnitudes for waveform effect
	for (int32_t i = midStartBin; i < midEndBin; i++) {
		int32_t magnitude = fastPythag(fft_result.output[i].r, fft_result.output[i].i);
		float amplitude = static_cast<float>(magnitude) / static_cast<float>(k_fixed_reference_magnitude);
		// Apply frequency-dependent scaling like mini_circle
		float freqScaling = std::pow(static_cast<float>(i) / (midStartBin + 1), 0.5f);
		amplitude *= freqScaling * 3.0f;              // Mid band scaling (increased)
		amplitude = std::min(1.0f, amplitude / 8.0f); // Normalize for much stronger response
		midBand.push_back(amplitude);
	}

	// Process high frequency band - collect individual bin magnitudes for waveform effect
	for (int32_t i = highStartBin; i < highEndBin; i++) {
		int32_t magnitude = fastPythag(fft_result.output[i].r, fft_result.output[i].i);
		float amplitude = static_cast<float>(magnitude) / static_cast<float>(k_fixed_reference_magnitude);
		// Apply frequency-dependent scaling like mini_circle
		float freqScaling = std::pow(static_cast<float>(i) / (highStartBin + 1), 0.5f);
		amplitude *= freqScaling * 4.5f;              // High band scaling (increased)
		amplitude = std::min(1.0f, amplitude / 8.0f); // Normalize for much stronger response
		highBand.push_back(amplitude);
	}
}

/// Render circular frequency band on canvas using line segments (waveform effect)
void renderCircularBand(oled_canvas::Canvas& canvas, const std::vector<float>& bandData, float radius, float thickness,
                        int32_t centerX, int32_t centerY, int32_t maxRadiusX, int32_t maxRadiusY, int32_t displayWidth,
                        int32_t displayHeight) {
	if (bandData.empty()) {
		return;
	}

	// Draw ellipse using line segments with waveform modulation (matching mini_circle approach)
	constexpr float twoPi = 2.0f * M_PI;
	constexpr float angleStep = twoPi / static_cast<float>(kNumCirclePoints);

	int32_t lastX = -1;
	int32_t lastY = -1;

	for (int32_t i = 0; i <= kNumCirclePoints; i++) {
		float angle = static_cast<float>(i) * angleStep;

		// Map circle point to band data sample (waveform effect)
		float sampleIndex =
		    (static_cast<float>(i) * static_cast<float>(bandData.size())) / static_cast<float>(kNumCirclePoints);
		int32_t index = static_cast<int32_t>(sampleIndex);

		// Get amplitude from band data at this angular position
		float amplitude = (index < static_cast<int32_t>(bandData.size())) ? bandData[index] * thickness : 0.0f;

		// Calculate modulated radii for waveform effect (matching mini_circle: r = radius + amplitude)
		float circleRadiusX = radius * static_cast<float>(maxRadiusX) + amplitude * static_cast<float>(maxRadiusX);
		float circleRadiusY = radius * static_cast<float>(maxRadiusY) + amplitude * static_cast<float>(maxRadiusY);

		// Calculate ellipse position
		float x = static_cast<float>(centerX) + circleRadiusX * cosf(angle);
		float y = static_cast<float>(centerY) + circleRadiusY * sinf(angle);

		// Convert to integer coordinates
		int32_t ix = static_cast<int32_t>(roundf(x));
		int32_t iy = static_cast<int32_t>(roundf(y));

		// Clamp to display bounds
		ix = std::clamp(ix, static_cast<int32_t>(0), displayWidth - 1);
		iy = std::clamp(iy, static_cast<int32_t>(0), displayHeight - 1);

		// Draw line from previous point to current point
		if (lastX >= 0 && lastY >= 0 && (lastX != ix || lastY != iy)) {
			canvas.drawLine(lastX, lastY, ix, iy);
		}
		else if (i == 0) {
			// First point, just draw a pixel
			canvas.drawPixel(ix, iy);
		}

		lastX = ix;
		lastY = iy;
	}
}

} // namespace deluge::hid::display
