#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <tinyxml2.h>

#include "imgui.h"

#include "core/geometry.h"
#include "resources/asset_store.h"

namespace editor {

struct UiNode {
    tinyxml2::XMLElement* xml = nullptr;
    int parent = -1;
    std::vector<int> children;
    std::string name;
    std::string type;
    std::string text;
    Vec2 size{};
    Vec2 anchor{};
    Vec2 position{};
    Vec2 scale{1, 1};
    float rotation = 0;
    bool visible = true;
    bool effective_visible = true;
    ImU32 color = IM_COL32_WHITE;
    Mat3 world{};
    Vec2 corners[4]{};
    Region region{};
};

struct LoadResult {
    bool ok = false;
    Vec2 scene{1280, 720};
    std::string status;
};

struct NodeProperties {
    std::string name;
    std::string text;
    bool visible = true;
    bool touch = false;
    Vec2 position{};
    Vec2 size{};
    Vec2 scale{1, 1};
    Vec2 anchor{};
    float rotation = 0;
};

class I2dUiEngine {
public:
    virtual ~I2dUiEngine() = default;

    virtual const char* name() const = 0;
    virtual const char* file_extension() const = 0;
    virtual LoadResult load(const std::filesystem::path& file, std::vector<UiNode>& nodes, AssetStore& assets) = 0;
    virtual LoadResult rebuild_loaded_document(const std::string& display_name, std::vector<UiNode>& nodes, AssetStore& assets) = 0;
    virtual bool save(const std::filesystem::path& file) = 0;
    virtual std::string snapshot() = 0;
    virtual bool restore_snapshot(const std::string& snapshot, const std::string& display_name, std::vector<UiNode>& nodes, AssetStore& assets, LoadResult* result) = 0;
    virtual NodeProperties read_properties(const UiNode& node) const = 0;
    virtual void write_common_properties(UiNode& node, const NodeProperties& properties, AssetStore& assets, std::vector<UiNode>& nodes) = 0;
    virtual void refresh_node(UiNode& node, AssetStore& assets) = 0;
    virtual void layout(std::vector<UiNode>& nodes, int index, const Mat3& parent) = 0;
    virtual int validate_root(const std::filesystem::path& root) = 0;
};

} // namespace editor
