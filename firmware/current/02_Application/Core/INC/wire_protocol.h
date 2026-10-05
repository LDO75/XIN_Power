#ifndef XIN_WIRE_PROTOCOL_H
#define XIN_WIRE_PROTOCOL_H
#include <stdint.h>
#include <stdbool.h>
#define WIRE_VERSION 5U
#define WIRE_MAX_PAYLOAD 448U
#define WIRE_MAX_FRAME (WIRE_MAX_PAYLOAD+10U)
enum { WIRE_COMMAND=1, WIRE_ACK=2, WIRE_ERROR=3, WIRE_INFO=4,
       WIRE_TELEMETRY=5, WIRE_FAULT=6, WIRE_CURVE=7, WIRE_POINT=8,
       WIRE_CAL_STATE=9, WIRE_CALIBRATION=10, WIRE_TEXT=11 };
enum { CMD_HELLO=1,CMD_STATUS,CMD_SET_V,CMD_SET_I,CMD_OUT,CMD_CLEAR,
       CMD_CAL_GET,CMD_CAL_RAW,CMD_CAL_V,CMD_CAL_I,CMD_CAL_SAVE,CMD_CAL_RESET,
       CMD_CURVE_GET,CMD_POINT_GET,CMD_POINT_START,CMD_POINT_STEP,
       CMD_POINT_CAPTURE,CMD_POINT_STOP,CMD_POINT_STATE,CMD_LOG,CMD_RATE,CMD_DIAG,
       CMD_HELP,CMD_PING,CMD_FW_STATUS=30,CMD_FW_BEGIN,CMD_FW_DATA,CMD_FW_END,
       CMD_FW_APPLY,CMD_FW_ABORT,CMD_REBOOT };
typedef struct { uint8_t bytes[WIRE_MAX_FRAME]; uint16_t used; uint32_t last_byte_ms, bad_frames; } WireParser_t;
typedef struct { uint8_t type; uint16_t sequence,length; const uint8_t *payload; } WirePacket_t;
uint16_t Wire_Crc16(const uint8_t *data,uint16_t length);
uint16_t Wire_Encode(uint8_t *dst,uint8_t type,uint16_t sequence,const uint8_t *payload,uint16_t length);
bool Wire_Feed(WireParser_t *parser,uint8_t value,uint32_t now,WirePacket_t *packet);
static inline uint16_t Wire_U16(const uint8_t *p){return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static inline uint32_t Wire_U32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static inline void Wire_Put16(uint8_t **p,uint16_t v){*(*p)++=(uint8_t)v;*(*p)++=(uint8_t)(v>>8);}
static inline void Wire_Put32(uint8_t **p,uint32_t v){Wire_Put16(p,(uint16_t)v);Wire_Put16(p,(uint16_t)(v>>16));}
#endif
