
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

#include "dx_browser.h"
#include "definitions_cxx.hpp"
#include "gui/menu_item/dx/cartridge.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui_timer_manager.h"
#include "hid/display/oled.h"
#include "util/dx7_converter.h"
#include "util/functions.h"

using namespace deluge::gui;

DxSyxBrowser::DxSyxBrowser() {
	fileIcon = deluge::hid::display::OLED::waveIcon;
	shouldWrapFolderContents = false;
	conversionMode_ = false;
	convertAllStopping_ = false;
}

static char const* allowedFileExtensionsSyx[] = {"SYX", NULL};
bool DxSyxBrowser::opened() {

	bool success = Browser::opened();
	if (!success)
		return false;

	allowedFileExtensions = allowedFileExtensionsSyx;

	allowFoldersSharingNameWithFile = true;
	outputTypeToLoad = OutputType::NONE;
	qwertyVisible = false;

	fileIndexSelected = 0;

	// Set title based on mode
	if (conversionMode_) {
		title = "CONVERT DX7 SYX";
	}
	else {
		title = "LOAD DX7 SYX";
	}

	Error error = StorageManager::initSD();
	if (error != Error::NONE)
		goto sdError;

	currentDir.set("DX7");

	// TODO: fill in last used name!
	error = arrivedInNewFolder(1, "", "DX7");
	if (error != Error::NONE)
		goto sdError;

	return true;
sdError:
	display->displayError(error);
	return false;
}

// TODO: this is identical to SampleBrowser, move to parent class?
Error DxSyxBrowser::getCurrentFilePath(String* path) {
	Error error;

	path->set(&currentDir);
	int oldLength = path->getLength();
	if (oldLength) {
		error = path->concatenateAtPos("/", oldLength);
		if (error != Error::NONE) {
gotError:
			path->clear();
			return error;
		}
	}

	FileItem* currentFileItem = getCurrentFileItem();

	error = path->concatenate(&currentFileItem->filename);
	if (error != Error::NONE)
		goto gotError;

	return Error::NONE;
}

void DxSyxBrowser::enterKeyPress() {
	FileItem* currentFileItem = getCurrentFileItem();
	if (!currentFileItem) {
		return;
	}

	if (currentFileItem->isFolder) {
		// [SIC]
		char const* filenameChars =
		    currentFileItem->filename
		        .get(); // Extremely weirdly, if we try to just put this inside the parentheses in the next line,
		                // it returns an empty string (&nothing). Surely this is a compiler error??

		Error error = goIntoFolder(filenameChars);
		if (error != Error::NONE) {
			display->displayError(error);
			close(); // Don't use goBackToSoundEditor() because that would do a left-scroll
			return;
		}
	}
	else {
		// TODO: c.f. slotbrowser, we might just be able to pass a file pointer to the FAT loader
		String path;
		getCurrentFilePath(&path);

		if (!path.isEmpty()) {
			if (conversionMode_) {
				// Convert the selected syx file - stay on browser page for batch conversion
				deluge::dx7::DX7Converter converter;
				Error error = converter.convertSysexToXML(path.get());
				if (error != Error::NONE) {
					display->displayError(error);
				}
				else {
					// Conversion successful (or already converted), move file to DX7_CONVERTED folder in convert all
					// mode
					if (convertAllMode_) {
						// Create DX7_CONVERTED folder if it doesn't exist
						Error folderError = createFoldersRecursiveIfNotExists("DX7_CONVERTED");
						if (folderError != Error::NONE) {
							display->displayError(folderError);
						}
						else {
							// Get the path relative to DX7 directory
							String relativePath;
							getCurrentFilePath(&relativePath);

							// Remove "DX7/" prefix to get relative path
							const char* relativePathStr = relativePath.get();
							const char dx7_prefix[] = "DX7/";
							const size_t dx7_prefix_len = sizeof(dx7_prefix) - 1;

							const char* relativePathStart = relativePathStr;
							if (relativePath.getLength() >= dx7_prefix_len
							    && strncmp(relativePathStr, dx7_prefix, dx7_prefix_len) == 0) {
								relativePathStart = relativePathStr + dx7_prefix_len;
							}

							// Find the directory part (everything before the last slash)
							const char* lastSlash = nullptr;
							for (const char* p = relativePathStart; *p; ++p) {
								if (*p == '/') {
									lastSlash = p;
								}
							}

							// Create destination directories if needed
							String destDir;
							destDir.set("DX7_CONVERTED");
							if (lastSlash != nullptr) {
								// Add the directory part
								destDir.concatenate("/");
								size_t dirLen = lastSlash - relativePathStart;
								char tempDir[256];
								strncpy(tempDir, relativePathStart, dirLen);
								tempDir[dirLen] = '\0';
								destDir.concatenate(tempDir);

								Error dirError = createFoldersRecursiveIfNotExists(destDir.get());
								if (dirError != Error::NONE) {
									display->displayError(dirError);
									return;
								}
							}

							// Construct full destination path for the file
							String destPath;
							destPath.set("DX7_CONVERTED");
							destPath.concatenate("/");
							destPath.concatenate(relativePathStart);

							// Move the file
							FRESULT result = f_rename(path.get(), destPath.get());
							if (result != FR_OK) {
								display->displayError(fresultToDelugeErrorCode(result));
							}
						}
					}
				}

				// In convert all mode, start timer to automatically advance to next file
				if (convertAllMode_) {
					uiTimerManager.setTimer(TimerName::UI_SPECIFIC, 500); // 500ms delay between conversions
				}
				// Don't close browser - user can continue converting more files
			}
			else {
				// Normal loading behavior - close browser and enter submenu
				close();
				if (menu_item::dxCartridge.tryLoad(path.get())) {
					soundEditor.enterSubmenu(&menu_item::dxCartridge);
				}
			}
		}

		// dx7ui.openFile(path.get());
	}
}

