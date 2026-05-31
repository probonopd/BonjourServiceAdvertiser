#ifndef DNS_SD_H
#define DNS_SD_H

#include <stdint.h>

#ifndef DNSSD_API
#  ifdef _WIN32
#    define DNSSD_API __cdecl
#  else
#    define DNSSD_API
#  endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * Core types
 * ---------------------------------------------------------------------- */
typedef void *   DNSServiceRef;
typedef uint32_t DNSServiceFlags;
typedef int32_t  DNSServiceErrorType;

/* -------------------------------------------------------------------------
 * Error codes
 * ---------------------------------------------------------------------- */
enum {
    kDNSServiceErr_NoError  =      0,
    kDNSServiceErr_Unknown  = -65537
};

/* -------------------------------------------------------------------------
 * Interface index constants
 * ---------------------------------------------------------------------- */
enum {
    kDNSServiceInterfaceIndexAny = 0
};

/* -------------------------------------------------------------------------
 * Raw record handle (DNSServiceRegisterRecord)
 * ---------------------------------------------------------------------- */
typedef void *DNSRecordRef;

/* DNS record types needed for device-info registration */
enum {
    kDNSServiceType_PTR = 12,
    kDNSServiceType_TXT = 16,
    kDNSServiceType_SRV = 33
};

/* DNS record class */
enum {
    kDNSServiceClass_IN = 1
};

/* Flags for DNSServiceRegisterRecord */
enum {
    kDNSServiceFlagsShared = 0x10,  /* multiple registrants allowed */
    kDNSServiceFlagsUnique = 0x20   /* unique; conflict detection  */
};

/* -------------------------------------------------------------------------
 * DNSServiceRegisterReply – callback delivered when registration completes
 * ---------------------------------------------------------------------- */
typedef void (DNSSD_API *DNSServiceRegisterReply)(
    DNSServiceRef       sdRef,
    DNSServiceFlags     flags,
    DNSServiceErrorType errorCode,
    const char         *name,
    const char         *regtype,
    const char         *domain,
    void               *context
);

/* -------------------------------------------------------------------------
 * Function pointer typedefs for dynamic loading of dnssd.dll
 * ---------------------------------------------------------------------- */
typedef DNSServiceErrorType (DNSSD_API *DNSServiceRegister_f)(
    DNSServiceRef          *sdRef,
    DNSServiceFlags         flags,
    uint32_t                interfaceIndex,
    const char             *name,
    const char             *regtype,
    const char             *domain,
    const char             *host,
    uint16_t                port,
    uint16_t                txtLen,
    const void             *txtRecord,
    DNSServiceRegisterReply callBack,
    void                   *context
);

typedef void (DNSSD_API *DNSServiceRefDeallocate_f)(
    DNSServiceRef sdRef
);

typedef DNSServiceErrorType (DNSSD_API *DNSServiceProcessResult_f)(
    DNSServiceRef sdRef
);

/* -------------------------------------------------------------------------
 * TXTRecordRef – 16-byte opaque storage; never access members directly
 * ---------------------------------------------------------------------- */
typedef struct _TXTRecordRef_t {
    char PrivateData[16];
} TXTRecordRef;

/* -------------------------------------------------------------------------
 * Function pointer typedefs for TXT record helpers
 * ---------------------------------------------------------------------- */
typedef void (DNSSD_API *TXTRecordCreate_f)(
    TXTRecordRef *txtRecord,
    uint16_t      bufferLen,
    void         *buffer
);

typedef DNSServiceErrorType (DNSSD_API *TXTRecordSetValue_f)(
    TXTRecordRef *txtRecord,
    const char   *key,
    uint8_t       valueSize,
    const void   *value
);

typedef uint16_t (DNSSD_API *TXTRecordGetLength_f)(
    const TXTRecordRef *txtRecord
);

typedef const void *(DNSSD_API *TXTRecordGetBytesPtr_f)(
    const TXTRecordRef *txtRecord
);

typedef void (DNSSD_API *TXTRecordDeallocate_f)(
    TXTRecordRef *txtRecord
);

/* -------------------------------------------------------------------------
 * Raw record registration API
 * ---------------------------------------------------------------------- */
typedef void (DNSSD_API *DNSServiceRegisterRecordReply)(
    DNSServiceRef       sdRef,
    DNSRecordRef        RecordRef,
    DNSServiceFlags     flags,
    DNSServiceErrorType errorCode,
    void               *context
);

typedef DNSServiceErrorType (DNSSD_API *DNSServiceCreateConnection_f)(
    DNSServiceRef *sdRef
);

typedef DNSServiceErrorType (DNSSD_API *DNSServiceRegisterRecord_f)(
    DNSServiceRef                 sdRef,
    DNSRecordRef                 *RecordRef,
    DNSServiceFlags               flags,
    uint32_t                      interfaceIndex,
    const char                   *fullname,
    uint16_t                      rrtype,
    uint16_t                      rrclass,
    uint16_t                      rdlen,
    const void                   *rdata,
    uint32_t                      ttl,
    DNSServiceRegisterRecordReply callBack,
    void                         *context
);

typedef DNSServiceErrorType (DNSSD_API *DNSServiceRemoveRecord_f)(
    DNSServiceRef   sdRef,
    DNSRecordRef    RecordRef,
    DNSServiceFlags flags
);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DNS_SD_H */
