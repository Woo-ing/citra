// Copyright 2015 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <array>
#include <cstddef>
#include <memory>
#include "common/assert.h"
#include "common/color.h"
#include "common/common_types.h"
#include "common/vector_math.h"
#include "core/core.h"
#include "core/hle/service/y2r_u.h"
#include "core/hw/y2r.h"
#include "core/memory.h"

namespace HW::Y2R {

using namespace Service::Y2R;

inline s32 clamp(s32 value, s32 min, s32 max) {
    return (value >= min) ? ((value <= max) ? value : max) : min;
}

struct Y2UTable {
    //本表总共需要4*256*256 = 256K字节
    u8 YV2R[256][256];
    u8 YU2B[256][256];
    u8 UV2IG[256][256];
    u8 YIG2G[256][256];

    Y2UTable(const StandardCoefficient coefficient) {
        // This conversion process is bit-exact with hardware, as far as could be tested.
        u32 index = static_cast<u32>(coefficient);
        auto& c = Y2R_U::standard_coefficients[index];
        const s32 rounding_offset = 0x18;

        s32 minIG = 0;
        s32 maxIG = c[2] * 255 + c[3] * 255 + 1;
        for (s32 V = 0; V < 256; ++V) {
            for (s32 U = 0; U < 256; ++U) {
                s32 i = c[2] * V + c[3] * U;

                UV2IG[U][V] = minIG + (i << 8) / maxIG;
            }
        }

        for (s32 Y = 0; Y < 256; ++Y) {
            s32 cY = c[0] * Y;
            for (s32 V = 0; V < 256; ++V) {
                s32 r = cY + c[1] * V;
                r = (r >> 3) + c[5] + rounding_offset;
                YV2R[Y][V] = clamp(r >> 5, 0, 0xFF);

                for (s32 U = 0; U < 256; ++U) {
                    s32 g = cY - c[2] * V - c[3] * U;
                    g = (g >> 3) + c[6] + rounding_offset;
                    YIG2G[Y][UV2IG[U][V]] = clamp(g >> 5, 0, 0xFF);
                }
            }
            for (s32 U = 0; U < 256; ++U) {
                s32 b = cY + c[4] * U;
                b = (b >> 3) + c[7] + rounding_offset;
                YU2B[Y][U] = clamp(b >> 5, 0, 0xFF);
            }
        }
    }

