#warning "USING A STUB RESOURCELIB"
#include "../../include/ResourceLib_HM3/ResourceLib_HM3.h"

extern "C" {

ResourceConverter* HM3_GetConverterForResource(const char*) {
    return nullptr;
}

ResourceGenerator* HM3_GetGeneratorForResource(const char*) {
    return nullptr;
}

ResourceTypesArray* HM3_GetSupportedResourceTypes() {
    auto* array = new ResourceTypesArray();
    array->Types = nullptr;
    array->TypeCount = 0;
    return array;
}

void HM3_FreeSupportedResourceTypes(ResourceTypesArray* p_Array) {
    delete p_Array;
}

bool HM3_IsResourceTypeSupported(const char*) {
    return false;
}

JsonString* HM3_GameStructToJson(const char*, const void*, size_t) {
    return nullptr;
}

bool HM3_JsonToGameStruct(const char*, const char*, size_t, void*, size_t) {
    return false;
}

void HM3_FreeJsonString(JsonString*) {}

StringView HM3_GetPropertyName(uint32_t) {
    return StringView{.Data = nullptr, .Size = 0};
}

} // extern "C"
