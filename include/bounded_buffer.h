#ifndef BOUNDED_BUFFER_H
#define BOUNDED_BUFFER_H
#include <cstddef>
#include <limits>
#include <string>

struct BoundedBuffer {
    std::string data;
    std::size_t limit;
    bool exceeded = false;
};

// C callback: neither oversized responses nor allocation exceptions may escape.
inline std::size_t bounded_write(void* contents, std::size_t size,
                                 std::size_t count, void* context) noexcept {
    auto& buffer = *static_cast<BoundedBuffer*>(context);
    if (size && count > std::numeric_limits<std::size_t>::max() / size) {
        buffer.exceeded = true;
        return 0;
    }
    const auto bytes = size * count;
    if (buffer.data.size() > buffer.limit || bytes > buffer.limit - buffer.data.size()) {
        buffer.exceeded = true;
        return 0;
    }
    try {
        if (bytes) buffer.data.append(static_cast<const char*>(contents), bytes);
        return bytes;
    } catch (...) { return 0; }
}
#endif
