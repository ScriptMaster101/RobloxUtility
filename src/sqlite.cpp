

#include "sqlite.h"
#include <windows.h>
#include <algorithm>
#include <cstring>
#include <regex>

using namespace sqlite_minimal;

uint64_t Database::readVarint(const uint8_t*& p) {
    uint64_t val = 0;
    for (int i = 0; i < 9; i++) {
        uint8_t b = *p++;
        val = (val << 7) | (b & 0x7F);
        if (!(b & 0x80)) break;
    }
    return val;
}

uint64_t Database::readVarint(const uint8_t* p, int& consumed) {
    const uint8_t* start = p;
    readVarint(p);
    consumed = static_cast<int>(p - start);

    uint64_t val = 0;
    p = start;
    for (int i = 0; i < 9; i++) {
        uint8_t b = *p++;
        val = (val << 7) | (b & 0x7F);
        if (!(b & 0x80)) break;
    }
    return val;
}

bool Database::open(const std::wstring& path) {

    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    DWORD size = GetFileSize(hFile, nullptr);
    if (size < 100) { CloseHandle(hFile); return false; }

    m_data.resize(size);
    DWORD read = 0;
    if (!ReadFile(hFile, m_data.data(), size, &read, nullptr) || read != size) {
        CloseHandle(hFile);
        return false;
    }
    CloseHandle(hFile);
    return open(m_data);
}

bool Database::open(const std::vector<uint8_t>& data) {
    m_data = data;
    if (m_data.size() < 100) return false;

    const char* magic = "SQLite format 3";
    if (memcmp(m_data.data(), magic, 15) != 0) return false;

    m_pageSize = ((uint16_t)m_data[16] << 8) | m_data[17];
    if (m_pageSize == 1) m_pageSize = 65536;
    if (m_pageSize < 512 || m_pageSize > 65536) return false;

    m_numPages = ((uint32_t)m_data[28] << 24) |
                 ((uint32_t)m_data[29] << 16) |
                 ((uint32_t)m_data[30] << 8)  |
                 ((uint32_t)m_data[31]);
    if (m_numPages == 0) return false;

    parseSchema();
    return !m_tables.empty();
}

uint32_t Database::getPageOffset(uint32_t pageNum) const {

    return (pageNum - 1) * m_pageSize;
}

const uint8_t* Database::getPage(uint32_t pageNum) const {
    if (pageNum < 1 || pageNum > m_numPages) return nullptr;
    uint32_t offset = getPageOffset(pageNum);
    if (offset + m_pageSize > m_data.size()) return nullptr;
    return m_data.data() + offset;
}

uint32_t Database::getPageDataOffset(uint32_t pageNum) const {

    return (pageNum == 1) ? 100 : 0;
}

std::vector<Database::Cell> Database::readLeafCells(uint32_t pageNum) {
    std::vector<Cell> cells;
    const uint8_t* page = getPage(pageNum);
    if (!page) return cells;

    uint32_t dataOff = getPageDataOffset(pageNum);
    const uint8_t* btree = page + dataOff;

    uint8_t pageType = btree[0];
    
    if (pageType == 0x05) {

        uint16_t numCells = (btree[3] << 8) | btree[4];
        const uint8_t* ptrs = btree + 12;
        
        for (uint16_t i = 0; i < numCells; i++) {

            uint32_t childPage = ((uint32_t)ptrs[0] << 24) |
                                 ((uint32_t)ptrs[1] << 16) |
                                 ((uint32_t)ptrs[2] << 8)  |
                                  (uint32_t)ptrs[3];
            ptrs += 4;

            while (*ptrs & 0x80) ptrs++;
            ptrs++;
            
            auto childCells = readLeafCells(childPage);
            cells.insert(cells.end(), childCells.begin(), childCells.end());
        }

        uint32_t rightChild = ((uint32_t)btree[8] << 24) |
                              ((uint32_t)btree[9] << 16) |
                              ((uint32_t)btree[10] << 8) |
                               (uint32_t)btree[11];
        if (rightChild != 0) {
            auto childCells = readLeafCells(rightChild);
            cells.insert(cells.end(), childCells.begin(), childCells.end());
        }
        return cells;
    }
    
    if (pageType != 0x0D) return cells;

    uint16_t numCells    = (btree[3] << 8) | btree[4];
    uint16_t cellStart   = (btree[5] << 8) | btree[6];

    const uint8_t* ptrs = btree + 8;

    for (uint16_t i = 0; i < numCells; i++) {
        uint16_t cellOff = (ptrs[i*2] << 8) | ptrs[i*2 + 1];

        if (cellOff + 4 > m_pageSize) continue;

        const uint8_t* cell = page + cellOff;
        const uint8_t* p = cell;

        uint64_t payloadLen = readVarint(p);
        uint64_t rowId      = readVarint(p);

        Cell c;
        c.rowId       = rowId;
        c.payload     = p;
        c.payloadLen  = static_cast<uint32_t>(payloadLen);
        c.overflowPage= 0;

        uint32_t maxLocal = m_pageSize - 35;
        if (payloadLen > maxLocal) {

            if (payloadLen > maxLocal + 4) {
                c.payloadLen = maxLocal - 4;
                const uint8_t* ov = p + c.payloadLen;
                c.overflowPage = ((uint32_t)ov[0] << 24) |
                                 ((uint32_t)ov[1] << 16) |
                                 ((uint32_t)ov[2] << 8)  |
                                 ((uint32_t)ov[3]);

            } else {
                c.payloadLen = maxLocal;
            }
        }

        cells.push_back(c);
    }

    return cells;
}

