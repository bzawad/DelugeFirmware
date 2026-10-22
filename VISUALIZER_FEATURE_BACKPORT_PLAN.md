# Visualizer Feature Backport Plan

## Overview
This document outlines the plan to backport the visualizer feature from branch `feature/oscilloscope-visualizer-more-clip` to branch `feature/oscilloscope-visualizer-more-clip-1.3`. The visualizer feature adds OLED display visualizations for audio waveforms, spectrum analysis, and various visual effects.

## Analysis Summary

### Files to Create (New Files)
All files in `src/deluge/hid/display/visualizer/` directory:
- `visualizer.h` - Main visualizer header with class definition and static methods
- `visualizer.cpp` - Main visualizer implementation with rendering logic
- `visualizer_common.h` - Common utilities and constants
- `visualizer_common.cpp` - Shared visualizer functionality
- `visualizer_waveform.h` - Waveform visualizer implementation
- `visualizer_waveform.cpp` - Waveform rendering logic
- `visualizer_line_spectrum.h` - Line spectrum visualizer implementation
- `visualizer_line_spectrum.cpp` - Line spectrum rendering logic
- `visualizer_bar_spectrum.h` - Bar spectrum visualizer implementation
- `visualizer_bar_spectrum.cpp` - Bar spectrum rendering logic
- `visualizer_stereo_line_spectrum.h` - Stereo line spectrum visualizer
- `visualizer_stereo_line_spectrum.cpp` - Stereo line spectrum rendering
- `visualizer_stereo_bar_spectrum.h` - Stereo bar spectrum visualizer
- `visualizer_stereo_bar_spectrum.cpp` - Stereo bar spectrum rendering
- `visualizer_cube.h` - Cube visualizer implementation
- `visualizer_cube.cpp` - Cube rendering logic
- `visualizer_tunnel.h` - Tunnel visualizer implementation
- `visualizer_tunnel.cpp` - Tunnel rendering logic
- `visualizer_starfield.h` - Starfield visualizer implementation
- `visualizer_starfield.cpp` - Starfield rendering logic
- `visualizer_skyline.h` - Skyline visualizer implementation
- `visualizer_skyline.cpp` - Skyline rendering logic
- `visualizer_pulsegrid.h` - Pulse grid visualizer implementation
- `visualizer_pulsegrid.cpp` - Pulse grid rendering logic
- `visualizer_midi_piano_roll.h` - MIDI piano roll visualizer
- `visualizer_midi_piano_roll.cpp` - MIDI piano roll rendering
- `visualizer_fft.h` - FFT processing utilities
- `visualizer_fft.cpp` - FFT computation for spectrum analysis

### Files to Modify (Existing Files with Integration)

#### Core Integration Files
1. **`src/deluge/gui/views/view.cpp`**
   - Add `#include "hid/display/visualizer.h"`
   - Add `Visualizer::reset()` call in `focusRegained()`
   - Refactor `modEncoderAction()` into helper methods
   - Refactor `modEncoderButtonAction()` into helper methods
   - Add SHIFT+LEVEL mod button visualizer toggle logic in `modButtonAction()`
   - Modify VU meter toggle logic to control visualizer
   - Add visualizer state management in `setModLedStates()`
   - Add visualizer disable logic in `potentiallyRenderVUMeter()`

2. **`src/deluge/processing/sound/sound.cpp`**
   - Add `#include "hid/display/visualizer.h"`
   - Add clip-specific audio sampling call in `render()` method

3. **`src/deluge/model/global_effectable/global_effectable_for_clip.cpp`**
   - Add visualizer audio sampling integration for kit clips

4. **`src/deluge/model/clip/instrument_clip_minder.cpp`**
   - Add visualizer state management for clip changes

#### View Integration Files
5. **`src/deluge/gui/views/session_view.cpp`**
   - Add visualizer rendering integration