void DxSyxBrowser::close() {
	// Stop any ongoing convert all process - gracefully after current file completes
	if (convertAllMode_) {
		convertAllStopping_ = true;
	}
	else {
		uiTimerManager.unsetTimer(TimerName::UI_SPECIFIC);
		Browser::close();
	}
}

ActionResult DxSyxBrowser::timerCallback() {
	if ((convertAllMode_ || convertAllStopping_) && conversionMode_) {
		// Convert all mode: convert current file, then try to advance to next
		int32_t currentIndex = fileIndexSelected;
		int32_t numFiles = fileItems.getNumElements();

		// If we're stopping, don't advance to next file - just wait for current conversion to complete
		if (convertAllStopping_) {
			// Check if we've reached the end of all available files or are stopping
			// We're done if: current index is at or beyond the last loaded file AND there are no more files to load
			if (currentIndex >= numFiles - 1 && numFileItemsDeletedAtEnd == 0) {
				// Don't reset convertAllMode_ - keep it active until user leaves the browser
				// convertAllMode_ = false;
				convertAllStopping_ = false;
				uiTimerManager.unsetTimer(TimerName::UI_SPECIFIC);
				// Don't close browser - let user continue converting more files
				// Browser::close();
				return ActionResult::DEALT_WITH;
			}

			// If advancing didn't change the file index, we might be stuck (e.g., wrapped to beginning)
			// In that case, stop the conversion but keep convert all mode active
			if (fileIndexSelected == currentIndex) {
				// Don't reset convertAllMode_ - keep it active until user leaves the browser
				// convertAllMode_ = false;
				convertAllStopping_ = false;
				uiTimerManager.unsetTimer(TimerName::UI_SPECIFIC);
				// Don't close browser - let user continue
				// Browser::close();
				return ActionResult::DEALT_WITH;
			}

			// Don't advance to next file when stopping - just wait
			return ActionResult::DEALT_WITH;
		}

		// Convert the currently selected file
		enterKeyPress();

		// Refresh the file list since files may have been moved
		arrivedInNewFolder(0, "", "");

		// Now try to advance to next file
		int32_t oldIndex = fileIndexSelected;
		selectEncoderAction(1);

		// If advancing didn't change the file index, we might be stuck or at the end
		// In that case, stop the conversion but keep convert all mode active
		if (fileIndexSelected == oldIndex) {
			// Don't reset convertAllMode_ - keep it active until user leaves the browser
			// convertAllMode_ = false;
			uiTimerManager.unsetTimer(TimerName::UI_SPECIFIC);
			return ActionResult::DEALT_WITH;
		}

		// Set timer for next conversion (500ms delay between conversions)
		uiTimerManager.setTimer(TimerName::UI_SPECIFIC, 500);
	}

	// Call parent timerCallback for any other functionality
	return QwertyUI::timerCallback();
}

DxSyxBrowser dxBrowser{};