    inline u32 getRGB(u8 Y, u8 U, u8 V) {
        return (YV2R[Y][V] << 24) | (YIG2G[Y][UV2IG[U][V]] << 16) | YU2B[Y][U] << 8;
    }
    inline void getRGB2(u8 Y1, u8 Y2, u8 U, u8 V, u32& p1, u32& p2) {
        u8 uv2ig = UV2IG[U][V];
        p1 = (YV2R[Y1][V] << 24) | (YIG2G[Y1][uv2ig] << 16) | YU2B[Y1][U] << 8;
        p2 = (YV2R[Y2][V] << 24) | (YIG2G[Y2][uv2ig] << 16) | YU2B[Y2][U] << 8;
    }
};

static const std::size_t MAX_TILES = 1024 / 8;
static const std::size_t TILE_SIZE = 8 * 8;
using ImageTile = std::array<u32, TILE_SIZE>;
static std::vector<std::unique_ptr<Y2UTable>> Y2RTables(ARRAY_SIZE(Y2R_U::standard_coefficients));

/// Converts a image strip from the source YUV format into individual 8x8 RGB32 tiles.
static void ConvertYUVToRGB(InputFormat input_format, const u8* input_Y, const u8* input_U,
                            const u8* input_V, ImageTile output[], unsigned int width,
                            unsigned int height, const StandardCoefficient coefficient) {
    unsigned int YI = 0;
    unsigned int UI = 0;
    unsigned int VI = 0;
    u8 Y, U, V = 0;

    u32 index = static_cast<u32>(coefficient);
    std::unique_ptr<Y2UTable>& table = Y2RTables[index];
    if (table == nullptr) {
        table = std::make_unique<Y2UTable>(coefficient);
    }

    switch (input_format) {
    case InputFormat::YUV422_Indiv8:
    case InputFormat::YUV422_Indiv16:
        for (unsigned int y = 0; y < height; ++y) {
            for (unsigned int x = 0; x < width; ++x) {
                // Y = input_Y[y * width + x];
                // U = input_U[(y * width + x) / 2];
                // V = input_V[(y * width + x) / 2];
                Y = input_Y[YI];
                U = input_U[UI];
                V = input_V[VI];
                ++YI;
                UI = VI = YI >> 1;

                output[x >> 3][(y << 3) + (x & 7)] = table->getRGB(Y, U, V);
            }
        }
        break;
    case InputFormat::YUV420_Indiv8:
    case InputFormat::YUV420_Indiv16:
        for (unsigned int y = 0; y < height; ++y) {
            for (unsigned int x = 0; x < width; x += 2) {
                // Y = input_Y[y * width + x];
                // U = input_U[((y / 2) * width + x) / 2];
                // V = input_V[((y / 2) * width + x) / 2];
                Y = input_Y[YI];
                U = input_U[UI];
                V = input_V[UI];
                table->getRGB2(Y, input_Y[YI + 1], U, V, output[x >> 3][(y << 3) + (x & 7)],
                               output[(x + 1) >> 3][(y << 3) + ((x + 1) & 7)]);
                ++UI;
                YI += 2;
            }
            UI -= width >> 1;
            ++y;
            for (unsigned int x = 0; x < width; x += 2) {
                // Y = input_Y[y * width + x];
                // U = input_U[((y / 2) * width + x) / 2];
                // V = input_V[((y / 2) * width + x) / 2];
                Y = input_Y[YI];
                U = input_U[UI];
                V = input_V[UI];
                table->getRGB2(Y, input_Y[YI + 1], U, V, output[x >> 3][(y << 3) + (x & 7)],
                               output[(x + 1) >> 3][(y << 3) + ((x + 1) & 7)]);
                ++UI;
                YI += 2;
            }
        }
        break;
    case InputFormat::YUYV422_Interleaved:
        for (unsigned int y = 0; y < height; ++y) {
            for (unsigned int x = 0; x < width; ++x) {
                // Y = input_Y[(y * width + x) * 2];
                // U = input_Y[(y * width + (x / 2) * 2) * 2 + 1];
                // V = input_Y[(y * width + (x / 2) * 2) * 2 + 3];
                UI = ((y * width + x & (~1)) << 1) + 1;
                VI = UI + 2;
                Y = input_Y[YI];
                U = input_U[UI];
                V = input_V[VI];
                YI += 2;
                output[x >> 3][(y << 3) + (x & 7)] = table->getRGB(Y, U, V);
            }
        }
        break;
    }
}

/// Simulates an incoming CDMA transfer. The N parameter is used to automatically convert 16-bit
/// formats to 8-bit.
template <std::size_t N>
static void ReceiveData(Memory::MemorySystem& memory, u8* output, ConversionBuffer& buf,
                        std::size_t amount_of_data) {
    const u8* input = memory.GetPointer(buf.address);

    std::size_t output_unit = buf.transfer_unit / N;
    ASSERT(amount_of_data % output_unit == 0);

    while (amount_of_data > 0) {
        if (N == 1) {
            memmove(output, input, output_unit);
        } else {
            for (std::size_t i = 0; i < output_unit; ++i) {
                output[i] = input[i * N];
            }
        }

        output += output_unit;
        input += buf.transfer_unit + buf.gap;

        buf.address += buf.transfer_unit + buf.gap;
        buf.image_size -= buf.transfer_unit;
        amount_of_data -= output_unit;
    }
}

/// Convert intermediate RGB32 format to the final output format while simulating an outgoing CDMA
/// transfer.
static void SendData(Memory::MemorySystem& memory, const u32* input, ConversionBuffer& buf,
                     int amount_of_data, OutputFormat output_format, u8 alpha) {

    u8* output = memory.GetPointer(buf.address);
    switch (output_format) {
    case OutputFormat::RGBA8:
        while (amount_of_data > 0) {
            u8* unit_end = output + buf.transfer_unit;
            while (output < unit_end) {
                u32 color = *input++;
                Common::Vec4<u8> col_vec{(u8)(color >> 24), (u8)(color >> 16), (u8)(color >> 8),
                                         alpha};

                Color::EncodeRGBA8(col_vec, output);
                output += 4;

                amount_of_data -= 1;
            }

            output += buf.gap;
            buf.address += buf.transfer_unit + buf.gap;
            buf.image_size -= buf.transfer_unit;
        }
        break;
    case OutputFormat::RGB8:
        while (amount_of_data > 0) {
            u8* unit_end = output + buf.transfer_unit;
            while (output < unit_end) {
                u32 color = *input++;
                Common::Vec4<u8> col_vec{(u8)(color >> 24), (u8)(color >> 16), (u8)(color >> 8),
                                         alpha};
                Color::EncodeRGB8(col_vec, output);
                output += 3;

                amount_of_data -= 1;
            }

            output += buf.gap;
            buf.address += buf.transfer_unit + buf.gap;
            buf.image_size -= buf.transfer_unit;
        }
        break;
    case OutputFormat::RGB5A1:
        while (amount_of_data > 0) {
            u8* unit_end = output + buf.transfer_unit;
            while (output < unit_end) {
                u32 color = *input++;
                Common::Vec4<u8> col_vec{(u8)(color >> 24), (u8)(color >> 16), (u8)(color >> 8),
                                         alpha};
                Color::EncodeRGB5A1(col_vec, output);
                output += 2;

                amount_of_data -= 1;
            }

            output += buf.gap;
            buf.address += buf.transfer_unit + buf.gap;
            buf.image_size -= buf.transfer_unit;
        }
        break;
    case OutputFormat::RGB565:
        while (amount_of_data > 0) {
            u8* unit_end = output + buf.transfer_unit;
            while (output < unit_end) {
                u32 color = *input++;
                Common::Vec4<u8> col_vec{(u8)(color >> 24), (u8)(color >> 16), (u8)(color >> 8),
                                         alpha};
                Color::EncodeRGB565(col_vec, output);
                output += 2;

                amount_of_data -= 1;
            }

            output += buf.gap;
            buf.address += buf.transfer_unit + buf.gap;
            buf.image_size -= buf.transfer_unit;
        }
        break;
    }
}

static const u8 linear_lut[TILE_SIZE] = {
    // clang-format off
     0,  1,  2,  3,  4,  5,  6,  7,
     8,  9, 10, 11, 12, 13, 14, 15,
    16, 17, 18, 19, 20, 21, 22, 23,
    24, 25, 26, 27, 28, 29, 30, 31,
    32, 33, 34, 35, 36, 37, 38, 39,
    40, 41, 42, 43, 44, 45, 46, 47,
    48, 49, 50, 51, 52, 53, 54, 55,
    56, 57, 58, 59, 60, 61, 62, 63,
    // clang-format on
};

static const u8 morton_lut[TILE_SIZE] = {
    // clang-format off
     0,  1,  4,  5, 16, 17, 20, 21,
     2,  3,  6,  7, 18, 19, 22, 23,
     8,  9, 12, 13, 24, 25, 28, 29,
    10, 11, 14, 15, 26, 27, 30, 31,
    32, 33, 36, 37, 48, 49, 52, 53,
    34, 35, 38, 39, 50, 51, 54, 55,
    40, 41, 44, 45, 56, 57, 60, 61,
    42, 43, 46, 47, 58, 59, 62, 63,
    // clang-format on
};

static void RotateTile0(const ImageTile& input, ImageTile& output, int height,
                        const u8 out_map[64]) {
    for (int i = 0; i < height * 8; ++i) {
        output[out_map[i]] = input[i];
    }
}

static void RotateTile90(const ImageTile& input, ImageTile& output, int height,
                         const u8 out_map[64]) {
    int out_i = 0;
    for (int x = 0; x < 8; ++x) {
        for (int y = height - 1; y >= 0; --y) {
            output[out_map[out_i++]] = input[y * 8 + x];
        }
    }
}

static void RotateTile180(const ImageTile& input, ImageTile& output, int height,
                          const u8 out_map[64]) {
    int out_i = 0;
    for (int i = height * 8 - 1; i >= 0; --i) {
        output[out_map[out_i++]] = input[i];
    }
}

static void RotateTile270(const ImageTile& input, ImageTile& output, int height,
                          const u8 out_map[64]) {
    int out_i = 0;
    for (int x = 8 - 1; x >= 0; --x) {
        for (int y = 0; y < height; ++y) {
            output[out_map[out_i++]] = input[y * 8 + x];
        }
    }
}

static void WriteTileToOutput(u32* output, const ImageTile& tile, int height, int line_stride) {
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < 8; ++x) {
            output[y * line_stride + x] = tile[y * 8 + x];
        }
    }
}

