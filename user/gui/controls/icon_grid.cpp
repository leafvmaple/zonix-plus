#include "icon_grid.h"
#include "../platform.h"
#include <2d/ZCLabel.h>
#include <base/ZCDirector.h>
#include <base/ZCEventListener.h>
#include <ui/UIWidget.h>

namespace zonix_gui {
namespace {

constexpr float CELL_WIDTH = 88.f;
constexpr float CELL_HEIGHT = 76.f;
constexpr float COLUMN_GAP = 8.f;
constexpr float ROW_GAP = 4.f;
constexpr float INSET_X = 8.f;
constexpr float INSET_Y = 8.f;
// Within a cell, measured from its bottom edge.
constexpr float LABEL_Y = 18.f;
constexpr float LABEL_MARGIN = 4.f;
constexpr float ICON_BOTTOM = 36.f;

using control_style::Color;
using MouseButton = zocos::EventMouse::MouseButton;

mstd::string display_name(const char* name) {
    mstd::string text(name);
    if (text.size() <= 10)
        return text;

    // Preserve the end of the name, including the extension, without splitting a UTF-8 character.
    auto continuation = [&](mstd::size_t index) {
        return (static_cast<unsigned char>(text[index]) & 0xc0) == 0x80;
    };
    mstd::size_t head = 5;
    mstd::size_t tail = text.size() - 4;
    while (head > 0 && continuation(head))
        --head;
    while (tail < text.size() && continuation(tail))
        ++tail;
    return text.substr(0, head) + "…" + text.substr(tail);
}

}  // namespace

class IconGrid::Tile final : public zocos::ui::Widget {
public:
    static Tile* create(zocos::Director& director, IconGrid& grid) {
        auto* label = control_style::make_label(director, "");
        if (!label)
            return nullptr;
        auto* tile = new (mstd::nothrow) Tile(grid, *label);
        if (!tile || !tile->init()) {
            delete tile;
            return nullptr;
        }
        tile->autorelease();
        tile->setAnchorPoint({0.f, 0.f});
        tile->setContentSize({CELL_WIDTH, CELL_HEIGHT});
        tile->addChild(label);
        return tile;
    }

    void set_item(const Item& item) {
        icon_ = item.icon;
        label_.setString(display_name(item.name));
        control_style::place_label(label_, CELL_WIDTH / 2.f, LABEL_Y, 0.5f, CELL_WIDTH - 2.f * LABEL_MARGIN);
    }

    void set_selected(bool selected) { selected_ = selected; }

    bool hitTest(float x, float y) const override {
        return isVisible() && grid_.contains({x, y}) && Widget::hitTest(x, y);
    }

private:
    Tile(const IconGrid& grid, zocos::Label& label) : grid_(grid), label_(label) {}

    void draw(zocos::Renderer& renderer, const zocos::Mat4& world) override {
        const auto& palette = grid_.palette_;
        if (selected_ || isPressed())
            palette.frame(renderer, world, {0.f, 0.f, CELL_WIDTH, CELL_HEIGHT}, control_style::PANEL_RADIUS,
                          isPressed() ? Color::Pressed : Color::Selected,
                          isPressed() ? Color::Border : Color::SelectedBorder);

        auto shape = [&](zocos::Rect area, float radius, Color color) {
            palette.round_box(renderer, world, area, radius, color);
        };
        if (icon_ == Icon::Folder) {
            constexpr float left = (CELL_WIDTH - 32.f) / 2.f;
            shape({left, ICON_BOTTOM + 16.f, 14.f, 10.f}, 3.f, Color::FolderTab);
            shape({left, ICON_BOTTOM, 32.f, 22.f}, 3.f, Color::FolderShade);
            shape({left, ICON_BOTTOM, 32.f, 18.f}, 3.f, Color::Folder);
        } else {
            constexpr float left = (CELL_WIDTH - 24.f) / 2.f;
            shape({left, ICON_BOTTOM - 2.f, 24.f, 30.f}, 3.f, Color::File);
            shape({left + 16.f, ICON_BOTTOM + 20.f, 8.f, 8.f}, 3.f, Color::FileFold);
            shape({left + 5.f, ICON_BOTTOM + 14.f, 9.f, 2.f}, 0.f, Color::FileShade);
            shape({left + 5.f, ICON_BOTTOM + 9.f, 14.f, 2.f}, 0.f, Color::FileShade);
            shape({left + 5.f, ICON_BOTTOM + 4.f, 14.f, 2.f}, 0.f, Color::FileShade);
        }
    }

