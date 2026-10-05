#include "wire_protocol.h"
#include <string.h>
uint16_t Wire_Crc16(const uint8_t *data,uint16_t length)
{
  uint16_t crc=0xFFFFU;
  for(unsigned i=0;i<length;i++)
  {
    crc^=(uint16_t)data[i]<<8;
    for(unsigned bit=0;bit<8;bit++) {crc=(uint16_t)((crc<<1)^((crc&0x8000U)?0x1021U:0U));}
  }
  return crc;
}
uint16_t Wire_Encode(uint8_t *dst,uint8_t type,uint16_t sequence,const uint8_t *payload,uint16_t length)
{
  if(length>WIRE_MAX_PAYLOAD){return 0U;}
  uint8_t *p=dst; *p++=0xA5U;*p++=0x5AU;*p++=WIRE_VERSION;*p++=type;
  Wire_Put16(&p,length);Wire_Put16(&p,sequence);
  if(length){(void)memcpy(p,payload,length);p+=length;}
  Wire_Put16(&p,Wire_Crc16(dst+2,(uint16_t)(length+6U)));
  return (uint16_t)(p-dst);
}
static void Wire_Discard(WireParser_t *parser,unsigned count)
{
  parser->used=(uint16_t)(parser->used-count);
  (void)memmove(parser->bytes,parser->bytes+count,parser->used);
}
bool Wire_Feed(WireParser_t *parser,uint8_t value,uint32_t now,WirePacket_t *packet)
{
  if(parser->used && now-parser->last_byte_ms>200U){parser->used=0U;parser->bad_frames++;}
  parser->last_byte_ms=now;
  if(parser->used>=WIRE_MAX_FRAME){Wire_Discard(parser,1U);parser->bad_frames++;}
  parser->bytes[parser->used++]=value;
  for(;;)
  {
    if(!parser->used){return false;}
    if(parser->bytes[0]!=0xA5U){Wire_Discard(parser,1U);continue;}
    if(parser->used<2U){return false;}
    if(parser->bytes[1]!=0x5AU){Wire_Discard(parser,1U);continue;}
    if(parser->used<8U){return false;}
    uint16_t length=Wire_U16(parser->bytes+4);
    if(parser->bytes[2]!=WIRE_VERSION || length>WIRE_MAX_PAYLOAD)
    {Wire_Discard(parser,1U);parser->bad_frames++;continue;}
    if(parser->used<length+10U){return false;}
    if(Wire_Crc16(parser->bytes+2,(uint16_t)(length+6U))!=Wire_U16(parser->bytes+8U+length))
    {Wire_Discard(parser,1U);parser->bad_frames++;continue;}
    packet->type=parser->bytes[3];packet->sequence=Wire_U16(parser->bytes+6);
    packet->length=length;packet->payload=parser->bytes+8;
    /* Caller consumes payload before the next byte. No allocation or ISR parsing. */
    parser->used=0U;
    return true;
  }
}
