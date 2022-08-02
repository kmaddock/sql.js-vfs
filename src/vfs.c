#include <stdint.h>
#include <emscripten.h>
#include <sqlite3.h>
#include <string.h>
#include <assert.h>
#include <stddef.h>
#include <stdio.h>

struct js_file
{
    sqlite3_file b;
    int handle;
};

#define JS_FILE_HANDLE_OFFSET 4

EM_JS(int, get_handle, (sqlite3_file* f),
{
    return getValue(f + 4, 'i32');
})

EM_JS(int, my_Close, (sqlite3_file* f), 
{
    return Module.vfs.fileClose(get_handle(f));
});
EM_ASYNC_JS(int, my_Read, (sqlite3_file* f, uint8_t* buffer, int iAmt, sqlite3_int64 iOfst), 
{
    let b = Module.HEAPU8.subarray(buffer, buffer + iAmt);
    return await Module.vfs.fileRead(get_handle(f), b, iOfst);
});
EM_ASYNC_JS(int, my_Write, (sqlite3_file* f, const uint8_t* buffer, int iAmt, sqlite3_int64 iOfst), 
{
    let b = Module.HEAPU8.subarray(buffer, buffer + iAmt);
    return await Module.vfs.fileWrite(get_handle(f), b, iOfst);
});
EM_ASYNC_JS(int, my_Truncate, (sqlite3_file* f, sqlite3_int64 size), 
{
    return await Module.vfs.fileTruncate(get_handle(f), size);
});
EM_ASYNC_JS(int, my_Sync, (sqlite3_file* f, int flags), 
{
    return await Module.vfs.fileSync(get_handle(f), flags);
});
EM_ASYNC_JS(int, my_FileSize, (sqlite3_file* f, sqlite3_int64 *pSize), 
{
    let [result, size] = await Module.vfs.fileSize(get_handle(f));
    setValue(pSize, size, "i64");
    return result;
});
EM_ASYNC_JS(int, my_Lock, (sqlite3_file* f, int lock), 
{
    return await Module.vfs.fileLock(get_handle(f), lock);
});
EM_ASYNC_JS(int, my_Unlock, (sqlite3_file* f, int lock), 
{
    return await Module.vfs.fileUnlock(get_handle(f), lock);
});
EM_ASYNC_JS(int, my_CheckReservedLock, (sqlite3_file* f, int *pResOut), 
{
    let [result, lock] = await Module.vfs.fileCheckReservedLock(get_handle(f));
    setValue(pResOut, lock, "i32");
    return result;
});

EM_ASYNC_JS(int, my_FileControl, (sqlite3_file* f, int op, void *pArg), 
{
    let arg = null;
    switch (op) {
        case Module.FCNTL_SIZE_HINT:
            arg = getValue(pArg, "i32");
            break;
        case Module.FCNTL_PRAGMA:
            arg = [
                UTF8ToString(getValue(pArg, "i32")),
                UTF8ToString(getValue(pArg + 4, "i32")),
                UTF8ToString(getValue(pArg + 8, "i32")),
            ];
            break;
        default:
            break;
    }
    return await Module.vfs.fileControl(get_handle(f), op, arg);
});
EM_ASYNC_JS(int, my_SectorSize, (sqlite3_file* f),
{
    return await Module.vfs.fileSectorSize(get_handle(f));
});
EM_ASYNC_JS(int, my_DeviceCharacteristics, (sqlite3_file* f),
{
    return await Module.vfs.fileDeviceCharacteristics(get_handle(f));
});
/* Methods above are valid for version 1 */
/*
EM_ASYNC_JS(int, my_ShmMap, (sqlite3_file*, int iPg, int pgsz, int, void volatile**) {});
EM_ASYNC_JS(int, my_ShmLock, (sqlite3_file*, int offset, int n, int flags) {});
EM_ASYNC_JS(void, my_ShmBarrier, (sqlite3_file*) {});
EM_ASYNC_JS(int, my_ShmUnmap, (sqlite3_file*, int deleteFlag) {});
*/
/* Methods above are valid for version 2 */
//EM_ASYNC_JS(int, my_Fetch, (sqlite3_file*, sqlite3_int64 iOfst, int iAmt, void **pp), {});
//EM_ASYNC_JS(int, my_Unfetch, (sqlite3_file*, sqlite3_int64 iOfst, void *p), {});

