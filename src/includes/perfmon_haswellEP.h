/*
 * =======================================================================================
 *
 *      Filename:  perfmon_haswell.h
 *
 *      Description:  Header File of perfmon module for Haswell.
 *
 *      Version:   <VERSION>
 *      Released:  <DATE>
 *
 *      Author:  Jan Treibig (jt), jan.treibig@gmail.com
 *      Project:  likwid
 *
 *      Copyright (C) 2013 Jan Treibig
 *
 *      This program is free software: you can redistribute it and/or modify it under
 *      the terms of the GNU General Public License as published by the Free Software
 *      Foundation, either version 3 of the License, or (at your option) any later
 *      version.
 *
 *      This program is distributed in the hope that it will be useful, but WITHOUT ANY
 *      WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 *      PARTICULAR PURPOSE.  See the GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License along with
 *      this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * =======================================================================================
 */

#include <perfmon_haswellEP_events.h>
#include <perfmon_haswell_events.h>
#include <perfmon_haswellEP_counters.h>
#include <perfmon_haswell_counters.h>
#include <error.h>
#include <affinity.h>
#include <limits.h>
#include <topology.h>


static int perfmon_numCountersHaswellEP = NUM_COUNTERS_HASWELL_EP;
static int perfmon_numCoreCountersHaswellEP = NUM_COUNTERS_CORE_HASWELL_EP;
static int perfmon_numArchEventsHaswellEP = NUM_ARCH_EVENTS_HASWELLEP;
static int perfmon_numCountersHaswell = NUM_COUNTERS_HASWELL;
static int perfmon_numCoreCountersHaswell = NUM_COUNTERS_CORE_HASWELL;
static int perfmon_numArchEventsHaswell = NUM_ARCH_EVENTS_HASWELL;

#define GET_READFD(cpu_id) \
    int read_fd = socket_fd; \
    if (socket_fd == -1 || thread_sockets[cpu_id] != -1) \
    { \
        read_fd = thread_sockets[cpu_id]; \
    } \
    if (read_fd == -1) \
    { \
        return -ENOENT; \
    } \

int perfmon_init_haswellEP(int cpu_id)
{
    uint64_t flags = 0x0ULL;
    GET_READFD(cpu_id);
    if ( cpuid_info.model == HASWELL_EP )
    {
        lock_acquire((int*) &socket_lock[affinity_core2node_lookup[cpu_id]], cpu_id);
    }
    CHECK_MSR_WRITE_ERROR(msr_twrite(read_fd, cpu_id, MSR_PERF_GLOBAL_OVF_CTRL, 0x0ULL));
    CHECK_MSR_WRITE_ERROR(msr_twrite(read_fd, cpu_id, MSR_PEBS_ENABLE, 0x0ULL));
    return 0;
}


uint32_t hasep_fixed_setup(RegisterIndex index, PerfmonEvent *event)
{
    int j;
    uint32_t flags = (0x2 << (4*index));
    for(j=0;j<event->numberOfOptions;j++)
    {
        switch (event->options[j].type)
        {
            case EVENT_OPTION_COUNT_KERNEL:
                flags |= (1ULL<<(index*4));
                break;
            case EVENT_OPTION_ANYTHREAD:
                flags |= (1ULL<<(2+(index*4)));
            default:
                break;
        }
    }
    return flags;
}

int hasep_pmc_setup(int cpu_id, RegisterIndex index, PerfmonEvent *event)
{
    int j;
    uint64_t flags = 0x0ULL;
    GET_READFD(cpu_id);

    flags = (1ULL<<22)|(1ULL<<16);
    /* Intel with standard 8 bit event mask: [7:0] */
    flags |= (event->umask<<8) + event->eventId;

    if (event->cfgBits != 0) /* set custom cfg and cmask */
    {
        flags |= ((event->cmask<<8) + event->cfgBits)<<16;
    }

    if (event->numberOfOptions > 0)
    {
        for(int j=0;j<event->numberOfOptions;j++)
        {
            switch (event->options[j].type)
            {
                case EVENT_OPTION_EDGE:
                    flags |= (1ULL<<18);
                    break;
                case EVENT_OPTION_COUNT_KERNEL:
                    flags |= (1ULL<<17);
                    break;
                case EVENT_OPTION_INVERT:
                    flags |= (1ULL<<23);
                    break;
                case EVENT_OPTION_ANYTHREAD:
                    flags |= (1ULL<<21);
                    break;
                case EVENT_OPTION_IN_TRANS:
                    flags |= (1ULL<<32);
                    break;
                case EVENT_OPTION_IN_TRANS_ABORT:
                    flags |= (1ULL<<33);
                    break;
                default:
                    break;
            }
        }
    }
    VERBOSEPRINTREG(cpu_id, counter_map[index].configRegister, LLU_CAST flags, SETUP_PMC)
    CHECK_MSR_WRITE_ERROR(msr_twrite(read_fd, cpu_id, counter_map[index].configRegister , flags));
    return 0;
}

int hasep_cbox_setup(int cpu_id, RegisterIndex index, PerfmonEvent *event)
{
    int j;
    uint64_t flags = 0x0ULL;
    uint64_t filter_flags;
    uint32_t filter0 = box_map[counter_map[index].type].filterRegister1;
    uint32_t filter1 = box_map[counter_map[index].type].filterRegister2;
    int set_state_all = 0;
    GET_READFD(cpu_id);

    if ((socket_lock[affinity_core2node_lookup[cpu_id]] != cpu_id))
    {
        return 0;
    }

    flags = (1ULL<<22);
    flags |= (event->umask<<8) + event->eventId;
    if (event->eventId == 0x34)
    {
        set_state_all = 1;
    }
    if (event->numberOfOptions > 0)
    {
        CHECK_MSR_WRITE_ERROR(msr_twrite(read_fd, cpu_id, filter0, 0x0ULL));
        CHECK_MSR_WRITE_ERROR(msr_twrite(read_fd, cpu_id, filter1, 0x0ULL));
        for(int j=0;j<event->numberOfOptions;j++)
        {
            filter_flags = 0x0ULL;
            switch (event->options[j].type)
            {
                case EVENT_OPTION_EDGE:
                    flags |= (1ULL<<18);
                    break;
                case EVENT_OPTION_INVERT:
                    flags |= (1ULL<<23);
                    break;
                case EVENT_OPTION_THRESHOLD:
                    flags |= (event->options[j].value<<24);
                    break;
                case EVENT_OPTION_OPCODE:
                    CHECK_MSR_READ_ERROR(msr_tread(read_fd, cpu_id, filter1, &filter_flags));
                    filter_flags |= (0x3<<27);
                    filter_flags |= (extractBitField(event->options[j].value,5,0) << 20);
                    VERBOSEPRINTREG(cpu_id, filter1, filter_flags, SETUP_CBOX_FILTER_OPCODE);
                    CHECK_MSR_WRITE_ERROR(msr_twrite(read_fd, cpu_id, filter1, filter_flags));
                    break;
                case EVENT_OPTION_NID:
                    CHECK_MSR_READ_ERROR(msr_tread(read_fd, cpu_id, filter1, &filter_flags));
                    filter_flags |= (extractBitField(event->options[j].value,16,0));
                    VERBOSEPRINTREG(cpu_id, filter1, filter_flags, SETUP_CBOX_FILTER_NID);
                    CHECK_MSR_WRITE_ERROR(msr_twrite(read_fd, cpu_id, filter1, filter_flags));
                    break;
                case EVENT_OPTION_STATE:
                    CHECK_MSR_READ_ERROR(msr_tread(read_fd, cpu_id, filter0, &filter_flags));
                    filter_flags |= (extractBitField(event->options[j].value,6,0) << 17);
                    VERBOSEPRINTREG(cpu_id, filter0, filter_flags, SETUP_CBOX_FILTER_STATE);
                    CHECK_MSR_WRITE_ERROR(msr_twrite(read_fd, cpu_id, filter0, filter_flags));
                    set_state_all = 0;
                    break;
                case EVENT_OPTION_TID:
                    CHECK_MSR_READ_ERROR(msr_tread(read_fd, cpu_id, filter0, &filter_flags));
                    filter_flags |= (extractBitField(event->options[j].value,6,0));
                    VERBOSEPRINTREG(cpu_id, filter0, filter_flags, SETUP_CBOX_FILTER_TID);
                    CHECK_MSR_WRITE_ERROR(msr_twrite(read_fd, cpu_id, filter0, filter_flags));
                    flags |= (1ULL<<19);
                    break;
                default:
                    break;
            }
        }
    }
    else
    {
        CHECK_MSR_WRITE_ERROR(msr_twrite(read_fd, cpu_id, filter0, 0x0ULL));
        CHECK_MSR_WRITE_ERROR(msr_twrite(read_fd, cpu_id, filter1, 0x0ULL));
    }
    if (set_state_all)
    {
        CHECK_MSR_READ_ERROR(msr_tread(read_fd, cpu_id, filter0, &filter_flags));
        filter_flags |= (0x1F << 17);
        VERBOSEPRINTREG(cpu_id, filter0, filter_flags, SETUP_CBOX_DEF_FILTER_STATE);
        CHECK_MSR_WRITE_ERROR(msr_twrite(read_fd, cpu_id, filter0, filter_flags));
    }
    VERBOSEPRINTREG(cpu_id, counter_map[index].configRegister, flags, SETUP_CBOX);
    CHECK_MSR_WRITE_ERROR(msr_twrite(read_fd, cpu_id, counter_map[index].configRegister, flags));
    return 0;
}

