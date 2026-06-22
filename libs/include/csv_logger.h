// =============================================================================
// csv_logger.h — Ghi dữ liệu đo vào file CSV
//
// Mỗi CsvLogger mở một file khi khởi tạo, ghi header, rồi cho phép
// append từng dòng dữ liệu. File được flush sau mỗi dòng để đảm bảo
// không mất data nếu chương trình bị tắt đột ngột.
// =============================================================================

#pragma once
#include <string>
#include <vector>
#include <fstream>

class CsvLogger {
public:
    // Mở file và ghi header. Throw std::runtime_error nếu không mở được.
    CsvLogger(const std::string& path, const std::vector<std::string>& columns);
    ~CsvLogger();

    // Ghi thêm một dòng dữ liệu. values phải khớp số cột với header.
    // Flush ngay sau khi ghi để tránh mất data khi crash.
    void write(const std::vector<std::string>& values);

    bool is_open() const { return file_.is_open(); }

private:
    std::ofstream file_;

    // Ghi một dòng: cells ngăn cách bằng dấu phẩy, kết thúc bằng '\n'
    void write_row(const std::vector<std::string>& cells);
};
