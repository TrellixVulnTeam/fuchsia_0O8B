/******************************************************************************
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
 * Copyright(c) 2016 - 2017 Intel Deutschland GmbH
 * Copyright(c) 2018        Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *****************************************************************************/
#include <zircon/status.h>

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fw/acpi.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fw/dbg.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fw/img.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-csr.h" /* for iwl_mvm_rx_card_state_notif */
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-debug.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-io.h" /* for iwl_mvm_rx_card_state_notif */
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-modparams.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-nvm-parse.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-op-mode.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-phy-db.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-prph.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-trans.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/mvm/mvm.h"
#ifdef CPTCFG_IWLWIFI_DEVICE_TESTMODE
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fw/testmode.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-dnt-cfg.h"
#endif

#define MVM_UCODE_ALIVE_TIMEOUT ZX_SEC(3)  // (HZ * CPTCFG_IWL_TIMEOUT_FACTOR)
#define MVM_UCODE_CALIB_TIMEOUT ZX_SEC(6)  // 2 * HZ * CPTCFG_IWL_TIMEOUT_FACTOR)

#define UCODE_VALID_OK cpu_to_le32(0x1)

struct iwl_mvm_alive_data {
  bool valid;
  uint32_t scd_base_addr;
};

/* set device type and latency */
static zx_status_t iwl_set_soc_latency(struct iwl_mvm* mvm) {
  struct iwl_soc_configuration_cmd cmd;
  zx_status_t ret;

  cmd.device_type = (mvm->trans->cfg->integrated) ? cpu_to_le32(SOC_CONFIG_CMD_INTEGRATED)
                                                  : cpu_to_le32(SOC_CONFIG_CMD_DISCRETE);
  cmd.soc_latency = cpu_to_le32(mvm->trans->cfg->soc_latency);

  ret = iwl_mvm_send_cmd_pdu(mvm, iwl_cmd_id(SOC_CONFIGURATION_CMD, SYSTEM_GROUP, 0), 0,
                             sizeof(cmd), &cmd);
  if (ret) {
    IWL_ERR(mvm, "Failed to set soc latency: %d\n", ret);
  }
  return ret;
}

static int iwl_send_tx_ant_cfg(struct iwl_mvm* mvm, uint8_t valid_tx_ant) {
  struct iwl_tx_ant_cfg_cmd tx_ant_cmd = {
      .valid = cpu_to_le32(valid_tx_ant),
  };

  IWL_DEBUG_FW(mvm, "select valid tx ant: %u\n", valid_tx_ant);
  return iwl_mvm_send_cmd_pdu(mvm, TX_ANT_CONFIGURATION_CMD, 0, sizeof(tx_ant_cmd), &tx_ant_cmd);
}

#if 0   // NEEDS_PORTING
static int iwl_send_rss_cfg_cmd(struct iwl_mvm* mvm) {
    int i;
    struct iwl_rss_config_cmd cmd = {
        .flags = cpu_to_le32(IWL_RSS_ENABLE),
        .hash_mask = IWL_RSS_HASH_TYPE_IPV4_TCP | IWL_RSS_HASH_TYPE_IPV4_UDP |
                     IWL_RSS_HASH_TYPE_IPV4_PAYLOAD | IWL_RSS_HASH_TYPE_IPV6_TCP |
                     IWL_RSS_HASH_TYPE_IPV6_UDP | IWL_RSS_HASH_TYPE_IPV6_PAYLOAD,
    };

    if (mvm->trans->num_rx_queues == 1) { return 0; }

    /* Do not direct RSS traffic to Q 0 which is our fallback queue */
    for (i = 0; i < ARRAY_SIZE(cmd.indirection_table); i++) {
        cmd.indirection_table[i] = 1 + (i % (mvm->trans->num_rx_queues - 1));
    }
    netdev_rss_key_fill(cmd.secret_key, sizeof(cmd.secret_key));

    return iwl_mvm_send_cmd_pdu(mvm, RSS_CONFIG_CMD, 0, sizeof(cmd), &cmd);
}

static int iwl_configure_rxq(struct iwl_mvm* mvm) {
    int i, num_queues, size;
    struct iwl_rfh_queue_config* cmd;

    /* Do not configure default queue, it is configured via context info */
    num_queues = mvm->trans->num_rx_queues - 1;

    size = sizeof(*cmd) + num_queues * sizeof(struct iwl_rfh_queue_data);

    cmd = kzalloc(size, GFP_KERNEL);
    if (!cmd) { return -ENOMEM; }

    cmd->num_queues = num_queues;

    for (i = 0; i < num_queues; i++) {
        struct iwl_trans_rxq_dma_data data;

        cmd->data[i].q_num = i + 1;
        iwl_trans_get_rxq_dma_data(mvm->trans, i + 1, &data);

        cmd->data[i].fr_bd_cb = cpu_to_le64(data.fr_bd_cb);
        cmd->data[i].urbd_stts_wrptr = cpu_to_le64(data.urbd_stts_wrptr);
        cmd->data[i].ur_bd_cb = cpu_to_le64(data.ur_bd_cb);
        cmd->data[i].fr_bd_wid = cpu_to_le32(data.fr_bd_wid);
    }

    return iwl_mvm_send_cmd_pdu(mvm, WIDE_ID(DATA_PATH_GROUP, RFH_QUEUE_CONFIG_CMD), 0, size, cmd);
}
#endif  // NEEDS_PORTING

static zx_status_t iwl_mvm_send_dqa_cmd(struct iwl_mvm* mvm) {
  struct iwl_dqa_enable_cmd dqa_cmd = {
      .cmd_queue = cpu_to_le32(IWL_MVM_DQA_CMD_QUEUE),
  };
  uint32_t cmd_id = iwl_cmd_id(DQA_ENABLE_CMD, DATA_PATH_GROUP, 0);
  zx_status_t ret;

  ret = iwl_mvm_send_cmd_pdu(mvm, cmd_id, 0, sizeof(dqa_cmd), &dqa_cmd);
  if (ret) {
    IWL_ERR(mvm, "Failed to send DQA enabling command: %d\n", ret);
  } else {
    IWL_DEBUG_FW(mvm, "Working in DQA mode\n");
  }

  return ret;
}

#if 0   // NEEDS_PORTING
void iwl_mvm_mfu_assert_dump_notif(struct iwl_mvm* mvm, struct iwl_rx_cmd_buffer* rxb) {
    struct iwl_rx_packet* pkt = rxb_addr(rxb);
    struct iwl_mfu_assert_dump_notif* mfu_dump_notif = (void*)pkt->data;
    __le32* dump_data = mfu_dump_notif->data;
    int n_words = le32_to_cpu(mfu_dump_notif->data_size) / sizeof(__le32);
    int i;

    if (mfu_dump_notif->index_num == 0) {
        IWL_INFO(mvm, "MFUART assert id 0x%x occurred\n", le32_to_cpu(mfu_dump_notif->assert_id));
    }

    for (i = 0; i < n_words; i++)
        IWL_DEBUG_INFO(mvm, "MFUART assert dump, dword %u: 0x%08x\n",
                       le16_to_cpu(mfu_dump_notif->index_num) * n_words + i,
                       le32_to_cpu(dump_data[i]));
}
#endif  // NEEDS_PORTING