int hasep_ubox_setup(int cpu_id, RegisterIndex index, PerfmonEvent *event)
{
    uint64_t flags = 0x0ULL;
    GET_READFD(cpu_id);

    if ((socket_lock[affinity_core2node_lookup[cpu_id]] != cpu_id))
    {
        return 0;
    }

    flags = (1ULL<<22)|(1ULL<<20);
    flags |= (event->umask<<8) + event->eventId;
    if (event->numberOfOptions > 0)
    {
        for(int j=0;j<event->numberOfOptions;j++)
        {
            switch (event->options[j].type)
            {
                case EVENT_OPTION_EDGE:
                    flags |= (1ULL<<18);
                    break;
                case EVENT_OPTION_INVERT:
                    flags |= (1ULL<<23);
                    break;
                case EVENT_OPTION_THRESHOLD:
                    flags |= (extractBitField(event->options[j].value,5,0)<<24);
                    break;
            }
        }
    }
    VERBOSEPRINTREG(cpu_id, counter_map[index].configRegister, flags, SETUP_UBOX);
    CHECK_MSR_WRITE_ERROR(msr_twrite(read_fd, cpu_id, counter_map[index].configRegister, flags));
    return 0;
}

int hasep_wbox_setup(int cpu_id, RegisterIndex index, PerfmonEvent *event)
{
    uint64_t flags = 0x0ULL;
    uint64_t filter = box_map[counter_map[index].type].filterRegister1;
    GET_READFD(cpu_id);

    if ((socket_lock[affinity_core2node_lookup[cpu_id]] != cpu_id))
    {
        return 0;
    }

    flags = (1ULL<<22)|(1ULL<<20);
    flags |= event->eventId;
    if ((event->umask > 0x00) && (event->umask <= 0x3))
    {
        flags |= (event->umask << 14);
    }
    else if (event->umask == 0xFF)
    {
        flags = (1ULL<<21);
    }
    if (event->numberOfOptions > 0)
    {
        for(int j=0;j<event->numberOfOptions;j++)
        {
            switch (event->options[j].type)
            {
                case EVENT_OPTION_EDGE:
                    flags |= (1ULL<<18);
                    break;
                case EVENT_OPTION_INVERT:
                    flags |= (1ULL<<23);
                    break;
                case EVENT_OPTION_THRESHOLD:
                    flags |= (extractBitField(event->options[j].value,5,0)<<24);
                    break;
                case EVENT_OPTION_OCCUPANCY:
                    flags |= (extractBitField(event->options[j].value,2,0)<<14);
                    break;
                case EVENT_OPTION_OCCUPANCY_FILTER:
                    VERBOSEPRINTREG(cpu_id, filter, extractBitField(event->options[j].value,32,0), SETUP_WBOX_FILTER);
                    CHECK_MSR_WRITE_ERROR(msr_twrite(read_fd, cpu_id, filter, extractBitField(event->options[j].value,32,0)));
                    break;
                case EVENT_OPTION_OCCUPANCY_EDGE:
                    flags |= (1ULL<<31);
                    break;
                case EVENT_OPTION_OCCUPANCY_INVERT:
                    flags |= (1ULL<<30);
                    break;
            }
        }
    }
    VERBOSEPRINTREG(cpu_id, counter_map[index].configRegister, flags, SETUP_WBOX);
    CHECK_MSR_WRITE_ERROR(msr_twrite(read_fd, cpu_id, counter_map[index].configRegister, flags));
    return 0;
}


int hasep_bbox_setup(int cpu_id, RegisterIndex index, PerfmonEvent *event)
{
    uint64_t flags = 0x0ULL;
    int opcode_flag = 0;
    int match0_flag = 0;
    int match1_flag = 0;
    PciDeviceIndex dev = counter_map[index].device;
    GET_READFD(cpu_id);

    if ((socket_lock[affinity_core2node_lookup[cpu_id]] != cpu_id))
    {
        return 0;
    }
    if (!pci_checkDevice(dev, cpu_id))
    {
        return -ENODEV;
    }

    flags = (1ULL<<22)|(1ULL<<20);
    flags |= (event->umask<<8) + event->eventId;
    if (event->numberOfOptions > 0)
    {
        for(int j=0;j<event->numberOfOptions;j++)
        {
            switch (event->options[j].type)
            {
                case EVENT_OPTION_EDGE:
                    flags |= (1ULL<<18);
                    break;
                case EVENT_OPTION_INVERT:
                    flags |= (1ULL<<23);
                    break;
                case EVENT_OPTION_THRESHOLD:
                    flags |= (extractBitField(event->options[j].value,8,0)<<24);
                    break;
                case EVENT_OPTION_OPCODE:
                    VERBOSEPRINTPCIREG(cpu_id, dev, PCI_UNC_HA_PMON_OPCODEMATCH,
                                        extractBitField(event->options[j].value,6,0), SETUP_BBOX_OPCODE);
                    CHECK_PCI_WRITE_ERROR(pci_twrite(read_fd, cpu_id, dev, PCI_UNC_HA_PMON_OPCODEMATCH,
                                        extractBitField(event->options[j].value,6,0)));
                    opcode_flag = 1;
                    break;
                case EVENT_OPTION_MATCH0:
                    VERBOSEPRINTPCIREG(cpu_id, dev, PCI_UNC_HA_PMON_ADDRMATCH0,
                                        (extractBitField(event->options[j].value,26,0)<<6), SETUP_BBOX_MATCH0);
                    CHECK_PCI_WRITE_ERROR(pci_twrite(read_fd, cpu_id, dev, PCI_UNC_HA_PMON_ADDRMATCH0,
                                        (extractBitField(event->options[j].value,26,0)<<6)));
                    match0_flag = 1;
                    break;
                case EVENT_OPTION_MATCH1:
                    VERBOSEPRINTPCIREG(cpu_id, dev, PCI_UNC_HA_PMON_ADDRMATCH1,
                                        extractBitField(event->options[j].value,13,0), SETUP_BBOX_MATCH1);
                    CHECK_PCI_WRITE_ERROR(pci_twrite(read_fd, cpu_id, dev, PCI_UNC_HA_PMON_ADDRMATCH1,
                                        extractBitField(event->options[j].value,13,0)<<6));
                    match1_flag = 1;
                    break;
            }
        }
    }
    if (!opcode_flag)
    {
        VERBOSEPRINTPCIREG(cpu_id, dev, PCI_UNC_HA_PMON_OPCODEMATCH, 0x0ULL, CLEAR_BBOX_OPCODE);
        CHECK_PCI_WRITE_ERROR(pci_twrite(read_fd, cpu_id, dev, PCI_UNC_HA_PMON_OPCODEMATCH, 0x0ULL));
    }
    if (!match0_flag)
    {
        VERBOSEPRINTPCIREG(cpu_id, dev, PCI_UNC_HA_PMON_ADDRMATCH0, 0x0ULL, CLEAR_BBOX_MATCH0);
        CHECK_PCI_WRITE_ERROR(pci_twrite(read_fd, cpu_id, dev, PCI_UNC_HA_PMON_ADDRMATCH0, 0x0ULL));
    }
    if (!opcode_flag)
    {
        VERBOSEPRINTPCIREG(cpu_id, dev, PCI_UNC_HA_PMON_ADDRMATCH1, 0x0ULL, CLEAR_BBOX_MATCH1);
        CHECK_PCI_WRITE_ERROR(pci_twrite(read_fd, cpu_id, dev, PCI_UNC_HA_PMON_ADDRMATCH1, 0x0ULL));
    }
    VERBOSEPRINTPCIREG(cpu_id, dev, counter_map[index].configRegister, flags, SETUP_BBOX);
    CHECK_PCI_WRITE_ERROR(pci_twrite(read_fd, cpu_id, dev, counter_map[index].configRegister, flags));
    return 0;
}

int hasep_sbox_setup(int cpu_id, RegisterIndex index, PerfmonEvent *event)
{
    uint64_t flags = 0x0ULL;
    GET_READFD(cpu_id);

    if ((socket_lock[affinity_core2node_lookup[cpu_id]] != cpu_id))
    {
        return 0;
    }
    if (!pci_checkDevice(counter_map[index].device, cpu_id))
    {
        return -ENODEV;
    }

    flags = (1ULL<<22);
    flags |= (event->umask<<8) + event->eventId;
    if (event->numberOfOptions > 0)
    {
        for(int j=0;j<event->numberOfOptions;j++)
        {
            switch (event->options[j].type)
            {
                case EVENT_OPTION_EDGE:
                    flags |= (1ULL<<18);
                    break;
                case EVENT_OPTION_INVERT:
                    flags |= (1ULL<<23);
                    break;
                case EVENT_OPTION_TID:
                    flags |= (1ULL<<19);
                    break;
                case EVENT_OPTION_THRESHOLD:
                    flags |= (extractBitField(event->options[j].value,8,0)<<24);
                    break;
            }
        }
    }
    VERBOSEPRINTREG(cpu_id, counter_map[index].configRegister, flags, SETUP_SBOX);
    CHECK_MSR_WRITE_ERROR(msr_twrite(read_fd, cpu_id, counter_map[index].configRegister, flags));
    return 0;
}


