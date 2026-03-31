#include "config.h"
#ifdef USE_DATASECURE

#include "address_table_object.h"
#include "apdu.h"
#include "association_table_object.h"
#include "bau.h"
#include "bau_systemB.h"
#include "bits.h"
#include "cemi_frame.h"
#include "device_object.h"
#include "platform.h"
#include "secure_application_layer.h"
#include "security_interface_object.h"
#include "string.h"
#include "transport_layer.h"

// Zephyr logging for debugging
#include <stdio.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>
#ifdef KNX_SNIFFER_MODE
LOG_MODULE_REGISTER(sec_app_layer, LOG_LEVEL_INF);
#else
LOG_MODULE_REGISTER(sec_app_layer, LOG_LEVEL_INF);
#endif

static const char* apciName(uint16_t apci) {
  switch (apci) {
    case 0x0000:
      return "GroupValueRead";
    case 0x0040:
      return "GroupValueResp";
    case 0x0080:
      return "GroupValueWrite";
    case 0x00C0:
      return "IndividualAddrWrite";
    case 0x0100:
      return "IndividualAddrRead";
    case 0x0140:
      return "IndividualAddrResp";
    case 0x0200:
      return "MemoryRead";
    case 0x0240:
      return "MemoryResp";
    case 0x0280:
      return "MemoryWrite";
    case 0x01CC:
      return "PropExtValueRead";
    case 0x01CD:
      return "PropExtValueResp";
    case 0x01CE:
      return "PropExtValueWriteCon";
    case 0x01CF:
      return "PropExtValueWriteConResp";
    case 0x01D0:
      return "PropExtValueWriteUnCon";
    case 0x01D2:
      return "PropExtDescRead";
    case 0x01D3:
      return "PropExtDescResp";
    case 0x01D4:
      return "FuncPropExtCmd";
    case 0x01D5:
      return "FuncPropExtStateRead";
    case 0x01D6:
      return "FuncPropExtStateResp";
    case 0x01FB:
      return "MemExtWrite";
    case 0x01FC:
      return "MemExtWriteResp";
    case 0x01FD:
      return "MemExtRead";
    case 0x01FE:
      return "MemExtReadResp";
    case 0x0300:
      return "DevDescRead";
    case 0x0340:
      return "DevDescResp";
    case 0x0380:
      return "Restart";
    case 0x0381:
      return "RestartMasterReset";
    case 0x2C7:
      return "FuncPropCmd";
    case 0x2C8:
      return "FuncPropStateRead";
    case 0x2C9:
      return "FuncPropStateResp";
    case 0x03D1:
      return "AuthorizeReq";
    case 0x03D2:
      return "AuthorizeResp";
    case 0x03D3:
      return "KeyWrite";
    case 0x03D4:
      return "KeyResp";
    case 0x03D5:
      return "PropValueRead";
    case 0x03D6:
      return "PropValueResp";
    case 0x03D7:
      return "PropValueWrite";
    case 0x03D8:
      return "PropDescRead";
    case 0x03D9:
      return "PropDescResp";
    case 0x03DC:
      return "IndAddrSerialRead";
    case 0x03DD:
      return "IndAddrSerialResp";
    case 0x03DE:
      return "IndAddrSerialWrite";
    case 0x03F1:
      return "SecureService";
    default:
      return "Unknown";
  }
}

// Select what cipher modes to include. We need AES128-CBC and AES128-CTR modes.
#define CBC 1
#define CTR 1
#define ECB 0
#include "aes.hpp"

static constexpr uint8_t kSecureDataPdu = 0;
static constexpr uint8_t kSecureSyncRequest = 2;
static constexpr uint8_t kSecureSyncResponse = 3;

SecureApplicationLayer::SecureApplicationLayer(
    DeviceObject& deviceObj, SecurityInterfaceObject& secIfObj, BauSystemB& bau)
    : ApplicationLayer(bau),
      _secIfObj(secIfObj),
      _deviceObj(deviceObj),
      _bauSystemB(bau),
      _lastSavedSequenceNumberStandard(_sequenceNumber) {}

void SecureApplicationLayer::groupAddressTable(AddressTableObject& addrTable) {
  _addrTab = &addrTable;
}

/* from transport layer */

void SecureApplicationLayer::dataGroupIndication(HopCountType hopType,
                                                 Priority priority,
                                                 uint16_t tsap, APDU& apdu) {
  if (_addrTab == nullptr) return;

  println("dataGroupIndication");

  if (apdu.type() == SecureService) {
    // Will be filled in by decodeSecureApdu()
    SecurityControl secCtrl;

    // Decrypt secure APDU
    // Somehow ugly that we need to know the size in advance here at this point
    uint16_t plainApduLength =
        apdu.length() - 3 - 6 - 4;  // secureAdsuLength - sizeof(tpci,apci,scf)
                                    // - sizeof(seqNum) - sizeof(mac)
    CemiFrame plainFrame(plainApduLength);

    // Decrypt secure APDU
    if (decodeSecureApdu(apdu, plainFrame.apdu(), secCtrl)) {
      // Process decrypted inner APDU
      ApplicationLayer::dataGroupIndication(hopType, priority, tsap,
                                            plainFrame.apdu(), secCtrl);
    }

    return;
  }

  ApplicationLayer::dataGroupIndication(hopType, priority, tsap, apdu);
}

void SecureApplicationLayer::dataGroupConfirm(AckType ack, HopCountType hopType,
                                              Priority priority, uint16_t tsap,
                                              APDU& apdu, bool status) {
  println("dataGroupConfirm");

  if (apdu.type() == SecureService) {
    // We do not care about confirmations of our sync communication
    if (isSyncService(apdu)) {
      return;
    }

    // Will be filled in by decodeSecureApdu()
    SecurityControl secCtrl;

    // Decrypt secure APDU
    // Somehow ugly that we need to know the size in advance here at this point
    uint16_t plainApduLength =
        apdu.length() - 3 - 6 - 4;  // secureAdsuLength - sizeof(tpci,apci,scf)
                                    // - sizeof(seqNum) - sizeof(mac)
    CemiFrame plainFrame(plainApduLength);

    // Decrypt secure APDU
    if (decodeSecureApdu(apdu, plainFrame.apdu(), secCtrl)) {
      // Process decrypted inner APDU
      ApplicationLayer::dataGroupConfirm(ack, hopType, priority, tsap,
                                         plainFrame.apdu(), secCtrl, status);
    }

    return;
  }

  ApplicationLayer::dataGroupConfirm(ack, hopType, priority, tsap, apdu,
                                     status);
}

void SecureApplicationLayer::dataBroadcastIndication(HopCountType hopType,
                                                     Priority priority,
                                                     uint16_t source,
                                                     APDU& apdu) {
  println("dataBroadcastIndication");

  if (apdu.type() == SecureService) {
    // Will be filled in by decodeSecureApdu()
    SecurityControl secCtrl;

    // Decrypt secure APDU
    // Somehow ugly that we need to know the size in advance here at this point
    uint16_t plainApduLength =
        apdu.length() - 3 - 6 - 4;  // secureAdsuLength - sizeof(tpci,apci,scf)
                                    // - sizeof(seqNum) - sizeof(mac)
    CemiFrame plainFrame(plainApduLength);

    // Decrypt secure APDU
    if (decodeSecureApdu(apdu, plainFrame.apdu(), secCtrl)) {
      // Process decrypted inner APDU
      ApplicationLayer::dataBroadcastIndication(hopType, priority, source,
                                                plainFrame.apdu(), secCtrl);
    }

    return;
  }

  ApplicationLayer::dataBroadcastIndication(hopType, priority, source, apdu);
}

void SecureApplicationLayer::dataBroadcastConfirm(AckType ack,
                                                  HopCountType hopType,
                                                  Priority priority, APDU& apdu,
                                                  bool status) {
  println("dataBroadcastConfirm");

  if (apdu.type() == SecureService) {
    // We do not care about confirmations of our sync communication
    if (isSyncService(apdu)) {
      return;
    }

    // Will be filled in by decodeSecureApdu()
    SecurityControl secCtrl;

    // Decrypt secure APDU
    // Somehow ugly that we need to know the size in advance here at this point
    uint16_t plainApduLength =
        apdu.length() - 3 - 6 - 4;  // secureAdsuLength - sizeof(tpci,apci,scf)
                                    // - sizeof(seqNum) - sizeof(mac)
    CemiFrame plainFrame(plainApduLength);

    // Decrypt secure APDU
    if (decodeSecureApdu(apdu, plainFrame.apdu(), secCtrl)) {
      // Process decrypted inner APDU
      ApplicationLayer::dataBroadcastConfirm(
          ack, hopType, priority, plainFrame.apdu(), secCtrl, status);
    }

    return;
  }

  ApplicationLayer::dataBroadcastConfirm(ack, hopType, priority, apdu, status);
}

void SecureApplicationLayer::dataSystemBroadcastIndication(HopCountType hopType,
                                                           Priority priority,
                                                           uint16_t source,
                                                           APDU& apdu) {
  println("dataSystemBroadcastIndication");

  if (apdu.type() == SecureService) {
    // Will be filled in by decodeSecureApdu()
    SecurityControl secCtrl;

    // Decrypt secure APDU
    // Somehow ugly that we need to know the size in advance here at this point
    uint16_t plainApduLength =
        apdu.length() - 3 - 6 - 4;  // secureAdsuLength - sizeof(tpci,apci,scf)
                                    // - sizeof(seqNum) - sizeof(mac)
    CemiFrame plainFrame(plainApduLength);

    // Decrypt secure APDU
    if (decodeSecureApdu(apdu, plainFrame.apdu(), secCtrl)) {
      // Process decrypted inner APDU
      ApplicationLayer::dataSystemBroadcastIndication(
          hopType, priority, source, plainFrame.apdu(), secCtrl);
    }

    return;
  }

  ApplicationLayer::dataSystemBroadcastIndication(hopType, priority, source,
                                                  apdu);
}

