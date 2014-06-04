// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#pragma once

#include "common/common_types.h"
#include "common/bit_field.h"

namespace GPU {

static const u32 kFrameCycles   = 268123480 / 60;   ///< 268MHz / 60 frames per second
static const u32 kFrameTicks    = kFrameCycles / 3; ///< Approximate number of instructions/frame

struct Registers {
    enum Id : u32 {
        MemoryFillStart1          = 0x1EF00010,
        MemoryFillEnd1            = 0x1EF00014,
        MemoryFillSize1           = 0x1EF00018,
        MemoryFillValue1          = 0x1EF0001C,
        MemoryFillStart2          = 0x1EF00020,
        MemoryFillEnd2            = 0x1EF00024,
        MemoryFillSize2           = 0x1EF00028,
        MemoryFillValue2          = 0x1EF0002C,

        FramebufferTopSize        = 0x1EF0045C,
        FramebufferTopLeft1       = 0x1EF00468,   // Main LCD, first framebuffer for 3D left
        FramebufferTopLeft2       = 0x1EF0046C,   // Main LCD, second framebuffer for 3D left
        FramebufferTopFormat      = 0x1EF00470,
        FramebufferTopSwapBuffers = 0x1EF00478,
        FramebufferTopStride      = 0x1EF00490,   // framebuffer row stride?
        FramebufferTopRight1      = 0x1EF00494,   // Main LCD, first framebuffer for 3D right
        FramebufferTopRight2      = 0x1EF00498,   // Main LCD, second framebuffer for 3D right

        FramebufferSubSize        = 0x1EF0055C,
        FramebufferSubLeft1       = 0x1EF00568,   // Sub LCD, first framebuffer
        FramebufferSubLeft2       = 0x1EF0056C,   // Sub LCD, second framebuffer
        FramebufferSubFormat      = 0x1EF00570,
        FramebufferSubSwapBuffers = 0x1EF00578,
        FramebufferSubStride      = 0x1EF00590,   // framebuffer row stride?
        FramebufferSubRight1      = 0x1EF00594,   // Sub LCD, unused first framebuffer
        FramebufferSubRight2      = 0x1EF00598,   // Sub LCD, unused second framebuffer

        DisplayInputBufferAddr  = 0x1EF00C00,
        DisplayOutputBufferAddr = 0x1EF00C04,
        DisplayOutputBufferSize = 0x1EF00C08,
        DisplayInputBufferSize  = 0x1EF00C0C,
        DisplayTransferFlags    = 0x1EF00C10,
        // Unknown??
        DisplayTriggerTransfer  = 0x1EF00C18,

        CommandListSize         = 0x1EF018E0,
        CommandListAddress      = 0x1EF018E8,
        ProcessCommandList      = 0x1EF018F0,
    };

    enum class FramebufferFormat : u32 {
        RGBA8  = 0,
        RGB8   = 1,
        RGB565 = 2,
        RGB5A1 = 3,
        RGBA4  = 4,
    };

    struct MemoryFillConfig {
        u32 address_start;
        u32 address_end; // ?
        u32 size;
        u32 value; // ?

        inline u32 GetStartAddress() const {
            return address_start * 8;
        }

        inline u32 GetEndAddress() const {
            return address_end * 8;
        }
    };

    MemoryFillConfig memory_fill[2];

    // TODO: Move these into the framebuffer struct
    u32 framebuffer_top_left_1;
    u32 framebuffer_top_left_2;
    u32 framebuffer_top_right_1;
    u32 framebuffer_top_right_2;
    u32 framebuffer_sub_left_1;
    u32 framebuffer_sub_left_2;
    u32 framebuffer_sub_right_1;
    u32 framebuffer_sub_right_2;

    struct FrameBufferConfig {
        union {
            u32 size;

            BitField< 0, 16, u32> width;
            BitField<16, 16, u32> height;
        };

        union {
            u32 format;

            BitField< 0, 3, FramebufferFormat> color_format;
        };

        union {
            u32 active_fb;

            BitField<0, 1, u32> second_fb_active;
        };

        u32 stride;
    };
    FrameBufferConfig top_framebuffer;
    FrameBufferConfig sub_framebuffer;

    struct {
        u32 input_address;
        u32 output_address;

        inline u32 GetPhysicalInputAddress() const {
            return input_address * 8;
        }