int hasep_mbox_setup(int cpu_id, RegisterIndex index, PerfmonEvent *event)
{
    uint64_t flags = 0x0ULL;
    PciDeviceIndex dev = counter_map[index].device;
    GET_READFD(cpu_id);

    if ((socket_lock[affinity_core2node_lookup[cpu_id]] != cpu_id))
    {
        return 0;
    }
    if (!pci_checkDevice(counter_map[index].device, cpu_id))
    {
        return -ENODEV;
    }

    flags = (1ULL<<22)|(1ULL<<20);
    flags |= (event->umask<<8) + event->eventId;
    if (event->numberOfOptions > 0)
    {
        for(int j=0;j<event->numberOfOptions;j++)
        {
            switch (event->options[j].type)
            {
                case EVENT_OPTION_EDGE:
                    flags |= (1ULL<<18);
                    break;
                case EVENT_OPTION_INVERT:
                    flags |= (1ULL<<23);
                    break;
                case EVENT_OPTION_THRESHOLD:
                    flags |= (extractBitField(event->options[j].value,8,0)<<24);
                    break;
            }
        }
    }
    VERBOSEPRINTPCIREG(cpu_id, dev, counter_map[index].configRegister, flags, SETUP_MBOX);
    CHECK_PCI_WRITE_ERROR(pci_twrite(read_fd, cpu_id, dev, counter_map[index].configRegister, flags));
    return 0;
}

int hasep_ibox_setup(int cpu_id, RegisterIndex index, PerfmonEvent *event)
{
    uint64_t flags = 0x0ULL;
    PciDeviceIndex dev = counter_map[index].device;
    GET_READFD(cpu_id);

    if ((socket_lock[affinity_core2node_lookup[cpu_id]] != cpu_id))
    {
        return 0;
    }
    if (!pci_checkDevice(counter_map[index].device, cpu_id))
    {
        return -ENODEV;
    }

    flags = (1ULL<<22)|(1ULL<<20);
    flags |= (event->umask<<8) + event->eventId;
    if (event->numberOfOptions > 0)
    {
        for(int j=0;j<event->numberOfOptions;j++)
        {
            switch (event->options[j].type)
            {
                case EVENT_OPTION_EDGE:
                    flags |= (1ULL<<18);
                    break;
                case EVENT_OPTION_INVERT:
                    flags |= (1ULL<<23);
                    break;
                case EVENT_OPTION_THRESHOLD:
                    flags |= (extractBitField(event->options[j].value,8,0)<<24);
                    break;
            }
        }
    }
    VERBOSEPRINTPCIREG(cpu_id, dev, counter_map[index].configRegister, flags, SETUP_IBOX);
    CHECK_PCI_WRITE_ERROR(pci_twrite(read_fd, cpu_id, dev, counter_map[index].configRegister, flags));
    return 0;
}


int hasep_pbox_setup(int cpu_id, RegisterIndex index, PerfmonEvent *event)
{
    uint64_t flags = 0x0ULL;
    PciDeviceIndex dev = counter_map[index].device;
    GET_READFD(cpu_id);

    if ((socket_lock[affinity_core2node_lookup[cpu_id]] != cpu_id))
    {
        return 0;
    }
    if (!pci_checkDevice(counter_map[index].device, cpu_id))
    {
        return -ENODEV;
    }

    flags = (1ULL<<22)|(1ULL<<20);
    flags |= (event->umask<<8) + event->eventId;
    if (event->numberOfOptions > 0)
    {
        for(int j=0;j<event->numberOfOptions;j++)
        {
            switch (event->options[j].type)
            {
                case EVENT_OPTION_EDGE:
                    flags |= (1ULL<<18);
                    break;
                case EVENT_OPTION_INVERT:
                    flags |= (1ULL<<23);
                    break;
                case EVENT_OPTION_THRESHOLD:
                    flags |= (extractBitField(event->options[j].value,8,0)<<24);
                    break;
            }
        }
    }
    VERBOSEPRINTPCIREG(cpu_id, dev, counter_map[index].configRegister, flags, SETUP_PBOX);
    CHECK_PCI_WRITE_ERROR(pci_twrite(read_fd, cpu_id, dev, counter_map[index].configRegister, flags));
    return 0;
}

int hasep_rbox_setup(int cpu_id, RegisterIndex index, PerfmonEvent *event)
{
    uint64_t flags = 0x0ULL;
    PciDeviceIndex dev = counter_map[index].device;
    GET_READFD(cpu_id);

    if ((socket_lock[affinity_core2node_lookup[cpu_id]] != cpu_id))
    {
        return 0;
    }
    if (!pci_checkDevice(counter_map[index].device, cpu_id))
    {
        return -ENODEV;
    }

    flags = (1ULL<<22)|(1ULL<<20);
    flags |= (event->umask<<8) + event->eventId;
    if (event->numberOfOptions > 0)
    {
        for(int j=0;j<event->numberOfOptions;j++)
        {
            switch (event->options[j].type)
            {
                case EVENT_OPTION_EDGE:
                    flags |= (1ULL<<18);
                    break;
                case EVENT_OPTION_INVERT:
                    flags |= (1ULL<<23);
                    break;
                case EVENT_OPTION_THRESHOLD:
                    flags |= (extractBitField(event->options[j].value,8,0)<<24);
                    break;
            }
        }
    }
    VERBOSEPRINTPCIREG(cpu_id, dev, counter_map[index].configRegister, flags, SETUP_PBOX);
    CHECK_PCI_WRITE_ERROR(pci_twrite(read_fd, cpu_id, dev, counter_map[index].configRegister, flags));
    return 0;
}

int hasep_qbox_setup(int cpu_id, RegisterIndex index, PerfmonEvent *event)
{
    uint64_t flags = 0x0ULL;
    PciDeviceIndex dev = counter_map[index].device;
    GET_READFD(cpu_id);

    if ((socket_lock[affinity_core2node_lookup[cpu_id]] != cpu_id))
    {
        return 0;
    }
    if (!pci_checkDevice(counter_map[index].device, cpu_id))
    {
        return -ENODEV;
    }

    flags = (1ULL<<22)|(1ULL<<20);
    flags |= (event->umask<<8) + event->eventId;
    if (event->cfgBits == 0x01)
    {
        flags |= (1ULL<<21);
    }
    if (event->numberOfOptions > 0)
    {
        for(int j=0;j<event->numberOfOptions;j++)
        {
            switch (event->options[j].type)
            {
                case EVENT_OPTION_EDGE:
                    flags |= (1ULL<<18);
                    break;
                case EVENT_OPTION_INVERT:
                    flags |= (1ULL<<23);
                    break;
                case EVENT_OPTION_THRESHOLD:
                    flags |= (extractBitField(event->options[j].value,8,0)<<24);
                    break;
                case EVENT_OPTION_MATCH0:
                    VERBOSEPRINTPCIREG(cpu_id, dev, PCI_UNC_V3_QPI_PMON_RX_MATCH_0, event->options[j].value, SETUP_QBOX_RX_MATCH0);
                    CHECK_PCI_WRITE_ERROR(pci_twrite(read_fd, cpu_id, dev, PCI_UNC_V3_QPI_PMON_RX_MATCH_0, event->options[j].value));
                    break;
                case EVENT_OPTION_MATCH1:
                    VERBOSEPRINTPCIREG(cpu_id, dev, PCI_UNC_V3_QPI_PMON_RX_MATCH_1, event->options[j].value, SETUP_QBOX_RX_MATCH1);
                    CHECK_PCI_WRITE_ERROR(pci_twrite(read_fd, cpu_id, dev, PCI_UNC_V3_QPI_PMON_RX_MATCH_1, event->options[j].value));
                    break;
                case EVENT_OPTION_MATCH2:
                    VERBOSEPRINTPCIREG(cpu_id, dev, PCI_UNC_V3_QPI_PMON_TX_MATCH_0, event->options[j].value, SETUP_QBOX_TX_MATCH0);
                    CHECK_PCI_WRITE_ERROR(pci_twrite(read_fd, cpu_id, dev, PCI_UNC_V3_QPI_PMON_TX_MATCH_0, event->options[j].value));
                    break;
                case EVENT_OPTION_MATCH3:
                    VERBOSEPRINTPCIREG(cpu_id, dev, PCI_UNC_V3_QPI_PMON_TX_MATCH_1, event->options[j].value, SETUP_QBOX_TX_MATCH1);
                    CHECK_PCI_WRITE_ERROR(pci_twrite(read_fd, cpu_id, dev, PCI_UNC_V3_QPI_PMON_TX_MATCH_1, event->options[j].value));
                    break;
                case EVENT_OPTION_MASK0:
                    VERBOSEPRINTPCIREG(cpu_id, dev, PCI_UNC_V3_QPI_PMON_RX_MASK_0, event->options[j].value, SETUP_QBOX_RX_MASK0);
                    CHECK_PCI_WRITE_ERROR(pci_twrite(read_fd, cpu_id, dev, PCI_UNC_V3_QPI_PMON_RX_MASK_0, event->options[j].value));
                    break;
                case EVENT_OPTION_MASK1:
                    VERBOSEPRINTPCIREG(cpu_id, dev, PCI_UNC_V3_QPI_PMON_RX_MASK_1, event->options[j].value, SETUP_QBOX_RX_MASK1);
                    CHECK_PCI_WRITE_ERROR(pci_twrite(read_fd, cpu_id, dev, PCI_UNC_V3_QPI_PMON_RX_MASK_1, event->options[j].value));
                    break;
                case EVENT_OPTION_MASK2:
                    VERBOSEPRINTPCIREG(cpu_id, dev, PCI_UNC_V3_QPI_PMON_TX_MASK_0, event->options[j].value, SETUP_QBOX_TX_MASK0);
                    CHECK_PCI_WRITE_ERROR(pci_twrite(read_fd, cpu_id, dev, PCI_UNC_V3_QPI_PMON_TX_MASK_0, event->options[j].value));
                    break;
                case EVENT_OPTION_MASK3:
                    VERBOSEPRINTPCIREG(cpu_id, dev, PCI_UNC_V3_QPI_PMON_TX_MASK_1, event->options[j].value, SETUP_QBOX_TX_MASK1);
                    CHECK_PCI_WRITE_ERROR(pci_twrite(read_fd, cpu_id, dev, PCI_UNC_V3_QPI_PMON_TX_MASK_1, event->options[j].value));
                    break;
            }
        }
    }
    VERBOSEPRINTPCIREG(cpu_id, dev, counter_map[index].configRegister, flags, SETUP_QBOX);
    CHECK_PCI_WRITE_ERROR(pci_twrite(read_fd, cpu_id, dev, counter_map[index].configRegister, flags));
}

