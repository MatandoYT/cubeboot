#define _LANGUAGE_ASSEMBLY
#include "asm.h"
#include "patch_asm.h"

// patch_inst vNTSC_11(_change_background_color) 0x81481cc8 .4byte 0xFFFF00FF
// patch_inst vNTSC_11(_test_only_a) 0x81301210 trap
// patch_inst vNTSC_11(_test_only_b) 0x81336124 b _addr_81336124
// patch_inst vNTSC_11(_skip_menu_logo) 0x8130d178 li r3, 5
// patch_inst vNTSC_11(_gameselect_hide_cubes) 0x81327454 nop

// patch_inst vNTSC_10(_force_lang) 0x8130b5b4 li r0, 2 // Force English=0, Japanese=2 (NTSC Only)
patch_inst vNTSC_11(_force_lang) 0x8130b73c li r0, 2 // Force English=0, Japanese=2 (NTSC Only)

patch_inst_ntsc "_reduce_arena_size" 0x813328ec 0x8135825c 0x8135d998 0x8135d998 lis r3, -0x7ea0 // 0x81700000 -> 0x81600000
patch_inst_pal "_reduce_arena_size" 0x8135b83c 0x8135817c 0x81360d10 lis r3, -0x7ea0 // 0x81700000 -> 0x81600000

patch_inst_ntsc "_increase_heap_size" 0x81307ed8 0x81307dc0 0x8130815c 0x81308174 lis r3, -0x7fa0 // 0x80700000 -> 0x806000000 (NTSC10 0x80800000 -> 0x80600000)
patch_inst_pal "_increase_heap_size" 0x81307dc0 0x81307dc0 0x81307f28 lis r3, -0x7fa0 // 0x80700000 -> 0x806000000

patch_inst_ntsc "_patch_thread_init" 0x81301234 0x81301070 0x81301424 0x81301428 bl pre_thread_init
patch_inst_pal "_patch_thread_init" 0x81301070 0x81301070 0x813011dc bl pre_thread_init

patch_inst_ntsc "_draw_watermark" 0x81314898 0x81314bb0 0x81314f48 0x81314f60 bl alpha_watermark
patch_inst_pal "_draw_watermark" 0x81315630 0x81314adc 0x81315770 bl alpha_watermark

patch_inst_ntsc "_gameselect_replace_draw" 0x81314200 0x81314518 0x813148b0 0x813148c8 bl mod_gameselect_draw
patch_inst_pal "_gameselect_replace_draw" 0x81314e58 0x81314444 0x81314f98 bl mod_gameselect_draw

patch_inst_ntsc "_gameselect_replace_input" 0x81326818 0x81326f94 0x8132732c 0x81327344 bl handle_gameselect_inputs
patch_inst_pal "_gameselect_replace_input" 0x81327968 0x81326ec0 0x81327aa8 bl handle_gameselect_inputs

.macro routine_gameselect_matrix_helper
    addi r3, r1, 0x74
    addi r4, r1, 0x14
    bl set_gameselect_view
    rept_inst 44 nop
.endm

# NOTE: this is a mid-finction patch
patch_inst_ntsc "_gameselect_draw_helper" 0x81326c14 0x81327430 0x813277c8 0x813277e0 routine_gameselect_matrix_helper
patch_inst_pal "_gameselect_draw_helper" 0x81327e04 0x8132735c 0x81327f44 routine_gameselect_matrix_helper

patch_inst_ntsc "_stub_dvdwait" 0x00000000 0x8130108c 0x81301440 0x81301444 nop
patch_inst_pal  "_stub_dvdwait" 0x8130108c 0x8130108c 0x813011f8 nop

patch_inst_ntsc "_replace_bs2tick" 0x81300a70 0x81300968 0x81300d08 0x81300d0c b bs2tick
patch_inst_pal  "_replace_bs2tick" 0x81300968 0x81300968 0x81300ac0 b bs2tick

patch_inst_ntsc "_replace_bs2start_a" 0x813023e0 0x813021e8 0x81302590 0x813025a8 bl bs2start
patch_inst_pal  "_replace_bs2start_a" 0x813021e8 0x813021e8 0x8130235c bl bs2start

patch_inst_ntsc "_replace_bs2start_b" 0x813024a4 0x813022ac 0x81302648 0x81302660 bl bs2start
patch_inst_pal "_replace_bs2start_b" 0x813022ac 0x813022ac 0x81302414 bl bs2start

patch_inst_ntsc "_replace_report" 0x8133491c 0x8135a344 0x81300520 0x81300520 b custom_OSReport
patch_inst_pal  "_replace_report" 0x8135d924 0x8135a264 0x81300520 b custom_OSReport

patch_inst_ntsc "_patch_menu_init" 0x8130128c 0x81301094 0x81301448 0x8130144c bl pre_menu_init
patch_inst_pal "_patch_menu_init" 0x81301094 0x81301094 0x81301200 bl pre_menu_init

patch_inst_ntsc "_patch_menu_alpha_setup" 0x81312108 0x81312358 0x813126f0 0x81312708 bl pre_menu_alpha_setup
patch_inst_pal "_patch_menu_alpha_setup" 0x81312c3c 0x81312284 0x81312d7c bl pre_menu_alpha_setup

patch_inst_pal "_fix_video_mode_init" 0x81300520 0x81300520 0x81300610 bl get_tvmode

patch_inst_global "_patch_pre_main" 0x81300090 bl pre_main