void SecureApplicationLayer::dataSystemBroadcastConfirm(HopCountType hopType,
                                                        Priority priority,
                                                        APDU& apdu,
                                                        bool status) {
  println("dataSystemBroadcastConfirm");

  if (apdu.type() == SecureService) {
    // We do not care about confirmations of our sync communication
    if (isSyncService(apdu)) {
      return;
    }

    // Will be filled in by decodeSecureApdu()
    SecurityControl secCtrl;

    // Decrypt secure APDU
    // Somehow ugly that we need to know the size in advance here at this point
    uint16_t plainApduLength =
        apdu.length() - 3 - 6 - 4;  // secureAdsuLength - sizeof(tpci,apci,scf)
                                    // - sizeof(seqNum) - sizeof(mac)
    CemiFrame plainFrame(plainApduLength);

    // Decrypt secure APDU
    if (decodeSecureApdu(apdu, plainFrame.apdu(), secCtrl)) {
      // Process decrypted inner APDU
      ApplicationLayer::dataSystemBroadcastConfirm(
          hopType, priority, plainFrame.apdu(), secCtrl, status);
    }

    return;
  }

  ApplicationLayer::dataSystemBroadcastConfirm(hopType, priority, apdu, status);
}

void SecureApplicationLayer::dataIndividualIndication(HopCountType hopType,
                                                      Priority priority,
                                                      uint16_t source,
                                                      APDU& apdu) {
  // Only log SecureService APDUs to reduce UART buffer pressure
  if (apdu.type() == SecureService) {
#ifdef KNX_SNIFFER_MODE
    printk("\n\n");
#else
    printk("\n\n");
    LOG_WRN("[0x%04X->0x%04X] len=%d", source,
            apdu.frame().destinationAddress(), apdu.length());
#endif

    // Will be filled in by decodeSecureApdu()
    SecurityControl secCtrl;

    // Decrypt secure APDU
    // Somehow ugly that we need to know the size in advance here at this point
    uint16_t plainApduLength =
        apdu.length() - 3 - 6 - 4;  // secureAdsuLength - sizeof(tpci,apci,scf)
                                    // - sizeof(seqNum) - sizeof(mac)
    CemiFrame plainFrame(plainApduLength);

    // Decrypt secure APDU
    if (decodeSecureApdu(apdu, plainFrame.apdu(), secCtrl)) {
#ifndef KNX_SNIFFER_MODE
      LOG_WRN(">>> Decoded OK! Inner APDU type=0x%04X len=%d",
              (uint16_t)plainFrame.apdu().type(), plainFrame.apdu().length());
#endif
#ifdef KNX_SNIFFER_MODE
      // SNIFFER: Log decoded APDU content
      LOG_WRN("[0x%04X->0x%04X] APCI=0x%04X (%s) len=%d", source,
              apdu.frame().destinationAddress(),
              (uint16_t)plainFrame.apdu().type(),
              apciName((uint16_t)plainFrame.apdu().type()),
              plainFrame.apdu().length());
      if (plainFrame.apdu().length() > 0) {
        LOG_HEXDUMP_WRN(plainFrame.apdu().data(),
                        plainFrame.apdu().length() + 1, "DECODED PAYLOAD:");
      }
#endif
      // Process decrypted inner APDU
      ApplicationLayer::dataIndividualIndication(hopType, priority, source,
                                                 plainFrame.apdu(), secCtrl);
    } else if (isSyncService(apdu)) {
      LOG_WRN(
          ">>> SyncService from 0x%04X processed internally (not a failure)",
          source);
    } else {
      LOG_ERR(
          "!!! decodeSecureApdu FAILED for individual from 0x%04X! DROPPED!",
          source);
    }

    return;
  }

#ifdef KNX_SNIFFER_MODE
  printk("\n\n");
#endif
  ApplicationLayer::dataIndividualIndication(hopType, priority, source, apdu);
}

void SecureApplicationLayer::dataIndividualConfirm(AckType ack,
                                                   HopCountType hopType,
                                                   Priority priority,
                                                   uint16_t source, APDU& apdu,
                                                   bool status) {
  println("dataIndividualConfirm");

  if (apdu.type() == SecureService) {
    // We do not care about confirmations of our sync communication
    if (isSyncService(apdu)) {
      return;
    }

    // Will be filled in by decodeSecureApdu()
    SecurityControl secCtrl;

    // Decrypt secure APDU
    // Somehow ugly that we need to know the size in advance here at this point
    uint16_t plainApduLength =
        apdu.length() - 3 - 6 - 4;  // secureAdsuLength - sizeof(tpci,apci,scf)
                                    // - sizeof(seqNum) - sizeof(mac)
    CemiFrame plainFrame(plainApduLength);

    // Decrypt secure APDU
    if (decodeSecureApdu(apdu, plainFrame.apdu(), secCtrl)) {
      // Process decrypted inner APDU
      ApplicationLayer::dataIndividualConfirm(ack, hopType, priority, source,
                                              apdu, secCtrl, status);
    }

    return;
  } else {
    ApplicationLayer::dataIndividualConfirm(ack, hopType, priority, source,
                                            apdu, status);
  }
}

void SecureApplicationLayer::dataConnectedIndication(Priority priority,
                                                     uint16_t tsap,
                                                     APDU& apdu) {
  if (apdu.type() == SecureService) {
#ifdef KNX_SNIFFER_MODE
    printk("\n\n");
#else
    printk("\n\n");
    LOG_WRN("[0x%04X->0x%04X] len=%d", apdu.frame().sourceAddress(),
            apdu.frame().destinationAddress(), apdu.length());
#endif

    // Will be filled in by decodeSecureApdu()
    SecurityControl secCtrl;

    // Decrypt secure APDU
    // Somehow ugly that we need to know the size in advance here at this point
    uint16_t plainApduLength =
        apdu.length() - 3 - 6 - 4;  // secureAdsuLength - sizeof(tpci,apci,scf)
                                    // - sizeof(seqNum) - sizeof(mac)
    CemiFrame plainFrame(plainApduLength);

    // Decrypt secure APDU
    if (decodeSecureApdu(apdu, plainFrame.apdu(), secCtrl)) {
#ifndef KNX_SNIFFER_MODE
      LOG_WRN(">>> Decoded OK! Inner APDU type=0x%04X len=%d",
              (uint16_t)plainFrame.apdu().type(), plainFrame.apdu().length());
#endif
#ifdef KNX_SNIFFER_MODE
      // SNIFFER: Log decoded APDU content
      LOG_WRN("[0x%04X->0x%04X] APCI=0x%04X (%s) len=%d",
              apdu.frame().sourceAddress(), apdu.frame().destinationAddress(),
              (uint16_t)plainFrame.apdu().type(),
              apciName((uint16_t)plainFrame.apdu().type()),
              plainFrame.apdu().length());
      if (plainFrame.apdu().length() > 0) {
        LOG_HEXDUMP_WRN(plainFrame.apdu().data(),
                        plainFrame.apdu().length() + 1, "DECODED PAYLOAD:");
      }
#endif
      // Process decrypted inner APDU
      ApplicationLayer::dataConnectedIndication(priority, tsap,
                                                plainFrame.apdu(), secCtrl);
    } else if (isSyncService(apdu)) {
      LOG_WRN(">>> SyncService (connected) tsap=0x%04X processed internally",
              tsap);
    } else {
      LOG_ERR("!!! decodeSecureApdu FAILED for connected tsap=0x%04X! DROPPED!",
              tsap);
    }

    return;
  }

#ifdef KNX_SNIFFER_MODE
  printk("\n\n");
#endif
  ApplicationLayer::dataConnectedIndication(priority, tsap, apdu);
}

void SecureApplicationLayer::dataConnectedConfirm(uint16_t tsap) {
  // Just the confirmation issued by the transport layer in case of
  // T_DATA_CONNECTED
  ApplicationLayer::dataConnectedConfirm(tsap);
}

/* to transport layer */

void SecureApplicationLayer::dataGroupRequest(AckType ack, HopCountType hopType,
                                              Priority priority, uint16_t tsap,
                                              APDU& apdu,
                                              const SecurityControl& secCtrl) {
  if (_addrTab == nullptr) return;

  println("dataGroupRequest");

  if (secCtrl.dataSecurity != DataSecurity::None) {
    apdu.frame().sourceAddress(_deviceObj.individualAddress());
    apdu.frame().destinationAddress(_addrTab->getGroupAddress(tsap));
    apdu.frame().addressType(GroupAddress);

    uint16_t secureApduLength =
        apdu.length() + 3 + 6 +
        4;  // 3(TPCI,APCI,SCF) + sizeof(seqNum) + apdu.length() + 4
    CemiFrame secureFrame(secureApduLength);

    // create secure APDU
    if (createSecureApdu(apdu, secureFrame.apdu(), secCtrl)) {
      ApplicationLayer::dataGroupRequest(ack, hopType, priority, tsap,
                                         secureFrame.apdu(), secCtrl);
    }

    return;
  }

  ApplicationLayer::dataGroupRequest(ack, hopType, priority, tsap, apdu,
                                     secCtrl);
}

