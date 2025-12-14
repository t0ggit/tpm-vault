#ifndef TPM_VAULT_LOOP_MANAGER_HPP
#define TPM_VAULT_LOOP_MANAGER_HPP

#include <string>
#include <vector>

namespace tpm_vault {

/**
 * @brief Менеджер для работы с loop-устройствами
 * 
 * Использует утилиту losetup для подключения/отключения
 * файлов-образов как блочных устройств.
 */
class LoopManager {
public:
    /**
     * @brief Конструктор
     */
    LoopManager() = default;
    
    /**
     * @brief Подключает файл как loop-устройство
     * @param image_path Путь к файлу образа
     * @return Путь к созданному loop-устройству (например, /dev/loop0)
     * @throws VaultError при ошибке подключения
     */
    std::string attach(const std::string& image_path);
    
    /**
     * @brief Отключает loop-устройство
     * @param loop_device Путь к loop-устройству
     * @throws VaultError при ошибке отключения
     */
    void detach(const std::string& loop_device);
    
    /**
     * @brief Находит loop-устройство для файла
     * @param image_path Путь к файлу образа
     * @return Путь к loop-устройству или пустая строка если не найдено
     */
    std::string find_loop_for_file(const std::string& image_path);
    
    /**
     * @brief Получает список всех подключённых loop-устройств
     * @return Вектор пар (loop-устройство, файл)
     */
    std::vector<std::pair<std::string, std::string>> list_attached();
};

} // namespace tpm_vault

#endif // TPM_VAULT_LOOP_MANAGER_HPP
