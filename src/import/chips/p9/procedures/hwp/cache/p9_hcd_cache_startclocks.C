/* IBM_PROLOG_BEGIN_TAG                                                   */
/* This is an automatically generated prolog.                             */
/*                                                                        */
/* $Source: src/import/chips/p9/procedures/hwp/cache/p9_hcd_cache_startclocks.C $ */
/*                                                                        */
/* OpenPOWER sbe Project                                                  */
/*                                                                        */
/* Contributors Listed Below - COPYRIGHT 2015,2017                        */
/* [+] International Business Machines Corp.                              */
/*                                                                        */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License");        */
/* you may not use this file except in compliance with the License.       */
/* You may obtain a copy of the License at                                */
/*                                                                        */
/*     http://www.apache.org/licenses/LICENSE-2.0                         */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/*                                                                        */
/* IBM_PROLOG_END_TAG                                                     */
///
/// @file  p9_hcd_cache_startclocks.C
/// @brief Quad Clock Start
///
/// Procedure Summary:
///   (Done) Setup L3 EDRAM/LCO
///   (Done) Setup OPCG_ALIGN
///   (Done) Drop partial good regional fences(always drop vital and pervasive)
///   (Done) Drop Vital fence
///   (Done) Reset abst clock muxsel, sync muxsel
///   (DONE) Set fabric node/chip ID from the nest version
///   (Done) module align_chiplets
///   (Done)  - set flushmode_inh to exit flush mode
///   (Done)  - set force align
///   (Done)  - set chiplet_is_aligned
///   (Done)  - clear chiplet_is_aligned
///   (Done)  - wait
///   (Done)  - check chiplet is aligned
///   (Done)  - clear force align
///   (Done) module start_clocks
///   (Done)  - Clear clock controller scan register before start
///   (Done)  - Start arrays + nsl regions
///   (Done)  - Start sl + refresh clock regions
///   (Done) Check for clocks started. If not, error
///   (Done) Drop the cache to PowerBus logical fence
///   (Done) Check for cache xstop, If so, error
///   (Done) Clear flushmode_inh to go into flush mode

// *HWP HWP Owner          : David Du         <daviddu@us.ibm.com>
// *HWP Backup HWP Owner   : Greg Still       <stillgs@us.ibm.com>
// *HWP FW Owner           : Prem Shanker Jha <premjha2@in.ibm.com>
// *HWP Team               : PM
// *HWP Consumed by        : SBE:SGPE
// *HWP Level              : 3

//------------------------------------------------------------------------------
// Includes
//------------------------------------------------------------------------------

#include <p9_misc_scom_addresses.H>
#include <p9_quad_scom_addresses.H>
#include <p9_hcd_common.H>
#include "p9_hcd_cache_startclocks.H"

//------------------------------------------------------------------------------
// Constant Definitions
//------------------------------------------------------------------------------

enum P9_HCD_CACHE_STARTCLOCKS_CONSTANTS
{
    CACHE_CLK_SYNC_POLL_TIMEOUT_HW_NS        = 1000000, // 10^6ns = 1ms timeout
    CACHE_CLK_SYNC_POLL_DELAY_HW_NS          = 10000,   // 10us poll loop delay
    CACHE_CLK_SYNC_POLL_DELAY_SIM_CYCLE      = 32000,   // 320k sim cycle delay

    CACHE_CPLT_ALIGN_DELAY_CACHE_CYCLES      = 255,     // in cache cycles
    CACHE_CPLT_ALIGN_POLL_TIMEOUT_HW_NS      = 1000000, // 10^6ns = 1ms timeout
    CACHE_CPLT_ALIGN_POLL_DELAY_HW_NS        = 10000,   // 10us poll loop delay
    CACHE_CPLT_ALIGN_POLL_DELAY_SIM_CYCLE    = 32000,   // 320k sim cycle delay

