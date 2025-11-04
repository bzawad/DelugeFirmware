# Visualizer Refactoring Plan

## Overview
Extract individual visualizer types (Waveform, Spectrum, Equalizer) from `visualizer.cpp` into separate files to improve maintainability and prevent the file from growing too large as more visualizer types are added.

## Current Structure Analysis

### File Sizes
- `visualizer.cpp`: ~895 lines
- `visualizer.h`: ~150 lines

### Visualizer Types
1. **Waveform** (`renderVisualizerWaveform`) - ~140 lines
   - Reads samples directly from `AudioEngine::visualizerSampleBuffer`
   - No FFT dependencies
   - Simple line-graph rendering

2. **Spectrum** (`renderVisualizerSpectrum`) - ~140 lines
   - Uses FFT computation (shared with Equalizer)
   - Per-pixel smoothing (`spectrumSmoothedValues`)
   - Frequency-compressed logarithmic scale

3. **Equalizer** (`renderVisualizerEqualizer`) - ~110 lines
   - Uses FFT computation (shared with Spectrum)
   - Per-bar smoothing (`equalizerSmoothedValues`)
   - Peak tracking with decay (`equalizerPeakHeights`, `equalizerPeakDecay`)
   - Weighted magnitude calculation for frequency bands

### Shared Code
- **FFT Utilities** (~150 lines):
  - `computeVisualizerFFT()` - FFT computation with caching
  - `initSpectrumHanningWindow()` - Window initialization
  - `isFFTSilent()` - Silence detection
  - `calculateWeightedMagnitude()` - Frequency band calculation
  - FFT buffers and caching structures

- **Common Helpers** (~50 lines):
  - `getVisualizerReadStartPos()` - Buffer position calculation
  - `applyVisualizerCompression()` - Visual compression formula
  - `calculateFrequencyBandRange()` - Equalizer-specific frequency bands
  - `updateAndDrawPeak()` - Equalizer-specific peak tracking

- **Lifecycle & Dispatch** (~100 lines):
  - `renderVisualizer()` - Main dispatch/router
  - `potentiallyRenderVisualizer()` - Conditional rendering
  - `requestVisualizerUpdateIfNeeded()` - Update scheduling
  - `reset()`, `setEnabled()`, `isEnabled()` - State management

### Shared Buffers & Constants
- FFT buffers (shared by Spectrum/Equalizer): `spectrumFFTInput`, `spectrumFFTOutput`, `spectrumHanningWindow`
- Smoothing arrays: `spectrumSmoothedValues` (Spectrum), `equalizerSmoothedValues` (Equalizer)
- Peak tracking: `equalizerPeakHeights`, `equalizerPeakDecay` (Equalizer only)
- Constants: Reference magnitudes, thresholds, compression parameters, display constants

## Recommended File Structure

```
src/deluge/hid/display/
├── visualizer.h                    # Main Visualizer class (public API)
├── visualizer.cpp                  # Main dispatch & lifecycle (~150 lines)
└── visualizer/
    ├── visualizer_common.h        # Shared types, forward declarations
    ├── visualizer_common.cpp      # Shared helpers & constants (~200 lines)
    ├── visualizer_fft.h          # FFT-related declarations
    ├── visualizer_fft.cpp        # FFT computation & buffers (~200 lines)
    ├── visualizer_waveform.h      # Waveform visualizer header
    ├── visualizer_waveform.cpp    # Waveform implementation (~150 lines)
    ├── visualizer_spectrum.h      # Spectrum visualizer header
    ├── visualizer_spectrum.cpp    # Spectrum implementation (~150 lines)
    ├── visualizer_equalizer.h     # Equalizer visualizer header
    └── visualizer_equalizer.cpp   # Equalizer implementation (~150 lines)
```

## Implementation Strategy

### Phase 1: Extract Shared Code
1. **Create `visualizer/visualizer_common.h`**:
   - Move `FFTResult` struct definition
   - Forward declarations
   - Shared constants (if needed in headers)

