#include "loop_manager.hpp"
#include "utils.hpp"

#include <sstream>
#include <algorithm>
#include <climits>
#include <cstdlib>

namespace tpm_vault {

std::string LoopManager::attach(const std::string& image_path) {
    // Проверяем, не подключён ли уже
    std::string existing = find_loop_for_file(image_path);
    if (!existing.empty()) {
        return existing; // Уже подключён
    }
    
    // losetup --find --show <image_path>
    std::string cmd = "losetup --find --show " + image_path;
    
    try {
        std::string loop_device = execute_command_output(cmd);
        
        // Проверяем что получили валидный путь
        if (loop_device.empty() || loop_device.find("/dev/loop") != 0) {
            throw VaultError("Invalid loop device returned: " + loop_device);
        }
        
        return loop_device;
    } catch (const VaultError&) {
        throw VaultError("Failed to attach " + image_path + " as loop device");
    }
}

void LoopManager::detach(const std::string& loop_device) {
    // losetup -d /dev/loopX
    std::string cmd = "losetup -d " + loop_device;
    
    int ret = execute_command(cmd);
    if (ret != 0) {
        throw VaultError("Failed to detach loop device " + loop_device);
    }
}

std::string LoopManager::find_loop_for_file(const std::string& image_path) {
    // Получаем абсолютный путь к файлу для сравнения
    char resolved_path[PATH_MAX];
    if (realpath(image_path.c_str(), resolved_path) == nullptr) {
        return ""; // Файл не существует
    }
    std::string abs_path(resolved_path);
    
    // losetup -j <image_path> выводит ассоциированные loop-устройства
    std::string cmd = "losetup -j " + abs_path + " 2>/dev/null";
    
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        return "";
    }
    
    char buffer[1024];
    std::string result;
    if (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result = buffer;
    }
    pclose(pipe);
    
    if (result.empty()) {
        return "";
    }
    
    // Формат вывода: /dev/loop0: [64769]:123456 (/path/to/file)
    // Извлекаем имя устройства до двоеточия
    size_t colon_pos = result.find(':');
    if (colon_pos != std::string::npos) {
        return result.substr(0, colon_pos);
    }
    
    return "";
}

std::vector<std::pair<std::string, std::string>> LoopManager::list_attached() {
    std::vector<std::pair<std::string, std::string>> result;
    
    // losetup -l выводит список всех loop-устройств
    // Формат: NAME SIZELIMIT OFFSET AUTOCLEAR RO BACK-FILE DIO LOG-SEC
    std::string cmd = "losetup -l -n -O NAME,BACK-FILE 2>/dev/null";
    
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        return result;
    }
    
    char buffer[512];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        std::string line(buffer);
        
        // Убираем trailing newline
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
            line.pop_back();
        }
        
        if (line.empty()) continue;
        
        // Разбираем строку: "/dev/loop0  /path/to/file"
        std::istringstream iss(line);
        std::string device, file;
        iss >> device;
        
        // Остаток строки - путь к файлу (может содержать пробелы)
        std::getline(iss >> std::ws, file);
        
        if (!device.empty() && !file.empty()) {
            result.emplace_back(device, file);
        }
    }
    
    pclose(pipe);
    return result;
}

} // namespace tpm_vault
