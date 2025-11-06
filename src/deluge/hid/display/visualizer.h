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
	/// @param canvas The OLED canvas to render to
	static void renderVisualizer(oled_canvas::Canvas& canvas);

	/// Render waveform visualization
	/// @param canvas The OLED canvas to render to
	static void renderVisualizerWaveform(oled_canvas::Canvas& canvas);

	/// Render spectrum visualization using FFT
	/// @param canvas The OLED canvas to render to
	static void renderVisualizerSpectrum(oled_canvas::Canvas& canvas);

	/// Render equalizer visualization with 16 frequency bands
	/// @param canvas The OLED canvas to render to
	static void renderVisualizerEqualizer(oled_canvas::Canvas& canvas);

	/// Check if visualizer should be rendered and render it if conditions are met
	/// @param canvas The OLED canvas to render to
	/// @return true if visualizer was rendered
	static bool potentiallyRenderVisualizer(oled_canvas::Canvas& canvas);

	/// Check if visualizer should be rendered and render it if conditions are met
	/// @param canvas The OLED canvas to render to
	/// @param view The current view containing VU meter and mod controllable state
	/// @return true if visualizer was rendered
	static bool potentiallyRenderVisualizer(oled_canvas::Canvas& canvas, View& view);

	/// Check if visualizer should be rendered and render it if conditions are met
	/// @param canvas The OLED canvas to render to
	/// @param displayVUMeter Whether VU meter is enabled
	/// @param visualizerEnabled Whether visualizer feature is enabled
	/// @param modControllable Current mod controllable
	/// @param mod_knob_mode Current mod knob mode
	/// @return true if visualizer was rendered
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

	/// Get current visualizer mode from runtime settings
	/// @return Current visualizer mode
	static uint32_t getMode();

	/// Sample audio data for visualizer display (waveform, spectrum, equalizer)
	/// Performs downsampling and stores samples in the circular buffer for display
	/// @param renderingBuffer The audio buffer to sample from
	/// @param numSamples Number of samples in the buffer
	static void sampleAudioForDisplay(deluge::dsp::StereoBuffer<q31_t> renderingBuffer, size_t numSamples);

	/// Whether visualizer display is enabled
	static bool displayVisualizer;

	/// Frame counter for update timing
	static uint32_t visualizerFrameCounter;

	/// Visualizer sample buffer and related variables
	static constexpr size_t kVisualizerBufferSize = 256;
	static std::array<int32_t, kVisualizerBufferSize> visualizerSampleBuffer;
	static std::atomic<uint32_t> visualizerWritePos;
	static std::atomic<uint32_t> visualizerSampleCount;
};

} // namespace deluge::hid::display
