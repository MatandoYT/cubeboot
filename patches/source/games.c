

// The concept here is that we have a thread which can be started and performs 3 actions in order
// 1. List all of the games in target_dir and sort them by name (only including certain file extensions)
// 2. Enumerate all of the games and evaluate if they are valid by checking their headers
// 3. Load assets for each approved path and store them in the auxiliarly data RAM using DMA

// Note that the thread is protected by a mutex
// Also note that the cube animation will return WAIT until step 3. starts running

// Given that assets are stored in "external memory" we need to be able to load them from the ARAM
// We will use a thread to handle queue requeust and loading the assets in the background

// This is how data is stored for file entries...

#include <gctypes.h>

#include "pmalloc/pmalloc.h"
#include "picolibc.h"
#include "reloc.h"
#include "attr.h"

#include "dolphin_os.h"
#include "dolphin_arq.h"
#include "dolphin_dvd.h"
#include "flippy_sync.h"
#include "dvd_threaded.h"

#include "grid.h"
#include "time.h"

#include "icon.h"
#include "bnr.h"

// Globals
int number_of_lines = 8;
int game_backing_count = 0;

static OSMutex game_enum_mutex_obj;
OSMutex *game_enum_mutex = &game_enum_mutex_obj;

// Backing
typedef enum {
    GM_LOAD_STATE_SETUP, // valid for use
    GM_LOAD_STATE_UNLOADED,
    GM_LOAD_STATE_UNLOADING,
    GM_LOAD_STATE_LOADING,
    GM_LOAD_STATE_LOADED, // valid for use
    GM_LOAD_STATE_FAILED
} gm_load_state_t;

typedef enum {
    GM_FILE_TYPE_UNKNOWN,
    GM_FILE_TYPE_DIRECTORY,
    GM_FILE_TYPE_PROGRAM,
    GM_FILE_TYPE_GAME
} gm_file_type_t;

typedef struct {
    u32 used;
    u8 data[ICON_PIXELDATA_LEN];
} gm_icon_buf_t;

typedef struct {
    u32 used;
    u8 data[BNR_PIXELDATA_LEN];
} gm_banner_buf_t;

typedef struct {
    ARQRequest req;
    u32 aram_offset;
    gm_load_state_t state;
    gm_icon_buf_t *buf;
} gm_icon_t;

typedef struct {
    ARQRequest req;
    u32 aram_offset;
    gm_load_state_t state;
    gm_banner_buf_t *buf;
} gm_banner_t;

typedef struct {
    gm_icon_t icon;
    gm_banner_t banner;
} gm_asset_t;

typedef struct {
    u8 game_id[6];
    u8 dvd_bnr_type;
	u32 dvd_bnr_offset;
} gm_extra_t;

typedef struct {
    char path[128];
    gm_file_type_t type;
} gm_path_entry_t;

typedef struct {
    char path[128];
    BNRDesc desc;
    gm_extra_t extra;
    gm_asset_t asset;
    gm_file_type_t type;
} gm_file_entry_t;

// TODO: use a log2 malloc copy strategy for this
__attribute_data_lowmem__ static gm_path_entry_t __gm_early_path_list[2000];
__attribute_data_lowmem__ static gm_path_entry_t *__gm_sorted_path_list[2000];

static gm_file_entry_t *gm_entry_backing[2000];
static u32 gm_entry_count = 0;

__attribute_data_lowmem__ static gm_icon_buf_t gm_icon_pool[64];
__attribute_data_lowmem__ static gm_banner_buf_t gm_banner_pool[64];

static inline gm_icon_buf_t *gm_get_icon_buf() {
    for (int i = 0; i < 64; i++) {
        if (gm_icon_pool[i].used == 0) {
            gm_icon_pool[i].used = 1;
            return &gm_icon_pool[i];
        }
    }

    return NULL;
}

static inline void gm_free_icon_buf(gm_icon_buf_t *buf) {
    buf->used = 0;
}

