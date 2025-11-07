# Code Review: Oscilloscope Visualizer Feature

## Summary of Changes

The staged changes implement improvements to the visualizer system with separate frame counters for different visualizer modes, increased buffer size, modified sampling strategy, and updated visual compression parameters.

## Issues Found

### 1. Unnecessary Change-Explanation Comments

Several comments explain *why* changes were made rather than *what* the code does. These should be removed or rephrased to focus on functionality.

**visualizer.cpp:**
- Line ~154: `// Sample every 2nd sample to get ~22k samples/sec for better responsiveness (slight CPU increase)` - Explains why the change was made
- Line ~158: `// Take the most recent sample from the end of the buffer for minimum latency` - Explains the rationale

**visualizer_common.cpp:**
- Line ~29: `// 2:1 visual compression (square root)` - This is okay as it describes what the setting does
- Line ~41: `// Buffer is full, start reading from the most recent samples (just before writePos)` - This explains implementation details

**visualizer_waveform.cpp:**
- Line ~69: `// If all samples are very small, skip display update to avoid flicker` - Explains why, not what
- Line ~123: `// Apply 2:1 visual compression to match spectrum/equalizer visualizers` - Explains why the change was made
- Line ~127: `// Using fixed reference means actual signal levels are displayed, not auto-scaled` - Explains design decision

### 2. Inconsistent Comment Style

Some comments are overly verbose about implementation rationale rather than describing current behavior.

### 3. Potential Performance Comments

The frame rate logic comments could be simplified to just state the current framerate settings without historical context.

## Recommended Fixes

1. **Remove change-rationale comments** and replace with descriptive comments of current functionality
2. **Simplify frame rate comments** to just indicate current settings
3. **Focus comments on "what" not "why"** - let the code speak for itself

## Specific Comment Replacements Needed

**visualizer.cpp:**
- Replace `// Sample every 2nd sample to get ~22k samples/sec for better responsiveness (slight CPU increase)` with `// Sample every 2nd sample for audio capture`

- Replace `// Take the most recent sample from the end of the buffer for minimum latency` with `// Use most recent sample from buffer end`

**visualizer_waveform.cpp:**
- Replace `// If all samples are very small, skip display update to avoid flicker` with `// Skip display update during silence`
- Replace `// Apply 2:1 visual compression to match spectrum/equalizer visualizers` with `// Apply 2:1 visual compression to amplitude`
- Remove or simplify `// Using fixed reference means actual signal levels are displayed, not auto-scaled`

**visualizer_common.cpp:**
- The compression comments are acceptable as they describe what the algorithm does

## Code Quality Assessment

The code changes appear functionally sound with appropriate updates to:
- Buffer management for different visualizer modes
- Sampling strategy for better responsiveness
- Visual consistency across visualizer types
- Memory usage (buffer size increase)

All changes maintain backward compatibility and follow existing code patterns.
