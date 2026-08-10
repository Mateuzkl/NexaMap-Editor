#include "main.h"

#include "sprite_appearances.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <limits>
#include <lzma.h>

namespace {
	constexpr size_t SpriteSheetWidth = 384;
	constexpr size_t SpriteSheetHeight = 384;
	constexpr size_t BytesPerPixel = 4;
	constexpr size_t PixelBytes = SpriteSheetWidth * SpriteSheetHeight * BytesPerPixel;
	constexpr size_t DecodeBufferBytes = PixelBytes + 256;
	constexpr size_t MaximumResidentSheets = 64;

	bool ReadByte(const std::vector<uint8_t>& input, size_t& offset, uint8_t& value) {
		if (offset >= input.size()) {
			return false;
		}
		value = input[offset++];
		return true;
	}
}

SpriteAppearances g_spriteAppearances;

ClientSpriteSheet::ClientSpriteSheet(const ClientSpriteSheetManifest& manifest) :
	firstSpriteId(manifest.firstSpriteId),
	lastSpriteId(manifest.lastSpriteId),
	layout(static_cast<ClientSpriteLayout>(manifest.spriteType)),
	file(manifest.file) {
}

ClientSpriteSize ClientSpriteSheet::getSpriteSize() const noexcept {
	switch (layout) {
		case ClientSpriteLayout::OneByTwo:
			return { 32, 64 };
		case ClientSpriteLayout::TwoByOne:
			return { 64, 32 };
		case ClientSpriteLayout::TwoByTwo:
			return { 64, 64 };
		case ClientSpriteLayout::ThreeByThree:
			return { 96, 96 };
		case ClientSpriteLayout::FourByFour:
			return { 128, 128 };
		case ClientSpriteLayout::FiveByFive:
			return { 160, 160 };
		case ClientSpriteLayout::OneByOne:
		default:
			return { 32, 32 };
	}
}

SpriteAppearances::~SpriteAppearances() {
	unload();
}

void SpriteAppearances::init() {
	unload();
	sheets.reserve(4000);
	startWorker();
}

void SpriteAppearances::unload() {
	stopWorker();
	sheets.clear();
	accessCounter = 0;
}

bool SpriteAppearances::loadCatalog(const ClientAssetsManifest& manifest, wxString& error, wxArrayString& warnings) {
	init();
	for (const ClientSpriteSheetManifest& entry : manifest.spriteSheets) {
		sheets.push_back(std::make_shared<ClientSpriteSheet>(entry));
	}
	if (sheets.empty()) {
		error = "The client catalog does not contain sprite sheets.";
		return false;
	}
	for (const std::string& warning : manifest.warnings) {
		warnings.push_back(wxstr(warning));
	}
	return true;
}

ClientSpriteSheetPtr SpriteAppearances::getSheetBySpriteId(uint32_t spriteId) const {
	const auto iterator = std::lower_bound(
		sheets.begin(),
		sheets.end(),
		spriteId,
		[](const ClientSpriteSheetPtr& sheet, uint32_t id) {
			return sheet->lastSpriteId < id;
		}
	);
	if (iterator == sheets.end() || spriteId < (*iterator)->firstSpriteId) {
		return nullptr;
	}
	return *iterator;
}

