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

#include "visualizer_midi_piano_roll.h"
#include "RZA1/cpu_specific.h" // For OLED_MAIN_WIDTH_PIXELS, OLED_MAIN_HEIGHT_PIXELS, OLED_MAIN_TOPMOST_PIXEL
#include "definitions_cxx.hpp" // For kMaxMIDIValue
#include "extern.h"            // For AudioEngine
#include "hid/display/oled.h"
#include "hid/display/oled_canvas/canvas.h"
#include "hid/display/visualizer.h"
#include "model/clip/clip.h"
#include "model/instrument/midi_instrument.h"
#include "playback/playback_handler.h"
#include <algorithm>
#include <array>

namespace deluge::hid::display {

// -----------------------------------------------------
// MIDI Note Tracking Structures
// -----------------------------------------------------

/// State for tracking individual notes with their durations
struct ActiveNote {
	uint8_t note;       // MIDI note number (0-127)
	uint32_t startTime; // When the note started (frame counter)
	uint32_t endTime;   // When the note ended (0 if still active)
};

// -----------------------------------------------------
// Constants
// -----------------------------------------------------

// Display dimensions for visualizer area (excluding topmost pixels)
constexpr int32_t kOLEDWidth = OLED_MAIN_WIDTH_PIXELS;
constexpr int32_t kOLEDHeight = OLED_MAIN_HEIGHT_PIXELS - OLED_MAIN_TOPMOST_PIXEL;
constexpr int32_t kOLEDHeightMinus1 = kOLEDHeight - 1;

// Time-based scrolling constants
// At 44.1kHz sample rate, target ~30 FPS scrolling at 100 BPM: 44100 / 30 ≈ 1470 samples per frame
constexpr uint32_t kMinTimeBetweenFramesAt100BPM = 1470;
constexpr float kReferenceBPM = 100.0f; // Reference BPM for tempo calculations

// Maximum simultaneous notes to track (limited by memory and performance constraints)
// This limit prevents excessive memory usage while allowing for complex polyphonic music.
// Value of 32 chosen as a reasonable upper bound for:
// - Memory: 32 notes * 9 bytes each = ~288 bytes (very reasonable for embedded system)
// - Polyphony: Most musical contexts rarely exceed 16-32 simultaneous notes
// - Performance: Linear search through 32 notes is fast enough for real-time operation
// - Display: OLED width of 128 pixels could theoretically show 128 notes, but 32 is practical
constexpr size_t kMaxActiveNotes = 32;

namespace {
// Static state for MIDI piano roll visualizer
struct MidiPianoRollState {
	std::array<ActiveNote, kMaxActiveNotes> activeNotes;
	size_t activeNotesCount = 0; // Number of currently active notes
	uint32_t frameCounter = 0;
	uint32_t lastRenderTime = 0;  // Last time the visualizer was rendered (audio sample timer)
	uint32_t accumulatedTime = 0; // Accumulated time for consistent timing
	bool initialized = false;
	bool isActive = false;
};

// State instance
static MidiPianoRollState state;
} // namespace

// -----------------------------------------------------
// Helper Functions
// -----------------------------------------------------

/// Reset the MIDI piano roll visualizer state (called when visualizer becomes active)
void resetMidiPianoRoll() {
	// Clear all active notes
	state.activeNotesCount = 0;

	// Reset frame counter and timing
	state.frameCounter = 0;
	state.lastRenderTime = AudioEngine::audioSampleTimer;
	state.accumulatedTime = 0;
	state.initialized = true;
	state.isActive = true;
}

/// Initialize the MIDI piano roll visualizer
void initializeMidiPianoRoll() {
	if (state.initialized) {
		return;
	}

	resetMidiPianoRoll();
}

/// Clean up old notes that have scrolled completely off screen
void cleanupOldNotes() {
	// Remove notes that have completely scrolled off the bottom of the screen
	// A note is off-screen if its end position (bottom of the note) has scrolled past the screen height

	// Compact the array by moving valid notes to the front
	size_t writeIndex = 0;
	for (size_t readIndex = 0; readIndex < state.activeNotesCount; ++readIndex) {
		const ActiveNote& note = state.activeNotes[readIndex];
		bool keepNote = true;

		// Calculate where this note would end on screen
		uint32_t endTime = note.endTime > 0 ? note.endTime : state.frameCounter;
		int32_t endY = static_cast<int32_t>(state.frameCounter - endTime);

		// Remove note if it has completely scrolled off screen (endY > screen height)
		if (endY > kOLEDHeightMinus1) {
			keepNote = false;
		}

		if (keepNote) {
			// Keep this note
			if (writeIndex != readIndex) {
				state.activeNotes[writeIndex] = note;
			}
			writeIndex++;
		}
	}
	state.activeNotesCount = writeIndex;
}

// -----------------------------------------------------
// MIDI Note Tracking Functions
// -----------------------------------------------------

/// Called when a MIDI note is sent or received (hooks into midiEngine.sendNote() and input processing)
/// @param note MIDI note number (0-127)
/// @param on true for note-on, false for note-off
/// @param velocity Note velocity
/// @param channel MIDI channel (0-15, or -1 for any channel in global mode)
/// @param visualizerActive true if MIDI piano roll visualizer is currently active
/// @param isInput true if this is input MIDI, false if output MIDI
void midiPianoRollNoteEvent(uint8_t note, bool on, uint8_t velocity, int32_t channel, bool visualizerActive,
                            bool isInput) {
	// Comprehensive bounds checking for MIDI note values
	if (note > kMaxMIDIValue) {
		return; // Invalid note number (MIDI notes are 0-127)
	}

	// Validate velocity range (should be 0-127 for note-off, 1-127 for note-on)
	if (on && (velocity == 0 || velocity > kMaxMIDIValue)) {
		return; // Invalid velocity for note-on
	}
	if (!on && velocity > kMaxMIDIValue) {
		return; // Invalid velocity for note-off
	}

	// Channel filtering for clip-level visualizer
	// When viewing a MIDI instrument clip, only show notes from that instrument's channel
	Clip* currentClip = Visualizer::getCurrentClipForVisualizer();
	if (currentClip && currentClip->type == ClipType::INSTRUMENT && currentClip->output
	    && currentClip->output->type == OutputType::MIDI_OUT && Visualizer::visualizer_toggle_enabled) {
		MIDIInstrument* midiInstrument = static_cast<MIDIInstrument*>(currentClip->output);
		int32_t clipChannel = midiInstrument->getChannel();
		// For MPE instruments, don't filter (show all notes for that instrument)
		// For regular MIDI instruments, only show notes on the instrument's channel
		if (clipChannel >= 0 && clipChannel <= 15 && channel != clipChannel) {
			return; // Note is not from the current clip's instrument channel
		}
	}

	// Update active state
	bool wasActive = state.isActive;
	state.isActive = visualizerActive;

	// Only process note events when visualizer is active to avoid wasting memory
	if (!visualizerActive) {
		// Clear state when visualizer becomes inactive to free memory
		if (wasActive && !visualizerActive) {
			resetMidiPianoRoll();
		}
		return;
	}

	// Update MIDI activity timer for silence detection
	Visualizer::midi_piano_roll_last_note_time = AudioEngine::audioSampleTimer;

	// Process the note event
	if (on) {
		// Note-on: check if this pitch is already active, if so retrigger it
		bool foundExisting = false;
		for (size_t i = 0; i < state.activeNotesCount; ++i) {
			if (state.activeNotes[i].note == note && state.activeNotes[i].endTime == 0) {
				// Retrigger existing note - update its start time
				state.activeNotes[i].startTime = state.frameCounter;
				foundExisting = true;
				break;
			}
		}

		// If not already active, create a new note (if we have space)
		if (!foundExisting && state.activeNotesCount < kMaxActiveNotes) {
			ActiveNote newNote{.note = note, .startTime = state.frameCounter, .endTime = 0};
			state.activeNotes[state.activeNotesCount] = newNote;
			state.activeNotesCount++;
		}
	}
	else {
		// Note-off: find and end the most recent active note for this pitch
		for (size_t i = state.activeNotesCount; i > 0; --i) {
			size_t index = i - 1;
			if (state.activeNotes[index].note == note && state.activeNotes[index].endTime == 0) { // Still active
				state.activeNotes[index].endTime = state.frameCounter;
				break; // Only end the most recent active note
			}
		}
	}
}

// -----------------------------------------------------
// Rendering Functions
// -----------------------------------------------------

/// Render the MIDI piano roll visualization
/// @param canvas The OLED canvas to render to
void renderVisualizerMidiPianoRollInternal(oled_canvas::Canvas& canvas) {
	// Initialize on first call
	initializeMidiPianoRoll();

	// Mark visualizer as active (since render function is only called when active)
	state.isActive = true;

	// Calculate tempo-based scrolling speed
	float bpm = playbackHandler.calculateBPMForDisplay();
	bpm = std::max(bpm, 1.0f);               // Prevent division by zero
	float tempo_ratio = kReferenceBPM / bpm; // Higher BPM = faster scrolling (smaller ratio)
	uint32_t minTimeBetweenFrames = static_cast<uint32_t>(kMinTimeBetweenFramesAt100BPM * tempo_ratio);

	// Accumulate time for consistent scrolling speed regardless of rendering frequency
	uint32_t currentTime = AudioEngine::audioSampleTimer;
	uint32_t timeDiff = currentTime - state.lastRenderTime;
	state.accumulatedTime += timeDiff;
	state.lastRenderTime = currentTime;

	// Increment frame counter by the number of frames that should have passed
	uint32_t framesToIncrement = state.accumulatedTime / minTimeBetweenFrames;
	if (framesToIncrement > 0) {
		state.frameCounter += framesToIncrement;
		state.accumulatedTime -= framesToIncrement * minTimeBetweenFrames;
	}

	// Clean up old notes that have scrolled off screen
	cleanupOldNotes();

	// Clear the canvas
	canvas.clear();

	// Draw all active notes
	for (size_t i = 0; i < state.activeNotesCount; ++i) {
		const auto& note = state.activeNotes[i];
		// Calculate Y positions for this note's duration
		uint32_t endTime = note.endTime > 0 ? note.endTime : state.frameCounter;

		// Calculate screen positions relative to current time
		// Y=0 is the top (most recent), higher Y values are older
		int32_t startY = state.frameCounter - note.startTime;
		int32_t endY = state.frameCounter - endTime;

		// Clamp to visible screen area
		int32_t drawStartY = std::max(static_cast<int32_t>(0), endY); // Start from where note ends (or 0)
		int32_t drawEndY = std::min(kOLEDHeightMinus1, startY);       // End where note starts

		// Only draw if there's something visible
		if (drawStartY <= drawEndY) {
			// Map note to X position (MIDI notes 0-127 map 1:1 to OLED pixels 0-127)
			int32_t x = note.note;
			if (x >= 0 && x < kOLEDWidth) {
				// Draw horizontal line for the note duration with bounds checking
				int32_t clampedStartY = std::max(static_cast<int32_t>(0), drawStartY);
				int32_t clampedEndY = std::min(kOLEDHeightMinus1, drawEndY);
				for (int32_t y = clampedStartY; y <= clampedEndY; y++) {
					canvas.drawPixel(x, y);
				}
			}
		}
	}
}

// -----------------------------------------------------
// Public Interface
// -----------------------------------------------------

/// Render MIDI piano roll visualizer on OLED display
/// @param canvas The OLED canvas to render to
void renderVisualizerMidiPianoRoll(oled_canvas::Canvas& canvas) {
	renderVisualizerMidiPianoRollInternal(canvas);

	// Mark OLED as changed following established visualizer pattern
	OLED::markChanged();
}

} // namespace deluge::hid::display
