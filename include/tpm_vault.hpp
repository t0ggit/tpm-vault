#ifndef TPM_VAULT_HPP
#define TPM_VAULT_HPP

#include <string>
#include <memory>
#include <vector>

namespace tpm_vault {

// Forward declarations
class TpmManager;
class LuksManager;
class LoopManager;

/**
 * @brief Информация об открытом хранилище
 */
struct VaultInfo {
    std::string name;           ///< Имя хранилища
    std::string image_path;     ///< Путь к файлу образа
    std::string loop_device;    ///< Loop-устройство
    std::string mapper_device;  ///< Device mapper устройство
    std::string mount_point;    ///< Точка монтирования
};

/**
 * @brief Основной класс приложения tpm-vault
 * 
 * Координирует работу TPM, LUKS и loop-устройств
 * для создания и управления зашифрованными хранилищами.
 */
class TpmVault {
public:
    /// Размер образа по умолчанию (100 МБ)
    static constexpr size_t DEFAULT_SIZE = 100 * 1024 * 1024;
    
    /// Размер ключа шифрования (512 бит = 64 байта)
    static constexpr size_t KEY_SIZE = 64;
    
    /**
     * @brief Конструктор
     * @throws VaultError при ошибке инициализации TPM
     */
    TpmVault();
    
    /**
     * @brief Деструктор
     */
    ~TpmVault();
    
    /**
     * @brief Создаёт новое зашифрованное хранилище
     * @param name Имя хранилища (без расширения)
     * @param size Размер образа в байтах
     * @throws VaultError при ошибке создания
     */
    void create(const std::string& name, size_t size = DEFAULT_SIZE);
    
    /**
     * @brief Открывает существующее хранилище
     * @param name Имя хранилища
     * @throws VaultError при ошибке открытия
     */
    void open(const std::string& name);
    
    /**
     * @brief Закрывает хранилище
     * @param name Имя хранилища
     * @throws VaultError при ошибке закрытия
     */
    void close(const std::string& name);
    
    /**
     * @brief Возвращает список открытых хранилищ
     * @return Вектор информации о хранилищах
     */
    std::vector<VaultInfo> list();
    
    /**
     * @brief Удаляет sealed object из TPM
     * @param name Имя хранилища
     * @throws VaultError при ошибке
     * 
     * @note Файл образа не удаляется. После wipe открыть
     *       существующий образ будет невозможно.
     */
    void wipe(const std::string& name);

private:
    /**
     * @brief Возвращает путь к файлу образа
     * @param name Имя хранилища
     * @return Путь вида "./<n>.img"
     */
    std::string get_image_path(const std::string& name) const;
    
    /**
     * @brief Возвращает путь к точке монтирования
     * @param name Имя хранилища
     * @return Путь вида "./<n>"
     */
    std::string get_mount_path(const std::string& name) const;
    
    /**
     * @brief Создаёт файл образа указанного размера
     * @param path Путь к файлу
     * @param size Размер в байтах
     */
    void create_image_file(const std::string& path, size_t size);
    
    /**
     * @brief Создаёт файловую систему ext4
     * @param device Путь к устройству
     */
    void create_filesystem(const std::string& device);
    
    /**
     * @brief Монтирует файловую систему
     * @param device Путь к устройству
     * @param mount_point Точка монтирования
     */
    void mount_filesystem(const std::string& device, const std::string& mount_point);
    
    /**
     * @brief Размонтирует файловую систему
     * @param mount_point Точка монтирования
     */
    void unmount_filesystem(const std::string& mount_point);
    
    /**
     * @brief Проверяет, смонтирована ли точка
     * @param mount_point Путь к точке монтирования
     * @return true если смонтирована
     */
    bool is_mounted(const std::string& mount_point);
    
    std::unique_ptr<TpmManager> tpm_;
    std::unique_ptr<LuksManager> luks_;
    std::unique_ptr<LoopManager> loop_;
};

} // namespace tpm_vault

#endif // TPM_VAULT_HPP
