#include "pixel_surface.h"
#include <minilib.h>
#include <sys/algorithm.hpp>

namespace desktop_ipc {

namespace {

PixelRect merge_damage(const PixelRect& batch, const PixelRect& rect) {
    if (batch.width == 0)
        return rect;
    auto left = sys::min(batch.x, rect.x);
    auto top = sys::min(batch.y, rect.y);
    auto right = sys::max(batch.x + batch.width, rect.x + rect.width);
    auto bottom = sys::max(batch.y + batch.height, rect.y + rect.height);
    return {left, top, right - left, bottom - top};
}

}  // namespace

PixelSurface::Result PixelSurface::receive(int fd, unsigned int max_width, unsigned int max_height) {
    if (header_bytes_ != sizeof(header_)) {
        auto* bytes = reinterpret_cast<unsigned char*>(&header_);
        long count = sys_read(fd, bytes + header_bytes_, sizeof(header_) - header_bytes_);
        if (count <= 0)
            return Result::Closed;
        header_bytes_ += static_cast<unsigned int>(count);
        if (header_bytes_ != sizeof(header_))
            return Result::Pending;

        const auto& rect = header_.rect;
        if (header_.request == PixelRequest::Size) {
            if (configured() || rect.x || rect.y || !rect.width || !rect.height || rect.width > PIXEL_MAX_WIDTH ||
                rect.height > PIXEL_MAX_HEIGHT || rect.width > max_width || rect.height > max_height)
                return Result::Closed;
            width_ = rect.width;
            height_ = rect.height;
            memset(surface_.data(), 0, width_ * height_ * sizeof(unsigned int));
            header_bytes_ = 0;
            return Result::Configured;
        }
        bool update = header_.request == PixelRequest::Update || header_.request == PixelRequest::UpdatePart;
        if (!update || !configured() || !rect.width || !rect.height || rect.x >= width_ || rect.y >= height_ ||
            rect.width > width_ - rect.x || rect.height > height_ - rect.y ||
            rect.width * rect.height > PIXEL_PACKET_CAPACITY)
            return Result::Closed;
        return Result::Pending;
    }

    const auto& rect = header_.rect;
    unsigned int size = rect.width * rect.height * sizeof(unsigned int);
    auto* bytes = reinterpret_cast<unsigned char*>(pending_.data());
    long count = sys_read(fd, bytes + pixel_bytes_, size - pixel_bytes_);
    if (count <= 0)
        return Result::Closed;
    pixel_bytes_ += static_cast<unsigned int>(count);
    if (pixel_bytes_ != size)
        return Result::Pending;
    for (unsigned int row = 0; row < rect.height; ++row) {
        auto* out = surface_.data() + (rect.y + row) * width_ + rect.x;
        memcpy(out, pending_.data() + row * rect.width, rect.width * sizeof(unsigned int));
    }
    header_bytes_ = pixel_bytes_ = 0;
    batch_damage_ = merge_damage(batch_damage_, rect);
    if (header_.request == PixelRequest::UpdatePart)
        return Result::Pending;
    damage_ = batch_damage_;
    batch_damage_ = {};
    return Result::Updated;
}

void PixelSurface::reset() {
    width_ = height_ = header_bytes_ = pixel_bytes_ = 0;
    batch_damage_ = {};
    damage_ = {};
}

}  // namespace desktop_ipc