bool SpriteAppearances::decodeSpriteSheet(const ClientSpriteSheetPtr& sheet, std::unique_ptr<uint8_t[]>& output, wxString& error) const {
	if (!sheet) {
		error = "Cannot load a null Canary/Crystal sprite sheet.";
		return false;
	}

	std::ifstream stream(sheet->file, std::ios::in | std::ios::binary);
	if (!stream.is_open()) {
		error = "Could not open sprite sheet: " + wxstr(sheet->file.string());
		return false;
	}
	const std::vector<uint8_t> input((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
	if (input.size() < 32) {
		error = "Sprite sheet is truncated: " + wxstr(sheet->file.string());
		return false;
	}

	size_t offset = 0;
	while (offset < input.size() && input[offset] == 0) {
		++offset;
	}
	if (offset + 5 > input.size() || input[offset] != 0x70 || input[offset + 1] != 0x0A || input[offset + 2] != 0xFA || input[offset + 3] != 0x80 || input[offset + 4] != 0x24) {
		error = "Sprite sheet has an invalid Canary/Crystal header: " + wxstr(sheet->file.string());
		return false;
	}
	offset += 5;

	uint8_t encodedSizeByte = 0;
	do {
		if (!ReadByte(input, offset, encodedSizeByte)) {
			error = "Sprite sheet has a truncated encoded-size field: " + wxstr(sheet->file.string());
			return false;
		}
	} while ((encodedSizeByte & 0x80) != 0);

	uint8_t properties = 0;
	if (!ReadByte(input, offset, properties) || offset + 12 > input.size()) {
		error = "Sprite sheet has truncated LZMA properties: " + wxstr(sheet->file.string());
		return false;
	}

	lzma_options_lzma options {};
	options.lc = properties % 9;
	const int remainder = properties / 9;
	options.lp = remainder % 5;
	options.pb = remainder / 5;
	if (options.pb > 4) {
		error = "Sprite sheet has invalid LZMA properties: " + wxstr(sheet->file.string());
		return false;
	}

	uint32_t dictionarySize = 0;
	for (uint8_t byteIndex = 0; byteIndex < 4; ++byteIndex) {
		dictionarySize |= static_cast<uint32_t>(input[offset++]) << (byteIndex * 8);
	}
	options.dict_size = dictionarySize;
	offset += 8;
	if (offset >= input.size()) {
		error = "Sprite sheet has no compressed payload: " + wxstr(sheet->file.string());
		return false;
	}

	lzma_filter filters[2] = {
		{ LZMA_FILTER_LZMA1, &options },
		{ LZMA_VLI_UNKNOWN, nullptr },
	};
	lzma_stream decoder = LZMA_STREAM_INIT;
	const lzma_ret initResult = lzma_raw_decoder(&decoder, filters);
	if (initResult != LZMA_OK) {
		error = wxString::Format("Could not initialize LZMA decoder for %s (error %d).", wxstr(sheet->file.string()), static_cast<int>(initResult));
		return false;
	}

	std::vector<uint8_t> decoded(DecodeBufferBytes);
	decoder.next_in = input.data() + offset;
	decoder.avail_in = input.size() - offset;
	decoder.next_out = decoded.data();
	decoder.avail_out = decoded.size();
	lzma_ret decodeResult = LZMA_OK;
	while (decodeResult == LZMA_OK && decoder.avail_out > 0) {
		decodeResult = lzma_code(&decoder, LZMA_FINISH);
	}
	const size_t decodedBytes = decoded.size() - decoder.avail_out;
	lzma_end(&decoder);
	if (decodeResult != LZMA_STREAM_END) {
		error = wxString::Format("Could not decode sprite sheet %s (LZMA error %d).", wxstr(sheet->file.string()), static_cast<int>(decodeResult));
		return false;
	}
	if (decodedBytes < 14 || decoded[0] != 'B' || decoded[1] != 'M') {
		error = "Decoded sprite sheet is not a valid bitmap: " + wxstr(sheet->file.string());
		return false;
	}

	uint32_t pixelOffset = 0;
	std::memcpy(&pixelOffset, decoded.data() + 10, sizeof(pixelOffset));
	if (pixelOffset > decodedBytes || decodedBytes - pixelOffset < PixelBytes) {
		error = "Decoded sprite sheet has an invalid bitmap offset: " + wxstr(sheet->file.string());
		return false;
	}

	uint8_t* pixelData = decoded.data() + pixelOffset;
	for (size_t y = 0; y < SpriteSheetHeight / 2; ++y) {
		uint8_t* top = pixelData + y * SpriteSheetWidth * BytesPerPixel;
		uint8_t* bottom = pixelData + (SpriteSheetHeight - y - 1) * SpriteSheetWidth * BytesPerPixel;
		std::swap_ranges(top, top + SpriteSheetWidth * BytesPerPixel, bottom);
	}
	for (size_t pixel = 0; pixel < PixelBytes; pixel += BytesPerPixel) {
		// CipSoft sheets are BGRA bitmaps and use magenta as an additional
		// transparency key. Some OTC packages preserve the key with alpha 255.
		if (pixelData[pixel + 0] == 0xFF && pixelData[pixel + 1] == 0x00 && pixelData[pixel + 2] == 0xFF) {
			pixelData[pixel + 3] = 0x00;
		}
	}

	output = std::make_unique<uint8_t[]>(PixelBytes);
	std::memcpy(output.get(), pixelData, PixelBytes);
	return true;
}

bool SpriteAppearances::loadSpriteSheet(const ClientSpriteSheetPtr& sheet, wxString& error) {
	if (!sheet) {
		error = "Cannot load a null Canary/Crystal sprite sheet.";
		return false;
	}
	processReadySheets();
	if (!sheet->pixels) {
		{
			std::unique_lock<std::mutex> lock(workerMutex);
			const auto pendingEnd = std::remove(pendingSheets.begin(), pendingSheets.end(), sheet);
			if (pendingEnd != pendingSheets.end()) {
				pendingSheets.erase(pendingEnd, pendingSheets.end());
				queuedSheetIds.erase(sheet->firstSpriteId);
			}
			workerCondition.wait(lock, [&]() { return activeSheet != sheet; });
		}
		processReadySheets();
	}
	if (!sheet->pixels) {
		std::unique_ptr<uint8_t[]> decoded;
		if (!decodeSpriteSheet(sheet, decoded, error)) {
			if (!sheet->synchronousFailureLogged) {
				wxLogWarning("Synchronous sprite-sheet decode failed: " + error);
				sheet->synchronousFailureLogged = true;
			}
			return false;
		}
		sheet->pixels = std::move(decoded);
		sheet->asyncFailed = false;
		sheet->synchronousFailureLogged = false;
	}
	sheet->lastAccess = ++accessCounter;
	trimLoadedSheets(sheet);
	return true;
}

void SpriteAppearances::trimLoadedSheets(const ClientSpriteSheetPtr& keep) {
	size_t loadedCount = 0;
	for (const ClientSpriteSheetPtr& sheet : sheets) {
		loadedCount += sheet->pixels ? 1 : 0;
	}
	while (loadedCount > MaximumResidentSheets) {
		ClientSpriteSheetPtr oldest;
		for (const ClientSpriteSheetPtr& sheet : sheets) {
			if (!sheet->pixels || sheet == keep) {
				continue;
			}
			if (!oldest || sheet->lastAccess < oldest->lastAccess) {
				oldest = sheet;
			}
		}
		if (!oldest) {
			return;
		}
		oldest->pixels.reset();
		--loadedCount;
	}
}

void SpriteAppearances::startWorker() {
	std::lock_guard<std::mutex> lock(workerMutex);
	if (worker.joinable()) {
		return;
	}
	workerStopping = false;
	worker = std::thread(&SpriteAppearances::workerLoop, this);
}

void SpriteAppearances::stopWorker() {
	{
		std::lock_guard<std::mutex> lock(workerMutex);
		workerStopping = true;
		pendingSheets.clear();
	}
	workerCondition.notify_all();
	if (worker.joinable()) {
		worker.join();
	}
	{
		std::lock_guard<std::mutex> lock(workerMutex);
		pendingSheets.clear();
		readySheets.clear();
		readyCount.store(0, std::memory_order_release);
		queuedSheetIds.clear();
		activeSheet.reset();
		workerStopping = false;
	}
}

void SpriteAppearances::workerLoop() {
	for (;;) {
		ClientSpriteSheetPtr sheet;
		{
			std::unique_lock<std::mutex> lock(workerMutex);
			workerCondition.wait(lock, [this]() { return workerStopping || !pendingSheets.empty(); });
			if (workerStopping) {
				return;
			}
			sheet = pendingSheets.front();
			pendingSheets.pop_front();
			activeSheet = sheet;
		}

		DecodedSheet decoded;
		decoded.sheet = sheet;
		decodeSpriteSheet(sheet, decoded.pixels, decoded.error);

		bool stop = false;
		{
			std::lock_guard<std::mutex> lock(workerMutex);
			activeSheet.reset();
			if (workerStopping) {
				stop = true;
			} else {
				readySheets.push_back(std::move(decoded));
				readyCount.fetch_add(1, std::memory_order_release);
			}
		}
		workerCondition.notify_all();
		if (stop) {
			return;
		}
	}
}

void SpriteAppearances::requestSpriteSheet(const ClientSpriteSheetPtr& sheet) {
	if (!sheet || sheet->pixels || sheet->asyncFailed) {
		return;
	}
	{
		std::lock_guard<std::mutex> lock(workerMutex);
		constexpr size_t MAX_PENDING_SHEETS = 12;
		if (workerStopping || !worker.joinable() || queuedSheetIds.size() >= MAX_PENDING_SHEETS || !queuedSheetIds.insert(sheet->firstSpriteId).second) {
			return;
		}
		pendingSheets.push_back(sheet);
	}
	workerCondition.notify_one();
}

void SpriteAppearances::processReadySheets() {
	if (readyCount.load(std::memory_order_acquire) == 0) {
		return;
	}

	std::deque<DecodedSheet> ready;
	{
		std::lock_guard<std::mutex> lock(workerMutex);
		ready.swap(readySheets);
		readyCount.store(0, std::memory_order_release);
		for (const DecodedSheet& decoded : ready) {
			queuedSheetIds.erase(decoded.sheet->firstSpriteId);
		}
	}

	for (DecodedSheet& decoded : ready) {
		if (std::find(sheets.begin(), sheets.end(), decoded.sheet) == sheets.end()) {
			continue;
		}
		if (!decoded.pixels) {
			decoded.sheet->asyncFailed = true;
			if (!decoded.error.empty()) {
				wxLogWarning("Background sprite-sheet decode failed: " + decoded.error);
			}
			continue;
		}
		if (!decoded.sheet->pixels) {
			decoded.sheet->pixels = std::move(decoded.pixels);
			decoded.sheet->lastAccess = ++accessCounter;
			trimLoadedSheets(decoded.sheet);
		}
	}
}

size_t SpriteAppearances::getPendingSheetCount() const {
	std::lock_guard<std::mutex> lock(workerMutex);
	return queuedSheetIds.size();
}

bool SpriteAppearances::copySpritePixels(const ClientSpriteSheetPtr& sheet, uint32_t spriteId, std::vector<uint8_t>& output, ClientSpriteSize& size, wxString& error) {
	if (!sheet || !sheet->pixels) {
		return false;
	}

	size = sheet->getSpriteSize();
	const size_t columns = SpriteSheetWidth / static_cast<size_t>(size.width);
	const size_t spriteOffset = spriteId - sheet->firstSpriteId;
	const size_t row = spriteOffset / columns;
	const size_t column = spriteOffset % columns;
	if ((row + 1) * static_cast<size_t>(size.height) > SpriteSheetHeight) {
		error = wxString::Format("Sprite ID %u exceeds the decoded sprite sheet bounds.", spriteId);
		return false;
	}

	output.resize(static_cast<size_t>(size.width) * size.height * BytesPerPixel);
	for (int y = 0; y < size.height; ++y) {
		const size_t sourceOffset = ((row * size.height + static_cast<size_t>(y)) * SpriteSheetWidth + column * size.width) * BytesPerPixel;
		const size_t destinationOffset = static_cast<size_t>(y) * size.width * BytesPerPixel;
		std::memcpy(output.data() + destinationOffset, sheet->pixels.get() + sourceOffset, static_cast<size_t>(size.width) * BytesPerPixel);
	}
	sheet->lastAccess = ++accessCounter;
	return true;
}

bool SpriteAppearances::getSpritePixels(uint32_t spriteId, std::vector<uint8_t>& output, ClientSpriteSize& size, wxString& error) {
	const ClientSpriteSheetPtr sheet = getSheetBySpriteId(spriteId);
	if (!sheet) {
		error = wxString::Format("Sprite ID %u is not present in assets/catalog-content.json.", spriteId);
		return false;
	}
	if (!loadSpriteSheet(sheet, error)) {
		return false;
	}
	return copySpritePixels(sheet, spriteId, output, size, error);
}

bool SpriteAppearances::getSpritePixelsIfLoaded(uint32_t spriteId, std::vector<uint8_t>& output, ClientSpriteSize& size, bool& pending) {
	processReadySheets();
	pending = false;
	const ClientSpriteSheetPtr sheet = getSheetBySpriteId(spriteId);
	if (!sheet || sheet->asyncFailed) {
		return false;
	}
	if (!sheet->pixels) {
		requestSpriteSheet(sheet);
		pending = true;
		return false;
	}
	wxString error;
	return copySpritePixels(sheet, spriteId, output, size, error);
}
