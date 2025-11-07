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

#include "visualizer_common.h"
#include "hid/display/visualizer.h"
#include "processing/engines/audio_engine.h"
#include <algorithm>
#include <cmath>

namespace deluge::hid::display {

// Shared constants (moved from visualizer.cpp anonymous namespace)
namespace {
// Visual compression parameters
constexpr float kCompressionExponent = 0.5f;         // 2:1 visual compression (square root)
constexpr float kFrequencyBoostExponent = 0.075f;    // High-frequency boost
constexpr float kFrequencyNormalizationHz = 1000.0f; // Frequency normalization base

// Display constants
constexpr int32_t kDisplayMargin = 2; // Standard margin for visualizer display areas
} // namespace

// Helper function to get read start position from circular buffer for most recent samples
uint32_t getVisualizerReadStartPos(uint32_t sampleCount) {
	uint32_t writePos = Visualizer::visualizerWritePos.load(std::memory_order_acquire);

	if (sampleCount >= Visualizer::kVisualizerBufferSize) {
		// Buffer is full, start reading from the most recent samples (just before writePos)
		// Go back by sampleCount to get the most recent samples
		return (writePos - sampleCount) % Visualizer::kVisualizerBufferSize;
	}
	else {
		// Buffer not full, start from the most recent samples available
		return (writePos - sampleCount) % Visualizer::kVisualizerBufferSize;
	}
}

// Apply music sweet-spot visual compression to amplitude and frequency
// Formula: (amplitude^kCompressionExponent) * ((frequency/kFrequencyNormalizationHz)^kFrequencyBoostExponent)
// The compression exponent provides 2:1 visual compression (square root scaling)
// The frequency boost term provides subtle high-frequency boost while reducing low-end dominance
float applyVisualizerCompression(float amplitude, float frequency) {
	// Normalize amplitude to 0-1 range if not already
	amplitude = std::clamp(amplitude, 0.0f, 1.0f);
	return std::pow(amplitude, kCompressionExponent)
	       * std::pow(frequency / kFrequencyNormalizationHz, kFrequencyBoostExponent);
}

} // namespace deluge::hid::display
