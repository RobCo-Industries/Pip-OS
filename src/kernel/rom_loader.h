#ifndef ROM_LOADER_H
#define ROM_LOADER_H

#include <stdint.h>
#include <stdbool.h>

// ROM Header Structure (as per spec)
#define ROM_MAGIC "ROM1"
#define ROM_MAGIC_SIZE 4
#define ROM_NAME_SIZE 16
#define ROM_VERSION_SIZE 8

typedef struct {
    char magic[ROM_MAGIC_SIZE];           // "ROM1"
    char name[ROM_NAME_SIZE];             // e.g., "DEITRIX"
    char version[ROM_VERSION_SIZE];       // e.g., "303"
    uint32_t load_address;                // Where to load in memory
    uint32_t entry_point;                 // Where to start execution
    uint32_t size;                        // Size of ROM in bytes
    uint32_t checksum;                    // CRC32 checksum
} __attribute__((packed)) rom_header_t;

// ROM detection and loading
bool rom_detect(void);
bool rom_verify(const rom_header_t* header);
bool rom_load(void);
void rom_chainload(uint32_t entry_point);

// Holotape support
#define HOLOTAPE_MAGIC "ROBCO78"
#define HOLOTAPE_MAGIC_SIZE 8
#define HOLOTAPE_TITLE_SIZE 64

typedef enum {
    HOLOTAPE_TYPE_GAME = 0,
    HOLOTAPE_TYPE_UTILITY = 1,
    HOLOTAPE_TYPE_DATA = 2
} holotape_type_t;

typedef struct {
    char magic[HOLOTAPE_MAGIC_SIZE];      // "ROBCO78"
    char title[HOLOTAPE_TITLE_SIZE];      // Game/program name
    holotape_type_t type;                 // Type of holotape
    uint32_t version;                     // Program version
    uint32_t load_address;                // Memory location
    uint32_t entry_point;                 // Start address
    uint32_t size;                        // Program size
    uint8_t icon[128];                    // 32x32 monochrome (128 bytes)
    // Payload follows
} __attribute__((packed)) holotape_header_t;

bool holotape_detect(void);
bool holotape_load(void);

#endif // ROM_LOADER_H
