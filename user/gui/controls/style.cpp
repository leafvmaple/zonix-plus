#include "style.h"
#include "../platform.h"
#include <2d/ZCLabel.h>
#include <base/ZCDirector.h>

namespace zonix_gui::control_style {
namespace {

constexpr zocos::Color4B COLORS[] = {{55, 75, 92, 255},    {42, 61, 80, 255},    {56, 97, 123, 255},
                                     {36, 54, 70, 255},    {31, 49, 65, 255},    {26, 42, 58, 255},
                                     {44, 77, 101, 255},   {239, 188, 104, 255}, {183, 126, 58, 255},
                                     {183, 206, 220, 255}, {107, 145, 166, 255}};
constexpr int COLOR_COUNT = static_cast<int>(Color::FileShade) + 1;
static_assert(sizeof(zocos::Color4B) == 4 && sizeof(COLORS) / sizeof(*COLORS) == COLOR_COUNT);

}  // namespace

zocos::Label* make_label(zocos::Director& director, const mstd::string& text) {
    auto* label = zocos::Label::createWithTTF(director, text, UI_FONT_PATH, 24.f);
    if (label)
        label->setScale(LABEL_SCALE);
    return label;
}

Palette::~Palette() {
    if (auto* device = director_.getRenderDevice(); device && texture_.isValid())
        device->destroyTexture(texture_);
}

bool Palette::init() {
    texture_ = director_.getRenderDevice()->createTexture(
        {COLOR_COUNT, 1, zocos::TextureFormat::RGBA8Unorm, {reinterpret_cast<const unsigned char*>(COLORS)}});
    return texture_.isValid();
}

void Palette::box(zocos::Renderer& renderer, const zocos::Mat4& world, zocos::Rect area, Color color,
                  zocos::RenderSortKey order) const {
    float sample = (static_cast<int>(color) + 0.5f) / COLOR_COUNT;
    renderer.addDrawSprite(world * zocos::Mat4::translate(area.x, area.y), {area.width, area.height}, texture_,
                           {sample, 0.f, 0.f, 1.f}, 1.f, order);
}

void Palette::button(zocos::Renderer& renderer, const zocos::Mat4& world, zocos::Size size, Color color,
                     zocos::RenderSortKey order) const {
    box(renderer, world, {0.f, 0.f, size.width, size.height}, Color::Border, order);
    box(renderer, world, {1.f, 1.f, size.width - 2.f, size.height - 2.f}, color, order);
}

}  // namespace zonix_gui::control_style
