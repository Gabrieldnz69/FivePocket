#pragma once
// v1506 (lead subagent block, owner DLC task): a lazy, read-only STFS content device.
//
// One STFS package file (LIVE or PIRS, read-only single-table layout) is presented to the registry as a content:
// namespace without extraction: the header, hash tree and directory are validated when the package is opened; a
// file's block chain is walked on its first read; each data block's SHA-1 is checked against its level-0 hash entry
// on its first read (cached), and reads are coalesced into one positioned read per physically contiguous run.
// No caps on package/file counts beyond the format's own bounds (64 directory blocks = 4096 entries, 170^3 blocks).
//
// Format reference: Xenia 95a5c3ee250f80c3b9d139658649d9ffb6db3eec, src/xenia/vfs/devices/stfs_xbox.h and
// stfs_container_device.cc (BSD-3-Clause), and this project's tools/host_guest_trace/stfs_inventory.py, whose
// validation rules this mirrors check for check (LIVE accepted; the free-block count is not required to be zero).
// Hash consistency only: Microsoft signatures and licenses are NOT authenticated. No writes, no decryption,
// no CON packages, no SVOD.

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "guest_read_only_file.h"

namespace recomp::stfs {

// ------------------------------------------------------------------ SHA-1 (FIPS 180-4), for hash-tree checks only
class Sha1 {
public:
    using Digest = std::array<std::uint8_t, 20>;
    static Digest of(const std::uint8_t* data, std::size_t size) noexcept { Sha1 h; h.update(data, size); return h.final(); }
    void update(const std::uint8_t* data, std::size_t size) noexcept {
        if (size == 0) { return; }
        length_ += size;
        if (used_) {
            const std::size_t take = std::min<std::size_t>(64 - used_, size);
            std::memcpy(buffer_ + used_, data, take); used_ += take; data += take; size -= take;
            if (used_ < 64) { return; }
            block(h_, buffer_); used_ = 0;
        }
        for (; size >= 64; data += 64, size -= 64) { block(h_, data); }
        std::memcpy(buffer_, data, size); used_ = size;
    }
    Digest final() noexcept {
        std::uint8_t tail[128] = {};
        std::memcpy(tail, buffer_, used_);
        tail[used_] = 0x80;
        const std::size_t tailBlocks = used_ + 1 + 8 <= 64 ? 1 : 2;
        const std::uint64_t bits = length_ * 8;
        for (int i = 0; i < 8; ++i) { tail[tailBlocks * 64 - 1 - i] = static_cast<std::uint8_t>(bits >> (8 * i)); }
        for (std::size_t i = 0; i < tailBlocks; ++i) { block(h_, tail + i * 64); }
        Digest out{};
        for (int i = 0; i < 5; ++i) {
            for (int j = 0; j < 4; ++j) { out[i * 4 + j] = static_cast<std::uint8_t>(h_[i] >> (24 - 8 * j)); }
        }
        return out;
    }
private:
    static std::uint32_t rol(std::uint32_t v, int s) noexcept { return (v << s) | (v >> (32 - s)); }
    static void block(std::uint32_t* h, const std::uint8_t* p) noexcept {
        std::uint32_t w[80];
        for (int i = 0; i < 16; ++i) {
            w[i] = (std::uint32_t{p[i * 4]} << 24) | (std::uint32_t{p[i * 4 + 1]} << 16) | (std::uint32_t{p[i * 4 + 2]} << 8) | p[i * 4 + 3];
        }
        for (int i = 16; i < 80; ++i) { w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1); }
        std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; ++i) {
            std::uint32_t f, k;
            if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999u; }
            else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1u; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDCu; }
            else { f = b ^ c ^ d; k = 0xCA62C1D6u; }
            const std::uint32_t t = rol(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rol(b, 30); b = a; a = t;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }
    std::uint32_t h_[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
    std::uint8_t buffer_[64] = {};
    std::size_t used_{0};
    std::uint64_t length_{0};
};

// ------------------------------------------------------------------ byte sources
class ByteSource {
public:
    virtual ~ByteSource() = default;
    [[nodiscard]] virtual std::uint64_t size() const noexcept = 0;
    // Exactly [offset, offset + bytes) or false.
    [[nodiscard]] virtual bool read(std::uint64_t offset, std::uint8_t* destination, std::size_t bytes) const noexcept = 0;
};

