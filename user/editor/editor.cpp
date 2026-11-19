#include "../command.h"
#include "document.h"
#include "../gui/controls/button.h"
#include "../gui/controls/dialog.h"
#include "../gui/controls/toolbar.h"
#include "../gui/mono_font.h"
#include "../gui/platform.h"
#include "../gui/settings.h"
#include "../gui/theme.h"
#include "../gui/window_channel.h"
#include <2d/ZCLabel.h>
#include <base/ZCAutoreleasePool.h>
#include <base/ZCDirector.h>
#include <base/ZCEventListener.h>
#include <base/keyboard.h>

namespace {

using editor::Document;
using zonix_gui::Button;
using zonix_gui::Dialog;
using zonix_gui::MonoFont;
using zonix_gui::control_style::Color;
using zonix_gui::control_style::Palette;
using zonix_gui::control_style::place_label;

// The window has the same size as the Terminal. Node coordinates have a
// bottom-left origin.
constexpr int WIDTH = desktop_ipc::TERMINAL_WIDTH;
constexpr int HEIGHT = desktop_ipc::TERMINAL_HEIGHT;
constexpr float TOOLBAR_HEIGHT = zonix_gui::Toolbar::HEIGHT;
constexpr float STATUS_HEIGHT = 28.f;
constexpr float STATUS_INSET = 12.f;
// The cursor position is right-aligned in the status bar.
constexpr float POSITION_WIDTH = 120.f;
constexpr float TEXT_HEIGHT = HEIGHT - TOOLBAR_HEIGHT - STATUS_HEIGHT;

// Extended keys the shared keyboard table does not name.
constexpr unsigned int KEY_PAGE_UP = 104;
constexpr unsigned int KEY_PAGE_DOWN = 109;
constexpr int CTRL_Q = 'q' - 'a' + 1;
constexpr int CTRL_S = 's' - 'a' + 1;

// Shows the part of the document around the cursor in the console font and
// puts the cursor where the pointer clicks.
class TextView final : public zocos::Node {
public:
    static constexpr float PAD_X = 8.f;
    static constexpr float PAD_Y = 6.f;
    static constexpr float SCROLLBAR_WIDTH = 4.f;
    static constexpr float SCROLLBAR_MARGIN = 4.f;
    static constexpr int ROWS = static_cast<int>((TEXT_HEIGHT - 2.f * PAD_Y) / MonoFont::HEIGHT);
    static constexpr int COLS =
        static_cast<int>((WIDTH - PAD_X - SCROLLBAR_WIDTH - 2.f * SCROLLBAR_MARGIN) / MonoFont::WIDTH);

    using OnPlace = mstd::function<void()>;

    static TextView* create(zocos::Director& director, Document& document, OnPlace on_place) {
        auto* view = new (mstd::nothrow) TextView(document, mstd::move(on_place));
        if (!view)
            return nullptr;
        view->autorelease();
        return view->build(director) ? view : nullptr;
    }

    // Scrolls just far enough to show the cursor.
    void reveal() {
        int line = document_.line();
        int column = document_.column();
        if (line < top_)
            top_ = line;
        else if (line >= top_ + ROWS)
            top_ = line - ROWS + 1;
        if (column < left_)
            left_ = column;
        else if (column >= left_ + COLS)
            left_ = column - COLS + 1;
    }

private:
    static constexpr zocos::Color4B TEXT_COLOR = zonix_gui::opaque_color(zonix_gui::theme::TEXT);
    static constexpr zocos::Color4B CURSOR_COLOR = zonix_gui::opaque_color(zonix_gui::theme::ACCENT);
    static constexpr zocos::Color4B THUMB_COLOR = zonix_gui::opaque_color(zonix_gui::theme::WINDOW_BORDER);
    static constexpr float CURSOR_WIDTH = 2.f;
    static constexpr float MIN_THUMB = 16.f;

    Document& document_;
    OnPlace on_place_;
    MonoFont font_;
    mstd::vector<zocos::QuadVertex> quads_;
    // The first line and display column shown.
    int top_{};
    int left_{};

