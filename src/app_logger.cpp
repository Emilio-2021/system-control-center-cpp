#include "app_logger.h"

#include <ctime>
#include <fstream>
#include <iomanip>
#include <mutex>

namespace {

std::mutex log_mutex;

} // namespace

void write_log(const std::filesystem::path& root, std::string_view level,
               std::string_view message) {
    std::lock_guard<std::mutex> lock(log_mutex);
    std::error_code error;
    const auto log_directory = root / "logs";
    std::filesystem::create_directories(log_directory, error);
    if (error) return;

    const auto log_file = log_directory / "log.txt";
    constexpr std::uintmax_t maximum_log_size = 5 * 1024 * 1024;
    if (std::filesystem::exists(log_file, error) &&
        std::filesystem::file_size(log_file, error) >= maximum_log_size) {
        for (int backup = 3; backup >= 1; --backup) {
            const auto current = log_directory / ("log.txt." + std::to_string(backup));
            const auto next = log_directory / ("log.txt." + std::to_string(backup + 1));
            if (backup == 3) std::filesystem::remove(current, error);
            else if (std::filesystem::exists(current, error)) std::filesystem::rename(current, next, error);
        }
        std::filesystem::rename(log_file, log_directory / "log.txt.1", error);
    }

    std::ofstream output(log_file, std::ios::app);
    if (!output) return;
    const auto now = std::time(nullptr);
    std::tm local_time{};
#ifdef _WIN32
    localtime_s(&local_time, &now);
#else
    localtime_r(&now, &local_time);
#endif
    output << std::put_time(&local_time, "%Y-%m-%d %H:%M:%S")
           << " | " << level << " | " << message << '\n';
}
