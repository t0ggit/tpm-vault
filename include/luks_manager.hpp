#ifndef TPM_VAULT_LUKS_MANAGER_HPP
#define TPM_VAULT_LUKS_MANAGER_HPP

#include <string>
#include <vector>
#include <cstdint>

namespace tpm_vault {

/**
 * @brief Менеджер для работы с LUKS2 контейнерами
 * 
 * Выполняет операции через внешние команды cryptsetup.
 * Ключ передаётся через stdin для безопасности.
 */
class LuksManager {
public:
    /**
     * @brief Конструктор
     */
    LuksManager() = default;
    
    /**
     * @brief Форматирует устройство как LUKS2
     * @param device Путь к устройству (например, /dev/loop0)
     * @param key Ключ шифрования (512 бит / 64 байта)
     * @throws VaultError при ошибке форматирования
     */
    void format(const std::string& device, const std::vector<uint8_t>& key);
    
    /**
     * @brief Открывает LUKS контейнер
     * @param device Путь к устройству
     * @param mapper_name Имя для device mapper (без /dev/mapper/)
     * @param key Ключ шифрования
     * @throws VaultError при ошибке открытия
     */
    void open(const std::string& device, const std::string& mapper_name, 
              const std::vector<uint8_t>& key);
    
    /**
     * @brief Закрывает LUKS контейнер
     * @param mapper_name Имя device mapper
     * @throws VaultError при ошибке закрытия
     */
    void close(const std::string& mapper_name);
    
    /**
     * @brief Проверяет, открыт ли контейнер
     * @param mapper_name Имя device mapper
     * @return true если контейнер открыт
     */
    bool is_open(const std::string& mapper_name);
    
    /**
     * @brief Возвращает путь к mapper устройству
     * @param mapper_name Имя device mapper
     * @return Полный путь /dev/mapper/<mapper_name>
     */
    static std::string get_mapper_path(const std::string& mapper_name);
    
    /**
     * @brief Формирует имя mapper для хранилища
     * @param vault_name Имя хранилища
     * @return Имя вида "tpm-vault-<name>"
     */
    static std::string get_mapper_name(const std::string& vault_name);
};

} // namespace tpm_vault

#endif // TPM_VAULT_LUKS_MANAGER_HPP
