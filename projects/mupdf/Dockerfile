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

FROM gcr.io/oss-fuzz-base/base-builder
RUN apt-get update && apt-get install -y make libtool pkg-config libzip-dev build-essential cmake zlib1g-dev vim
RUN git clone --recursive --depth 1 git://git.ghostscript.com/mupdf.git mupdf

RUN git clone --depth 1 https://github.com/mozilla/pdf.js pdf.js && \
    zip -q $SRC/pdf_fuzzer_seed_corpus.zip pdf.js/test/pdfs/*.pdf && \
    rm -rf pdf.js

RUN git clone https://github.com/HiraokaHyperTools/SampleXpsDocuments_1_0 xps_samples && \
    zip -q $SRC/xps_fuzzer_seed_corpus.zip xps_samples/**/*.xps && \ 
    rm -rf xps_samples

RUN git clone https://github.com/linebender/resvg/ svg_samples && \ 
    zip -q $SRC/svg_fuzzer_seed_corpus.zip $(find svg_samples/crates/resvg/tests/tests/ -type f -name '*.svg') && \
    rm -rf svg_samples


ADD https://raw.githubusercontent.com/AFLplusplus/AFLplusplus/refs/heads/stable/dictionaries/pdf.dict $SRC/pdf_fuzzer.dict

RUN git clone https://github.com/nih-at/libzip $SRC/libzip

WORKDIR mupdf
COPY *.zip $SRC/
COPY *.cc $SRC/
COPY *.a $SRC/
# COPY *.txt $SRC/
COPY build.sh *.options $SRC/
