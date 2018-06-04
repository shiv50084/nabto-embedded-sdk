
#include "nc_packet.h"

#include <platform/np_logging.h>

#include <string.h>

uint8_t* uint16_write_forward(uint8_t* buf, uint16_t val)
{
    uint8_t d0 = (uint8_t)(((val) >> 8) & 0xff);
    uint8_t d1 = (uint8_t)( (val)       & 0xff);
    *buf = d0;
    buf++;
    *buf = d1;
    buf++;
    return buf;    
}

void uint16_write(uint8_t* buf, uint16_t val)
{
    uint8_t d0 = (uint8_t)(((val) >> 8) & 0xff);
    uint8_t d1 = (uint8_t)( (val)       & 0xff);
    *buf = d0;
    buf++;
    *buf = d1;
}

uint16_t uint16_read(uint8_t* buf)
{
    uint16_t res = *buf << 8;
    return res + *(buf+1);
}

uint8_t* init_packet_header(uint8_t* buf, enum application_data_type ad)
{
    *buf = (uint8_t)ad;
    buf++;
    memset(buf, 0, 5);
    return buf+5;
}

uint8_t* insert_packet_extension(struct np_platform* pl, np_communication_buffer* buf, enum extension_type et, uint8_t* data, uint16_t dataLen)
{
    uint8_t* start = pl->buf.start(buf);
    uint16_t len = uint16_read(start+2);
    uint16_t extLen = uint16_read(start+4);
    uint8_t* ptr = start+6+len;
    ptr = uint16_write_forward(ptr, et);
    ptr = uint16_write_forward(ptr, dataLen);
    memcpy(ptr, data, dataLen);
    len = len + dataLen + 4;
    extLen = extLen + dataLen + 4;
    uint16_write(start+2, len);
    uint16_write(start+4, extLen);
    NABTO_LOG_TRACE(0, "len: %u, extLen: %u", len, extLen);
    return ptr+dataLen;
}

uint8_t* writeUint16LengthData(uint8_t* buf, uint8_t* data, uint16_t size)
{
    uint16_write(buf, size);
    memcpy(buf+2, data, size);
    return buf+2+size;
}