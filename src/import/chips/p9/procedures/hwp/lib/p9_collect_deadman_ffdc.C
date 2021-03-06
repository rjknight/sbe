/* IBM_PROLOG_BEGIN_TAG                                                   */
/* This is an automatically generated prolog.                             */
/*                                                                        */
/* $Source: src/import/chips/p9/procedures/hwp/lib/p9_collect_deadman_ffdc.C $ */
/*                                                                        */
/* OpenPOWER sbe Project                                                  */
/*                                                                        */
/* Contributors Listed Below - COPYRIGHT 2017                             */
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
/// @file  p9_collect_deadman_ffdc.C
/// @brief Collects PPE State FFDC based on fails in the Deadman Timer
///
/// *HWP HW Owner        : Greg Still <stillgs@us.ibm.com>
/// *HWP HW Backup Owner : Brian Vanderpool <vanderp@us.ibm.com>
/// *HWP FW Owner        : Amit Tendolkar <amit.tendolkar@in.ibm.com>
/// *HWP Team            : PM
/// *HWP Level           : 2
/// *HWP Consumed by     : SBE
//-----------------------------------------------------------------------------
// Includes
//-----------------------------------------------------------------------------
#include <fapi2.H>
#include <p9_collect_deadman_ffdc.H>
#include <p9_sbe_ppe_ffdc.H>
#include <p9_ppe_defs.H>
#include <p9_eq_clear_atomic_lock.H>
#include <p9_quad_scom_addresses.H>

static const uint32_t DM_FFDC_SCOMS_PU_MAX = 4;
static const uint32_t DM_FFDC_SCOMS_EX_MAX = 3;

// @note This enum is used to order/index, as well as pack, data added to
//       the array of 64 bit buffers. To optimize on space, relevant SCOM
//       data bits are packed into bits 0-31 or 32-63 of each element
//       in the array. SCOMs where all 64 bits are relevant are not packed.
//       This order and packing matches that of the SCOMs in error xml for
//       RC_CHECK_MASTER_STOP15_FAILED and if need be, both should change in
//       lock-step.
typedef enum
{
    FFDC_IDX_PU_SCOM_FIRST = 0,                   // 0
    FFDC_____PU_OCB_OCI_OCCFLG32__CCSR32 = FFDC_IDX_PU_SCOM_FIRST,
    FFDC_____PU_OCB_OCI_QCSR32__QSSR32,           // 1
    FFDC_IDX_PU_SCOM_MAX,                         // 2
    FFDC_IDX_EX_SCOM_FIRST = FFDC_IDX_PU_SCOM_MAX,
    FFDC_____EQ_PPM_SSHSRC32__EX_CME_LFIR32 = FFDC_IDX_EX_SCOM_FIRST,
    FFDC_____EX_CME_SICR_64,                      // 3
    FFDC_____EX_CME_SISR_64,                      // 4
    FFDC_IDX_EX_SCOM_MAX,                         // 5
    FFDC_____EQ_ATOMIC_LOCK_REG = FFDC_IDX_EX_SCOM_MAX,
    FFDC_____EC0_PPM_SSHSRC32__EC1_PPM_SSHSRC32,  // 6
    FFDC_IDX_SCOM_MAX                             // 7
}   dmanFfdcScomIndex_t;


