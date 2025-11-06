/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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

#include "visualizer_spectrum.h"
#include "hid/display/oled.h"
#include "hid/display/oled_canvas/canvas.h"
#include "hid/display/visualizer.h"
#include "model/settings/runtime_feature_settings.h"
#include "util/functions.h"
#include "visualizer_common.h"
#include "visualizer_fft.h"
#include <algorithm>
#include <cmath>

namespace deluge::hid::display {

// Spectrum-specific constants and buffers
namespace {
// Smoothing filter coefficients (first-order IIR: smoothed = alpha*old + (1-alpha)*new)
constexpr float kSmoothingAlpha = 0.8f; // Smoothing factor for old value
constexpr float kSmoothingBeta = 0.2f;  // Weight for new value

// Visualizer calibration constants
constexpr int32_t kFFTReferenceMagnitude = 50000000; // Q31 format for FFT output

// Static smoothing arrays for visual compression (optional time-averaging)
constexpr int32_t kMaxSpectrumPixels = 128; // Max display width (OLED is typically 128 pixels wide)
float spectrumSmoothedValues[kMaxSpectrumPixels] = {0.0f};
} // namespace

/// Render visualizer spectrum on OLED display using FFT
void renderVisualizerSpectrum(oled_canvas::Canvas& canvas) {
	// Cache visualizer mode to avoid redundant runtime feature settings queries
	uint32_t visualizer_mode = deluge::hid::display::Visualizer::getMode();

	constexpr int32_t kDisplayWidth = OLED_MAIN_WIDTH_PIXELS;
	constexpr int32_t kDisplayHeight = OLED_MAIN_HEIGHT_PIXELS - OLED_MAIN_TOPMOST_PIXEL;
	constexpr int32_t kMargin = kDisplayMargin;
	constexpr int32_t kGraphMinX = kMargin;
	constexpr int32_t kGraphMaxX = kDisplayWidth - kMargin - 1;
	constexpr int32_t kGraphHeight = kDisplayHeight - (kMargin * 2);
	constexpr int32_t kGraphMinY = OLED_MAIN_TOPMOST_PIXEL + kMargin;
	constexpr int32_t kGraphMaxY = OLED_MAIN_TOPMOST_PIXEL + kDisplayHeight - kMargin - 1;

	// Compute FFT using shared helper function (with caching optimization)
	FFTResult fftResult = computeVisualizerFFT();
	if (!fftResult.isValid) {
		// Not enough samples or FFT config not available, draw empty
		return;
	}

	// Fixed reference magnitude for fixed-amplitude display aligned with VU meter
	// When VU meter shows clipping, spectrum should be at peak
	// FFT output is Q31 complex format, so magnitude is sqrt(r^2 + i^2) in Q31 range.
	constexpr int32_t kFixedReferenceMagnitude = kFFTReferenceMagnitude;

	// Check for silence by examining a few representative bins
	// If all bins are very small, draw baseline
	if (fftResult.isSilent) {
		canvas.clearAreaExact(kGraphMinX, kGraphMinY, kGraphMaxX, kGraphMaxY + 1);
		// Draw baseline at bottom (zero line for spectrum)
		canvas.drawHorizontalLine(kGraphMaxY, kGraphMinX, kGraphMaxX);
		OLED::markChanged();
		return;
	}

	// Clear the visualizer area before drawing
	canvas.clearAreaExact(kGraphMinX, kGraphMinY, kGraphMaxX, kGraphMaxY + 1);

	// Render spectrum line graph (low frequencies on left, high on right)
	int32_t lastX = -1;
	int32_t lastY = -1;
	bool isFirstPoint = true;

	// Map frequency bins to display pixels using modified logarithmic frequency scale
	// Bass frequencies (20-200 Hz) are compressed to take up less horizontal space
	// while keeping the rest of the frequency range proportional and natural
	constexpr int32_t kNumBins = 129;                                         // 129 bins (from FFT)
	constexpr int32_t kNumPixels = kGraphMaxX - kGraphMinX + 1;               // 124 pixels
	constexpr float kMinFrequency = 20.0f;                                    // Start at 20 Hz for bass
	constexpr float kMaxFrequency = static_cast<float>(::kSampleRate) / 2.0f; // Nyquist frequency

	// Precompute logarithmic scale constant: log10(sample_rate / 2 / 20)
	const float logScaleConstant = std::log10(kMaxFrequency / kMinFrequency);

	// Frequency compression exponent: 0.7 compresses bass range (20-200 Hz) by ~half
	// while keeping mids and highs readable and proportional
	constexpr float kFrequencyCompressionExponent = 0.7f;

	for (int32_t pixel = 0; pixel < kNumPixels; pixel++) {
		// Map pixel to frequency using modified logarithmic scale with compression
		// Apply power curve to compress low frequencies (bass range) more than high frequencies
		// Formula: compressedX = normalizedX^exponent, then f = 20 * 10^(compressedX * log10(sample_rate / 2 / 20))
		float normalizedX = static_cast<float>(pixel) / static_cast<float>(kNumPixels - 1);
		float compressedX = std::pow(normalizedX, kFrequencyCompressionExponent);
		float frequency = kMinFrequency * std::pow(10.0f, compressedX * logScaleConstant);

		// Map frequency to FFT bin index
		// Bin i represents frequency: f_i = i * sample_rate / kSpectrumFFTSize
		// So: bin = frequency * kSpectrumFFTSize / sample_rate
		float binFloat = frequency * static_cast<float>(256) / static_cast<float>(::kSampleRate);

		// Use linear interpolation between adjacent bins to avoid stepping artifacts
		// when multiple pixels map to the same bin (especially at low frequencies)
		int32_t binIndexLow = static_cast<int32_t>(std::floor(binFloat));
		int32_t binIndexHigh = binIndexLow + 1;
		float fraction = binFloat - static_cast<float>(binIndexLow);

		// Clamp bin indices to valid range
		binIndexLow = std::max(static_cast<int32_t>(0), std::min(binIndexLow, kNumBins - 1));
		binIndexHigh = std::max(static_cast<int32_t>(0), std::min(binIndexHigh, kNumBins - 1));

		// Get magnitudes for both bins
		int32_t magnitudeLow = fastPythag(fftResult.output[binIndexLow].r, fftResult.output[binIndexLow].i);
		int32_t magnitudeHigh = fastPythag(fftResult.output[binIndexHigh].r, fftResult.output[binIndexHigh].i);

		// Interpolate between the two bins
		// Use 64-bit intermediate to prevent overflow during calculation
		float magnitudeFloat =
		    static_cast<float>(magnitudeLow) * (1.0f - fraction) + static_cast<float>(magnitudeHigh) * fraction;

		// Apply music sweet-spot visual compression
		// Normalize amplitude to 0-1 range
		float amplitude = magnitudeFloat / static_cast<float>(kFixedReferenceMagnitude);
		float display_value = applyVisualizerCompression(amplitude, frequency);

		// Apply smoothing filter for stability (first-order IIR: smoothed = alpha*old + beta*new)
		// Use per-pixel smoothing instead of per-bin to avoid conflicts when multiple pixels map to same bin
		// This prevents stepping artifacts, especially at low frequencies after compression
		// Only use smoothing buffer when in spectrum mode (conditional memory usage)
		if (pixel < kMaxSpectrumPixels && visualizer_mode == RuntimeFeatureStateVisualizer::VisualizerSpectrum) {
			spectrumSmoothedValues[pixel] =
			    spectrumSmoothedValues[pixel] * kSmoothingAlpha + display_value * kSmoothingBeta;
			display_value = spectrumSmoothedValues[pixel];
		}

		// Clamp display_value to valid range and scale to graph height
		display_value = std::clamp(display_value, 0.0f, 1.0f);
		int32_t scaledHeight = static_cast<int32_t>(display_value * static_cast<float>(kGraphHeight));

		// Convert to pixel Y position (spectrum: baseline at bottom, magnitude grows upward)
		// When magnitude is 0, y should be at bottom (kGraphMaxY)
		// When magnitude reaches fixed reference, y should be at top (kGraphMinY)
		// Clamp scaledHeight to graph height to prevent overflow
		scaledHeight = std::min(scaledHeight, kGraphHeight);
		int32_t y = kGraphMaxY - scaledHeight;
		// Clamp to valid display range (kGraphMinY = top, kGraphMaxY = bottom)
		y = std::clamp(y, kGraphMinY, kGraphMaxY);

		// X position
		int32_t x = kGraphMinX + pixel;

		// Draw line from previous point to current point
		if (!isFirstPoint && lastX >= 0 && lastX != x) {
			canvas.drawLine(lastX, lastY, x, y);
		}
		else if (isFirstPoint) {
			// First point, just draw a pixel
			canvas.drawPixel(x, y);
			isFirstPoint = false;
		}

		lastX = x;
		lastY = y;
	}

	// Mark OLED as changed so it gets sent to display
	OLED::markChanged();
}

} // namespace deluge::hid::display
