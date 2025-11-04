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
#include "NE10.h"
#include "definitions_cxx.hpp"
#include "deluge/model/settings/runtime_feature_settings.h"
#include "dsp/fft/fft_config_manager.h"
#include "extern.h"
#include "gui/l10n/l10n.h"
#include "gui/ui/ui.h"
#include "hid/display/oled.h"
#include "modulation/params/param.h"
#include "processing/engines/audio_engine.h"
#include "util/functions.h"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace deluge::hid::display {

// Static member variables
bool Visualizer::displayVisualizer = false;
uint32_t Visualizer::visualizerFrameCounter = 0;

// Static buffers for spectrum visualizer FFT computation
namespace {
constexpr int32_t kSpectrumFFTSize = 256;
constexpr int32_t kSpectrumFFTMagnitude = 8;                            // 2^8 = 256
constexpr int32_t kSpectrumFFTOutputSize = (kSpectrumFFTSize >> 1) + 1; // 129 bins

// Visualizer calibration constants
// Reference magnitudes for amplitude scaling
constexpr int32_t kWaveformReferenceMagnitude = 125; // Q15 format, ~-78dBFS typical audio
constexpr int32_t kFFTReferenceMagnitude = 50000000; // Q31 format for FFT output

// Silence detection thresholds
constexpr int32_t kWaveformSilenceThreshold = 10; // Q15 format
constexpr int32_t kFFTSilenceThreshold = 100;     // FFT magnitude threshold

// Visual compression parameters
constexpr float kCompressionExponent = 0.45f;        // Soft knee compression
constexpr float kFrequencyBoostExponent = 0.075f;    // High-frequency boost
constexpr float kFrequencyNormalizationHz = 1000.0f; // Frequency normalization base

// Smoothing filter coefficients (first-order IIR: smoothed = alpha*old + (1-alpha)*new)
constexpr float kSmoothingAlpha = 0.8f; // Smoothing factor for old value
constexpr float kSmoothingBeta = 0.2f;  // Weight for new value

// Peak decay parameters
constexpr float kPeakDecayRate = 0.005f; // Decay rate per frame

// Format conversion and display constants
constexpr int32_t kQ31ToQ15Shift = 16;        // Convert from Q31 to Q15 format (31-15 = 16 bits)
constexpr int32_t kSilenceCheckInterval = 16; // Interval for checking silence in loops (reduces CPU usage)
constexpr int32_t kDisplayMargin = 2;         // Standard margin for visualizer display areas

// Visualizer update and display constants
constexpr uint32_t kMaxDisplaySamples = 48;  // Reduced for waveform-style sparse display
constexpr uint32_t kVisualizerFrameSkip = 2; // Update every 2 frames (~30fps instead of ~60fps)

// Static FFT buffers
// Memory usage: ~5.5KB total
// - visualizerSampleBuffer[256] = 1KB (used by all modes, in audio_engine.cpp)
// - spectrumFFTInput[256] = 1KB (shared by spectrum/equalizer modes)
// - spectrumFFTOutput[129] = ~1KB (shared by spectrum/equalizer modes)
// - spectrumHanningWindow[256] = 1KB (shared by spectrum/equalizer modes)
// - spectrumSmoothedValues[128] = 512B (only used by spectrum mode)
// - equalizerSmoothedValues[16] = 64B (only used by equalizer mode)
// - equalizerPeakHeights[16] + equalizerPeakDecay[16] = ~128B (only used by equalizer mode)
//
// Memory allocation trade-off: Static allocation ensures no runtime allocation failures and
// predictable memory usage. Dynamic allocation would save memory when visualizer is disabled but
// adds complexity, potential fragmentation issues, and risk of allocation failures in embedded systems.
// Given the moderate memory footprint (~5.5KB) and the visualizer's optional nature, static allocation
// is the preferred approach for reliability.
int32_t spectrumFFTInput[kSpectrumFFTSize];
ne10_fft_cpx_int32_t spectrumFFTOutput[kSpectrumFFTOutputSize];

// FFT result caching to avoid recomputation on every frame
struct CachedFFTResult {
	uint32_t lastWritePos; // Last buffer write position when FFT was computed
	ne10_fft_cpx_int32_t cachedOutput[kSpectrumFFTOutputSize];
	bool isValid;
};
static CachedFFTResult cachedFFT = {0, {}, false};

// Precomputed Hanning window in Q31 format
int32_t spectrumHanningWindow[kSpectrumFFTSize];

// Static peak tracking arrays for equalizer visualizer
// Store peak heights in normalized 0-1 range for proper decay scaling
constexpr int32_t kEqualizerNumBars = 16;
float equalizerPeakHeights[kEqualizerNumBars] = {0.0f};
float equalizerPeakDecay[kEqualizerNumBars] = {0.0f};

// Static smoothing arrays for visual compression (optional time-averaging)
// Use per-pixel smoothing for spectrum to avoid conflicts when multiple pixels map to same bin
constexpr int32_t kMaxSpectrumPixels = 128; // Max display width (OLED is typically 128 pixels wide)
float spectrumSmoothedValues[kMaxSpectrumPixels] = {0.0f};
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
void Visualizer::calculateFrequencyBandRange(int32_t bar, float& lowerFreq, float& upperFreq) {
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

// Initialize Hanning window coefficients (called once)
void Visualizer::initSpectrumHanningWindow() {
	static bool initialized = false;
	if (initialized) {
		return;
	}
	initialized = true;

	for (int32_t i = 0; i < kSpectrumFFTSize; i++) {
		// Hanning window: w(n) = 0.5 * (1 - cos(2πn/(N-1)))
		// Convert to Q31 format (multiply by 2^31)
		float windowValue = 0.5f * (1.0f - std::cos(2.0f * std::numbers::pi_v<float> * i / (kSpectrumFFTSize - 1)));
		spectrumHanningWindow[i] = static_cast<int32_t>(windowValue * ONE_Q31f);
	}
}

// Check if FFT output indicates silence by examining representative bins
bool Visualizer::isFFTSilent(const ne10_fft_cpx_int32_t* fftOutput, int32_t threshold) {
	int32_t sampleMagnitude =
	    fastPythag(fftOutput[kSpectrumFFTOutputSize / 2].r, fftOutput[kSpectrumFFTOutputSize / 2].i);
	if (sampleMagnitude < threshold) {
		// Check a few more bins to confirm silence
		for (int32_t i = 0; i < kSpectrumFFTOutputSize; i += kSilenceCheckInterval) {
			int32_t mag = fastPythag(fftOutput[i].r, fftOutput[i].i);
			if (mag >= threshold) {
				return false;
			}
		}
		return true;
	}
	return false;
}

// Helper function to get read start position from circular buffer
uint32_t Visualizer::getVisualizerReadStartPos(uint32_t sampleCount) {
	using namespace AudioEngine;
	if (sampleCount >= kVisualizerBufferSize) {
		// Buffer is full, oldest sample is at writePos (next to be overwritten)
		return visualizerWritePos.load(std::memory_order_acquire);
	}
	else {
		// Buffer not full, start from beginning
		return 0;
	}
}

// Apply music sweet-spot visual compression to amplitude and frequency
// Formula: (amplitude^kCompressionExponent) * ((frequency/kFrequencyNormalizationHz)^kFrequencyBoostExponent)
// The compression exponent compresses dynamics (soft knee effect)
// The frequency boost term provides subtle high-frequency boost while reducing low-end dominance
float Visualizer::applyVisualizerCompression(float amplitude, float frequency) {
	// Normalize amplitude to 0-1 range if not already
	amplitude = std::clamp(amplitude, 0.0f, 1.0f);
	return std::pow(amplitude, kCompressionExponent)
	       * std::pow(frequency / kFrequencyNormalizationHz, kFrequencyBoostExponent);
}

// Compute FFT for visualizer with caching optimization
// Returns FFT output and validity flags
FFTResult Visualizer::computeVisualizerFFT() {
	using namespace AudioEngine;
	FFTResult result = {nullptr, false, false};

	// Read sample count atomically (single read is safe)
	uint32_t sampleCount = visualizerSampleCount.load(std::memory_order_acquire);
	if (sampleCount < kSpectrumFFTSize) {
		// Not enough samples yet
		return result;
	}

	// Get FFT config (lazy initialization)
	ne10_fft_r2c_cfg_int32_t fftConfig = FFTConfigManager::getConfig(kSpectrumFFTMagnitude);
	if (fftConfig == nullptr) {
		// FFT config not available - this can happen during initialization or if memory allocation fails
		// The visualizer will gracefully degrade by returning an invalid result, which causes
		// the render functions to draw empty/blank displays rather than crashing
		return result;
	}

	// Check if we can use cached FFT result
	uint32_t currentWritePos = visualizerWritePos.load(std::memory_order_acquire);
	if (cachedFFT.isValid) {
		// Calculate buffer position difference (handle wrap-around)
		uint32_t posDiff = (currentWritePos >= cachedFFT.lastWritePos)
		                       ? (currentWritePos - cachedFFT.lastWritePos)
		                       : (kVisualizerBufferSize - cachedFFT.lastWritePos + currentWritePos);

		// Only recompute if buffer has advanced significantly (>= 1/4 FFT size = kSpectrumFFTSize / 4)
		// Cache threshold trade-off: Lower values (e.g., 1/8 FFT size) provide more frequent updates but higher CPU
		// usage. Higher values (e.g., 1/2 FFT size) reduce CPU usage but may cause visual lag. Current value (1/4 FFT
		// size) balances real-time responsiveness with performance, updating approximately every 1.4ms at 44.1kHz
		// sample rate.
		constexpr uint32_t kFFTCacheThreshold = kSpectrumFFTSize / 4;
		if (posDiff < kFFTCacheThreshold) {
			// Use cached result
			result.output = cachedFFT.cachedOutput;
			result.isValid = true;
			result.isSilent = isFFTSilent(cachedFFT.cachedOutput, kFFTSilenceThreshold);
			return result;
		}
	}

	// Calculate read start position from circular buffer
	uint32_t readStartPos = getVisualizerReadStartPos(sampleCount);

	// Copy samples and apply Hanning window
	// Samples are in Q15 format, window is in Q31 format
	// Result: (Q15 * Q31) >> kQ31ToQ15Shift = Q15
	initSpectrumHanningWindow();
	for (int32_t i = 0; i < kSpectrumFFTSize; i++) {
		uint32_t bufferIndex = (readStartPos + i) % kVisualizerBufferSize;
		// Buffer bounds check: visualizerSampleBuffer is guaranteed to be at least kVisualizerBufferSize
		// and bufferIndex is modulo kVisualizerBufferSize, so it's always valid
		int32_t sample = visualizerSampleBuffer[bufferIndex]; // Q15

		// Apply Hanning window: multiply Q15 sample by Q31 window, shift right by kQ31ToQ15Shift
		// Use 64-bit intermediate to prevent overflow
		int64_t windowedSample =
		    (static_cast<int64_t>(sample) * static_cast<int64_t>(spectrumHanningWindow[i])) >> kQ31ToQ15Shift;
		spectrumFFTInput[i] = static_cast<int32_t>(windowedSample);
	}

	// Perform FFT (real-to-complex)
	ne10_fft_r2c_1d_int32_neon(spectrumFFTOutput, spectrumFFTInput, fftConfig, false);

	// Update cache
	cachedFFT.lastWritePos = currentWritePos;
	for (int32_t i = 0; i < kSpectrumFFTOutputSize; i++) {
		cachedFFT.cachedOutput[i] = spectrumFFTOutput[i];
	}
	cachedFFT.isValid = true;

	result.output = spectrumFFTOutput;
	result.isValid = true;
	result.isSilent = isFFTSilent(spectrumFFTOutput, kFFTSilenceThreshold);
	return result;
}

// Calculate weighted average magnitude for a frequency band using FFT bin interpolation
// Uses weighted interpolation to smooth transitions between bins, preventing stepping artifacts
// when frequency bands don't align exactly with FFT bins
float Visualizer::calculateWeightedMagnitude(const FFTResult& fftResult, float lowerFreq, float upperFreq,
                                             float freqResolution) {
	// Convert frequencies to FFT bin indices (using floating point for interpolation)
	float startBinFloat = lowerFreq / freqResolution;
	float endBinFloat = upperFreq / freqResolution;

	// Clamp to valid bin range
	startBinFloat = std::max(0.0f, std::min(startBinFloat, static_cast<float>(kSpectrumFFTOutputSize - 1)));
	endBinFloat = std::max(0.0f, std::min(endBinFloat, static_cast<float>(kSpectrumFFTOutputSize - 1)));
	if (endBinFloat <= startBinFloat) {
		endBinFloat = startBinFloat + 1.0f; // Ensure at least one bin worth of range
	}
	endBinFloat = std::min(endBinFloat, static_cast<float>(kSpectrumFFTOutputSize - 1));

	float weightedSum = 0.0f;
	float totalWeight = 0.0f;

	int32_t startBin = static_cast<int32_t>(std::floor(startBinFloat));
	int32_t endBin = static_cast<int32_t>(std::floor(endBinFloat));

	// Clamp integer bin indices for safety
	startBin = std::max(static_cast<int32_t>(0), std::min(startBin, kSpectrumFFTOutputSize - 1));
	endBin = std::max(static_cast<int32_t>(0), std::min(endBin, kSpectrumFFTOutputSize - 1));

	// Handle partial overlap with first bin
	if (startBin >= 0 && startBin < kSpectrumFFTOutputSize) {
		float binEndFreq = (startBin + 1) * freqResolution;
		float overlapStart = std::max(lowerFreq, static_cast<float>(startBin) * freqResolution);
		float overlapEnd = std::min(upperFreq, binEndFreq);
		if (overlapEnd > overlapStart) {
			float weight = (overlapEnd - overlapStart) / freqResolution;
			int32_t magnitude = fastPythag(fftResult.output[startBin].r, fftResult.output[startBin].i);
			weightedSum += static_cast<float>(magnitude) * weight;
			totalWeight += weight;
		}
	}

	// Handle full bins in the middle (if any)
	for (int32_t bin = startBin + 1; bin < endBin; bin++) {
		if (bin >= 0 && bin < kSpectrumFFTOutputSize) {
			int32_t magnitude = fastPythag(fftResult.output[bin].r, fftResult.output[bin].i);
			weightedSum += static_cast<float>(magnitude);
			totalWeight += 1.0f;
		}
	}

	// Handle partial overlap with last bin
	if (endBin >= 0 && endBin < kSpectrumFFTOutputSize && endBin > startBin) {
		float binStartFreq = endBin * freqResolution;
		float overlapStart = std::max(lowerFreq, binStartFreq);
		float overlapEnd = std::min(upperFreq, (endBin + 1) * freqResolution);
		if (overlapEnd > overlapStart) {
			float weight = (overlapEnd - overlapStart) / freqResolution;
			int32_t magnitude = fastPythag(fftResult.output[endBin].r, fftResult.output[endBin].i);
			weightedSum += static_cast<float>(magnitude) * weight;
			totalWeight += weight;
		}
	}

	// Calculate weighted average magnitude
	if (totalWeight > 0.0f) {
		return weightedSum / totalWeight;
	}
	return 0.0f;
}

// Update peak tracking and draw peak indicator for equalizer bar
// Peak decays using squared decay formula for smooth exponential-like decay
void Visualizer::updateAndDrawPeak(oled_canvas::Canvas& canvas, int32_t bar, float normalizedHeight, int32_t barLeftX,
                                   int32_t barRightX, int32_t kGraphMinY, int32_t kGraphMaxY, int32_t kGraphHeight,
                                   uint32_t visualizerMode) {
	// Only use peak tracking arrays when in equalizer mode (conditional memory usage)
	if (visualizerMode != RuntimeFeatureStateVisualizer::VisualizerEqualizer) {
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

		// Draw 3-pixel thick line (peak line + one pixel above + one pixel below if within bounds)
		canvas.drawHorizontalLine(peakY, barLeftX, barRightX);
		if (peakY > kGraphMinY) {
			canvas.drawHorizontalLine(peakY - 1, barLeftX, barRightX);
		}
		if (peakY < kGraphMaxY) {
			canvas.drawHorizontalLine(peakY + 1, barLeftX, barRightX);
		}
	}
}

/// Render visualizer waveform or spectrum on OLED display
void Visualizer::renderVisualizer(oled_canvas::Canvas& canvas) {
	// Check visualizer mode
	uint32_t visualizerMode = runtimeFeatureSettings.get(RuntimeFeatureSettingType::Visualizer);

	if (visualizerMode == RuntimeFeatureStateVisualizer::VisualizerSpectrum) {
		// Render spectrum using FFT
		renderVisualizerSpectrum(canvas);
		return;
	}
	else if (visualizerMode == RuntimeFeatureStateVisualizer::VisualizerEqualizer) {
		// Render equalizer using FFT
		renderVisualizerEqualizer(canvas);
		return;
	}
	else {
		// Default to waveform rendering (for VisualizerWaveform)
		renderVisualizerWaveform(canvas);
		return;
	}
}

/// Render visualizer waveform on OLED display
void Visualizer::renderVisualizerWaveform(oled_canvas::Canvas& canvas) {
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

/// Render visualizer spectrum on OLED display using FFT
void Visualizer::renderVisualizerSpectrum(oled_canvas::Canvas& canvas) {
	// Cache visualizer mode to avoid redundant runtime feature settings queries
	uint32_t visualizerMode = runtimeFeatureSettings.get(RuntimeFeatureSettingType::Visualizer);

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
	constexpr int32_t kNumBins = kSpectrumFFTOutputSize;                      // 129 bins
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
		float binFloat = frequency * static_cast<float>(kSpectrumFFTSize) / static_cast<float>(::kSampleRate);

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
		if (pixel < kMaxSpectrumPixels && visualizerMode == RuntimeFeatureStateVisualizer::VisualizerSpectrum) {
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
void Visualizer::renderVisualizerEqualizer(oled_canvas::Canvas& canvas) {
	// Cache visualizer mode to avoid redundant runtime feature settings queries
	uint32_t visualizerMode = runtimeFeatureSettings.get(RuntimeFeatureSettingType::Visualizer);

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
	// If all bins are very small, draw baseline
	if (fftResult.isSilent) {
		canvas.clearAreaExact(kGraphMinX, kGraphMinY, kGraphMaxX, kGraphMaxY + 1);
		// Draw baseline as individual 1-pixel bars at each bar location (not a full-width line)
		for (int32_t bar = 0; bar < kEqualizerNumBars; bar++) {
			int32_t barLeftX = kEqualizerContentStartX + (bar * (kBarWidth + kBarGap));
			int32_t barRightX = barLeftX + kBarWidth - 1;
			// Draw 1-pixel baseline at bottom of each bar location
			canvas.drawHorizontalLine(kGraphMaxY, barLeftX, barRightX);
		}
		OLED::markChanged();
		return;
	}

	// Clear the visualizer area before drawing
	canvas.clearAreaExact(kGraphMinX, kGraphMinY, kGraphMaxX, kGraphMaxY + 1);

	// Calculate frequency resolution per bin
	float freqResolution = static_cast<float>(::kSampleRate) / static_cast<float>(kSpectrumFFTSize);

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
		if (visualizerMode == RuntimeFeatureStateVisualizer::VisualizerEqualizer) {
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

		// Draw filled bar using vertical lines (draw from left to right)
		for (int32_t x = barLeftX; x <= barRightX; x++) {
			canvas.drawVerticalLine(x, barTopY, barBottomY);
		}

		// Update peak tracking and draw peak indicator
		float normalizedHeight = static_cast<float>(scaledHeight) / static_cast<float>(kGraphHeight);
		updateAndDrawPeak(canvas, bar, normalizedHeight, barLeftX, barRightX, kGraphMinY, kGraphMaxY, kGraphHeight,
		                  visualizerMode);
	}

	// Mark OLED as changed so it gets sent to display
	OLED::markChanged();
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
	// Request OLED refresh for visualizer if active (ensures continuous updates)
	// Use frame skipping to reduce CPU usage (update every 2 frames = ~30fps instead of ~60fps)
	// Cache runtime feature check to avoid redundant calls

	// Re-enable visualizer if conditions are met (handles case where displayVisualizer was reset)
	// This ensures the visualizer comes back when VU meter is re-enabled
	if (!displayVisualizer && visualizerEnabled && displayVUMeter && modControllable && modKnobMode == 0) {
		displayVisualizer = true;
	}

	if (displayVisualizer && visualizerEnabled && displayVUMeter && modControllable && modKnobMode == 0) {
		// Frame counter overflow is handled correctly by modulo arithmetic
		visualizerFrameCounter++;
		// Update every N frames for ~30fps (reduce CPU usage)
		if ((visualizerFrameCounter % kVisualizerFrameSkip) == 0) {
			renderUIsForOled();
		}
	}
}

/// Reset visualizer state (called when switching views)
void Visualizer::reset() {
	displayVisualizer = false;
	visualizerFrameCounter = 0;
}

/// Set whether visualizer display is enabled
/// @param enabled Whether visualizer should be enabled
void Visualizer::setEnabled(bool enabled) {
	displayVisualizer = enabled;
}

/// Get whether visualizer display is enabled
/// @return true if visualizer display is enabled
bool Visualizer::isEnabled() {
	return displayVisualizer;
}

} // namespace deluge::hid::display
