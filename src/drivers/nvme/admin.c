/*
    BloreOS - Operating System
    Copyright (C) 2023 Martin Blore

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/
#include <stdint.h>
#include <stdbool.h>
#include <str.h>

#include "internal.h"

// Retrieve the currently allocated number of I/O queues.
bool nvme_get_number_of_queues(nvme_controller_t *ctrl)
{
    nvme_command_t command = {0};
    nvme_completion_t completion = {0};

    command.opcode = NVME_ADMIN_GET_FEATURES;
    command.cdw10 = NVME_FEATURE_NUM_QUEUES;

    if (!nvme_execute_command(ctrl,
                              &ctrl->admin_queue,
                              &command,
                              &completion)) {
        return false;
    }

    // The returned queue counts are zero-based.
    const uint32_t submission_queues =
        (completion.result & NVME_FEATURE_NSQ_MASK) + 1;

    const uint32_t completion_queues =
        (completion.result >> NVME_FEATURE_NCQ_SHIFT) + 1;

    kprintf("NVME: I/O submission queues: %d\n",
        (int)submission_queues);

    kprintf("NVME: I/O completion queues: %d\n",
        (int)completion_queues);

    return true;
}