static inline gm_banner_buf_t *gm_get_banner_buf() {
    for (int i = 0; i < 64; i++) {
        if (gm_banner_pool[i].used == 0) {
            gm_banner_pool[i].used = 1;
            return &gm_banner_pool[i];
        }
    }

    return NULL;
}

static inline void gm_free_banner_buf(gm_banner_buf_t *buf) {
    buf->used = 0;
}

// TODO: swap the icon/banner callback with req->owner checks
static void arq_icon_callback_setup(u32 arq_request_ptr) {
    ARQRequest *req = (ARQRequest*)arq_request_ptr;
    gm_icon_t *icon = (gm_icon_t*)req;

    icon->state = GM_LOAD_STATE_LOADED;
}

static void arq_icon_callback_unload(u32 arq_request_ptr) {
    ARQRequest *req = (ARQRequest*)arq_request_ptr;
    gm_icon_t *icon = (gm_icon_t*)req;

    icon->state = GM_LOAD_STATE_UNLOADED;
    gm_free_icon_buf(icon->buf);
}

static void arq_icon_callback_load(u32 arq_request_ptr) {
    ARQRequest *req = (ARQRequest*)arq_request_ptr;
    gm_icon_t *icon = (gm_icon_t*)req;

    icon->state = GM_LOAD_STATE_LOADED;
}

static void arq_banner_callback_setup(u32 arq_request_ptr) {
    OSReport("CALLBACK arq_banner_callback_setup\n");
    ARQRequest *req = (ARQRequest*)arq_request_ptr;
    gm_banner_t *banner = (gm_banner_t*)req;

    banner->state = GM_LOAD_STATE_LOADED;
}

static void arq_banner_callback_unload(u32 arq_request_ptr) {
    OSReport("CALLBACK arq_banner_callback_unload\n");
    ARQRequest *req = (ARQRequest*)arq_request_ptr;
    gm_banner_t *banner = (gm_banner_t*)req;

    banner->state = GM_LOAD_STATE_UNLOADED;
    gm_free_banner_buf(banner->buf);
}

static void arq_banner_callback_load(u32 arq_request_ptr) {
    OSReport("CALLBACK arq_banner_callback_load\n");
    ARQRequest *req = (ARQRequest*)arq_request_ptr;
    gm_banner_t *banner = (gm_banner_t*)req;

    banner->state = GM_LOAD_STATE_LOADED;
}

// asset offload helpers
// void gm_icon_setup(gm_icon_t *icon, u32 aram_offset) // init time only
// void gm_icon_unload // init time only
// void gm_icon_load // runtime funcs
// void gm_icon_free // runtime funcs

void gm_banner_setup(gm_banner_t *banner, u32 aram_offset) {
    OSReport("Setting up banner\n");
    banner->aram_offset = aram_offset;
    banner->state = GM_LOAD_STATE_SETUP;

    ARQRequest *req = &banner->req;
    u32 owner = make_type('I', 'C', 'O', 'N');
    u32 type = ARAM_DIR_MRAM_TO_ARAM;
    u32 priority = ARQ_PRIORITY_LOW;
    u32 source = (u32)banner->buf->data;
    u32 dest = aram_offset;
    u32 length = BNR_PIXELDATA_LEN;

    dolphin_ARQPostRequest(req, owner, type, priority, source, dest, length, &arq_banner_callback_setup);
}

void gm_banner_setup_unload(gm_banner_t *banner, u32 aram_offset) {
    banner->aram_offset = aram_offset;
    banner->state = GM_LOAD_STATE_UNLOADING;

    ARQRequest *req = &banner->req;
    u32 owner = make_type('I', 'C', 'O', 'N');
    u32 type = ARAM_DIR_MRAM_TO_ARAM;
    u32 priority = ARQ_PRIORITY_LOW;
    u32 source = (u32)banner->buf->data;
    u32 dest = aram_offset;
    u32 length = BNR_PIXELDATA_LEN;

    dolphin_ARQPostRequest(req, owner, type, priority, source, dest, length, &arq_banner_callback_unload);
}

