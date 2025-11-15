# Visualizer Circle Mode Implementation Plan

## Overview
Add a fourth visualizer mode called "Circle" to the Deluge Firmware's visualizer feature. The Circle visualizer will display audio frequency bands as concentric circles with varying radii and thicknesses, adapted from the existing `multi_band_circle_waveform.cpp` reference implementation but modified for monochrome OLED display using canvas drawing primitives.

## Requirements
- **Fourth visualizer option**: Available as "Circle" in Community Features settings
- **Hotkey**: CV button for Circle mode switching when visualizer is active
- **Monochrome**: OLED display doesn't support color, so use monochrome rendering
- **Safe patterns**: Follow existing visualizer implementation patterns
- **Documentation**: Update relevant documentation files

## Implementation Details

### 1. Enum and Constants Updates

#### File: `src/deluge/model/settings/runtime_feature_settings.h`
```cpp
enum RuntimeFeatureStateVisualizer : uint32_t {
    VisualizerOff = 0,
    VisualizerWaveform = 1,
    VisualizerSpectrum = 2,
    VisualizerEqualizer = 3,
    VisualizerCircle = 4  // NEW: Add Circle mode
};
```

#### File: `src/deluge/model/settings/runtime_feature_settings.cpp`
- Add Circle option to the SetupVisualizerSetting function:
```cpp
{
    .displayName = display->haveOLED() ? "Circle" : "CIRC",
    .value = RuntimeFeatureStateVisualizer::VisualizerCircle,
},
```

### 2. Visualizer Core Implementation

#### File: `src/deluge/hid/display/visualizer.h`
- Add renderVisualizerCircle method declaration:
```cpp
/// Render circle visualization using FFT
/// @param canvas The OLED canvas to render to
static void renderVisualizerCircle(oled_canvas::Canvas& canvas);
```

#### File: `src/deluge/hid/display/visualizer.cpp`
- Update renderVisualizer method to handle Circle mode:
```cpp
if (visualizer_mode == RuntimeFeatureStateVisualizer::VisualizerCircle) {
    // Render circle visualization using FFT
    ::deluge::hid::display::renderVisualizerCircle(canvas);
    return;
}
```
- Add renderVisualizerCircle wrapper method:
```cpp
/// Render circle visualization using FFT
/// @param canvas The OLED canvas to render to
void Visualizer::renderVisualizerCircle(oled_canvas::Canvas& canvas) {
    ::deluge::hid::display::renderVisualizerCircle(canvas);
}
```
- Update isEnabled method to include Circle mode:
```cpp
return (visualizer_mode == RuntimeFeatureStateVisualizer::VisualizerWaveform)
       || (visualizer_mode == RuntimeFeatureStateVisualizer::VisualizerSpectrum)
       || (visualizer_mode == RuntimeFeatureStateVisualizer::VisualizerEqualizer)
       || (visualizer_mode == RuntimeFeatureStateVisualizer::VisualizerCircle);  // NEW
```

### 3. Circle Visualizer Implementation

#### File: `src/deluge/hid/display/visualizer/visualizer_circle.h` (NEW)
```cpp
#pragma once

#include "visualizer_common.h"

namespace deluge::hid::display {

// Render circle visualization with frequency bands as concentric circles
void renderVisualizerCircle(oled_canvas::Canvas& canvas);

// Calculate frequency band data for circle rendering
void calculateCircleBandData(FFTResult& fft_result, std::vector<float>& lowBand,
                           std::vector<float>& midBand, std::vector<float>& highBand);

// Render circular frequency band on canvas
void renderCircularBand(oled_canvas::Canvas& canvas, const std::vector<float>& bandData,
                       float radius, float thickness, int32_t centerX, int32_t centerY,
                       int32_t maxRadius, int32_t displayWidth, int32_t displayHeight);

} // namespace deluge::hid::display
```

#### File: `src/deluge/hid/display/visualizer/visualizer_circle.cpp` (NEW)
- Implement renderVisualizerCircle function following the pattern of visualizer_spectrum.cpp
- Use FFT computation shared with other visualizers
- Handle silence detection
- Clear display area
- Process frequency bands (Low: 20-250Hz, Mid: 250-2000Hz, High: 2000-20000Hz)
- Render concentric circles with different radii for each band
- Use canvas drawing primitives instead of OpenGL

