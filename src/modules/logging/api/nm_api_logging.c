#include "nm_api_logging.h"
#include <stdio.h>

NabtoDeviceLogCallback logCallback;
void* userData;

void nm_api_log_default_log_callback(NabtoDeviceLogMessage* msg, void* data)
{

}

void nm_api_log(uint32_t severity, uint32_t module,
                uint32_t line, const char* file,
                const char* fmt, va_list args);
void nm_api_log_buf(uint32_t severity, uint32_t module,
                    uint32_t line, const char* file,
                    const uint8_t* buf, size_t len);

void np_log_init()
{
    np_log.log = &nm_api_log;
    np_log.log_buf = &nm_api_log_buf;
    logCallback = &nm_api_log_default_log_callback;
    userData = NULL;
}

void nm_api_log(uint32_t severity, uint32_t module,
                uint32_t line, const char* file,
                const char* fmt, va_list args)
{
    if ((NABTO_LOG_MODULE_FILTER & module) || module == 0 ) {
        NabtoDeviceLogMessage msg;
        char log[128];
        int ret;

        ret = vsnprintf(log, 128, fmt, args);
        if (ret >= 128) {
            // TODO: handle too long log lines
            // The log line was too large for the array
        }

        switch(severity) {
            case NABTO_LOG_SEVERITY_FATAL:
                msg.severity = NABTO_DEVICE_LOG_FATAL;
                break;
            case NABTO_LOG_SEVERITY_ERROR:
                msg.severity = NABTO_DEVICE_LOG_ERROR;
                break;
            case NABTO_LOG_SEVERITY_WARN:
                msg.severity = NABTO_DEVICE_LOG_WARN;
                break;
            case NABTO_LOG_SEVERITY_INFO:
                msg.severity = NABTO_DEVICE_LOG_INFO;
                break;
            case NABTO_LOG_SEVERITY_TRACE:
                msg.severity = NABTO_DEVICE_LOG_TRACE;
                break;
            default:
                msg.severity = NABTO_DEVICE_LOG_ERROR;
                break;
        }

        msg.file = file;
        msg.line = line;
        msg.message = log;
        logCallback(&msg, userData);
    }
}

void nm_api_log_buf_line(uint32_t severity, uint32_t module,
                uint32_t line, const char* file,
                const char* fmt)
{
    if ((NABTO_LOG_MODULE_FILTER & module) || module == 0 ) {
        NabtoDeviceLogMessage msg;
        char log[128];
        int ret;

        ret = snprintf(log, 128, fmt);
        if (ret >= 128) {
            // TODO: handle too long log lines
            // The log line was too large for the array
        }

        switch(severity) {
            case NABTO_LOG_SEVERITY_FATAL:
                msg.severity = NABTO_DEVICE_LOG_FATAL;
                break;
            case NABTO_LOG_SEVERITY_ERROR:
                msg.severity = NABTO_DEVICE_LOG_ERROR;
                break;
            case NABTO_LOG_SEVERITY_WARN:
                msg.severity = NABTO_DEVICE_LOG_WARN;
                break;
            case NABTO_LOG_SEVERITY_INFO:
                msg.severity = NABTO_DEVICE_LOG_INFO;
                break;
            case NABTO_LOG_SEVERITY_TRACE:
                msg.severity = NABTO_DEVICE_LOG_TRACE;
                break;
            default:
                msg.severity = NABTO_DEVICE_LOG_ERROR;
                break;
        }

        msg.file = file;
        msg.line = line;
        msg.message = log;
        logCallback(&msg, userData);
    }
}
void nm_api_log_buf(uint32_t severity, uint32_t module,
                    uint32_t line, const char* file,
                    const uint8_t* buf, size_t len)
{
    char str[128];
    char* ptr;
    size_t chunks = len/16;
    size_t i, n;
    int ret = 0;
    va_list list = {};
    
    for (i = 0; i < chunks; i++) {
        ret = sprintf(str, "%04lx: ", i*16);
        ptr = str + ret;
        for (n = 0; n < 16; n++) {
            ret = sprintf(ptr, "%02x ", buf[i*16+n]);
            ptr = ptr + ret;
        }
        ret = sprintf(ptr, ": ");
        ptr = ptr + ret;
        
        for (n = 0; n < 16; n++) {
            if(buf[i*16+n] > 0x1F && buf[i*16+n] < 0x7F && buf[i*16+n] != 0x25) {
                ret = sprintf(ptr, "%c", (char)buf[i*16+n]);
                ptr = ptr + ret;
            } else {
                ret = sprintf(ptr, ".");
                ptr = ptr + ret;
            }
        }
        nm_api_log(severity, module, line, file, str, list);
    }
    ret = sprintf(str, "%04lx: ", chunks*16);
    ptr = str + ret;
    for (n = chunks*16; n < len; n++) {
        ret = sprintf(ptr, "%02x ", buf[n]);
        ptr = ptr + ret;
    }
    for (; n < chunks*16+16; n++) {
        ret = sprintf(ptr, "   ");
        ptr = ptr + ret;
    }
    ret = sprintf(ptr, ": ");
    ptr = ptr + ret;
        
    for (n = chunks*16; n < len; n++) {
        if(buf[n] > 0x1F && buf[n] < 0x7F) {
            ret = sprintf(ptr, "%c", (char)buf[n]);
            ptr = ptr + ret;
        } else {
            ret = sprintf(ptr, ".");
            ptr = ptr + ret;
        }
    }
    nm_api_log(severity, module, line, file, str, list);

}

void nm_api_logging_set_callback(NabtoDeviceLogCallback cb, void* data)
{
    userData = data;
    logCallback = cb;
}