void gm_banner_load(gm_banner_t *banner) {
    banner->state = GM_LOAD_STATE_LOADING;

    ARQRequest *req = &banner->req;
    u32 owner = make_type('I', 'C', 'O', 'N');
    u32 type = ARAM_DIR_ARAM_TO_MRAM;
    u32 priority = ARQ_PRIORITY_LOW;
    u32 source = banner->aram_offset;
    u32 dest = (u32)banner->buf->data;
    u32 length = BNR_PIXELDATA_LEN;

    dolphin_ARQPostRequest(req, owner, type, priority, source, dest, length, &arq_banner_callback_load);
}

void gm_banner_free(gm_banner_t *banner) {
    banner->state = GM_LOAD_STATE_UNLOADED;
    gm_free_banner_buf(banner->buf);
}

// HEAP
pmalloc_t pmblock;
pmalloc_t *pm = &pmblock;
__attribute_aligned_data_lowmem__ static u8 gm_heap_buffer[2 * 1024 * 1024];

// MACROS
#define gm_malloc(x) pmalloc_memalign(pm, x, 32);
#define gm_free(x) pmalloc_freealign(pm, x);

void gm_init_heap() {
    OSReport("Initializing heap [%x]\n", sizeof(gm_heap_buffer));
    
    // Initialise our pmalloc
	pmalloc_init(pm);
	pmalloc_addblock(pm, &gm_heap_buffer[0], sizeof(gm_heap_buffer));
}

// HELPERS
static char *valid_game_exts[] = {".gcm", ".iso", ".fdi"};
static char *valid_prog_exts[] = {".dol", ".dol+cli"}; // TODO: add .elf
static gm_file_type_t gm_get_file_type(const char *path) {
    const char *ext = strrchr(path, '.');
    if (ext == NULL) return GM_FILE_TYPE_UNKNOWN;

    for (int i = 0; i < sizeof(valid_game_exts) / sizeof(char*); i++) {
        if (strcasecmp(ext, valid_game_exts[i]) == 0) return GM_FILE_TYPE_GAME;
    }

    for (int i = 0; i < sizeof(valid_prog_exts) / sizeof(char*); i++) {
        if (strcasecmp(ext, valid_prog_exts[i]) == 0) return GM_FILE_TYPE_PROGRAM;
    }

    return GM_FILE_TYPE_UNKNOWN;
}

static void ipl_panic() {
    // TODO: set XFB
    OSReport("PANIC: unknown\n");
    while(1);
}

int gm_cmp_path_entry(const void* ptr_a, const void* ptr_b){
    const gm_path_entry_t *obj_a = *(gm_path_entry_t**)ptr_a;
    const gm_path_entry_t *obj_b = *(gm_path_entry_t**)ptr_b;

    return strcasecmp(obj_a->path, obj_b->path);
}

// DEFS
typedef struct {
    int num_paths;
} gm_list_info;

// 1. func named gm_list_files returns {pointer to path array, number of paths}
// 2. func named gm_check_headers returns {pointer to valid path array, number of valid paths}
// 3. func named gm_load_assets returns {void}

// Before the next go a dealloc function is called to free all of the memory before the thread is started again

