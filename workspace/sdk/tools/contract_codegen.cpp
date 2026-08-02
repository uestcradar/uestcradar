#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct Field {
    std::string name;
    std::string type;
    std::size_t offset{};
    bool reserved{};
};

struct Contract {
    std::string name;
    std::string frame;
    std::string metadata;
    std::uint64_t type_id{};
    std::uint32_t type_version{};
    std::size_t metadata_size{};
    std::string element_type;
    std::string cpp_element;
    std::string rows;
    std::string columns;
    std::size_t payload_offset{};
    std::string visualization_kind;
    std::vector<Field> fields;
    std::string source_json;
};

std::string read_file(const fs::path& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("cannot read " + path.string());
    }
    return {std::istreambuf_iterator<char>(input), {}};
}

void write_file(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    {
        std::ifstream existing(path);
        if (existing) {
            const std::string current{
                std::istreambuf_iterator<char>(existing), {}};
            if (current == text) {
                return;
            }
        }
    }
    std::ofstream output(path);
    if (!output) {
        throw std::runtime_error("cannot write " + path.string());
    }
    output << text;
}

std::string regex_escape(const std::string& text) {
    static const std::regex special{R"([.^$|()\[\]{}*+?\\])"};
    return std::regex_replace(text, special, R"(\$&)" );
}

std::string string_value(const std::string& object, const std::string& key) {
    const std::regex pattern{"\"" + regex_escape(key) +
        "\"\\s*:\\s*\"([^\"]*)\""};
    std::smatch match;
    if (!std::regex_search(object, match, pattern)) {
        throw std::runtime_error("missing string property '" + key + "'");
    }
    return match[1].str();
}

std::uint64_t integer_value(const std::string& object, const std::string& key) {
    const std::regex pattern{"\"" + regex_escape(key) +
        "\"\\s*:\\s*([0-9]+)"};
    std::smatch match;
    if (!std::regex_search(object, match, pattern)) {
        throw std::runtime_error("missing integer property '" + key + "'");
    }
    return std::stoull(match[1].str());
}

bool bool_value_or(
    const std::string& object, const std::string& key, bool fallback) {
    const std::regex pattern{"\"" + regex_escape(key) +
        "\"\\s*:\\s*(true|false)"};
    std::smatch match;
    return std::regex_search(object, match, pattern)
        ? match[1].str() == "true"
        : fallback;
}

std::string delimited_value(
    const std::string& text,
    const std::string& key,
    char opening,
    char closing) {
    const std::string marker = "\"" + key + "\"";
    const auto key_pos = text.find(marker);
    if (key_pos == std::string::npos) {
        throw std::runtime_error("missing property '" + key + "'");
    }
    const auto begin = text.find(opening, key_pos + marker.size());
    if (begin == std::string::npos) {
        throw std::runtime_error("invalid property '" + key + "'");
    }
    std::size_t depth = 0;
    bool quoted = false;
    bool escaped = false;
    for (std::size_t index = begin; index < text.size(); ++index) {
        const char value = text[index];
        if (quoted) {
            if (escaped) {
                escaped = false;
            } else if (value == '\\') {
                escaped = true;
            } else if (value == '"') {
                quoted = false;
            }
            continue;
        }
        if (value == '"') {
            quoted = true;
        } else if (value == opening) {
            ++depth;
        } else if (value == closing && --depth == 0) {
            return text.substr(begin, index - begin + 1);
        }
    }
    throw std::runtime_error("unterminated property '" + key + "'");
}

std::size_t scalar_size(const std::string& type) {
    static const std::map<std::string, std::size_t> sizes{
        {"uint32", 4}, {"uint64", 8}, {"int16", 2},
        {"float32", 4}, {"float64", 8},
    };
    const auto found = sizes.find(type);
    if (found == sizes.end()) {
        throw std::runtime_error("unsupported scalar type '" + type + "'");
    }
    return found->second;
}

std::size_t element_size(const std::string& type) {
    static const std::map<std::string, std::size_t> sizes{
        {"complex_int16", 4}, {"complex_float32", 8}, {"float32", 4},
    };
    const auto found = sizes.find(type);
    if (found == sizes.end()) {
        throw std::runtime_error("unsupported payload element '" + type + "'");
    }
    return found->second;
}

