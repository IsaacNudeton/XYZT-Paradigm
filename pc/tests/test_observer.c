/*
 * test_observer.c — Observer depth stack Z0-Z4, integration test
 */
#include "test.h"

void run_observer_tests(void) {
    /* Observer Depth Stack (Z0-Z4) */
    printf("--- Observer Depth Z0-Z4 ---\n");
    {
        check("Z0 obs_bool(+42)=1", 1, obs_bool(42));
        check("Z0 obs_bool(-7)=0", 0, obs_bool(-7));
        check("Z0 obs_bool(0)=0", 0, obs_bool(0));

        check("Z1 and(50,50,30)=1", 1, obs_and(50, 50, 30));
        check("Z1 and(50,10,30)=0", 0, obs_and(50, 10, 30));
        check("Z1 and(10,50,30)=0", 0, obs_and(10, 50, 30));
        check("Z1 and(-50,50,30)=1", 1, obs_and(-50, 50, 30));

        check("Z3 freq(3,50)=1 resonant", 1, obs_freq(3, 50));
        check("Z3 freq(1,50)=0 relay", 0, obs_freq(1, 50));
        check("Z3 freq(0,50)=0 dead", 0, obs_freq(0, 50));
        check("Z3 freq(2,0)=0 no mass", 0, obs_freq(2, 0));

        check("Z4 corr(10,5,1)=1 both up", 1, obs_corr(10, 5, 1));
        check("Z4 corr(3,8,0)=1 both down", 1, obs_corr(3, 8, 0));
        check("Z4 corr(10,5,0)=0 diverge", 0, obs_corr(10, 5, 0));
        check("Z4 corr(3,8,1)=0 diverge", 0, obs_corr(3, 8, 1));
    }

    /* Observer Depth Integration: 5 views of same collision */
    printf("--- Observer Depth Integration ---\n");
    {
        Engine eng; engine_init(&eng);
        Graph *g0 = &eng.shells[0].g;
        int sa = graph_add(g0, "zdep_a", 0, &eng.T);
        int sb = graph_add(g0, "zdep_b", 0, &eng.T);
        int sd = graph_add(g0, "zdep_d", 0, &eng.T);
        for (int n = sa; n <= sd; n++) {
            g0->nodes[n].layer_zero = 0;
            g0->nodes[n].identity.len = 64;
            memset(g0->nodes[n].identity.w, 0xFF, 8);
        }
        g0->nodes[sa].val = 80; g0->nodes[sb].val = -80;
        g0->nodes[sd].valence = 50;
        int32_t prev_val = g0->nodes[sd].val;
        int sc = graph_add(g0, "zdep_c", 0, &eng.T);
        g0->nodes[sc].layer_zero = 0; g0->nodes[sc].identity.len = 64;
        memset(g0->nodes[sc].identity.w, 0xFF, 8);
        g0->nodes[sc].val = -80;
        graph_wire(g0, sa, sa, sd, 255, 0);
        graph_wire(g0, sc, sc, sd, 255, 0);
        for (int t = 0; t < 25; t++) engine_tick(&eng);
        Node *d = &g0->nodes[sd];

        int z0 = obs_bool(d->val);
        int z1 = obs_and(d->val, d->I_energy, 30);
        int z2 = obs_xor(d->I_energy, d->val);
        int z3 = obs_freq(d->n_incoming, d->valence);
        int z4 = obs_corr(d->val, prev_val, d->I_energy);

        check("zdep: I_energy > 0", 1, d->I_energy > 0 ? 1 : 0);
        check("zdep: Z0 returns 0 or 1", 1, (z0 == 0 || z0 == 1) ? 1 : 0);
        check("zdep: Z2 XOR fires", 1, z2);
        check("zdep: valence grew", 1, d->valence > 50 ? 1 : 0);
        (void)z1; (void)z3; (void)z4;
        engine_destroy(&eng);
    }
}
