#define _CRT_SECURE_NO_WARNINGS

#define MAP_SIZE 65536

#include "dr_api.h"
#include "drmgr.h"
#include "drreg.h"
#include "drwrap.h"
#include "drx.h"

#ifdef USE_DRSYMS
#include "drsyms.h"
#endif
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "drtable.h"
#include "drvector.h"
#include "hashtable.h"
#include "limits.h"
#include "utils.h"

/*
   WinAFL - DynamoRIO client (instrumentation) code
   ------------------------------------------------

   Origin code Written and maintained by Ivan Fratric <ifratric@google.com>

   WDFuzzer fork written by Jingyi Shi <shijy16@google.com>

   Copyright 2016 Google Inc. All Rights Reserved.

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

*/

//=================================================
//=                                               =
//=                   winafl                      =
//=                                               =
//=================================================

#define MAX_MODULES 16

static HANDLE pipe;
char *modules[MAX_MODULES];
int module_num = 0;
uint last_pc = 0;

typedef struct _module_entry_t {
    app_pc start;
    app_pc end;
} module_entry_t;

unsigned char *afl_area;
static int winafl_tls_field;

static void module_table_entry_free(void *entry) {
    free((module_entry_t *)entry);
    return;
}

#define NUM_THREAD_MODULE_CACHE 1
typedef struct _module_table_t {
    drvector_t vector;
    module_entry_t *cache;
} module_table_t;
static module_table_t *table;

static void setup_modules() {
    table = malloc(sizeof(module_table_t));
    table->cache = NULL;
    drvector_init(&table->vector, 16, false, module_table_entry_free);
}

char ReadCommandFromPipe() {
    DWORD num_read;
    char result;
    ReadFile(pipe, &result, 1, &num_read, NULL);
    return result;
}

void WriteCommandToPipe(char cmd) {
    DWORD num_written;
    WriteFile(pipe, &cmd, 1, &num_written, NULL);
}

void WriteDWORDCommandToPipe(DWORD data) {
    DWORD num_written;
    WriteFile(pipe, &data, sizeof(DWORD), &num_written, NULL);
}

static void setup_pipe() {
    pipe = CreateFile(options.pipe_name,  // pipe name
                      GENERIC_READ |      // read and write access
                          GENERIC_WRITE,
                      0,              // no sharing
                      NULL,           // default security attributes
                      OPEN_EXISTING,  // opens existing pipe
                      0,              // default attributes
                      NULL);          // no template file

    if (pipe == INVALID_HANDLE_VALUE)
        DR_ASSERT_MSG(false, "error connecting to pipe");
}

static void setup_shmem() {
    HANDLE map_file;

    map_file = OpenFileMapping(FILE_MAP_ALL_ACCESS,  // read/write access
                               FALSE,                // do not inherit the name
                               options.shm_name);    // name of mapping object

    if (map_file == NULL) DR_ASSERT_MSG(false, "error accesing shared memory");

    afl_area = (unsigned char *)MapViewOfFile(
        map_file,             // handle to map object
        FILE_MAP_ALL_ACCESS,  // read/write permission
        0, 0, MAP_SIZE);

    if (afl_area == NULL) DR_ASSERT_MSG(false, "error accesing shared memory");
    memset(afl_area, 0, MAP_SIZE);
}

static app_pc lookup_pc(app_pc pc) {
    app_pc ret = -1;
    if (table->cache) {
        if (pc >= table->cache->start && pc <= table->cache->end) {
            return table->cache->start;
        }
    }

    module_entry_t *entry;
    drvector_lock(&table->vector);
    for (int i = table->vector.entries - 1; i >= 0; i--) {
        entry = drvector_get_entry(&table->vector, i);
        ASSERT(entry != NULL, "fail to get module entry");
        if (pc >= entry->start && pc <= entry->end) {
            table->cache = entry;
            ret = entry->start;
            break;
        }
        entry = NULL;
    }
    drvector_unlock(&table->vector);
    return ret;
}

static dr_emit_flags_t instrument_bb_coverage(void *drcontext, void *tag,
                                              instrlist_t *bb, instr_t *inst,
                                              bool for_trace, bool translating,
                                              void *user_data) {
    if (!drmgr_is_first_instr(drcontext, inst)) return DR_EMIT_DEFAULT;
    app_pc start_pc = dr_fragment_app_pc(tag);
    app_pc module_start = lookup_pc(start_pc);
    if (module_start == -1) return DR_EMIT_DEFAULT;

    uint offset = (uint)(start_pc - module_start);
    offset &= MAP_SIZE - 1;
    afl_area[offset] += 1;
    return DR_EMIT_DEFAULT;
}

static dr_emit_flags_t instrument_edge_coverage(void *drcontext, void *tag,
                                                instrlist_t *bb, instr_t *inst,
                                                bool for_trace,
                                                bool translating,
                                                void *user_data) {
    if (!drmgr_is_first_instr(drcontext, inst)) return DR_EMIT_DEFAULT;
    app_pc start_pc = dr_fragment_app_pc(tag);
    app_pc module_start = lookup_pc(start_pc);
    if (module_start == -1) return DR_EMIT_DEFAULT;

    uint offset = (uint)(start_pc - module_start);
    offset &= MAP_SIZE - 1;

    offset = offset ^ last_pc;
    last_pc = (offset >> 1) & (MAP_SIZE - 1);
    afl_area[offset & (MAP_SIZE - 1)] += 1;
    return DR_EMIT_DEFAULT;
}