#pragma once
#include <stdint.h>
#include <stddef.h>
#include <gctypes.h>

#define GCN_ALIGNED(type) type __attribute__((aligned(32)))

//MUST be a multiple of 32 for DMA reasons in the cube
#define MAX_FILE_NAME 256
#define FD_IPC_MAXRESP 1024*16

#define IPC_MAGIC 0xAA55F641
#pragma pack(push,1)

typedef enum {
    IPC_READ_STATUS        = 0x00,
    IPC_SET_ACTIVE         = 0x01,

    IPC_FILE_READ          = 0x08,
    IPC_FILE_WRITE         = 0x09,
    IPC_FILE_OPEN          = 0x0A,
    IPC_FILE_CLOSE         = 0x0B,
    IPC_FILE_STAT          = 0x0C,
    IPC_FILE_SEEK          = 0x0D,
    IPC_FILE_UNLINK        = 0x0E,
    IPC_FILE_READDIR       = 0x0F,

    IPC_CMD_MAX            = 0x1F
} ipc_command_type_t;

typedef enum
{
    IPC_DEVICE_SD    = 0x00,
    IPC_DEVICE_WIFI  = 0x20,
    IPC_DEVICE_FLASH = 0x40,
    IPC_DEVICE_ETH   = 0x60,

    IPC_DEVICE_MAX   = 0xE0
} ipc_device_type_t;

typedef struct
{
    uint32_t result;
    uint64_t fsize;
    int8_t fd; //Valid after open
    uint8_t pad[19];
} file_status_t;

enum {
    IPC_FILE_FLAG_DISABLECACHE    = 0x01,
    IPC_FILE_FLAG_DISABLEFASTSEEK = 0x02,
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
            uint32_t read_offset;
            uint32_t read_length;
        };
    };
} ipc_req_header_t;

typedef struct {
    ipc_req_header_t hdr;
    file_entry_t file;
} ipc_req_open_t;

typedef struct
{
    ipc_req_header_t hdr;
    file_entry_t file;
} ipc_req_unlink_t;

typedef struct {
    ipc_req_header_t hdr;
} ipc_req_read_t;

typedef struct {
    ipc_req_header_t hdr;
} ipc_req_readdir_t;

typedef struct{
    ipc_req_header_t hdr;
} ipc_req_status_t;

typedef struct {
    ipc_req_header_t hdr;
} ipc_req_seek_t;

typedef struct
{
    ipc_req_header_t hdr;
} ipc_req_close_t;

enum file_entry_type_enum {
    FILE_ENTRY_TYPE_FILE = 0,
    FILE_ENTRY_TYPE_DIR = 1,

    FILE_ENTRY_TYPE_MAX = 0xFF
};


static const size_t ipc_payloadlen[IPC_CMD_MAX+1] = {
    0, 0, 0, 0, 0, 0, 0, 0, //CMD 0-7
    0, //FILE_READ
    0, //FILE_WRITE
    sizeof(file_entry_t), //FILE_OPEN
    0, //FILE_CLOSE
    0, //FILE_STAT
    0, //FILE_SEEK
    sizeof(file_entry_t), //FILE_UNLINK
    0, //READDIR
};

#pragma pack(pop)