static bool iwl_alive_fn(struct iwl_notif_wait_data* notif_wait, struct iwl_rx_packet* pkt,
                         void* data) {
  struct iwl_mvm* mvm = containerof(notif_wait, struct iwl_mvm, notif_wait);
  struct iwl_mvm_alive_data* alive_data = data;
  struct mvm_alive_resp_v3* palive3;
  struct mvm_alive_resp* palive;
  struct iwl_umac_alive* umac;
  struct iwl_lmac_alive* lmac1;
  struct iwl_lmac_alive* lmac2 = NULL;
  uint16_t status;
  uint32_t umac_error_event_table;

  if (iwl_rx_packet_payload_len(pkt) == sizeof(*palive)) {
    palive = (void*)pkt->data;
    umac = &palive->umac_data;
    lmac1 = &palive->lmac_data[0];
    lmac2 = &palive->lmac_data[1];
    status = le16_to_cpu(palive->status);
  } else {
    palive3 = (void*)pkt->data;
    umac = &palive3->umac_data;
    lmac1 = &palive3->lmac_data;
    status = le16_to_cpu(palive3->status);
  }

  mvm->error_event_table[0] = le32_to_cpu(lmac1->error_event_table_ptr);
  if (lmac2) {
    mvm->error_event_table[1] = le32_to_cpu(lmac2->error_event_table_ptr);
  }
  mvm->log_event_table = le32_to_cpu(lmac1->log_event_table_ptr);

  umac_error_event_table = le32_to_cpu(umac->error_info_addr);

  if (!umac_error_event_table) {
    mvm->support_umac_log = false;
  } else if (umac_error_event_table >= mvm->trans->cfg->min_umac_error_event_table) {
    mvm->support_umac_log = true;
    mvm->umac_error_event_table = umac_error_event_table;
  } else {
    IWL_ERR(mvm, "Not valid error log pointer 0x%08X for %s uCode\n", mvm->umac_error_event_table,
            (mvm->fwrt.cur_fw_img == IWL_UCODE_INIT) ? "Init" : "RT");
    mvm->support_umac_log = false;
  }

  alive_data->scd_base_addr = le32_to_cpu(lmac1->scd_base_ptr);
  alive_data->valid = status == IWL_ALIVE_STATUS_OK;

#ifdef CPTCFG_IWLWIFI_DEVICE_TESTMODE
  iwl_tm_set_fw_ver(mvm->trans, le32_to_cpu(lmac1->ucode_major), le32_to_cpu(lmac1->ucode_minor));
#endif
  IWL_DEBUG_FW(mvm, "Alive ucode status 0x%04x revision 0x%01X 0x%01X\n", status, lmac1->ver_type,
               lmac1->ver_subtype);

  if (lmac2) {
    IWL_DEBUG_FW(mvm, "Alive ucode CDB\n");
  }

  IWL_DEBUG_FW(mvm, "UMAC version: Major - 0x%x, Minor - 0x%x\n", le32_to_cpu(umac->umac_major),
               le32_to_cpu(umac->umac_minor));

  return true;
}

__UNUSED static bool iwl_wait_init_complete(struct iwl_notif_wait_data* notif_wait,
                                            struct iwl_rx_packet* pkt, void* data) {
  WARN_ON(pkt->hdr.cmd != INIT_COMPLETE_NOTIF);

  return true;
}

static bool iwl_wait_phy_db_entry(struct iwl_notif_wait_data* notif_wait, struct iwl_rx_packet* pkt,
                                  void* data) {
  struct iwl_phy_db* phy_db = data;

  if (pkt->hdr.cmd != CALIB_RES_NOTIF_PHY_DB) {
    WARN_ON(pkt->hdr.cmd != INIT_COMPLETE_NOTIF);
    return true;
  }

  WARN_ON(iwl_phy_db_set_section(phy_db, pkt));

  return false;
}

static zx_status_t iwl_mvm_load_ucode_wait_alive(struct iwl_mvm* mvm,
                                                 enum iwl_ucode_type ucode_type) {
  struct iwl_notification_wait alive_wait;
  struct iwl_mvm_alive_data alive_data;
  const struct fw_img* fw;
  zx_status_t ret;
  enum iwl_ucode_type old_type = mvm->fwrt.cur_fw_img;
  static const uint16_t alive_cmd[] = {MVM_ALIVE};

  set_bit(IWL_FWRT_STATUS_WAIT_ALIVE, &mvm->fwrt.status);
  if (ucode_type == IWL_UCODE_REGULAR &&
      iwl_fw_dbg_conf_usniffer(mvm->fw, FW_DBG_START_FROM_ALIVE) &&
      !(fw_has_capa(&mvm->fw->ucode_capa, IWL_UCODE_TLV_CAPA_USNIFFER_UNIFIED))) {
    fw = iwl_get_ucode_image(mvm->fw, IWL_UCODE_REGULAR_USNIFFER);
  } else {
    fw = iwl_get_ucode_image(mvm->fw, ucode_type);
  }
  if (WARN_ON(!fw)) {
    return ZX_ERR_INVALID_ARGS;
  }
  iwl_fw_set_current_image(&mvm->fwrt, ucode_type);
  clear_bit(IWL_MVM_STATUS_FIRMWARE_RUNNING, &mvm->status);

  iwl_init_notification_wait(&mvm->notif_wait, &alive_wait, alive_cmd, ARRAY_SIZE(alive_cmd),
                             iwl_alive_fn, &alive_data);

  ret = iwl_trans_start_fw(mvm->trans, fw, ucode_type == IWL_UCODE_INIT);
  if (ret != ZX_OK) {
    iwl_fw_set_current_image(&mvm->fwrt, old_type);
    iwl_remove_notification(&mvm->notif_wait, &alive_wait);
    return ret;
  }

  /*
   * Some things may run in the background now, but we
   * just wait for the ALIVE notification here.
   */
  ret = iwl_wait_notification(&mvm->notif_wait, &alive_wait, MVM_UCODE_ALIVE_TIMEOUT);
  if (ret != ZX_OK) {
    struct iwl_trans* trans = mvm->trans;

    if (trans->cfg->device_family >= IWL_DEVICE_FAMILY_22000)
      IWL_ERR(mvm, "SecBoot CPU1 Status: 0x%x, CPU2 Status: 0x%x\n",
              iwl_read_prph(trans, UMAG_SB_CPU_1_STATUS),
              iwl_read_prph(trans, UMAG_SB_CPU_2_STATUS));
    else if (trans->cfg->device_family >= IWL_DEVICE_FAMILY_8000)
      IWL_ERR(mvm, "SecBoot CPU1 Status: 0x%x, CPU2 Status: 0x%x\n",
              iwl_read_prph(trans, SB_CPU_1_STATUS), iwl_read_prph(trans, SB_CPU_2_STATUS));
    iwl_fw_set_current_image(&mvm->fwrt, old_type);
    return ret;
  }

  if (mvm->trans->to_load_firmware && !alive_data.valid) {
    IWL_ERR(mvm, "Loaded ucode is not valid!\n");
    iwl_fw_set_current_image(&mvm->fwrt, old_type);
    return ZX_ERR_IO_INVALID;
  }

  iwl_trans_fw_alive(mvm->trans, alive_data.scd_base_addr);

  /*
   * Note: all the queues are enabled as part of the interface
   * initialization, but in firmware restart scenarios they
   * could be stopped, so wake them up. In firmware restart,
   * mac80211 will have the queues stopped as well until the
   * reconfiguration completes. During normal startup, they
   * will be empty.
   */

  memset(&mvm->queue_info, 0, sizeof(mvm->queue_info));
  /*
   * Set a 'fake' TID for the command queue, since we use the
   * hweight() of the tid_bitmap as a refcount now. Not that
   * we ever even consider the command queue as one we might
   * want to reuse, but be safe nevertheless.
   */
  mvm->queue_info[IWL_MVM_DQA_CMD_QUEUE].tid_bitmap = BIT(IWL_MAX_TID_COUNT + 2);

  set_bit(IWL_MVM_STATUS_FIRMWARE_RUNNING, &mvm->status);
#ifdef CPTCFG_IWLWIFI_DEBUGFS
  iwl_fw_set_dbg_rec_on(&mvm->fwrt);
#endif
  clear_bit(IWL_FWRT_STATUS_WAIT_ALIVE, &mvm->fwrt.status);

  return ZX_OK;
}