// An already-open regular file (owned). Positioned reads only; never a guest-derived host path.
class FdByteSource final : public ByteSource {
public:
    static std::unique_ptr<FdByteSource> open(const std::string& path, std::string& error) {
        return adopt(::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW), error);
    }
    // Takes ownership of an already-open descriptor (e.g. from openat with O_NOFOLLOW); closes it on failure.
    static std::unique_ptr<FdByteSource> adopt(int fd, std::string& error) {
        struct stat info {};
        if (fd < 0) { error = "open failed"; return nullptr; }
        if (fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) || info.st_size < 0) { ::close(fd); error = "not a regular file"; return nullptr; }
        return std::unique_ptr<FdByteSource>(new FdByteSource(fd, static_cast<std::uint64_t>(info.st_size)));
    }
    ~FdByteSource() override { ::close(fd_); }
    [[nodiscard]] std::uint64_t size() const noexcept override { return size_; }
    [[nodiscard]] bool read(std::uint64_t offset, std::uint8_t* destination, std::size_t bytes) const noexcept override {
        static_assert(sizeof(off_t) >= 8);
        if (offset > size_ || bytes > size_ - offset || (bytes && !destination)) { return false; }
        std::size_t done = 0;
        while (done < bytes) {
            const auto count = pread(fd_, destination + done, bytes - done, static_cast<off_t>(offset + done));
            if (count < 0 && errno == EINTR) { continue; }
            if (count <= 0) { return false; }
            done += static_cast<std::size_t>(count);
        }
        return true;
    }
private:
    FdByteSource(int fd, std::uint64_t size) : fd_(fd), size_(size) {}
    int fd_;
    std::uint64_t size_;
};

// In-memory bytes (host tests and mutants).
class MemoryByteSource final : public ByteSource {
public:
    explicit MemoryByteSource(std::vector<std::uint8_t> bytes) : bytes_(std::move(bytes)) {}
    [[nodiscard]] std::uint64_t size() const noexcept override { return bytes_.size(); }
    [[nodiscard]] bool read(std::uint64_t offset, std::uint8_t* destination, std::size_t bytes) const noexcept override {
        if (offset > bytes_.size() || bytes > bytes_.size() - offset || (bytes && !destination)) { return false; }
        if (bytes) { std::memcpy(destination, bytes_.data() + offset, bytes); }
        return true;
    }
private:
    std::vector<std::uint8_t> bytes_;
};

// ------------------------------------------------------------------ layout (read-only STFS, one hash table copy)
constexpr std::uint64_t BlockBytes = 4096;
constexpr std::uint32_t Fanout = 170;
constexpr std::uint32_t EndOfChain = 0xFFFFFF;
constexpr std::uint64_t HeaderMinimum = 0x971A;
constexpr std::uint64_t MaximumPackageBytes = 8ULL << 30;
constexpr std::uint32_t MaximumDirectoryBlocks = 64;   // 4096 entries, as stfs_inventory.py
constexpr std::size_t MaximumTableCacheBytes = 32u << 20;

inline std::uint64_t dataOffset(std::uint64_t block, std::uint64_t base) noexcept {
    std::uint64_t physical = block;
    for (const std::uint64_t coverage : {std::uint64_t{170}, std::uint64_t{28900}, std::uint64_t{4913000}}) {
        physical += (block + coverage) / coverage;
        if (block < coverage) { break; }
    }
    return base + physical * BlockBytes;
}

inline std::uint64_t tableOffset(std::uint64_t block, int level, std::uint64_t base) noexcept {
    std::uint64_t physical = 0;
    if (level == 0) {
        physical = block < 170 ? 0 : (block / 170) * 171 + block / 28900 + 1 + (block >= 28900 ? 1 : 0);
    } else if (level == 1) {
        physical = block < 28900 ? 171 : (block / 28900) * 29071 + 1;
    } else {
        physical = 29071;
    }
    return base + physical * BlockBytes;
}

struct Entry {
    std::uint32_t index{0};        // directory slot number (block * 64 + slot)
    std::string path;              // original case, '/'-separated
    bool directory{false};
    bool contiguous{false};
    std::uint64_t size{0};
    std::uint32_t start{0};
    std::uint32_t blocks{0};
    std::uint32_t created{0};      // FAT timestamps as stored
    std::uint32_t modified{0};
};

