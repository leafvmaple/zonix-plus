#include "dialog.h"
#include <2d/ZCLabel.h>
#include <base/ZCDirector.h>
#include <base/ZCEventListener.h>
#include <base/keyboard.h>

namespace zonix_gui {
namespace {

constexpr int LAYER = 2;
constexpr float WIDTH = 340.f;
constexpr float HEIGHT = 160.f;
constexpr float INSET = 16.f;

void set_text(zocos::Label& label, const mstd::string& text) {
    label.setString(text);
    label.updateLayout();
}

}  // namespace

Dialog* Dialog::create(zocos::Director& director) {
    auto* dialog = new (mstd::nothrow) Dialog(director);
    if (!dialog)
        return nullptr;
    dialog->autorelease();
    return dialog->build(director) ? dialog : nullptr;
}

bool Dialog::build(zocos::Director& director) {
    if (!Widget::init() || !palette_.init())
        return false;
    setAnchorPoint({0.f, 0.f});
    setContentSize(
        {static_cast<float>(director.getFramebufferWidth()), static_cast<float>(director.getFramebufferHeight())});
    panel_ = {(getContentSize().width - WIDTH) / 2.f, (getContentSize().height - HEIGHT) / 2.f};
    setLocalZOrder(LAYER);
    setVisible(false);

    auto label_at = [&](float y) {
        auto* label = control_style::make_label(director, "");
        if (label) {
            label->setAnchorPoint({0.f, 0.5f});
            label->setPosition({panel_.x + INSET, panel_.y + y});
            label->setRenderLayer(LAYER);
            addChild(label);
        }
        return label;
    };
    title_ = label_at(134.f);
    value_ = label_at(96.f);
    error_ = label_at(59.f);

    auto button_at = [&](const char* label, float x, Button::Action action) {
        auto* button = Button::create(director, label, mstd::move(action));
        if (button) {
            button->layout({80.f, 29.f});
            button->setPosition({panel_.x + x, panel_.y + 15.f});
            button->set_render_layer(LAYER);
            addChild(button);
        }
        return button;
    };
    ok_ = button_at("OK", 244.f, [this] { accept(); });
    delete_ = button_at("Delete", 244.f, [this] { accept(); });
    auto* cancel = button_at("Cancel", 154.f, [this] { close(); });
    auto* keys = zocos::EventListenerKeyboard::create([this](zocos::EventKeyboard& event) { key_pressed(event); });
    auto* mouse = zocos::EventListenerMouse::create();
    if (!title_ || !value_ || !error_ || !ok_ || !delete_ || !cancel || !keys || !mouse)
        return false;
    auto block_menu = [this](zocos::EventMouse& event) {
        if (isVisible())
            event.stopPropagation();
    };
    mouse->onMouseDown = block_menu;
    mouse->onMouseUp = block_menu;
    auto& dispatcher = director.getEventDispatcher();
    dispatcher.addEventListenerWithNodePriority(keys, this);
    dispatcher.addEventListenerWithNodePriority(mouse, this);
    return true;
}

void Dialog::show(const char* title, Submit submit) {
    set_text(*title_, title);
    set_text(*error_, "");
    submit_ = mstd::move(submit);
    ok_->setVisible(limit_ != 0);
    delete_->setVisible(limit_ == 0);
    setVisible(true);
}

void Dialog::prompt(const char* title, const mstd::string& initial, mstd::size_t limit, Submit submit) {
    input_ = initial;
    limit_ = limit;
    cursor_ = input_.size();
    select_all_ = !input_.empty();
    show(title, mstd::move(submit));
    update_input();
}

void Dialog::confirm(const char* title, const mstd::string& target, Submit submit) {
    input_.clear();
    limit_ = 0;
    show(title, mstd::move(submit));
    set_text(*value_, target);
}

void Dialog::close() {
    setVisible(false);
    submit_ = {};
}

void Dialog::accept() {
    if (const char* error = submit_(input_))
        set_text(*error_, error);
    else
        close();
}

void Dialog::key_pressed(zocos::EventKeyboard& event) {
    if (!isVisible())
        return;
    event.stopPropagation();
    if (event.getKeyCode() == DISPLAY_KEY_ESCAPE) {
        close();
    } else if (limit_ != 0) {
        int key = keyboard::KeyboardState::character(event.getKeyCode(), event.getModifiers());
        if (key == '\n' && !event.isRepeated())
            accept();
        else
            edit(key);
    }
}

void Dialog::edit(int key) {
    using keyboard::Key;
    bool printable = key >= ' ' && key <= '~';
    bool backspace = key == '\b';
    bool remove = key == static_cast<int>(Key::Delete);
    if (key == 1) {  // Ctrl-A
        select_all_ = !input_.empty();
    } else if (printable || backspace || remove) {
        if (select_all_) {
            input_.clear();
            cursor_ = 0;
        } else if (backspace && cursor_ > 0) {
            --cursor_;
            input_ = input_.substr(0, cursor_) + input_.substr(cursor_ + 1);
        } else if (remove && cursor_ < input_.size()) {
            input_ = input_.substr(0, cursor_) + input_.substr(cursor_ + 1);
        }
        select_all_ = false;
        if (printable && input_.size() < limit_) {
            const char byte[]{static_cast<char>(key), '\0'};
            input_ = input_.substr(0, cursor_) + byte + input_.substr(cursor_);
            ++cursor_;
        }
    } else if (key == static_cast<int>(Key::Home) || key == static_cast<int>(Key::Left)) {
        cursor_ = key == static_cast<int>(Key::Home) || select_all_ || cursor_ == 0 ? 0 : cursor_ - 1;
        select_all_ = false;
    } else if (key == static_cast<int>(Key::End) || key == static_cast<int>(Key::Right)) {
        cursor_ =
            key == static_cast<int>(Key::End) || select_all_ ? input_.size() : mstd::min(cursor_ + 1, input_.size());
        select_all_ = false;
    } else {
        return;
    }
    set_text(*error_, "");
    update_input();
}

void Dialog::update_input() {
    set_text(*value_, select_all_ ? input_ : input_.substr(0, cursor_) + "|" + input_.substr(cursor_));
}

void Dialog::draw(zocos::Renderer& renderer, const zocos::Mat4& world) {
    using control_style::Color;
    auto order = zocos::makeRenderSortKey(0, LAYER, 0);
    palette_.box(renderer, world, {panel_.x, panel_.y, WIDTH, HEIGHT}, Color::Border, order);
    palette_.box(renderer, world, {panel_.x + 1.f, panel_.y + 1.f, WIDTH - 2.f, HEIGHT - 2.f}, Color::Toolbar, order);
    if (limit_ != 0) {
        palette_.box(renderer, world, {panel_.x + INSET - 5.f, panel_.y + 81.f, WIDTH - 2.f * INSET + 10.f, 30.f},
                     select_all_ ? Color::Selected : Color::Sidebar, order);
    }
}

bool Dialog::hitTest(float x, float y) const {
    return isVisible() && Widget::hitTest(x, y);
}

}  // namespace zonix_gui