    const IconGrid& grid_;
    zocos::Label& label_;
    Icon icon_ = Icon::File;
    bool selected_{};
};

IconGrid::IconGrid(zocos::Director& director, OnInteraction on_interaction)
    : palette_(director), on_interaction_(mstd::move(on_interaction)) {}

IconGrid* IconGrid::create(zocos::Director& director, zocos::Size size, OnInteraction on_interaction) {
    auto* grid = new (mstd::nothrow) IconGrid(director, mstd::move(on_interaction));
    if (!grid)
        return nullptr;
    grid->autorelease();
    return grid->init_grid(director, size) ? grid : nullptr;
}

bool IconGrid::init_grid(zocos::Director& director, zocos::Size size) {
    columns_ = static_cast<int>((size.width - 2.f * INSET_X + COLUMN_GAP) / (CELL_WIDTH + COLUMN_GAP));
    int rows = static_cast<int>((size.height - 2.f * INSET_Y + ROW_GAP) / (CELL_HEIGHT + ROW_GAP));
    if (!Node::init() || columns_ <= 0 || rows <= 0 || !palette_.init())
        return false;
    setAnchorPoint({0.f, 0.f});
    setContentSize(size);
    tiles_.reserve(rows * columns_);
    for (int index = 0; index < rows * columns_; ++index) {
        auto* tile = Tile::create(director, *this);
        if (!tile)
            return false;
        tile->setVisible(false);
        tile->setPosition({INSET_X + (index % columns_) * (CELL_WIDTH + COLUMN_GAP),
                           size.height - INSET_Y - CELL_HEIGHT - (index / columns_) * (CELL_HEIGHT + ROW_GAP)});
        tile->addEventListener([this, index](zocos::ui::Widget&) { click(index); });
        tiles_.push_back(tile);
        addChild(tile);
    }

    auto* mouse = zocos::EventListenerMouse::create();
    auto* keys = zocos::EventListenerKeyboard::create([this](zocos::EventKeyboard&) { cancel_click(); });
    if (!mouse || !keys)
        return false;
    mouse->onMouseDown = [this](zocos::EventMouse& event) {
        if (event.getMouseButton() != MouseButton::BUTTON_LEFT || item_at({event.getX(), event.getY()}) != clicked_)
            cancel_click();
    };
    // mini-cocos sends the mouse release before the corresponding Widget touch release.
    mouse->onMouseUp = [this](zocos::EventMouse& event) {
        if (event.getMouseButton() != MouseButton::BUTTON_LEFT)
            return;
        if (is_pointer_reset({event.getX(), event.getY()}))
            cancel_click();
        else
            double_click_ = is_double_click(event.getModifiers());
    };
    director.getEventDispatcher().addEventListenerWithNodePriority(mouse, this);
    director.getEventDispatcher().addEventListenerWithNodePriority(keys, this);
    return true;
}

void IconGrid::set_items(const mstd::vector<Item>& items) {
    select(NO_ITEM);
    count_ = static_cast<int>(mstd::min(items.size(), tiles_.size()));
    for (int i = 0; i < capacity(); ++i) {
        tiles_[i]->setVisible(i < count_);
        if (i < count_)
            tiles_[i]->set_item(items[i]);
    }
}

void IconGrid::select(int index) {
    cancel_click();
    highlight(index >= 0 && index < count_ ? index : NO_ITEM);
}

bool IconGrid::select_at(zocos::Vec2 pointer) {
    if (!contains(pointer))
        return false;
    select(item_at(pointer));
    return true;
}

void IconGrid::onExit() {
    cancel_click();
    Node::onExit();
}

bool IconGrid::contains(zocos::Vec2 pointer) const {
    for (const zocos::Node* node = this; node; node = node->getParent()) {
        if (!node->isVisible())
            return false;
    }
    const auto size = getContentSize();
    zocos::Vec2 local;
    return convertToNodeSpace(pointer, local) && local.x >= 0.f && local.x < size.width && local.y >= 0.f &&
           local.y < size.height;
}

int IconGrid::item_at(zocos::Vec2 pointer) const {
    for (int i = 0; i < count_; ++i) {
        if (tiles_[i]->hitTest(pointer.x, pointer.y))
            return i;
    }
    return NO_ITEM;
}

void IconGrid::highlight(int index) {
    if (selected_ != NO_ITEM)
        tiles_[selected_]->set_selected(false);
    selected_ = index;
    if (selected_ != NO_ITEM)
        tiles_[selected_]->set_selected(true);
}

void IconGrid::cancel_click() {
    clicked_ = NO_ITEM;
    double_click_ = false;
}

void IconGrid::click(int index) {
    highlight(index);
    bool activate = double_click_ && clicked_ == index;
    clicked_ = activate ? NO_ITEM : index;
    on_interaction_(activate ? Interaction::Activate : Interaction::Select);
}

}  // namespace zonix_gui
