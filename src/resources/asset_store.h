#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>

#include <GLFW/glfw3.h>

namespace editor {

struct GLTexture {
    GLuint id = 0;
    int w = 0;
    int h = 0;
};

struct Region {
    GLTexture* texture = nullptr;
    float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
    bool rotated = false;
    bool valid() const { return texture && texture->id; }
};

class AssetStore {
public:
    explicit AssetStore(std::filesystem::path root);
    ~AssetStore();

    void set_root(std::filesystem::path root);
    void clear();
    Region resolve(const std::string& type, const std::string& path, const std::string& plist);

private:
    struct Frame {
        int x = 0, y = 0, w = 0, h = 0;
        bool rotated = false;
    };

    struct Atlas {
        std::filesystem::path image;
        std::unordered_map<std::string, Frame> frames;
        bool loaded = false;
    };

    std::filesystem::path root_;
    std::unordered_map<std::string, GLTexture> textures_;
    std::unordered_map<std::string, Atlas> atlases_;

    GLTexture* texture(const std::filesystem::path& raw);
    Atlas& load_atlas(const std::string& rel);
    Region atlas_region(const std::string& plist, const std::string& name);
};

} // namespace editor
