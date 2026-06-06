#ifndef CSV_PARSER_H
#define CSV_PARSER_H

#include <string>
#include <vector>
#include <map>

class CsvParser {
public:
    using Row = std::map<std::string, std::string>;
    using Table = std::vector<Row>;

    static Table parse(const std::string& filename);

    static void write(const std::string& filename,
                      const std::vector<std::string>& headers,
                      const Table& table);

private:
    static std::vector<std::string> parseLine(const std::string& line);
    static std::string unquoteField(const std::string& field);
    static std::string quoteField(const std::string& field);
};

#endif // CSV_PARSER_H
