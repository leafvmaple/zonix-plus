#pragma once

#include "style.h"
#include <2d/ZCNode.h>

namespace zocos {
class Director;
class EventMouse;
class Scene;
}  // namespace zocos

namespace zonix_gui {

class ContextMenu final : public zocos::Node {
public:
    struct Item {
        mstd::string label;
        mstd::function<void()> activate;
    };
    using Items = mstd::vector<Item>;
    using ItemProvider = mstd::function<Items(zocos::Vec2)>;

    // The scene owns the menu. The provider receives window coordinates with a
    // bottom-left origin. An empty list leaves the menu closed.
    static bool attach(zocos::Director& director, zocos::Scene& scene, ItemProvider items_at);

    void onExit() override;

private:
    ContextMenu(zocos::Director& director, ItemProvider items_at);
    bool init() override;
    bool contains(zocos::Vec2 pointer) const;
    void mouse_down(const zocos::EventMouse& event);
    void mouse_up(const zocos::EventMouse& event);
    void open(zocos::Vec2 pointer);
    void draw(zocos::Renderer& renderer, const zocos::Mat4& world) override;

    zocos::Director& director_;
    ItemProvider items_at_;
    control_style::Palette palette_;
    bool right_pressed_{};
};

}  // namespace zonix_gui
