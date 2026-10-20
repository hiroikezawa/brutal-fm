#include "bfm_host.h"
int main(void)
{
    static int16_t buf[2 * 44100 * 2];
    uint32_t i;
    host_init();
    printf("sizeof patch %zu sound %zu pattern %zu\n", sizeof(bfm_patch_t), sizeof(bfm_sound_t), sizeof(bfm_pattern_t));
    for (i = 0; i < BFM_NFACTORY; i++) {
        int32_t pk;
        host_load(i);
        host_render(0, 64);
        bfm_key_push(48, 100, 1);
        pk = host_render(buf, 44100);
        bfm_key_push(48, 0, 0);
        pk = host_render(buf + 2 * 44100, 44100) > pk ? pk : pk;
        printf("%2u %-12s peak %6d voices %u\n", i, BFM_FACTORY[i].name, pk, bfm_voices_on());
    }
    return 0;
}