#if 0   // NEEDS_PORTING
static int iwl_run_unified_mvm_ucode(struct iwl_mvm* mvm, bool read_nvm) {
    struct iwl_notification_wait init_wait;
    struct iwl_nvm_access_complete_cmd nvm_complete = {};
    struct iwl_init_extended_cfg_cmd init_cfg = {
        .init_flags = cpu_to_le32(BIT(IWL_INIT_NVM)),
    };
    static const uint16_t init_complete[] = {
        INIT_COMPLETE_NOTIF,
    };
    int ret;

    lockdep_assert_held(&mvm->mutex);

    iwl_init_notification_wait(&mvm->notif_wait, &init_wait, init_complete,
                               ARRAY_SIZE(init_complete), iwl_wait_init_complete, NULL);

    /* Will also start the device */
    ret = iwl_mvm_load_ucode_wait_alive(mvm, IWL_UCODE_REGULAR);
    if (ret) {
        IWL_ERR(mvm, "Failed to start RT ucode: %d\n", ret);
        iwl_fw_assert_error_dump(&mvm->fwrt);
        goto error;
    }

    /* Send init config command to mark that we are sending NVM access
     * commands
     */
    ret = iwl_mvm_send_cmd_pdu(mvm, WIDE_ID(SYSTEM_GROUP, INIT_EXTENDED_CFG_CMD), 0,
                               sizeof(init_cfg), &init_cfg);
    if (ret) {
        IWL_ERR(mvm, "Failed to run init config command: %d\n", ret);
        goto error;
    }

    /* Load NVM to NIC if needed */
    if (mvm->nvm_file_name) {
        iwl_read_external_nvm(mvm->trans, mvm->nvm_file_name, mvm->nvm_sections);
        iwl_mvm_load_nvm_to_nic(mvm);
    }

    if (IWL_MVM_PARSE_NVM && read_nvm) {
        ret = iwl_nvm_init(mvm);
        if (ret) {
            IWL_ERR(mvm, "Failed to read NVM: %d\n", ret);
            goto error;
        }
    }

    ret = iwl_mvm_send_cmd_pdu(mvm, WIDE_ID(REGULATORY_AND_NVM_GROUP, NVM_ACCESS_COMPLETE), 0,
                               sizeof(nvm_complete), &nvm_complete);
    if (ret) {
        IWL_ERR(mvm, "Failed to run complete NVM access: %d\n", ret);
        goto error;
    }

    /* We wait for the INIT complete notification */
    ret = iwl_wait_notification(&mvm->notif_wait, &init_wait, MVM_UCODE_ALIVE_TIMEOUT);
    if (ret) { return ret; }

    /* Read the NVM only at driver load time, no need to do this twice */
    if (!IWL_MVM_PARSE_NVM && read_nvm) {
        mvm->nvm_data = iwl_get_nvm(mvm->trans, mvm->fw);
        if (IS_ERR(mvm->nvm_data)) {
            ret = PTR_ERR(mvm->nvm_data);
            mvm->nvm_data = NULL;
            IWL_ERR(mvm, "Failed to read NVM: %d\n", ret);
            return ret;
        }
    }

    return 0;

error:
    iwl_remove_notification(&mvm->notif_wait, &init_wait);
    return ret;
}
#endif  // NEEDS_PORTING

static zx_status_t iwl_send_phy_cfg_cmd(struct iwl_mvm* mvm) {
  struct iwl_phy_cfg_cmd phy_cfg_cmd;
  enum iwl_ucode_type ucode_type = mvm->fwrt.cur_fw_img;
#ifdef CPTCFG_IWLWIFI_SUPPORT_DEBUG_OVERRIDES
  uint32_t override_mask, flow_override, flow_src;
  uint32_t event_override, event_src;
  const struct iwl_tlv_calib_ctrl* default_calib = &mvm->fw->default_calib[ucode_type];
#endif

  /* Set parameters */
  phy_cfg_cmd.phy_cfg = cpu_to_le32(iwl_mvm_get_phy_config(mvm));

  /* set flags extra PHY configuration flags from the device's cfg */
  phy_cfg_cmd.phy_cfg |= cpu_to_le32(mvm->cfg->extra_phy_cfg_flags);

  phy_cfg_cmd.calib_control.event_trigger = mvm->fw->default_calib[ucode_type].event_trigger;
  phy_cfg_cmd.calib_control.flow_trigger = mvm->fw->default_calib[ucode_type].flow_trigger;

#ifdef CPTCFG_IWLWIFI_SUPPORT_DEBUG_OVERRIDES
  override_mask = mvm->trans->dbg_cfg.MVM_CALIB_OVERRIDE_CONTROL;
  if (override_mask) {
    IWL_DEBUG_INFO(mvm, "calib settings overriden by user, control=0x%x\n", override_mask);

    switch (ucode_type) {
      case IWL_UCODE_INIT:
        flow_override = mvm->trans->dbg_cfg.MVM_CALIB_INIT_FLOW;
        event_override = mvm->trans->dbg_cfg.MVM_CALIB_INIT_EVENT;
        IWL_DEBUG_CALIB(mvm, "INIT: flow_override %x, event_override %x\n", flow_override,
                        event_override);
        break;
      case IWL_UCODE_REGULAR:
        flow_override = mvm->trans->dbg_cfg.MVM_CALIB_D0_FLOW;
        event_override = mvm->trans->dbg_cfg.MVM_CALIB_D0_EVENT;
        IWL_DEBUG_CALIB(mvm, "REGULAR: flow_override %x, event_override %x\n", flow_override,
                        event_override);
        break;
      case IWL_UCODE_WOWLAN:
        flow_override = mvm->trans->dbg_cfg.MVM_CALIB_D3_FLOW;
        event_override = mvm->trans->dbg_cfg.MVM_CALIB_D3_EVENT;
        IWL_DEBUG_CALIB(mvm, "WOWLAN: flow_override %x, event_override %x\n", flow_override,
                        event_override);
        break;
      default:
        IWL_ERR(mvm, "ERROR: calib case isn't valid\n");
        flow_override = 0;
        event_override = 0;
        break;
    }

    IWL_DEBUG_CALIB(mvm, "override_mask %x\n", override_mask);

    /* find the new calib setting for the flow calibrations */
    flow_src = le32_to_cpu(default_calib->flow_trigger);
    IWL_DEBUG_CALIB(mvm, "flow_src %x\n", flow_src);

    flow_override &= override_mask;
    flow_src &= ~override_mask;
    flow_override |= flow_src;

    phy_cfg_cmd.calib_control.flow_trigger = cpu_to_le32(flow_override);
    IWL_DEBUG_CALIB(mvm, "new flow calib setting = %x\n", flow_override);

    /* find the new calib setting for the event calibrations */
    event_src = le32_to_cpu(default_calib->event_trigger);
    IWL_DEBUG_CALIB(mvm, "event_src %x\n", event_src);

    event_override &= override_mask;
    event_src &= ~override_mask;
    event_override |= event_src;

    phy_cfg_cmd.calib_control.event_trigger = cpu_to_le32(event_override);
    IWL_DEBUG_CALIB(mvm, "new event calib setting = %x\n", event_override);
  }
#endif
  IWL_DEBUG_INFO(mvm, "Sending Phy CFG command: 0x%x\n", phy_cfg_cmd.phy_cfg);

  return iwl_mvm_send_cmd_pdu(mvm, PHY_CONFIGURATION_CMD, 0, sizeof(phy_cfg_cmd), &phy_cfg_cmd);
}