    TextView(Document& document, OnPlace on_place) : document_(document), on_place_(mstd::move(on_place)) {}

    bool build(zocos::Director& director) {
        auto* device = director.getRenderDevice();
        auto* mouse = zocos::EventListenerMouse::create();
        if (!Node::init() || !device || !mouse || font_.init(*device) < 0)
            return false;
        setAnchorPoint({0.f, 0.f});
        setContentSize({static_cast<float>(WIDTH), TEXT_HEIGHT});
        // Every cell, the cursor and the scrollbar.
        quads_.reserve((ROWS * COLS + 2) * 6);
        mouse->onMouseDown = [this](zocos::EventMouse& event) {
            if (event.getMouseButton() == zocos::EventMouse::MouseButton::BUTTON_LEFT)
                click({event.getX(), event.getY()});
        };
        director.getEventDispatcher().addEventListenerWithNodePriority(mouse, this);
        return true;
    }

    void click(zocos::Vec2 pointer) {
        const auto size = getContentSize();
        zocos::Vec2 local;
        if (!convertToNodeSpace(pointer, local) || local.x < 0.f || local.x >= size.width || local.y < 0.f ||
            local.y >= size.height)
            return;
        int row = static_cast<int>((size.height - PAD_Y - local.y) / MonoFont::HEIGHT);
        // The cursor goes to the nearer edge of the character under the pointer.
        int column = static_cast<int>((local.x - PAD_X + MonoFont::WIDTH / 2.f) / MonoFont::WIDTH);
        document_.place(top_ + mstd::clamp(row, 0, ROWS - 1), left_ + mstd::max(column, 0));
        on_place_();
    }

    void add(zocos::Rect area, zocos::Rect cell, zocos::Color4B color) {
        const float x1 = area.x + area.width;
        const float y1 = area.y + area.height;
        const float u1 = cell.x + cell.width;
        const float v1 = cell.y + cell.height;
        quads_.push_back({{area.x, area.y}, {cell.x, cell.y}, color});
        quads_.push_back({{x1, area.y}, {u1, cell.y}, color});
        quads_.push_back({{x1, y1}, {u1, v1}, color});
        quads_.push_back({{area.x, area.y}, {cell.x, cell.y}, color});
        quads_.push_back({{x1, y1}, {u1, v1}, color});
        quads_.push_back({{area.x, y1}, {cell.x, v1}, color});
    }

    void draw(zocos::Renderer& renderer, const zocos::Mat4& world) override {
        const float top_edge = getContentSize().height - PAD_Y;
        const char* text = document_.data();
        const int lines = document_.line_count();
        mstd::size_t offset = document_.line_offset(top_);
        quads_.clear();

        auto cell_area = [&](int row, int column) {
            return zocos::Rect{PAD_X + static_cast<float>((column - left_) * MonoFont::WIDTH),
                               top_edge - static_cast<float>((row + 1) * MonoFont::HEIGHT),
                               static_cast<float>(MonoFont::WIDTH), static_cast<float>(MonoFont::HEIGHT)};
        };
        auto cursor_at = [&](int row, int column) {
            if (offset != document_.cursor() || column < left_ || column > left_ + COLS)
                return;
            // The bar sits between two characters, half on each.
            auto area = cell_area(row, column);
            area.x -= CURSOR_WIDTH / 2.f;
            area.width = CURSOR_WIDTH;
            add(area, MonoFont::solid(), CURSOR_COLOR);
        };
        for (int row = 0; row < ROWS && top_ + row < lines; ++row) {
            int column = 0;
            for (; offset < document_.size() && text[offset] != '\n'; ++offset) {
                cursor_at(row, column);
                char byte = text[offset];
                if (byte != ' ' && byte != '\t' && column >= left_ && column < left_ + COLS)
                    add(cell_area(row, column), MonoFont::cell(byte), TEXT_COLOR);
                column = Document::advance(column, byte);
            }
            cursor_at(row, column);
            ++offset;
        }

        if (lines > ROWS) {
            const float track = TEXT_HEIGHT - 2.f * PAD_Y;
            const float thumb = mstd::max(MIN_THUMB, track * ROWS / lines);
            const float travel = mstd::min(1.f, static_cast<float>(top_) / (lines - ROWS));
            const float y = static_cast<float>(static_cast<int>(top_edge - thumb - (track - thumb) * travel));
            add({WIDTH - SCROLLBAR_MARGIN - SCROLLBAR_WIDTH, y, SCROLLBAR_WIDTH, thumb}, MonoFont::solid(),
                THUMB_COLOR);
        }
        if (!quads_.empty())
            renderer.addDrawQuads(world, font_.texture(), quads_);
    }
};

class StatusBar final : public zocos::Node {
public:
    explicit StatusBar(zocos::Director& director) : palette_(director) {}

