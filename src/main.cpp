#include "tpm_vault.hpp"
#include "utils.hpp"

#include <iostream>
#include <iomanip>
#include <cstring>

using namespace tpm_vault;

void print_usage(const char* program_name) {
    std::cerr << "Usage: " << program_name << " <command> [arguments]\n"
              << "\n"
              << "Commands:\n"
              << "  create <name> [size]  Create a new encrypted vault\n"
              << "                        size: default 100M (supports M, G suffixes)\n"
              << "  open <name>           Open and mount an existing vault\n"
              << "  close <name>          Unmount and close a vault\n"
              << "  list                  List open vaults in current directory\n"
              << "  wipe <name>           Remove TPM sealed object (vault becomes inaccessible)\n"
              << "\n"
              << "Examples:\n"
              << "  " << program_name << " create secrets\n"
              << "  " << program_name << " create backup 1G\n"
              << "  " << program_name << " open secrets\n"
              << "  " << program_name << " close secrets\n"
              << "  " << program_name << " list\n"
              << "  " << program_name << " wipe secrets\n";
}

int cmd_create(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Error: Missing vault name\n";
        std::cerr << "Usage: " << argv[0] << " create <name> [size]\n";
        return 1;
    }
    
    std::string name = argv[2];
    size_t size = TpmVault::DEFAULT_SIZE;
    
    if (argc >= 4) {
        try {
            size = parse_size(argv[3]);
        } catch (const VaultError& e) {
            std::cerr << "Error: " << e.what() << "\n";
            return 1;
        }
    }
    
    try {
        TpmVault vault;
        
        std::cout << "Creating vault '" << name << "' (" << format_size(size) << ")...\n";
        vault.create(name, size);
        
        std::cout << "Vault '" << name << "' created successfully.\n";
        std::cout << "  Image: " << name << ".img\n";
        std::cout << "  Key sealed in TPM with PCR policy (sha256:0,7)\n";
        std::cout << "\nTo use: " << argv[0] << " open " << name << "\n";
        
        return 0;
        
    } catch (const VaultError& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}

int cmd_open(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Error: Missing vault name\n";
        std::cerr << "Usage: " << argv[0] << " open <name>\n";
        return 1;
    }
    
    std::string name = argv[2];
    
    try {
        TpmVault vault;
        
        std::cout << "Opening vault '" << name << "'...\n";
        vault.open(name);
        
        std::cout << "Vault '" << name << "' opened and mounted at ./" << name << "\n";
        
        return 0;
        
    } catch (const VaultError& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}

int cmd_close(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Error: Missing vault name\n";
        std::cerr << "Usage: " << argv[0] << " close <name>\n";
        return 1;
    }
    
    std::string name = argv[2];
    
    try {
        TpmVault vault;
        
        std::cout << "Closing vault '" << name << "'...\n";
        vault.close(name);
        
        std::cout << "Vault '" << name << "' closed.\n";
        
        return 0;
        
    } catch (const VaultError& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}

int cmd_list(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    
    try {
        TpmVault vault;
        auto vaults = vault.list();
        
        if (vaults.empty()) {
            std::cout << "No open vaults in current directory.\n";
            return 0;
        }
        
        std::cout << "Open vaults:\n\n";
        
        for (const auto& v : vaults) {
            std::cout << "  " << v.name << "\n";
            std::cout << "    Image:       " << v.image_path << "\n";
            std::cout << "    Loop device: " << v.loop_device << "\n";
            std::cout << "    Mapper:      " << v.mapper_device << "\n";
            std::cout << "    Mount point: " << v.mount_point << "\n";
            std::cout << "\n";
        }
        
        return 0;
        
    } catch (const VaultError& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}

int cmd_wipe(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Error: Missing vault name\n";
        std::cerr << "Usage: " << argv[0] << " wipe <name>\n";
        return 1;
    }
    
    std::string name = argv[2];
    
    // Предупреждение
    std::cout << "WARNING: This will delete the TPM sealed object for '" << name << "'.\n";
    std::cout << "         The vault image will remain but become inaccessible.\n";
    std::cout << "         This operation cannot be undone!\n";
    std::cout << "\nType 'yes' to confirm: ";
    
    std::string confirmation;
    std::getline(std::cin, confirmation);
    
    if (confirmation != "yes") {
        std::cout << "Operation cancelled.\n";
        return 0;
    }
    
    try {
        TpmVault vault;
        
        std::cout << "Wiping TPM sealed object for '" << name << "'...\n";
        vault.wipe(name);
        
        std::cout << "TPM sealed object for '" << name << "' has been deleted.\n";
        std::cout << "The vault is now permanently inaccessible.\n";
        
        return 0;
        
    } catch (const VaultError& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }
    
    std::string command = argv[1];
    
    if (command == "create") {
        return cmd_create(argc, argv);
    } else if (command == "open") {
        return cmd_open(argc, argv);
    } else if (command == "close") {
        return cmd_close(argc, argv);
    } else if (command == "list") {
        return cmd_list(argc, argv);
    } else if (command == "wipe") {
        return cmd_wipe(argc, argv);
    } else if (command == "-h" || command == "--help" || command == "help") {
        print_usage(argv[0]);
        return 0;
    } else {
        std::cerr << "Error: Unknown command '" << command << "'\n\n";
        print_usage(argv[0]);
        return 1;
    }
}