struct Options {
    bool verifyData{true};     // SHA-1 of each data block against its level-0 entry, on its first read (cached)
    bool eagerChains{false};   // walk every file's chain at open (tests; the runtime walks on first read)
};

// FAT date/time (date in the high half) to a Windows FILETIME; 0 for an invalid stamp. Xenia decode_fat_timestamp.
inline std::uint64_t fatToFiletime(std::uint32_t stamp) noexcept {
    const std::uint32_t date = stamp >> 16, time = stamp & 0xFFFF;
    const int year = static_cast<int>(date >> 9) + 1980, month = (date >> 5) & 15, day = date & 31;
    const int hour = time >> 11, minute = (time >> 5) & 63, second = (time & 31) * 2;
    if (month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 || minute > 59 || second > 59) { return 0; }
    // days from civil (Howard Hinnant), relative to 1601-01-01
    const int y = year - (month <= 2 ? 1 : 0);
    const int era = y / 400, yoe = y - era * 400;
    const int doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const long long days1970 = static_cast<long long>(era) * 146097 + doe - 719468;
    const long long days1601 = days1970 + 134774;
    return static_cast<std::uint64_t>(((days1601 * 24 + hour) * 60 + minute) * 60 + second) * 10000000ULL;
}

class Package : public std::enable_shared_from_this<Package> {
public:
    using Digest = Sha1::Digest;

    static std::shared_ptr<Package> open(std::unique_ptr<ByteSource> source, Options options, std::string& error) noexcept {
        try {
            std::shared_ptr<Package> package(new Package(std::move(source), options));
            if (!package->parse(error)) { return nullptr; }
            return package;
        } catch (const std::bad_alloc&) { error = "out of memory"; return nullptr; }
        catch (...) { error = "unexpected failure"; return nullptr; }
    }

    [[nodiscard]] const std::array<std::uint8_t, 4>& magic() const noexcept { return magic_; }
    [[nodiscard]] std::uint32_t titleId() const noexcept { return titleId_; }
    [[nodiscard]] std::uint32_t contentType() const noexcept { return contentType_; }
    [[nodiscard]] std::uint32_t totalBlocks() const noexcept { return total_; }
    [[nodiscard]] std::uint64_t packageBytes() const noexcept { return source_->size(); }
    // 256 bytes of UTF-16BE, cut at the first NUL and zero padded (as stfs_inventory.py's catalog record).
    [[nodiscard]] const std::array<std::uint8_t, 256>& displayName() const noexcept { return display_; }
    [[nodiscard]] const std::vector<Entry>& entries() const noexcept { return entries_; }

    // Reads bytes of a regular file entry (position in entries()). Thread safe. False on any bound, I/O or hash failure.
    [[nodiscard]] bool read(std::size_t entry, std::uint64_t offset, std::uint8_t* destination, std::size_t bytes) const noexcept {
        try {
            if (entry >= entries_.size()) { return false; }
            const auto& e = entries_[entry];
            if (e.directory || offset > e.size || bytes > e.size - offset || (bytes && !destination)) { return false; }
            if (bytes == 0) { return true; }
            const auto chain = chainFor(entry);
            if (!chain) { return false; }
            std::uint64_t position = offset;
            std::size_t remaining = bytes, written = 0;
            std::uint64_t pendingOffset = 0; std::size_t pendingBytes = 0, pendingAt = 0;
            const auto flush = [&]() {
                if (pendingBytes == 0) { return true; }
                const bool ok = source_->read(pendingOffset, destination + pendingAt, pendingBytes);
                pendingBytes = 0;
                return ok;
            };
            std::vector<std::uint8_t> scratch;
            while (remaining) {
                const std::uint64_t fileBlock = position / BlockBytes;
                const std::size_t within = static_cast<std::size_t>(position % BlockBytes);
                const std::size_t take = static_cast<std::size_t>(std::min<std::uint64_t>(BlockBytes - within, remaining));
                const std::uint32_t block = chain->blockAt(fileBlock);
                if (block == EndOfChain) { return false; }
                const std::uint64_t physical = dataOffset(block, base_);
                if (options_.verifyData && !verified_[block].load(std::memory_order_acquire)) {
                    // First read of this block: read it with the following unverified, physically contiguous blocks
                    // this request covers (at most 64 = 256 KiB) in one positioned read, check each block's SHA-1
                    // against its level-0 entry, then copy the requested bytes.
                    if (!flush()) { return false; }
                    std::size_t count = 1, covered = take;
                    while (count < 64 && covered < remaining) {
                        const std::uint32_t following = chain->blockAt(fileBlock + count);
                        if (following == EndOfChain || dataOffset(following, base_) != physical + count * BlockBytes
                                || verified_[following].load(std::memory_order_acquire)) { break; }
                        covered += static_cast<std::size_t>(std::min<std::uint64_t>(BlockBytes, remaining - covered));
                        ++count;
                    }
                    scratch.resize(count * BlockBytes);
                    if (!source_->read(physical, scratch.data(), scratch.size())) { return false; }
                    for (std::size_t k = 0; k < count; ++k) {
                        const std::uint32_t number = k == 0 ? block : chain->blockAt(fileBlock + k);
                        Digest expected{};
                        if (!recordHash(number, expected) || Sha1::of(scratch.data() + k * BlockBytes, BlockBytes) != expected) { return false; }
                        verified_[number].store(1, std::memory_order_release);
                    }
                    std::memcpy(destination + written, scratch.data() + within, covered);
                    position += covered; written += covered; remaining -= covered;
                    continue;
                } else if (pendingBytes && pendingOffset + pendingBytes == physical + within) {
                    pendingBytes += take;
                } else {
                    if (!flush()) { return false; }
                    pendingOffset = physical + within; pendingBytes = take; pendingAt = written;
                }
                position += take; written += take; remaining -= take;
            }
            return flush();
        } catch (...) { return false; }
    }