    bool init() override { return Node::init() && palette_.init(); }

    void draw(zocos::Renderer& renderer, const zocos::Mat4& world) override {
        palette_.box(renderer, world, {0.f, 0.f, WIDTH, STATUS_HEIGHT}, Color::Panel);
        palette_.box(renderer, world, {0.f, STATUS_HEIGHT - 1.f, WIDTH, 1.f}, Color::Divider);
    }

private:
    Palette palette_;
};

class Editor {
public:
    long run(const char* path) {
        FileStat font{};
        long error = sys_stat(const_cast<char*>(zonix_gui::UI_FONT_PATH), &font);
        if (error < 0)
            return error;
        // A file that cannot be edited still opens a window that says why.
        long loaded = document_.load(path);
        if (loaded == SYS_ERROR_NOT_SUPPORTED)
            problem_ = "Not an ASCII text file";
        else if (loaded == SYS_ERROR_FULL)
            problem_ = "The file is larger than 64 KiB";
        else if (loaded < 0)
            problem_ = mstd::string("Cannot open: ") + command_error_text(loaded);

        auto& director = zocos::Director::getInstance();
        auto view = zonix_gui::make_window_view({}, [this] { request_close(); });
        if (!director.init(WIDTH, HEIGHT, "Text Editor", mstd::move(view)))
            return SYS_ERROR_IO;
        {
            zocos::AutoreleasePool startup("Editor controls");
            auto* scene = zocos::Scene::create();
            if (scene && build_scene(director, *scene, path)) {
                update_controls();
                director.runWithScene(scene);
                while (!done_ && director.mainLoop()) {}
            } else {
                error = SYS_ERROR_FULL;
            }
        }
        director.shutdown();
        return error;
    }

private:
    Document document_;
    TextView* view_{};
    Dialog* dialog_{};
    Button* save_{};
    zocos::Label* status_{};
    zocos::Label* position_{};
    // Why the file cannot be edited, or empty.
    mstd::string problem_;
    // The result of the last action, until the next key.
    mstd::string message_;
    bool done_{};

    bool read_only() const { return !problem_.empty(); }

    static zocos::Label* add_text(zocos::Director& director, zocos::Scene& scene) {
        auto* label = zonix_gui::control_style::make_label(director, "");
        if (label) {
            label->setOpacity(0.85f);
            scene.addChild(label);
        }
        return label;
    }

    bool build_scene(zocos::Director& director, zocos::Scene& scene, const char* path) {
        auto* bar = new (mstd::nothrow) StatusBar(director);
        if (!bar || !bar->init()) {
            delete bar;
            return false;
        }
        bar->autorelease();
        scene.addChild(bar);

        auto* toolbar = zonix_gui::Toolbar::create(director, WIDTH);
        if (!toolbar)
            return false;
        toolbar->setPosition({0.f, HEIGHT - TOOLBAR_HEIGHT});
        scene.addChild(toolbar);
        save_ = toolbar->add_button("Save", [this] {
            save();
            update_controls();
        });
        toolbar->set_text(path);

        view_ = TextView::create(director, document_, [this] {
            message_.clear();
            update_controls();
        });
        if (!view_)
            return false;
        view_->setPosition({0.f, STATUS_HEIGHT});
        scene.addChild(view_);

        status_ = add_text(director, scene);
        position_ = add_text(director, scene);
        dialog_ = Dialog::create(director);
        auto* keys = zocos::EventListenerKeyboard::create([this](zocos::EventKeyboard& event) { handle_key(event); });
        if (!save_ || !status_ || !position_ || !dialog_ || !keys)
            return false;
        scene.addChild(dialog_, dialog_->getLocalZOrder());
        director.getEventDispatcher().addEventListenerWithNodePriority(keys, &scene);
        return true;
    }