void SecureApplicationLayer::dataBroadcastRequest(
    AckType ack, HopCountType hopType, Priority priority, APDU& apdu,
    const SecurityControl& secCtrl) {
  println("dataBroadcastRequest");

  if (secCtrl.dataSecurity != DataSecurity::None) {
    apdu.frame().sourceAddress(_deviceObj.individualAddress());
    apdu.frame().destinationAddress(0x0000);
    apdu.frame().addressType(GroupAddress);
    apdu.frame().systemBroadcast(Broadcast);

    uint16_t secureApduLength =
        apdu.length() + 3 + 6 +
        4;  // 3(TPCI,APCI,SCF) + sizeof(seqNum) + apdu.length() + 4
    CemiFrame secureFrame(secureApduLength);

    // create secure APDU
    if (createSecureApdu(apdu, secureFrame.apdu(), secCtrl)) {
      ApplicationLayer::dataBroadcastRequest(ack, hopType, SystemPriority,
                                             secureFrame.apdu(), secCtrl);
    }

    return;
  }

  ApplicationLayer::dataBroadcastRequest(ack, hopType, SystemPriority, apdu,
                                         secCtrl);
}

void SecureApplicationLayer::dataSystemBroadcastRequest(
    AckType ack, HopCountType hopType, Priority priority, APDU& apdu,
    const SecurityControl& secCtrl) {
  println("dataSystemBroadcastRequest");

  if (secCtrl.dataSecurity != DataSecurity::None) {
    apdu.frame().sourceAddress(_deviceObj.individualAddress());
    apdu.frame().destinationAddress(0x0000);
    apdu.frame().addressType(GroupAddress);
    apdu.frame().systemBroadcast(SysBroadcast);

    uint16_t secureApduLength =
        apdu.length() + 3 + 6 +
        4;  // 3(TPCI,APCI,SCF) + sizeof(seqNum) + apdu.length() + 4
    CemiFrame secureFrame(secureApduLength);

    // create secure APDU
    if (createSecureApdu(apdu, secureFrame.apdu(), secCtrl)) {
      ApplicationLayer::dataSystemBroadcastRequest(ack, hopType, SystemPriority,
                                                   secureFrame.apdu(), secCtrl);
    }

    return;
  }

  ApplicationLayer::dataSystemBroadcastRequest(ack, hopType, SystemPriority,
                                               apdu, secCtrl);
}

void SecureApplicationLayer::dataIndividualRequest(
    AckType ack, HopCountType hopType, Priority priority, uint16_t destination,
    APDU& apdu, const SecurityControl& secCtrl) {
#ifdef KNX_SNIFFER_MODE
  // SNIFFER: Block ALL TX silently
  return;
#endif

  if (secCtrl.dataSecurity != DataSecurity::None) {
    printk("\n\n");
    LOG_WRN("[0x%04X->0x%04X] APCI=0x%04X (%s) len=%d",
            _deviceObj.individualAddress(), destination, (uint16_t)apdu.type(),
            apciName((uint16_t)apdu.type()), apdu.length());
    if (apdu.length() > 0) {
      LOG_HEXDUMP_WRN(apdu.data(), apdu.length() + 1, "TX PAYLOAD:");
    }

    // DEBUG TPCI
    CemiFrame& currentFrame = apdu.frame();
    LOG_WRN("<<< TX TPDU: seq_num=0x%02X, type=0x%02X",
            currentFrame.tpdu().sequenceNumber(), currentFrame.tpdu().type());

    apdu.frame().sourceAddress(_deviceObj.individualAddress());
    apdu.frame().destinationAddress(destination);
    apdu.frame().addressType(IndividualAddress);

    uint16_t secureApduLength =
        apdu.length() + 3 + 6 +
        4;  // 3(TPCI,APCI,SCF) + sizeof(seqNum) + apdu.length() + 4
    CemiFrame secureFrame(secureApduLength);

    // create secure APDU
    if (createSecureApdu(apdu, secureFrame.apdu(), secCtrl)) {
      LOG_WRN("<<< TX Individual SECURE OK, encrypted len=%d",
              secureFrame.apdu().length());
      ApplicationLayer::dataIndividualRequest(
          ack, hopType, priority, destination, secureFrame.apdu(), secCtrl);
    } else {
      LOG_ERR(
          "!!! TX Individual createSecureApdu FAILED! Response NOT sent "
          "to 0x%04X!",
          destination);
    }

    return;
  }

  LOG_WRN("<<< TX Individual PLAIN to 0x%04X APCI=0x%04X (%s) len=%d",
          destination, (uint16_t)apdu.type(), apciName((uint16_t)apdu.type()),
          apdu.length());
  if (apdu.length() > 0) {
    LOG_HEXDUMP_WRN(apdu.data(), apdu.length() + 1, "TX PAYLOAD:");
  }
  ApplicationLayer::dataIndividualRequest(ack, hopType, priority, destination,
                                          apdu, secCtrl);
}

void SecureApplicationLayer::dataConnectedRequest(
    uint16_t tsap, Priority priority, APDU& apdu,
    const SecurityControl& secCtrl) {
#ifdef KNX_SNIFFER_MODE
  // SNIFFER: Block ALL TX silently
  return;
#endif

  if (secCtrl.dataSecurity != DataSecurity::None) {
    printk("\n\n");
    LOG_WRN("[0x%04X->0x%04X] APCI=0x%04X (%s) len=%d",
            _deviceObj.individualAddress(),
            _transportLayer->getConnectionAddress(), (uint16_t)apdu.type(),
            apciName((uint16_t)apdu.type()), apdu.length());
    if (apdu.length() > 0) {
      LOG_HEXDUMP_WRN(apdu.data(), apdu.length() + 1, "TX PAYLOAD:");
    }

    apdu.frame().sourceAddress(_deviceObj.individualAddress());
    apdu.frame().destinationAddress(_transportLayer->getConnectionAddress());
    apdu.frame().addressType(IndividualAddress);

    uint16_t secureApduLength =
        apdu.length() + 3 + 6 +
        4;  // 3(TPCI,APCI,SCF) + sizeof(seqNum) + apdu.length() + 4
    CemiFrame secureFrame(secureApduLength);

    // create secure APDU
    if (createSecureApdu(apdu, secureFrame.apdu(), secCtrl)) {
      LOG_WRN("<<< TX Connected SECURE OK, encrypted len=%d",
              secureFrame.apdu().length());
      ApplicationLayer::dataConnectedRequest(tsap, priority, secureFrame.apdu(),
                                             secCtrl);
    } else {
      LOG_ERR("!!! TX Connected createSecureApdu FAILED! Response NOT sent!");
    }

    return;
  }

  LOG_WRN("<<< TX Connected PLAIN tsap=0x%04X APCI=0x%04X (%s) len=%d", tsap,
          (uint16_t)apdu.type(), apciName((uint16_t)apdu.type()),
          apdu.length());
  if (apdu.length() > 0) {
    LOG_HEXDUMP_WRN(apdu.data(), apdu.length() + 1, "TX PAYLOAD:");
  }

  // DEBUG TPCI
  CemiFrame& currentFrame = apdu.frame();
  LOG_WRN("<<< TX TPDU: seq_num=0x%02X, type=0x%02X",
          currentFrame.tpdu().sequenceNumber(), currentFrame.tpdu().type());

  currentFrame.sourceAddress(
      _deviceObj
          .individualAddress());  // apdu must be valid until it was confirmed
  ApplicationLayer::dataConnectedRequest(tsap, priority, apdu, secCtrl);
}

void SecureApplicationLayer::encryptAesCbc(uint8_t* buffer, uint16_t bufLen,
                                           const uint8_t* iv,
                                           const uint8_t* key) {
  // Use zeroes as IV for first round
  uint8_t zeroIv[16] = {0x00};

  struct AES_ctx ctx;
  AES_init_ctx_iv(&ctx, key, zeroIv);

  // Now encrypt first block B0.
  AES_CBC_encrypt_buffer(&ctx, (uint8_t*)iv, 16);

  // Encrypt remaining buffer
  AES_CBC_encrypt_buffer(&ctx, buffer, bufLen);
}

void SecureApplicationLayer::xcryptAesCtr(uint8_t* buffer, uint16_t bufLen,
                                          const uint8_t* iv,
                                          const uint8_t* key) {
  struct AES_ctx ctx;

  AES_init_ctx_iv(&ctx, key, iv);

  AES_CTR_xcrypt_buffer(&ctx, buffer, bufLen);
}

uint32_t SecureApplicationLayer::calcAuthOnlyMac(uint8_t* apdu,
                                                 uint8_t apduLength,
                                                 const uint8_t* key,
                                                 uint8_t* iv, uint8_t* ctr0) {
  uint16_t bufLen = 2 + apduLength;  // 2 bytes for the length field (uint16_t)
  // AES-128 operates on blocks of 16 bytes, add padding
  uint16_t bufLenPadded = (bufLen + 15) / 16 * 16;
  uint8_t buffer[bufLenPadded];
  // Make sure to have zeroes everywhere, because of the padding
  memset(buffer, 0x00, bufLenPadded);

  uint8_t* pBuf = buffer;

  pBuf = pushWord(apduLength, pBuf);
  pBuf = pushByteArray(apdu, apduLength, pBuf);

  encryptAesCbc(buffer, bufLenPadded, iv, key);
  xcryptAesCtr(buffer, 4, ctr0, key);  // 4 bytes only for the MAC

  uint32_t mac;
  popInt(mac, &buffer[0]);

  return mac;
}

