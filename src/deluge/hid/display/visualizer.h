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

#include "definitions_cxx.hpp"
#include "dsp/fft/fft_config_manager.h"
#include "dsp_ng/core/types.hpp"
#include "gui/views/view.h"
#include "hid/display/visualizer/visualizer_common.h"
#include "model/settings/runtime_feature_settings.h"
#include "oled.h"
#include "oled_canvas/canvas.h"
#include "processing/engines/audio_engine.h"
#include <array>
#include <atomic>

// Forward declarations
class ModControllable;

namespace deluge::hid::display {

/// Visualizer rendering utilities for OLED display
/// Renders waveform, spectrum, and equalizer visualizations
class Visualizer {
public:
	/// Main entry point for rendering visualizer
	/// Renders to the visualizer canvas (above main UI)
	static void renderVisualizer();

	/// Render waveform visualization
	/// Renders to the visualizer canvas
	static void renderVisualizerWaveform();

	/// Render spectrum visualization using FFT
	/// Renders to the visualizer canvas
	static void renderVisualizerSpectrum();

	/// Render equalizer visualization with 16 frequency bands
	/// Renders to the visualizer canvas
	static void renderVisualizerEqualizer();

	/// Get reference to visualizer canvas for visualizer rendering
	/// @return Reference to the visualizer canvas
	static oled_canvas::Canvas& getVisualizerCanvas();

	/// Check if visualizer should be rendered and render it if conditions are met
	/// Renders to visualizer canvas and allows normal UI rendering to continue
	/// @return false (always allows UI rendering to continue)
	static bool potentiallyRenderVisualizer(oled_canvas::Canvas& canvas);

	/// Check if visualizer should be rendered and render it if conditions are met
	/// Renders to visualizer canvas and allows normal UI rendering to continue
	/// @param canvas The OLED canvas (unused, kept for API compatibility)
	/// @param view The current view containing VU meter and mod controllable state
	/// @return false (always allows UI rendering to continue)
	static bool potentiallyRenderVisualizer(oled_canvas::Canvas& canvas, View& view);

	/// Check if visualizer should be rendered and render it if conditions are met
	/// Renders to visualizer canvas and allows normal UI rendering to continue
	/// @param canvas The OLED canvas (unused, kept for API compatibility)
	/// @param displayVUMeter Whether VU meter is enabled
	/// @param visualizerEnabled Whether visualizer feature is enabled
	/// @param modControllable Current mod controllable
	/// @param mod_knob_mode Current mod knob mode
	/// @return false (always allows UI rendering to continue)
	static bool potentiallyRenderVisualizer(oled_canvas::Canvas& canvas, bool displayVUMeter, bool visualizer_enabled,
	                                        ModControllable* modControllable, int32_t mod_knob_mode);

	/// Request OLED refresh for visualizer if active
	static void requestVisualizerUpdateIfNeeded();

	/// Request OLED refresh for visualizer if active
	/// @param view The current view containing VU meter and mod controllable state
	static void requestVisualizerUpdateIfNeeded(View& view);

	/// Request OLED refresh for visualizer if active
	/// @param displayVUMeter Whether VU meter is enabled
	/// @param visualizerEnabled Whether visualizer feature is enabled
	/// @param modControllable Current mod controllable
	/// @param mod_knob_mode Current mod knob mode
	static void requestVisualizerUpdateIfNeeded(bool displayVUMeter, bool visualizer_enabled,
	                                            ModControllable* modControllable, int32_t mod_knob_mode);

	/// Reset visualizer state (called when switching views)
	static void reset();

	/// Set whether visualizer display is enabled
	/// @param enabled Whether visualizer should be enabled
	static void setEnabled(bool enabled);

	/// Get whether visualizer is currently displaying
	/// @return true if visualizer display is active
	static bool isDisplaying();

	/// Get whether visualizer feature is enabled in runtime settings
	/// @return true if visualizer is set to Waveform, Spectrum, or Equalizer mode
	static bool isEnabled();

	/// Get whether visualizer is active (feature enabled AND display conditions met)
	/// @return true if visualizer is actively running
	static bool isActive();

	/// Get whether visualizer is active (feature enabled AND display conditions met)
	/// @param view The current view containing VU meter and mod controllable state
	/// @return true if visualizer is actively running
	static bool isActive(View& view);

	/// Get whether visualizer is active (feature enabled AND display conditions met)
	/// @param displayVUMeter Whether VU meter is enabled
	/// @param modControllable Current mod controllable
	/// @param mod_knob_mode Current mod knob mode
	/// @return true if visualizer is actively running
	static bool isActive(bool displayVUMeter, ModControllable* modControllable, int32_t mod_knob_mode);

	/// Get current visualizer mode from runtime settings or temporary override
	/// @return Current visualizer mode
	static uint32_t getMode();

	/// Set current visualizer mode temporarily (overrides runtime setting)
	/// @param mode The visualizer mode to set (VisualizerWaveform, VisualizerSpectrum, VisualizerEqualizer)
	static void setCurrentMode(uint32_t mode);

	/// Reset current visualizer mode to use runtime setting
	static void resetCurrentMode();

	/// Toggle independent visualizer state (independent of VU meter)
	/// Can be enabled/disabled via SHIFT+LEVEL/PAN mod button
	static void toggleIndependent();

	/// Get whether independent visualizer mode is enabled
	/// @return true if independent visualizer is enabled
	static bool isIndependentEnabled();

	/// Sample audio data for visualizer display (waveform, spectrum, equalizer)
	/// Performs downsampling and stores samples in the circular buffer for display
	/// @param renderingBuffer The audio buffer to sample from
	/// @param numSamples Number of samples in the buffer
	static void sampleAudioForDisplay(deluge::dsp::StereoBuffer<q31_t> renderingBuffer, size_t numSamples);

	/// Whether visualizer display is enabled
	static bool display_visualizer;

	/// Whether visualizer is enabled independently of VU meter
	static bool display_visualizer_independent;

	/// Current visualizer mode override (0 = use runtime setting, otherwise use this mode)
	static uint32_t current_visualizer_mode;

	/// Timestamp when silence was first detected (for delayed hiding)
	static uint32_t silenceStartTime;

	/// Frame counters for update timing (per visualizer type for different framerates)
	static uint32_t visualizerFrameCounterWaveform;
	static uint32_t visualizerFrameCounterSpectrum;
	static uint32_t visualizerFrameCounterEqualizer;

	/// Visualizer sample buffer and related variables
	static constexpr size_t kVisualizerBufferSize = 512;
	static std::array<int32_t, kVisualizerBufferSize> visualizerSampleBuffer;
	static std::atomic<uint32_t> visualizerWritePos;
	static std::atomic<uint32_t> visualizerSampleCount;
};

} // namespace deluge::hid::display
