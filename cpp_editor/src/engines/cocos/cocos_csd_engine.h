#pragma once

#include "engine/ui_engine.h"

namespace editor {

class CocosCsdEngine : public I2dUiEngine {
public:
    const char* name() const override { return "Cocos Studio CSD"; }
    const char* file_extension() const override { return ".csd"; }

    LoadResult load(const std::filesystem::path& file, std::vector<UiNode>& nodes, AssetStore& assets) override;
    LoadResult rebuild_loaded_document(const std::string& display_name, std::vector<UiNode>& nodes, AssetStore& assets) override;
    bool save(const std::filesystem::path& file) override;
    std::string snapshot() override;
    bool restore_snapshot(const std::string& snapshot, const std::string& display_name, std::vector<UiNode>& nodes, AssetStore& assets, LoadResult* result) override;
    NodeProperties read_properties(const UiNode& node) const override;
    void write_common_properties(UiNode& node, const NodeProperties& properties, AssetStore& assets, std::vector<UiNode>& nodes) override;
    void refresh_node(UiNode& node, AssetStore& assets) override;
    void layout(std::vector<UiNode>& nodes, int index, const Mat3& parent) override;
    int validate_root(const std::filesystem::path& root) override;

    tinyxml2::XMLDocument& document() { return doc_; }

private:
    tinyxml2::XMLDocument doc_;

    void parse_node(tinyxml2::XMLElement* element, int parent, std::vector<UiNode>& nodes, AssetStore& assets);
};

} // namespace editor