2. **Create `visualizer/visualizer_common.cpp`**:
   - Move shared helper functions:
     - `getVisualizerReadStartPos()`
     - `applyVisualizerCompression()`
   - Move shared constants that don't need to be in header
   - Keep in anonymous namespace where appropriate

3. **Create `visualizer/visualizer_fft.h`**:
   - Declare FFT-related functions:
     - `computeVisualizerFFT()`
     - `initSpectrumHanningWindow()`
     - `isFFTSilent()`
     - `calculateWeightedMagnitude()`
   - Declare FFT buffer accessors (if needed)

4. **Create `visualizer/visualizer_fft.cpp`**:
   - Move FFT computation code
   - Move FFT buffers (`spectrumFFTInput`, `spectrumFFTOutput`, `spectrumHanningWindow`, `cachedFFT`)
   - Move FFT-related helper functions
   - Keep buffers in anonymous namespace

### Phase 2: Extract Individual Visualizers
1. **Create `visualizer/visualizer_waveform.h`**:
   - Declare `renderVisualizerWaveform()` function
   - Include necessary forward declarations

2. **Create `visualizer/visualizer_waveform.cpp`**:
   - Move `renderVisualizerWaveform()` implementation
   - Include waveform-specific constants
   - No dependencies on FFT code

3. **Create `visualizer/visualizer_spectrum.h`**:
   - Declare `renderVisualizerSpectrum()` function
   - Forward declarations

4. **Create `visualizer/visualizer_spectrum.cpp`**:
   - Move `renderVisualizerSpectrum()` implementation
   - Move `spectrumSmoothedValues` buffer (spectrum-specific)
   - Include FFT headers

5. **Create `visualizer/visualizer_equalizer.h`**:
   - Declare `renderVisualizerEqualizer()` function
   - Declare equalizer-specific helpers:
     - `calculateFrequencyBandRange()`
     - `updateAndDrawPeak()`

6. **Create `visualizer/visualizer_equalizer.cpp`**:
   - Move `renderVisualizerEqualizer()` implementation
   - Move equalizer-specific functions:
     - `calculateFrequencyBandRange()`
     - `updateAndDrawPeak()`
   - Move equalizer-specific buffers:
     - `equalizerSmoothedValues`
     - `equalizerPeakHeights`
     - `equalizerPeakDecay`
     - `kEqualizerFrequencies` constant

### Phase 3: Refactor Main Visualizer Class
1. **Update `visualizer.h`**:
   - Keep public API methods unchanged
   - Remove private method declarations that moved to separate files
   - Add forward declarations as needed
   - Keep `FFTResult` struct (or move to common header if used externally)

2. **Update `visualizer.cpp`**:
   - Keep lifecycle methods (`reset()`, `setEnabled()`, `isEnabled()`)
   - Keep dispatch methods (`renderVisualizer()`, `potentiallyRenderVisualizer()`, `requestVisualizerUpdateIfNeeded()`)
   - Include new header files for individual visualizers
   - `renderVisualizer()` delegates to extracted functions

### Phase 4: Update Includes & Dependencies
1. **Include order**:
   - `visualizer.h` includes `visualizer/visualizer_common.h` if needed
   - Individual visualizer files include their dependencies
   - FFT visualizers include `visualizer/visualizer_fft.h`
   - Common helpers included where needed

2. **Namespace organization**:
   - All code remains in `deluge::hid::display` namespace
   - Shared helpers can be in anonymous namespace in .cpp files
   - Buffers stay in anonymous namespace

## Design Decisions

### 1. Namespace Structure
- All visualizer code stays in `deluge::hid::display` namespace
- Individual visualizer functions can be in the same namespace (not nested classes)
- This maintains backward compatibility with existing calls

