/*============================================================================
 * BlueXLogger - bxl_base64.h
 * Base64 and quoted-printable encoders used by the MIME layer.
 *
 * Created by Xencode-CLI by xanthorox
 *==========================================================================*/
#ifndef BXL_BASE64_H
#define BXL_BASE64_H

#include "bxl_common.h"
#include "bxl_util.h"

/* Standard base64 with CRLF line wrapping at 76 columns (RFC 2045).
 * When wrap is 0 the output is a single unbroken line. */
int bxl_base64_encode(const void *data, size_t len, int wrap, BxlBuf *out);

/* Convenience: allocate a NUL-terminated string. Caller frees. */
char *bxl_base64_encode_str(const void *data, size_t len);

/* Decode. Whitespace is ignored. Returns BXL_FALSE on malformed input. */
int bxl_base64_decode(const char *in, size_t in_len, BxlBuf *out);

/* Quoted-printable body encoding (RFC 2045 section 6.7).
 * Soft line breaks are inserted so no line exceeds 76 columns. */
int bxl_quoted_printable_encode(const void *data, size_t len, BxlBuf *out);

/* Encode a header value as an RFC 2047 encoded-word when it contains
 * non-ASCII or would otherwise be unsafe. ASCII-safe values pass through. */
int bxl_header_encode(const char *value, BxlBuf *out);

#endif /* BXL_BASE64_H */
