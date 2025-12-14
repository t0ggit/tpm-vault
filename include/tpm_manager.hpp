#ifndef TPM_VAULT_TPM_MANAGER_HPP
#define TPM_VAULT_TPM_MANAGER_HPP

#include <string>
#include <vector>
#include <cstdint>
#include <memory>

// Forward declaration для FAPI контекста
struct FAPI_CONTEXT;

namespace tpm_vault {

/**
 * @brief Менеджер для работы с TPM2 через FAPI
 * 
 * Использует исключительно Feature API (FAPI) из tpm2-tss.
 * Поддерживает seal/unseal операции с политикой PCR.
 */
class TpmManager {
public:
    /**
     * @brief Конструктор - инициализирует FAPI контекст
     * @throws VaultError при ошибке инициализации
     */
    TpmManager();
    
    /**
     * @brief Деструктор - освобождает FAPI контекст
     */
    ~TpmManager();
    
    // Запрещаем копирование
    TpmManager(const TpmManager&) = delete;
    TpmManager& operator=(const TpmManager&) = delete;
    
    /**
     * @brief Выполняет provisioning TPM (создание иерархии)
     * @throws VaultError при ошибке (кроме "уже provisioned")
     */
    void provision();
    
    /**
     * @brief Запечатывает данные в TPM с политикой PCR
     * @param name Имя хранилища (используется для формирования пути)
     * @param data Данные для запечатывания (максимум 128 байт)
     * @throws VaultError при ошибке запечатывания
     */
    void seal(const std::string& name, const std::vector<uint8_t>& data);
    
    /**
     * @brief Извлекает запечатанные данные из TPM
     * @param name Имя хранилища
     * @return Извлечённые данные
     * @throws VaultError при ошибке (например, PCR изменились)
     */
    std::vector<uint8_t> unseal(const std::string& name);
    
    /**
     * @brief Удаляет sealed object из TPM
     * @param name Имя хранилища
     * @throws VaultError при ошибке удаления
     */
    void remove(const std::string& name);
    
    /**
     * @brief Проверяет существование sealed object
     * @param name Имя хранилища
     * @return true если объект существует
     */
    bool exists(const std::string& name);

private:
    /**
     * @brief Формирует путь к sealed object в FAPI keystore
     * @param name Имя хранилища
     * @return Путь вида "/HS/SRK/seal_<name>"
     */
    std::string get_seal_path(const std::string& name) const;
    
    /**
     * @brief Путь к политике PCR
     */
    std::string get_policy_path() const;
    
    /**
     * @brief Импортирует политику PCR если ещё не импортирована
     */
    void ensure_pcr_policy();
    
    FAPI_CONTEXT* ctx_;
    bool policy_imported_;
    
    // PCR policy JSON для sha256:0,7
    static const char* PCR_POLICY_JSON;
    static const char* POLICY_PATH;
};

} // namespace tpm_vault

#endif // TPM_VAULT_TPM_MANAGER_HPP
