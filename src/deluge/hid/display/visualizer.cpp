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

namespace deluge::hid::display {

// Static member variables
bool Visualizer::display_visualizer = false;
bool Visualizer::display_visualizer_independent = false;
uint32_t Visualizer::current_visualizer_mode = 0; // 0 = use runtime setting
uint32_t Visualizer::silenceStartTime = 0;
uint32_t Visualizer::visualizerFrameCounterWaveform = 0;
uint32_t Visualizer::visualizerFrameCounterSpectrum = 0;
uint32_t Visualizer::visualizerFrameCounterEqualizer = 0;

// Visualizer sample buffer initialization
alignas(CACHE_LINE_SIZE) std::array<int32_t, Visualizer::kVisualizerBufferSize> Visualizer::visualizerSampleBuffer{};
std::atomic<uint32_t> Visualizer::visualizerWritePos{0};
std::atomic<uint32_t> Visualizer::visualizerSampleCount{0};

/// Render visualizer waveform or spectrum on OLED display
void Visualizer::renderVisualizer() {
	// Check visualizer mode
	uint32_t visualizer_mode = getMode();

	if (visualizer_mode == RuntimeFeatureStateVisualizer::VisualizerSpectrum) {
		// Render spectrum using FFT
		::deluge::hid::display::renderVisualizerSpectrum(getVisualizerCanvas());
		return;
	}
	if (visualizer_mode == RuntimeFeatureStateVisualizer::VisualizerEqualizer) {
		// Render equalizer using FFT
		::deluge::hid::display::renderVisualizerEqualizer(getVisualizerCanvas());
		return;
	}
	// Default to waveform rendering (for VisualizerWaveform)
	::deluge::hid::display::renderVisualizerWaveform(getVisualizerCanvas());
}

/// Render waveform visualization
void Visualizer::renderVisualizerWaveform() {
	::deluge::hid::display::renderVisualizerWaveform(getVisualizerCanvas());
}

/// Render spectrum visualization using FFT
void Visualizer::renderVisualizerSpectrum() {
	::deluge::hid::display::renderVisualizerSpectrum(getVisualizerCanvas());
}

/// Render equalizer visualization with 16 frequency bands
void Visualizer::renderVisualizerEqualizer() {
	::deluge::hid::display::renderVisualizerEqualizer(getVisualizerCanvas());
}

/// Get reference to visualizer canvas for visualizer rendering
oled_canvas::Canvas& Visualizer::getVisualizerCanvas() {
	return OLED::visualizer;
}

/// Check if visualizer should be rendered and render it if conditions are met
/// Renders to visualizer canvas and allows normal UI rendering to continue
bool Visualizer::potentiallyRenderVisualizer(oled_canvas::Canvas& canvas) {
	int32_t mod_knob_mode = 0;
	if (view.activeModControllableModelStack.modControllable != nullptr) {
		mod_knob_mode = *view.activeModControllableModelStack.modControllable->getModKnobMode();
	}

	return potentiallyRenderVisualizer(canvas, view.displayVUMeter, isEnabled(),
	                                   view.activeModControllableModelStack.modControllable, mod_knob_mode);
}

/// Check if visualizer should be rendered and render it if conditions are met
/// Renders to visualizer canvas and allows normal UI rendering to continue
bool Visualizer::potentiallyRenderVisualizer(oled_canvas::Canvas& canvas, View& view) {
	int32_t mod_knob_mode = 0;
	if (view.activeModControllableModelStack.modControllable != nullptr) {
		mod_knob_mode = *view.activeModControllableModelStack.modControllable->getModKnobMode();
	}

	return potentiallyRenderVisualizer(canvas, view.displayVUMeter, isEnabled(),
	                                   view.activeModControllableModelStack.modControllable, mod_knob_mode);
}

/// Check if visualizer should be rendered and render it if conditions are met
/// Renders to visualizer canvas and allows normal UI rendering to continue
bool Visualizer::potentiallyRenderVisualizer(oled_canvas::Canvas& canvas, bool displayVUMeter, bool visualizer_enabled,
                                             ModControllable* modControllable, int32_t mod_knob_mode) {
	// Check if visualizer feature is enabled in Waveform, Spectrum, or Equalizer mode in runtime settings
	if (visualizer_enabled) {
		bool shouldRender = false;

		// Check independent mode conditions (visualizer is only controlled by SHIFT+LEVEL/PAN hotkey)
		bool independentConditionsMet = display_visualizer_independent && modControllable != nullptr;

		// Render only when independent mode is enabled
		if (independentConditionsMet) {
			shouldRender = true;
			if (!display_visualizer) {
				display_visualizer = true;
			}
			// Render to visualizer canvas (displays above main UI)
			renderVisualizer();
			OLED::markChanged();
		}

		// Disable visualizer only if BOTH conditions fail AND independent mode is off
		if (!shouldRender && display_visualizer) {
			// Only disable if independent mode is also off
			if (!display_visualizer_independent) {
				display_visualizer = false;
				getVisualizerCanvas().clear();
				OLED::markChanged();
			}
		}
	}
	else {
		// Visualizer feature not enabled - disable if currently displaying
		if (display_visualizer) {
			display_visualizer = false;
			display_visualizer_independent = false; // Also clear independent state
			getVisualizerCanvas().clear();
			OLED::markChanged();
		}
	}
	// Always return false to allow normal UI rendering to continue
	return false;
}

/// Request OLED refresh for visualizer if active
void Visualizer::requestVisualizerUpdateIfNeeded() {
	int32_t mod_knob_mode = 0;
	if (view.activeModControllableModelStack.modControllable != nullptr) {
		mod_knob_mode = *view.activeModControllableModelStack.modControllable->getModKnobMode();
	}

	requestVisualizerUpdateIfNeeded(view.displayVUMeter, isEnabled(),
	                                view.activeModControllableModelStack.modControllable, mod_knob_mode);
}

/// Request OLED refresh for visualizer if active
void Visualizer::requestVisualizerUpdateIfNeeded(View& view) {
	int32_t mod_knob_mode = 0;
	if (view.activeModControllableModelStack.modControllable != nullptr) {
		mod_knob_mode = *view.activeModControllableModelStack.modControllable->getModKnobMode();
	}

	requestVisualizerUpdateIfNeeded(view.displayVUMeter, isEnabled(),
	                                view.activeModControllableModelStack.modControllable, mod_knob_mode);
}

void Visualizer::requestVisualizerUpdateIfNeeded(bool displayVUMeter, bool visualizer_enabled,
                                                 ModControllable* modControllable, int32_t mod_knob_mode) {
	// Check independent mode conditions (visualizer is only controlled by SHIFT+LEVEL/PAN hotkey)
	bool independentConditionsMet = visualizer_enabled && display_visualizer_independent && modControllable != nullptr;

	// Check if visualizer should be active (only when independent mode is enabled)
	if (independentConditionsMet) {
		// Enable visualizer if conditions are met
		if (!display_visualizer) {
			display_visualizer = true;
		}

		// Get current visualizer mode to determine appropriate framerate
		uint32_t visualizer_mode = getMode();
		uint32_t frameSkip = 2;                                   // Default 30fps for spectrum/equalizer
		uint32_t* frameCounter = &visualizerFrameCounterSpectrum; // Default

		// Set framerate based on visualizer type (all at 30fps for consistency)
		if (visualizer_mode == RuntimeFeatureStateVisualizer::VisualizerWaveform) {
			frameSkip = 2; // 30fps for waveform
			frameCounter = &visualizerFrameCounterWaveform;
		}
		else if (visualizer_mode == RuntimeFeatureStateVisualizer::VisualizerSpectrum) {
			frameSkip = 2; // 30fps for spectrum
			frameCounter = &visualizerFrameCounterSpectrum;
		}
		else if (visualizer_mode == RuntimeFeatureStateVisualizer::VisualizerEqualizer) {
			frameSkip = 2; // 30fps for equalizer
			frameCounter = &visualizerFrameCounterEqualizer;
		}

		// Request OLED update at appropriate frame rate
		(*frameCounter)++;
		if (*frameCounter >= frameSkip) {
			*frameCounter = 0;
			renderUIsForOled();
		}
		return;
	}
	// Disable visualizer if conditions aren't met (only if independent mode is also off)
	if (display_visualizer && !display_visualizer_independent) {
		display_visualizer = false;
	}
}

void Visualizer::reset() {
	display_visualizer = false;
	display_visualizer_independent = false;
	current_visualizer_mode = 0; // Reset to use runtime setting
	silenceStartTime = 0;
	visualizerFrameCounterWaveform = 0;
	visualizerFrameCounterSpectrum = 0;
	visualizerFrameCounterEqualizer = 0;
	// Clear visualizer canvas when resetting visualizer
	getVisualizerCanvas().clear();
	OLED::markChanged();
}

void Visualizer::setEnabled(bool enabled) {
	display_visualizer = enabled;
	if (!enabled) {
		// Only clear visualizer canvas if independent mode is also off
		// This prevents VU meter toggle from disabling independent visualizer
		if (!display_visualizer_independent) {
			getVisualizerCanvas().clear();
			OLED::markChanged();
		}
	}
}

bool Visualizer::isDisplaying() {
	return display_visualizer;
}

bool Visualizer::isEnabled() {
	uint32_t visualizer_mode = runtimeFeatureSettings.get(RuntimeFeatureSettingType::Visualizer);
	return (visualizer_mode == RuntimeFeatureStateVisualizer::VisualizerWaveform)
	       || (visualizer_mode == RuntimeFeatureStateVisualizer::VisualizerSpectrum)
	       || (visualizer_mode == RuntimeFeatureStateVisualizer::VisualizerEqualizer);
}

/// Check if visualizer is actively running
bool Visualizer::isActive() {
	int32_t mod_knob_mode = 0;
	if (view.activeModControllableModelStack.modControllable != nullptr) {
		mod_knob_mode = *view.activeModControllableModelStack.modControllable->getModKnobMode();
	}

	return isActive(view.displayVUMeter, view.activeModControllableModelStack.modControllable, mod_knob_mode);
}

/// Check if visualizer is actively running
bool Visualizer::isActive(View& view) {
	int32_t mod_knob_mode = 0;
	if (view.activeModControllableModelStack.modControllable != nullptr) {
		mod_knob_mode = *view.activeModControllableModelStack.modControllable->getModKnobMode();
	}

	return isActive(view.displayVUMeter, view.activeModControllableModelStack.modControllable, mod_knob_mode);
}

bool Visualizer::isActive(bool displayVUMeter, ModControllable* modControllable, int32_t mod_knob_mode) {
	// Check independent mode conditions (visualizer is only controlled by SHIFT+LEVEL/PAN hotkey)
	bool independentActive = isEnabled() && display_visualizer_independent && modControllable != nullptr;

	// Active only when independent mode is enabled
	return independentActive;
}

uint32_t Visualizer::getMode() {
	// If a current mode override is set, return it
	if (current_visualizer_mode != 0) {
		return current_visualizer_mode;
	}
	// Otherwise return the runtime setting
	return runtimeFeatureSettings.get(RuntimeFeatureSettingType::Visualizer);
}

void Visualizer::setCurrentMode(uint32_t mode) {
	current_visualizer_mode = mode;
}

void Visualizer::resetCurrentMode() {
	current_visualizer_mode = 0;
}

/// Toggle independent visualizer state (independent of VU meter)
void Visualizer::toggleIndependent() {
	// Only allow toggling if visualizer feature is enabled
	if (!isEnabled()) {
		return;
	}

	display_visualizer_independent = !display_visualizer_independent;
	if (display_visualizer_independent) {
		// Enable visualizer when toggling on
		display_visualizer = true;
		OLED::markChanged();
	}
	else {
		// Disable visualizer and clear visualizer canvas when toggling off
		display_visualizer = false;
		getVisualizerCanvas().clear();
		OLED::markChanged();
	}
}

/// Get whether independent visualizer mode is enabled
bool Visualizer::isIndependentEnabled() {
	return display_visualizer_independent;
}

void Visualizer::sampleAudioForDisplay(deluge::dsp::StereoBuffer<q31_t> renderingBuffer, size_t numSamples) {
	// Sample audio for visualizer visualization (downsample for efficiency)
	// Only sample if visualizer feature is enabled in Waveform, Spectrum, or Equalizer mode to save CPU cycles
	if (isEnabled()) {
		// Take every Nth sample to reduce CPU load - sample rate is 44.1kHz, we only need ~48-64 samples for display
		// Sample every 2nd sample to get ~22k samples/sec for better responsiveness (slight CPU increase)
		constexpr uint32_t visualizer_sample_interval = 2;
		// Q31 to Q15 conversion: shift right by 16 bits (31-15 = 16)
		constexpr uint32_t q31_to_q15_shift = 16;
		static uint32_t sample_counter = 0;
		sample_counter++;
		if (sample_counter >= visualizer_sample_interval) {
			sample_counter = 0;
			// Take the most recent sample from the end of the buffer for minimum latency
			size_t mid_sample = numSamples - 1;
			if (mid_sample < renderingBuffer.size()) {
				// Combine stereo channels: (L + R) / 2, then convert from Q31 to normalized int
				int32_t sample_l = renderingBuffer[mid_sample].l >> q31_to_q15_shift; // Convert Q31 to Q15 range
				int32_t sample_r = renderingBuffer[mid_sample].r >> q31_to_q15_shift;
				int32_t combined = (sample_l + sample_r) >> 1; // Average of L and R

				// Write to circular buffer (thread-safe for single writer, multiple readers)
				uint32_t write_pos = visualizerWritePos.load(std::memory_order_acquire);
				visualizerSampleBuffer[write_pos] = combined;
				visualizerWritePos.store((write_pos + 1) % kVisualizerBufferSize, std::memory_order_release);
				if (visualizerSampleCount.load(std::memory_order_acquire) < kVisualizerBufferSize) {
					visualizerSampleCount.fetch_add(1, std::memory_order_release);
				}
			}
		}
	}
}

} // namespace deluge::hid::display
