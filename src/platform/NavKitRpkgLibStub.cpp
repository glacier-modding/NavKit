#warning "USING A STUB RPKG-LIB"
#include "../../include/navkit-rpkg-lib/navkit-rpkg-lib.h"

#include <cstring>

namespace {
    constexpr auto UNAVAILABLE_MESSAGE = "navkit-rpkg-lib is not available on this platform.";

    void report(void (*log_callback)(const char*)) {
        if (log_callback) {
            log_callback(UNAVAILABLE_MESSAGE);
        }
    }

    RustStringList* emptyList() {
        auto* list = new RustStringList();
        list->entries = nullptr;
        list->length = 0;
        return list;
    }

    char* duplicate(const char* text) {
        const size_t length = std::strlen(text);
        auto* copy = new char[length + 1];
        std::memcpy(copy, text, length + 1);
        return copy;
    }
} // namespace

extern "C" {

int extract_scene_mesh_resources(const char*, const char*, const PartitionManager*, const char*, const char*,
    void (*log_callback)(const char*)) {
    report(log_callback);
    return 1;
}

void extract_resources_from_rpkg(const char*, const char* const*, uintptr_t, const PartitionManager*, const char*,
    const char*, void (*log_callback)(const char*)) {
    report(log_callback);
}

PartitionManager* scan_packages(const char*, const char*, void (*log_callback)(const char*)) {
    report(log_callback);
    return nullptr;
}

RustStringList* get_all_resources_hashes_by_type_from_rpkg_files(
    const PartitionManager*, const char*, void (*log_callback)(const char*)) {
    report(log_callback);
    return emptyList();
}

HashList* get_hash_list_from_file_or_repo(const char*, void (*log_callback)(const char*)) {
    report(log_callback);
    return nullptr;
}

RustStringList* get_all_referenced_hashes_by_hash_from_rpkg_files(
    const char*, const PartitionManager*, void (*log_callback)(const char*)) {
    report(log_callback);
    return emptyList();
}

char* get_mati_json_by_hash(const char*, const PartitionManager*, void (*log_callback)(const char*)) {
    report(log_callback);
    return duplicate("{}");
}

uint32_t hash_list_get_version(const HashList*) {
    return 0;
}

RustStringList* hash_list_get_all_hashes(const HashList*) {
    return emptyList();
}

char* hash_list_get_path_by_hash(const HashList*, const char*) {
    return nullptr;
}

char* hash_list_get_hint_by_hash(const HashList*, const char*) {
    return nullptr;
}

uint32_t hash_list_get_resource_type_by_hash(const HashList*, const char*) {
    return 0;
}

const char* get_string_from_list(RustStringList* list, uintptr_t index) {
    if (!list || !list->entries || index >= list->length) {
        return nullptr;
    }
    return list->entries[index];
}

void free_rust_string_list(RustStringList* ptr) {
    if (!ptr) {
        return;
    }
    for (uintptr_t i = 0; i < ptr->length; i++) {
        delete[] ptr->entries[i];
    }
    delete[] ptr->entries;
    delete ptr;
}

void free_entities_json(EntitiesJson*) {}

void free_string(char* ptr) {
    delete[] ptr;
}

void free_hash_list(HashList*) {}

void free_hashset_string(HashSet<String>*) {}

void free_partition_manager(PartitionManager*) {}

} // extern "C"