static sqlite3_io_methods my_io_methods = 
{
    .iVersion = 1,
    .xClose = my_Close,
    .xRead = my_Read,
    .xWrite = my_Write,
    .xTruncate = my_Truncate,
    .xSync = my_Sync,
    .xFileSize = my_FileSize,
    .xLock = my_Lock,
    .xUnlock = my_Unlock,
    .xCheckReservedLock = my_CheckReservedLock,
    .xFileControl = my_FileControl,
    .xSectorSize = my_SectorSize,
    .xDeviceCharacteristics = my_DeviceCharacteristics,
//    .xShmMap = my_ShmMap,
//   .xShmLock = my_ShmLock,
//  .xShmBarrier = my_ShmBarrier,
//    .xShmUnmap = 
    //.xFetch = my_Fetch,
    //.xUnfetch = my_Unfetch,
};

EM_ASYNC_JS(int, my_open2, (const char *zName, int* outHandle, int flags, int *pOutFlags),
{
    let [result, handle, outFlags] = await Module.vfs.open(UTF8ToString(zName), flags);
    setValue(outHandle, handle, "i32");
    setValue(pOutFlags, outFlags, "i32");
    return result;
});

static int my_open(sqlite3_vfs* vfs, const char *zName, sqlite3_file* f,
               int flags, int *pOutFlags)
{
    f->pMethods = &my_io_methods;
    return my_open2(zName, &((struct js_file*)f)->handle, flags, pOutFlags);
}

EM_ASYNC_JS(int, my_delete, (sqlite3_vfs*, const char *zName, int syncDir), {
    return await Module.vfs.delete(UTF8ToString(zName), syncDir);
});

EM_ASYNC_JS(int, my_access, (sqlite3_vfs*, const char *zName, int flags, int *pResOut), {
    let [result, access] = await Module.vfs.access(UTF8ToString(zName), flags);
    setValue(pResOut, access, "i32");
    return result;
});

int my_fullPathName(sqlite3_vfs* vfs, const char *zName, int nOut, char *zOut)
{
    strncpy(zOut, zName, nOut);
    zOut[nOut - 1] = 0;
    return SQLITE_OK;
}

EM_JS(int, my_randomness, (sqlite3_vfs*, int nByte, char *zOut),
{
    let b = Module.HEAPU8.subarray(zOut, zOut + nByte);
    crypto.getRandomValues(b);
    return 0;
});

EM_ASYNC_JS(int, my_sleep, (sqlite3_vfs*, int microseconds), {
    console.log(`sleep(${microseconds})`);
    await new Promise(r => setTimeout(r, microseconds / 1000));
    return 0;
});

EM_JS(int, my_currentTime, (sqlite3_vfs*, double* currentTime), {
    setValue(currentTime, Date.now().getTime(), double);
    return 0;
});

EM_JS(int, my_getLastError, (sqlite3_vfs*, int a, char *str), {
    let [result, message] = Module.vfs.getLastError();

    let messagePtr = allocateUTF8(message);
    _strncpy(str, messagePtr, a);
    _free(messagePtr);

    return result;
});

EM_JS(int, my_currentTimeInt64, (sqlite3_vfs*, sqlite3_int64* currentTime), {
    setValue(currentTime, Date.now().getTime(), i64);
    return 0;
});

static sqlite3_vfs my_vfs =
{
    .iVersion = 3,
    .szOsFile = sizeof(struct js_file),
    .mxPathname = 1024,
    .zName = "CloudFlare DO VFS",
    .xOpen = my_open,
    .xDelete = my_delete,
    .xAccess = my_access,
    .xFullPathname = my_fullPathName,
    .xRandomness = my_randomness,
    .xSleep = my_sleep,
};

int register_my_vfs()
{
    //EM_ASM({ console.log(`Expected offset: ${$0}`); }, offsetof(struct js_file, handle));
    assert(offsetof(struct js_file, handle) == JS_FILE_HANDLE_OFFSET);
    return sqlite3_vfs_register(&my_vfs, 1);
}

int unregister_my_vfs()
{
    return sqlite3_vfs_unregister(&my_vfs);
}
