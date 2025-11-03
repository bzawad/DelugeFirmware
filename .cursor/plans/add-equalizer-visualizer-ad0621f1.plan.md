<!-- ad0621f1-5907-4100-9f34-17220f0ce88e 0b7ccc54-ad6a-4f55-b657-204293dddf93 -->
# Implementation Plan: Equalizer Visualizer

## Overview

Extend the existing visualizer feature with a new "Equalizer" type that displays audio frequency spectrum as vertical bars. The implementation will use FFT analysis and render frequency bands from low (left) to high (right) across the 128x43 pixel display.

## Key Files to Modify

1. **src/deluge/model/settings/runtime_feature_settings.h**

- Add `VisualizerEqualizer = 3` to `RuntimeFeatureStateVisualizer` enum (currently has Off=0, Waveform=1, Bars=2)

2. **src/deluge/model/settings/runtime_feature_settings.cpp**

- Update `SetupVisualizerSetting()` to add "Equalizer" option to the visualizer settings menu

3. **src/deluge/processing/engines/audio_engine.cpp**

- Modify audio sampling logic (around line 624) to also sample when `VisualizerEqualizer` mode is active
- Need to collect enough samples for FFT (e.g., 256-512 samples for 256-512 point FFT)

4. **src/deluge/gui/views/view.cpp**

- Update `potentiallyRenderVisualizer()` to check for both `VisualizerWaveform` and `VisualizerEqualizer`
- Update `requestVisualizerUpdateIfNeeded()` similarly
- Create new `renderVisualizerEqualizer()` function that:
- Reads samples from `visualizerSampleBuffer`
- Performs FFT using NE10 library (via `FFTConfigManager::getConfig()`)
- Divides frequency spectrum into bars (suggest 32 bars to match reference, or fit to 128 pixels)
- Renders bars from left (low freq) to right (high freq) using logarithmic frequency scaling
- Handles peak tracking with decay (similar to reference implementation)

5. **src/deluge/gui/views/view.h**

- Add `renderVisualizerEqualizer()` method declaration

## Implementation Details

### FFT Configuration

- Use 256-point or 512-point FFT (power of 2) - magnitude 8 or 9
- Use `FFTConfigManager::getConfig(magnitude)` to get FFT config
- Use `ne10_fft_r2c_1d_int32_neon()` to perform FFT
- FFT operates on Q31/Q15 format integers (already in visualizerSampleBuffer)

### Frequency Band Calculation

- Use logarithmic frequency mapping (like reference: 20Hz to 20kHz)
- Map frequency bins to bars using logarithmic scale
- Left side: low frequencies (20-200 Hz)
- Right side: high frequencies (10-20 kHz)

### Display Rendering

- 128 pixels wide, 43 pixels tall (same as waveform)
- Draw vertical bars using `Canvas::drawVerticalLine()` or similar
- Implement peak tracking with decay (store peak heights per bar, decay over time)
- Use green monochrome rendering (matching OLED display style)
- **Add a test line on the bottom row** (e.g., horizontal line at y = bottom of display) to verify the display is rendering correctly even if frequency logic is incorrect

### Sample Collection

- Extend existing sampling logic to work for equalizer mode
- May need larger buffer or different sampling strategy for FFT (need N samples for N-point FFT)
- Consider using circular buffer with sufficient size (256-512 samples)

## Notes

- The `VisualizerBars = 2` enum value exists but appears unused - we'll add `VisualizerEqualizer = 3`
- Reference implementation uses 32 bars, but we may adjust based on 128-pixel width
- Display area constants: `OLED_MAIN_WIDTH_PIXELS` (128) and `OLED_MAIN_HEIGHT_PIXELS - OLED_MAIN_TOPMOST_PIXEL` (43)
- Audio samples are in Q15 format (int32_t) in `visualizerSampleBuffer[kVisualizerBufferSize]`