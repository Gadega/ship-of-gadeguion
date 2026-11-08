#include "ObjectExtension.h"

ObjectExtension& ObjectExtension::GetInstance() {
    static ObjectExtension instance;
    return instance;
}

ObjectExtension::Id ObjectExtension::RegisterId() {
    return NextId++;
}

void ObjectExtension::Free(const void* object) {
    if (object == nullptr) {
        return;
    }

    for (auto it = Data.begin(); it != Data.end();) {
        if (it->first.first == object) {
            it = Data.erase(it);
        } else {
            ++it;
        }
    }
}

extern "C" void ObjectExtension_Free(const void* object) {
    ObjectExtension::GetInstance().Free(object);
}
