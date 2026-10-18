// Minimal GBA hardware definitions
#ifndef GBA_H
#define GBA_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef uint8_t u8;   typedef int8_t s8;
typedef uint16_t u16; typedef int16_t s16;
typedef uint32_t u32; typedef int32_t s32;

#define EWRAM_BSS __attribute__((section(".sbss")))
#define IWRAM_CODE __attribute__((section(".iwram"), long_call, target("arm")))

#define REG16(a) (*(volatile u16 *)(a))
#define REG32(a) (*(volatile u32 *)(a))

#define REG_DISPCNT   REG16(0x04000000)
#define REG_DISPSTAT  REG16(0x04000004)
#define REG_VCOUNT    REG16(0x04000006)
#define REG_BGCNT(n)  REG16(0x04000008 + (n) * 2)
#define REG_BGHOFS(n) REG16(0x04000010 + (n) * 4)
#define REG_BGVOFS(n) REG16(0x04000012 + (n) * 4)
#define REG_WIN0H     REG16(0x04000040)
#define REG_WIN0V     REG16(0x04000044)
#define REG_WININ     REG16(0x04000048)
#define REG_WINOUT    REG16(0x0400004A)
#define REG_BLDCNT    REG16(0x04000050)
#define REG_BLDALPHA  REG16(0x04000052)
#define REG_BLDY      REG16(0x04000054)
#define REG_KEYINPUT  REG16(0x04000130)
#define REG_IE        REG16(0x04000200)
#define REG_IF        REG16(0x04000202)
#define REG_WAITCNT   REG16(0x04000204)
#define REG_IME       REG16(0x04000208)
#define REG_SOUNDCNT_X REG16(0x04000084)

#define DCNT_MODE0  0x0000
#define DCNT_OBJ_1D 0x0040
#define DCNT_BLANK  0x0080
#define DCNT_BG0    0x0100
#define DCNT_BG1    0x0200
#define DCNT_BG2    0x0400
#define DCNT_BG3    0x0800
#define DCNT_OBJ    0x1000
#define DCNT_WIN0   0x2000

#define BG_PRIO(n)  (n)
#define BG_CBB(n)   ((n) << 2)
#define BG_SBB(n)   ((n) << 8)
#define BG_4BPP     0
#define BG_SIZE_256 0x0000
#define BG_SIZE_512 0xC000

#define SE_HFLIP 0x0400
#define SE_VFLIP 0x0800
#define SE_PAL(n) ((n) << 12)

#define VRAM        ((volatile u16 *)0x06000000)
#define CHARBLOCK(n) ((volatile u16 *)(0x06000000 + (n) * 0x4000))
#define SCREENBLOCK(n) ((volatile u16 *)(0x06000000 + (n) * 0x800))
#define OBJ_VRAM    ((volatile u16 *)0x06010000)
#define PAL_BG      ((volatile u16 *)0x05000000)
#define PAL_OBJ     ((volatile u16 *)0x05000200)
#define OAM         ((volatile u16 *)0x07000000)
#define SRAM        ((volatile u8 *)0x0E000000)

#define DSTAT_VBL_IRQ 0x0008
#define IRQ_VBLANK    0x0001

#define KEY_A      0x0001
#define KEY_B      0x0002
#define KEY_SELECT 0x0004
#define KEY_START  0x0008
#define KEY_RIGHT  0x0010
#define KEY_LEFT   0x0020
#define KEY_UP     0x0040
#define KEY_DOWN   0x0080
#define KEY_R      0x0100
#define KEY_L      0x0200
#define KEY_DIR    0x00F0

#define RGB15(r, g, b) ((r) | ((g) << 5) | ((b) << 10))

// OAM attribute helpers
#define ATTR0_HIDE   0x0200
#define ATTR0_SQUARE 0x0000
#define ATTR0_WIDE   0x4000
#define ATTR0_TALL   0x8000
#define ATTR0_BLEND  0x0400
#define ATTR1_HFLIP  0x1000
#define ATTR1_SIZE8  0x0000
#define ATTR1_SIZE16 0x4000
#define ATTR1_SIZE32 0x8000
#define ATTR2_PRIO(n) ((n) << 10)
#define ATTR2_PAL(n)  ((n) << 12)

#ifndef HOST_SIM
static inline void VBlankIntrWait(void) { __asm__ volatile("swi 0x05" ::: "r0", "r1", "r2", "r3", "memory"); }
#else
// Host-side simulator (test/sim): hardware is redirected to plain arrays so the
// game rules (map.c, battle.c, ai.c, ...) compile and run on a PC.
extern u16 host_vram[0x18000 / 2], host_pal[0x400 / 2], host_oam[0x400 / 2], host_io[0x400 / 2];
extern u8 host_sram[0x10000];
#undef EWRAM_BSS
#undef IWRAM_CODE
#undef REG16
#undef REG32
#undef VRAM
#undef CHARBLOCK
#undef SCREENBLOCK
#undef OBJ_VRAM
#undef PAL_BG
#undef PAL_OBJ
#undef OAM
#undef SRAM
#define EWRAM_BSS
#define IWRAM_CODE
#define REG16(a) (*(volatile u16 *)&host_io[((a) & 0x3FF) >> 1])
#define REG32(a) (*(volatile u32 *)&host_io[((a) & 0x3FC) >> 1])
#define VRAM        ((volatile u16 *)host_vram)
#define CHARBLOCK(n) ((volatile u16 *)host_vram + (n) * 0x2000)
#define SCREENBLOCK(n) ((volatile u16 *)host_vram + (n) * 0x400)
#define OBJ_VRAM    ((volatile u16 *)host_vram + 0x8000)
#define PAL_BG      ((volatile u16 *)host_pal)
#define PAL_OBJ     ((volatile u16 *)host_pal + 0x100)
#define OAM         ((volatile u16 *)host_oam)
#define SRAM        ((volatile u8 *)host_sram)
static inline void VBlankIntrWait(void) {}
#endif

#endif
