/*
 * Copyright © 2015-2023 Synthstrom Audible Limited
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

#include "cartridge.h"
#include "dsp/dx/engine.h"
#include "fatfs.hpp"
#include "gui/ui/browser/dx_browser.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui_timer_manager.h"
#include "hid/display/display.h"
#include "memory/allocate_unique.h"
#include "memory/sdram_allocator.h"
#include "model/song/song.h"
#include "processing/sound/sound.h"
#include "processing/source.h"
#include "storage/DX7Cartridge.h"
#include "util/functions.h"
#include "util/try.h"
#include <etl/vector.h>
#include <memory>

static bool openFile(std::string_view path, DX7Cartridge* data) {
	using namespace deluge;
	using enum deluge::l10n::String;
	constexpr size_t minSize = kSmallSysexSize;

	FatFS::FileInfo fno = D_TRY_CATCH(FatFS::stat(path), error, {
		return false; // fail quickly if file doesn't exist
	});

	FSIZE_t filesize = fno.fsize;
	if (filesize < minSize) {
		display->displayPopup(l10n::get(STRING_FOR_DX_ERROR_FILE_TOO_SMALL));
	}

	// Open the file
	FatFS::File file = D_TRY_CATCH(FatFS::File::open(path, FA_READ), error, {
		display->displayPopup(l10n::get(STRING_FOR_DX_ERROR_READ_ERROR));
		return false;
	});

	l10n::String error = EMPTY_STRING;
	int readsize = std::min((int)filesize, 8192);

	std::unique_ptr buffer = D_TRY_CATCH_MOVE((allocate_unique<std::byte, memory::sdram_allocator>(readsize)), error, {
		display->displayPopup(l10n::get(STRING_FOR_DX_ERROR_READ_ERROR));
		return false;
	});

	std::span<std::byte> readbuffer = D_TRY_CATCH(file.read({buffer.get(), filesize}), error, {
		return false; //
	});

	// Check for basic file issues that indicate poor data quality
	bool fileTooSmall = (readbuffer.size() < minSize);
	bool notSysexFormat = (readbuffer.size() >= 1 && readbuffer[0] != std::byte{0xF0});

	// Try to load the data (now always succeeds, using defaults for missing data)
	error = data->load(readbuffer);
	if (error != EMPTY_STRING) {
		display->displayPopup(l10n::get(error), 3);
		return false;
	}

	// Show warnings for data quality issues
	if (fileTooSmall) {
		display->displayPopup(l10n::get(STRING_FOR_DX_ERROR_FILE_TOO_SMALL), 3);
	}
	else if (notSysexFormat) {
		display->displayPopup(l10n::get(STRING_FOR_DX_ERROR_NO_SYSEX_START), 3);
	}
	else {
		// For valid SYSEX format, check for checksum and ID issues
		size_t expectedMsgSize = 0;
		size_t i;
		for (i = 0; i < readbuffer.size(); ++i) {
			if (readbuffer[i] == std::byte{0xF7}) {
				expectedMsgSize = i + 1;
				break;
			}
		}

		if (expectedMsgSize > 0) {
			size_t dataSize = (expectedMsgSize == kSysexSize) ? 4096 : 155;
			bool hasChecksumError =
			    (expectedMsgSize <= readbuffer.size()
			     && sysexChecksum({&data->getRawVoice()[0], dataSize}) != readbuffer[expectedMsgSize - 2]);

			const std::byte* voiceData = data->getRawVoice() - 6;
			bool hasSysexIdError =
			    (voiceData[1] != std::byte{67} || (voiceData[3] != std::byte{9} && voiceData[3] != std::byte{0}));

			if (hasChecksumError) {
				display->displayPopup(l10n::get(deluge::l10n::String::STRING_FOR_DX_ERROR_CHECKSUM_FAIL), 3);
			}
			else if (hasSysexIdError) {
				display->displayPopup(l10n::get(deluge::l10n::String::STRING_FOR_DX_ERROR_SYSEX_ID), 3);
			}
		}
	}

	return true;
}
namespace deluge::gui::menu_item {

DxCartridge dxCartridge{l10n::String::STRING_FOR_DX_CARTRIDGE};

void DxCartridge::beginSession(MenuItem* navigatedBackwardFrom) {
	readValueAgain();
}

void DxCartridge::readValueAgain() {
	if (pd == nullptr) {
		return;
	}
	if (display->haveOLED()) {
		renderUIsForOled();
	}
	else {
		drawValue();
	}

	DxPatch* patch = soundEditor.currentSource->ensureDxPatch();
	pd->unpackProgram(patch->params, currentValue);
	soundEditor.currentSound->killAllVoices();
	Instrument* instrument = getCurrentInstrument();
	if (instrument->type == OutputType::SYNTH && !instrument->existsOnCard) {
		char name[11];
		pd->getProgramName(currentValue, name);
		if (name[0] != 0) {
			instrument->name.set(name);
		}
	}
}

void DxCartridge::drawPixelsForOled() {
	if (pd == nullptr) {
		return;
	}
	char names[32][11];
	pd->getProgramNames(names);

	etl::vector<std::string_view, 32> itemNames = {};
	for (int i = 0; i < pd->numPatches(); i++) {
		itemNames.push_back(names[i]);
	}
	drawItemsForOled(itemNames, currentValue - scrollPos, scrollPos);
}

void DxCartridge::drawValue() {
	char names[32][11];
	pd->getProgramNames(names);

	display->setScrollingText(names[currentValue]);
}

bool DxCartridge::tryLoad(std::string_view path) {
	if (pd == nullptr) {
		pd = new DX7Cartridge();
	}
	currentValue = 0;
	scrollPos = 0;

	return openFile(path, pd);
}

void DxCartridge::selectEncoderAction(int32_t offset) {
	int32_t newValue = currentValue + offset;
	int32_t numValues = pd->numPatches();

	if (display->haveOLED()) {
		if (newValue >= numValues || newValue < 0) {
			return;
		}
	}
	else {
		if (newValue >= numValues) {
			newValue %= numValues;
		}
		else if (newValue < 0) {
			newValue = (newValue % numValues + numValues) % numValues;
		}
	}

	currentValue = newValue;

	if (display->haveOLED()) {
		if (currentValue < scrollPos) {
			scrollPos = currentValue;
		}
		else if (currentValue >= scrollPos + kOLEDMenuNumOptionsVisible) {
			scrollPos++;
		}
	}

	readValueAgain();
}

MenuItem* DxCartridge::selectButtonPress() {
	soundEditor.exitCompletely();
	return nullptr;
}

} // namespace deluge::gui::menu_item
