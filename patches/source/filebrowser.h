#include <gctypes.h>

#include "../../cubeboot/include/bnr.h"

extern int game_backing_count;
extern OSMutex *game_enum_mutex;
// extern game_backing_entry_t *game_backing_list[1000];

typedef struct {
    BNR banner;
    int backing_index;
    int in_use;
    u8 game_id[6];
    u8 padding[96 - (4 * 2) - 6]; // pad to 8kb
} game_asset_t;

void start_file_enum();
void start_main_loop();

bool is_dol_slot(int slot_num);

game_asset_t *claim_game_asset();
void free_game_asset(int backing_index);
game_asset_t *get_game_asset(int backing_index);

const char *get_game_path(int backing_index);
