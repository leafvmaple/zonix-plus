#pragma once

#include "style.h"
#include <2d/ZCNode.h>

namespace zonix_gui {

class IconGrid final : public zocos::Node {
public:
    enum class Icon { File, Folder };
    enum class Interaction { Select, Activate };

    struct Item {
        const char* name;
        Icon icon;
    };
    using OnInteraction = mstd::function<void(Interaction)>;
    static constexpr int NO_ITEM = -1;

    static IconGrid* create(zocos::Director& director, zocos::Size size, OnInteraction on_interaction);

    int columns() const { return columns_; }
    // Lays the tiles out for a new size, adding or removing tiles so the grid
    // holds as many as fit. Clears the selection. False if none fits.
    bool resize(zocos::Director& director, zocos::Size size);
    int capacity() const { return static_cast<int>(tiles_.size()); }
    // Copies labels for display. The caller can discard the items after this call.
    void set_items(const mstd::vector<Item>& items);
    int selection() const { return selected_; }
    void select(int index);
    // Selects the item at a window coordinate, or clears selection on empty space.
    // Returns false if the point is outside the grid.
    bool select_at(zocos::Vec2 pointer);
    void onExit() override;

private:
    class Tile;
    explicit IconGrid(zocos::Director& director, OnInteraction on_interaction);
    bool init_grid(zocos::Director& director, zocos::Size size);
    bool add_tile(zocos::Director& director);
    void place_tiles();
    bool contains(zocos::Vec2 pointer) const;
    int item_at(zocos::Vec2 pointer) const;
    void highlight(int index);
    void cancel_click();
    void click(int index);

    control_style::Palette palette_;
    OnInteraction on_interaction_;
    mstd::vector<Tile*> tiles_;
    int columns_{};
    int count_{};
    int selected_ = NO_ITEM;
    int clicked_ = NO_ITEM;
    bool double_click_{};
};

}  // namespace zonix_gui
