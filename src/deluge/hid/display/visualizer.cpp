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

#include "hid/display/visualizer.h"
#include "deluge/model/settings/runtime_feature_settings.h"
#include "extern.h"
#include "gui/l10n/l10n.h"
#include "gui/ui/ui.h"
#include "hid/display/visualizer/visualizer_equalizer.h"
#include "hid/display/visualizer/visualizer_spectrum.h"
#include "hid/display/visualizer/visualizer_waveform.h"
#include "modulation/params/param.h"

// Forward declaration for global UI rendering function
extern void renderUIsForOled();

namespace deluge::hid::display {

// Static member variables
bool Visualizer::displayVisualizer = false;
uint32_t Visualizer::visualizerFrameCounter = 0;

/// Render visualizer waveform or spectrum on OLED display
void Visualizer::renderVisualizer(oled_canvas::Canvas& canvas) {
	// Check visualizer mode
	uint32_t visualizerMode = runtimeFeatureSettings.get(RuntimeFeatureSettingType::Visualizer);

	if (visualizerMode == RuntimeFeatureStateVisualizer::VisualizerSpectrum) {
		// Render spectrum using FFT
		::deluge::hid::display::renderVisualizerSpectrum(canvas);
		return;
	}
	else if (visualizerMode == RuntimeFeatureStateVisualizer::VisualizerEqualizer) {
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
bool Visualizer::potentiallyRenderVisualizer(oled_canvas::Canvas& canvas, bool displayVUMeter, bool visualizerEnabled,
                                             ModControllable* modControllable, int32_t modKnobMode) {
	// Check if visualizer feature is enabled in Waveform, Spectrum, or Equalizer mode in runtime settings
	if (visualizerEnabled) {
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
	if (displayVisualizer && (!visualizerEnabled || !displayVUMeter || !modControllable || modKnobMode != 0)) {
		displayVisualizer = false;
	}
	return false;
}

void Visualizer::requestVisualizerUpdateIfNeeded(bool displayVUMeter, bool visualizerEnabled,
                                                 ModControllable* modControllable, int32_t modKnobMode) {
	// Check if visualizer should be active
	if (visualizerEnabled && displayVUMeter && modControllable && modKnobMode == 0) {
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

bool Visualizer::isEnabled() {
	return displayVisualizer;
}

} // namespace deluge::hid::display
