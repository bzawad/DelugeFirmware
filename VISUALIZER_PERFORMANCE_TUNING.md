# Visualizer Performance Analysis: Branch 1.2 vs 1.3

## Executive Summary

The visualizer framerate issue in branch 1.2 stems from two primary issues:

1. **Frame Rate Mismatch**: The original `kFrameSkip = 2` constant was tuned for branch 1.3's audio scheduling but is inappropriate for branch 1.2's significantly different timing parameters.

2. **CPU Contention** (CRITICAL): Branch 1.2's audio engine runs ~2000 times/second with small buffers (16-24 samples), calling `requestVisualizerUpdateIfNeeded()` each time. Without early-exit optimization, every call executes conditional checks even when not updating, causing CPU overhead that worsens with more active channels. This overhead doesn't exist in branch 1.3 due to larger audio buffers (~128 samples) reducing call frequency.

## Root Cause Analysis

### Audio Engine Scheduling Differences

**Branch 1.3 (Source - Good Performance)**:
```cpp
addRepeatingTask(&(AudioEngine::routine_task), p++, 8/44100., 64/44100., 128/44100.,
                 "audio  routine", RESOURCE_NONE);
```
- Minimum interval: 8 samples (~0.18ms @ 44.1kHz)
- Typical interval: 64 samples (~1.45ms)
- Maximum interval: 128 samples (~2.9ms)

**Branch 1.2 (Current - Poor Performance)**:
```cpp
addRepeatingTask(&(AudioEngine::routine), p++, 0.00001, 16/44100., 24/44100.,
                 "audio  routine");
```
- Minimum interval: 0.00001s (0.01ms)
- Typical interval: 16 samples (~0.36ms)
- Maximum interval: 24 samples (~0.54ms)

### Impact on Visualizer Update Rate

The visualizer uses a frame counter that increments once per audio render call:

```cpp
visualizer_frame_counter++;
if (visualizer_frame_counter >= kFrameSkip) {
    visualizer_frame_counter = 0;
    renderUIsForOled();
}
```

Current setting: `kFrameSkip = 2`

**Effective Update Rates:**

Branch 1.3 (Target ~30fps):
- Audio renders every ~2.9ms (average case: 128 samples)
- Skip 2 renders: ~5.8ms between OLED updates
- Effective rate: ~172 Hz
- **Still faster than target, but within reasonable bounds**

Branch 1.2 (Actual):
- Audio renders every ~0.54ms (average case: 24 samples)
- Skip 2 renders: ~1.08ms between OLED updates
- Effective rate: ~926 Hz
- **WAY too fast - overwhelming the OLED subsystem**

### Why High Update Rate Causes Poor Performance

1. **SPI Transfer Queue Saturation**: The OLED SPI transfer queue (`SPI_TRANSFER_QUEUE_SIZE = 32`) can become backed up with too many pending transfers

2. **OLED Hardware Limitations**: The physical OLED display has refresh rate limits. Attempting to update faster than the hardware can process causes transfers to queue up, creating latency

3. **CPU Overhead**: Excessive OLED update attempts consume CPU cycles that could be used for actual audio processing

4. **Perceived Lag**: When the transfer queue backs up, there's a delay between when rendering occurs and when the display actually updates, creating apparent lag and reduced responsiveness

## Calculation for Target 30fps

To achieve the desired 30fps (~33.3ms per frame):

**Branch 1.2:**
```
Target interval: 33.3ms
Audio render interval: ~0.54ms (24 samples @ 44.1kHz)
Required frame skip: 33.3ms / 0.54ms ≈ 62 renders
```

**Branch 1.3:**
```
Target interval: 33.3ms
Audio render interval: ~2.9ms (128 samples @ 44.1kHz)
Required frame skip: 33.3ms / 2.9ms ≈ 11 renders
```

## Recommended Solutions

### Option 1: Adjust Frame Skip for Branch 1.2 (Simple Fix)

Change `visualizer.h`:
```cpp
// Current:
static constexpr uint32_t kFrameSkip = 2;

// Recommended for branch 1.2:
static constexpr uint32_t kFrameSkip = 60;  // Target ~30fps with 24-sample audio buffers
```

**Pros:**
- Simple one-line change
- Brings performance in line with expectations
- No architecture changes required

**Cons:**
- Hardcoded value still assumes specific audio scheduling
- May need adjustment if audio scheduling changes
- Doesn't adapt to runtime conditions

### Option 2: Time-Based Frame Limiting (Robust Fix)

Replace the simple counter with time-based limiting:

```cpp
// In visualizer.h
static constexpr uint32_t kTargetVisualizerUpdateInterval = 44100 / 30; // ~1470 samples for 30fps
inline static uint32_t last_visualizer_update_time = 0;

// In visualizer.cpp requestVisualizerUpdateIfNeeded()
uint32_t samples_since_last_update = AudioEngine::audioSampleTimer - last_visualizer_update_time;
if (samples_since_last_update >= kTargetVisualizerUpdateInterval) {
    last_visualizer_update_time = AudioEngine::audioSampleTimer;
    renderUIsForOled();
}
```

**Pros:**
- Independent of audio engine scheduling
- Consistent framerate regardless of audio buffer size
- More maintainable across branches
- Adapts automatically to runtime audio scheduling changes

**Cons:**
- Slightly more complex logic
- Requires additional state variable
- Small amount of additional computation per audio render

### Option 3: Hybrid Approach (Balanced Fix)

Combine both approaches - use time-based limiting with a minimum frame skip as a safety valve:

