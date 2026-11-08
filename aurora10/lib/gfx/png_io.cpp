#include "png_io.hpp"

#include <fstream>
#include <system_error>
#include <vector>

#include "dds_io.hpp"
#include "png.h"
#include "../fs_helper.hpp"

static aurora::Module Log("aurora::gfx::png");

namespace aurora::gfx::png {

struct PngStructs {
  png_structp pStruct;
  png_infop pInfo;
  std::ifstream file;

  ~PngStructs() {
    png_destroy_read_struct(&pStruct, &pInfo, nullptr);
  }
};

static void readPngData(png_structp png, png_bytep data, const size_t length) {
  auto structs = static_cast<PngStructs*>(png_get_io_ptr(png));
  structs->file.read(reinterpret_cast<char*>(data), static_cast<std::streamsize>(length));

  if (structs->file.eof() || structs->file.fail()) {
    png_error(png, "file read failed!");
  }
}

std::optional<ConvertedTexture>
load_png_file(const std::filesystem::path& path) noexcept {
  PngStructs structs{};

  structs.file = std::move(std::ifstream(path, std::ifstream::in | std::ifstream::binary));
  if (!structs.file) {
    Log.error("failed to open file: {}", fs_path_to_string(path));
    return std::nullopt;
  }

  structs.pStruct = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
  if (!structs.pStruct) {
    Log.error("png_create_read_struct failed");
    return std::nullopt;
  }

  structs.pInfo = png_create_info_struct(structs.pStruct);
  if (!structs.pInfo) {
    Log.error("png_create_info_struct failed");
    return std::nullopt;
  }

  // I'm scared of putting any locals after that setjmp.
  std::vector<png_bytep> rowPointers;
  ByteBuffer imageData{};
  png_uint_32 width, height;
  int bit_depth, color_type, interlace_type, compression_type, filter_type;
  size_t rowBytes;
  int i;

  if (setjmp(png_jmpbuf(structs.pStruct))) {
    Log.error("libpng encountered an error");
    return std::nullopt;
  }

  png_set_read_fn(structs.pStruct, &structs, readPngData);
  png_read_info(structs.pStruct, structs.pInfo);

  if (!png_get_IHDR(structs.pStruct, structs.pInfo, &width, &height, &bit_depth, &color_type, &interlace_type, &compression_type, &filter_type)) {
    Log.error("libpng unable to read IHDR");
    return std::nullopt;
  }

  // Always read as RGBA8.
  png_set_gray_to_rgb(structs.pStruct);
  png_set_filler(structs.pStruct, 0xFF, PNG_FILLER_AFTER);
  png_set_expand(structs.pStruct);
  png_set_strip_16(structs.pStruct);

  png_read_update_info(structs.pStruct, structs.pInfo);
  rowBytes = png_get_rowbytes(structs.pStruct, structs.pInfo);
  rowPointers.resize(height);

  imageData.append_zeroes(rowBytes * height);

  for (i = 0; i < height; i++) {
    rowPointers[i] = imageData.data() + i * rowBytes;
  }

  png_read_image(structs.pStruct, rowPointers.data());
  png_read_end(structs.pStruct, nullptr);

  return ConvertedTexture{
    .format = wgpu::TextureFormat::RGBA8Unorm,
    .width = width,
    .height = height,
    .mips = 1,
    .data = std::move(imageData)
  };
}
}
namespace aurora::gfx::png {
bool write_rgba8_png(const std::filesystem::path& path, uint32_t width, uint32_t height, const uint8_t* pixels) noexcept {
  png_image image{};
  image.version = PNG_IMAGE_VERSION;
  image.width = width;
  image.height = height;
  image.format = PNG_FORMAT_RGBA;
  // To memory, then through std::ofstream: wide paths on Windows (png_image_write_to_file takes a narrow one).
  png_alloc_size_t size = 0;
  if (!png_image_write_to_memory(&image, nullptr, &size, 0, pixels, 0, nullptr) || size == 0) {
    png_image_free(&image);
    return false;
  }
  std::vector<uint8_t> bytes(size);
  if (!png_image_write_to_memory(&image, bytes.data(), &size, 0, pixels, 0, nullptr)) {
    png_image_free(&image);
    return false;
  }
  auto temp = path;
  temp += ".tmp";
  {
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    if (!out) {
      return false;
    }
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(size));
    if (!out) {
      return false;
    }
  }
  std::error_code ec;
  std::filesystem::rename(temp, path, ec);
  if (ec) {
    std::filesystem::remove(temp, ec);
    return false;
  }
  return true;
}
} // namespace aurora::gfx::png
