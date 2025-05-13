#include <cstdint>
#include <filesystem>
#include <inttypes.h>
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>
#include <zip.h>
#include <zlib.h>
#include <string.h>

#define ALIGNMENT ((size_t)16)
#define KBYTE ((size_t)1024)
#define MBYTE (1024 * KBYTE)
#define GBYTE (1024 * MBYTE)
#define MAX_ALLOCATION (1 * GBYTE)
#define MAX_XPS_SIZE (10 * MBYTE)
#define XPS_GROWTH_RATE (500)

extern "C" size_t LLVMFuzzerMutate(uint8_t *Data, size_t Size, size_t MaxSize)
{
    printf("mutating...\n");
    memset(Data, 0, Size);

    return Size;
}

void printHexTable(const uint8_t *data, size_t size, size_t bytesPerRow = 16)
{
    for (size_t i = 0; i < size; i += bytesPerRow)
    {
        printf("%08zx  ", i); // Print the offset
        for (size_t j = 0; j < bytesPerRow; ++j)
        {
            if (i + j < size)
                printf("%02x ", data[i + j]); // Print hex value
            else
                printf("   "); // Padding for incomplete rows
        }

        printf(" |");
        for (size_t j = 0; j < bytesPerRow; ++j)
        {
            if (i + j < size)
            {
                uint8_t c = data[i + j];
                printf("%c", (c >= 32 && c <= 126) ? c : '.'); // Print ASCII or '.'
            }
            else
            {
                printf(" ");
            }
        }
        printf("|\n");
    }
}

namespace fs = std::filesystem;

extern "C" size_t LLVMFuzzerCustomMutator(uint8_t *data, size_t size,
                                          size_t maxSize, unsigned int seed)
{
    uint8_t *new_buf;
    zip_uint64_t new_length = 0;
    unsigned long crc = crc32(0, Z_NULL, 0);
    crc = crc32(crc, data, size);


    void *copied_data = malloc(size);
    memcpy(copied_data, data, size);

    zip_error_t err;
    zip_error_init(&err);

    zip_source_t *src = zip_source_buffer_create(copied_data, size, 0, &err);

    if (!src)
    {
        fprintf(stderr, "Could not open source: %s\n", zip_error_strerror(&err));
        zip_error_fini(&err);
        // TODO: return a dummy result
        return 0;
    }

    zip_t *za = zip_open_from_source(src, 0, &err);
    if (!za)
    {
        fprintf(stderr, "Could not open archive: %s\n", zip_error_strerror(&err));
        zip_source_free(src);
        zip_error_fini(&err);
        // TODO: return a dummy result
        return 0;
    }

    zip_error_fini(&err);
    zip_source_keep(src); // increment reference counter so we can still copy the buf once we're done.

    std::vector<zip_int64_t> interesting_files;

    zip_int64_t num_entries = zip_get_num_entries(za, 0);
    for (zip_int64_t i = 0; i < num_entries; i++)
    {
        struct zip_stat stat;
        if (zip_stat_index(za, i, 0, &stat) == 0)
        {
            std::string name = stat.name;
            auto path = fs::path(stat.name);
            auto ext = path.extension();

            if (ext == ".fpage" || ext == ".fdseq" || ext == ".fdoc")
            {
                interesting_files.push_back(i);
            }
        }
    }

    auto num_files = interesting_files.size();
    if (num_files == 0)
    {
        fprintf(stderr, "No interesting files in archive"); // TODO: This should just be empty...
        zip_close(za);
        zip_error_fini(&err);
        // TODO: return something ?
        return size;
    }

    auto vec_entry_to_modify = crc % num_files;
    auto change_file_index = interesting_files.at(vec_entry_to_modify);

    struct zip_stat stat;
    zip_stat_init(&stat);
    zip_stat_index(za, change_file_index, 0, &stat);

    size_t size_to_allocate = stat.size + XPS_GROWTH_RATE;

    if (((size - stat.size) + size_to_allocate) > maxSize) {
        size_to_allocate = maxSize - (size - stat.size);
        fprintf(stdout, "Clipping to: 0x%zx\n", size_to_allocate);
    }

    uint8_t *file_data = (uint8_t *)malloc(size_to_allocate);
    zip_file_t *f = zip_fopen_index(za, change_file_index, 0);

    printf("Picked file %s to modify\n", stat.name);
    zip_fread(f, file_data, stat.size);
    zip_fclose(f);
    size_t new_size = LLVMFuzzerMutate(file_data, stat.size, size_to_allocate);

    printf("old_size: %lu, new_size: %lu\n", stat.size, new_size);

    zip_source_t *modified_file = zip_source_buffer(za, file_data, (zip_uint64_t) new_size, 0);

    if (!modified_file)
    {
        free(file_data);
        zip_close(za);
        zip_error_fini(&err);
        return size;
    }

    int result = zip_file_replace(za, change_file_index, modified_file, 0);

    if (zip_close(za) < 0)
    {
        fprintf(stderr, "cannot close the archive because: %s\n", zip_strerror(za));
        return size;
    }

    if (zip_source_is_deleted(src))
    {
        fprintf(stderr, "The source was deleted!!!");
    }
    else
    {
        zip_stat_t new_stat;
        if (zip_source_stat(src, &new_stat) < 0)
        {
            fprintf(stderr, "Cannot stat source: %s\n", zip_error_strerror(zip_source_error(src)));
            return size;
        }
        new_length = new_stat.size;
        new_buf = (uint8_t *)malloc(new_stat.size);

        memset(new_buf, 1, new_length);

        if (zip_source_open(src) < 0) {
            fprintf(stderr, "Cannot open source: %s\n", zip_error_strerror(zip_source_error(src)));
            zip_source_close(src);
            return size;
        }
        
        zip_source_seek(src, 0, SEEK_SET);
        zip_uint64_t read_bytes = (zip_uint64_t)zip_source_read(src, new_buf, new_length);

        if (read_bytes < new_length)
        {
            fprintf(stderr, "Only read %lu/%lu bytes into the buffer...\n", read_bytes, new_length);
            return size;
        }
        
        zip_source_close(src);
    }
    
    
    
    unsigned long crc_new = crc32(0, Z_NULL, 0);
    crc_new = crc32(crc, (uint8_t *)data, size);
    
    printf("sz: %lu -> %lu, crc: %lu -> %lu\n", size, (size_t)new_length, crc, crc_new);
    memcpy(data, new_buf, new_length);

    return (size_t)new_length;
}

int main()
{
    const char *filename = "test.xps";
    FILE *file = fopen(filename, "rb");
    if (!file)
    {
        std::cerr << "Failed to open file: " << filename << std::endl;
        return 1;
    }

    fseek(file, 0, SEEK_END);
    size_t file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    size_t max_size = file_size + 500;

    uint8_t *data = (uint8_t *)malloc(max_size);
    if (!data)
    {
        std::cerr << "Failed to allocate memory for file data." << std::endl;
        fclose(file);
        return 1;
    }

    fread(data, 1, file_size, file);
    fclose(file);

    std::cout << "Loaded file: " << filename << " (" << file_size << " bytes)" << std::endl;

    size_t new_sz = LLVMFuzzerCustomMutator(data, file_size, max_size, 0);

    FILE *output_file = fopen("test_out.xps", "wb");
    if (!output_file)
    {
        std::cerr << "Failed to open output file: test_out.xps" << std::endl;
        free(data);
        return 1;
    }

    fwrite(data, 1, new_sz, output_file);
    fclose(output_file);

    std::cout << "Modified data written to test_out.xps" << std::endl;

    free(data);
    return 0;
}
