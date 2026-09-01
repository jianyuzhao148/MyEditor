#pragma once

#include <array>
#include <cstdio>
#include <string>

#include <tinyxml2.h>

namespace editor {

using XmlDocument = tinyxml2::XMLDocument;

inline float attrf(tinyxml2::XMLElement* e, const char* n, float f = 0) {
    if (e) e->QueryFloatAttribute(n, &f);
    return f;
}

inline bool attrb(tinyxml2::XMLElement* e, const char* n, bool f = true) {
    if (!e || !e->Attribute(n)) return f;
    std::string v = e->Attribute(n);
    for (char& c : v) c = (char)std::tolower((unsigned char)c);
    return v != "false" && v != "0";
}

inline const char* attrs(tinyxml2::XMLElement* e, const char* n, const char* f = "") {
    return e && e->Attribute(n) ? e->Attribute(n) : f;
}

inline tinyxml2::XMLElement* child(tinyxml2::XMLElement* e, const char* n) {
    return e ? e->FirstChildElement(n) : nullptr;
}

template <std::size_t N>
inline void copy_buf(std::array<char, N>& b, const char* s) {
    std::snprintf(b.data(), b.size(), "%s", s ? s : "");
}

inline tinyxml2::XMLElement* ensure_child(XmlDocument& d, tinyxml2::XMLElement* p, const char* n) {
    tinyxml2::XMLElement* e = child(p, n);
    if (!e) {
        e = d.NewElement(n);
        p->InsertEndChild(e);
    }
    return e;
}

} // namespace editor