Contract parse_contract(const fs::path& path) {
    Contract value;
    value.source_json = read_file(path);
    if (integer_value(value.source_json, "schema_version") != 1) {
        throw std::runtime_error(path.string() + ": unsupported schema_version");
    }
    if (string_value(value.source_json, "byte_order") != "little_endian") {
        throw std::runtime_error(path.string() + ": only little_endian is supported");
    }
    value.name = string_value(value.source_json, "name");
    value.frame = string_value(value.source_json, "cpp_frame");
    value.metadata = string_value(value.source_json, "cpp_metadata");
    value.type_id = integer_value(value.source_json, "type_id");
    const auto parsed_version = integer_value(value.source_json, "type_version");
    if (parsed_version > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error(path.string() + ": type_version is too large");
    }
    value.type_version = static_cast<std::uint32_t>(parsed_version);
    value.metadata_size = integer_value(value.source_json, "metadata_size");

    const auto fields = delimited_value(value.source_json, "fields", '[', ']');
    const std::regex object_pattern{R"(\{[^{}]*\})"};
    for (auto it = std::sregex_iterator(fields.begin(), fields.end(), object_pattern);
         it != std::sregex_iterator(); ++it) {
        const std::string object = it->str();
        value.fields.push_back({
            string_value(object, "name"),
            string_value(object, "type"),
            static_cast<std::size_t>(integer_value(object, "offset")),
            bool_value_or(object, "reserved", false),
        });
    }
    const auto payload = delimited_value(value.source_json, "payload", '{', '}');
    if (string_value(payload, "kind") != "matrix") {
        throw std::runtime_error(path.string() + ": only matrix payloads are supported");
    }
    value.element_type = string_value(payload, "element_type");
    value.cpp_element = string_value(payload, "cpp_element");
    value.rows = string_value(payload, "rows");
    value.columns = string_value(payload, "columns");
    value.payload_offset = integer_value(payload, "offset");
    const auto visualization =
        delimited_value(value.source_json, "visualization", '{', '}');
    value.visualization_kind = string_value(visualization, "kind");
    return value;
}

void validate_contract(const Contract& value) {
    if (value.type_id == 0 || value.type_version == 0) {
        throw std::runtime_error(value.name + ": type_id and type_version must be non-zero");
    }
    if (value.fields.empty() || value.payload_offset != value.metadata_size) {
        throw std::runtime_error(value.name + ": invalid metadata/payload boundary");
    }
    std::vector<bool> occupied(value.metadata_size, false);
    std::set<std::string> field_names;
    std::set<std::string> public_fields;
    for (const auto& field : value.fields) {
        if (!field_names.insert(field.name).second) {
            throw std::runtime_error(value.name + ": duplicate metadata field name");
        }
        const auto size = scalar_size(field.type);
        if (field.offset > value.metadata_size ||
            size > value.metadata_size - field.offset) {
            throw std::runtime_error(value.name + ": field exceeds metadata boundary");
        }
        for (std::size_t index = field.offset; index < field.offset + size; ++index) {
            if (occupied[index]) {
                throw std::runtime_error(value.name + ": metadata fields overlap");
            }
            occupied[index] = true;
        }
        if (!field.reserved) {
            public_fields.insert(field.name);
        }
    }
    if (std::find(occupied.begin(), occupied.end(), false) != occupied.end()) {
        throw std::runtime_error(
            value.name + ": metadata padding must be an explicit reserved field");
    }
    if (!public_fields.count(value.rows) || !public_fields.count(value.columns)) {
        throw std::runtime_error(value.name + ": payload shape references an unknown field");
    }
    static_cast<void>(element_size(value.element_type));
}

std::string cpp_scalar(const std::string& type) {
    static const std::map<std::string, std::string> names{
        {"uint32", "std::uint32_t"}, {"uint64", "std::uint64_t"},
        {"int16", "std::int16_t"}, {"float32", "float"},
        {"float64", "double"},
    };
    return names.at(type);
}

std::string generate_cpp(const std::vector<Contract>& contracts) {
    std::ostringstream out;
    out << "// Generated by contract-codegen. Do not edit.\n#pragma once\n\n";
    out << "namespace uestcradar::internal {\n\n";
    for (const auto& contract : contracts) {
        out << "template <>\nstruct ContractTraits<" << contract.frame << "> {\n"
            << "    using Metadata = " << contract.metadata << ";\n"
            << "    using Element = " << contract.cpp_element << ";\n"
            << "    static std::uint64_t type_id() noexcept;\n"
            << "    static std::uint32_t type_version() noexcept;\n"
            << "    static std::size_t metadata_bytes() noexcept;\n"
            << "    static std::size_t rows(const Metadata& value) noexcept;\n"
            << "    static std::size_t columns(const Metadata& value) noexcept;\n"
            << "    static std::size_t payload_bytes(const Metadata& value);\n"
            << "    static void store(std::span<std::byte> payload, const Metadata& value);\n"
            << "    static Metadata load(std::span<const std::byte> payload);\n"
            << "};\n\n";
    }
    out << "}  // namespace uestcradar::internal\n";
    return out.str();
}

