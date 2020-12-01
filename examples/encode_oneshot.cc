// Copyright (c) the JPEG XL Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// This example encodes a file containing a floating point image to another
// file containing JPEG XL image with a single frame.

#include <limits.h>
#include <string.h>

#include <sstream>
#include <string>
#include <vector>

#include "jxl/encode.h"
#include "jxl/thread_parallel_runner.h"

/**
 * Reads from .pfm file (Portable FloatMap)
 *
 * @param filename name of the file to read
 * @param pixels vector to fill with loaded pixels as 32-bit floating point with
 * 3-channel RGB
 * @param xsize set to width of loaded image
 * @param ysize set to height of loaded image
 */
bool ReadPFM(const char* filename, std::vector<float>* pixels, uint32_t* xsize,
             uint32_t* ysize) {
  FILE* file = fopen(filename, "rb");
  if (!file) {
    fprintf(stderr, "Could not open %s for reading.\n", filename);
    return false;
  }
  uint32_t endian_test = 1;
  uint8_t little_endian[4];
  memcpy(little_endian, &endian_test, 4);

  if (fseek(file, 0, SEEK_END) != 0) {
    fclose(file);
    return false;
  }

  long size = ftell(file);
  // Avoid invalid file or directory.
  if (size >= LONG_MAX || size < 0) {
    fclose(file);
    return false;
  }

  if (fseek(file, 0, SEEK_SET) != 0) {
    fclose(file);
    return false;
  }

  std::vector<char> data;
  data.resize(size);

  size_t readsize = fread(data.data(), 1, size, file);
  if ((long)readsize != size) {
    return false;
  }
  if (fclose(file) != 0) {
    return false;
  }

  std::stringstream datastream;
  std::string datastream_content(data.data(), data.size());
  datastream.str(datastream_content);

  std::string pf_token;
  getline(datastream, pf_token, '\n');
  if (pf_token != "PF") {
    fprintf(stderr,
            "%s doesn't seem to be a 3 channel Portable FloatMap file (missing "
            "'PF\\n' "
            "bytes).\n",
            filename);
    return false;
  }

  std::string xsize_token;
  getline(datastream, xsize_token, ' ');
  *xsize = std::stoi(xsize_token);

  std::string ysize_token;
  getline(datastream, ysize_token, '\n');
  *ysize = std::stoi(ysize_token);

  std::string endianness_token;
  getline(datastream, endianness_token, '\n');
  bool input_little_endian;
  if (endianness_token == "1.0") {
    input_little_endian = false;
  } else if (endianness_token == "-1.0") {
    input_little_endian = true;
  } else {
    fprintf(stderr,
            "%s doesn't seem to be a Portable FloatMap file (endianness token "
            "isn't '1.0' or '-1.0').\n",
            filename);
    return false;
  }

  size_t offset = pf_token.size() + 1 + xsize_token.size() + 1 +
                  ysize_token.size() + 1 + endianness_token.size() + 1;

  if (data.size() != *ysize * *xsize * 3 * 4 + offset) {
    fprintf(stderr,
            "%s doesn't seem to be a Portable FloatMap file (pixel data bytes "
            "are %d, but expected %d * %d * 3 * 4 + %d (%d).\n",
            filename, (int)data.size(), (int)*ysize, (int)*xsize, (int)offset,
            (int)(*ysize * *xsize * 3 * 4 + offset));
    return false;
  }

  if (!!little_endian[0] != input_little_endian) {
    fprintf(stderr,
            "%s has a different endianness than we do, conversion is not "
            "supported.\n",
            filename);
    return false;
  }

  pixels->resize(*ysize * *xsize * 3);

  for (int y = *ysize - 1; y >= 0; y--) {
    for (int x = 0; x < (int)*xsize; x++) {
      for (int c = 0; c < 3; c++) {
        memcpy(pixels->data() + (y * *xsize + x) * 3 + c, data.data() + offset,
               sizeof(float));
        offset += sizeof(float);
      }
    }
  }

  return true;
}

/**
 * Compresses the provided pixels.
 *
 * @param pixels input pixels
 * @param xsize width of the input image
 * @param ysize height of the input image
 * @param compressed will be populated with the compressed bytes
 */