### 2. Buffer Management
- FFT buffers remain static in `visualizer_fft.cpp` (shared by Spectrum/Equalizer)
- Spectrum-specific buffers in `visualizer_spectrum.cpp`
- Equalizer-specific buffers in `visualizer_equalizer.cpp`
- No dynamic allocation changes

### 3. Function Visibility
- Individual render functions can remain `static` in their respective files
- Or can be in anonymous namespace
- Main `Visualizer` class methods remain public static

### 4. Forward Declarations
- Use forward declarations to minimize include dependencies
- `FFTResult` struct can stay in `visualizer.h` (public API) or move to common
- Canvas and other types use forward declarations where possible

## File Breakdown

### visualizer.h (minimal changes)
- Public API declarations
- Forward declarations
- `FFTResult` struct (if used in public API)
- Static member variables

### visualizer.cpp (~150 lines)
- Static member variable definitions
- `renderVisualizer()` - dispatch/router
- `potentiallyRenderVisualizer()` - conditional rendering
- `requestVisualizerUpdateIfNeeded()` - update scheduling
- `reset()`, `setEnabled()`, `isEnabled()` - state management

### visualizer/visualizer_common.h
- `FFTResult` struct (if moved from visualizer.h)
- Forward declarations
- Common type definitions

### visualizer/visualizer_common.cpp (~200 lines)
- `getVisualizerReadStartPos()`
- `applyVisualizerCompression()`
- Shared constants (display margins, thresholds, compression parameters)

### visualizer/visualizer_fft.h
- FFT function declarations
- Buffer accessor declarations (if needed)

### visualizer/visualizer_fft.cpp (~200 lines)
- `computeVisualizerFFT()`
- `initSpectrumHanningWindow()`
- `isFFTSilent()`
- `calculateWeightedMagnitude()`
- FFT buffers (anonymous namespace)

### visualizer/visualizer_waveform.h
- `renderVisualizerWaveform()` declaration

### visualizer/visualizer_waveform.cpp (~150 lines)
- `renderVisualizerWaveform()` implementation
- Waveform-specific constants

### visualizer/visualizer_spectrum.h
- `renderVisualizerSpectrum()` declaration

### visualizer/visualizer_spectrum.cpp (~150 lines)
- `renderVisualizerSpectrum()` implementation
- `spectrumSmoothedValues` buffer (anonymous namespace)
- Spectrum-specific constants

### visualizer/visualizer_equalizer.h
- `renderVisualizerEqualizer()` declaration
- `calculateFrequencyBandRange()` declaration
- `updateAndDrawPeak()` declaration

### visualizer/visualizer_equalizer.cpp (~150 lines)
- `renderVisualizerEqualizer()` implementation
- `calculateFrequencyBandRange()` implementation
- `updateAndDrawPeak()` implementation
- Equalizer-specific buffers (anonymous namespace)
- `kEqualizerFrequencies` constant

## Benefits

1. **Maintainability**: Each visualizer type is isolated in its own file
2. **Scalability**: Adding new visualizer types is straightforward
3. **Compilation**: Changes to one visualizer don't require recompiling others
4. **Readability**: Smaller, focused files are easier to understand
5. **Testing**: Individual visualizers can be tested in isolation
6. **Code Organization**: Clear separation of concerns

## Migration Notes

1. **No API Changes**: Public interface remains unchanged
2. **Backward Compatible**: All existing code continues to work
3. **Incremental**: Can be done in phases, testing after each phase
4. **Include Paths**: New files use `visualizer/` subdirectory, includes updated accordingly

## Testing Strategy

1. Test each phase independently
2. Verify all three visualizer types render correctly
3. Ensure no memory leaks or buffer issues
4. Confirm performance characteristics unchanged
5. Test visualizer lifecycle (enable/disable, view switching)

## Future Considerations

- If more visualizer types are added, follow the same pattern
- Consider a factory pattern if visualizer creation becomes complex
- Potential for making visualizers pluggable/configurable in the future

