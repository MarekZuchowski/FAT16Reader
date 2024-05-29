#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include "file_reader.h"

#define SECTOR_SIZE 512
#define ENTRY_SIZE 32
#define FILENAME_MAX_LENGTH 12

#define FAT_CHAIN_ENDING_VALUE 0xFFF8
#define SIGNATURE 0xAA55

#define ATTR_READONLY 0x01
#define ATTR_HIDDEN 0x02
#define ATTR_SYSTEM 0x04
#define ATTR_VOLUME_LABEL 0x08
#define ATTR_DIRECTORY 0x10
#define ATTR_ARCHIVE 0x20

struct disk_t* disk_open_from_file(const char* volume_file_name) {
    if(volume_file_name == NULL) {
        errno = EFAULT;
        return NULL;
    }

    FILE *file = fopen(volume_file_name, "rb");
    if(file == NULL) {
        errno = ENOENT;
        return NULL;
    }

    struct disk_t *disk;
    if((disk = malloc(sizeof(struct disk_t))) == NULL) {
        fclose(file);
        errno = ENOMEM;
        return NULL;
    }

    disk->disk = file;

    return disk;
}

int disk_read(struct disk_t* pdisk, int32_t first_sector, void* buffer, int32_t sectors_to_read) {
    if(pdisk == NULL || buffer == NULL) {
        errno = EFAULT;
        return -1;
    }

    fseek(pdisk->disk, SECTOR_SIZE * first_sector, SEEK_SET);
    int32_t result = fread(buffer, SECTOR_SIZE, sectors_to_read, pdisk->disk);
    if(result != sectors_to_read) {
        errno = ERANGE;
        return -1;
    }

    return result;
}

int disk_close(struct disk_t* pdisk) {
    if(pdisk == NULL) {
        errno = EFAULT;
        return -1;
    }

    fclose(pdisk->disk);
    free(pdisk);
    return 0;
}

struct volume_t* fat_open(struct disk_t* pdisk, uint32_t first_sector) {
    if(pdisk == NULL) {
        errno = EFAULT;
        return NULL;
    }

    struct volume_t *volume;
    if((volume = malloc(sizeof(struct volume_t))) == NULL) {
        errno = ENOMEM;
        return NULL;
    }

    if((disk_read(pdisk, first_sector, volume, 1) == -1)) {
        free(volume);
        errno = ERANGE;
        return NULL;
    }

    if(volume->boot_sector.signature != SIGNATURE) {
        free(volume);
        errno = EINVAL;
        return NULL;
    }

    int fat_size = volume->boot_sector.sectors_per_fat * volume->boot_sector.bytes_per_sector;
    if((volume->primary_fat = malloc(sizeof(uint8_t) * fat_size)) == NULL) {
        free(volume);
        errno = ENOMEM;
        return NULL;
    }
    if((volume->secondary_fat = malloc(sizeof(uint8_t) * fat_size)) == NULL) {
        free(volume);
        errno = ENOMEM;
        return NULL;
    }

    uint8_t fat_sector = volume->boot_sector.reserved_sectors;
    if(disk_read(pdisk, fat_sector, volume->primary_fat, volume->boot_sector.sectors_per_fat) == -1) {
        free(volume->primary_fat);
        free(volume->secondary_fat);
        free(volume);
        errno = ERANGE;
        return NULL;
    }

    fat_sector += volume->boot_sector.sectors_per_fat;
    if(disk_read(pdisk, fat_sector, volume->secondary_fat, volume->boot_sector.sectors_per_fat) == -1) {
        free(volume->primary_fat);
        free(volume->secondary_fat);
        free(volume);
        errno = ERANGE;
        return NULL;
    }

    if(memcmp(volume->primary_fat, volume->secondary_fat, fat_size) != 0) {
        free(volume->primary_fat);
        free(volume->secondary_fat);
        free(volume);
        errno = EINVAL;
        return NULL;
    }

    int root_directory_size = volume->boot_sector.root_dir_capacity * ENTRY_SIZE;
    if((volume->root_directory = malloc(sizeof(uint8_t) * root_directory_size)) == NULL) {
        free(volume->primary_fat);
        free(volume->secondary_fat);
        free(volume);
        errno = ENOMEM;
        return NULL;
    }

    int root_directory_sector = fat_sector + volume->boot_sector.sectors_per_fat;
    int root_directory_size_in_sectors = root_directory_size / volume->boot_sector.bytes_per_sector;
    if(disk_read(pdisk, root_directory_sector, volume->root_directory, root_directory_size_in_sectors) == -1) {
        free(volume->primary_fat);
        free(volume->secondary_fat);
        free(volume->root_directory);
        free(volume);
        errno = ERANGE;
        return NULL;
    }