bool EncodeJxlOneshot(const std::vector<float>& pixels, const uint32_t xsize,
                      const uint32_t ysize, std::vector<uint8_t>* compressed) {
  JxlEncoder* enc = JxlEncoderCreate(nullptr);

  void* runner = JxlThreadParallelRunnerCreate(
      NULL, JxlThreadParallelRunnerDefaultNumWorkerThreads());
  if (JXL_ENC_SUCCESS !=
      JxlEncoderSetParallelRunner(enc, JxlThreadParallelRunner, runner)) {
    fprintf(stderr, "JxlEncoderSetParallelRunner failed\n");
    JxlThreadParallelRunnerDestroy(runner);
    JxlEncoderDestroy(enc);
    return false;
  }

  JxlPixelFormat pixel_format = {3, JXL_TYPE_FLOAT, JXL_NATIVE_ENDIAN, 0};
  if (JXL_ENC_SUCCESS != JxlEncoderSetDimensions(enc, xsize, ysize)) {
    fprintf(stderr, "JxlEncoderSetDimensions failed\n");
    JxlThreadParallelRunnerDestroy(runner);
    JxlEncoderDestroy(enc);
    return false;
  }

  if (JXL_ENC_SUCCESS !=
      JxlEncoderAddImageFrame(enc, &pixel_format, (void*)pixels.data(),
                              sizeof(float) * pixels.size())) {
    fprintf(stderr, "JxlEncoderAddImageFrame failed\n");
    JxlThreadParallelRunnerDestroy(runner);
    JxlEncoderDestroy(enc);
    return false;
  }

  compressed->resize(64);
  uint8_t* next_out = compressed->data();
  size_t avail_out = compressed->size() - (next_out - compressed->data());
  JxlEncoderStatus process_result = JXL_ENC_NEED_MORE_OUTPUT;
  while (process_result == JXL_ENC_NEED_MORE_OUTPUT) {
    process_result = JxlEncoderProcessOutput(enc, &next_out, &avail_out);
    if (process_result == JXL_ENC_NEED_MORE_OUTPUT) {
      size_t offset = next_out - compressed->data();
      compressed->resize(compressed->size() * 2);
      next_out = compressed->data() + offset;
      avail_out = compressed->size() - offset;
    }
  }
  compressed->resize(next_out - compressed->data());
  if (JXL_ENC_SUCCESS != process_result) {
    fprintf(stderr, "JxlEncoderProcessOutput failed\n");
    JxlThreadParallelRunnerDestroy(runner);
    JxlEncoderDestroy(enc);
    return false;
  }

  JxlEncoderDestroy(enc);
  return true;
}

/**
 * Writes bytes to file.
 */
bool WriteFile(const std::vector<uint8_t> bytes, const char* filename) {
  FILE* file = fopen(filename, "wb");
  if (!file) {
    fprintf(stderr, "Could not open %s for writing\n", filename);
    return false;
  }
  if (fwrite(bytes.data(), sizeof(uint8_t), bytes.size(), file) !=
      bytes.size()) {
    fprintf(stderr, "Could not write bytes to %s\n", filename);
    return false;
  }
  if (fclose(file) != 0) {
    fprintf(stderr, "Could not close %s\n", filename);
    return false;
  }
  return true;
}

int main(int argc, char* argv[]) {
  if (argc != 3) {
    fprintf(stderr,
            "Usage: %s <pfm> <jxl>\n"
            "Where:\n"
            "  pfm = input Portable FloatMap image filename\n"
            "  jxl = output JPEG XL image filename\n"
            "Output files will be overwritten.\n",
            argv[0]);
    return 1;
  }

  const char* pfm_filename = argv[1];
  const char* jxl_filename = argv[2];

  std::vector<float> pixels;
  uint32_t xsize;
  uint32_t ysize;
  if (!ReadPFM(pfm_filename, &pixels, &xsize, &ysize)) {
    fprintf(stderr, "Couldn't load %s\n", pfm_filename);
    return 2;
  }

  std::vector<uint8_t> compressed;
  if (!EncodeJxlOneshot(pixels, xsize, ysize, &compressed)) {
    fprintf(stderr, "Couldn't encode jxl\n");
    return 3;
  }

  if (!WriteFile(compressed, jxl_filename)) {
    fprintf(stderr, "Couldn't write jxl file\n");
    return 4;
  }

  return 0;
}