/**
 * Performs a Y2R colorspace conversion.
 *
 * The Y2R hardware implements hardware-accelerated YUV to RGB colorspace conversions. It is most
 * commonly used for video playback or to display camera input to the screen.
 *
 * The conversion process is quite configurable, and can be divided in distinct steps. From
 * observation, it appears that the hardware buffers a single 8-pixel tall strip of image data
 * internally and converts it in one go before writing to the output and loading the next strip.
 *
 * The steps taken to convert one strip of image data are:
 *
 * - The hardware receives data via CDMA (http://3dbrew.org/wiki/Corelink_DMA_Engines), which is
 *   presumably stored in one or more internal buffers. This process can be done in several separate
 *   transfers, as long as they don't exceed the size of the internal image buffer. This allows
 *   flexibility in input strides.
 * - The input data is decoded into a YUV tuple. Several formats are suported, see the `InputFormat`
 *   enum.
 * - The YUV tuple is converted, using fixed point calculations, to RGB. This step can be configured
 *   using a set of coefficients to support different colorspace standards. See `CoefficientSet`.
 * - The strip can be optionally rotated 90, 180 or 270 degrees. Since each strip is processed
 *   independently, this notably rotates each *strip*, not the entire image. This means that for 90
 *   or 270 degree rotations, the output will be in terms of several 8 x height images, and for any
 *   non-zero rotation the strips will have to be re-arranged so that the parts of the image will
 *   not be shuffled together. This limitation makes this a feature of somewhat dubious utility. 90
 *   or 270 degree rotations in images with non-even height don't seem to work properly.
 * - The data is converted to the output RGB format. See the `OutputFormat` enum.
 * - The data can be output either linearly line-by-line or in the swizzled 8x8 tile format used by
 *   the PICA. This is decided by the `BlockAlignment` enum. If 8x8 alignment is used, then the
 *   image must have a height divisible by 8. The image width must always be divisible by 8.
 * - The final data is then CDMAed out to main memory and the next image strip is processed. This
 *   offers the same flexibility as the input stage.
 *
 * In this implementation, to avoid the combinatorial explosion of parameter combinations, common
 * intermediate formats are used and where possible tables or parameters are used instead of
 * diverging code paths to keep the amount of branches in check. Some steps are also merged to
 * increase efficiency.
 *
 * Output for all valid settings combinations matches hardware, however output in some edge-cases
 * differs:
 *
 * - `Block8x8` alignment with non-mod8 height produces different garbage patterns on the last
 *   strip, especially when combined with rotation.
 * - Hardware, when using `Linear` alignment with a non-even height and 90 or 270 degree rotation
 *   produces misaligned output on the last strip. This implmentation produces output with the
 *   correct "expected" alignment.
 *
 * Hardware behaves strangely (doesn't fire the completion interrupt, for example) in these cases,
 * so they are believed to be invalid configurations anyway.
 */