zx_status_t iwl_run_init_mvm_ucode(struct iwl_mvm* mvm, bool read_nvm) {
  struct iwl_notification_wait calib_wait;
  static const uint16_t init_complete[] = {INIT_COMPLETE_NOTIF, CALIB_RES_NOTIF_PHY_DB};
  int ret = ZX_OK;

#if 0   // NEEDS_PORTING
  // The chip we use (7265D) doesn't have unified ucode.
  if (iwl_mvm_has_unified_ucode(mvm)) {
    return iwl_run_unified_mvm_ucode(mvm, true);
  }
#endif  // NEEDS_PORTING

  lockdep_assert_held(&mvm->mutex);

  if (WARN_ON_ONCE(mvm->calibrating)) {
    return 0;
  }

  iwl_init_notification_wait(&mvm->notif_wait, &calib_wait, init_complete,
                             ARRAY_SIZE(init_complete), iwl_wait_phy_db_entry, mvm->phy_db);

  /* Will also start the device */
  ret = iwl_mvm_load_ucode_wait_alive(mvm, IWL_UCODE_INIT);
  if (ret != ZX_OK) {
    IWL_ERR(mvm, "Failed to start INIT ucode: %s\n", zx_status_get_string(ret));
    goto remove_notif;
  }
#ifdef CPTCFG_IWLWIFI_DEVICE_TESTMODE
  iwl_dnt_start(mvm->trans);
#endif

  if (mvm->cfg->device_family < IWL_DEVICE_FAMILY_8000) {
    ret = iwl_mvm_send_bt_init_conf(mvm);
    if (ret != ZX_OK) {
      goto remove_notif;
    }
  }

  /* Read the NVM only at driver load time, no need to do this twice */
  if (read_nvm) {
    ret = iwl_nvm_init(mvm);
    if (ret != ZX_OK) {
      IWL_ERR(mvm, "Failed to read NVM: %s\n", zx_status_get_string(ret));
      goto remove_notif;
    }
  }

#if 0   // NEEDS_PORTING
  /* In case we read the NVM from external file, load it to the NIC */
  if (mvm->nvm_file_name) {
    iwl_mvm_load_nvm_to_nic(mvm);
  }

  WARN_ONCE(mvm->nvm_data->nvm_version < mvm->trans->cfg->nvm_ver,
            "Too old NVM version (0x%0x, required = 0x%0x)", mvm->nvm_data->nvm_version,
            mvm->trans->cfg->nvm_ver);

  /*
   * abort after reading the nvm in case RF Kill is on, we will complete
   * the init seq later when RF kill will switch to off
   */
  if (iwl_mvm_is_radio_hw_killed(mvm)) {
    IWL_DEBUG_RF_KILL(mvm, "jump over all phy activities due to RF kill\n");
    goto remove_notif;
  }

  mvm->calibrating = true;

  /* Send TX valid antennas before triggering calibrations */
  ret = iwl_send_tx_ant_cfg(mvm, iwl_mvm_get_valid_tx_ant(mvm));
  if (ret) {
    goto remove_notif;
  }

  ret = iwl_send_phy_cfg_cmd(mvm);
  if (ret) {
    IWL_ERR(mvm, "Failed to run INIT calibrations: %d\n", ret);
    goto remove_notif;
  }

  /*
   * Some things may run in the background now, but we
   * just wait for the calibration complete notification.
   */
  ret = iwl_wait_notification(&mvm->notif_wait, &calib_wait, MVM_UCODE_CALIB_TIMEOUT);
  if (!ret) {
    goto out;
  }

  if (iwl_mvm_is_radio_hw_killed(mvm)) {
    IWL_DEBUG_RF_KILL(mvm, "RFKILL while calibrating.\n");
    ret = 0;
  } else {
    IWL_ERR(mvm, "Failed to run INIT calibrations: %d\n", ret);
  }

  goto out;
#endif  // NEEDS_PORTING

remove_notif:
  iwl_remove_notification(&mvm->notif_wait, &calib_wait);
#if 0   // NEEDS_PORTING
out:
#endif  // NEEDS_PORTING
  mvm->calibrating = false;

#if 0   // NEEDS_PORTING
  if (iwlmvm_mod_params.init_dbg && !mvm->nvm_data) {
    /* we want to debug INIT and we have no NVM - fake */
    mvm->nvm_data = kzalloc(sizeof(struct iwl_nvm_data) + sizeof(struct ieee80211_channel) +
                                sizeof(struct ieee80211_rate),
                            GFP_KERNEL);
    if (!mvm->nvm_data) {
      return -ENOMEM;
    }
    mvm->nvm_data->bands[0].channels = mvm->nvm_data->channels;
    mvm->nvm_data->bands[0].n_channels = 1;
    mvm->nvm_data->bands[0].n_bitrates = 1;
    mvm->nvm_data->bands[0].bitrates = (void*)mvm->nvm_data->channels + 1;
    mvm->nvm_data->bands[0].bitrates->hw_value = 10;
  }
#endif  // NEEDS_PORTING

  return ret;
}

static zx_status_t iwl_mvm_config_ltr(struct iwl_mvm* mvm) {
  struct iwl_ltr_config_cmd cmd = {
      .flags = cpu_to_le32(LTR_CFG_FLAG_FEATURE_ENABLE),
  };

  if (!mvm->trans->ltr_enabled) {
    return ZX_OK;
  }

  return iwl_mvm_send_cmd_pdu(mvm, LTR_CONFIG, 0, sizeof(cmd), &cmd);
}

#if 0  // NEEDS_PORTING
#ifdef CONFIG_ACPI
static int iwl_mvm_sar_set_profile(struct iwl_mvm* mvm, union acpi_object* table,
                                   struct iwl_mvm_sar_profile* profile, bool enabled) {
    int i;

    profile->enabled = enabled;

    for (i = 0; i < ACPI_SAR_TABLE_SIZE; i++) {
        if ((table[i].type != ACPI_TYPE_INTEGER) || (table[i].integer.value > U8_MAX)) {
            return -EINVAL;
        }

        profile->table[i] = table[i].integer.value;
    }

    return 0;
}