#define HASEP_FREEZE_UNCORE \
    if (haveLock && eventSet->regTypeMask & ~(0xFULL)) \
    { \
        VERBOSEPRINTREG(cpu_id, MSR_UNC_V3_U_PMON_GLOBAL_CTL, LLU_CAST (1ULL<<31), FREEZE_UNCORE); \
        CHECK_MSR_WRITE_ERROR(msr_twrite(read_fd, cpu_id, MSR_UNC_V3_U_PMON_GLOBAL_CTL, (1ULL<<31))); \
    }

#define HASEP_UNFREEZE_UNCORE \
    if (haveLock && eventSet->regTypeMask & ~(0xFULL)) \
    { \
        VERBOSEPRINTREG(cpu_id, MSR_UNC_V3_U_PMON_GLOBAL_CTL, LLU_CAST (1ULL<<29), UNFREEZE_UNCORE); \
        CHECK_MSR_WRITE_ERROR(msr_twrite(read_fd, cpu_id, MSR_UNC_V3_U_PMON_GLOBAL_CTL, (1ULL<<29))); \
    }

#define HASEP_UNFREEZE_UNCORE_AND_RESET_CTR \
    if (haveLock && (eventSet->regTypeMask & ~(REG_TYPE_MASK(FIXED)|REG_TYPE_MASK(PMC)|REG_TYPE_MASK(THERMAL)|REG_TYPE_MASK(POWER)))) \
    { \
        for (int j=0; j<NUM_UNITS; j++) \
        { \
            if (eventSet->regTypeMask & REG_TYPE_MASK(j)) \
            { \
                if (box_map[j].ctrlRegister != 0x0ULL) \
                { \
                    if (box_map[j].isPci) \
                    { \
                        VERBOSEPRINTPCIREG(cpu_id, box_map[j].device, box_map[j].ctrlRegister, LLU_CAST 0x2ULL, CLEAR_PCI_CTR); \
                        CHECK_PCI_WRITE_ERROR(pci_twrite(read_fd, cpu_id, box_map[j].device, box_map[j].ctrlRegister, 0x2ULL)); \
                    } \
                    else \
                    { \
                        VERBOSEPRINTREG(cpu_id, box_map[j].ctrlRegister, LLU_CAST 0x2ULL, CLEAR_CTR); \
                        CHECK_MSR_WRITE_ERROR(msr_twrite(read_fd, cpu_id, box_map[j].ctrlRegister, 0x2ULL)); \
                    } \
                } \
                else \
                { \
                    for (int k=0;k<perfmon_numCounters;k++) \
                    { \
                        if ((counter_map[k].type == j) && (counter_map[k].type != WBOX0FIX) && (counter_map[k].type != POWER)) \
                        { \
                            if (box_map[j].isPci) \
                            { \
                                VERBOSEPRINTPCIREG(cpu_id, box_map[j].device, counter_map[k].counterRegister, 0x0ULL, CLEAR_PCI_CTR_MANUAL); \
                                CHECK_PCI_WRITE_ERROR(pci_twrite(read_fd, cpu_id, box_map[j].device, counter_map[k].counterRegister, 0x0ULL)); \
                                if (counter_map[k].counterRegister2 != 0x0) \
                                { \
                                    VERBOSEPRINTPCIREG(cpu_id, box_map[j].device, counter_map[k].counterRegister2, 0x0ULL, CLEAR_PCI_CTR_MANUAL); \
                                    CHECK_PCI_WRITE_ERROR(pci_twrite(read_fd, cpu_id, box_map[j].device, counter_map[k].counterRegister2, 0x0ULL)); \
                                } \
                            } \
                            else \
                            { \
                                VERBOSEPRINTREG(cpu_id, counter_map[k].counterRegister, 0x0ULL, CLEAR_CTR_MANUAL); \
                                CHECK_MSR_WRITE_ERROR(msr_twrite(read_fd, cpu_id, counter_map[k].counterRegister, 0x0ULL)); \
                                if (counter_map[k].counterRegister2 != 0x0) \
                                { \
                                    VERBOSEPRINTREG(cpu_id, counter_map[k].counterRegister2, 0x0ULL, CLEAR_CTR_MANUAL); \
                                    CHECK_MSR_WRITE_ERROR(msr_twrite(read_fd, cpu_id, counter_map[k].counterRegister2, 0x0ULL)); \
                                } \
                            } \
                        } \
                    } \
                } \
            } \
        } \
        VERBOSEPRINTREG(cpu_id, MSR_UNC_V3_U_PMON_GLOBAL_CTL, LLU_CAST (1ULL<<29), UNFREEZE_UNCORE); \
        CHECK_MSR_WRITE_ERROR(msr_twrite(read_fd, cpu_id, MSR_UNC_V3_U_PMON_GLOBAL_CTL, (1ULL<<29))); \
    }

#define HASEP_FREEZE_UNCORE_AND_RESET_CTL \
    if (haveLock && (eventSet->regTypeMask & ~(REG_TYPE_MASK(FIXED)|REG_TYPE_MASK(PMC)|REG_TYPE_MASK(THERMAL)|REG_TYPE_MASK(POWER)))) \
    { \
        VERBOSEPRINTREG(cpu_id, MSR_UNC_V3_U_PMON_GLOBAL_CTL, LLU_CAST (1ULL<<31), FREEZE_UNCORE); \
        CHECK_MSR_WRITE_ERROR(msr_twrite(read_fd, cpu_id, MSR_UNC_V3_U_PMON_GLOBAL_CTL, (1ULL<<31))); \
        for (int j=0; j<NUM_UNITS; j++) \
        { \
            if (eventSet->regTypeMask & REG_TYPE_MASK(j)) \
            { \
                if (box_map[j].ctrlRegister != 0x0ULL) \
                { \
                    if (box_map[j].isPci) \
                    { \
                        VERBOSEPRINTPCIREG(cpu_id, box_map[j].device, box_map[j].ctrlRegister, LLU_CAST 0x1ULL, CLEAR_PCI_CTL); \
                        CHECK_PCI_WRITE_ERROR(pci_twrite(read_fd, cpu_id, box_map[j].device, box_map[j].ctrlRegister, 0x1ULL)); \
                    } \
                    else \
                    { \
                        VERBOSEPRINTREG(cpu_id, box_map[j].ctrlRegister, LLU_CAST 0x1ULL, CLEAR_CTL); \
                        CHECK_MSR_WRITE_ERROR(msr_twrite(read_fd, cpu_id, box_map[j].ctrlRegister, 0x1ULL)); \
                    } \
                } \
                else \
                { \
                    for (int k=0;k<perfmon_numCounters;k++) \
                    { \
                        if ((counter_map[k].type == j) && (counter_map[k].type != WBOX0FIX) && (counter_map[k].type != POWER)) \
                        { \
                            if (box_map[j].isPci) \
                            { \
                                VERBOSEPRINTPCIREG(cpu_id, box_map[j].device, counter_map[k].configRegister, 0x0ULL, CLEAR_PCI_CTL_MANUAL); \
                                CHECK_PCI_WRITE_ERROR(pci_twrite(read_fd, cpu_id, box_map[j].device, counter_map[k].configRegister, 0x0ULL)); \
                            } \
                            else \
                            { \
                                VERBOSEPRINTREG(cpu_id, counter_map[k].configRegister, 0x0ULL, CLEAR_CTL_MANUAL); \
                                CHECK_MSR_WRITE_ERROR(msr_twrite(read_fd, cpu_id, counter_map[k].configRegister, 0x0ULL)); \
                            } \
                        } \
                    } \
                } \
            } \
        } \
    }