gm_list_info gm_list_files(const char *target_dir) {
    // List all of the games in target_dir and sort them by name (only including certain file extensions)
    OSReport("Listing files in %s\n", target_dir);
    u64 start_time = gettime();

    int res = dvd_custom_open("/", FILE_ENTRY_TYPE_DIR, 0);
    if (res != 0) {
        OSReport("PANIC: SD Card could not be opened\n");
        while(1);
    }

    // TODO: if the SD card is not inserted this would be a good place to bail out 
    file_status_t *status = dvd_custom_status();
    if (status->result != 0) {
        OSReport("PANIC: SD Card could not be opened\n");
        while(1);
    }

    uint8_t dir_fd = status->fd;
    OSReport("found readdir fd=%u\n", dir_fd);

    // now list everything
    static GCN_ALIGNED(file_entry_t) ent;
    int path_entry_count = 0;
    char file_full_path_buf[128] = {0};

    // TODO: switch to using DVD Mutex (this is all happening in a thread)
    while(1) {
        int ret = dvd_custom_readdir(&ent, dir_fd);
        if (ret != 0) ipl_panic();

        if (ent.name[0] == 0) break; // end of directory
        if (ent.type == FILE_ENTRY_TYPE_DIR) continue; // TODO: add DIR listing

        // only check file ext for now
        gm_file_type_t file_type = GM_FILE_TYPE_UNKNOWN;
        if (ent.type == FILE_ENTRY_TYPE_DIR) {
            file_type = GM_FILE_TYPE_DIRECTORY;
        } else {
            file_type = gm_get_file_type(ent.name);
        }

        if (file_type == GM_FILE_TYPE_UNKNOWN) continue; // check if the file is valid

#ifdef PRINT_READDIR_NAMES
        // logging
        OSReport("READDIR ent(%u): %s [len=%d]\n", ent.type, ent.name, strlen(ent.name));
#endif

        // combine the path
        strcpy(file_full_path_buf, target_dir);
        strcat(file_full_path_buf, ent.name);

        // store the path
        gm_path_entry_t *entry = &__gm_early_path_list[path_entry_count];
        strcpy(entry->path, file_full_path_buf);
        entry->type = file_type;

        // setup sort list
        __gm_sorted_path_list[path_entry_count] = entry;
        path_entry_count++;

        if (path_entry_count >= 2000) {
            OSReport("WARNING: Too many files in directory\n");
            break;
        }
    }

    f32 runtime = (f32)diff_usec(start_time, gettime()) / 1000.0;
    OSReport("File enum completed! took=%f (%d)\n", runtime, game_backing_count);

    return (gm_list_info){path_entry_count};
}

void gm_sort_files(int path_count) {
    // Sort the paths by name
    u64 start_time = gettime();
    qsort(__gm_sorted_path_list, path_count, sizeof(gm_path_entry_t*), &gm_cmp_path_entry);

    f32 runtime = (f32)diff_usec(start_time, gettime()) / 1000.0;
    OSReport("Sort took=%f\n", runtime);
    (void)runtime;
}

void gm_check_headers(int path_count) {
    // Here we will also check for override assets and matching save icons

    // Enumerate all of the games
    u64 start_time = gettime();
    for (int i = 0; i < path_count; i++) {
        gm_path_entry_t *entry = __gm_sorted_path_list[i];
        // OSReport("Checking header %s [%d]\n", entry->path, entry->type);

        if (entry->type == GM_FILE_TYPE_GAME) {
            // check if the banner file exists
            dolphin_game_into_t info = get_game_info(entry->path);
            if (!info.valid) continue;
            OSReport("Found game %s\n", entry->path); // lets do this!

            // create a new entry
            gm_file_entry_t *backing = gm_malloc(sizeof(gm_file_entry_t));
            memcpy(backing->path, entry->path, sizeof(backing->path));
            backing->type = GM_FILE_TYPE_GAME;

            // copy the extra info
            memcpy(backing->extra.game_id, info.game_id, sizeof(backing->extra.game_id));
            backing->extra.dvd_bnr_offset = info.bnr_offset;
            backing->extra.dvd_bnr_type = info.bnr_type;

            // set heap pointer
            gm_entry_backing[gm_entry_count] = backing;
            gm_entry_count++;
        } else if (entry->type == GM_FILE_TYPE_PROGRAM || entry->type == GM_FILE_TYPE_DIRECTORY) {
            OSReport("Found other %s\n", entry->path); // lets do this!

            // create a new entry
            gm_file_entry_t *backing = gm_malloc(sizeof(gm_file_entry_t));
            memcpy(backing->path, entry->path, sizeof(backing->path));
            backing->type = entry->type;

            // set heap pointer
            gm_entry_backing[gm_entry_count] = backing;
            gm_entry_count++;
        }
    }

    OSReport("Total entries = %d\n", gm_entry_count);

    f32 runtime = (f32)diff_usec(start_time, gettime()) / 1000.0;
    OSReport("Header check took=%f\n", runtime);
    (void)runtime;
}