static int iwl_mvm_sar_get_wrds_table(struct iwl_mvm* mvm) {
    union acpi_object *wifi_pkg, *table, *data;
    bool enabled;
    int ret;

    data = iwl_acpi_get_object(mvm->dev, ACPI_WRDS_METHOD);
    if (IS_ERR(data)) { return PTR_ERR(data); }

    wifi_pkg = iwl_acpi_get_wifi_pkg(mvm->dev, data, ACPI_WRDS_WIFI_DATA_SIZE);
    if (IS_ERR(wifi_pkg)) {
        ret = PTR_ERR(wifi_pkg);
        goto out_free;
    }

    if (wifi_pkg->package.elements[1].type != ACPI_TYPE_INTEGER) {
        ret = -EINVAL;
        goto out_free;
    }

    enabled = !!(wifi_pkg->package.elements[1].integer.value);

    /* position of the actual table */
    table = &wifi_pkg->package.elements[2];

    /* The profile from WRDS is officially profile 1, but goes
     * into sar_profiles[0] (because we don't have a profile 0).
     */
    ret = iwl_mvm_sar_set_profile(mvm, table, &mvm->sar_profiles[0], enabled);
out_free:
    kfree(data);
    return ret;
}

static int iwl_mvm_sar_get_ewrd_table(struct iwl_mvm* mvm) {
    union acpi_object *wifi_pkg, *data;
    bool enabled;
    int i, n_profiles, ret;

    data = iwl_acpi_get_object(mvm->dev, ACPI_EWRD_METHOD);
    if (IS_ERR(data)) { return PTR_ERR(data); }

    wifi_pkg = iwl_acpi_get_wifi_pkg(mvm->dev, data, ACPI_EWRD_WIFI_DATA_SIZE);
    if (IS_ERR(wifi_pkg)) {
        ret = PTR_ERR(wifi_pkg);
        goto out_free;
    }

    if ((wifi_pkg->package.elements[1].type != ACPI_TYPE_INTEGER) ||
        (wifi_pkg->package.elements[2].type != ACPI_TYPE_INTEGER)) {
        ret = -EINVAL;
        goto out_free;
    }

    enabled = !!(wifi_pkg->package.elements[1].integer.value);
    n_profiles = wifi_pkg->package.elements[2].integer.value;

    /*
     * Check the validity of n_profiles.  The EWRD profiles start
     * from index 1, so the maximum value allowed here is
     * ACPI_SAR_PROFILES_NUM - 1.
     */
    if (n_profiles <= 0 || n_profiles >= ACPI_SAR_PROFILE_NUM) {
        ret = -EINVAL;
        goto out_free;
    }

    for (i = 0; i < n_profiles; i++) {
        /* the tables start at element 3 */
        static int pos = 3;

        /* The EWRD profiles officially go from 2 to 4, but we
         * save them in sar_profiles[1-3] (because we don't
         * have profile 0).  So in the array we start from 1.
         */
        ret = iwl_mvm_sar_set_profile(mvm, &wifi_pkg->package.elements[pos],
                                      &mvm->sar_profiles[i + 1], enabled);
        if (ret < 0) { break; }

        /* go to the next table */
        pos += ACPI_SAR_TABLE_SIZE;
    }

out_free:
    kfree(data);
    return ret;
}

static int iwl_mvm_sar_get_wgds_table(struct iwl_mvm* mvm) {
    union acpi_object *wifi_pkg, *data;
    int i, j, ret;
    int idx = 1;

    data = iwl_acpi_get_object(mvm->dev, ACPI_WGDS_METHOD);
    if (IS_ERR(data)) { return PTR_ERR(data); }

    wifi_pkg = iwl_acpi_get_wifi_pkg(mvm->dev, data, ACPI_WGDS_WIFI_DATA_SIZE);
    if (IS_ERR(wifi_pkg)) {
        ret = PTR_ERR(wifi_pkg);
        goto out_free;
    }

    for (i = 0; i < ACPI_NUM_GEO_PROFILES; i++) {
        for (j = 0; j < ACPI_GEO_TABLE_SIZE; j++) {
            union acpi_object* entry;

            entry = &wifi_pkg->package.elements[idx++];
            if ((entry->type != ACPI_TYPE_INTEGER) || (entry->integer.value > U8_MAX)) {
                ret = -EINVAL;
                goto out_free;
            }

            mvm->geo_profiles[i].values[j] = entry->integer.value;
        }
    }
    ret = 0;
out_free:
    kfree(data);
    return ret;
}

int iwl_mvm_sar_select_profile(struct iwl_mvm* mvm, int prof_a, int prof_b) {
    union {
        struct iwl_dev_tx_power_cmd v5;
        struct iwl_dev_tx_power_cmd_v4 v4;
    } cmd;
    int i, j, idx;
    int profs[ACPI_SAR_NUM_CHAIN_LIMITS] = {prof_a, prof_b};
    int len;

    BUILD_BUG_ON(ACPI_SAR_NUM_CHAIN_LIMITS < 2);
    BUILD_BUG_ON(ACPI_SAR_NUM_CHAIN_LIMITS * ACPI_SAR_NUM_SUB_BANDS != ACPI_SAR_TABLE_SIZE);

    cmd.v5.v3.set_mode = cpu_to_le32(IWL_TX_POWER_MODE_SET_CHAINS);

    if (fw_has_api(&mvm->fw->ucode_capa, IWL_UCODE_TLV_API_REDUCE_TX_POWER)) {
        len = sizeof(cmd.v5);
    } else if (fw_has_capa(&mvm->fw->ucode_capa, IWL_UCODE_TLV_CAPA_TX_POWER_ACK)) {
        len = sizeof(cmd.v4);
    } else {
        len = sizeof(cmd.v4.v3);
    }

    for (i = 0; i < ACPI_SAR_NUM_CHAIN_LIMITS; i++) {
        struct iwl_mvm_sar_profile* prof;

        /* don't allow SAR to be disabled (profile 0 means disable) */
        if (profs[i] == 0) { return -EPERM; }

        /* we are off by one, so allow up to ACPI_SAR_PROFILE_NUM */
        if (profs[i] > ACPI_SAR_PROFILE_NUM) { return -EINVAL; }

        /* profiles go from 1 to 4, so decrement to access the array */
        prof = &mvm->sar_profiles[profs[i] - 1];

        /* if the profile is disabled, do nothing */
        if (!prof->enabled) {
            IWL_DEBUG_RADIO(mvm, "SAR profile %d is disabled.\n", profs[i]);
            /* if one of the profiles is disabled, we fail all */
            return -ENOENT;
        }

        IWL_DEBUG_RADIO(mvm, "  Chain[%d]:\n", i);
        for (j = 0; j < ACPI_SAR_NUM_SUB_BANDS; j++) {
            idx = (i * ACPI_SAR_NUM_SUB_BANDS) + j;
            cmd.v5.v3.per_chain_restriction[i][j] = cpu_to_le16(prof->table[idx]);
            IWL_DEBUG_RADIO(mvm, "    Band[%d] = %d * .125dBm\n", j, prof->table[idx]);
        }
    }

#ifdef CPTCFG_IWLMVM_VENDOR_CMDS
    mvm->sar_chain_a_profile = prof_a;
    mvm->sar_chain_b_profile = prof_b;
#endif
    IWL_DEBUG_RADIO(mvm, "Sending REDUCE_TX_POWER_CMD per chain\n");

    return iwl_mvm_send_cmd_pdu(mvm, REDUCE_TX_POWER_CMD, 0, len, &cmd);
}

