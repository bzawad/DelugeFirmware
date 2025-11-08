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

#include "visualizer_fft.h"
#include "dsp/fft/fft_config_manager.h"
#include "hid/display/visualizer.h"
#include "processing/engines/audio_engine.h"
#include "util/functions.h"
#include "visualizer_common.h"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace deluge::hid::display {

// Static FFT buffers (shared by Spectrum/Equalizer visualizers)
namespace {
constexpr int32_t kSpectrumFFTSize = 256;
constexpr int32_t kSpectrumFFTMagnitude = 8;                            // 2^8 = 256
constexpr int32_t kSpectrumFFTOutputSize = (kSpectrumFFTSize >> 1) + 1; // 129 bins

// Silence detection thresholds
constexpr int32_t kFFTSilenceThreshold = 100; // FFT magnitude threshold

// Format conversion and display constants
constexpr int32_t kQ31ToQ15Shift = 16;        // Convert from Q31 to Q15 format (31-15 = 16 bits)
constexpr int32_t kSilenceCheckInterval = 16; // Interval for checking silence in loops (reduces CPU usage)

// Static FFT buffers
int32_t spectrumFFTInput[kSpectrumFFTSize];
ne10_fft_cpx_int32_t spectrumFFTOutput[kSpectrumFFTOutputSize];

// Precomputed Hanning window in Q31 format
int32_t spectrumHanningWindow[kSpectrumFFTSize];

// FFT result caching to avoid recomputation on every frame
struct CachedFFTResult {
	uint32_t lastWritePos; // Last buffer write position when FFT was computed
	ne10_fft_cpx_int32_t cachedOutput[kSpectrumFFTOutputSize];
	bool isValid;

	CachedFFTResult(uint32_t lastWritePos = 0, bool isValid = false) : lastWritePos(lastWritePos), isValid(isValid) {
		// Initialize cachedOutput array to zero
		memset(cachedOutput, 0, sizeof(cachedOutput));
	}
};
static CachedFFTResult cachedFFT{0, false};
} // namespace

// Initialize Hanning window coefficients (called once)
void initSpectrumHanningWindow() {
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
bool isFFTSilent(const ne10_fft_cpx_int32_t* fftOutput, int32_t threshold) {
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

// Compute FFT for visualizer with caching optimization
// Returns FFT output and validity flags
FFTResult computeVisualizerFFT() {
	FFTResult result{nullptr, false, false};

	// Read sample count atomically (single read is safe)
	uint32_t sampleCount = Visualizer::visualizerSampleCount.load(std::memory_order_acquire);
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
	uint32_t currentWritePos = Visualizer::visualizerWritePos.load(std::memory_order_acquire);
	if (cachedFFT.isValid) {
		// Calculate buffer position difference (handle wrap-around)
		uint32_t posDiff = (currentWritePos >= cachedFFT.lastWritePos)
		                       ? (currentWritePos - cachedFFT.lastWritePos)
		                       : (Visualizer::kVisualizerBufferSize - cachedFFT.lastWritePos + currentWritePos);

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
			bool isCurrentlySilent = isFFTSilent(cachedFFT.cachedOutput, kFFTSilenceThreshold);

			// Apply silence delay timer (same as waveform visualizer)
			constexpr uint32_t kSilenceDelaySamples = 22050; // 0.5 second at 44.1kHz
			if (isCurrentlySilent) {
				// Silence detected - start or check timer
				if (Visualizer::silenceStartTime == 0) {
					// First silence detection - record start time
					Visualizer::silenceStartTime = AudioEngine::audioSampleTimer;
				}
				else {
					// Check if silence has lasted 0.5 second
					uint32_t silenceDuration = AudioEngine::audioSampleTimer - Visualizer::silenceStartTime;
					result.isSilent = (silenceDuration >= kSilenceDelaySamples);
				}
			}
			else {
				// Sound detected - reset silence timer and clear silence state
				if (Visualizer::silenceStartTime != 0) {
					Visualizer::silenceStartTime = 0;
				}
				result.isSilent = false;
			}

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
		uint32_t bufferIndex = (readStartPos + i) % Visualizer::kVisualizerBufferSize;
		// Buffer bounds check: visualizerSampleBuffer is guaranteed to be at least Visualizer::kVisualizerBufferSize
		// and bufferIndex is modulo Visualizer::kVisualizerBufferSize, so it's always valid
		int32_t sample = Visualizer::visualizerSampleBuffer[bufferIndex]; // Q15

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
	bool isCurrentlySilent = isFFTSilent(spectrumFFTOutput, kFFTSilenceThreshold);

	// Apply silence delay timer (same as waveform visualizer)
	constexpr uint32_t kSilenceDelaySamples = 22050; // 0.5 second at 44.1kHz
	if (isCurrentlySilent) {
		// Silence detected - start or check timer
		if (Visualizer::silenceStartTime == 0) {
			// First silence detection - record start time
			Visualizer::silenceStartTime = AudioEngine::audioSampleTimer;
		}
		else {
			// Check if silence has lasted 0.5 second
			uint32_t silenceDuration = AudioEngine::audioSampleTimer - Visualizer::silenceStartTime;
			result.isSilent = (silenceDuration >= kSilenceDelaySamples);
		}
	}
	else {
		// Sound detected - reset silence timer and clear silence state
		if (Visualizer::silenceStartTime != 0) {
			Visualizer::silenceStartTime = 0;
		}
		result.isSilent = false;
	}

	return result;
}

// Calculate weighted average magnitude for a frequency band using FFT bin interpolation
// Uses weighted interpolation to smooth transitions between bins, preventing stepping artifacts
// when frequency bands don't align exactly with FFT bins
float calculateWeightedMagnitude(const FFTResult& fftResult, float lowerFreq, float upperFreq, float freqResolution) {
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

	// Return average magnitude (avoid division by zero)
	return (totalWeight > 0.0f) ? (weightedSum / totalWeight) : 0.0f;
}

} // namespace deluge::hid::display
