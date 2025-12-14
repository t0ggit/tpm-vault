# tpm-vault: Handbook

Подробное руководство по разработке и тестированию приложения tpm-vault — инструмента для создания зашифрованных дисков с ключами, запечатанными в TPM2.

## Содержание

1. [Обзор проекта](#обзор-проекта)
2. [Теоретическая база](#теоретическая-база)
3. [Окружение разработки](#окружение-разработки)
4. [Архитектура приложения](#архитектура-приложения)
5. [Работа с TPM2 через FAPI](#работа-с-tpm2-через-fapi)
6. [Настройка QEMU с swtpm](#настройка-qemu-с-swtpm)
7. [Сборка и тестирование](#сборка-и-тестирование)
8. [Возможные проблемы и решения](#возможные-проблемы-и-решения)
9. [Ссылки и материалы](#ссылки-и-материалы)

---

## Обзор проекта

### Цель

Создать инструмент для безопасного хранения данных в зашифрованных образах дисков, где ключ шифрования:
- Генерируется случайным образом (512 бит)
- Запечатывается (seal) в TPM2 с привязкой к PCR
- Извлекается (unseal) только при неизменных значениях PCR

### Сценарий использования

```
┌─────────────┐     ┌─────────────┐     ┌─────────────┐
│  tpm-vault  │────▶│    TPM2     │────▶│  LUKS2      │
│   create    │     │   seal()    │     │  format()   │
└─────────────┘     └─────────────┘     └─────────────┘
       │                   │                   │
       │           PCR 0,7 policy              │
       │                   │                   │
       ▼                   ▼                   ▼
┌─────────────┐     ┌─────────────┐     ┌─────────────┐
│  tpm-vault  │────▶│    TPM2     │────▶│  LUKS2      │
│    open     │     │  unseal()   │     │   open()    │
└─────────────┘     └─────────────┘     └─────────────┘
```

---

## Теоретическая база

### TPM2 (Trusted Platform Module)

TPM — это аппаратный криптопроцессор, обеспечивающий:
- Генерацию и хранение криптографических ключей
- Измерение состояния системы (PCR)
- Запечатывание данных с привязкой к состоянию системы

### PCR (Platform Configuration Registers)

PCR — это регистры TPM, содержащие хеши (измерения) компонентов системы:

| PCR | Содержимое |
|-----|------------|
| 0 | UEFI firmware |
| 1 | UEFI конфигурация |
| 2 | Код загрузки платформы |
| 3 | Данные загрузки платформы |
| 4 | Boot manager |
| 5 | GPT / MBR |
| 6 | Resume events |
| 7 | Secure Boot state |

### Seal / Unseal

- **Seal** — запечатывание данных в TPM с привязкой к политике (например, PCR)
- **Unseal** — извлечение данных; успешно только если политика удовлетворена

### LUKS2 (Linux Unified Key Setup)

LUKS — стандарт шифрования дисков в Linux:
- Поддержка нескольких ключей (key slots)
- Современные алгоритмы (AES-XTS)
- Метаданные в заголовке тома

---

## Окружение разработки

### Версии ПО

| Компонент | Версия | Примечание |
|-----------|--------|------------|
| Ubuntu | 22.04 LTS | Хост и гостевая ОС |
| QEMU | 6.2+ | С поддержкой TPM |
| swtpm | 0.7+ | Программный TPM |
| tpm2-tss | 3.2+ | Библиотека TSS |
| cryptsetup | 2.4+ | LUKS2 support |
| CMake | 3.16+ | Система сборки |
| GCC | 11+ | C++17 support |

### Установка на Ubuntu 22.04

```bash
# Базовые инструменты
sudo apt update
sudo apt install -y build-essential cmake pkg-config git

# TPM2 библиотеки
sudo apt install -y libtss2-dev tpm2-tools

# LUKS и файловые системы
sudo apt install -y cryptsetup e2fsprogs

# Для виртуализации (на хосте)
sudo apt install -y qemu-system-x86 qemu-utils ovmf swtpm
```

---

## Архитектура приложения

### Классы

```
┌───────────────────────────────────────────────────────┐
│                     TpmVault                          │
│  - create(), open(), close(), list(), wipe()          │
├───────────────────────────────────────────────────────┤
│         │                │                │           │
│    TpmManager       LuksManager       LoopManager     │
│    - seal()         - format()        - attach()      │
│    - unseal()       - open()          - detach()      │
│    - remove()       - close()         - find_loop()   │
└───────────────────────────────────────────────────────┘
```

### Флоу при создании хранилища

```
1. generate_random_bytes(64)     → master_key
2. create_image_file(name.img)   → файл образа
3. loop_attach(name.img)         → /dev/loopX
4. luks_format(/dev/loopX, key)  → LUKS2 контейнер
5. luks_open()                   → /dev/mapper/tpm-vault-name
6. mkfs.ext4()                   → файловая система
7. luks_close()                  → закрываем временно
8. tpm_seal(name, key)           → ключ в TPM
9. secure_erase(key)             → затираем память
```

### Флоу при открытии

```
1. tpm_unseal(name)              → master_key из TPM
2. loop_attach(name.img)         → /dev/loopX
3. luks_open(/dev/loopX, key)    → /dev/mapper/tpm-vault-name
4. mount(mapper, ./name)         → точка монтирования
5. secure_erase(key)             → затираем память
```

---

## Работа с TPM2 через FAPI

### Почему FAPI?

TPM2 Software Stack (TSS) предоставляет несколько уровней API:

| API | Уровень | Сложность | Использование |
|-----|---------|-----------|---------------|
| SAPI | Низкий | Высокая | Прямой доступ к командам TPM |
| ESAPI | Средний | Средняя | С управлением сессиями |
| **FAPI** | Высокий | Низкая | **Рекомендуется для приложений** |

FAPI выбран потому что:
- Простой API (меньше кода)
- Автоматическое управление ресурсами
- Встроенная поддержка политик PCR
- JSON-конфигурация

### Инициализация FAPI

```cpp
#include <tss2/tss2_fapi.h>

FAPI_CONTEXT *ctx = nullptr;
TSS2_RC rc = Fapi_Initialize(&ctx, nullptr);  // uri = nullptr

// Provisioning (создание иерархии)
rc = Fapi_Provision(ctx, nullptr, nullptr, nullptr);
if (rc == TSS2_FAPI_RC_ALREADY_PROVISIONED) {
    // OK — уже инициализирован
}

// ... использование ...

Fapi_Finalize(&ctx);
```

### Политика PCR в JSON

```json
{
    "description": "PCR policy sha256:0,7",
    "policy": [
        {
            "type": "POLICYPCR",
            "currentPCRandBanks": [
                {
                    "hashAlg": "TPM2_ALG_SHA256",
                    "pcrSelect": [0, 7]
                }
            ]
        }
    ]
}
```

Ключевой момент: `currentPCRandBanks` захватывает **текущие** значения PCR в момент создания политики.

### Seal операция

```cpp
// Импорт политики
Fapi_Import(ctx, "/policy/pcr07", policy_json);

// Создание sealed object
Fapi_CreateSeal(
    ctx,
    "/HS/SRK/seal_mykey",  // путь в keystore
    "noDa",                 // тип (без dictionary attack protection)
    0,                      // size (0 = использовать data)
    "/policy/pcr07",        // путь к политике
    nullptr,                // пароль (не используем)
    data                    // данные для запечатывания
);
```

### Unseal операция

```cpp
uint8_t *data = nullptr;
size_t size = 0;

TSS2_RC rc = Fapi_Unseal(ctx, "/HS/SRK/seal_mykey", &data, &size);

if (rc == TSS2_FAPI_RC_AUTHORIZATION_FAILED) {
    // PCR изменились!
}

// Использование data...

Fapi_Free(data);  // Обязательно освободить!
```

### Ограничения FAPI

- **Максимум 128 байт** для sealed data (ограничение TPM2)
- Политика должна быть импортирована перед использованием
- Путь `/HS/SRK/...` — объекты под Storage Root Key

---

## Настройка QEMU с swtpm

### Подготовка OVMF (UEFI)

```bash
# Копируем OVMF_VARS для записи
cp /usr/share/OVMF/OVMF_VARS_4M.fd ./OVMF_VARS.fd
chmod +w OVMF_VARS.fd
```

### Создание диска

```bash
qemu-img create -f qcow2 disk.qcow2 20G
```

### Запуск swtpm

```bash
# Создаём директорию для состояния TPM
mkdir -p tpmstate

# Запуск swtpm daemon
swtpm socket \
    --tpmstate dir=$PWD/tpmstate \
    --ctrl type=unixio,path=$PWD/swtpm-sock,mode=0666 \
    --tpm2 \
    --log level=20 \
    --daemon

# Проверка
ls -la swtpm-sock
```

### Запуск QEMU

```bash
qemu-system-x86_64 \
    -enable-kvm \
    -cpu host \
    -smp 4 \
    -m 4G \
    -machine q35,smm=on \
    -global driver=cfi.pflash01,property=secure,value=on \
    -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
    -drive if=pflash,format=raw,file=$PWD/OVMF_VARS.fd \
    -drive file=disk.qcow2,format=qcow2,if=virtio \
    -chardev socket,id=chrtpm,path=$PWD/swtpm-sock \
    -tpmdev emulator,id=tpm0,chardev=chrtpm \
    -device tpm-crb,tpmdev=tpm0 \
    -nic user,model=virtio-net-pci,hostfwd=tcp::2222-:22 \
    -serial mon:stdio
```

### Проверка TPM в гостевой системе

```bash
# Проверка устройства
ls -la /dev/tpm*

# Вывод информации через tpm2-tools
tpm2_getcap properties-fixed

# Чтение PCR
tpm2_pcrread sha256:0,7
```

### Конфигурация FAPI в гостевой системе

Файл `/etc/tpm2-tss/fapi-config.json`:

```json
{
    "profile_name": "P_ECCP256SHA256",
    "profile_dir": "/etc/tpm2-tss/fapi-profiles/",
    "user_dir": "~/.local/share/tpm2-tss/user/keystore",
    "system_dir": "/var/lib/tpm2-tss/system/keystore",
    "tcti": "",
    "system_pcrs": [0, 1, 2, 3, 4, 5, 6, 7],
    "log_dir": "/run/tpm2-tss/eventlog/",
    "ek_cert_less": "yes"
}
```

> **Важно:** `"ek_cert_less": "yes"` необходим для swtpm, так как он не имеет сертификата EK.

---

## Сборка и тестирование

### Сборка

```bash
cd tpm-vault

# Создание директории сборки
mkdir build && cd build

# Конфигурация
cmake ..

# Сборка
make -j$(nproc)

# Результат
ls -la tpm-vault
```

### Тестирование (в VM с swtpm)

```bash
# Копируем бинарник в VM
scp -P 2222 tpm-vault user@localhost:~/

# В VM
ssh -p 2222 user@localhost

# Создание тестового хранилища
cd ~
sudo ./tpm-vault create test 50M

# Проверка файлов
ls -la test.img

# Открытие
sudo ./tpm-vault open test

# Запись тестовых данных
sudo sh -c 'echo "Secret data" > test/secret.txt'

# Список открытых хранилищ
sudo ./tpm-vault list

# Закрытие
sudo ./tpm-vault close test

# Повторное открытие
sudo ./tpm-vault open test
cat test/secret.txt  # Должны увидеть "Secret data"

# Закрытие и очистка
sudo ./tpm-vault close test
```

### Тест изменения PCR

```bash
# Создаём хранилище
sudo ./tpm-vault create pcr-test 50M

# Расширяем PCR 7 (имитация изменения Secure Boot)
sudo tpm2_pcrextend 7:sha256=0000000000000000000000000000000000000000000000000000000000000000

# Попытка открыть — должна завершиться ошибкой
sudo ./tpm-vault open pcr-test
# Error: TPM unseal failed — PCR values have changed
```

---

## Возможные проблемы и решения

### 1. "Failed to initialize FAPI context"

**Причина:** FAPI не может подключиться к TPM.

**Решение:**
```bash
# Проверить наличие /dev/tpm0
ls -la /dev/tpm*

# Проверить права
sudo chmod 666 /dev/tpm0

# Проверить конфигурацию FAPI
cat /etc/tpm2-tss/fapi-config.json
```

### 2. "Provision failed"

**Причина:** TPM не инициализирован или ошибка EK сертификата.

**Решение:**
```bash
# Добавить в fapi-config.json:
"ek_cert_less": "yes"

# Или выполнить provisioning вручную:
tss2_provision
```

### 3. "Data too large to seal"

**Причина:** Попытка запечатать более 128 байт.

**Решение:** Использовать ключ ≤128 байт или шифровать данные отдельно.

### 4. QEMU не видит TPM

**Причина:** Неправильные параметры или swtpm не запущен.

**Решение:**
```bash
# Проверить swtpm
ps aux | grep swtpm

# Проверить socket
ls -la swtpm-sock

# Перезапустить swtpm
pkill swtpm
swtpm socket --tpmstate dir=$PWD/tpmstate --ctrl type=unixio,path=$PWD/swtpm-sock --tpm2 --daemon
```

### 5. "This operation requires root privileges"

**Причина:** Команды losetup, cryptsetup, mount требуют root.

**Решение:** Использовать `sudo ./tpm-vault ...`

### 6. PCR всегда нулевые

**Причина:** swtpm стартует с пустыми PCR.

**Решение:** 
- Это нормально для тестирования
- Используйте `--flags not-need-init,startup-clear` при запуске swtpm
- В реальной системе PCR заполняются в процессе загрузки

---

## Ссылки и материалы

### Официальная документация

- [TPM2-TSS Documentation](https://tpm2-tss.readthedocs.io/)
- [TPM2-TSS GitHub](https://github.com/tpm2-software/tpm2-tss)
- [TCG FAPI Specification](https://trustedcomputinggroup.org/resource/tss-fapi/)
- [LUKS2 Format](https://gitlab.com/cryptsetup/cryptsetup/-/wikis/LUKS2-Format)

### Статьи и руководства

- [Arch Wiki: Trusted Platform Module](https://wiki.archlinux.org/title/Trusted_Platform_Module)
- [Ubuntu: TPM](https://ubuntu.com/server/docs/security-tpm)
- [QEMU TPM Device](https://www.qemu.org/docs/master/specs/tpm.html)

---

## Заключение

Проект tpm-vault демонстрирует практическое применение TPM2 для защиты ключей шифрования. Ключевые технологии:

1. **TPM2 FAPI** — высокоуровневый API для работы с TPM
2. **PCR политики** — привязка данных к состоянию системы
3. **LUKS2** — современное шифрование дисков
4. **swtpm** — программная эмуляция TPM для разработки
