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
#ifndef NVME_INTERNAL_H
#define NVME_INTERNAL_H

#include <stdint.h>
#include <stdbool.h>
#include <dma.h>

#define NVME_DEBUG 0

#define nvme_log(...)                  \
    do {                               \
        if (NVME_DEBUG) {              \
            kprintf(__VA_ARGS__);      \
        }                              \
    } while (0)

#define NVME_PCI_CLASS     0x01
#define NVME_PCI_SUBCLASS  0x08
#define NVME_PCI_PROG_IF   0x02

#define PCI_BAR_IO_SPACE        (1U << 0)
#define PCI_BAR_TYPE_MASK       (3U << 1)
#define PCI_BAR_TYPE_64BIT      (2U << 1)
#define PCI_BAR_ADDRESS_MASK    0xFFFFFFF0U

#define NVME_REG_CAP 0x00
#define NVME_REG_VS  0x08
#define NVME_REG_CC   0x14
#define NVME_REG_CSTS 0x1C
#define NVME_REG_AQA  0x24
#define NVME_REG_ASQ  0x28
#define NVME_REG_ACQ  0x30
#define NVME_REG_INTMS 0x0C
#define NVME_INTERRUPT_MASK_ALL 0xFFFFFFFFU
#define NVME_REG_DOORBELL_BASE 0x1000
#define NVME_MMIO_PAGE_COUNT  2

#define NVME_DOORBELLS_PER_QUEUE 2
#define NVME_DOORBELL_SQ_TAIL   0
#define NVME_DOORBELL_CQ_HEAD   1

#define NVME_AQA_ACQS_SHIFT     16
#define NVME_VS_MAJOR_SHIFT     16
#define NVME_VS_MINOR_SHIFT      8
#define NVME_VS_COMPONENT_MASK   0xFFU

#define NVME_CAP_MQES_MASK          0xFFFFULL
#define NVME_CAP_TO_SHIFT           24
#define NVME_CAP_TO_MASK            0xFFULL
#define NVME_CAP_TO_UNIT_MS         500
#define NVME_CAP_DSTRD_SHIFT        32
#define NVME_CAP_DSTRD_MASK         0xFULL
#define NVME_DOORBELL_BASE_SIZE     4
#define NVME_CAP_MPSMIN_SHIFT       48
#define NVME_CAP_MPSMAX_SHIFT       52
#define NVME_CAP_MPS_MASK           0xFULL
#define NVME_MPS_BASE_SHIFT         12
#define NVME_CAP_CSS_NVM            (1ULL << 37)

// Controller config and status flags.
#define NVME_CC_EN    (1U << 0)
#define NVME_CSTS_RDY (1U << 0)
#define NVME_CSTS_CFS (1U << 1)

#define NVME_CC_CSS_SHIFT     4
#define NVME_CC_MPS_SHIFT     7
#define NVME_CC_AMS_SHIFT     11
#define NVME_CC_IOSQES_SHIFT  16
#define NVME_CC_IOCQES_SHIFT  20

#define NVME_CC_CSS_NVM         0
#define NVME_CC_MPS_4K          0
#define NVME_CC_AMS_ROUND_ROBIN 0

#define NVME_IOSQES_64_BYTES 6
#define NVME_IOCQES_16_BYTES 4

#define NVME_COMPLETION_PHASE_MASK (1U << 0)
#define NVME_COMMAND_TIMEOUT_MS    5000
#define NVME_COMPLETION_STATUS_MASK 0x0FFEU

#define NVME_ADMIN_GET_FEATURES  0x0A
#define NVME_FEATURE_NUM_QUEUES  0x07
#define NVME_FEATURE_NSQ_MASK    0xFFFFU
#define NVME_FEATURE_NCQ_SHIFT   16

#define NVME_ADMIN_IDENTIFY           0x06
#define NVME_IDENTIFY_DATA_SIZE       4096

#define NVME_IDENTIFY_CNS_NAMESPACE   0x00
#define NVME_IDENTIFY_CNS_CONTROLLER  0x01
#define NVME_IDENTIFY_CNS_ACTIVE_LIST 0x02

// Identify Controller data fields.
#define NVME_IDENTIFY_SN_OFFSET  4
#define NVME_IDENTIFY_SN_LENGTH  20

#define NVME_IDENTIFY_MN_OFFSET  24
#define NVME_IDENTIFY_MN_LENGTH  40

#define NVME_IDENTIFY_FR_OFFSET  64
#define NVME_IDENTIFY_FR_LENGTH  8

#define NVME_IDENTIFY_NN_OFFSET  516

// Identify Namespace data fields.
#define NVME_IDENTIFY_NSZE_OFFSET   0
#define NVME_IDENTIFY_NCAP_OFFSET   8
#define NVME_IDENTIFY_NUSE_OFFSET   16
#define NVME_IDENTIFY_NLBAF_OFFSET  25
#define NVME_IDENTIFY_FLBAS_OFFSET  26
#define NVME_IDENTIFY_FLBAS_MASK    0x0F

#define NVME_IDENTIFY_LBAF_OFFSET  128
#define NVME_IDENTIFY_LBAF_SIZE    4
#define NVME_IDENTIFY_LBADS_OFFSET 2
#define NVME_IDENTIFY_LBADS_MIN    9

#define NVME_IDENTIFY_MS_OFFSET   0
#define NVME_IDENTIFY_DPS_OFFSET  29
#define NVME_IDENTIFY_DPS_PI_MASK 0x07

