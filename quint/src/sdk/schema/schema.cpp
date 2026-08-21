#include "schema.h"

#include <core/interfaces/interfaces.h>
#include <sdk/interfaces/schema_system.h>

c_schema::schema_table_map_t c_schema::schema_table_map;
std::mutex c_schema::map_mutex;

std::string c_schema::extract_class_name(const std::string& full_name)
{
    size_t arrow_pos = full_name.find("->");
    if (arrow_pos == std::string::npos)
        return "";
    return full_name.substr(0, arrow_pos);
}

std::string c_schema::extract_field_name(const std::string& full_name)
{
    size_t arrow_pos = full_name.find("->");
    if (arrow_pos == std::string::npos)
        return "";
    return full_name.substr(arrow_pos + 2);
}

void c_schema::init_class(const std::string& class_name)
{
    std::lock_guard<std::mutex> lock(map_mutex);

    if (schema_table_map.find(class_name) != schema_table_map.end())
        return;

    std::deque<const char*> schema_modules = {
        "client.dll",
        "engine2.dll",
        "schemasystem.dll",
        "animationsystem.dll"
    };

    for (const char* dll_file : schema_modules)
    {
        auto* type_scope = g_interfaces->m_schema_system->find_type_scope_for_module(dll_file);
        if (!type_scope)
            continue;

        auto* class_info = type_scope->find_declared_class(class_name.c_str());
        if (!class_info)
            continue;

        short fields_size = class_info->get_fields_size();
        auto* fields = class_info->get_fields();

        auto& key_value_map = schema_table_map[class_name];

        for (int i = 0; i < fields_size; ++i)
        {
            auto& field = fields[i];
            key_value_map.emplace(field.m_name, field.m_offset);
        }

        return;
    }

    schema_table_map.emplace(class_name, schema_key_value_map_t{});
    printf_s("[Schema] Warning: Class not found: %s\n", class_name.c_str());
}

uint16_t c_schema::get_offset(const std::string& full_name)
{
    std::lock_guard<std::mutex> lock(map_mutex);

    static std::unordered_map<std::string, uint16_t> offset_cache;

    auto cache_it = offset_cache.find(full_name);
    if (cache_it != offset_cache.end())
        return cache_it->second;

    std::string class_name = extract_class_name(full_name);
    std::string field_name = extract_field_name(full_name);

    if (class_name.empty() || field_name.empty())
    {
        printf_s("[Schema] Error: Invalid format: %s\n", full_name.c_str());
        return 0;
    }

    auto class_it = schema_table_map.find(class_name);
    if (class_it == schema_table_map.end())
    {
        std::lock_guard<std::mutex> unlock(map_mutex);
        init_class(class_name);
        class_it = schema_table_map.find(class_name);
    }

    if (class_it == schema_table_map.end())
    {
        return 0;
    }

    auto& class_map = class_it->second;
    auto field_it = class_map.find(field_name);

    if (field_it == class_map.end())
    {
        return 0;
    }

    uint16_t offset = field_it->second;
    offset_cache[full_name] = offset;

    return offset;
}