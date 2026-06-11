

#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <map>
#include <variant>

namespace sqlite_minimal {

using CellValue = std::variant<
    std::monostate,
    int64_t,
    double,
    std::string,
    std::vector<uint8_t>
>;

using Row = std::vector<CellValue>;

struct ColumnDef {
    std::string name;
    int         index;
};

struct TableInfo {
    std::string name;
    std::vector<ColumnDef> columns;
    int         rootPage;
    std::string createSql;
};

class Database {
public:

    bool open(const std::wstring& path);
    bool open(const std::vector<uint8_t>& data);

    const TableInfo* getTable(const std::string& name) const;

    std::vector<Row> readTable(const std::string& tableName,
                                const std::string& hostFilter = "");

    static const CellValue* getColumn(const Row& row,
                                       const std::vector<ColumnDef>& cols,
                                       const std::string& colName);

    static bool        isNull(const CellValue& v);
    static int64_t     asInt(const CellValue& v);
    static std::string asText(const CellValue& v);
    static const std::vector<uint8_t>& asBlob(const CellValue& v);

    uint32_t pageSize() const { return m_pageSize; }

private:

    static uint64_t readVarint(const uint8_t*& p);
    static uint64_t readVarint(const uint8_t* p, int& consumed);

    const uint8_t* getPage(uint32_t pageNum) const;
    uint32_t       getPageOffset(uint32_t pageNum) const;
    uint32_t       getPageDataOffset(uint32_t pageNum) const;

    struct Cell {
        uint64_t           rowId;
        const uint8_t*     payload;
        uint32_t           payloadLen;
        uint32_t           overflowPage;
    };
    std::vector<Cell> readLeafCells(uint32_t pageNum);
    uint32_t          getNextLeafPage(uint32_t pageNum);

    static Row deserializeRecord(const uint8_t* payload, uint32_t len);

    void parseSchema();
    static std::vector<ColumnDef> parseCreateTable(const std::string& sql);

    std::vector<uint8_t> m_data;
    uint32_t             m_pageSize = 0;
    uint32_t             m_numPages = 0;
    std::map<std::string, TableInfo> m_tables;
};

}