    CACHE_CLK_START_POLL_TIMEOUT_HW_NS       = 1000000, // 10^6ns = 1ms timeout
    CACHE_CLK_START_POLL_DELAY_HW_NS         = 10000,   // 10us poll loop delay
    CACHE_CLK_START_POLL_DELAY_SIM_CYCLE     = 32000    // 320k sim cycle delay
};

//------------------------------------------------------------------------------
// Procedure: Quad Clock Start
//------------------------------------------------------------------------------

fapi2::ReturnCode
p9_hcd_cache_startclocks(
    const fapi2::Target<fapi2::TARGET_TYPE_EQ>& i_target)
{
    FAPI_INF(">>p9_hcd_cache_startclocks");
    fapi2::buffer<uint64_t>                     l_qcsr;
    fapi2::buffer<uint64_t>                     l_qssr;
    fapi2::buffer<uint64_t>                     l_data64;
    uint64_t                                    l_region_clock;
    uint64_t                                    l_l2sync_clock;
    uint64_t                                    l_l2pscom_mask;
    uint64_t                                    l_l3pscom_mask;
    uint32_t                                    l_scom_addr;
    uint32_t                                    l_poll_loops;
    uint32_t                                    l_attr_system_id     = 0;
    uint8_t                                     l_attr_group_id      = 0;
    uint8_t                                     l_attr_chip_id       = 0;
    uint8_t                                     l_attr_chip_unit_pos = 0;
    uint8_t                                     l_attr_system_ipl_phase;
    uint8_t                                     l_attr_sys_force_all_cores;
    uint8_t                                     l_attr_dd1_skip_flushmode_inhibit_drop;
    fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP> l_chip =
        i_target.getParent<fapi2::TARGET_TYPE_PROC_CHIP>();
    fapi2::Target<fapi2::TARGET_TYPE_PERV>      l_perv =
        i_target.getParent<fapi2::TARGET_TYPE_PERV>();
    fapi2::Target<fapi2::TARGET_TYPE_SYSTEM>    l_sys;
    auto l_core_functional_vector =
        i_target.getChildren<fapi2::TARGET_TYPE_CORE>
        (fapi2::TARGET_STATE_FUNCTIONAL);

#ifndef __PPE__

    uint8_t                                     l_attr_is_simulation;

    FAPI_TRY(FAPI_ATTR_GET(fapi2::ATTR_IS_SIMULATION,            l_sys,
                           l_attr_is_simulation));

#endif

    FAPI_TRY(FAPI_ATTR_GET(fapi2::ATTR_SYSTEM_IPL_PHASE,         l_sys,
                           l_attr_system_ipl_phase));
    FAPI_TRY(FAPI_ATTR_GET(fapi2::ATTR_SYS_FORCE_ALL_CORES,      l_sys,
                           l_attr_sys_force_all_cores));
    FAPI_TRY(FAPI_ATTR_GET(fapi2::ATTR_CHIP_EC_FEATURE_HW388878, l_chip,
                           l_attr_dd1_skip_flushmode_inhibit_drop));
    FAPI_TRY(FAPI_ATTR_GET(fapi2::ATTR_PROC_FABRIC_GROUP_ID,     l_chip,
                           l_attr_group_id));
    FAPI_TRY(FAPI_ATTR_GET(fapi2::ATTR_PROC_FABRIC_CHIP_ID,      l_chip,
                           l_attr_chip_id));
    FAPI_TRY(FAPI_ATTR_GET(fapi2::ATTR_PROC_FABRIC_SYSTEM_ID,    l_chip,
                           l_attr_system_id));
    FAPI_TRY(FAPI_ATTR_GET(fapi2::ATTR_CHIP_UNIT_POS,            l_perv,
                           l_attr_chip_unit_pos));
    l_attr_chip_unit_pos = l_attr_chip_unit_pos - p9hcd::PERV_TO_EQ_POS_OFFSET;

    FAPI_TRY(getScom(l_chip, PU_OCB_OCI_QCSR_SCOM, l_qcsr));
    FAPI_DBG("Working on cache[%d], good EXs in QCSR[%016llX]",
             l_attr_chip_unit_pos, l_qcsr);

    // -----------------------------
    // Prepare to start cache clocks
    // -----------------------------
    // QCCR[0/4] EDRAM_ENABLE_DC
    // QCCR[1/5] EDRAM_VWL_ENABLE_DC
    // QCCR[2/6] L3_EX0/1_EDRAM_VROW_VBLH_ENABLE_DC
    // QCCR[3/7] EDRAM_VPP_ENABLE_DC
    // 0x0 -> 0x8 -> 0xC -> 0xE -> 0xF to turn on edram
    // stagger EDRAM turn-on per EX (not both at same time)

    l_region_clock = p9hcd::CLK_REGION_ALL_BUT_EX_DPLL;
    l_l2sync_clock = 0;
    l_l2pscom_mask = 0;
    l_l3pscom_mask = 0;

    if (l_qcsr & BIT64(l_attr_chip_unit_pos << 1))
    {
        l_qssr |= (BIT64(l_attr_chip_unit_pos << 1) |
                   BIT64(l_attr_chip_unit_pos + 14));
        l_region_clock |= p9hcd::CLK_REGION_EX0_L2_L3_REFR;
        l_l2sync_clock |= BIT64(36);
        FAPI_DBG("Sequence EX0 EDRAM enables via QPPM_QCCR[0-3]");
        FAPI_TRY(putScom(i_target, EQ_QPPM_QCCR_WOR, MASK_SET(0)));
        FAPI_TRY(fapi2::delay(12000, 200));
        FAPI_TRY(putScom(i_target, EQ_QPPM_QCCR_WOR, MASK_SET(1)));
        FAPI_TRY(fapi2::delay(1000, 200));
        FAPI_TRY(putScom(i_target, EQ_QPPM_QCCR_WOR, MASK_SET(2)));
        FAPI_TRY(fapi2::delay(4000, 200));
        FAPI_TRY(putScom(i_target, EQ_QPPM_QCCR_WOR, MASK_SET(3)));
        FAPI_TRY(fapi2::delay(1000, 200));
    }
    else
    {
        l_l2pscom_mask |= (BIT64(2) | BIT64(10));
        l_l3pscom_mask |= (BIT64(4) | BIT64(6) | BIT64(8));
    }

    if (l_qcsr & BIT64((l_attr_chip_unit_pos << 1) + 1))
    {
        // Now we only skip L2 if we are in HB mode(not force_all_cores)
        // AND we are give second configured EX to process
        //   (l_l2sync_clock is already initialized by EX0)
        // Note: more details see comments in p9_hcd_cache_scominit.C
        if (!l_attr_sys_force_all_cores && l_l2sync_clock)
        {
            l_region_clock |= p9hcd::CLK_REGION_EX1_L3_REFR;
        }
        else
        {
            l_qssr |= (BIT64((l_attr_chip_unit_pos << 1) + 1) |
                       BIT64(l_attr_chip_unit_pos + 14));
            l_region_clock |= p9hcd::CLK_REGION_EX1_L2_L3_REFR;
            l_l2sync_clock |= BIT64(37);
        }

        FAPI_DBG("Sequence EX1 EDRAM enables via QPPM_QCCR[4-7]");
        FAPI_TRY(putScom(i_target, EQ_QPPM_QCCR_WOR, MASK_SET(4)));
        FAPI_TRY(fapi2::delay(12000, 200));
        FAPI_TRY(putScom(i_target, EQ_QPPM_QCCR_WOR, MASK_SET(5)));
        FAPI_TRY(fapi2::delay(1000, 200));
        FAPI_TRY(putScom(i_target, EQ_QPPM_QCCR_WOR, MASK_SET(6)));
        FAPI_TRY(fapi2::delay(4000, 200));
        FAPI_TRY(putScom(i_target, EQ_QPPM_QCCR_WOR, MASK_SET(7)));
        FAPI_TRY(fapi2::delay(1000, 200));
    }
    else
    {
        l_l2pscom_mask |= (BIT64(3) | BIT64(11));
        l_l3pscom_mask |= (BIT64(5) | BIT64(7) | BIT64(9));
    }

    FAPI_DBG("Assert cache EX1 ID bit2 via CPLT_CTRL0[6]");
    FAPI_TRY(putScom(i_target, EQ_CPLT_CTRL0_OR, MASK_SET(6)));

    FAPI_DBG("Set inop_align/wait/wait_cycles via OPCG_ALIGN[0-3,12-19,52-63]");
    FAPI_TRY(getScom(i_target, EQ_OPCG_ALIGN, l_data64));
    l_data64.insertFromRight<0, 4>(0x5).
    insertFromRight<12, 8>(0x0).
    insertFromRight<52, 12>(0x10);
    FAPI_TRY(putScom(i_target, EQ_OPCG_ALIGN, l_data64));

    FAPI_DBG("Drop partial good fences via CPLT_CTRL1[4,5,6/7,8/9,10,11,12/13]");
    FAPI_TRY(putScom(i_target, EQ_CPLT_CTRL1_CLEAR,
                     (l_region_clock | p9hcd::CLK_REGION_ANEP)));

    FAPI_DBG("Drop vital fence via CPLT_CTRL1[3]");
    FAPI_TRY(putScom(i_target, EQ_CPLT_CTRL1_CLEAR, MASK_SET(3)));

    FAPI_DBG("Assert EX-L2 clock sync enables via QPPM_EXCGCR[36,37]");
    FAPI_TRY(putScom(i_target, EQ_QPPM_EXCGCR_OR, l_l2sync_clock));

    FAPI_DBG("Poll for EX-L2 clock sync dones via QPPM_QACSR[36,37]");
    l_poll_loops = CACHE_CLK_SYNC_POLL_TIMEOUT_HW_NS /
                   CACHE_CLK_SYNC_POLL_DELAY_HW_NS;

    do
    {
        fapi2::delay(CACHE_CLK_SYNC_POLL_DELAY_HW_NS,
                     CACHE_CLK_SYNC_POLL_DELAY_SIM_CYCLE);

        FAPI_TRY(getScom(i_target, EQ_QPPM_QACSR, l_data64));
    }
    while(((l_data64 & l_l2sync_clock) != l_l2sync_clock) &&
          ((--l_poll_loops) != 0));

    FAPI_ASSERT((l_poll_loops != 0),
                fapi2::CACHE_CLK_SYNC_TIMEOUT()
                .set_EQ_QPPM_QACSR(l_data64)
                .set_CACHE_CLK_SYNC_POLL_DELAY_HW_NS(CACHE_CLK_SYNC_POLL_DELAY_HW_NS)
                .set_CACHE_CLK_SYNC_POLL_TIMEOUT_HW_NS(CACHE_CLK_SYNC_POLL_TIMEOUT_HW_NS)
                .set_CACHE_TARGET(i_target),
                "EX-L2 Clock Sync Timeout");
    FAPI_DBG("EX-L2 clock sync done");

    FAPI_DBG("Reset abstclk & syncclk muxsel(io_clk_sel) via CPLT_CTRL0[0:1]");
    FAPI_TRY(putScom(i_target, EQ_CPLT_CTRL0_CLEAR, MASK_CLR(0, 2, 3)));

    if (l_attr_system_ipl_phase !=
        fapi2::ENUM_ATTR_SYSTEM_IPL_PHASE_CACHE_CONTAINED)
    {
        FAPI_DBG("Set fabric group ID[%x] chip ID[%x] system ID[%x]",
                 l_attr_group_id, l_attr_chip_id, l_attr_system_id);
        FAPI_TRY(getScom(i_target, EQ_CPLT_CONF0, l_data64));
        l_data64.insertFromRight<48, 4>(l_attr_group_id).
        insertFromRight<52, 3>(l_attr_chip_id).
        insertFromRight<56, 5>(l_attr_system_id);
        FAPI_TRY(putScom(i_target, EQ_CPLT_CONF0, l_data64));
    }

    // -------------------------------
    // Align chiplets
    // -------------------------------

    FAPI_DBG("Assert flushmode_inhibit via CPLT_CTRL0[2]");
    FAPI_TRY(putScom(i_target, EQ_CPLT_CTRL0_OR, MASK_SET(2)));

    FAPI_DBG("Assert force_align via CPLT_CTRL0[3]");
    FAPI_TRY(putScom(i_target, EQ_CPLT_CTRL0_OR, MASK_SET(3)));

    FAPI_DBG("Set then unset clear_chiplet_is_aligned via SYNC_CONFIG[7]");
    FAPI_TRY(getScom(i_target, EQ_SYNC_CONFIG, l_data64));
    FAPI_TRY(putScom(i_target, EQ_SYNC_CONFIG, DATA_SET(7)));
    FAPI_TRY(putScom(i_target, EQ_SYNC_CONFIG, DATA_UNSET(7)));

    FAPI_TRY(fapi2::delay(
                 CACHE_CPLT_ALIGN_DELAY_CACHE_CYCLES * p9hcd::CLK_PERIOD_CORE2CACHE *
                 p9hcd::CLK_PERIOD_250PS / 1000,
                 CACHE_CPLT_ALIGN_DELAY_CACHE_CYCLES * p9hcd::CLK_PERIOD_CORE2CACHE *
                 p9hcd::SIM_CYCLE_4U4D));

    FAPI_DBG("Poll for cache chiplet aligned");
    l_poll_loops = CACHE_CPLT_ALIGN_POLL_TIMEOUT_HW_NS /
                   CACHE_CPLT_ALIGN_POLL_DELAY_HW_NS;

    do
    {
        FAPI_TRY(getScom(i_target, EQ_CPLT_STAT0, l_data64));

        fapi2::delay(CACHE_CPLT_ALIGN_POLL_DELAY_HW_NS,
                     CACHE_CPLT_ALIGN_POLL_DELAY_SIM_CYCLE);
    }
    while((l_data64.getBit<9>() != 1) && ((--l_poll_loops) != 0));

    FAPI_ASSERT((l_poll_loops != 0),
                fapi2::CACHE_CPLT_ALIGN_TIMEOUT()
                .set_EQ_CPLT_STAT0(l_data64)
                .set_CACHE_CPLT_ALIGN_POLL_DELAY_HW_NS(CACHE_CPLT_ALIGN_POLL_DELAY_HW_NS)
                .set_CACHE_CPLT_ALIGN_POLL_TIMEOUT_HW_NS(CACHE_CPLT_ALIGN_POLL_TIMEOUT_HW_NS)
                .set_CACHE_TARGET(i_target),
                "Cache Chiplets Aligned Timeout");
    FAPI_DBG("Cache chiplets aligned now");

    FAPI_DBG("Drop force_align via CPLT_CTRL0[3]");
    FAPI_TRY(putScom(i_target, EQ_CPLT_CTRL0_CLEAR, MASK_SET(3)));

    // -------------------------------
    // Start cache clocks
    // -------------------------------

    FAPI_DBG("Clear all bits prior start cache clocks via SCAN_REGION_TYPE");
    FAPI_TRY(putScom(i_target, EQ_SCAN_REGION_TYPE, MASK_ZERO));

    FAPI_DBG("Start cache clocks(all but anep+dpll) via CLK_REGION");
    l_data64 = (p9hcd::CLK_START_CMD |
                l_region_clock       |
                p9hcd::CLK_THOLD_ALL);
    FAPI_TRY(putScom(i_target, EQ_CLK_REGION, l_data64));

    FAPI_DBG("Poll for cache clocks running via CPLT_STAT0[8]");
    l_poll_loops = CACHE_CLK_START_POLL_TIMEOUT_HW_NS /
                   CACHE_CLK_START_POLL_DELAY_HW_NS;

    do
    {
        fapi2::delay(CACHE_CLK_START_POLL_DELAY_HW_NS,
                     CACHE_CLK_START_POLL_DELAY_SIM_CYCLE);

        FAPI_TRY(getScom(i_target, EQ_CPLT_STAT0, l_data64));
    }
    while((l_data64.getBit<8>() != 1) && ((--l_poll_loops) != 0));

    FAPI_ASSERT((l_poll_loops != 0),
                fapi2::CACHE_CLK_START_TIMEOUT()
                .set_EQ_CPLT_STAT(l_data64)
                .set_CACHE_CLK_START_POLL_DELAY_HW_NS(CACHE_CLK_START_POLL_DELAY_HW_NS)
                .set_CACHE_CLK_START_POLL_TIMEOUT_HW_NS(CACHE_CLK_START_POLL_TIMEOUT_HW_NS)
                .set_CACHE_TARGET(i_target),
                "Cache Clock Start Timeout");

    FAPI_DBG("Check cache clocks running");
    FAPI_TRY(getScom(i_target, EQ_CLOCK_STAT_SL, l_data64));

    FAPI_ASSERT(((l_data64 & l_region_clock) == 0),
                fapi2::CACHE_CLK_START_FAILED()
                .set_EQ_CLK_STAT(l_data64)
                .set_CACHE_TARGET(i_target),
                "Cache Clock Start Failed");
    FAPI_DBG("Cache clocks running now");

    // -------------------------------
    // Cleaning up
    // -------------------------------

#ifndef __PPE__

    // ignore xstop checkstop in sim
    if (!l_attr_is_simulation)
    {

#endif

        FAPI_DBG("Check the Global Checkstop FIR of Cache Chiplet");
        FAPI_TRY(getScom(i_target, EQ_XFIR, l_data64));
        FAPI_ASSERT(((l_data64 & BITS64(0, 27)) == 0),
                    fapi2::CACHE_CHECKSTOP_AFTER_CLK_START()
                    .set_EQ_XFIR(l_data64)
                    .set_CACHE_TARGET(i_target),
                    "Cache Chiplet Checkstop");

#ifndef __PPE__

    }

#endif

    if (!l_attr_dd1_skip_flushmode_inhibit_drop)
    {
        FAPI_DBG("Drop flushmode_inhibit via CPLT_CTRL0[2]");
        FAPI_TRY(putScom(i_target, EQ_CPLT_CTRL0_CLEAR, MASK_SET(2)));
    }

    FAPI_DBG("Drop partial good and assert partial bad L2/L3 pscom masks");
    l_data64 = (l_l2pscom_mask | l_l3pscom_mask);
    FAPI_TRY(putScom(i_target, EQ_RING_FENCE_MASK_LATCH_REG, l_data64));

    // Allow the CME to access the PCB Slave NET_CTRL registers
    for(auto& it : l_core_functional_vector)
    {
        FAPI_TRY(FAPI_ATTR_GET(fapi2::ATTR_CHIP_UNIT_POS,
                               it.getParent<fapi2::TARGET_TYPE_PERV>(),
                               l_attr_chip_unit_pos));
        FAPI_DBG("Drop core[%d] PCB Mux Disable via C_SLAVE_CONFIG[7]",
                 (l_attr_chip_unit_pos - p9hcd::PERV_TO_CORE_POS_OFFSET));
        l_scom_addr = (C_SLAVE_CONFIG_REG + (0x1000000 *
                                             (l_attr_chip_unit_pos - p9hcd::PERV_TO_CORE_POS_OFFSET)));
        FAPI_TRY(getScom(l_chip, l_scom_addr, l_data64));
        FAPI_TRY(putScom(l_chip, l_scom_addr, DATA_UNSET(7)));
    }

    // -------------------------------
    // Update Status
    // -------------------------------

    FAPI_DBG("Set cache as ready to run in STOP history register");
    FAPI_TRY(putScom(i_target, EQ_PPM_SSHSRC, 0));

    FAPI_DBG("Set cache as running in QSSR");
    FAPI_TRY(putScom(l_chip, PU_OCB_OCI_QSSR_SCOM1, l_qssr));

fapi_try_exit:

    FAPI_INF("<<p9_hcd_cache_startclocks");
    return fapi2::current_err;
}