    void save() {
        if (read_only() || !document_.modified())
            return;
        long error = document_.save();
        message_ = error < 0 ? mstd::string("Not saved: ") + command_error_text(error) : mstd::string("Saved");
    }

    // Closes at once unless there are changes to lose.
    void request_close() {
        if (!document_.modified()) {
            done_ = true;
        } else if (!dialog_->isVisible()) {
            dialog_->confirm(
                "Discard unsaved changes?", document_.path(),
                [this](const mstd::string&) -> const char* {
                    done_ = true;
                    return nullptr;
                },
                Dialog::Danger::Discard);
        }
    }

    void insert(char byte) {
        if (!document_.insert(byte))
            message_ = "The file has reached 64 KiB";
    }

    void handle_key(const zocos::EventKeyboard& event) {
        using keyboard::Key;
        auto code = static_cast<unsigned int>(event.getKeyCode());
        auto modifiers = static_cast<unsigned int>(event.getModifiers());
        int key = keyboard::KeyboardState::character(code, modifiers);
        bool ctrl = (modifiers & DISPLAY_MOD_CTRL) != 0;
        message_.clear();

        if (code == KEY_PAGE_UP) {
            document_.vertical(-(TextView::ROWS - 1));
        } else if (code == KEY_PAGE_DOWN) {
            document_.vertical(TextView::ROWS - 1);
        } else if (key == static_cast<int>(Key::Up)) {
            document_.vertical(-1);
        } else if (key == static_cast<int>(Key::Down)) {
            document_.vertical(1);
        } else if (key == static_cast<int>(Key::Left)) {
            document_.left();
        } else if (key == static_cast<int>(Key::Right)) {
            document_.right();
        } else if (key == static_cast<int>(Key::Home)) {
            ctrl ? document_.start() : document_.home();
        } else if (key == static_cast<int>(Key::End)) {
            ctrl ? document_.finish() : document_.end();
        } else if (ctrl) {
            // Control characters are commands, never text.
            if (key == CTRL_Q)
                request_close();
            else if (key == CTRL_S && !event.isRepeated())
                save();
        } else if (!read_only()) {
            if (key == static_cast<int>(Key::Delete))
                document_.erase_after();
            else if (key == '\b')
                document_.erase_before();
            else if (key == '\n' || key == '\t' || (key >= ' ' && key <= '~'))
                insert(static_cast<char>(key));
        }
        update_controls();
    }

    void update_controls() {
        view_->reveal();
        save_->set_enabled(document_.modified() && !read_only());
        if (read_only())
            status_->setString(problem_);
        else if (!message_.empty())
            status_->setString(message_);
        else
            status_->setString(document_.modified() ? "Modified" : "");
        position_->setString("Ln " + mstd::to_string(document_.line() + 1) + ", Col " +
                             mstd::to_string(document_.column() + 1));
        place_label(*status_, STATUS_INSET, STATUS_HEIGHT / 2.f, 0.f, WIDTH - POSITION_WIDTH - 2.f * STATUS_INSET);
        place_label(*position_, WIDTH - STATUS_INSET, STATUS_HEIGHT / 2.f, 1.f, POSITION_WIDTH - STATUS_INSET);
    }
};

}  // namespace

extern "C" int main(int argc, char** argv) {
    if (argc != 2 || argv[1][0] != '/')
        return command_usage("Open a file from Files in Desktop.\n");
    zonix_gui::control_style::TextSize::set(zonix_gui::Settings::load().text_size);
    Editor editor;
    long error = editor.run(argv[1]);
    zonix_gui::finish();
    return error < 0 ? command_error("editor", nullptr, error) : 0;
}