std::string generate_cpp_impl(const Contract& contract) {
    std::ostringstream out;
    out << "// Generated by contract-codegen. Do not edit.\n"
        << "#include <data.h>\n\n"
        << "#include \"contract_runtime.hpp\"\n"
        << "#include \"contract_traits.generated.hpp\"\n\n"
        << "namespace uestcradar::internal {\n\n"
        << "namespace {\n"
        << "constexpr std::size_t kMetadataBytes = " << contract.metadata_size << "U;\n"
        << "constexpr std::size_t kElementBytes = " << element_size(contract.element_type) << "U;\n"
        << "constexpr const char* kName = \"" << contract.name << "\";\n"
        << "static_assert(sizeof(ContractTraits<" << contract.frame
        << ">::Element) == kElementBytes, \"payload element ABI mismatch\");\n"
        << "}  // namespace\n\n"
        << "std::uint64_t ContractTraits<" << contract.frame << ">::type_id() noexcept { return "
        << contract.type_id << "ULL; }\n"
        << "std::uint32_t ContractTraits<" << contract.frame << ">::type_version() noexcept { return "
        << contract.type_version << "U; }\n"
        << "std::size_t ContractTraits<" << contract.frame << ">::metadata_bytes() noexcept { return kMetadataBytes; }\n"
        << "std::size_t ContractTraits<" << contract.frame << ">::rows(const Metadata& value) noexcept { return value."
        << contract.rows << "; }\n"
        << "std::size_t ContractTraits<" << contract.frame << ">::columns(const Metadata& value) noexcept { return value."
        << contract.columns << "; }\n"
        << "std::size_t ContractTraits<" << contract.frame << ">::payload_bytes(const Metadata& value) {\n"
        << "    return checked_matrix_bytes(kMetadataBytes, rows(value), columns(value), kElementBytes, kName);\n"
        << "}\n"
        << "void ContractTraits<" << contract.frame << ">::store(std::span<std::byte> payload, const Metadata& value) {\n"
        << "    require_metadata_span(payload, kMetadataBytes, kName);\n";
        for (const auto& field : contract.fields) {
            if (field.reserved) {
                out << "    store_wire<" << cpp_scalar(field.type) << ">(payload, "
                    << field.offset << "U, {});\n";
            } else {
                out << "    store_wire<" << cpp_scalar(field.type) << ">(payload, "
                    << field.offset << "U, value." << field.name << ");\n";
            }
        }
        out << "}\n"
            << "ContractTraits<" << contract.frame << ">::Metadata ContractTraits<"
            << contract.frame << ">::load(std::span<const std::byte> payload) {\n"
            << "    require_metadata_span(payload, kMetadataBytes, kName);\n";
        for (const auto& field : contract.fields) {
            if (field.reserved) {
                out << "    if (load_wire<" << cpp_scalar(field.type) << ">(payload, "
                    << field.offset << "U) != 0) throw std::invalid_argument(std::string{kName} + \" reserved metadata is not zero\");\n";
            }
        }
        out << "    return {\n";
        for (const auto& field : contract.fields) {
            if (!field.reserved) {
                out << "        ." << field.name << " = load_wire<"
                    << cpp_scalar(field.type) << ">(payload, " << field.offset << "U),\n";
            }
        }
        out << "    };\n}\n\n";
    out << "}  // namespace uestcradar::internal\n";
    return out.str();
}

std::string generate_manifest(const std::vector<Contract>& contracts) {
    std::ostringstream out;
    out << "{\n  \"schema_version\": 1,\n  \"contracts\": [\n";
    for (std::size_t index = 0; index < contracts.size(); ++index) {
        std::istringstream lines(contracts[index].source_json);
        std::string line;
        while (std::getline(lines, line)) {
            out << "    " << line << '\n';
        }
        if (index + 1 != contracts.size()) {
            out.seekp(-1, std::ios_base::cur);
            out << ",\n";
        }
    }
    out << "  ]\n}\n";
    return out.str();
}

