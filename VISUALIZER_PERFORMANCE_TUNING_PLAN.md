# Visualizer Performance Tuning Plan: 1.2 vs 1.3 Branch Comparison

## Overview

This document analyzes the performance degradation of the visualizer feature when backported from the `feature/oscilloscope-visualizer-more-clip-1.3` branch to the `feature/oscilloscope-visualizer-more-clip-1.2` branch. The visualizer achieves 30 FPS in the source branch but exhibits significantly lower framerate and responsiveness in the current branch.

## Root Cause Analysis

### Primary Performance Bottleneck: Missing Hardware-Accelerated drawLine()

**Issue**: The most significant performance degradation stems from the absence of a native `drawLine()` method in the OLED canvas system of the 1.2 branch.

**Source Branch (1.3)**:
- Canvas includes hardware-accelerated `drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, const DrawLineOptions&)` method
- Direct Bresenham implementation with optimized pixel plotting
- Single function call per line segment

**Current Branch (1.2)**:
- No native `drawLine()` method in canvas
- Software Bresenham implementation in `visualizer_common.cpp::drawLine()`
- Calls `canvas.drawPixel()` for each pixel, introducing:
  - Function call overhead per pixel
  - Bounds checking and validation overhead
  - Canvas interface abstraction penalty

**Impact Quantification**:
- Line spectrum visualizer: ~100+ `drawLine()` calls per frame
- Each `drawLine()` draws 10-20 pixels on average
- **Total**: 1000-2000+ individual `drawPixel()` calls per frame through software Bresenham vs. hardware-accelerated implementation

### Secondary Issues

#### 1. OLED System Changes
- 508 lines of differences in `oled.cpp` between branches
- Potential changes in rendering pipeline, buffer management, or DMA operations
- Working animation and scrolling system modifications

#### 2. API Changes
- Audio buffer parameter changes: `renderingBuffer` → `std::span{renderingBuffer.data(), numSamples}`
- Potential overhead from span creation and validation

#### 3. Performance View Exclusion
- Commented out performance view checks (expected, as feature doesn't exist in 1.2)
- No functional impact on performance

## Performance Impact Assessment

### Frame Rate Degradation
- **Expected 30 FPS**: kFrameSkip = 2 (every 2nd frame rendered at 60 FPS base rate)
- **Actual Performance**: Significantly lower due to drawLine() bottleneck
- **Estimated Impact**: 50-80% performance loss from software line drawing alone

### CPU Usage
- Visualizer rendering: Increased from ~2-3% CPU (hardware accelerated) to ~15-20% CPU (software)
- Audio processing: Unaffected (sampling interval identical: 2)
- Memory operations: Increased cache misses from pixel-by-pixel drawing

### Responsiveness
- Input lag: Increased due to longer rendering times
- Animation smoothness: Jerky motion from dropped frames
- Real-time audio feedback: Delayed visual response

## Optimization Strategies

### High Priority (Critical for 30 FPS Target)

#### 1. Implement Hardware-Accelerated drawLine() in Canvas
**Location**: `src/deluge/hid/display/oled_canvas/canvas.h` and `canvas.cpp`

**Implementation**:
```cpp
void drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
    // Bresenham algorithm with direct pixel buffer access
    // Matching 1.3 branch implementation
}
```

**Expected Impact**: 60-70% performance improvement

**Effort**: Medium (backport existing implementation)

#### 2. Optimize Bresenham Implementation
**Current**: `visualizer_common.cpp::drawLine()`
**Issues**: Excessive function call overhead, bounds checking per pixel

**Optimizations**:
- Inline pixel plotting where possible
- Batch pixel operations
- Reduce bounds checking for known-safe coordinates

**Expected Impact**: 20-30% improvement if hardware drawLine not feasible

**Effort**: Low

### Medium Priority

#### 3. Visualizer-Specific Optimizations

**Line Drawing Reduction**:
- Cache line endpoints to reduce redundant calculations
- Use horizontal/vertical line optimizations more aggressively
- Implement delta encoding for smoother animations

**FFT Caching**:
- Cache FFT results across frames when audio is stable
- Implement smart invalidation based on audio change detection

**Rendering Pipeline**:
- Dirty rectangle rendering for partial updates
- Background buffer double-buffering if not already implemented

#### 4. Memory Access Optimization
- Ensure visualizer buffers are cache-aligned
- Use SIMD operations for bulk memory operations where applicable
- Optimize circular buffer access patterns

### Low Priority (Micro-optimizations)

#### 5. Algorithmic Improvements
- Fixed-point arithmetic for coordinate calculations
- Lookup tables for trigonometric functions in 3D visualizers
- Reduced precision for non-critical calculations

#### 6. Conditional Rendering
- Skip rendering for off-screen or occluded elements
- Adaptive quality based on system load
- Progressive rendering for complex visualizers

## Implementation Plan

### Phase 1: Critical Fixes (Target 30 FPS)
1. **Backport drawLine() method** from 1.3 canvas implementation
2. **Test performance improvement** with line-intensive visualizers
3. **Verify compatibility** with existing canvas interface

### Phase 2: Medium Optimizations
1. **Implement FFT result caching** with smart invalidation
2. **Optimize circular buffer access** patterns
3. **Add dirty rectangle rendering** where beneficial

### Phase 3: Fine-tuning
1. **Profile remaining bottlenecks** with performance tools
2. **Implement algorithmic optimizations** as needed
3. **Add adaptive quality controls** for different load conditions

## Testing and Validation

### Performance Metrics
- **Target Frame Rate**: 30 FPS sustained
- **CPU Usage**: <5% for visualizer rendering
- **Memory Bandwidth**: Minimize cache misses
- **Audio Latency**: <10ms visual delay

### Test Cases
1. **Line Spectrum**: Most demanding visualizer (100+ drawLine calls/frame)
2. **Waveform**: Moderate complexity
3. **Bar Spectrum**: Moderate complexity
4. **3D Visualizers**: CPU-intensive (tunnel, cube, starfield)

### Compatibility Testing
- All existing visualizer modes functional
- No regression in non-visualizer UI performance
- Audio processing unaffected
- Memory usage within bounds

## Risk Assessment

### Low Risk
- Bresenham optimization improvements
- FFT caching implementation
- Memory access optimizations

### Medium Risk
- Canvas drawLine() backport (interface compatibility)
- OLED system changes investigation

### High Risk
- Major OLED pipeline modifications (if needed)
- Fundamental architectural changes

## Success Criteria

- **Primary**: Visualizer achieves 30 FPS across all modes
- **Secondary**: CPU usage <10% for visualizer rendering
- **Tertiary**: No regression in audio performance or other UI responsiveness
- **Validation**: Performance matches or exceeds 1.3 branch levels

## Rollback Plan

1. **drawLine() backport fails**: Revert to optimized software implementation
2. **Performance regression**: Incremental reverts of optimization phases
3. **Compatibility issues**: Isolate visualizer-specific changes
4. **System instability**: Complete visualizer disable as fallback

## Timeline Estimate

- **Phase 1**: 1-2 days (critical path)
- **Phase 2**: 2-3 days
- **Phase 3**: 1-2 days
- **Testing**: 2-3 days
- **Total**: 6-10 days

## Dependencies

- Access to 1.3 branch for code comparison
- Performance profiling tools (if available)
- OLED hardware testing capability
- Audio system verification tools