    // GFMETA1 bytes for one "content:" namespace (guest_file_metadata.h): every directory and file, lower-cased.
    [[nodiscard]] std::vector<std::uint8_t> metadataCatalog() const {
        std::vector<std::uint8_t> out{'G', 'F', 'M', 'E', 'T', 'A', '1', 0};
        const auto u32 = [&](std::uint64_t v) { for (int i = 3; i >= 0; --i) { out.push_back(static_cast<std::uint8_t>(v >> (8 * i))); } };
        const auto u64 = [&](std::uint64_t v) { for (int i = 7; i >= 0; --i) { out.push_back(static_cast<std::uint8_t>(v >> (8 * i))); } };
        const std::string mount = "content:";
        u32(entries_.size() + 1); u32(mount.size()); out.insert(out.end(), mount.begin(), mount.end());
        const auto record = [&](const std::string& path, std::uint64_t created, std::uint64_t modified,
                                std::uint64_t allocation, std::uint64_t size, std::uint32_t attributes) {
            u32(path.size()); out.insert(out.end(), path.begin(), path.end());
            u64(created); u64(modified); u64(modified); u64(modified); u64(allocation); u64(size); u32(attributes);
        };
        record("content:\\", 0, 0, 0, 0, 0x11);
        for (const auto& e : entries_) {
            record(guestPath(e), fatToFiletime(e.created), fatToFiletime(e.modified),
                   e.directory ? 0 : std::uint64_t{e.blocks} * BlockBytes, e.directory ? 0 : e.size, e.directory ? 0x11 : 0x01);
        }
        return out;
    }

