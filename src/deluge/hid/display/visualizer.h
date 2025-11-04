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

#pragma once

#include "definitions_cxx.hpp"
#include "dsp/fft/fft_config_manager.h"
#include "model/settings/runtime_feature_settings.h"
#include "oled.h"
#include "oled_canvas/canvas.h"
#include "processing/engines/audio_engine.h"

// Forward declarations
class ModControllable;

// FFT computation result structure
struct FFTResult {
	ne10_fft_cpx_int32_t* output;
	bool isValid;
	bool isSilent;
};

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
	/// @param displayVUMeter Whether VU meter is enabled
	/// @param visualizerEnabled Whether visualizer feature is enabled
	/// @param modControllable Current mod controllable
	/// @param modKnobMode Current mod knob mode
	/// @return true if visualizer was rendered
	static bool potentiallyRenderVisualizer(oled_canvas::Canvas& canvas, bool displayVUMeter, bool visualizerEnabled,
	                                        ModControllable* modControllable, int32_t modKnobMode);

	/// Request OLED refresh for visualizer if active
	/// @param displayVUMeter Whether VU meter is enabled
	/// @param visualizerEnabled Whether visualizer feature is enabled
	/// @param modControllable Current mod controllable
	/// @param modKnobMode Current mod knob mode
	static void requestVisualizerUpdateIfNeeded(bool displayVUMeter, bool visualizerEnabled,
	                                            ModControllable* modControllable, int32_t modKnobMode);

	/// Reset visualizer state (called when switching views)
	static void reset();

	/// Set whether visualizer display is enabled
	/// @param enabled Whether visualizer should be enabled
	static void setEnabled(bool enabled);

	/// Get whether visualizer display is enabled
	/// @return true if visualizer display is enabled
	static bool isEnabled();

private:
	/// Initialize Hanning window coefficients
	static void initSpectrumHanningWindow();

	/// Check if FFT output indicates silence
	/// @param fftOutput FFT output buffer
	/// @param threshold Silence detection threshold
	/// @return true if silent
	static bool isFFTSilent(const ne10_fft_cpx_int32_t* fftOutput, int32_t threshold);

	/// Get read start position from circular buffer
	/// @param sampleCount Current sample count
	/// @return Read start position
	static uint32_t getVisualizerReadStartPos(uint32_t sampleCount);

	/// Apply music sweet-spot visual compression
	/// @param amplitude Input amplitude (0-1 range)
	/// @param frequency Frequency in Hz
	/// @return Compressed amplitude
	static float applyVisualizerCompression(float amplitude, float frequency);

	/// Compute FFT for visualizer with caching optimization
	/// @return FFT result structure with output and validity flags
	static FFTResult computeVisualizerFFT();

	/// Calculate frequency band range for equalizer bar
	/// @param bar Bar index (0-15)
	/// @param lowerFreq Output lower frequency
	/// @param upperFreq Output upper frequency
	static void calculateFrequencyBandRange(int32_t bar, float& lowerFreq, float& upperFreq);

	/// Calculate weighted average magnitude for a frequency band
	/// @param fftResult FFT computation result
	/// @param lowerFreq Lower frequency bound
	/// @param upperFreq Upper frequency bound
	/// @param freqResolution Frequency resolution per FFT bin
	/// @return Weighted average magnitude
	static float calculateWeightedMagnitude(const FFTResult& fftResult, float lowerFreq, float upperFreq,
	                                        float freqResolution);

	/// Update peak tracking and draw peak indicator for equalizer bar
	/// @param canvas The OLED canvas to render to
	/// @param bar Bar index
	/// @param normalizedHeight Current normalized height
	/// @param barLeftX Left X coordinate of bar
	/// @param barRightX Right X coordinate of bar
	/// @param kGraphMinY Minimum Y coordinate of graph
	/// @param kGraphMaxY Maximum Y coordinate of graph
	/// @param kGraphHeight Graph height
	/// @param visualizerMode Current visualizer mode
	static void updateAndDrawPeak(oled_canvas::Canvas& canvas, int32_t bar, float normalizedHeight, int32_t barLeftX,
	                              int32_t barRightX, int32_t kGraphMinY, int32_t kGraphMaxY, int32_t kGraphHeight,
	                              uint32_t visualizerMode);

	/// Whether visualizer display is enabled
	static bool displayVisualizer;

	/// Frame counter for update timing
	static uint32_t visualizerFrameCounter;
};

} // namespace deluge::hid::display