uint32_t SecureApplicationLayer::calcConfAuthMac(
    uint8_t* associatedData, uint16_t associatedDataLength, uint8_t* apdu,
    uint8_t apduLength, const uint8_t* key, uint8_t* iv) {
  uint16_t bufLen = 2 + associatedDataLength +
                    apduLength;  // 2 bytes for the length field (uint16_t)
  // AES-128 operates on blocks of 16 bytes, add padding
  uint16_t bufLenPadded = (bufLen + 15) / 16 * 16;
  uint8_t buffer[bufLenPadded];
  // Make sure to have zeroes everywhere, because of the padding
  memset(buffer, 0x00, bufLenPadded);

  uint8_t* pBuf = buffer;

  pBuf = pushWord(associatedDataLength, pBuf);
  pBuf = pushByteArray(associatedData, associatedDataLength, pBuf);
  pBuf = pushByteArray(apdu, apduLength, pBuf);

  encryptAesCbc(buffer, bufLenPadded, iv, key);

  uint32_t mac;
  popInt(mac, &buffer[bufLenPadded - 16]);  // bufLenPadded has a guaranteed
                                            // minimum size of 16 bytes

  return mac;
}

void SecureApplicationLayer::block0(uint8_t* buffer, uint8_t* seqNum,
                                    uint16_t indSrcAddr, uint16_t dstAddr,
                                    bool dstAddrIsGroupAddr,
                                    uint8_t extFrameFormat, uint8_t tpci,
                                    uint8_t apci, uint8_t payloadLength) {
  uint8_t* pBuf = buffer;
  pBuf = pushByteArray(seqNum, 6, pBuf);
  pBuf = pushWord(indSrcAddr, pBuf);
  pBuf = pushWord(dstAddr, pBuf);
  pBuf = pushByte(0x00, pBuf);  // FT: frametype
  pBuf = pushByte((dstAddrIsGroupAddr ? 0x80 : 0x00) | (extFrameFormat & 0xf),
                  pBuf);        // AT: address type
  pBuf = pushByte(tpci, pBuf);  // TPCI
  pBuf = pushByte(apci, pBuf);  // APCI // draft spec shows something different!
  pBuf = pushByte(0x00, pBuf);  // Reserved: fixed 0x00 (really?)
  pBuf = pushByte(payloadLength, pBuf);  // Payload length
}

void SecureApplicationLayer::blockCtr0(uint8_t* buffer, uint8_t* seqNum,
                                       uint16_t indSrcAddr, uint16_t dstAddr) {
  uint8_t* pBuf = buffer;
  pBuf = pushByteArray(seqNum, 6, pBuf);
  pBuf = pushWord(indSrcAddr, pBuf);
  pBuf = pushWord(dstAddr, pBuf);
  pBuf = pushInt(0x00000000, pBuf);
  pBuf = pushByte(0x01, pBuf);
}

uint16_t SecureApplicationLayer::groupAddressIndex(uint16_t groupAddr) {
  // Just for safety reasons, we should never get here, because the
  // dataGroupIndication will return already return early without doing anything
  if (_addrTab == nullptr) return 0;

  return _addrTab->getTsap(groupAddr);
}

const uint8_t* SecureApplicationLayer::securityKey(uint16_t addr,
                                                   bool isGroupAddress) {
  if (isGroupAddress) {
    uint16_t gaIndex = groupAddressIndex(addr);

    if (gaIndex > 0) return _secIfObj.groupKey(gaIndex);
  } else {
    uint16_t iaIndex = _secIfObj.indAddressIndex(addr);

    if (iaIndex > 0) return _secIfObj.p2pKey(iaIndex);
  }

  return nullptr;
}

// returns next outgoing sequence number for secure communication
uint64_t SecureApplicationLayer::nextSequenceNumber(bool toolAccess) {
  if (toolAccess) {
    return _sequenceNumberToolAccess;
  } else {
    return _sequenceNumber;
  }
}

// stores next outgoing sequence number for secure communication
void SecureApplicationLayer::updateSequenceNumber(bool toolAccess,
                                                  uint64_t seqNum) {
  if (toolAccess) {
    _sequenceNumberToolAccess = seqNum;

    // BLOCK SAVES UNTIL LOADED: Prevent overwriting persistent data with
    // defaults during early boot transmissions (first 3 seconds).
    if (!_sequenceNumberToolLoaded) return;

    if (_sequenceNumberToolAccess >= _lastSavedSequenceNumberTool + 2000) {
      _secIfObj.setSequenceNumber(true, _sequenceNumberToolAccess);
      _lastSavedSequenceNumberTool = _sequenceNumberToolAccess;
      LOG_INF(">>> SEQ_DEBUG: SAVED Tool SeqNum to Flash: 0x%llX",
              _sequenceNumberToolAccess);

      // Isolated NVS persistence
      _bauSystemB.platform().saveSequenceNumber(true,
                                                _sequenceNumberToolAccess);

      // Force immediate commit to NVS
      _bauSystemB.writeMemory();
      _bauSystemB.memory().saveMemory();
    }
  } else {
    _sequenceNumber = seqNum;

    // BLOCK SAVES UNTIL LOADED: Prevent overwriting persistent data with
    // defaults during early boot transmissions (first 3 seconds).
    if (!_sequenceNumberLoaded) return;

    if (_sequenceNumber >= _lastSavedSequenceNumber + 2000) {
      // Isolated NVS persistence
      _bauSystemB.platform().saveSequenceNumber(false, _sequenceNumber);

      // REDUCED PRESSURE: Only save standard KNX memory every 10,000 instead of
      // 2,000 Isolated NVS already has the high-frequency updates.
      if (_sequenceNumber >= _lastSavedSequenceNumberStandard + 10000) {
        LOG_INF(
            ">>> SEQ_DEBUG: Frequency limit reached. Syncing full EEPROM "
            "buffer.");
        _secIfObj.setSequenceNumber(false, _sequenceNumber);
        _lastSavedSequenceNumberStandard = _sequenceNumber;
        _bauSystemB.writeMemory();
        _bauSystemB.memory().saveMemory();
      }
    }
  }
}

uint64_t SecureApplicationLayer::lastValidSequenceNumber(bool toolAccess,
                                                         uint16_t srcAddr) {
  if (toolAccess) {
    // TODO: check if we really have to support multiple tools at the same time
    if (srcAddr == _deviceObj.individualAddress())
      return _sequenceNumberToolAccess;

    return _lastValidSequenceNumberTool;
  } else {
    return _secIfObj.getLastValidSequenceNumber(srcAddr);
  }

  return 0;
}

void SecureApplicationLayer::updateLastValidSequence(bool toolAccess,
                                                     uint16_t remoteAddr,
                                                     uint64_t seqNo) {
  if (toolAccess)
    // TODO: check if we really have to support multiple tools at the same time
    _lastValidSequenceNumberTool = seqNo;
  else {
    _secIfObj.setLastValidSequenceNumber(remoteAddr, seqNo);
  }
}

void SecureApplicationLayer::sendSyncRequest(uint16_t dstAddr,
                                             bool dstAddrIsGroupAddr,
                                             const SecurityControl& secCtrl,
                                             bool systemBcast) {
#ifdef KNX_SNIFFER_MODE
  // SNIFFER: Block SyncRequest silently
  return;
#endif
  if (secCtrl.dataSecurity != DataSecurity::AuthConf) {
    println("sync.req is always sent with auth+conf!");
    return;
  }

  _syncReqBroadcastOutgoing = (dstAddr == 0x0000) && dstAddrIsGroupAddr;

  // use random number in SyncResponse
  uint64_t challenge = getRandomNumber();

  uint8_t asdu[6];
  sixBytesFromUInt64(challenge, &asdu[0]);

  CemiFrame request(2 + 6 + sizeof(asdu) +
                    4);  // 2 bytes (APCI, SCF) + 6 bytes (SeqNum) + 6 bytes
                         // (challenge) + 4 bytes (MAC)
  // Note: additional TPCI byte is already handled internally!

  uint8_t tpci = 0;

  if (!_syncReqBroadcastOutgoing) {
    if (isConnected()) {
      tpci |= 0x40 |
              _transportLayer
                  ->getTpciSeqNum();  // get next TPCI sequence number for MAC
                                      // calculation from TL (T_DATA_CONNECTED)
    }
  }

  print("sendSyncRequest: TPCI: ");
  println(tpci, HEX);

  if (secure(request.data() + APDU_LPDU_DIFF, kSecureSyncRequest,
             _deviceObj.individualAddress(), dstAddr, dstAddrIsGroupAddr, tpci,
             asdu, sizeof(asdu), secCtrl, systemBcast)) {
    println("SyncRequest: ");
    request.apdu().printPDU();

    if (_syncReqBroadcastOutgoing) {
      _transportLayer->dataBroadcastRequest(
          AckType::AckDontCare, HopCountType::NetworkLayerParameter,
          Priority::SystemPriority, request.apdu());
    } else {
      // either send on T_DATA_INDIVIDUAL or T_DATA_CONNECTED
      if (isConnected()) {
        _transportLayer->dataConnectedRequest(getConnectedTsasp(),
                                              SystemPriority, request.apdu());
      } else {
        // Send encrypted SyncResponse message using T_DATA_INDIVIDUAL
        _transportLayer->dataIndividualRequest(
            AckType::AckDontCare, NetworkLayerParameter, SystemPriority,
            dstAddr, request.apdu());
      }
    }

    Addr toAddr =
        _syncReqBroadcastOutgoing ? (Addr)GrpAddr(0) : (Addr)IndAddr(dstAddr);
    _pendingOutgoingSyncRequests.insertOrAssign(toAddr, challenge);
  } else {
    println("SyncRequest: failure during encryption");
  }
}