    int data_area_sector = root_directory_sector + root_directory_size_in_sectors;
    int data_area_size_in_sectors = volume->boot_sector.small_number_of_sectors - data_area_sector;
    int data_area_size = data_area_size_in_sectors * volume->boot_sector.bytes_per_sector;
    if((volume->data_area = malloc(sizeof(uint8_t) * data_area_size)) == NULL) {
        free(volume->primary_fat);
        free(volume->secondary_fat);
        free(volume->root_directory);
        free(volume);
        errno = ENOMEM;
        return NULL;
    }

    if(disk_read(pdisk, data_area_sector, volume->data_area, data_area_size_in_sectors) == -1) {
        free(volume->primary_fat);
        free(volume->secondary_fat);
        free(volume->root_directory);
        free(volume->data_area);
        free(volume);
        errno = ENOMEM;
        return NULL;
    }

    return volume;
}

int fat_close(struct volume_t* pvolume) {
    if(pvolume == NULL) {
        errno = EFAULT;
        return -1;
    }

    free(pvolume->primary_fat);
    free(pvolume->secondary_fat);
    free(pvolume->root_directory);
    free(pvolume->data_area);
    free(pvolume);
    return 0;
}

struct file_t* file_open(struct volume_t* pvolume, const char* file_name) {
    if(pvolume == NULL || file_name == NULL) {
        errno = EFAULT;
        return NULL;
    }

    struct SFN *pfile = (struct SFN *) pvolume->root_directory;
    for(int i = 0; i < pvolume->boot_sector.root_dir_capacity; i++, pfile++) {
        if((*((uint8_t *) pfile->filename) == 0x00) || (*((uint8_t *) pfile->filename) == 0xe5))
            continue;

        char current_filename[FILENAME_MAX_LENGTH + 1] = {0};
        int j;
        for(j = 0; j < 8; ++j) {
            if(*(pfile->filename + j) == ' ')
                break;
            current_filename[j] = *((char *)pfile->filename + j);
        }

        for(int k = 0; k < 3; ++k) {
            if(*(pfile->ext + k) == ' ')
                break;
            if(k == 0)
                current_filename[j++] = '.';
            current_filename[j+k] = *(pfile->ext + k);
        }

        if(strcmp(file_name, current_filename) == 0) {
            if ((pfile->file_attributes & (uint8_t) ATTR_DIRECTORY) || (pfile->file_attributes & (uint8_t) ATTR_VOLUME_LABEL)) {
                errno = EISDIR;
                return NULL;
            }
            struct file_t *file = malloc(sizeof(struct file_t));
            if(file == NULL) {
                errno = ENOMEM;
                return NULL;
            }
            memcpy(&file->handle, pfile, sizeof(struct SFN));

            if((file->data = malloc(sizeof(uint8_t) * file->handle.size)) == NULL) {
                free(file);
                errno = ENOMEM;
                return NULL;
            }

            uint16_t bytes_left_to_read = pfile->size % (pvolume->boot_sector.sectors_per_cluster * pvolume->boot_sector.bytes_per_sector);
            uint16_t index = pfile->low_order_address_of_first_cluster;

            int k = 0;
            for(; *((uint16_t *) pvolume->primary_fat + index) < FAT_CHAIN_ENDING_VALUE ; ++k) {
                memcpy(file->data + (k * (pvolume->boot_sector.sectors_per_cluster * pvolume->boot_sector.bytes_per_sector)), pvolume->data_area + ((index - 2) * pvolume->boot_sector.sectors_per_cluster * pvolume->boot_sector.bytes_per_sector), pvolume->boot_sector.sectors_per_cluster * pvolume->boot_sector.bytes_per_sector);
                index = *((uint16_t *) pvolume->primary_fat + index);
            }

            if(bytes_left_to_read > 0) {
                memcpy(file->data + (k * (pvolume->boot_sector.sectors_per_cluster * pvolume->boot_sector.bytes_per_sector)), pvolume->data_area + ((index - 2) * pvolume->boot_sector.sectors_per_cluster * pvolume->boot_sector.bytes_per_sector), bytes_left_to_read);
            }
            file->current_byte = 0;

            return file;
        }
    }

    return NULL;
}

int file_close(struct file_t* stream) {
    if(stream == NULL) {
        errno = EFAULT;
        return -1;
    }

    free(stream->data);
    free(stream);
    return 0;
}

