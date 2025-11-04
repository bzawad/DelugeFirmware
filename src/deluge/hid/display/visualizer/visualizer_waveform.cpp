/*
 * Copyright © 2014-2023 Synthstrom Audible Deluge Firmware.
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

#include "visualizer_waveform.h"
#include "hid/display/oled.h"
#include "hid/display/oled_canvas/canvas.h"
#include "processing/engines/audio_engine.h"
#include <algorithm>

namespace deluge::hid::display {

// Waveform-specific constants
namespace {
// Visualizer calibration constants
constexpr int32_t kWaveformReferenceMagnitude = 125; // Q15 format, ~-78dBFS typical audio
constexpr int32_t kWaveformSilenceThreshold = 10;    // Q15 format

// Visualizer update and display constants
constexpr uint32_t kMaxDisplaySamples = 48;  // Reduced for waveform-style sparse display
constexpr uint32_t kVisualizerFrameSkip = 2; // Update every 2 frames (~30fps instead of ~60fps)

// Format conversion and display constants
constexpr int32_t kSilenceCheckInterval = 16; // Interval for checking silence in loops (reduces CPU usage)
} // namespace

/// Render visualizer waveform on OLED display
void renderVisualizerWaveform(oled_canvas::Canvas& canvas) {
	constexpr int32_t kDisplayWidth = OLED_MAIN_WIDTH_PIXELS;
	constexpr int32_t kDisplayHeight = OLED_MAIN_HEIGHT_PIXELS - OLED_MAIN_TOPMOST_PIXEL;
	constexpr int32_t kCenterY = OLED_MAIN_TOPMOST_PIXEL + (kDisplayHeight / 2);
	constexpr int32_t kMargin = kDisplayMargin;
	constexpr int32_t kGraphMinX = kMargin;
	constexpr int32_t kGraphMaxX = kDisplayWidth - kMargin - 1;
	constexpr int32_t kGraphHeight = kDisplayHeight - (kMargin * 2);

	// Read sample count atomically (single read is safe)
	uint32_t sampleCount = AudioEngine::visualizerSampleCount.load(std::memory_order_acquire);
	if (sampleCount < 2) {
		// Not enough samples yet, draw empty
		return;
	}

	// Determine how many samples to display - use fewer samples for waveform-style sparse display
	uint32_t numSamplesToDisplay = std::min(sampleCount, kMaxDisplaySamples);

	// Calculate step size for downsampling if we have more samples than pixels (integer arithmetic)
	uint32_t stepSize = (sampleCount > kMaxDisplaySamples) ? (sampleCount / kMaxDisplaySamples) : 1;
	uint32_t remainder = (sampleCount > kMaxDisplaySamples) ? (sampleCount % kMaxDisplaySamples) : 0;

	// Start reading from write position and work backwards to get most recent samples
	uint32_t readStartPos = getVisualizerReadStartPos(sampleCount);

	// Fixed reference amplitude for fixed-amplitude display aligned with VU meter
	// When VU meter shows clipping, waveform should reach peak
	// Samples are in Q15 format (range ~-32768 to +32768)
	// Value determined empirically to align waveform peaks with VU meter clipping indication
	constexpr int32_t kFixedReferenceMagnitude = kWaveformReferenceMagnitude;

	// Check for silence by examining a few representative samples
	// If all samples are very small, draw baseline
	constexpr int32_t kSilenceThreshold = kWaveformSilenceThreshold;
	int32_t sampleMagnitude = std::abs(
	    AudioEngine::visualizerSampleBuffer[(readStartPos + sampleCount / 2) % AudioEngine::kVisualizerBufferSize]);
	if (sampleMagnitude < kSilenceThreshold) {
		// Check a few more samples to confirm silence
		bool isSilent = true;
		for (uint32_t i = 0; i < numSamplesToDisplay; i += kSilenceCheckInterval) {
			uint32_t bufferIndex = (readStartPos + i) % AudioEngine::kVisualizerBufferSize;
			int32_t mag = std::abs(AudioEngine::visualizerSampleBuffer[bufferIndex]);
			if (mag >= kSilenceThreshold) {
				isSilent = false;
				break;
			}
		}
		if (isSilent) {
			// Clear the visualizer area
			canvas.clearAreaExact(kGraphMinX, OLED_MAIN_TOPMOST_PIXEL + kMargin, kGraphMaxX,
			                      OLED_MAIN_TOPMOST_PIXEL + kDisplayHeight - kMargin + 1);
			// Draw baseline at center (zero line for waveform)
			canvas.drawHorizontalLine(kCenterY, kGraphMinX, kGraphMaxX);
			OLED::markChanged();
			return;
		}
	}

	// Clear the visualizer area before drawing to prevent ghosting from previous frames
	canvas.clearAreaExact(kGraphMinX, OLED_MAIN_TOPMOST_PIXEL + kMargin, kGraphMaxX,
	                      OLED_MAIN_TOPMOST_PIXEL + kDisplayHeight - kMargin + 1);

	// Draw waveform - spread samples evenly across full width
	// Reset drawing state to prevent connecting to previous frame (fixes vertical line at start)
	uint32_t sampleIndex = 0;
	uint32_t remainderAccumulator = 0;
	int32_t lastX = -1;
	int32_t lastY = -1;
	bool isFirstPoint = true;

	// Ensure numSamplesToDisplay is not zero (defensive programming)
	if (numSamplesToDisplay == 0) {
		return;
	}

	for (uint32_t i = 0; i < numSamplesToDisplay; i++) {
		// Calculate buffer index directly to avoid nested loop
		uint32_t bufferIndex = (readStartPos + sampleIndex) % AudioEngine::kVisualizerBufferSize;
		// Get sample value
		int32_t sample = AudioEngine::visualizerSampleBuffer[bufferIndex];

		// Calculate Y position using fixed amplitude scaling (center at kCenterY)
		// Scale sample from [-kFixedReferenceMagnitude, +kFixedReferenceMagnitude] to display height
		// Positive samples go up from center, negative samples go down
		// Using fixed reference means actual signal levels are displayed, not auto-scaled
		int32_t scaledHeight = 0;
		if (sample != 0) {
			// Scale linearly: height = (sample * (kGraphHeight / 2)) / kFixedReferenceMagnitude
			// Use 64-bit intermediate to prevent overflow
			scaledHeight = static_cast<int32_t>((static_cast<int64_t>(sample) * static_cast<int64_t>(kGraphHeight / 2))
			                                    / kFixedReferenceMagnitude);
		}

		// Convert to pixel Y position (waveform: center at kCenterY, positive samples go up, negative go down)
		// When sample is 0, y should be at center (kCenterY)
		// When sample reaches +kFixedReferenceMagnitude, y should be at top (kGraphMinY)
		// When sample reaches -kFixedReferenceMagnitude, y should be at bottom (kGraphMaxY)
		// Clamp scaledHeight to prevent overflow
		scaledHeight = std::clamp(scaledHeight, -(kGraphHeight / 2), (kGraphHeight / 2));
		int32_t y = kCenterY - scaledHeight;
		// Clamp to valid display range
		y = std::clamp(y, static_cast<int32_t>(OLED_MAIN_TOPMOST_PIXEL + kMargin),
		               static_cast<int32_t>(OLED_MAIN_TOPMOST_PIXEL + kDisplayHeight - kMargin - 1));

		// Calculate X position - spread samples evenly across full width
		int32_t x = kGraphMinX + static_cast<int32_t>((i * (kGraphMaxX - kGraphMinX + 1)) / numSamplesToDisplay);
		// Ensure we don't exceed bounds
		if (x > kGraphMaxX) {
			x = kGraphMaxX;
		}
		if (i == numSamplesToDisplay - 1) {
			x = kGraphMaxX; // Ensure last point reaches the end
		}

		// Draw line from previous point to current point
		// Skip connecting first point to avoid vertical line from previous frame
		if (!isFirstPoint && lastX >= 0 && lastX != x) {
			canvas.drawLine(lastX, lastY, x, y);
		}
		else if (isFirstPoint) {
			// First point of this frame, just draw a pixel (don't connect to previous frame)
			canvas.drawPixel(x, y);
			isFirstPoint = false;
		}

		lastX = x;
		lastY = y;

		// Advance sample index using integer-only math with remainder accumulation
		sampleIndex += stepSize;
		remainderAccumulator += remainder;
		if (remainderAccumulator >= kMaxDisplaySamples) {
			sampleIndex++;
			remainderAccumulator -= kMaxDisplaySamples;
		}
	}

	// Mark OLED as changed so it gets sent to display
	OLED::markChanged();
}

} // namespace deluge::hid::display