int iwl_mvm_get_sar_geo_profile(struct iwl_mvm* mvm) {
    struct iwl_geo_tx_power_profiles_resp* resp;
    int ret;

    struct iwl_geo_tx_power_profiles_cmd geo_cmd = {
        .ops = cpu_to_le32(IWL_PER_CHAIN_OFFSET_GET_CURRENT_TABLE),
    };
    struct iwl_host_cmd cmd = {
        .id = WIDE_ID(PHY_OPS_GROUP, GEO_TX_POWER_LIMIT),
        .len =
            {
                sizeof(geo_cmd),
            },
        .flags = CMD_WANT_SKB,
        .data = {&geo_cmd},
    };

    ret = iwl_mvm_send_cmd(mvm, &cmd);
    if (ret) {
        IWL_ERR(mvm, "Failed to get geographic profile info %d\n", ret);
        return ret;
    }

    resp = (void*)cmd.resp_pkt->data;
    ret = le32_to_cpu(resp->profile_idx);
    if (WARN_ON(ret > ACPI_NUM_GEO_PROFILES)) {
        ret = -EIO;
        IWL_WARN(mvm, "Invalid geographic profile idx (%d)\n", ret);
    }

    iwl_free_resp(&cmd);
    return ret;
}

static int iwl_mvm_sar_geo_init(struct iwl_mvm* mvm) {
    struct iwl_geo_tx_power_profiles_cmd cmd = {
        .ops = cpu_to_le32(IWL_PER_CHAIN_OFFSET_SET_TABLES),
    };
    int ret, i, j;
    uint16_t cmd_wide_id = WIDE_ID(PHY_OPS_GROUP, GEO_TX_POWER_LIMIT);

    ret = iwl_mvm_sar_get_wgds_table(mvm);
    if (ret < 0) {
        IWL_DEBUG_RADIO(mvm, "Geo SAR BIOS table invalid or unavailable. (%d)\n", ret);
        /* we don't fail if the table is not available */
        return 0;
    }

    IWL_DEBUG_RADIO(mvm, "Sending GEO_TX_POWER_LIMIT\n");

    BUILD_BUG_ON(ACPI_NUM_GEO_PROFILES * ACPI_WGDS_NUM_BANDS * ACPI_WGDS_TABLE_SIZE + 1 !=
                 ACPI_WGDS_WIFI_DATA_SIZE);

    BUILD_BUG_ON(ACPI_NUM_GEO_PROFILES > IWL_NUM_GEO_PROFILES);

    for (i = 0; i < ACPI_NUM_GEO_PROFILES; i++) {
        struct iwl_per_chain_offset* chain = (struct iwl_per_chain_offset*)&cmd.table[i];

        for (j = 0; j < ACPI_WGDS_NUM_BANDS; j++) {
            uint8_t* value;

            value = &mvm->geo_profiles[i].values[j * ACPI_GEO_PER_CHAIN_SIZE];
            chain[j].max_tx_power = cpu_to_le16(value[0]);
            chain[j].chain_a = value[1];
            chain[j].chain_b = value[2];
            IWL_DEBUG_RADIO(mvm,
                            "SAR geographic profile[%d] Band[%d]: chain A = %d chain B = %d "
                            "max_tx_power = %d\n",
                            i, j, value[1], value[2], value[0]);
        }
    }
    return iwl_mvm_send_cmd_pdu(mvm, cmd_wide_id, 0, sizeof(cmd), &cmd);
}

#else  /* CONFIG_ACPI */
static int iwl_mvm_sar_get_wrds_table(struct iwl_mvm* mvm) {
    return -ENOENT;
}

static int iwl_mvm_sar_get_ewrd_table(struct iwl_mvm* mvm) {
    return -ENOENT;
}

static int iwl_mvm_sar_geo_init(struct iwl_mvm* mvm) {
    return 0;
}

int iwl_mvm_sar_select_profile(struct iwl_mvm* mvm, int prof_a, int prof_b) {
    return -ENOENT;
}

int iwl_mvm_get_sar_geo_profile(struct iwl_mvm* mvm) {
    return -ENOENT;
}
#endif /* CONFIG_ACPI */

static int iwl_mvm_sar_init(struct iwl_mvm* mvm) {
    int ret;

    ret = iwl_mvm_sar_get_wrds_table(mvm);
    if (ret < 0) {
        IWL_DEBUG_RADIO(mvm, "WRDS SAR BIOS table invalid or unavailable. (%d)\n", ret);
        /*
         * If not available, don't fail and don't bother with EWRD.
         * Return 1 to tell that we can't use WGDS either.
         */
        return 1;
    }

    ret = iwl_mvm_sar_get_ewrd_table(mvm);
    /* if EWRD is not available, we can still use WRDS, so don't fail */
    if (ret < 0) {
        IWL_DEBUG_RADIO(mvm, "EWRD SAR BIOS table invalid or unavailable. (%d)\n", ret);
    }

#if defined(CPTCFG_IWLMVM_VENDOR_CMDS) && defined(CONFIG_ACPI)
    /*
     * if no profile was chosen by the user yet, choose profile 1 (WRDS) as
     * default for both chains
     */
    if (mvm->sar_chain_a_profile && mvm->sar_chain_b_profile) {
        ret = iwl_mvm_sar_select_profile(mvm, mvm->sar_chain_a_profile, mvm->sar_chain_b_profile);
    } else
#endif
        ret = iwl_mvm_sar_select_profile(mvm, 1, 1);

    /*
     * If we don't have profile 0 from BIOS, just skip it.  This
     * means that SAR Geo will not be enabled either, even if we
     * have other valid profiles.
     */
    if (ret == -ENOENT) { return 1; }

    return ret;
}
#endif  // NEEDS_PORTING

static int iwl_mvm_load_rt_fw(struct iwl_mvm* mvm) {
  int ret;

#if 0   // NEEDS_PORTING
  // The chip we use (7265D) doesn't have unified ucode.
  if (iwl_mvm_has_unified_ucode(mvm)) {
    return iwl_run_unified_mvm_ucode(mvm, false);
  }
#endif  // NEEDS_PORTING

  ret = iwl_run_init_mvm_ucode(mvm, false);
  if (ret != ZX_OK) {
    IWL_ERR(mvm, "Failed to run INIT ucode: %d\n", ret);

    if (iwlmvm_mod_params.init_dbg) {
      return 0;
    }
    return ret;
  }

  /*
   * Stop and start the transport without entering low power
   * mode. This will save the state of other components on the
   * device that are triggered by the INIT firwmare (MFUART).
   */
  _iwl_trans_stop_device(mvm->trans, false);
  ret = _iwl_trans_start_hw(mvm->trans, false);
  if (ret) {
    return ret;
  }

#if 0   // NEEDS_PORTING
  iwl_fw_dbg_apply_point(&mvm->fwrt, IWL_FW_INI_APPLY_EARLY);
#endif  // NEEDS_PORTING

  ret = iwl_mvm_load_ucode_wait_alive(mvm, IWL_UCODE_REGULAR);
  if (ret) {
    return ret;
  }

#if 0   // NEEDS_PORTING
  iwl_fw_dbg_apply_point(&mvm->fwrt, IWL_FW_INI_APPLY_AFTER_ALIVE);
#endif  // NEEDS_PORTING

  return iwl_init_paging(&mvm->fwrt, mvm->fwrt.cur_fw_img);
}

