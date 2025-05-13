/*
# Copyright 2018 Google Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
################################################################################
*/

#include <cstdint>
#include <filesystem>
#include <inttypes.h>
#include <iostream>
#include <mupdf/fitz.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include <zip.h>
#include <zlib.h>

#define ALIGNMENT ((size_t)16)
#define KBYTE ((size_t)1024)
#define MBYTE (1024 * KBYTE)
#define GBYTE (1024 * MBYTE)
#define MAX_ALLOCATION (1 * GBYTE)

#define MAX_XPS_SIZE (10 * MBYTE)
#define XPS_GROWTH_RATE (500)

static size_t used;

static void *fz_limit_reached_ossfuzz(size_t oldsize, size_t size) {
  if (oldsize == 0)
    fprintf(stderr,
            "limit: %zu Mbyte used: %zu Mbyte allocation: %zu: limit reached\n",
            MAX_ALLOCATION / MBYTE, used / MBYTE, size);
  else
    fprintf(stderr,
            "limit: %zu Mbyte used: %zu Mbyte reallocation: %zu -> %zu: limit "
            "reached\n",
            MAX_ALLOCATION / MBYTE, used / MBYTE, oldsize, size);
  fflush(0);
  return NULL;
}

static void *fz_malloc_ossfuzz(void *opaque, size_t size) {
  char *ptr = NULL;

  if (size == 0)
    return NULL;
  if (size > SIZE_MAX - ALIGNMENT)
    return NULL;
  if (size + ALIGNMENT > MAX_ALLOCATION - used)
    return fz_limit_reached_ossfuzz(0, size + ALIGNMENT);

  ptr = (char *)malloc(size + ALIGNMENT);
  if (ptr == NULL)
    return NULL;

  memcpy(ptr, &size, sizeof(size));
  used += size + ALIGNMENT;

  return ptr + ALIGNMENT;
}

static void fz_free_ossfuzz(void *opaque, void *ptr) {
  size_t size;

  if (ptr == NULL)
    return;
  if (ptr < (void *)ALIGNMENT)
    return;

  ptr = (char *)ptr - ALIGNMENT;
  memcpy(&size, ptr, sizeof(size));

  used -= size + ALIGNMENT;
  free(ptr);
}

static void *fz_realloc_ossfuzz(void *opaque, void *old, size_t size) {
  size_t oldsize;
  char *ptr;

  if (old == NULL)
    return fz_malloc_ossfuzz(opaque, size);
  if (old < (void *)ALIGNMENT)
    return NULL;

  if (size == 0) {
    fz_free_ossfuzz(opaque, old);
    return NULL;
  }
  if (size > SIZE_MAX - ALIGNMENT)
    return NULL;

  old = (char *)old - ALIGNMENT;
  memcpy(&oldsize, old, sizeof(oldsize));

  if (size + ALIGNMENT > MAX_ALLOCATION - used + oldsize + ALIGNMENT)
    return fz_limit_reached_ossfuzz(oldsize + ALIGNMENT, size + ALIGNMENT);

  ptr = (char *)realloc(old, size + ALIGNMENT);
  if (ptr == NULL)
    return NULL;

  used -= oldsize + ALIGNMENT;
  memcpy(ptr, &size, sizeof(size));
  used += size + ALIGNMENT;

  return ptr + ALIGNMENT;
}

static fz_alloc_context fz_alloc_ossfuzz = {
    NULL, fz_malloc_ossfuzz, fz_realloc_ossfuzz, fz_free_ossfuzz};

namespace fs = std::filesystem;

extern "C" size_t LLVMFuzzerMutate(uint8_t *Data, size_t Size, size_t MaxSize);

extern "C" size_t LLVMFuzzerCustomMutator(uint8_t *data, size_t size,
                                          size_t maxSize, unsigned int seed) {

  uint16_t crc = crc32(0, Z_NULL, 0);
  crc = crc32(crc, data, size);

  zip_error_t *err = (zip_error_t *)malloc(sizeof(zip_error_t));
  zip_error_init(err);
  zip_source_t *src = zip_source_buffer_create(data, size, 0, err);

  if (!src) {
    zip_error_fini(err);
    // TODO: return a dummy result
    return 0;
  }

  zip_t *za = zip_open_from_source(src, 0, err);
  if (!za) {
    zip_source_free(src);
    zip_error_fini(err);
    // TODO: return a dummy result
    return 0;
  }

  zip_source_keep(src); // increment reference counter so we can still copy the
                        // buf once we're done.

  std::vector<zip_int64_t> interesting_files;

  zip_int64_t num_entries = zip_get_num_entries(za, 0);
  for (zip_int64_t i = 0; i < num_entries; i++) {
    struct zip_stat stat;
    if (zip_stat_index(za, i, 0, &stat) == 0) {
      std::string name = stat.name;
      auto path = fs::path(stat.name);
      auto ext = path.extension();

      if (ext == ".fpage" || ext == ".fdseq" || ext == ".fdoc") {
        interesting_files.push_back(i);
      }
    }
  }

  auto num_files = interesting_files.size();
  if (num_files == 0) {
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

  size_t size_to_allocate = (size_t)stat.size + XPS_GROWTH_RATE;
  if (size_to_allocate > maxSize) {
    size_to_allocate = maxSize;
  }

  uint8_t *file_data = (uint8_t *)malloc(size_to_allocate);
  memset(file_data, 0, size_to_allocate);

  zip_file_t *f = zip_fopen_index(za, file_to_modify, 0);
  zip_fread(f, file_data, stat.size);
  size_t new_size = LLVMFuzzerMutate(file_data, sizeof(file_data), stat.size);

  zip_source_t *modified_file = zip_source_buffer(za, file_data, new_size, 0);
  if (!modified_file) {
    free(file_data);
    zip_close(za);
    zip_error_fini(err);
    return size;
  }
  int result =
      zip_file_replace(za, file_to_modify, file_data, modified_file, 0);
  if (result != 0) {
    printf("Error replacing zip");
    free(file_data);
    zip_close(za);
    zip_error_fini(err);
  }

  free(file_data);
  zip_close(za);
  zip_error_fini(err);

  return new_size;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  fz_context *ctx;
  fz_stream *stream;
  fz_document *doc;
  fz_pixmap *pix;

  used = 0;

  ctx = fz_new_context(&fz_alloc_ossfuzz, nullptr, FZ_STORE_DEFAULT);
  stream = NULL;
  doc = NULL;
  pix = NULL;

  fz_var(stream);
  fz_var(doc);
  fz_var(pix);

  fz_try(ctx) {
    fz_register_document_handlers(ctx);
    stream = fz_open_memory(ctx, data, size);
    doc = fz_open_document_with_stream(ctx, "xps", stream);

    for (int i = 0; i < fz_count_pages(ctx, doc); i++) {
      pix = fz_new_pixmap_from_page_number(ctx, doc, i, fz_identity,
                                           fz_device_rgb(ctx), 0);
      fz_drop_pixmap(ctx, pix);
      pix = NULL;
    }
  }
  fz_always(ctx) {
    fz_drop_pixmap(ctx, pix);
    fz_drop_document(ctx, doc);
    fz_drop_stream(ctx, stream);
  }
  fz_catch(ctx) {
    fz_report_error(ctx);
    fz_log_error(ctx, "error rendering pages");
  }

  fz_flush_warnings(ctx);
  fz_drop_context(ctx);

  return 0;
}
