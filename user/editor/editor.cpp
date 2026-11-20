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
constexpr int control(char letter) {
    return letter - 'a' + 1;
}
// The clipboard is a file, so every editor window shares it.
constexpr const char* CLIPBOARD_PATH = "/tmp/clipboard";
constexpr const char* UNTITLED = "Untitled";

// Shows the part of the document around the cursor in the console font. A
// click puts the cursor under the pointer, and dragging or Shift-clicking
// selects.
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
    static constexpr zocos::Color4B SELECTION_COLOR = zonix_gui::opaque_color(zonix_gui::theme::SELECTION);
    static constexpr float CURSOR_WIDTH = 2.f;
    static constexpr float MIN_THUMB = 16.f;

    Document& document_;
    OnPlace on_place_;
    MonoFont font_;
    mstd::vector<zocos::QuadVertex> quads_;
    // The first line and display column shown.
    int top_{};
    int left_{};
    // The left button went down in the text and has not come up.
    bool dragging_{};

    TextView(Document& document, OnPlace on_place) : document_(document), on_place_(mstd::move(on_place)) {}

    bool build(zocos::Director& director) {
        auto* device = director.getRenderDevice();
        auto* mouse = zocos::EventListenerMouse::create();
        if (!Node::init() || !device || !mouse || font_.init(*device) < 0)
            return false;
        setAnchorPoint({0.f, 0.f});
        setContentSize({static_cast<float>(WIDTH), TEXT_HEIGHT});
        // Every cell twice (its selection and its glyph), the cursor and the scrollbar.
        quads_.reserve((2 * ROWS * COLS + 2) * 6);
        mouse->onMouseDown = [this](zocos::EventMouse& event) {
            if (event.getMouseButton() != zocos::EventMouse::MouseButton::BUTTON_LEFT)
                return;
            bool extend = (static_cast<unsigned int>(event.getModifiers()) & DISPLAY_MOD_SHIFT) != 0;
            dragging_ = point({event.getX(), event.getY()}, extend, false);
        };
        mouse->onMouseMove = [this](zocos::EventMouse& event) {
            if (dragging_)
                point({event.getX(), event.getY()}, true, true);
        };
        mouse->onMouseUp = [this](zocos::EventMouse& event) {
            if (event.getMouseButton() == zocos::EventMouse::MouseButton::BUTTON_LEFT)
                dragging_ = false;
        };
        director.getEventDispatcher().addEventListenerWithNodePriority(mouse, this);
        return true;
    }

    // Moves the cursor to the pointer, extending the selection if asked.
    // Outside the text only a drag in progress counts, clamped to the edge.
    bool point(zocos::Vec2 pointer, bool extend, bool clamp) {
        const auto size = getContentSize();
        zocos::Vec2 local;
        if (!convertToNodeSpace(pointer, local))
            return false;
        bool inside = local.x >= 0.f && local.x < size.width && local.y >= 0.f && local.y < size.height;
        if (!inside && !clamp)
            return false;
        int row = static_cast<int>((size.height - PAD_Y - local.y) / MonoFont::HEIGHT);
        if (size.height - PAD_Y - local.y < 0.f)
            row = -1;
        // The cursor goes to the nearer edge of the character under the pointer.
        int column = static_cast<int>((local.x - PAD_X + MonoFont::WIDTH / 2.f) / MonoFont::WIDTH);
        document_.place(top_ + mstd::clamp(row, clamp ? -1 : 0, clamp ? ROWS : ROWS - 1), left_ + mstd::max(column, 0),
                        extend);
        on_place_();
        return true;
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
        const mstd::size_t selected_from = document_.selection_start();
        const mstd::size_t selected_to = document_.selection_end();
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
        // Selected characters, and the line end of a selected line break, sit
        // on a band as wide as they are shown.
        auto selection_at = [&](int row, int column, int next) {
            if (offset < selected_from || offset >= selected_to)
                return;
            int from = mstd::max(column, left_);
            int to = mstd::min(next, left_ + COLS);
            if (from >= to)
                return;
            auto area = cell_area(row, from);
            area.width = static_cast<float>((to - from) * MonoFont::WIDTH);
            add(area, MonoFont::solid(), SELECTION_COLOR);
        };
        for (int row = 0; row < ROWS && top_ + row < lines; ++row) {
            int column = 0;
            for (; offset < document_.size() && text[offset] != '\n'; ++offset) {
                cursor_at(row, column);
                char byte = text[offset];
                int next = Document::advance(column, byte);
                selection_at(row, column, next);
                if (byte != ' ' && byte != '\t' && column >= left_ && column < left_ + COLS)
                    add(cell_area(row, column), MonoFont::cell(byte), TEXT_COLOR);
                column = next;
            }
            if (offset < document_.size())
                selection_at(row, column, column + 1);
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
    // A null path starts an untitled document.
    long run(const char* path) {
        FileStat font{};
        long error = sys_stat(const_cast<char*>(zonix_gui::UI_FONT_PATH), &font);
        if (error < 0)
            return error;
        if (!document_.ready())
            return SYS_ERROR_FULL;
        // A file that cannot be edited still opens a window that says why.
        long loaded = path ? document_.load(path) : 0;
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
            if (scene && build_scene(director, *scene)) {
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
    zonix_gui::Toolbar* toolbar_{};
    // What Find looked for last.
    mstd::string search_;
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

    bool build_scene(zocos::Director& director, zocos::Scene& scene) {
        auto* bar = new (mstd::nothrow) StatusBar(director);
        if (!bar || !bar->init()) {
            delete bar;
            return false;
        }
        bar->autorelease();
        scene.addChild(bar);

        toolbar_ = zonix_gui::Toolbar::create(director, WIDTH);
        if (!toolbar_)
            return false;
        toolbar_->setPosition({0.f, HEIGHT - TOOLBAR_HEIGHT});
        scene.addChild(toolbar_);
        save_ = toolbar_->add_button("Save", [this] {
            save();
            update_controls();
        });
        show_path();

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

    void show_path() { toolbar_->set_text(document_.untitled() ? UNTITLED : document_.path()); }

    // An untitled document asks for a name first.
    void save() {
        if (read_only())
            return;
        if (document_.untitled()) {
            save_as();
            return;
        }
        if (!document_.modified())
            return;
        long error = document_.save();
        message_ = error < 0 ? mstd::string("Not saved: ") + command_error_text(error) : mstd::string("Saved");
    }

    void save_as() {
        if (read_only() || dialog_->isVisible())
            return;
        mstd::string initial = document_.untitled() ? mstd::string("/home/") : mstd::string(document_.path());
        dialog_->prompt("Save as", initial, PROCESS_MAX_PATH - 1, [this](const mstd::string& path) -> const char* {
            if (path.empty() || path[0] != '/')
                return "Use a full path, such as /home/notes.txt";
            FileStat st{};
            bool same = !document_.untitled() && strcmp(const_cast<char*>(path.c_str()),
                                                        const_cast<char*>(document_.path())) == 0;
            if (!same && sys_stat(const_cast<char*>(path.c_str()), &st) == 0)
                return st.type == FILE_TYPE_DIRECTORY ? "That is a folder" : "That file already exists";
            long error = document_.save_as(path.c_str());
            if (error < 0)
                return command_error_text(error);
            message_ = "Saved";
            show_path();
            update_controls();
            return nullptr;
        });
    }

    // The selection goes to the clipboard file, which it replaces.
    bool copy() {
        if (!document_.has_selection())
            return false;
        int fd = sys_open(const_cast<char*>(CLIPBOARD_PATH), O_WRONLY | O_CREAT | O_CLOEXEC);
        long error = fd;
        if (fd >= 0) {
            error = sys_ftruncate(fd, 0);
            if (error == 0)
                error = command_write_all(fd, document_.data() + document_.selection_start(),
                                          static_cast<long>(document_.selection_end() - document_.selection_start()));
            if (sys_close(fd) < 0 && error == 0)
                error = SYS_ERROR_IO;
        }
        if (error < 0) {
            message_ = mstd::string("Not copied: ") + command_error_text(error);
            return false;
        }
        return true;
    }

    void paste() {
        int fd = sys_open(const_cast<char*>(CLIPBOARD_PATH), O_RDONLY | O_CLOEXEC);
        if (fd == SYS_ERROR_NOT_FOUND) {
            message_ = "The clipboard is empty";
            return;
        }
        if (fd < 0) {
            message_ = mstd::string("Not pasted: ") + command_error_text(fd);
            return;
        }
        mstd::vector<char> text;
        text.resize(Document::CAPACITY + 1);
        mstd::size_t length = 0;
        long count;
        while (length < text.size() && (count = sys_read(fd, text.data() + length, text.size() - length)) > 0)
            length += static_cast<mstd::size_t>(count);
        sys_close(fd);
        if (length > Document::CAPACITY || !document_.insert(text.data(), length))
            message_ = "The clipboard does not fit or is not ASCII text";
    }

    void find(const mstd::string& needle) {
        if (!needle.empty() && !document_.find(needle.c_str(), needle.size()))
            message_ = "Not found: " + needle;
    }

    void open_find() {
        if (dialog_->isVisible())
            return;
        dialog_->prompt("Find", search_, 64, [this](const mstd::string& needle) -> const char* {
            if (needle.empty())
                return "Type the text to find";
            search_ = needle;
            if (!document_.find(needle.c_str(), needle.size()))
                return "Not found";
            update_controls();
            return nullptr;
        });
    }

    // Closes at once unless there are changes to lose.
    void request_close() {
        if (!document_.modified()) {
            done_ = true;
        } else if (!dialog_->isVisible()) {
            dialog_->confirm(
                "Discard unsaved changes?", document_.untitled() ? UNTITLED : document_.path(),
                [this](const mstd::string&) -> const char* {
                    done_ = true;
                    return nullptr;
                },
                Dialog::Danger::Discard);
        }
    }

    void type(char byte) {
        if (!document_.type(byte))
            message_ = "The file has reached 64 KiB";
    }

    void handle_key(const zocos::EventKeyboard& event) {
        using keyboard::Key;
        auto code = static_cast<unsigned int>(event.getKeyCode());
        auto modifiers = static_cast<unsigned int>(event.getModifiers());
        int key = keyboard::KeyboardState::character(code, modifiers);
        bool ctrl = (modifiers & DISPLAY_MOD_CTRL) != 0;
        bool shift = (modifiers & DISPLAY_MOD_SHIFT) != 0;
        bool repeated = event.isRepeated();
        message_.clear();

        if (code == KEY_PAGE_UP) {
            document_.vertical(-(TextView::ROWS - 1), shift);
        } else if (code == KEY_PAGE_DOWN) {
            document_.vertical(TextView::ROWS - 1, shift);
        } else if (key == static_cast<int>(Key::Up)) {
            document_.vertical(-1, shift);
        } else if (key == static_cast<int>(Key::Down)) {
            document_.vertical(1, shift);
        } else if (key == static_cast<int>(Key::Left)) {
            document_.left(shift);
        } else if (key == static_cast<int>(Key::Right)) {
            document_.right(shift);
        } else if (key == static_cast<int>(Key::Home)) {
            ctrl ? document_.start(shift) : document_.home(shift);
        } else if (key == static_cast<int>(Key::End)) {
            ctrl ? document_.finish(shift) : document_.end(shift);
        } else if (ctrl) {
            // Control characters are commands, never text.
            command(key, shift, repeated);
        } else if (!read_only()) {
            if (key == static_cast<int>(Key::Delete))
                document_.erase_after();
            else if (key == '\b')
                document_.erase_before();
            else if (key == '\n' || key == '\t' || (key >= ' ' && key <= '~'))
                type(static_cast<char>(key));
        }
        update_controls();
    }

    void command(int key, bool shift, bool repeated) {
        if (key == control('q')) {
            request_close();
        } else if (key == control('a')) {
            document_.select_all();
        } else if (key == control('c') && !repeated) {
            copy();
        } else if (key == control('f') && !repeated) {
            open_find();
        } else if (key == control('g')) {
            find(search_);
        } else if (read_only()) {
            return;
        } else if (key == control('s') && !repeated) {
            shift ? save_as() : save();
        } else if (key == control('x') && !repeated) {
            if (copy())
                document_.erase_before();
        } else if (key == control('v') && !repeated) {
            paste();
        } else if (key == control('z')) {
            shift ? document_.redo() : document_.undo();
        } else if (key == control('y')) {
            document_.redo();
        }
    }

    void update_controls() {
        view_->reveal();
        save_->set_enabled(!read_only() && (document_.modified() || document_.untitled()));
        if (read_only())
            status_->setString(problem_);
        else if (!message_.empty())
            status_->setString(message_);
        else
            status_->setString(document_.modified() ? "Modified" : "");
        if (document_.has_selection())
            position_->setString(mstd::to_string(document_.selection_end() - document_.selection_start()) +
                                 " selected");
        else
            position_->setString("Ln " + mstd::to_string(document_.line() + 1) + ", Col " +
                                 mstd::to_string(document_.column() + 1));
        place_label(*status_, STATUS_INSET, STATUS_HEIGHT / 2.f, 0.f, WIDTH - POSITION_WIDTH - 2.f * STATUS_INSET);
        place_label(*position_, WIDTH - STATUS_INSET, STATUS_HEIGHT / 2.f, 1.f, POSITION_WIDTH - STATUS_INSET);
    }
};

}  // namespace

extern "C" int main(int argc, char** argv) {
    if (argc > 2 || (argc == 2 && argv[1][0] != '/'))
        return command_usage("Open Text Editor from the Dock, or a file from Files, in Desktop.\n");
    zonix_gui::control_style::TextSize::set(zonix_gui::Settings::load().text_size);
    Editor editor;
    long error = editor.run(argc == 2 ? argv[1] : nullptr);
    zonix_gui::finish();
    return error < 0 ? command_error("editor", nullptr, error) : 0;
}