zx_status_t iwl_mvm_up(struct iwl_mvm* mvm) {
  zx_status_t ret;

  lockdep_assert_held(&mvm->mutex);

  ret = iwl_trans_start_hw(mvm->trans);
  if (ret != ZX_OK) {
    return ret;
  }

  ret = iwl_mvm_load_rt_fw(mvm);
  if (ret != ZX_OK) {
    IWL_ERR(mvm, "Failed to start RT ucode: %d\n", ret);
#if 0   // NEEDS_PORTING
    iwl_fw_assert_error_dump(&mvm->fwrt);
#endif  // NEEDS_PORTING
    goto error;
  }

  iwl_get_shared_mem_conf(&mvm->fwrt);

#if 0   // NEEDS_PORTING
    // Smart FIFO is used to aggregate the DMA transactions to optimize power usage.
    ret = iwl_mvm_sf_update(mvm, NULL, false);
    if (ret != ZX_OK) { IWL_ERR(mvm, "Failed to initialize Smart Fifo\n"); }
#endif  // NEEDS_PORTING

#ifdef CPTCFG_IWLWIFI_DEVICE_TESTMODE
  iwl_dnt_start(mvm->trans);
#endif

#if 0   // NEEDS_PORTING
    if (!mvm->trans->ini_valid) {
        mvm->fwrt.dump.conf = FW_DBG_INVALID;
        /* if we have a destination, assume EARLY START */
        if (mvm->fw->dbg.dest_tlv) { mvm->fwrt.dump.conf = FW_DBG_START_FROM_ALIVE; }
        iwl_fw_start_dbg_conf(&mvm->fwrt, FW_DBG_START_FROM_ALIVE);
    }
#endif  // NEEDS_PORTING

#ifdef CPTCFG_MAC80211_LATENCY_MEASUREMENTS
  if (iwl_fw_dbg_trigger_enabled(mvm->fw, FW_DBG_TRIGGER_TX_LATENCY)) {
    struct iwl_fw_dbg_trigger_tlv* trig;
    struct iwl_fw_dbg_trigger_tx_latency* thrshold_trig;
    uint32_t thrshld;
    uint32_t vif;
    uint32_t iface = 0;
    uint16_t tid;
    uint16_t mode;
    uint32_t window;

    trig = iwl_fw_dbg_get_trigger(mvm->fw, FW_DBG_TRIGGER_TX_LATENCY);
    vif = le32_to_cpu(trig->vif_type);
    if (vif == IWL_FW_DBG_CONF_VIF_ANY) {
      iface = BIT(IEEE80211_TX_LATENCY_BSS);
      iface |= BIT(IEEE80211_TX_LATENCY_P2P);
    } else if (vif <= IWL_FW_DBG_CONF_VIF_AP) {
      iface = BIT(IEEE80211_TX_LATENCY_BSS);
    } else {
      iface = BIT(IEEE80211_TX_LATENCY_P2P);
    }
    thrshold_trig = (void*)trig->data;
    thrshld = le32_to_cpu(thrshold_trig->thrshold);
    tid = le16_to_cpu(thrshold_trig->tid_bitmap);
    mode = le16_to_cpu(thrshold_trig->mode);
    window = le32_to_cpu(thrshold_trig->window);
    IWL_DEBUG_INFO(mvm,
                   "Tx latency trigger cfg: threshold = %u, tid = 0x%x, mode = 0x%x, window = "
                   "%u vif = 0x%x\n",
                   thrshld, tid, mode, window, iface);
    ieee80211_tx_lat_thrshld_cfg(mvm->hw, thrshld, tid, window, mode, iface);
  }
#endif

  ret = iwl_send_tx_ant_cfg(mvm, iwl_mvm_get_valid_tx_ant(mvm));
  if (ret != ZX_OK) {
    goto error;
  }

  if (!iwl_mvm_has_unified_ucode(mvm)) {
    /* Send phy db control command and then phy db calibration */
    ret = iwl_send_phy_db_data(mvm->phy_db);
    if (ret != ZX_OK) {
      goto error;
    }

    ret = iwl_send_phy_cfg_cmd(mvm);
    if (ret != ZX_OK) {
      goto error;
    }
  }

  ret = iwl_mvm_send_bt_init_conf(mvm);
  if (ret != ZX_OK) {
    goto error;
  }

  if (fw_has_capa(&mvm->fw->ucode_capa, IWL_UCODE_TLV_CAPA_SOC_LATENCY_SUPPORT)) {
    ret = iwl_set_soc_latency(mvm);
    if (ret != ZX_OK) {
      goto error;
    }
  }

#if 0   // NEEDS_PORTING
    /* Init RSS configuration */
    if (mvm->trans->cfg->device_family >= IWL_DEVICE_FAMILY_22000) {
        ret = iwl_configure_rxq(mvm);
        if (ret) {
            IWL_ERR(mvm, "Failed to configure RX queues: %d\n", ret);
            goto error;
        }
    }

    // 7265D firmware doesn't have new RX API.
    if (iwl_mvm_has_new_rx_api(mvm)) {
        ret = iwl_send_rss_cfg_cmd(mvm);
        if (ret) {
            IWL_ERR(mvm, "Failed to configure RSS queues: %d\n", ret);
            goto error;
        }
    }

    /* init the fw <-> mac80211 STA mapping */
    for (i = 0; i < ARRAY_SIZE(mvm->fw_id_to_mac_id); i++) {
        RCU_INIT_POINTER(mvm->fw_id_to_mac_id[i], NULL);
    }
#endif  // NEEDS_PORTING

  mvm->tdls_cs.peer.sta_id = IWL_MVM_INVALID_STA;

  /* reset quota debouncing buffer - 0xff will yield invalid data */
  memset(&mvm->last_quota_cmd, 0xff, sizeof(mvm->last_quota_cmd));

  ret = iwl_mvm_send_dqa_cmd(mvm);
  if (ret != ZX_OK) {
    goto error;
  }

#if 0   // NEEDS_PORTING
    // TODO(WLAN-1204): port iwl_mvm_add_aux_sta later.
    /* Add auxiliary station for scanning */
    ret = iwl_mvm_add_aux_sta(mvm);
    if (ret) { goto error; }
#endif  // NEEDS_PORTING

  /* Add all the PHY contexts with a default value */
  wlan_channel_t chandef = {
      .primary = 1,
      .cbw = WLAN_CHANNEL_BANDWIDTH__20,
  };
  for (size_t i = 0; i < NUM_PHY_CTX; i++) {
    /*
     * The channel used here isn't relevant as it's
     * going to be overwritten in the other flows.
     * For now use the first channel we have.
     */
    ret = iwl_mvm_phy_ctxt_add(mvm, &mvm->phy_ctxts[i], &chandef, 1, 1);
    if (ret != ZX_OK) {
      goto error;
    }
  }

#ifdef CONFIG_THERMAL
  if (iwl_mvm_is_tt_in_fw(mvm)) {
    /* in order to give the responsibility of ct-kill and
     * TX backoff to FW we need to send empty temperature reporting
     * cmd during init time
     */
    iwl_mvm_send_temp_report_ths_cmd(mvm);
  } else {
    /* Initialize tx backoffs to the minimal possible */
    iwl_mvm_tt_tx_backoff(mvm, 0);
  }

  /* TODO: read the budget from BIOS / Platform NVM */

  /*
   * In case there is no budget from BIOS / Platform NVM the default
   * budget should be 2000mW (cooling state 0).
   */
  if (iwl_mvm_is_ctdp_supported(mvm)) {
    ret = iwl_mvm_ctdp_command(mvm, CTDP_CMD_OPERATION_START, mvm->cooling_dev.cur_state);
    if (ret) {
      goto error;
    }
  }
#else
  /* Initialize tx backoffs to the minimal possible */
  iwl_mvm_tt_tx_backoff(mvm, 0);
#endif

  WARN_ON(iwl_mvm_config_ltr(mvm) != ZX_OK);

  ret = iwl_mvm_power_update_device(mvm);
  if (ret != ZX_OK) {
    goto error;
  }

#if 0   // NEEDS_PORTING
    /*
    * RTNL is not taken during Ct-kill, but we don't need to scan/Tx
    * anyway, so don't init MCC.
    */
    if (!test_bit(IWL_MVM_STATUS_HW_CTKILL, &mvm->status)) {
        ret = iwl_mvm_init_mcc(mvm);
        if (ret) { goto error; }
    }

    if (fw_has_capa(&mvm->fw->ucode_capa, IWL_UCODE_TLV_CAPA_UMAC_SCAN)) {
        mvm->scan_type = IWL_SCAN_TYPE_NOT_SET;
        mvm->hb_scan_type = IWL_SCAN_TYPE_NOT_SET;
        ret = iwl_mvm_config_scan(mvm);
        if (ret != ZX_OK) {
            goto error;
        }
    }
#endif  // NEEDS_PORTING

  /* allow FW/transport low power modes if not during restart */
  if (!test_bit(IWL_MVM_STATUS_IN_HW_RESTART, &mvm->status)) {
    iwl_mvm_unref(mvm, IWL_MVM_REF_UCODE_DOWN);
  }

#ifdef CPTCFG_IWLWIFI_LTE_COEX
  iwl_mvm_send_lte_commands(mvm);
#endif

#ifdef CPTCFG_IWLMVM_VENDOR_CMDS
  /* set_mode must be IWL_TX_POWER_MODE_SET_DEVICE if this was
   * ever initialized.
   */
  if (le32_to_cpu(mvm->txp_cmd.v5.v3.set_mode) == IWL_TX_POWER_MODE_SET_DEVICE) {
    int len;

    if (fw_has_api(&mvm->fw->ucode_capa, IWL_UCODE_TLV_API_REDUCE_TX_POWER)) {
      len = sizeof(mvm->txp_cmd.v5);
    } else if (fw_has_capa(&mvm->fw->ucode_capa, IWL_UCODE_TLV_CAPA_TX_POWER_ACK)) {
      len = sizeof(mvm->txp_cmd.v4);
    } else {
      len = sizeof(mvm->txp_cmd.v4.v3);
    }

    if (iwl_mvm_send_cmd_pdu(mvm, REDUCE_TX_POWER_CMD, 0, len, &mvm->txp_cmd)) {
      IWL_ERR(mvm, "failed to update TX power\n");
    }
  }
#endif

#ifdef CPTCFG_IWLWIFI_FRQ_MGR
  iwl_mvm_fm_notify_current_dcdc();
#endif

#if 0   // NEEDS_PORTING
    ret = iwl_mvm_sar_init(mvm);
    if (ret == 0) {
        ret = iwl_mvm_sar_geo_init(mvm);
        if (ret) { goto error; }
    } else if (ret > 0) {
        /* we can't use SAR Geo if basic SAR is not available */
        IWL_ERR(mvm, "BIOS contains WGDS but no WRDS\n");
    } else {
        goto error;
    }
#endif  // NEEDS_PORTING

#if 0   // NEEDS_PORTING
    iwl_mvm_leds_sync(mvm);
#endif  // NEEDS_PORTING

  IWL_DEBUG_INFO(mvm, "RT uCode started.\n");
  return ZX_OK;

error:
  if (!iwlmvm_mod_params.init_dbg || ret == ZX_OK) {
    iwl_mvm_stop_device(mvm);
  }

  return ret;
}

