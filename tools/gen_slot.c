/*============================================================================
 * BlueXLogger - tools/gen_slot.c
 * Emits the placeholder BXL_CFG resource slot that is compiled into the
 * payload template. The builder later overwrites this slot in-place.
 *
 * Created by Xencode-CLI by xanthorox
 *==========================================================================*/
#include <stdio.h>
#include <string.h>

#define SLOT_SIZE 8192

int main(int argc, char **argv)
{
    unsigned char buf[SLOT_SIZE];
    FILE *f;

    if (argc < 2) {
        fprintf(stderr, "usage: gen_slot <output.bin>\n");
        return 1;
    }

    memset(buf, 0, sizeof(buf));

    /* Placeholder magic: "BXCFG0" + two NULs. The payload treats this as
     * "not configured" and falls back to a side-by-side config file. */
    memcpy(buf, "BXCFG0\0\0", 8);

    f = fopen(argv[1], "wb");
    if (!f) {
        fprintf(stderr, "gen_slot: cannot open %s\n", argv[1]);
        return 1;
    }

    if (fwrite(buf, 1, sizeof(buf), f) != sizeof(buf)) {
        fclose(f);
        fprintf(stderr, "gen_slot: short write\n");
        return 1;
    }

    fclose(f);
    printf("gen_slot: wrote %s (%d bytes, placeholder)\n", argv[1], SLOT_SIZE);
    return 0;
}
