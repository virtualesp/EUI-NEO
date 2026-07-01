#include "eui_neo.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <string>

namespace app {

const DslAppConfig& dslAppConfig() {
    static const DslAppConfig config = DslAppConfig{}
        .title("Masonry Browser")
        .pageId("masonry_browser")
        .clearColor({0.98f, 0.98f, 0.97f, 1.0f})
        .windowSize(1920, 1080)
        .fps(90.0);
    return config;
}

namespace {

struct Pin {
    int imageIndex;
    float ratio;
    float offsetY;
};

constexpr float kRailWidth = 84.0f;
constexpr float kTopHeight = 96.0f;
constexpr float kPagePad = 20.0f;
constexpr float kGap = 18.0f;
constexpr std::size_t kPinCount = 22;

float scrollOffset = 0.0f;

void composeRail(eui::Ui& ui, float height);
void composeMain(eui::Ui& ui, float width, float height);
void composeTopBar(eui::Ui& ui, float width);
void composeMasonry(eui::Ui& ui, float width, float height);
void pinCard(eui::Ui& ui, const std::string& id, const Pin& pin, float x, float y, float width, float height);
void navIcon(eui::Ui& ui, const std::string& id, unsigned int icon, float y, bool active = false);
void iconText(eui::Ui& ui, const std::string& id, unsigned int icon, float x, float y, float size, eui::Color color);

eui::Transition motion();
eui::Color ink();
eui::Color muted();
eui::Color chrome();
std::string zhAll();
std::string zhSearch();
std::string imageUrl(int index);
std::string pinId(int index);

const std::array<Pin, kPinCount> kPins = {{
    {0, 2.08f, 0.00f}, {1, 1.72f, 0.08f}, {2, 0.99f, 0.18f}, {3, 1.64f, 0.10f},
    {4, 1.48f, 0.06f}, {5, 0.99f, 0.00f}, {6, 1.58f, 0.12f}, {7, 1.02f, 0.22f},
    {8, 1.18f, 0.34f}, {9, 0.82f, 0.18f}, {10, 1.00f, 0.04f}, {11, 1.34f, 0.20f},
    {12, 0.96f, 0.10f}, {13, 1.86f, 0.18f}, {14, 1.16f, 0.26f}, {15, 1.54f, 0.10f},
    {16, 0.88f, 0.30f}, {17, 1.42f, 0.02f}, {18, 1.10f, 0.26f}, {19, 0.76f, 0.14f},
    {20, 1.68f, 0.22f}, {21, 1.04f, 0.06f},
}};

} // namespace

// Root layout: fixed navigation rail and scrollable image browser.
void compose(eui::Ui& ui, const eui::Screen& screen) {
    ui.stack("root")
        .size(screen.width, screen.height)
        .content([&] {
            composeMain(ui, std::max(0.0f, screen.width - kRailWidth), screen.height);
            composeRail(ui, screen.height);
        })
        .build();
}

namespace {

// Left navigation rail.
void composeRail(eui::Ui& ui, float height) {
    ui.stack("rail")
        .size(kRailWidth, height)
        .zIndex(10)
        .content([&] {
            ui.rect("rail.bg").size(kRailWidth, height).color({1.0f, 1.0f, 1.0f, 1.0f}).transition(motion()).build();
            ui.rect("rail.rule").position(kRailWidth - 1.0f, 0.0f).size(1.0f, height).color({0.86f, 0.86f, 0.84f, 1.0f}).transition(motion()).build();

            ui.text("rail.logo")
                .position(0.0f, 24.0f)
                .size(kRailWidth, 44.0f)
                .text("P")
                .fontSize(29.0f)
                .lineHeight(36.0f)
                .fontWeight(900)
                .color({0.90f, 0.02f, 0.14f, 1.0f})
                .transition(motion())
                .horizontalAlign(eui::HorizontalAlign::Center)
                .verticalAlign(eui::VerticalAlign::Center)
                .build();

            navIcon(ui, "rail.home", 0xF015, 118.0f, true);
            navIcon(ui, "rail.grid", 0xF00A, 206.0f);
            navIcon(ui, "rail.plus", 0xF067, 294.0f);
            navIcon(ui, "rail.bell", 0xF0F3, 382.0f);
            navIcon(ui, "rail.chat", 0xF086, 470.0f);
            navIcon(ui, "rail.settings", 0xF013, std::max(0.0f, height - 68.0f));
        })
        .build();
}

// Main content: top search bar, active tab, masonry scroll view.
void composeMain(eui::Ui& ui, float width, float height) {
    ui.stack("main")
        .position(kRailWidth, 0.0f)
        .size(width, height)
        .content([&] {
            ui.rect("main.bg").size(width, height).color({0.985f, 0.985f, 0.972f, 1.0f}).transition(motion()).build();
            composeMasonry(ui, width, std::max(0.0f, height - kTopHeight - 70.0f));
            composeTopBar(ui, width);

            ui.text("filter.all")
                .position(kPagePad + 18.0f, kTopHeight + 10.0f)
                .size(52.0f, 32.0f)
                .zIndex(8)
                .text(zhAll())
                .fontSize(18.0f)
                .lineHeight(24.0f)
                .fontWeight(840)
                .color(ink())
                .transition(motion())
                .build();
            ui.rect("filter.all.underline")
                .position(kPagePad + 18.0f, kTopHeight + 43.0f)
                .size(38.0f, 2.0f)
                .zIndex(8)
                .color(ink())
                .radius(1.0f)
                .transition(motion())
                .build();
        })
        .build();
}

// Top bar: search field, visual search button, avatar, and dropdown.
void composeTopBar(eui::Ui& ui, float width) {
    const float searchX = kPagePad;
    const float searchY = 20.0f;
    const float avatarW = 72.0f;
    const float searchW = std::max(220.0f, width - kPagePad * 2.0f - avatarW);

    ui.stack("top.search")
        .position(searchX, searchY)
        .size(searchW, 56.0f)
        .zIndex(8)
        .content([&] {
            ui.rect("top.search.bg")
                .size(searchW, 56.0f)
                .color(chrome())
                .radius(15.0f)
                .transition(motion())
                .build();
            iconText(ui, "top.search.icon", 0xF002, 18.0f, 15.0f, 24.0f, muted());
            ui.text("top.search.placeholder")
                .position(50.0f, 15.0f)
                .size(160.0f, 28.0f)
                .text(zhSearch())
                .fontSize(20.0f)
                .lineHeight(26.0f)
                .fontWeight(760)
                .color({0.40f, 0.40f, 0.38f, 1.0f})
                .transition(motion())
                .build();
            iconText(ui, "top.search.camera", 0xF030, searchW - 48.0f, 13.0f, 28.0f, ink());
        })
        .build();

    ui.stack("top.avatar")
        .position(searchX + searchW + 18.0f, 23.0f)
        .size(58.0f, 48.0f)
        .zIndex(8)
        .content([&] {
            ui.rect("top.avatar.dot")
                .size(38.0f, 38.0f)
                .color({0.08f, 0.08f, 0.075f, 1.0f})
                .radius(19.0f)
                .transition(motion())
                .build();
            ui.text("top.avatar.initial")
                .size(38.0f, 38.0f)
                .text("A")
                .fontSize(17.0f)
                .lineHeight(24.0f)
                .fontWeight(820)
                .color({1.0f, 1.0f, 0.96f, 1.0f})
                .transition(motion())
                .horizontalAlign(eui::HorizontalAlign::Center)
                .verticalAlign(eui::VerticalAlign::Center)
                .build();
            iconText(ui, "top.avatar.chevron", 0xF107, 42.0f, 11.0f, 18.0f, muted());
        })
        .build();
}

// Masonry grid: measured stack inside a scrollView.
void composeMasonry(eui::Ui& ui, float width, float height) {
    components::scrollView(ui, "masonry.scroll")
        .position(kPagePad, kTopHeight + 70.0f)
        .size(std::max(0.0f, width - kPagePad * 2.0f), height)
        .scrollbarWidth(0.0f)
        .scrollbarGap(0.0f)
        .offset(scrollOffset)
        .step(54.0f)
        .contentKey("masonry.browser.v1")
        .zIndex(1)
        .onChange([](float value) { scrollOffset = value; })
        .content([&](eui::Ui& contentUi, float contentW, float) {
            const int columns = contentW >= 1740.0f ? 6 : (contentW >= 1400.0f ? 5 : (contentW >= 1080.0f ? 4 : 3));
            const float cardW = std::max(160.0f, (contentW - kGap * static_cast<float>(columns - 1)) / static_cast<float>(columns));
            std::array<float, 8> columnY{};
            std::array<float, kPins.size()> xs{};
            std::array<float, kPins.size()> ys{};
            std::array<float, kPins.size()> hs{};
            float maxY = 0.0f;

            for (std::size_t i = 0; i < kPins.size(); ++i) {
                int column = 0;
                for (int c = 1; c < columns; ++c) {
                    if (columnY[c] < columnY[column]) {
                        column = c;
                    }
                }

                xs[i] = static_cast<float>(column) * (cardW + kGap);
                ys[i] = columnY[column];
                hs[i] = cardW * kPins[i].ratio;
                columnY[column] += hs[i] + kGap;
                maxY = std::max(maxY, columnY[column]);
            }

            contentUi.stack("masonry.grid")
                .size(contentW, maxY)
                .content([&] {
                    for (std::size_t i = 0; i < kPins.size(); ++i) {
                        pinCard(contentUi, pinId(static_cast<int>(i)), kPins[i], xs[i], ys[i], cardW, hs[i]);
                    }
                })
                .build();
        })
        .build();
}

// Individual image tile.
void pinCard(eui::Ui& ui, const std::string& id, const Pin& pin, float x, float y, float width, float height) {
    ui.stack(id)
        .position(x, y)
        .size(width, height)
        .content([&] {
            ui.rect(id + ".shadow")
                .size(width, height)
                .color({0.0f, 0.0f, 0.0f, 0.0f})
                .radius(17.0f)
                .shadow(18.0f, 0.0f, 8.0f, {0.0f, 0.0f, 0.0f, 0.10f})
                .build();

            ui.rect(id + ".skeleton")
                .size(width, height)
                .color({0.88f, 0.88f, 0.84f, 1.0f})
                .radius(17.0f)
                .transition(motion())
                .build();

            ui.image(id + ".image")
                .size(width, height)
                .source(imageUrl(pin.imageIndex))
                .coverViewport(width, height, 0.0f, pin.offsetY * height)
                .radius(17.0f)
                .build();
        })
        .build();
}

// Reusable visual pieces.
void navIcon(eui::Ui& ui, const std::string& id, unsigned int icon, float y, bool active) {
    ui.stack(id)
        .position(22.0f, y)
        .size(40.0f, 40.0f)
        .content([&] {
            if (active) {
                ui.rect(id + ".pill").size(40.0f, 40.0f).color(ink()).radius(20.0f).build();
            }
            iconText(ui, id + ".icon", icon, 0.0f, 0.0f, 40.0f, active ? eui::Color{1.0f, 1.0f, 1.0f, 1.0f} : ink());
        })
        .build();
}

void iconText(eui::Ui& ui, const std::string& id, unsigned int icon, float x, float y, float size, eui::Color color) {
    ui.text(id)
        .position(x, y)
        .size(size, size)
        .icon(icon)
        .fontSize(size * 0.64f)
        .lineHeight(size)
        .color(color)
        .transition(motion())
        .horizontalAlign(eui::HorizontalAlign::Center)
        .verticalAlign(eui::VerticalAlign::Center)
        .build();
}

eui::Color ink() {
    return {0.02f, 0.02f, 0.02f, 1.0f};
}

eui::Color muted() {
    return {0.43f, 0.43f, 0.40f, 1.0f};
}

eui::Color chrome() {
    return {0.848f, 0.848f, 0.820f, 1.0f};
}

eui::Transition motion() {
    auto transition = eui::Transition::make(0.14f, eui::Ease::OutCubic);
    transition.animate(eui::AnimProperty::Color | eui::AnimProperty::TextColor |
                       eui::AnimProperty::Shadow | eui::AnimProperty::Opacity);
    return transition;
}

std::string zhAll() {
    return eui::utf8(0x5168) + eui::utf8(0x90E8);
}

std::string zhSearch() {
    return eui::utf8(0x641C) + eui::utf8(0x7D22);
}

std::string imageUrl(int index) {
    return "https://picsum.photos/seed/eui-neo-masonry-" + std::to_string(index) + "/900/1300.jpg";
}

std::string pinId(int index) {
    char buffer[24] = {};
    std::snprintf(buffer, sizeof(buffer), "pin.%02d", index);
    return std::string(buffer);
}

} // namespace

} // namespace app
