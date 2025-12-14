#include "luks_manager.hpp"
#include "utils.hpp"

#include <sstream>

namespace tpm_vault {

std::string LuksManager::get_mapper_path(const std::string& mapper_name) {
    return "/dev/mapper/" + mapper_name;
}

std::string LuksManager::get_mapper_name(const std::string& vault_name) {
    return "tpm-vault-" + vault_name;
}

void LuksManager::format(const std::string& device, const std::vector<uint8_t>& key) {
    // cryptsetup luksFormat --type luks2 --key-file - --key-size 512 <device>
    // --batch-mode отключает интерактивные запросы
    // --key-size в битах (512 бит = 64 байта)
    std::ostringstream cmd;
    cmd << "cryptsetup luksFormat"
        << " --type luks2"
        << " --batch-mode"
        << " --key-file -"
        << " --key-size " << (key.size() * 8)  // размер в битах
        << " " << device;
    
    int ret = execute_command(cmd.str(), &key);
    if (ret != 0) {
        throw VaultError("Failed to format LUKS container on " + device);
    }
}

void LuksManager::open(const std::string& device, const std::string& mapper_name,
                       const std::vector<uint8_t>& key) {
    // Проверяем, не открыт ли уже
    if (is_open(mapper_name)) {
        throw VaultError(mapper_name + " is already open");
    }
    
    // cryptsetup luksOpen --key-file - <device> <mapper-name>
    std::ostringstream cmd;
    cmd << "cryptsetup open"
        << " --type luks2"
        << " --key-file -"
        << " " << device
        << " " << mapper_name;
    
    int ret = execute_command(cmd.str(), &key);
    if (ret != 0) {
        throw VaultError("Failed to open LUKS container on " + device);
    }
}

void LuksManager::close(const std::string& mapper_name) {
    if (!is_open(mapper_name)) {
        return; // Уже закрыт
    }
    
    // cryptsetup luksClose <mapper-name>
    std::ostringstream cmd;
    cmd << "cryptsetup close " << mapper_name;
    
    int ret = execute_command(cmd.str());
    if (ret != 0) {
        throw VaultError("Failed to close LUKS container " + mapper_name);
    }
}

bool LuksManager::is_open(const std::string& mapper_name) {
    std::string mapper_path = get_mapper_path(mapper_name);
    return file_exists(mapper_path);
}

} // namespace tpm_vault