    [[nodiscard]] static std::string guestPath(const Entry& e) {
        std::string path = "content:\\";
        for (const char c : e.path) { path.push_back(c == '/' ? '\\' : (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c); }
        return path;
    }

private:
    struct Run { std::uint64_t firstFileBlock; std::uint32_t start; std::uint32_t count; };
    struct Chain {
        std::vector<Run> runs;
        [[nodiscard]] std::uint32_t blockAt(std::uint64_t fileBlock) const noexcept {
            auto it = std::upper_bound(runs.begin(), runs.end(), fileBlock,
                                       [](std::uint64_t value, const Run& run) { return value < run.firstFileBlock; });
            if (it == runs.begin()) { return EndOfChain; }
            --it;
            if (fileBlock - it->firstFileBlock >= it->count) { return EndOfChain; }
            return it->start + static_cast<std::uint32_t>(fileBlock - it->firstFileBlock);
        }
    };

    Package(std::unique_ptr<ByteSource> source, Options options) : source_(std::move(source)), options_(options) {}

    static std::uint32_t be32(const std::uint8_t* p) noexcept {
        return (std::uint32_t{p[0]} << 24) | (std::uint32_t{p[1]} << 16) | (std::uint32_t{p[2]} << 8) | p[3];
    }
    static std::uint32_t le24(const std::uint8_t* p) noexcept { return std::uint32_t{p[0]} | (std::uint32_t{p[1]} << 8) | (std::uint32_t{p[2]} << 16); }

    bool parse(std::string& error) {
        const std::uint64_t size = source_->size();
        if (size < HeaderMinimum || size > MaximumPackageBytes) { error = "package size rejected"; return false; }
        std::vector<std::uint8_t> header(HeaderMinimum);
        if (!source_->read(0, header.data(), header.size())) { error = "header read failed"; return false; }
        std::memcpy(magic_.data(), header.data(), 4);
        const bool live = std::memcmp(header.data(), "LIVE", 4) == 0, pirs = std::memcmp(header.data(), "PIRS", 4) == 0;
        contentType_ = be32(&header[0x344]);
        titleId_ = be32(&header[0x360]);
        const std::uint32_t metadataVersion = be32(&header[0x348]);
        if (!(live || pirs) || contentType_ != 2 || (metadataVersion != 1 && metadataVersion != 2)) {
            error = "package magic/type/version rejected"; return false;
        }
        const std::uint32_t headerSize = be32(&header[0x340]);
        if (headerSize < HeaderMinimum || headerSize > std::min<std::uint64_t>(size, 1024 * 1024)) { error = "header bounds rejected"; return false; }
        base_ = (std::uint64_t{headerSize} + BlockBytes - 1) & ~(BlockBytes - 1);
        const std::uint8_t* volume = &header[0x379];
        bool zeros = true;
        for (std::size_t i = 0x39D; i < 0x3AD; ++i) { zeros = zeros && header[i] == 0; }
        if (volume[0] != 0x24 || volume[1] != 0 || volume[2] != 1 || !zeros) {
            error = "only single-table read-only STFS version zero is supported"; return false;
        }
        total_ = be32(volume + 28);
        directoryBlocks_ = std::uint32_t{volume[3]} | (std::uint32_t{volume[4]} << 8);
        firstDirectory_ = le24(volume + 5);
        std::memcpy(rootHash_.data(), volume + 8, 20);
        if (total_ < 1 || total_ > std::min<std::uint64_t>(std::uint64_t{170} * 170 * 170, size / BlockBytes)
                || directoryBlocks_ < 1 || directoryBlocks_ > MaximumDirectoryBlocks
                || dataOffset(total_ - 1, base_) + BlockBytes > size) {
            error = "package allocation bounds rejected (truncated?)"; return false;
        }
        rootLevel_ = total_ <= 170 ? 0 : total_ <= 28900 ? 1 : 2;
        // Display name: all 128 UTF-16BE units must decode (a lone or reversed surrogate is rejected, as Python's
        // strict decode of the whole field); the name is the text before the first NUL, zero padded.
        bool high = false, ended = false;
        for (std::size_t i = 0; i < 128; ++i) {
            const std::uint16_t ch = static_cast<std::uint16_t>((header[0x411 + i * 2] << 8) | header[0x412 + i * 2]);
            if (high ? (ch < 0xDC00 || ch > 0xDFFF) : (ch >= 0xDC00 && ch <= 0xDFFF)) { error = "display name is not UTF-16"; return false; }
            high = !high && ch >= 0xD800 && ch <= 0xDBFF;
            ended = ended || ch == 0;
            if (!ended) { display_[i * 2] = header[0x411 + i * 2]; display_[i * 2 + 1] = header[0x412 + i * 2]; }
        }
        if (high) { error = "display name is not UTF-16"; return false; }
        verified_.reset(new std::atomic<std::uint8_t>[total_]);
        for (std::uint32_t i = 0; i < total_; ++i) { verified_[i].store(0, std::memory_order_relaxed); }
        chains_.resize(0);
        if (!directory(error)) { return false; }
        chains_.resize(entries_.size());
        if (options_.eagerChains) {
            for (std::size_t i = 0; i < entries_.size(); ++i) {
                if (!entries_[i].directory && !chainFor(i)) { error = "file block chain rejected"; return false; }
            }
        }
        return true;
    }

    // Level table at (block, level), its SHA-1 checked up to the root hash. Caller holds mutex_.
    const std::uint8_t* tableLocked(std::uint64_t block, int level) const {
        const std::uint64_t offset = tableOffset(block, level, base_);
        const auto found = tables_.find(offset);
        if (found != tables_.end()) { return found->second->data(); }
        Digest expected{};
        if (level == rootLevel_) {
            expected = rootHash_;
        } else {
            const std::uint8_t* parent = tableLocked(block, level + 1);
            if (!parent) { return nullptr; }
            std::uint64_t span = 170;
            for (int i = 0; i < level; ++i) { span *= 170; }
            const std::uint64_t slot = (block / span) % 170;
            std::memcpy(expected.data(), parent + slot * 24, 20);
        }
        auto data = std::make_unique<std::array<std::uint8_t, BlockBytes>>();
        if (offset + BlockBytes > source_->size() || !source_->read(offset, data->data(), BlockBytes)
                || Sha1::of(data->data(), BlockBytes) != expected) { return nullptr; }
        if ((tables_.size() + 1) * BlockBytes > MaximumTableCacheBytes) { tables_.clear(); }   // re-verified when read again
        const auto* raw = data->data();
        tables_.emplace(offset, std::move(data));
        return raw;
    }

    // (hash, next) of an allocated data block. Caller holds mutex_.
    bool recordLocked(std::uint32_t block, Digest& hash, std::uint32_t& next) const {
        if (block >= total_) { return false; }
        const std::uint8_t* table = tableLocked(block, 0);
        if (!table) { return false; }
        const std::uint8_t* entry = table + (block % 170) * 24;
        const std::uint32_t info = be32(entry + 20);
        if ((info >> 30) != 2) { return false; }
        std::memcpy(hash.data(), entry, 20);
        next = info & EndOfChain;
        return true;
    }

    bool recordHash(std::uint32_t block, Digest& hash) const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::uint32_t next = 0;
        return recordLocked(block, hash, next);
    }

