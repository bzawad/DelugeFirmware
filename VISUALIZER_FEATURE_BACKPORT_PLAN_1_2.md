# Visualizer Feature Backport Plan: 1.3 → 1.2

## Overview

This document outlines the plan to backport the visualizer feature from `feature/oscilloscope-visualizer-more-clip-1.3` into the current branch `feature/oscilloscope-visualizer-more-clip-1.2`.

**Important Notes:**
- Only import code related to the visualizer feature
- Do not create integration files that don't exist in the target branch
- Focus on visualizer-specific functionality only
- DO NOT commit, create branches, or push any changes

## Scope Analysis

Based on git diff analysis between the two branches:

### Files to Create (New Files)
All files in `src/deluge/hid/display/visualizer/` directory (30+ files):

**Core Visualizer Files:**
- `src/deluge/hid/display/visualizer.h` - Main visualizer class header
- `src/deluge/hid/display/visualizer.cpp` - Main visualizer class implementation

**Visualizer Implementations:**
- `visualizer_waveform.h/cpp` - Waveform visualization
- `visualizer_line_spectrum.h/cpp` - Line spectrum visualization
- `visualizer_bar_spectrum.h/cpp` - Bar spectrum visualization
- `visualizer_stereo_line_spectrum.h/cpp` - Stereo line spectrum
- `visualizer_stereo_bar_spectrum.h/cpp` - Stereo bar spectrum
- `visualizer_cube.h/cpp` - 3D cube visualization
- `visualizer_tunnel.h/cpp` - Tunnel visualization
- `visualizer_starfield.h/cpp` - Starfield visualization
- `visualizer_skyline.h/cpp` - Skyline visualization
- `visualizer_pulsegrid.h/cpp` - Pulse grid visualization
- `visualizer_midi_piano_roll.h/cpp` - MIDI piano roll visualization

**Common/Shared Files:**
- `visualizer_common.h/cpp` - Shared utilities and constants
- `visualizer_fft.h/cpp` - FFT processing utilities

### Files to Modify (Existing Files)

**Runtime Feature Settings:**
- `src/deluge/model/settings/runtime_feature_settings.h` - Add visualizer enums and settings
- `src/deluge/model/settings/runtime_feature_settings.cpp` - Add visualizer setting initialization

**Localization Files:**
- `src/deluge/gui/l10n/english.json` - Add visualizer-related strings
- `src/deluge/gui/l10n/seven_segment.json` - Add seven-segment display strings
- `src/deluge/gui/l10n/strings.h` - Add string constants
- `src/deluge/gui/l10n/g_english.cpp` - Generated English strings
- `src/deluge/gui/l10n/g_seven_segment.cpp` - Generated seven-segment strings

**Core Integration Files:**
- `src/deluge/deluge.cpp` - Main deluge file integration
- `src/deluge/gui/menu_item/runtime_feature/settings.cpp` - Runtime feature menu integration
- `src/deluge/gui/views/view.cpp` - Base view class integration
- `src/deluge/gui/views/arranger_view.cpp` - Arranger view integration
- `src/deluge/gui/views/instrument_clip_view.cpp` - Instrument clip view integration
- `src/deluge/gui/views/session_view.cpp` - Session view integration
- `src/deluge/gui/ui/keyboard/keyboard_screen.cpp` - Keyboard screen integration
- `src/deluge/hid/buttons.cpp` - Button handling integration
- `src/deluge/io/midi/midi_engine.cpp` - MIDI engine integration
- `src/deluge/model/clip/instrument_clip_minder.cpp` - Clip minder integration
- `src/deluge/model/global_effectable/global_effectable_for_clip.cpp` - Global effectable integration
- `src/deluge/model/settings/runtime_feature_settings.cpp` - Settings integration
- `src/deluge/processing/engines/audio_engine.cpp` - Audio engine integration
- `src/deluge/processing/sound/sound.cpp` - Sound processing integration

**Build System:**
- `src/deluge/CMakeLists.txt` - Add visualizer files to build
- `src/CMakeLists.txt` - Update if needed

## Implementation Plan

### Phase 1: Create New Visualizer Files
1. Create the `src/deluge/hid/display/visualizer/` directory structure
2. Copy all 30+ visualizer files from the 1.3 branch
3. Ensure proper file permissions and structure

### Phase 2: Update Runtime Feature Settings
1. Add `RuntimeFeatureStateVisualizer` enum to `runtime_feature_settings.h`
2. Add `Visualizer` setting type to `RuntimeFeatureSettingType` enum
3. Update `SetupVisualizerSetting` function in `runtime_feature_settings.cpp`
4. Initialize visualizer setting in `init()` method

### Phase 3: Update Localization
1. Add visualizer-related strings to `english.json`
2. Add seven-segment strings to `seven_segment.json`
3. Update generated files (`g_english.cpp`, `g_seven_segment.cpp`)
4. Update `strings.h` with new constants

### Phase 4: Integrate Visualizer into Existing Files
1. Update each integration file listed above with visualizer-related changes
2. Focus only on visualizer-specific code additions/modifications
3. Preserve existing functionality in target branch

### Phase 5: Update Build System
1. Add all new visualizer source files to `src/deluge/CMakeLists.txt`
2. Ensure proper compilation order and dependencies

## Risk Assessment

### Low Risk Items:
- Creating new visualizer files (completely isolated functionality)
- Adding new runtime feature settings (extensible system)
- Localization updates (additive changes)

### Medium Risk Items:
- Integration into existing view classes (potential conflicts)
- Audio engine modifications (performance/timing sensitive)
- Button handling changes (UI behavior changes)

### High Risk Items:
- Core deluge.cpp integration (central file with many responsibilities)
- MIDI engine modifications (real-time sensitive)

## Dependencies

The visualizer feature depends on:
- OLED canvas system (already exists in 1.2)
- FFT configuration manager (already exists in 1.2)
- Runtime feature settings system (already exists in 1.2)
- Audio engine infrastructure (already exists in 1.2)

## Testing Strategy

After implementation:
1. Verify compilation succeeds
2. Check that visualizer can be enabled/disabled in settings
3. Test visualizer display in various views
4. Verify audio processing doesn't interfere with existing functionality
5. Test MIDI integration if applicable

## Rollback Plan

If issues arise:
1. Visualizer files can be completely removed
2. Runtime feature settings can be reverted
3. Localization changes can be undone
4. Integration changes can be selectively reverted

## Success Criteria

- All visualizer files compile successfully
- Visualizer appears as option in runtime feature settings
- Visualizer displays correctly in supported views
- No regression in existing functionality
- Audio processing performance maintained
