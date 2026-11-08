#pragma once
namespace tinyxml2 {
class XMLAttribute {};
class XMLPrinter {
  public:
    const char* CStr() const { return ""; }
};
class XMLElement {
  public:
    const char* Name() const { return ""; }
    int IntAttribute(const char*, int fallback = 0) const { return fallback; }
    unsigned UnsignedAttribute(const char*, unsigned fallback = 0) const { return fallback; }
    bool BoolAttribute(const char*, bool fallback = false) const { return fallback; }
    float FloatAttribute(const char*, float fallback = 0.0f) const { return fallback; }
    const char* Attribute(const char*) const { return nullptr; }
    const char* Attribute(const char*, const char*) const { return nullptr; }
    const char* Attribute(const char*, int) const { return nullptr; }
    const char* Attribute(const char*, unsigned) const { return nullptr; }
    const char* Attribute(const char*, bool) const { return nullptr; }
    const char* Attribute(const char*, double) const { return nullptr; }
    const char* GetText() const { return nullptr; }
    XMLElement* FirstChildElement(const char* = nullptr) const { return nullptr; }
    XMLElement* NextSiblingElement(const char* = nullptr) const { return nullptr; }
    int QueryIntAttribute(const char*, int*) const { return -1; }
    int QueryUnsignedAttribute(const char*, unsigned*) const { return -1; }
    int QueryBoolAttribute(const char*, bool*) const { return -1; }
    int QueryFloatAttribute(const char*, float*) const { return -1; }
    const XMLAttribute* FindAttribute(const char*) const { return nullptr; }
    void SetAttribute(const char*, int) {}
    void SetAttribute(const char*, unsigned) {}
    void SetAttribute(const char*, bool) {}
    void SetAttribute(const char*, float) {}
    void SetAttribute(const char*, double) {}
    void SetAttribute(const char*, const char*) {}
    void InsertEndChild(XMLElement*) {}
};
class XMLDocument {
  public:
    int Parse(const char*, unsigned long = 0) { return 0; }
    bool Error() const { return false; }
    XMLElement* RootElement() const { return nullptr; }
    XMLElement* FirstChildElement(const char* = nullptr) const { return nullptr; }
    XMLElement* NewElement(const char*) { return nullptr; }
    void InsertFirstChild(XMLElement*) {}
    void InsertEndChild(XMLElement*) {}
    void Accept(XMLPrinter*) {}
};
} // namespace tinyxml2