std::string generate_go(const std::vector<Contract>& contracts) {
    std::ostringstream out;
    out << "// Code generated by contract-codegen. DO NOT EDIT.\n"
        << "package contracts\n\n"
        << "import (\n  \"encoding/binary\"\n  \"fmt\"\n  \"math\"\n)\n\n"
        << "type Field struct { Name, Type string; Offset uint32; Reserved bool }\n"
        << "type Contract struct { Name string; TypeID uint64; TypeVersion uint32; MetadataBytes, ElementBytes uint32; ElementType, Rows, Columns, Visualization string; Fields []Field }\n\n"
        << "var Catalog = map[uint64]Contract{\n";
    for (const auto& contract : contracts) {
        out << "  " << contract.type_id << ": {Name: \"" << contract.name
            << "\", TypeID: " << contract.type_id << ", TypeVersion: "
            << contract.type_version << ", MetadataBytes: " << contract.metadata_size
            << ", ElementBytes: " << element_size(contract.element_type)
            << ", ElementType: \"" << contract.element_type << "\", Rows: \""
            << contract.rows << "\", Columns: \"" << contract.columns
            << "\", Visualization: \"" << contract.visualization_kind << "\", Fields: []Field{";
        for (const auto& field : contract.fields) {
            out << "{Name: \"" << field.name << "\", Type: \"" << field.type
                << "\", Offset: " << field.offset << ", Reserved: "
                << (field.reserved ? "true" : "false") << "},";
        }
        out << "}},\n";
    }
    out << "}\n\n"
        << "func DecodeMetadata(typeID uint64, version uint32, payload []byte) (map[string]any, error) {\n"
        << "  c, ok := Catalog[typeID]; if !ok || c.TypeVersion != version { return nil, fmt.Errorf(\"unsupported contract %d/%d\", typeID, version) }\n"
        << "  if len(payload) < int(c.MetadataBytes) { return nil, fmt.Errorf(\"truncated %s metadata\", c.Name) }\n"
        << "  result := make(map[string]any)\n  for _, f := range c.Fields {\n    var v any\n    switch f.Type {\n"
        << "    case \"uint32\": v = binary.LittleEndian.Uint32(payload[f.Offset:f.Offset+4])\n"
        << "    case \"uint64\": v = binary.LittleEndian.Uint64(payload[f.Offset:f.Offset+8])\n"
        << "    case \"int16\": v = int16(binary.LittleEndian.Uint16(payload[f.Offset:f.Offset+2]))\n"
        << "    case \"float32\": v = math.Float32frombits(binary.LittleEndian.Uint32(payload[f.Offset:f.Offset+4]))\n"
        << "    case \"float64\": v = math.Float64frombits(binary.LittleEndian.Uint64(payload[f.Offset:f.Offset+8]))\n"
        << "    default: return nil, fmt.Errorf(\"unsupported field type %s\", f.Type)\n    }\n"
        << "    if f.Reserved { if !numericZero(v) { return nil, fmt.Errorf(\"non-zero reserved field %s\", f.Name) }; continue }; result[f.Name] = v\n"
        << "  }\n  rows, ok := dimension(result[c.Rows]); if !ok { return nil, fmt.Errorf(\"invalid %s rows\", c.Name) }; columns, ok := dimension(result[c.Columns]); if !ok { return nil, fmt.Errorf(\"invalid %s columns\", c.Name) }\n"
        << "  if rows == 0 || columns == 0 || rows > (^uint64(0))/columns || rows*columns > (^uint64(0)-uint64(c.MetadataBytes))/uint64(c.ElementBytes) { return nil, fmt.Errorf(\"invalid %s dimensions\", c.Name) }\n"
        << "  expected := uint64(c.MetadataBytes) + rows*columns*uint64(c.ElementBytes); if expected != uint64(len(payload)) { return nil, fmt.Errorf(\"%s payload length mismatch\", c.Name) }; return result, nil\n}\n\n"
        << "func numericZero(value any) bool { switch v := value.(type) { case uint32: return v == 0; case uint64: return v == 0; case int16: return v == 0; case float32: return v == 0; case float64: return v == 0 }; return false }\n"
        << "func dimension(value any) (uint64, bool) { switch v := value.(type) { case uint32: return uint64(v), true; case uint64: return v, true; case int16: return uint64(v), v >= 0 }; return 0, false }\n";
    return out.str();
}