void SecureApplicationLayer::sendSyncResponse(uint16_t dstAddr,
                                              bool dstAddrIsGroupAddr,
                                              const SecurityControl& secCtrl,
                                              uint64_t remoteNextSeqNum,
                                              bool systemBcast) {
#ifdef KNX_SNIFFER_MODE
  // SNIFFER: Block SyncResponse silently
  return;
#endif
  LOG_INF(">>> sendSyncResponse() to 0x%04X", dstAddr);

  if (secCtrl.dataSecurity != DataSecurity::AuthConf) {
    println("sync.res is always sent with auth+conf!");
    LOG_ERR("sync.res REJECTED: not AuthConf!");
    return;
  }

  uint64_t ourNextSeqNum = nextSequenceNumber(secCtrl.toolAccess);

  uint8_t asdu[12];
  sixBytesFromUInt64(ourNextSeqNum, &asdu[0]);
  sixBytesFromUInt64(remoteNextSeqNum, &asdu[6]);

  CemiFrame response(
      2 + 6 + sizeof(asdu) +
      4);  // 2 bytes (APCI, SCF) + 6 bytes (SeqNum) + 12 bytes + 4 bytes (MAC)
  // Note: additional TPCI byte is already handled internally!

  uint8_t tpci = 0;

  if (!_syncReqBroadcastIncoming) {
    if (isConnected()) {
      tpci |= 0x40 |
              _transportLayer
                  ->getTpciSeqNum();  // get next TPCI sequence number for MAC
                                      // calculation from TL (T_DATA_CONNECTED)
    }
  }

  print("sendSyncResponse: TPCI: ");
  println(tpci, HEX);

  if (secure(response.data() + APDU_LPDU_DIFF, kSecureSyncResponse,
             _deviceObj.individualAddress(), dstAddr, dstAddrIsGroupAddr, tpci,
             asdu, sizeof(asdu), secCtrl, systemBcast)) {
    _lastSyncRes = millis();

    println("SyncResponse: ");
    response.apdu().printPDU();

    if (_syncReqBroadcastIncoming) {
      LOG_INF(">>> Sending SyncResponse via BROADCAST");
      _transportLayer->dataBroadcastRequest(
          AckType::AckDontCare, HopCountType::NetworkLayerParameter,
          Priority::SystemPriority, response.apdu());
    } else {
      // either send on T_DATA_INDIVIDUAL or T_DATA_CONNECTED
      if (isConnected()) {
        LOG_INF(">>> Sending SyncResponse via CONNECTED");
        _transportLayer->dataConnectedRequest(getConnectedTsasp(),
                                              SystemPriority, response.apdu());
      } else {
        LOG_INF(">>> Sending SyncResponse via INDIVIDUAL to 0x%04X", dstAddr);
        // Send encrypted SyncResponse message using T_DATA_INDIVIDUAL
        _transportLayer->dataIndividualRequest(
            AckType::AckDontCare, NetworkLayerParameter, SystemPriority,
            dstAddr, response.apdu());
      }
    }
    LOG_INF(">>> SyncResponse SENT successfully!");
  } else {
    println("SyncResponse: failure during encryption");
    LOG_ERR(">>> SyncResponse FAILED: encryption error!");
  }
}

void SecureApplicationLayer::receivedSyncRequest(
    uint16_t srcAddr, uint16_t dstAddr, bool dstAddrIsGroupAddr,
    const SecurityControl& secCtrl, uint8_t* seqNum, uint64_t challenge,
    bool systemBcast) {
  println("Received SyncRequest:");
  LOG_INF(">>> receivedSyncRequest from 0x%04X to 0x%04X", srcAddr, dstAddr);

  uint64_t nextRemoteSeqNum = sixBytesToUInt64(seqNum);
  uint64_t nextSeqNum =
      1 + lastValidSequenceNumber(secCtrl.toolAccess, srcAddr);

  if (nextRemoteSeqNum > nextSeqNum) {
    updateLastValidSequence(secCtrl.toolAccess, srcAddr, nextRemoteSeqNum - 1);
    nextSeqNum = nextRemoteSeqNum;
  }

  _syncReqBroadcastIncoming = (dstAddr == 0x0000) && dstAddrIsGroupAddr;

  // Remember challenge for securing the sync.res later
  _pendingIncomingSyncRequests.insertOrAssign(
      _syncReqBroadcastIncoming ? (Addr)GrpAddr(0) : (Addr)IndAddr(srcAddr),
      challenge);

  uint16_t toAddr = _syncReqBroadcastIncoming ? dstAddr : srcAddr;
  bool toIsGroupAddress = _syncReqBroadcastIncoming;

  LOG_INF(">>> Sending SyncResponse to 0x%04X (isGroup=%d)", toAddr,
          toIsGroupAddress);
  sendSyncResponse(toAddr, toIsGroupAddress, secCtrl, nextSeqNum, systemBcast);
}

void SecureApplicationLayer::receivedSyncResponse(
    uint16_t remote, const SecurityControl& secCtrl, uint8_t* plainApdu) {
  println("Received SyncResponse:");

  if (_syncReqBroadcastOutgoing) {
    if (_pendingOutgoingSyncRequests.get(GrpAddr(0)) == nullptr) {
      println(
          "Cannot handle sync.res without pending sync.req! "
          "(broadcast/systembroadcast)");
      return;
    }
  } else {
    if (_pendingOutgoingSyncRequests.get(IndAddr(remote)) == nullptr) {
      println("Cannot handle sync.res without pending sync.req!");
      return;
    }
  }

  // Bytes 0-5 in the "APDU" buffer contain the remote sequence number
  // Bytes 6-11 in the "APDU" buffer contain the local sequence number
  uint64_t remoteSeq = sixBytesToUInt64(plainApdu + 0);
  uint64_t localSeq = sixBytesToUInt64(plainApdu + 6);

  uint64_t last = lastValidSequenceNumber(secCtrl.toolAccess, remote);

  if (remoteSeq - 1 > last) {
    // logger.debug("sync.res update {} last valid {} seq -> {}", remote,
    // toolAccess ? "tool access" : "p2p", remoteSeq -1);
    updateLastValidSequence(secCtrl.toolAccess, remote, remoteSeq - 1);
  }

  uint64_t next = nextSequenceNumber(secCtrl.toolAccess);

  if (localSeq > next) {
    // logger.debug("sync.res update local next {} seq -> {}", toolAccess ?
    // "tool access" : "p2p", localSeq);
    updateSequenceNumber(secCtrl.toolAccess, localSeq);
  }

  Addr remoteAddr =
      _syncReqBroadcastOutgoing ? (Addr)GrpAddr(0) : (Addr)IndAddr(remote);
  _pendingOutgoingSyncRequests.erase(remoteAddr);
}