// returns amount of space used in aram
static int gm_load_banner(gm_file_entry_t *entry, u32 aram_offset, bool force_unload) {
    if (entry->extra.dvd_bnr_offset == 0) return false;

    // load the banner
    dvd_custom_open(entry->path, FILE_ENTRY_TYPE_FILE, IPC_FILE_FLAG_DISABLECACHE | IPC_FILE_FLAG_DISABLEFASTSEEK);
    file_status_t *status = dvd_custom_status();
    if (status == NULL || status->result != 0) {
        OSReport("ERROR: could not open file\n");
        return false;
    }

    __attribute_aligned_data__ static BNR banner_buffer;
    dvd_threaded_read(&banner_buffer, sizeof(BNR), entry->extra.dvd_bnr_offset, status->fd);
    dvd_custom_close(status->fd);

    entry->asset.banner.state = GM_LOAD_STATE_LOADING;
    void *banner_ptr = gm_get_banner_buf();
    if (banner_ptr == NULL) {
        OSReport("ERROR: could not allocate memory\n");
        return false;
    }

    memcpy(banner_ptr, &banner_buffer.pixelData[0], BNR_PIXELDATA_LEN);
    entry->asset.banner.buf = banner_ptr;
    if (force_unload) {
        gm_banner_setup_unload(&entry->asset.banner, aram_offset);
    } else {
        gm_banner_setup(&entry->asset.banner, aram_offset);
    }

    // TODO: check current language using extra.dvd_bnr_type
    memcpy(&entry->desc, &banner_buffer.desc[0], sizeof(BNRDesc));

    return true;
}

// returns amount of space used in aram
static bool gm_load_icon(gm_file_entry_t *entry, u32 aram_offset, bool force_unload) {
    return false;
}

void gm_load_assets() {
    u32 aram_offset = (1 * 1024 * 1024); // 1MB mark

    for (int i = 0; i < gm_entry_count; i++) {
        gm_file_entry_t *entry = gm_entry_backing[i];
        OSReport("[%d] Loading assets %s (%d)\n", i, entry->path, entry->type);

        bool force_unload = false;
        if (i >= 48) {
            force_unload = true;
        }

        // Load assets for each approved path and store them in the auxiliarly data RAM using DMA
        if (entry->type == GM_FILE_TYPE_GAME) {
            // load the banner
            bool bnr_loaded = gm_load_banner(entry, aram_offset, force_unload);
            if (!bnr_loaded) {
                OSReport("Failed to load banner %s\n", entry->path);
                continue;
            }

            // // load the icon
            // bool icon_loaded = gm_load_icon(entry, aram_offset, force_unload);
            // if (!icon_loaded) {
            //     OSReport("Failed to load icon %s\n", entry->path);
            //     continue;
            // }
        } else {
            // // load the icon
            // bool icon_loaded = gm_load_icon(entry, aram_offset, force_unload);
            // if (!icon_loaded) {
            //     OSReport("Failed to load icon %s\n", entry->path);
            //     continue;
            // }
        }
    }
}

static bool first = true;
void gm_start_thread() {
    if (first) {
        gm_init_heap();
        first = false;
    }

    // Start the thread
    // pass

    gm_list_info list_info = gm_list_files("/");
    gm_sort_files(list_info.num_paths);
    gm_check_headers(list_info.num_paths);
    gm_load_assets();
    grid_setup_func();
}

// TODO: thread

// // match https://github.com/projectPiki/pikmin2/blob/snakecrowstate-work/include/Dolphin/OS/OSThread.h#L55-L74
// static u8 thread_obj[0x310];
// static u8 thread_stack[32 * 1024];
// void start_file_enum() {
//     u32 thread_stack_size = sizeof(thread_stack);
//     void *thread_stack_top = thread_stack + thread_stack_size;
//     s32 thread_priority = DEFAULT_THREAD_PRIO + 3;

//     OSInitMutex(game_enum_mutex);

//     dolphin_OSCreateThread(&thread_obj[0], file_enum_worker, 0, thread_stack_top, thread_stack_size, thread_priority, 0);
//     dolphin_OSResumeThread(&thread_obj[0]);
// }
