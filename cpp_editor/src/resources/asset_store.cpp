#include "resources/asset_store.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>

#include <tinyxml2.h>

#include "core/geometry.h"
#include "core/xml_util.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace fs = std::filesystem;

namespace editor {

static fs::path extract_plist_frame(const fs::path& atlas_path, const fs::path& plist_path, const std::string& frame_name, int x, int y, int w, int h, bool rotated) {
    std::error_code ec;
    fs::path dir = fs::temp_directory_path(ec) / "csd_editor_plist" / std::to_string(std::hash<std::string>{}(slash(atlas_path)));
    fs::create_directories(dir, ec);
    std::string safe = frame_name;
    for (char& c : safe) {
        if (c == '/' || c == '\\' || c == ':') c = '_';
    }
    fs::path out = dir / (safe + ".v2.tga");
    if (fs::exists(out, ec)) return out;

    int sw = w, sh = h, scrx = 0, scry = 0, scrw = w, scrh = h;
    XmlDocument pd;
    if (pd.LoadFile(plist_path.string().c_str()) == tinyxml2::XML_SUCCESS) {
        auto value = [](tinyxml2::XMLElement* d, const char* k) -> tinyxml2::XMLElement* {
            for (tinyxml2::XMLElement* e = d ? d->FirstChildElement("key") : nullptr; e; e = e->NextSiblingElement("key")) {
                if (e->GetText() && std::string(e->GetText()) == k) return e->NextSiblingElement();
            }
            return nullptr;
        };
        tinyxml2::XMLElement* root = child(pd.RootElement(), "dict");
        tinyxml2::XMLElement* frames = value(root, "frames");
        tinyxml2::XMLElement* fd = nullptr;
        for (tinyxml2::XMLElement* k = frames ? frames->FirstChildElement("key") : nullptr; k; k = k->NextSiblingElement("key")) {
            if (k->GetText() && frame_name == k->GetText()) {
                fd = k->NextSiblingElement("dict");
                break;
            }
        }
        if (fd) {
            tinyxml2::XMLElement* z = value(fd, "sourceSize");
            if (z && z->GetText()) std::sscanf(z->GetText(), "{%d,%d}", &sw, &sh);
            z = value(fd, "sourceColorRect");
            if (z && z->GetText()) std::sscanf(z->GetText(), "{{%d,%d},{%d,%d}}", &scrx, &scry, &scrw, &scrh);
        }
    }

    int cropw = rotated ? h : w;
    int croph = rotated ? w : h;
    int aw = 0, ah = 0, n = 0;
    stbi_uc* px = stbi_load(atlas_path.string().c_str(), &aw, &ah, &n, 4);
    if (!px) return {};

    std::vector<stbi_uc> piece((size_t)cropw * croph * 4);
    for (int row = 0; row < croph; ++row) {
        for (int col = 0; col < cropw; ++col) {
            int sx = std::clamp(x + col, 0, aw - 1);
            int sy = std::clamp(y + row, 0, ah - 1);
            std::copy_n(px + ((size_t)sy * aw + sx) * 4, 4, piece.data() + ((size_t)row * cropw + col) * 4);
        }
    }
    stbi_image_free(px);

    std::vector<stbi_uc> image((size_t)sw * sh * 4, 0);
    for (int row = 0; row < croph; ++row) {
        for (int col = 0; col < cropw; ++col) {
            int dx = rotated ? row : col;
            int dy = rotated ? (cropw - 1 - col) : row;
            if (dx >= 0 && dy >= 0 && dx < scrw && dy < scrh && scrx + dx >= 0 && scry + dy >= 0 && scrx + dx < sw && scry + dy < sh) {
                std::copy_n(piece.data() + ((size_t)row * cropw + col) * 4, 4, image.data() + ((size_t)(scry + dy) * sw + scrx + dx) * 4);
            }
        }
    }

    std::ofstream f(out, std::ios::binary);
    if (!f) return {};
    unsigned char header[18] = {0};
    header[2] = 2;
    header[12] = (unsigned char)(sw & 255);
    header[13] = (unsigned char)(sw >> 8);
    header[14] = (unsigned char)(sh & 255);
    header[15] = (unsigned char)(sh >> 8);
    header[16] = 32;
    header[17] = 40;
    f.write((char*)header, 18);
    for (size_t i = 0; i < image.size(); i += 4) {
        f.put((char)image[i + 2]).put((char)image[i + 1]).put((char)image[i]).put((char)image[i + 3]);
    }
    return out;
}

AssetStore::AssetStore(fs::path root) : root_(std::move(root)) {}

AssetStore::~AssetStore() {
    clear();
}

void AssetStore::set_root(fs::path root) {
    clear();
    root_ = std::move(root);
}

void AssetStore::clear() {
    for (auto& kv : textures_) {
        if (kv.second.id) glDeleteTextures(1, &kv.second.id);
    }
    textures_.clear();
    atlases_.clear();
}

Region AssetStore::resolve(const std::string& type, const std::string& path, const std::string& plist) {
    if (path.empty() || lower(type) == "default") return {};
    if (lower(type) == "plistsubimage" && !plist.empty()) return atlas_region(plist, path);
    GLTexture* t = texture(root_ / fs::path(path));
    return t ? Region{t, 0, 0, 1, 1, false} : Region{};
}

GLTexture* AssetStore::texture(const fs::path& raw) {
    fs::path p = raw.lexically_normal();
    std::string key = slash(p);
    auto old = textures_.find(key);
    if (old != textures_.end()) return old->second.id ? &old->second : nullptr;

    int w = 0, h = 0, n = 0;
    stbi_uc* px = stbi_load(p.string().c_str(), &w, &h, &n, 4);
    GLTexture out{};
    if (px) {
        glGenTextures(1, &out.id);
        glBindTexture(GL_TEXTURE_2D, out.id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, 0x812F);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, 0x812F);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
        stbi_image_free(px);
        out.w = w;
        out.h = h;
    }
    auto it = textures_.emplace(key, out).first;
    return it->second.id ? &it->second : nullptr;
}

