#pragma once
// Reads files out of an Unreal Engine 4.27 pak archive (pak version 11) through its full directory
// index (tools/lib/pak.mjs). Paths are relative to the mount point, for example
// "Astro/Content/Items/ItemTypes/Components/Generator_Small.uasset".
// Supported: an unencrypted index, uncompressed and Zlib-compressed entries, no entry encryption. This
// is what ASTRONEER ships; anything else is refused with a FormatError instead of being misread.
// Only the index stays in memory; each entry is read from the file when asked for.
// Layouts follow UE 4.27 FPakInfo, FPakFile::LoadIndexInternal and FPakFile::DecodePakEntry.
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "reader.h"

namespace rd::extract {

class Pak {
public:
    // Opens the file and reads the index. Throws ToolError when the file cannot be opened and
    // FormatError when it is not a pak this reader supports.
    explicit Pak(const std::filesystem::path& file);

    // Every file, in the order of the directory index.
    const std::vector<std::string>& Files() const { return files_; }

    // The file's contents. Throws FormatError when it is missing or its entry is malformed, and
    // std::runtime_error when its zlib data is corrupt or the pak cannot be read.
    Bytes Read(std::string_view path) const;

private:
    struct HandleCloser {
        void operator()(void* handle) const;
    };

    Bytes ReadAt(int64_t offset, int64_t length, int64_t limit) const;

    std::unique_ptr<void, HandleCloser> handle_;
    int64_t fileSize_ = 0;
    std::vector<std::string> compressionNames_;  // methods 1-5 of the entries
    Bytes encodedEntries_;
    std::vector<std::string> files_;
    std::vector<int32_t> locations_;  // per file: offset of its entry in encodedEntries_
    std::unordered_map<std::string, size_t> fileIndex_;
};

}  // namespace rd::extract