bool SecureApplicationLayer::decrypt(uint8_t* plainApdu,
                                     uint16_t plainApduLength, uint16_t srcAddr,
                                     uint16_t dstAddr, bool dstAddrIsGroupAddr,
                                     uint8_t tpci, uint8_t* secureAsdu,
                                     SecurityControl& secCtrl,
                                     bool systemBcast) {
  const uint8_t* pBuf;
  uint8_t scf;

  pBuf = popByte(scf, secureAsdu);

  bool toolAccess = ((scf & 0x80) == 0x80);
  bool systemBroadcast = ((scf & 0x08) == 0x08);
  uint8_t sai =
      (scf >> 4) & 0x07;  // sai can only be 0x0 (CCM auth only) or 0x1 (CCM
                          // with auth+conf), other values are reserved
  bool authOnly = (sai == 0);
  uint8_t service =
      (scf & 0x07);  // only 0x0 (S-A_Data-PDU), 0x2 (S-A_Sync_Req-PDU) or 0x3
                     // (S-A_Sync_Rsp-PDU) are valid values

  // DEBUG: Log ALL decrypt calls to understand what ETS is sending
  const char* serviceNames[] = {"DATA", "???", "SYNC_REQ", "SYNC_RES"};
  const char* serviceName = (service <= 3) ? serviceNames[service] : "UNKNOWN";
  LOG_INF(
      ">>> decrypt: %s from 0x%04X to 0x%04X, len=%d, toolAccess=%d, "
      "authOnly=%d",
      serviceName, srcAddr, dstAddr, plainApduLength, toolAccess, authOnly);

  if (systemBroadcast != systemBcast) {
    println("SBC flag in SCF does not match actual communication mode!");
  }

  secCtrl.toolAccess = toolAccess;
  secCtrl.dataSecurity = authOnly ? DataSecurity::Auth : DataSecurity::AuthConf;

  bool syncReq = service == kSecureSyncRequest;
  bool syncRes = service == kSecureSyncResponse;

  // const uint8_t* key = dstAddrIsGroupAddr ? securityKey(dstAddr,
  // dstAddrIsGroupAddr) : toolAccess ? toolKey() : securityKey(srcAddr, false);
  const uint8_t* key = dstAddrIsGroupAddr && (dstAddr != 0)
                           ? securityKey(dstAddr, dstAddrIsGroupAddr)
                       : toolAccess ? _secIfObj.toolKey()
                                    : securityKey(srcAddr, false);

  if (key == nullptr) {
    print("Error: No key found. toolAccess: ");
    println(toolAccess ? "true" : "false");
    return false;
  }

  uint8_t seqNum[6];
  pBuf = popByteArray(seqNum, 6, pBuf);
  uint64_t receivedSeqNumber = sixBytesToUInt64(seqNum);

  // Provide array for KNX serial number if it is a SyncRequest
  // DataService and SyncResponse do not use this variable.
  uint8_t knxSerialNumber[6];

  uint16_t remainingPlainApduLength = plainApduLength;

  if (service == kSecureDataPdu) {
#ifdef KNX_SNIFFER_MODE
    // SNIFFER: Skip sequence number validation — we're just observing
#else
    if (srcAddr != _deviceObj.individualAddress()) {
      uint64_t lastValidSeq = lastValidSequenceNumber(toolAccess, srcAddr);
      uint64_t expectedSeqNumber = lastValidSeq + 1;

      LOG_INF(
          ">>> SeqNum check: received=0x%llX, lastValid=0x%llX, "
          "expected=0x%llX",
          (unsigned long long)receivedSeqNumber,
          (unsigned long long)lastValidSeq,
          (unsigned long long)expectedSeqNumber);

      if (receivedSeqNumber < expectedSeqNumber) {
        // security failure - REPLAY ATTACK or sequence out of sync
        LOG_ERR(
            "!!! SECURITY FAILURE: seqNum 0x%llX < expected 0x%llX (REPLAY?)",
            (unsigned long long)receivedSeqNumber,
            (unsigned long long)expectedSeqNumber);
        LOG_ERR(
            "!!! This may happen if device restarted and lost sequence state!");
        return false;
      }
    }
#endif
  } else if (syncReq) {
    pBuf = popByteArray(knxSerialNumber, 6, pBuf);
    remainingPlainApduLength -= 6;

    // DEBUG: Print serial numbers for comparison
    LOG_INF(">>> syncReq: Checking Serial Number match");
    LOG_HEXDUMP_INF(knxSerialNumber, 6, "Serial from ETS:");
    const uint8_t* deviceSerial = _deviceObj.propertyData(PID_SERIAL_NUMBER);
    if (deviceSerial != nullptr) {
      LOG_HEXDUMP_INF(deviceSerial, 6, "Device Serial:");
    } else {
      LOG_ERR("Device Serial is NULL!");
    }

    // Check if sync.req is addressed to us
    uint8_t emptySerialNumber[6] = {0};
    bool serialMatches =
        (memcmp(knxSerialNumber, _deviceObj.propertyData(PID_SERIAL_NUMBER),
                6) == 0);
    bool isWildcard = (memcmp(knxSerialNumber, emptySerialNumber, 6) == 0);

    // Accept if: serial matches OR is wildcard (empty serial = broadcast to
    // all)
    if (!serialMatches && !isWildcard) {
      LOG_ERR(">>> syncReq REJECTED: Serial doesn't match and not wildcard!");
      return false;
    }

    LOG_INF(">>> syncReq ACCEPTED: serial %s",
            serialMatches ? "matches" : "is wildcard");

    // if responded to another request within the last 1 second, ignore
    if ((millis() - _lastSyncRes) < 1000) {
      LOG_WRN(
          ">>> syncReq REJECTED: Too soon after last response (rate limit)");
      return false;
    }
  } else if (syncRes) {
    // fetch challenge depending on srcAddr to handle multiple requests
    uint64_t* challenge = _pendingOutgoingSyncRequests.get(IndAddr(srcAddr));

    if (challenge == nullptr) {
      println("Cannot find matching challenge for source address!");
      return false;
    }

    uint8_t _challengeSixBytes[6];
    sixBytesFromUInt64(*challenge, _challengeSixBytes);

    // in a sync.res, seq actually contains our challenge from sync.req xored
    // with a random value extract the random value and store it in seqNum to
    // use it for block0 and ctr0
    for (uint8_t i = 0; i < sizeof(seqNum); i++) {
      seqNum[i] ^= _challengeSixBytes[i];
    }
  }

  pBuf = popByteArray(plainApdu, remainingPlainApduLength, pBuf);

  // No LTE-HEE for now
  // Data Secure always uses extended frame format with APDU length > 15 octets
  // (long messages), no standard frames
  uint8_t extendedFrameFormat = 0;
  // Clear IV buffer
  uint8_t iv[16] = {0x00};
  // Create first block B0 for AES CBC MAC calculation, used as IV later
  /*
    printHex("seq: ", seqNum, 6);
    printHex("src: ", (uint8_t*) &srcAddr, 2);
    printHex("dst: ", (uint8_t*) &dstAddr, 2);
    print("dstAddrisGroup: ");println(dstAddrIsGroupAddr ? "true" : "false");
  */
  block0(iv, seqNum, srcAddr, dstAddr, dstAddrIsGroupAddr, extendedFrameFormat,
         tpci | (SecureService >> 8), SecureService & 0x00FF,
         remainingPlainApduLength);

  // Clear block counter0 buffer
  uint8_t ctr0[16] = {0x00};
  // Create first block for block counter 0
  blockCtr0(ctr0, seqNum, srcAddr, dstAddr);

  uint32_t mac;
  pBuf = popInt(mac, pBuf);

#ifndef KNX_SNIFFER_MODE
  LOG_WRN(
      ">>> DECRYPT PARAMS: src=0x%04X, dst=0x%04X, grp=%d, tpci=0x%02X, "
      "extFmt=%d",
      srcAddr, dstAddr, dstAddrIsGroupAddr, tpci, extendedFrameFormat);
  // LOG_HEXDUMP_WRN(key, 16, ">>> KEY:");
  // LOG_HEXDUMP_WRN(iv, 16, ">>> IV (Block 0):");
  // LOG_HEXDUMP_WRN(ctr0, 16, ">>> Ctr0:");
  LOG_HEXDUMP_WRN(plainApdu, remainingPlainApduLength, ">>> APDU:");
#endif

  if (authOnly) {
    // APDU is already plain, no decryption needed

    // Only check the MAC
    uint32_t calculatedMac =
        calcAuthOnlyMac(plainApdu, remainingPlainApduLength, key, iv, ctr0);

    if (calculatedMac != mac) {
      // security failure
      print("security failure(auth): calculated MAC: ");
      print(calculatedMac, HEX);
      print(" != received MAC: ");
      print(mac, HEX);
      println("\n");

      LOG_ERR("!!! MAC MISMATCH (auth): calc=0x%08X, recv=0x%08X",
              calculatedMac, mac);
      LOG_HEXDUMP_ERR(SecurityInterfaceObject::fdsk(), 16, "Active FDSK:");
      LOG_HEXDUMP_ERR(key, 16, "Active KEY (Used for decrypt):");
      const uint8_t* serial = _deviceObj.propertyData(PID_SERIAL_NUMBER);
      if (serial) LOG_HEXDUMP_ERR(serial, 6, "Device Serial:");

      return false;
    }

    memcpy(plainApdu, secureAsdu, remainingPlainApduLength);
  } else {
    // APDU is encrypted and needs decryption

    uint16_t bufLen = 4 + remainingPlainApduLength;
    // AES-128 operates on blocks of 16 bytes, add padding
    uint16_t bufLenPadded = (bufLen + 15) / 16 * 16;
    uint8_t buffer[bufLenPadded];
    // uint8_t buffer[bufLen];
    //  Make sure to have zeroes everywhere, because of the padding
    memset(buffer, 0x00, bufLenPadded);

    pushInt(mac, &buffer[0]);
    pushByteArray(plainApdu, remainingPlainApduLength,
                  &buffer[4]);  // apdu is still encrypted

    xcryptAesCtr(buffer, bufLenPadded, ctr0, key);
    // xcryptAesCtr(buffer, bufLen, ctr0, key);

    uint32_t decryptedMac;
    popInt(decryptedMac, &buffer[0]);
    popByteArray(plainApdu, remainingPlainApduLength,
                 &buffer[4]);  // apdu is now decrypted (overwritten)

    // Do calculations for Auth+Conf
    uint8_t associatedData[syncReq ? 7 : 1];
    associatedData[0] = scf;

    if (syncReq) {
      memcpy(&associatedData[1], knxSerialNumber, 6);
    }

    /*
            printHex("APDU--------->", plainApdu, remainingPlainApduLength);
            printHex("Key---------->", key, 16);
            printHex("ASSOC-------->", associatedData, sizeof(associatedData));
    */
    uint32_t calculatedMac =
        calcConfAuthMac(associatedData, sizeof(associatedData), plainApdu,
                        remainingPlainApduLength, key, iv);

    if (calculatedMac != decryptedMac) {
      // security failure
      print("security failure(conf+auth): calculated MAC: ");
      print(calculatedMac, HEX);
      print(" != decrypted MAC: ");
      print(decryptedMac, HEX);
      println("\n");

      LOG_ERR("!!! MAC MISMATCH (conf+auth): calc=0x%08X, decrypt=0x%08X",
              calculatedMac, decryptedMac);
      LOG_HEXDUMP_ERR(SecurityInterfaceObject::fdsk(), 16, "Active FDSK:");
      LOG_HEXDUMP_ERR(key, 16, "Active KEY (Used for decrypt):");
      const uint8_t* serial = _deviceObj.propertyData(PID_SERIAL_NUMBER);
      if (serial) LOG_HEXDUMP_ERR(serial, 6, "Device Serial:");

      return false;
    }

#ifdef KNX_SNIFFER_MODE
    // SNIFFER: Don't process sync requests/responses, don't update state
    if (syncReq) {
      LOG_WRN(">>> SNIFFER: SyncRequest from 0x%04X (observed, not processed)",
              srcAddr);
      return false;
    } else if (syncRes) {
      LOG_WRN(">>> SNIFFER: SyncResponse from 0x%04X (observed, not processed)",
              srcAddr);
      return false;
    }
    // SNIFFER: Skip sequence state updates
#else
    // prevent a sync.req sent by us to trigger sync notification, this happens
    // if we provide our own tool key for decryption above
    if (syncReq && (srcAddr == _deviceObj.individualAddress())) return false;

    if (syncReq) {
      uint64_t challenge = sixBytesToUInt64(&plainApdu[0]);
      receivedSyncRequest(srcAddr, dstAddr, dstAddrIsGroupAddr, secCtrl, seqNum,
                          challenge, systemBroadcast);
      LOG_WRN(
          ">>> SyncRequest from 0x%04X handled OK (sends SyncResponse, "
          "returns false by design)",
          srcAddr);
      return false;
    } else if (syncRes) {
      receivedSyncResponse(srcAddr, secCtrl, plainApdu);
      return false;
    } else {
      if (srcAddr == _deviceObj.individualAddress()) {
        print("Update our next ");
        print(toolAccess ? "tool access" : "");
        print(" seq from ");
        print(srcAddr, HEX);
        print(" -> (+1) ");
        println(receivedSeqNumber, HEX);
        updateSequenceNumber(toolAccess, receivedSeqNumber + 1);
      } else {
        print("Update last valid ");
        print(toolAccess ? "tool access" : "");
        print(" seq from ");
        print(srcAddr, HEX);
        print(" -> ");
        println(receivedSeqNumber, HEX);
        updateLastValidSequence(toolAccess, srcAddr, receivedSeqNumber);
      }
    }
#endif

    // Dump decrypted payload to see what ETS sent
    char rxHex[128];
    int rPos = 0;
    for (int i = 0; i < remainingPlainApduLength && i < 32; i++) {
      rPos += sprintf(&rxHex[rPos], "%02X ", plainApdu[i]);
    }
    LOG_INF(">>> decrypt: SUCCESS. Payload (len=%d): %s",
            remainingPlainApduLength, rxHex);
  }

  LOG_INF(">>> decrypt: SUCCESS - data ready for processing");
  return true;
}

