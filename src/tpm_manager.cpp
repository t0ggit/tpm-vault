#include "tpm_manager.hpp"
#include "utils.hpp"

#include <tss2/tss2_fapi.h>
#include <tss2/tss2_rc.h>

#include <cstring>
#include <sstream>

namespace tpm_vault {

// Политика PCR для sha256:0,7
// Использует currentPCRandBanks для захвата текущих значений PCR при создании
const char* TpmManager::PCR_POLICY_JSON = 
    "{"
    "\"description\":\"PCR policy for tpm-vault (sha256:0,7)\","
    "\"policy\":["
        "{"
            "\"type\":\"POLICYPCR\","
            "\"currentPCRandBanks\":["
                "{"
                    "\"hash\":\"TPM2_ALG_SHA256\","
                    "\"pcrSelect\":[0,7]"
                "}"
            "]"
        "}"
    "]"
    "}";

const char* TpmManager::POLICY_PATH = "/policy/tpm_vault_pcr";

TpmManager::TpmManager() : ctx_(nullptr), policy_imported_(false) {
    TSS2_RC rc = Fapi_Initialize(&ctx_, nullptr);
    if (rc != TSS2_RC_SUCCESS) {
        std::ostringstream oss;
        oss << "Failed to initialize FAPI context: " << Tss2_RC_Decode(rc)
            << " (0x" << std::hex << rc << ")";
        throw VaultError(oss.str());
    }
}

TpmManager::~TpmManager() {
    if (ctx_) {
        Fapi_Finalize(&ctx_);
    }
}

void TpmManager::provision() {
    TSS2_RC rc = Fapi_Provision(ctx_, nullptr, nullptr, nullptr);
    
    if (rc == TSS2_FAPI_RC_ALREADY_PROVISIONED) {
        // TPM уже provisioned - это нормально
        return;
    }
    
    if (rc != TSS2_RC_SUCCESS) {
        std::ostringstream oss;
        oss << "Failed to provision TPM: " << Tss2_RC_Decode(rc)
            << " (0x" << std::hex << rc << ")";
        throw VaultError(oss.str());
    }
}

void TpmManager::ensure_pcr_policy() {
    if (policy_imported_) {
        return;
    }
    
    // Пробуем импортировать политику
    TSS2_RC rc = Fapi_Import(ctx_, POLICY_PATH, PCR_POLICY_JSON);
    
    if (rc == TSS2_FAPI_RC_PATH_ALREADY_EXISTS) {
        // Политика уже существует - отлично
        policy_imported_ = true;
        return;
    }
    
    if (rc != TSS2_RC_SUCCESS) {
        std::ostringstream oss;
        oss << "Failed to import PCR policy: " << Tss2_RC_Decode(rc)
            << " (0x" << std::hex << rc << ")";
        throw VaultError(oss.str());
    }
    
    policy_imported_ = true;
}

std::string TpmManager::get_seal_path(const std::string& name) const {
    return "/HS/SRK/seal_" + name;
}

std::string TpmManager::get_policy_path() const {
    return POLICY_PATH;
}

void TpmManager::seal(const std::string& name, const std::vector<uint8_t>& data) {
    if (data.size() > 128) {
        throw VaultError("Data too large to seal (max 128 bytes, got " + 
                        std::to_string(data.size()) + ")");
    }
    
    // Убеждаемся, что политика PCR импортирована
    ensure_pcr_policy();
    
    std::string path = get_seal_path(name);
    
    // Удаляем существующий объект если есть
    Fapi_Delete(ctx_, path.c_str());
    
    // Создаём sealed object с политикой PCR
    // type = "noDa" отключает защиту от dictionary attack (для тестирования)
    TSS2_RC rc = Fapi_CreateSeal(
        ctx_,
        path.c_str(),           // path
        "noDa",                 // type
        data.size(),            // size - размер данных в байтах
        POLICY_PATH,            // policyPath - политика PCR
        nullptr,                // authValue (пароль не используем)
        data.data()             // data
    );
    
    if (rc != TSS2_RC_SUCCESS) {
        std::ostringstream oss;
        oss << "Failed to seal data in TPM: " << Tss2_RC_Decode(rc)
            << " (0x" << std::hex << rc << ")";
        throw VaultError(oss.str());
    }
}

std::vector<uint8_t> TpmManager::unseal(const std::string& name) {
    std::string path = get_seal_path(name);
    
    uint8_t* data = nullptr;
    size_t size = 0;
    
    TSS2_RC rc = Fapi_Unseal(ctx_, path.c_str(), &data, &size);
    
    if (rc != TSS2_RC_SUCCESS) {
        // Проверяем специфические ошибки
        if (rc == TSS2_FAPI_RC_AUTHORIZATION_FAILED || 
            rc == TSS2_FAPI_RC_POLICY_UNKNOWN) {
            throw VaultError("TPM unseal failed — PCR values have changed");
        }
        if (rc == TSS2_FAPI_RC_KEY_NOT_FOUND || 
            rc == TSS2_FAPI_RC_PATH_NOT_FOUND) {
            throw VaultError("No TPM sealed object found for " + name);
        }
        
        std::ostringstream oss;
        oss << "Failed to unseal data from TPM: " << Tss2_RC_Decode(rc)
            << " (0x" << std::hex << rc << ")";
        throw VaultError(oss.str());
    }
    
    // Копируем данные в вектор
    std::vector<uint8_t> result(data, data + size);

    // Освобождаем память FAPI
    // (данные скопированы и будут безопасно затёрты в SecureBuffer)
    Fapi_Free(data);

    return result;
}

void TpmManager::remove(const std::string& name) {
    std::string path = get_seal_path(name);
    
    TSS2_RC rc = Fapi_Delete(ctx_, path.c_str());
    
    if (rc == TSS2_FAPI_RC_KEY_NOT_FOUND || 
        rc == TSS2_FAPI_RC_PATH_NOT_FOUND) {
        throw VaultError("No TPM sealed object found for " + name);
    }
    
    if (rc != TSS2_RC_SUCCESS) {
        std::ostringstream oss;
        oss << "Failed to delete sealed object: " << Tss2_RC_Decode(rc)
            << " (0x" << std::hex << rc << ")";
        throw VaultError(oss.str());
    }
}

bool TpmManager::exists(const std::string& name) {
    std::string path = get_seal_path(name);
    
    // Пробуем получить информацию об объекте
    char* info = nullptr;
    TSS2_RC rc = Fapi_GetInfo(ctx_, &info);
    
    if (info) {
        Fapi_Free(info);
    }
    
    // Более надёжный способ - попробовать прочитать путь
    // Но это может быть дорого, поэтому используем List
    char* pathList = nullptr;
    rc = Fapi_List(ctx_, "/HS/SRK", &pathList);
    
    if (rc != TSS2_RC_SUCCESS || !pathList) {
        return false;
    }
    
    std::string paths(pathList);
    Fapi_Free(pathList);
    
    // Ищем наш путь в списке
    std::string search_path = get_seal_path(name);
    return paths.find(search_path) != std::string::npos;
}

} // namespace tpm_vault