    // Walk `count` blocks from `start`; the chain must end in EndOfChain exactly there (no cycles: a revisited block
    // repeats forever and never reaches the end). Caller holds mutex_.
    bool walkLocked(std::uint32_t start, std::uint32_t count, std::vector<Run>& runs) const {
        if (count > total_) { return false; }
        std::uint32_t current = start;
        Digest hash{};
        for (std::uint32_t i = 0; i < count; ++i) {
            std::uint32_t next = 0;
            if (!recordLocked(current, hash, next)) { return false; }
            if (!runs.empty() && runs.back().start + runs.back().count == current) { ++runs.back().count; }
            else { runs.push_back({i, current, 1}); }
            current = next;
        }
        return count == 0 || current == EndOfChain;
    }

    std::shared_ptr<const Chain> chainFor(std::size_t entry) const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (chains_[entry]) { return chains_[entry]; }
        const auto& e = entries_[entry];
        auto chain = std::make_shared<Chain>();
        if (!walkLocked(e.start, e.blocks, chain->runs)) { return nullptr; }
        if (e.contiguous && e.blocks && (chain->runs.size() != 1 || chain->runs[0].start != e.start)) { return nullptr; }
        chains_[entry] = chain;
        return chain;
    }

    static bool component(const std::string& name) {
        if (name.empty() || name.size() > 40 || name == "." || name == ".." || name.back() == '.' || name.back() == ' ') { return false; }
        for (const unsigned char c : name) {
            if (c < 32 || c >= 127 || std::strchr("/\\:*?\"<>|", c)) { return false; }
        }
        return true;
    }

    bool directory(std::string& error) {
        std::vector<Run> runs;
        std::vector<std::uint32_t> blocks;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!walkLocked(firstDirectory_, directoryBlocks_, runs)) { error = "directory chain rejected"; return false; }
        }
        for (const auto& run : runs) { for (std::uint32_t i = 0; i < run.count; ++i) { blocks.push_back(run.start + i); } }
        std::map<std::uint32_t, std::size_t> byIndex;
        std::set<std::string> seen;
        std::array<std::uint8_t, BlockBytes> data{};
        for (std::size_t blockIndex = 0; blockIndex < blocks.size(); ++blockIndex) {
            const auto number = blocks[blockIndex];
            Digest expected{};
            if (!source_->read(dataOffset(number, base_), data.data(), BlockBytes) || !recordHash(number, expected)
                    || Sha1::of(data.data(), BlockBytes) != expected) { error = "directory block hash rejected"; return false; }
            verified_[number].store(1, std::memory_order_release);
            for (std::uint32_t slot = 0; slot < 64; ++slot) {
                const std::uint8_t* raw = data.data() + slot * 64;
                bool empty = true;
                for (int i = 0; i < 64; ++i) { empty = empty && raw[i] == 0; }
                if (empty) { continue; }
                Entry e;
                e.index = static_cast<std::uint32_t>(blockIndex * 64 + slot);
                const std::uint32_t length = raw[40] & 63;
                e.directory = (raw[40] & 128) != 0;
                e.contiguous = (raw[40] & 64) != 0;
                bool padded = true;
                for (std::uint32_t i = length; i < 40; ++i) { padded = padded && raw[i] == 0; }
                if (length < 1 || length > 40 || !padded) { error = "malformed name storage"; return false; }
                std::string name(reinterpret_cast<const char*>(raw), length);
                if (!component(name)) { error = "unsafe name"; return false; }
                const std::uint32_t parent = (std::uint32_t{raw[50]} << 8) | raw[51];
                if (parent != 0xFFFF) {
                    const auto found = byIndex.find(parent);
                    if (found == byIndex.end() || !entries_[found->second].directory) { error = "parent must precede child and be a directory"; return false; }
                    name = entries_[found->second].path + "/" + name;
                }
                std::string lower = name;
                for (auto& c : lower) { if (c >= 'A' && c <= 'Z') { c = static_cast<char>(c + 32); } }
                if (name.size() > 1024 || std::count(name.begin(), name.end(), '/') > 32 || !seen.insert(lower).second) {
                    error = "duplicate or deeply nested path"; return false;
                }
                e.path = name;
                const std::uint32_t valid = le24(raw + 41), allocated = le24(raw + 44);
                e.start = le24(raw + 47);
                e.size = be32(raw + 52);
                e.created = be32(raw + 56);
                e.modified = be32(raw + 60);
                if (allocated != valid || allocated != (e.size + BlockBytes - 1) / BlockBytes || (e.directory && (allocated || e.size))) {
                    error = "file size/allocation mismatch"; return false;
                }
                e.blocks = allocated;
                byIndex.emplace(e.index, entries_.size());
                entries_.push_back(std::move(e));
            }
        }
        return true;
    }

    std::unique_ptr<ByteSource> source_;
    Options options_;
    std::array<std::uint8_t, 4> magic_{};
    std::uint32_t titleId_{0}, contentType_{0}, total_{0}, directoryBlocks_{0}, firstDirectory_{0};
    std::uint64_t base_{0};
    int rootLevel_{0};
    Digest rootHash_{};
    std::array<std::uint8_t, 256> display_{};
    std::vector<Entry> entries_;
    mutable std::mutex mutex_;
    mutable std::map<std::uint64_t, std::unique_ptr<std::array<std::uint8_t, BlockBytes>>> tables_;
    mutable std::vector<std::shared_ptr<const Chain>> chains_;
    std::unique_ptr<std::atomic<std::uint8_t>[]> verified_;
};

// One regular file of a package, as the registry's read-only file interface.
class File final : public recomp::bootstrap::GuestReadOnlyFile {
public:
    File(std::shared_ptr<const Package> package, std::size_t entry) : package_(std::move(package)), entry_(entry) {}
    [[nodiscard]] std::uint64_t size() const noexcept override { return package_->entries()[entry_].size; }
    [[nodiscard]] bool read(std::uint64_t offset, std::uint8_t* destination, std::size_t bytes) const noexcept override {
        return package_->read(entry_, offset, destination, bytes);
    }
private:
    std::shared_ptr<const Package> package_;
    std::size_t entry_;
};

// XCONTENT_DATA (308 bytes): device 1, type 2, the package's display name, `name` (<= 40 ASCII alphanumerics).
inline std::array<std::uint8_t, 0x134> catalogRecord(const Package& package, const std::string& name) {
    std::array<std::uint8_t, 0x134> record{};
    record[3] = 1; record[7] = 2;
    std::copy(package.displayName().begin(), package.displayName().end(), record.begin() + 8);
    std::copy(name.begin(), name.end(), record.begin() + 264);
    return record;
}

} // namespace recomp::stfs