std::string generate_typescript(const std::vector<Contract>& contracts) {
    std::ostringstream out;
    out << "// Generated by contract-codegen. Do not edit.\n"
        << "export interface ContractField { name: string; type: string; offset: number; reserved: boolean }\n"
        << "export interface ContractDescriptor { name: string; typeId: bigint; typeVersion: number; metadataBytes: number; elementBytes: number; elementType: string; rows: string; columns: string; visualization: string; fields: readonly ContractField[] }\n"
        << "export const contracts: ReadonlyMap<bigint, ContractDescriptor> = new Map([\n";
    for (const auto& contract : contracts) {
        out << "  [" << contract.type_id << "n, {name: \"" << contract.name
            << "\", typeId: " << contract.type_id << "n, typeVersion: "
            << contract.type_version << ", metadataBytes: " << contract.metadata_size
            << ", elementBytes: " << element_size(contract.element_type)
            << ", elementType: \"" << contract.element_type << "\", rows: \""
            << contract.rows << "\", columns: \"" << contract.columns
            << "\", visualization: \"" << contract.visualization_kind << "\", fields: [";
        for (const auto& field : contract.fields) {
            out << "{name: \"" << field.name << "\", type: \"" << field.type
                << "\", offset: " << field.offset << ", reserved: "
                << (field.reserved ? "true" : "false") << "},";
        }
        out << "]}],\n";
    }
    out << "]);\n\n"
        << "export function decodeMetadata(typeId: bigint, typeVersion: number, payload: Uint8Array): Readonly<Record<string, number | bigint>> {\n"
        << "  const c = contracts.get(typeId); if (!c || c.typeVersion !== typeVersion) throw new Error(`unsupported contract ${typeId}/${typeVersion}`);\n"
        << "  if (payload.byteLength < c.metadataBytes) throw new Error(`truncated ${c.name} metadata`);\n"
        << "  const view = new DataView(payload.buffer, payload.byteOffset, payload.byteLength); const result: Record<string, number | bigint> = {};\n"
        << "  for (const f of c.fields) { let value: number | bigint; switch (f.type) {\n"
        << "    case \"uint32\": value = view.getUint32(f.offset, true); break; case \"uint64\": value = view.getBigUint64(f.offset, true); break;\n"
        << "    case \"int16\": value = view.getInt16(f.offset, true); break; case \"float32\": value = view.getFloat32(f.offset, true); break; case \"float64\": value = view.getFloat64(f.offset, true); break;\n"
        << "    default: throw new Error(`unsupported field type ${f.type}`); }\n"
        << "    if (f.reserved) { if (value !== 0 && value !== 0n) throw new Error(`non-zero reserved field ${f.name}`); } else result[f.name] = value;\n"
        << "  }\n  const rows = Number(result[c.rows]); const columns = Number(result[c.columns]); const expected = c.metadataBytes + rows * columns * c.elementBytes;\n"
        << "  if (!Number.isSafeInteger(rows) || !Number.isSafeInteger(columns) || rows <= 0 || columns <= 0 || !Number.isSafeInteger(expected) || expected !== payload.byteLength) throw new Error(`${c.name} payload length mismatch`);\n"
        << "  return result;\n}\n";
    return out.str();
}

std::vector<Contract> load_catalog(
    const fs::path& catalog_path, const fs::path& contracts_dir) {
    const std::string catalog = read_file(catalog_path);
    const std::regex entry{
        R"(UESTCRADAR_CONTRACT\(\s*([a-zA-Z0-9_]+)\s*,\s*([a-zA-Z0-9_]+)\s*,\s*([a-zA-Z0-9_]+)\s*\))"};
    std::vector<Contract> result;
    std::set<std::uint64_t> ids;
    for (auto it = std::sregex_iterator(catalog.begin(), catalog.end(), entry);
         it != std::sregex_iterator(); ++it) {
        Contract value = parse_contract(contracts_dir / (it->str(1) + ".json"));
        if (value.name != it->str(1) || value.frame != it->str(2) ||
            value.metadata != it->str(3)) {
            throw std::runtime_error(value.name + ": catalog and JSON names differ");
        }
        validate_contract(value);
        if (!ids.insert(value.type_id).second) {
            throw std::runtime_error("duplicate type_id " + std::to_string(value.type_id));
        }
        result.push_back(std::move(value));
    }
    if (result.empty()) {
        throw std::runtime_error("contract catalog is empty");
    }
    return result;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 4) {
            std::cerr << "usage: contract-codegen <catalog.def> <contracts-dir> <output-dir>\n";
            return 2;
        }
        const auto contracts = load_catalog(argv[1], argv[2]);
        const fs::path output_dir = argv[3];
        write_file(output_dir / "contract_traits.generated.hpp", generate_cpp(contracts));
        for (const auto& contract : contracts) {
            write_file(
                output_dir / ("contract_" + contract.name + ".generated.cpp"),
                generate_cpp_impl(contract));
        }
        write_file(output_dir / "contracts.manifest.json", generate_manifest(contracts));
        write_file(output_dir / "contracts.generated.go", generate_go(contracts));
        write_file(output_dir / "contracts.generated.ts", generate_typescript(contracts));
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "contract-codegen: " << error.what() << '\n';
        return 1;
    }
}
