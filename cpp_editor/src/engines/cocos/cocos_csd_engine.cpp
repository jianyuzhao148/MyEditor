#include "engines/cocos/cocos_csd_engine.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <sstream>

#include "core/xml_util.h"

namespace fs = std::filesystem;

namespace editor {

LoadResult CocosCsdEngine::load(const fs::path& file, std::vector<UiNode>& nodes, AssetStore& assets) {
    doc_.DeleteChildren();
    nodes.clear();
    if (doc_.LoadFile(file.string().c_str()) != tinyxml2::XML_SUCCESS) {
        return {false, {1280, 720}, "Failed to parse: " + file.string()};
    }

    LoadResult result = rebuild_loaded_document(file.filename().string(), nodes, assets);
    if (!result.ok) result.status += ": " + file.string();
    return result;
}

LoadResult CocosCsdEngine::rebuild_loaded_document(const std::string& display_name, std::vector<UiNode>& nodes, AssetStore& assets) {
    nodes.clear();
    tinyxml2::XMLElement* content = child(child(doc_.RootElement(), "Content"), "Content");
    tinyxml2::XMLElement* object = child(content, "ObjectData");
    if (!object) {
        return {false, {1280, 720}, "ObjectData not found"};
    }

    parse_node(object, -1, nodes, assets);
    tinyxml2::XMLElement* sz = child(object, "Size");
    Vec2 scene = {std::max(1.f, attrf(sz, "X", 1280)), std::max(1.f, attrf(sz, "Y", 720))};
    if (!nodes.empty()) layout(nodes, 0, {});
    return {true, scene, "Loaded " + display_name + " (" + std::to_string(nodes.size()) + " nodes)"};
}

bool CocosCsdEngine::save(const fs::path& file) {
    return doc_.SaveFile(file.string().c_str()) == tinyxml2::XML_SUCCESS;
}

std::string CocosCsdEngine::snapshot() {
    tinyxml2::XMLPrinter printer;
    doc_.Print(&printer);
    return printer.CStr();
}

bool CocosCsdEngine::restore_snapshot(const std::string& snapshot_text, const std::string& display_name, std::vector<UiNode>& nodes, AssetStore& assets, LoadResult* result) {
    doc_.DeleteChildren();
    if (doc_.Parse(snapshot_text.c_str()) != tinyxml2::XML_SUCCESS) return false;
    LoadResult r = rebuild_loaded_document(display_name, nodes, assets);
    if (result) *result = r;
    return r.ok;
}

NodeProperties CocosCsdEngine::read_properties(const UiNode& node) const {
    NodeProperties props;
    props.name = node.name;
    props.text = attrs(node.xml, "LabelText", attrs(node.xml, "ButtonText", attrs(node.xml, "PlaceHolderText", "")));
    props.visible = node.visible;
    props.touch = attrb(node.xml, "TouchEnable", false);
    props.position = node.position;
    props.size = node.size;
    props.scale = node.scale;
    props.anchor = node.anchor;
    props.rotation = node.rotation;
    return props;
}

void CocosCsdEngine::write_common_properties(UiNode& node, const NodeProperties& props, AssetStore& assets, std::vector<UiNode>& nodes) {
    node.xml->SetAttribute("Name", props.name.c_str());
    node.xml->SetAttribute("Visible", props.visible ? "True" : "False");
    node.xml->SetAttribute("TouchEnable", props.touch ? "True" : "False");
    node.xml->SetAttribute("RotationSkewX", props.rotation);
    node.xml->SetAttribute("RotationSkewY", props.rotation);

    auto set_pair = [&](const char* tag, const char* x, const char* y, Vec2 v) {
        tinyxml2::XMLElement* e = ensure_child(doc_, node.xml, tag);
        e->SetAttribute(x, v.x);
        e->SetAttribute(y, v.y);
    };
    set_pair("Position", "X", "Y", props.position);
    set_pair("Size", "X", "Y", props.size);
    set_pair("Scale", "ScaleX", "ScaleY", props.scale);
    set_pair("AnchorPoint", "ScaleX", "ScaleY", props.anchor);

    refresh_node(node, assets);
    if (!nodes.empty()) layout(nodes, 0, {});
}

void CocosCsdEngine::parse_node(tinyxml2::XMLElement* element, int parent, std::vector<UiNode>& nodes, AssetStore& assets) {
    int index = (int)nodes.size();
    nodes.push_back({});
    nodes[index].xml = element;
    nodes[index].parent = parent;
    refresh_node(nodes[index], assets);
    if (parent >= 0) nodes[parent].children.push_back(index);

    tinyxml2::XMLElement* children = child(element, "Children");
    for (tinyxml2::XMLElement* c = children ? children->FirstChildElement("AbstractNodeData") : nullptr; c; c = c->NextSiblingElement("AbstractNodeData")) {
        parse_node(c, index, nodes, assets);
    }
}

void CocosCsdEngine::refresh_node(UiNode& n, AssetStore& assets) {
    n.name = attrs(n.xml, "Name", "unnamed");
    n.type = attrs(n.xml, "ctype", n.xml->Name());
    n.visible = attrb(n.xml, "Visible", true);
    n.rotation = attrf(n.xml, "RotationSkewX", 0);
    n.text = attrs(n.xml, "LabelText", attrs(n.xml, "ButtonText", attrs(n.xml, "PlaceHolderText", "")));
    tinyxml2::XMLElement* e = child(n.xml, "Size");
    n.size = {attrf(e, "X"), attrf(e, "Y")};
    e = child(n.xml, "AnchorPoint");
    n.anchor = {attrf(e, "ScaleX"), attrf(e, "ScaleY")};
    e = child(n.xml, "Position");
    n.position = {attrf(e, "X"), attrf(e, "Y")};
    e = child(n.xml, "Scale");
    n.scale = {attrf(e, "ScaleX", 1), attrf(e, "ScaleY", 1)};
    e = child(n.xml, "CColor");
    n.color = IM_COL32((int)attrf(e, "R", 255), (int)attrf(e, "G", 255), (int)attrf(e, "B", 255), (int)attrf(e, "A", 255));
    tinyxml2::XMLElement* f = child(n.xml, "FileData");
    if (!f) f = child(n.xml, "NormalFileData");
    n.region = assets.resolve(attrs(f, "Type"), attrs(f, "Path"), attrs(f, "Plist"));
}

void CocosCsdEngine::layout(std::vector<UiNode>& nodes, int index, const Mat3& parent) {
    UiNode& n = nodes[index];
    n.effective_visible = n.visible && (n.parent < 0 || nodes[n.parent].effective_visible);
    n.world = mul(parent, transform(n.position, n.scale, n.rotation));
    float l = -n.anchor.x * n.size.x;
    float b = -n.anchor.y * n.size.y;
    float r = l + n.size.x;
    float t = b + n.size.y;
    n.corners[0] = n.world.apply({l, b});
    n.corners[1] = n.world.apply({r, b});
    n.corners[2] = n.world.apply({r, t});
    n.corners[3] = n.world.apply({l, t});

    Mat3 child_origin = n.world;
    child_origin.tx += n.world.a * l + n.world.c * b;
    child_origin.ty += n.world.b * l + n.world.d * b;
    for (int c : n.children) layout(nodes, c, child_origin);
}

int CocosCsdEngine::validate_root(const fs::path& root) {
    size_t files = 0, nodes = 0, failed = 0;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        if (!it->is_regular_file() || lower(it->path().extension().string()) != file_extension()) continue;
        ++files;
        tinyxml2::XMLDocument d;
        if (d.LoadFile(it->path().string().c_str()) != tinyxml2::XML_SUCCESS) {
            std::fprintf(stderr, "PARSE FAILED: %s\n", it->path().string().c_str());
            ++failed;
            continue;
        }
        tinyxml2::XMLElement* object = child(child(child(d.RootElement(), "Content"), "Content"), "ObjectData");
        if (!object) {
            std::fprintf(stderr, "OBJECT DATA MISSING: %s\n", it->path().string().c_str());
            ++failed;
            continue;
        }
        std::vector<tinyxml2::XMLElement*> stack{object};
        while (!stack.empty()) {
            tinyxml2::XMLElement* e = stack.back();
            stack.pop_back();
            ++nodes;
            tinyxml2::XMLElement* cs = child(e, "Children");
            for (tinyxml2::XMLElement* c = cs ? cs->FirstChildElement("AbstractNodeData") : nullptr; c; c = c->NextSiblingElement("AbstractNodeData")) {
                stack.push_back(c);
            }
        }
    }
    std::printf("Validated %zu CSD files, %zu UI nodes, %zu failures.\n", files, nodes, failed);
    return failed ? 2 : 0;
}

} // namespace editor