#define NVME_METADATA_NONE        0
#define NVME_PROTECTION_NONE      0

#define NVME_ADMIN_SET_FEATURES 0x09

#define NVME_IO_QUEUE_PAIR_COUNT    1
#define NVME_IO_QUEUE_ENTRIES       64
#define NVME_IO_QUEUE_ID            1

#define NVME_ADMIN_CREATE_IO_CQ 0x05

// Create I/O queue command fields.
#define NVME_QUEUE_SIZE_SHIFT       16
#define NVME_QUEUE_PHYS_CONTIGUOUS  (1U << 0)

#define NVME_ADMIN_CREATE_IO_SQ 0x01
#define NVME_QUEUE_CQID_SHIFT   16

// NVM I/O commands.
#define NVME_NVM_READ           0x02
#define NVME_NVM_WRITE          0x01
#define NVME_SLBA_HIGH_SHIFT     32

// Represents a single 64-byte NVMe submission queue entry.
typedef struct
{
    uint8_t opcode;
    uint8_t flags;
    uint16_t command_id;

    uint32_t namespace_id;
    uint32_t cdw2;
    uint32_t cdw3;

    uint64_t metadata_ptr;
    uint64_t prp1;
    uint64_t prp2;

    uint32_t cdw10;
    uint32_t cdw11;
    uint32_t cdw12;
    uint32_t cdw13;
    uint32_t cdw14;
    uint32_t cdw15;
} nvme_command_t;

#define NVME_COMMAND_SIZE 64

_Static_assert(
    sizeof(nvme_command_t) == NVME_COMMAND_SIZE,
    "Invalid NVMe command structure size"
);

// Represents a single 16-byte NVMe completion queue entry.
typedef struct
{
    uint32_t result;
    uint32_t cdw1;

    uint16_t sq_head;
    uint16_t sq_id;

    uint16_t command_id;
    uint16_t status;
} nvme_completion_t;

#define NVME_COMPLETION_SIZE 16

_Static_assert(
    sizeof(nvme_completion_t) == NVME_COMPLETION_SIZE,
    "Invalid NVMe completion structure size"
);

// Represents an NVMe submission and completion queue pair.
typedef struct
{
    dma_buffer_t submission;
    dma_buffer_t completion;

    uint16_t queue_id;
    uint16_t entry_count;

    uint16_t sq_tail;
    uint16_t cq_head;

    uint8_t cq_phase;

    uint16_t next_command_id;
    bool failed;
} nvme_queue_t;

#define NVME_ADMIN_QUEUE_ENTRIES 64
#define NVME_ADMIN_QUEUE_ID 0

// Maximum number of namespaces supported per controller.
#define NVME_MAX_NAMESPACES 32

// Represents an identified NVMe storage namespace.
typedef struct
{
    uint32_t id;
    uint32_t block_size;
    uint64_t block_count;
    uint64_t capacity_bytes;

    bool valid;
} nvme_namespace_t;

// Represents an NVMe controller and its runtime state.
typedef struct
{
    void *registers;

    uint32_t max_queue_entries;
    uint32_t timeout_ms;
    uint32_t doorbell_stride;

    nvme_queue_t admin_queue;
    nvme_queue_t io_queue;
    uint32_t namespace_ids[NVME_MAX_NAMESPACES];
    uint32_t namespace_count;
} nvme_controller_t;

// Represents an independent namespace attached to a controller.
typedef struct
{
    nvme_controller_t *controller;
    nvme_namespace_t namespace;
} nvme_device_t;

struct pci_device;

// Controller management.
bool nvme_enable_pci(struct pci_device *dev);
void *nvme_map_registers(struct pci_device *dev);
void nvme_read_version(void *registers);
bool nvme_read_capabilities(nvme_controller_t *ctrl);
bool nvme_reset_controller(nvme_controller_t *ctrl);
bool nvme_configure_admin_queues(nvme_controller_t *ctrl);
bool nvme_enable_controller(nvme_controller_t *ctrl);
void nvme_mask_interrupts(nvme_controller_t *ctrl);

// Queue management.
bool nvme_allocate_queue(
    nvme_queue_t *queue,
    uint16_t queue_id,
    uint16_t entry_count);

bool nvme_execute_command(
    nvme_controller_t *ctrl,
    nvme_queue_t *queue,
    nvme_command_t *command,
    nvme_completion_t *completion);

// Administration commands.
bool nvme_get_number_of_queues(nvme_controller_t *ctrl);
bool nvme_identify_controller(nvme_controller_t *ctrl);
bool nvme_identify_active_namespaces(nvme_controller_t *ctrl);
bool nvme_identify_namespace(nvme_controller_t *ctrl,
                             uint32_t namespace_id,
                            nvme_namespace_t *ns);
bool nvme_set_number_of_queues(nvme_controller_t *ctrl);
bool nvme_create_io_completion_queue(nvme_controller_t *ctrl);
bool nvme_create_io_submission_queue(nvme_controller_t *ctrl);

// NVM I/O operations.
bool nvme_read_block(nvme_controller_t *ctrl,
                     const nvme_namespace_t *ns,
                     uint64_t lba,
                     dma_buffer_t *buffer);

bool nvme_write_block(nvme_controller_t *ctrl,
                      const nvme_namespace_t *ns,
                      uint64_t lba,
                      dma_buffer_t *buffer);

// block_device interface implementation.
bool nvme_block_read(void *context, uint64_t lba, uint32_t count, void *buffer);
bool nvme_block_write(void *context, uint64_t lba, uint32_t count, const void *buffer);

#endif