#include "device_object.h"

#include <string.h>  // Thay <cstring> cho Zephyr compatibility

#include "bits.h"
#include "callback_property.h"
#include "config.h"
#include "data_property.h"

#define LEN_KNX_SERIAL 6
#define MAX_APDU_LENGTH_VALUE 55


DeviceObject::DeviceObject() {
  // Default to zeros (must be set via manufacturerId() and bauNumber() from
  // provisioning)
  static uint8_t serialNumber[LEN_KNX_SERIAL] = {0};

  static uint8_t hardwareTypeBuf[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  static uint8_t orderInfoBuf[10] = {'S', 'H', '0', '0', '1',
                                     ' ', ' ', ' ', ' ', ' '};

  Property* properties[] = {
      new DataProperty(PID_OBJECT_TYPE, false, PDT_UNSIGNED_INT, 1,
                       ReadLv3 | WriteLv0, (uint16_t)OT_DEVICE),
      new DataProperty(PID_SERIAL_NUMBER, false, PDT_GENERIC_06, 1,
                       ReadLv3 | WriteLv0, serialNumber),
      new CallbackProperty<DeviceObject>(
          this, PID_MANUFACTURER_ID, false, PDT_UNSIGNED_INT, 1,
          ReadLv3 | WriteLv0,
          [](DeviceObject* io, uint16_t start, uint8_t count,
             uint8_t* data) -> uint8_t {
            if (start == 0) {
              uint16_t currentNoOfElements = 1;
              pushWord(currentNoOfElements, data);
              return 1;
            }

            pushByteArray(io->propertyData(PID_SERIAL_NUMBER), 2, data);
            return 1;
          }),
      new DataProperty(PID_DEVICE_CONTROL, true, PDT_BITSET8, 1,
                       ReadLv3 | WriteLv3, (uint8_t)0),
      new DataProperty(PID_ORDER_INFO, false, PDT_GENERIC_10, 1,
                       ReadLv3 | WriteLv0, orderInfoBuf),
      new DataProperty(PID_VERSION, false, PDT_VERSION, 1, ReadLv3 | WriteLv0,
                       (uint16_t)4),
      new DataProperty(PID_ROUTING_COUNT, true, PDT_UNSIGNED_CHAR, 1,
                       ReadLv3 | WriteLv3, (uint8_t)(6 << 4)),
      new CallbackProperty<DeviceObject>(
          this, PID_PROG_MODE, true, PDT_BITSET8, 1, ReadLv3 | WriteLv3,
          [](DeviceObject* io, uint16_t start, uint8_t count,
             uint8_t* data) -> uint8_t {
            if (start == 0) {
              uint16_t currentNoOfElements = 1;
              pushWord(currentNoOfElements, data);
              return 1;
            }

            *data = io->_prgMode;
            return 1;
          },
          [](DeviceObject* io, uint16_t start, uint8_t count,
             const uint8_t* data) -> uint8_t {
            if (start == 0) return 1;

            io->_prgMode = *data;
            return 1;
          }),
      new DataProperty(PID_MAX_APDU_LENGTH, false, PDT_UNSIGNED_INT, 1,
                       ReadLv3 | WriteLv0, (uint16_t)MAX_APDU_LENGTH_VALUE),
      new CallbackProperty<DeviceObject>(
          this, PID_SUBNET_ADDR, false, PDT_UNSIGNED_CHAR, 1,
          ReadLv3 | WriteLv0,
          [](DeviceObject* io, uint16_t start, uint8_t count,
             uint8_t* data) -> uint8_t {
            if (start == 0) {
              uint16_t currentNoOfElements = 1;
              pushWord(currentNoOfElements, data);
              return 1;
            }

            *data = ((io->_ownAddress >> 8) & 0xff);

            return 1;
          }),
      new CallbackProperty<DeviceObject>(
          this, PID_DEVICE_ADDR, false, PDT_UNSIGNED_CHAR, 1,
          ReadLv3 | WriteLv0,
          [](DeviceObject* io, uint16_t start, uint8_t count,
             uint8_t* data) -> uint8_t {
            if (start == 0) {
              uint16_t currentNoOfElements = 1;
              pushWord(currentNoOfElements, data);
              return 1;
            }

            *data = (io->_ownAddress & 0xff);
            return 1;
          }),
      new DataProperty(PID_IO_LIST, false, PDT_UNSIGNED_INT, 8,
                       ReadLv3 | WriteLv0),
      new DataProperty(PID_HARDWARE_TYPE, true, PDT_GENERIC_06, 1,
                       ReadLv3 | WriteLv3, hardwareTypeBuf),
      new DataProperty(PID_DEVICE_DESCRIPTOR, false, PDT_GENERIC_02, 1,
                       ReadLv3 | WriteLv0),
#ifdef USE_RF
      new DataProperty(PID_RF_DOMAIN_ADDRESS_CEMI_SERVER, true, PDT_GENERIC_06,
                       1, ReadLv3 | WriteLv3),
#endif
  };
  initializeProperties(sizeof(properties), properties);
}

uint8_t* DeviceObject::save(uint8_t* buffer) {
  buffer = pushWord(_ownAddress, buffer);
  return InterfaceObject::save(buffer);
}

const uint8_t* DeviceObject::restore(const uint8_t* buffer) {
  buffer = popWord(_ownAddress, buffer);
  return InterfaceObject::restore(buffer);
}

uint16_t DeviceObject::saveSize() { return 2 + InterfaceObject::saveSize(); }

uint16_t DeviceObject::individualAddress() { return _ownAddress; }

// uint16_t DeviceObject::individualAddress() {
//   // HARDCODE IA
//   uint16_t hardcoded_ia = 0x1205;
//   return hardcoded_ia;
// }

void DeviceObject::individualAddress(uint16_t value) { _ownAddress = value; }

#define USER_STOPPED 0x1
#define OWN_ADDR_DUPL 0x2
#define VERIFY_MODE 0x4
#define SAFE_STATE 0x8

void DeviceObject::individualAddressDuplication(bool value) {
  Property* prop = property(PID_DEVICE_CONTROL);
  uint8_t data;
  prop->read(data);

  if (value)
    data |= OWN_ADDR_DUPL;
  else
    data &= ~OWN_ADDR_DUPL;

  prop->write(data);
}

bool DeviceObject::verifyMode() {
  Property* prop = property(PID_DEVICE_CONTROL);
  uint8_t data;
  prop->read(data);
  return (data & VERIFY_MODE) > 0;
}

void DeviceObject::verifyMode(bool value) {
  Property* prop = property(PID_DEVICE_CONTROL);
  uint8_t data;
  prop->read(data);

  if (value)
    data |= VERIFY_MODE;
  else
    data &= ~VERIFY_MODE;

  prop->write(data);
}

bool DeviceObject::progMode() { return _prgMode == 1; }

void DeviceObject::progMode(bool value) {
  if (value)
    _prgMode = 1;
  else
    _prgMode = 0;
}

uint16_t DeviceObject::manufacturerId() {
  uint16_t manufacturerId;
  popWord(manufacturerId, propertyData(PID_SERIAL_NUMBER));
  return manufacturerId;
}

void DeviceObject::manufacturerId(uint16_t value) {
  uint8_t data[LEN_KNX_SERIAL];
  memcpy(data, propertyData(PID_SERIAL_NUMBER), LEN_KNX_SERIAL);
  pushWord(value, data);
  propertyValue(PID_SERIAL_NUMBER, data);
}

uint32_t DeviceObject::bauNumber() {
  uint32_t bauNumber;
  popInt(bauNumber, propertyData(PID_SERIAL_NUMBER) + 2);
  return bauNumber;
}

void DeviceObject::bauNumber(uint32_t value) {
  uint8_t data[LEN_KNX_SERIAL];
  memcpy(data, propertyData(PID_SERIAL_NUMBER), LEN_KNX_SERIAL);
  pushInt(value, data + 2);
  propertyValue(PID_SERIAL_NUMBER, data);
}

const uint8_t* DeviceObject::orderNumber() {
  DataProperty* prop = (DataProperty*)property(PID_ORDER_INFO);
  return prop->data();
}

void DeviceObject::orderNumber(const uint8_t* value) {
  Property* prop = property(PID_ORDER_INFO);
  prop->write(value);
}

const uint8_t* DeviceObject::hardwareType() {
  DataProperty* prop = (DataProperty*)property(PID_HARDWARE_TYPE);
  return prop->data();
}

void DeviceObject::hardwareType(const uint8_t* value) {
  Property* prop = property(PID_HARDWARE_TYPE);
  prop->write(value);
}

uint16_t DeviceObject::version() {
  Property* prop = property(PID_VERSION);
  uint16_t value;
  prop->read(value);
  return value;
}

void DeviceObject::version(uint16_t value) {
  Property* prop = property(PID_VERSION);
  prop->write(value);
}

uint16_t DeviceObject::maskVersion() {
  Property* prop = property(PID_DEVICE_DESCRIPTOR);
  uint16_t value;
  prop->read(value);
  return value;
}

void DeviceObject::maskVersion(uint16_t value) {
  Property* prop = property(PID_DEVICE_DESCRIPTOR);
  prop->write(value);
}

uint16_t DeviceObject::maxApduLength() {
  Property* prop = property(PID_MAX_APDU_LENGTH);
  uint16_t value;
  prop->read(value);
  return value;
}

void DeviceObject::maxApduLength(uint16_t value) {
  Property* prop = property(PID_MAX_APDU_LENGTH);
  prop->write(value);
}

const uint8_t* DeviceObject::rfDomainAddress() {
  DataProperty* prop =
      (DataProperty*)property(PID_RF_DOMAIN_ADDRESS_CEMI_SERVER);
  return prop->data();
}

void DeviceObject::rfDomainAddress(uint8_t* value) {
  Property* prop = property(PID_RF_DOMAIN_ADDRESS_CEMI_SERVER);
  prop->write(value);
}

uint8_t DeviceObject::defaultHopCount() {
  Property* prop = property(PID_ROUTING_COUNT);
  uint8_t value;
  prop->read(value);
  return (value >> 4) & 0x07;
}
