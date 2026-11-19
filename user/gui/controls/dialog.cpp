#include "dialog.h"
#include <2d/ZCLabel.h>
#include <base/ZCDirector.h>
#include <base/ZCEventListener.h>
#include <base/keyboard.h>

namespace zonix_gui {
namespace {

constexpr int LAYER = 2;
constexpr float WIDTH = 320.f;
constexpr float HEIGHT = 160.f;
constexpr float RADIUS = 8.f;
constexpr float INSET = 16.f;
// Vertical positions are measured from the bottom of the panel.
constexpr float TITLE_Y = 134.f;
constexpr float FIELD_BOTTOM = 76.f;
constexpr float FIELD_HEIGHT = 32.f;
constexpr float FIELD_INSET = 10.f;
constexpr float VALUE_Y = FIELD_BOTTOM + FIELD_HEIGHT / 2.f;
constexpr float ERROR_Y = 60.f;
constexpr float BUTTON_WIDTH = 80.f;
constexpr float BUTTON_GAP = 8.f;

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
    panel_ = {static_cast<float>(static_cast<int>((getContentSize().width - WIDTH) / 2.f)),
              static_cast<float>(static_cast<int>((getContentSize().height - HEIGHT) / 2.f))};
    setLocalZOrder(LAYER);
    setVisible(false);

    auto add_label = [&] {
        auto* label = control_style::make_label(director, "");
        if (label) {
            label->setRenderLayer(LAYER);
            addChild(label);
        }
        return label;
    };
    title_ = add_label();
    value_ = add_label();
    error_ = add_label();

    auto button_at = [&](const char* label, float x, Button::Style style, Button::Action action) {
        auto* button = Button::create(director, label, mstd::move(action));
        if (button) {
            button->set_style(style);
            button->layout({BUTTON_WIDTH, control_style::CONTROL_HEIGHT});
            button->setPosition({panel_.x + x, panel_.y + INSET});
            button->set_render_layer(LAYER);
            addChild(button);
        }
        return button;
    };
    constexpr float accept_x = WIDTH - INSET - BUTTON_WIDTH;
    ok_ = button_at("OK", accept_x, Button::Style::Accent, [this] { accept(); });
    delete_ = button_at("Delete", accept_x, Button::Style::Danger, [this] { accept(); });
    discard_ = button_at("Discard", accept_x, Button::Style::Danger, [this] { accept(); });
    auto* cancel = button_at("Cancel", accept_x - BUTTON_GAP - BUTTON_WIDTH, Button::Style::Raised,
                             [this] { close(); });
    auto* keys = zocos::EventListenerKeyboard::create([this](zocos::EventKeyboard& event) { key_pressed(event); });
    auto* mouse = zocos::EventListenerMouse::create();
    if (!title_ || !value_ || !error_ || !ok_ || !delete_ || !discard_ || !cancel || !keys || !mouse)
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
    error_->setOpacity(0.85f);
    return true;
}

void Dialog::write(zocos::Label& label, const mstd::string& text, float inset, float y) {
    label.setString(text);
    control_style::place_label(label, panel_.x + inset, panel_.y + y, 0.f, WIDTH - 2.f * inset);
}

void Dialog::show(const char* title, Submit submit) {
    write(*title_, title, INSET, TITLE_Y);
    write(*error_, "", INSET, ERROR_Y);
    submit_ = mstd::move(submit);
    ok_->setVisible(limit_ != 0);
    delete_->setVisible(limit_ == 0 && danger_ == Danger::Delete);
    discard_->setVisible(limit_ == 0 && danger_ == Danger::Discard);
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

void Dialog::confirm(const char* title, const mstd::string& target, Submit submit, Danger danger) {
    input_.clear();
    limit_ = 0;
    danger_ = danger;
    show(title, mstd::move(submit));
    write(*value_, target, INSET, VALUE_Y);
}

void Dialog::close() {
    setVisible(false);
    submit_ = {};
}

void Dialog::accept() {
    if (const char* error = submit_(input_))
        write(*error_, error, INSET, ERROR_Y);
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
    write(*error_, "", INSET, ERROR_Y);
    update_input();
}

void Dialog::update_input() {
    write(*value_, select_all_ ? input_ : input_.substr(0, cursor_) + "|" + input_.substr(cursor_),
          INSET + FIELD_INSET, VALUE_Y);
}

void Dialog::draw(zocos::Renderer& renderer, const zocos::Mat4& world) {
    using control_style::Color;
    auto order = zocos::makeRenderSortKey(0, LAYER, 0);
    const auto size = getContentSize();
    palette_.box(renderer, world, {0.f, 0.f, size.width, size.height}, Color::Scrim, order);
    palette_.round_box(renderer, world, {panel_.x, panel_.y - 4.f, WIDTH, HEIGHT}, RADIUS, Color::Shadow, order);
    palette_.frame(renderer, world, {panel_.x, panel_.y, WIDTH, HEIGHT}, RADIUS, Color::Panel, Color::Border, order);
    if (limit_ != 0) {
        palette_.frame(renderer, world, {panel_.x + INSET, panel_.y + FIELD_BOTTOM, WIDTH - 2.f * INSET, FIELD_HEIGHT},
                       control_style::CONTROL_RADIUS, select_all_ ? Color::Selected : Color::Field,
                       select_all_ ? Color::SelectedBorder : Color::Divider, order);
    }
}

bool Dialog::hitTest(float x, float y) const {
    return isVisible() && Widget::hitTest(x, y);
}

}  // namespace zonix_gui
