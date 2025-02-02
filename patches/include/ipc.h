#pragma once
#include <stddef.h>
#include <stdint.h>

#define GCN_ALIGNED(type) type __attribute__((aligned(32)))

//MUST be a multiple of 32 for DMA reasons in the cube
#define MAX_FILE_NAME 256
#define FD_IPC_MAXRESP 1024*16

#define IPC_MAGIC 0xAA55F641
#pragma pack(push,1)

#define IPC_READ_STATUS_RESPONSE_LEN    sizeof(file_status_t)
#define IPC_FILE_READ_RESPONSE_LEN      read_len
#define IPC_FILE_WRITE_RESPONSE_LEN     0
#define IPC_FILE_OPEN_RESPONSE_LEN      0
#define IPC_FILE_CLOSE_RESPONSE_LEN     0
#define IPC_FILE_STAT_RESPONSE_LEN      static_assert(0, "STAT format not yet defined");
#define IPC_FILE_SEEK_RESPONSE_LEN      0
#define IPC_FILE_UNLINK_RESPONSE_LEN    0
#define IPC_FILE_READDIR_RESPONSE_LEN   sizeof(file_entry_t)

#define IPC_RESERVED0_SIZE 204

typedef enum {
    IPC_READ_STATUS        = 0x00,
    IPC_SET_DEFAULT_FD     = 0x01, //Purely 2040

    IPC_FILE_READ          = 0x08,
    IPC_FILE_WRITE         = 0x09,
    IPC_FILE_OPEN          = 0x0A,
    IPC_FILE_CLOSE         = 0x0B,
    IPC_FILE_STAT          = 0x0C,
    IPC_FILE_SEEK          = 0x0D,
    IPC_FILE_UNLINK        = 0x0E,
    IPC_FILE_READDIR       = 0x0F,

    IPC_RESERVED0          = 0x10,
    IPC_FILE_OPEN_FLASH    = 0x11, //Purely 2040
    IPC_FILE_UNLINK_FLASH  = 0x12, //Purely 2040
    IPC_RESERVED3          = 0x13,

    IPC_CMD_MAX            = 0x1F
} ipc_command_type_t;

typedef struct
{
    uint32_t result;
    uint64_t fsize;
    uint8_t fd; //Valid after open
    uint8_t pad[19];
} file_status_t;

typedef struct
{
    uint8_t data[FD_IPC_MAXRESP-32];
} file_payload_t;

enum {
    IPC_FILE_FLAG_DISABLECACHE    = 0x01,
    IPC_FILE_FLAG_DISABLEFASTSEEK = 0x02,
    IPC_FILE_FLAG_DISABLESPEEDEMU = 0x04,
    IPC_FILE_FLAG_WRITE           = 0x08,
};
typedef struct {
    char name[MAX_FILE_NAME];
    uint8_t type;
    uint8_t flags;
    uint64_t size;
    uint32_t date;
    uint32_t time;
    uint32_t last_status;
    uint8_t pad[10];
} file_entry_t;

typedef struct {
    uint32_t magic;
    uint8_t ipc_command_type;
    uint8_t fd;
    uint8_t pad;
    uint8_t subcmd;
    union
    {
        uint8_t shortpayload[8];
        __attribute__((packed)) struct
        {
            uint32_t offset;
            uint32_t length;
        };
    };
} ipc_req_header_t;

typedef struct {
    ipc_req_header_t hdr;
    file_entry_t file;
} ipc_req_open_t;

typedef struct {
    //Setup alignment such that the payload is 32-byte aligned for the GCN's DMA
    ipc_req_header_t hdr;
    uint8_t pad[16];
    file_payload_t payload;
} ipc_req_write_t;

typedef struct
{
    ipc_req_header_t hdr;
    file_entry_t file;
} ipc_req_unlink_t;

enum file_entry_type_enum {
    FILE_ENTRY_TYPE_FILE = 0,
    FILE_ENTRY_TYPE_DIR = 1,

    FILE_ENTRY_TYPE_MAX = 0xFF
};

#pragma pack(pop)

static const size_t ipc_payloadlen[IPC_CMD_MAX] = {
    0, 0, 0, 0, 0, 0, 0, 0, //CMD 0-7
    0, //FILE_READ
    0, //FILE_WRITE
    sizeof(file_entry_t), //FILE_OPEN
    0, //FILE_CLOSE
    0, //FILE_STAT
    0, //FILE_SEEK
    sizeof(file_entry_t), //FILE_UNLINK
    0, //READDIR

    IPC_RESERVED0_SIZE, //RESERVED0
    0, //FILE_OPEN_FLASH is purely internal to RP2040 and has no meaning over IPC
    0, //FILE_UNLINK_FLASH is purely internal
};

