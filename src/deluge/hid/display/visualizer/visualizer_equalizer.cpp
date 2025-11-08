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

#include "visualizer_equalizer.h"
#include "hid/display/oled.h"
#include "hid/display/oled_canvas/canvas.h"
#include "hid/display/visualizer.h"
#include "model/settings/runtime_feature_settings.h"
#include "visualizer_common.h"
#include "visualizer_fft.h"
#include <algorithm>
#include <cmath>

namespace deluge::hid::display {

// Equalizer-specific constants and buffers
namespace {
// Smoothing filter coefficients (first-order IIR: smoothed = alpha*old + (1-alpha)*new)
constexpr float kSmoothingAlpha = 0.8f; // Smoothing factor for old value
constexpr float kSmoothingBeta = 0.2f;  // Weight for new value

// Peak decay parameters
constexpr float kPeakDecayRate = 0.005f; // Decay rate per frame

// Visualizer calibration constants
constexpr int32_t kFFTReferenceMagnitude = 50000000; // Q31 format for FFT output

// Static peak tracking arrays for equalizer visualizer
// Store peak heights in normalized 0-1 range for proper decay scaling
constexpr int32_t kEqualizerNumBars = 16;
float equalizerPeakHeights[kEqualizerNumBars] = {0.0f};
float equalizerPeakDecay[kEqualizerNumBars] = {0.0f};

// Static smoothing arrays for visual compression (optional time-averaging)
float equalizerSmoothedValues[kEqualizerNumBars] = {0.0f};

// 16 frequency band center frequencies (Hz) - standard equalizer bands
// Band 1: 31 Hz (Sub-bass), 2: 50 Hz (Bass thump), 3: 80 Hz (Bass body), 4: 125 Hz (Upper bass),
// 5: 200 Hz (Low mids), 6: 315 Hz (Warmth), 7: 500 Hz (Midrange), 8: 800 Hz (Mid clarity),
// 9: 1.25 kHz (Presence), 10: 2 kHz (Presence), 11: 3.15 kHz (Upper mids), 12: 5 kHz (Clarity),
// 13: 8 kHz (High presence), 14: 12.5 kHz (Brilliance), 15: 16 kHz (Air), 16: 20 kHz (Ultrasonic)
constexpr float kEqualizerFrequencies[kEqualizerNumBars] = {31.0f,   50.0f,    80.0f,    125.0f,  200.0f,  315.0f,
                                                            500.0f,  800.0f,   1250.0f,  2000.0f, 3150.0f, 5000.0f,
                                                            8000.0f, 12500.0f, 16000.0f, 20000.0f};
} // namespace

// Calculate frequency band range for equalizer bar
// Uses logarithmic spacing around center frequency (approximately 1/3 octave bands)
void calculateFrequencyBandRange(int32_t bar, float& lowerFreq, float& upperFreq) {
	float centerFreq = kEqualizerFrequencies[bar];
	if (bar == 0) {
		// First band: from 20 Hz to midpoint between band 1 (31 Hz) and band 2 (50 Hz)
		lowerFreq = 20.0f;
		upperFreq = (kEqualizerFrequencies[bar] + kEqualizerFrequencies[bar + 1]) / 2.0f;
	}
	else if (bar == kEqualizerNumBars - 1) {
		// Last band: from midpoint between previous and center to 20kHz
		lowerFreq = (kEqualizerFrequencies[bar - 1] + centerFreq) / 2.0f;
		upperFreq = 20000.0f;
	}
	else {
		// Middle bands: range between midpoints of adjacent bands
		lowerFreq = (kEqualizerFrequencies[bar - 1] + centerFreq) / 2.0f;
		upperFreq = (centerFreq + kEqualizerFrequencies[bar + 1]) / 2.0f;
	}
}

// Update peak tracking and draw peak indicator for equalizer bar
void updateAndDrawPeak(oled_canvas::Canvas& canvas, int32_t bar, float normalizedHeight, int32_t barLeftX,
                       int32_t barRightX, int32_t kGraphMinY, int32_t kGraphMaxY, int32_t kGraphHeight,
                       uint32_t visualizer_mode) {
	// Only use peak tracking arrays when in equalizer mode (conditional memory usage)
	if (visualizer_mode != RuntimeFeatureStateVisualizer::VisualizerEqualizer) {
		return;
	}

	normalizedHeight = std::min(1.0f, normalizedHeight); // Clamp to 0-1 range

	if (normalizedHeight > equalizerPeakHeights[bar]) {
		// New peak reached - set peak to current height and reset decay
		equalizerPeakHeights[bar] = normalizedHeight;
		equalizerPeakDecay[bar] = 0.0f;
	}
	else {
		// Accumulate decay and apply squared decay formula
		// Peak decays using squared decay: peak = max(current, peak - decay^2)
		// This provides smooth exponential-like decay that slows down as peak approaches current value
		equalizerPeakDecay[bar] += kPeakDecayRate;
		equalizerPeakHeights[bar] =
		    std::max(normalizedHeight, equalizerPeakHeights[bar] - equalizerPeakDecay[bar] * equalizerPeakDecay[bar]);
	}

	// Draw peak indicator as thicker horizontal line (3 pixels thick, matching reference)
	// Convert normalized peak height back to pixels for drawing
	if (equalizerPeakHeights[bar] > 0.0f) {
		int32_t peakHeightPixels = static_cast<int32_t>(equalizerPeakHeights[bar] * static_cast<float>(kGraphHeight));
		int32_t peakY = kGraphMaxY - peakHeightPixels;
		peakY = std::clamp(peakY, kGraphMinY, kGraphMaxY);

		// Draw 2-pixel thick horizontal line for peak indicator
		for (int32_t thickness = 0; thickness <= 1; thickness++) {
			int32_t drawY = peakY + thickness;
			if (drawY >= kGraphMinY && drawY <= kGraphMaxY) {
				canvas.drawHorizontalLine(drawY, barLeftX, barRightX);
			}
		}
	}
}

/// Render visualizer equalizer on OLED display using FFT with 16 frequency bands (16 bars)
///
/// Algorithm:
/// 1. Compute FFT on most recent 256 audio samples (shared with spectrum visualizer)
/// 2. For each of 16 frequency bands, map to FFT bins using weighted interpolation:
///    - Each band covers a frequency range (lowerFreq to upperFreq)
///    - Bins overlapping the range contribute proportionally to their overlap
///    - Weighted average magnitude is calculated across all overlapping bins
/// 3. Apply visual compression: (amplitude^0.45) * ((frequency/1000)^0.075)
///    - Compression exponent provides soft-knee dynamics compression
///    - Frequency term provides subtle high-frequency boost
/// 4. Apply smoothing filter (first-order IIR) to stabilize display
/// 5. Track peak heights with squared decay for visual feedback
void renderVisualizerEqualizer(oled_canvas::Canvas& canvas) {
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

	// Bar layout constants: 16 bars with even margins and clean pixel alignment
	// Bar width: 5 px, Gap: 2 px, Margins: 7 px each side = 124 px total
	// Layout: 7px left margin + [5px bar + 2px gap] × 15 + 5px final bar + 7px right margin
	constexpr int32_t kBarWidth = 5;
	constexpr int32_t kBarGap = 2;
	constexpr int32_t kEqualizerMargin = 7;
	constexpr int32_t kEqualizerContentStartX = kGraphMinX + kEqualizerMargin;
	constexpr int32_t kEqualizerContentEndX = kGraphMaxX - kEqualizerMargin;

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
	// If all bins are very small, clear the visualizer area after delay
	if (fftResult.isSilent) {
		// Clear the visualizer area completely (same as waveform visualizer)
		canvas.clearAreaExact(kGraphMinX, kGraphMinY, kGraphMaxX, kGraphMaxY + 1);
		return;
	}

	// Clear the visualizer area before drawing
	canvas.clearAreaExact(kGraphMinX, kGraphMinY, kGraphMaxX, kGraphMaxY + 1);

	// Calculate frequency resolution per bin
	float freqResolution = static_cast<float>(::kSampleRate) / static_cast<float>(256);

	// Render 16 frequency bars
	for (int32_t bar = 0; bar < kEqualizerNumBars; bar++) {
		float centerFreq = kEqualizerFrequencies[bar];

		// Calculate frequency range for this band
		float lowerFreq, upperFreq;
		calculateFrequencyBandRange(bar, lowerFreq, upperFreq);

		// Calculate weighted average magnitude using FFT bin interpolation
		float avgMagnitudeFloat = calculateWeightedMagnitude(fftResult, lowerFreq, upperFreq, freqResolution);

		// Apply music sweet-spot visual compression
		// Normalize amplitude to 0-1 range
		float amplitude = avgMagnitudeFloat / static_cast<float>(kFixedReferenceMagnitude);
		float display_value = applyVisualizerCompression(amplitude, centerFreq);

		// Apply smoothing filter for stability (first-order IIR: smoothed = alpha*old + beta*new)
		// Only use smoothing buffer when in equalizer mode (conditional memory usage)
		if (visualizer_mode == RuntimeFeatureStateVisualizer::VisualizerEqualizer) {
			equalizerSmoothedValues[bar] =
			    equalizerSmoothedValues[bar] * kSmoothingAlpha + display_value * kSmoothingBeta;
			display_value = equalizerSmoothedValues[bar];
		}
		// If not in equalizer mode, use display_value directly (skip smoothing)
		// This shouldn't happen since we're in renderVisualizerEqualizer, but handle gracefully

		// Clamp display_value to valid range and scale to graph height
		display_value = std::clamp(display_value, 0.0f, 1.0f);
		int32_t scaledHeight = static_cast<int32_t>(display_value * static_cast<float>(kGraphHeight));

		// Clamp scaledHeight to graph height
		scaledHeight = std::min(scaledHeight, kGraphHeight);

		// Calculate bar position (bar width: 5 px, gap: 2 px between bars)
		int32_t barLeftX = kEqualizerContentStartX + (bar * (kBarWidth + kBarGap));
		int32_t barRightX = barLeftX + kBarWidth - 1;
		int32_t barBottomY = kGraphMaxY;
		int32_t barTopY = kGraphMaxY - scaledHeight;

		// Clamp bar coordinates to valid display range
		barLeftX = std::clamp(barLeftX, kEqualizerContentStartX, kEqualizerContentEndX);
		barRightX = std::clamp(barRightX, kEqualizerContentStartX, kEqualizerContentEndX);
		barTopY = std::clamp(barTopY, kGraphMinY, kGraphMaxY);

		// Draw bar with checkered dither pattern using 2 pixel squares
		for (int32_t x = barLeftX; x <= barRightX; x++) {
			for (int32_t y = barTopY; y <= barBottomY; y++) {
				// Checkered pattern: draw pixel if (x + y) is even
				if ((x + y) % 2 == 0) {
					canvas.drawPixel(x, y);
				}
			}
		}

		// Update peak tracking and draw peak indicator
		float normalizedHeight = static_cast<float>(scaledHeight) / static_cast<float>(kGraphHeight);
		updateAndDrawPeak(canvas, bar, normalizedHeight, barLeftX, barRightX, kGraphMinY, kGraphMaxY, kGraphHeight,
		                  visualizer_mode);
	}
}

} // namespace deluge::hid::display
