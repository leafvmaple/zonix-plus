#include "pixel_surface.h"
#include <minilib.h>
#include <sys/algorithm.hpp>
#include <sys/cstdlib.hpp>

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
            bool resize = configured();
            if (rect.x || rect.y || !rect.width || !rect.height || rect.width > PIXEL_MAX_WIDTH ||
                rect.height > PIXEL_MAX_HEIGHT || rect.width > max_width || rect.height > max_height)
                return Result::Closed;
            sys::size_t bytes = sys::size_t{rect.width} * rect.height * sizeof(unsigned int);
            surface_.reset(static_cast<unsigned int*>(malloc(bytes)));
            if (!surface_)
                return Result::Closed;
            memset(surface_.get(), 0, bytes);
            width_ = rect.width;
            height_ = rect.height;
            header_bytes_ = 0;
            batch_damage_ = {};
            return resize ? Result::Resized : Result::Configured;
        }
        if (header_.request == PixelRequest::EditText)
            return configured() && !rect.x && !rect.y && !rect.width && !rect.height ? Result::Pending : Result::Closed;
        bool update = header_.request == PixelRequest::Update || header_.request == PixelRequest::UpdatePart;
        if (!update || !configured() || !rect.width || !rect.height || rect.x >= width_ || rect.y >= height_ ||
            rect.width > width_ - rect.x || rect.height > height_ - rect.y ||
            rect.width * rect.height > PIXEL_PACKET_CAPACITY)
            return Result::Closed;
        return Result::Pending;
    }

    if (header_.request == PixelRequest::EditText)
        return receive_edit_path(fd);

    const auto& rect = header_.rect;
    unsigned int size = rect.width * rect.height * sizeof(unsigned int);
    auto* bytes = reinterpret_cast<unsigned char*>(pending_.data());
    long count = sys_read(fd, bytes + payload_bytes_, size - payload_bytes_);
    if (count <= 0)
        return Result::Closed;
    payload_bytes_ += static_cast<unsigned int>(count);
    if (payload_bytes_ != size)
        return Result::Pending;
    for (unsigned int row = 0; row < rect.height; ++row) {
        auto* out = surface_.get() + (rect.y + row) * width_ + rect.x;
        memcpy(out, pending_.data() + row * rect.width, rect.width * sizeof(unsigned int));
    }
    header_bytes_ = payload_bytes_ = 0;
    batch_damage_ = merge_damage(batch_damage_, rect);
    if (header_.request == PixelRequest::UpdatePart)
        return Result::Pending;
    damage_ = batch_damage_;
    batch_damage_ = {};
    return Result::Updated;
}

PixelSurface::Result PixelSurface::receive_edit_path(int fd) {
    long count = sys_read(fd, edit_path_.data() + payload_bytes_, edit_path_.size() - payload_bytes_);
    if (count <= 0)
        return Result::Closed;
    payload_bytes_ += static_cast<unsigned int>(count);
    if (payload_bytes_ != edit_path_.size())
        return Result::Pending;
    header_bytes_ = payload_bytes_ = 0;
    if (edit_path_[0] != '/' || edit_path_[1] == '\0')
        return Result::Closed;
    for (char byte : edit_path_)
        if (byte == '\0')
            return Result::EditText;
    return Result::Closed;
}

void PixelSurface::FreeSurface::operator()(unsigned int* pixels) const noexcept {
    free(pixels);
}

void PixelSurface::reset() {
    surface_.reset();
    width_ = height_ = header_bytes_ = payload_bytes_ = 0;
    batch_damage_ = {};
    damage_ = {};
}

}  // namespace desktop_ipc