fapi2::ReturnCode
p9_collect_deadman_ffdc (
    const fapi2::Target<fapi2::TARGET_TYPE_CORE>& i_core,
    const uint32_t                                i_reason )
{
    FAPI_IMP (">> p9_collect_deadman_ffdc RC: 0x%.8X", i_reason);
    fapi2::ReturnCode l_rc;

    auto l_chip = i_core.getParent<fapi2::TARGET_TYPE_PROC_CHIP>();
    auto l_ex = i_core.getParent<fapi2::TARGET_TYPE_EX>();
    auto l_eq = i_core.getParent<fapi2::TARGET_TYPE_EQ>();

    fapi2::buffer<uint64_t> l_data64;

    // Address ordering is relative to PU SCOMs in dmanFfdcScomIndex_t
    const uint64_t l_dmFfdcPUScomAddrs[DM_FFDC_SCOMS_PU_MAX] =
    {
        PU_OCB_OCI_OCCFLG_SCOM, // 0x0006C08A
        PU_OCB_OCI_CCSR_SCOM,   // 0x0006C090
        PU_OCB_OCI_QCSR_SCOM,   // 0x0006C094
        PU_OCB_OCI_QSSR_SCOM    // 0x0006C098
    };

    // Address ordering is relative to EX SCOMs in dmanFfdcScomIndex_t
    const uint64_t l_dmFfdcEXScomAddrs[DM_FFDC_SCOMS_EX_MAX] =
    {
        EX_CME_SCOM_LFIR,       // 0x10012000
        EX_CME_SCOM_SICR_SCOM,  // 0x1001203D
        EX_CME_LCL_SISR_SCOM    // 0x1001204C
    };

    // Buffer to hold the packed SCOM data based on dmanFfdcScomIndex_t
    fapi2::buffer<uint64_t> l_dmanFfdcScoms[FFDC_IDX_SCOM_MAX] = {default_64};

    // Vectors to hold PPE state for SGPE and CME
    std::vector<uint64_t> l_v_reg_ffdc_cme;
    std::vector<uint64_t> l_v_reg_ffdc_sgpe;

    // Get SGPE internal state
    l_rc = p9_sbe_ppe_ffdc ( l_chip,
                             SGPE_BASE_ADDRESS,
                             l_v_reg_ffdc_sgpe );

    // Read and add PU SCOMs to the FFDC buffer
    // Incr address for every whereas buffer index for every other iteration
    // to allow packing 32 bit SCOM data per 64 bit buffer
    for (uint8_t l_addrIdx = 0, l_buffIdx = FFDC_IDX_PU_SCOM_FIRST;
         ((l_buffIdx < FFDC_IDX_PU_SCOM_MAX) ||
          (l_addrIdx < DM_FFDC_SCOMS_PU_MAX));
         l_buffIdx += ++l_addrIdx / 2 )
    {
        l_rc = fapi2::getScom ( l_chip,
                                l_dmFfdcPUScomAddrs[l_addrIdx],
                                l_data64 );

        if (l_rc == fapi2::FAPI2_RC_SUCCESS)
        {
            // Copy bits 0-31 of SCOM data to the FFDC buffer entry at bits:
            (l_addrIdx & 0x01) ?
            // Odd Index: Copy data bits 0-31 to bits 32-63 of buffer
            (l_data64.extract<0, 32, 32> (l_dmanFfdcScoms[l_buffIdx])) :
            // Even Indiex: Copy data bits 0-31 to bits 0-31 of buffer
            (l_data64.extract<0, 32, 0>  (l_dmanFfdcScoms[l_buffIdx]));  // 0-31
        }
        else // data already defaulted, optimize after debug
        {
            FAPI_ERR ( "Fail PU SCOM Addr 0x%08X %08X",
                       (uint32_t)(l_dmFfdcPUScomAddrs[l_addrIdx] >> 32),
                       (uint32_t)(l_dmFfdcPUScomAddrs[l_addrIdx] & 0xFFFFFFFF) );
        }
    }

    // Drop the EQ atomic lock to be able to access FFDC regs
    l_rc = p9_clear_atomic_lock (l_eq);

    if (l_rc == fapi2::FAPI2_RC_SUCCESS)
    {
        FAPI_IMP ("Grabbed eq atomic lock!");

        // Read & add EQ_PPM_SSHSRC bits 0-31 to the FFDC buffer
        l_rc = fapi2::getScom ( l_eq,
                                EQ_PPM_SSHSRC,
                                l_data64 );

        if (l_rc == fapi2::FAPI2_RC_SUCCESS)
        {
            // pack bits 0-31 of SCOM data to bits 0-31 of buffer entry
            l_data64.extract<0, 32, 0> (
                l_dmanFfdcScoms[FFDC_____EQ_PPM_SSHSRC32__EX_CME_LFIR32] );
        }

        // else just add the default

        // Read & add CME (EX) SCOMs to the FFDC buffer
        // Note that unlike pu scoms above, address and buffer indices increment
        // in lock step. The first entry is packed with the prev eq scom data
        for ( uint8_t l_addrIdx = 0, l_buffIdx = FFDC_IDX_EX_SCOM_FIRST;
              ((l_buffIdx < FFDC_IDX_EX_SCOM_MAX) ||
               (l_addrIdx < DM_FFDC_SCOMS_EX_MAX));
              ++l_buffIdx, ++l_addrIdx )
        {
            l_rc = fapi2::getScom ( l_ex,
                                    l_dmFfdcEXScomAddrs[l_addrIdx],
                                    l_data64 );

            if (l_rc == fapi2::FAPI2_RC_SUCCESS)
            {
                if (l_buffIdx == FFDC_____EQ_PPM_SSHSRC32__EX_CME_LFIR32)
                {
                    // pack bits 0-31 of SCOM data to bits 32-63 of buffer entry
                    l_data64.extract<0, 32, 32> (l_dmanFfdcScoms[l_buffIdx]);
                }
                else
                {
                    // for other 2 EX SCOMs, copy all 64 bits of data to buffer
                    l_dmanFfdcScoms[l_buffIdx] = l_data64;
                }
            }

            // else just add the default
        }

        // Read the CME state
        uint8_t l_exChipUnitPos = 0;
        FAPI_ATTR_GET ( fapi2::ATTR_CHIP_UNIT_POS, l_ex, l_exChipUnitPos );
        uint64_t l_cmeBaseAddr = getCmeBaseAddress (l_exChipUnitPos);
        l_rc = p9_sbe_ppe_ffdc ( l_chip,
                                 l_cmeBaseAddr,
                                 l_v_reg_ffdc_cme );
    }
    else
    {
        FAPI_ERR ("Error grabbing eq atomic lock! eq/ex SCOMs defaulted!");
    }

    // Add the quad atomic lock reg to FFDC
    l_rc = fapi2::getScom ( l_eq, EQ_ATOMIC_LOCK_REG,
                            l_dmanFfdcScoms[FFDC_____EQ_ATOMIC_LOCK_REG] );

    // Read & add Core SCOM(s) to the FFDC buffer
    const fapi2::Target<fapi2::TARGET_TYPE_SYSTEM> FAPI_SYSTEM;
    fapi2::ATTR_FUSED_CORE_MODE_Type l_attr_fused_mode;
    FAPI_ATTR_GET ( fapi2::ATTR_FUSED_CORE_MODE,
                    FAPI_SYSTEM,
                    l_attr_fused_mode );

    for ( auto& l_core : l_ex.getChildren <fapi2::TARGET_TYPE_CORE>
          (fapi2::TARGET_STATE_FUNCTIONAL) )
    {
        bool l_read = false;

        if (l_core == i_core)
        {
            // always read the core that was saved away
            l_read = true;
        }
        else
        {
            // read sibling core data only if in fused mode
            if (l_attr_fused_mode == fapi2::ENUM_ATTR_FUSED_CORE_MODE_CORE_FUSED)
            {
                l_read = true;
            }
        }

        if (l_read == true)
        {
            l_rc = fapi2::getScom ( l_core,
                                    C_PPM_SSHSRC,
                                    l_data64 );

            if (l_rc == fapi2::FAPI2_RC_SUCCESS)
            {
                uint8_t l_chipUnitPos = 0;
                FAPI_ATTR_GET ( fapi2::ATTR_CHIP_UNIT_POS,
                                l_core,
                                l_chipUnitPos );

                (l_chipUnitPos & 0x01) ?
                // Odd: pack bits 0-31 of SCOM data to bits 32-63 of buffer entry
                (l_data64.extract<0, 32, 32> (
                     l_dmanFfdcScoms[FFDC_____EC0_PPM_SSHSRC32__EC1_PPM_SSHSRC32] )) :
                // Even: pack bits 0-31 of SCOM data to bits 0-31 of buffer entry
                (l_data64.extract<0, 32, 0> (
                     l_dmanFfdcScoms[FFDC_____EC0_PPM_SSHSRC32__EC1_PPM_SSHSRC32] ));
            }

            // else just use the default
        }

        // else just use the default
    }

    // Add FFDC to a single FAPI RC, to avoid code bloat from multiple
    // generated ffdc classes & error info classes per FAPI RC.
    // Note, we are adding 16 FFDC members. Limit is 20.
    FAPI_ASSERT ( false,
                  fapi2::CHECK_MASTER_STOP15_DEADMAN_TIMEOUT ()
                  .set_DEADMAN_TIMEOUT_REASON (i_reason)
                  .set_CORE_TARGET (i_core)
                  .set_PU_OCB_OCI_OCCFLG__PU_OCB_OCI_CCSR (
                      l_dmanFfdcScoms[FFDC_____PU_OCB_OCI_OCCFLG32__CCSR32])
                  .set_PU_OCB_OCI_QCSR__PU_OCB_OCI_QSSR (
                      l_dmanFfdcScoms[FFDC_____PU_OCB_OCI_QCSR32__QSSR32])
                  .set_EQ_PPM_SSHSRC__EX_CME_SCOM_LFIR (
                      l_dmanFfdcScoms[FFDC_____EQ_PPM_SSHSRC32__EX_CME_LFIR32])
                  .set_EX_CME_SCOM_SICR_SCOM (
                      l_dmanFfdcScoms[FFDC_____EX_CME_SICR_64])
                  .set_EX_CME_LCL_SISR_SCOM (
                      l_dmanFfdcScoms[FFDC_____EX_CME_SISR_64])
                  .set_EQ_ATOMIC_LOCK_REG (
                      l_dmanFfdcScoms[FFDC_____EQ_ATOMIC_LOCK_REG])
                  .set_C0_PPM_SSHSRC__C1_PPM_SSHSRC (
                      l_dmanFfdcScoms[FFDC_____EC0_PPM_SSHSRC32__EC1_PPM_SSHSRC32])
                  .set_SGPE_XSR__IAR (l_v_reg_ffdc_sgpe[REG_FFDC_IDX_XSR_IAR])
                  .set_SGPE_IR__EDR (l_v_reg_ffdc_sgpe[REG_FFDC_IDX_IR_EDR])
                  .set_SGPE_LR__SPRG0 (l_v_reg_ffdc_sgpe[REG_FFDC_IDX_LR_SPRG0])
                  .set_SGPE_SRR0__SRR1 (l_v_reg_ffdc_sgpe[REG_FFDC_IDX_SRR0_SRR1])
                  .set_CME_XSR__IAR (l_v_reg_ffdc_cme[REG_FFDC_IDX_XSR_IAR])
                  .set_CME_IR__EDR (l_v_reg_ffdc_cme[REG_FFDC_IDX_IR_EDR])
                  .set_CME_LR__SPRG0 (l_v_reg_ffdc_cme[REG_FFDC_IDX_LR_SPRG0])
                  .set_CME_SRR0__SRR1 (l_v_reg_ffdc_cme[REG_FFDC_IDX_SRR0_SRR1]),
                  "Check Master STOP15 error 0x%.8X", i_reason );

fapi_try_exit:
    FAPI_IMP ("<< p9_collect_deadman_ffdc");
    return fapi2::current_err;
}
