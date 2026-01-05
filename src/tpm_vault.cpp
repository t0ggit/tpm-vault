#include "tpm_vault.hpp"
#include "tpm_manager.hpp"
#include "luks_manager.hpp"
#include "loop_manager.hpp"
#include "utils.hpp"

#include <fstream>
#include <sstream>
#include <cstring>
#include <climits>
#include <exception>
#include <sys/mount.h>
#include <mntent.h>

namespace tpm_vault {

TpmVault::TpmVault() 
    : tpm_(std::make_unique<TpmManager>())
    , luks_(std::make_unique<LuksManager>())
    , loop_(std::make_unique<LoopManager>()) {
    
    // Проверяем права root
    if (!is_root()) {
        throw VaultError("This operation requires root privileges");
    }
    
    // Выполняем provisioning TPM
    tpm_->provision();
}

TpmVault::~TpmVault() = default;

std::string TpmVault::get_image_path(const std::string& name) const {
    return get_current_directory() + "/" + name + ".img";
}

std::string TpmVault::get_mount_path(const std::string& name) const {
    return get_current_directory() + "/" + name;
}

void TpmVault::create_image_file(const std::string& path, size_t size) {
    // Используем fallocate для быстрого создания файла
    std::ostringstream cmd;
    cmd << "fallocate -l " << size << " " << path;
    
    int ret = execute_command(cmd.str());
    if (ret != 0) {
        // Fallback на dd если fallocate не сработал
        cmd.str("");
        cmd << "dd if=/dev/zero of=" << path 
            << " bs=1M count=" << (size / (1024 * 1024))
            << " 2>/dev/null";
        ret = execute_command(cmd.str());
        if (ret != 0) {
            throw VaultError("Failed to create image file: " + path);
        }
    }
}

void TpmVault::create_filesystem(const std::string& device) {
    // mkfs.ext4 -q (quiet mode)
    std::string cmd = "mkfs.ext4 -q " + device;
    
    int ret = execute_command(cmd);
    if (ret != 0) {
        throw VaultError("Failed to create ext4 filesystem on " + device);
    }
}

void TpmVault::mount_filesystem(const std::string& device, const std::string& mount_point) {
    ensure_directory(mount_point);
    
    std::string cmd = "mount " + device + " " + mount_point;
    int ret = execute_command(cmd);
    if (ret != 0) {
        throw VaultError("Failed to mount " + device + " to " + mount_point);
    }
}

void TpmVault::unmount_filesystem(const std::string& mount_point) {
    if (!is_mounted(mount_point)) {
        return;
    }
    
    std::string cmd = "umount " + mount_point;
    int ret = execute_command(cmd);
    if (ret != 0) {
        throw VaultError("Failed to unmount " + mount_point);
    }
}

bool TpmVault::is_mounted(const std::string& mount_point) {
    // Получаем абсолютный путь
    char resolved[PATH_MAX];
    if (realpath(mount_point.c_str(), resolved) == nullptr) {
        return false;
    }
    std::string abs_path(resolved);
    
    // Читаем /proc/mounts
    FILE* mtab = setmntent("/proc/mounts", "r");
    if (!mtab) {
        return false;
    }
    
    struct mntent* entry;
    bool found = false;
    while ((entry = getmntent(mtab)) != nullptr) {
        if (abs_path == entry->mnt_dir) {
            found = true;
            break;
        }
    }
    
    endmntent(mtab);
    return found;
}

void TpmVault::create(const std::string& name, size_t size) {
    std::string image_path = get_image_path(name);
    std::string mapper_name = LuksManager::get_mapper_name(name);
    std::string mapper_path = LuksManager::get_mapper_path(mapper_name);
    
    // Проверяем, не существует ли уже образ
    if (file_exists(image_path)) {
        throw VaultError(name + ".img already exists in current directory");
    }
    
    // 1. Генерируем случайный мастер-ключ (64 байта / 512 бит)
    SecureBuffer master_key(KEY_SIZE);
    {
        auto random_bytes = generate_random_bytes(KEY_SIZE);
        std::memcpy(master_key.data(), random_bytes.data(), KEY_SIZE);
        secure_erase(random_bytes);
    }
    
    std::string loop_device;
    
    try {
        // 2. Создаём файл образа
        create_image_file(image_path, size);
        
        // 3. Подключаем как loop-устройство
        loop_device = loop_->attach(image_path);
        
        // 4. Форматируем как LUKS2
        luks_->format(loop_device, master_key.vector());
        
        // 5. Временно открываем для создания файловой системы
        luks_->open(loop_device, mapper_name, master_key.vector());
        
        // 6. Создаём файловую систему ext4
        create_filesystem(mapper_path);
        
        // 7. Закрываем LUKS
        luks_->close(mapper_name);
        
        // 8. Отключаем loop-устройство
        loop_->detach(loop_device);
        loop_device.clear();
        
        // 9. Запечатываем мастер-ключ в TPM с политикой PCR
        tpm_->seal(name, master_key.vector());
        
        // Ключ будет автоматически затёрт в деструкторе SecureBuffer
        
    } catch (const VaultError& e) {
        // Cleanup при ошибке
        if (luks_->is_open(mapper_name)) {
            try { luks_->close(mapper_name); } catch (...) {}
        }
        if (!loop_device.empty()) {
            try { loop_->detach(loop_device); } catch (...) {}
        }
        if (file_exists(image_path)) {
            std::remove(image_path.c_str());
        }
        throw;
    }
}

void TpmVault::open(const std::string& name) {
    std::string image_path = get_image_path(name);
    std::string mount_path = get_mount_path(name);
    std::string mapper_name = LuksManager::get_mapper_name(name);
    std::string mapper_path = LuksManager::get_mapper_path(mapper_name);
    
    // Проверяем наличие файла образа
    if (!file_exists(image_path)) {
        throw VaultError(name + ".img not found in current directory");
    }
    
    // Проверяем, не открыто ли уже
    if (luks_->is_open(mapper_name)) {
        throw VaultError(name + " is already open");
    }
    
    // 1. Извлекаем мастер-ключ из TPM
    SecureBuffer master_key(KEY_SIZE);
    {
        auto unsealed = tpm_->unseal(name);
        if (unsealed.size() != KEY_SIZE) {
            secure_erase(unsealed);
            throw VaultError("Invalid key size from TPM");
        }
        std::memcpy(master_key.data(), unsealed.data(), KEY_SIZE);
        secure_erase(unsealed);
    }
    
    std::string loop_device;
    
    try {
        // 2. Подключаем образ как loop-устройство
        loop_device = loop_->attach(image_path);
        
        // 3. Открываем LUKS-контейнер
        luks_->open(loop_device, mapper_name, master_key.vector());
        
        // 4. Монтируем файловую систему
        mount_filesystem(mapper_path, mount_path);
        
    } catch (const VaultError& e) {
        // Cleanup при ошибке
        if (luks_->is_open(mapper_name)) {
            try { luks_->close(mapper_name); } catch (...) {}
        }
        if (!loop_device.empty()) {
            try { loop_->detach(loop_device); } catch (...) {}
        }
        throw;
    }
}

void TpmVault::close(const std::string& name) {
    std::string image_path = get_image_path(name);
    std::string mount_path = get_mount_path(name);
    std::string mapper_name = LuksManager::get_mapper_name(name);

    std::exception_ptr first_error;

    // 1. Размонтируем файловую систему
    try {
        unmount_filesystem(mount_path);
    } catch (...) {
        if (!first_error) first_error = std::current_exception();
    }

    // 2. Закрываем LUKS-устройство
    try {
        luks_->close(mapper_name);
    } catch (...) {
        if (!first_error) first_error = std::current_exception();
    }

    // 3. Отключаем loop-устройство
    try {
        std::string loop_device = loop_->find_loop_for_file(image_path);
        if (!loop_device.empty()) {
            loop_->detach(loop_device);
        }
    } catch (...) {
        if (!first_error) first_error = std::current_exception();
    }

    // Перебрасываем первую ошибку после попытки очистить всё
    if (first_error) {
        std::rethrow_exception(first_error);
    }
}

std::vector<VaultInfo> TpmVault::list() {
    std::vector<VaultInfo> result;
    std::string cwd = get_current_directory();
    
    // Получаем список всех loop-устройств
    auto loops = loop_->list_attached();
    
    for (const auto& [loop_dev, backing_file] : loops) {
        // Проверяем, что файл в текущей директории и имеет расширение .img
        if (backing_file.find(cwd) != 0) continue;
        if (backing_file.size() <= 4) continue;
        if (backing_file.substr(backing_file.size() - 4) != ".img") continue;
        
        // Извлекаем имя хранилища
        std::string filename = backing_file.substr(cwd.size() + 1);
        std::string name = filename.substr(0, filename.size() - 4);
        
        std::string mapper_name = LuksManager::get_mapper_name(name);
        std::string mapper_path = LuksManager::get_mapper_path(mapper_name);
        std::string mount_path = get_mount_path(name);
        
        // Проверяем, что LUKS открыт и смонтирован
        if (luks_->is_open(mapper_name) && is_mounted(mount_path)) {
            VaultInfo info;
            info.name = name;
            info.image_path = backing_file;
            info.loop_device = loop_dev;
            info.mapper_device = mapper_path;
            info.mount_point = mount_path;
            result.push_back(info);
        }
    }
    
    return result;
}

void TpmVault::wipe(const std::string& name) {
    // Удаляем sealed object из TPM
    tpm_->remove(name);
}

} // namespace tpm_vault