#if 0  // NEEDS_PORTING
int iwl_mvm_load_d3_fw(struct iwl_mvm* mvm) {
    int ret, i;

    lockdep_assert_held(&mvm->mutex);

    ret = iwl_trans_start_hw(mvm->trans);
    if (ret) { return ret; }

    ret = iwl_mvm_load_ucode_wait_alive(mvm, IWL_UCODE_WOWLAN);
    if (ret) {
        IWL_ERR(mvm, "Failed to start WoWLAN firmware: %d\n", ret);
        goto error;
    }

#ifdef CPTCFG_IWLWIFI_DEVICE_TESTMODE
    iwl_dnt_start(mvm->trans);
#endif
    ret = iwl_send_tx_ant_cfg(mvm, iwl_mvm_get_valid_tx_ant(mvm));
    if (ret) { goto error; }

    /* Send phy db control command and then phy db calibration*/
    ret = iwl_send_phy_db_data(mvm->phy_db);
    if (ret) { goto error; }

    ret = iwl_send_phy_cfg_cmd(mvm);
    if (ret) { goto error; }

    /* init the fw <-> mac80211 STA mapping */
    for (i = 0; i < ARRAY_SIZE(mvm->fw_id_to_mac_id); i++) {
        RCU_INIT_POINTER(mvm->fw_id_to_mac_id[i], NULL);
    }

    /* Add auxiliary station for scanning */
    ret = iwl_mvm_add_aux_sta(mvm);
    if (ret) { goto error; }

    return 0;
error:
    iwl_mvm_stop_device(mvm);
    return ret;
}

void iwl_mvm_rx_card_state_notif(struct iwl_mvm* mvm, struct iwl_rx_cmd_buffer* rxb) {
    struct iwl_rx_packet* pkt = rxb_addr(rxb);
    struct iwl_card_state_notif* card_state_notif = (void*)pkt->data;
    uint32_t flags = le32_to_cpu(card_state_notif->flags);

    IWL_DEBUG_RF_KILL(mvm, "Card state received: HW:%s SW:%s CT:%s\n",
                      (flags & HW_CARD_DISABLED) ? "Kill" : "On",
                      (flags & SW_CARD_DISABLED) ? "Kill" : "On",
                      (flags & CT_KILL_CARD_DISABLED) ? "Reached" : "Not reached");
}

void iwl_mvm_rx_mfuart_notif(struct iwl_mvm* mvm, struct iwl_rx_cmd_buffer* rxb) {
    struct iwl_rx_packet* pkt = rxb_addr(rxb);
    struct iwl_mfuart_load_notif* mfuart_notif = (void*)pkt->data;

    IWL_DEBUG_INFO(
        mvm,
        "MFUART: installed ver: 0x%08x, external ver: 0x%08x, status: 0x%08x, duration: 0x%08x\n",
        le32_to_cpu(mfuart_notif->installed_ver), le32_to_cpu(mfuart_notif->external_ver),
        le32_to_cpu(mfuart_notif->status), le32_to_cpu(mfuart_notif->duration));

    if (iwl_rx_packet_payload_len(pkt) == sizeof(*mfuart_notif)) {
        IWL_DEBUG_INFO(mvm, "MFUART: image size: 0x%08x\n", le32_to_cpu(mfuart_notif->image_size));
    }
}
#endif  // NEEDS_PORTING