        inline u32 GetPhysicalOutputAddress() const {
            return output_address * 8;
        }

        union {
            u32 output_size;

            BitField< 0, 16, u32> output_width;
            BitField<16, 16, u32> output_height;
        };

        union {
            u32 input_size;

            BitField< 0, 16, u32> input_width;
            BitField<16, 16, u32> input_height;
        };

        union {
            u32 flags;

            BitField< 0, 1, u32> flip_data;
            BitField< 8, 3, FramebufferFormat> input_format;
            BitField<12, 3, FramebufferFormat> output_format;
            BitField<16, 1, u32> output_tiled;
        };

        u32 unknown;
        u32 trigger;
    } display_transfer;

    u32 command_list_size;
    u32 command_list_address;
    u32 command_processing_enabled;
};

extern Registers g_regs;

enum {
    TOP_ASPECT_X        = 0x5,
    TOP_ASPECT_Y        = 0x3,

    TOP_HEIGHT          = 240,
    TOP_WIDTH           = 400,
    BOTTOM_WIDTH        = 320,

    // Physical addresses in FCRAM (chosen arbitrarily)
    PADDR_TOP_LEFT_FRAME1       = 0x201D4C00,
    PADDR_TOP_LEFT_FRAME2       = 0x202D4C00,
    PADDR_TOP_RIGHT_FRAME1      = 0x203D4C00,
    PADDR_TOP_RIGHT_FRAME2      = 0x204D4C00,
    PADDR_SUB_FRAME1            = 0x205D4C00,
    PADDR_SUB_FRAME2            = 0x206D4C00,
    // Physical addresses in FCRAM used by ARM9 applications
/*    PADDR_TOP_LEFT_FRAME1       = 0x20184E60,
    PADDR_TOP_LEFT_FRAME2       = 0x201CB370,
    PADDR_TOP_RIGHT_FRAME1      = 0x20282160,
    PADDR_TOP_RIGHT_FRAME2      = 0x202C8670,
    PADDR_SUB_FRAME1            = 0x202118E0,
    PADDR_SUB_FRAME2            = 0x20249CF0,*/

    // Physical addresses in VRAM
    // TODO: These should just be deduced from the ones above
    PADDR_VRAM_TOP_LEFT_FRAME1  = 0x181D4C00,
    PADDR_VRAM_TOP_LEFT_FRAME2  = 0x182D4C00,
    PADDR_VRAM_TOP_RIGHT_FRAME1 = 0x183D4C00,
    PADDR_VRAM_TOP_RIGHT_FRAME2 = 0x184D4C00,
    PADDR_VRAM_SUB_FRAME1       = 0x185D4C00,
    PADDR_VRAM_SUB_FRAME2       = 0x186D4C00,
    // Physical addresses in VRAM used by ARM9 applications
/*    PADDR_VRAM_TOP_LEFT_FRAME2  = 0x181CB370,
    PADDR_VRAM_TOP_RIGHT_FRAME1 = 0x18282160,
    PADDR_VRAM_TOP_RIGHT_FRAME2 = 0x182C8670,
    PADDR_VRAM_SUB_FRAME1       = 0x182118E0,
    PADDR_VRAM_SUB_FRAME2       = 0x18249CF0,*/
};

/// Framebuffer location
enum FramebufferLocation {
    FRAMEBUFFER_LOCATION_UNKNOWN,   ///< Framebuffer location is unknown
    FRAMEBUFFER_LOCATION_FCRAM,     ///< Framebuffer is in the GSP heap
    FRAMEBUFFER_LOCATION_VRAM,      ///< Framebuffer is in VRAM
};

/**
 * Sets whether the framebuffers are in the GSP heap (FCRAM) or VRAM
 * @param 
 */
void SetFramebufferLocation(const FramebufferLocation mode);

/**
 * Gets a read-only pointer to a framebuffer in memory
 * @param address Physical address of framebuffer
 * @return Returns const pointer to raw framebuffer
 */
const u8* GetFramebufferPointer(const u32 address);

/**
 * Gets the location of the framebuffers
 */
const FramebufferLocation GetFramebufferLocation();

template <typename T>
inline void Read(T &var, const u32 addr);

template <typename T>
inline void Write(u32 addr, const T data);

/// Update hardware
void Update();

/// Initialize hardware
void Init();

/// Shutdown hardware
void Shutdown();


} // namespace
