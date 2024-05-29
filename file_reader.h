#ifndef FAT16READER_FILE_READER_H
#define FAT16READER_FILE_READER_H

struct disk_t {
    FILE *disk;
};

struct boot_sector_t {
    uint8_t jump_code[3];
    uint64_t oem_name;
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t fats_number;
    uint16_t root_dir_capacity;
    uint16_t small_number_of_sectors;
    uint8_t media_type;
    uint16_t sectors_per_fat;
    uint16_t sectors_per_track;
    uint16_t number_of_heads;
    uint32_t hidden_sectors;
    uint32_t large_number_of_sectors;
    uint8_t drive_number;
    uint8_t check_disk_integrity;
    uint8_t extended_boot_signature;
    uint32_t volume_serial_number;
    uint8_t volume_label[11];
    uint64_t file_system_type;
    uint8_t bootstrap_code[448];
    uint16_t signature;
} __attribute((__packed__));

union my_time_t {
    uint16_t time;
    struct {
        uint16_t hour :5;
        uint16_t min :6;
        uint16_t sec :5;
    };
};

union date_t {
    uint16_t date;
    struct {
        uint16_t year :7;
        uint16_t month :5;
        uint16_t day :4;
    };
};

struct SFN {
    char filename[8];
    char ext[3];
    uint8_t file_attributes;
    uint8_t reserved;
    uint8_t file_creation_time;
    union my_time_t creation_time;
    union date_t creation_date;
    uint16_t access_date;
    uint16_t high_order_address_of_first_cluster;
    union my_time_t modified_time;
    union date_t modified_date;
    uint16_t low_order_address_of_first_cluster;
    uint32_t size;
} __attribute((__packed__));

struct file_t {
    struct SFN handle;
    uint8_t *data;
    int32_t current_byte;
};

struct dir_t {
    struct SFN *dir;
    uint16_t number_of_entries;
    uint16_t current_entry;
};

struct dir_entry_t {
    char name[13];
    size_t size;
    uint8_t is_archived;
    uint8_t is_readonly;
    uint8_t is_system;
    uint8_t is_hidden;
    uint8_t is_directory;
};

struct volume_t {
    struct boot_sector_t boot_sector;
    uint8_t *primary_fat;
    uint8_t *secondary_fat;
    uint8_t *root_directory;
    uint8_t *data_area;
};

struct disk_t* disk_open_from_file(const char* volume_file_name);
int disk_read(struct disk_t* pdisk, int32_t first_sector, void* buffer, int32_t sectors_to_read);
int disk_close(struct disk_t* pdisk);

struct volume_t* fat_open(struct disk_t* pdisk, uint32_t first_sector);
int fat_close(struct volume_t* pvolume);

struct file_t* file_open(struct volume_t* pvolume, const char* file_name);
int file_close(struct file_t* stream);
size_t file_read(void *ptr, size_t size, size_t nmemb, struct file_t *stream);
int32_t file_seek(struct file_t* stream, int32_t offset, int whence);

struct dir_t* dir_open(struct volume_t* pvolume, const char* dir_path);
int dir_read(struct dir_t* pdir, struct dir_entry_t* pentry);
int dir_close(struct dir_t* pdir);

#endif