Key constants (adapted from multi_band_circle_waveform.h):
```cpp
namespace {
constexpr int32_t kCircleFFTSize = 512; // Match FFT size from visualizer_fft.cpp
constexpr int32_t kLowCutoff = 250;     // 20-250 Hz
constexpr int32_t kMidCutoff = 2000;    // 250-2000 Hz
constexpr int32_t kHighCutoff = 20000;  // 2000-20000 Hz
constexpr float kLowRadius = 0.2f;      // Inner circle radius (normalized)
constexpr float kMidRadius = 0.5f;      // Middle circle radius (normalized)
constexpr float kHighRadius = 0.8f;     // Outer circle radius (normalized)
constexpr float kThickness = 0.15f;     // Circle thickness (normalized)
} // namespace
```

### 4. Hotkey Implementation

#### File: `src/deluge/hid/buttons.cpp`
- Add CV button handling for Circle mode in the visualizer hotkey section:
```cpp
else if (b == CV) {  // NEW: CV button for Circle mode
    deluge::hid::display::Visualizer::setSessionMode(RuntimeFeatureStateVisualizer::VisualizerCircle);
    goto dealtWith;
}
```

### 5. Documentation Updates

#### File: `website/src/content/docs/features/visualizer.mdx`
- Add Circle mode to the enabling section options list
- Add Circle mode to display conditions
- Add new section describing Circle mode:
```markdown
### Circle Mode <Badge text="c1.5" variant="tip" />

Circle mode displays audio frequency content as concentric circles, with different frequency bands represented by circles of varying radii.

- **Inner circle (Low frequencies)**: 20-250 Hz range, smaller radius
- **Middle circle (Mid frequencies)**: 250-2000 Hz range, medium radius
- **Outer circle (High frequencies)**: 2000-20000 Hz range, larger radius

Each circle's thickness and intensity varies based on the amplitude of its corresponding frequency band, creating a radial representation of the audio spectrum.
```

#### File: `website/src/content/docs/features/community_features.mdx`
- Update visualizer description to mention Circle mode

#### File: `website/src/content/docs/changelogs/CHANGELOG.mdx`
- Add entry for Circle visualizer addition

## Technical Implementation Notes

### Canvas Drawing Adaptation
The reference `multi_band_circle_waveform.cpp` uses OpenGL for rendering circles. For OLED canvas:

- Use `canvas.drawCircle()` or approximate circles with line segments
- Use `canvas.drawLine()` for circle outlines with varying thickness
- Implement circle drawing algorithm that works with integer coordinates
- Handle circle scaling to fit OLED display dimensions (typically 128x64 pixels)

### Frequency Band Processing
- Reuse existing FFT computation infrastructure
- Filter FFT results into three frequency bands using the same approach as other visualizers
- Apply appropriate amplitude scaling and normalization for OLED display range

### Performance Considerations
- Follow existing pattern of 30fps update rate
- Ensure FFT computation is shared and not duplicated
- Use efficient circle drawing algorithms suitable for embedded system

### Memory and Resource Usage
- Minimize additional memory usage beyond existing FFT buffers
- Reuse existing smoothing and processing utilities where possible
- Ensure implementation fits within Deluge's resource constraints

## Testing Strategy

1. **Unit Testing**: Verify circle rendering with synthetic audio data
2. **Integration Testing**: Test hotkey switching and mode persistence
3. **Performance Testing**: Ensure 30fps rendering performance
4. **Visual Testing**: Verify circle appearance across different audio signals
5. **Edge Case Testing**: Test with silence, clipping, and various frequency content

## Files to Create/Modify

### New Files:
- `src/deluge/hid/display/visualizer/visualizer_circle.h`
- `src/deluge/hid/display/visualizer/visualizer_circle.cpp`

### Modified Files:
- `src/deluge/model/settings/runtime_feature_settings.h`
- `src/deluge/model/settings/runtime_feature_settings.cpp`
- `src/deluge/hid/display/visualizer.h`
- `src/deluge/hid/display/visualizer.cpp`
- `src/deluge/hid/buttons.cpp`
- `website/src/content/docs/features/visualizer.mdx`
- `website/src/content/docs/features/community_features.mdx`
- `website/src/content/docs/changelogs/CHANGELOG.mdx`

## Dependencies
- Existing FFT computation infrastructure
- OLED canvas drawing primitives
- Visualizer common utilities and constants
- Runtime feature settings system

## Risk Assessment
- **Low Risk**: Follows established patterns from existing visualizers
- **Low Risk**: Reuses existing FFT and audio processing infrastructure
- **Low Risk**: Canvas-based rendering is well-tested in existing visualizers
- **Medium Risk**: Circle drawing algorithm needs optimization for OLED performance

## Success Criteria
1. Circle mode appears in Community Features settings
2. CV button switches to Circle mode when visualizer is active
3. Circles render correctly showing frequency band amplitudes
4. Performance maintains 30fps with other visualizer modes
5. Documentation accurately describes the new feature
6. Implementation follows Deluge coding standards and patterns
