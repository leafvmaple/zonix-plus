#include "context_menu.h"
#include "../platform.h"
#include <2d/ZCLabel.h>
#include <base/ZCDirector.h>
#include <base/ZCEventListener.h>
#include <display.h>
#include <ui/UIWidget.h>

namespace zonix_gui {
namespace {

constexpr int OVERLAY_LAYER = 1;
constexpr float MIN_WIDTH = 126.f;
constexpr float ROW_HEIGHT = 26.f;
constexpr float INSET = 12.f;
using MouseButton = zocos::EventMouse::MouseButton;
using control_style::Color;
using control_style::Palette;

class MenuRow final : public zocos::ui::Widget {
public:
    explicit MenuRow(const Palette& palette) : palette_(palette) {}

    static MenuRow* create(zocos::Director& director, const Palette& palette, const mstd::string& text) {
        auto* label = control_style::make_label(director, text);
        if (!label || !label->updateLayout())
            return nullptr;
        auto* row = new (mstd::nothrow) MenuRow(palette);
        if (!row)
            return nullptr;
        row->init();
        row->autorelease();
        row->setAnchorPoint({0.f, 0.f});
        label->setAnchorPoint({0.f, 0.5f});
        label->setPosition({INSET, ROW_HEIGHT / 2.f});
        label->setRenderLayer(OVERLAY_LAYER);
        row->label_ = label;
        row->addChild(label);
        return row;
    }

    float preferred_width() const { return label_->getContentSize().width * control_style::LABEL_SCALE + 2 * INSET; }

    void layout(float width, float y) {
        setContentSize({width, ROW_HEIGHT});
        setPosition({0.f, y});
        float available = width - 2 * INSET;
        float text_width = label_->getContentSize().width;
        label_->setScale(mstd::min(control_style::LABEL_SCALE, available / mstd::max(text_width, 1.f)));
    }

    void draw(zocos::Renderer& renderer, const zocos::Mat4& world) override {
        palette_.button(renderer, world, getContentSize(), isPressed() ? Color::Pressed : Color::Button,
                        zocos::makeRenderSortKey(0, OVERLAY_LAYER, 0));
    }

protected:
    bool hitTest(float x, float y) const override { return getParent()->isVisible() && Widget::hitTest(x, y); }

private:
    const Palette& palette_;
    zocos::Label* label_{};
};

}  // namespace

ContextMenu::ContextMenu(zocos::Director& director, ItemProvider items_at)
    : director_(director), items_at_(mstd::move(items_at)), palette_(director) {}

bool ContextMenu::attach(zocos::Director& director, zocos::Scene& scene, ItemProvider items_at) {
    auto* menu = new (mstd::nothrow) ContextMenu(director, mstd::move(items_at));
    if (!menu || !menu->init()) {
        delete menu;
        return false;
    }
    scene.addChild(menu, OVERLAY_LAYER);
    menu->release();
    return true;
}

bool ContextMenu::init() {
    if (!Node::init() || !palette_.init())
        return false;
    setAnchorPoint({0.f, 0.f});
    setVisible(false);
    auto* mouse = zocos::EventListenerMouse::create();
    auto* keys = zocos::EventListenerKeyboard::create([this](zocos::EventKeyboard& event) {
        if (isVisible()) {
            setVisible(false);
            if (event.getKeyCode() == DISPLAY_KEY_ESCAPE)
                event.stopPropagation();
        }
    });
    if (!mouse || !keys)
        return false;
    mouse->onMouseDown = [this](zocos::EventMouse& event) {
        mouse_down(event);
    };
    mouse->onMouseUp = [this](zocos::EventMouse& event) {
        mouse_up(event);
    };
    auto& dispatcher = director_.getEventDispatcher();
    dispatcher.addEventListenerWithNodePriority(mouse, this);
    dispatcher.addEventListenerWithNodePriority(keys, this);
    return true;
}

void ContextMenu::onExit() {
    setVisible(false);
    right_pressed_ = false;
    Node::onExit();
}

bool ContextMenu::contains(zocos::Vec2 pointer) const {
    zocos::Vec2 local;
    const auto size = getContentSize();
    return convertToNodeSpace(pointer, local) && local.x >= 0.f && local.y >= 0.f && local.x <= size.width &&
           local.y <= size.height;
}

void ContextMenu::mouse_down(const zocos::EventMouse& event) {
    if (event.getMouseButton() == MouseButton::BUTTON_RIGHT)
        right_pressed_ = true;
    if (isVisible() && !contains({event.getX(), event.getY()}))
        setVisible(false);
}

void ContextMenu::mouse_up(const zocos::EventMouse& event) {
    const auto button = event.getMouseButton();
    zocos::Vec2 pointer{event.getX(), event.getY()};
    if (button == MouseButton::BUTTON_LEFT && is_pointer_reset(pointer)) {
        right_pressed_ = false;
        setVisible(false);
    } else if (button == MouseButton::BUTTON_RIGHT && right_pressed_) {
        right_pressed_ = false;
        open(pointer);
    }
}

void ContextMenu::open(zocos::Vec2 pointer) {
    setVisible(false);
    removeAllChildren();
    auto items = items_at_(pointer);
    float height = items.size() * ROW_HEIGHT;
    float window_width = static_cast<float>(director_.getFramebufferWidth());
    float window_height = static_cast<float>(director_.getFramebufferHeight());
    if (items.empty() || height > window_height || window_width <= 2 * INSET)
        return;

    mstd::vector<MenuRow*> rows;
    float width = MIN_WIDTH;
    for (auto& item : items) {
        auto* row = MenuRow::create(director_, palette_, item.label);
        if (!row)
            return;
        width = mstd::max(width, row->preferred_width());
        row->addEventListener([this, action = mstd::move(item.activate)](zocos::ui::Widget&) {
            setVisible(false);
            action();
        });
        rows.push_back(row);
    }

    width = mstd::min(width, window_width);
    float y = height;
    for (MenuRow* row : rows) {
        y -= ROW_HEIGHT;
        row->layout(width, y);
        addChild(row);
    }
    setContentSize({width, height});
    setPosition({mstd::clamp(pointer.x, 0.f, window_width - width),
                 mstd::clamp(pointer.y + 1.f - height, 0.f, window_height - height)});
    setVisible(true);
}

}  // namespace zonix_gui
