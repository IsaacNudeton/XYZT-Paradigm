/*
 * tests/test_gateway_propagation.c
 * Proves inter-cube routing on the GPU substrate without CPU interference.
 * 
 * Isaac Oravec & Claude, March 2026
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../engine.h"
#include "../wire.h"
#include "../substrate.cuh"

int main() {
    printf("=== PURE GPU GATEWAY PROPAGATION TEST ===\n\n");

    int n_cubes = VOL_X * VOL_Y * VOL_Z;
    CubeState *h_cubes = (CubeState*)calloc(n_cubes, sizeof(CubeState));

    if (!h_cubes) {
        printf("Failed to allocate host cubes.\n");
        return 1;
    }

    printf("1. Wiring physical substrate (local + gateways)...\n");
    /* CRITICAL: Establish the spatial voxel-to-voxel connections */
    wire_local_3d(h_cubes, n_cubes);
    wire_gateways(h_cubes, n_cubes);

    printf("2. Initializing GPU substrate...\n");
    if (substrate_init(n_cubes) != 0) {
        printf("GPU substrate init failed.\n");
        free(h_cubes);
        return 1;
    }

    /* Calculate exact positions for a clean +X axis ray.
     * Local coords: pos = x + y*CUBE_DIM + z*CUBE_DIM*CUBE_DIM
     */
    int pos_x0 = 0; /* Left face:  x=0, y=0, z=0 */
    int pos_x3 = 3; /* Right face: x=3, y=0, z=0 */

    /* Cube indices stepping along the +X axis */
    int c0 = 0; /* cx=0, cy=0, cz=0 */
    int c1 = 1; /* cx=1, cy=0, cz=0 */
    int c2 = 2; /* cx=2, cy=0, cz=0 */

    printf("3. Injecting massive impulse (255) at Cube 0 (x=3, y=0, z=0)...\n");
    h_cubes[c0].substrate[pos_x3] = 255;
    h_cubes[c0].active[pos_x3] = 1;

    printf("4. Uploading topology to GPU...\n");
    if (substrate_upload(h_cubes, n_cubes) != 0) {
        printf("GPU upload failed.\n");
        substrate_destroy();
        free(h_cubes);
        return 1;
    }

    /* Manually seed gateway lane for cube 0 +X face.
     * Our impulse is at lx=3 (the +X face of cube 0).
     * This should route to cube 1's -X face (lx=0).
     * Gateway lane[0] = +X direction, seed with impulse value (255).
     */
    printf("5. Seeding gateway lane (cube 0, +X direction, value=255)...\n");
    substrate_seed_gateway_lane(c0, 0, 255);

    printf("\nTracking wavefront along the +X axis:\n");
    printf("-----------------------------------------------------------------\n");
    printf(" Tick | C0(Out) Src | C1(In) Gate | C1(Out) Exit | C2(In) Deep \n");
    printf("-----------------------------------------------------------------\n");

    int steps = 200; /* Run longer to see full propagation curve */
    for (int i = 1; i <= steps; i++) {
        /* GPU Propagation Pipeline (No CPU interference) */
        substrate_route_step(n_cubes);
        substrate_inject_gateways(n_cubes);
        substrate_tick(n_cubes);

        /* Sample wave every 2 ticks to watch the propagation curve */
        if (i % 2 == 0) {
            substrate_download(h_cubes, n_cubes);
            
            int val_c0_out = h_cubes[c0].substrate[pos_x3];
            int val_c1_in  = h_cubes[c1].substrate[pos_x0]; 
            int val_c1_out = h_cubes[c1].substrate[pos_x3]; 
            int val_c2_in  = h_cubes[c2].substrate[pos_x0]; 

            printf(" %4d | %11d | %11d | %12d | %11d \n", 
                   i, val_c0_out, val_c1_in, val_c1_out, val_c2_in);
        }
    }

    printf("-----------------------------------------------------------------\n");

    substrate_destroy();
    free(h_cubes);
    return 0;
}