void PerformConversion(Memory::MemorySystem& memory, ConversionConfiguration& cvt) {
    ASSERT(cvt.input_line_width % 8 == 0);
    ASSERT(cvt.block_alignment != BlockAlignment::Block8x8 || cvt.input_lines % 8 == 0);
    // Tiles per row
    std::size_t num_tiles = cvt.input_line_width >> 3;
    ASSERT(num_tiles <= MAX_TILES);

    // Buffer used as a CDMA source/target.
    std::unique_ptr<u8[]> data_buffer(new u8[cvt.input_line_width << 5]);
    // Intermediate storage for decoded 8x8 image tiles. Always stored as RGB32.
    std::unique_ptr<ImageTile[]> tiles(new ImageTile[num_tiles]);
    ImageTile tmp_tile;

    // LUT used to remap writes to a tile. Used to allow linear or swizzled output without
    // requiring two different code paths.
    const u8* tile_remap = nullptr;
    switch (cvt.block_alignment) {
    case BlockAlignment::Linear:
        tile_remap = linear_lut;
        break;
    case BlockAlignment::Block8x8:
        tile_remap = morton_lut;
        break;
    }

    for (unsigned int y = 0; y < cvt.input_lines; y += 8) {
        unsigned int row_height = std::min(cvt.input_lines - y, 8u);

        // Total size in pixels of incoming data required for this strip.
        const std::size_t row_data_size = row_height * cvt.input_line_width;

        u8* input_Y = data_buffer.get();
        u8* input_U = input_Y + (cvt.input_line_width << 3);
        u8* input_V = input_U + (cvt.input_line_width << 2);

        switch (cvt.input_format) {
        case InputFormat::YUV422_Indiv8:
            ReceiveData<1>(memory, input_Y, cvt.src_Y, row_data_size);
            ReceiveData<1>(memory, input_U, cvt.src_U, row_data_size >> 1);
            ReceiveData<1>(memory, input_V, cvt.src_V, row_data_size >> 1);
            break;
        case InputFormat::YUV420_Indiv8:
            ReceiveData<1>(memory, input_Y, cvt.src_Y, row_data_size);
            ReceiveData<1>(memory, input_U, cvt.src_U, row_data_size >> 2);
            ReceiveData<1>(memory, input_V, cvt.src_V, row_data_size >> 2);
            break;
        case InputFormat::YUV422_Indiv16:
            ReceiveData<2>(memory, input_Y, cvt.src_Y, row_data_size);
            ReceiveData<2>(memory, input_U, cvt.src_U, row_data_size >> 1);
            ReceiveData<2>(memory, input_V, cvt.src_V, row_data_size >> 1);
            break;
        case InputFormat::YUV420_Indiv16:
            ReceiveData<2>(memory, input_Y, cvt.src_Y, row_data_size);
            ReceiveData<2>(memory, input_U, cvt.src_U, row_data_size >> 2);
            ReceiveData<2>(memory, input_V, cvt.src_V, row_data_size >> 2);
            break;
        case InputFormat::YUYV422_Interleaved:
            input_U = nullptr;
            input_V = nullptr;
            ReceiveData<1>(memory, input_Y, cvt.src_YUYV, row_data_size << 1);
            break;
        }

        // Note(yuriks): If additional optimization is required, input_format can be moved to a
        // template parameter, so that its dispatch can be moved to outside the inner loop.
        ConvertYUVToRGB(cvt.input_format, input_Y, input_U, input_V, tiles.get(),
                        cvt.input_line_width, row_height, cvt.coefficient);

        u32* output_buffer = reinterpret_cast<u32*>(data_buffer.get());

        for (std::size_t i = 0; i < num_tiles; ++i) {
            int image_strip_width = 0;
            int output_stride = 0;

            switch (cvt.rotation) {
            case Rotation::None:
                RotateTile0(tiles[i], tmp_tile, row_height, tile_remap);
                image_strip_width = cvt.input_line_width;
                output_stride = 8;
                break;
            case Rotation::Clockwise_90:
                RotateTile90(tiles[i], tmp_tile, row_height, tile_remap);
                image_strip_width = 8;
                output_stride = row_height << 3;
                break;
            case Rotation::Clockwise_180:
                // For 180 and 270 degree rotations we also invert the order of tiles in the strip,
                // since the rotates are done individually on each tile.
                RotateTile180(tiles[num_tiles - i - 1], tmp_tile, row_height, tile_remap);
                image_strip_width = cvt.input_line_width;
                output_stride = 8;
                break;
            case Rotation::Clockwise_270:
                RotateTile270(tiles[num_tiles - i - 1], tmp_tile, row_height, tile_remap);
                image_strip_width = 8;
                output_stride = row_height << 3;
                break;
            }

            switch (cvt.block_alignment) {
            case BlockAlignment::Linear:
                WriteTileToOutput(output_buffer, tmp_tile, row_height, image_strip_width);
                output_buffer += output_stride;
                break;
            case BlockAlignment::Block8x8:
                WriteTileToOutput(output_buffer, tmp_tile, 8, 8);
                output_buffer += TILE_SIZE;
                break;
            }
        }

        // Note(yuriks): If additional optimization is required, output_format can be moved to a
        // template parameter, so that its dispatch can be moved to outside the inner loop.
        SendData(memory, reinterpret_cast<u32*>(data_buffer.get()), cvt.dst, (int)row_data_size,
                 cvt.output_format, (u8)cvt.alpha);
    }
}
} // namespace HW::Y2R
