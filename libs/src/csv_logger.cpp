// =============================================================================
// csv_logger.cpp — Hiện thực CsvLogger
// =============================================================================

#include "../include/csv_logger.h"
#include <stdexcept>

// Mở file và ghi header row ngay khi khởi tạo.
// Throw nếu không mở được file (path sai hoặc không có quyền ghi).
CsvLogger::CsvLogger(const std::string& path, const std::vector<std::string>& columns) {
    file_.open(path);
    if (!file_.is_open())
        throw std::runtime_error("CsvLogger: cannot open file: " + path);
    write_row(columns);  // dòng đầu tiên = header
}

CsvLogger::~CsvLogger() {
    if (file_.is_open()) file_.close();
}

// Ghi một dòng dữ liệu và flush ngay (để không mất data nếu program crash).
void CsvLogger::write(const std::vector<std::string>& values) {
    write_row(values);
    file_.flush();  // flush ngay sau mỗi dòng, không đợi buffer đầy
}

// Ghi một dòng CSV: các cell ngăn cách bằng dấu phẩy, kết thúc bằng newline.
void CsvLogger::write_row(const std::vector<std::string>& cells) {
    for (size_t i = 0; i < cells.size(); ++i) {
        file_ << cells[i];
        if (i + 1 < cells.size()) file_ << ',';
    }
    file_ << '\n';
}