```cpp
static constexpr uint32_t kTargetVisualizerUpdateInterval = 44100 / 30;
static constexpr uint32_t kMinFrameSkip = 2; // Minimum renders between updates

// Check both conditions
visualizer_frame_counter++;
uint32_t samples_since_last_update = AudioEngine::audioSampleTimer - last_visualizer_update_time;

if (visualizer_frame_counter >= kMinFrameSkip &&
    samples_since_last_update >= kTargetVisualizerUpdateInterval) {
    visualizer_frame_counter = 0;
    last_visualizer_update_time = AudioEngine::audioSampleTimer;
    renderUIsForOled();
}
```

**Pros:**
- Best of both worlds
- Prevents excessive updates even with anomalous timing
- Time-accurate framerate
- Fail-safe against rapid updates

**Cons:**
- Most complex solution
- Requires most code changes

## Testing Recommendations

After implementing any solution:

1. **Visual Inspection**: Verify smooth animation in all visualizer modes (Waveform, Spectrum, Cube, Tunnel, etc.)

2. **Performance Testing**: Ensure no audio dropouts or glitching with complex songs

3. **FPS Measurement**: Add temporary logging to verify actual update rate:
   ```cpp
   static uint32_t update_count = 0;
   static uint32_t time_at_last_fps_log = 0;
   update_count++;
   if (AudioEngine::audioSampleTimer - time_at_last_fps_log >= 44100) { // Every second
       // Log: update_count updates in 1 second
       update_count = 0;
       time_at_last_fps_log = AudioEngine::audioSampleTimer;
   }
   ```

4. **Cross-Branch Comparison**: Test the same song on both branches to ensure equivalent visual performance

## Additional Findings

### OLED Subsystem Architecture

- **Transfer Queue**: 32-entry circular buffer for SPI transfers
- **DMA-based**: Uses DMA channel for efficient data transfer
- **Update Pipeline**: `renderUIsForOled()` → `doAnyPendingOLEDRendering()` → `OLED::sendMainImage()` → `enqueueSPITransfer()`
- **Rate Limiting**: `doAnyPendingUIRendering()` is scheduled at 0.01s intervals (~100Hz), but this doesn't directly limit visualizer updates since multiple updates can queue within one interval

### Audio Engine Differences Summary

The audio engine in branch 1.2 uses much more aggressive timing to reduce latency:
- **1.2**: Optimized for minimal latency with frequent small renders
- **1.3**: Balanced approach with larger buffers for efficiency

This difference affects all systems that hook into the audio render callback, not just the visualizer.

## Conclusion

The performance degradation stems from **two issues**:

1. **Configuration mismatch**: The visualizer's frame skip constant was tuned for branch 1.3's audio scheduling parameters, not branch 1.2's different timing.

2. **CPU contention** (CRITICAL): Without early-exit optimization, `requestVisualizerUpdateIfNeeded()` executes full conditional logic ~2000 times/second in branch 1.2, creating CPU overhead that intensifies with more active channels.

**Implemented Solution**:
- ✅ Time-Based Frame Limiting (Option 2) - provides consistent 30fps independent of audio scheduling
- ✅ Early-Exit Optimization - minimizes CPU overhead by checking time first, exiting immediately if not ready to update
- ✅ Removed FPS debug counter - eliminates unnecessary overhead on embedded system

These optimizations address both the frame rate issue and CPU contention, especially critical when multiple channels are playing.

## Files Involved

- `src/deluge/hid/display/visualizer.h` - Frame skip constant definition (line 286: `kFrameSkip = 2`)
- `src/deluge/hid/display/visualizer.cpp` - Frame limiting logic in `requestVisualizerUpdateIfNeeded()` (lines 345-349)
- `src/deluge/deluge.cpp` - Audio engine task scheduling parameters (line 578)
- `src/deluge/processing/engines/audio_engine.cpp` - Audio render routine that calls visualizer (line 627)
- `src/deluge/gui/ui/ui.cpp` - OLED rendering pipeline (`doAnyPendingOLEDRendering()`)
- `src/deluge/drivers/oled/oled.c` - Low-level SPI transfer queue (32-entry circular buffer)

---

## Quick Reference: Implementation Cheat Sheet

### Problem
Visualizer runs at ~926 Hz in branch 1.2 instead of target 30 fps, overwhelming OLED subsystem.

### Root Cause
`kFrameSkip = 2` was tuned for branch 1.3's larger audio buffers (~128 samples), but branch 1.2 uses much smaller buffers (~24 samples), causing 31x more frequent visualizer updates.

### Quick Fix (Option 1 - Simple)
**File**: `src/deluge/hid/display/visualizer.h:286`
```cpp
// Change from:
static constexpr uint32_t kFrameSkip = 2;

// To:
static constexpr uint32_t kFrameSkip = 60;  // Tuned for branch 1.2's ~24 sample buffers
```

### Robust Fix (Option 2 - IMPLEMENTED ✅)
**File**: `src/deluge/hid/display/visualizer.h`
```cpp
// Add new constant and variable:
static constexpr uint32_t kTargetVisualizerUpdateInterval = 44100 / 30;  // ~1470 samples
inline static uint32_t last_visualizer_update_time = 0;
```

**File**: `src/deluge/hid/display/visualizer.cpp` - Beginning of `requestVisualizerUpdateIfNeeded()`
```cpp
// CRITICAL: Early exit optimization - check time FIRST before any other logic
// This is essential for branch 1.2 where audio thread calls this ~2000 times/second
uint32_t samples_since_last_update = AudioEngine::audioSampleTimer - last_visualizer_update_time;
if (samples_since_last_update < kTargetVisualizerUpdateInterval) {
    return; // Not time to update - exit immediately with minimal overhead
}

// ... rest of function (only executes ~30 times/second now)
```

**Key Optimization**: By checking time at the beginning, 99% of calls (~1970/2000 per second) exit immediately with just:
- 1 subtraction
- 1 comparison
- 1 return

This minimal overhead prevents CPU contention even when many channels are playing.