uint32_t Database::getNextLeafPage(uint32_t pageNum) {

    (void)pageNum;
    return 0;
}

Row Database::deserializeRecord(const uint8_t* payload, uint32_t len) {
    Row row;
    if (len < 1) return row;

    const uint8_t* p = payload;
    const uint8_t* end = payload + len;

    uint64_t headerSize = readVarint(p);
    if (headerSize == 0 || p + headerSize > end) return row;

    const uint8_t* headerEnd = p + headerSize - 1;

    std::vector<uint64_t> serialTypes;
    while (p < headerEnd) {
        serialTypes.push_back(readVarint(p));
    }

    const uint8_t* dataPtr = headerEnd;

    for (auto st : serialTypes) {
        if (st == 0) {

            row.push_back(std::monostate{});
        } else if (st == 1) {
            row.push_back((int64_t)(int8_t)*dataPtr);
            dataPtr += 1;
        } else if (st == 2) {
            row.push_back((int64_t)(int16_t)((dataPtr[0] << 8) | dataPtr[1]));
            dataPtr += 2;
        } else if (st == 3) {
            int64_t val = ((int64_t)dataPtr[0] << 16) |
                          ((int64_t)dataPtr[1] << 8)  |
                           (int64_t)dataPtr[2];

            if (val & 0x800000) val |= 0xFFFFFFFFFF000000ULL;
            row.push_back(val);
            dataPtr += 3;
        } else if (st == 4) {
            row.push_back((int64_t)(int32_t)((dataPtr[0] << 24) |
                                              (dataPtr[1] << 16) |
                                              (dataPtr[2] << 8)  |
                                               dataPtr[3]));
            dataPtr += 4;
        } else if (st == 5) {

            int64_t val = ((int64_t)dataPtr[0] << 40) |
                          ((int64_t)dataPtr[1] << 32) |
                          ((int64_t)dataPtr[2] << 24) |
                          ((int64_t)dataPtr[3] << 16) |
                          ((int64_t)dataPtr[4] << 8)  |
                           (int64_t)dataPtr[5];
            if (val & 0x800000000000ULL) val |= 0xFFFF000000000000ULL;
            row.push_back(val);
            dataPtr += 6;
        } else if (st == 6) {
            row.push_back((int64_t)((int64_t)dataPtr[0] << 56 |
                                     (int64_t)dataPtr[1] << 48 |
                                     (int64_t)dataPtr[2] << 40 |
                                     (int64_t)dataPtr[3] << 32 |
                                     (int64_t)dataPtr[4] << 24 |
                                     (int64_t)dataPtr[5] << 16 |
                                     (int64_t)dataPtr[6] << 8  |
                                     (int64_t)dataPtr[7]));
            dataPtr += 8;
        } else if (st == 7) {

            uint64_t bits = 0;
            for (int i = 0; i < 8; i++) bits = (bits << 8) | dataPtr[i];
            double d;
            memcpy(&d, &bits, sizeof(d));
            row.push_back(d);
            dataPtr += 8;
        } else if (st == 8) {
            row.push_back((int64_t)0);
        } else if (st == 9) {
            row.push_back((int64_t)1);
        } else if (st >= 12 && (st % 2 == 0)) {

            uint64_t blobLen = (st - 12) / 2;
            if (dataPtr + blobLen <= end) {
                row.push_back(std::vector<uint8_t>(dataPtr, dataPtr + blobLen));
                dataPtr += blobLen;
            }
        } else if (st >= 13 && (st % 2 == 1)) {

            uint64_t textLen = (st - 13) / 2;
            if (dataPtr + textLen <= end) {
                row.push_back(std::string((const char*)dataPtr, textLen));
                dataPtr += textLen;
            }
        }
    }

    return row;
}

