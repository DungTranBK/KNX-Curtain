#include "application_program_object.h"

#include <string.h>  // Thay <cstring> cho Zephyr compatibility

#include "bits.h"
#include "callback_property.h"
#include "data_property.h"
#include "dptconvert.h"

ApplicationProgramObject::ApplicationProgramObject(Memory& memory)
#if MASK_VERSION == 0x091A
    : TableObject(memory, 0x0100, 0x0100)
#else
    : TableObject(memory)
#endif
{
  // M-0085_A-008A-20 -> AppNumber: 138 (0x8A), AppVersion: 32 (0x20)
  const uint8_t appProgId[3] = {0x00, 0x8A, 0x20};

  Property* properties[] = {
      new DataProperty(PID_SERIAL_NUMBER, false, PDT_GENERIC_06, 1,
                       ReadLv3 | WriteLv0),
      new DataProperty(PID_MANUFACTURER_ID, false, PDT_UNSIGNED_INT, 1,
                       ReadLv3 | WriteLv0),
      new DataProperty(PID_ORDER_INFO, false, PDT_CHAR_BLOCK, 1,
                       ReadLv3 | WriteLv0),
      new DataProperty(PID_FIRMWARE_REVISION, false, PDT_GENERIC_02, 1,
                       ReadLv3 | WriteLv0),
      new DataProperty(PID_VERSION, false, PDT_UNSIGNED_INT, 1,
                       ReadLv3 | WriteLv0),
      new DataProperty(PID_DEVICE_CONTROL, false, PDT_GENERIC_01, 1,
                       ReadLv3 | WriteLv0),
      new DataProperty(PID_OBJECT_TYPE, false, PDT_UNSIGNED_INT, 1,
                       ReadLv3 | WriteLv0, (uint16_t)OT_APPLICATION_PROG),
      new DataProperty(PID_PROG_VERSION, true, PDT_GENERIC_05, 1,
                       ReadLv3 | WriteLv3),
      new DataProperty((PropertyID)66, false, PDT_GENERIC_03, 1,
                       ReadLv3 | WriteLv2, appProgId),
      new CallbackProperty<ApplicationProgramObject>(
          this, PID_PEI_TYPE, false, PDT_UNSIGNED_CHAR, 1, ReadLv3 | WriteLv0,
          [](ApplicationProgramObject* io, uint16_t start, uint8_t count,
             uint8_t* data) -> uint8_t {
            if (start == 0) {
              uint16_t currentNoOfElements = 1;
              pushWord(currentNoOfElements, data);
              return 1;
            }

            data[0] = 0;
            return 1;
          })};

  TableObject::initializeProperties(sizeof(properties), properties);
}

uint8_t* ApplicationProgramObject::save(uint8_t* buffer) {
  uint8_t programVersion[5];
  property(PID_PROG_VERSION)->read(programVersion);
  buffer = pushByteArray(programVersion, 5, buffer);

  return TableObject::save(buffer);
}

const uint8_t* ApplicationProgramObject::restore(const uint8_t* buffer) {
  uint8_t programVersion[5];
  buffer = popByteArray(programVersion, 5, buffer);
  property(PID_PROG_VERSION)->write(programVersion);

  return TableObject::restore(buffer);
}

uint16_t ApplicationProgramObject::saveSize() {
  return TableObject::saveSize() + 5;  // sizeof(programVersion)
}

uint8_t* ApplicationProgramObject::data(uint32_t addr) {
  return TableObject::data() + addr;
}

uint8_t ApplicationProgramObject::getByte(uint32_t addr) {
  return *(TableObject::data() + addr);
}

uint16_t ApplicationProgramObject::getWord(uint32_t addr) {
  return ::getWord(TableObject::data() + addr);
}

uint32_t ApplicationProgramObject::getInt(uint32_t addr) {
  return ::getInt(TableObject::data() + addr);
}

double ApplicationProgramObject::getFloat(uint32_t addr,
                                          ParameterFloatEncodings encoding) {
  switch (encoding) {
    case Float_Enc_DPT9:
      return float16FromPayload(TableObject::data() + addr, 0);
      break;

    case Float_Enc_IEEE754Single:
      return float32FromPayload(TableObject::data() + addr, 0);
      break;

    case Float_Enc_IEEE754Double:
      return float64FromPayload(TableObject::data() + addr, 0);
      break;

    default:
      return 0;
      break;
  }
}