6. **`src/deluge/gui/views/arranger_view.cpp`**
   - Add visualizer rendering integration

7. **`src/deluge/gui/views/instrument_clip_view.cpp`**
   - Add clip-specific visualizer integration

8. **`src/deluge/gui/ui/keyboard/keyboard_screen.cpp`**
   - Add visualizer rendering integration

#### Runtime Feature Settings
9. **`src/deluge/gui/menu_item/runtime_feature/settings.cpp`**
   - Add visualizer runtime feature setting

#### Localization Files
10. **`src/deluge/gui/l10n/english.json`**
    - Add visualizer-related strings

11. **`src/deluge/gui/l10n/g_english.cpp`**
    - Add visualizer string generation

12. **`src/deluge/gui/l10n/g_seven_segment.cpp`**
    - Add seven-segment display strings

13. **`src/deluge/gui/l10n/seven_segment.json`**
    - Add seven-segment visualizer strings

14. **`src/deluge/gui/l10n/strings.h`**
    - Add string ID definitions

### Dependencies and Prerequisites

#### Required Headers/Definitions
- Ensure `dsp/fft/fft_config_manager.h` exists
- Ensure `dsp_ng/core/types.hpp` exists
- Ensure `oled_canvas/canvas.h` exists
- Ensure `model/settings/runtime_feature_settings.h` exists

#### Runtime Feature Settings
- Add `RuntimeFeatureSettingType::Visualizer` enum value
- Add `RuntimeFeatureStateVisualizer` enum with visualizer mode definitions

### Implementation Steps

#### Phase 1: Create Visualizer Core Files
1. Create `src/deluge/hid/display/visualizer/` directory
2. Copy and create all visualizer implementation files from the source branch
3. Ensure all dependencies are properly included

#### Phase 2: Runtime Feature Integration
1. Add visualizer setting to runtime feature settings
2. Add localization strings for visualizer modes
3. Update string generation files

#### Phase 3: Core Integration
1. Modify `view.cpp` for visualizer state management and UI integration
2. Modify `sound.cpp` for audio sampling
3. Modify `global_effectable_for_clip.cpp` for kit clip audio sampling

#### Phase 4: View Integration
1. Update all view files to integrate visualizer rendering
2. Update clip minder for visualizer state management

#### Phase 5: Testing and Validation
1. Verify visualizer builds successfully
2. Test visualizer toggle functionality
3. Test different visualizer modes
4. Verify audio sampling works correctly

### Key Integration Points

#### Audio Sampling
- Global visualizer samples from the main audio engine output
- Clip-specific visualizer samples from individual clip processing
- Audio is downsampled and stored in circular buffers for display

#### Display Integration
- Visualizer renders on OLED display when VU meter is enabled
- Can be toggled independently with SHIFT+LEVEL mod button
- Different modes available (waveform, spectrum, stereo, special effects)

#### UI State Management
- Visualizer state resets when switching views
- Integrates with existing VU meter toggle logic
- Session and clip-specific visualizer modes supported

### Risk Assessment

#### Low Risk
- Visualizer files are self-contained in their own directory
- Feature is gated behind runtime feature setting
- Only activates when VU meter is enabled or explicitly toggled

#### Medium Risk
- Audio sampling integration in sound processing pipeline
- OLED rendering performance impact
- Memory usage for audio buffers and FFT processing

#### Mitigation Strategies
- Ensure audio sampling is efficient (downsampling, not blocking)
- Add performance monitoring for OLED rendering
- Make visualizer easily disableable if issues arise

### Success Criteria
- Visualizer feature compiles successfully
- Visualizer can be enabled/disabled via settings
- Visualizer displays correctly in supported views
- Audio sampling works without audio glitches
- Performance impact is minimal
- Feature integrates cleanly with existing VU meter functionality

### Notes
- Documentation files (website/changelog) are intentionally excluded as requested
- Only visualizer-related code is being backported
- No integration files for non-existent features in target branch