bool SecureApplicationLayer::decodeSecureApdu(APDU& secureApdu, APDU& plainApdu,
                                              SecurityControl& secCtrl) {
  // Decode secure APDU

  println("decodeSecureApdu: Secure APDU: ");
  secureApdu.printPDU();

  uint16_t srcAddress = secureApdu.frame().sourceAddress();
  uint16_t dstAddress = secureApdu.frame().destinationAddress();
  bool isDstAddrGroupAddr = secureApdu.frame().addressType() == GroupAddress;
  bool isSystemBroadcast = secureApdu.frame().systemBroadcast();
  uint8_t tpci =
      secureApdu.frame()
          .data()[TPDU_LPDU_DIFF];  // FIXME: when cEMI class is refactored,
                                    // there might be additional info fields in
                                    // cEMI [fixed TPDU_LPDU_DIFF]
  print("decodeSecureApdu: TPCI: ");
  println(tpci, HEX);
  // Note:
  // The TPCI is also included in the MAC calculation to provide authenticity
  // for this field. However, a secure APDU (with a MAC) is only included in
  // transport layer PDUs T_DATA_GROUP, T_DATA_TAG_GROUP, T_DATA_INDIVIDUAL,
  // T_DATA_CONNECTED and not in T_CONNECT, T_DISCONNECT, T_ACK, T_NACK. This
  // means the DATA/CONTROL flag is always 0(=DATA). The flag "NUMBERED"
  // differentiates between T_DATA_INDIVIDUAL and T_DATA_CONNECTED. The
  // seqNumber is only used in T_DATA_CONNECTED and 0 in case of T_DATA_GROUP
  // and T_DATA_GROUP (leaving out T_DATA_TAG_GROUP). Summary: effectively only
  // the "NUMBERED" flag (bit6) and the SeqNumber (bit5-2) are used from
  // transport layer.
  //          In T_DATA_* services the bits1-0 of TPCI octet are used as bits9-8
  //          for APCI type which is fixed to 0x03. SecureService APCI is
  //          0x03F1.

  // FIXME: when cEMI class is refactored, there might be additional info fields
  // in cEMI (fixed APDU_LPDU_DIFF) We are starting from TPCI octet (including):
  // plainApdu.frame().data()+APDU_LPDU_DIFF
  if (decrypt(plainApdu.frame().data() + APDU_LPDU_DIFF, plainApdu.length() + 1,
              srcAddress, dstAddress, isDstAddrGroupAddr, tpci,
              secureApdu.data() + 1, secCtrl, isSystemBroadcast)) {
    println("decodeSecureApdu: Plain APDU: ");
    plainApdu.frame().apdu().printPDU();

    return true;
  }

  return false;
}

bool SecureApplicationLayer::secure(uint8_t* buffer, uint16_t service,
                                    uint16_t srcAddr, uint16_t dstAddr,
                                    bool dstAddrIsGroupAddr, uint8_t tpci,
                                    uint8_t* apdu, uint16_t apduLength,
                                    const SecurityControl& secCtrl,
                                    bool systemBcast) {
  bool toolAccess = secCtrl.toolAccess;
  bool confidentiality = secCtrl.dataSecurity == DataSecurity::AuthConf;

  if (toolAccess) {
    if (!confidentiality) {
      println("Error: tool access requires auth+conf security");
      return false;
    }

    if (dstAddrIsGroupAddr && dstAddr != 0) {
      println("Error: tool access requires individual address");
      return false;
    }
  }

  const uint8_t* key = toolAccess ? _secIfObj.toolKey()
                                  : securityKey(dstAddr, dstAddrIsGroupAddr);

  if (key == nullptr) {
    print("Error: No key found. toolAccess: ");
    println(toolAccess ? "true" : "false");
    return false;
  }

  bool syncReq = service == kSecureSyncRequest;
  bool syncRes = service == kSecureSyncResponse;

  tpci |= SecureService >> 8;  // OR'ing upper two APCI bits
  uint8_t apci = SecureService & 0x00FF;
  uint8_t* pBuf = buffer;
  pBuf = pushByte(tpci, pBuf);  // TPCI
  pBuf = pushByte(apci, pBuf);  // APCI

  uint8_t scf;
  scf = service;
  scf |= toolAccess ? 0x80 : 0;
  scf |= confidentiality ? 0x10 : 0;
  scf |= systemBcast ? 0x8 : 0;

  pBuf = pushByte(scf, pBuf);  // SCF

  uint64_t seqSend = nextSequenceNumber(toolAccess);

  if (seqSend == 0) println("0 is not a valid sequence number");

  uint8_t seq[6];
  sixBytesFromUInt64(seqSend, seq);

  if (!syncRes) pBuf = pushByteArray(seq, 6, pBuf);  // Sequence Number

  // Prepare associated data depending on service (SyncRequest, SyncResponse or
  // just DataService)
  uint8_t associatedData[syncReq ? 7 : 1];
  associatedData[0] = scf;

  if (syncReq) {
    // TODO: NYI lookup serial number of target device for SBC sync.req
    uint8_t remoteSerialNo[6] = {0};

    uint8_t emptySerialNo[6] = {0};
    pBuf = pushByteArray(systemBcast ? remoteSerialNo : emptySerialNo, 6, pBuf);
    pushByteArray(_deviceObj.propertyData(PID_SERIAL_NUMBER), 6,
                  &associatedData[1]);
  } else if (syncRes) {
    // use random number in SyncResponse
    uint64_t randomNumber = getRandomNumber();
    sixBytesFromUInt64(randomNumber, seq);

    Addr remote =
        _syncReqBroadcastIncoming ? (Addr)GrpAddr(0) : (Addr)IndAddr(dstAddr);

    // Get challenge from sync.req
    uint64_t* challenge = _pendingIncomingSyncRequests.get(remote);

    if (challenge == nullptr) {
      println("Cannot send sync.res without corresponding sync.req");
      return false;
    } else {
      _pendingIncomingSyncRequests.erase(remote);
    }

    uint8_t challengeSixBytes[6];
    sixBytesFromUInt64(*challenge, challengeSixBytes);
    // printHex("Decrypted challenge: ", challengeSixBytes, 6);

    // Now XOR the new random SeqNum with the challenge from the SyncRequest
    uint8_t rndXorChallenge[6];
    pushByteArray(seq, 6, rndXorChallenge);

    for (uint8_t i = 0; i < sizeof(rndXorChallenge); i++) {
      rndXorChallenge[i] ^= challengeSixBytes[i];
    }

    pBuf = pushByteArray(rndXorChallenge, 6, pBuf);
  }

  // No LTE-HEE for now
  // Data Secure always uses extended frame format with APDU length > 15 octets
  // (long messages), no standard frames
  uint8_t extendedFrameFormat = 0;
  // Clear IV buffer
  uint8_t iv[16] = {0x00};
  // Create first block B0 for AES CBC MAC calculation, used as IV later
  /*
      printHex("seq: ", seq, 6);
      printHex("src: ", (uint8_t*) &srcAddr, 2);
      printHex("dst: ", (uint8_t*) &dstAddr, 2);
      print("dstAddrisGroup: ");println(dstAddrIsGroupAddr ? "true" : "false");
  */
  block0(iv, seq, srcAddr, dstAddr, dstAddrIsGroupAddr, extendedFrameFormat,
         tpci, apci, apduLength);

  // Clear block counter0 buffer
  uint8_t ctr0[16] = {0x00};
  // Create first block for block counter 0
  blockCtr0(ctr0, seq, srcAddr, dstAddr);

  if (confidentiality) {
    // Do calculations for Auth+Conf
    /*
            printHex("APDU--------->", apdu, apduLength);
            printHex("Key---------->", key, 16);
            printHex("ASSOC-------->", associatedData, sizeof(associatedData));
    */
    uint32_t mac = calcConfAuthMac(associatedData, sizeof(associatedData), apdu,
                                   apduLength, key, iv);

    uint8_t tmpBuffer[4 + apduLength];
    pushInt(mac, tmpBuffer);
    pushByteArray(apdu, apduLength, &tmpBuffer[4]);

    xcryptAesCtr(tmpBuffer, apduLength + 4, ctr0, key);  // APDU + MAC (4 bytes)

    pBuf = pushByteArray(tmpBuffer + 4, apduLength, pBuf);  // Encrypted APDU
    pBuf = pushByteArray(tmpBuffer + 0, 4, pBuf);           // Encrypted MAC

    // print("MAC(encrypted): ");
    // println(*((uint32_t*)(tmpBuffer + 0)),HEX);
  } else {
    // Do calculations for AuthOnly
    uint32_t tmpMac = calcAuthOnlyMac(apdu, apduLength, key, iv, ctr0);

    pBuf = pushByteArray(apdu, apduLength, pBuf);  // Plain APDU
    pBuf = pushInt(tmpMac, pBuf);                  // MAC

    print("MAC: ");
    println(tmpMac, HEX);
  }

  return true;
}

