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


#define ALIGNMENT ((size_t) 16)
#define KBYTE ((size_t) 1024)
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

namespace fs = std::filesystem;

extern "C" size_t LLVMFuzzerCustomMutator(uint8_t *data, size_t size,
                                          size_t maxSize, unsigned int seed)
{

    unsigned long crc = crc32(0, Z_NULL, 0);
    crc = crc32(crc, data, size);

    zip_error_t *err = (zip_error_t *)malloc(sizeof(zip_error_t));
    zip_error_init(err);
    zip_source_t *src = zip_source_file_create("test2.xps", 0, size, err);
    zip_stat_t src_stat_orig; 
    if (zip_source_stat(src, &src_stat_orig) < 0) {
        fprintf(stderr, "stat failed: %s\n", zip_error_strerror(zip_source_error(src)));
    }

    if (!src)
    {
        zip_error_fini(err);
        // TODO: return a dummy result
        return 0;
    }

    zip_t *za = zip_open_from_source(src, 0, err);
    if (!za)
    {
        zip_source_free(src);
        zip_error_fini(err);
        // TODO: return a dummy result
        return 0;
    }

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
        zip_close(za);
        zip_error_fini(err);
        // TODO: return something ?
        return size;
    }

    auto vec_entry_to_modify = crc % num_files;
    auto file_to_modify = interesting_files.at(vec_entry_to_modify);

    struct zip_stat stat;
    zip_stat_init(&stat);
    zip_stat_index(za, file_to_modify, 0, &stat);

    size_t size_to_allocate = stat.size + XPS_GROWTH_RATE;

    if (((size - stat.size) + size_to_allocate) > maxSize) {
        size_to_allocate = maxSize - (size - stat.size);
    }
    fprintf(stdout, "Clipping to: 0x%zx\n", size_to_allocate);
    
    uint8_t *file_data = (uint8_t *)malloc(size_to_allocate);
    memset(file_data, 0, size_to_allocate);

    zip_file_t *f = zip_fopen_index(za, file_to_modify, 0);
    
    printf("Picked file %s to modify\n", stat.name);
    zip_fread(f, file_data, stat.size);
    size_t new_size = LLVMFuzzerMutate(file_data, sizeof(file_data), stat.size);
    

    zip_source_t *modified_file = zip_source_buffer(za, file_data, new_size, 0);
    if (!modified_file)
    {
        free(file_data);
        zip_close(za);
        zip_error_fini(err);
        return size;
    }
    int result = zip_file_replace(za, file_to_modify, modified_file, 0);

    struct zip_stat new_stat;
    zip_stat_index(za, file_to_modify, 0, &new_stat); 

    if (result != 0)
    {
        printf("Error replacing zip");
        free(file_data);
        zip_close(za);
        zip_error_fini(err);
    }


    fprintf(stderr, "ze: %s\n", zip_error_strerror(zip_source_error(src)));
    int err_src = zip_source_begin_write(src); 
    // zip_close(za);
    err_src |= zip_source_commit_write(src);
    if(err_src < 0) {
	    fprintf(stderr, "error!!!\n");
	    fprintf(stderr, "ze: %s\n", zip_error_strerror(zip_source_error(src)));
    } else {
	    printf("commited :)");
    }

    zip_stat_t src_stat; 
    if (zip_source_stat(src, &src_stat) < 0) {
        fprintf(stderr, "stat failed: %s\n", zip_error_strerror(zip_source_error(src)));
    }

    size_t new_size2 = src_stat.size;
    fprintf(stdout, "old size: 0x%zx, new size: 0x%zx, max size: 0x%zx\n", src_stat_orig.size, new_size2, maxSize);
    // open for reading
    int retval = zip_source_open(src);
    retval |= zip_source_seek(src, 0, SEEK_SET);
    if (retval < 0) {
        fprintf(stderr, "Not able to seek or open :(");
    }
    // size_t read_size = zip_source_read(src, data, new_size2);
    // if (read_size != new_size2) {
    //     fprintf(stderr, "BAD!!!! 0x%zx != 0x%zx\n", read_size, new_size2);
    // }


    // now rewrite things from src into data. 
    zip_error_fini(err);

    return new_size2;
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

    LLVMFuzzerCustomMutator(data, file_size, max_size, 0);

    FILE *output_file = fopen("test_out.xps", "wb");
    if (!output_file)
    {
        std::cerr << "Failed to open output file: test_out.xps" << std::endl;
        free(data);
        return 1;
    }

    fwrite(data, 1, file_size, output_file);
    fclose(output_file);

    std::cout << "Modified data written to test_out.xps" << std::endl;

    free(data);
    return 0;
}
