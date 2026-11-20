#pragma once

#include "button.h"

namespace zocos {
class EventKeyboard;
}  // namespace zocos

namespace zonix_gui {

class Dialog final : public zocos::ui::Widget {
public:
    // A null return accepts the action. An error message keeps the dialog open.
    using Submit = mstd::function<const char*(const mstd::string&)>;

    static Dialog* create(zocos::Director& director);
    void prompt(const char* title, const mstd::string& initial, mstd::size_t limit, Submit submit);
    // What the button that confirms a destructive action says.
    enum class Danger { Delete, Discard };
    void confirm(const char* title, const mstd::string& target, Submit submit, Danger danger = Danger::Delete);
    // Covers a window of a new size, keeping the panel centred.
    void fit(zocos::Size window);

private:
    explicit Dialog(zocos::Director& director) : palette_(director) {}
    bool build(zocos::Director& director);
    // Sets a label's text `inset` pixels from the panel's left edge, centred on `y`.
    void write(zocos::Label& label, const mstd::string& text, float inset, float y);
    void show(const char* title, Submit submit);
    void close();
    void accept();
    void key_pressed(zocos::EventKeyboard& event);
    void edit(int key);
    void update_input();
    void draw(zocos::Renderer& renderer, const zocos::Mat4& world) override;
    bool hitTest(float x, float y) const override;

    control_style::Palette palette_;
    zocos::Vec2 panel_{};
    zocos::Label* title_{};
    zocos::Label* value_{};
    zocos::Label* error_{};
    Button* ok_{};
    Button* delete_{};
    Button* discard_{};
    Danger danger_ = Danger::Delete;
    Submit submit_;
    mstd::string input_;
    mstd::size_t cursor_{};
    mstd::size_t limit_{};
    bool select_all_{};
};

}  // namespace zonix_gui