bool SecureApplicationLayer::createSecureApdu(APDU& plainApdu, APDU& secureApdu,
                                              const SecurityControl& secCtrl) {
  // Create secure APDU

  println("createSecureApdu: Plain APDU: ");
  plainApdu.printPDU();

  uint16_t srcAddress = plainApdu.frame().sourceAddress();
  uint16_t dstAddress = plainApdu.frame().destinationAddress();
  bool isDstAddrGroupAddr = plainApdu.frame().addressType() == GroupAddress;
  bool isSystemBroadcast = plainApdu.frame().systemBroadcast();
  uint8_t tpci = 0x00;

  if (isConnected()) {
    tpci |=
        0x40 | _transportLayer
                   ->getTpciSeqNum();  // get next TPCI sequence number for MAC
                                       // calculation from TL (T_DATA_CONNECTED)
  }

  print("createSecureApdu: TPCI: ");
  println(tpci, HEX);
  // Note:
  // The TPCI is also included in the MAC calculation to provide authenticity
  // for this field. However, a secure APDU (with a MAC) is only included in
  // transport layer PDUs T_DATA_GROUP, T_DATA_TAG_GROUP, T_DATA_INDIVIDUAL,
  // T_DATA_CONNECTED and not in T_CONNECT, T_DISCONNECT, T_ACK, T_NACK. This
  // means the DATA/CONTROL flag is always 0(=DATA). The flag "NUMBERED"
  // differentiates between T_DATA_INDIVIDUAL and T_DATA_CONNECTED. The
  // seqNumber is only used in T_DATA_CONNECTED and 0 in case of T_DATA_GROUP
  // and T_DATA_GROUP (leaving out T_DATA_TAG_GROUP). Summary: effectively only
  // the "NUMBERED" flag (bit6) and the SeqNumber (bit5-2) are used from
  // transport layer.
  //          In T_DATA_* services the bits1-0 of TPCI octet are used as bits9-8
  //          for APCI type which is fixed to 0x03. SecureService APCI is
  //          0x03F1.

  // FIXME: when cEMI class is refactored, there might be additional info fields
  // in cEMI (fixed APDU_LPDU_DIFF) We are starting from TPCI octet (including):
  // plainApdu.frame().data()+APDU_LPDU_DIFF
  if (secure(secureApdu.frame().data() + APDU_LPDU_DIFF, kSecureDataPdu,
             srcAddress, dstAddress, isDstAddrGroupAddr, tpci,
             plainApdu.frame().data() + APDU_LPDU_DIFF, plainApdu.length() + 1,
             secCtrl, isSystemBroadcast)) {
    print("Update our next ");
    print(secCtrl.toolAccess ? "tool access" : "");
    print(" seq from ");
    print(srcAddress, HEX);
    print(" -> (+1) ");
    println(nextSequenceNumber(secCtrl.toolAccess), HEX);

    uint64_t currentSeqNum = nextSequenceNumber(secCtrl.toolAccess);
    LOG_INF(">>> TX SeqNum: 0x%llX (%llu) [toolAccess=%d]", currentSeqNum,
            currentSeqNum, secCtrl.toolAccess);

    updateSequenceNumber(secCtrl.toolAccess,
                         nextSequenceNumber(secCtrl.toolAccess) + 1);

    println("createSecureApdu: Secure APDU: ");
    secureApdu.frame().apdu().printPDU();

    return true;
  }

  return false;
}

uint64_t SecureApplicationLayer::getRandomNumber() {
  return 0x000102030405;  // TODO: generate random number
}

void SecureApplicationLayer::loop() {
  // Proactive Jump on Boot logic
  if (!_sequenceNumberLoaded || !_sequenceNumberToolLoaded) {
    if (_bootTime == 0) {
      _bootTime = millis();
    }

    // Wait 3 seconds for memory restoration to finish
    if (millis() - _bootTime > 3000) {
      LOG_INF(">>> SEQ_DEBUG: STARTING PROACTIVE JUMP...");

      // Handle General Sequence Number
      if (!_sequenceNumberLoaded) {
        uint64_t saved = _secIfObj.getSequenceNumber(false);
        uint64_t isolated = _bauSystemB.platform().loadSequenceNumber(false);
        LOG_INF(">>> SEQ_DEBUG: Read SeqNum from Flash = %llu, Isolated = %llu",
                saved, isolated);

        // Survived ETS download? Use isolated if it's more up-to-date
        if (isolated > saved) {
          saved = isolated;
          LOG_INF(
              ">>> SEQ_DEBUG: Isolated NVS had more recent value. Using it.");
        }

        if (saved < 300000000000ULL && isolated < 300000000000ULL) {
          _sequenceNumber = 300000000000ULL;
          LOG_INF(
              ">>> SEQ_DEBUG: Both Flash and Isolated below 300B floor. "
              "Initializing floor.");
        } else {
          _sequenceNumber = (isolated > saved ? isolated : saved) + 2000;
          LOG_INF(
              ">>> SEQ_DEBUG: Jump +2000 from %llu (Isolated: %llu, Flash: "
              "%llu). "
              "New: %llu",
              (isolated > saved ? isolated : saved), isolated, saved,
              _sequenceNumber);
        }

        _secIfObj.setSequenceNumber(false, _sequenceNumber);
        _bauSystemB.platform().saveSequenceNumber(false, _sequenceNumber);
        _lastSavedSequenceNumber = _sequenceNumber;
        _lastSavedSequenceNumberStandard = _sequenceNumber;
        _sequenceNumberLoaded = true;
      }

      // Handle Tool Sequence Number
      if (!_sequenceNumberToolLoaded) {
        uint64_t saved = _secIfObj.getSequenceNumber(true);
        uint64_t isolated = _bauSystemB.platform().loadSequenceNumber(true);
        LOG_INF(
            ">>> SEQ_DEBUG: Read ToolSeqNum from Flash = %llu, Isolated = %llu",
            saved, isolated);

        if (isolated > saved) {
          saved = isolated;
          LOG_INF(
              ">>> SEQ_DEBUG: Isolated NVS had more recent Tool value. Using "
              "it.");
        }

        if (saved < 50) {  // Default for tool access
          _sequenceNumberToolAccess = 50;
        } else {
          _sequenceNumberToolAccess = saved + 2000;
        }

        _secIfObj.setSequenceNumber(true, _sequenceNumberToolAccess);
        _bauSystemB.platform().saveSequenceNumber(true,
                                                  _sequenceNumberToolAccess);
        _lastSavedSequenceNumberTool = _sequenceNumberToolAccess;
        _sequenceNumberToolLoaded = true;
      }

      // EXPLICIT COMMIT TO FLASH
      _bauSystemB.writeMemory();
      _bauSystemB.memory().saveMemory();
      LOG_INF(">>> SEQ_DEBUG: JUMP SUCCESSFUL & PERSISTED TO FLASH");
    }
  }

  // TODO: handle timeout of outgoing sync requests
  //_pendingOutgoingSyncRequests
}

bool SecureApplicationLayer::isSyncService(APDU& secureApdu) {
  uint8_t scf = *(secureApdu.data() + 1);
  uint8_t service =
      (scf & 0x07);  // only 0x0 (S-A_Data-PDU), 0x2 (S-A_Sync_Req-PDU) or 0x3
                     // (S-A_Sync_Rsp-PDU) are valid values

  if ((service == kSecureSyncRequest) || (service == kSecureSyncResponse)) {
    return true;
  }

  return false;
}
#endif
