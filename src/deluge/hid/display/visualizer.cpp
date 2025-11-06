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

#include "hid/display/visualizer.h"
#include "deluge/model/settings/runtime_feature_settings.h"
#include "extern.h"
#include "gui/l10n/l10n.h"
#include "gui/ui/ui.h"
#include "gui/views/view.h"
#include "hid/display/visualizer/visualizer_equalizer.h"
#include "hid/display/visualizer/visualizer_spectrum.h"
#include "hid/display/visualizer/visualizer_waveform.h"
#include "modulation/params/param.h"
#include <atomic>

// Forward declaration for global UI rendering function
extern void renderUIsForOled();

namespace deluge::hid::display {

// Static member variables
bool Visualizer::displayVisualizer = false;
uint32_t Visualizer::visualizerFrameCounter = 0;

// Visualizer sample buffer initialization
alignas(CACHE_LINE_SIZE) int32_t Visualizer::visualizerSampleBuffer[kVisualizerBufferSize]{};
std::atomic<uint32_t> Visualizer::visualizerWritePos{0};
std::atomic<uint32_t> Visualizer::visualizerSampleCount{0};

/// Render visualizer waveform or spectrum on OLED display
void Visualizer::renderVisualizer(oled_canvas::Canvas& canvas) {
	// Check visualizer mode
	uint32_t visualizer_mode = getMode();

	if (visualizer_mode == RuntimeFeatureStateVisualizer::VisualizerSpectrum) {
		// Render spectrum using FFT
		::deluge::hid::display::renderVisualizerSpectrum(canvas);
		return;
	}
	else if (visualizer_mode == RuntimeFeatureStateVisualizer::VisualizerEqualizer) {
		// Render equalizer using FFT
		::deluge::hid::display::renderVisualizerEqualizer(canvas);
		return;
	}
	else {
		// Default to waveform rendering (for VisualizerWaveform)
		::deluge::hid::display::renderVisualizerWaveform(canvas);
		return;
	}
}

/// Render waveform visualization
/// @param canvas The OLED canvas to render to
void Visualizer::renderVisualizerWaveform(oled_canvas::Canvas& canvas) {
	::deluge::hid::display::renderVisualizerWaveform(canvas);
}

/// Render spectrum visualization using FFT
/// @param canvas The OLED canvas to render to
void Visualizer::renderVisualizerSpectrum(oled_canvas::Canvas& canvas) {
	::deluge::hid::display::renderVisualizerSpectrum(canvas);
}

/// Render equalizer visualization with 16 frequency bands
/// @param canvas The OLED canvas to render to
void Visualizer::renderVisualizerEqualizer(oled_canvas::Canvas& canvas) {
	::deluge::hid::display::renderVisualizerEqualizer(canvas);
}

/// Check if visualizer should be rendered and render it if conditions are met
bool Visualizer::potentiallyRenderVisualizer(oled_canvas::Canvas& canvas) {
	int32_t modKnobMode = 0;
	if (view.activeModControllableModelStack.modControllable) {
		modKnobMode = *view.activeModControllableModelStack.modControllable->getModKnobMode();
	}

	return potentiallyRenderVisualizer(canvas, view.displayVUMeter, isEnabled(),
	                                   view.activeModControllableModelStack.modControllable, modKnobMode);
}

/// Check if visualizer should be rendered and render it if conditions are met
bool Visualizer::potentiallyRenderVisualizer(oled_canvas::Canvas& canvas, View& view) {
	int32_t modKnobMode = 0;
	if (view.activeModControllableModelStack.modControllable) {
		modKnobMode = *view.activeModControllableModelStack.modControllable->getModKnobMode();
	}

	return potentiallyRenderVisualizer(canvas, view.displayVUMeter, isEnabled(),
	                                   view.activeModControllableModelStack.modControllable, modKnobMode);
}

/// Check if visualizer should be rendered and render it if conditions are met
bool Visualizer::potentiallyRenderVisualizer(oled_canvas::Canvas& canvas, bool displayVUMeter, bool visualizer_enabled,
                                             ModControllable* modControllable, int32_t modKnobMode) {
	// Check if visualizer feature is enabled in Waveform, Spectrum, or Equalizer mode in runtime settings
	if (visualizer_enabled) {
		// Re-enable visualizer if VU meter is enabled and feature is in an active mode (handles case where
		// displayVisualizer was reset in focusRegained() but VU meter is still active)
		if (displayVUMeter && modControllable && modKnobMode == 0) {
			if (!displayVisualizer) {
				displayVisualizer = true;
			}
			renderVisualizer(canvas);
			return true;
		}
	}
	// If visualizer should be displayed but conditions aren't met, disable it
	if (displayVisualizer && (!visualizer_enabled || !displayVUMeter || !modControllable || modKnobMode != 0)) {
		displayVisualizer = false;
	}
	return false;
}

/// Request OLED refresh for visualizer if active
void Visualizer::requestVisualizerUpdateIfNeeded() {
	int32_t modKnobMode = 0;
	if (view.activeModControllableModelStack.modControllable) {
		modKnobMode = *view.activeModControllableModelStack.modControllable->getModKnobMode();
	}

	requestVisualizerUpdateIfNeeded(view.displayVUMeter, isEnabled(),
	                                view.activeModControllableModelStack.modControllable, modKnobMode);
}

/// Request OLED refresh for visualizer if active
void Visualizer::requestVisualizerUpdateIfNeeded(View& view) {
	int32_t modKnobMode = 0;
	if (view.activeModControllableModelStack.modControllable) {
		modKnobMode = *view.activeModControllableModelStack.modControllable->getModKnobMode();
	}

	requestVisualizerUpdateIfNeeded(view.displayVUMeter, isEnabled(),
	                                view.activeModControllableModelStack.modControllable, modKnobMode);
}

void Visualizer::requestVisualizerUpdateIfNeeded(bool displayVUMeter, bool visualizer_enabled,
                                                 ModControllable* modControllable, int32_t modKnobMode) {
	// Check if visualizer should be active
	if (visualizer_enabled && displayVUMeter && modControllable && modKnobMode == 0) {
		// Enable visualizer if conditions are met
		if (!displayVisualizer) {
			displayVisualizer = true;
		}
		// Request OLED update for visualizer at reduced frame rate to prevent excessive CPU usage
		// Update every 2 frames (~30fps instead of ~60fps) for better performance
		visualizerFrameCounter++;
		if (visualizerFrameCounter >= 2) {
			visualizerFrameCounter = 0;
			renderUIsForOled();
		}
		return;
	}
	// Disable visualizer if conditions aren't met
	if (displayVisualizer) {
		displayVisualizer = false;
	}
}

void Visualizer::reset() {
	displayVisualizer = false;
	visualizerFrameCounter = 0;
}

void Visualizer::setEnabled(bool enabled) {
	displayVisualizer = enabled;
}

bool Visualizer::isDisplaying() {
	return displayVisualizer;
}

bool Visualizer::isEnabled() {
	uint32_t visualizer_mode = runtimeFeatureSettings.get(RuntimeFeatureSettingType::Visualizer);
	return (visualizer_mode == RuntimeFeatureStateVisualizer::VisualizerWaveform)
	       || (visualizer_mode == RuntimeFeatureStateVisualizer::VisualizerSpectrum)
	       || (visualizer_mode == RuntimeFeatureStateVisualizer::VisualizerEqualizer);
}

/// Check if visualizer is actively running
bool Visualizer::isActive() {
	int32_t modKnobMode = 0;
	if (view.activeModControllableModelStack.modControllable) {
		modKnobMode = *view.activeModControllableModelStack.modControllable->getModKnobMode();
	}

	return isActive(view.displayVUMeter, view.activeModControllableModelStack.modControllable, modKnobMode);
}

/// Check if visualizer is actively running
bool Visualizer::isActive(View& view) {
	int32_t modKnobMode = 0;
	if (view.activeModControllableModelStack.modControllable) {
		modKnobMode = *view.activeModControllableModelStack.modControllable->getModKnobMode();
	}

	return isActive(view.displayVUMeter, view.activeModControllableModelStack.modControllable, modKnobMode);
}

bool Visualizer::isActive(bool displayVUMeter, ModControllable* modControllable, int32_t modKnobMode) {
	return isEnabled() && displayVUMeter && modControllable && modKnobMode == 0;
}

uint32_t Visualizer::getMode() {
	return runtimeFeatureSettings.get(RuntimeFeatureSettingType::Visualizer);
}

void Visualizer::sampleAudioForDisplay(deluge::dsp::StereoBuffer<q31_t> renderingBuffer, size_t numSamples) {
	// Sample audio for visualizer visualization (downsample for efficiency)
	// Only sample if visualizer feature is enabled in Waveform, Spectrum, or Equalizer mode to save CPU cycles
	if (isEnabled()) {
		// Take every Nth sample to reduce CPU load - sample rate is 44.1kHz, we only need ~48-64 samples for display
		// Sample every 4th sample to get ~11k samples/sec to capture quick transients (percussive hits)
		constexpr uint32_t kVisualizerSampleInterval = 4;
		// Q31 to Q15 conversion: shift right by 16 bits (31-15 = 16)
		constexpr uint32_t kQ31ToQ15Shift = 16;
		static uint32_t sampleCounter = 0;
		sampleCounter++;
		if (sampleCounter >= kVisualizerSampleInterval) {
			sampleCounter = 0;
			// Take a sample from the middle of the buffer for better representation
			size_t midSample = numSamples / 2;
			if (midSample < renderingBuffer.size()) {
				// Combine stereo channels: (L + R) / 2, then convert from Q31 to normalized int
				int32_t sampleL = renderingBuffer[midSample].l >> kQ31ToQ15Shift; // Convert Q31 to Q15 range
				int32_t sampleR = renderingBuffer[midSample].r >> kQ31ToQ15Shift;
				int32_t combined = (sampleL + sampleR) >> 1; // Average of L and R

				// Write to circular buffer (thread-safe for single writer, multiple readers)
				uint32_t writePos = visualizerWritePos.load(std::memory_order_acquire);
				visualizerSampleBuffer[writePos] = combined;
				visualizerWritePos.store((writePos + 1) % kVisualizerBufferSize, std::memory_order_release);
				if (visualizerSampleCount.load(std::memory_order_acquire) < kVisualizerBufferSize) {
					visualizerSampleCount.fetch_add(1, std::memory_order_release);
				}
			}
		}
	}
}

} // namespace deluge::hid::display