size_t file_read(void *ptr, size_t size, size_t nmemb, struct file_t *stream) {
    if(ptr == NULL || stream == NULL) {
        errno = EFAULT;
        return -1;
    }

    uint16_t bytes_left_to_read = stream->handle.size - stream->current_byte;
    if(bytes_left_to_read == 0) {
        return 0;
    }

    uint16_t bytes_to_read = size * nmemb;
    if(bytes_to_read <= bytes_left_to_read) {
        memcpy(ptr, stream->data + stream->current_byte, bytes_to_read);
        file_seek(stream, bytes_to_read, SEEK_CUR);
        return nmemb;
    }
    else {
        memcpy(ptr, stream->data + stream->current_byte, bytes_left_to_read);
        file_seek(stream, bytes_left_to_read, SEEK_CUR);
        return bytes_left_to_read / size;
    }
}

int32_t file_seek(struct file_t* stream, int32_t offset, int whence) {
    if(stream == NULL) {
        errno = EFAULT;
        return -1;
    }

    int32_t new_position;
    if((whence == SEEK_SET && offset < 0) || (whence == SEEK_END && offset > 0) || (whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END)) {
        errno = EINVAL;
        return -1;
    }

    if(whence == SEEK_SET) {
        if((uint32_t) offset > stream->handle.size) {
            errno = ENXIO;
            return -1;
        }
        new_position = offset;
    }
    else if(whence == SEEK_CUR) {
        if((offset + stream->current_byte) < 0 || (uint32_t)(offset + stream->current_byte) > stream->handle.size) {
            errno = ENXIO;
            return -1;
        }
        new_position = stream->current_byte + offset;
    }
    else {
        if((uint32_t) -(offset) > stream->handle.size) {
            errno = ENXIO;
            return -1;
        }
        new_position = stream->handle.size + offset;
    }

    stream->current_byte = new_position;
    return new_position;
}

struct dir_t* dir_open(struct volume_t* pvolume, const char* dir_path) {
    if(pvolume == NULL || dir_path == NULL) {
        errno = EFAULT;
        return NULL;
    }

    if(strcmp(dir_path, "\\") == 0) {
        struct dir_t *pdir;
        if((pdir = malloc(sizeof(struct dir_t))) == NULL) {
            errno = ENOMEM;
            return NULL;
        }

        pdir->dir = (struct SFN *) pvolume->root_directory;
        pdir->number_of_entries = pvolume->boot_sector.root_dir_capacity;
        pdir->current_entry = 0;

        return pdir;
    }

    errno = ENOENT;
    return NULL;
}

int dir_read(struct dir_t* pdir, struct dir_entry_t* pentry) {
    if(pdir == NULL || pentry == NULL) {
        errno = EFAULT;
        return -1;
    }

    if(pdir->current_entry == pdir->number_of_entries) {
        errno = ENXIO;
        return -1;
    }

    struct SFN *current_entry = ((struct SFN *) pdir->dir) + pdir->current_entry;
    for(; pdir->current_entry < pdir->number_of_entries; current_entry++, pdir->current_entry++) {
        if ((*((uint8_t *) current_entry->filename) == 0x00) || (*((uint8_t *) current_entry->filename) == 0xe5))
            continue;
        if(current_entry->file_attributes & (uint8_t) ATTR_VOLUME_LABEL)
            continue;

        char filename[FILENAME_MAX_LENGTH + 1] = {0};
        int j;
        for (j = 0; j < 8; ++j) {
            if (*(current_entry->filename + j) == ' ')
                break;
            filename[j] = *((char *) current_entry->filename + j);
        }

        int k;
        for (k = 0; k < 3; ++k) {
            if (*(current_entry->ext + k) == ' ')
                break;
            if (k == 0)
                filename[j++] = '.';
            filename[j + k] = *(current_entry->ext + k);
        }

        memcpy(pentry->name, filename, FILENAME_MAX_LENGTH + 1);
        pentry->size = current_entry->size;
        pentry->is_archived = (current_entry->file_attributes & (uint8_t) ATTR_ARCHIVE) ? 1 : 0;
        pentry->is_readonly = (current_entry->file_attributes & (uint8_t) ATTR_READONLY) ? 1 : 0;
        pentry->is_system = (current_entry->file_attributes & (uint8_t) ATTR_SYSTEM) ? 1 : 0;
        pentry->is_hidden = (current_entry->file_attributes & (uint8_t) ATTR_HIDDEN) ? 1 : 0;
        pentry->is_directory = (current_entry->file_attributes & (uint8_t) ATTR_DIRECTORY) ? 1 : 0;

        pdir->current_entry++;
        return 0;
    }

    return 1;
}

int dir_close(struct dir_t* pdir) {
    if(pdir == NULL) {
        errno = EFAULT;
        return -1;
    }

    free(pdir);
    return 0;
}