void Database::parseSchema() {

    auto cells = readLeafCells(1);
    
    for (auto& cell : cells) {
        Row row = deserializeRecord(cell.payload, cell.payloadLen);
        if (row.size() < 5) continue;

        std::string type  = asText(row[0]);
        std::string name  = asText(row[1]);

        int64_t rootPage  = asInt(row[3]);
        std::string sql   = asText(row[4]);

        if (type == "table" && !name.empty()) {
            TableInfo info;
            info.name     = name;
            info.rootPage = static_cast<int>(rootPage);
            info.createSql= sql;
            info.columns  = parseCreateTable(sql);
            m_tables[name] = info;
        }
    }
}

std::vector<ColumnDef> Database::parseCreateTable(const std::string& sql) {
    std::vector<ColumnDef> cols;

    size_t open = sql.find('(');
    size_t close = sql.rfind(')');
    if (open == std::string::npos || close == std::string::npos || close <= open) return cols;

    std::string colSection = sql.substr(open + 1, close - open - 1);
    

    std::vector<std::string> parts;
    int depth = 0;
    std::string current;
    for (char c : colSection) {
        if (c == '(') depth++;
        else if (c == ')') depth--;
        if (c == ',' && depth == 0) {
            parts.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }
    if (!current.empty()) parts.push_back(current);

    int idx = 0;
    for (auto& part : parts) {

        size_t start = part.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        part = part.substr(start);
        

        size_t space = part.find_first_of(" \t\r\n(");
        if (space == std::string::npos) space = part.size();
        std::string colName = part.substr(0, space);
        

        std::string upper = colName;
        std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
        if (upper == "PRIMARY" || upper == "FOREIGN" || upper == "UNIQUE" ||
            upper == "CHECK" || upper == "CONSTRAINT") {
            continue;
        }

        ColumnDef cd;
        cd.name = colName;
        cd.index = idx++;
        cols.push_back(cd);
    }
    return cols;
}

const TableInfo* Database::getTable(const std::string& name) const {
    auto it = m_tables.find(name);
    return (it != m_tables.end()) ? &it->second : nullptr;
}

std::vector<Row> Database::readTable(const std::string& tableName,
                                      const std::string& hostFilter) {
    std::vector<Row> rows;
    const TableInfo* table = getTable(tableName);
    if (!table) return rows;

    int hostKeyIdx = -1;
    for (auto& col : table->columns) {
        if (col.name == "host_key") { hostKeyIdx = col.index; break; }
    }

    uint32_t pageNum = table->rootPage;
    while (pageNum != 0) {
        auto cells = readLeafCells(pageNum);
        for (auto& cell : cells) {
            Row row = deserializeRecord(cell.payload, cell.payloadLen);
            if (row.empty()) continue;

            if (!hostFilter.empty() && hostKeyIdx >= 0 && hostKeyIdx < (int)row.size()) {
                std::string host = asText(row[hostKeyIdx]);

                if (host != hostFilter) {

                    if (hostFilter[0] == '.' && host.find(hostFilter) == std::string::npos) {
                        continue;
                    }
                    if (hostFilter[0] != '.' && host != hostFilter) {
                        continue;
                    }
                }
            }

            rows.push_back(std::move(row));
        }
        pageNum = getNextLeafPage(pageNum);
    }
    return rows;
}

const CellValue* Database::getColumn(const Row& row,
                                      const std::vector<ColumnDef>& cols,
                                      const std::string& colName) {
    for (auto& col : cols) {
        if (col.name == colName && col.index < (int)row.size()) {
            return &row[col.index];
        }
    }
    return nullptr;
}

bool Database::isNull(const CellValue& v) {
    return std::holds_alternative<std::monostate>(v);
}

int64_t Database::asInt(const CellValue& v) {
    if (std::holds_alternative<int64_t>(v)) return std::get<int64_t>(v);
    if (std::holds_alternative<double>(v))  return (int64_t)std::get<double>(v);
    if (std::holds_alternative<std::string>(v)) {
        try { return std::stoll(std::get<std::string>(v)); }
        catch (...) { return 0; }
    }
    return 0;
}

std::string Database::asText(const CellValue& v) {
    if (std::holds_alternative<std::string>(v)) return std::get<std::string>(v);
    if (std::holds_alternative<int64_t>(v)) return std::to_string(std::get<int64_t>(v));
    if (std::holds_alternative<double>(v))  return std::to_string(std::get<double>(v));
    if (std::holds_alternative<std::vector<uint8_t>>(v)) {
        auto& blob = std::get<std::vector<uint8_t>>(v);
        return std::string((const char*)blob.data(), blob.size());
    }
    return "";
}

const std::vector<uint8_t>& Database::asBlob(const CellValue& v) {
    static std::vector<uint8_t> empty;
    if (std::holds_alternative<std::vector<uint8_t>>(v))
        return std::get<std::vector<uint8_t>>(v);
    return empty;
}
