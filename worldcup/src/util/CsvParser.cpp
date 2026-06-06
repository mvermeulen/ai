#include "CsvParser.h"
#include <fstream>
#include <stdexcept>

CsvParser::Table CsvParser::parse(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open CSV file: " + filename);
    }

    Table result;
    std::vector<std::string> headers;
    std::string line;
    int lineNum = 0;

    while (std::getline(file, line)) {
        ++lineNum;

        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (line.empty()) continue;

        auto fields = parseLine(line);

        if (lineNum == 1) {
            headers = fields;
            continue;
        }

        if (fields.size() != headers.size()) {
            throw std::runtime_error("CSV line " + std::to_string(lineNum) +
                                     " has " + std::to_string(fields.size()) +
                                     " fields but expected " +
                                     std::to_string(headers.size()));
        }

        Row row;
        for (size_t i = 0; i < headers.size(); ++i) {
            row[headers[i]] = fields[i];
        }
        result.push_back(row);
    }

    file.close();
    return result;
}

void CsvParser::write(const std::string& filename,
                      const std::vector<std::string>& headers,
                      const Table& table) {
    std::ofstream file(filename);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open CSV file for writing: " + filename);
    }

    for (size_t i = 0; i < headers.size(); ++i) {
        if (i > 0) file << ",";
        file << quoteField(headers[i]);
    }
    file << "\n";

    for (const auto& row : table) {
        for (size_t i = 0; i < headers.size(); ++i) {
            if (i > 0) file << ",";
            auto it = row.find(headers[i]);
            if (it != row.end()) {
                file << quoteField(it->second);
            } else {
                file << "\"\"";
            }
        }
        file << "\n";
    }

    file.close();
}

std::vector<std::string> CsvParser::parseLine(const std::string& line) {
    std::vector<std::string> fields;
    std::string field;
    bool inQuotes = false;
    size_t i = 0;

    while (i < line.length()) {
        char c = line[i];

        if (inQuotes) {
            if (c == '"') {
                if (i + 1 < line.length() && line[i + 1] == '"') {
                    field += '"';
                    i += 2;
                    continue;
                } else {
                    inQuotes = false;
                    i++;
                    continue;
                }
            } else {
                field += c;
                i++;
            }
        } else {
            if (c == '"') {
                inQuotes = true;
                i++;
            } else if (c == ',') {
                fields.push_back(field);
                field.clear();
                i++;
            } else {
                field += c;
                i++;
            }
        }
    }

    fields.push_back(field);
    return fields;
}

std::string CsvParser::unquoteField(const std::string& field) {
    if (field.length() < 2 || field.front() != '"' || field.back() != '"') {
        return field;
    }

    std::string unquoted = field.substr(1, field.length() - 2);
    std::string result;
    for (size_t i = 0; i < unquoted.length(); ++i) {
        if (unquoted[i] == '"' && i + 1 < unquoted.length() && unquoted[i + 1] == '"') {
            result += '"';
            i++;
        } else {
            result += unquoted[i];
        }
    }
    return result;
}

std::string CsvParser::quoteField(const std::string& field) {
    if (field.find(',') != std::string::npos ||
        field.find('"') != std::string::npos ||
        field.find('\n') != std::string::npos) {
        std::string quoted = "\"";
        for (char c : field) {
            if (c == '"') {
                quoted += "\"\"";
            } else {
                quoted += c;
            }
        }
        quoted += "\"";
        return quoted;
    }
    return field;
}