#define HASEP_SETUP_BOX(id) \
    if (haveLock && (eventSet->regTypeMask & REG_TYPE_MASK(id))) \
    { \
        flags = (1ULL<<22)|(1ULL<<20); \
        flags |= (event->umask<<8) + event->eventId; \
        if (event->numberOfOptions > 0) \
        { \
            for(int j=0;j<event->numberOfOptions;j++) \
            { \
                switch (event->options[j].type) \
                { \
                    case EVENT_OPTION_EDGE: \
                        flags |= (1ULL<<18); \
                        break; \
                    case EVENT_OPTION_INVERT: \
                        flags |= (1ULL<<23); \
                        break; \
                    case EVENT_OPTION_THRESHOLD: \
                        flags |= (extractBitField(event->options[j].value,5,0)<<24); \
                        break; \
                } \
            } \
        } \
        VERBOSEPRINTREG(cpu_id, reg, flags, SETUP_##id); \
        CHECK_MSR_WRITE_ERROR(msr_twrite(read_fd, cpu_id, reg, flags)); \
    }

#define HASEP_SETUP_PCI_BOX(id) \
    if (haveLock && (eventSet->regTypeMask & REG_TYPE_MASK(id))) \
    { \
        flags = (1ULL<<22)|(1ULL<<20); \
        flags |= (event->umask<<8) + event->eventId; \
        if (event->numberOfOptions > 0) \
        { \
            for(int j=0;j<event->numberOfOptions;j++) \
            { \
                switch (event->options[j].type) \
                { \
                    case EVENT_OPTION_EDGE: \
                        flags |= (1ULL<<18); \
                        break; \
                    case EVENT_OPTION_INVERT: \
                        flags |= (1ULL<<23); \
                        break; \
                    case EVENT_OPTION_THRESHOLD: \
                        flags |= (extractBitField(event->options[j].value,8,0)<<24); \
                        break; \
                } \
            } \
        } \
        VERBOSEPRINTPCIREG(cpu_id, dev, reg, flags, SETUP_##id); \
        CHECK_PCI_WRITE_ERROR(pci_twrite(read_fd, cpu_id, dev, reg, flags)); \
    }

int perfmon_setupCounterThread_haswellEP(
        int thread_id,
        PerfmonEventSet* eventSet)
{
    int haveLock = 0;
    int ret;
    uint64_t flags;
    uint32_t uflags;
    uint64_t fixed_flags = 0x0ULL;
    int cpu_id = groupSet->threads[thread_id].processorId;
    GET_READFD(cpu_id);

    if ((socket_lock[affinity_core2node_lookup[cpu_id]] == cpu_id))
    {
        haveLock = 1;
    }

    if (eventSet->regTypeMask & (REG_TYPE_MASK(FIXED)|REG_TYPE_MASK(PMC)))
    {
        VERBOSEPRINTREG(cpu_id, MSR_PERF_GLOBAL_CTRL, 0x0ULL, FREEZE_PMC_AND_FIXED)
        CHECK_MSR_WRITE_ERROR(msr_twrite(read_fd, cpu_id, MSR_PERF_GLOBAL_CTRL, 0x0ULL));
        CHECK_MSR_WRITE_ERROR(msr_twrite(read_fd, cpu_id, MSR_PERF_GLOBAL_OVF_CTRL, 0x0ULL));
        CHECK_MSR_WRITE_ERROR(msr_twrite(read_fd, cpu_id, MSR_PEBS_ENABLE, 0x0ULL));
    }
    HASEP_FREEZE_UNCORE;
    for (int i=0;i < eventSet->numberOfEvents;i++)
    {
        RegisterIndex index = eventSet->events[i].index;
        PerfmonEvent *event = &(eventSet->events[i].event);
        uint64_t reg = counter_map[index].configRegister;
        PciDeviceIndex dev = counter_map[index].device;
        eventSet->events[i].threadCounter[thread_id].init = TRUE;
        flags = 0x0ULL;
        switch (eventSet->events[i].type)
        {
            case PMC:
                hasep_pmc_setup(cpu_id, index, event);
                break;

            case FIXED:
                fixed_flags |= hasep_fixed_setup(index, event);
                break;

            case POWER:
                break;

            case CBOX0:
            case CBOX1:
            case CBOX2:
            case CBOX3:
            case CBOX4:
            case CBOX5:
            case CBOX6:
            case CBOX7:
            case CBOX8:
            case CBOX9:
            case CBOX10:
            case CBOX11:
            case CBOX12:
            case CBOX13:
            case CBOX14:
            case CBOX15:
            case CBOX16:
            case CBOX17:
                hasep_cbox_setup(cpu_id, index, event);
                break;

            case UBOX:
                hasep_ubox_setup(cpu_id, index, event);
                break;
            case UBOXFIX:
                flags = (1ULL<<22)|(1ULL<<20);
                VERBOSEPRINTREG(cpu_id, reg, flags, SETUP_UBOXFIX);
                CHECK_MSR_WRITE_ERROR(msr_twrite(read_fd, cpu_id, reg, flags));
                break;

            case SBOX0:
            case SBOX1:
            case SBOX2:
            case SBOX3:
                hasep_sbox_setup(cpu_id, index, event);
                break;

            case BBOX0:
            case BBOX1:
                hasep_bbox_setup(cpu_id, index, event);
                break;

            case WBOX:
                hasep_wbox_setup(cpu_id, index, event);
                break;
            case WBOX0FIX:
                break;

            case MBOX0:
            case MBOX1:
            case MBOX2:
            case MBOX3:
            case MBOX4:
            case MBOX5:
            case MBOX6:
            case MBOX7:
                hasep_mbox_setup(cpu_id, index, event);
                break;

            case PBOX:
                hasep_pbox_setup(cpu_id, index, event);
                break;

            case RBOX0:
            case RBOX1:
                hasep_rbox_setup(cpu_id, index, event);
                break;

            case QBOX0:
            case QBOX1:
                hasep_qbox_setup(cpu_id, index, event);
                break;

            case IBOX0:
            case IBOX1:
                hasep_ibox_setup(cpu_id, index, event);
                break;

            default:
                /* should never be reached */
                break;
        }
    }
    if (fixed_flags > 0x0ULL)
    {
        VERBOSEPRINTREG(cpu_id, MSR_PERF_FIXED_CTR_CTRL, LLU_CAST fixed_flags, SETUP_FIXED)
        CHECK_MSR_WRITE_ERROR(msr_twrite(read_fd, cpu_id, MSR_PERF_FIXED_CTR_CTRL, fixed_flags));
    }
    return 0;
}

int perfmon_startCountersThread_haswellEP(int thread_id, PerfmonEventSet* eventSet)
{
    int ret;
    int haveLock = 0;
    uint64_t flags = 0x0ULL;
    uint64_t tmp = 0x0ULL;
    uint32_t uflags = 0x10000UL; /* Clear freeze bit */
    int cpu_id = groupSet->threads[thread_id].processorId;
    GET_READFD(cpu_id);

    if ((socket_lock[affinity_core2node_lookup[cpu_id]] == cpu_id))
    {
        haveLock = 1;
    }

    //CHECK_MSR_WRITE_ERROR(msr_twrite(read_fd, cpu_id, MSR_PERF_GLOBAL_CTRL, 0x0ULL));

    for (int i=0;i < eventSet->numberOfEvents;i++)
    {
        if (eventSet->events[i].threadCounter[thread_id].init == TRUE)
        {
            RegisterIndex index = eventSet->events[i].index;
            uint64_t reg = counter_map[index].configRegister;
            uint64_t counter1 = counter_map[index].counterRegister;
            uint64_t counter2 = counter_map[index].counterRegister2;
            switch (eventSet->events[i].type)
            {
                case PMC:
                    CHECK_MSR_WRITE_ERROR(msr_twrite(read_fd, cpu_id, counter1, 0x0ULL));
                    flags |= (1ULL<<(index-cpuid_info.perf_num_fixed_ctr));  /* enable counter */
                    break;

                case FIXED:
                    CHECK_MSR_WRITE_ERROR(msr_twrite(read_fd, cpu_id, counter1, 0x0ULL));
                    flags |= (1ULL<<(index+32));  /* enable fixed counter */
                    break;

                case POWER:
                    if (haveLock)
                    {
                        tmp = 0x0ULL;
                        CHECK_POWER_READ_ERROR(power_tread(read_fd, cpu_id, counter1,(uint32_t*)&tmp));
                        VERBOSEPRINTREG(cpu_id, counter1, LLU_CAST tmp, START_POWER)
                        eventSet->events[i].threadCounter[thread_id].startData = tmp;
                    }
                    break;
                case WBOX0FIX:
                    if (haveLock)
                    {
                        tmp = 0x0ULL;
                        CHECK_MSR_READ_ERROR(msr_tread(read_fd, cpu_id, counter1, &tmp));
                        VERBOSEPRINTREG(cpu_id, counter1, LLU_CAST tmp, START_WBOXFIX);
                        eventSet->events[i].threadCounter[thread_id].startData = tmp;
                    }
                    break;

                default:
                    /* should never be reached */
                    break;
            }
        }
    }

    HASEP_UNFREEZE_UNCORE_AND_RESET_CTR;

    if (eventSet->regTypeMask & (REG_TYPE_MASK(PMC)|REG_TYPE_MASK(FIXED)))
    {
        VERBOSEPRINTREG(cpu_id, MSR_PERF_GLOBAL_CTRL, LLU_CAST flags, UNFREEZE_PMC_AND_FIXED)
        CHECK_MSR_WRITE_ERROR(msr_twrite(read_fd, cpu_id, MSR_PERF_GLOBAL_CTRL, flags));
        CHECK_MSR_WRITE_ERROR(msr_twrite(read_fd, cpu_id, MSR_PERF_GLOBAL_OVF_CTRL, 0x30000000FULL));
    }

    return 0;
}

int has_uncore_read(int cpu_id, RegisterIndex index, PerfmonEvent *event,
                     uint64_t* cur_result, int* overflows, int flags,
                     int global_offset, int box_offset)
{
    uint64_t result = 0x0ULL;
    uint64_t tmp = 0x0ULL;
    uint64_t reg = counter_map[index].configRegister;
    RegisterType type = counter_map[index].type;
    PciDeviceIndex dev = counter_map[index].device;
    uint64_t counter1 = counter_map[index].counterRegister;
    uint64_t counter2 = counter_map[index].counterRegister2;
    GET_READFD(cpu_id);

    if (box_map[type].isPci && pci_checkDevice(dev, cpu_id))
    {
        CHECK_PCI_READ_ERROR(pci_tread(read_fd, cpu_id, dev, counter1, (uint32_t*)&tmp));
        VERBOSEPRINTPCIREG(cpu_id, dev, counter1, LLU_CAST tmp, READ_PCI_REG_1);
        if (flags & FREEZE_FLAG_CLEAR_CTR)
        {
            VERBOSEPRINTPCIREG(cpu_id, dev, counter1, LLU_CAST 0x0U, CLEAR_PCI_REG_1);
            CHECK_PCI_WRITE_ERROR(pci_twrite(read_fd, cpu_id, dev, counter1, 0x0U));
        }
        if (counter2 != 0x0)
        {
            result = (tmp<<32);
            CHECK_PCI_READ_ERROR(pci_tread(read_fd, cpu_id, dev, counter2, (uint32_t*)&tmp));
            result += tmp;
            VERBOSEPRINTPCIREG(cpu_id, dev, counter2, LLU_CAST result, READ_PCI_REG_2);
            if (flags & FREEZE_FLAG_CLEAR_CTR)
            {
                VERBOSEPRINTPCIREG(cpu_id, dev, counter2, LLU_CAST 0x0U, CLEAR_PCI_REG_2);
                CHECK_PCI_WRITE_ERROR(pci_twrite(read_fd, cpu_id, dev, counter2, 0x0U));
            }
        }
        else
        {
            result = tmp;
        }
        
    }
    else if (!box_map[type].isPci && counter1 != 0x0)
    {
        CHECK_MSR_READ_ERROR(msr_tread(read_fd, cpu_id, counter1, &result));
        VERBOSEPRINTREG(cpu_id, counter1, LLU_CAST flags, READ_MSR_REG);
        if (flags & FREEZE_FLAG_CLEAR_CTR)
        {
            VERBOSEPRINTREG(cpu_id, counter1, LLU_CAST 0x0ULL, CLEAR_MSR_REG);
            CHECK_MSR_WRITE_ERROR(msr_twrite(read_fd, cpu_id, counter1, 0x0ULL));
        }
    }
    else
    {
        return -EFAULT;
    }

    if (result < *cur_result)
    {
        uint64_t ovf_values = 0x0ULL;
        int test_local = 0;
        if (global_offset != -1)
        {
            CHECK_MSR_READ_ERROR(msr_tread(read_fd, cpu_id,
                                           MSR_UNC_U_PMON_GLOBAL_STATUS,
                                           &ovf_values));
            VERBOSEPRINTREG(cpu_id, MSR_UNC_U_PMON_GLOBAL_STATUS, LLU_CAST ovf_values, READ_GLOBAL_OVFL);
            if (ovf_values & (1<<global_offset))
            {
                VERBOSEPRINTREG(cpu_id, MSR_UNC_U_PMON_GLOBAL_STATUS, LLU_CAST (1<<global_offset), CLEAR_GLOBAL_OVFL);
                CHECK_MSR_WRITE_ERROR(msr_twrite(read_fd, cpu_id,
                                                 MSR_UNC_U_PMON_GLOBAL_STATUS,
                                                 (1<<global_offset)));
                test_local = 1;
            }
        }
        else
        {
            test_local = 1;
        }

        if (test_local)
        {
            ovf_values = 0x0ULL;
            if (ivybridge_box_map[type].isPci)
            {
                CHECK_PCI_READ_ERROR(pci_tread(read_fd, cpu_id, dev,
                                              box_map[type].statusRegister,
                                              (uint32_t*)&ovf_values));
                VERBOSEPRINTPCIREG(cpu_id, dev, box_map[type].statusRegister, LLU_CAST ovf_values, READ_BOX_OVFL);
            }
            else
            {
                CHECK_MSR_READ_ERROR(msr_tread(read_fd, cpu_id,
                                              box_map[type].statusRegister,
                                              &ovf_values));
                VERBOSEPRINTREG(cpu_id, box_map[type].statusRegister, LLU_CAST ovf_values, READ_BOX_OVFL);
            }
            if (ovf_values & (1<<box_offset))
            {
                (*overflows)++;
                if (ivybridge_box_map[type].isPci)
                {
                    VERBOSEPRINTPCIREG(cpu_id, dev, box_map[type].statusRegister, LLU_CAST (1<<box_offset), RESET_BOX_OVFL);
                    CHECK_PCI_WRITE_ERROR(pci_twrite(read_fd, cpu_id, dev,
                                                    box_map[type].statusRegister,
                                                    (1<<box_offset)));
                }
                else
                {
                    VERBOSEPRINTREG(cpu_id, box_map[type].statusRegister, LLU_CAST (1<<box_offset), RESET_BOX_OVFL);
                    CHECK_MSR_WRITE_ERROR(msr_twrite(read_fd, cpu_id,
                                                     box_map[type].statusRegister,
                                                     (1<<box_offset)));
                }
            }
        }
    }
    *cur_result = result;
    return 0;
}

#define HASEP_CHECK_CORE_OVERFLOW(offset) \
    if (counter_result < eventSet->events[i].threadCounter[thread_id].counterData) \
    { \
        uint64_t ovf_values = 0x0ULL; \
        CHECK_MSR_READ_ERROR(msr_tread(read_fd, cpu_id, MSR_PERF_GLOBAL_STATUS, &ovf_values)); \
        if (ovf_values & (1ULL<<offset)) \
        { \
            eventSet->events[i].threadCounter[thread_id].overflows++; \
        } \
        CHECK_MSR_WRITE_ERROR(msr_twrite(read_fd, cpu_id, MSR_PERF_GLOBAL_OVF_CTRL, (1ULL<<offset))); \
    }


#define HASEP_CHECK_LOCAL_OVERFLOW \
    if (counter_result < eventSet->events[i].threadCounter[thread_id].counterData) \
    { \
        uint64_t ovf_values = 0x0ULL; \
        uint64_t offset = getCounterTypeOffset(eventSet->events[i].index); \
        CHECK_MSR_READ_ERROR(msr_tread(read_fd, cpu_id, box_map[eventSet->events[i].type].statusRegister, &ovf_values)); \
        if (ovf_values & (1ULL<<offset)) \
        { \
            eventSet->events[i].threadCounter[thread_id].overflows++; \
            CHECK_MSR_WRITE_ERROR(msr_twrite(read_fd, cpu_id, box_map[eventSet->events[i].type].statusRegister, (1ULL<<offset))); \
        } \
    }

int perfmon_stopCountersThread_haswellEP(int thread_id, PerfmonEventSet* eventSet)
{
    int bit;
    int haveLock = 0;
    uint64_t flags;
    uint32_t uflags = 0x10100UL; /* Set freeze bit */
    uint64_t counter_result = 0x0ULL;
    int cpu_id = groupSet->threads[thread_id].processorId;
    GET_READFD(cpu_id);

    if ((socket_lock[affinity_core2node_lookup[cpu_id]] == cpu_id))
    {
        haveLock = 1;
    }

    if (eventSet->regTypeMask & (REG_TYPE_MASK(PMC)|REG_TYPE_MASK(FIXED)))
    {
        VERBOSEPRINTREG(cpu_id, MSR_PERF_GLOBAL_CTRL, 0x0ULL, FREEZE_PMC_AND_FIXED)
        CHECK_MSR_WRITE_ERROR(msr_twrite(read_fd, cpu_id, MSR_PERF_GLOBAL_CTRL, 0x0ULL));
    }
    HASEP_FREEZE_UNCORE_AND_RESET_CTL;


    for (int i=0;i < eventSet->numberOfEvents;i++)
    {
        if (eventSet->events[i].threadCounter[thread_id].init == TRUE)
        {
            counter_result= 0x0ULL;
            RegisterIndex index = eventSet->events[i].index;
            PerfmonEvent *event = &(eventSet->events[i].event);
            PciDeviceIndex dev = counter_map[index].device;
            uint64_t reg = counter_map[index].configRegister;
            uint64_t counter1 = counter_map[index].counterRegister;
            uint64_t counter2 = counter_map[index].counterRegister2;
            uint64_t* current = &(eventSet->events[i].threadCounter[thread_id].counterData);
            int* overflows = &(eventSet->events[i].threadCounter[thread_id].overflows);
            switch (eventSet->events[i].type)
            {
                case PMC:
                    CHECK_MSR_READ_ERROR(msr_tread(read_fd, cpu_id, counter1, &counter_result));
                    HASEP_CHECK_CORE_OVERFLOW(index-cpuid_info.perf_num_fixed_ctr);
                    VERBOSEPRINTREG(cpu_id, counter1, LLU_CAST counter_result, READ_PMC)
                    eventSet->events[i].threadCounter[thread_id].counterData = counter_result;
                    break;

                case FIXED:
                    CHECK_MSR_READ_ERROR(msr_tread(read_fd, cpu_id, counter1, &counter_result));
                    HASEP_CHECK_CORE_OVERFLOW(index+32);
                    VERBOSEPRINTREG(cpu_id, counter1, LLU_CAST counter_result, READ_FIXED)
                    eventSet->events[i].threadCounter[thread_id].counterData = counter_result;
                    break;

                case POWER:
                    if (haveLock && (eventSet->regTypeMask & REG_TYPE_MASK(POWER)))
                    {
                        CHECK_POWER_READ_ERROR(power_tread(read_fd, cpu_id, counter1, (uint32_t*)&counter_result));
                        VERBOSEPRINTREG(cpu_id, counter1, LLU_CAST counter_result, STOP_POWER)
                        if (counter_result < eventSet->events[i].threadCounter[thread_id].counterData)
                        {
                            eventSet->events[i].threadCounter[thread_id].overflows++;
                        }
                        eventSet->events[i].threadCounter[thread_id].counterData = counter_result;
                    }
                    break;

                case THERMAL:
                    CHECK_TEMP_READ_ERROR(thermal_tread(read_fd, cpu_id,(uint32_t*)&counter_result));
                    eventSet->events[i].threadCounter[thread_id].counterData = counter_result;
                    break;

                case CBOX0:
                case CBOX1:
                case CBOX2:
                case CBOX3:
                case CBOX4:
                case CBOX5:
                case CBOX6:
                case CBOX7:
                case CBOX8:
                case CBOX9:
                case CBOX10:
                case CBOX11:
                case CBOX12:
                case CBOX13:
                case CBOX14:
                case CBOX15:
                case CBOX16:
                case CBOX17:
                    has_uncore_read(cpu_id, index, event, current, overflows,
                                    FREEZE_FLAG_CLEAR_CTR, -1, getCounterTypeOffset(index));
                    break;

                case UBOX:
                    has_uncore_read(cpu_id, index, event, current, overflows,
                                    FREEZE_FLAG_CLEAR_CTR, 1, getCounterTypeOffset(index));
                    break;
                case UBOXFIX:
                    has_uncore_read(cpu_id, index, event, current, overflows,
                                    FREEZE_FLAG_CLEAR_CTR, 0, getCounterTypeOffset(index));
                    break;

                case SBOX0:
                case SBOX1:
                case SBOX2:
                case SBOX3:
                    has_uncore_read(cpu_id, index, event, current, overflows,
                                    FREEZE_FLAG_CLEAR_CTR, -1, getCounterTypeOffset(index));
                    break;

                case WBOX:
                    has_uncore_read(cpu_id, index, event, current, overflows,
                                    FREEZE_FLAG_CLEAR_CTR, 2, getCounterTypeOffset(index));
                    break;
                case WBOX0FIX:
                    if (haveLock)
                    {
                        CHECK_MSR_READ_ERROR(msr_tread(read_fd, cpu_id, counter1, &counter_result));
                        if (counter_result < *current)
                        {
                            (*overflows)++;
                        }
                        *current = counter_result;
                    }
                    break;

                case BBOX0:
                    has_uncore_read(cpu_id, index, event, current, overflows,
                                    FREEZE_FLAG_CLEAR_CTR, 21, getCounterTypeOffset(index));
                    break;
                case BBOX1:
                    has_uncore_read(cpu_id, index, event, current, overflows,
                                    FREEZE_FLAG_CLEAR_CTR, 22, getCounterTypeOffset(index));
                    break;

                case MBOX0:
                case MBOX1:
                case MBOX2:
                case MBOX3:
                    has_uncore_read(cpu_id, index, event, current, overflows,
                                    FREEZE_FLAG_CLEAR_CTR, 23, getCounterTypeOffset(index)+1);
                    break;

                case MBOX4:
                case MBOX5:
                case MBOX6:
                case MBOX7:
                    has_uncore_read(cpu_id, index, event, current, overflows,
                                    FREEZE_FLAG_CLEAR_CTR, 24, getCounterTypeOffset(index)+1);
                    break;

                case PBOX:
                    has_uncore_read(cpu_id, index, event, current, overflows,
                                    FREEZE_FLAG_CLEAR_CTR, 29, getCounterTypeOffset(index));
                    break;

                case IBOX0:
                    has_uncore_read(cpu_id, index, event, current, overflows,
                                    FREEZE_FLAG_CLEAR_CTR, 34, getCounterTypeOffset(index));
                    break;
                case IBOX1:
                    has_uncore_read(cpu_id, index, event, current, overflows,
                                    FREEZE_FLAG_CLEAR_CTR, 34, getCounterTypeOffset(index)+2);
                    break;

                case RBOX0:
                    has_uncore_read(cpu_id, index, event, current, overflows,
                                    FREEZE_FLAG_CLEAR_CTR, 27, getCounterTypeOffset(index));
                    break;
                case RBOX1:
                    has_uncore_read(cpu_id, index, event, current, overflows,
                                    FREEZE_FLAG_CLEAR_CTR, 28, getCounterTypeOffset(index));
                    break;

                case QBOX0:
                    has_uncore_read(cpu_id, index, event, current, overflows,
                                    FREEZE_FLAG_CLEAR_CTR, 25, getCounterTypeOffset(index));
                    break;
                case QBOX1:
                    has_uncore_read(cpu_id, index, event, current, overflows,
                                    FREEZE_FLAG_CLEAR_CTR, 26, getCounterTypeOffset(index));
                    break;

                case QBOX0FIX:
                case QBOX1FIX:
                    if (eventSet->events[i].event.eventId == 0x00)
                    {
                        pci_tread(read_fd, cpu_id, dev, counter1, (uint32_t*)&counter_result);
                        switch(extractBitField(counter_result, 3, 0))
                        {
                            case 0x2:
                                counter_result = 5.6E9;
                                break;
                            case 0x3:
                                counter_result = 6.4E9;
                                break;
                            case 0x4:
                                counter_result = 7.2E9;
                                break;
                            case 0x5:
                                counter_result = 8.0E9;
                                break;
                            case 0x6:
                                counter_result = 8.8E9;
                                break;
                            case 0x7:
                                counter_result = 9.6E9;
                                break;
                            default:
                                counter_result = 0;
                                break;
                        }
                        
                    }
                    else if ((eventSet->events[i].event.eventId == 0x01) ||
                             (eventSet->events[i].event.eventId == 0x02))
                    {
                        pci_tread(read_fd, cpu_id, dev, counter1, (uint32_t*)&counter_result);
                        counter_result = extractBitField(counter_result, 32,0);
                    }
                    eventSet->events[i].threadCounter[thread_id].counterData = counter_result;
                    break;

                default:
                    /* should never be reached */
                    break;
            }
        }
        eventSet->events[i].threadCounter[thread_id].init = FALSE;
    }


    return 0;
}


int perfmon_readCountersThread_haswellEP(int thread_id, PerfmonEventSet* eventSet)
{
    int bit;
    uint64_t tmp = 0x0ULL;
    uint64_t flags;
    int haveLock = 0;
    uint64_t counter_result = 0x0ULL;
    int cpu_id = groupSet->threads[thread_id].processorId;
    GET_READFD(cpu_id);

    if ((socket_lock[affinity_core2node_lookup[cpu_id]] == cpu_id))
    {
        haveLock = 1;
    }

    if (eventSet->regTypeMask & (REG_TYPE_MASK(FIXED)|REG_TYPE_MASK(PMC)))
    {
        CHECK_MSR_READ_ERROR(msr_tread(read_fd, cpu_id, MSR_PERF_GLOBAL_CTRL, &flags));
        VERBOSEPRINTREG(cpu_id, MSR_PERF_GLOBAL_CTRL, LLU_CAST flags, SAFE_PMC_FLAGS)
        CHECK_MSR_WRITE_ERROR(msr_twrite(read_fd, cpu_id, MSR_PERF_GLOBAL_CTRL, 0x0ULL));
        VERBOSEPRINTREG(cpu_id, MSR_PERF_GLOBAL_CTRL, 0x0ULL, RESET_PMC_FLAGS)
    }
    HASEP_FREEZE_UNCORE;

    for (int i=0;i < eventSet->numberOfEvents;i++)
    {
        if (eventSet->events[i].threadCounter[thread_id].init == TRUE)
        {
            counter_result= 0x0ULL;
            RegisterIndex index = eventSet->events[i].index;
            PerfmonEvent *event = &(eventSet->events[i].event);
            PciDeviceIndex dev = counter_map[index].device;
            uint64_t reg = counter_map[index].configRegister;
            uint64_t counter1 = counter_map[index].counterRegister;
            uint64_t counter2 = counter_map[index].counterRegister2;
            uint64_t* current = &(eventSet->events[i].threadCounter[thread_id].counterData);
            int* overflows = &(eventSet->events[i].threadCounter[thread_id].overflows);
            switch (eventSet->events[i].type)
            {
                case PMC:
                    CHECK_MSR_READ_ERROR(msr_tread(read_fd, cpu_id, counter1, &counter_result));
                    HASEP_CHECK_CORE_OVERFLOW(index-cpuid_info.perf_num_fixed_ctr);
                    VERBOSEPRINTREG(cpu_id, counter1, LLU_CAST counter_result, READ_PMC)
                    eventSet->events[i].threadCounter[thread_id].counterData = counter_result;
                    break;

                case FIXED:
                    CHECK_MSR_READ_ERROR(msr_tread(read_fd, cpu_id, counter1, &counter_result));
                    HASEP_CHECK_CORE_OVERFLOW(index+32);
                    VERBOSEPRINTREG(cpu_id, counter1, LLU_CAST counter_result, READ_FIXED)
                    eventSet->events[i].threadCounter[thread_id].counterData = counter_result;
                    break;

                case POWER:
                    if (haveLock && (eventSet->regTypeMask & REG_TYPE_MASK(POWER)))
                    {
                        CHECK_POWER_READ_ERROR(power_tread(read_fd, cpu_id, counter1, (uint32_t*)&counter_result));
                        VERBOSEPRINTREG(cpu_id, counter1, LLU_CAST counter_result, STOP_POWER)
                        if (counter_result < eventSet->events[i].threadCounter[thread_id].counterData)
                        {
                            eventSet->events[i].threadCounter[thread_id].overflows++;
                        }
                        eventSet->events[i].threadCounter[thread_id].counterData = counter_result;
                    }
                    break;

                case THERMAL:
                    CHECK_TEMP_READ_ERROR(thermal_tread(read_fd, cpu_id,(uint32_t*)&counter_result));
                    eventSet->events[i].threadCounter[thread_id].counterData = counter_result;
                    break;

                case CBOX0:
                case CBOX1:
                case CBOX2:
                case CBOX3:
                case CBOX4:
                case CBOX5:
                case CBOX6:
                case CBOX7:
                case CBOX8:
                case CBOX9:
                case CBOX10:
                case CBOX11:
                case CBOX12:
                case CBOX13:
                case CBOX14:
                case CBOX15:
                case CBOX16:
                case CBOX17:
                    has_uncore_read(cpu_id, index, event, current, overflows,
                                    FREEZE_FLAG_CLEAR_CTR, -1, getCounterTypeOffset(index));
                    break;

                case UBOX:
                    has_uncore_read(cpu_id, index, event, current, overflows,
                                    FREEZE_FLAG_CLEAR_CTR, 1, getCounterTypeOffset(index));
                    break;
                case UBOXFIX:
                    has_uncore_read(cpu_id, index, event, current, overflows,
                                    FREEZE_FLAG_CLEAR_CTR, 0, getCounterTypeOffset(index));
                    break;

                case SBOX0:
                case SBOX1:
                case SBOX2:
                case SBOX3:
                    has_uncore_read(cpu_id, index, event, current, overflows,
                                    FREEZE_FLAG_CLEAR_CTR, -1, getCounterTypeOffset(index));
                    break;

                case WBOX:
                    has_uncore_read(cpu_id, index, event, current, overflows,
                                    FREEZE_FLAG_CLEAR_CTR, 2, getCounterTypeOffset(index));
                    break;
                case WBOX0FIX:
                    if (haveLock)
                    {
                        CHECK_MSR_READ_ERROR(msr_tread(read_fd, cpu_id, counter1, &counter_result));
                        if (counter_result < *current)
                        {
                            (*overflows)++;
                        }
                        *current = counter_result;
                    }
                    break;

                case BBOX0:
                    has_uncore_read(cpu_id, index, event, current, overflows,
                                    FREEZE_FLAG_CLEAR_CTR, 21, getCounterTypeOffset(index));
                    break;
                case BBOX1:
                    has_uncore_read(cpu_id, index, event, current, overflows,
                                    FREEZE_FLAG_CLEAR_CTR, 22, getCounterTypeOffset(index));
                    break;

                case MBOX0:
                case MBOX1:
                case MBOX2:
                case MBOX3:
                    has_uncore_read(cpu_id, index, event, current, overflows,
                                    FREEZE_FLAG_CLEAR_CTR, 23, getCounterTypeOffset(index)+1);
                    break;

                case MBOX4:
                case MBOX5:
                case MBOX6:
                case MBOX7:
                    has_uncore_read(cpu_id, index, event, current, overflows,
                                    FREEZE_FLAG_CLEAR_CTR, 24, getCounterTypeOffset(index)+1);
                    break;

                case PBOX:
                    has_uncore_read(cpu_id, index, event, current, overflows,
                                    FREEZE_FLAG_CLEAR_CTR, 29, getCounterTypeOffset(index));
                    break;

                case IBOX0:
                    has_uncore_read(cpu_id, index, event, current, overflows,
                                    FREEZE_FLAG_CLEAR_CTR, 34, getCounterTypeOffset(index));
                    break;
                case IBOX1:
                    has_uncore_read(cpu_id, index, event, current, overflows,
                                    FREEZE_FLAG_CLEAR_CTR, 34, getCounterTypeOffset(index)+2);
                    break;

                case RBOX0:
                    has_uncore_read(cpu_id, index, event, current, overflows,
                                    FREEZE_FLAG_CLEAR_CTR, 27, getCounterTypeOffset(index));
                    break;
                case RBOX1:
                    has_uncore_read(cpu_id, index, event, current, overflows,
                                    FREEZE_FLAG_CLEAR_CTR, 28, getCounterTypeOffset(index));
                    break;

                case QBOX0:
                    has_uncore_read(cpu_id, index, event, current, overflows,
                                    FREEZE_FLAG_CLEAR_CTR, 25, getCounterTypeOffset(index));
                    break;
                case QBOX1:
                    has_uncore_read(cpu_id, index, event, current, overflows,
                                    FREEZE_FLAG_CLEAR_CTR, 26, getCounterTypeOffset(index));
                    break;

                case QBOX0FIX:
                case QBOX1FIX:
                    if (eventSet->events[i].event.eventId == 0x00)
                    {
                        pci_tread(read_fd, cpu_id, dev, counter1, (uint32_t*)&counter_result);
                        switch(extractBitField(counter_result, 3, 0))
                        {
                            case 0x2:
                                counter_result = 5.6E9;
                                break;
                            case 0x3:
                                counter_result = 6.4E9;
                                break;
                            case 0x4:
                                counter_result = 7.2E9;
                                break;
                            case 0x5:
                                counter_result = 8.0E9;
                                break;
                            case 0x6:
                                counter_result = 8.8E9;
                                break;
                            case 0x7:
                                counter_result = 9.6E9;
                                break;
                            default:
                                counter_result = 0;
                                break;
                        }
                        
                    }
                    else if ((eventSet->events[i].event.eventId == 0x01) ||
                             (eventSet->events[i].event.eventId == 0x02))
                    {
                        pci_tread(read_fd, cpu_id, dev, counter1, (uint32_t*)&counter_result);
                        counter_result = extractBitField(counter_result, 32,0);
                    }
                    eventSet->events[i].threadCounter[thread_id].counterData = counter_result;
                    break;

                default:
                    /* should never be reached */
                    break;
            }
        }
    }

    HASEP_UNFREEZE_UNCORE;
    if (eventSet->regTypeMask & (REG_TYPE_MASK(FIXED)|REG_TYPE_MASK(PMC)))
    {
        VERBOSEPRINTREG(cpu_id, MSR_PERF_GLOBAL_CTRL, LLU_CAST flags, RESTORE_PMC_FLAGS)
        CHECK_MSR_WRITE_ERROR(msr_twrite(read_fd, cpu_id, MSR_PERF_GLOBAL_CTRL, flags));
    }

    return 0;
}