static tinyxml2::XMLElement* plist_value(tinyxml2::XMLElement* dict, const std::string& key) {
    for (tinyxml2::XMLElement* e = dict ? dict->FirstChildElement("key") : nullptr; e; e = e->NextSiblingElement("key")) {
        if (e->GetText() && key == e->GetText()) return e->NextSiblingElement();
    }
    if (key == "textureFileName") {
        for (tinyxml2::XMLElement* e = dict ? dict->FirstChildElement("key") : nullptr; e; e = e->NextSiblingElement("key")) {
            if (e->GetText() && std::string(e->GetText()) == "realTextureFileName") return e->NextSiblingElement();
        }
    }
    return nullptr;
}

AssetStore::Atlas& AssetStore::load_atlas(const std::string& rel) {
    auto it = atlases_.try_emplace(rel).first;
    Atlas& a = it->second;
    if (a.loaded) return a;
    a.loaded = true;

    fs::path pp = (root_ / fs::path(rel)).lexically_normal();
    XmlDocument doc;
    if (doc.LoadFile(pp.string().c_str()) != tinyxml2::XML_SUCCESS) return a;
    tinyxml2::XMLElement* dict = child(doc.RootElement(), "dict");
    tinyxml2::XMLElement* frames = plist_value(dict, "frames");
    for (tinyxml2::XMLElement* key = frames ? frames->FirstChildElement("key") : nullptr; key; key = key->NextSiblingElement("key")) {
        tinyxml2::XMLElement* fd = key->NextSiblingElement("dict");
        tinyxml2::XMLElement* fv = plist_value(fd, "frame");
        if (!key->GetText() || !fv || !fv->GetText()) continue;
        Frame f;
        if (std::sscanf(fv->GetText(), "{{%d,%d},{%d,%d}}", &f.x, &f.y, &f.w, &f.h) != 4) continue;
        tinyxml2::XMLElement* rv = plist_value(fd, "rotated");
        f.rotated = rv && std::string(rv->Name()) == "true";
        a.frames[key->GetText()] = f;
    }
    tinyxml2::XMLElement* meta = plist_value(dict, "metadata");
    tinyxml2::XMLElement* tv = plist_value(meta, "textureFileName");
    fs::path image = tv && tv->GetText() ? fs::path(tv->GetText()) : fs::path(pp.stem().string() + ".png");
    a.image = image.is_absolute() ? image : pp.parent_path() / image;
    return a;
}

Region AssetStore::atlas_region(const std::string& plist, const std::string& name) {
    Atlas& a = load_atlas(plist);
    auto it = a.frames.find(name);
    if (it == a.frames.end()) return {};
    const Frame& f = it->second;
    fs::path extracted = extract_plist_frame(a.image, root_ / fs::path(plist), name, f.x, f.y, f.w, f.h, f.rotated);
    GLTexture* t = extracted.empty() ? texture(a.image) : texture(extracted);
    return t ? Region{t, 0, 0, 1, 1, false} : Region{};
}

} // namespace editor
